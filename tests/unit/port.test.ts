import { describe, expect, it } from "vitest";
import { deriveProjectPort, normalizeProjectRoot, DEFAULT_BRIDGE_PORT } from "../../src/port.js";

describe("deriveProjectPort", () => {
  it("stays inside the ephemeral range [49152, 65535]", () => {
    for (const p of [
      "C:/Users/dev/GameA",
      "C:/Users/dev/GameB",
      "/home/dev/projects/thing",
      "D:/very/deep/nested/worktree/two",
    ]) {
      const port = deriveProjectPort(p);
      expect(port).toBeGreaterThanOrEqual(49152);
      expect(port).toBeLessThanOrEqual(65535);
    }
  });

  it("is deterministic for the same path", () => {
    expect(deriveProjectPort("C:/Users/dev/GameA")).toBe(deriveProjectPort("C:/Users/dev/GameA"));
  });

  it("gives different worktrees different ports", () => {
    // The whole point: two checkouts of the same project on adjacent paths
    // must not collide on one port.
    expect(deriveProjectPort("C:/work/game-main")).not.toBe(deriveProjectPort("C:/work/game-feature"));
  });

  it("is insensitive to trailing slash, backslashes, and case", () => {
    const a = deriveProjectPort("C:/Users/Dev/GameA");
    expect(deriveProjectPort("C:/Users/Dev/GameA/")).toBe(a);
    expect(deriveProjectPort("C:\\Users\\Dev\\GameA")).toBe(a);
    expect(deriveProjectPort("c:/users/dev/gamea")).toBe(a);
  });

  it("normalizes to the canonical form the C++ side hashes", () => {
    expect(normalizeProjectRoot("C:\\Users\\Dev\\GameA\\")).toBe("c:/users/dev/gamea");
  });

  it("pins a known value so drift from the C++ implementation is caught", () => {
    // If this value changes, src/port.ts and BridgeServer.cpp::DeriveProjectPort
    // have diverged (or the algorithm changed) — update both, not just one.
    // sha1("c:/users/dev/gamea") first 4 bytes -> big-endian uint32 -> % 16384 + 49152.
    expect(deriveProjectPort("C:/Users/Dev/GameA")).toBe(referencePort("c:/users/dev/gamea"));
  });
});

// Independent reference implementation (does not call the module under test)
// so the pinned-value assertion catches an accidental edit to port.ts too.
function referencePort(normalized: string): number {
  // eslint-disable-next-line @typescript-eslint/no-var-requires
  const { createHash } = require("node:crypto");
  const h = createHash("sha1").update(normalized, "utf8").digest();
  const v = ((h[0] << 24) | (h[1] << 16) | (h[2] << 8) | h[3]) >>> 0;
  return 49152 + (v % 16384);
}

describe("DEFAULT_BRIDGE_PORT", () => {
  it("is the legacy fixed port", () => {
    expect(DEFAULT_BRIDGE_PORT).toBe(9877);
  });
});
