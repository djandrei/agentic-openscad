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

class Geometry;

namespace mcp {

// Project session without a GUI. Compiles and renders through the same code
// path as the command line; keeps a virtual camera and view options for
// offscreen captures.
class HeadlessSession : public ProjectSession
{
public:
  explicit HeadlessSession(std::shared_ptr<Project> project);

  nlohmann::json status() const override;
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

  // Geometry of the last successful render, for geometry queries and exports.
  std::shared_ptr<const Geometry> lastGeometry() const { return last_.geometry; }

private:
  void ensureCompiled(CompileMode mode, double timeoutSeconds);

  std::filesystem::path resolveFile(const std::string& file) const;

  EngineResult last_;
  std::string lastFile_;
  std::map<std::string, ParameterSet> parameterValues_;  // per relative file
  Camera camera_;
  ViewOptions viewOptions_;
  std::string colorScheme_ = "Cornfield";
  std::string mode_ = "preview";
};

}  // namespace mcp
