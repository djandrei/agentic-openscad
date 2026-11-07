#include "mcp/engine/ParamEngine.h"

#include <boost/property_tree/ptree.hpp>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "core/customizer/ParameterObject.h"
#include "core/customizer/ParameterSet.h"
#include "core/str_utf8_wrapper.h"
#include "mcp/protocol/Registry.h"

namespace fs = std::filesystem;

namespace mcp::param {

using nlohmann::json;

namespace {

std::string encodeNumber(double value)
{
  // The customizer stores numbers as text; integral values must not carry ".0".
  std::ostringstream out;
  out.precision(15);
  out << value;
  return out.str();
}

std::string encode(const json& value)
{
  if (value.is_string()) return value.get<std::string>();
  if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
  if (value.is_number()) return encodeNumber(value.get<double>());
  if (value.is_array()) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < value.size(); ++i) {
      if (!value[i].is_number()) throw ToolError("vector parameter values must be numbers");
      if (i > 0) out << ", ";
      out << encodeNumber(value[i].get<double>());
    }
    out << "]";
    return out.str();
  }
  throw ToolError("parameter values must be numbers, booleans, strings or arrays of numbers");
}

json decode(const std::string& text)
{
  if (text == "true") return true;
  if (text == "false") return false;
  if (!text.empty() && text.front() == '[' && text.back() == ']') {
    json list = json::array();
    std::string inner = text.substr(1, text.size() - 2);
    std::istringstream in(inner);
    std::string item;
    bool allNumbers = true;
    while (std::getline(in, item, ',')) {
      char *end = nullptr;
      const double v = std::strtod(item.c_str(), &end);
      if (end == item.c_str()) {
        allNumbers = false;
        break;
      }
      list.push_back(v);
    }
    if (allNumbers) return list;
    return text;
  }
  if (!text.empty()) {
    char *end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (end != text.c_str() && *end == '\0') return v;
  }
  return text;
}

}  // namespace

ParameterSet fromJson(const json& values)
{
  if (!values.is_object()) throw ToolError("values must be an object of parameter name to value");
  ParameterSet set;
  for (const auto& [name, value] : values.items()) {
    set[name] = boost::property_tree::ptree(encode(value));
  }
  return set;
}

json toJson(const ParameterSet& set)
{
  json out = json::object();
  for (const auto& [name, value] : set) out[name] = decode(value.data());
  return out;
}

json describe(const SourceFile *sourceFile, const ParameterSet *overrides)
{
  ParameterObjects parameters = ParameterObjects::fromSourceFile(sourceFile);
  if (overrides) parameters.importValues(*overrides);
  json list = json::array();
  for (const auto& parameter : parameters) {
    json entry;
    entry["name"] = parameter->name();
    if (!parameter->description().empty()) entry["caption"] = parameter->description();
    if (!parameter->group().empty()) entry["group"] = parameter->group();
    entry.merge_patch(parameter->jsonValue());
    entry["value"] = decode(parameter->exportValue().data());
    list.push_back(entry);
  }
  return list;
}

fs::path presetFile(const fs::path& scadFile)
{
  fs::path path = scadFile;
  return path.replace_extension(".json");
}

ParameterSets readPresets(const fs::path& scadFile)
{
  ParameterSets sets;
  std::error_code ec;
  const fs::path file = presetFile(scadFile);
  if (fs::exists(file, ec)) sets.readFile(file.string());
  return sets;
}

void writePreset(const fs::path& scadFile, const ParameterSet& preset)
{
  ParameterSets sets = readPresets(scadFile);
  bool replaced = false;
  for (auto& existing : sets) {
    if (existing.name() == preset.name()) {
      existing = preset;
      replaced = true;
    }
  }
  if (!replaced) sets.push_back(preset);
  sets.writeFile(presetFile(scadFile).string());
}

std::vector<std::string> presetNames(const fs::path& scadFile)
{
  std::vector<std::string> names;
  for (const auto& set : readPresets(scadFile)) names.push_back(set.name());
  return names;
}

}  // namespace mcp::param
