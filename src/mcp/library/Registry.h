#pragma once

#include <string>
#include <vector>

#include "json/json.hpp"

namespace mcp {
class Project;
}

namespace mcp::library {

struct LibraryEntry {
  std::string name;
  std::string description;
  std::string repository;  // git repository URL
  std::string ref;         // branch or tag; empty = try main, then master
  std::string license;
  std::string docs;
  std::string category;
  std::string usage;  // e.g. "include <BOSL2/std.scad>"
  bool bundled = false;
  std::string source;  // registry name this entry came from

  nlohmann::json toJson() const;
};

// The registry built into the binary, seeded from https://openscad.org/libraries.html.
const std::string& bundledRegistryJson();

// Parses a registry document: {"name": ..., "libraries": [{name, repository, ...}]}.
std::vector<LibraryEntry> parseRegistry(const nlohmann::json& document, const std::string& sourceName);

// Bundled entries plus the registries listed in the project config
// (`registries`: paths relative to the project or URLs). Later registries
// override earlier entries with the same name. Problems loading a registry
// are appended to `problems` instead of failing.
std::vector<LibraryEntry> loadRegistries(const Project& project, std::vector<std::string> *problems);

const LibraryEntry *findEntry(const std::vector<LibraryEntry>& entries, const std::string& name);

// Direct archive download URLs for a repository on a known forge (GitHub,
// GitLab, Codeberg) for the given ref, or the candidates to try when ref is
// empty (main, master). Empty when the host is unknown.
std::vector<std::string> archiveCandidates(const std::string& repository, const std::string& ref);

// Last path component of a repository URL without ".git".
std::string repositoryName(const std::string& repository);

}  // namespace mcp::library
