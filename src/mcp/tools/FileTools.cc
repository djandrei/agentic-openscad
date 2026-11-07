#include "mcp/tools/FileTools.h"

#include <optional>
#include <string>
#include <vector>

#include "mcp/session/FileStore.h"
#include "mcp/tools/CoreTools.h"

namespace mcp {

using nlohmann::json;

namespace {

json pathSchema(const std::string& description)
{
  return json{{"type", "string"}, {"description", description}};
}

std::optional<std::string> optionalString(const json& args, const char *key)
{
  if (args.contains(key) && args[key].is_string()) return args[key].get<std::string>();
  return std::nullopt;
}

}  // namespace

void registerFileTools(ToolRegistry& tools, SessionManager& sessions)
{
  tools.add(
    ToolDef{"file_list",
            "List the files of the project (paths relative to the project root) with size and version. "
            "Hidden entries are skipped unless includeHidden is set.",
            json{{"type", "object"},
                 {"properties",
                  json{{"project", projectArgSchema()},
                       {"dir", pathSchema("Sub-folder to list; default is the project root.")},
                       {"recursive", json{{"type", "boolean"}, {"description", "Default true."}}},
                       {"includeHidden", json{{"type", "boolean"}, {"description", "Default false."}}}}},
                 {"additionalProperties", false}},
            [&sessions](const json& args) {
              auto& session = sessions.resolve(args);
              bool truncated = false;
              const auto entries =
                session.files().list(args.value("dir", std::string(".")), args.value("recursive", true),
                                     args.value("includeHidden", false), &truncated);
              json list = json::array();
              for (const auto& entry : entries) list.push_back(entry.toJson());
              return ToolResult::data(json{{"files", list}, {"truncated", truncated}});
            }});

  tools.add(ToolDef{
    "file_read",
    "Read a project file. Returns the content (or a window of lines when startLine/lineCount are "
    "given) and the file version to pass as baseVersion to file_write.",
    json{
      {"type", "object"},
      {"properties",
       json{{"project", projectArgSchema()},
            {"path", pathSchema("File path relative to the project root.")},
            {"startLine",
             json{{"type", "integer"}, {"minimum", 1}, {"description", "First line (1-based)."}}},
            {"lineCount",
             json{{"type", "integer"}, {"minimum", 1}, {"description", "Number of lines to return."}}}}},
      {"required", json::array({"path"})},
      {"additionalProperties", false}},
    [&sessions](const json& args) {
      auto& session = sessions.resolve(args);
      const auto content = session.files().read(args["path"].get<std::string>(),
                                                args.value("startLine", 1), args.value("lineCount", 0));
      return ToolResult::data(content.toJson());
    }});

  tools.add(ToolDef{
    "file_write",
    "Create or replace a project file. Pass either `content` (the whole file) or `edits` (exact "
    "text replacements applied in order; each `old` must occur exactly once unless replaceAll). "
    "Pass `baseVersion` from file_read to fail safely if the file changed meanwhile. The viewer shows "
    "the file with the changed lines highlighted.",
    json{
      {"type", "object"},
      {"properties",
       json{
         {"project", projectArgSchema()},
         {"path", pathSchema("File path relative to the project root; folders are created.")},
         {"content", json{{"type", "string"}, {"description", "Whole new content."}}},
         {"edits", json{{"type", "array"},
                        {"minItems", 1},
                        {"items", json{{"type", "object"},
                                       {"properties", json{{"old", json{{"type", "string"}}},
                                                           {"new", json{{"type", "string"}}},
                                                           {"replaceAll", json{{"type", "boolean"}}}}},
                                       {"required", json::array({"old", "new"})},
                                       {"additionalProperties", false}}}}},
         {"baseVersion", json{{"type", "string"}, {"description", "Version the edit is based on."}}}}},
      {"required", json::array({"path"})},
      {"additionalProperties", false}},
    [&sessions](const json& args) {
      auto& session = sessions.resolve(args);
      const std::string path = args["path"].get<std::string>();
      const bool hasContent = args.contains("content");
      const bool hasEdits = args.contains("edits");
      if (hasContent == hasEdits)
        throw ToolError("file_write: provide exactly one of 'content' or 'edits'");
      const auto baseVersion = optionalString(args, "baseVersion");

      FileChange change;
      if (hasContent) {
        change = session.files().write(path, args["content"].get<std::string>(), baseVersion);
      } else {
        std::vector<TextEdit> edits;
        for (const auto& edit : args["edits"]) {
          edits.push_back(TextEdit{edit["old"].get<std::string>(), edit["new"].get<std::string>(),
                                   edit.value("replaceAll", false)});
        }
        change = session.files().patch(path, edits, baseVersion);
      }
      session.fileWritten(change);
      return ToolResult::data(change.toJson());
    }});

  tools.add(
    ToolDef{"file_rename", "Rename or move a project file (the target must not exist).",
            json{{"type", "object"},
                 {"properties", json{{"project", projectArgSchema()},
                                     {"from", pathSchema("Current path relative to the project root.")},
                                     {"to", pathSchema("New path relative to the project root.")}}},
                 {"required", json::array({"from", "to"})},
                 {"additionalProperties", false}},
            [&sessions](const json& args) {
              auto& session = sessions.resolve(args);
              const std::string from = args["from"].get<std::string>();
              const std::string to = args["to"].get<std::string>();
              session.files().rename(from, to);
              session.fileRenamed(from, to);
              return ToolResult::data(json{{"from", from}, {"to", to}});
            }});

  tools.add(
    ToolDef{"file_delete", "Delete a project file.",
            json{{"type", "object"},
                 {"properties", json{{"project", projectArgSchema()},
                                     {"path", pathSchema("File path relative to the project root.")}}},
                 {"required", json::array({"path"})},
                 {"additionalProperties", false}},
            [&sessions](const json& args) {
              auto& session = sessions.resolve(args);
              const std::string path = args["path"].get<std::string>();
              session.files().remove(path);
              session.fileRemoved(path);
              return ToolResult::data(json{{"path", path}, {"deleted", true}});
            }});
}

}  // namespace mcp
