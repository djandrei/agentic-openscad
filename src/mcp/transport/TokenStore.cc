#include "mcp/transport/TokenStore.h"

#include <algorithm>

#include "mcp/transport/HttpTransport.h"

namespace mcp {

namespace {

// Comparison whose duration does not depend on where the strings differ.
bool constantTimeEquals(const std::string& a, const std::string& b)
{
  if (a.size() != b.size()) return false;
  unsigned char diff = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
  }
  return diff == 0;
}

}  // namespace

TokenEntry TokenStore::add(const std::string& label)
{
  const std::string value = generateToken();
  const std::lock_guard<std::mutex> lock(mutex_);
  return insertLocked(value, label);
}

TokenEntry TokenStore::addValue(const std::string& value, const std::string& label)
{
  const std::lock_guard<std::mutex> lock(mutex_);
  for (const TokenEntry& entry : entries_) {
    if (entry.value == value) return entry;
  }
  return insertLocked(value, label);
}

bool TokenStore::remove(int id)
{
  const std::lock_guard<std::mutex> lock(mutex_);
  const auto it = std::find_if(entries_.begin(), entries_.end(),
                               [id](const TokenEntry& entry) { return entry.id == id; });
  if (it == entries_.end()) return false;
  entries_.erase(it);
  return true;
}

std::vector<TokenEntry> TokenStore::list() const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return entries_;
}

bool TokenStore::authenticate(const std::string& value)
{
  const std::lock_guard<std::mutex> lock(mutex_);
  TokenEntry *match = nullptr;
  for (TokenEntry& entry : entries_) {
    if (constantTimeEquals(entry.value, value)) match = &entry;
  }
  if (!match) return false;
  match->lastUsed = std::chrono::system_clock::now();
  return true;
}

TokenEntry TokenStore::insertLocked(const std::string& value, const std::string& label)
{
  TokenEntry entry;
  entry.id = nextId_++;
  entry.value = value;
  entry.label = label;
  entry.created = std::chrono::system_clock::now();
  entries_.push_back(entry);
  return entry;
}

}  // namespace mcp
