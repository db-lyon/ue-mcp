import { describe, it, expect } from "vitest";
import { execFileSync } from "node:child_process";
import path from "node:path";
import { fileURLToPath } from "node:url";
// @ts-expect-error - plain ESM script, no types
import { lintText, RULES, EXCLUDED_PATHS, isExcluded, ALLOW_MARKER } from "../../scripts/lint-prose.mjs";

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");

type Finding = { rule: string; line: number; found: string };
const rules = (text: string, rel: string): string[] =>
  (lintText(text, { rel }) as Finding[]).map((f) => f.rule);

// The character the em-dash rule bans, built rather than typed, so this file
// does not have to exempt itself from the rule it is testing.
const EM = String.fromCharCode(0x2014);

/**
 * Every rule here replaced a rule somebody had to remember. A check nobody has
 * seen fail is indistinguishable from no check at all, so each one is proved
 * against a deliberate violation and against text that must stay legal.
 */
describe("each rule fires on a real violation", () => {
  const cases: Array<[string, string, string]> = [
    ["em-dash", `A sentence with an ${EM} in it.`, "docs/x.md"],
    ["tier", "The live tier runs nightly.", "docs/x.md"],
    ["competitor", "Closing the gap against runreal here.", "docs/x.md"],
    ["competitor", "Faster than Monolith at this.", "docs/x.md"],
    ["session-link", "See https://claude.ai/code/session_01AbCdEfGhIjKlMnOp for details.", "docs/x.md"],
    ["session-link", "Claude-Session: https://example.invalid/x", "docs/x.md"],
    ["dead-ref", "Read the plan in plans/TODO.md for more.", "docs/x.md"],
    ["dead-ref", "That file is gitignored anyway.", "docs/x.md"],
    ["no-kill", "execSync('taskkill /IM UnrealEditor.exe');", "scripts/x.js"],
    ["no-hot-reload", 'await bridge.call("hot_reload", {});', "scripts/x.js"],
  ];

  for (const [rule, text, rel] of cases) {
    it(`${rule}: ${text.slice(0, 46)}`, () => {
      expect(rules(text, rel), `not caught: ${text}`).toContain(rule);
    });
  }
});

describe("the two ambiguous names are matched in any case, deliberately", () => {
  // These are project names that are also ordinary English. Matching them in
  // any case means GPU prose about flop counts needs a marker. That is the
  // trade taken on purpose: one marker costs a line, and a missed one puts a
  // competitor's name in a release.
  it("flags them however they are capitalised", () => {
    for (const text of ["MONOLITH is faster", "monolith is faster", "Flop counts matter"]) {
      expect(rules(text, "docs/x.md"), text).toContain("competitor");
    }
  });

  it("leaves the adjective alone, since it is not the name", () => {
    expect(rules("A monolithic handler is hard to read.", "docs/x.md")).toEqual([]);
  });
});

describe("each rule leaves legal text alone", () => {
  const legal: Array<[string, string]> = [
    ["npm run golden:record -- --connected", "docs/x.md"],
    ["A monolithic handler is hard to read.", "docs/x.md"],
    ["A spaced hyphen - like this - is fine.", "docs/x.md"],
    ["Sessions are per editor.", "docs/x.md"],
    ["See docs/engineering/releasing.md.", "docs/x.md"],
  ];
  for (const [text, rel] of legal) {
    it(text.slice(0, 46), () => {
      expect(rules(text, rel), `false positive on: ${text}`).toEqual([]);
    });
  }
});

/**
 * Every case below was demonstrated against an earlier version of these rules:
 * the first group slipped through, the second was wrongly rejected. They are
 * kept as fixtures so a future loosening or tightening shows up here.
 */
describe("shapes that used to slip through", () => {
  const cases: Array<[string, string, string, string]> = [
    ["competitor in an identifier", "const runrealClient = new Client();", "docs/x.md", "competitor"],
    ["competitor mid camelCase", "function syncWithChongdashuBridge() {}", "docs/x.md", "competitor"],
    ["competitor in caps", "MONOLITH does this differently.", "docs/x.md", "competitor"],
    ["competitor in lower case", "monolith does this differently.", "docs/x.md", "competitor"],
    ["competitor with underscores", "port of unreal_analyzer parser", "docs/x.md", "competitor"],
    ["competitor spaced out", "faster than the Unreal Analyzer plugin", "docs/x.md", "competitor"],
    ["dead ref capitalised", "see Plans/TODO.md", "docs/x.md", "dead-ref"],
    ["a kill by another name", "execSync('powershell Stop-Process -Name UnrealEditor')", "scripts/x.js", "no-kill"],
    ["a kill in src rather than scripts", 'execSync("taskkill /IM UnrealEditor.exe")', "src/x.ts", "no-kill"],
  ];
  for (const [name, text, rel, want] of cases) {
    it(name, () => {
      expect(rules(text, rel), `slipped through: ${text}`).toContain(want);
    });
  }
});

describe("shapes that used to be rejected wrongly", () => {
  const legal: Array<[string, string]> = [
    ['path.join(binDir, "UnrealEditor-UE_MCP_Bridge.dll")', "src/x.ts"],
    ["const binDirUnrealEditor = x;", "src/x.ts"],
    ["process.kill(pid, 0);", "src/x.ts"],
    ['hot_reload: bp("read", "Hot reload C++", "hot_reload"),', "src/tools/editor.ts"],
    ["Each editor session gets its own port. See claude.ai/ for the client.", "docs/x.md"],
    ["Works with chatgpt.com/ and other MCP clients across a session.", "docs/x.md"],
    ["Assets land under /Game/scratch/ during a run.", "docs/x.md"],
    ["See https://example.com/comms/index.html", "docs/x.md"],
    ["See the roadmaps/plans/overview page", "docs/x.md"],
    ["Timing regions need UE 5.7 or newer with trace compiled in.", "src/x.ts"],
    ["// gated at 5.7, which is the oldest engine whose headers were tested", "src/x.ts"],
    ["Unreal Engine 5.4 to 5.8.", "docs/x.md"],
  ];
  for (const [text, rel] of legal) {
    it(text.slice(0, 52), () => {
      expect(rules(text, rel), `wrongly rejected: ${text}`).toEqual([]);
    });
  }
});

describe("scope keeps a rule off the files it would misfire on", () => {
  it("does not apply the script rules outside scripts/", () => {
    expect(rules('await bridge.call("hot_reload", {});', "src/tools/editor.ts")).toEqual([]);
  });

  it("does not apply the dead-ref rule outside docs/", () => {
    // CLAUDE.md legitimately points contributors at plans/.
    expect(rules("Plans live in plans/<name>.md.", "CLAUDE.md")).toEqual([]);
  });

  it("applies the prose rules to a commit message, which has no path", () => {
    expect(rules(`fix: a change ${EM} with a dash`, "")).toContain("em-dash");
  });
});

describe("exemptions are explicit and greppable", () => {
  it("exempts the line carrying the marker", () => {
    expect(rules(`the live tier // ${ALLOW_MARKER}: tier`, "docs/x.md")).toEqual([]);
  });

  it("exempts the line directly below a marker", () => {
    expect(rules(`// ${ALLOW_MARKER}: tier\nthe live tier`, "docs/x.md")).toEqual([]);
  });

  it("exempts one rule only, not the line", () => {
    const text = `the live tier and an ${EM} // ${ALLOW_MARKER}: tier`;
    expect(rules(text, "docs/x.md")).toEqual(["em-dash"]);
  });

  it("still honours the marker the em-dash rule shipped with", () => {
    expect(rules(`an ${EM} here // em-dash-allowed: shows the character`, "docs/x.md")).toEqual([]);
  });
});

/**
 * Several rules are driven by a list. Deleting entries from one used to leave
 * every test green, because nothing asserted what the list has to contain:
 * the rule still fired, just on less. These pin the contents.
 */
describe("the lists a rule is built from are complete", () => {
  const catches = (text: string, rel = "docs/x.md"): boolean => rules(text, rel).length > 0;

  it("names every comparison project", () => {
    for (const name of [
      "chongdashu", "GenOrca", "runreal", "mcp-unreal",
      "ChiR24", "unreal-analyzer", "Soverance", "Claireon", "Monolith", "Flop",
    ]) {
      expect(catches(`a mention of ${name} here`), name).toBe(true);
    }
  });

  it("names every path a published doc may not point at", () => {
    for (const dir of ["plans/", "comms/", "scratch/"]) {
      expect(catches(`see ${dir}notes.md`), dir).toBe(true);
    }
    expect(catches("that file is gitignored")).toBe(true);
  });

  it("names every way a script could end somebody's editor", () => {
    for (const kill of [
      'execSync("taskkill /IM UnrealEditor.exe")',
      "execSync('Stop-Process -Name UnrealEditor')",
      "execSync('pkill UnrealEditor')",
      "execSync('kill -9 1234')",
      "process.kill(pid);",
    ]) {
      expect(catches(kill, "scripts/x.js"), kill).toBe(true);
    }
  });

  it("names both ways a build gets validated by a reload", () => {
    for (const m of ["live_coding_compile", "hot_reload"]) {
      expect(catches(`await call("${m}", {})`, "scripts/x.js"), m).toBe(true);
    }
  });

  it("names every hedge that has been attached to the range", () => {
    for (const hedge of [
      "5.4 to 5.8, compiled and verified on 5.7",
      "5.4 to 5.8, compiled and tested here",
      "5.4 to 5.8, built and tested on the newest",
      "5.4 to 5.8, tested on the last two",
      "5.4 to 5.8, verified on two of them",
      "5.4 to 5.8, version-gated but not built",
      "5.4 to 5.8, though 5.4 is untested",
    ]) {
      expect(catches(hedge), hedge).toBe(true);
    }
  });

  it("names every vendor whose session links are refused", () => {
    for (const v of ["claude", "chatgpt", "openai", "copilot", "cursor", "gemini", "codex"]) {
      expect(catches(`see https://${v}.ai/code/session_01AbCdEfGhIjKlMnOp`), v).toBe(true);
    }
  });
});

describe("exclusions name a path and a reason", () => {
  it("excludes what it says it excludes", () => {
    expect(isExcluded("assets/epic-catalog.snapshot.json")).toBe(true);
    expect(isExcluded("tests/ue_mcp/Content/Python/Foo.cpp")).toBe(true);
    expect(isExcluded("tests/golden/editor-down.json")).toBe(true);
    expect(isExcluded("src/index.ts")).toBe(false);
  });

  it("gives a reason for every exclusion, so the table reads as an argument", () => {
    for (const e of EXCLUDED_PATHS as Array<{ path: string; reason: string }>) {
      expect(e.reason.length, e.path).toBeGreaterThan(60);
    }
  });
});

describe("the gate holds over the whole tree", () => {
  it("every rule has an id, a scope and a stated reason", () => {
    for (const r of RULES as Array<{ id: string; scope: string; why: string }>) {
      expect(r.id).toMatch(/^[a-z-]+$/);
      expect(["tracked", "docs", "scripts", "code", "published"]).toContain(r.scope);
      expect(r.why.length, r.id).toBeGreaterThan(30);
    }
  });

  it("passes on the committed tree", () => {
    // The gate is only worth having if the tree it guards is clean, and a
    // linter nobody has run against reality is a linter with unknown rules.
    const out = execFileSync("node", ["scripts/lint-prose.mjs"], {
      cwd: REPO,
      encoding: "utf8",
      stdio: ["ignore", "pipe", "pipe"],
    });
    expect(out).toContain("clean");
  });
});
