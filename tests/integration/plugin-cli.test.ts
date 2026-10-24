/**
 * `ue-mcp plugin <subcommand>`, spawned in a sandbox: npm is the recording
 * fake (installing packages built on disk), the registry is a local FakeHttp,
 * and every assertion reads what the command left in the project.
 */
import { spawn } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import yaml from "js-yaml";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeHttp, plain, runCli, Sandbox, type CliResult } from "./harness.js";
import {
  helloTask,
  placeInstalled,
  pluginsYml,
  skillText,
  startSpecsBridge,
  writeFakeNpmScript,
  writePluginPackage,
} from "./plugin-fixtures.js";

const PKG = "ue-mcp-clifix";
const SLUG = "clifix";

let sb: Sandbox;
let registry: FakeHttp;
let npmScript: string;
const published: Array<{ auth: string | undefined; body: Record<string, unknown> }> = [];

function manifest(version: string): string {
  return `actionPrefix: cf
inject:
  project:
    hello:
      task: cf_hello
      description: Greets (${version})
tasks:
  cf_hello:
    class_path: tasks/hello
nativeModule:
  uePluginName: ClifixNative
  minBridgeApi: 1
  source: ue/Plugins/ClifixNative
  supportedEngineVersions: ["5.6"]
  specs: handler-specs.json
  handlers:
    cf_ping:
      description: Pings
flows:
  clifix_hello:
    description: Greets
    steps:
      1:
        task: cf_hello
  extra_hi:
    description: Greets again
    group: extra
    steps:
      1:
        task: cf_hello
registry:
  tagline: Fixture plugin for the CLI tests
`;
}

function clifix(version: string): string {
  const v1 = version === "1.0.0";
  return writePluginPackage(sb.path("pkgs", `clifix-${version}`), {
    name: PKG,
    version,
    manifest: manifest(version),
    pkg: { description: "A fixture plugin", repository: { url: "git+https://github.com/example/ue-mcp-clifix.git" } },
    files: {
      "tasks/hello.js": helloTask(`${PKG}@${version}`),
      "README.md": `# clifix ${version}\n\nFixture readme.\n`,
      "ue/Plugins/ClifixNative/ClifixNative.uplugin": `{"VersionName": "${version}"}`,
      ...(v1 ? { "ue/Plugins/ClifixNative/Source/Old.cpp": "// only in 1.0.0\n" } : { "ue/Plugins/ClifixNative/Source/New.cpp": "// from 1.1.0\n" }),
    },
    skills: {
      guide: skillText("guide", "How to greet", 'Call project(action="cf_hello") to greet.'),
      ...(v1 ? { old: skillText("old", "Only shipped by 1.0.0") } : {}),
    },
  });
}

/** A project folder with a ue-mcp.yml and no package.json, as `ue-mcp init` leaves one. */
function project(name: string, plugins: string[] = []): string {
  return path.dirname(sb.makeProject(name, { config: plugins.length ? pluginsYml(plugins) : "ue-mcp:\n  version: 1\n" }));
}

function plugin(args: string[], cwd: string, env: Record<string, string | undefined> = {}): Promise<CliResult> {
  return runCli(sb, ["plugin", ...args], { cwd, env: { NPM_FAKE_SCRIPT: npmScript, UE_MCP_REGISTRY: registry.origin, ...env } });
}

function declared(projectDir: string): Array<{ name: string; version?: string }> {
  const doc = yaml.load(fs.readFileSync(path.join(projectDir, "ue-mcp.yml"), "utf8")) as { plugins?: Array<{ name: string; version?: string }> };
  return doc.plugins ?? [];
}

const exists = (...p: string[]) => fs.existsSync(path.join(...p));

beforeAll(async () => {
  sb = new Sandbox();
  const v1 = clifix("1.0.0");
  const v2 = clifix("1.1.0");
  npmScript = writeFakeNpmScript(sb, {
    versions: { [PKG]: { "1.0.0": v1, "1.1.0": v2 } },
    tarballs: { "ue-mcp-clifix-1.0.0.tgz": v1 },
  });
  registry = await FakeHttp.start((req) => {
    const url = new URL(req.url, "http://x");
    if (url.pathname === "/api/resolve") {
      return url.searchParams.get("name") === SLUG ? { json: { package: PKG } } : { status: 404, json: { error: "unknown" } };
    }
    if (url.pathname === "/api/plugins") {
      return { json: { plugins: [{ slug: SLUG, name: "CLI Fixture", packageName: PKG, tagline: "old tagline", category: "tools", pricing: "free", status: "published" }] } };
    }
    if (url.pathname === "/api/publish" && req.method === "POST") {
      published.push({ auth: req.headers.authorization, body: JSON.parse(req.body) });
      return { json: { ok: true } };
    }
    return { status: 404, body: "not found" };
  });
}, 30_000);

afterAll(async () => {
  await registry?.stop();
  sb?.cleanup();
});

describe("install, list, update, uninstall", () => {
  let game: string;
  beforeAll(() => {
    game = project("Game");
  });

  it("install resolves a registry slug, installs the package and records it", async () => {
    covers("cli.plugin.install");
    const r = await plugin(["install", SLUG, "--version", "1.0.0"], game);
    expect(r.code, r.stderr + r.stdout).toBe(0);
    expect(registry.requests.some((q) => q.url === `/api/resolve?name=${SLUG}`)).toBe(true);
    expect(plain(r.stdout)).toContain(`resolved '${SLUG}' -> '${PKG}' via the registry`);

    const calls = sb.npmCalls().filter((c) => path.resolve(c.cwd) === path.resolve(game));
    expect(calls.map((c) => c.args)).toEqual([["init", "-y"], ["install", "--save", `${PKG}@1.0.0`]]);
    expect(declared(game)).toEqual([{ name: PKG, version: "1.0.0" }]);
    expect(exists(game, "Plugins", "ClifixNative", "Source", "Old.cpp")).toBe(true);
    const state = JSON.parse(fs.readFileSync(path.join(game, ".ue-mcp", "native-modules.json"), "utf8"));
    expect(state[PKG].pluginVersion).toBe("1.0.0");
  });

  it("install copies the plugin's skills under its namespace and records who owns them", async () => {
    covers("plugin.skills.sync");
    const skill = fs.readFileSync(path.join(game, ".claude", "skills", "clifix-guide", "SKILL.md"), "utf8");
    expect(skill).toMatch(/^name: clifix-guide$/m);
    expect(exists(game, ".claude", "skills", "clifix-old", "SKILL.md")).toBe(true);
    const owners = JSON.parse(fs.readFileSync(path.join(game, ".ue-mcp", "skills.json"), "utf8"));
    expect(owners[PKG].sort()).toEqual(["clifix-guide", "clifix-old"]);
  });

  it("list shows each declared plugin with its version, pin and categories", async () => {
    covers("cli.plugin.list");
    const r = await plugin(["list"], game);
    expect(r.code, r.stderr).toBe(0);
    expect(plain(r.stdout)).toContain(`${PKG}@1.0.0 (pinned 1.0.0) - ok - injects: project`);
  });

  it("config lists the plugin's flow groups and writes a toggle to the chosen layer", async () => {
    covers("cli.plugin.config");
    const listed = await plugin(["config", SLUG, "--list-groups"], game);
    expect(listed.code, listed.stderr).toBe(0);
    expect(plain(listed.stdout)).toMatch(/on\s+clifix/);
    expect(plain(listed.stdout)).toMatch(/on\s+extra/);

    const [global, local] = await Promise.all([
      plugin(["config", SLUG, "--disable", "extra"], game),
      plugin(["config", PKG, "--enable", "clifix", "--local"], game),
    ]);
    expect(global.code, global.stderr).toBe(0);
    expect(local.code, local.stderr).toBe(0);
    const g = yaml.load(fs.readFileSync(path.join(sb.home, ".ue-mcp", "config.yml"), "utf8")) as Record<string, any>;
    expect(g["ue-mcp"].pluginConfig[SLUG].groups).toEqual({ extra: false });
    const l = yaml.load(fs.readFileSync(path.join(game, "ue-mcp.local.yml"), "utf8")) as Record<string, any>;
    expect(l["ue-mcp"].pluginConfig[SLUG].groups).toEqual({ clifix: true });

    const after = await plugin(["config", SLUG, "--list-groups"], game);
    expect(plain(after.stdout)).toMatch(/off\s+extra \[set in/);

    const bad = await plugin(["config", SLUG, "--enable", "nope"], game);
    expect(bad.code).toBe(1);
    expect(bad.stderr).toContain("'nope' is not a group of clifix");
  });

  it("update moves the plugin to the newest version, its pin, native files and skills with it", async () => {
    covers("cli.plugin.update");
    const r = await plugin(["update"], game);
    expect(r.code, r.stderr + r.stdout).toBe(0);
    const out = plain(r.stdout);
    expect(out).toContain(`updating ${PKG} (1.0.0 -> 1.1.0)`);
    expect(out).toContain("Rebuild needed: yes");
    expect(sb.npmCalls().some((c) => c.args.join(" ") === `view ${PKG} version`)).toBe(true);
    expect(declared(game)).toEqual([{ name: PKG, version: "1.1.0" }]);
    expect(exists(game, "Plugins", "ClifixNative", "Source", "New.cpp")).toBe(true);
    expect(exists(game, "Plugins", "ClifixNative", "Source", "Old.cpp")).toBe(false);
    expect(exists(game, ".claude", "skills", "clifix-old")).toBe(false);
    expect(exists(game, ".claude", "skills", "clifix-guide", "SKILL.md")).toBe(true);
  });

  it("uninstall leaves everything in place when npm uninstall fails", async () => {
    covers("cli.plugin.uninstall");
    const r = await plugin(["uninstall", PKG], game, { NPM_FAKE_FAIL: "uninstall" });
    expect(r.code).toBe(1);
    expect(r.stderr).toContain(`npm uninstall ${PKG} exited 1`);
    expect(declared(game)).toEqual([{ name: PKG, version: "1.1.0" }]);
    expect(exists(game, ".claude", "skills", "clifix-guide", "SKILL.md")).toBe(true);
    expect(exists(game, "Plugins", "ClifixNative", "Source", "New.cpp")).toBe(true);
  });

  it("uninstall removes the package, its entry, its native module and its skills", async () => {
    covers("cli.plugin.uninstall", "plugin.skills.sync");
    // Build output the install never recorded, which a file-by-file undeploy left behind.
    fs.mkdirSync(path.join(game, "Plugins", "ClifixNative", "Binaries", "Win64"), { recursive: true });
    fs.writeFileSync(path.join(game, "Plugins", "ClifixNative", "Binaries", "Win64", "UnrealEditor-ClifixNative.dll"), "dll");
    fs.mkdirSync(path.join(game, "Plugins", "ClifixNative", "Intermediate", "Build"), { recursive: true });
    fs.writeFileSync(path.join(game, "Plugins", "ClifixNative", "Intermediate", "Build", "x.obj"), "obj");
    const r = await plugin(["uninstall", PKG], game);
    expect(r.code, r.stderr + r.stdout).toBe(0);
    expect(declared(game)).toEqual([]);
    expect(exists(game, "node_modules", PKG)).toBe(false);
    expect(exists(game, "Plugins", "ClifixNative")).toBe(false);
    expect(plain(r.stdout)).toContain("Plugins/ClifixNative/ (source, Binaries and Intermediate)");
    expect(exists(game, ".claude", "skills", "clifix-guide")).toBe(false);
    const pj = JSON.parse(fs.readFileSync(path.join(game, "package.json"), "utf8"));
    expect(pj.dependencies?.[PKG]).toBeUndefined();
  });
});

describe("remove of a superseded plugin's native module", () => {
  // A .uproject as the editor writes it: tabs, CRLF, the entry in the middle.
  const UPROJECT = [
    "{",
    '\t"FileVersion": 3,',
    '\t"EngineAssociation": "5.8",',
    '\t"Plugins": [',
    "\t\t{",
    '\t\t\t"Name": "UE_MCP_Bridge",',
    '\t\t\t"Enabled": true',
    "\t\t},",
    "\t\t{",
    '\t\t\t"Name": "PIE_Studio",',
    '\t\t\t"Enabled": true',
    "\t\t},",
    "\t\t{",
    '\t\t\t"Name": "ModelingToolsEditorMode",',
    '\t\t\t"Enabled": true',
    "\t\t}",
    "\t]",
    "}",
  ].join("\r\n");
  const WITHOUT = UPROJECT.replace('\t\t{\r\n\t\t\t"Name": "PIE_Studio",\r\n\t\t\t"Enabled": true\r\n\t\t},\r\n', "");

  function vale(name: string): { dir: string; uproject: string } {
    const dir = project(name, ["pie-studio"]);
    const uproject = path.join(dir, `${name}.uproject`);
    fs.writeFileSync(uproject, UPROJECT);
    for (const f of ["PIE_Studio.uplugin", "Source/PIE_Studio/PIE_Studio.Build.cs", "Binaries/Win64/UnrealEditor-PIE_Studio.dll", "Intermediate/Build/Win64/x.obj"]) {
      fs.mkdirSync(path.dirname(path.join(dir, "Plugins", "PIE_Studio", f)), { recursive: true });
      fs.writeFileSync(path.join(dir, "Plugins", "PIE_Studio", f), "x");
    }
    return { dir, uproject };
  }

  it("deletes the whole plugin folder and its .uproject entry, keeping the file's formatting, and says what it removed", async () => {
    covers("cli.plugin.uninstall");
    const { dir, uproject } = vale("ValeLike");
    const r = await plugin(["remove", "pie-studio"], dir);
    expect(r.code, r.stderr + r.stdout).toBe(0);
    expect(exists(dir, "Plugins", "PIE_Studio")).toBe(false);
    expect(fs.readFileSync(uproject, "utf8")).toBe(WITHOUT);
    expect(declared(dir)).toEqual([]);
    const out = plain(r.stdout);
    expect(out).toContain("removed pie-studio:");
    expect(out).toContain("Plugins/PIE_Studio/ (source, Binaries and Intermediate)");
    expect(out).toContain("the PIE_Studio entry in ValeLike.uproject");
  });

  it("refuses while an editor has the project open, and touches nothing", async () => {
    covers("cli.plugin.uninstall");
    const { dir, uproject } = vale("ValeOpen");
    // A process named like the editor with the .uproject on its command line, which is how the probe tells.
    const exe = sb.path("fake-editor", process.platform === "win32" ? "UnrealEditor.exe" : "UnrealEditor");
    fs.mkdirSync(path.dirname(exe), { recursive: true });
    fs.copyFileSync(process.execPath, exe);
    fs.chmodSync(exe, 0o755);
    const editor = spawn(exe, ["-e", "setInterval(() => {}, 1000)", uproject], { stdio: "ignore", env: sb.env() });
    try {
      const r = await plugin(["remove", "pie-studio"], dir);
      expect(r.code).toBe(1);
      expect(r.stderr).toContain(`an editor has ValeOpen.uproject open (pid ${editor.pid})`);
      expect(r.stderr).toContain("Nothing was removed.");
      expect(exists(dir, "Plugins", "PIE_Studio", "Binaries", "Win64", "UnrealEditor-PIE_Studio.dll")).toBe(true);
      expect(fs.readFileSync(uproject, "utf8")).toBe(UPROJECT);
      expect(declared(dir)).toEqual([{ name: "pie-studio" }]);
      expect(sb.npmCalls().some((c) => path.resolve(c.cwd) === path.resolve(dir))).toBe(false);
    } finally {
      editor.kill();
    }
  }, 60_000);
});

describe.concurrent("each in its own project", () => {
  it("records the package npm installed from a folder, not the path given", async () => {
    covers("cli.plugin.install");
    const local = project("LocalDir");
    const r = await plugin(["install", path.relative(local, sb.path("pkgs", "clifix-1.0.0"))], local);
    expect(r.code, r.stderr + r.stdout).toBe(0);
    expect(declared(local)).toEqual([{ name: PKG }]);
    expect(exists(local, ".claude", "skills", "clifix-guide", "SKILL.md")).toBe(true);
  });

  it("records the package npm installed from a tarball", async () => {
    covers("cli.plugin.install");
    const tgz = project("LocalTgz");
    const r = await plugin(["install", "ue-mcp-clifix-1.0.0.tgz"], tgz);
    expect(r.code, r.stderr + r.stdout).toBe(0);
    expect(declared(tgz)).toEqual([{ name: PKG }]);
    const list = await plugin(["list"], tgz);
    expect(plain(list.stdout)).toContain(`${PKG}@1.0.0 - ok`);
  });

  it("update restores a listed plugin's deleted skill and prunes an unlisted plugin's", async () => {
    covers("plugin.skills.sync");
    const dir = project("Synced", [PKG]);
    placeInstalled(dir, sb.path("pkgs", "clifix-1.1.0"));

    const first = await plugin(["update"], dir);
    expect(first.code, first.stderr + first.stdout).toBe(0);
    expect(plain(first.stdout)).toContain(`${PKG}@1.1.0 is current`);
    expect(exists(dir, ".claude", "skills", "clifix-guide", "SKILL.md")).toBe(true);

    fs.writeFileSync(path.join(dir, "ue-mcp.yml"), "ue-mcp:\n  version: 1\nplugins: []\n");
    const second = await plugin(["update"], dir);
    expect(second.code, second.stderr + second.stdout).toBe(0);
    expect(exists(dir, ".claude", "skills", "clifix-guide")).toBe(false);
    expect(exists(dir, ".ue-mcp", "skills.json")).toBe(false);
  });

  it("create scaffolds a package whose skills pass check-skills", async () => {
    covers("cli.plugin.create", "cli.plugin.check-skills");
    const dir = sb.path("authoring", "ue-mcp-scaffolded");
    const r = await plugin(["create", "ue-mcp-scaffolded", "--dir", dir], sb.root);
    expect(r.code, r.stderr + r.stdout).toBe(0);
    const pj = JSON.parse(fs.readFileSync(path.join(dir, "package.json"), "utf8"));
    expect(pj.name).toBe("ue-mcp-scaffolded");
    const m = yaml.load(fs.readFileSync(path.join(dir, "ue-mcp.plugin.yml"), "utf8")) as { actionPrefix: string; tasks: Record<string, unknown> };
    expect(m.actionPrefix).toBe("ums");
    expect(Object.keys(m.tasks).length).toBeGreaterThan(0);
    expect(exists(dir, "skills", "workflow", "SKILL.md")).toBe(true);

    const again = await plugin(["create", "ue-mcp-scaffolded", "--dir", dir], sb.root);
    expect(again.code).toBe(1);
    expect(again.stderr).toContain("exists and is not empty");

    const check = await plugin(["check-skills", dir], sb.root);
    expect(check.code, check.stderr + check.stdout).toBe(0);
    expect(plain(check.stdout)).toContain("skill(s) ok");
    expect(plain(check.stdout)).toContain(".claude/skills/scaffolded-workflow");
  });

  it("check-skills fails on a skill that names an action nothing provides", async () => {
    covers("cli.plugin.check-skills");
    const dir = writePluginPackage(sb.path("authoring", "broken-skills"), {
      name: "ue-mcp-broken",
      version: "0.1.0",
      manifest: "actionPrefix: br\n",
      skills: { guide: skillText("guide", "Teaches a call that does not exist", 'Call project(action="no_such_action").') },
    });
    const r = await plugin(["check-skills", dir], sb.root);
    expect(r.code).toBe(1);
    expect(plain(r.stdout)).toContain("no_such_action");
    expect(r.stderr).toContain("1 problem(s) in 1 skill(s)");
  });

  it("record-specs writes the handler contracts a running bridge publishes, and --check compares", async () => {
    covers("cli.plugin.record-specs");
    const host = path.dirname(sb.makeProject("SpecsHost"));
    const pkgDir = writePluginPackage(sb.path("authoring", "specs-plugin"), {
      name: PKG,
      version: "1.1.0",
      manifest: manifest("1.1.0"),
      files: { "ue/Plugins/ClifixNative/ClifixNative.uplugin": "{}" },
    });
    const spec = { params: [{ name: "value", type: "string", required: true, description: "What to echo" }] };
    const bridge = await startSpecsBridge(host, { cf_ping: spec, unrelated: spec });
    try {
      const uproject = path.join(host, "SpecsHost.uproject");
      const missing = await plugin(["record-specs", pkgDir, "--project", uproject, "--check"], sb.root);
      expect(missing.code).toBe(1);
      expect(missing.stderr).toContain("differs from the contracts the running module registered");

      const r = await plugin(["record-specs", pkgDir, "--project", uproject], sb.root);
      expect(r.code, r.stderr + r.stdout).toBe(0);
      expect(plain(r.stdout)).toContain("recorded 1 handler contracts to handler-specs.json");
      const written = JSON.parse(fs.readFileSync(path.join(pkgDir, "handler-specs.json"), "utf8"));
      expect(written).toEqual({ handlerCount: 1, handlers: { cf_ping: spec } });

      const check = await plugin(["record-specs", pkgDir, "--project", uproject, "--check"], sb.root);
      expect(check.code, check.stderr).toBe(0);
      expect(plain(check.stdout)).toContain("matches the running module (1 handlers)");
      expect(bridge.handshakes()).toBeGreaterThanOrEqual(3);
    } finally {
      await bridge.stop();
    }
  });

  it("record-specs refuses without a project to read from", async () => {
    covers("cli.plugin.record-specs");
    const r = await plugin(["record-specs", sb.path("pkgs", "clifix-1.1.0")], sb.root);
    expect(r.code).toBe(1);
    expect(r.stderr).toContain("record-specs needs --project");
  });

  it("plugin with no subcommand or an unknown one prints usage and fails", async () => {
    covers("cli.plugin.usage");
    const [none, unknown] = await Promise.all([plugin([], sb.root), plugin(["frobnicate"], sb.root)]);
    for (const r of [none, unknown]) {
      expect(r.code).toBe(1);
      expect(plain(r.stderr)).toContain("ue-mcp plugin install <name> [--version x.y.z]");
      expect(plain(r.stderr)).toContain("ue-mcp plugin record-specs");
    }
  });
});

describe("publish", () => {
  it("publish --dry-run prints the listing diff without a token and posts nothing", async () => {
    covers("cli.plugin.publish.dry-run");
    const before = registry.requests.length;
    const r = await plugin(["publish", sb.path("pkgs", "clifix-1.1.0"), "--dry-run"], sb.root);
    expect(r.code, r.stderr + r.stdout).toBe(0);
    const out = plain(r.stdout);
    expect(out).toContain(`dry run - nothing posted. Changes to listing '${SLUG}'`);
    expect(out).toContain('tagline: "old tagline" -> "Fixture plugin for the CLI tests"');
    expect(out).toMatch(/readme: 0 chars -> \d+ chars/);
    const made = registry.requests.slice(before);
    expect(made.map((q) => `${q.method} ${q.url}`)).toEqual(["GET /api/plugins"]);
  });

  it("publish posts the merged listing with the token as a bearer", async () => {
    covers("cli.plugin.publish");
    const noToken = await plugin(["publish", sb.path("pkgs", "clifix-1.1.0")], sb.root);
    expect(noToken.code).toBe(1);
    expect(noToken.stderr).toContain("not logged in");
    expect(published).toHaveLength(0);

    const r = await plugin(["publish", sb.path("pkgs", "clifix-1.1.0")], sb.root, { UE_MCP_PUBLISH_TOKEN: "uemcp_test_token" });
    expect(r.code, r.stderr + r.stdout).toBe(0);
    expect(plain(r.stdout)).toContain(`published '${SLUG}'`);
    expect(published).toHaveLength(1);
    const { auth, body } = published[0];
    expect(auth).toBe("Bearer uemcp_test_token");
    expect(body).toMatchObject({
      slug: SLUG,
      name: "CLI Fixture",
      packageName: PKG,
      tagline: "Fixture plugin for the CLI tests",
      category: "tools",
      repoUrl: "https://github.com/example/ue-mcp-clifix",
      provides: ["skills", "flows", "tools"],
      ueVersions: ["5.6"],
    });
    expect(body.readme).toContain("# clifix 1.1.0");
    expect(body.status).toBeUndefined();
  });
});
