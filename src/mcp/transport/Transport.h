#pragma once

#include <functional>

namespace mcp {

// Runs a unit of work on the thread that owns the Server and the sessions,
// and returns once it has completed. Transports call every Server method
// through their executor, so the protocol and session layers stay
// single-threaded. The headless mode uses directExecutor; the GUI marshals
// onto the Qt main thread.
using Executor = std::function<void(const std::function<void()>&)>;

inline void directExecutor(const std::function<void()>& fn)
{
  fn();
}

}  // namespace mcp
