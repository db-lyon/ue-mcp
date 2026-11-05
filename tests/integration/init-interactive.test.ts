/**
 * Interactive `ue-mcp init`, answered through a pseudo-terminal (node-pty),
 * and the prompts' refusal when there is no terminal to ask on.
 */
import * as fs from "node:fs";
import { createRequire } from "node:module";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { ENTRY, plain, runCli, Sandbox, snapshotTree, TSX } from "./harness.js";
import { fakeSigning } from "./feedback-fakes.js";

type Pty = typeof import("node-pty");
let pty: Pty | null = null;
let ptyError = "";
try {
  pty = createRequire(import.meta.url)("node-pty") as Pty;
} catch (e) {
  ptyError = e instanceof Error ? e.message : String(e);
}

let sb: Sandbox;
beforeAll(() => {
  sb = new Sandbox("ue-mcp-init-tty-");
});
afterAll(() => sb.cleanup());

/** One question the driver waits for, and the keys that answer it. */
interface Answer {
  prompt: string;
  /** Sent one at a time: a prompt reads each data event as one key. */
  keys: string[];
}

/** Run a command in a pseudo-terminal, answering each prompt as it appears. */
function runInTerminal(
  args: string[],
  cwd: string,
  answers: Answer[],
  env: Record<string, string> = {},
): Promise<{ code: number; output: string }> {
  if (!pty) throw new Error(`node-pty did not load: ${ptyError}`);
  return new Promise((resolve, reject) => {
    const term = pty!.spawn(process.execPath, ["--import", TSX, ENTRY, ...args], {
      name: "xterm-color",
      cols: 200,
      rows: 60,
      cwd,
      env: sb.env({ UE_MCP_LOG_LEVEL: "error", ...env }),
    });
    let output = "";
    let next = 0;
    let searchFrom = 0;
    const timer = setTimeout(() => {
      term.kill();
      reject(new Error(`init did not finish; waiting for "${answers[next]?.prompt}"\n${plain(output)}`));
    }, 90_000);
    term.onData((data) => {
      output += data;
      const text = plain(output);
      while (next < answers.length) {
        const at = text.indexOf(answers[next].prompt, searchFrom);
        if (at === -1) break;
        searchFrom = at + answers[next].prompt.length;
        const keys = answers[next].keys;
        next++;
        // Give the prompt a moment to switch stdin to raw mode, and each key its own read.
        keys.forEach((key, i) => setTimeout(() => term.write(key), 200 * (i + 1)));
      }
    });
    term.onExit(({ exitCode }) => {
      clearTimeout(timer);
      resolve({ code: exitCode, output: plain(output) });
    });
  });
}

const ENTER = "\r";
const DOWN = "\x1b[B";
const SPACE = " ";

describe("interactive init", () => {
  it.runIf(pty !== null || process.env.CI !== undefined)("asks each question on a terminal and writes what was answered", async () => {
    covers("cli.init.interactive");
    const uproject = sb.makeProject("Asked");
    const projectDir = path.dirname(uproject);
    // Claude Code counts as installed once ~/.claude exists, so its prompt appears.
    fs.mkdirSync(path.join(sb.home, ".claude"), { recursive: true });

    const r = await runInTerminal(["init", uproject, "--install", "source"], projectDir, [
      { prompt: "Tool categories", keys: [ENTER] },
      { prompt: "Native Unreal tools", keys: [ENTER] },
      { prompt: "Include native tools in these categories", keys: [ENTER] },
      // micro is first; one down picks lean.
      { prompt: "Context strategy", keys: [DOWN, ENTER] },
      { prompt: "Configure MCP clients", keys: [ENTER] },
      // Feedback is first and off on a fresh install; space turns it on.
      { prompt: "Agent behavior", keys: [SPACE, ENTER] },
    ]);

    expect(r.code, r.output).toBe(0);
    expect(r.output).toContain("Context strategy: lean");
    expect(r.output).toContain("Setup complete!");

    const yml = fs.readFileSync(path.join(projectDir, "ue-mcp.yml"), "utf8");
    expect(yml).toMatch(/strategy:\s*lean/);
    expect(yml).not.toMatch(/-\s*feedback/);
    expect(fs.existsSync(path.join(projectDir, "Plugins", "UE_MCP_Bridge", "UE_MCP_Bridge.uplugin"))).toBe(true);
    const mcp = JSON.parse(fs.readFileSync(path.join(projectDir, ".mcp.json"), "utf8")) as { mcpServers: Record<string, unknown> };
    expect(Object.keys(mcp.mcpServers)).toContain("ue-mcp");
  });
});

describe("feedback review on a terminal", () => {
  it.runIf(pty !== null || process.env.CI !== undefined)("approves, discards and skips entries as answered, one at a time", async () => {
    covers("cli.feedback.review");
    const signing = await fakeSigning();
    try {
      const pending = path.join(sb.home, ".ue-mcp", "pending-feedback");
      fs.mkdirSync(pending, { recursive: true });
      const entry = (id: string, title: string) =>
        fs.writeFileSync(
          path.join(pending, `${id}.json`),
          JSON.stringify({ id, createdAt: new Date().toISOString(), project: "Reviewed", title, body: `Body of ${title}`, labels: ["agent-feedback"], author: "bot", repo: "db-lyon/ue-mcp" }),
        );
      entry("20260101-000001-aaaa", "First report");
      entry("20260101-000002-bbbb", "Second report");
      entry("20260101-000003-cccc", "Third report");

      const prompt = "[a]pprove  [d]iscard  [s]kip  [q]uit >";
      const r = await runInTerminal(["feedback", "review"], sb.root, [
        { prompt, keys: ["a", ENTER] },
        { prompt, keys: ["d", ENTER] },
        { prompt, keys: ["s", ENTER] },
      ], { UE_MCP_FEEDBACK_ENDPOINT: `${signing.origin}/sign` });

      expect(r.code, r.output).toBe(0);
      expect(signing.reports().map((x) => x.title)).toEqual(["First report"]);
      expect(fs.readdirSync(pending).filter((f) => f.endsWith(".json")).sort()).toEqual(["20260101-000003-cccc.json"]);
    } finally {
      await signing.stop();
    }
  });
});

describe("prompts without a terminal", () => {
  it("init refuses at once, names the flags that answer it, and writes nothing", async () => {
    covers("cli.init.interactive");
    const uproject = sb.makeProject("NoTty");
    const projectDir = path.dirname(uproject);
    const before = snapshotTree(projectDir);
    const r = await runCli(sb, ["init", uproject], { cwd: projectDir });
    expect(r.code).toBe(2);
    const out = plain(r.stdout + r.stderr);
    expect(out).toContain("stdin is not one");
    expect(out).toContain("--yes");
    expect(out).toContain("--clients");
    expect(out).not.toContain("TypeError");
    expect(snapshotTree(projectDir)).toEqual(before);
  });

  it("plugin config without --enable or --disable refuses and names them", async () => {
    covers("cli.plugin.config");
    const uproject = sb.makeProject("PluginNoTty", { config: "ue-mcp:\n  version: 1\nplugins:\n  - name: tty-probe\n" });
    const projectDir = path.dirname(uproject);
    const pkg = path.join(projectDir, "node_modules", "tty-probe");
    fs.mkdirSync(pkg, { recursive: true });
    fs.writeFileSync(path.join(pkg, "package.json"), JSON.stringify({ name: "tty-probe", version: "1.0.0" }));
    fs.writeFileSync(
      path.join(pkg, "ue-mcp.plugin.yml"),
      "actionPrefix: ttyp\nflows:\n  one:\n    description: A flow in a group\n    group: extras\n    steps:\n      \"1\":\n        task: project.get_status\n",
    );
    const before = snapshotTree(projectDir);
    const r = await runCli(sb, ["plugin", "config", "tty-probe"], { cwd: projectDir });
    expect(r.code).toBe(1);
    const out = plain(r.stdout + r.stderr);
    expect(out).toContain("needs an interactive terminal");
    expect(out).toContain("--enable");
    expect(out).not.toContain("setRawMode");
    expect(snapshotTree(projectDir)).toEqual(before);
  });
});
