#include "mcp/docs/Html.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <string>

namespace mcp::docs {

namespace {

constexpr char kPreSpace = '\x01';
constexpr char kPreNewline = '\x02';

std::string lower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

void appendUtf8(std::string& out, unsigned long code)
{
  if (code < 0x80) {
    out.push_back(static_cast<char>(code));
  } else if (code < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  } else if (code < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (code >> 18)));
    out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  }
}

std::string decodeEntities(const std::string& text)
{
  static const std::map<std::string, const char *> named{
    {"amp", "&"},    {"lt", "<"},    {"gt", ">"},    {"quot", "\""},  {"apos", "'"},  {"nbsp", " "},
    {"copy", "©"},   {"ndash", "–"}, {"mdash", "—"}, {"hellip", "…"}, {"deg", "°"},   {"times", "×"},
    {"larr", "←"},   {"rarr", "→"},  {"le", "≤"},    {"ge", "≥"},     {"ne", "≠"},    {"minus", "−"},
    {"middot", "·"}, {"laquo", "«"}, {"raquo", "»"}, {"lsquo", "‘"},  {"rsquo", "’"}, {"ldquo", "“"},
    {"rdquo", "”"},  {"bull", "•"},  {"pi", "π"},    {"sup2", "²"},   {"sup3", "³"},  {"frac12", "½"}};
  std::string out;
  out.reserve(text.size());
  size_t i = 0;
  while (i < text.size()) {
    if (text[i] != '&') {
      out.push_back(text[i++]);
      continue;
    }
    const size_t end = text.find(';', i);
    if (end == std::string::npos || end - i > 10) {
      out.push_back(text[i++]);
      continue;
    }
    const std::string entity = text.substr(i + 1, end - i - 1);
    if (!entity.empty() && entity[0] == '#') {
      const unsigned long code = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X')
                                   ? std::strtoul(entity.c_str() + 2, nullptr, 16)
                                   : std::strtoul(entity.c_str() + 1, nullptr, 10);
      if (code > 0) appendUtf8(out, code);
    } else {
      const auto it = named.find(entity);
      if (it != named.end()) out += it->second;
      else out += "&" + entity + ";";
    }
    i = end + 1;
  }
  return out;
}

bool isBlockTag(const std::string& tag)
{
  static const char *blocks[] = {"p",   "div",        "br",      "li",         "ul",     "ol",
                                 "tr",  "td",         "th",      "table",      "pre",    "hr",
                                 "h1",  "h2",         "h3",      "h4",         "h5",     "h6",
                                 "dt",  "dd",         "section", "article",    "header", "footer",
                                 "nav", "blockquote", "figure",  "figcaption", "title"};
  for (const char *b : blocks) {
    if (tag == b) return true;
  }
  return false;
}

bool closingEndsLine(const std::string& tag)
{
  static const char *tags[] = {
    "p",  "div",     "pre",     "table",  "blockquote", "h1",  "h2",     "h3",         "h4",   "h5",
    "h6", "section", "article", "header", "footer",     "nav", "figure", "figcaption", "title"};
  for (const char *t : tags) {
    if (tag == t) return true;
  }
  return false;
}

std::string collapse(const std::string& text)
{
  std::string out;
  out.reserve(text.size());
  int newlines = 0;
  bool space = false;
  for (char c : text) {
    if (c == '\n') {
      if (newlines < 2) out.push_back('\n');
      ++newlines;
      space = false;
    } else if (c == ' ' || c == '\t' || c == '\r') {
      space = true;
    } else {
      if (space && !out.empty() && out.back() != '\n') out.push_back(' ');
      space = false;
      newlines = 0;
      out.push_back(c);
    }
  }
  // Trim trailing whitespace on each line.
  std::string trimmed;
  trimmed.reserve(out.size());
  size_t lineStart = 0;
  while (lineStart <= out.size()) {
    size_t lineEnd = out.find('\n', lineStart);
    if (lineEnd == std::string::npos) lineEnd = out.size();
    size_t end = lineEnd;
    while (end > lineStart && out[end - 1] == ' ') --end;
    trimmed.append(out, lineStart, end - lineStart);
    if (lineEnd < out.size()) trimmed.push_back('\n');
    lineStart = lineEnd + 1;
  }
  while (!trimmed.empty() && (trimmed.front() == '\n' || trimmed.front() == ' ')) trimmed.erase(0, 1);
  return trimmed;
}

}  // namespace

std::string htmlToText(const std::string& html)
{
  std::string out;
  out.reserve(html.size());
  size_t i = 0;
  bool inPre = false;
  while (i < html.size()) {
    if (html[i] != '<') {
      if (inPre) {
        // Protect code indentation and line breaks from whitespace collapsing.
        if (html[i] == ' ') out.push_back(kPreSpace);
        else if (html[i] == '\n') out.push_back(kPreNewline);
        else if (html[i] == '\t') out.append(2, kPreSpace);
        else out.push_back(html[i]);
      } else {
        out.push_back(html[i] == '\n' ? ' ' : html[i]);
      }
      ++i;
      continue;
    }
    if (html.compare(i, 4, "<!--") == 0) {
      const size_t end = html.find("-->", i);
      i = end == std::string::npos ? html.size() : end + 3;
      continue;
    }
    const size_t close = html.find('>', i);
    if (close == std::string::npos) break;
    std::string tag = html.substr(i + 1, close - i - 1);
    const bool closing = !tag.empty() && tag[0] == '/';
    if (closing) tag.erase(0, 1);
    size_t nameEnd = 0;
    while (nameEnd < tag.size() && !std::isspace(static_cast<unsigned char>(tag[nameEnd])) &&
           tag[nameEnd] != '/') {
      ++nameEnd;
    }
    const std::string name = lower(tag.substr(0, nameEnd));
    i = close + 1;

    if (!closing && (name == "script" || name == "style")) {
      const size_t end = lower(html).find("</" + name, i);
      i = end == std::string::npos ? html.size() : html.find('>', end) + 1;
      continue;
    }
    if (name == "pre") inPre = !closing;
    if (!closing && (name == "h1" || name == "h2" || name == "h3")) {
      out += "\n\n" + std::string(name[1] - '0', '#') + " ";
      continue;
    }
    if (isBlockTag(name)) {
      // Every block starts on a new line; only "paragraph-like" blocks also
      // end one, so list items and table cells do not produce blank lines.
      if (!closing || closingEndsLine(name)) out.push_back('\n');
    } else if (name == "img") {
      out.push_back(' ');
    }
  }
  std::string text = collapse(decodeEntities(out));
  std::replace(text.begin(), text.end(), kPreSpace, ' ');
  std::replace(text.begin(), text.end(), kPreNewline, '\n');
  return text;
}

std::string htmlTitle(const std::string& html)
{
  const std::string lowered = lower(html);
  for (const char *tag : {"<h1", "<title"}) {
    const size_t start = lowered.find(tag);
    if (start == std::string::npos) continue;
    const size_t open = lowered.find('>', start);
    const size_t end = lowered.find(std::string("</") + (tag + 1), open);
    if (open == std::string::npos || end == std::string::npos) continue;
    std::string text = htmlToText(html.substr(open + 1, end - open - 1));
    text.erase(std::remove(text.begin(), text.end(), '\n'), text.end());
    while (!text.empty() && text.front() == '#') text.erase(0, 1);
    while (!text.empty() && text.front() == ' ') text.erase(0, 1);
    if (!text.empty()) return text;
  }
  return "";
}

}  // namespace mcp::docs
