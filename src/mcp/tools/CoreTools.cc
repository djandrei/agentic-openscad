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
         "paths you pass are relative to the project root and must stay inside it. Files on disk "
         "are the source of truth: write them with file_write (whole content or exact edits, with "
         "baseVersion from file_read to detect concurrent changes); in GUI mode the user sees "
         "every change in the editor. Use status to see the open projects, the run mode (gui or "
         "headless) and whether the user paused you. Typical loop: file_write, check, preview "
         "(read the messages), view_capture to look at the model, render, geometry_info, export. "
         "docs_builtin/docs_search/docs_page answer OpenSCAD language questions; library_catalog and "
         "library_install add libraries; params_* drive the customizer. The OpenSCAD language itself "
         "is unchanged.";
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
        entry["mode"] = sessions.mode();
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
      info["mode"] = sessions.mode();
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
              info["mode"] = sessions.mode();
              return ToolResult::data(info);
            }});

  tools.add(ToolDef{
    "project_set",
    "Change project settings in .openscad-project.json: the main file (the one previewed and "
    "rendered by default) and autoPreview (GUI: preview after each write to the main file).",
    json{{"type", "object"},
         {"properties", json{{"project", projectArgSchema()},
                             {"main", json{{"type", "string"},
                                           {"description", "Main .scad file, relative to the root."}}},
                             {"autoPreview", json{{"type", "boolean"}}}}},
         {"additionalProperties", false}},
    [&sessions](const json& args) {
      auto& session = sessions.resolve(args);
      auto& project = session.project();
      if (args.contains("main")) {
        const std::string main = args["main"].get<std::string>();
        try {
          project.config()["main"] = project.relative(project.resolve(main));
        } catch (const std::invalid_argument& e) {
          throw ToolError(e.what());
        }
      }
      if (args.contains("autoPreview"))
        project.config()["autoPreview"] = args["autoPreview"].get<bool>();
      project.save();
      return ToolResult::data(project.infoJson());
    }});
}

}  // namespace mcp
