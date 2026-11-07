#include "mcp/transport/StdioTransport.h"

#include <istream>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>

#include "mcp/protocol/JsonRpc.h"

namespace mcp {

StdioTransport::StdioTransport(Server& server, std::istream& in, std::ostream& out, Executor executor)
  : server_(server), in_(in), out_(out), executor_(std::move(executor))
{
}

void StdioTransport::send(const nlohmann::json& message)
{
  const std::lock_guard<std::mutex> lock(writeMutex_);
  out_ << jsonrpc::serialize(message) << '\n';
  out_.flush();
}

int StdioTransport::run()
{
  server_.setNotificationSink([this](const nlohmann::json& message) { send(message); });

  std::string line;
  while (std::getline(in_, line)) {
    if (line.empty() || line.find_first_not_of(" \t\r") == std::string::npos) continue;
    std::optional<nlohmann::json> response;
    executor_([&] { response = server_.handleText(line); });
    if (response) send(*response);
  }

  server_.setNotificationSink(nullptr);
  return 0;
}

}  // namespace mcp
