#!/usr/bin/env python3
"""
Stop hook: Validate that the Kantext ontological foundation was resolved
during this session AND that post-write validation occurred.

Three checks:
  1. Kantext was consulted at all (2+ calls for foundation resolution)
  2. If writes happened, a Kantext call occurred AFTER the last write
     (proving the agent validated its work against the ontology)
  3. Respects stop_hook_active to prevent infinite loops
"""

import json
import os
import sys


def get_state_dir():
    tmp = os.environ.get("TEMP", os.environ.get("TMP", os.environ.get("TMPDIR", "/tmp")))
    return os.path.join(tmp, "kantext-hooks")


def cleanup_state(session_id):
    state_dir = get_state_dir()
    for prefix in ("resolved-", "writes-"):
        path = os.path.join(state_dir, f"{prefix}{session_id}")
        try:
            os.remove(path)
        except OSError:
            pass


def read_json(path):
    if not os.path.exists(path):
        return None
    try:
        with open(path, "r") as f:
            return json.load(f)
    except (json.JSONDecodeError, OSError):
        return None


def block(reason):
    json.dump({"decision": "block", "reason": reason}, sys.stdout)
    sys.exit(2)


def main():
    try:
        input_data = json.loads(sys.stdin.read())
    except (json.JSONDecodeError, EOFError):
        sys.exit(0)

    session_id = input_data.get("session_id", "default")

    if input_data.get("stop_hook_active", False):
        cleanup_state(session_id)
        sys.exit(0)

    state_dir = get_state_dir()
    resolved = read_json(os.path.join(state_dir, f"resolved-{session_id}"))
    writes = read_json(os.path.join(state_dir, f"writes-{session_id}"))

    # Check 1: Kantext was consulted at all
    if resolved is None:
        block(
            "The Kantext ontological foundation was never resolved during this session.\n\n"
            "Before completing, you must:\n"
            "1. Call Kantext MCP server_status() to check workspace state\n"
            "2. Load and render the workspace contexts to understand the ontological foundation\n"
            "3. Validate that your work is consistent with the project's knowledge structure"
        )

    kantext_calls = resolved.get("calls", [])
    if len(kantext_calls) < 2:
        block(
            f"Only {len(kantext_calls)} Kantext tool call(s) were made this session.\n"
            "A thorough ontological resolution requires at minimum:\n"
            "  - server_status() to check workspace state\n"
            "  - render() or query() to actually read the ontological foundation\n\n"
            "Please call render() or query() on the loaded contexts to validate\n"
            "your work against the project's ontology before completing."
        )

    # Check 2: If writes happened, Kantext must have been called AFTER the last write
    if writes is not None:
        write_list = writes.get("writes", [])
        if write_list:
            last_write_epoch = max(w.get("epoch", 0) for w in write_list)
            last_kantext_epoch = resolved.get("last_kantext_epoch", 0)

            if last_kantext_epoch < last_write_epoch:
                last_write_tool = max(write_list, key=lambda w: w.get("epoch", 0)).get("tool", "unknown")
                block(
                    f"Write operations occurred (last: {last_write_tool}) but Kantext was not "
                    "consulted afterwards to validate the changes.\n\n"
                    "Before completing, you must validate your work against the ontology:\n"
                    "1. Call render() or query() on the relevant Kantext contexts\n"
                    "2. Verify your changes are consistent with the project's ontological foundation\n"
                    "3. If changes conflict with the ontology, explain why or correct them"
                )

    cleanup_state(session_id)
    sys.exit(0)


if __name__ == "__main__":
    main()
