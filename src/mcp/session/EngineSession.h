#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/customizer/ParameterSet.h"
#include "glview/Camera.h"
#include "io/export.h"
#include "json/json.hpp"
#include "mcp/engine/CompileEngine.h"
#include "mcp/session/ProjectSession.h"
#include "mcp/session/SessionObserver.h"

class Geometry;

namespace mcp {

// The one session implementation: compiles and renders through the same
// code path as the command line, keeps the camera, view options and
// customizer overrides the agent set, and reports every change to an
// optional observer (the viewer). Used by headless and viewer mode alike.
class EngineSession : public ProjectSession
{
public:
  explicit EngineSession(std::shared_ptr<Project> project);

  void setObserver(SessionObserver *observer) { observer_ = observer; }
  SessionObserver *observer() const { return observer_; }

  nlohmann::json status() const override;
  bool paused() const override { return paused_; }
  void setPaused(bool paused) { paused_ = paused; }

  void fileWritten(const FileChange& change) override;
  void fileRemoved(const std::string& relativePath) override;
  void fileRenamed(const std::string& from, const std::string& to) override;
  void librariesChanged() override;

  CompileOutcome compile(const CompileRequest& request) override;
  nlohmann::json cameraGet() const override;
  nlohmann::json cameraSet(const nlohmann::json& spec) override;
  nlohmann::json viewOptionsGet() const override;
  nlohmann::json viewOptionsSet(const nlohmann::json& spec) override;
  std::vector<view::CaptureImage> capture(const CaptureRequest& request) override;
  nlohmann::json geometryInfo() override;
  nlohmann::json exportGeometry(const std::string& format, const std::string& path,
                                const nlohmann::json& options) override;
  nlohmann::json paramsGet(const std::string& file) override;
  nlohmann::json paramsSet(const std::string& file, const nlohmann::json& values,
                           const std::string& preset, bool reset) override;
  nlohmann::json presetsList(const std::string& file) override;
  nlohmann::json presetSave(const std::string& file, const std::string& name) override;

  // State a viewer mirrors.
  const EngineResult& lastResult() const { return last_; }
  const std::string& lastFile() const { return lastFile_; }
  CompileMode lastMode() const { return lastMode_; }
  std::shared_ptr<const Geometry> lastGeometry() const { return last_.geometry; }
  const Camera& camera() const { return camera_; }
  const ViewOptions& viewOptions() const { return viewOptions_; }
  const std::string& colorScheme() const { return colorScheme_; }
  const std::string& viewMode() const { return mode_; }  // preview | render | thrownTogether
  // Customizer parameters of the last compiled file with the current
  // overrides, without reparsing (empty when nothing was compiled).
  nlohmann::json parameterSummary() const;

private:
  void ensureCompiled(CompileMode mode, double timeoutSeconds);
  std::filesystem::path resolveFile(const std::string& file) const;

  SessionObserver *observer_ = nullptr;
  bool paused_ = false;
  EngineResult last_;
  std::string lastFile_;
  CompileMode lastMode_ = CompileMode::Check;
  std::map<std::string, ParameterSet> parameterValues_;  // per relative file
  Camera camera_;
  ViewOptions viewOptions_;
  std::string colorScheme_ = "Cornfield";
  std::string mode_ = "preview";
};

}  // namespace mcp
