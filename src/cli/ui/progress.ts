/**
 * A single-line terminal progress bar for long waits (editor startup, builds).
 *
 * Everything goes to stderr: stdout is the MCP stdio channel and a stray byte
 * there corrupts the protocol. On a TTY the line rewrites in place; when the
 * output is piped or logged (CI, an agent transcript) it degrades to one plain
 * line per change, so a log does not fill with escape codes.
 */

import { BOLD, CYAN, DIM, GREEN, RESET, HIDE_CURSOR, SHOW_CURSOR, CLEAR_LINE } from "./ansi.js";

const SPINNER = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"];
const BAR_WIDTH = 24;

export interface ProgressState {
  /** 0..1, or null when the work has no measurable fraction yet. */
  fraction: number | null;
  /** Short label: what is happening right now. */
  message: string;
  /** Optional right-hand detail (module counts, elapsed time). */
  detail?: string;
}

export interface ProgressBar {
  update(state: ProgressState): void;
  /** Stop rendering. `finalLine`, if given, is left on screen. */
  stop(finalLine?: string): void;
}

function renderBar(fraction: number | null, frame: number): string {
  if (fraction === null) {
    return `${CYAN}${SPINNER[frame % SPINNER.length]}${RESET}`;
  }
  const clamped = Math.max(0, Math.min(1, fraction));
  const filled = Math.round(clamped * BAR_WIDTH);
  return `${GREEN}${"█".repeat(filled)}${RESET}${DIM}${"░".repeat(BAR_WIDTH - filled)}${RESET} ${String(Math.round(clamped * 100)).padStart(3)}%`;
}

/**
 * Start a progress bar. Returns a handle even when stderr is not a TTY, so
 * callers never branch on it.
 */
export function startProgress(title: string): ProgressBar {
  const stream = process.stderr;
  const isTty = Boolean(stream.isTTY);
  let frame = 0;
  let last: ProgressState = { fraction: null, message: "starting" };
  let lastPlain = "";
  let stopped = false;

  const draw = (): void => {
    if (stopped) return;
    const detail = last.detail ? ` ${DIM}${last.detail}${RESET}` : "";
    const line = `  ${BOLD}${title}${RESET} ${renderBar(last.fraction, frame)} ${last.message}${detail}`;
    stream.write(`\r${CLEAR_LINE}${line}`);
  };

  let timer: NodeJS.Timeout | null = null;
  if (isTty) {
    stream.write(HIDE_CURSOR);
    // Redraw on a timer as well as on update, so the spinner keeps moving
    // while the engine is between progress reports.
    timer = setInterval(() => {
      frame += 1;
      draw();
    }, 120);
    timer.unref?.();
  } else {
    stream.write(`  ${title}\n`);
  }

  return {
    update(state: ProgressState): void {
      if (stopped) return;
      last = state;
      if (isTty) {
        draw();
        return;
      }
      // Non-TTY: one line per distinct message, no rewriting.
      const pct = state.fraction === null ? "" : ` ${Math.round(state.fraction * 100)}%`;
      const plain = `  ${state.message}${pct}${state.detail ? ` (${state.detail})` : ""}`;
      if (plain !== lastPlain) {
        stream.write(`${plain}\n`);
        lastPlain = plain;
      }
    },
    stop(finalLine?: string): void {
      if (stopped) return;
      stopped = true;
      if (timer) clearInterval(timer);
      if (isTty) {
        stream.write(`\r${CLEAR_LINE}`);
        stream.write(SHOW_CURSOR);
      }
      if (finalLine) stream.write(`  ${finalLine}\n`);
    },
  };
}
