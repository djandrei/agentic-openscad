#include "mcp/session/EngineSession.h"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/Tree.h"
#include "core/customizer/ParameterObject.h"
#include "mcp/engine/CompileEngine.h"
#include "mcp/engine/ParamEngine.h"
#include "mcp/engine/ViewEngine.h"
#include "mcp/protocol/Registry.h"

namespace mcp {

EngineSession::EngineSession(std::shared_ptr<Project> project) : ProjectSession(std::move(project))
{
  camera_.viewall = true;
  camera_.autocenter = true;
}

nlohmann::json EngineSession::status() const
{
  return nlohmann::json{{"busy", false},
                        {"paused", paused_},
                        {"hasGeometry", last_.geometry != nullptr},
                        {"lastFile", lastFile_}};
}

CompileOutcome EngineSession::compile(const CompileRequest& request)
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
  const auto values = parameterValues_.find(project().relative(file));
  EngineResult result = CompileEngine::run(file, request.mode, request.timeoutSeconds, false,
                                           values == parameterValues_.end() ? nullptr : &values->second);

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
    lastMode_ = request.mode;
  }
  finishOutcome(outcome, firstSeq, evaluated);
  if (observer_) observer_->compileFinished(*this, request, outcome);
  return outcome;
}

void EngineSession::fileWritten(const FileChange& change)
{
  if (observer_) observer_->fileWritten(*this, change);
}

void EngineSession::fileRemoved(const std::string& relativePath)
{
  if (observer_) observer_->fileRemoved(*this, relativePath);
}

void EngineSession::fileRenamed(const std::string& from, const std::string& to)
{
  if (observer_) observer_->fileRenamed(*this, from, to);
}

void EngineSession::librariesChanged()
{
  if (observer_) observer_->librariesChanged(*this);
}

void EngineSession::ensureCompiled(CompileMode mode, double timeoutSeconds)
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

nlohmann::json EngineSession::cameraGet() const
{
  return view::cameraToJson(camera_);
}

nlohmann::json EngineSession::cameraSet(const nlohmann::json& spec)
{
  const bool fit = view::applyCameraSpec(camera_, spec);
  camera_.viewall = fit;
  camera_.autocenter = fit;
  if (observer_) observer_->cameraChanged(*this);
  return view::cameraToJson(camera_);
}

nlohmann::json EngineSession::viewOptionsGet() const
{
  return view::viewOptionsToJson(viewOptions_, colorScheme_, mode_);
}

nlohmann::json EngineSession::viewOptionsSet(const nlohmann::json& spec)
{
  view::applyViewOptionSpec(viewOptions_, colorScheme_, spec);
  if (spec.contains("mode")) {
    const std::string mode = spec["mode"].get<std::string>();
    if (mode != "preview" && mode != "render" && mode != "thrownTogether") {
      throw ToolError("mode must be preview, render or thrownTogether");
    }
    mode_ = mode;
  }
  if (observer_) observer_->viewOptionsChanged(*this);
  return viewOptionsGet();
}

std::vector<view::CaptureImage> EngineSession::capture(const CaptureRequest& request)
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
  if (request.keep) {
    camera_ = lastCamera;
    if (observer_) observer_->cameraChanged(*this);
  }
  return images;
}

nlohmann::json EngineSession::geometryInfo()
{
  if (!last_.geometry) throw ToolError("no rendered geometry: call render first");
  return view::geometryInfo(last_.geometry);
}

nlohmann::json EngineSession::exportGeometry(const std::string& format, const std::string& path,
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

std::filesystem::path EngineSession::resolveFile(const std::string& file) const
{
  std::filesystem::path path;
  try {
    path = project().resolve(file);
  } catch (const std::invalid_argument& e) {
    throw ToolError(e.what());
  }
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) throw ToolError("file not found: " + file);
  return path;
}

nlohmann::json EngineSession::paramsGet(const std::string& file)
{
  const auto path = resolveFile(file);
  const auto relative = project().relative(path);
  const EngineResult parsed = CompileEngine::run(path, CompileMode::Check, 30);
  if (!parsed.sourceFile) throw ToolError("the file does not parse; fix the syntax errors first");
  const auto values = parameterValues_.find(relative);
  return nlohmann::json{
    {"file", relative},
    {"parameters", param::describe(parsed.sourceFile.get(),
                                   values == parameterValues_.end() ? nullptr : &values->second)},
    {"overrides",
     values == parameterValues_.end() ? nlohmann::json::object() : param::toJson(values->second)},
    {"presets", param::presetNames(path)}};
}

nlohmann::json EngineSession::paramsSet(const std::string& file, const nlohmann::json& values,
                                        const std::string& preset, bool reset)
{
  const auto path = resolveFile(file);
  const auto relative = project().relative(path);
  if (reset) parameterValues_.erase(relative);
  if (!preset.empty()) {
    bool found = false;
    for (const auto& set : param::readPresets(path)) {
      if (set.name() == preset) {
        parameterValues_[relative] = set;
        found = true;
      }
    }
    if (!found) throw ToolError("unknown preset: " + preset);
  }
  if (values.is_object() && !values.empty()) {
    auto& current = parameterValues_[relative];
    for (const auto& [name, value] : param::fromJson(values)) current[name] = value;
  }
  if (observer_) observer_->paramsChanged(*this, relative);
  return paramsGet(file);
}

nlohmann::json EngineSession::presetsList(const std::string& file)
{
  const auto path = resolveFile(file);
  nlohmann::json list = nlohmann::json::array();
  for (const auto& set : param::readPresets(path)) {
    list.push_back(nlohmann::json{{"name", set.name()}, {"values", param::toJson(set)}});
  }
  return nlohmann::json{{"file", project().relative(path)},
                        {"presetFile", project().relative(param::presetFile(path))},
                        {"presets", list}};
}

nlohmann::json EngineSession::presetSave(const std::string& file, const std::string& name)
{
  if (name.empty()) throw ToolError("preset name must not be empty");
  const auto path = resolveFile(file);
  const auto relative = project().relative(path);
  const EngineResult parsed = CompileEngine::run(path, CompileMode::Check, 30);
  if (!parsed.sourceFile) throw ToolError("the file does not parse; fix the syntax errors first");
  ParameterObjects objects = ParameterObjects::fromSourceFile(parsed.sourceFile.get());
  const auto values = parameterValues_.find(relative);
  if (values != parameterValues_.end()) objects.importValues(values->second);
  param::writePreset(path, objects.exportValues(name));
  if (observer_) observer_->paramsChanged(*this, relative);
  return presetsList(file);
}

}  // namespace mcp
