import { describe, it, expect } from "vitest";
import { distTag, isPrerelease } from "../../scripts/release-version.mjs";
import { distTagForVersion, isPrereleaseVersion } from "../../src/version-check.js";

/**
 * The channel rule exists twice on purpose: the publish job runs before tsc has
 * produced dist/, and the package ships only dist/ and plugin/, so neither copy
 * can import the other. This pins them together, and pins the one deliberate
 * difference (the runtime copy degrades instead of throwing).
 */
const CASES: Array<{ version: string; tag: string; pre: boolean }> = [
  { version: "1.1.44", tag: "latest", pre: false },
  { version: "1.2.0-beta", tag: "beta", pre: true },
  { version: "1.2.0-beta.2", tag: "beta", pre: true },
  { version: "1.3.0-rc.1", tag: "rc", pre: true },
  { version: "0.1.0", tag: "latest", pre: false },
  { version: "2.0.0-alpha.7", tag: "alpha", pre: true },
  { version: "1.2.0-1", tag: "next", pre: true },
  { version: "1.2.0-latest", tag: "next", pre: true },
];

describe("release channel parity between the pipeline and the shipped CLI", () => {
  for (const { version, tag, pre } of CASES) {
    it(`${version} -> tag ${tag}, prerelease ${pre}`, () => {
      expect(distTag(version)).toBe(tag);
      expect(distTagForVersion(version)).toBe(tag);
      expect(isPrerelease(version)).toBe(pre);
      expect(isPrereleaseVersion(version)).toBe(pre);
    });
  }

  it("degrades to the stable channel at runtime instead of throwing", () => {
    // The pipeline copy fails the job on a malformed version. The runtime copy
    // sits in a CLI path where a crash is worse than a conservative answer.
    expect(() => distTag("garbage")).toThrow();
    expect(distTagForVersion("garbage")).toBe("latest");
    expect(isPrereleaseVersion("garbage")).toBe(false);
  });
});
