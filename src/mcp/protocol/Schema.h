#pragma once

#include <optional>
#include <string>

#include "json/json.hpp"

// A deliberately small JSON Schema validator for tool arguments. Supports the
// subset used by our tool definitions: type (string or list), required,
// properties, additionalProperties: false, enum, items, minItems, maxItems,
// minimum, maximum.
namespace mcp::schema {

using json = nlohmann::json;

// Returns an error description, or nullopt when `value` conforms to `schema`.
std::optional<std::string> validate(const json& schema, const json& value,
                                    const std::string& path = "$");

}  // namespace mcp::schema
