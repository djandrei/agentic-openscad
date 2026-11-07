#include "mcp/protocol/Registry.h"

#include <optional>
#include <string>
#include <utility>

namespace mcp {

json textContent(const std::string& text)
{
  return json{{"type", "text"}, {"text", text}};
}

json imageContent(const std::string& base64Data, const std::string& mimeType)
{
  return json{{"type", "image"}, {"data", base64Data}, {"mimeType", mimeType}};
}

ToolResult ToolResult::text(const std::string& text)
{
  ToolResult r;
  r.content.push_back(textContent(text));
  return r;
}

ToolResult ToolResult::data(const json& structured)
{
  ToolResult r;
  r.content.push_back(textContent(structured.dump(2)));
  r.structuredContent = structured;
  return r;
}

ToolResult ToolResult::error(const std::string& message)
{
  ToolResult r;
  r.content.push_back(textContent(message));
  r.isError = true;
  return r;
}

json ToolResult::toJson() const
{
  json j{{"content", content}, {"isError", isError}};
  if (!structuredContent.is_null()) j["structuredContent"] = structuredContent;
  return j;
}

void ToolRegistry::add(ToolDef def)
{
  for (auto& existing : tools_) {
    if (existing.name == def.name) {
      existing = std::move(def);
      return;
    }
  }
  tools_.push_back(std::move(def));
}

const ToolDef *ToolRegistry::find(const std::string& name) const
{
  for (const auto& tool : tools_) {
    if (tool.name == name) return &tool;
  }
  return nullptr;
}

json ToolRegistry::listJson() const
{
  json list = json::array();
  for (const auto& tool : tools_) {
    list.push_back(
      json{{"name", tool.name}, {"description", tool.description}, {"inputSchema", tool.inputSchema}});
  }
  return list;
}

json ResourceContent::toJson() const
{
  json j{{"uri", uri}, {"mimeType", mimeType}};
  if (!blobBase64.empty()) {
    j["blob"] = blobBase64;
  } else {
    j["text"] = text;
  }
  return j;
}

void ResourceRegistry::add(ResourceDef def)
{
  resources_.push_back(std::move(def));
}

void ResourceRegistry::addTemplate(ResourceTemplateDef def)
{
  templates_.push_back(std::move(def));
}

void ResourceRegistry::addResolver(Resolver resolver)
{
  resolvers_.push_back(std::move(resolver));
}

json ResourceRegistry::listJson() const
{
  json list = json::array();
  for (const auto& res : resources_) {
    list.push_back(json{{"uri", res.uri},
                        {"name", res.name},
                        {"description", res.description},
                        {"mimeType", res.mimeType}});
  }
  return list;
}

json ResourceRegistry::listTemplatesJson() const
{
  json list = json::array();
  for (const auto& tpl : templates_) {
    list.push_back(json{{"uriTemplate", tpl.uriTemplate},
                        {"name", tpl.name},
                        {"description", tpl.description},
                        {"mimeType", tpl.mimeType}});
  }
  return list;
}

std::optional<ResourceContent> ResourceRegistry::read(const std::string& uri) const
{
  for (const auto& res : resources_) {
    if (res.uri == uri) return res.read();
  }
  for (const auto& resolver : resolvers_) {
    if (auto content = resolver(uri)) return content;
  }
  return std::nullopt;
}

void PromptRegistry::add(PromptDef def)
{
  prompts_.push_back(std::move(def));
}

const PromptDef *PromptRegistry::find(const std::string& name) const
{
  for (const auto& prompt : prompts_) {
    if (prompt.name == name) return &prompt;
  }
  return nullptr;
}

json PromptRegistry::listJson() const
{
  json list = json::array();
  for (const auto& prompt : prompts_) {
    list.push_back(
      json{{"name", prompt.name}, {"description", prompt.description}, {"arguments", prompt.arguments}});
  }
  return list;
}

}  // namespace mcp
