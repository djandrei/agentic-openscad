#include "mcp/transport/HttpTransport.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <iomanip>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <boost/asio.hpp>
#ifndef _WIN32
#include <sys/socket.h>
#include <sys/time.h>
#endif
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include "mcp/protocol/JsonRpc.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace mcp {

std::string generateToken(size_t bytes)
{
  std::random_device rd;
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (size_t i = 0; i < bytes; ++i) {
    out << std::setw(2) << (rd() & 0xffU);
  }
  return out.str();
}

namespace {

std::string toLower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

// Host part of an Origin header value ("http://localhost:1234" -> "localhost").
std::string originHost(const std::string& origin)
{
  std::string rest = origin;
  const auto scheme = rest.find("://");
  if (scheme != std::string::npos) rest = rest.substr(scheme + 3);
  if (!rest.empty() && rest[0] == '[') {
    const auto close = rest.find(']');
    return close == std::string::npos ? rest : rest.substr(0, close + 1);
  }
  const auto end = rest.find_first_of(":/");
  return end == std::string::npos ? rest : rest.substr(0, end);
}

bool originAllowed(const std::string& origin)
{
  if (origin.empty() || origin == "null") return true;
  const std::string host = toLower(originHost(origin));
  return host == "localhost" || host == "127.0.0.1" || host == "[::1]";
}

HttpTransport::Response jsonResponse(unsigned status, const std::string& body)
{
  return {status, "application/json", body, {}};
}

HttpTransport::Response errorResponse(unsigned status, const std::string& message)
{
  return jsonResponse(status, nlohmann::json{{"error", message}}.dump());
}

}  // namespace

class HttpTransport::Impl
{
public:
  Impl(Server& server, HttpOptions options, Executor executor)
    : server(server), options(std::move(options)), executor(std::move(executor))
  {
  }

  Server& server;
  HttpOptions options;
  Executor executor;
  net::io_context ioc{1};
  std::optional<tcp::acceptor> acceptor;
  std::thread thread;
  std::atomic<bool> running{false};

  void doAccept(HttpTransport *owner)
  {
    acceptor->async_accept([this, owner](beast::error_code ec, tcp::socket socket) {
      if (!ec && running) serve(owner, std::move(socket));
      if (running && acceptor && acceptor->is_open()) doAccept(owner);
    });
  }

  void serve(HttpTransport *owner, tcp::socket socket)
  {
    // Read/write timeouts so a stalled client cannot block the single
    // serving thread for ever.
#ifdef _WIN32
    const DWORD timeoutMs = 30000;
    setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char *>(&timeoutMs), sizeof(timeoutMs));
    setsockopt(socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO,
               reinterpret_cast<const char *>(&timeoutMs), sizeof(timeoutMs));
#else
    const timeval timeout{30, 0};
    setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
    try {
      serveRequests(owner, socket);
    } catch (const std::exception&) {
      // Malformed request or broken connection: drop it, keep accepting.
    }
    beast::error_code ec;
    socket.shutdown(tcp::socket::shutdown_both, ec);
  }

  void serveRequests(HttpTransport *owner, tcp::socket& socket)
  {
    beast::error_code ec;
    for (;;) {
      beast::flat_buffer buffer;
      http::request_parser<http::string_body> parser;
      parser.body_limit(64U * 1024U * 1024U);
      http::read(socket, buffer, parser, ec);
      if (ec) break;
      const auto req = parser.release();

      std::map<std::string, std::string> headers;
      for (const auto& field : req) {
        headers[toLower(std::string(field.name_string()))] = std::string(field.value());
      }
      const Response result =
        owner->handle(std::string(req.method_string()), std::string(req.target()), headers, req.body());

      http::response<http::string_body> res{static_cast<http::status>(result.status), req.version()};
      res.set(http::field::server, "agentic-openscad");
      res.set(http::field::content_type, result.contentType);
      for (const auto& [name, value] : result.headers) res.set(name, value);
      res.keep_alive(req.keep_alive());
      res.body() = result.body;
      res.prepare_payload();
      http::write(socket, res, ec);
      if (ec || !req.keep_alive()) break;
    }
  }
};

HttpTransport::HttpTransport(Server& server, HttpOptions options, Executor executor)
  : impl_(std::make_unique<Impl>(server, std::move(options), std::move(executor)))
{
}

HttpTransport::~HttpTransport()
{
  stop();
}

void HttpTransport::start()
{
  if (impl_->running) return;
  beast::error_code ec;
  const auto address = net::ip::make_address(impl_->options.bindAddress, ec);
  if (ec) throw std::runtime_error("MCP HTTP: invalid bind address " + impl_->options.bindAddress);
  const tcp::endpoint endpoint(address, impl_->options.port);

  impl_->ioc.restart();
  impl_->acceptor.emplace(impl_->ioc);
  impl_->acceptor->open(endpoint.protocol(), ec);
  if (!ec) impl_->acceptor->set_option(net::socket_base::reuse_address(true), ec);
  if (!ec) impl_->acceptor->bind(endpoint, ec);
  if (!ec) impl_->acceptor->listen(net::socket_base::max_listen_connections, ec);
  if (ec) {
    impl_->acceptor.reset();
    throw std::runtime_error("MCP HTTP: cannot listen on " + impl_->options.bindAddress + ":" +
                             std::to_string(impl_->options.port) + ": " + ec.message());
  }
  impl_->options.port = impl_->acceptor->local_endpoint().port();
  impl_->running = true;
  impl_->doAccept(this);
  impl_->thread = std::thread([this] { impl_->ioc.run(); });
}

void HttpTransport::stop()
{
  if (!impl_->running) return;
  impl_->running = false;
  net::post(impl_->ioc, [this] {
    beast::error_code ec;
    if (impl_->acceptor) impl_->acceptor->close(ec);
  });
  impl_->ioc.stop();
  if (impl_->thread.joinable()) impl_->thread.join();
  impl_->acceptor.reset();
}

bool HttpTransport::running() const
{
  return impl_->running;
}

unsigned short HttpTransport::port() const
{
  return impl_->options.port;
}

std::string HttpTransport::url() const
{
  return "http://" + impl_->options.bindAddress + ":" + std::to_string(port()) + impl_->options.path;
}

HttpTransport::Response HttpTransport::handle(const std::string& method, const std::string& target,
                                              const std::map<std::string, std::string>& headers,
                                              const std::string& body)
{
  const std::string path = target.substr(0, target.find('?'));
  if (path != impl_->options.path) return errorResponse(404, "Not found");

  const auto origin = headers.find("origin");
  if (origin != headers.end() && !originAllowed(origin->second)) {
    return errorResponse(403, "Origin not allowed");
  }

  if (!impl_->options.token.empty()) {
    const auto auth = headers.find("authorization");
    if (auth == headers.end() || auth->second != "Bearer " + impl_->options.token) {
      Response r = errorResponse(401, "Unauthorized: expected Authorization: Bearer <token>");
      r.headers["WWW-Authenticate"] = "Bearer";
      return r;
    }
  }

  if (method != "POST") {
    Response r = errorResponse(405, "Method not allowed: this server accepts JSON-RPC over POST only");
    r.headers["Allow"] = "POST";
    return r;
  }

  std::optional<nlohmann::json> response;
  impl_->executor([&] { response = impl_->server.handleText(body); });
  if (!response) return {202, "application/json", "", {}};
  return jsonResponse(200, jsonrpc::serialize(*response));
}

}  // namespace mcp
