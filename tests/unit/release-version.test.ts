import { describe, it, expect } from "vitest";
import { execFileSync } from "node:child_process";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import {
  FALLBACK_TAG,
  STABLE_TAG,
  VersionError,
  distTag,
  isPrerelease,
  isPublished,
  outputsFor,
  parseVersion,
} from "../../scripts/release-version.mjs";

const SCRIPT = path.join(
  path.dirname(fileURLToPath(import.meta.url)),
  "..",
  "..",
  "scripts",
  "release-version.mjs",
);

/** The versions the release pipeline has to get right. */
const CASES: Array<{ version: string; tag: string; pre: boolean }> = [
  { version: "1.1.44", tag: "latest", pre: false },
  { version: "1.2.0-beta", tag: "beta", pre: true },
  { version: "1.2.0-beta.2", tag: "beta", pre: true },
  { version: "1.3.0-rc.1", tag: "rc", pre: true },
];

describe("parseVersion", () => {
  it("splits a plain release", () => {
    expect(parseVersion("1.1.44")).toEqual({
      major: 1,
      minor: 1,
      patch: 44,
      prerelease: null,
      build: null,
    });
  });

  it("splits a dotted prerelease", () => {
    expect(parseVersion("1.3.0-rc.1")).toMatchObject({ major: 1, minor: 3, patch: 0, prerelease: "rc.1" });
  });

  it("keeps build metadata out of the prerelease field", () => {
    expect(parseVersion("1.2.0-beta+sha.abc")).toMatchObject({ prerelease: "beta", build: "sha.abc" });
  });

  it("rejects non-semver input rather than guessing a channel", () => {
    for (const bad of ["", "v1.2.0", "1.2", "1.2.0.1", "latest", "1.2.0-"]) {
      expect(() => parseVersion(bad)).toThrowError(VersionError);
    }
  });
});

describe("isPrerelease", () => {
  for (const { version, pre } of CASES) {
    it(`${version} -> ${pre}`, () => {
      expect(isPrerelease(version)).toBe(pre);
    });
  }
});

describe("distTag", () => {
  for (const { version, tag } of CASES) {
    it(`${version} -> ${tag}`, () => {
      expect(distTag(version)).toBe(tag);
    });
  }

  it("keeps latest for every plain release", () => {
    expect(distTag("0.1.0")).toBe(STABLE_TAG);
    expect(distTag("10.20.30")).toBe(STABLE_TAG);
  });

  it("uses the first identifier, not a substring match on one hardcoded word", () => {
    expect(distTag("2.0.0-alpha.7")).toBe("alpha");
    expect(distTag("2.0.0-canary")).toBe("canary");
    expect(distTag("2.0.0-nightly.20260805")).toBe("nightly");
  });

  it("falls back when the identifier cannot be an npm tag", () => {
    // npm rejects a dist-tag that parses as a semver range.
    expect(distTag("1.2.0-1")).toBe(FALLBACK_TAG);
    expect(distTag("1.2.0-0.3")).toBe(FALLBACK_TAG);
  });

  it("never lets a prerelease claim the stable tag", () => {
    expect(distTag("1.2.0-latest")).toBe(FALLBACK_TAG);
    expect(distTag("1.2.0-LATEST.1")).toBe(FALLBACK_TAG);
  });
});

describe("isPublished", () => {
  const list = JSON.stringify(["1.1.43", "1.1.44", "1.2.0-beta"]);

  it("finds a version already on the registry", () => {
    expect(isPublished(list, "1.1.44")).toBe(true);
    expect(isPublished(list, "1.2.0-beta")).toBe(true);
  });

  it("reports a genuinely new version as unpublished", () => {
    expect(isPublished(list, "1.1.45")).toBe(false);
    expect(isPublished(list, "1.2.0-beta.2")).toBe(false);
  });

  it("accepts the bare-string shape npm emits for a single version", () => {
    expect(isPublished('"1.0.0"', "1.0.0")).toBe(true);
    expect(isPublished('"1.0.0"', "1.0.1")).toBe(false);
  });

  it("refuses to answer when the registry response is empty or broken", () => {
    expect(() => isPublished("", "1.1.44")).toThrowError(VersionError);
    expect(() => isPublished("   ", "1.1.44")).toThrowError(VersionError);
    expect(() => isPublished("not json", "1.1.44")).toThrowError(VersionError);
  });

  it("does not treat a prerelease of a published stable as published", () => {
    // The exact bug that wedged the old `npm view ue-mcp version` check.
    expect(isPublished(JSON.stringify(["1.1.44"]), "1.2.0-beta")).toBe(false);
  });
});

describe("outputsFor", () => {
  it("emits GITHUB_OUTPUT key=value lines", () => {
    expect(outputsFor("1.2.0-beta.2")).toEqual(["dist_tag=beta", "prerelease=true"]);
    expect(outputsFor("1.1.44")).toEqual(["dist_tag=latest", "prerelease=false"]);
  });
});

describe("CLI (what the workflow actually runs)", () => {
  for (const { version, tag, pre } of CASES) {
    it(`prints both outputs for ${version}`, () => {
      const out = execFileSync(process.execPath, [SCRIPT, "--version", version], {
        encoding: "utf8",
      }).trim();
      expect(out.split(/\r?\n/)).toEqual([`dist_tag=${tag}`, `prerelease=${pre}`]);
    });
  }

  it("answers the published-membership question", () => {
    const out = execFileSync(
      process.execPath,
      [SCRIPT, "--version", "1.2.0-beta", "--published-in", JSON.stringify(["1.1.44"])],
      { encoding: "utf8" },
    ).trim();
    expect(out).toBe("published=false");
  });

  it("exits non-zero on a malformed version instead of publishing under a guess", () => {
    expect(() =>
      execFileSync(process.execPath, [SCRIPT, "--version", "not-a-version"], { stdio: "pipe" }),
    ).toThrow();
  });
});
