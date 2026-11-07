#include "mcp/session/SessionManager.h"

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/parsersettings.h"
#include "mcp/protocol/Registry.h"

namespace fs = std::filesystem;

namespace mcp {

namespace {

fs::path librariesDirOf(const Project& project)
{
  const auto libraries = project.config().value("libraries", nlohmann::json::object());
  return project.root() / libraries.value("dir", "libraries");
}

}  // namespace

SessionManager::SessionManager(std::string mode, Factory factory)
  : mode_(std::move(mode)), factory_(std::move(factory))
{
}

ProjectSession& SessionManager::open(const fs::path& dir)
{
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    throw ToolError("project folder does not exist or is not a directory: " + dir.string());
  }
  const fs::path canonical = fs::canonical(dir, ec);
  if (ec) throw ToolError("cannot resolve project folder: " + dir.string());

  if (auto *existing = find(canonical.string())) {
    setCurrent(existing);
    return *existing;
  }

  std::shared_ptr<Project> project;
  try {
    project = std::make_shared<Project>(canonical);
    project->loadOrCreate();
  } catch (const std::exception& e) {
    throw ToolError(std::string("cannot open project: ") + e.what());
  }
  // The project's libraries folder is searched first by use/include while
  // the project is open.
  add_librarydir_front(librariesDirOf(*project).string());
  sessions_.push_back(factory_(project));
  setCurrent(sessions_.back().get());
  return *current_;
}

void SessionManager::close(const std::string& id)
{
  for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
    if ((*it)->project().id() == id) {
      remove_librarydir(librariesDirOf((*it)->project()).string());
      const bool wasCurrent = current_ == it->get();
      if (wasCurrent) current_ = nullptr;
      sessions_.erase(it);
      if (wasCurrent) setCurrent(sessions_.empty() ? nullptr : sessions_.back().get());
      break;
    }
  }
}

std::vector<ProjectSession *> SessionManager::sessions() const
{
  std::vector<ProjectSession *> result;
  result.reserve(sessions_.size());
  for (const auto& session : sessions_) result.push_back(session.get());
  return result;
}

ProjectSession *SessionManager::find(const std::string& idOrPath) const
{
  for (const auto& session : sessions_) {
    if (session->project().id() == idOrPath) return session.get();
  }
  std::error_code ec;
  const fs::path canonical = fs::canonical(idOrPath, ec);
  if (ec) return nullptr;
  for (const auto& session : sessions_) {
    if (session->project().root() == canonical) return session.get();
  }
  return nullptr;
}

ProjectSession& SessionManager::resolve(const nlohmann::json& args)
{
  if (args.is_object() && args.contains("project") && args["project"].is_string()) {
    const std::string key = args["project"].get<std::string>();
    if (auto *session = find(key)) {
      setCurrent(session);
      return *session;
    }
    throw ToolError("project is not open: " + key + " (call project_open first)");
  }
  if (!current_) throw ToolError("no project is open: call project_open with the project folder");
  return *current_;
}

void SessionManager::setCurrent(ProjectSession *session)
{
  const bool changed = current_ != session;
  current_ = session;
  if (changed && currentChanged_) currentChanged_(session);
}

}  // namespace mcp
