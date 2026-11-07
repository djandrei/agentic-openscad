#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "json/json.hpp"

class Geometry;

namespace mcp {

enum class CompileMode {
  Check,    // parse only
  Preview,  // parse, instantiate, CSG tree (what F5 evaluates before drawing)
  Render    // parse, instantiate, full geometry evaluation (F6)
};

struct EngineResult {
  bool parsed = false;
  bool instantiated = false;
  bool timedOut = false;
  std::shared_ptr<const Geometry> geometry;  // Render only
  std::string ast;                           // when dumps requested (Check and later)
  std::string csgTree;                       // when dumps requested (Preview and later)
  std::string csgProducts;                   // when dumps requested (Preview and later)
  double parseMs = 0;
  double evaluateMs = 0;
  double geometryMs = 0;
};

// The OpenSCAD pipeline without any GUI: the same steps the command line
// runs for an export. Messages are printed through printutils as usual, so
// MessageLog captures them. Usable from both the headless session and, for
// check and dumps, from the GUI session.
class CompileEngine
{
public:
  static EngineResult run(const std::filesystem::path& file, CompileMode mode, double timeoutSeconds,
                          bool wantDumps = false);

  // Summary of a geometry for tool results: dimension, emptiness, facets,
  // bounding box.
  static nlohmann::json geometrySummary(const Geometry *geometry);
};

}  // namespace mcp
