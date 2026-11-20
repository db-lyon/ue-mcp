/**
 * PIE request ids: a caller-chosen injection_id or a request_id read back from a remote player is bounded before
 * the call leaves this process, and the bound matches the one the editor enforces.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it, vi } from "vitest";
import { runBridge } from "../../src/flow/run-action.js";
import type { FlowContext } from "../../src/flow/context.js";
import type { BridgeActionSpec, ToolContext } from "../../src/surface/tool-types.js";
import { pieTool } from "../../src/tools/pie.js";
import {
  MAX_REQUEST_ID_LENGTH,
  interceptRequestIds,
  requestIdProblem,
} from "../../src/pie/remote-ids.js";

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const ctx = {} as ToolContext;

describe("requestIdProblem", () => {
  it("accepts a minted id, an id at the bound, and an absent id", () => {
    expect(requestIdProblem("injection_id", "hold-3-1a2b3c4d")).toBeNull();
    expect(requestIdProblem("injection_id", "h".repeat(MAX_REQUEST_ID_LENGTH))).toBeNull();
    expect(requestIdProblem("injection_id", undefined)).toBeNull();
  });

  it("refuses an id past the bound and says how to proceed", () => {
    const problem = requestIdProblem("request_id", "r".repeat(MAX_REQUEST_ID_LENGTH + 1));
    expect(problem).toMatch(new RegExp(`at most ${MAX_REQUEST_ID_LENGTH}`));
    expect(problem).toMatch(/leave injection_id out/);
  });

  it("refuses an id carrying a control character", () => {
    expect(requestIdProblem("injection_id", "hold\nforged log line")).toMatch(/control character/);
  });
});

describe("interceptRequestIds", () => {
  it("lets a call with bounded ids through to the bridge", async () => {
    await expect(interceptRequestIds(ctx, { injection_id: "tape-1", client: 1 })).resolves.toBeUndefined();
  });

  it("throws for either id spelling", async () => {
    const long = "x".repeat(MAX_REQUEST_ID_LENGTH + 1);
    await expect(interceptRequestIds(ctx, { injection_id: long })).rejects.toThrow(/injection_id is/);
    await expect(interceptRequestIds(ctx, { request_id: long })).rejects.toThrow(/request_id is/);
  });

  it("refuses before the bridge is called", async () => {
    const call = vi.fn(async () => ({ success: true }));
    const flowCtx = { bridge: { call, capabilities: null } } as unknown as FlowContext;
    await expect(runBridge(flowCtx, "pie.remote_result", "pie_remote_result", undefined, undefined,
      { request_id: "y".repeat(MAX_REQUEST_ID_LENGTH + 1) }, undefined, { intercept: interceptRequestIds }))
      .rejects.toThrow(/at most/);
    expect(call).not.toHaveBeenCalled();
  });
});

describe("pie actions that take a request id", () => {
  it("check it before sending", () => {
    for (const name of ["inject_input_start", "inject_input_update", "inject_input_stop", "inject_input_tape", "remote_result"]) {
      expect((pieTool.actions[name] as BridgeActionSpec).intercept, name).toBe(interceptRequestIds);
    }
  });

  it("use the same bound the editor enforces", () => {
    const header = fs.readFileSync(
      path.join(REPO, "plugin/ue_mcp_bridge/Source/UE_MCP_BridgeRuntime/Public/MCPPIERemoteControl.h"),
      "utf8",
    );
    const bound = /static constexpr int32 MaxIdLength = (\d+);/.exec(header);
    expect(bound?.[1]).toBe(String(MAX_REQUEST_ID_LENGTH));
  });
});
