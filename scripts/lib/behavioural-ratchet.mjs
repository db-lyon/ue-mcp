// The coverage files and the one rule the ratchet adds on top of an
// inventory: the grandfathered gap list may only shrink (#1223). Two
// inventories share it: bridge actions (behavioural-*) and every other
// user-facing surface (integration-*). Shared by both generators, their unit
// tests and the release gate.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

export const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");

/** Repo-relative paths, so a gate can read the same file at another commit. */
export const COVERAGE_DIR = "tests/coverage";
export const INVENTORY_FILE = `${COVERAGE_DIR}/behavioural-coverage.json`;
export const RATCHET_FILE = `${COVERAGE_DIR}/behavioural-gaps.json`;
export const EXEMPTIONS_FILE = `${COVERAGE_DIR}/behavioural-exemptions.json`;

/** The non-bridge surfaces: the hand-kept manifest, its generated inventory, gaps and exemptions. */
export const INTEGRATION_MANIFEST_FILE = `${COVERAGE_DIR}/integration-surfaces.json`;
export const INTEGRATION_INVENTORY_FILE = `${COVERAGE_DIR}/integration-coverage.json`;
export const INTEGRATION_RATCHET_FILE = `${COVERAGE_DIR}/integration-gaps.json`;
export const INTEGRATION_EXEMPTIONS_FILE = `${COVERAGE_DIR}/integration-exemptions.json`;

/** An exemption reason shorter than this is a label, not a reason. */
export const MIN_EXEMPTION_REASON = 40;

/**
 * Parse the ratchet file's text: `{ "note": string, "gaps": string[] }`.
 * @param {string} text
 * @returns {string[]}
 */
export function parseRatchet(text, file = RATCHET_FILE) {
  const parsed = JSON.parse(text);
  if (!parsed || !Array.isArray(parsed.gaps)) throw new Error(`${file} has no "gaps" array`);
  return parsed.gaps.map(String);
}

/** @returns {string[]} */
export function readRatchet(root = REPO_ROOT, file = RATCHET_FILE) {
  return parseRatchet(fs.readFileSync(path.join(root, file), "utf8"), file);
}

/**
 * Parse the exemptions file's text: `{ "note": string, "exemptions": { method: reason } }`.
 * @param {string} text
 * @returns {Record<string, string>}
 */
export function parseExemptions(text, file = EXEMPTIONS_FILE) {
  const parsed = JSON.parse(text);
  if (!parsed || typeof parsed.exemptions !== "object" || parsed.exemptions === null) {
    throw new Error(`${file} has no "exemptions" object`);
  }
  return Object.fromEntries(Object.entries(parsed.exemptions).map(([k, v]) => [k, String(v)]));
}

/** @returns {Record<string, string>} */
export function readExemptions(root = REPO_ROOT, file = EXEMPTIONS_FILE) {
  return parseExemptions(fs.readFileSync(path.join(root, file), "utf8"), file);
}

/**
 * Entries the head ratchet carries that the baseline did not. Any entry is a
 * newly grandfathered gap, which is exactly what the ratchet exists to refuse.
 * @param {string[]} baseline
 * @param {string[]} head
 * @returns {string[]}
 */
export function ratchetGrowth(baseline, head) {
  const before = new Set(baseline);
  return head.filter((m) => !before.has(m)).sort();
}
