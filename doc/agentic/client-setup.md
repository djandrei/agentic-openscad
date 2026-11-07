# Connecting an MCP client

## Claude Code, headless (the simplest)

Claude Code spawns the server itself over stdio; nothing else runs.

```bash
claude mcp add openscad -- /path/to/openscad --mcp-stdio --project /path/to/project
```

Omit `--project` to let the agent choose with `project_open`. The process
exits when Claude Code closes it. Log output goes to stderr; stdout carries
only JSON-RPC.

## Claude Code with the viewer (watch the agent work)

Start the viewer; it is the default window of the fork:

```bash
openscad --project /path/to/project            # port from the settings, default 7645
openscad --mcp-port 7645 --project /path/to/project
```

It prints the `claude mcp add` command on stderr (the token is random per
start; `--mcp-token` fixes it, `--mcp-token-file` writes it to a file):

```bash
claude mcp add --transport http openscad http://127.0.0.1:7645/mcp --header "Authorization: Bearer <token>"
```

The viewer follows the project of the agent's last tool call. *Pause agent*
in the status bar makes every tool call fail until resumed; the person may
also orbit the 3D view with the mouse (the agent's camera remains the
reference and the view snaps back on its next camera or view action) and
click a file in the list to look at it. Each panel has a close button;
the toggle buttons in the status bar bring panels back, and *View only*
(or `F11`) hides every panel so the 3D view fills the window, then
restores the same panels. A hidden panel whose content changed shows a
dot on its button, red when a compile failed or a tool call errored. The
panel layout is remembered between starts. The classic OpenSCAD GUI is
still available with `openscad --classic` for debugging; it has no agent
server.

## Headless HTTP

```bash
openscad --mcp-port 7645 --mcp-headless --project /path/to/project
```

prints the URL, the token and the `claude mcp add` command on stderr. A
launch without a display behaves the same way on the default port.

## Other clients

Any MCP client that speaks stdio or Streamable HTTP (POST JSON-RPC to
`/mcp`, `Authorization: Bearer <token>`) works. `tests/agentic/e2e_client.py`
is a minimal standard-library client and doubles as an example.

## Requirements and environment

- `view_capture` needs an offscreen OpenGL context (EGL is tried first, then
  GLX; OSMesa if built with it). On a server without a GPU, Mesa's llvmpipe
  through EGL works.
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
(`project_set` changes it); `autoPreview` previews in the viewer after each
write to the main file; `registries` lists extra library catalogs.
