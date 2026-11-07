#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/customizer/ParameterSet.h"
#include "json/json.hpp"

class SourceFile;

// Customizer parameters without the GUI: description of a file's parameters,
// JSON <-> ParameterSet conversion and the preset file next to the .scad.
namespace mcp::param {

// Converts agent-supplied values ({name: number|bool|string|[numbers]}) to
// the string-encoded ParameterSet the customizer uses.
ParameterSet fromJson(const nlohmann::json& values);
nlohmann::json toJson(const ParameterSet& set);

// The parameters a parsed source file declares, with their current values
// after applying `overrides` (may be null). Each entry carries name,
// caption, group, type-specific fields from the customizer (initial, min,
// max, step, options) and `value`.
nlohmann::json describe(const SourceFile *sourceFile, const ParameterSet *overrides);

std::filesystem::path presetFile(const std::filesystem::path& scadFile);
ParameterSets readPresets(const std::filesystem::path& scadFile);
// Adds or replaces a preset in the file next to `scadFile`.
void writePreset(const std::filesystem::path& scadFile, const ParameterSet& preset);
std::vector<std::string> presetNames(const std::filesystem::path& scadFile);

}  // namespace mcp::param
