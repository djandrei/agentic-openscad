#include "mcp/tools/CoreTools.h"

#include <string>

#include "mcp/BuildInfo.h"
#include "version.h"

namespace mcp {

using nlohmann::json;

json projectArgSchema()
{
  return json{{"type", "string"},
              {"description",
               "Project folder (as returned by project_open). Defaults to the current project when "
               "only one is open or one was used last."}};
}

std::string serverInstructions()
{
  return "Agentic-OpenSCAD gives you control of an OpenSCAD instance. Work happens inside a "
         "project: a folder on disk opened with project_open, which creates "
         ".openscad-project.json and designates a main file (main.scad by default). All file "
         "paths you pass are relative to the project root and must stay inside it. Use status "
         "to see the open projects and the current mode (gui or headless). Call tools/list for "
         "the full tool set; the OpenSCAD language itself is unchanged.";
}

void registerCoreTools(ToolRegistry& tools, SessionManager& sessions)
{
  tools.add(
    ToolDef{"version",
            "Report the OpenSCAD version, the MCP server name and protocol version, and the run mode.",
            json{{"type", "object"}, {"properties", json::object()}, {"additionalProperties", false}},
            [&sessions](const json&) {
              return ToolResult::data(json{{"openscad", openscad_versionnumber},
                                           {"server", serverName()},
                                           {"protocolVersion", protocolVersion()},
                                           {"mode", sessions.mode()}});
            }});

  tools.add(ToolDef{
    "status", "Report the run mode, the open projects and which one is current.",
    json{{"type", "object"}, {"properties", json::object()}, {"additionalProperties", false}},
    [&sessions](const json&) {
      json projects = json::array();
      for (auto *session : sessions.sessions()) {
        json entry = session->project().infoJson();
        entry.erase("config");
        entry.update(session->status());
        projects.push_back(entry);
      }
      json result{{"mode", sessions.mode()}, {"projects", projects}};
      result["current"] = sessions.current() ? json(sessions.current()->project().id()) : json(nullptr);
      return ToolResult::data(result);
    }});

  tools.add(ToolDef{
    "project_open",
    "Open a folder as a project (creating .openscad-project.json if missing) and make it the "
    "current project. In GUI mode this shows the project in a window. Returns the project info.",
    json{{"type", "object"},
         {"properties",
          json{{"path", json{{"type", "string"},
                             {"description", "Absolute path of the project folder (must exist)."}}}}},
         {"required", json::array({"path"})},
         {"additionalProperties", false}},
    [&sessions](const json& args) {
      auto& session = sessions.open(args["path"].get<std::string>());
      json info = session.project().infoJson();
      info.update(session.status());
      return ToolResult::data(info);
    }});

  tools.add(
    ToolDef{"project_list", "List the open projects.",
            json{{"type", "object"}, {"properties", json::object()}, {"additionalProperties", false}},
            [&sessions](const json&) {
              json list = json::array();
              for (auto *session : sessions.sessions()) {
                json entry = session->project().infoJson();
                entry.erase("config");
                list.push_back(entry);
              }
              return ToolResult::data(json{{"projects", list}});
            }});

  tools.add(
    ToolDef{"project_info",
            "Describe a project: root folder, main file, and the contents of .openscad-project.json.",
            json{{"type", "object"},
                 {"properties", json{{"project", projectArgSchema()}}},
                 {"additionalProperties", false}},
            [&sessions](const json& args) {
              auto& session = sessions.resolve(args);
              json info = session.project().infoJson();
              info.update(session.status());
              return ToolResult::data(info);
            }});
}

}  // namespace mcp
