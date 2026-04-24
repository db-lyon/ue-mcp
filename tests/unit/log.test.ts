import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";

describe("log helper", () => {
  const originalLevel = process.env.UE_MCP_LOG_LEVEL;
  let stderr: ReturnType<typeof vi.spyOn>;

  beforeEach(() => {
    stderr = vi.spyOn(console, "error").mockImplementation(() => {});
  });

  afterEach(() => {
    stderr.mockRestore();
    if (originalLevel === undefined) delete process.env.UE_MCP_LOG_LEVEL;
    else process.env.UE_MCP_LOG_LEVEL = originalLevel;
    vi.resetModules();
  });

  it("emits info lines at default level", async () => {
    delete process.env.UE_MCP_LOG_LEVEL;
    const { info } = await import("../../src/log.js");
    info("test", "hello");
    expect(stderr).toHaveBeenCalledTimes(1);
    expect(stderr.mock.calls[0][0]).toContain("[ue-mcp] info test: hello");
  });

  it("suppresses debug below info", async () => {
    delete process.env.UE_MCP_LOG_LEVEL;
    const { debug } = await import("../../src/log.js");
    debug("test", "hidden");
    expect(stderr).not.toHaveBeenCalled();
  });

  it("honours UE_MCP_LOG_LEVEL=debug", async () => {
    process.env.UE_MCP_LOG_LEVEL = "debug";
    vi.resetModules();
    const { debug } = await import("../../src/log.js");
    debug("test", "shown");
    expect(stderr).toHaveBeenCalledTimes(1);
  });

  it("formats Error instances", async () => {
    process.env.UE_MCP_LOG_LEVEL = "warn";
    vi.resetModules();
    const { warn } = await import("../../src/log.js");
    warn("test", "boom", new Error("root cause"));
    expect(stderr).toHaveBeenCalledTimes(1);
    expect(stderr.mock.calls[0][0]).toContain("root cause");
  });
});
