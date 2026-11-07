# Connecting an MCP client

## Claude Code, headless (the simplest)

Claude Code spawns the server itself over stdio; nothing else runs.

```bash
claude mcp add openscad -- /path/to/openscad --mcp-stdio --project /path/to/project
```

Omit `--project` to let the agent choose with `project_open`. The process
exits when Claude Code closes it. Log output goes to stderr; stdout carries
only JSON-RPC.

## Claude Code, GUI (watch the agent work)

Start OpenSCAD with the server enabled:

```bash
openscad --mcp-port 7645 --project /path/to/project
```

The status bar shows `Agent: :7645`. Its menu offers *Copy Claude Code
command*, which puts the following on the clipboard (the token is random per
start; `--mcp-token` fixes it, `--mcp-token-file` writes it to a file):

```bash
claude mcp add --transport http openscad http://127.0.0.1:7645/mcp --header "Authorization: Bearer <token>"
```

Without `--mcp-port` the server can be started later from the status bar
menu (port and auto-start are remembered in the preferences).

The Agent dock (tabified with the console) shows tool calls and changes.
*Pause agent* makes every tool call fail until resumed; *Follow agent*
switches the editor to the file and line being changed.

## Headless HTTP

```bash
openscad --mcp-port 7645 --mcp-headless --project /path/to/project
```

prints the URL, the token and the `claude mcp add` command on stderr. Useful
when the client cannot spawn processes.

## Other clients

Any MCP client that speaks stdio or Streamable HTTP (POST JSON-RPC to
`/mcp`, `Authorization: Bearer <token>`) works. `tests/agentic/e2e_client.py`
is a minimal standard-library client and doubles as an example.

## Requirements and environment

- `view_capture` in headless mode needs an offscreen OpenGL context (EGL is
  tried first, then GLX; OSMesa if built with it). On a server without a GPU,
  Mesa's llvmpipe through EGL works.
- `docs_update` and `library_install` need network access to openscad.org
  and the forges; everything else is offline.
- `AGENTIC_OPENSCAD_DOCS_CACHE` overrides the documentation cache folder.
- Set `OPENSCADPATH` for extra global library folders; project libraries
  (`<project>/libraries`) are always searched first.

## Project configuration

`.openscad-project.json` is created on first open:

```json
{
  "main": "main.scad",
  "autoPreview": true,
  "libraries": {"dir": "libraries", "installed": {}},
  "registries": []
}
```

`main` is the file previewed, rendered and exported by default
(`project_set` changes it); `autoPreview` previews in the GUI after each
write to the main file; `registries` lists extra library catalogs.
