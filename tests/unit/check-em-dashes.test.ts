import { describe, it, expect } from "vitest";
import { execFileSync } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import {
  ALLOW_MARKER,
  EM_DASH,
  EXCLUDED_PATHS,
  isExcluded,
  scanFiles,
  scanText,
  trackedFiles,
} from "../../scripts/check-em-dashes.mjs";

const REPO_ROOT = path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const SCRIPT = path.join(REPO_ROOT, "scripts", "check-em-dashes.mjs");

/**
 * Every em dash in this file is built from its code point. Writing the literal
 * would make the test file its own first finding, and the marker that would
 * silence it is the very thing under test.
 */
const EM = String.fromCharCode(0x2014);

describe("scanText", () => {
  it("exports the character it is looking for", () => {
    expect(EM_DASH).toBe(EM);
  });

  it("reports line and column for an unmarked em dash", () => {
    const findings = scanText(`first line\nsecond ${EM} line\n`);
    expect(findings).toEqual([{ line: 2, column: 8, text: `second ${EM} line` }]);
  });

  it("reports every occurrence on one line", () => {
    expect(scanText(`a ${EM} b ${EM} c`)).toHaveLength(2);
  });

  it("finds nothing in text without the character", () => {
    expect(scanText("plain - hyphenated - prose\n")).toEqual([]);
  });

  it("exempts a line carrying the marker", () => {
    expect(scanText(`code ${EM} here // ${ALLOW_MARKER}: it is the data`)).toEqual([]);
  });

  it("exempts a line whose predecessor carries the marker", () => {
    expect(scanText(`// ${ALLOW_MARKER}: it is the data\ncode ${EM} here`)).toEqual([]);
  });

  it("does not let a marker reach two lines down", () => {
    const findings = scanText(`// ${ALLOW_MARKER}\nplain line\ncode ${EM} here`);
    expect(findings).toHaveLength(1);
    expect(findings[0].line).toBe(3);
  });
});

describe("EXCLUDED_PATHS", () => {
  it("states a reason for every entry", () => {
    for (const entry of EXCLUDED_PATHS) {
      expect(entry.reason.length).toBeGreaterThan(40);
    }
  });

  it("excludes the harvested catalog snapshot by exact path only", () => {
    expect(isExcluded("assets/epic-catalog.snapshot.json")).toBe(true);
    expect(isExcluded("assets/epic-catalog.snapshot.json.bak")).toBe(false);
    expect(isExcluded("assets/other.json")).toBe(false);
  });

  it("excludes the superseded bridge copy but never the real plugin source", () => {
    expect(isExcluded("tests/ue_mcp/Content/Python/ue_mcp_bridge/Source/x.cpp")).toBe(true);
    expect(isExcluded("plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/HandlerUtils.h")).toBe(false);
    expect(isExcluded("tests/ue_mcp/Content/PythonOther/x.py")).toBe(false);
    expect(isExcluded("tests/smoke/editor.test.ts")).toBe(false);
  });

  it("matches paths written with backslashes", () => {
    expect(isExcluded("assets\\epic-catalog.snapshot.json")).toBe(true);
  });
});

describe("the tracked tree", () => {
  it("carries no em dash outside the marked lines", () => {
    expect(scanFiles(trackedFiles(REPO_ROOT), REPO_ROOT)).toEqual([]);
  });

  it("scans the files this repo authors", () => {
    const files = trackedFiles(REPO_ROOT);
    expect(files).toContain("CLAUDE.md");
    expect(files).toContain("src/epic-enrich.ts");
    expect(files.length).toBeGreaterThan(400);
  });
});

describe("the command line", () => {
  it("exits 0 and says so on a clean tree", () => {
    const out = execFileSync("node", [SCRIPT], { cwd: REPO_ROOT }).toString();
    expect(out).toContain("No em dashes in");
  });

  it("exits 1 and names the file and line on a finding", () => {
    // The fixture is written outside the repo. A tracked fixture holding a
    // real em dash would either fail the tree scan or need the very marker
    // that would stop this assertion from seeing anything.
    const dir = mkdtempSync(path.join(tmpdir(), "em-dash-"));
    try {
      writeFileSync(path.join(dir, "offender.md"), `one\ntwo\nthree ${EM} four\n`, "utf8");
      let status = 0;
      let stderr = "";
      try {
        execFileSync("node", [SCRIPT, "--files", "offender.md"], {
          cwd: dir,
          stdio: ["ignore", "pipe", "pipe"],
        });
      } catch (e: any) {
        status = e.status;
        stderr = e.stderr.toString();
      }
      expect(status).toBe(1);
      expect(stderr).toContain("offender.md:3:7");
      expect(stderr).toContain("1 em dash(es) found");
    } finally {
      rmSync(dir, { recursive: true, force: true });
    }
  });

  it("rejects an unknown argument", () => {
    let status = 0;
    try {
      execFileSync("node", [SCRIPT, "--everything"], {
        cwd: REPO_ROOT,
        stdio: ["ignore", "pipe", "pipe"],
      });
    } catch (e: any) {
      status = e.status;
    }
    expect(status).toBe(2);
  });
});
