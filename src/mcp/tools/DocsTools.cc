#include "mcp/tools/DocsTools.h"

#include <optional>
#include <string>

#include "mcp/docs/Docs.h"
#include "mcp/library/Registry.h"
#include "mcp/tools/CoreTools.h"

namespace mcp {

using nlohmann::json;

namespace {

constexpr size_t kDefaultPageChars = 40000;

ProjectSession *currentSession(SessionManager& sessions, const json& args)
{
  if (args.contains("project") || sessions.current()) {
    try {
      return &sessions.resolve(args);
    } catch (const ToolError&) {
      return nullptr;
    }
  }
  return nullptr;
}

json pageResult(const std::string& id, std::string text, size_t maxChars)
{
  const bool truncated = text.size() > maxChars;
  if (truncated) text.resize(maxChars);
  return json{{"id", id}, {"text", text}, {"truncated", truncated}};
}

const char *kLanguageRules =
  "OpenSCAD language rules that matter when generating code:\n"
  "- Statements end with ';'. Module definitions `module m() { ... }` and blocks do not.\n"
  "- Variables are set once per scope at compile time: the last assignment in a scope wins, even "
  "if it appears later in the file; there is no mutation inside loops. Use `let(...)` or function "
  "recursion instead.\n"
  "- Transformations and boolean operations are modifiers: `translate([1,0,0]) cube(5);` applies "
  "to the next statement or block; they cannot be stored in variables.\n"
  "- `difference()` subtracts every child after the first; make subtracted shapes slightly longer "
  "than the walls they cut (e.g. `translate([0,0,-1]) cylinder(h = thickness + 2, ...)`) to avoid "
  "coincident faces.\n"
  "- 2D shapes (square, circle, polygon, text, offset) and 3D shapes do not mix; extrude 2D with "
  "linear_extrude/rotate_extrude, flatten 3D with projection().\n"
  "- Resolution: set `$fn` (e.g. 64) for round parts; it affects render time. Use `$preview` to "
  "reduce detail in preview.\n"
  "- Functions are pure expressions: `function f(x) = x * 2;`. Lists use `[a, b, c]`, ranges "
  "`[start:step:end]`, list comprehensions `[for (i = [0:3]) i * 2]`, and `each`.\n"
  "- `echo()` prints to the log (visible in preview/render messages); `assert()` stops with an error.\n"
  "- Customizer: top-level assignments before the first module become parameters; comments like "
  "`// [1:100]` or `// [a, b, c]` define ranges and options; `/* [Group] */` groups them.\n"
  "- Libraries: `use <lib/file.scad>` imports modules and functions; `include <...>` also pulls in "
  "variables and top-level geometry. The project's `libraries` folder is searched first.\n"
  "- Meshes from polyhedron() need outward-facing (clockwise from outside) faces; render fails on "
  "non-manifold input.";

const char *kWorkflow =
  "Recommended loop with this server:\n"
  "1. project_open the folder; project_info shows the main file.\n"
  "2. Write code with file_write (whole content or exact edits; pass baseVersion from file_read).\n"
  "3. check (syntax) then preview: read every error/warning with its line and fix it.\n"
  "4. view_capture from a few presets (diagonal, top, front) to see the shape; adjust.\n"
  "5. render, then geometry_info for volume, bounding box and manifoldness; export when done.\n"
  "6. For reusable parts, params_get/params_set expose customizer parameters; library_catalog and "
  "library_install add libraries; docs_search/docs_builtin answer language questions.";

}  // namespace

void registerDocsTools(ToolRegistry& tools, SessionManager& sessions)
{
  tools.add(ToolDef{
    "docs_search",
    "Search the OpenSCAD documentation: builtin reference (signatures and semantics), the offline "
    "User Manual and cheat sheet (downloaded once with docs_update), and the README/markdown files "
    "of the project's installed libraries. Returns ranked hits with snippets; read a hit with "
    "docs_page.",
    json{
      {"type", "object"},
      {"properties",
       json{
         {"project", projectArgSchema()},
         {"query", json{{"type", "string"}, {"minLength", 1}}},
         {"scope",
          json{{"type", "string"},
               {"enum", json::array({"all", "manual", "cheatsheet", "builtins", "libraries"})}}},
         {"limit",
          json{{"type", "integer"}, {"minimum", 1}, {"maximum", 50}, {"description", "Default 8."}}}}},
      {"required", json::array({"query"})},
      {"additionalProperties", false}},
    [&sessions](const json& args) {
      ProjectSession *session = currentSession(sessions, args);
      return ToolResult::data(
        docs::Docs::instance().search(args["query"].get<std::string>(), args.value("scope", "all"),
                                      args.value("limit", 8), session ? &session->project() : nullptr));
    }});

  tools.add(ToolDef{
    "docs_page",
    "Read a documentation page by id: 'builtins' (all builtins), 'builtins/<name>', 'cheatsheet', "
    "'manual' (list of pages), 'manual/<Page>' (e.g. manual/Primitive_Solids) or "
    "'library/<name>/<file>' for an installed library's documentation.",
    json{{"type", "object"},
         {"properties", json{{"project", projectArgSchema()},
                             {"id", json{{"type", "string"}, {"minLength", 1}}},
                             {"maxChars", json{{"type", "integer"},
                                               {"minimum", 500},
                                               {"maximum", 200000},
                                               {"description", "Default 40000."}}}}},
         {"required", json::array({"id"})},
         {"additionalProperties", false}},
    [&sessions](const json& args) {
      const std::string id = args["id"].get<std::string>();
      const size_t maxChars = args.value("maxChars", kDefaultPageChars);
      if (id.rfind("library/", 0) == 0) {
        ProjectSession *session = currentSession(sessions, args);
        if (!session) throw ToolError("open a project first to read library documentation");
        auto text = docs::Docs::instance().libraryDocument(session->project(), id.substr(8));
        if (!text) throw ToolError("unknown library document: " + id);
        return ToolResult::data(pageResult(id, *text, maxChars));
      }
      auto text = docs::Docs::instance().page(id);
      if (!text)
        throw ToolError("unknown documentation page: " + id + " (see docs_page 'manual' for the list)");
      return ToolResult::data(pageResult(id, *text, maxChars));
    }});

  tools.add(ToolDef{
    "docs_builtin",
    "Reference for one builtin module, function, keyword or special variable: call signatures, a "
    "summary of its semantics, and the best matching manual sections.",
    json{{"type", "object"},
         {"properties", json{{"name", json{{"type", "string"}, {"minLength", 1}}}}},
         {"required", json::array({"name"})},
         {"additionalProperties", false}},
    [](const json& args) {
      const std::string name = args["name"].get<std::string>();
      auto& docs = docs::Docs::instance();
      json entry = docs.builtin(name);
      if (entry.is_null())
        throw ToolError("unknown builtin: " + name + " (docs_page 'builtins' lists them)");
      json manualHits = docs.search(name, "manual", 3, nullptr)["hits"];
      entry["manual"] = manualHits;
      return ToolResult::data(entry);
    }});

  tools.add(
    ToolDef{"docs_status", "Report what documentation is available locally and where it is cached.",
            json{{"type", "object"}, {"properties", json::object()}, {"additionalProperties", false}},
            [](const json&) { return ToolResult::data(docs::Docs::instance().status()); }});

  tools.add(ToolDef{
    "docs_update",
    "Download the offline User Manual and the cheat sheet from openscad.org into the local cache "
    "(network access) and rebuild the search index. Needed once before manual searches work.",
    json{{"type", "object"},
         {"properties",
          json{{"force", json{{"type", "boolean"}, {"description", "Re-download even if cached."}}}}},
         {"additionalProperties", false}},
    [](const json& args) {
      return ToolResult::data(docs::Docs::instance().update(args.value("force", false)));
    }});
}

void registerDocsResources(ResourceRegistry& resources, PromptRegistry& prompts,
                           SessionManager& sessions)
{
  resources.add(ResourceDef{
    "openscad://docs/builtins", "OpenSCAD builtins",
    "Signatures and semantics of every builtin module, function and special variable", "text/plain", [] {
      return ResourceContent{"openscad://docs/builtins", "text/plain",
                             docs::Docs::instance().page("builtins").value_or(""), ""};
    }});
  resources.add(ResourceDef{
    "openscad://docs/cheatsheet", "OpenSCAD cheat sheet",
    "The openscad.org cheat sheet as text (after docs_update)", "text/plain", [] {
      return ResourceContent{
        "openscad://docs/cheatsheet", "text/plain",
        docs::Docs::instance().page("cheatsheet").value_or("(not downloaded; call docs_update)"), ""};
    }});
  resources.add(ResourceDef{"openscad://docs/manual", "OpenSCAD User Manual",
                            "List of the offline manual pages (after docs_update)", "text/plain", [] {
                              return ResourceContent{"openscad://docs/manual", "text/plain",
                                                     docs::Docs::instance().page("manual").value_or(""),
                                                     ""};
                            }});
  resources.addTemplate(ResourceTemplateDef{"openscad://docs/manual/{page}", "Manual page",
                                            "One page of the offline User Manual", "text/plain"});
  resources.addTemplate(ResourceTemplateDef{"openscad://docs/builtins/{name}", "Builtin reference",
                                            "Signatures and semantics of one builtin", "text/plain"});
  resources.addTemplate(ResourceTemplateDef{"openscad://library/{name}/{file}", "Library documentation",
                                            "README or markdown file of an installed library",
                                            "text/plain"});
  resources.addResolver([&sessions](const std::string& uri) -> std::optional<ResourceContent> {
    const std::string docsPrefix = "openscad://docs/";
    const std::string libraryPrefix = "openscad://library/";
    if (uri.rfind(docsPrefix, 0) == 0) {
      auto text = docs::Docs::instance().page(uri.substr(docsPrefix.size()));
      if (!text) return std::nullopt;
      return ResourceContent{uri, "text/plain", *text, ""};
    }
    if (uri.rfind(libraryPrefix, 0) == 0 && sessions.current()) {
      auto text = docs::Docs::instance().libraryDocument(sessions.current()->project(),
                                                         uri.substr(libraryPrefix.size()));
      if (!text) return std::nullopt;
      return ResourceContent{uri, "text/plain", *text, ""};
    }
    return std::nullopt;
  });

  prompts.add(PromptDef{
    "openscad-language-rules", "Rules of the OpenSCAD language that commonly trip up generated code",
    json::array(), [](const json&) {
      return json{{"description", "OpenSCAD language rules"},
                  {"messages",
                   json::array({json{{"role", "user"},
                                     {"content", json{{"type", "text"}, {"text", kLanguageRules}}}}})}};
    }});
  prompts.add(PromptDef{
    "openscad-workflow", "How to iterate on a model with this server's tools", json::array(),
    [](const json&) {
      return json{
        {"description", "Agentic-OpenSCAD workflow"},
        {"messages", json::array({json{{"role", "user"},
                                       {"content", json{{"type", "text"}, {"text", kWorkflow}}}}})}};
    }});
  prompts.add(PromptDef{
    "library-usage",
    "How to use a library from the catalog (include line, docs, README excerpt when installed)",
    json::array({json{
      {"name", "name"}, {"description", "Library name from library_catalog"}, {"required", true}}}),
    [&sessions](const json& args) {
      const std::string name = args["name"].get<std::string>();
      std::string text = "Library " + name + ":\n";
      std::vector<std::string> problems;
      const Project *project = sessions.current() ? &sessions.current()->project() : nullptr;
      std::vector<library::LibraryEntry> entries;
      if (project) entries = library::loadRegistries(*project, &problems);
      else entries = library::parseRegistry(json::parse(library::bundledRegistryJson()), "bundled");
      if (const auto *entry = library::findEntry(entries, name)) {
        text +=
          entry->description + "\nRepository: " + entry->repository + "\nDocs: " + entry->docs + "\n";
        if (!entry->usage.empty()) text += "Usage:\n" + entry->usage + "\n";
        text += "Install with library_install(\"" + entry->name + "\") unless bundled.\n";
      } else {
        text += "(not in the catalog)\n";
      }
      if (project) {
        for (const auto& doc : docs::Docs::instance().libraryDocuments(*project)) {
          if (doc.id.rfind("library/" + name + "/", 0) == 0 &&
              doc.title.find("README") != std::string::npos) {
            text +=
              "\nREADME excerpt:\n" + doc.text.substr(0, 4000) + (doc.text.size() > 4000 ? "\n…" : "\n");
            break;
          }
        }
      }
      return json{
        {"description", "Using " + name},
        {"messages",
         json::array({json{{"role", "user"}, {"content", json{{"type", "text"}, {"text", text}}}}})}};
    }});
}

}  // namespace mcp
