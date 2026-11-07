#include "mcp/engine/ViewEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "core/Tree.h"
#include "geometry/Geometry.h"
#include "geometry/PolySet.h"
#include "geometry/Polygon2d.h"
#include "glview/ColorMap.h"
#include "glview/OffscreenView.h"
#include "glview/RenderSettings.h"
#include "mcp/engine/Base64.h"
#include "mcp/engine/CompileEngine.h"
#include "mcp/protocol/Registry.h"
#ifdef ENABLE_MANIFOLD
#include "geometry/manifold/ManifoldGeometry.h"
#include "manifold/manifold.h"
#endif
#ifdef ENABLE_CGAL
#include "geometry/cgal/CGALNefGeometry.h"
#include "geometry/cgal/cgalutils.h"
#endif

namespace fs = std::filesystem;

namespace mcp::view {

using nlohmann::json;

const std::map<std::string, std::array<double, 3>>& presets()
{
  static const std::map<std::string, std::array<double, 3>> table{
    {"top", {90, 0, 0}},  {"bottom", {270, 0, 0}}, {"left", {0, 0, 90}},       {"right", {0, 0, 270}},
    {"front", {0, 0, 0}}, {"back", {0, 0, 180}},   {"diagonal", {35, 0, -25}}, {"iso", {35, 0, -25}},
  };
  return table;
}

json cameraToJson(const Camera& camera)
{
  const auto vpt = camera.getVpt();
  const auto vpr = camera.getVpr();
  return json{{"vpt", {vpt.x(), vpt.y(), vpt.z()}},
              {"vpr", {vpr.x(), vpr.y(), vpr.z()}},
              {"vpd", camera.zoomValue()},
              {"fov", camera.fovValue()},
              {"projection",
               camera.projection == Camera::ProjectionType::ORTHOGONAL ? "orthogonal" : "perspective"}};
}

namespace {

std::array<double, 3> triple(const json& value, const char *name)
{
  if (!value.is_array() || value.size() != 3 || !value[0].is_number() || !value[1].is_number() ||
      !value[2].is_number()) {
    throw ToolError(std::string(name) + " must be an array of three numbers");
  }
  return {value[0].get<double>(), value[1].get<double>(), value[2].get<double>()};
}

}  // namespace

bool applyCameraSpec(Camera& camera, const json& spec)
{
  bool fit = false;
  if (spec.contains("preset")) {
    const std::string name = spec["preset"].get<std::string>();
    const auto it = presets().find(name);
    if (it == presets().end()) throw ToolError("unknown camera preset: " + name);
    camera.object_rot << it->second[0], it->second[1], it->second[2];
    fit = true;
  }
  if (spec.contains("vpt")) {
    const auto v = triple(spec["vpt"], "vpt");
    camera.setVpt(v[0], v[1], v[2]);
    fit = false;
  }
  if (spec.contains("vpr")) {
    const auto v = triple(spec["vpr"], "vpr");
    camera.setVpr(v[0], v[1], v[2]);
  }
  if (spec.contains("vpd")) {
    camera.setVpd(spec["vpd"].get<double>());
    fit = false;
  }
  if (spec.contains("fov")) camera.setVpf(spec["fov"].get<double>());
  if (spec.contains("projection")) {
    const std::string projection = spec["projection"].get<std::string>();
    if (projection == "orthogonal" || projection == "ortho" || projection == "o") {
      camera.projection = Camera::ProjectionType::ORTHOGONAL;
    } else if (projection == "perspective" || projection == "p") {
      camera.projection = Camera::ProjectionType::PERSPECTIVE;
    } else {
      throw ToolError("projection must be 'perspective' or 'orthogonal'");
    }
  }
  if (spec.contains("viewAll")) fit = spec["viewAll"].get<bool>();
  return fit;
}

std::vector<std::string> colorSchemeNames()
{
  const auto names = ColorMap::instance().colorSchemeNames();
  return {names.begin(), names.end()};
}

json viewOptionsToJson(const ViewOptions& options, const std::string& colorScheme,
                       const std::string& mode)
{
  return json{{"edges", options.flags.at("edges")},
              {"axes", options.flags.at("axes")},
              {"crosshairs", options.flags.at("crosshairs")},
              {"scales", options.flags.at("scales")},
              {"colorScheme", colorScheme},
              {"mode", mode},
              {"colorSchemes", colorSchemeNames()}};
}

void applyViewOptionSpec(ViewOptions& options, std::string& colorScheme, const json& spec)
{
  for (const char *flag : {"edges", "axes", "crosshairs", "scales"}) {
    if (spec.contains(flag)) options.flags[flag] = spec[flag].get<bool>();
  }
  if (spec.contains("colorScheme")) {
    const std::string name = spec["colorScheme"].get<std::string>();
    if (!ColorMap::instance().findColorScheme(name)) {
      throw ToolError("unknown color scheme: " + name + " (see view_options for the list)");
    }
    colorScheme = name;
  }
}

namespace {

// RenderSettings::colorscheme is global; set it for the duration of a capture.
struct ColorSchemeScope {
  std::string previous;
  explicit ColorSchemeScope(const std::string& scheme) : previous(RenderSettings::inst()->colorscheme)
  {
    if (!scheme.empty()) RenderSettings::inst()->colorscheme = scheme;
  }
  ~ColorSchemeScope() { RenderSettings::inst()->colorscheme = previous; }
};

CaptureImage finishCapture(const std::string& png, const Camera& camera, unsigned width, unsigned height,
                           const std::string& name)
{
  if (png.empty()) {
    throw ToolError(
      "capture failed: no offscreen OpenGL context is available (EGL or OSMesa is "
      "required for view_capture in this mode)");
  }
  CaptureImage image;
  image.name = name;
  image.pngBase64 = base64Encode(png);
  image.width = width;
  image.height = height;
  image.camera = cameraToJson(camera);
  return image;
}

}  // namespace

CaptureImage captureGeometry(const std::shared_ptr<const Geometry>& geometry, Camera camera,
                             const ViewOptions& options, const std::string& colorScheme, unsigned width,
                             unsigned height, const std::string& name)
{
  if (!geometry) throw ToolError("no geometry to capture: render first");
  const ColorSchemeScope scope(colorScheme);
  camera.pixel_width = width;
  camera.pixel_height = height;
  std::ostringstream png(std::ios::binary);
  if (!export_png(geometry, options, camera, png)) return finishCapture("", camera, width, height, name);
  return finishCapture(png.str(), camera, width, height, name);
}

CaptureImage capturePreview(Tree& tree, Camera camera, const ViewOptions& options,
                            const std::string& colorScheme, unsigned width, unsigned height,
                            const std::string& name)
{
  const ColorSchemeScope scope(colorScheme);
  camera.pixel_width = width;
  camera.pixel_height = height;
  auto glview = prepare_preview(tree, options, camera);
  if (!glview) return finishCapture("", camera, width, height, name);
  std::ostringstream png(std::ios::binary);
  export_png(*glview, png);
  return finishCapture(png.str(), camera, width, height, name);
}

namespace {

struct MeshMetrics {
  double volume = 0;
  double area = 0;
};

MeshMetrics polySetMetrics(const PolySet& ps)
{
  MeshMetrics m;
  for (const auto& face : ps.indices) {
    if (face.size() < 3) continue;
    const Vector3d& a = ps.vertices[face[0]];
    for (size_t i = 1; i + 1 < face.size(); ++i) {
      const Vector3d& b = ps.vertices[face[i]];
      const Vector3d& c = ps.vertices[face[i + 1]];
      m.area += (b - a).cross(c - a).norm() / 2.0;
      m.volume += a.dot(b.cross(c)) / 6.0;
    }
  }
  m.volume = std::abs(m.volume);
  return m;
}

json singleGeometryInfo(const std::shared_ptr<const Geometry>& geometry)
{
  json info = CompileEngine::geometrySummary(geometry.get());
  if (auto poly = std::dynamic_pointer_cast<const Polygon2d>(geometry)) {
    info["area"] = poly->area();
    info["contours"] = poly->outlines().size();
    info["convex"] = poly->is_convex();
    return info;
  }
#ifdef ENABLE_MANIFOLD
  if (auto mani = std::dynamic_pointer_cast<const ManifoldGeometry>(geometry)) {
    info["backend"] = "manifold";
    info["manifold"] = mani->isManifold();
    info["vertices"] = mani->numVertices();
    info["volume"] = mani->getManifold().Volume();
    info["surfaceArea"] = mani->getManifold().SurfaceArea();
    return info;
  }
#endif
#ifdef ENABLE_CGAL
  if (auto nef = std::dynamic_pointer_cast<const CGALNefGeometry>(geometry)) {
    info["backend"] = "cgal";
    if (nef->p3) {
      info["manifold"] = nef->p3->is_simple();
      info["vertices"] = nef->p3->number_of_vertices();
      info["volumes"] = nef->p3->number_of_volumes();
      if (auto ps = CGALUtils::createPolySetFromNefPolyhedron3(*nef->p3)) {
        const auto metrics = polySetMetrics(*ps);
        info["volume"] = metrics.volume;
        info["surfaceArea"] = metrics.area;
      }
    }
    return info;
  }
#endif
  if (auto ps = std::dynamic_pointer_cast<const PolySet>(geometry)) {
    info["backend"] = "polyset";
    info["vertices"] = ps->vertices.size();
    info["convex"] = ps->isConvex();
    info["triangular"] = ps->isTriangular();
    const auto metrics = polySetMetrics(*ps);
    info["volume"] = metrics.volume;
    info["surfaceArea"] = metrics.area;
    return info;
  }
  return info;
}

}  // namespace

json geometryInfo(const std::shared_ptr<const Geometry>& geometry)
{
  if (!geometry) return json{{"empty", true}};
  if (auto list = std::dynamic_pointer_cast<const GeometryList>(geometry)) {
    const auto children = list->flatten();
    json info = CompileEngine::geometrySummary(geometry.get());
    info["backend"] = "list";
    info["children"] = children.size();
    json parts = json::array();
    double volume = 0;
    bool anyVolume = false;
    for (const auto& child : children) {
      json part = singleGeometryInfo(child.second);
      if (part.contains("volume")) {
        volume += part["volume"].get<double>();
        anyVolume = true;
      }
      if (parts.size() < 20) parts.push_back(part);
    }
    if (anyVolume) info["volume"] = volume;
    info["parts"] = parts;
    return info;
  }
  return singleGeometryInfo(geometry);
}

namespace {

CmdLineExportOptions toExportOptions(const json& options)
{
  CmdLineExportOptions result;
  if (!options.is_object()) return result;
  for (const auto& [key, value] : options.items()) {
    if (value.is_object()) {
      for (const auto& [name, v] : value.items()) {
        result[key][name] = v.is_string() ? v.get<std::string>() : v.dump();
      }
      continue;
    }
    const auto slash = key.find('/');
    if (slash == std::string::npos) throw ToolError("export option keys must be 'section/key': " + key);
    result[key.substr(0, slash)][key.substr(slash + 1)] =
      value.is_string() ? value.get<std::string>() : value.dump();
  }
  return result;
}

}  // namespace

json exportGeometry(const std::shared_ptr<const Geometry>& geometry, const std::string& format,
                    const fs::path& target, const json& options, const std::string& sourceFile,
                    Camera camera, const ViewOptions& viewOptions, const std::string& colorScheme)
{
  if (!geometry) throw ToolError("no geometry to export: render first");
  FileFormat fileFormat;
  if (!fileformat::fromIdentifier(format, fileFormat))
    throw ToolError("unknown export format: " + format);

  std::error_code ec;
  fs::create_directories(target.parent_path(), ec);

  if (fileFormat == FileFormat::PNG) {
    const auto image = captureGeometry(geometry, camera, viewOptions, colorScheme,
                                       camera.pixel_width ? camera.pixel_width : 800,
                                       camera.pixel_height ? camera.pixel_height : 600, "export");
    // The capture is base64; re-render straight to the file instead.
    const ColorSchemeScope scope(colorScheme);
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out) throw ToolError("cannot write " + target.string());
    if (!export_png(geometry, viewOptions, camera, out)) throw ToolError("PNG export failed");
    out.close();
    return json{{"format", "png"},
                {"path", target.string()},
                {"bytes", fs::file_size(target, ec)},
                {"width", image.width},
                {"height", image.height}};
  }

  const int dimension = fileformat::is3D(fileFormat) ? 3 : fileformat::is2D(fileFormat) ? 2 : 0;
  if (dimension == 0) throw ToolError("format " + format + " is not a geometry export");
  if (geometry->getDimension() != static_cast<unsigned>(dimension)) {
    throw ToolError("the rendered object is " + std::to_string(geometry->getDimension()) + "D but " +
                    format + " needs a " + std::to_string(dimension) + "D object");
  }
  if (geometry->isEmpty()) throw ToolError("the rendered object is empty");

  const ExportInfo info = createExportInfo(fileFormat, fileformat::info(fileFormat), sourceFile, &camera,
                                           toExportOptions(options));
  if (!exportFileByName(geometry, target.string(), info))
    throw ToolError("export failed: " + target.string());
  return json{{"format", fileformat::info(fileFormat).identifier},
              {"path", target.string()},
              {"bytes", fs::file_size(target, ec)}};
}

}  // namespace mcp::view
