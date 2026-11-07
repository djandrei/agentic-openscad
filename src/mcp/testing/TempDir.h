#pragma once

#include <filesystem>
#include <string>
#include <system_error>

#include "mcp/transport/HttpTransport.h"

namespace mcp::testing {

// Unique temporary directory removed on destruction. Test-only helper.
struct TempDir {
  std::filesystem::path path;

  TempDir()
  {
    path = std::filesystem::temp_directory_path() / ("agentic-openscad-test-" + generateToken(6));
    std::filesystem::create_directories(path);
  }
  ~TempDir()
  {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
};

}  // namespace mcp::testing
