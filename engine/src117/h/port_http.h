// port_http.h -- the port's small blocking HTTPS client surface. Calls are for
// service workers only; no UI/game code should call Send on its own thread.
#ifndef PORT_HTTP_H
#define PORT_HTTP_H

#include <atomic>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace PortHttp {

struct Request {
  std::string method = "GET";
  std::string url;
  std::string body;
  std::string contentType;
  std::string bearer;
  std::string caFile;
  size_t maxBytes = 8 * 1024 * 1024;
  long timeoutMs = 30000;
  long connectTimeoutMs = 10000;
  std::atomic<bool> *pCancel = nullptr;
};

struct Response {
  long status = 0;
  std::vector<unsigned char> bytes;
  std::map<std::string, std::string> headers;  // Lower-case names, trimmed values.
  std::string error;                            // Transport/protocol error, never credentials.
  bool bCancelled = false;
  bool bTooLarge = false;
  unsigned redirects = 0;

  std::string Text() const { return std::string(bytes.begin(), bytes.end()); }
  std::string Header(const std::string &lowerName) const;
};

bool Available();
std::string Version();
Response Send(const Request &request);

}  // namespace PortHttp

#endif
