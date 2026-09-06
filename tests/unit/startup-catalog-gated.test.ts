import { describe, it, expect } from "vitest";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const SRC = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..", "src");

// Normalised: the working tree checks out with CRLF on Windows, so a match
// written with a bare newline passes on the machine that wrote the file and
// fails everywhere else, including on the same machine after a checkout.
const index = fs.readFileSync(path.join(SRC, "index.ts"), "utf8").split("\r\n").join("\n");

/**
 * The Epic catalog call is the FIRST thing this server says to an editor. It
 * ran on the raw bridge, before any guard existed, so a modal was met by the
 * one route with no gate on it: the refusal carries no toolsets, the surface
 * fell back to a cache, and nothing was latched for the calls that followed.
 */
describe("the startup catalog call is gated like everything else", () => {
  it("sends epic_list_toolsets through the guarded bridge", () => {
    const call = /(\w+)\.call\("epic_list_toolsets"/.exec(index);
    expect(call, "epic_list_toolsets call not found").toBeTruthy();
    const receiver = call![1];
    expect(
      index.includes(`const ${receiver} = session.guarded;`),
      `the startup catalog call went out on '${receiver}', which is not the guarded bridge`,
    ).toBe(true);
  });

  it("creates the guard before the surface is built, not after", () => {
    const guardThenLoad = /dialogGuardFor\(session\);\s*\n\s*const load = await buildSessionLoad/;
    expect(
      guardThenLoad.test(index),
      "no guard is created before buildSessionLoad, so the first call has none behind it",
    ).toBe(true);
  });

  it("keeps dialogGuardFor hoisted, so it can be called before its definition", () => {
    // A const arrow here is a temporal dead zone at the point the surface
    // build needs it, which is the whole reason the guard was created late.
    expect(index).toMatch(/function dialogGuardFor\(/);
    expect(index).not.toMatch(/const dialogGuardFor =/);
  });
});
