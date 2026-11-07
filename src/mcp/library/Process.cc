#include "mcp/library/Process.h"

#include <string>
#include <vector>

#include "mcp/protocol/Registry.h"

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif

namespace mcp::library {

#ifndef _WIN32

int runProcess(const std::vector<std::string>& args, std::string *output)
{
  if (args.empty()) throw ToolError("no program given");
  int pipefd[2];
  if (pipe(pipefd) != 0) throw ToolError("cannot create pipe");
  const pid_t pid = fork();
  if (pid < 0) throw ToolError("cannot fork");
  if (pid == 0) {
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[0]);
    close(pipefd[1]);
    std::vector<char *> argv;
    for (const auto& arg : args) argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  close(pipefd[1]);
  std::string collected;
  char buffer[4096];
  for (;;) {
    const ssize_t n = read(pipefd[0], buffer, sizeof(buffer));
    if (n <= 0) break;
    collected.append(buffer, static_cast<size_t>(n));
  }
  close(pipefd[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  if (output) *output = collected;
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return -1;
}

bool programAvailable(const std::string& program)
{
  std::string ignored;
  try {
    return runProcess({program, "--version"}, &ignored) == 0;
  } catch (const ToolError&) {
    return false;
  }
}

#else

int runProcess(const std::vector<std::string>&, std::string *)
{
  throw ToolError("running external programs is not supported on this platform");
}

bool programAvailable(const std::string&)
{
  return false;
}

#endif

}  // namespace mcp::library
