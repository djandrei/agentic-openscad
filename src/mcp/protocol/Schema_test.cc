#include "mcp/protocol/Schema.h"

#include <catch2/catch_all.hpp>

using mcp::schema::validate;
using nlohmann::json;

TEST_CASE("Schema validation checks types", "[mcp][schema]")
{
  REQUIRE_FALSE(validate(json{{"type", "string"}}, json("x")).has_value());
  REQUIRE(validate(json{{"type", "string"}}, json(1)).has_value());
  REQUIRE_FALSE(validate(json{{"type", "integer"}}, json(3)).has_value());
  REQUIRE_FALSE(validate(json{{"type", "integer"}}, json(3.0)).has_value());
  REQUIRE(validate(json{{"type", "integer"}}, json(3.5)).has_value());
  REQUIRE_FALSE(validate(json{{"type", json::array({"string", "null"})}}, json(nullptr)).has_value());
  REQUIRE(validate(json{{"type", json::array({"string", "null"})}}, json(true)).has_value());
}

TEST_CASE("Schema validation checks object properties", "[mcp][schema]")
{
  const json schema{
    {"type", "object"},
    {"properties", json{{"name", json{{"type", "string"}}}, {"n", json{{"type", "integer"}}}}},
    {"required", json::array({"name"})},
    {"additionalProperties", false}};
  REQUIRE_FALSE(validate(schema, json{{"name", "a"}, {"n", 2}}).has_value());
  auto missing = validate(schema, json{{"n", 2}});
  REQUIRE(missing.has_value());
  REQUIRE(missing->find("name") != std::string::npos);
  auto wrongType = validate(schema, json{{"name", "a"}, {"n", "two"}});
  REQUIRE(wrongType.has_value());
  REQUIRE(wrongType->find("$.n") != std::string::npos);
  REQUIRE(validate(schema, json{{"name", "a"}, {"extra", 1}}).has_value());
}

TEST_CASE("Schema validation checks enums, arrays and ranges", "[mcp][schema]")
{
  const json enumSchema{{"type", "string"}, {"enum", json::array({"iso", "top"})}};
  REQUIRE_FALSE(validate(enumSchema, json("iso")).has_value());
  REQUIRE(validate(enumSchema, json("left")).has_value());

  const json arraySchema{
    {"type", "array"}, {"items", json{{"type", "number"}}}, {"minItems", 1}, {"maxItems", 3}};
  REQUIRE_FALSE(validate(arraySchema, json::array({1, 2.5})).has_value());
  REQUIRE(validate(arraySchema, json::array()).has_value());
  REQUIRE(validate(arraySchema, json::array({1, 2, 3, 4})).has_value());
  REQUIRE(validate(arraySchema, json::array({1, "x"})).has_value());

  const json lengthSchema{{"type", "string"}, {"minLength", 1}, {"maxLength", 3}};
  REQUIRE_FALSE(validate(lengthSchema, json("ab")).has_value());
  REQUIRE(validate(lengthSchema, json("")).has_value());
  REQUIRE(validate(lengthSchema, json("abcd")).has_value());

  const json rangeSchema{{"type", "integer"}, {"minimum", 1}, {"maximum", 65535}};
  REQUIRE_FALSE(validate(rangeSchema, json(80)).has_value());
  REQUIRE(validate(rangeSchema, json(0)).has_value());
  REQUIRE(validate(rangeSchema, json(70000)).has_value());
}
