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
  CLEAR_LINE,
  CYAN,
  DIM,
  GREEN,
  HIDE_CURSOR,
  MOVE_UP,
  RESET,
  SHOW_CURSOR,
} from "./ansi.js";

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

    function render(firstRender = false) {
      if (!firstRender) {
        // Move cursor back up to redraw
        process.stdout.write(MOVE_UP(items.length + 1));
      }

      console.log(
        `  ${BOLD}?${RESET} ${title} ${DIM}(↑↓ move, space toggle, enter confirm)${RESET}`,
      );

      for (let i = 0; i < items.length; i++) {
        process.stdout.write(CLEAR_LINE);
        const pointer = i === cursor ? `${CYAN}❯${RESET}` : " ";
        const check = states[i]
          ? `${GREEN}◉${RESET}`
          : `${DIM}○${RESET}`;
        const label =
          i === cursor ? `${BOLD}${items[i].label}${RESET}` : items[i].label;
        const suffix = items[i].suffix
          ? ` ${DIM}${items[i].suffix}${RESET}`
          : "";
        console.log(`   ${pointer} ${check} ${label}${suffix}`);
      }
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

        // Show final state
        process.stdout.write(MOVE_UP(items.length + 1));
        const enabled = items
          .filter((_, i) => states[i])
          .map((i) => i.label);
        const disabled = items
          .filter((_, i) => !states[i])
          .map((i) => i.label);
        process.stdout.write(CLEAR_LINE);

        if (disabled.length === 0) {
          console.log(`  ${GREEN}✓${RESET} All ${items.length} categories enabled`);
        } else {
          console.log(
            `  ${GREEN}✓${RESET} ${enabled.length} enabled, ${disabled.length} disabled: ${DIM}${disabled.join(", ")}${RESET}`,
          );
        }

        // Clear the item lines
        for (let i = 0; i < items.length; i++) {
          process.stdout.write(CLEAR_LINE + "\n");
        }
        process.stdout.write(MOVE_UP(items.length));

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

    function render(firstRender = false) {
      if (!firstRender) {
        process.stdout.write(MOVE_UP(items.length + 1));
      }

      console.log(
        `  ${BOLD}?${RESET} ${title} ${DIM}(↑↓ move, enter select)${RESET}`,
      );

      for (let i = 0; i < items.length; i++) {
        process.stdout.write(CLEAR_LINE);
        const pointer = i === cursor ? `${CYAN}❯${RESET}` : " ";
        const label =
          i === cursor ? `${BOLD}${items[i]}${RESET}` : items[i];
        console.log(`   ${pointer} ${label}`);
      }
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

        process.stdout.write(MOVE_UP(items.length + 1));
        process.stdout.write(CLEAR_LINE);
        console.log(
          `  ${GREEN}✓${RESET} ${title}: ${BOLD}${items[cursor]}${RESET}`,
        );
        for (let i = 0; i < items.length; i++) {
          process.stdout.write(CLEAR_LINE + "\n");
        }
        process.stdout.write(MOVE_UP(items.length));

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
