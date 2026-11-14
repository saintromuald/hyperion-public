//! ShieldBattery-mediated relay failover for canonical native sessions.
//!
//! The result credential remains in the child's protected environment and is never logged. Launch
//! JSON contributes only the public game/user identity and the home-relay trust material.

use std::net::{IpAddr, SocketAddr};
use std::sync::Arc;
use std::time::Duration;

use base64::prelude::{Engine as _, BASE64_STANDARD};
use bytes::Bytes;
use http_body_util::{BodyExt, Full, Limited};
use hyper::body::Incoming;
use hyper::{Request, StatusCode, Uri};
use hyper_rustls::{HttpsConnector, HttpsConnectorBuilder};
use hyper_util::client::legacy::{connect::HttpConnector, Client};
use hyper_util::rt::TokioExecutor;
use rally_point_client::transport::rustls::pki_types::CertificateDer;
use rally_point_client::transport::rustls::RootCertStore;
use rally_point_client::{ClientEndpoint, RehomeFuture, RehomeOutcome, RehomeProvider};
use serde::Deserialize;

use crate::setup::{HomeRelay, HyperionIdentity};

// Leave margin beyond the server's 10-second coordinator deadline for its response work.
const REQUEST_TIMEOUT: Duration = Duration::from_secs(15);
const MAX_RESPONSE_BYTES: usize = 64 * 1024;

type HttpClient = Client<HttpsConnector<HttpConnector>, Full<Bytes>>;

fn server_url_is_allowed(uri: &Uri) -> bool {
    match uri.scheme_str() {
        Some("https") => true,
        Some("http") => uri.host().is_some_and(|host| {
            host.eq_ignore_ascii_case("localhost")
                || host
                    .trim_start_matches('[')
                    .trim_end_matches(']')
                    .parse::<IpAddr>()
                    .is_ok_and(|address| address.is_loopback())
        }),
        _ => false,
    }
}

#[derive(Clone)]
pub struct RehomeConfig {
    server_url: String,
    game_id: String,
    user_id: u32,
    result_code: String,
}

impl RehomeConfig {
    pub fn from_environment(identity: Option<&HyperionIdentity>) -> Option<Self> {
        let identity = identity?;
        let server_url = std::env::var("HYPERION_SB_SERVER_URL").ok()?;
        let result_code = std::env::var("HYPERION_SB_RESULT_CODE").ok()?;
        if server_url.trim().is_empty() || result_code.is_empty() {
            return None;
        }
        let uri = format!(
            "{}/api/1/games/{}/netcode-rehome",
            server_url.trim_end_matches('/'),
            identity.game_id
        )
        .parse::<Uri>()
        .ok()?;
        if !server_url_is_allowed(&uri) {
            return None;
        }
        if uri.authority().is_none_or(|authority| {
            authority.as_str().contains('@') || authority.as_str().is_empty()
        }) || uri.query().is_some()
        {
            return None;
        }
        Some(Self {
            server_url,
            game_id: identity.game_id.clone(),
            user_id: identity.local_user_id,
            result_code,
        })
    }

    pub fn into_provider(self, home_connected: SocketAddr) -> Arc<dyn RehomeProvider> {
        let https = HttpsConnectorBuilder::new()
            .with_webpki_roots()
            .https_or_http()
            .enable_http1()
            .build();
        let client = Client::builder(TokioExecutor::new()).build(https);
        Arc::new(ServerRehome {
            url: format!(
                "{}/api/1/games/{}/netcode-rehome",
                self.server_url.trim_end_matches('/'),
                self.game_id
            ),
            user_id: self.user_id,
            result_code: self.result_code,
            home_connected,
            client,
        })
    }
}

struct ServerRehome {
    url: String,
    user_id: u32,
    result_code: String,
    home_connected: SocketAddr,
    client: HttpClient,
}

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct RehomeRequest<'a> {
    user_id: u32,
    result_code: &'a str,
    dead_relay_id: u64,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
struct RehomeResponse {
    decision: String,
    #[serde(default)]
    relay: Option<HomeRelay>,
}

#[derive(Debug)]
enum Decision {
    Stay,
    Unavailable,
    Departed,
    NewTarget(HomeRelay),
}

fn decision_from_response(response: RehomeResponse) -> Decision {
    match response.decision.as_str() {
        "stay" => Decision::Stay,
        "newTarget" => match response.relay {
            Some(relay) => Decision::NewTarget(relay),
            None => Decision::Unavailable,
        },
        _ => Decision::Unavailable,
    }
}

fn decision_from_refusal(status: StatusCode, body: &[u8]) -> Decision {
    if status == StatusCode::CONFLICT {
        if let Ok(value) = serde_json::from_slice::<serde_json::Value>(body) {
            if value.get("code").and_then(|code| code.as_str()) == Some("AlreadyReported") {
                return Decision::Departed;
            }
        }
    }
    Decision::Unavailable
}

async fn read_response(response: hyper::Response<Incoming>) -> Result<(StatusCode, Bytes), ()> {
    let status = response.status();
    let body = Limited::new(response.into_body(), MAX_RESPONSE_BYTES)
        .collect()
        .await
        .map_err(|_| ())?
        .to_bytes();
    Ok((status, body))
}

impl ServerRehome {
    async fn do_rehome(&self, dead_relay_id: u64) -> RehomeOutcome {
        let uri = match self.url.parse::<Uri>() {
            Ok(uri) => uri,
            Err(_) => return RehomeOutcome::Unavailable,
        };
        let body = match serde_json::to_vec(&RehomeRequest {
            user_id: self.user_id,
            result_code: &self.result_code,
            dead_relay_id,
        }) {
            Ok(body) => body,
            Err(_) => return RehomeOutcome::Unavailable,
        };
        let request = match Request::post(uri)
            .header(hyper::header::CONTENT_TYPE, "application/json")
            .header(hyper::header::ORIGIN, "shieldbattery://game")
            .body(Full::new(Bytes::from(body)))
        {
            Ok(request) => request,
            Err(_) => return RehomeOutcome::Unavailable,
        };
        let decision =
            match tokio::time::timeout(REQUEST_TIMEOUT, self.client.request(request)).await {
                Ok(Ok(response)) => {
                    match tokio::time::timeout(REQUEST_TIMEOUT, read_response(response)).await {
                        Ok(Ok((status, body))) if status.is_success() => {
                            match serde_json::from_slice::<RehomeResponse>(&body) {
                                Ok(response) => decision_from_response(response),
                                Err(_) => Decision::Unavailable,
                            }
                        }
                        Ok(Ok((status, body))) => decision_from_refusal(status, &body),
                        _ => Decision::Unavailable,
                    }
                }
                _ => Decision::Unavailable,
            };

        match decision {
            Decision::Stay => RehomeOutcome::Stay,
            Decision::Unavailable => RehomeOutcome::Unavailable,
            Decision::Departed => RehomeOutcome::Departed,
            Decision::NewTarget(relay) => {
                let (endpoint, target) = match endpoint_for_relay(&relay) {
                    Ok(value) => value,
                    Err(_) => return RehomeOutcome::Unavailable,
                };
                let Some((relay_addr, fallback_addrs)) =
                    order_dial_addrs(&target.addrs, self.home_connected)
                else {
                    return RehomeOutcome::Unavailable;
                };
                RehomeOutcome::NewTarget {
                    relay_id: relay.relay_id,
                    endpoint,
                    relay_addr,
                    fallback_addrs,
                    server_name: target.server_name,
                }
            }
        }
    }
}

impl RehomeProvider for ServerRehome {
    fn rehome(&self, dead_relay_id: u64) -> RehomeFuture<'_> {
        Box::pin(async move { self.do_rehome(dead_relay_id).await })
    }
}

fn endpoint_for_relay(relay: &HomeRelay) -> Result<(ClientEndpoint, RelayTarget), String> {
    let cert = BASE64_STANDARD
        .decode(&relay.cert)
        .map_err(|_| "invalid relay certificate".to_string())?;
    let mut roots = RootCertStore::empty();
    roots
        .add(CertificateDer::from(cert))
        .map_err(|error| error.to_string())?;
    let addrs = relay
        .addrs
        .iter()
        .map(|addr| addr.parse::<SocketAddr>())
        .collect::<Result<Vec<_>, _>>()
        .map_err(|error| error.to_string())?;
    if addrs.is_empty() {
        return Err("replacement relay has no dial address".to_string());
    }
    let endpoint = ClientEndpoint::bind(roots).map_err(|error| error.to_string())?;
    Ok((
        endpoint,
        RelayTarget {
            addrs,
            server_name: relay.server_name.clone(),
        },
    ))
}

struct RelayTarget {
    addrs: Vec<SocketAddr>,
    server_name: String,
}

fn order_dial_addrs(
    addrs: &[SocketAddr],
    home_connected: SocketAddr,
) -> Option<(SocketAddr, Vec<SocketAddr>)> {
    let same_family = |addr: &SocketAddr| addr.is_ipv6() == home_connected.is_ipv6();
    let lead = addrs
        .iter()
        .position(|addr| same_family(addr) && addr.port() == home_connected.port())
        .or_else(|| addrs.iter().position(same_family))
        .or((!addrs.is_empty()).then_some(0))?;
    let fallback = addrs
        .iter()
        .enumerate()
        .filter(|(index, _)| *index != lead)
        .map(|(_, addr)| *addr)
        .collect();
    Some((addrs[lead], fallback))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn server_urls_require_tls_except_for_literal_loopback_development() {
        for allowed in [
            "https://shieldbattery.invalid",
            "http://127.0.0.1:5555",
            "http://localhost:5555",
            "http://[::1]:5555",
        ] {
            assert!(
                server_url_is_allowed(&allowed.parse().unwrap()),
                "{allowed}"
            );
        }
        for rejected in [
            "http://shieldbattery.invalid",
            "http://192.168.1.20:5555",
            "http://localhost.example:5555",
            "ftp://127.0.0.1",
        ] {
            assert!(
                !server_url_is_allowed(&rejected.parse().unwrap()),
                "{rejected}"
            );
        }
    }

    #[test]
    fn response_decisions_fail_closed() {
        assert!(matches!(
            decision_from_response(serde_json::from_str(r#"{"decision":"stay"}"#).unwrap()),
            Decision::Stay
        ));
        assert!(matches!(
            decision_from_response(serde_json::from_str(r#"{"decision":"newTarget"}"#).unwrap()),
            Decision::Unavailable
        ));
        assert!(matches!(
            decision_from_response(serde_json::from_str(r#"{"decision":"unknown"}"#).unwrap()),
            Decision::Unavailable
        ));
    }

    #[test]
    fn only_an_authenticated_departure_conflict_ends_reconnecting() {
        assert!(matches!(
            decision_from_refusal(StatusCode::CONFLICT, br#"{"code":"AlreadyReported"}"#),
            Decision::Departed
        ));
        assert!(matches!(
            decision_from_refusal(StatusCode::CONFLICT, br#"{"code":"NotLoaded"}"#),
            Decision::Unavailable
        ));
        assert!(matches!(
            decision_from_refusal(StatusCode::INTERNAL_SERVER_ERROR, b"offline"),
            Decision::Unavailable
        ));
    }

    #[test]
    fn dial_order_prefers_the_connected_family_and_port() {
        let home: SocketAddr = "127.0.0.1:15000".parse().unwrap();
        let v6: SocketAddr = "[::1]:15000".parse().unwrap();
        let other: SocketAddr = "127.0.0.1:15001".parse().unwrap();
        let (lead, fallback) = order_dial_addrs(&[v6, other], home).unwrap();
        assert_eq!(lead, other);
        assert_eq!(fallback, vec![v6]);
    }

    #[tokio::test]
    async fn provider_posts_authenticated_rehome_and_builds_the_replacement() {
        use tokio::io::{AsyncReadExt, AsyncWriteExt};

        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let http_addr = listener.local_addr().unwrap();
        let relay_addr: SocketAddr = "127.0.0.1:15001".parse().unwrap();
        let cert = rcgen::generate_simple_self_signed(["localhost".to_string()])
            .unwrap()
            .cert
            .der()
            .to_vec();
        let cert_b64 = BASE64_STANDARD.encode(&cert);
        let server = tokio::spawn(async move {
            let (mut stream, _) = listener.accept().await.unwrap();
            let mut request = vec![0; 16 * 1024];
            let read = stream.read(&mut request).await.unwrap();
            let request = String::from_utf8_lossy(&request[..read]).to_string();
            let response = format!(
                "{{\"decision\":\"newTarget\",\"relay\":{{\"relayId\":2,\"addrs\":[\"{relay_addr}\"],\"serverName\":\"localhost\",\"cert\":\"{cert_b64}\"}}}}"
            );
            let reply = format!(
                "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
                response.len(),
                response
            );
            stream.write_all(reply.as_bytes()).await.unwrap();
            stream.shutdown().await.unwrap();
            request
        });
        let provider = RehomeConfig {
            server_url: format!("http://{http_addr}"),
            game_id: "game-id".to_string(),
            user_id: 42,
            result_code: "secret-result-code".to_string(),
        }
        .into_provider("127.0.0.1:15000".parse().unwrap());
        let outcome = provider.rehome(9).await;
        assert!(matches!(
            outcome,
            RehomeOutcome::NewTarget {
                relay_id: 2,
                relay_addr: actual_addr,
                ..
            } if actual_addr == relay_addr
        ));
        let request = server.await.unwrap();
        assert!(request.starts_with("POST /api/1/games/game-id/netcode-rehome"));
        assert!(request
            .to_ascii_lowercase()
            .contains("\r\norigin: shieldbattery://game\r\n"));
        assert!(request.contains("\"userId\":42"));
        assert!(request.contains("\"deadRelayId\":9"));
        assert!(request.contains("\"resultCode\":\"secret-result-code\""));
    }
}
