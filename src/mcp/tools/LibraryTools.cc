#include "mcp/tools/LibraryTools.h"

#include <string>

#include "mcp/library/LibraryManager.h"
#include "mcp/tools/CoreTools.h"

namespace mcp {

using nlohmann::json;

void registerLibraryTools(ToolRegistry& tools, SessionManager& sessions)
{
  tools.add(ToolDef{
    "library_catalog",
    "List the OpenSCAD libraries available for installation (bundled registry seeded from "
    "openscad.org plus the project's registries) with description, repository, docs URL and the "
    "include/use line to write. `installed` tells whether the project already has it.",
    json{{"type", "object"},
         {"properties", json{{"project", projectArgSchema()},
                             {"query", json{{"type", "string"},
                                            {"description", "Substring of name or description."}}}}},
         {"additionalProperties", false}},
    [&sessions](const json& args) {
      auto& session = sessions.resolve(args);
      return ToolResult::data(
        library::LibraryManager(session.project()).catalog(args.value("query", "")));
    }});

  tools.add(ToolDef{"library_installed",
                    "List the libraries installed in the project's libraries folder.",
                    json{{"type", "object"},
                         {"properties", json{{"project", projectArgSchema()}}},
                         {"additionalProperties", false}},
                    [&sessions](const json& args) {
                      auto& session = sessions.resolve(args);
                      return ToolResult::data(library::LibraryManager(session.project()).installed());
                    }});

  tools.add(ToolDef{
    "library_install",
    "Install a library into <project>/libraries/<name> so `use <name/file.scad>` works. `library` is "
    "a catalog name (e.g. BOSL2), a git repository URL on GitHub/GitLab/Codeberg (archive download) "
    "or elsewhere (git clone), or a zip URL/path. Reinstalls if already present.",
    json{{"type", "object"},
         {"properties",
          json{{"project", projectArgSchema()},
               {"library",
                json{{"type", "string"}, {"description", "Catalog name, repository URL or zip."}}},
               {"ref", json{{"type", "string"}, {"description", "Branch or tag; default main/master."}}},
               {"dirName", json{{"type", "string"}, {"description", "Folder name under libraries/."}}}}},
         {"required", json::array({"library"})},
         {"additionalProperties", false}},
    [&sessions](const json& args) {
      auto& session = sessions.resolve(args);
      return ToolResult::data(library::LibraryManager(session.project())
                                .install(args["library"].get<std::string>(), args.value("ref", ""),
                                         args.value("dirName", "")));
    }});

  tools.add(ToolDef{
    "library_remove", "Remove an installed library folder and forget it in the project config.",
    json{{"type", "object"},
         {"properties", json{{"project", projectArgSchema()}, {"name", json{{"type", "string"}}}}},
         {"required", json::array({"name"})},
         {"additionalProperties", false}},
    [&sessions](const json& args) {
      auto& session = sessions.resolve(args);
      return ToolResult::data(
        library::LibraryManager(session.project()).remove(args["name"].get<std::string>()));
    }});

  tools.add(ToolDef{
    "library_files",
    "List the files of an installed library. With `describe`, parse its .scad files and report the "
    "top-level modules and functions with their parameter names (use `filter` to narrow to a file).",
    json{{"type", "object"},
         {"properties",
          json{{"project", projectArgSchema()},
               {"name", json{{"type", "string"}}},
               {"filter", json{{"type", "string"}, {"description", "Substring of the file path."}}},
               {"describe", json{{"type", "boolean"}}}}},
         {"required", json::array({"name"})},
         {"additionalProperties", false}},
    [&sessions](const json& args) {
      auto& session = sessions.resolve(args);
      return ToolResult::data(library::LibraryManager(session.project())
                                .files(args["name"].get<std::string>(), args.value("filter", ""),
                                       args.value("describe", false)));
    }});

  tools.add(ToolDef{"library_registry_list", "List the library registries the project consults.",
                    json{{"type", "object"},
                         {"properties", json{{"project", projectArgSchema()}}},
                         {"additionalProperties", false}},
                    [&sessions](const json& args) {
                      auto& session = sessions.resolve(args);
                      return ToolResult::data(library::LibraryManager(session.project()).registries());
                    }});

  tools.add(ToolDef{
    "library_registry_add",
    "Add a registry (URL or project-relative path of a JSON file with {\"name\", \"libraries\": "
    "[{name, repository, description, ref, docs, usage}]}) to the project; its entries extend or "
    "override the bundled catalog.",
    json{{"type", "object"},
         {"properties", json{{"project", projectArgSchema()}, {"location", json{{"type", "string"}}}}},
         {"required", json::array({"location"})},
         {"additionalProperties", false}},
    [&sessions](const json& args) {
      auto& session = sessions.resolve(args);
      return ToolResult::data(
        library::LibraryManager(session.project()).addRegistry(args["location"].get<std::string>()));
    }});
}

}  // namespace mcp
