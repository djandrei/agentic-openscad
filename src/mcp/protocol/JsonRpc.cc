#include "mcp/protocol/JsonRpc.h"

#include <optional>
#include <string>

namespace mcp::jsonrpc {

json makeResult(const json& id, json result)
{
  return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

json makeError(const json& id, const Error& error)
{
  json err{{"code", error.code}, {"message", error.message}};
  if (!error.data.is_null()) err["data"] = error.data;
  return json{{"jsonrpc", "2.0"}, {"id", id}, {"error", err}};
}

json makeNotification(const std::string& method, json params)
{
  return json{{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}};
}

static bool isValidId(const json& id)
{
  return id.is_string() || id.is_number() || id.is_null();
}

static bool looksLikeMessage(const json& message)
{
  if (!message.is_object()) return false;
  if (message.contains("method")) {
    if (!message["method"].is_string()) return false;
    if (message.contains("id") && !isValidId(message["id"])) return false;
    return true;
  }
  // A response: result or error with an id.
  return (message.contains("result") || message.contains("error")) && message.contains("id");
}

std::optional<json> parse(const std::string& text, Error& error)
{
  json parsed = json::parse(text, nullptr, false);
  if (parsed.is_discarded()) {
    error = {kParseError, "Parse error"};
    return std::nullopt;
  }
  if (parsed.is_array()) {
    if (parsed.empty()) {
      error = {kInvalidRequest, "Invalid Request: empty batch"};
      return std::nullopt;
    }
    for (const auto& item : parsed) {
      if (!looksLikeMessage(item)) {
        error = {kInvalidRequest, "Invalid Request"};
        return std::nullopt;
      }
    }
    return parsed;
  }
  if (!looksLikeMessage(parsed)) {
    error = {kInvalidRequest, "Invalid Request"};
    return std::nullopt;
  }
  return parsed;
}

bool isRequest(const json& message)
{
  return message.is_object() && message.contains("method") && message.contains("id");
}

bool isNotification(const json& message)
{
  return message.is_object() && message.contains("method") && !message.contains("id");
}

std::string serialize(const json& message)
{
  // dump() with default indent (-1) emits no newlines; control characters in
  // strings are escaped.
  return message.dump();
}

}  // namespace mcp::jsonrpc
