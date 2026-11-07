#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "json/json.hpp"
#include "utils/printutils.h"

namespace mcp {

// One OpenSCAD log message in structured form.
struct LogMessage {
  std::uint64_t seq = 0;
  std::string severity;  // error | warning | deprecated | echo | trace | info
  std::string group;     // OpenSCAD message group name
  std::string text;
  std::string file;  // absolute path, empty when unknown
  int line = 0;      // 1-based, 0 when unknown
  int column = 0;

  nlohmann::json toJson() const;
};

// Process-wide ring buffer of every message OpenSCAD prints, with monotonic
// sequence numbers so a tool can return "the messages of this run" and
// log_get can page through history. Thread-safe (render runs on a worker
// thread in the GUI).
class MessageLog
{
public:
  static constexpr size_t kCapacity = 5000;

  static MessageLog& instance();

  // Registers/unregisters the printutils listener.
  void attach();
  void detach();

  void record(const Message& message);

  std::uint64_t lastSeq() const;
  // Messages with seq > since, oldest first, at most `limit` (0 = all).
  std::vector<LogMessage> since(std::uint64_t since, size_t limit = 0) const;

  struct Counts {
    int errors = 0;
    int warnings = 0;
  };
  static Counts count(const std::vector<LogMessage>& messages);
  static std::string severityOf(message_group group);

private:
  mutable std::mutex mutex_;
  std::deque<LogMessage> buffer_;
  std::uint64_t seq_ = 0;
  bool attached_ = false;
};

}  // namespace mcp
