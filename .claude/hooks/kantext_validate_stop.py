#!/usr/bin/env python3
"""
Stop hook: Validate that the Kantext ontological foundation was resolved
during this session before allowing the agent to finish.

If the foundation was never consulted, blocks the stop and instructs the
agent to validate its work against the ontology before completing.

Respects stop_hook_active to prevent infinite loops — if the stop hook
already fired once and we're in a retry, allow the stop.
"""

import json
import os
import sys


def get_state_dir():
    tmp = os.environ.get("TEMP", os.environ.get("TMP", os.environ.get("TMPDIR", "/tmp")))
    return os.path.join(tmp, "kantext-hooks")


def cleanup_state(session_id):
    state_dir = get_state_dir()
    state_file = os.path.join(state_dir, f"resolved-{session_id}")
    try:
        os.remove(state_file)
    except OSError:
        pass


def main():
    try:
        input_data = json.loads(sys.stdin.read())
    except (json.JSONDecodeError, EOFError):
        sys.exit(0)

    session_id = input_data.get("session_id", "default")

    # Prevent infinite loops: if this hook already blocked once, let it through
    if input_data.get("stop_hook_active", False):
        cleanup_state(session_id)
        sys.exit(0)

    state_dir = get_state_dir()
    state_file = os.path.join(state_dir, f"resolved-{session_id}")

    if not os.path.exists(state_file):
        output = {
            "decision": "block",
            "reason": "\n".join([
                "The Kantext ontological foundation was never resolved during this session.",
                "",
                "Before completing, you must:",
                "1. Call Kantext MCP server_status() to check workspace state",
                "2. Load and render the workspace contexts to understand the ontological foundation",
                "3. Validate that your work is consistent with the project's knowledge structure",
                "",
                "This ensures all actions are grounded in the project's ontology.",
            ]),
        }
        json.dump(output, sys.stdout)
        sys.exit(2)

    # Foundation was resolved — read the state for logging, then clean up
    try:
        with open(state_file, "r") as f:
            state = json.load(f)
        call_count = len(state.get("calls", []))
        if call_count < 2:
            # Only one Kantext call was made — likely just server_status without
            # actually rendering/querying the foundation. Warn but allow.
            print(
                json.dumps({
                    "decision": "block",
                    "reason": "\n".join([
                        f"Only {call_count} Kantext tool call(s) were made this session.",
                        "A thorough ontological resolution requires at minimum:",
                        "  - server_status() to check workspace state",
                        "  - render() or query() to actually read the ontological foundation",
                        "",
                        "Please call render() or query() on the loaded contexts to validate",
                        "your work against the project's ontology before completing.",
                    ]),
                }),
                file=sys.stdout,
            )
            sys.exit(2)
    except (json.JSONDecodeError, OSError):
        pass

    cleanup_state(session_id)
    sys.exit(0)


if __name__ == "__main__":
    main()
