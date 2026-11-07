#include "mcp/transport/HttpTransport.h"

#include <catch2/catch_all.hpp>
#include <map>
#include <string>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include "mcp/protocol/Registry.h"
#include "mcp/protocol/Server.h"

using nlohmann::json;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace {

const std::string kInitialize =
  R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})";

struct Fixture {
  mcp::ToolRegistry tools;
  mcp::ResourceRegistry resources;
  mcp::PromptRegistry prompts;
  mcp::Server server{mcp::ServerInfo{"t", "1", ""}, tools, resources, prompts};
  mcp::HttpOptions options;
  Fixture() { options.token = "secret"; }
  std::map<std::string, std::string> authHeaders() { return {{"authorization", "Bearer secret"}}; }
};

http::response<http::string_body> post(unsigned short port, const std::string& body,
                                       const std::string& token)
{
  net::io_context ioc;
  tcp::socket socket(ioc);
  socket.connect(tcp::endpoint(net::ip::make_address("127.0.0.1"), port));
  http::request<http::string_body> req{http::verb::post, "/mcp", 11};
  req.set(http::field::host, "127.0.0.1");
  req.set(http::field::content_type, "application/json");
  req.set(http::field::accept, "application/json, text/event-stream");
  if (!token.empty()) req.set(http::field::authorization, "Bearer " + token);
  req.body() = body;
  req.prepare_payload();
  http::write(socket, req);
  beast::flat_buffer buffer;
  http::response<http::string_body> res;
  http::read(socket, buffer, res);
  return res;
}

}  // namespace

TEST_CASE("HTTP transport rejects wrong path, bad origin, missing token and non-POST", "[mcp][http]")
{
  Fixture f;
  mcp::HttpTransport transport(f.server, f.options);

  REQUIRE(transport.handle("POST", "/other", f.authHeaders(), kInitialize).status == 404);

  auto badOrigin = f.authHeaders();
  badOrigin["origin"] = "http://evil.example.com";
  REQUIRE(transport.handle("POST", "/mcp", badOrigin, kInitialize).status == 403);

  auto goodOrigin = f.authHeaders();
  goodOrigin["origin"] = "http://localhost:3000";
  REQUIRE(transport.handle("POST", "/mcp", goodOrigin, kInitialize).status == 200);

  REQUIRE(transport.handle("POST", "/mcp", {}, kInitialize).status == 401);
  REQUIRE(transport.handle("POST", "/mcp", {{"authorization", "Bearer wrong"}}, kInitialize).status ==
          401);

  auto get = transport.handle("GET", "/mcp", f.authHeaders(), "");
  REQUIRE(get.status == 405);
  REQUIRE(get.headers.at("Allow") == "POST");
  REQUIRE(transport.handle("DELETE", "/mcp", f.authHeaders(), "").status == 405);
}

TEST_CASE("HTTP transport returns JSON-RPC responses and 202 for notifications", "[mcp][http]")
{
  Fixture f;
  mcp::HttpTransport transport(f.server, f.options);

  auto response = transport.handle("POST", "/mcp?x=1", f.authHeaders(), kInitialize);
  REQUIRE(response.status == 200);
  REQUIRE(response.contentType == "application/json");
  json body = json::parse(response.body);
  REQUIRE(body["result"]["protocolVersion"] == "2025-06-18");

  auto accepted = transport.handle("POST", "/mcp", f.authHeaders(),
                                   R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
  REQUIRE(accepted.status == 202);
  REQUIRE(accepted.body.empty());

  auto parseError = transport.handle("POST", "/mcp", f.authHeaders(), "nope");
  REQUIRE(parseError.status == 200);
  REQUIRE(json::parse(parseError.body)["error"]["code"] == -32700);
}

TEST_CASE("HTTP transport serves requests on a loopback socket", "[mcp][http]")
{
  Fixture f;
  int executions = 0;
  mcp::HttpTransport transport(f.server, f.options, [&](const std::function<void()>& fn) {
    ++executions;
    fn();
  });
  transport.start();
  REQUIRE(transport.running());
  REQUIRE(transport.port() != 0);
  REQUIRE(transport.url() == "http://127.0.0.1:" + std::to_string(transport.port()) + "/mcp");

  auto res = post(transport.port(), kInitialize, "secret");
  REQUIRE(res.result_int() == 200);
  REQUIRE(json::parse(res.body())["result"]["serverInfo"]["name"] == "t");
  REQUIRE(executions == 1);

  auto denied = post(transport.port(), kInitialize, "");
  REQUIRE(denied.result_int() == 401);

  transport.stop();
  REQUIRE_FALSE(transport.running());
}

TEST_CASE("generateToken yields distinct hex strings", "[mcp][http]")
{
  const auto a = mcp::generateToken();
  const auto b = mcp::generateToken();
  REQUIRE(a.size() == 32);
  REQUIRE(a != b);
  REQUIRE(a.find_first_not_of("0123456789abcdef") == std::string::npos);
}
