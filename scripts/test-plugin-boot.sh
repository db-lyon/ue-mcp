#!/usr/bin/env bash
# Boot the local server against tests/ue_mcp/ue_mcp.uproject just long enough
# to capture the plugin loader's startup logs, then print the lines that say
# whether the configured plugins loaded.
#
# Exit status: 0 if at least one plugin loaded successfully (look for
# "loaded N/N plugin(s)" where the two N's are equal and non-zero); 1 if any
# plugin warned or all were skipped.

set -u
cd "$(dirname "$0")/.."

LOG=$(mktemp -t ue-mcp-boot.XXXXXX.log)
trap 'rm -f "$LOG"' EXIT

echo "→ building"
npx tsc

echo "→ booting server (5s)"
node dist/index.js tests/ue_mcp/ue_mcp.uproject </dev/null 2>"$LOG" &
PID=$!
sleep 5
kill -TERM "$PID" 2>/dev/null || true
wait "$PID" 2>/dev/null || true

echo
echo "── plugin lines from boot log ────────────────────────────────"
grep -iE 'plugin|tools, [0-9]+ tasks' "$LOG" || {
  echo "(no plugin or registration lines in boot log)"
  echo
  echo "── full log ──────────────────────────────────────────────────"
  cat "$LOG"
  exit 1
}

if grep -qE 'warn plugin' "$LOG"; then
  echo
  echo "FAIL: plugin loader emitted a warning"
  exit 1
fi

if grep -qE 'loaded ([1-9][0-9]*)/\1 plugin\(s\)' "$LOG"; then
  echo
  echo "OK"
  exit 0
fi

echo
echo "FAIL: did not see a successful 'loaded N/N plugin(s)' line"
exit 1
