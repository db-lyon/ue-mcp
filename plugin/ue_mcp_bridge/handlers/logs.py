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


def list_crashes(params: dict) -> dict:
    """List all crash reports in Saved/Crashes."""
    _require_unreal()

    project_dir = str(unreal.Paths.project_dir()) if hasattr(unreal.Paths, "project_dir") else ""
    crashes_dir = os.path.join(project_dir, "Saved", "Crashes")

    crashes = []
    if os.path.isdir(crashes_dir):
        for crash_folder in os.listdir(crashes_dir):
            crash_path = os.path.join(crashes_dir, crash_folder)
            if os.path.isdir(crash_path):
                crash_info = {
                    "folder": crash_folder,
                    "path": crash_path,
                    "files": [],
                }
                try:
                    stat = os.stat(crash_path)
                    crash_info["modified"] = stat.st_mtime
                except Exception:
                    pass

                # List files in crash folder
                try:
                    for file in os.listdir(crash_path):
                        file_path = os.path.join(crash_path, file)
                        if os.path.isfile(file_path):
                            crash_info["files"].append(file)
                except Exception:
                    pass

                crashes.append(crash_info)

    # Sort by modification time (newest first)
    crashes.sort(key=lambda x: x.get("modified", 0), reverse=True)

    return {
        "crashesDir": crashes_dir,
        "crashCount": len(crashes),
        "crashes": crashes,
    }


def get_crash_info(params: dict) -> dict:
    """Get detailed information about a specific crash report."""
    _require_unreal()

    crash_folder = params.get("crashFolder", "")
    if not crash_folder:
        raise ValueError("crashFolder parameter required")

    project_dir = str(unreal.Paths.project_dir()) if hasattr(unreal.Paths, "project_dir") else ""
    crash_path = os.path.join(project_dir, "Saved", "Crashes", crash_folder)

    if not os.path.isdir(crash_path):
        return {"available": False, "note": f"Crash folder not found: {crash_folder}"}

    crash_info = {
        "folder": crash_folder,
        "path": crash_path,
        "files": {},
    }

    # Read all files in crash folder
    for file in os.listdir(crash_path):
        file_path = os.path.join(crash_path, file)
        if os.path.isfile(file_path):
            try:
                stat = os.stat(file_path)
                file_info = {
                    "size": stat.st_size,
                    "modified": stat.st_mtime,
                }

                # Try to read text files
                if file.endswith((".log", ".txt", ".ini", ".xml", ".json")):
                    try:
                        with open(file_path, "r", encoding="utf-8", errors="replace") as f:
                            content = f.read()
                            file_info["content"] = content
                            file_info["lineCount"] = len(content.splitlines())
                    except Exception:
                        pass

                crash_info["files"][file] = file_info
            except Exception:
                pass

    return crash_info


def check_for_crashes(params: dict) -> dict:
    """Check if editor has crashed recently (detects if editor process died unexpectedly)."""
    _require_unreal()

    project_dir = str(unreal.Paths.project_dir()) if hasattr(unreal.Paths, "project_dir") else ""
    crashes_dir = os.path.join(project_dir, "Saved", "Crashes")
    log_dir = os.path.join(project_dir, "Saved", "Logs")

    recent_crashes = []
    if os.path.isdir(crashes_dir):
        import time
        now = time.time()
        # Check crashes from last 24 hours
        recent_threshold = now - (24 * 60 * 60)

        for crash_folder in os.listdir(crashes_dir):
            crash_path = os.path.join(crashes_dir, crash_folder)
            if os.path.isdir(crash_path):
                try:
                    stat = os.stat(crash_path)
                    if stat.st_mtime > recent_threshold:
                        recent_crashes.append({
                            "folder": crash_folder,
                            "path": crash_path,
                            "timestamp": stat.st_mtime,
                        })
                except Exception:
                    pass

    # Check log for crash indicators
    log_file = os.path.join(log_dir, "Editor.log")
    crash_indicators = []
    if os.path.exists(log_file):
        try:
            with open(log_file, "r", encoding="utf-8", errors="replace") as f:
                lines = f.readlines()
                # Look for crash-related keywords in last 1000 lines
                for i, line in enumerate(lines[-1000:], len(lines) - 1000):
                    line_lower = line.lower()
                    if any(keyword in line_lower for keyword in ["crash", "fatal error", "assertion failed", "access violation", "exception"]):
                        crash_indicators.append({
                            "line": i + 1,
                            "content": line.rstrip(),
                        })
        except Exception:
            pass

    return {
        "recentCrashCount": len(recent_crashes),
        "recentCrashes": recent_crashes,
        "crashIndicatorsInLog": len(crash_indicators),
        "logIndicators": crash_indicators[-20:] if crash_indicators else [],  # Last 20 indicators
    }


HANDLERS = {
    "get_output_log": get_output_log,
    "get_message_log": get_message_log,
    "search_log": search_log,
    "list_crashes": list_crashes,
    "get_crash_info": get_crash_info,
    "check_for_crashes": check_for_crashes,
}
