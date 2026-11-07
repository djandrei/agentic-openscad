#pragma once

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "json/json.hpp"

// Registries of the three MCP primitives the server exposes: tools, resources
// and prompts. Transport- and Qt-free.
namespace mcp {

using json = nlohmann::json;

// Thrown by tool handlers to report a user-visible failure. The server turns it
// into a tool result with isError = true (not a JSON-RPC error). `data` is
// returned as structuredContent so the agent can act on it (e.g. the current
// file version after a conflict).
class ToolError : public std::runtime_error
{
public:
  explicit ToolError(const std::string& message, json data = nullptr)
    : std::runtime_error(message), data_(std::move(data))
  {
  }
  const json& data() const { return data_; }

private:
  json data_;
};

json textContent(const std::string& text);
json imageContent(const std::string& base64Data, const std::string& mimeType);

struct ToolResult {
  json content = json::array();
  json structuredContent = nullptr;
  bool isError = false;

  static ToolResult text(const std::string& text);
  // Structured JSON result: the text content carries the pretty-printed JSON
  // for clients that only read text; structuredContent carries it verbatim.
  static ToolResult data(const json& structured);
  static ToolResult error(const std::string& message, json data = nullptr);

  json toJson() const;
};

struct ToolDef {
  std::string name;
  std::string description;
  json inputSchema = json{{"type", "object"}, {"properties", json::object()}};
  std::function<ToolResult(const json& args)> handler;
};

class ToolRegistry
{
public:
  void add(ToolDef def);
  const ToolDef *find(const std::string& name) const;
  json listJson() const;
  size_t size() const { return tools_.size(); }

private:
  std::vector<ToolDef> tools_;
};

struct ResourceContent {
  std::string uri;
  std::string mimeType;
  std::string text;        // for text resources
  std::string blobBase64;  // for binary resources (text left empty)
  json toJson() const;
};

struct ResourceDef {
  std::string uri;
  std::string name;
  std::string description;
  std::string mimeType;
  std::function<ResourceContent()> read;
};

struct ResourceTemplateDef {
  std::string uriTemplate;
  std::string name;
  std::string description;
  std::string mimeType;
};

class ResourceRegistry
{
public:
  // Resolvers serve URIs that are not registered statically (templates).
  using Resolver = std::function<std::optional<ResourceContent>(const std::string& uri)>;

  void add(ResourceDef def);
  void addTemplate(ResourceTemplateDef def);
  void addResolver(Resolver resolver);
  json listJson() const;
  json listTemplatesJson() const;
  std::optional<ResourceContent> read(const std::string& uri) const;

private:
  std::vector<ResourceDef> resources_;
  std::vector<ResourceTemplateDef> templates_;
  std::vector<Resolver> resolvers_;
};

struct PromptDef {
  std::string name;
  std::string description;
  json arguments = json::array();  // [{name, description, required}]
  // Returns {"description": ..., "messages": [...]}
  std::function<json(const json& args)> get;
};

class PromptRegistry
{
public:
  void add(PromptDef def);
  const PromptDef *find(const std::string& name) const;
  json listJson() const;

private:
  std::vector<PromptDef> prompts_;
};

}  // namespace mcp
