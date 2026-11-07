#include "mcp/docs/SearchIndex.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mcp::docs {

namespace {

bool tokenChar(unsigned char c)
{
  return std::isalnum(c) || c == '_' || c == '$';
}

std::string lower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

}  // namespace

std::vector<std::string> SearchIndex::tokenize(const std::string& text)
{
  std::vector<std::string> tokens;
  std::string current;
  for (unsigned char c : text) {
    if (tokenChar(c)) {
      current.push_back(static_cast<char>(std::tolower(c)));
    } else if (!current.empty()) {
      if (current.size() >= 2) tokens.push_back(current);
      current.clear();
    }
  }
  if (current.size() >= 2) tokens.push_back(current);
  return tokens;
}

void SearchIndex::add(Document document)
{
  const auto existing = byId_.find(document.id);
  if (existing != byId_.end()) {
    // Replace: rebuild is simpler than patching postings.
    std::vector<Document> all = documents_;
    all[existing->second] = std::move(document);
    clear();
    for (auto& doc : all) add(std::move(doc));
    return;
  }
  const size_t index = documents_.size();
  std::map<std::string, unsigned> counts;
  for (const auto& token : tokenize(document.text)) ++counts[token];
  for (const auto& token : tokenize(document.title)) counts[token] += 3;
  size_t length = 0;
  for (const auto& [token, count] : counts) {
    postings_[token].push_back({index, count});
    length += count;
  }
  lengths_.push_back(std::max<size_t>(length, 1));
  byId_[document.id] = index;
  documents_.push_back(std::move(document));
}

void SearchIndex::clear()
{
  documents_.clear();
  lengths_.clear();
  postings_.clear();
  byId_.clear();
}

const Document *SearchIndex::get(const std::string& id) const
{
  const auto it = byId_.find(id);
  return it == byId_.end() ? nullptr : &documents_[it->second];
}

std::string makeSnippet(const std::string& text, const std::vector<std::string>& terms, size_t radius)
{
  const std::string lowered = lower(text);
  size_t best = std::string::npos;
  for (const auto& term : terms) {
    const size_t pos = lowered.find(term);
    if (pos != std::string::npos && (best == std::string::npos || pos < best)) best = pos;
  }
  if (best == std::string::npos) best = 0;
  const size_t start = best > radius ? best - radius : 0;
  const size_t end = std::min(text.size(), best + radius);
  std::string snippet = text.substr(start, end - start);
  std::replace(snippet.begin(), snippet.end(), '\n', ' ');
  if (start > 0) snippet = "…" + snippet;
  if (end < text.size()) snippet += "…";
  return snippet;
}

std::vector<Hit> SearchIndex::search(const std::string& query, const std::string& scope,
                                     size_t limit) const
{
  const auto terms = tokenize(query);
  if (terms.empty() || documents_.empty()) return {};
  std::map<size_t, double> scores;
  const double n = static_cast<double>(documents_.size());
  for (const auto& term : terms) {
    const auto it = postings_.find(term);
    if (it == postings_.end()) continue;
    const double idf = std::log(1.0 + n / static_cast<double>(it->second.size()));
    for (const auto& posting : it->second) {
      const double tf =
        static_cast<double>(posting.count) / std::sqrt(static_cast<double>(lengths_[posting.document]));
      scores[posting.document] += tf * idf;
    }
  }
  // Prefer documents matching every term.
  std::vector<std::pair<size_t, double>> ranked;
  for (const auto& [document, score] : scores) {
    if (!scope.empty() && scope != "all" && documents_[document].scope != scope) continue;
    size_t matched = 0;
    for (const auto& term : terms) {
      const auto it = postings_.find(term);
      if (it == postings_.end()) continue;
      if (std::any_of(it->second.begin(), it->second.end(),
                      [&](const Posting& p) { return p.document == document; })) {
        ++matched;
      }
    }
    ranked.emplace_back(document, score * (1.0 + static_cast<double>(matched)));
  }
  std::sort(ranked.begin(), ranked.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
  std::vector<Hit> hits;
  for (const auto& [document, score] : ranked) {
    if (hits.size() >= limit) break;
    const Document& doc = documents_[document];
    hits.push_back(Hit{doc.id, doc.title, doc.scope, score, makeSnippet(doc.text, terms)});
  }
  return hits;
}

}  // namespace mcp::docs
