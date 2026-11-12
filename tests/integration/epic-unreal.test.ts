/**
 * Epic's own Unreal MCP server and Unreal Assistant, through the real CLI and
 * server (#1825, #1826): doctor and project(get_status) report the server a
 * UE 5.9 project starts beside ue-mcp, init turns its auto-start off or moves
 * its port in the project's per-user ini, and init attaches ue-mcp to the
 * assistant's per-user AIAssistant.json and detaches it again. The engine is
 * a fake tree in the sandbox, and Unreal's per-user folder is the sandbox's.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { userIniPath, UNREAL_MCP_SECTION } from "../../src/integrations/epic/unreal-mcp-server.js";
import { userSettingsDir } from "../../src/integrations/epic/unreal-assistant.js";
import { McpServer, PACKAGE_VERSION, plain, runCli, Sandbox, snapshotTree, type CliOptions, type CliResult } from "./harness.js";

let sb: Sandbox;
let engineRoot: string;
let fakeNpm: string;

beforeAll(() => {
  sb = new Sandbox("ue-mcp-epic-");
  // An installed UE 5.9 as far as these files go: both plugins, and the Base ini that turns auto-start on.
  engineRoot = sb.path("UE_5.9");
  const plugins = path.join(engineRoot, "Engine", "Plugins", "Experimental");
  for (const name of ["ModelContextProtocol", "AIAssistant"]) {
    fs.mkdirSync(path.join(plugins, name), { recursive: true });
    fs.writeFileSync(path.join(plugins, name, `${name}.uplugin`), JSON.stringify({ EnabledByDefault: true }));
  }
  // Engine/Source is what marks a folder as an engine root to the resolver.
  fs.mkdirSync(path.join(engineRoot, "Engine", "Source"), { recursive: true });
  fs.mkdirSync(path.join(engineRoot, "Engine", "Config"), { recursive: true });
  fs.writeFileSync(path.join(engineRoot, "Engine", "Config", "BaseEditorPerProjectUserSettings.ini"), `[${UNREAL_MCP_SECTION}]\nbAutoStartServer=True\n`);
  fs.mkdirSync(path.join(engineRoot, "Engine", "Build"), { recursive: true });
  fs.writeFileSync(path.join(engineRoot, "Engine", "Build", "InstalledBuild.txt"), "");
  fakeNpm = sb.path("fake-npm-script.mjs");
  fs.writeFileSync(fakeNpm, `export default function (args) {
  if (args.join(" ") === "view ue-mcp version") process.stdout.write(process.env.FAKE_NPM_LATEST + "\\n");
  return 0;
}
`);
});
afterAll(() => sb.cleanup());

function cli(args: string[], opts: CliOptions = {}): Promise<CliResult> {
  return runCli(sb, args, { ...opts, env: { UE_MCP_TEST_ENGINE_ROOT: engineRoot, NPM_FAKE_SCRIPT: fakeNpm, FAKE_NPM_LATEST: PACKAGE_VERSION, ...opts.env } });
}

const readJson = (file: string) => JSON.parse(fs.readFileSync(file, "utf8"));
const assistantConfig = () => path.join(userSettingsDir(sb.env()), "UnrealEngine", "Common", "AIAssistant.json");
type Step = { id: string; status: string; target: string; detail: string };

describe("Epic's Unreal MCP server", () => {
  it("doctor reports it running beside ue-mcp, with its port and URL, and writes nothing", async () => {
    covers("cli.doctor.unreal-mcp");
    const uproject = sb.makeProject("BesideGame", { engine: "5.9" });
    const before = snapshotTree(path.dirname(uproject));
    const r = await cli(["doctor", uproject]);
    const out = plain(r.stdout);
    expect(r.code, out + r.stderr).toBe(0);
    expect(out).toMatch(/epic mcp:\s+starts with the editor on http:\/\/127\.0\.0\.1:8000\/mcp \(auto-start from the engine's Base ini/);
    expect(out).toContain("Epic's Unreal MCP server starts with this project's editor on http://127.0.0.1:8000/mcp, beside ue-mcp.");
    expect(out).toContain("--no-unreal-mcp-autostart");
    expect(out).toMatch(/epic assistant:\s+ue-mcp not attached/);
    expect(snapshotTree(path.dirname(uproject))).toEqual(before);

    // Disabled in the .uproject: reported as such, and no note.
    const off = sb.makeProject("OffGame", { engine: "5.9" });
    fs.writeFileSync(off, JSON.stringify({ ...readJson(off), Plugins: [{ Name: "ModelContextProtocol", Enabled: false }] }));
    const offOut = plain((await cli(["doctor", off])).stdout);
    expect(offOut).toMatch(/epic mcp:\s+plugin disabled in the \.uproject/);
    expect(offOut).not.toContain("beside ue-mcp");

    // An engine without the server says nothing about it.
    const old = sb.makeProject("OldGame", { engine: "5.6" });
    expect(plain((await cli(["doctor", old], { env: { UE_MCP_TEST_ENGINE_ROOT: sb.path("no-engine") } })).stdout)).not.toContain("epic mcp:");
  });

  it("project(get_status) names it while it auto-starts, and drops it once init turns that off", async () => {
    covers("server.status.unreal-mcp", "cli.init.unreal-mcp");
    const uproject = sb.makeProject("StatusGame", { engine: "5.9" });
    const status = async () => {
      const server = await McpServer.start(sb, { projects: [uproject], env: { UE_MCP_TEST_ENGINE_ROOT: engineRoot } });
      try {
        const r = await server.call("tools", { action: "call", category: "project", method: "get_status" });
        expect(r.isError, r.text).toBe(false);
        return JSON.parse(r.text);
      } finally {
        await server.close();
      }
    };
    expect((await status()).unrealMcpServer).toMatchObject({ url: "http://127.0.0.1:8000/mcp", autoStartSource: "engine", portSource: "default" });

    const r = await cli(["init", uproject, "--json", "--install", "source", "--no-unreal-mcp-autostart"]);
    expect(r.code, r.stdout + r.stderr).toBe(0);
    const ini = userIniPath(path.dirname(uproject));
    expect(JSON.parse(r.stdout).steps.find((s: Step) => s.id === "unreal-mcp")).toMatchObject({ status: "changed", target: ini, detail: "Epic's Unreal MCP server: auto-start off" });
    expect(fs.readFileSync(ini, "utf8")).toBe(`[${UNREAL_MCP_SECTION}]\nbAutoStartServer=False\n`);
    expect((await status()).unrealMcpServer).toBeUndefined();
  });

  it("init moves its port, keeps the rest of the user ini, and a repeat changes nothing", async () => {
    covers("cli.init.unreal-mcp", "cli.init.rerun");
    const uproject = sb.makeProject("PortGame", { engine: "5.9" });
    const ini = userIniPath(path.dirname(uproject));
    fs.mkdirSync(path.dirname(ini), { recursive: true });
    fs.writeFileSync(ini, "[/Script/UnrealEd.EditorPerProjectUserSettings]\r\nbKeep=True\r\n");

    const planned = JSON.parse((await cli(["init", uproject, "--json", "--dry-run", "--install", "source", "--unreal-mcp-port", "8100"])).stdout).plan;
    expect(planned.unrealMcp).toMatchObject({ pluginEnabled: true, autoStart: true, port: 8000, url: "http://127.0.0.1:8000/mcp", runsBeside: true, change: { port: 8100 } });

    const r = await cli(["init", uproject, "--json", "--install", "source", "--unreal-mcp-port", "8100"]);
    expect(r.code, r.stdout + r.stderr).toBe(0);
    expect(fs.readFileSync(ini, "utf8")).toBe(`[/Script/UnrealEd.EditorPerProjectUserSettings]\r\nbKeep=True\r\n\r\n[${UNREAL_MCP_SECTION}]\r\nServerPortNumber=8100\r\n`);
    const doctor = plain((await cli(["doctor", uproject])).stdout);
    expect(doctor).toContain("starts with the editor on http://127.0.0.1:8100/mcp");

    const again = JSON.parse((await cli(["init", uproject, "--json", "--install", "source", "--unreal-mcp-port", "8100"])).stdout);
    expect(again.steps.filter((s: Step) => s.id === "unreal-mcp")).toEqual([]);

    // The bridge's own port is refused before anything is written.
    const bridge = JSON.parse((await cli(["init", uproject, "--json", "--dry-run", "--install", "source"])).stdout).plan.bridge.port;
    const clash = await cli(["init", uproject, "--json", "--install", "source", "--unreal-mcp-port", String(bridge)]);
    expect(clash.code).toBe(1);
    expect(JSON.parse(clash.stdout).error).toContain("is the ue-mcp bridge port");
  });

  it("a project on an engine without it gets a warning, not a failure", async () => {
    covers("cli.init.unreal-mcp");
    const uproject = sb.makeProject("NoServerGame", { engine: "5.6" });
    const r = await cli(["init", uproject, "--json", "--install", "source", "--no-unreal-mcp-autostart", "--unreal-assistant"], { env: { UE_MCP_TEST_ENGINE_ROOT: sb.path("no-engine") } });
    expect(r.code, r.stdout + r.stderr).toBe(0);
    const result = JSON.parse(r.stdout);
    expect(result.warnings).toEqual(expect.arrayContaining([
      expect.stringContaining("has no Unreal MCP server"),
      expect.stringContaining("Epic's Unreal Assistant was not attached"),
    ]));
    expect(fs.existsSync(userIniPath(path.dirname(uproject)))).toBe(false);
    expect(fs.existsSync(assistantConfig())).toBe(false);
  });
});

describe("Epic's Unreal Assistant", () => {
  it("init attaches ue-mcp beside the editor's own server, a repeat changes nothing, and --no-unreal-assistant takes it out", async () => {
    covers("cli.init.unreal-assistant");
    const uproject = sb.makeProject("AssistGame", { engine: "5.9" });
    const file = assistantConfig();

    const planned = JSON.parse((await cli(["init", uproject, "--json", "--dry-run", "--install", "source", "--unreal-assistant"])).stdout).plan;
    expect(planned.unrealAssistant).toMatchObject({ available: true, configPath: file, attached: false, action: "attach", shadowedBy: null });
    expect(fs.existsSync(file)).toBe(false);

    const r = await cli(["init", uproject, "--json", "--install", "source", "--unreal-assistant", "--pin"]);
    expect(r.code, r.stdout + r.stderr).toBe(0);
    expect(JSON.parse(r.stdout).steps.find((s: Step) => s.id === "unreal-assistant")).toMatchObject({ status: "changed", target: file });
    expect(readJson(file)).toEqual({
      mcp_servers: [
        { name: "unreal-mcp", type: "http", url: "http://127.0.0.1:8000/mcp", headers: [] },
        { name: "ue-mcp", command: "npx", args: ["-y", "--min-release-age=0", `ue-mcp@${PACKAGE_VERSION}`, uproject.replace(/\\/g, "/")], env: [] },
      ],
    });
    expect(plain((await cli(["doctor", uproject])).stdout)).toMatch(/epic assistant:\s+ue-mcp attached/);

    const again = JSON.parse((await cli(["init", uproject, "--json", "--install", "source", "--unreal-assistant", "--pin"])).stdout);
    expect(again.steps.find((s: Step) => s.id === "unreal-assistant")).toMatchObject({ status: "unchanged" });
    // Unset keeps it as it is.
    const kept = JSON.parse((await cli(["init", uproject, "--json", "--install", "source"])).stdout);
    expect(kept.steps.find((s: Step) => s.id === "unreal-assistant")).toBeUndefined();
    expect(readJson(file).mcp_servers).toHaveLength(2);

    const removed = await cli(["init", uproject, "--json", "--install", "source", "--no-unreal-assistant"]);
    expect(removed.code, removed.stdout + removed.stderr).toBe(0);
    expect(JSON.parse(removed.stdout).steps.find((s: Step) => s.id === "unreal-assistant")).toMatchObject({ status: "removed", target: file });
    // Only the stand-in for the engine's default was left, so the file goes and the engine chooses again.
    expect(fs.existsSync(file)).toBe(false);
  });

  it("merges into a config the user already has, saving it first, and never drops their servers", async () => {
    covers("cli.init.unreal-assistant");
    const uproject = sb.makeProject("MergeGame", { engine: "5.9" });
    const file = assistantConfig();
    fs.mkdirSync(path.dirname(file), { recursive: true });
    const original = JSON.stringify({ agent: { command: "claude-agent-acp", arguments: [] }, mcp_servers: [{ name: "mine", type: "http", url: "http://127.0.0.1:9100/mcp", headers: [] }] }, null, 2);
    fs.writeFileSync(file, original);

    const r = await cli(["init", uproject, "--json", "--install", "source", "--unreal-assistant"]);
    expect(r.code, r.stdout + r.stderr).toBe(0);
    expect(JSON.parse(r.stdout).steps.find((s: Step) => s.id === "unreal-assistant").detail).toContain(`${file}.ue-mcp-backup`);
    expect(fs.readFileSync(`${file}.ue-mcp-backup`, "utf8")).toBe(original);
    const after = readJson(file);
    expect(after.agent).toEqual({ command: "claude-agent-acp", arguments: [] });
    expect(after.mcp_servers.map((s: { name: string }) => s.name)).toEqual(["mine", "ue-mcp"]);

    await cli(["init", uproject, "--json", "--install", "source", "--no-unreal-assistant"]);
    expect(readJson(file)).toEqual(JSON.parse(original));
  });

  it("warns when an engine-level config shadows the user file", async () => {
    covers("cli.init.unreal-assistant");
    const uproject = sb.makeProject("ShadowGame", { engine: "5.9" });
    const shadow = path.join(engineRoot, "Engine", "Config", "AIAssistant.json");
    fs.writeFileSync(shadow, "{}");
    try {
      const r = await cli(["init", uproject, "--json", "--install", "source", "--unreal-assistant"]);
      expect(r.code, r.stdout + r.stderr).toBe(0);
      expect(JSON.parse(r.stdout).warnings).toEqual(expect.arrayContaining([expect.stringContaining(`reads ${shadow} first`)]));
      expect(plain((await cli(["doctor", uproject])).stdout)).toContain(`The Unreal Assistant reads ${shadow} first`);
    } finally {
      fs.rmSync(shadow);
      await cli(["init", uproject, "--json", "--install", "source", "--no-unreal-assistant"]);
    }
  });
});
