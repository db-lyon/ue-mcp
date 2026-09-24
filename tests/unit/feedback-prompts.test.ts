/**
 * #1151: the feedback nudges and the update notice can be switched off.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";

import {
  feedbackPromptsActive,
  feedbackPromptsEnabled,
  setFeedbackPrompts,
  setUpdateNotices,
  updateNoticesEnabled,
} from "../../src/user-state.js";
import {
  SERVER_INSTRUCTIONS,
  SERVER_INSTRUCTIONS_LEAN,
  withoutFeedbackSection,
} from "../../src/instructions.js";

let saved: string | undefined;
let dir: string;

beforeEach(() => {
  saved = process.env.UE_MCP_USER_STATE;
  dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-prompts-"));
  process.env.UE_MCP_USER_STATE = path.join(dir, "state.json");
});

afterEach(() => {
  if (saved === undefined) delete process.env.UE_MCP_USER_STATE;
  else process.env.UE_MCP_USER_STATE = saved;
  fs.rmSync(dir, { recursive: true, force: true });
});

describe("feedback prompts", () => {
  it("default on, and off when the user says so", () => {
    expect(feedbackPromptsEnabled()).toBe(true);
    setFeedbackPrompts(false);
    expect(feedbackPromptsEnabled()).toBe(false);
    setFeedbackPrompts(true);
    expect(feedbackPromptsEnabled()).toBe(true);
  });

  it("are off when the project disables the feedback category", () => {
    expect(feedbackPromptsActive(["gas"])).toBe(true);
    expect(feedbackPromptsActive(["feedback"])).toBe(false);
  });
});

describe("update notices", () => {
  it("default on, and off when the user says so", () => {
    expect(updateNoticesEnabled()).toBe(true);
    setUpdateNotices(false);
    expect(updateNoticesEnabled()).toBe(false);
    setUpdateNotices(true);
    expect(updateNoticesEnabled()).toBe(true);
  });
});

describe("withoutFeedbackSection", () => {
  it.each([
    ["full", SERVER_INSTRUCTIONS],
    ["lean", SERVER_INSTRUCTIONS_LEAN],
  ])("drops the FEEDBACK block from the %s instructions and keeps the rest", (_name, text) => {
    expect(text).toContain("═══ FEEDBACK ═══");
    const out = withoutFeedbackSection(text);
    expect(out).not.toContain("═══ FEEDBACK ═══");
    expect(out).not.toContain("feedback(action=\"submit\")");
    expect(out).toContain("═══ FLOWS");
  });
});
