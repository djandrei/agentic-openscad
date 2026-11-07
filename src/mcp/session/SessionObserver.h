#pragma once

#include <string>

namespace mcp {

class EngineSession;
struct CompileOutcome;
struct CompileRequest;
struct FileChange;

// Receives what happens in a session so a viewer can mirror it. All calls
// are made on the thread that runs the tools (the main thread), after the
// session's own state has been updated. Default implementations do nothing.
class SessionObserver
{
public:
  virtual ~SessionObserver() = default;

  virtual void fileWritten(EngineSession& /*session*/, const FileChange& /*change*/) {}
  virtual void fileRemoved(EngineSession& /*session*/, const std::string& /*relativePath*/) {}
  virtual void fileRenamed(EngineSession& /*session*/, const std::string& /*from*/,
                           const std::string& /*to*/)
  {
  }
  // After check/preview/render; the session's lastResult() holds the tree and geometry.
  virtual void compileFinished(EngineSession& /*session*/, const CompileRequest& /*request*/,
                               const CompileOutcome& /*outcome*/)
  {
  }
  virtual void cameraChanged(EngineSession& /*session*/) {}
  virtual void viewOptionsChanged(EngineSession& /*session*/) {}
  virtual void paramsChanged(EngineSession& /*session*/, const std::string& /*relativePath*/) {}
  virtual void librariesChanged(EngineSession& /*session*/) {}
};

}  // namespace mcp
