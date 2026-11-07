#include "mcp/protocol/Schema.h"

#include <cmath>
#include <optional>
#include <string>

namespace mcp::schema {

static bool typeMatches(const std::string& type, const json& value)
{
  if (type == "string") return value.is_string();
  if (type == "integer") {
    if (value.is_number_integer()) return true;
    return value.is_number_float() && std::floor(value.get<double>()) == value.get<double>();
  }
  if (type == "number") return value.is_number();
  if (type == "boolean") return value.is_boolean();
  if (type == "object") return value.is_object();
  if (type == "array") return value.is_array();
  if (type == "null") return value.is_null();
  return false;
}

std::optional<std::string> validate(const json& schema, const json& value, const std::string& path)
{
  if (!schema.is_object()) return std::nullopt;

  if (schema.contains("type")) {
    const auto& type = schema["type"];
    bool ok = false;
    std::string expected;
    if (type.is_string()) {
      expected = type.get<std::string>();
      ok = typeMatches(expected, value);
    } else if (type.is_array()) {
      for (const auto& t : type) {
        if (!expected.empty()) expected += "|";
        expected += t.get<std::string>();
        if (typeMatches(t.get<std::string>(), value)) ok = true;
      }
    }
    if (!ok) return path + ": expected " + expected + ", got " + std::string(value.type_name());
  }

  if (schema.contains("enum")) {
    bool found = false;
    for (const auto& allowed : schema["enum"]) {
      if (allowed == value) {
        found = true;
        break;
      }
    }
    if (!found) return path + ": value " + value.dump() + " is not one of " + schema["enum"].dump();
  }

  if (value.is_object()) {
    if (schema.contains("required")) {
      for (const auto& key : schema["required"]) {
        if (!value.contains(key.get<std::string>())) {
          return path + ": missing required property '" + key.get<std::string>() + "'";
        }
      }
    }
    const json properties = schema.value("properties", json::object());
    for (const auto& [key, propValue] : value.items()) {
      if (properties.contains(key)) {
        if (auto err = validate(properties[key], propValue, path + "." + key)) return err;
      } else if (schema.contains("additionalProperties") && schema["additionalProperties"] == false) {
        return path + ": unexpected property '" + key + "'";
      }
    }
  }

  if (value.is_array()) {
    if (schema.contains("minItems") && value.size() < schema["minItems"].get<size_t>()) {
      return path + ": expected at least " + std::to_string(schema["minItems"].get<size_t>()) + " items";
    }
    if (schema.contains("maxItems") && value.size() > schema["maxItems"].get<size_t>()) {
      return path + ": expected at most " + std::to_string(schema["maxItems"].get<size_t>()) + " items";
    }
    if (schema.contains("items")) {
      for (size_t i = 0; i < value.size(); ++i) {
        if (auto err = validate(schema["items"], value[i], path + "[" + std::to_string(i) + "]"))
          return err;
      }
    }
  }

  if (value.is_number()) {
    const double v = value.get<double>();
    if (schema.contains("minimum") && v < schema["minimum"].get<double>()) {
      return path + ": value " + value.dump() + " is below minimum " + schema["minimum"].dump();
    }
    if (schema.contains("maximum") && v > schema["maximum"].get<double>()) {
      return path + ": value " + value.dump() + " is above maximum " + schema["maximum"].dump();
    }
  }

  return std::nullopt;
}

}  // namespace mcp::schema
