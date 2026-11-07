#include "mcp/engine/CompileEngine.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

#include "core/AST.h"
#include "core/BuiltinContext.h"
#include "core/CSGNode.h"
#include "core/CSGTreeEvaluator.h"
#include "core/Context.h"
#include "core/EvaluationSession.h"
#include "core/RenderVariables.h"
#include "core/SourceFile.h"
#include "core/Tree.h"
#include "core/node.h"
#include "core/parsersettings.h"
#include "core/progress.h"
#include "geometry/Geometry.h"
#include "geometry/GeometryEvaluator.h"
#include "glview/Camera.h"
#include "openscad.h"
#include "utils/exceptions.h"
#include "utils/printutils.h"

namespace fs = std::filesystem;

namespace mcp {

namespace {

using Clock = std::chrono::steady_clock;

double millisSince(Clock::time_point start)
{
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct Deadline {
  Clock::time_point at;
};

// Progress callback: OpenSCAD calls it per node; throwing ProgressCancelException
// aborts the evaluation cleanly, which is how timeouts are enforced.
void progressCallback(const std::shared_ptr<const AbstractNode>&, void *userdata, int)
{
  const auto *deadline = static_cast<const Deadline *>(userdata);
  if (Clock::now() > deadline->at) throw ProgressCancelException();
}

// Restores the working directory on scope exit; the pipeline chdirs to the
// document folder like the command line does so relative imports resolve.
struct CwdGuard {
  fs::path previous;
  std::error_code ec;
  CwdGuard() : previous(fs::current_path(ec)) {}
  ~CwdGuard()
  {
    if (!previous.empty()) fs::current_path(previous, ec);
  }
};

}  // namespace

nlohmann::json CompileEngine::geometrySummary(const Geometry *geometry)
{
  if (!geometry) return nlohmann::json{{"empty", true}};
  nlohmann::json summary{{"dimension", geometry->getDimension()},
                         {"empty", geometry->isEmpty()},
                         {"facets", geometry->numFacets()}};
  const auto box = geometry->getBoundingBox();
  if (!box.isEmpty()) {
    summary["boundingBox"] =
      nlohmann::json{{"min", {box.min().x(), box.min().y(), box.min().z()}},
                     {"max", {box.max().x(), box.max().y(), box.max().z()}},
                     {"size", {box.sizes().x(), box.sizes().y(), box.sizes().z()}}};
  }
  return summary;
}

EngineResult CompileEngine::run(const fs::path& file, CompileMode mode, double timeoutSeconds,
                                bool wantDumps)
{
  EngineResult result;
  const auto started = Clock::now();
  Deadline deadline{started + std::chrono::milliseconds(static_cast<long long>(timeoutSeconds * 1000))};

  std::string text;
  {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
      LOG(message_group::Error, "Can't open input file '%1$s'", file.string());
      return result;
    }
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  text += "\n\x03\n" + commandline_commands;

  resetSuppressedMessages();
  const std::string filename = file.string();
  SourceFile *parsed = nullptr;
  if (!parse(parsed, text, filename, filename, false)) {
    delete parsed;
    parsed = nullptr;
  }
  result.parseMs = millisSince(started);
  if (!parsed) return result;
  std::shared_ptr<SourceFile> rootFile(parsed);
  result.sourceFile = rootFile;
  result.parsed = true;
  rootFile->handleDependencies();
  if (wantDumps) result.ast = rootFile->dump("");
  if (mode == CompileMode::Check) return result;

  const fs::path parent = file.parent_path();
  std::shared_ptr<const AbstractNode> rootNode;
  std::shared_ptr<AbstractNode> absoluteRoot;
  const auto evaluateStart = Clock::now();
  {
    CwdGuard cwd;
    std::error_code ec;
    fs::current_path(parent, ec);

    EvaluationSession session{parent.string()};
    ContextHandle<BuiltinContext> builtinContext{Context::create<BuiltinContext>(&session)};
    RenderVariables renderVariables;
    renderVariables.preview = mode == CompileMode::Preview;
    renderVariables.time = 0;
    renderVariables.camera = Camera();
    renderVariables.applyToContext(builtinContext);

    AbstractNode::resetIndexCounter();
    std::shared_ptr<const FileContext> fileContext;
    try {
      absoluteRoot = rootFile->instantiate(*builtinContext, &fileContext);
    } catch (const HardWarningException&) {
      LOG("Execution aborted");
      return result;
    }
    if (absoluteRoot) {
      const Location *nextLocation = nullptr;
      std::shared_ptr<AbstractNode> tagged = find_root_tag(absoluteRoot, &nextLocation);
      rootNode = tagged ? tagged : absoluteRoot;
      if (nextLocation) {
        LOG(message_group::Warning, *nextLocation, builtinContext->documentRoot(),
            "More than one Root Modifier (!)");
      }
    }
  }
  result.evaluateMs = millisSince(evaluateStart);
  if (!rootNode) {
    LOG(message_group::Error, "Compilation failed! (no top level object found)");
    return result;
  }
  result.instantiated = true;
  result.rootNode = rootNode;
  result.documentPath = parent.string();

  Tree tree(rootNode, parent.string());
  const auto geometryStart = Clock::now();
  progress_report_prep(absoluteRoot, &progressCallback, &deadline);
  try {
    GeometryEvaluator geometryEvaluator(tree);
    if (mode == CompileMode::Preview) {
      CSGTreeEvaluator csgEvaluator(tree, &geometryEvaluator);
      auto csgRoot = csgEvaluator.buildCSGTree(*rootNode);
      if (wantDumps) {
        result.csgTree = tree.getString(*rootNode, "\t");
        result.csgProducts =
          (!csgRoot || csgRoot->isEmptySet()) ? "No top-level CSG object" : csgRoot->dump();
      }
    } else {
      result.geometry = geometryEvaluator.evaluateGeometry(*rootNode, true);
      if (wantDumps) result.csgTree = tree.getString(*rootNode, "\t");
    }
  } catch (const ProgressCancelException&) {
    result.timedOut = true;
    LOG(message_group::Error, "Evaluation cancelled after %1$.0f seconds (timeout)", timeoutSeconds);
  } catch (const HardWarningException&) {
    LOG("Execution aborted");
  }
  progress_report_fin();
  result.geometryMs = millisSince(geometryStart);
  return result;
}

}  // namespace mcp
