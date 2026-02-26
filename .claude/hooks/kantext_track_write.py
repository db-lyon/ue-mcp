#!/usr/bin/env python3
"""
PostToolUse hook: Track write operations so the Stop hook can verify
that Kantext validation happened AFTER the last mutation.
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
    state_file = os.path.join(state_dir, f"writes-{session_id}")

    existing = {}
    if os.path.exists(state_file):
        try:
            with open(state_file, "r") as f:
                existing = json.load(f)
        except (json.JSONDecodeError, OSError):
            existing = {}

    writes = existing.get("writes", [])
    writes.append({
        "tool": tool_name,
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "epoch": time.time(),
    })

    with open(state_file, "w") as f:
        json.dump({"writes": writes}, f, indent=2)

    sys.exit(0)


if __name__ == "__main__":
    main()
