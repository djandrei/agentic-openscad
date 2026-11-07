#include "mcp/session/ProjectSession.h"

#include "mcp/protocol/Registry.h"

namespace mcp {

CompileOutcome ProjectSession::compile(const CompileRequest&)
{
  throw ToolError("this session cannot compile files");
}

void ProjectSession::finishOutcome(CompileOutcome& outcome, std::uint64_t firstSeq,
                                   bool evaluatedWithoutError)
{
  outcome.messages = MessageLog::instance().since(firstSeq);
  const auto counts = MessageLog::count(outcome.messages);
  outcome.errors = counts.errors;
  outcome.warnings = counts.warnings;
  outcome.ok = evaluatedWithoutError && outcome.parsed && outcome.errors == 0 && !outcome.timedOut;
}

}  // namespace mcp
