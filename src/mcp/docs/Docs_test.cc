#include "mcp/docs/Docs.h"

#include <catch2/catch_all.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/Builtins.h"
#include "mcp/docs/Html.h"
#include "mcp/docs/SearchIndex.h"
#include "mcp/testing/TempDir.h"

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

// setenv/unsetenv are POSIX; MinGW has _putenv_s, where an empty value removes the variable.
void setEnv(const char *name, const std::string& value)
{
#ifdef _WIN32
  _putenv_s(name, value.c_str());
#else
  setenv(name, value.c_str(), 1);
#endif
}

void unsetEnv(const char *name)
{
#ifdef _WIN32
  _putenv_s(name, "");
#else
  unsetenv(name);
#endif
}

}  // namespace

TEST_CASE("htmlToText strips markup, decodes entities and keeps structure", "[mcp][docs]")
{
  const std::string html =
    "<html><head><title>Primitive Solids - Manual</title><style>p{}</style></head>"
    "<body><script>var x = 1;</script><h2>cube</h2><p>Creates a <b>cube</b> &amp; more&nbsp;text.</p>"
    "<pre>cube([1,\n 2, 3]);</pre><ul><li>one</li><li>two</li></ul><!-- comment --></body></html>";
  const std::string text = mcp::docs::htmlToText(html);
  REQUIRE(text.find("var x") == std::string::npos);
  REQUIRE(text.find("p{}") == std::string::npos);
  REQUIRE(text.find("## cube") != std::string::npos);
  REQUIRE(text.find("Creates a cube & more text.") != std::string::npos);
  REQUIRE(text.find("cube([1,\n 2, 3]);") != std::string::npos);
  REQUIRE(text.find("one\ntwo") != std::string::npos);
  REQUIRE(text.find("comment") == std::string::npos);
  REQUIRE(mcp::docs::htmlTitle(html) == "Primitive Solids - Manual");
  REQUIRE(mcp::docs::htmlTitle("<h1 class='x'>Heading &lt;1&gt;</h1><title>t</title>") == "Heading <1>");
}

TEST_CASE("SearchIndex ranks documents by term relevance", "[mcp][docs]")
{
  mcp::docs::SearchIndex index;
  index.add(
    {"a", "Primitive Solids", "manual", "cube creates a cube. sphere creates a sphere. cylinder too."});
  index.add({"b", "Extrusion", "manual", "linear_extrude extrudes 2D shapes with twist and scale."});
  index.add({"c", "$fn", "builtins", "$fn sets the number of fragments for circles."});
  REQUIRE(index.size() == 3);
  REQUIRE(mcp::docs::SearchIndex::tokenize("linear_extrude($fn=12, Twist)") ==
          std::vector<std::string>{"linear_extrude", "$fn", "12", "twist"});

  auto hits = index.search("linear_extrude twist", "all", 5);
  REQUIRE_FALSE(hits.empty());
  REQUIRE(hits[0].id == "b");
  REQUIRE(hits[0].snippet.find("twist") != std::string::npos);

  auto scoped = index.search("cube", "builtins", 5);
  REQUIRE(scoped.empty());
  auto fn = index.search("$fn", "all", 5);
  REQUIRE(fn.size() == 1);
  REQUIRE(fn[0].id == "c");
  REQUIRE(index.search("", "all", 5).empty());

  index.add({"a", "Primitive Solids", "manual", "nothing here"});
  REQUIRE(index.size() == 3);
  REQUIRE(index.search("cube", "all", 5).empty());
}

TEST_CASE("Docs serves builtins and a cached manual", "[mcp][docs]")
{
  Builtins::initialize();
  mcp::testing::TempDir cache;
  fs::create_directories(cache.path / "manual");
  {
    std::ofstream out(cache.path / "manual" / "Primitive_Solids.html");
    out << "<html><head><title>Primitive Solids</title></head><body><h1>Primitive Solids</h1>"
           "<h2>cube</h2><p>Creates a cube or rectangular prism in the first octant.</p></body></html>";
    std::ofstream cs(cache.path / "cheatsheet.html");
    cs << "<html><body><h1>Cheat Sheet</h1><p>linear_extrude(height, twist)</p></body></html>";
  }
  setEnv("AGENTIC_OPENSCAD_DOCS_CACHE", cache.path.string());
  auto& docs = mcp::docs::Docs::instance();
  docs.update(false);  // cache present: no network needed
  json status = docs.status();
  REQUIRE(status["manualAvailable"] == true);
  REQUIRE(status["manualPages"] == 1);
  REQUIRE(status["cheatsheetAvailable"] == true);
  REQUIRE(status["builtins"].get<int>() > 60);

  json cube = docs.builtin("cube");
  REQUIRE(cube["kind"] == "module");
  REQUIRE(cube["signatures"].size() >= 2);
  REQUIRE(cube["summary"].get<std::string>().find("box") != std::string::npos);
  REQUIRE(docs.builtin("sin")["kind"] == "function");
  REQUIRE(docs.builtin("$fn")["kind"] == "special variable");
  REQUIRE(docs.builtin("nonsense").is_null());

  REQUIRE(docs.manualPages()[0]["id"] == "manual/Primitive_Solids");
  REQUIRE(docs.page("manual/Primitive_Solids")->find("first octant") != std::string::npos);
  REQUIRE(docs.page("manual")->find("Primitive_Solids") != std::string::npos);
  REQUIRE(docs.page("builtins")->find("cylinder") != std::string::npos);
  REQUIRE(docs.page("builtins/translate")->find("Moves children") != std::string::npos);
  REQUIRE_FALSE(docs.page("manual/Nope").has_value());

  json search = docs.search("rectangular prism octant", "manual", 5, nullptr);
  REQUIRE(search["hits"].size() == 1);
  REQUIRE(search["hits"][0]["id"] == "manual/Primitive_Solids");
  REQUIRE_FALSE(search.contains("note"));
  json cheat = docs.search("twist", "cheatsheet", 5, nullptr);
  REQUIRE(cheat["hits"][0]["id"] == "cheatsheet");
  json builtins = docs.search("extrude twist", "builtins", 5, nullptr);
  REQUIRE(builtins["hits"][0]["id"] == "builtins/linear_extrude");
  unsetEnv("AGENTIC_OPENSCAD_DOCS_CACHE");
}
