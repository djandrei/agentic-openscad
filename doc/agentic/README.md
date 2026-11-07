# Agentic-OpenSCAD

Agentic-OpenSCAD is a fork of [OpenSCAD](https://openscad.org) built for AI
agents. Every command reaches OpenSCAD from the agent through a
[Model Context Protocol](https://modelcontextprotocol.io) (MCP) server: the
agent writes ordinary OpenSCAD code; the server compiles it, reports errors
and warnings with file and line, shows the model as images, measures the
geometry, manages customizer parameters and libraries, and serves the
documentation. A person does not operate the application; they can watch
the agent work in the **viewer**, or run everything headless.

Documents in this folder:

- `client-setup.md`: connecting Claude Code (or any MCP client) in viewer and
  headless mode.
- `tools.md`: generated reference of every tool, resource and prompt.
- `upstream-merge.md`: how the fork tracks upstream OpenSCAD.
- `baseline-tests.txt`: the upstream test baseline recorded before the fork's
  changes.

## Concepts

**Project.** A folder on disk. `project_open` creates
`.openscad-project.json` in it (main file, autoPreview, libraries,
registries). Every file path the agent passes is relative to that folder and
must stay inside it; files on disk are the only state. Writes are atomic
and versioned (content hash) so the agent can detect concurrent changes.

**Modes.** The same engine and tools serve both:

- *Viewer* (default when a display exists): `openscad [--mcp-port N]
  [--project DIR]`. The server runs inside the Qt process over HTTP on
  `127.0.0.1` with a bearer token. The only window is the viewer: the 3D
  view showing the last preview or render with the agent's camera and view
  options, a read-only code pane with the file the agent last touched and
  its changed lines highlighted, the project's file list, the customizer
  parameters, the messages of the last compile, the tool-call log and the
  change stream. The person can orbit the view with the mouse, pick a file
  to look at, and press *Pause*; nothing else is clickable. The classic
  OpenSCAD GUI remains reachable with `--classic` for debugging.
- *Headless*: `openscad --mcp-stdio --project DIR` (spawned by the MCP
  client over stdin/stdout), `openscad --mcp-port N --mcp-headless`, or
  any launch without a display. No window; captures are drawn with an
  offscreen OpenGL context (EGL or OSMesa; a NULLGL build serves everything
  except `view_capture`).

**Feedback.** `check`, `preview` and `render` return the structured messages
of that run (severity, text, file, line, column), counts, timing and, for
renders, a geometry summary. `view_capture` returns PNG images from named
views, always rendered offscreen from the agent's camera; `geometry_info`
reports bounding box, facets, manifoldness, volume and surface area.

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
  session/    Project, FileStore (sandbox, versions, atomic writes), MessageLog,
              EngineSession (the one session type) + SessionObserver, SessionManager
  engine/     CompileEngine (the CLI pipeline as a function), ViewEngine (camera, captures,
              geometry statistics, export), ParamEngine (customizer)
  library/    registry, download (TLS), zip extraction, git fallback, LibraryManager
  docs/       HTML to text, search index, builtin docs, Docs cache
  tools/      the tool definitions, grouped by area
  headless/   the headless entry point (stdio or HTTP, requests run on the main thread)
  viewer/     ViewerApp (agent-mode Qt entry point), ViewerBridge (server inside Qt,
              main-thread executor), ViewerWindow (the viewer, a SessionObserver)
```

Every tool is written once against `ProjectSession`, implemented by
`EngineSession`, which compiles through `CompileEngine` exactly like the
command line and reports what happened to an optional `SessionObserver`.
The viewer is such an observer: it rebuilds the 3D scene from the engine's
tree or geometry, mirrors the camera and view options, and shows files,
messages, parameters and tool calls. Transports hand each request to an
executor that runs it on the main thread, in both modes.

Hooks into upstream files are deliberately few and listed in
`upstream-merge.md`; the classic GUI sources are untouched.

## Security model

The agent is in control: there are no confirmation prompts, only the
Pause button. The server binds to the loopback interface only, the HTTP
transport requires a per-session token and rejects foreign `Origin`
headers, file tools cannot leave the project folder (symlinks are
resolved), exports land inside the project, and there is no shell tool.
Library installation downloads archives from the configured registries; a
`git clone` fallback runs without a shell.

## Testing

- Unit tests: `./build/OpenSCADUnitTests "[mcp]"` (both the viewer build and
  the `-DHEADLESS=ON` build).
- End-to-end: `ctest -R agentic` runs the Python client against the stdio
  server, the headless HTTP server and (with `xvfb-run`) the viewer.
- Manual: `python3 tests/agentic/e2e_client.py --launch ./build/openscad --launch-mode gui`.

## Not done

- Packaging (AppImage and other installers) is unchanged from upstream.
- SSE streaming of notifications over HTTP is not implemented; every tool
  works without it.
- A single transport thread serves requests one at a time; `cancel` of a
  running compile is therefore not offered (use `timeoutSeconds`).
