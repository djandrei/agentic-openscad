#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "json/json.hpp"
#include "mcp/engine/CompileEngine.h"
#include "mcp/session/FileStore.h"
#include "mcp/session/MessageLog.h"
#include "mcp/session/Project.h"

namespace mcp {

// Text currently shown in an editor for a file, which may differ from disk.
struct EditorState {
  std::string text;
  std::string version;  // FileStore::versionOf(text)
  bool unsaved = false;
};

struct CompileRequest {
  std::string file;  // relative to the project root
  CompileMode mode = CompileMode::Preview;
  double timeoutSeconds = 120;
};

struct CompileOutcome {
  bool ok = false;      // parsed, evaluated, no errors, not timed out
  bool parsed = false;  // syntax was accepted
  bool timedOut = false;
  bool unsavedEditor = false;  // GUI: the editor text differed from disk and was compiled instead
  std::vector<LogMessage> messages;
  int errors = 0;
  int warnings = 0;
  double millis = 0;
  nlohmann::json geometry;  // Render: CompileEngine::geometrySummary
};

// One open project. Tool handlers are written against this interface; the
// headless and the GUI implementations provide the behaviour.
class ProjectSession
{
public:
  explicit ProjectSession(std::shared_ptr<Project> project)
    : project_(std::move(project)), files_(*project_)
  {
  }
  virtual ~ProjectSession() = default;

  Project& project() { return *project_; }
  const Project& project() const { return *project_; }
  FileStore& files() { return files_; }

  // Mode-specific status fields merged into the `status` tool result.
  virtual nlohmann::json status() const = 0;

  // True while the user has paused the agent (GUI only).
  virtual bool paused() const { return false; }
  // True while a compile or render started by the user is running (GUI only).
  virtual bool busy() const { return false; }

  // Editor contents for a file when it is open in an editor; nullopt when not
  // open or in headless mode.
  virtual std::optional<EditorState> editorState(const std::string& /*relativePath*/) const
  {
    return std::nullopt;
  }

  // Hooks invoked after the file store changed the disk.
  virtual void fileWritten(const FileChange& /*change*/) {}
  virtual void fileRemoved(const std::string& /*relativePath*/) {}
  virtual void fileRenamed(const std::string& /*from*/, const std::string& /*to*/) {}

  // Parses / previews / renders a project file and returns the structured
  // messages of that run. Throws ToolError when the file is missing, the
  // session is busy, or the session cannot compile at all (the default).
  virtual CompileOutcome compile(const CompileRequest& request);

protected:
  // Fills message list, counts and ok flag from the log captured since `firstSeq`.
  static void finishOutcome(CompileOutcome& outcome, std::uint64_t firstSeq, bool evaluatedWithoutError);

private:
  std::shared_ptr<Project> project_;
  FileStore files_;
};

}  // namespace mcp
