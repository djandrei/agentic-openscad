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
