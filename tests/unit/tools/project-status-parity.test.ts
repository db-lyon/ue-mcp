/**
 * project(get_status) is a read composite over its parts (flows, capabilities,
 * parity, holders) reduced into its response, and answers exactly as the
 * handler it replaced. That handler, as it stood, is kept below as the oracle.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, describe, expect, it, vi } from "vitest";
import type { TaskResult } from "@db-lyon/flowkit";

const holders = vi.hoisted(() => ({ value: null as unknown }));
vi.mock("../../../src/editor/project-holders.js", async (importOriginal) => ({
  ...(await importOriginal<typeof import("../../../src/editor/project-holders.js")>()),
  detectProjectHolders: vi.fn(async () => holders.value),
}));

const { projectTool } = await import("../../../src/tools/project.js");
const { ALL_TOOLS } = await import("../../../src/tools.js");
const { checkPluginFreshness } = await import("../../../src/editor/bridge-freshness.js");
const { checkBridgeParity, deployedPlugin } = await import("../../../src/bridge/bridge-parity.js");
const { connectedEditorOf } = await import("../../../src/editor/editor-control.js");
const { detectProjectHolders } = await import("../../../src/editor/project-holders.js");
const { readDeployedBridgeApiVersion } = await import("../../../src/extensions/bridge-api.js");
const { CLIENT_PROTOCOL_VERSION, describeProtocolMismatch } = await import("../../../src/bridge/bridge.js");
const { readLogState, readEngineSnapshot } = await import("../../../src/editor/engine-observer.js");
const { isTargetDiverged } = await import("../../../src/sessions/project-switch.js");
const { ueMcpConfigRejections, describeConfigRejections } = await import("../../../src/config/project.js");
const { toolGraphOf } = await import("../../../src/surface/target-params.js");
const { buildFlowRegistry } = await import("../../../src/flow/registry.js");
const { createLiveTask } = await import("../../../src/flow/live-task.js");
import type { ToolContext, ToolDef } from "../../../src/core/types.js";
import type { FlowContext } from "../../../src/flow/context.js";

/** The get_status handler, verbatim from before the move. */
async function legacyStatus(ctx: ToolContext): Promise<unknown> {
  const flows = ctx.getFlows?.() ?? [];
  const bridgeApiVersion = ctx.project.projectDir
    ? readDeployedBridgeApiVersion(ctx.project.projectDir)
    : null;
  const freshness = checkPluginFreshness(ctx.project.projectPath ?? null);
  const parity = checkBridgeParity(toolGraphOf(ctx), ctx.bridge.capabilities);
  const offlineEngine = ctx.bridge.isConnected ? null : (() => {
    const logState = readLogState(ctx.project.projectPath ?? null);
    const snapshot = readEngineSnapshot(ctx.project.projectPath ?? null);
    if (!logState.logPath && !snapshot) return null;
    return {
      phase: snapshot?.phase ?? logState.phase,
      blocked: logState.blocking || Boolean(snapshot?.modal),
      modal: snapshot?.modal ?? undefined,
      slowTask: snapshot?.slowTask ?? undefined,
      gameThreadStalledSeconds: snapshot?.gameThreadStalledSeconds ?? undefined,
      gameThreadTicking: snapshot?.gameThreadTicking,
      modulesLoaded: snapshot?.modulesLoaded,
      snapshotAgeSeconds: snapshot?.ageSeconds,
      secondsSinceLogWrite: logState.secondsSinceWrite ?? undefined,
      lastLogLine: logState.lastLine ?? undefined,
      recentErrors: logState.errors.length > 0 ? logState.errors : undefined,
      hint: "editor(action='get_engine_state') runs the full out-of-process probe (process table, native dialogs).",
    };
  })();
  const target = ctx.bridge.getTarget();
  const answeringPid = connectedEditorOf(ctx.bridge)?.pid ?? null;
  const h = await detectProjectHolders(ctx.project.projectDir, ctx.project.projectPath, answeringPid);
  return {
    engine: offlineEngine ?? undefined,
    pluginBuildStale: freshness.checked ? freshness.stale : undefined,
    pluginBuildWarning: freshness.stale ? freshness.message : undefined,
    deployedPlugin: deployedPlugin(ctx.bridge.capabilities, parity),
    mode: ctx.bridge.isConnected ? "live" : "disconnected",
    editorConnected: ctx.bridge.isConnected,
    answeringPid: answeringPid ?? undefined,
    editorTarget: {
      projectPath: target.projectPath,
      port: target.port,
      portSource: target.portSource,
    },
    projectEditors: h ? { count: h.count, editors: h.editors } : undefined,
    projectEditorsWarning: h?.warning,
    editorTargetMismatch: isTargetDiverged(ctx.project, target) || undefined,
    configWarnings: (() => {
      const rejected = ueMcpConfigRejections(ctx.project.projectDir);
      return rejected.length > 0 ? describeConfigRejections(rejected) : undefined;
    })(),
    project: ctx.project.isLoaded ? { name: ctx.project.projectName, path: ctx.project.projectPath, contentDir: ctx.project.contentDir, engineAssociation: ctx.project.engineAssociation, config: Object.keys(ctx.project.config).length > 0 ? ctx.project.config : undefined } : null,
    bridgeApiVersion: bridgeApiVersion ?? undefined,
    bridgeProtocol: ctx.bridge.capabilities
      ? {
          plugin: ctx.bridge.capabilities.protocolVersion,
          client: CLIENT_PROTOCOL_VERSION,
          builtAt: ctx.bridge.capabilities.builtAt,
          actionCount: ctx.bridge.capabilities.actionCount,
          mismatch: describeProtocolMismatch(ctx.bridge.capabilities) ?? undefined,
        }
      : undefined,
    flows: flows.length > 0 ? flows : undefined,
    editors: ctx.sessions && ctx.sessions.size > 1
      ? ctx.sessions.list().map((s) => s.info(s === ctx.sessions!.active))
      : undefined,
  };
}

const legacyTool: ToolDef = projectTool.rebuild!({
  ...projectTool.actions,
  get_status: { kind: "handler", effect: "read", description: projectTool.actions.get_status.description, handler: legacyStatus },
});

const dirs: string[] = [];
afterAll(() => { for (const d of dirs) fs.rmSync(d, { recursive: true, force: true }); });

function projectDir(configYaml?: string): string {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-status-"));
  dirs.push(dir);
  fs.writeFileSync(path.join(dir, "Demo.uproject"), JSON.stringify({ EngineAssociation: "5.8" }));
  if (configYaml) fs.writeFileSync(path.join(dir, "ue-mcp.yml"), configYaml);
  return dir;
}

interface Scenario {
  dir?: string;
  connected: boolean;
  capabilities?: Record<string, unknown>;
  flows?: unknown[];
  sessions?: number;
  holders?: unknown;
}

function context(tool: ToolDef, s: Scenario): ToolContext {
  const projectPath = s.dir ? path.join(s.dir, "Demo.uproject") : null;
  const project = {
    projectDir: s.dir ?? null,
    projectPath,
    projectName: s.dir ? "Demo" : null,
    contentDir: s.dir ? path.join(s.dir, "Content") : null,
    engineAssociation: s.dir ? "5.8" : null,
    isLoaded: !!s.dir,
    config: {},
    ensureLoaded: () => {},
  };
  const bridge = {
    isConnected: s.connected,
    capabilities: s.capabilities,
    call: async () => ({ success: true }),
    getTarget: () => ({ projectPath, port: 9000, portSource: "lockfile", verified: true }),
  };
  const list = Array.from({ length: s.sessions ?? 1 }, (_, i) => ({
    name: `e${i}`,
    info: (active: boolean) => ({ name: `e${i}`, active }),
  }));
  const sessions = s.sessions ? { size: list.length, list: () => list, active: list[0] } : undefined;
  return {
    project,
    bridge,
    sessions,
    getFlows: () => s.flows ?? [],
    getToolGraph: () => [tool, ...ALL_TOOLS.filter((t) => t.name !== tool.name)],
  } as unknown as ToolContext;
}

function settled(result: TaskResult): Record<string, unknown> {
  return { success: result.success, data: result.data, error: result.error?.message };
}

async function answer(tool: ToolDef, s: Scenario) {
  holders.value = s.holders ?? null;
  const live = await createLiveTask({ registry: buildFlowRegistry([tool]) }, context(tool, s) as FlowContext, "project.get_status", {});
  const liveResult = JSON.stringify(settled(await live.run()));
  const direct = JSON.stringify(await projectTool.rebuild!(tool.actions).handler(context(tool, s), { action: "get_status" }));
  return { liveResult, direct };
}

const CAPS = { protocolVersion: 1, builtAt: "2026-09-01T00:00:00Z", actions: ["list_assets"], actionCount: 1, pid: 42, legacy: false };

/** Fields each scenario exists to exercise, so a scenario cannot pass by answering nothing. */
const SHOWS: Record<string, string[]> = {
  "a connected editor missing advertised methods": ["deployedPlugin", "bridgeProtocol", "answeringPid"],
  "flows, several sessions and several holders": ["projectEditorsWarning", "\"editors\"", "beacon"],
};

describe("project(get_status), a read composite", () => {
  it("is a flow action now", () => {
    expect(projectTool.actions.get_status.kind).toBe("flow");
  });

  const scenarios: Record<string, () => Scenario> = {
    "no project, no editor": () => ({ connected: false }),
    "a project with no editor": () => ({ dir: projectDir(), connected: false }),
    "a connected editor missing advertised methods": () => ({ dir: projectDir(), connected: true, capabilities: CAPS }),
    "flows, several sessions and several holders": () => ({
      dir: projectDir(),
      connected: true,
      capabilities: CAPS,
      flows: [{ name: "beacon", availability: "editor" }],
      sessions: 2,
      holders: { count: 2, editors: [{ pid: 42 }, { pid: 43 }], warning: "two editors hold this project" },
    }),
    "a project with its own ue-mcp.yml": () => ({ dir: projectDir("ue-mcp:\n  bridge:\n    port: nope\n"), connected: false }),
  };

  for (const [name, make] of Object.entries(scenarios)) {
    it(`answers the same bytes: ${name}`, async () => {
      const s = make();
      const before = await answer(legacyTool, s);
      const after = await answer(projectTool, s);
      expect(after).toEqual(before);
      expect(JSON.parse(after.liveResult).success).toBe(true);
      for (const key of SHOWS[name] ?? []) expect(after.liveResult, key).toContain(key);
    });
  }
});
