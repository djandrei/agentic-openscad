#pragma once

#include <optional>
#include <string>

#include "json/json.hpp"

// Minimal JSON-RPC 2.0 helpers used by the MCP server.
namespace mcp::jsonrpc {

using json = nlohmann::json;

constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kInternalError = -32603;
// MCP: resources/read for an unknown URI.
constexpr int kResourceNotFound = -32002;

struct Error {
  int code;
  std::string message;
  json data = nullptr;
};

json makeResult(const json& id, json result);
json makeError(const json& id, const Error& error);
json makeNotification(const std::string& method, json params = json::object());

// Parses one JSON-RPC message (an object or a batch array). Returns nullopt and
// fills `error` when the text is not valid JSON or not a JSON-RPC message.
std::optional<json> parse(const std::string& text, Error& error);

bool isRequest(const json& message);       // has "method" and "id"
bool isNotification(const json& message);  // has "method" but no "id"

// Serializes a message on a single line (the stdio transport requires no
// embedded newlines).
std::string serialize(const json& message);

}  // namespace mcp::jsonrpc
