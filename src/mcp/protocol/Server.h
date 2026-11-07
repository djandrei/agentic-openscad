#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "json/json.hpp"
#include "mcp/protocol/Registry.h"

namespace mcp {

struct ServerInfo {
  std::string name;
  std::string version;
  // Free text handed to the client in the initialize result; tells the agent
  // how to use the server.
  std::string instructions;
};

// Transport-agnostic MCP server: turns incoming JSON-RPC messages into
// responses. Not thread-safe; the transport must call it from one thread at a
// time (see Executor in transport/Transport.h).
class Server
{
public:
  using NotificationSink = std::function<void(const json&)>;

  Server(ServerInfo info, ToolRegistry& tools, ResourceRegistry& resources, PromptRegistry& prompts);

  // Parses and handles one message. Returns the response to send, or nullopt
  // when nothing must be sent (notifications).
  std::optional<json> handleText(const std::string& text);
  std::optional<json> handleMessage(const json& message);

  bool isInitialized() const { return initialized_; }
  const std::string& protocolVersion() const { return protocolVersion_; }
  static const std::vector<std::string>& supportedProtocolVersions();

  // Server -> client notifications are handed to the transport through the sink.
  void setNotificationSink(NotificationSink sink) { sink_ = std::move(sink); }
  void notify(const std::string& method, json params = json::object());

  json capabilities() const;

private:
  std::optional<json> handleSingle(const json& message);
  json dispatch(const json& id, const std::string& method, const json& params);
  json onInitialize(const json& params);
  json onToolsCall(const json& params);
  json onResourcesRead(const json& params);
  json onPromptsGet(const json& params);

  ServerInfo info_;
  ToolRegistry& tools_;
  ResourceRegistry& resources_;
  PromptRegistry& prompts_;
  NotificationSink sink_;
  bool initialized_ = false;
  std::string protocolVersion_;
};

}  // namespace mcp
