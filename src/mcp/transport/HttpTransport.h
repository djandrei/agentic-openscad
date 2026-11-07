#pragma once

#include <map>
#include <memory>
#include <string>

#include "mcp/protocol/Server.h"
#include "mcp/transport/Transport.h"

namespace mcp {

struct HttpOptions {
  std::string bindAddress = "127.0.0.1";
  unsigned short port = 0;  // 0 = pick a free port; see HttpTransport::port()
  std::string token;        // empty = no authentication (tests only)
  std::string path = "/mcp";
};

// Random bearer token, `bytes` random bytes rendered as hex.
std::string generateToken(size_t bytes = 16);

// MCP Streamable HTTP transport (POST only; no server-initiated SSE stream).
// Binds to the loopback address, validates the Origin header against
// localhost to block DNS-rebinding, and requires `Authorization: Bearer
// <token>`. Requests are served on a dedicated thread; Server calls go through
// the executor.
class HttpTransport
{
public:
  struct Response {
    unsigned status;
    std::string contentType;
    std::string body;
    std::map<std::string, std::string> headers;
  };

  HttpTransport(Server& server, HttpOptions options, Executor executor = directExecutor);
  ~HttpTransport();

  HttpTransport(const HttpTransport&) = delete;
  HttpTransport& operator=(const HttpTransport&) = delete;

  // Binds and starts the serving thread. Throws std::runtime_error when the
  // address cannot be bound.
  void start();
  void stop();
  bool running() const;
  unsigned short port() const;
  std::string url() const;  // http://<bind>:<port><path>

  // Handles one HTTP request without a socket. Header names must be lowercase.
  // Public so the protocol-level behaviour is unit-testable.
  Response handle(const std::string& method, const std::string& target,
                  const std::map<std::string, std::string>& headers, const std::string& body);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mcp
