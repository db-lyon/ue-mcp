import { describe, it, expect } from "vitest";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { staleDeployedEntries } from "../../src/deployer.js";

const ARTIFACTS = new Set(["Binaries", "Intermediate"]);
const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");

/**
 * The deployed plugin tree is a MIRROR of plugin/, not a merge.
 *
 * This is what makes "never hand-copy a file into the test project" a property
 * of the tool instead of a rule somebody has to remember: anything in the
 * deployed tree that the source does not have is removed on the next deploy,
 * so a hand-placed file never survives to be compiled. A copy-only sync left a
 * renamed file sitting there and UBT compiled both, which is a link error for
 * symbols defined twice.
 */
describe("the mirror rule decides what the deployed tree may keep", () => {
  it("drops a file the source tree does not have", () => {
    const stale = staleDeployedEntries(
      ["Real.cpp", "HandCopied.cpp"],
      new Set(["real.cpp"]),
      ARTIFACTS,
      true,
    );
    expect(stale, "a hand-placed file survived, so it would be compiled").toEqual(["HandCopied.cpp"]);
  });

  it("drops the old name after a rename, which is the link error it prevents", () => {
    // Splitting EngineStatus.cpp out of BridgeServer.cpp left both compiling.
    const stale = staleDeployedEntries(
      ["EngineStatus.cpp", "BridgeServer.cpp"],
      new Set(["enginestatus.cpp"]),
      ARTIFACTS,
      true,
    );
    expect(stale).toEqual(["BridgeServer.cpp"]);
  });

  it("keeps build artifact directories, which the source never has", () => {
    const stale = staleDeployedEntries(
      ["Binaries", "Intermediate", "Source"],
      new Set(["source"]),
      ARTIFACTS,
      true,
    );
    expect(stale).toEqual([]);
  });

  it("treats one file as one file on a case-insensitive filesystem", () => {
    // Windows and macOS. Treating these as two deletes the file just copied in.
    const stale = staleDeployedEntries(["handlers.cpp"], new Set(["handlers.cpp"]), ARTIFACTS, true);
    expect(stale).toEqual([]);
  });

  it("treats them as two files where the filesystem does", () => {
    // Linux, where Handlers.cpp and handlers.cpp really are different files.
    const stale = staleDeployedEntries(["Handlers.cpp"], new Set(["handlers.cpp"]), ARTIFACTS, false);
    expect(stale).toEqual(["Handlers.cpp"]);
  });

  it("keeps everything when the source names everything", () => {
    const names = ["A.cpp", "B.h", "Source"];
    const stale = staleDeployedEntries(names, new Set(names.map((n) => n.toLowerCase())), ARTIFACTS, true);
    expect(stale).toEqual([]);
  });
});

describe("the deployed tree is not the place to edit", () => {
  it("says so where a contributor would look", () => {
    // The mirror only enforces this on the NEXT deploy, so somebody editing
    // the deployed tree silently loses that work until then. The prose is the
    // half the check cannot do.
    const claude = fs.readFileSync(path.join(REPO, "CLAUDE.md"), "utf8");
    expect(claude).toMatch(/never hand-copy|The deployer syncs|deployer does it/i);
  });
});
