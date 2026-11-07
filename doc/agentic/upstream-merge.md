# Tracking upstream OpenSCAD

Agentic-OpenSCAD is a fork of https://github.com/openscad/openscad that merges
upstream `master` periodically. To keep merges cheap, fork code lives in its own
directories and touches upstream files only at a small, documented set of hook
points. Keep this list current whenever a hook is added or removed.

## Fork-only locations (no upstream conflicts expected)

| Path | Purpose |
|---|---|
| `src/mcp/` | MCP server: protocol, transports, sessions (GUI and headless) |
| `src/mcp/viewer/` | the agent viewer (Qt): entry point, server bridge, window |
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
| `src/openscad.cc` | includes of `mcp/Options.h` and `mcp/headless/HeadlessMain.h` | 1 |
| `src/openscad.cc` | `--mcp-stdio`, `--mcp-port`, `--mcp-headless`, `--mcp-token`, `--mcp-token-file`, `--project` options after the `run-all-gui-tests` block | 1 |
| `src/openscad.cc` | `mcp::Options` filled and `mcp::runHeadless()` dispatched before the `cmdlinemode` decision | 1 |
| `src/openscad.cc` | `--classic` option; GUI branch runs `mcp::runViewerApp()` unless `--classic`; no-display branch runs headless | V0 |
| `src/utils/printutils.{h,cc}` | `add_output_listener` / `remove_output_listener` and one call in `PRINT_NOCACHE` (structured message capture) | 3 |
| `src/core/parsersettings.{h,cc}` | `add_librarydir_front` / `remove_librarydir` (project library folders on the search path) | 6 |
| `CMakeLists.txt` | `add_subdirectory(tests/agentic)` under `ENABLE_MCP` after the tests subdirectory | 8 |
| `README.md` | the "Agentic-OpenSCAD" section at the top | 8 |

Source hooks are wrapped in `#ifdef ENABLE_MCP`; the CMake ones in
`if(ENABLE_MCP)`. Since Stage V0 the classic GUI (`openscad_gui.cc`,
`MainWindow`, `TabManager`, `ParameterWidget`) is untouched and only
reachable with `--classic`; agent mode has its own window in
`src/mcp/viewer/`.

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
