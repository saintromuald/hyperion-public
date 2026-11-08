// port_http.cpp -- libcurl behind a bounded worker-only request API. Redirects
// are followed here rather than by curl so an Authorization header can never
// cross an origin or an HTTPS-to-HTTP boundary accidentally.
#include "../../h/port_http.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>

#include "../../h/port_replaykraken.h"

#if !defined(__EMSCRIPTEN__)
#include <curl/curl.h>
#endif

namespace PortHttp {
namespace {

std::string Lower(std::string s)
{
  for (char &c : s)
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  return s;
}

std::string Trim(const std::string &s)
{
  const size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos)
    return "";
  const size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

bool RequestEndpoint(const std::string &url, ReplayKraken::Endpoint *pOut, std::string *pError)
{
  if (url.find('#') != std::string::npos) {
    if (pError)
      *pError = "Request URLs cannot contain a fragment.";
    return false;
  }
  // Base-server settings reject queries; a concrete request may carry one.
  return ReplayKraken::EndpointParse(url.substr(0, url.find('?')), pOut, pError);
}

std::string ResolveLocation(const std::string &current, const std::string &location)
{
  if (location.find("http://") == 0 || location.find("https://") == 0)
    return location;
  const size_t scheme = current.find("://");
  if (scheme == std::string::npos)
    return "";
  if (location.compare(0, 2, "//") == 0)
    return current.substr(0, scheme + 1) + location;
  const size_t path = current.find('/', scheme + 3);
  const std::string origin = current.substr(0, path);
  if (!location.empty() && location[0] == '/')
    return origin + location;
  const size_t query = current.find_first_of("?#", path);
  const size_t slash = current.rfind('/', query);
  if (slash == std::string::npos || slash < scheme + 3)
    return origin + "/" + location;
  return current.substr(0, slash + 1) + location;
}

}  // namespace

std::string Response::Header(const std::string &lowerName) const
{
  const auto at = headers.find(lowerName);
  return at == headers.end() ? "" : at->second;
}

#if defined(__EMSCRIPTEN__)

bool Available() { return false; }
std::string Version() { return "native HTTPS unavailable in the browser build"; }
Response Send(const Request &)
{
  Response response;
  response.error = "Online replay services are unavailable in the browser build.";
  return response;
}

#else

namespace {

bool CurlReady()
{
  static std::once_flag once;
  static bool ready = false;
  std::call_once(once, [] { ready = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK; });
  return ready;
}

struct Transfer {
  const Request *request;
  Response *response;
  size_t headerBytes = 0;
};

size_t BodyCallback(char *data, size_t size, size_t count, void *opaque)
{
  Transfer &transfer = *static_cast<Transfer *>(opaque);
  if (transfer.request->pCancel && transfer.request->pCancel->load(std::memory_order_relaxed)) {
    transfer.response->bCancelled = true;
    return 0;
  }
  if (size != 0 && count > std::numeric_limits<size_t>::max() / size)
    return 0;
  const size_t bytes = size * count;
  if (bytes > transfer.request->maxBytes - std::min(transfer.request->maxBytes, transfer.response->bytes.size())) {
    transfer.response->bTooLarge = true;
    return 0;
  }
  const unsigned char *at = reinterpret_cast<const unsigned char *>(data);
  transfer.response->bytes.insert(transfer.response->bytes.end(), at, at + bytes);
  return bytes;
}

size_t HeaderCallback(char *data, size_t size, size_t count, void *opaque)
{
  Transfer &transfer = *static_cast<Transfer *>(opaque);
  const size_t bytes = size * count;
  transfer.headerBytes += bytes;
  if (transfer.headerBytes > 64 * 1024)
    return 0;
  std::string line(data, bytes);
  const size_t colon = line.find(':');
  if (colon == std::string::npos)
    return bytes;
  const std::string name = Lower(Trim(line.substr(0, colon)));
  const std::string value = Trim(line.substr(colon + 1));
  if (name.empty())
    return bytes;
  auto &old = transfer.response->headers[name];
  if (!old.empty() && !value.empty())
    old += ", ";
  old += value;
  return bytes;
}

int ProgressCallback(void *opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
  Transfer &transfer = *static_cast<Transfer *>(opaque);
  if (transfer.request->pCancel && transfer.request->pCancel->load(std::memory_order_relaxed)) {
    transfer.response->bCancelled = true;
    return 1;
  }
  return 0;
}

Response Failure(Response response, const std::string &error)
{
  response.status = 0;
  response.error = error;
  response.bytes.clear();
  return response;
}

}  // namespace

bool Available() { return CurlReady(); }
std::string Version() { return CurlReady() ? curl_version() : "libcurl initialization failed"; }

Response Send(const Request &request)
{
  Response response;
  if (!CurlReady())
    return Failure(response, "libcurl could not be initialized.");
  if (request.maxBytes == 0 || request.maxBytes > 64 * 1024 * 1024 || request.body.size() > 4 * 1024 * 1024)
    return Failure(response, "The HTTP request bounds are invalid.");
  std::string url = request.url;
  std::string method = request.method.empty() ? "GET" : request.method;
  std::string body = request.body;
  std::string bearer = request.bearer;
  for (unsigned redirect = 0;; ++redirect) {
    ReplayKraken::Endpoint endpoint;
    std::string endpointError;
    if (!RequestEndpoint(url, &endpoint, &endpointError))
      return Failure(response, endpointError);
    if (endpoint.origin.compare(0, 6, "https:") == 0 && request.caFile.empty())
      return Failure(response, "The pinned CA bundle is not configured; HTTPS was not attempted.");
    response = Response();
    response.redirects = redirect;
    CURL *curl = curl_easy_init();
    if (!curl)
      return Failure(response, "libcurl could not create a request.");
    Transfer transfer{&request, &response};
    char curlError[CURL_ERROR_SIZE] = {};
    curl_slist *headers = nullptr;
    if (!request.contentType.empty() && !body.empty())
      headers = curl_slist_append(headers, ("Content-Type: " + request.contentType).c_str());
    if (!bearer.empty())
      headers = curl_slist_append(headers, ("Authorization: Bearer " + bearer).c_str());
    headers = curl_slist_append(headers, "Accept: application/json, application/octet-stream");
    headers = curl_slist_append(headers, "Connection: close");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlError);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Hyperion-ReplayKraken/1");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 0L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https,http");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, std::max(1L, request.timeoutMs));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, std::max(1L, request.connectTimeoutMs));
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 20L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    if (endpoint.origin.compare(0, 6, "https:") == 0)
      curl_easy_setopt(curl, CURLOPT_CAINFO, request.caFile.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, BodyCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);
    if (method == "GET") {
      curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else {
      curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
      if (!body.empty() || method == "POST" || method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
      }
    }
    const CURLcode result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (response.bCancelled)
      return Failure(response, "The request was cancelled.");
    if (response.bTooLarge)
      return Failure(response, "The server response exceeded the configured size limit.");
    if (result != CURLE_OK) {
      response.bCancelled = result == CURLE_ABORTED_BY_CALLBACK && request.pCancel && request.pCancel->load();
      return Failure(response, curlError[0] ? curlError : curl_easy_strerror(result));
    }
    const bool bRedirect = response.status == 301 || response.status == 302 || response.status == 303 ||
                           response.status == 307 || response.status == 308;
    if (!bRedirect)
      return response;
    const std::string location = response.Header("location");
    if (location.empty())
      return Failure(response, "The server returned a redirect without a Location header.");
    if (redirect >= 3)
      return Failure(response, "The server exceeded the three-redirect limit.");
    const std::string next = ResolveLocation(url, Trim(location));
    ReplayKraken::Endpoint nextEndpoint;
    if (!ReplayKraken::RedirectAllowed(endpoint, next, !bearer.empty(), &nextEndpoint, &endpointError))
      return Failure(response, endpointError);
    // EndpointParse accepts API base URLs. Redirect targets name resources, so
    // only its security decisions are reused; the unmodified next URL is fetched.
    if (endpoint.origin != nextEndpoint.origin)
      bearer.clear();
    if (response.status == 303 || ((response.status == 301 || response.status == 302) && method != "GET" &&
                                   method != "HEAD")) {
      method = "GET";
      body.clear();
      bearer = nextEndpoint.origin == endpoint.origin ? bearer : "";
    }
    url = next;
  }
}

#endif

}  // namespace PortHttp
