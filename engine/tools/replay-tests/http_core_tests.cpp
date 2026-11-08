// Native libcurl/transport integration against transport_fixture.py (loopback only).
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "h/port_http.h"
#include "h/port_replaykraken.h"

namespace {
void Check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}

PortHttp::Request Get(const std::string &url)
{
  PortHttp::Request request;
  request.url = url;
  request.timeoutMs = 2000;
  request.connectTimeoutMs = 500;
  request.maxBytes = 1024;
  return request;
}
}

int main()
{
  try {
    Check(PortHttp::Available(), "libcurl unavailable");
    const std::string base = "http://127.0.0.1:18790";
    {
      auto response = PortHttp::Send(Get(base + "/api/users/@me"));
      Check(response.status == 401, "identity without token is 401");
    }
    {
      auto request = Get(base + "/api/users/@me");
      request.bearer = "fixture-token";
      auto response = PortHttp::Send(request);
      Check(response.status == 200 && response.Text().find("alice") != std::string::npos, "bearer identity");
    }
    {
      auto request = Get(base + "/redirect/same");
      request.bearer = "fixture-token";
      auto response = PortHttp::Send(request);
      Check(response.status == 200 && response.redirects == 1, "same-origin redirect followed once");
    }
    {
      auto request = Get(base + "/redirect/cross");
      request.bearer = "fixture-token";
      auto response = PortHttp::Send(request);
      Check(response.status == 0 && response.error.find("another origin") != std::string::npos,
            "cross-origin credentials refused");
    }
    {
      ReplayKraken::Endpoint from, next;
      std::string error;
      Check(ReplayKraken::EndpointParse("https://example.org", &from, &error), error.c_str());
      Check(!ReplayKraken::RedirectAllowed(from, "http://127.0.0.1:18790/api/matches", true, &next, &error),
            "HTTPS-to-HTTP downgrade refused before any socket use");
    }
    {
      auto request = Get(base + "/large");
      request.maxBytes = 16;
      auto response = PortHttp::Send(request);
      Check(response.bTooLarge, "oversized response is bounded");
    }
    {
      auto response = PortHttp::Send(Get(base + "/malformed"));
      ReplayKraken::Endpoint endpoint;
      std::string error;
      Check(ReplayKraken::EndpointParse(base, &endpoint, &error), error.c_str());
      ReplayKraken::MatchPage page;
      Check(!ReplayKraken::ParsePage(response.Text(), endpoint, &page, &error), "malformed archive JSON rejected");
    }
    {
      auto response = PortHttp::Send(Get(base + "/api/matches/uuid/replay"));
      Check(response.status == 200 && response.bytes.size() == 3 && response.bytes[0] == 'a', "raw replay bytes");
    }
    {
      auto request = Get(base + "/slow");
      std::atomic<bool> cancel{false};
      request.pCancel = &cancel;
      std::thread canceller([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        cancel.store(true);
      });
      auto response = PortHttp::Send(request);
      canceller.join();
      Check(response.bCancelled, "in-flight cancellation is honored");
    }
    std::cout << "PASS native HTTP transport\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << "\n";
    return 1;
  }
}
