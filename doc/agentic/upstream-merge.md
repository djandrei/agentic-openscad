# Tracking upstream OpenSCAD

Agentic-OpenSCAD is a fork of https://github.com/openscad/openscad that merges
upstream `master` periodically. To keep merges cheap, fork code lives in its own
directories and touches upstream files only at a small, documented set of hook
points. Keep this list current whenever a hook is added or removed.

## Fork-only locations (no upstream conflicts expected)

| Path | Purpose |
|---|---|
| `src/mcp/` | MCP server: protocol, transports, sessions (GUI and headless) |
| `src/gui/agent/` | Agent dock, follow-agent, diff stream (GUI only) |
| `resources/agentic/` | Library registry, extra builtin docs |
| `tests/agentic/` | Fixture projects and the end-to-end client |
| `doc/agentic/` | Fork documentation |
| `.github/workflows/agentic-ci.yml` | Fork CI |

## Hook points in upstream files

| File | Hook | Stage |
|---|---|---|
| `CMakeLists.txt` | `option(ENABLE_MCP ...)` next to the other options | 0 |
| `CMakeLists.txt` | `include(src/mcp/sources.cmake)` after the `ENABLE_PYTHON` source block | 0 |
| `CMakeLists.txt` | `src/mcp/*_test.cc` appended to `TEST_SOURCES` | 0 |
| `cmake/Modules/info.cmake` | one `message(STATUS "MCP server: ...")` line | 0 |

Stages 1 and later will add hooks in `src/openscad.cc` (CLI flags),
`src/openscad_gui.cc` (server start-up), `src/gui/OpenSCADApp.*`,
`src/gui/MainWindow.*` and `src/gui/TabManager.*`. Each must be listed here
when added.

## Merge procedure

```bash
git remote add upstream https://github.com/openscad/openscad.git   # once
git fetch upstream
git merge upstream/master
# resolve conflicts; the table above tells you which hunks are ours
git submodule update --init --recursive
cmake --build build && ./build/OpenSCADUnitTests
cmake --build build-headless && ./build-headless/OpenSCADUnitTests
./scripts/beautify.sh --check
```

Then run the end-to-end agent test (Stage 8) before pushing.

## Rules

- Do not modify or delete upstream workflows under `.github/workflows/`; disable
  the ones you do not want in the fork's GitHub Actions settings instead.
- The upstream experimental AI Chat dock (`src/core/AIService.*`, `src/gui/ai/`)
  is left as is.
- Prefer adding a new file over editing an upstream one. When an upstream edit is
  unavoidable, keep it to a few lines that call into fork code.
