#include "mcp/tools/CompileTools.h"

#include <filesystem>
#include <stdexcept>
#include <string>

#include "mcp/engine/CompileEngine.h"
#include "mcp/session/MessageLog.h"
#include "mcp/tools/CoreTools.h"

namespace mcp {

using nlohmann::json;

namespace {

constexpr size_t kMaxDumpChars = 200000;

json fileArgSchema()
{
  return json{{"type", "string"},
              {"description",
               "File to compile, relative to the project root. Defaults to the project's main file."}};
}

json timeoutSchema()
{
  return json{{"type", "number"},
              {"minimum", 1},
              {"maximum", 3600},
              {"description", "Seconds before evaluation is cancelled. Default 120."}};
}

json compileSchema(bool withTimeout)
{
  json properties{{"project", projectArgSchema()}, {"file", fileArgSchema()}};
  if (withTimeout) properties["timeoutSeconds"] = timeoutSchema();
  return json{{"type", "object"}, {"properties", properties}, {"additionalProperties", false}};
}

// Message paths relative to the project where possible.
json messagesJson(const CompileOutcome& outcome, const Project& project)
{
  json list = json::array();
  for (const auto& message : outcome.messages) {
    json j = message.toJson();
    if (!message.file.empty()) {
      const std::filesystem::path path(message.file);
      const auto relative = path.lexically_relative(project.root());
      if (!relative.empty() && *relative.begin() != "..") j["file"] = relative.generic_string();
    }
    list.push_back(j);
  }
  return list;
}

ToolResult runCompile(SessionManager& sessions, const json& args, CompileMode mode, const char *modeName)
{
  auto& session = sessions.resolve(args);
  CompileRequest request;
  request.file = args.value("file", session.project().mainFile());
  request.mode = mode;
  request.timeoutSeconds = args.value("timeoutSeconds", 120.0);
  const CompileOutcome outcome = session.compile(request);

  json result{{"ok", outcome.ok},
              {"mode", modeName},
              {"file", request.file},
              {"parsed", outcome.parsed},
              {"errors", outcome.errors},
              {"warnings", outcome.warnings},
              {"timedOut", outcome.timedOut},
              {"unsavedEditor", outcome.unsavedEditor},
              {"millis", outcome.millis},
              {"messages", messagesJson(outcome, session.project())}};
  if (!outcome.geometry.is_null()) result["geometry"] = outcome.geometry;
  return ToolResult::data(result);
}

ToolResult runDump(SessionManager& sessions, const json& args, CompileMode mode,
                   std::string EngineResult::*field, const char *kind)
{
  auto& session = sessions.resolve(args);
  const std::string file = args.value("file", session.project().mainFile());
  std::filesystem::path path;
  try {
    path = session.project().resolve(file);
  } catch (const std::invalid_argument& e) {
    throw ToolError(e.what());
  }
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) throw ToolError("file not found: " + file);

  const auto firstSeq = MessageLog::instance().lastSeq();
  const EngineResult result = CompileEngine::run(path, mode, args.value("timeoutSeconds", 120.0), true);
  const auto messages = MessageLog::instance().since(firstSeq);
  const auto counts = MessageLog::count(messages);
  std::string text = result.*field;
  const bool truncated = text.size() > kMaxDumpChars;
  if (truncated) text.resize(kMaxDumpChars);
  json out{{"kind", kind},
           {"file", file},
           {"parsed", result.parsed},
           {"errors", counts.errors},
           {"warnings", counts.warnings},
           {"truncated", truncated},
           {"text", text}};
  return ToolResult::data(out);
}

}  // namespace

void registerCompileTools(ToolRegistry& tools, SessionManager& sessions)
{
  tools.add(ToolDef{
    "check",
    "Parse a file without evaluating it. Fast syntax check; returns parser errors with line and "
    "column.",
    compileSchema(false),
    [&sessions](const json& args) { return runCompile(sessions, args, CompileMode::Check, "check"); }});

  tools.add(ToolDef{
    "preview",
    "Evaluate a file the way F5 does: parse, run the script and build the CSG tree. Returns every "
    "message of the run (errors, warnings, echo output) with file/line, counts and timing. In GUI "
    "mode the result is shown in the 3D view.",
    compileSchema(true), [&sessions](const json& args) {
      return runCompile(sessions, args, CompileMode::Preview, "preview");
    }});

  tools.add(ToolDef{
    "render",
    "Evaluate a file the way F6 does: full geometry evaluation. Slower than preview but yields the "
    "final mesh; the result includes a geometry summary (dimension, facets, bounding box) besides "
    "the messages. In GUI mode the mesh is shown in the 3D view.",
    compileSchema(true), [&sessions](const json& args) {
      return runCompile(sessions, args, CompileMode::Render, "render");
    }});

  tools.add(ToolDef{"ast_dump",
                    "Return the parsed abstract syntax tree of a file as OpenSCAD source text.",
                    compileSchema(false), [&sessions](const json& args) {
                      return runDump(sessions, args, CompileMode::Check, &EngineResult::ast, "ast");
                    }});

  tools.add(ToolDef{
    "csg_tree_dump",
    "Evaluate a file and return the resulting CSG tree (the .csg export: every primitive with its "
    "resolved parameters and transforms).",
    compileSchema(true), [&sessions](const json& args) {
      return runDump(sessions, args, CompileMode::Preview, &EngineResult::csgTree, "csg_tree");
    }});

  tools.add(ToolDef{
    "csg_products_dump",
    "Evaluate a file and return the normalized CSG products (the .term export) used for preview.",
    compileSchema(true), [&sessions](const json& args) {
      return runDump(sessions, args, CompileMode::Preview, &EngineResult::csgProducts, "csg_products");
    }});

  tools.add(ToolDef{
    "log_get",
    "Return OpenSCAD's log messages with sequence numbers greater than `since` (all modes, all "
    "projects), oldest first. Use the last `seq` you saw to page.",
    json{{"type", "object"},
         {"properties",
          json{{"since", json{{"type", "integer"}, {"minimum", 0}, {"description", "Default 0."}}},
               {"limit", json{{"type", "integer"},
                              {"minimum", 1},
                              {"maximum", 2000},
                              {"description", "Default 500."}}}}},
         {"additionalProperties", false}},
    [](const json& args) {
      auto& log = MessageLog::instance();
      const auto messages = log.since(args.value("since", 0), args.value("limit", 500));
      json list = json::array();
      for (const auto& message : messages) list.push_back(message.toJson());
      return ToolResult::data(json{{"messages", list}, {"lastSeq", log.lastSeq()}});
    }});
}

}  // namespace mcp
