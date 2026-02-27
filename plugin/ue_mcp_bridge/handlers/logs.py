"""Editor log access handlers."""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False

import os
import glob as globmod


def _require_unreal():
    if not HAS_UNREAL:
        raise RuntimeError("Unreal module not available")


def get_output_log(params: dict) -> dict:
    """Read recent entries from the editor output log file."""
    _require_unreal()

    project_dir = str(unreal.Paths.project_dir()) if hasattr(unreal.Paths, "project_dir") else ""
    log_dir = os.path.join(project_dir, "Saved", "Logs")

    if not os.path.isdir(log_dir):
        return {"available": False, "logDir": log_dir, "note": "Log directory not found"}

    log_file = os.path.join(log_dir, "Editor.log")
    if not os.path.exists(log_file):
        log_files = sorted(globmod.glob(os.path.join(log_dir, "*.log")), key=os.path.getmtime, reverse=True)
        log_file = log_files[0] if log_files else None

    if not log_file or not os.path.exists(log_file):
        return {"available": False, "note": "No log file found"}

    max_lines = params.get("maxLines", 100)
    filter_text = params.get("filter", "")
    category_filter = params.get("category", "")

    with open(log_file, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    tail = lines[-max_lines * 3:] if len(lines) > max_lines * 3 else lines

    if filter_text or category_filter:
        filtered = []
        for line in tail:
            if filter_text and filter_text.lower() not in line.lower():
                continue
            if category_filter and category_filter not in line:
                continue
            filtered.append(line.rstrip())
        result_lines = filtered[-max_lines:]
    else:
        result_lines = [l.rstrip() for l in tail[-max_lines:]]

    return {
        "logFile": log_file,
        "lineCount": len(result_lines),
        "totalLines": len(lines),
        "lines": result_lines,
    }


def get_message_log(params: dict) -> dict:
    """Get entries from the Message Log (compiler errors, map check, etc.)."""
    _require_unreal()

    log_name = params.get("logName", "BlueprintLog")

    return {
        "logName": log_name,
        "note": "Message Log reading requires FMessageLog C++ API. Use get_output_log for file-based log access, or editor.execute_python with unreal.log() for specific queries.",
    }


def search_log(params: dict) -> dict:
    """Search log file for a pattern."""
    _require_unreal()

    query = params.get("query", "")
    if not query:
        raise ValueError("query parameter required")

    project_dir = str(unreal.Paths.project_dir()) if hasattr(unreal.Paths, "project_dir") else ""
    log_dir = os.path.join(project_dir, "Saved", "Logs")
    log_file = os.path.join(log_dir, "Editor.log")

    if not os.path.exists(log_file):
        return {"available": False, "note": "Log file not found"}

    matches = []
    with open(log_file, "r", encoding="utf-8", errors="replace") as f:
        for i, line in enumerate(f, 1):
            if query.lower() in line.lower():
                matches.append({"line": i, "content": line.rstrip()})
                if len(matches) >= 200:
                    break

    return {"query": query, "matchCount": len(matches), "matches": matches}


HANDLERS = {
    "get_output_log": get_output_log,
    "get_message_log": get_message_log,
    "search_log": search_log,
}
