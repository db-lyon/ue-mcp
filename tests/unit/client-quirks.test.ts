import { describe, expect, it } from "vitest";
import { progressRenderingNote } from "../../src/client-quirks.js";

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
