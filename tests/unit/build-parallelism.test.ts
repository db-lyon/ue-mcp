import { describe, it, expect, afterEach } from "vitest";
import { safeParallelActions, ranOutOfMemory, describeMemoryFailure } from "../../src/editor-control.js";

const GB = 1024 ** 3;

/**
 * UnrealBuildTool runs one compile per physical core, and each one maps the
 * Unreal precompiled header. On a machine with less memory than cores x PCH
 * the compiler does not queue, it fails outright, several minutes in, with an
 * error naming the paging file. That reads as a broken machine rather than a
 * default that does not fit it, so the number is computed here instead of
 * being a setting somebody has to find.
 */
describe("how many compiles this machine can run at once", () => {
  const saved = process.env.UE_MCP_MAX_PARALLEL_ACTIONS;
  afterEach(() => {
    if (saved === undefined) delete process.env.UE_MCP_MAX_PARALLEL_ACTIONS;
    else process.env.UE_MCP_MAX_PARALLEL_ACTIONS = saved;
  });

  it("caps a 32 GB machine below its core count", () => {
    // The reported case: 12 physical cores, 32 GB, twelve parallel PCH
    // compiles needing about 59 GB against a 53.5 GB ceiling.
    expect(safeParallelActions(32 * GB, 12)).toBe(5);
  });

  it("leaves a machine with headroom alone", () => {
    // 128 GB could fit 24, so the core count is what limits it and no cap is
    // applied at the call site.
    expect(safeParallelActions(128 * GB, 12)).toBe(12);
  });

  it("never returns zero, however little memory there is", () => {
    expect(safeParallelActions(4 * GB, 8)).toBe(1);
    expect(safeParallelActions(1 * GB, 8)).toBe(1);
  });

  it("never exceeds the core count", () => {
    expect(safeParallelActions(256 * GB, 4)).toBe(4);
  });

  it("takes an explicit override for a machine this estimate gets wrong", () => {
    process.env.UE_MCP_MAX_PARALLEL_ACTIONS = "2";
    expect(safeParallelActions(128 * GB, 12)).toBe(2);
  });

  it("ignores an override that names no usable number", () => {
    for (const bad of ["0", "-3", "banana", ""]) {
      process.env.UE_MCP_MAX_PARALLEL_ACTIONS = bad;
      expect(safeParallelActions(32 * GB, 12), bad).toBe(5);
    }
  });
});

describe("telling a memory failure apart from a code failure", () => {
  it("recognises what the compiler actually prints", () => {
    expect(ranOutOfMemory("c1xx: error C3859: Failed to create virtual memory for PCH")).toBe(true);
    expect(ranOutOfMemory("c1xx: fatal error C1076: compiler limit: internal heap limit reached")).toBe(true);
    expect(ranOutOfMemory("the system returned code 1455: The paging file is too small")).toBe(true);
  });

  it("does not claim a real compile error was a memory problem", () => {
    expect(ranOutOfMemory("error C2065: 'Foo': undeclared identifier")).toBe(false);
    expect(ranOutOfMemory("LNK2019: unresolved external symbol")).toBe(false);
  });

  it("says what happened in the terms it happened in", () => {
    const msg = describeMemoryFailure(5, 32 * GB);
    expect(msg).toContain("ran out of memory");
    expect(msg).toContain("32 GB");
    expect(msg).toContain("5 parallel processes");
    expect(msg).not.toContain("C3859");
  });

  it("does not say processes when there was one", () => {
    expect(describeMemoryFailure(1, 8 * GB)).toContain("1 parallel process");
  });
});
