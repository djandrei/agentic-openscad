#include "mcp/library/Download.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include "core/HTTPClient.h"
#include "mcp/protocol/Registry.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

namespace mcp::library {

namespace {

constexpr int kMaxRedirects = 5;

std::string readLocal(const std::filesystem::path& path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in) throw ToolError("cannot read " + path.string());
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

struct Fetched {
  unsigned status = 0;
  std::string location;
  std::string body;
};

template <typename Stream>
Fetched exchange(Stream& stream, const ParsedURL& url)
{
  http::request<http::empty_body> request{http::verb::get, url.target, 11};
  request.set(http::field::host, url.host_header);
  request.set(http::field::user_agent, "agentic-openscad");
  request.set(http::field::accept, "*/*");
  http::write(stream, request);

  beast::flat_buffer buffer;
  http::response_parser<http::string_body> parser;
  parser.body_limit(std::numeric_limits<std::uint64_t>::max());
  http::read(stream, buffer, parser);
  auto response = parser.release();
  Fetched result;
  result.status = response.result_int();
  if (response.count(http::field::location))
    result.location = std::string(response[http::field::location]);
  result.body = std::move(response.body());
  return result;
}

Fetched fetchOnce(const std::string& urlText)
{
  const ParsedURL url = parseURL(urlText);
  if (!url.error.empty()) throw ToolError("invalid URL " + urlText + ": " + url.error);
  net::io_context ioc;
  tcp::resolver resolver(ioc);
  const auto endpoints = resolver.resolve(url.host, std::to_string(url.port));

  if (url.scheme == "https") {
    ssl::context ctx(ssl::context::tlsv12_client);
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_peer);
    beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);
    if (!SSL_set_tlsext_host_name(stream.native_handle(), url.host.c_str())) {
      throw ToolError("cannot set TLS server name for " + url.host);
    }
    beast::get_lowest_layer(stream).connect(endpoints);
    stream.handshake(ssl::stream_base::client);
    Fetched result = exchange(stream, url);
    beast::error_code ec;
    stream.shutdown(ec);
    return result;
  }
  if (url.scheme == "http") {
    beast::tcp_stream stream(ioc);
    stream.connect(endpoints);
    Fetched result = exchange(stream, url);
    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    return result;
  }
  throw ToolError("unsupported URL scheme in " + urlText);
}

std::string resolveLocation(const std::string& base, const std::string& location)
{
  if (location.rfind("http://", 0) == 0 || location.rfind("https://", 0) == 0) return location;
  const ParsedURL url = parseURL(base);
  const std::string origin = url.scheme + "://" + url.host_header;
  if (!location.empty() && location[0] == '/') return origin + location;
  const auto slash = base.rfind('/');
  return (slash == std::string::npos ? base : base.substr(0, slash)) + "/" + location;
}

}  // namespace

std::string download(const std::string& url)
{
  if (url.rfind("file://", 0) == 0) return readLocal(url.substr(7));
  if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return readLocal(url);

  std::string current = url;
  for (int hop = 0; hop <= kMaxRedirects; ++hop) {
    Fetched fetched;
    try {
      fetched = fetchOnce(current);
    } catch (const ToolError&) {
      throw;
    } catch (const std::exception& e) {
      throw ToolError("download failed for " + current + ": " + e.what());
    }
    if (fetched.status >= 300 && fetched.status < 400 && !fetched.location.empty()) {
      current = resolveLocation(current, fetched.location);
      continue;
    }
    if (fetched.status >= 400) {
      throw ToolError("download failed for " + current + ": HTTP " + std::to_string(fetched.status),
                      nlohmann::json{{"status", fetched.status}, {"url", current}});
    }
    return fetched.body;
  }
  throw ToolError("too many redirects for " + url);
}

}  // namespace mcp::library
