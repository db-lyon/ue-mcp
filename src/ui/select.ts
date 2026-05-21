/**
 * Interactive terminal selectors used during `ue-mcp init`.
 *
 * checkboxSelect - multiple items, toggle with space, enter to confirm.
 * singleSelect   - one-of-N, enter to confirm.
 * multiSelect    - every item default-checked, thin wrapper over checkboxSelect.
 *
 * Ctrl+C exits the process. Arrow keys and k/j both move the cursor.
 */

import {
  BOLD,
  CYAN,
  DIM,
  GREEN,
  HIDE_CURSOR,
  MOVE_UP,
  RESET,
  SHOW_CURSOR,
} from "./ansi.js";

// Clears from the cursor to the end of the screen (CSI 0 J).
const CLEAR_TO_END = "\x1b[J";

// Strip CSI/SGR escapes so width math counts only visible characters.
function visibleLength(s: string): number {
  // eslint-disable-next-line no-control-regex
  return s.replace(/\x1b\[[0-9;?]*[a-zA-Z]/g, "").length;
}

// How many terminal rows a printed line actually occupies once the
// terminal wraps it at the column boundary. console.log adds a newline
// regardless, so a 0-char line still takes one row.
function visualRowsForLine(line: string, width: number): number {
  const len = visibleLength(line);
  if (len === 0) return 1;
  return Math.max(1, Math.ceil(len / width));
}

export interface CheckboxItem {
  label: string;
  checked: boolean;
  suffix?: string;
}

export function checkboxSelect(
  title: string,
  items: CheckboxItem[],
): Promise<boolean[]> {
  return new Promise((resolve) => {
    let cursor = 0;
    const states = items.map((i) => i.checked);

    process.stdout.write(HIDE_CURSOR);

    // Track how many TERMINAL rows the previous frame occupied — including
    // any rows produced by wrapping a long label+suffix at the column
    // boundary. Without this, MOVE_UP only walks back logical lines and
    // leaves wrapped fragments stranded above each redraw.
    let lastRowsRendered = 0;

    function render(firstRender = false) {
      const width = process.stdout.columns ?? 80;

      if (!firstRender) {
        // Walk the cursor back to the start of the previous frame, then
        // erase everything from there to the end of the screen so wrapped
        // fragments from the previous render can't leak through.
        process.stdout.write(MOVE_UP(lastRowsRendered));
        process.stdout.write("\r");
        process.stdout.write(CLEAR_TO_END);
      }

      const titleLine = `  ${BOLD}?${RESET} ${title} ${DIM}(↑↓ move, space toggle, enter confirm)${RESET}`;
      console.log(titleLine);
      let rows = visualRowsForLine(titleLine, width);

      for (let i = 0; i < items.length; i++) {
        const pointer = i === cursor ? `${CYAN}❯${RESET}` : " ";
        const check = states[i]
          ? `${GREEN}◉${RESET}`
          : `${DIM}○${RESET}`;
        const label =
          i === cursor ? `${BOLD}${items[i].label}${RESET}` : items[i].label;
        const suffix = items[i].suffix
          ? ` ${DIM}${items[i].suffix}${RESET}`
          : "";
        const itemLine = `   ${pointer} ${check} ${label}${suffix}`;
        console.log(itemLine);
        rows += visualRowsForLine(itemLine, width);
      }

      lastRowsRendered = rows;
    }

    render(true);

    const stdin = process.stdin;
    stdin.setRawMode(true);
    stdin.resume();
    stdin.setEncoding("utf-8");

    function onData(key: string) {
      // Ctrl+C
      if (key === "\x03") {
        process.stdout.write(SHOW_CURSOR);
        stdin.setRawMode(false);
        stdin.removeListener("data", onData);
        process.exit(0);
      }

      // Enter
      if (key === "\r" || key === "\n") {
        process.stdout.write(SHOW_CURSOR);
        stdin.setRawMode(false);
        stdin.pause();
        stdin.removeListener("data", onData);

        // Walk back to the top of the menu and erase the whole frame so
        // we can replace it with a one-line summary. lastRowsRendered
        // includes wrapped rows; MOVE_UP by logical-item-count would
        // strand wrapped fragments above the summary.
        process.stdout.write(MOVE_UP(lastRowsRendered));
        process.stdout.write("\r");
        process.stdout.write(CLEAR_TO_END);

        const enabledLabels = items
          .filter((_, i) => states[i])
          .map((i) => i.label);
        const disabledLabels = items
          .filter((_, i) => !states[i])
          .map((i) => i.label);

        if (disabledLabels.length === 0) {
          console.log(`  ${GREEN}✓${RESET} ${title}: all ${items.length} selected`);
        } else {
          console.log(
            `  ${GREEN}✓${RESET} ${title}: ${enabledLabels.length}/${items.length} selected${disabledLabels.length > 0 ? ` ${DIM}(skipped: ${disabledLabels.join(", ")})${RESET}` : ""}`,
          );
        }

        resolve(states);
        return;
      }

      // Space — toggle
      if (key === " ") {
        states[cursor] = !states[cursor];
        render();
        return;
      }

      // Arrow up / k
      if (key === "\x1b[A" || key === "k") {
        cursor = (cursor - 1 + items.length) % items.length;
        render();
        return;
      }

      // Arrow down / j
      if (key === "\x1b[B" || key === "j") {
        cursor = (cursor + 1) % items.length;
        render();
        return;
      }

      // 'a' — toggle all
      if (key === "a") {
        const allChecked = states.every(Boolean);
        for (let i = 0; i < states.length; i++) states[i] = !allChecked;
        render();
        return;
      }
    }

    stdin.on("data", onData);
  });
}

export function singleSelect(
  title: string,
  items: string[],
): Promise<number> {
  return new Promise((resolve) => {
    let cursor = 0;

    process.stdout.write(HIDE_CURSOR);

    let lastRowsRendered = 0;

    function render(firstRender = false) {
      const width = process.stdout.columns ?? 80;

      if (!firstRender) {
        process.stdout.write(MOVE_UP(lastRowsRendered));
        process.stdout.write("\r");
        process.stdout.write(CLEAR_TO_END);
      }

      const titleLine = `  ${BOLD}?${RESET} ${title} ${DIM}(↑↓ move, enter select)${RESET}`;
      console.log(titleLine);
      let rows = visualRowsForLine(titleLine, width);

      for (let i = 0; i < items.length; i++) {
        const pointer = i === cursor ? `${CYAN}❯${RESET}` : " ";
        const label =
          i === cursor ? `${BOLD}${items[i]}${RESET}` : items[i];
        const itemLine = `   ${pointer} ${label}`;
        console.log(itemLine);
        rows += visualRowsForLine(itemLine, width);
      }

      lastRowsRendered = rows;
    }

    render(true);

    const stdin = process.stdin;
    stdin.setRawMode(true);
    stdin.resume();
    stdin.setEncoding("utf-8");

    function onData(key: string) {
      if (key === "\x03") {
        process.stdout.write(SHOW_CURSOR);
        stdin.setRawMode(false);
        stdin.removeListener("data", onData);
        process.exit(0);
      }

      if (key === "\r" || key === "\n") {
        process.stdout.write(SHOW_CURSOR);
        stdin.setRawMode(false);
        stdin.pause();
        stdin.removeListener("data", onData);

        process.stdout.write(MOVE_UP(lastRowsRendered));
        process.stdout.write("\r");
        process.stdout.write(CLEAR_TO_END);
        console.log(
          `  ${GREEN}✓${RESET} ${title}: ${BOLD}${items[cursor]}${RESET}`,
        );

        resolve(cursor);
        return;
      }

      if (key === "\x1b[A" || key === "k") {
        cursor = (cursor - 1 + items.length) % items.length;
        render();
      }
      if (key === "\x1b[B" || key === "j") {
        cursor = (cursor + 1) % items.length;
        render();
      }
    }

    stdin.on("data", onData);
  });
}

export function multiSelect(
  title: string,
  items: string[],
): Promise<boolean[]> {
  const checkItems = items.map((label) => ({
    label,
    checked: true,
  }));
  return checkboxSelect(title, checkItems);
}
