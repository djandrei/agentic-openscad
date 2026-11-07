#include "mcp/headless/HeadlessMain.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <utility>
#include <csignal>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>

#include "mcp/BuildInfo.h"
#include "mcp/session/EngineSession.h"
#include "mcp/protocol/Registry.h"
#include "mcp/protocol/Server.h"
#include "mcp/session/MessageLog.h"
#include "mcp/session/SessionManager.h"
#include "mcp/tools/CompileTools.h"
#include "mcp/tools/CoreTools.h"
#include "mcp/tools/DocsTools.h"
#include "mcp/tools/FileTools.h"
#include "mcp/tools/LibraryTools.h"
#include "mcp/tools/ParamTools.h"
#include "mcp/tools/ViewTools.h"
#include "mcp/transport/HttpTransport.h"
#include "mcp/transport/StdioTransport.h"
#include "version.h"

namespace mcp {

namespace {

std::atomic<bool> g_stopRequested{false};

void onSignal(int)
{
  g_stopRequested = true;
}

// Hands closures from other threads to the main thread and waits for them.
class MainThreadQueue
{
public:
  void execute(const std::function<void()>& fn)
  {
    std::promise<void> done;
    auto future = done.get_future();
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      if (stopped_) return;
      tasks_.emplace_back(&fn, &done);
    }
    ready_.notify_one();
    future.wait();
  }

  // Runs queued tasks on the calling (main) thread until `stop()` is true.
  void run(const std::function<bool()>& stop)
  {
    while (!stop()) {
      std::unique_lock<std::mutex> lock(mutex_);
      ready_.wait_for(lock, std::chrono::milliseconds(200), [&] { return !tasks_.empty(); });
      if (tasks_.empty()) continue;
      auto [fn, done] = tasks_.front();
      tasks_.pop_front();
      lock.unlock();
      (*fn)();
      done->set_value();
    }
  }

  void shutdown()
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = true;
    for (auto& [fn, done] : tasks_) done->set_value();  // release waiting transport threads
    tasks_.clear();
  }

private:
  std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<std::pair<const std::function<void()> *, std::promise<void> *>> tasks_;
  bool stopped_ = false;
};

bool writeTokenFile(const std::string& path, const std::string& token)
{
  std::ofstream out(path, std::ios::trunc);
  if (!out) return false;
  out << token << '\n';
  out.close();
  std::error_code ec;
  std::filesystem::permissions(path,
                               std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, ec);
  return true;
}

}  // namespace

int runHeadless(const Options& options)
{
  ToolRegistry tools;
  ResourceRegistry resources;
  PromptRegistry prompts;
  SessionManager sessions("headless", [](std::shared_ptr<Project> project) {
    return std::make_unique<EngineSession>(std::move(project));
  });
  MessageLog::instance().attach();
  registerCoreTools(tools, sessions);
  registerFileTools(tools, sessions);
  registerCompileTools(tools, sessions);
  registerViewTools(tools, sessions);
  registerParamTools(tools, sessions);
  registerLibraryTools(tools, sessions);
  registerDocsTools(tools, sessions);
  registerDocsResources(resources, prompts, sessions);
  Server server(ServerInfo{serverName(), openscad_versionnumber, serverInstructions()}, tools, resources,
                prompts);

  if (!options.project.empty()) {
    try {
      sessions.open(options.project);
    } catch (const std::exception& e) {
      std::cerr << "agentic-openscad: " << e.what() << "\n";
      return 1;
    }
  }

  if (options.stdio) {
    StdioTransport transport(server, std::cin, std::cout);
    return transport.run();
  }

  // Requests arrive on the transport thread but must run on the main thread:
  // OpenSCAD's recursion/stack check is calibrated for the main thread's
  // stack, and evaluation on another thread trips it. The main thread below
  // drains this queue.
  MainThreadQueue queue;
  HttpOptions httpOptions;
  httpOptions.port = static_cast<unsigned short>(options.port);
  httpOptions.token = options.token.empty() ? generateToken() : options.token;
  HttpTransport transport(server, httpOptions,
                          [&queue](const std::function<void()>& fn) { queue.execute(fn); });
  try {
    transport.start();
  } catch (const std::exception& e) {
    std::cerr << "agentic-openscad: " << e.what() << "\n";
    return 1;
  }
  if (!options.tokenFile.empty() && !writeTokenFile(options.tokenFile, httpOptions.token)) {
    std::cerr << "agentic-openscad: cannot write token file " << options.tokenFile << "\n";
    transport.stop();
    return 1;
  }
  std::cerr << "agentic-openscad: MCP server listening on " << transport.url() << "\n";
  if (options.tokenFile.empty()) {
    std::cerr << "agentic-openscad: bearer token " << httpOptions.token << "\n";
  }
  std::cerr << "agentic-openscad: claude mcp add --transport http openscad " << transport.url()
            << " --header \"Authorization: Bearer " << httpOptions.token << "\"\n";

  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);
  queue.run([] { return g_stopRequested.load(); });
  queue.shutdown();
  transport.stop();
  return 0;
}

}  // namespace mcp
