/**
 * Unit tests never read the developer's ~/.ue-mcp/state.json. A stored
 * preference there (dialog mode "auto", feedback prompts off) changed what the
 * code under test did, so a suite passed or failed by machine.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, expect } from "vitest";

let dir: string | null = null;
let saved: string | undefined;

beforeAll(() => {
  const testPath = (expect.getState().testPath ?? "").split(path.sep).join("/");
  if (!testPath.includes("/tests/unit/")) return;
  saved = process.env.UE_MCP_USER_STATE;
  dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-unit-state-"));
  process.env.UE_MCP_USER_STATE = path.join(dir, "state.json");
});

afterAll(() => {
  if (!dir) return;
  if (saved === undefined) delete process.env.UE_MCP_USER_STATE;
  else process.env.UE_MCP_USER_STATE = saved;
  fs.rmSync(dir, { recursive: true, force: true });
  dir = null;
});
