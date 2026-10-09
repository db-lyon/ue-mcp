/**
 * Removing the bridge, or the project, leaves client configs pointing at it
 * (spec section 10). doctor names them and `doctor --fix` removes exactly those
 * entries, leaving every other server and every live entry alone.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import { findDeadClientEntries, formatDoctor, removeDeadClientEntries, type DoctorReport } from "../../src/cli/doctor.js";
import { ProjectFixture } from "../helpers/project-fixture.js";

let fixture: ProjectFixture;
const saved = { ...process.env };

beforeEach(() => {
  fixture = new ProjectFixture("ue-mcp-doctor-dead-");
  for (const k of ["HOME", "USERPROFILE"]) process.env[k] = fixture.root;
  process.env.APPDATA = path.join(fixture.root, "AppData");
});

afterEach(() => {
  for (const k of Object.keys(process.env)) if (!(k in saved)) delete process.env[k];
  Object.assign(process.env, saved);
  fixture.cleanup();
});

const fwd = (p: string) => p.replace(/\\/g, "/");

function withBridge(uproject: string): void {
  const dir = path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge");
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(path.join(dir, "UE_MCP_Bridge.uplugin"), "{}");
}

function writeJson(file: string, servers: Record<string, unknown>): void {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, JSON.stringify({ mcpServers: servers }, null, 2));
}

describe("dead client entries", () => {
  it("finds entries for a missing project or a project without the bridge, and removes only those", () => {
    const live = fixture.makeProject("Live");
    withBridge(live);
    const bare = fixture.makeProject("Bare");
    const gone = fwd(path.join(fixture.root, "Gone", "Gone.uproject"));
    const projectDir = path.dirname(live);

    const projectMcp = path.join(projectDir, ".mcp.json");
    writeJson(projectMcp, { "ue-mcp": { command: "npx", args: ["-y", "ue-mcp@latest", fwd(live)] } });
    const globalMcp = path.join(fixture.root, ".claude", ".mcp.json");
    writeJson(globalMcp, {
      "ue-mcp": { command: "npx", args: ["-y", "ue-mcp@latest", gone] },
      other: { command: "something", args: [] },
    });
    const codex = path.join(fixture.root, ".codex", "config.toml");
    fs.mkdirSync(path.dirname(codex), { recursive: true });
    fs.writeFileSync(codex, `[mcp_servers.other]\ncommand = "x"\n\n[mcp_servers.ue-mcp]\ncommand = "npx"\nargs = ["-y", "ue-mcp@latest", "${fwd(bare)}"]\nenabled = true\n`);

    const dead = findDeadClientEntries(projectDir);
    expect(dead.map((d) => [d.config, d.reason]).sort()).toEqual([
      [codex, "no bridge installed"],
      [globalMcp, "project missing"],
    ].sort());

    const changed = removeDeadClientEntries(projectDir, dead);
    expect(changed.sort()).toEqual([codex, globalMcp].sort());
    expect(JSON.parse(fs.readFileSync(globalMcp, "utf-8")).mcpServers).toEqual({ other: { command: "something", args: [] } });
    expect(fs.readFileSync(codex, "utf-8")).toContain("[mcp_servers.other]");
    expect(fs.readFileSync(codex, "utf-8")).not.toContain("mcp_servers.ue-mcp");
    expect(JSON.parse(fs.readFileSync(projectMcp, "utf-8")).mcpServers["ue-mcp"]).toBeDefined();
    expect(findDeadClientEntries(projectDir)).toEqual([]);
  });

  it("is reported as a problem that names the fix", () => {
    const report: DoctorReport = {
      selfVersion: "1.0.0",
      registryLatest: "1.0.0",
      npmGlobal: { version: "1.0.0", dir: null },
      localShadow: null,
      effectiveNpx: "1.0.0",
      runningServers: [],
      targetProjectDir: null,
      bridgePlugin: null,
      bareNpxConfigs: [],
      deadClientEntries: [{ client: "Claude Desktop", config: "C:/x/claude_desktop_config.json", project: "C:/Gone/Gone.uproject", reason: "project missing" }],
    };
    const out = formatDoctor(report).replace(/\x1b\[[0-9;]*m/g, "");
    expect(out).toContain("C:/Gone/Gone.uproject, which no longer exists");
    expect(out).toContain("ue-mcp doctor --fix");
    expect(out).not.toContain("Everything aligned");
  });
});
