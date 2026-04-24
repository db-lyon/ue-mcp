/**
 * ANSI escape-code constants and tiny status helpers.
 *
 * Kept in one place so every interactive surface (init, resolve, hook-handler
 * diagnostics) renders consistent colours and icons.
 */

export const RESET = "\x1b[0m";
export const BOLD = "\x1b[1m";
export const DIM = "\x1b[2m";
export const GREEN = "\x1b[32m";
export const YELLOW = "\x1b[33m";
export const CYAN = "\x1b[36m";
export const RED = "\x1b[31m";

export const HIDE_CURSOR = "\x1b[?25l";
export const SHOW_CURSOR = "\x1b[?25h";
export const CLEAR_LINE = "\x1b[2K";
export const MOVE_UP = (n: number): string => `\x1b[${n}A`;

export const ok = (msg: string): void => console.log(`  ${GREEN}✓${RESET} ${msg}`);
export const warn = (msg: string): void => console.log(`  ${YELLOW}!${RESET} ${msg}`);
export const fail = (msg: string): void => console.log(`  ${RED}✗${RESET} ${msg}`);
export const info = (msg: string): void => console.log(`  ${DIM}${msg}${RESET}`);
