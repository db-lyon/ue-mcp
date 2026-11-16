/**
 * Every module under src/ has been placed (#817, plan 6.8).
 *
 * Multi-editor work is a per-module question: does this thing have to be told
 * which editor, or is one answer right for the whole process? Answering it
 * module by module was how the per-session state, the CLI targeting and the
 * config layering got done, and the risk on the other side of that work is a
 * new module that nobody asks the question about, quietly holding one editor's
 * state for all of them.
 *
 * So each module answers it on its own first line (after a shebang, if any),
 * and a module without the answer fails the build:
 *
 *   // @session per-session: <reason>
 *       Holds per-editor state, is built once per session, or is handed a
 *       session / project / bridge and answers differently for each. It has to
 *       be told which editor.
 *   // @session independent: <reason>
 *       Pure logic over its arguments, shared declarations, process-wide
 *       infrastructure, or user and machine scoped state that is deliberately
 *       not per editor.
 *
 * Generated modules carry the tag too: their generators write it.
 */
import { describe, it, expect } from "vitest";
import { readdirSync, readFileSync, statSync } from "node:fs";
import { dirname, join, relative, sep } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const srcDir = join(here, "..", "..", "src");

const TAG = /^\/\/ @session (per-session|independent): (.*)$/;
const ANY_TAG = /^\s*\/\/\s*@session\b/;

function listModules(dir: string): string[] {
  const out: string[] = [];
  for (const entry of readdirSync(dir)) {
    const full = join(dir, entry);
    if (statSync(full).isDirectory()) {
      out.push(...listModules(full));
      continue;
    }
    if (entry.endsWith(".ts") && !entry.endsWith(".d.ts")) {
      out.push(relative(srcDir, full).split(sep).join("/"));
    }
  }
  return out.sort();
}

/** The module's lines, with a leading shebang dropped. */
function linesOf(module: string): string[] {
  const lines = readFileSync(join(srcDir, module), "utf8").split(/\r?\n/);
  return lines[0]?.startsWith("#!") ? lines.slice(1) : lines;
}

const HOW =
  "Put `// @session per-session: <reason>` or `// @session independent: <reason>` on the module's " +
  "first line (after a shebang). The question is whether it has to be told which editor it is acting on (#817).";

describe("module inventory", () => {
  const modules = listModules(srcDir);

  it("finds the modules to place", () => {
    expect(modules.length).toBeGreaterThan(100);
    expect(modules).toContain("index.ts");
    expect(modules).toContain("tools/asset.ts");
  });

  it("has every module answer the session question on its first line, with a reason", () => {
    const problems: string[] = [];
    for (const module of modules) {
      const lines = linesOf(module);
      const m = TAG.exec(lines[0] ?? "");
      if (!m) problems.push(`${module}: no @session tag on its first line`);
      else if (m[2].trim().length < 20) problems.push(`${module}: the @session reason is too short to be one`);
      const extra = lines.slice(1).filter((l) => ANY_TAG.test(l)).length;
      if (extra > 0) problems.push(`${module}: more than one @session tag`);
    }
    expect(problems, [...problems, HOW].join("\n")).toEqual([]);
  });

  it("keeps the known anchors in place", () => {
    const kind = (module: string) => TAG.exec(linesOf(module)[0] ?? "")?.[1];
    expect(kind("sessions/session.ts")).toBe("per-session");
    expect(kind("bridge/bridge.ts")).toBe("per-session");
    expect(kind("tools/asset.ts")).toBe("per-session");
    expect(kind("core/log.ts")).toBe("independent");
  });
});
