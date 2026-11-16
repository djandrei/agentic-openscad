#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace mcp {

struct TokenEntry {
  int id = 0;         // stable handle for the UI, never reused
  std::string value;  // the bearer token
  std::string label;  // may be empty
  std::chrono::system_clock::time_point created;
  std::optional<std::chrono::system_clock::time_point> lastUsed;
};

// In-memory set of bearer tokens the HTTP transport accepts. Nothing is
// persisted. authenticate() runs on the transport's serving thread, the other
// calls on the viewer's main thread, so every member takes the mutex. An
// empty store authenticates nothing.
class TokenStore
{
public:
  // Adds a token with a freshly generated value.
  TokenEntry add(const std::string& label);
  // Adds a token with a given value (the startup token). If the value is
  // already present, returns the existing entry unchanged.
  TokenEntry addValue(const std::string& value, const std::string& label);
  // Returns false if no entry has this id.
  bool remove(int id);
  // Copies of the entries, in creation order.
  std::vector<TokenEntry> list() const;
  // True if `value` matches a token; stamps that token's lastUsed.
  bool authenticate(const std::string& value);

private:
  TokenEntry insertLocked(const std::string& value, const std::string& label);

  mutable std::mutex mutex_;
  std::vector<TokenEntry> entries_;
  int nextId_ = 1;
};

}  // namespace mcp
