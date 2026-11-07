#!/usr/bin/env python3
"""End-to-end client for the Agentic-OpenSCAD MCP server (standard library only).

Drives the server either over stdio (spawning the openscad binary) or over the
HTTP transport of an already running instance (GUI or headless), and runs a
scenario of tool calls with assertions. Exit code 0 means every step passed.

Examples:
  e2e_client.py --stdio ./build/openscad --project /tmp/proj
  e2e_client.py --url http://127.0.0.1:7645/mcp --token SECRET --project /tmp/proj
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
import urllib.request


class McpError(Exception):
    pass


class StdioClient:
    def __init__(self, command):
        self.proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, text=True, bufsize=1)
        self.next_id = 1

    def request(self, method, params=None):
        msg = {"jsonrpc": "2.0", "id": self.next_id, "method": method}
        if params is not None:
            msg["params"] = params
        self.next_id += 1
        self.proc.stdin.write(json.dumps(msg) + "\n")
        self.proc.stdin.flush()
        line = self.proc.stdout.readline()
        if not line:
            err = self.proc.stderr.read()
            raise McpError("server closed stdout; stderr:\n" + err)
        return json.loads(line)

    def notify(self, method, params=None):
        msg = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            msg["params"] = params
        self.proc.stdin.write(json.dumps(msg) + "\n")
        self.proc.stdin.flush()

    def close(self):
        self.proc.stdin.close()
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()


class HttpClient:
    def __init__(self, url, token):
        self.url = url
        self.token = token
        self.next_id = 1

    def _post(self, msg):
        data = json.dumps(msg).encode()
        req = urllib.request.Request(self.url, data=data, method="POST")
        req.add_header("Content-Type", "application/json")
        req.add_header("Accept", "application/json, text/event-stream")
        if self.token:
            req.add_header("Authorization", "Bearer " + self.token)
        with urllib.request.urlopen(req, timeout=120) as resp:
            body = resp.read()
            return json.loads(body) if body else None

    def request(self, method, params=None):
        msg = {"jsonrpc": "2.0", "id": self.next_id, "method": method}
        if params is not None:
            msg["params"] = params
        self.next_id += 1
        return self._post(msg)

    def notify(self, method, params=None):
        msg = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            msg["params"] = params
        self._post(msg)

    def close(self):
        pass


class Scenario:
    def __init__(self, client):
        self.client = client
        self.failures = 0
        self.steps = 0

    def check(self, condition, description):
        self.steps += 1
        if condition:
            print(f"  ok    {description}")
        else:
            self.failures += 1
            print(f"  FAIL  {description}")

    def call(self, tool, **args):
        response = self.client.request("tools/call", {"name": tool, "arguments": args})
        if "error" in response:
            raise McpError(f"{tool}: JSON-RPC error {response['error']}")
        return response["result"]

    def tool_ok(self, tool, **args):
        result = self.call(tool, **args)
        self.check(not result.get("isError"), f"{tool} succeeds" +
                   ("" if not result.get("isError") else ": " + result["content"][0]["text"]))
        if result.get("isError"):
            return {}
        return result.get("structuredContent", {})

    def tool_fails(self, tool, **args):
        result = self.call(tool, **args)
        self.check(result.get("isError"), f"{tool} fails as expected")
        return result.get("structuredContent", {})


def run_scenario(client, project_dir, expect_mode=None):
    s = Scenario(client)
    init = client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                         "clientInfo": {"name": "e2e", "version": "0"}})
    s.check("result" in init and init["result"]["serverInfo"]["name"] == "agentic-openscad",
            "initialize returns agentic-openscad")
    s.check("instructions" in init.get("result", {}), "initialize carries instructions")
    client.notify("notifications/initialized")

    tools = client.request("tools/list")["result"]["tools"]
    names = {t["name"] for t in tools}
    for required in ["project_open", "file_write", "file_read", "file_list", "editor_get", "status"]:
        s.check(required in names, f"tool {required} listed")

    info = s.tool_ok("project_open", path=project_dir)
    project = info["id"]
    s.check(info["main"] == "main.scad", "project main defaults to main.scad")
    if expect_mode:
        s.check(info.get("mode") == expect_mode, f"session mode is {expect_mode}")

    content = "// e2e\ncube(10);\n"
    written = s.tool_ok("file_write", project=project, path="main.scad", content=content)
    s.check(written["created"], "main.scad created")
    version = written["version"]

    read = s.tool_ok("file_read", project=project, path="main.scad")
    s.check(read["content"] == content and read["version"] == version, "file_read returns written content")

    conflict = s.tool_fails("file_write", project=project, path="main.scad", content="x", baseVersion="stale")
    s.check(conflict.get("currentVersion") == version, "conflict reports the current version")

    edited = s.tool_ok("file_write", project=project, path="main.scad", baseVersion=version,
                       edits=[{"old": "cube(10);", "new": "cube(20);"}])
    s.check(edited["change"]["startLine"] == 2, "edit reports the changed line")

    editor = s.tool_ok("editor_get", project=project, path="main.scad")
    s.check(editor["text"] == "// e2e\ncube(20);\n", "editor_get shows the edited text")
    if expect_mode == "gui":
        s.check(editor["source"] == "editor" and editor["unsaved"] is False, "GUI editor tab reflects the write")

    listing = s.tool_ok("file_list", project=project)
    s.check(any(f["path"] == "main.scad" for f in listing["files"]), "file_list contains main.scad")

    s.tool_ok("file_write", project=project, path="parts/bolt.scad", content="sphere(1);\n")
    s.tool_ok("file_rename", project=project, **{"from": "parts/bolt.scad", "to": "parts/screw.scad"})
    s.tool_ok("file_delete", project=project, path="parts/screw.scad")
    s.tool_fails("file_read", project=project, path="parts/screw.scad")
    s.tool_fails("file_read", project=project, path="../outside.scad")

    status = s.tool_ok("status")
    s.check(status["current"] == project, "status reports the current project")

    # Compile feedback
    s.tool_ok("file_write", project=project, path="main.scad", content="cube(10);\nsphere(;\n")
    broken = s.tool_ok("preview", project=project)
    s.check(broken.get("ok") is False and broken.get("parsed") is False, "preview of a syntax error is not ok")
    s.check(any(m["severity"] == "error" and m.get("line") == 2 for m in broken.get("messages", [])),
            "syntax error reported on line 2")

    s.tool_ok("file_write", project=project, path="main.scad",
              content="echo(\"size\", 20);\ncube(undefined_var);\ncube(20);\n")
    warned = s.tool_ok("preview", project=project)
    s.check(warned.get("parsed") and warned.get("warnings", 0) >= 1, "preview reports the undefined variable warning")
    s.check(any(m["severity"] == "echo" for m in warned.get("messages", [])), "preview captures echo output")

    s.tool_ok("file_write", project=project, path="main.scad", content="cube(20);\n")
    clean = s.tool_ok("preview", project=project)
    s.check(clean.get("ok") and clean.get("errors") == 0 and clean.get("warnings") == 0, "clean preview is ok")

    rendered = s.tool_ok("render", project=project)
    s.check(rendered.get("ok"), "render is ok")
    geometry = rendered.get("geometry", {})
    s.check(geometry.get("dimension") == 3 and geometry.get("empty") is False, "render reports 3D geometry")
    s.check(geometry.get("boundingBox", {}).get("max") == [20, 20, 20], "render bounding box is 20x20x20")

    # Visual and geometric feedback
    info = s.tool_ok("geometry_info", project=project)
    s.check(abs(info.get("volume", 0) - 8000) < 1e-6, "geometry_info volume is 8000")
    s.check(abs(info.get("surfaceArea", 0) - 2400) < 1e-6, "geometry_info surface area is 2400")

    camera = s.tool_ok("camera_set", project=project, preset="diagonal")
    s.check(camera.get("vpr") == [55, 0, 25], "camera_set diagonal reports vpr [55, 0, 25]")
    s.check(s.tool_ok("camera_get", project=project).get("vpr") == [55, 0, 25], "camera_get agrees")
    options = s.tool_ok("view_options", project=project, edges=True)
    s.check(options.get("edges") is True, "view_options enables edges")

    response = client.request("tools/call", {"name": "view_capture",
                                              "arguments": {"project": project, "views": ["diagonal", "top"],
                                                            "width": 320, "height": 240, "mode": "render"}})
    result = response.get("result", {})
    images = [c for c in result.get("content", []) if c.get("type") == "image"]
    s.check(not result.get("isError") and len(images) == 2 and all(i["data"].startswith("iVBOR") for i in images),
            "view_capture returns two PNG images" +
            ("" if not result.get("isError") else ": " + result["content"][0]["text"]))

    exported = s.tool_ok("export", project=project, format="binstl", path="out/cube.stl")
    s.check(exported.get("bytes", 0) > 84 and os.path.exists(os.path.join(project, "out", "cube.stl")),
            "export writes out/cube.stl")

    # Customizer
    s.tool_ok("file_write", project=project, path="main.scad",
              content="size = 20; // [5:50]\nlabel = \"a\"; // [a, b]\ncube(size);\n")
    params = s.tool_ok("params_get", project=project)
    size = next((p for p in params.get("parameters", []) if p["name"] == "size"), {})
    s.check(size.get("value") == 20 and size.get("max") == 50, "params_get describes size with its range")
    changed = s.tool_ok("params_set", project=project, values={"size": 35})
    s.check(next((p["value"] for p in changed.get("parameters", []) if p["name"] == "size"), None) == 35,
            "params_set reports the new value")
    rendered = s.tool_ok("render", project=project)
    s.check(rendered.get("geometry", {}).get("boundingBox", {}).get("max") == [35, 35, 35],
            "render uses the customizer value")
    presets = s.tool_ok("params_preset_save", project=project, name="big")
    s.check("big" in [p["name"] for p in presets.get("presets", [])], "preset saved next to the file")
    s.check(os.path.exists(os.path.join(project, "main.json")), "main.json exists")
    s.tool_ok("params_set", project=project, reset=True)
    s.tool_ok("file_write", project=project, path="main.scad", content="cube(20);\n")
    s.tool_ok("render", project=project)

    # Libraries
    import io
    import zipfile
    zip_path = os.path.join(tempfile.gettempdir(), "agentic-e2e-lib.zip")
    with zipfile.ZipFile(zip_path, "w") as zf:
        zf.writestr("e2elib-main/shapes.scad", "module box(s = 10) { cube(s); }\n")
        zf.writestr("e2elib-main/README.md", "# e2elib\n")
    catalog = s.tool_ok("library_catalog", project=project, query="BOSL2")
    s.check(any(l["name"] == "BOSL2" for l in catalog.get("libraries", [])), "library_catalog lists BOSL2")
    installed = s.tool_ok("library_install", project=project, library="file://" + zip_path, dirName="e2elib")
    s.check(installed.get("files") == 2, "library_install extracts the archive")
    files = s.tool_ok("library_files", project=project, name="e2elib", describe=True)
    s.check(any(m["name"] == "box" for d in files.get("described", []) for m in d.get("modules", [])),
            "library_files describes module box")
    s.tool_ok("file_write", project=project, path="main.scad", content="use <e2elib/shapes.scad>\nbox(20);\n")
    lib_preview = s.tool_ok("preview", project=project)
    s.check(lib_preview.get("ok") and lib_preview.get("warnings") == 0, "preview resolves use <e2elib/...>")
    s.tool_ok("library_remove", project=project, name="e2elib")
    s.tool_ok("file_write", project=project, path="main.scad", content="cube(20);\n")
    s.tool_ok("render", project=project)

    csg = s.tool_ok("csg_tree_dump", project=project)
    s.check("cube(size = [20, 20, 20]" in csg.get("text", ""), "csg_tree_dump shows the cube")
    checked = s.tool_ok("check", project=project)
    s.check(checked.get("ok"), "check passes on the clean file")
    log = s.tool_ok("log_get", since=0, limit=5)
    s.check(len(log.get("messages", [])) >= 1 and log.get("lastSeq", 0) > 0, "log_get returns messages")

    print(f"{s.steps - s.failures}/{s.steps} checks passed")
    return s.failures == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--stdio", nargs="+", metavar="CMD", help="openscad binary (and extra args) to spawn with --mcp-stdio")
    parser.add_argument("--url", help="HTTP MCP endpoint of a running instance")
    parser.add_argument("--token", default="", help="bearer token for --url")
    parser.add_argument("--project", help="project folder (a temporary one is created when omitted)")
    parser.add_argument("--expect-mode", choices=["gui", "headless"], help="assert the session mode")
    args = parser.parse_args()

    project_dir = args.project or tempfile.mkdtemp(prefix="agentic-e2e-")
    os.makedirs(project_dir, exist_ok=True)

    if args.stdio:
        client = StdioClient(args.stdio + ["--mcp-stdio"])
    elif args.url:
        client = HttpClient(args.url, args.token)
    else:
        parser.error("one of --stdio or --url is required")

    try:
        ok = run_scenario(client, project_dir, args.expect_mode)
    finally:
        client.close()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
