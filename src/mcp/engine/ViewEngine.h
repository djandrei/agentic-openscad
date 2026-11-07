#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "glview/Camera.h"
#include "io/export.h"
#include "json/json.hpp"

class Geometry;
class Tree;

// Camera, capture, geometry statistics and export helpers shared by the
// headless and the GUI session. No Qt.
namespace mcp::view {

struct CaptureImage {
  std::string name;
  std::string pngBase64;
  unsigned width = 0;
  unsigned height = 0;
  nlohmann::json camera;
};

// Named camera orientations (vpr-style rotations in degrees, as the GUI's
// View menu sets them).
const std::map<std::string, std::array<double, 3>>& presets();

nlohmann::json cameraToJson(const Camera& camera);
// Applies {preset, vpt, vpr, vpd, fov, projection} to the camera. Returns true
// when the spec asks the view to be fitted to the model (preset or viewAll),
// which callers do once the model's bounding box is known.
bool applyCameraSpec(Camera& camera, const nlohmann::json& spec);

nlohmann::json viewOptionsToJson(const ViewOptions& options, const std::string& colorScheme,
                                 const std::string& mode);
// Applies {edges, axes, crosshairs, scales, colorScheme} to the options;
// throws ToolError for an unknown color scheme.
void applyViewOptionSpec(ViewOptions& options, std::string& colorScheme, const nlohmann::json& spec);
std::vector<std::string> colorSchemeNames();

// Renders the geometry (F6-style) offscreen. Throws ToolError when no
// offscreen OpenGL context can be created.
CaptureImage captureGeometry(const std::shared_ptr<const Geometry>& geometry, Camera camera,
                             const ViewOptions& options, const std::string& colorScheme, unsigned width,
                             unsigned height, const std::string& name);
// Renders the CSG tree (F5-style, OpenCSG or thrown together) offscreen.
CaptureImage capturePreview(Tree& tree, Camera camera, const ViewOptions& options,
                            const std::string& colorScheme, unsigned width, unsigned height,
                            const std::string& name);

// Dimension, facets, bounding box, volume, surface area, manifoldness, ...
nlohmann::json geometryInfo(const std::shared_ptr<const Geometry>& geometry);

// Writes the geometry in `format` (export identifier such as stl, binstl,
// 3mf, off, obj, svg, dxf, pdf, png) to `target`. Returns {format, path, bytes}.
nlohmann::json exportGeometry(const std::shared_ptr<const Geometry>& geometry, const std::string& format,
                              const std::filesystem::path& target, const nlohmann::json& options,
                              const std::string& sourceFile, Camera camera,
                              const ViewOptions& viewOptions, const std::string& colorScheme);

}  // namespace mcp::view
