#!/usr/bin/env tsx
/**
 * Run one of the repo's own flows, declared in tests/ue_mcp/ue-mcp.yml,
 * through the shipped flow tool against the editor that has tests/ue_mcp open.
 *
 *     tsx scripts/repo-flow.ts <flowName>
 *
 * The bridge is the repo harness's (scripts/bridge-target.mjs), which refuses
 * any editor that is not tests/ue_mcp, and it is used raw: the recordings these
 * flows make feed the server's own surface, so reading them back through that
 * surface would be circular. Task class paths in the config resolve from the
 * repository root, which is where npm runs this.
 */
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { ALL_TOOLS } from "../src/tools.js";
import { loadFlowConfig } from "../src/flow/loader.js";
import { buildFlowRegistry } from "../src/flow/registry.js";
import { createFlowTool } from "../src/flow/flow-tool.js";
import { ProjectContext } from "../src/config/project.js";
import type { FlowConfig } from "../src/flow/schema.js";
import type { IBridge } from "../src/bridge/bridge.js";
import type { ToolContext } from "../src/core/types.js";
import { connectTestBridge, TEST_PROJECT_DIR, TEST_PROJECT_UPROJECT } from "./bridge-target.mjs";

interface TestBridge {
  call: (method: string, params?: Record<string, unknown>, timeoutMs?: number) => Promise<unknown>;
  close: () => void;
  projectDir: string;
}

export interface RepoFlowResult {
  success: boolean;
  summary?: string;
  steps?: Array<{ name: string; data?: Record<string, unknown>; error?: { message: string } }>;
}

/** The harness connection as the IBridge a flow step calls. */
function asBridge(bridge: TestBridge): IBridge {
  return {
    isConnected: true,
    capabilities: null,
    call: (method, params, timeoutMs) => bridge.call(method, params, timeoutMs),
    connect: async () => {},
    retargetProject: () => { throw new Error("a repo flow never retargets its editor"); },
    getTarget: () => ({ projectPath: TEST_PROJECT_UPROJECT, port: 0, portSource: "lockfile", verified: true }),
  } as IBridge;
}

/** Run `flowName` from tests/ue_mcp/ue-mcp.yml over `bridge`. `configure` edits the loaded config first. */
export async function runRepoFlow(
  flowName: string,
  bridge: IBridge,
  configure?: (config: FlowConfig) => void,
): Promise<RepoFlowResult> {
  const config = loadFlowConfig(ALL_TOOLS, TEST_PROJECT_DIR).config;
  if (!config.flows[flowName]) {
    throw new Error(`tests/ue_mcp/ue-mcp.yml declares no flow '${flowName}'. It has: ${Object.keys(config.flows).join(", ")}`);
  }
  configure?.(config);
  const project = new ProjectContext();
  project.setProject(TEST_PROJECT_UPROJECT);
  const ctx = { bridge, project, getToolGraph: () => ALL_TOOLS } as unknown as ToolContext;
  const flow = createFlowTool(buildFlowRegistry(ALL_TOOLS), () => config);
  return await flow.handler(ctx, { action: "run", flowName }) as RepoFlowResult;
}

async function main(flowName: string): Promise<boolean> {
  const bridge = (await connectTestBridge({ log: console.log })) as unknown as TestBridge;
  try {
    console.log(`target confirmed: ${bridge.projectDir}`);
    const result = await runRepoFlow(flowName, asBridge(bridge));
    if (result.summary) console.log(result.summary);
    const last = result.steps?.[result.steps.length - 1];
    if (result.success && last?.data) console.log(JSON.stringify(last.data));
    return result.success;
  } finally {
    bridge.close();
  }
}

const isMain = process.argv[1] !== undefined
  && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url);

if (isMain) {
  const flowName = process.argv[2];
  if (!flowName) {
    console.error("usage: tsx scripts/repo-flow.ts <flowName>");
    process.exit(2);
  }
  main(flowName).then(
    (ok) => process.exit(ok ? 0 : 1),
    (e: unknown) => {
      console.error(`[repo-flow] ${e instanceof Error ? e.message : String(e)}`);
      process.exit(1);
    },
  );
}
