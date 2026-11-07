#include "mcp/headless/HeadlessSession.h"

#include <chrono>
#include <filesystem>
#include <stdexcept>

#include "mcp/engine/CompileEngine.h"
#include "mcp/protocol/Registry.h"

namespace mcp {

nlohmann::json HeadlessSession::status() const
{
  return nlohmann::json{{"mode", "headless"}, {"busy", false}, {"paused", false}};
}

CompileOutcome HeadlessSession::compile(const CompileRequest& request)
{
  std::filesystem::path file;
  try {
    file = project().resolve(request.file);
  } catch (const std::invalid_argument& e) {
    throw ToolError(e.what());
  }
  std::error_code ec;
  if (!std::filesystem::is_regular_file(file, ec)) throw ToolError("file not found: " + request.file);

  const auto firstSeq = MessageLog::instance().lastSeq();
  const auto started = std::chrono::steady_clock::now();
  const EngineResult result = CompileEngine::run(file, request.mode, request.timeoutSeconds);

  CompileOutcome outcome;
  outcome.parsed = result.parsed;
  outcome.timedOut = result.timedOut;
  outcome.millis =
    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  if (request.mode == CompileMode::Render) {
    lastGeometry_ = result.geometry;
    outcome.geometry = CompileEngine::geometrySummary(result.geometry.get());
  }
  const bool evaluated = request.mode == CompileMode::Check ? result.parsed : result.instantiated;
  finishOutcome(outcome, firstSeq, evaluated);
  return outcome;
}

}  // namespace mcp
