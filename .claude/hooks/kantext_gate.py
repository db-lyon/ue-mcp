#!/usr/bin/env python3
"""
PreToolUse hook: Block all non-Kantext tools until the ontological foundation
has been resolved for this session.

Flow:
  1. Tool call arrives
  2. If it's a Kantext MCP tool → allow (the agent is resolving the foundation)
  3. If the foundation state file exists → allow (already resolved)
  4. Otherwise → deny with detailed instructions to resolve foundation first
"""

import json
import os
import sys
import time

STALE_THRESHOLD_SECONDS = 8 * 60 * 60  # 8 hours


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


def deny_with_instructions():
    output = {
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "deny",
            "permissionDecisionReason": (
                "Kantext ontological foundation has not been resolved for this session."
            ),
            "additionalContext": "\n".join([
                "REQUIRED: Before taking any other action, you MUST resolve the project's "
                "ontological foundation via the Kantext MCP server.",
                "",
                "Steps:",
                "1. Call server_status(include_contexts=true) to verify the workspace and see loaded contexts",
                "2. Call list_contexts() to enumerate all available knowledge contexts",
                "3. Call render() with template='model' on the primary contexts to understand the foundation:",
                "   - UEConcepts: Asset taxonomy, UE type system, relationships, modules, PCG, landscape, foliage, config",
                "   - BlueprintOntology: Blueprint anatomy, editor states, compilation lifecycle",
                "   - Traits: Replication, serialization, blueprint exposure, threading",
                "   - Workflows: Development patterns (UnderstandProject, CreateBlueprint, ModifyBlueprint, etc.)",
                "   - McpSurface: Complete MCP tool surface organized by capability domain",
                "",
                "The .kantext/ directory contains these pre-loaded contexts. They define the ontological",
                "foundation for this UE-MCP project. You must internalize this foundation before proceeding.",
                "",
                "Only after understanding the ontological context should you address the user's request.",
            ])
        }
    }
    json.dump(output, sys.stdout)


def main():
    try:
        input_data = json.loads(sys.stdin.read())
    except (json.JSONDecodeError, EOFError):
        sys.exit(0)

    tool_name = input_data.get("tool_name", "")
    session_id = input_data.get("session_id", "default")

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

    deny_with_instructions()
    sys.exit(0)


if __name__ == "__main__":
    main()
