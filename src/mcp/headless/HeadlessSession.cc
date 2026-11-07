#include "mcp/headless/HeadlessSession.h"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/Tree.h"
#include "mcp/engine/CompileEngine.h"
#include "mcp/engine/ViewEngine.h"
#include "mcp/protocol/Registry.h"

namespace mcp {

HeadlessSession::HeadlessSession(std::shared_ptr<Project> project) : ProjectSession(std::move(project))
{
  camera_.viewall = true;
  camera_.autocenter = true;
}

nlohmann::json HeadlessSession::status() const
{
  return nlohmann::json{{"mode", "headless"},
                        {"busy", false},
                        {"paused", false},
                        {"hasGeometry", last_.geometry != nullptr},
                        {"lastFile", lastFile_}};
}

CompileOutcome HeadlessSession::compile(const CompileRequest& request)
{
  std::filesystem::path file;
  try {
    file = project().resolve(request.file);
  } catch (const std::invalid_argument& e) {
    throw ToolError(e.what());
  }
  std::error_code ec;
  if (!std::filesystem::is_regular_file(file, ec)) throw ToolError("file not found: " + request.file);

  const auto firstSeq = MessageLog::instance().lastSeq();
  const auto started = std::chrono::steady_clock::now();
  EngineResult result = CompileEngine::run(file, request.mode, request.timeoutSeconds);

  CompileOutcome outcome;
  outcome.parsed = result.parsed;
  outcome.timedOut = result.timedOut;
  outcome.millis =
    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  if (request.mode == CompileMode::Render) {
    outcome.geometry = CompileEngine::geometrySummary(result.geometry.get());
  }
  const bool evaluated = request.mode == CompileMode::Check ? result.parsed : result.instantiated;
  if (request.mode != CompileMode::Check && result.instantiated) {
    // Keep the geometry of the previous render when this run was a preview.
    if (request.mode == CompileMode::Preview) result.geometry = last_.geometry;
    last_ = std::move(result);
    lastFile_ = project().relative(file);
  }
  finishOutcome(outcome, firstSeq, evaluated);
  return outcome;
}

void HeadlessSession::ensureCompiled(CompileMode mode, double timeoutSeconds)
{
  const bool haveTree = last_.rootNode != nullptr;
  const bool haveGeometry = last_.geometry != nullptr;
  if ((mode == CompileMode::Render && haveGeometry) || (mode == CompileMode::Preview && haveTree))
    return;
  CompileRequest request;
  request.file = lastFile_.empty() ? project().mainFile() : lastFile_;
  request.mode = mode;
  request.timeoutSeconds = timeoutSeconds;
  const CompileOutcome outcome = compile(request);
  if (!outcome.parsed || (mode == CompileMode::Render && !last_.geometry) ||
      (mode == CompileMode::Preview && !last_.rootNode)) {
    throw ToolError("the model does not compile; run preview or render and fix the errors first");
  }
}

nlohmann::json HeadlessSession::cameraGet() const
{
  return view::cameraToJson(camera_);
}

nlohmann::json HeadlessSession::cameraSet(const nlohmann::json& spec)
{
  const bool fit = view::applyCameraSpec(camera_, spec);
  camera_.viewall = fit;
  camera_.autocenter = fit;
  return view::cameraToJson(camera_);
}

nlohmann::json HeadlessSession::viewOptionsGet() const
{
  return view::viewOptionsToJson(viewOptions_, colorScheme_, mode_);
}

nlohmann::json HeadlessSession::viewOptionsSet(const nlohmann::json& spec)
{
  view::applyViewOptionSpec(viewOptions_, colorScheme_, spec);
  if (spec.contains("mode")) {
    const std::string mode = spec["mode"].get<std::string>();
    if (mode != "preview" && mode != "render" && mode != "thrownTogether") {
      throw ToolError("mode must be preview, render or thrownTogether");
    }
    mode_ = mode;
  }
  return viewOptionsGet();
}

std::vector<view::CaptureImage> HeadlessSession::capture(const CaptureRequest& request)
{
  const std::string mode = request.mode.empty() ? mode_ : request.mode;
  if (mode != "preview" && mode != "render" && mode != "thrownTogether") {
    throw ToolError("mode must be preview, render or thrownTogether");
  }
  ViewOptions options = viewOptions_;
  std::string colorScheme = colorScheme_;
  view::applyViewOptionSpec(options, colorScheme, request.options);
  options.previewer = mode == "thrownTogether" ? Previewer::THROWNTOGETHER : Previewer::OPENCSG;
  ensureCompiled(mode == "render" ? CompileMode::Render : CompileMode::Preview, request.timeoutSeconds);

  std::vector<view::CaptureImage> images;
  Camera lastCamera = camera_;
  for (const auto& spec : request.views) {
    Camera camera = camera_;
    std::string name = "current";
    if (spec.is_string()) {
      name = spec.get<std::string>();
      if (name != "current") {
        const bool fit = view::applyCameraSpec(camera, nlohmann::json{{"preset", name}});
        camera.viewall = fit;
        camera.autocenter = fit;
      }
    } else if (spec.is_object()) {
      name = spec.value("name", std::string("custom"));
      const bool fit = view::applyCameraSpec(camera, spec);
      camera.viewall = fit;
      camera.autocenter = fit;
    }
    if (mode == "render") {
      images.push_back(view::captureGeometry(last_.geometry, camera, options, colorScheme, request.width,
                                             request.height, name));
    } else {
      Tree tree(last_.rootNode, last_.documentPath);
      images.push_back(
        view::capturePreview(tree, camera, options, colorScheme, request.width, request.height, name));
    }
    lastCamera = camera;
  }
  if (request.keep) camera_ = lastCamera;
  return images;
}

nlohmann::json HeadlessSession::geometryInfo()
{
  if (!last_.geometry) throw ToolError("no rendered geometry: call render first");
  return view::geometryInfo(last_.geometry);
}

nlohmann::json HeadlessSession::exportGeometry(const std::string& format, const std::string& path,
                                               const nlohmann::json& options)
{
  if (!last_.geometry) throw ToolError("no rendered geometry: call render first");
  std::filesystem::path target;
  try {
    target = project().resolve(path);
  } catch (const std::invalid_argument& e) {
    throw ToolError(e.what());
  }
  nlohmann::json result =
    view::exportGeometry(last_.geometry, format, target, options,
                         (project().root() / lastFile_).string(), camera_, viewOptions_, colorScheme_);
  result["path"] = project().relative(target);
  return result;
}

}  // namespace mcp
