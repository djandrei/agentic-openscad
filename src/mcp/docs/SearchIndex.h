#pragma once

#include <map>
#include <string>
#include <vector>

namespace mcp::docs {

struct Document {
  std::string id;
  std::string title;
  std::string scope;  // manual | cheatsheet | builtins | libraries
  std::string text;
};

struct Hit {
  std::string id;
  std::string title;
  std::string scope;
  double score = 0;
  std::string snippet;
};

// Small inverted index with tf-idf ranking and a title bonus. Tokens are
// lower-case runs of letters, digits, '_' and '$' (so "$fn" and
// "linear_extrude" stay whole).
class SearchIndex
{
public:
  static std::vector<std::string> tokenize(const std::string& text);

  void add(Document document);
  void clear();
  size_t size() const { return documents_.size(); }
  const Document *get(const std::string& id) const;
  const std::vector<Document>& documents() const { return documents_; }

  // Hits ordered by score; `scope` empty or "all" searches everything.
  std::vector<Hit> search(const std::string& query, const std::string& scope, size_t limit) const;

private:
  struct Posting {
    size_t document;
    unsigned count;
  };
  std::vector<Document> documents_;
  std::vector<size_t> lengths_;
  std::map<std::string, std::vector<Posting>> postings_;
  std::map<std::string, size_t> byId_;
};

std::string makeSnippet(const std::string& text, const std::vector<std::string>& terms,
                        size_t radius = 110);

}  // namespace mcp::docs
