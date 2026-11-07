#include "mcp/session/ProjectSession.h"

#include "mcp/protocol/Registry.h"

namespace mcp {

CompileOutcome ProjectSession::compile(const CompileRequest&)
{
  throw ToolError("this session cannot compile files");
}

nlohmann::json ProjectSession::cameraGet() const
{
  throw ToolError("this session has no view");
}

nlohmann::json ProjectSession::cameraSet(const nlohmann::json&)
{
  throw ToolError("this session has no view");
}

nlohmann::json ProjectSession::viewOptionsGet() const
{
  throw ToolError("this session has no view");
}

nlohmann::json ProjectSession::viewOptionsSet(const nlohmann::json&)
{
  throw ToolError("this session has no view");
}

std::vector<view::CaptureImage> ProjectSession::capture(const CaptureRequest&)
{
  throw ToolError("this session cannot capture views");
}

nlohmann::json ProjectSession::geometryInfo()
{
  throw ToolError("this session has no geometry");
}

nlohmann::json ProjectSession::exportGeometry(const std::string&, const std::string&,
                                              const nlohmann::json&)
{
  throw ToolError("this session cannot export");
}

nlohmann::json ProjectSession::paramsGet(const std::string&)
{
  throw ToolError("this session has no customizer");
}

nlohmann::json ProjectSession::paramsSet(const std::string&, const nlohmann::json&, const std::string&,
                                         bool)
{
  throw ToolError("this session has no customizer");
}

nlohmann::json ProjectSession::presetsList(const std::string&)
{
  throw ToolError("this session has no customizer");
}

nlohmann::json ProjectSession::presetSave(const std::string&, const std::string&)
{
  throw ToolError("this session has no customizer");
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
