#!/usr/bin/env python3
"""Run every benchmark check against a running Unreal Editor and print a score.

The checks read editor state through Unreal's own Python API, so they score any
MCP server, or none, the same way. This script sends them to the editor over
the Python plugin's remote execution, the channel Epic ships for it. It talks
to the editor only; it never starts or stops one, and it never calls an MCP
server.

Requirements:
  - Python 3.8 or later.
  - The editor has the Python Editor Script Plugin enabled and remote execution
    turned on: Project Settings > Plugins > Python > Enable Remote Execution, or
    launch the editor with
    -ini:Engine:[/Script/PythonScriptPlugin.PythonScriptPluginSettings]:bRemoteExecution=True

Usage:
  python benchmark/run_checks.py --engine "C:/Program Files/Epic Games/UE_5.7"
  python benchmark/run_checks.py --project MyGame --only 01,03 --json

Without remote execution, paste each file in benchmark/checks into the
editor's Output Log with the console set to Python, or run
`py "<path>/benchmark/checks/01_blueprint_function.py"` from the Cmd console.
Each prints one PASS or FAIL line.
"""
import argparse
import glob
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CHECKS = os.path.join(HERE, "checks")
REMOTE_EXECUTION = os.path.join("Engine", "Plugins", "Experimental", "PythonScriptPlugin", "Content", "Python")


def find_remote_execution(engine):
    """The directory holding the engine's remote_execution.py."""
    candidates = []
    if engine:
        candidates.append(engine)
    for var in ("UE_ROOT", "UE_MCP_TEST_ENGINE_ROOT"):
        if os.environ.get(var):
            candidates.append(os.environ[var])
    candidates += sorted(glob.glob("C:/Program Files/Epic Games/UE_5.*"), reverse=True)
    candidates += sorted(glob.glob("/Users/Shared/Epic Games/UE_5.*"), reverse=True)
    for root in candidates:
        root = root.rstrip("/\\")
        if os.path.basename(root) == "Engine":
            root = os.path.dirname(root)
        path = os.path.join(root, REMOTE_EXECUTION)
        if os.path.isfile(os.path.join(path, "remote_execution.py")):
            return path
    return None


def pick_node(nodes, project):
    """The one editor to score. Several editors and no --project is refused, so a check never reaches the wrong one."""
    if project:
        want = project.lower().replace("\\", "/").rstrip("/")
        nodes = [
            n for n in nodes
            if n.get("project_name", "").lower() == want
            or n.get("project_root", "").lower().replace("\\", "/").rstrip("/") == want
        ]
    if len(nodes) == 1:
        return nodes[0], None
    listing = "\n".join("  %s  %s" % (n.get("project_name"), n.get("project_root")) for n in nodes) or "  (none)"
    if not nodes:
        return None, "No editor answered%s. Is remote execution on? Editors seen:\n%s" % (
            " for --project %s" % project if project else "", listing)
    return None, "More than one editor answered; name one with --project <name or root>:\n" + listing


def verdict(output_lines, check_id):
    for line in reversed(output_lines):
        text = line.strip()
        if text.startswith("PASS %s" % check_id) or text.startswith("FAIL %s" % check_id):
            return text.startswith("PASS"), text
    return False, "FAIL %s: the check printed no verdict" % check_id


def main():
    parser = argparse.ArgumentParser(description="Score the benchmark tasks against a running editor.")
    parser.add_argument("--engine", help="Engine root, used to find remote_execution.py (default: UE_ROOT, then the usual install folders)")
    parser.add_argument("--project", help="Project name or root of the editor to score, required when more than one editor is running")
    parser.add_argument("--only", help="Comma-separated check numbers or ids, e.g. 01,03")
    parser.add_argument("--json", action="store_true", help="Print one JSON document")
    parser.add_argument("--discover-seconds", type=float, default=6.0, help="How long to wait for editors to answer (default 6)")
    args = parser.parse_args()

    files = sorted(glob.glob(os.path.join(CHECKS, "*.py")))
    if args.only:
        wanted = [w.strip() for w in args.only.split(",") if w.strip()]
        files = [f for f in files if any(os.path.basename(f).startswith(w) for w in wanted)]
    if not files:
        print("No checks selected.", file=sys.stderr)
        return 2

    path = find_remote_execution(args.engine)
    if not path:
        print("Could not find the engine's remote_execution.py; pass --engine <engine root>.", file=sys.stderr)
        return 2
    sys.path.insert(0, path)
    import remote_execution  # noqa: E402  (the engine's own module)

    session = remote_execution.RemoteExecution()
    session.start()
    try:
        deadline = time.time() + args.discover_seconds
        while time.time() < deadline:
            time.sleep(0.5)
        node, problem = pick_node(session.remote_nodes, args.project)
        if problem:
            print(problem, file=sys.stderr)
            return 2
        session.open_command_connection(node["node_id"])
        results = []
        for f in files:
            check_id = os.path.splitext(os.path.basename(f))[0]
            with open(f, encoding="utf-8") as fh:
                source = fh.read()
            # ExecuteFile reads a command that names a .py file as a path, so literal code
            # that happens to contain ".py " is sent as the file's path instead.
            command = '"%s"' % f.replace("\\", "/") if (".py " in source or source.rstrip().endswith(".py")) else source
            reply = session.run_command(command, unattended=True, exec_mode=remote_execution.MODE_EXEC_FILE)
            lines = [o.get("output", "") for o in reply.get("output", [])]
            if not reply.get("success"):
                lines.append("FAIL %s: the editor raised %s" % (check_id, reply.get("result")))
            passed, line = verdict(lines, check_id)
            results.append({"id": check_id, "pass": passed, "line": line})
            if not args.json:
                print(line)
    finally:
        session.stop()

    score = sum(1 for r in results if r["pass"])
    summary = {
        "project": node.get("project_name"),
        "engine": node.get("engine_version"),
        "score": score,
        "total": len(results),
        "results": results,
    }
    if args.json:
        print(json.dumps(summary, indent=2))
    else:
        print("\nScore: %d / %d  (%s, UE %s)" % (score, len(results), summary["project"], summary["engine"]))
    return 0 if score == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
