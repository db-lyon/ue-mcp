/**
 * npm's release-age gate refuses `npx ue-mcp@<new release>` even when ue-mcp is
 * in min-release-age-exclude, because npx ignores the exclusion (#1510). The
 * written launch lifts the gate, `ue-mcp update` rewrites a launch that does
 * not, and doctor reports and fixes the ones it finds.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import {
  NPX_AGE_GATE_OVERRIDE,
  isNpxCommand,
  liftAgeGateInEntry,
  liftsAgeGate,
  readUeMcpEntry,
} from "../../src/integrations/claude-code/mcp-client-config.js";
import {
  findAgeGatedLaunches,
  formatDoctor,
  liftAgeGatedLaunches,
  readNpmAgeGate,
  type DoctorReport,
  type NpmAgeGate,
} from "../../src/cli/doctor.js";
import { repinClientConfigs } from "../../src/editor/bridge-update.js";
import { ProjectFixture } from "../helpers/project-fixture.js";

let fixture: ProjectFixture;
const saved = { ...process.env };

beforeEach(() => {
  fixture = new ProjectFixture("ue-mcp-age-gate-");
  for (const k of ["HOME", "USERPROFILE"]) process.env[k] = fixture.root;
  process.env.APPDATA = path.join(fixture.root, "AppData");
});

afterEach(() => {
  for (const k of Object.keys(process.env)) if (!(k in saved)) delete process.env[k];
  Object.assign(process.env, saved);
  fixture.cleanup();
});

const fwd = (p: string) => p.replace(/\\/g, "/");
const GATE: NpmAgeGate = { npm: "12.0.1", minReleaseAge: 7, before: null, overridable: true };

function writeJson(file: string, servers: Record<string, unknown>): void {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, JSON.stringify({ mcpServers: servers }, null, 2));
}

/** A doctor report carrying only the gate findings. */
const report = (gate: NpmAgeGate, launches: DoctorReport["ageGatedLaunches"]): DoctorReport => ({
  selfVersion: "1.0.0",
  registryLatest: null,
  npmGlobal: { version: null, dir: null },
  localShadow: null,
  effectiveNpx: null,
  runningServers: [],
  targetProjectDir: null,
  bridgePlugin: null,
  bareNpxConfigs: [],
  npmAgeGate: gate,
  ageGatedLaunches: launches,
});

const answers = (npm: string, config: string) => (cmd: string) =>
  cmd === "npm --version" ? npm : cmd === "npm config get min-release-age before" ? config : null;

describe("launch args", () => {
  it("recognises the override in either spelling, and npx by name or path", () => {
    expect(NPX_AGE_GATE_OVERRIDE).toBe("--min-release-age=0");
    expect(liftsAgeGate(["-y", "--min-release-age=0", "ue-mcp@1.0.0"])).toBe(true);
    expect(liftsAgeGate(["-y", "--min-release-age", "0", "ue-mcp@1.0.0"])).toBe(true);
    expect(liftsAgeGate(["-y", "--min-release-age", "3", "ue-mcp@1.0.0"])).toBe(false);
    expect(liftsAgeGate(["-y", "ue-mcp@1.0.0"])).toBe(false);
    for (const c of ["npx", "npx.cmd", "C:/Program Files/nodejs/npx.cmd", "/usr/local/bin/npx", "C:\\node\\npx.exe"]) expect(isNpxCommand(c), c).toBe(true);
    for (const c of ["node", "ue-mcp", "pnpx", null]) expect(isNpxCommand(c), String(c)).toBe(false);
  });
});

describe("readNpmAgeGate", () => {
  it("reads min-release-age on npm 11.10 and later", () => {
    expect(readNpmAgeGate(answers("12.0.1", "min-release-age=7\nbefore=null"))).toEqual({ npm: "12.0.1", minReleaseAge: 7, before: null, overridable: true });
    expect(readNpmAgeGate(answers("11.10.0", "min-release-age=3\r\nbefore=null"))?.minReleaseAge).toBe(3);
  });

  it("ignores min-release-age on an npm that does not apply it, but still reports before", () => {
    expect(readNpmAgeGate(answers("10.9.9", "min-release-age=7\nbefore=null"))).toBeNull();
    expect(readNpmAgeGate(answers("10.9.9", "min-release-age=7\nbefore=2026-01-01"))).toEqual({ npm: "10.9.9", minReleaseAge: null, before: "2026-01-01", overridable: false });
  });

  it("is null with no gate, a zero gate, or no npm", () => {
    expect(readNpmAgeGate(answers("12.0.1", "min-release-age=null\nbefore=null"))).toBeNull();
    expect(readNpmAgeGate(answers("12.0.1", "min-release-age=0\nbefore=null"))).toBeNull();
    expect(readNpmAgeGate(() => null)).toBeNull();
  });
});

describe("age-gated launches", () => {
  it("finds npx launches without the override, and --fix adds it without touching anything else", () => {
    const uproject = fixture.makeProject("Game");
    const projectDir = path.dirname(uproject);
    const other = fwd(path.join(fixture.root, "Other", "Other.uproject"));
    const projectMcp = path.join(projectDir, ".mcp.json");
    writeJson(projectMcp, { "ue-mcp": { command: "npx", args: ["-y", "ue-mcp@1.2.3", fwd(uproject), other] }, keep: { command: "x", args: ["y"] } });
    const globalMcp = path.join(fixture.root, ".claude", ".mcp.json");
    writeJson(globalMcp, { "ue-mcp": { command: "npx", args: ["-y", NPX_AGE_GATE_OVERRIDE, "ue-mcp@1.2.3", fwd(uproject)] } });
    const cursor = path.join(projectDir, ".cursor", "mcp.json");
    writeJson(cursor, { "ue-mcp": { command: "node", args: ["C:/ue-mcp/dist/index.js", fwd(uproject)] } });
    const codex = path.join(fixture.root, ".codex", "config.toml");
    fs.mkdirSync(path.dirname(codex), { recursive: true });
    fs.writeFileSync(codex, `[mcp_servers.ue-mcp]\ncommand = "npx"\nargs = ["-y", "ue-mcp@beta", "${fwd(uproject)}"]\nenabled = true\n\n[mcp_servers.other]\ncommand = "x"\n`);

    expect(findAgeGatedLaunches(projectDir, null)).toEqual([]);
    const gated = findAgeGatedLaunches(projectDir, GATE);
    expect(gated.map((g) => [g.config, g.spec, g.lifted]).sort()).toEqual([[projectMcp, "ue-mcp@1.2.3", false], [codex, "ue-mcp@beta", false]].sort());

    const out = formatDoctor(report(GATE, gated));
    expect(out).toContain(`runs \`npx ue-mcp@1.2.3\`, which npm's min-release-age=7 refuses`);
    expect(out).toContain("npx ignores it");
    expect(out).toContain("Run `ue-mcp doctor --fix` to add `--min-release-age=0` to the launch.");

    expect(liftAgeGatedLaunches(projectDir, gated).sort()).toEqual([projectMcp, codex].sort());
    const fixed = JSON.parse(fs.readFileSync(projectMcp, "utf-8")).mcpServers;
    expect(fixed["ue-mcp"]).toEqual({ command: "npx", args: ["-y", NPX_AGE_GATE_OVERRIDE, "ue-mcp@1.2.3", fwd(uproject), other] });
    expect(fixed.keep).toEqual({ command: "x", args: ["y"] });
    const toml = fs.readFileSync(codex, "utf-8");
    expect(toml).toContain(`args = ["-y", "${NPX_AGE_GATE_OVERRIDE}", "ue-mcp@beta", "${fwd(uproject)}"]`);
    expect(toml).toContain("[mcp_servers.other]");
    expect(findAgeGatedLaunches(projectDir, GATE)).toEqual([]);
    expect(liftAgeGateInEntry({ configPath: projectMcp, format: "json" })).toBe(false);
  });

  it("reports a lifted launch an older npm cannot honour, and does not rewrite it", () => {
    const uproject = fixture.makeProject("Old");
    const projectDir = path.dirname(uproject);
    const projectMcp = path.join(projectDir, ".mcp.json");
    writeJson(projectMcp, { "ue-mcp": { command: "npx", args: ["-y", NPX_AGE_GATE_OVERRIDE, "ue-mcp@1.2.3", fwd(uproject)] } });
    const gate: NpmAgeGate = { npm: "10.9.9", minReleaseAge: null, before: "2026-01-01", overridable: false };
    const gated = findAgeGatedLaunches(projectDir, gate);
    expect(gated).toEqual([{ client: "Claude Code (project)", config: projectMcp, spec: "ue-mcp@1.2.3", lifted: true }]);
    const out = formatDoctor(report(gate, gated));
    expect(out).toContain("under npm's before=2026-01-01, which `--min-release-age=0` cannot lift on npm 10.9.9");
    expect(liftAgeGatedLaunches(projectDir, gated)).toEqual([]);
  });
});

describe("repinClientConfigs", () => {
  it("rewrites an entry already on the version when its launch lacks the override", () => {
    const uproject = fixture.makeProject("Repin");
    const mcp = path.join(path.dirname(uproject), ".mcp.json");
    writeJson(mcp, { "ue-mcp": { command: "npx", args: ["-y", "ue-mcp@2.0.0", fwd(uproject)] } });
    expect(repinClientConfigs(uproject, "2.0.0")).toEqual([mcp]);
    expect(readUeMcpEntry({ configPath: mcp, format: "json" }).args).toEqual(["-y", NPX_AGE_GATE_OVERRIDE, "ue-mcp@2.0.0", fwd(uproject)]);
    expect(repinClientConfigs(uproject, "2.0.0")).toEqual([]);
  });
});
