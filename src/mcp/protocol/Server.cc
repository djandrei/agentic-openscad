#include "mcp/protocol/Server.h"

#include <algorithm>
#include <exception>
#include <optional>
#include <string>
#include <vector>

#include "mcp/BuildInfo.h"
#include "mcp/protocol/JsonRpc.h"
#include "mcp/protocol/Schema.h"

namespace mcp {

namespace {

// Raised inside dispatch() to produce a JSON-RPC error response.
struct RpcFailure {
  jsonrpc::Error error;
};

[[noreturn]] void fail(int code, const std::string& message, json data = nullptr)
{
  throw RpcFailure{{code, message, std::move(data)}};
}

}  // namespace

Server::Server(ServerInfo info, ToolRegistry& tools, ResourceRegistry& resources,
               PromptRegistry& prompts)
  : info_(std::move(info)),
    tools_(tools),
    resources_(resources),
    prompts_(prompts),
    protocolVersion_(mcp::protocolVersion())
{
}

const std::vector<std::string>& Server::supportedProtocolVersions()
{
  static const std::vector<std::string> versions{"2025-06-18", "2025-03-26", "2024-11-05"};
  return versions;
}

json Server::capabilities() const
{
  return json{{"tools", json{{"listChanged", false}}},
              {"resources", json{{"subscribe", false}, {"listChanged", false}}},
              {"prompts", json{{"listChanged", false}}},
              {"logging", json::object()}};
}

void Server::notify(const std::string& method, json params)
{
  if (sink_) sink_(jsonrpc::makeNotification(method, std::move(params)));
}

std::optional<json> Server::handleText(const std::string& text)
{
  jsonrpc::Error error;
  auto parsed = jsonrpc::parse(text, error);
  if (!parsed) return jsonrpc::makeError(nullptr, error);
  return handleMessage(*parsed);
}

std::optional<json> Server::handleMessage(const json& message)
{
  if (message.is_array()) {
    json responses = json::array();
    for (const auto& item : message) {
      if (auto response = handleSingle(item)) responses.push_back(*response);
    }
    if (responses.empty()) return std::nullopt;
    return responses;
  }
  return handleSingle(message);
}

std::optional<json> Server::handleSingle(const json& message)
{
  if (!message.is_object() || !message.contains("method")) {
    // A response from the client (to a server request). We send none, so ignore.
    return std::nullopt;
  }
  const std::string method = message["method"].get<std::string>();
  const json params = message.value("params", json::object());

  if (jsonrpc::isNotification(message)) {
    // notifications/initialized, notifications/cancelled, ...: nothing to do.
    return std::nullopt;
  }

  const json id = message["id"];
  if (message.value("jsonrpc", "") != "2.0") {
    return jsonrpc::makeError(id,
                              {jsonrpc::kInvalidRequest, "Invalid Request: jsonrpc must be \"2.0\""});
  }
  try {
    return jsonrpc::makeResult(id, dispatch(id, method, params));
  } catch (const RpcFailure& failure) {
    return jsonrpc::makeError(id, failure.error);
  } catch (const std::exception& e) {
    return jsonrpc::makeError(id, {jsonrpc::kInternalError, std::string("Internal error: ") + e.what()});
  }
}

json Server::dispatch(const json& /*id*/, const std::string& method, const json& params)
{
  if (method == "initialize") return onInitialize(params);
  if (method == "ping") return json::object();

  if (!initialized_) fail(jsonrpc::kInvalidRequest, "Server not initialized: send initialize first");

  if (method == "tools/list") return json{{"tools", tools_.listJson()}};
  if (method == "tools/call") return onToolsCall(params);
  if (method == "resources/list") return json{{"resources", resources_.listJson()}};
  if (method == "resources/templates/list") {
    return json{{"resourceTemplates", resources_.listTemplatesJson()}};
  }
  if (method == "resources/read") return onResourcesRead(params);
  if (method == "prompts/list") return json{{"prompts", prompts_.listJson()}};
  if (method == "prompts/get") return onPromptsGet(params);
  if (method == "logging/setLevel") return json::object();

  fail(jsonrpc::kMethodNotFound, "Method not found: " + method);
}

json Server::onInitialize(const json& params)
{
  const std::string requested = params.value("protocolVersion", "");
  const auto& supported = supportedProtocolVersions();
  if (std::find(supported.begin(), supported.end(), requested) != supported.end()) {
    protocolVersion_ = requested;
  } else {
    protocolVersion_ = supported.front();
  }
  initialized_ = true;

  json result{{"protocolVersion", protocolVersion_},
              {"capabilities", capabilities()},
              {"serverInfo", json{{"name", info_.name}, {"version", info_.version}}}};
  if (!info_.instructions.empty()) result["instructions"] = info_.instructions;
  return result;
}

json Server::onToolsCall(const json& params)
{
  if (!params.contains("name") || !params["name"].is_string()) {
    fail(jsonrpc::kInvalidParams, "tools/call: missing tool name");
  }
  const std::string name = params["name"].get<std::string>();
  const ToolDef *tool = tools_.find(name);
  if (!tool) fail(jsonrpc::kInvalidParams, "Unknown tool: " + name);

  json args = params.value("arguments", json::object());
  if (args.is_null()) args = json::object();
  if (auto err = schema::validate(tool->inputSchema, args)) {
    fail(jsonrpc::kInvalidParams, "Invalid arguments for tool '" + name + "': " + *err);
  }

  try {
    return tool->handler(args).toJson();
  } catch (const ToolError& e) {
    return ToolResult::error(e.what()).toJson();
  } catch (const std::exception& e) {
    return ToolResult::error(std::string("Tool '") + name + "' failed: " + e.what()).toJson();
  }
}

json Server::onResourcesRead(const json& params)
{
  if (!params.contains("uri") || !params["uri"].is_string()) {
    fail(jsonrpc::kInvalidParams, "resources/read: missing uri");
  }
  const std::string uri = params["uri"].get<std::string>();
  auto content = resources_.read(uri);
  if (!content) fail(jsonrpc::kResourceNotFound, "Resource not found: " + uri, json{{"uri", uri}});
  if (content->uri.empty()) content->uri = uri;
  return json{{"contents", json::array({content->toJson()})}};
}

json Server::onPromptsGet(const json& params)
{
  if (!params.contains("name") || !params["name"].is_string()) {
    fail(jsonrpc::kInvalidParams, "prompts/get: missing name");
  }
  const std::string name = params["name"].get<std::string>();
  const PromptDef *prompt = prompts_.find(name);
  if (!prompt) fail(jsonrpc::kInvalidParams, "Unknown prompt: " + name);
  json args = params.value("arguments", json::object());
  for (const auto& argDef : prompt->arguments) {
    if (argDef.value("required", false) && !args.contains(argDef["name"].get<std::string>())) {
      fail(jsonrpc::kInvalidParams,
           "prompts/get: missing required argument '" + argDef["name"].get<std::string>() + "'");
    }
  }
  return prompt->get(args);
}

}  // namespace mcp
