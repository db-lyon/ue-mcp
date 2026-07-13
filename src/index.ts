#!/usr/bin/env node
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { EditorBridge } from "./bridge.js";
import { ProjectContext } from "./project.js";
import { attach, attachSummary } from "./deployer.js";
import { SERVER_INSTRUCTIONS, SERVER_INSTRUCTIONS_LEAN, SERVER_INSTRUCTIONS_MICRO } from "./instructions.js";
import { resolveContextStrategy, applyLeanContext, buildMicroGateway } from "./lean-context.js";
import { isDirectiveResponse, type ToolDef, type ToolContext, type PluginInfo, type ElicitFn } from "./types.js";
import { McpError, ErrorCode } from "./errors.js";
import { info, warn } from "./log.js";
import { startVersionCheck, consumeUpgradeNotice } from "./version-check.js";
import { buildFlowRegistry } from "./flow/registry.js";
import { GuardedBridge } from "./flow/guarded-bridge.js";
import {
  GUARD_TASK_NAME,
  guardTaskRegistered,
  makeResolveExistingFile,
  makeSourceControlGuard,
} from "./flow/source-control-guard.js";
import { loadFlowConfig } from "./flow/loader.js";
import { createFlowTool } from "./flow/flow-tool.js";
import { startFlowHttpServer } from "./flow/http-server.js";
import type { FlowContext } from "./flow/context.js";
import type { FlowConfig, PluginEntry } from "./flow/schema.js";
import { loadPlugins, type PluginRecord } from "./plugin/loader.js";
import * as fs from "node:fs";
import * as path from "node:path";
import yaml from "js-yaml";

import { ALL_TOOLS } from "./tools.js";
import { enrichToolsWithEpicCatalog, type EpicCatalog } from "./epic-enrich.js";
import { saveCatalogCache, loadCatalogCache, loadBakedCatalog } from "./epic-cache.js";

type TextBlock = { type: "text"; text: string };

function withUpgradeNotice(content: TextBlock[]): TextBlock[] {
  const notice = consumeUpgradeNotice();
  return notice ? [{ type: "text" as const, text: notice }, ...content] : content;
}

async function main() {
  const bridge = new EditorBridge();
  const project = new ProjectContext();

  // Kick off the npm registry check in the background; the next tool response
  // injects the notice if a newer version is published.
  const { createRequire } = await import("node:module");
  const require = createRequire(import.meta.url);
  const pkg = require("../package.json") as { version: string };
  startVersionCheck(pkg.version);

  // ── Project init ─────────────────────────────────────────────────
  // Moved ahead of tool registration so plugin resolution can walk the
  // project's node_modules.
  const projectArg = process.argv.find((a) => !a.startsWith("-") && a !== process.argv[0] && a !== process.argv[1]);

  if (projectArg) {
    try {
      project.setProject(projectArg);
      console.error(`[ue-mcp] Project loaded: ${project.projectName} (engine ${project.engineAssociation ?? "unknown"})`);

      // #492: pass the .uproject path to the bridge so it can read the
      // per-project port lockfile when connecting (lets multiple editors
      // coexist on adjacent ports).
      bridge.projectPathForLockfile = project.projectPath;

      // Non-destructive attach — never overwrites local bridge source.
      // Source deployment is reserved for `ue-mcp init` / `ue-mcp deploy`.
      const result = attach(project);
      console.error(`[ue-mcp] ${attachSummary(result)}`);
    } catch (e) {
      console.error(`[ue-mcp] Failed to initialize project: ${e instanceof Error ? e.message : e}`);
    }
  }

  // ── Plugins ──────────────────────────────────────────────────────
  // Read the user's `plugins:` entries from ue-mcp.yml (best-effort — a
  // missing or invalid file means zero plugins, never a fatal error). Then
  // resolve, validate, and inject into target categories BEFORE the flow
  // registry is built so plugin tasks register cleanly.
  const configDir = project.projectDir ?? undefined;
  const pluginEntries = readPluginsEntries(configDir);
  const pluginLoad = await loadPlugins(ALL_TOOLS, pluginEntries, configDir, pkg.version);
  const activeTools = pluginLoad.tools;
  const pluginRecords = pluginLoad.records;

  // ── Epic 5.8 native toolset surfacing (best-effort, startup) ─────
  // If the editor bridge is reachable now, pull Epic's live toolset catalog and
  // inject each tool as a first-class action into the matching ue-mcp category
  // (GAS tools into `gas`, Niagara into `niagara`, etc.). This must run before
  // the flow registry and MCP tool registration below so the injected actions
  // are dispatchable and advertised. When the editor is not up yet, the `epic`
  // gateway still works; a server restart picks up enrichment. (Re-enrichment on
  // late connect would require tools/list_changed and is a follow-up.)
  const nativeCfg = project.config.nativeTools ?? {};
  const nativeEnabled = nativeCfg.enabled !== false; // on by default (opt-out)
  if (!nativeEnabled) {
    console.error("[ue-mcp] Native Epic tools disabled via ue-mcp.yml (nativeTools.enabled=false); epic gateway still available");
  } else {
    try {
      // Source priority: live editor (most current, refreshes the cache) ->
      // project cache (last-seen) -> baked snapshot shipped with the package
      // (deterministic default so the surface appears on first cold startup and
      // matches the generated docs). First available wins.
      let catalog: EpicCatalog | null = null;
      let source = "";
      if (!bridge.isConnected) {
        await bridge.connect(2000).catch(() => {});
      }
      if (bridge.isConnected) {
        catalog = (await bridge.call("epic_list_toolsets", { includeSchemas: true }, 20000)) as EpicCatalog;
        if (catalog?.toolsets?.length) {
          saveCatalogCache(configDir, catalog, project.engineAssociation);
          source = "live editor";
        }
      }
      if (!catalog?.toolsets?.length) {
        catalog = loadCatalogCache(configDir);
        if (catalog?.toolsets?.length) source = "project cache";
      }
      if (!catalog?.toolsets?.length) {
        catalog = loadBakedCatalog();
        if (catalog?.toolsets?.length) source = "baked snapshot";
      }
      if (catalog?.toolsets?.length) {
        const enriched = enrichToolsWithEpicCatalog(activeTools, catalog, {
          excludeCategories: nativeCfg.exclude,
        });
        if (enriched.injected > 0) {
          const summary = Object.entries(enriched.byCategory).map(([c, n]) => `${c}:${n}`).join(", ");
          console.error(`[ue-mcp] Epic 5.8 toolsets (${source}): surfaced ${enriched.injected} tools (${summary})`);
        }
      }
    } catch (e) {
      console.error(`[ue-mcp] Epic toolset enrichment skipped: ${e instanceof Error ? e.message : e}`);
    }
  }

  // ── Context-seeding strategy (full | lean | micro) ───────────────
  // Applied AFTER plugin + Epic enrichment so lean/micro discovery covers the
  // injected actions too, and BEFORE the flow registry + MCP registration so
  // the gateway / catalog / describe surfaces are dispatchable and advertised.
  //   full  - every category tool advertised with its full action catalog
  //   lean  - trimmed category tools + a `catalog` discovery tool (names stay
  //           visible, descriptions/params on demand)
  //   micro - one `tools` gateway (list_categories / describe / call) fronts
  //           everything; smallest possible seed
  const contextStrategy = resolveContextStrategy(project.config.context?.strategy);
  const disabled = new Set(project.config.disable ?? []);
  const enabledActive = activeTools.filter((t) => !disabled.has(t.name));

  let advertisedTools: ToolDef[];
  let registryTools: ToolDef[];
  if (contextStrategy === "micro") {
    const gateway = buildMicroGateway(enabledActive);
    advertisedTools = [gateway];
    // Keep every category task in the registry so flows still resolve.
    registryTools = [gateway, ...activeTools];
  } else if (contextStrategy === "lean") {
    const leaned = applyLeanContext(activeTools);
    advertisedTools = leaned.filter((t) => !disabled.has(t.name));
    registryTools = leaned;
  } else {
    advertisedTools = enabledActive;
    registryTools = activeTools;
  }
  if (contextStrategy !== "full") {
    console.error(`[ue-mcp] Context strategy: ${contextStrategy}`);
  }

  // Lazy flow accessor — reads ue-mcp.yml fresh each call so agents see
  // edits without a server restart. project(get_status) uses this so the
  // first call agents make in any session reveals the registered flows.
  const getFlows = (): Array<{ name: string; description?: string }> => {
    try {
      const cfg = loadFlowConfig(activeTools, configDir, {
        tasks: pluginLoad.taskDefs,
        flows: pluginLoad.flowDefs,
      }).config;
      return Object.entries(cfg.flows).map(([name, def]) => ({
        name,
        description: (def as { description?: string }).description,
      }));
    } catch {
      return [];
    }
  };

  const getPlugins = (): PluginInfo[] => pluginRecords.map((r) => toPluginInfo(r, project));

  // Elicitation is only meaningful once the client has advertised support
  // during initialize. We lazily probe at call time so the function is bound
  // to whatever the live capabilities are, not a stale snapshot.
  const buildElicit = (mcp: McpServer): ElicitFn | undefined => {
    return async (params) => {
      const caps = mcp.server.getClientCapabilities();
      if (!caps?.elicitation) {
        // Surface a JSON-RPC-style error shape so callers can distinguish
        // "user declined" from "client has no UI for this".
        throw new McpError(
          ErrorCode.UNKNOWN_ACTION,
          "Connected MCP client did not advertise the `elicitation` capability — cannot obtain a deterministic user approval. Upgrade your client (Claude Code >= 2.1.76) or run the action from a client that supports MCP elicitation.",
        );
      }
      const result = await mcp.server.elicitInput(params);
      return result as Awaited<ReturnType<ElicitFn>>;
    };
  };

  // Wrap the raw bridge so every mutating action can be gated by a source-
  // control guard. The raw `bridge` stays in scope for connection lifecycle
  // (connect / reconnect / lockfile); the guarded wrapper is what tools and
  // tasks see. With no guard installed it is a pass-through.
  const guardedBridge = new GuardedBridge(bridge, makeResolveExistingFile(project));
  const ctx: ToolContext = { bridge: guardedBridge, project, getFlows, getPlugins };

  // ── Flow engine: task registry ──────────────────────────────────
  const registry = buildFlowRegistry(registryTools);
  for (const { name, ctor } of pluginLoad.taskRegistrations) {
    registry.register(name, ctor);
  }
  for (const { classPath, ctor } of pluginLoad.classPathRegistrations) {
    registry.registerClassPath(classPath, ctor);
  }
  const taskCount = registry.listRegistered().length;

  // Install the write guard iff a plugin registered the hook task. The guard
  // task runs with the RAW bridge in its context so it cannot recurse through
  // the gate. See flow/source-control-guard.ts.
  if (guardTaskRegistered(registry)) {
    guardedBridge.setGuard(makeSourceControlGuard(registry, bridge, ctx));
    info("guard", `source-control write guard active (${GUARD_TASK_NAME})`);
  }

  // ── Plugin knowledge → server instructions ──────────────────────
  // Attach per-category markdown to the AI-facing docs. Sized to the same
  // budget as SERVER_INSTRUCTIONS itself; deeper plugin docs remain
  // readable on demand via the file-reading surface.
  const knowledgeBlock = buildKnowledgeBlock(pluginLoad.knowledgeByCategory);
  const baseInstructions = contextStrategy === "micro"
    ? SERVER_INSTRUCTIONS_MICRO
    : contextStrategy === "lean"
      ? SERVER_INSTRUCTIONS_LEAN
      : SERVER_INSTRUCTIONS;
  const serverInstructions = knowledgeBlock
    ? `${baseInstructions}\n\n═══ PLUGIN KNOWLEDGE ═══\n${knowledgeBlock}`
    : baseInstructions;

  const server = new McpServer({
    name: "ue-mcp",
    version: "0.6.4",
  }, {
    instructions: serverInstructions,
  });

  ctx.elicit = buildElicit(server);

  const tools = advertisedTools;

  // ── Register category tools — dispatched through the task registry ──
  for (const tool of tools) {
    const shape: Record<string, z.ZodType> = {};
    for (const [key, schema] of Object.entries(tool.schema)) {
      shape[key] = schema;
    }

    server.tool(tool.name, tool.description, shape, async (params) => {
      const action = params.action as string;
      const taskName = `${tool.name}.${action}`;
      const { action: _, ...taskParams } = params;
      const flowCtx: FlowContext = { bridge: guardedBridge, project, getFlows, getPlugins, elicit: ctx.elicit };

      try {
        const task = await registry.create(taskName, flowCtx, taskParams);
        const result = await task.run();

        if (!result.success) {
          const msg = result.error?.message ?? `Task ${taskName} failed`;
          return {
            content: withUpgradeNotice([{ type: "text" as const, text: `Error [TASK_FAILED]: ${msg}` }]),
            isError: true,
          };
        }

        const stringify = (v: unknown) =>
          typeof v === "string" ? v : JSON.stringify(v, null, 2);

        // Preserve directive responses (execute_python workaround tracking).
        // Emit three blocks: (1) prose directive, (2) machine-readable JSON
        // so clients that strip prose still see the intent, (3) the actual
        // tool result. Block 2 is tagged with MACHINE_DIRECTIVE and a stable
        // JSON envelope.
        if (result.data?.__directive) {
          const blocks: Array<{ type: "text"; text: string }> = [
            { type: "text" as const, text: result.data.directive as string },
          ];
          if (result.data.machine) {
            blocks.push({
              type: "text" as const,
              text: "MACHINE_DIRECTIVE=" + JSON.stringify(result.data.machine),
            });
          }
          blocks.push({ type: "text" as const, text: stringify(result.data.result) });
          return { content: withUpgradeNotice(blocks) };
        }

        return {
          content: withUpgradeNotice([{ type: "text" as const, text: stringify(result.data) }]),
        };
      } catch (e) {
        const msg = e instanceof Error ? e.message : String(e);
        const code = e instanceof McpError ? e.code : "UNKNOWN";
        return {
          content: withUpgradeNotice([{ type: "text" as const, text: `Error [${code}]: ${msg}` }]),
          isError: true,
        };
      }
    });
  }

  // ── Load ue-mcp.yml and register flow tool ──────────────────────
  // Log initial load
  const initialLoad = loadFlowConfig(activeTools, configDir, {
    tasks: pluginLoad.taskDefs,
    flows: pluginLoad.flowDefs,
  });
  console.error(`[ue-mcp] ue-mcp.yml loaded — ${Object.keys(initialLoad.config.flows).length} flow(s), ${Object.keys(initialLoad.config.tasks).length} custom task(s)`);

  // Config is reloaded on every flow call — edit ue-mcp.yml without restarting
  const reloadConfig = (): FlowConfig => loadFlowConfig(activeTools, configDir, {
    tasks: pluginLoad.taskDefs,
    flows: pluginLoad.flowDefs,
  }).config;

  const flowTool = createFlowTool(registry, reloadConfig);
  const flowShape: Record<string, z.ZodType> = {};
  for (const [key, schema] of Object.entries(flowTool.schema)) {
    flowShape[key] = schema;
  }
  server.tool(flowTool.name, flowTool.description, flowShape, async (params) => {
    try {
      const result = await flowTool.handler(ctx, params);
      const text = typeof result === "string" ? result : JSON.stringify(result, null, 2);
      return { content: withUpgradeNotice([{ type: "text" as const, text }]) };
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      return { content: withUpgradeNotice([{ type: "text" as const, text: `Error: ${msg}` }]), isError: true };
    }
  });

  // ── Optional HTTP surface for flow.run (#144) ───────────────────
  // Off by default; opt-in via ue-mcp.yml `ue-mcp.http: { enabled: true, port: 7723 }`.
  // Binds to 127.0.0.1 only — do NOT expose to the network without adding auth.
  if (project.config.http?.enabled) {
    try {
      startFlowHttpServer(flowTool, ctx, {
        port: project.config.http.port,
        host: project.config.http.host,
      });
    } catch (e) {
      console.error(`[ue-mcp] Failed to start HTTP server: ${e instanceof Error ? e.message : e}`);
    }
  }

  // ── Bridge connection ────────────────────────────────────────────
  try {
    await bridge.connect();
    info("bridge", "editor bridge connected - live mode active");
  } catch (e) {
    info("bridge", "editor not reachable - will retry in background", e);
  }
  bridge.startReconnecting();

  if (disabled.size > 0) {
    console.error(`[ue-mcp] Disabled categories: ${[...disabled].join(", ")}`);
  }
  const activePluginCount = pluginRecords.filter((r) => r.status === "active").length;
  const pluginNote = pluginRecords.length > 0
    ? `, ${activePluginCount}/${pluginRecords.length} plugin(s)`
    : "";
  console.error(`[ue-mcp] Registered ${tools.length + 1} tools, ${taskCount} tasks (flow engine)${pluginNote}`);

  const transport = new StdioServerTransport();
  await server.connect(transport);
}

/**
 * Best-effort read of the `plugins:` array from ue-mcp.yml. Returns [] when
 * the file is missing, unreadable, or malformed — plugin failures are loud at
 * load time, not fatal here.
 */
function readPluginsEntries(configDir: string | undefined): PluginEntry[] {
  if (!configDir) return [];
  const configPath = path.join(configDir, "ue-mcp.yml");
  if (!fs.existsSync(configPath)) return [];
  try {
    const raw = yaml.load(fs.readFileSync(configPath, "utf-8")) as { plugins?: unknown } | null;
    if (!raw || !Array.isArray(raw.plugins)) return [];
    const out: PluginEntry[] = [];
    for (const entry of raw.plugins) {
      if (entry && typeof entry === "object" && typeof (entry as { name?: unknown }).name === "string") {
        const e = entry as { name: string; version?: unknown };
        out.push({
          name: e.name,
          version: typeof e.version === "string" ? e.version : undefined,
        });
      }
    }
    return out;
  } catch (e) {
    warn("plugin", `failed to parse plugins: from ue-mcp.yml - ${(e as Error).message}`);
    return [];
  }
}

function buildKnowledgeBlock(knowledgeByCategory: Record<string, string[]>): string {
  const lines: string[] = [];
  for (const [category, blobs] of Object.entries(knowledgeByCategory)) {
    if (blobs.length === 0) continue;
    lines.push(`── ${category} ──`);
    for (const blob of blobs) lines.push(blob.trim());
    lines.push("");
  }
  return lines.join("\n").trim();
}

function toPluginInfo(rec: PluginRecord, project: ProjectContext): PluginInfo {
  const uePluginPresent = rec.uePluginDependency
    ? isUePluginEnabled(project, rec.uePluginDependency)
    : undefined;
  return {
    name: rec.name,
    version: rec.version,
    actionPrefix: rec.actionPrefix,
    status: rec.status,
    statusReason: rec.statusReason,
    minServerVersion: rec.minServerVersion,
    uePluginDependency: rec.uePluginDependency,
    uePluginPresent,
    injected: rec.injected,
    provided: rec.provided,
    knowledge: rec.knowledge,
    flows: rec.flows,
    tasks: rec.tasks,
    pkgDir: rec.pkgDir,
    manifestPath: rec.manifestPath,
  };
}

function isUePluginEnabled(project: ProjectContext, name: string): boolean | undefined {
  if (!project.projectPath) return undefined;
  try {
    const raw = JSON.parse(fs.readFileSync(project.projectPath, "utf-8")) as {
      Plugins?: Array<{ Name?: string; Enabled?: boolean }>;
    };
    if (!raw.Plugins) return false;
    const entry = raw.Plugins.find((p) => p.Name === name);
    if (!entry) return false;
    return entry.Enabled !== false;
  } catch {
    return undefined;
  }
}

// Route subcommands
const subcmd = process.argv[2];
if (subcmd === "init") {
  process.argv.splice(2, 1);
  import("./init.js");
} else if (subcmd === "update") {
  process.argv.splice(2, 1);
  import("./update.js");
} else if (subcmd === "doctor") {
  process.argv.splice(2, 1);
  import("./doctor.js").then((m) => m.runDoctorCli());
} else if (subcmd === "deploy") {
  process.argv.splice(2, 1);
  import("./deploy-cli.js");
} else if (subcmd === "hook") {
  import("./hook-handler.js");
} else if (subcmd === "uninstall-hooks") {
  process.argv.splice(2, 1);
  import("./uninstall-hooks.js");
} else if (subcmd === "auth") {
  process.argv.splice(2, 1);
  // #620: invoked via the index.js bin, argv[1] is index.js so auth-cli's
  // own "am I the entry point" guard never fires. Call the export directly.
  import("./auth-cli.js").then((m) => m.runFeedbackAuthStep()).catch((e) => {
    console.error(`[ue-mcp] auth failed: ${e instanceof Error ? e.message : e}`);
    process.exit(1);
  });
} else if (subcmd === "feedback") {
  process.argv.splice(2, 1);
  import("./feedback-cli.js");
} else if (subcmd === "resolve") {
  import("./resolve.js");
} else if (subcmd === "build") {
  process.argv.splice(2, 1);
  import("./build-cli.js");
} else if (subcmd === "plugin") {
  process.argv.splice(2, 1);
  import("./plugin-cli.js");
} else if (subcmd === "context") {
  process.argv.splice(2, 1);
  import("./context-cli.js");
} else if (subcmd === "version" || subcmd === "--version" || subcmd === "-v") {
  const { createRequire } = await import("node:module");
  const require = createRequire(import.meta.url);
  const pkg = require("../package.json");
  console.log(pkg.version);
} else {
  main().catch((e) => {
    console.error(`[ue-mcp] Fatal error: ${e}`);
    process.exit(1);
  });
}
