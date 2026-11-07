#include "mcp/headless/HeadlessSession.h"

namespace mcp {

nlohmann::json HeadlessSession::status() const
{
  return nlohmann::json{{"mode", "headless"}, {"busy", false}, {"paused", false}};
}

}  // namespace mcp
