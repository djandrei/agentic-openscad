#pragma once

#include <string>

namespace mcp::docs {

// Plain text of an HTML document: scripts and styles dropped, block elements
// separated by newlines, headings prefixed with '#', entities decoded,
// whitespace collapsed.
std::string htmlToText(const std::string& html);

// The <h1> text if present, otherwise the <title>, otherwise empty.
std::string htmlTitle(const std::string& html);

}  // namespace mcp::docs
