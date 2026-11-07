# Agentic-OpenSCAD

Agentic-OpenSCAD is a fork of [OpenSCAD](https://openscad.org) that exposes
everything a user can do in the application to an AI agent through the
[Model Context Protocol](https://modelcontextprotocol.io) (MCP). The agent
writes ordinary OpenSCAD code; the server compiles it, reports errors and
warnings with file and line, shows the model as images, measures the geometry,
manages customizer parameters and libraries, and serves the documentation.
The user can watch the agent work in the normal GUI, or run everything
headless.

Documents in this folder:

- `client-setup.md`: connecting Claude Code (or any MCP client) in GUI and
  headless mode.
- `tools.md`: generated reference of every tool, resource and prompt.
- `upstream-merge.md`: how the fork tracks upstream OpenSCAD.
- `baseline-tests.txt`: the upstream test baseline recorded before the fork's
  changes.

## Concepts

**Project.** A folder on disk. `project_open` creates
`.openscad-project.json` in it (main file, autoPreview, libraries,
registries). Every file path the agent passes is relative to that folder and
must stay inside it; writes are atomic and versioned (content hash) so the
agent can detect concurrent edits. In GUI mode one project is one window.

**Modes.** The same tools exist in both:

- *GUI*: `openscad --mcp-port 7645 --project DIR`. The server runs inside the
  Qt process over HTTP on `127.0.0.1` with a bearer token. Every file write
  appears in an editor tab with the changed lines highlighted, previews and
  renders show in the 3D view, and the Agent dock (next to the console) shows
  the tool calls, a stream of every change, the project tree, a Pause button
  and a Follow-agent toggle. The status bar control starts/stops the server
  and copies the Claude Code command.
- *Headless*: `openscad --mcp-stdio --project DIR` (spawned by the MCP client
  over stdin/stdout) or `openscad --mcp-port N --mcp-headless`. No window;
  captures are drawn with an offscreen OpenGL context (EGL or OSMesa; a
  NULLGL build serves everything except `view_capture`).

**Feedback.** `check`, `preview` and `render` return the structured messages
of that run (severity, text, file, line, column), counts, timing and, for
renders, a geometry summary. `view_capture` returns PNG images from named
views; `geometry_info` reports bounding box, facets, manifoldness, volume and
surface area. Clients that cannot see images still get the numbers.

**Documentation.** Builtin signatures and semantics are built in. The
offline User Manual and cheat sheet are downloaded once with `docs_update`
into `$XDG_CACHE_HOME/agentic-openscad/docs` (or
`$AGENTIC_OPENSCAD_DOCS_CACHE`). README files of installed libraries are
searchable too.

**Libraries.** `library_install BOSL2` downloads a library archive into
`<project>/libraries/BOSL2`; that folder is first on the `use`/`include`
search path while the project is open. The catalog is seeded from
openscad.org and extensible with registries (JSON files or URLs).

## Architecture

```
src/mcp/
  protocol/   JSON-RPC, MCP lifecycle, tool/resource/prompt registries, schema validation
  transport/  stdio (newline-delimited) and Streamable HTTP (Boost.Beast, loopback, token)
  session/    Project, FileStore (sandbox, versions, atomic writes), MessageLog, SessionManager
  engine/     CompileEngine (the CLI pipeline as a function), ViewEngine (camera, captures,
              geometry statistics, export), ParamEngine (customizer)
  library/    registry, download (TLS), zip extraction, git fallback, LibraryManager
  docs/       HTML to text, search index, builtin docs, Docs cache
  tools/      the tool definitions, grouped by area
  headless/   HeadlessSession + the headless entry point
  gui/        AgentBridge (server inside Qt, GUI-thread executor), GuiSession, AgentDock,
              AgentStatusWidget
```

Every tool is written once against `ProjectSession`; `HeadlessSession` and
`GuiSession` implement it. Transports hand each request to an *executor*: in
headless mode it runs inline; in GUI mode it is queued to the Qt main thread
and executed only when no compile or render holds the GUI lock, so tools never
run nested inside OpenSCAD's own event pumping.

Hooks into upstream files are deliberately few and listed in
`upstream-merge.md`.

## Security model

The agent is meant to be in control: there are no confirmation prompts, only
the user's Pause button. The server binds to the loopback interface only, the
HTTP transport requires a per-session token and rejects foreign `Origin`
headers, file tools cannot leave the project folder (symlinks are resolved),
exports land inside the project, and there is no shell tool. Library
installation downloads archives from the configured registries; a `git clone`
fallback runs without a shell.

## Testing

- Unit tests: `./build/OpenSCADUnitTests "[mcp]"` (both the GUI and the
  `-DHEADLESS=ON` build).
- End-to-end: `ctest -R agentic` runs the Python client against the stdio
  server, the headless HTTP server and (with `xvfb-run`) the GUI.
- Manual: `python3 tests/agentic/e2e_client.py --launch ./build/openscad --launch-mode gui`.

## Not done

- Packaging (AppImage and other installers) is unchanged from upstream.
- SSE streaming of notifications over HTTP is not implemented; every tool
  works without it.
- A single transport thread serves requests one at a time; `cancel` of a
  running compile is therefore not offered (use `timeoutSeconds`).
