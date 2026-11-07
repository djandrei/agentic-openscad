#include "mcp/session/MessageLog.h"

#include <mutex>
#include <string>
#include <vector>

#include "core/AST.h"

namespace mcp {

nlohmann::json LogMessage::toJson() const
{
  nlohmann::json j{{"seq", seq}, {"severity", severity}, {"group", group}, {"text", text}};
  if (!file.empty()) j["file"] = file;
  if (line > 0) {
    j["line"] = line;
    j["column"] = column;
  }
  return j;
}

MessageLog& MessageLog::instance()
{
  static MessageLog log;
  return log;
}

namespace {

void listener(const Message& message, void *userdata)
{
  static_cast<MessageLog *>(userdata)->record(message);
}

}  // namespace

void MessageLog::attach()
{
  if (attached_) return;
  attached_ = true;
  add_output_listener(&listener, this);
}

void MessageLog::detach()
{
  if (!attached_) return;
  attached_ = false;
  remove_output_listener(&listener, this);
}

std::string MessageLog::severityOf(message_group group)
{
  switch (group) {
  case message_group::Error:
  case message_group::Parser_Error:
  case message_group::Export_Error:
  case message_group::UI_Error:       return "error";
  case message_group::Warning:
  case message_group::UI_Warning:
  case message_group::Font_Warning:
  case message_group::Export_Warning: return "warning";
  case message_group::Deprecated:     return "deprecated";
  case message_group::Echo:           return "echo";
  case message_group::Trace:          return "trace";
  default:                            return "info";
  }
}

void MessageLog::record(const Message& message)
{
  LogMessage entry;
  entry.severity = severityOf(message.group);
  entry.group = getGroupName(message.group);
  entry.text = message.msg;
  if (!message.loc.isNone()) {
    entry.file = message.loc.filePath().generic_string();
    entry.line = message.loc.firstLine();
    entry.column = message.loc.firstColumn();
  }
  const std::lock_guard<std::mutex> lock(mutex_);
  entry.seq = ++seq_;
  buffer_.push_back(std::move(entry));
  while (buffer_.size() > kCapacity) buffer_.pop_front();
}

std::uint64_t MessageLog::lastSeq() const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return seq_;
}

std::vector<LogMessage> MessageLog::since(std::uint64_t since, size_t limit) const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  std::vector<LogMessage> result;
  for (const auto& entry : buffer_) {
    if (entry.seq <= since) continue;
    if (limit && result.size() >= limit) break;
    result.push_back(entry);
  }
  return result;
}

MessageLog::Counts MessageLog::count(const std::vector<LogMessage>& messages)
{
  Counts counts;
  for (const auto& message : messages) {
    if (message.severity == "error") ++counts.errors;
    else if (message.severity == "warning" || message.severity == "deprecated") ++counts.warnings;
  }
  return counts;
}

}  // namespace mcp
