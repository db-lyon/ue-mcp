import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { elicitationNeedsRelay, progressRenderingNote } from "../../src/client-quirks.js";

describe("progressRenderingNote", () => {
  it("explains the collapse on the affected Claude Code versions", () => {
    const note = progressRenderingNote({ name: "claude-code", version: "2.1.116" });
    expect(note).toContain("51713");
    expect(note).toContain("2.1.116");
  });

  it("stays quiet on the last version that rendered progress", () => {
    expect(progressRenderingNote({ name: "claude-code", version: "2.1.101" })).toBeNull();
  });

  it("stays quiet for other clients, which render progress normally", () => {
    expect(progressRenderingNote({ name: "cursor", version: "9.9.9" })).toBeNull();
    expect(progressRenderingNote({ name: "mcp-inspector" })).toBeNull();
    expect(progressRenderingNote(undefined)).toBeNull();
  });

  it("compares versions numerically, not lexically", () => {
    // "2.1.99" < "2.1.116" numerically, the other way round as strings.
    expect(progressRenderingNote({ name: "claude-code", version: "2.1.99" })).toBeNull();
    expect(progressRenderingNote({ name: "claude-code", version: "2.1.152" })).not.toBeNull();
  });

  it("stays quiet once the client shipped the fix", () => {
    // 2.1.153 fixed it. Telling a user on a current build that their client
    // cannot draw progress is a wrong explanation, which is worse than none.
    expect(progressRenderingNote({ name: "claude-code", version: "2.1.153" })).toBeNull();
    expect(progressRenderingNote({ name: "claude-code", version: "2.1.221" })).toBeNull();
    expect(progressRenderingNote({ name: "claude-code", version: "2.2.0" })).toBeNull();
    expect(progressRenderingNote({ name: "claude-code", version: "3.0.0" })).toBeNull();
  });
});

describe("elicitationNeedsRelay", () => {
  const clearOverride = () => {
    delete process.env.UE_MCP_DIALOG_RELAY;
  };
  beforeEach(clearOverride);
  afterEach(clearOverride);

  it("relays for a client nobody has checked, which is every client by default", () => {
    // The two failure modes are not equal. Relaying needlessly costs one call;
    // not relaying against a client that collapses the message puts a question
    // in front of someone with the question missing. Absence of evidence about
    // a client is not evidence it renders.
    expect(elicitationNeedsRelay({ name: "claude-code", version: "2.1.160" })).toBe(true);
    expect(elicitationNeedsRelay({ name: "some-new-agent" })).toBe(true);
    expect(elicitationNeedsRelay(undefined)).toBe(true);
  });

  it("skips the round trip for a client known to render the whole message", () => {
    expect(elicitationNeedsRelay({ name: "pi-coding-agent", version: "0.1.0" })).toBe(false);
    // Matched as a substring of the name the client sends, which is rarely
    // exactly the name it goes by.
    expect(elicitationNeedsRelay({ name: "Pi-Coding-Agent/2" })).toBe(false);
    // pi-mcp-adapter names its client after the SERVER it is bridging, not
    // after pi, so the name is pi-mcp-<server> and never mentions pi-coding-agent.
    // It hands the message to pi's TUI whole.
    expect(elicitationNeedsRelay({ name: "pi-mcp-ue-mcp", version: "2.32.1" })).toBe(false);
  });

  it("lets the environment settle it either way, without waiting for a release", () => {
    process.env.UE_MCP_DIALOG_RELAY = "off";
    expect(elicitationNeedsRelay({ name: "claude-code" })).toBe(false);
    process.env.UE_MCP_DIALOG_RELAY = "on";
    expect(elicitationNeedsRelay({ name: "pi-coding-agent" })).toBe(true);
    // Anything else is not an instruction, so the client decides.
    process.env.UE_MCP_DIALOG_RELAY = "maybe";
    expect(elicitationNeedsRelay({ name: "claude-code" })).toBe(true);
    expect(elicitationNeedsRelay({ name: "pi-coding-agent" })).toBe(false);
  });
});
