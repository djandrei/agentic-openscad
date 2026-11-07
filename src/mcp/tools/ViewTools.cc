#include "mcp/tools/ViewTools.h"

#include <string>
#include <vector>

#include "mcp/engine/ViewEngine.h"
#include "mcp/tools/CoreTools.h"

namespace mcp {

using nlohmann::json;

namespace {

json cameraSpecProperties()
{
  json presetNames = json::array();
  for (const auto& [name, rot] : view::presets()) presetNames.push_back(name);
  return json{
    {"preset", json{{"type", "string"},
                    {"enum", presetNames},
                    {"description", "Named orientation; fits the whole model into view."}}},
    {"vpt", json{{"type", "array"},
                 {"items", json{{"type", "number"}}},
                 {"minItems", 3},
                 {"maxItems", 3},
                 {"description", "Camera target (as $vpt)."}}},
    {"vpr", json{{"type", "array"},
                 {"items", json{{"type", "number"}}},
                 {"minItems", 3},
                 {"maxItems", 3},
                 {"description", "Rotation in degrees (as $vpr)."}}},
    {"vpd", json{{"type", "number"}, {"minimum", 0.001}, {"description", "Distance (as $vpd)."}}},
    {"fov", json{{"type", "number"},
                 {"minimum", 1},
                 {"maximum", 179},
                 {"description", "Field of view in degrees (as $vpf)."}}},
    {"projection", json{{"type", "string"}, {"enum", json::array({"perspective", "orthogonal"})}}},
    {"viewAll", json{{"type", "boolean"}, {"description", "Fit the whole model into view."}}}};
}

json viewOptionProperties()
{
  return json{
    {"edges", json{{"type", "boolean"}}},
    {"axes", json{{"type", "boolean"}}},
    {"crosshairs", json{{"type", "boolean"}}},
    {"scales", json{{"type", "boolean"}, {"description", "Scale markers on the axes."}}},
    {"colorScheme",
     json{{"type", "string"}, {"description", "One of the names reported by view_options."}}},
    {"mode", json{{"type", "string"}, {"enum", json::array({"preview", "render", "thrownTogether"})}}}};
}

}  // namespace

void registerViewTools(ToolRegistry& tools, SessionManager& sessions)
{
  tools.add(ToolDef{
    "camera_get",
    "Return the current camera: vpt (target), vpr (rotation), vpd (distance), fov, projection.",
    json{{"type", "object"},
         {"properties", json{{"project", projectArgSchema()}}},
         {"additionalProperties", false}},
    [&sessions](const json& args) { return ToolResult::data(sessions.resolve(args).cameraGet()); }});

  json cameraSetProperties = cameraSpecProperties();
  cameraSetProperties["project"] = projectArgSchema();
  tools.add(ToolDef{
    "camera_set",
    "Change the camera. Any subset of preset, vpt, vpr, vpd, fov, projection and viewAll may be "
    "given; a preset or viewAll fits the model into view. Returns the new camera.",
    json{{"type", "object"}, {"properties", cameraSetProperties}, {"additionalProperties", false}},
    [&sessions](const json& args) {
      auto& session = sessions.resolve(args);
      json spec = args;
      spec.erase("project");
      return ToolResult::data(session.cameraSet(spec));
    }});

  json optionProperties = viewOptionProperties();
  optionProperties["project"] = projectArgSchema();
  tools.add(ToolDef{
    "view_options",
    "Get or set view options: edges, axes, crosshairs, scales, colorScheme, and the view mode "
    "(preview = F5 look, render = F6 mesh, thrownTogether). Without arguments it only reports the "
    "current values and the available color schemes.",
    json{{"type", "object"}, {"properties", optionProperties}, {"additionalProperties", false}},
    [&sessions](const json& args) {
      auto& session = sessions.resolve(args);
      json spec = args;
      spec.erase("project");
      if (spec.empty()) return ToolResult::data(session.viewOptionsGet());
      return ToolResult::data(session.viewOptionsSet(spec));
    }});

  json captureProperties = viewOptionProperties();
  captureProperties.erase("mode");
  json viewItem{
    {"anyOf", json::array({json{{"type", "string"}},
                           json{{"type", "object"}, {"properties", cameraSpecProperties()}}})}};
  json properties{
    {"project", projectArgSchema()},
    {"views", json{{"type", "array"},
                   {"minItems", 1},
                   {"maxItems", 8},
                   {"items", viewItem},
                   {"description",
                    "Views to capture: \"current\", a preset name (top, bottom, left, right, "
                    "front, back, diagonal/iso), or a camera spec object (optionally with a "
                    "name). Default [\"current\"]."}}},
    {"width",
     json{{"type", "integer"}, {"minimum", 64}, {"maximum", 2048}, {"description", "Default 800."}}},
    {"height",
     json{{"type", "integer"}, {"minimum", 64}, {"maximum", 2048}, {"description", "Default 600."}}},
    {"mode", json{{"type", "string"},
                  {"enum", json::array({"preview", "render", "thrownTogether"})},
                  {"description", "What to draw; default is the current view mode."}}},
    {"keep",
     json{{"type", "boolean"}, {"description", "Keep the last captured view's camera afterwards."}}},
    {"timeoutSeconds", json{{"type", "number"}, {"minimum", 1}, {"maximum", 3600}}}};
  for (const auto& [key, value] : captureProperties.items()) properties[key] = value;
  tools.add(ToolDef{
    "view_capture",
    "Capture PNG images of the model from one or more views and return them as image content. In "
    "GUI mode the capture shows what the user sees (the 3D view, scaled to the requested size); in "
    "headless mode the model is drawn offscreen (preview or render look). Use render mode after "
    "`render` to see the final mesh. Edge/axis/color overrides apply to the capture only.",
    json{{"type", "object"}, {"properties", properties}, {"additionalProperties", false}},
    [&sessions](const json& args) {
      auto& session = sessions.resolve(args);
      CaptureRequest request;
      if (args.contains("views")) {
        for (const auto& view : args["views"]) request.views.push_back(view);
      } else {
        request.views.push_back("current");
      }
      request.width = args.value("width", 800);
      request.height = args.value("height", 600);
      request.mode = args.value("mode", std::string());
      request.keep = args.value("keep", false);
      request.timeoutSeconds = args.value("timeoutSeconds", 120.0);
      request.options = json::object();
      for (const char *key : {"edges", "axes", "crosshairs", "scales", "colorScheme"}) {
        if (args.contains(key)) request.options[key] = args[key];
      }
      const auto images = session.capture(request);
      ToolResult result;
      json views = json::array();
      for (const auto& image : images) {
        result.content.push_back(textContent("view '" + image.name + "' (" +
                                             std::to_string(image.width) + "x" +
                                             std::to_string(image.height) + ")"));
        result.content.push_back(imageContent(image.pngBase64, "image/png"));
        views.push_back(json{{"name", image.name},
                             {"width", image.width},
                             {"height", image.height},
                             {"camera", image.camera},
                             {"bytes", image.pngBase64.size() / 4 * 3}});
      }
      result.structuredContent = json{{"views", views}};
      return result;
    }});

  tools.add(ToolDef{
    "geometry_info",
    "Statistics of the last rendered geometry (call render first): dimension, bounding box, facet "
    "and vertex counts, manifoldness, volume and surface area (3D) or area and contours (2D).",
    json{{"type", "object"},
         {"properties", json{{"project", projectArgSchema()}}},
         {"additionalProperties", false}},
    [&sessions](const json& args) { return ToolResult::data(sessions.resolve(args).geometryInfo()); }});

  tools.add(ToolDef{
    "export",
    "Write the last rendered geometry (call render first) to a file inside the project. Formats: "
    "stl (ascii), binstl, obj, off, wrl, 3mf, pov (3D); svg, dxf, pdf (2D); png (image of the "
    "current camera). Options use OpenSCAD's export settings, e.g. {\"export-pdf/paper-size\": \"a4\"}.",
    json{{"type", "object"},
         {"properties",
          json{{"project", projectArgSchema()},
               {"format", json{{"type", "string"},
                               {"enum", json::array({"stl", "asciistl", "binstl", "obj", "off", "wrl",
                                                     "3mf", "pov", "svg", "dxf", "pdf", "png"})}}},
               {"path",
                json{{"type", "string"}, {"description", "Target path relative to the project root."}}},
               {"options",
                json{{"type", "object"}, {"description", "Export settings as section/key: value."}}}}},
         {"required", json::array({"format", "path"})},
         {"additionalProperties", false}},
    [&sessions](const json& args) {
      auto& session = sessions.resolve(args);
      return ToolResult::data(session.exportGeometry(args["format"].get<std::string>(),
                                                     args["path"].get<std::string>(),
                                                     args.value("options", json::object())));
    }});
}

}  // namespace mcp
