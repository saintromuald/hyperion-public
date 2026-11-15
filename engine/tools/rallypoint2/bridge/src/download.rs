//! Bounded HTTPS downloads for native runtime installers.
//!
//! The caller supplies the expected byte count and streaming callbacks. Redirects never leave
//! HTTPS, responses are not buffered whole, and every network wait has a deadline.

use std::ffi::{c_char, c_void, CStr};
use std::future::Future;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;
use std::time::Duration;

use bytes::Bytes;
use http_body_util::{BodyExt, Full};
use hyper::body::Incoming;
use hyper::header::{CONTENT_LENGTH, LOCATION};
use hyper::{Request, StatusCode, Uri};
use hyper_rustls::{HttpsConnector, HttpsConnectorBuilder};
use hyper_util::client::legacy::{connect::HttpConnector, Client};
use hyper_util::rt::TokioExecutor;

pub const RP_DOWNLOAD_OK: i32 = 0;
pub const RP_DOWNLOAD_ERR_SETUP: i32 = -300;
pub const RP_DOWNLOAD_ERR_NETWORK: i32 = -301;
pub const RP_DOWNLOAD_ERR_TIMEOUT: i32 = -302;
pub const RP_DOWNLOAD_ERR_STATUS: i32 = -303;
pub const RP_DOWNLOAD_ERR_REDIRECT: i32 = -304;
pub const RP_DOWNLOAD_ERR_SIZE: i32 = -305;
pub const RP_DOWNLOAD_ERR_CANCELLED: i32 = -306;
pub const RP_DOWNLOAD_ERR_WRITE: i32 = -307;
pub const RP_DOWNLOAD_ERR_PANIC: i32 = -308;

const CONNECT_TIMEOUT: Duration = Duration::from_secs(20);
const HEADER_TIMEOUT: Duration = Duration::from_secs(30);
const IDLE_TIMEOUT: Duration = Duration::from_secs(45);
const CANCEL_POLL: Duration = Duration::from_millis(50);
const TOTAL_TIMEOUT: Duration = Duration::from_secs(30 * 60);
const MAX_REDIRECTS: usize = 5;
const MAX_DOWNLOAD_BYTES: u64 = 4u64 << 30;

type HttpClient = Client<HttpsConnector<HttpConnector>, Full<Bytes>>;
type WriteCallback = unsafe extern "C" fn(*mut c_void, *const u8, usize) -> bool;
type ProgressCallback = unsafe extern "C" fn(*mut c_void, u64, u64);
type CancelCallback = unsafe extern "C" fn(*mut c_void) -> bool;

#[derive(Clone, Copy)]
struct Callbacks {
    context: *mut c_void,
    write: WriteCallback,
    progress: Option<ProgressCallback>,
    cancel: Option<CancelCallback>,
}

// The pointers are borrowed only while rp_https_download runs on its own calling thread.
unsafe impl Send for Callbacks {}

impl Callbacks {
    fn cancelled(&self) -> bool {
        self.cancel
            .is_some_and(|cancel| unsafe { cancel(self.context) })
    }

    fn progress(&self, received: u64, total: u64) {
        if let Some(progress) = self.progress {
            unsafe { progress(self.context, received, total) }
        }
    }
}

#[derive(Debug)]
struct DownloadError {
    code: i32,
    message: String,
}

impl DownloadError {
    fn new(code: i32, message: impl Into<String>) -> Self {
        Self {
            code,
            message: message.into(),
        }
    }
}

type DownloadResult<T> = Result<T, DownloadError>;

fn valid_https_uri(uri: &Uri) -> bool {
    uri.scheme_str() == Some("https")
        && uri.host().is_some_and(|host| !host.is_empty())
        && uri.authority().is_some_and(|authority| {
            !authority.as_str().contains('@') && !authority.as_str().is_empty()
        })
        && uri
            .query()
            .is_none_or(|query| !query.contains('\r') && !query.contains('\n'))
}

fn origin(uri: &Uri) -> DownloadResult<String> {
    let scheme = uri
        .scheme_str()
        .ok_or_else(|| DownloadError::new(RP_DOWNLOAD_ERR_REDIRECT, "redirect has no scheme"))?;
    let authority = uri
        .authority()
        .ok_or_else(|| DownloadError::new(RP_DOWNLOAD_ERR_REDIRECT, "redirect has no authority"))?;
    Ok(format!("{scheme}://{authority}"))
}

/// Resolves the redirect forms an HTTP server may put in Location. Credentials and scheme
/// downgrades are rejected by valid_https_uri after resolution.
fn redirect_uri(base: &Uri, location: &str) -> DownloadResult<Uri> {
    if location.is_empty() || location.bytes().any(|b| b <= 0x20 || b == 0x7f) {
        return Err(DownloadError::new(
            RP_DOWNLOAD_ERR_REDIRECT,
            "redirect location is empty or contains control characters",
        ));
    }
    let resolved = if location.starts_with("https://") || location.starts_with("http://") {
        location.to_string()
    } else if location.starts_with("//") {
        format!("https:{location}")
    } else if location.starts_with('/') {
        format!("{}{}", origin(base)?, location)
    } else {
        let path = base.path();
        let directory = path
            .rsplit_once('/')
            .map_or("/", |(directory, _)| directory);
        format!("{}{}/{}", origin(base)?, directory, location)
    };
    let uri = resolved.parse::<Uri>().map_err(|_| {
        DownloadError::new(
            RP_DOWNLOAD_ERR_REDIRECT,
            "redirect location is not a valid URI",
        )
    })?;
    if !valid_https_uri(&uri) {
        return Err(DownloadError::new(
            RP_DOWNLOAD_ERR_REDIRECT,
            "redirect left HTTPS or contained credentials",
        ));
    }
    Ok(uri)
}

fn response_size(response: &hyper::Response<Incoming>, expected: u64) -> DownloadResult<()> {
    let mut lengths = response.headers().get_all(CONTENT_LENGTH).iter();
    let Some(first) = lengths.next() else {
        return Ok(());
    };
    let first = first
        .to_str()
        .map_err(|_| DownloadError::new(RP_DOWNLOAD_ERR_SIZE, "Content-Length is not valid"))?;
    if lengths.any(|_| true) {
        return Err(DownloadError::new(
            RP_DOWNLOAD_ERR_SIZE,
            "response has multiple Content-Length fields",
        ));
    }
    let actual = first.trim().parse::<u64>().map_err(|_| {
        DownloadError::new(RP_DOWNLOAD_ERR_SIZE, "Content-Length is not a byte count")
    })?;
    if actual != expected {
        return Err(DownloadError::new(
            RP_DOWNLOAD_ERR_SIZE,
            format!("server announced {actual} bytes; expected {expected}"),
        ));
    }
    Ok(())
}

fn chunk_size(received: u64, chunk: usize, expected: u64) -> DownloadResult<u64> {
    let next = received.checked_add(chunk as u64).ok_or_else(|| {
        DownloadError::new(RP_DOWNLOAD_ERR_SIZE, "download byte count overflowed")
    })?;
    if next > expected {
        return Err(DownloadError::new(
            RP_DOWNLOAD_ERR_SIZE,
            "download exceeded its expected size",
        ));
    }
    Ok(next)
}

fn finish_size(received: u64, expected: u64) -> DownloadResult<()> {
    if received == expected {
        Ok(())
    } else {
        Err(DownloadError::new(
            RP_DOWNLOAD_ERR_SIZE,
            format!("download ended at {received} bytes; expected {expected}"),
        ))
    }
}

fn require_ok(status: StatusCode) -> DownloadResult<()> {
    if status == StatusCode::OK {
        Ok(())
    } else {
        Err(DownloadError::new(
            RP_DOWNLOAD_ERR_STATUS,
            format!("official download returned HTTP {}", status.as_u16()),
        ))
    }
}

async fn wait_with_cancel<F: Future>(
    future: F,
    callbacks: Callbacks,
    timeout: Duration,
    timeout_message: &'static str,
) -> DownloadResult<F::Output> {
    tokio::select! {
        result = tokio::time::timeout(timeout, future) => {
            result.map_err(|_| DownloadError::new(RP_DOWNLOAD_ERR_TIMEOUT, timeout_message))
        }
        _ = async {
            loop {
                if callbacks.cancelled() {
                    break;
                }
                tokio::time::sleep(CANCEL_POLL).await;
            }
        } => Err(DownloadError::new(RP_DOWNLOAD_ERR_CANCELLED, "download cancelled")),
    }
}

async fn request(
    client: &HttpClient,
    uri: &Uri,
    callbacks: Callbacks,
) -> DownloadResult<hyper::Response<Incoming>> {
    let request = Request::builder()
        .uri(uri)
        .header("User-Agent", "Hyperion-Pluto-Installer/1")
        .header("Accept", "application/octet-stream")
        .body(Full::new(Bytes::new()))
        .map_err(|_| {
            DownloadError::new(RP_DOWNLOAD_ERR_SETUP, "could not construct HTTPS request")
        })?;
    match wait_with_cancel(
        client.request(request),
        callbacks,
        HEADER_TIMEOUT,
        "HTTPS response headers timed out",
    )
    .await
    {
        Ok(Ok(response)) => Ok(response),
        Ok(Err(error)) => Err(DownloadError::new(
            RP_DOWNLOAD_ERR_NETWORK,
            format!("HTTPS request failed: {error}"),
        )),
        Err(error) => Err(error),
    }
}

async fn download(
    client: &HttpClient,
    initial: Uri,
    expected: u64,
    callbacks: Callbacks,
) -> DownloadResult<()> {
    let mut uri = initial;
    for redirects in 0..=MAX_REDIRECTS {
        if callbacks.cancelled() {
            return Err(DownloadError::new(
                RP_DOWNLOAD_ERR_CANCELLED,
                "download cancelled",
            ));
        }
        let response = request(client, &uri, callbacks).await?;
        if response.status().is_redirection() {
            if redirects == MAX_REDIRECTS {
                return Err(DownloadError::new(
                    RP_DOWNLOAD_ERR_REDIRECT,
                    "too many HTTPS redirects",
                ));
            }
            let location = response.headers().get(LOCATION).ok_or_else(|| {
                DownloadError::new(RP_DOWNLOAD_ERR_REDIRECT, "redirect had no Location field")
            })?;
            let location = location.to_str().map_err(|_| {
                DownloadError::new(RP_DOWNLOAD_ERR_REDIRECT, "redirect Location was not text")
            })?;
            uri = redirect_uri(&uri, location)?;
            continue;
        }
        require_ok(response.status())?;
        response_size(&response, expected)?;
        let mut body = response.into_body();
        let mut received = 0u64;
        callbacks.progress(0, expected);
        loop {
            if callbacks.cancelled() {
                return Err(DownloadError::new(
                    RP_DOWNLOAD_ERR_CANCELLED,
                    "download cancelled",
                ));
            }
            let frame = match wait_with_cancel(
                body.frame(),
                callbacks,
                IDLE_TIMEOUT,
                "HTTPS download stalled",
            )
            .await
            {
                Ok(Some(Ok(frame))) => frame,
                Ok(Some(Err(error))) => {
                    return Err(DownloadError::new(
                        RP_DOWNLOAD_ERR_NETWORK,
                        format!("HTTPS stream failed: {error}"),
                    ))
                }
                Ok(None) => break,
                Err(error) => return Err(error),
            };
            let Some(data) = frame.data_ref() else {
                continue;
            };
            if data.is_empty() {
                continue;
            }
            received = chunk_size(received, data.len(), expected)?;
            let written =
                unsafe { (callbacks.write)(callbacks.context, data.as_ptr(), data.len()) };
            if !written {
                if callbacks.cancelled() {
                    return Err(DownloadError::new(
                        RP_DOWNLOAD_ERR_CANCELLED,
                        "download cancelled",
                    ));
                }
                return Err(DownloadError::new(
                    RP_DOWNLOAD_ERR_WRITE,
                    "could not write the downloaded archive",
                ));
            }
            callbacks.progress(received, expected);
        }
        return finish_size(received, expected);
    }
    unreachable!("the redirect loop returns at its bound")
}

fn copy_error(error: &mut [u8], message: &str) {
    if error.is_empty() {
        return;
    }
    let bytes = message.as_bytes();
    let count = bytes.len().min(error.len() - 1);
    unsafe {
        ptr::copy_nonoverlapping(bytes.as_ptr(), error.as_mut_ptr(), count);
        *error.get_unchecked_mut(count) = 0;
    }
}

/// Streams exactly `expected_size` bytes from an HTTPS URL to `write`.
///
/// Redirects (at most five) must remain HTTPS and cannot contain userinfo. Connect, header,
/// idle-read, and total deadlines bound the operation. `cancel` interrupts in-flight header/body
/// waits within a bounded poll interval; returning false from `write` aborts the transfer.
///
/// # Safety
/// `url` and `error` must be valid C strings / writable buffers as described by their lengths.
/// `context` must remain valid for every callback until this synchronous call returns. Callback
/// pointers must remain callable and may not unwind into Rust.
#[no_mangle]
pub unsafe extern "C" fn rp_https_download(
    url: *const c_char,
    expected_size: u64,
    context: *mut c_void,
    write: Option<WriteCallback>,
    progress: Option<ProgressCallback>,
    cancel: Option<CancelCallback>,
    error: *mut c_char,
    error_cap: usize,
) -> i32 {
    let result = catch_unwind(AssertUnwindSafe(|| {
        if error_cap > 0 && !error.is_null() {
            *error = 0;
        }
        let run = || -> DownloadResult<()> {
            if url.is_null() || expected_size == 0 || expected_size > MAX_DOWNLOAD_BYTES {
                return Err(DownloadError::new(
                    RP_DOWNLOAD_ERR_SETUP,
                    "invalid URL or download size",
                ));
            }
            let Some(write) = write else {
                return Err(DownloadError::new(
                    RP_DOWNLOAD_ERR_SETUP,
                    "missing download write callback",
                ));
            };
            let url = CStr::from_ptr(url).to_str().map_err(|_| {
                DownloadError::new(RP_DOWNLOAD_ERR_SETUP, "download URL is not UTF-8")
            })?;
            let uri = url.parse::<Uri>().map_err(|_| {
                DownloadError::new(RP_DOWNLOAD_ERR_SETUP, "download URL is invalid")
            })?;
            if !valid_https_uri(&uri) {
                return Err(DownloadError::new(
                    RP_DOWNLOAD_ERR_SETUP,
                    "only an HTTPS URL without credentials is accepted",
                ));
            }
            let callbacks = Callbacks {
                context,
                write,
                progress,
                cancel,
            };
            if callbacks.cancelled() {
                return Err(DownloadError::new(
                    RP_DOWNLOAD_ERR_CANCELLED,
                    "download cancelled",
                ));
            }
            let runtime = tokio::runtime::Builder::new_current_thread()
                .enable_all()
                .build()
                .map_err(|e| {
                    DownloadError::new(
                        RP_DOWNLOAD_ERR_NETWORK,
                        format!("could not start HTTPS runtime: {e}"),
                    )
                })?;
            runtime.block_on(async {
                let mut http = HttpConnector::new();
                http.set_connect_timeout(Some(CONNECT_TIMEOUT));
                let https = HttpsConnectorBuilder::new()
                    .with_webpki_roots()
                    .https_only()
                    .enable_http1()
                    .build();
                let client: HttpClient = Client::builder(TokioExecutor::new()).build(https);
                match tokio::time::timeout(
                    TOTAL_TIMEOUT,
                    download(&client, uri, expected_size, callbacks),
                )
                .await
                {
                    Ok(result) => result,
                    Err(_) => Err(DownloadError::new(
                        RP_DOWNLOAD_ERR_TIMEOUT,
                        "HTTPS download exceeded its total time limit",
                    )),
                }
            })
        };
        match run() {
            Ok(()) => RP_DOWNLOAD_OK,
            Err(failure) => {
                if error_cap > 0 && !error.is_null() {
                    let buffer = std::slice::from_raw_parts_mut(error as *mut u8, error_cap);
                    copy_error(buffer, &failure.message);
                }
                failure.code
            }
        }
    }));
    match result {
        Ok(code) => code,
        Err(_) => {
            if error_cap > 0 && !error.is_null() {
                let buffer = unsafe { std::slice::from_raw_parts_mut(error as *mut u8, error_cap) };
                copy_error(buffer, "HTTPS downloader panicked");
            }
            RP_DOWNLOAD_ERR_PANIC
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn uri(text: &str) -> Uri {
        text.parse().unwrap()
    }

    #[test]
    fn only_credential_free_https_urls_are_accepted() {
        assert!(valid_https_uri(&uri(
            "https://github.com/tscmoo/pluto/releases/download/v2.0/pluto-v2.0.zip"
        )));
        for rejected in [
            "http://github.com/pluto.zip",
            "https://user@example.com/pluto.zip",
            "https:///pluto.zip",
            "file:///tmp/pluto.zip",
        ] {
            if let Ok(parsed) = rejected.parse::<Uri>() {
                assert!(!valid_https_uri(&parsed), "{rejected}");
            }
        }
    }

    #[test]
    fn redirects_resolve_without_permitting_a_downgrade() {
        let base = uri("https://github.com/tscmoo/pluto/releases/download/v2.0/pluto-v2.0.zip");
        assert_eq!(
            redirect_uri(
                &base,
                "https://release-assets.githubusercontent.com/pluto.zip"
            )
            .unwrap()
            .to_string(),
            "https://release-assets.githubusercontent.com/pluto.zip"
        );
        assert_eq!(
            redirect_uri(&base, "/download/pluto.zip")
                .unwrap()
                .to_string(),
            "https://github.com/download/pluto.zip"
        );
        assert_eq!(
            redirect_uri(&base, "pluto-2.zip").unwrap().to_string(),
            "https://github.com/tscmoo/pluto/releases/download/v2.0/pluto-2.zip"
        );
        assert_eq!(
            redirect_uri(&base, "//release-assets.githubusercontent.com/pluto.zip")
                .unwrap()
                .to_string(),
            "https://release-assets.githubusercontent.com/pluto.zip"
        );
        assert_eq!(
            redirect_uri(&base, "http://github.com/pluto.zip")
                .unwrap_err()
                .code,
            RP_DOWNLOAD_ERR_REDIRECT
        );
        assert_eq!(
            redirect_uri(&base, "https://user@github.com/pluto.zip")
                .unwrap_err()
                .code,
            RP_DOWNLOAD_ERR_REDIRECT
        );
        assert!(redirect_uri(&base, "/bad path").is_err());
    }

    #[test]
    fn exact_size_checks_reject_short_and_long_bodies() {
        assert_eq!(chunk_size(10, 5, 15).unwrap(), 15);
        assert_eq!(
            chunk_size(10, 6, 15).unwrap_err().code,
            RP_DOWNLOAD_ERR_SIZE
        );
        assert_eq!(
            chunk_size(u64::MAX, 1, u64::MAX).unwrap_err().code,
            RP_DOWNLOAD_ERR_SIZE
        );
        assert!(finish_size(15, 15).is_ok());
        assert_eq!(finish_size(14, 15).unwrap_err().code, RP_DOWNLOAD_ERR_SIZE);
    }

    #[test]
    fn non_success_http_status_is_an_error() {
        assert!(require_ok(StatusCode::OK).is_ok());
        let failure = require_ok(StatusCode::NOT_FOUND).unwrap_err();
        assert_eq!(failure.code, RP_DOWNLOAD_ERR_STATUS);
        assert!(failure.message.contains("404"));
    }

    unsafe extern "C" fn cancel_now(_: *mut c_void) -> bool {
        true
    }

    unsafe extern "C" fn write_never(_: *mut c_void, _: *const u8, _: usize) -> bool {
        panic!("a cancelled transfer must not write")
    }

    #[test]
    fn cancellation_callback_is_pollable() {
        let callbacks = Callbacks {
            context: ptr::null_mut(),
            write: write_never,
            progress: None,
            cancel: Some(cancel_now),
        };
        assert!(callbacks.cancelled());
    }

    unsafe extern "C" fn cancel_atomic(context: *mut c_void) -> bool {
        let flag = unsafe { &*(context as *const std::sync::atomic::AtomicBool) };
        flag.load(std::sync::atomic::Ordering::SeqCst)
    }

    #[tokio::test]
    async fn cancellation_interrupts_an_in_flight_wait() {
        use std::sync::atomic::{AtomicBool, Ordering};
        use std::sync::Arc;

        let flag = Arc::new(AtomicBool::new(false));
        let setter_flag = Arc::clone(&flag);
        let setter = std::thread::spawn(move || {
            std::thread::sleep(Duration::from_millis(25));
            setter_flag.store(true, Ordering::SeqCst);
        });
        let callbacks = Callbacks {
            context: Arc::as_ptr(&flag) as *mut c_void,
            write: write_never,
            progress: None,
            cancel: Some(cancel_atomic),
        };
        let started = std::time::Instant::now();
        let result = wait_with_cancel(
            std::future::pending::<()>(),
            callbacks,
            Duration::from_secs(30),
            "the in-flight test wait timed out",
        )
        .await;
        setter.join().unwrap();
        assert_eq!(result.unwrap_err().code, RP_DOWNLOAD_ERR_CANCELLED);
        assert!(
            started.elapsed() < Duration::from_secs(2),
            "cancellation did not interrupt the pending wait promptly"
        );
    }

    #[test]
    fn error_copy_is_always_terminated() {
        let mut small = [b'x'; 5];
        copy_error(&mut small, "more than four");
        assert_eq!(&small, b"more\0");
        let mut none = [];
        copy_error(&mut none, "ignored");
    }
}
