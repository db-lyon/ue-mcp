/**
 * `--editor <name-or-path>` on the one-shot CLI subcommands (#817, plan 6.2).
 *
 * The property that matters is that a name means the same editor to the CLI
 * that it means to the running server. The CLI has no session registry, so it
 * has to reach the same answer from the argv recorded in the MCP client
 * config, using SessionRegistry's own naming rule.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import {
  parseEditorFlag,
  namesForProjects,
  discoverConfiguredEditors,
  resolveEditorFlag,
  takeEditorTarget,
  EditorFlagError,
} from "../../src/editor-flag.js";
import { SessionRegistry } from "../../src/session.js";

let root: string;

function makeProject(dirName: string, projectName = dirName): string {
  const dir = path.join(root, dirName);
  fs.mkdirSync(dir, { recursive: true });
  const uproject = path.join(dir, `${projectName}.uproject`);
  fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: "5.6" }), "utf-8");
  return uproject;
}

function writeMcpJson(cwd: string, projects: string[]): void {
  fs.mkdirSync(cwd, { recursive: true });
  fs.writeFileSync(
    path.join(cwd, ".mcp.json"),
    JSON.stringify({
      mcpServers: {
        "ue-mcp": { command: "npx", args: ["ue-mcp", ...projects.map((p) => p.replace(/\\/g, "/"))] },
      },
    }),
    "utf-8",
  );
}

// Discovery reads the user's real client configs, which is the point in
// production and machine-dependent in a test. Point HOME/APPDATA at the
// temp root so only the configs this file writes are visible.
const realEnv = {
  HOME: process.env.HOME,
  USERPROFILE: process.env.USERPROFILE,
  APPDATA: process.env.APPDATA,
};

beforeEach(() => {
  root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-editorflag-"));
  const fakeHome = path.join(root, "home");
  fs.mkdirSync(fakeHome, { recursive: true });
  process.env.HOME = fakeHome;
  process.env.USERPROFILE = fakeHome;
  process.env.APPDATA = path.join(fakeHome, "AppData", "Roaming");
});

afterEach(() => {
  for (const [k, v] of Object.entries(realEnv)) {
    if (v === undefined) delete process.env[k];
    else process.env[k] = v;
  }
  fs.rmSync(root, { recursive: true, force: true });
});

describe("parseEditorFlag", () => {
  it("takes --editor <value> and leaves everything else in order", () => {
    const r = parseEditorFlag(["deploy", "--editor", "beta", "--force", "path/x"]);
    expect(r.editor).toBe("beta");
    expect(r.rest).toEqual(["deploy", "--force", "path/x"]);
  });

  it("takes the --editor=<value> spelling", () => {
    const r = parseEditorFlag(["--editor=alpha", "lean"]);
    expect(r.editor).toBe("alpha");
    expect(r.rest).toEqual(["lean"]);
  });

  it("leaves an argument list without the flag byte-identical", () => {
    const argv = ["lean", "--ci", "C:/proj/Alpha"];
    const r = parseEditorFlag(argv);
    expect(r.editor).toBeUndefined();
    expect(r.rest).toEqual(argv);
  });

  it("does not swallow the next flag when --editor has no value", () => {
    const r = parseEditorFlag(["--editor", "--build"]);
    expect(r.editor).toBeUndefined();
    expect(r.rest).toEqual(["--build"]);
  });
});

describe("namesForProjects", () => {
  it("assigns the same names SessionRegistry would", () => {
    const alpha = makeProject("Alpha");
    const beta = makeProject("Beta");
    const registry = new SessionRegistry();
    const sa = registry.register({ projectPath: alpha });
    const sb = registry.register({ projectPath: beta });

    expect(namesForProjects([alpha, beta])).toEqual([sa.name, sb.name]);
  });

  it("de-duplicates a repeated base name the way the registry does", () => {
    const first = makeProject("one", "Shared");
    const second = makeProject("two", "Shared");
    expect(namesForProjects([first, second])).toEqual(["Shared", "Shared-2"]);
  });
});

describe("discoverConfiguredEditors", () => {
  it("reads every project positional out of a project-scoped client config", () => {
    const alpha = makeProject("Alpha");
    const beta = makeProject("Beta");
    const cwd = path.join(root, "workspace");
    writeMcpJson(cwd, [alpha, beta]);

    const found = discoverConfiguredEditors(cwd);
    expect(found.map((e) => e.name)).toEqual(["Alpha", "Beta"]);
    expect(found.map((e) => path.resolve(e.projectPath))).toEqual([
      path.resolve(alpha),
      path.resolve(beta),
    ]);
  });
});

describe("resolveEditorFlag", () => {
  it("resolves a registered session name to its project", () => {
    const alpha = makeProject("Alpha");
    const beta = makeProject("Beta");
    const cwd = path.join(root, "workspace");
    writeMcpJson(cwd, [alpha, beta]);

    expect(path.resolve(resolveEditorFlag("beta", cwd))).toBe(path.resolve(beta));
    expect(path.resolve(resolveEditorFlag("Alpha", cwd))).toBe(path.resolve(alpha));
  });

  it("resolves a path when no name matches", () => {
    const gamma = makeProject("Gamma");
    const cwd = path.join(root, "workspace");
    fs.mkdirSync(cwd, { recursive: true });

    expect(path.resolve(resolveEditorFlag(gamma, cwd))).toBe(path.resolve(gamma));
    expect(path.resolve(resolveEditorFlag(path.dirname(gamma), cwd))).toBe(path.resolve(gamma));
  });

  it("prefers a registered name over a same-named directory in cwd", () => {
    const real = makeProject("Alpha");
    const cwd = path.join(root, "workspace");
    writeMcpJson(cwd, [real]);
    // A decoy directory in cwd with the same name and its own .uproject.
    const decoyDir = path.join(cwd, "Alpha");
    fs.mkdirSync(decoyDir, { recursive: true });
    fs.writeFileSync(path.join(decoyDir, "Alpha.uproject"), "{}", "utf-8");

    expect(path.resolve(resolveEditorFlag("Alpha", cwd))).toBe(path.resolve(real));
  });

  it("refuses an unknown target and names what is registered", () => {
    const alpha = makeProject("Alpha");
    const cwd = path.join(root, "workspace");
    writeMcpJson(cwd, [alpha]);

    expect(() => resolveEditorFlag("nope", cwd)).toThrow(EditorFlagError);
    try {
      resolveEditorFlag("nope", cwd);
    } catch (e) {
      expect((e as Error).message).toContain("Alpha");
    }
  });
});

describe("takeEditorTarget", () => {
  it("returns the untouched argv when the flag is absent", () => {
    const argv = ["build", "C:/proj/Alpha"];
    expect(takeEditorTarget(argv, root)).toEqual({ rest: argv });
  });

  it("returns a project path and the remaining argv when the flag is present", () => {
    const alpha = makeProject("Alpha");
    const cwd = path.join(root, "workspace");
    writeMcpJson(cwd, [alpha]);

    const r = takeEditorTarget(["build", "--editor", "Alpha"], cwd);
    expect(path.resolve(r.projectPath!)).toBe(path.resolve(alpha));
    expect(r.rest).toEqual(["build"]);
  });
});
