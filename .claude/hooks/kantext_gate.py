#!/usr/bin/env python3
"""
PreToolUse hook: On the first tool call of a session, DENIES it and injects
the full Kantext ontological foundation. This forces Claude to absorb the
project context before acting, preventing generic file exploration.
The foundation is produced by starting the Kantext MCP binary, performing
the full handshake, loading .kant files, and rendering all frames.
Subsequent calls in the same session pass through silently.
"""

import json
import os
import subprocess
import sys
import time

STALE_THRESHOLD_SECONDS = 8 * 60 * 60


def get_state_dir():
    tmp = os.environ.get("TEMP", os.environ.get("TMP", os.environ.get("TMPDIR", "/tmp")))
    return os.path.join(tmp, "kantext-hooks")


def get_state_file(session_id):
    return os.path.join(get_state_dir(), f"resolved-{session_id}")


def is_kantext_tool(tool_name):
    return "kantext" in tool_name.lower()


def is_resolved(state_file):
    if not os.path.exists(state_file):
        return False
    age = time.time() - os.path.getmtime(state_file)
    if age > STALE_THRESHOLD_SECONDS:
        os.remove(state_file)
        return False
    return True


def mark_resolved(session_id):
    state_dir = get_state_dir()
    os.makedirs(state_dir, exist_ok=True)
    state_file = os.path.join(state_dir, f"resolved-{session_id}")
    with open(state_file, "w") as f:
        json.dump({"resolved_at": time.time()}, f)


KANTEXT_BIN = r"c:\Users\david\Projects\UE\Vale\kantext-mcp-windows-x86_64.exe"
WORKSPACE = r"C:\Users\david\Projects\UE\ue-mcp"
PRELOAD_FILES = [
    ".kantext/UEConcepts.kant",
    ".kantext/BlueprintOntology.kant",
    ".kantext/Traits.kant",
    ".kantext/Workflows.kant",
    ".kantext/McpSurface.kant",
]


def mcp_send(proc, method, params=None, msg_id=None):
    msg = {"jsonrpc": "2.0", "method": method}
    if params is not None:
        msg["params"] = params
    if msg_id is not None:
        msg["id"] = msg_id
    proc.stdin.write((json.dumps(msg) + "\n").encode("utf-8"))
    proc.stdin.flush()


def mcp_recv(proc, expected_id, timeout=10):
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = proc.stdout.readline()
        if not line:
            return None
        try:
            resp = json.loads(line.decode("utf-8", errors="replace"))
            if resp.get("id") == expected_id:
                return resp
        except json.JSONDecodeError:
            continue
    return None


def mcp_call(proc, tool_name, arguments, msg_id):
    mcp_send(proc, "tools/call", {"name": tool_name, "arguments": arguments}, msg_id=msg_id)
    resp = mcp_recv(proc, expected_id=msg_id, timeout=10)
    if not resp or "result" not in resp:
        return None
    parts = []
    for item in resp["result"].get("content", []):
        if item.get("type") == "text":
            parts.append(item["text"])
    return "\n".join(parts) if parts else None


def resolve_foundation():
    """Start kantext MCP, handshake, load .kant files, render all frames."""
    if not os.path.exists(KANTEXT_BIN):
        return None

    try:
        proc = subprocess.Popen(
            [KANTEXT_BIN, "--workspace", WORKSPACE],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            bufsize=0,
        )
    except OSError:
        return None

    try:
        # Handshake
        mcp_send(proc, "initialize", {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {"name": "kantext-gate-hook", "version": "1.0.0"},
        }, msg_id=1)
        if not mcp_recv(proc, expected_id=1, timeout=10):
            return None
        mcp_send(proc, "notifications/initialized")

        # Load all .kant files, collect frame IDs
        frame_ids = ["frame:1"]  # workspace context from bootstrap
        for i, path in enumerate(PRELOAD_FILES, start=10):
            text = mcp_call(proc, "load", {
                "path": path,
                "enable_external_resolution": False,
                "debug": False,
            }, msg_id=i)
            if text:
                try:
                    data = json.loads(text)
                    fid = data.get("id")
                    if fid:
                        frame_ids.append(fid)
                except json.JSONDecodeError:
                    pass

        # Render each frame with model template
        rendered_parts = []
        for i, fid in enumerate(frame_ids, start=50):
            text = mcp_call(proc, "render", {
                "id": fid,
                "template": "model",
                "query": "/**",
                "limit": 200,
            }, msg_id=i)
            if text:
                try:
                    data = json.loads(text)
                    rendered = data.get("rendered", "")
                    if rendered:
                        rendered_parts.append(rendered)
                except json.JSONDecodeError:
                    if text:
                        rendered_parts.append(text)

        return "\n\n".join(rendered_parts) if rendered_parts else None

    except (OSError, BrokenPipeError, ValueError):
        return None
    finally:
        try:
            proc.stdin.close()
        except OSError:
            pass
        try:
            proc.terminate()
            proc.wait(timeout=3)
        except (OSError, subprocess.TimeoutExpired):
            proc.kill()


def main():
    try:
        input_data = json.loads(sys.stdin.read())
    except (json.JSONDecodeError, EOFError):
        sys.exit(0)

    tool_name = input_data.get("tool_name", "")
    session_id = input_data.get("session_id", "default")

    # Always allow Kantext tools
    if is_kantext_tool(tool_name):
        json.dump({
            "hookSpecificOutput": {
                "hookEventName": "PreToolUse",
                "permissionDecision": "allow",
                "permissionDecisionReason": "Kantext MCP tools are always allowed.",
            }
        }, sys.stdout)
        sys.exit(0)

    state_file = get_state_file(session_id)
    if is_resolved(state_file):
        sys.exit(0)

    # Not resolved yet — call Kantext MCP and DENY this tool call so Claude
    # absorbs the foundation before acting. The denial forces a re-plan.
    foundation = resolve_foundation()
    mark_resolved(session_id)

    if foundation:
        json.dump({
            "hookSpecificOutput": {
                "hookEventName": "PreToolUse",
                "permissionDecision": "deny",
                "denialReason": (
                    "STOP. Before taking any action, absorb this project's ontological "
                    "foundation from the Kantext MCP. This context defines the project's "
                    "domain model, tool surface, workflows, and type system. Use it to "
                    "inform your approach — do NOT do generic file exploration.\n\n"
                    + foundation
                    + "\n\nYou now have the full Kantext foundation. "
                    "Retry your task using this context. "
                    "If you need deeper detail, call the Kantext MCP tools directly "
                    "(server_status, list_contexts, render, query)."
                ),
            }
        }, sys.stdout)
    else:
        json.dump({
            "hookSpecificOutput": {
                "hookEventName": "PreToolUse",
                "permissionDecision": "deny",
                "denialReason": (
                    "STOP. This project has a Kantext ontological foundation that MUST "
                    "be consulted before any action. The binary could not be reached, so "
                    "you MUST call the Kantext MCP directly:\n"
                    "1. server_status(include_contexts=true) to see loaded contexts\n"
                    "2. render(id=<frame_id>, template='model', query='/**', limit=200) "
                    "on preloaded contexts\n"
                    "Contexts are ALREADY PRELOADED — do NOT call load().\n"
                    "Do this NOW before taking any other action."
                ),
            }
        }, sys.stdout)
    sys.exit(0)


if __name__ == "__main__":
    main()
