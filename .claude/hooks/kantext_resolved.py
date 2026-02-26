#!/usr/bin/env python3
"""
PostToolUse hook: Mark the Kantext ontological foundation as resolved after
a successful Kantext MCP tool call.

Fires only on tools matching '.*kantext.*' (configured in settings.json).
Writes a state file that the gate hook checks on subsequent tool calls.
"""

import json
import os
import sys
import time


def get_state_dir():
    tmp = os.environ.get("TEMP", os.environ.get("TMP", os.environ.get("TMPDIR", "/tmp")))
    return os.path.join(tmp, "kantext-hooks")


def main():
    try:
        input_data = json.loads(sys.stdin.read())
    except (json.JSONDecodeError, EOFError):
        sys.exit(0)

    session_id = input_data.get("session_id", "default")
    tool_name = input_data.get("tool_name", "unknown")

    state_dir = get_state_dir()
    os.makedirs(state_dir, exist_ok=True)
    state_file = os.path.join(state_dir, f"resolved-{session_id}")

    existing = {}
    if os.path.exists(state_file):
        try:
            with open(state_file, "r") as f:
                existing = json.load(f)
        except (json.JSONDecodeError, OSError):
            existing = {}

    calls = existing.get("calls", [])
    calls.append({
        "tool": tool_name,
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    })

    state = {
        "resolved_at": existing.get("resolved_at", time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())),
        "session_id": session_id,
        "calls": calls,
    }

    with open(state_file, "w") as f:
        json.dump(state, f, indent=2)

    sys.exit(0)


if __name__ == "__main__":
    main()
