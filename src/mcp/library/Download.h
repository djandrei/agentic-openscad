#pragma once

#include <string>

namespace mcp::library {

// Fetches `url` synchronously and returns the body. Supports http(s) with
// redirects (up to 5) and TLS verification against the system certificates,
// file:// URLs and plain local paths. Throws ToolError; for HTTP failures the
// error data carries {"status": code} so callers can try alternatives.
std::string download(const std::string& url);

}  // namespace mcp::library
