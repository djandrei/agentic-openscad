#include "mcp/tools/ParamTools.h"

#include <string>

#include "mcp/tools/CoreTools.h"

namespace mcp {

using nlohmann::json;

namespace {

json fileSchema()
{
  return json{
    {"type", "string"},
    {"description", "File relative to the project root; defaults to the project's main file."}};
}

std::string fileOf(const json& args, ProjectSession& session)
{
  return args.value("file", session.project().mainFile());
}

}  // namespace

void registerParamTools(ToolRegistry& tools, SessionManager& sessions)
{
  tools.add(ToolDef{
    "params_get",
    "List the customizer parameters a file declares (top-level assignments with their customizer "
    "comments: caption, group, range/step or options) with their current values, the active "
    "overrides and the preset names.",
    json{{"type", "object"},
         {"properties", json{{"project", projectArgSchema()}, {"file", fileSchema()}}},
         {"additionalProperties", false}},
    [&sessions](const json& args) {
      auto& session = sessions.resolve(args);
      return ToolResult::data(session.paramsGet(fileOf(args, session)));
    }});

  tools.add(ToolDef{
    "params_set",
    "Set customizer parameter values for a file (numbers, booleans, strings or vectors by "
    "parameter name), select a saved preset, or reset to the design defaults. The values apply to "
    "the next preview/render; in GUI mode the Customizer panel shows them. Returns params_get.",
    json{{"type", "object"},
         {"properties",
          json{{"project", projectArgSchema()},
               {"file", fileSchema()},
               {"values", json{{"type", "object"}, {"description", "Parameter name to value."}}},
               {"preset",
                json{{"type", "string"}, {"description", "Preset to load before applying values."}}},
               {"reset", json{{"type", "boolean"}, {"description", "Drop all overrides first."}}}}},
         {"additionalProperties", false}},
    [&sessions](const json& args) {
      auto& session = sessions.resolve(args);
      return ToolResult::data(
        session.paramsSet(fileOf(args, session), args.value("values", json::object()),
                          args.value("preset", std::string()), args.value("reset", false)));
    }});

  tools.add(ToolDef{"params_presets_list",
                    "List the customizer presets stored next to a file (<file>.json) with their values.",
                    json{{"type", "object"},
                         {"properties", json{{"project", projectArgSchema()}, {"file", fileSchema()}}},
                         {"additionalProperties", false}},
                    [&sessions](const json& args) {
                      auto& session = sessions.resolve(args);
                      return ToolResult::data(session.presetsList(fileOf(args, session)));
                    }});

  tools.add(
    ToolDef{"params_preset_save",
            "Save the current parameter values as a named preset in <file>.json (replacing a preset of "
            "the same name).",
            json{{"type", "object"},
                 {"properties", json{{"project", projectArgSchema()},
                                     {"file", fileSchema()},
                                     {"name", json{{"type", "string"}, {"minLength", 1}}}}},
                 {"required", json::array({"name"})},
                 {"additionalProperties", false}},
            [&sessions](const json& args) {
              auto& session = sessions.resolve(args);
              return ToolResult::data(
                session.presetSave(fileOf(args, session), args["name"].get<std::string>()));
            }});
}

}  // namespace mcp
