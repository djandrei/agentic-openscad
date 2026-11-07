#include <string>

#include "mcp/library/Registry.h"

namespace mcp::library {

// Seeded from https://openscad.org/libraries.html (2026-11-06). `ref` is left
// empty where the default branch is not known; the installer then tries
// "main" and "master".
const std::string& bundledRegistryJson()
{
  static const std::string text = R"JSON({
  "name": "openscad.org",
  "libraries": [
    {"name": "MCAD", "description": "Parametric CAD library bundled with OpenSCAD (gears, nuts and bolts, motors, shapes).", "repository": "https://github.com/openscad/MCAD", "docs": "https://github.com/openscad/MCAD", "category": "general", "usage": "use <MCAD/involute_gears.scad>", "bundled": true},
    {"name": "BOSL", "description": "Belfry OpenScad Library: tools, shapes and helpers for easier OpenSCAD use.", "repository": "https://github.com/revarbat/BOSL", "docs": "https://github.com/revarbat/BOSL/wiki", "category": "general", "usage": "include <BOSL/constants.scad>\nuse <BOSL/shapes.scad>"},
    {"name": "BOSL2", "description": "Belfry OpenScad Library v2 (beta): tools, shapes, attachments, rounding, threads, gears and more.", "repository": "https://github.com/BelfrySCAD/BOSL2", "ref": "master", "docs": "https://github.com/BelfrySCAD/BOSL2/wiki", "category": "general", "usage": "include <BOSL2/std.scad>"},
    {"name": "dotSCAD", "description": "Reduces the burden of 3D modeling in mathematics (paths, curves, surfaces, mazes, ...).", "repository": "https://github.com/JustinSDK/dotSCAD", "docs": "https://openhome.cc/eGossip/OpenSCAD/lib3x.html", "category": "general", "usage": "use <dotSCAD/src/bezier_curve.scad>"},
    {"name": "NopSCADlib", "description": "Expanding library of parts for 3D printers and electronics enclosures.", "repository": "https://github.com/nophead/NopSCADlib", "docs": "https://github.com/nophead/NopSCADlib/blob/master/readme.md", "category": "general", "usage": "include <NopSCADlib/lib.scad>"},
    {"name": "UB.scad", "description": "Full 3D printing workflow: modifying and generating objects, view helpers, mechanical parts.", "repository": "https://github.com/UBaer21/UB.scad", "docs": "https://github.com/UBaer21/UB.scad", "category": "general", "usage": "include <UB.scad/libraries/ub.scad>"},
    {"name": "FunctionalOpenSCAD", "description": "Functional OpenSCAD: implements OpenSCAD geometry in OpenSCAD functions.", "repository": "https://github.com/thehans/FunctionalOpenSCAD", "docs": "https://github.com/thehans/FunctionalOpenSCAD", "category": "general", "usage": "use <FunctionalOpenSCAD/functional.scad>"},
    {"name": "constructive", "description": "Stamping-style approach for mechanical parts that fit together, with less math.", "repository": "https://github.com/solidboredom/constructive", "docs": "https://github.com/solidboredom/constructive", "category": "general", "usage": "include <constructive/constructive-compiled.scad>"},
    {"name": "StoneAgeLib", "description": "Scripts for 3D-printable models (CC0); needs OpenSCAD 2025 with Manifold.", "repository": "https://github.com/Stone-Age-Sculptor/StoneAgeLib", "license": "CC0-1.0", "docs": "https://github.com/Stone-Age-Sculptor/StoneAgeLib", "category": "general"},
    {"name": "BOLTS", "description": "Open Library of Technical Specifications: standard parts.", "repository": "https://github.com/boltsparts/BOLTS", "docs": "https://www.bolts-library.org/", "category": "general"},
    {"name": "OpenSCAD-Snippet", "description": "Asset collection: mechanical parts, furniture and base meshes for animation and game design.", "repository": "https://github.com/AngeloNicoli/OpenSCAD-Snippet", "docs": "https://github.com/AngeloNicoli/OpenSCAD-Snippet", "category": "general"},
    {"name": "openscad_boardgame_toolkit", "description": "Board game toolkit: boxes with layouts, finger holes, catches and multi-color 3MF output.", "repository": "https://github.com/pinkfish/openscad_boardgame_toolkit", "docs": "https://github.com/pinkfish/openscad_boardgame_toolkit", "category": "general"},
    {"name": "Round-Anything", "description": "Utilities for rounding parts, plus an approach to developing parts.", "repository": "https://github.com/Irev-Dev/Round-Anything", "docs": "https://kurthutten.com/blog/round-anything-a-pragmatic-approach-to-openscad-design/", "category": "single-topic", "usage": "include <Round-Anything/polyround.scad>"},
    {"name": "MarksEnclosureHelper", "description": "Generates a two-piece hinged box with rounded corners and closures.", "repository": "https://github.com/sbambach/MarksEnclosureHelper", "docs": "https://github.com/sbambach/MarksEnclosureHelper", "category": "single-topic"},
    {"name": "funcutils", "description": "Function-literal utilities for functional programming in OpenSCAD.", "repository": "https://github.com/thehans/funcutils", "docs": "https://github.com/thehans/funcutils", "category": "single-topic"},
    {"name": "threads-scad", "description": "Threading library for metric internal and external threads, holes, nuts and bolts.", "repository": "https://github.com/rcolyer/threads-scad", "docs": "https://github.com/rcolyer/threads-scad", "category": "single-topic", "usage": "use <threads-scad/threads.scad>"},
    {"name": "smooth-prim", "description": "Smooth primitives: primitives with specified rounded edges.", "repository": "https://github.com/rcolyer/smooth-prim", "docs": "https://github.com/rcolyer/smooth-prim", "category": "single-topic", "usage": "use <smooth-prim/smooth_prim.scad>"},
    {"name": "plot-function", "description": "Plots functions in Cartesian, polar and axial coordinates.", "repository": "https://github.com/rcolyer/plot-function", "docs": "https://github.com/rcolyer/plot-function", "category": "single-topic", "usage": "use <plot-function/plot_function.scad>"},
    {"name": "closepoints", "description": "Creates polyhedra from layered point lists.", "repository": "https://github.com/rcolyer/closepoints", "docs": "https://github.com/rcolyer/closepoints", "category": "single-topic", "usage": "use <closepoints/closepoints.scad>"},
    {"name": "openscad-tray", "description": "Designs trays with optional subdivisions.", "repository": "https://github.com/sofian/openscad-tray", "docs": "https://github.com/sofian/openscad-tray", "category": "single-topic", "usage": "use <openscad-tray/tray.scad>"},
    {"name": "YAPP_Box", "description": "Yet Another Parametric Projectbox generator for electronics project boxes.", "repository": "https://github.com/mrWheel/YAPP_Box", "docs": "https://mrwheel-docs.gitbook.io/yappgenerator_en/", "category": "single-topic", "usage": "include <YAPP_Box/library/YAPPgenerator_v3.scad>"},
    {"name": "Stemfie_OpenSCAD", "description": "Parts for the STEMFIE educational construction toy.", "repository": "https://github.com/Cantareus/Stemfie_OpenSCAD", "docs": "https://www.stemfie.org/", "category": "single-topic"},
    {"name": "catchnhole", "description": "Creates nut catches, screw holes and countersinks.", "repository": "https://github.com/mmalecki/catchnhole", "docs": "https://github.com/mmalecki/catchnhole", "category": "single-topic", "usage": "use <catchnhole/catchnhole.scad>"},
    {"name": "pathbuilder", "description": "2D shapes with fillets and chamfers from SVG path syntax.", "repository": "https://github.com/dinther/pathbuilder", "docs": "https://github.com/dinther/pathbuilder", "category": "single-topic", "usage": "use <pathbuilder/pathbuilder.scad>"},
    {"name": "openscad-scon", "description": "SCON: a JSON-like configuration data subset for OpenSCAD.", "repository": "https://github.com/wmacevoy/openscad-scon", "docs": "https://github.com/wmacevoy/openscad-scon", "category": "single-topic"},
    {"name": "A2D", "description": "Altair's 2D library: functions, modules and constants for 2D drawing.", "repository": "https://github.com/ridercz/A2D", "docs": "https://github.com/ridercz/A2D", "category": "single-topic", "usage": "use <A2D/A2D.scad>"},
    {"name": "openscad-doll-house", "description": "Creates wooden doll houses.", "repository": "https://codeberg.org/adrien-delhorme/openscad-doll-house", "docs": "https://codeberg.org/adrien-delhorme/openscad-doll-house", "category": "single-topic"},
    {"name": "openscad-new-dimensions", "description": "Draws dimensions.", "repository": "https://codeberg.org/adrien-delhorme/openscad-new-dimensions", "docs": "https://codeberg.org/adrien-delhorme/openscad-new-dimensions", "category": "single-topic"},
    {"name": "LEGO.scad", "description": "Creates LEGO-compatible bricks.", "repository": "https://github.com/cfinke/LEGO.scad", "docs": "https://github.com/cfinke/LEGO.scad", "category": "single-topic", "usage": "use <LEGO.scad/LEGO.scad>"},
    {"name": "Technic.scad", "description": "Creates LEGO Technic-compatible components.", "repository": "https://github.com/cfinke/Technic.scad", "docs": "https://github.com/cfinke/Technic.scad", "category": "single-topic", "usage": "use <Technic.scad/Technic.scad>"}
  ]
})JSON";
  return text;
}

}  // namespace mcp::library
