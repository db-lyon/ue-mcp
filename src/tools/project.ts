import { checkPluginFreshness } from "../plugin-freshness.js";
import * as fs from "node:fs";
import * as path from "node:path";
import { z } from "zod";
import { categoryTool, bp, type ToolContext, type ToolDef } from "../types.js";
import { deploy, deploySummary, attach, attachSummary } from "../deployer.js";
import { selectEngine } from "../engine-root.js";
import { collapsingEnvWarnings } from "../session-env.js";
import { buildProject, startEditor, isBridgeReachable } from "../editor-control.js";
import { resolveConfigPath, findIniFiles, parseIni, buildTagTree } from "../config-parser.js";
import { parseHeader, collectFiles, findSourceRoots, resolveModuleDir } from "../cpp-parser.js";
import { loadEngineIndex, type EngineIndex } from "../engine-index.js";
import {
  verifySymbols,
  suggestBuildDeps,
  findExampleUsage,
  lintHeader,
  findBuildCs,
} from "../cpp-correctness.js";
import {
  classHierarchy,
  findCallees,
  findCallers,
  findReferences,
  symbolContext,
} from "../engine-analysis.js";
import { readDeployedBridgeApiVersion } from "../plugin/bridge-api.js";
import { CLIENT_PROTOCOL_VERSION, describeProtocolMismatch } from "../bridge.js";
import { searchTools, searchToolGraph, type ToolSearchHit } from "../tool-search.js";
import { actionSchema, resolveActionRef, suggestActions } from "../action-schema.js";
import { availabilityReport } from "../offline.js";
import { inspectInstall } from "../install-check.js";
import { listContent } from "../content-index.js";
import { getWorkarounds } from "../workaround-tracker.js";
import { readLogState, readEngineSnapshot } from "../engine-observer.js";
import { switchProject, isTargetDiverged } from "../project-switch.js";
import { ueMcpConfigRejections, describeConfigRejections } from "../project.js";
import { CURSOR_PARAM, paged } from "../pagination.js";

/**
 * The environment variables flattening every registered editor into one, right
 * now.
 *
 * `collapsingEnvWarnings` is answered from the session set, and the session set
 * is not fixed. index.ts computes it once over the projects named on the
 * command line, where the list is empty by design at one editor - and
 * `add_editor` is the only runtime path to a second one, so a server that
 * started single and grew had asked the question exactly once, at the moment
 * the answer was guaranteed to be "nothing to say".
 *
 * Asking again wherever the set is read or changed is the fix. Undefined
 * rather than an empty array when there is nothing to report, so a
 * single-editor response is byte-identical to what it always was.
 */
export function envWarningsFor(ctx: ToolContext): string[] | undefined {
  if (!ctx.sessions) return undefined;
  const lines = collapsingEnvWarnings(ctx.sessions.list().map((s) => s.name));
  return lines.length > 0 ? lines : undefined;
}

/**
 * The engine tree the engine-source readers work against.
 *
 * These used to ask the .uproject's EngineAssociation and nothing else, so a
 * project whose engine is a source build beside it got "Could not resolve
 * engine install path" while `Build.bat` sat one directory up (#962). They now
 * go through the same resolver `build_project` uses, and the failure it throws
 * names every path probed instead of one env var.
 */
function requireEngineRoot(ctx: ToolContext): string {
  ctx.project.ensureLoaded();
  const engineRoot = selectEngine(ctx.project.engineLookup(), "engineRoot").engineRoot;
  if (!engineRoot) throw new Error("Could not resolve engine install path");
  return engineRoot;
}

/**
 * The engine symbol index for this project's engine.
 *
 * Cached on disk per engine, so the first call on a machine pays a scan of
 * roughly 31,000 headers and every call after it is a file read. The scan is
 * slow only because of first-touch I/O, so the actions that use this declare a
 * long timeout rather than pretending it is instant.
 */
function requireEngineIndex(ctx: ToolContext, refresh = false): {
  index: EngineIndex;
  source: string;
  cacheFile: string | null;
  buildMs?: number;
} {
  const engineRoot = requireEngineRoot(ctx);
  const loaded = loadEngineIndex(engineRoot, { refresh });
  return {
    index: loaded.index,
    source: loaded.source,
    cacheFile: loaded.cacheFile,
    buildMs: loaded.buildMs,
  };
}

/** Read a names[] parameter that also accepts a single string. */
function nameList(value: unknown, field: string): string[] {
  const list = typeof value === "string"
    ? value.split(",").map((v) => v.trim()).filter(Boolean)
    : Array.isArray(value)
      ? value.filter((v): v is string => typeof v === "string" && v.trim() !== "")
      : [];
  if (list.length === 0) throw new Error(`Missing '${field}'. Pass an array of symbol names, or a comma-separated string.`);
  if (list.length > 200) throw new Error(`'${field}' is capped at 200 names per call (got ${list.length}).`);
  return list;
}

/**
 * Resolve a module name to its Source/<Module> directory, searching the project
 * Source roots AND every plugin under Plugins/<*>/Source/ (which findSourceRoots
 * does not cover). Empty moduleName returns the project's first module dir.
 * (#543: plugin-module source authoring.)
 */
function resolveSourceModuleDir(projectDir: string, projectName: string | null, moduleName: string): string | null {
  const roots = [...findSourceRoots(projectDir, projectName)];
  // Add each plugin's Source dir as a search root.
  const pluginsDir = path.join(projectDir, "Plugins");
  if (fs.existsSync(pluginsDir)) {
    const walk = (dir: string, depth: number) => {
      if (depth > 3) return;
      for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        if (!entry.isDirectory()) continue;
        const full = path.join(dir, entry.name);
        if (entry.name === "Source") roots.push(full);
        else walk(full, depth + 1);
      }
    };
    try { walk(pluginsDir, 0); } catch { /* ignore unreadable plugin dirs */ }
  }
  for (const root of roots) {
    if (!moduleName) {
      // First module dir that holds a Build.cs.
      if (!fs.existsSync(root)) continue;
      for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
        if (entry.isDirectory() && fs.existsSync(path.join(root, entry.name, `${entry.name}.Build.cs`))) {
          return path.join(root, entry.name);
        }
      }
    } else {
      const modDir = path.join(root, moduleName);
      if (fs.existsSync(path.join(modDir, `${moduleName}.Build.cs`))) return modDir;
    }
  }
  return null;
}

export const projectTool: ToolDef = categoryTool(
  "project",
  "Project status and editor connection: get_status (is the editor connected?), set_project (switch/redirect the bridge to another .uproject), get_info. Also config INI files, module load state, and C++ source inspection. Call project(get_status) first in any session.",
  {
    get_status: {
      description: "Check server mode and editor connection. Also reports pluginBuildStale when the compiled bridge is older than its source, which is the real cause of 'Unknown method' on handlers that do exist. Params: none (#785)",
      handler: async (ctx) => {
        const flows = ctx.getFlows?.() ?? [];
        const bridgeApiVersion = ctx.project.projectDir
          ? readDeployedBridgeApiVersion(ctx.project.projectDir)
          : null;
        // #785: surface staleness on the first call agents make, so an
        // "Unknown method" later is read as a stale build rather than a
        // missing feature.
        const freshness = checkPluginFreshness(ctx.project.projectPath ?? null);

        // "disconnected" on its own has never been actionable: it is the same
        // word for "no editor", "editor still loading shaders", and "editor
        // blocked on a dialog nobody can see". The engine's own log and the
        // plugin's status snapshot are plain file reads, so read them whenever
        // there is no live bridge to ask. The full probe (process table,
        // native dialog windows) costs seconds and lives in
        // editor(get_engine_state).
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
            // False during startup: there is no engine loop to stall yet, so
            // the stall figure above is deliberately absent rather than zero.
            gameThreadTicking: snapshot?.gameThreadTicking,
            modulesLoaded: snapshot?.modulesLoaded,
            snapshotAgeSeconds: snapshot?.ageSeconds,
            secondsSinceLogWrite: logState.secondsSinceWrite ?? undefined,
            lastLogLine: logState.lastLine ?? undefined,
            recentErrors: logState.errors.length > 0 ? logState.errors : undefined,
            hint: "editor(action='get_engine_state') runs the full out-of-process probe (process table, native dialogs).",
          };
        })();

        // Which editor this connection belongs to (#818). "connected" on its
        // own never said whose editor answered, so a bridge left on another
        // project read as a healthy session.
        const target = ctx.bridge.getTarget();

        return {
          engine: offlineEngine ?? undefined,
          pluginBuildStale: freshness.checked ? freshness.stale : undefined,
          pluginBuildWarning: freshness.stale ? freshness.message : undefined,
          mode: ctx.bridge.isConnected ? "live" : "disconnected",
          editorConnected: ctx.bridge.isConnected,
          editorTarget: {
            projectPath: target.projectPath,
            port: target.port,
            portSource: target.portSource,
          },
          // Bridge calls and path resolution would be hitting different
          // projects. Unreachable through set_project, reported so it can
          // never be silent again.
          editorTargetMismatch: isTargetDiverged(ctx.project, target) || undefined,
          // D3: a `ue-mcp:` key that failed validation is dropped and the rest
          // of the block still applies. Reported here because the alternative
          // signal is a warn() on stderr, which MCP clients write to a log
          // nobody opens - and the setting most often lost this way was
          // bridge.port, which put the client and the editor on different ports.
          configWarnings: (() => {
            const rejected = ueMcpConfigRejections(ctx.project.projectDir);
            return rejected.length > 0 ? describeConfigRejections(rejected) : undefined;
          })(),
          project: ctx.project.isLoaded ? { name: ctx.project.projectName, path: ctx.project.projectPath, contentDir: ctx.project.contentDir, engineAssociation: ctx.project.engineAssociation, config: Object.keys(ctx.project.config).length > 0 ? ctx.project.config : undefined } : null,
          // Bridge ABI version of the deployed plugin in this project.
          // Plugins declaring nativeModule.minBridgeApi compare against
          // this number; older bridges refuse newer plugins.
          //
          // Read from the header on disk, which describes the source, not the
          // loaded binary. bridgeProtocol below comes from the running plugin
          // itself and is the one to trust when the two disagree.
          bridgeApiVersion: bridgeApiVersion ?? undefined,
          // #821: what the connected plugin said it was, and whether that
          // matches the client. A mismatch here is the reason behind an
          // "Unknown method" on an action the schema advertises.
          bridgeProtocol: ctx.bridge.capabilities
            ? {
                plugin: ctx.bridge.capabilities.protocolVersion,
                client: CLIENT_PROTOCOL_VERSION,
                builtAt: ctx.bridge.capabilities.builtAt,
                actionCount: ctx.bridge.capabilities.actionCount,
                mismatch: describeProtocolMismatch(ctx.bridge.capabilities) ?? undefined,
              }
            : undefined,
          // Pre-built sequences for this project. If the user's request
          // matches a flow's name/description, prefer flow(action="run")
          // over composing the sequence by hand. See SERVER_INSTRUCTIONS.
          flows: flows.length > 0 ? flows : undefined,
          // #817: only beyond one editor, so a single-editor status response
          // is exactly what it has always been.
          editors: ctx.sessions && ctx.sessions.size > 1
            ? ctx.sessions.list().map((s) => s.info(s === ctx.sessions!.active))
            : undefined,
        };
      },
    },
    set_project: {
      description: "Switch project: moves both path resolution and the editor connection to the new .uproject. Params: projectPath",
      handler: async (ctx, p) => {
        const projectPath = p.projectPath as string;
        if (!projectPath) throw new Error("Missing 'projectPath'");

        // #817: with several editors registered, switching this session onto a
        // project another session already holds would leave two sessions
        // pointed at one editor. Name the one that already has it instead.
        const existing = ctx.sessions?.find(projectPath);
        if (existing && existing !== ctx.session) {
          throw new Error(
            `'${existing.name}' is already registered for that project. ` +
              `Use project(action='use_editor', editorTarget='${existing.name}') to switch to it.`,
          );
        }

        // switchProject moves the bridge and the path resolver together (#818).
        // Doing it here by hand is what left the socket on the previous
        // project's editor while every path resolved against the new one.
        const switched = await switchProject(ctx.project, ctx.bridge, projectPath);
        // Sessions are keyed by project root, so the key has to move with the
        // project. Without this the session stays addressable only under the
        // project it just left.
        const editor = ctx.sessions && ctx.session ? ctx.sessions.rekey(ctx.session) : undefined;
        const result = deploy(ctx.project);
        return {
          success: true,
          editor: editor?.name,
          projectName: ctx.project.projectName,
          contentDir: ctx.project.contentDir,
          engineAssociation: ctx.project.engineAssociation,
          previousProject: switched.previousProjectPath ?? undefined,
          editorConnected: switched.connected,
          // The editor this connection belongs to. Always the project above.
          editorTarget: {
            projectPath: switched.target.projectPath,
            port: switched.target.port,
            portSource: switched.target.portSource,
          },
          // Present when no editor answered: the switch still completed, and
          // nothing can reach the previous project's editor any more.
          editorUnreachable: switched.connectError,
          bridgeSetup: deploySummary(result),
        };
      },
    },
    list_editors: {
      description: "List every editor session this server drives: name, project, bridge port, whether the socket is connected, whether anything is answering on that port, and which session untargeted calls fall through to. Params: none (#817)",
      handler: async (ctx) => {
        if (!ctx.sessions) {
          return {
            editorCount: 1,
            activeEditor: null,
            editors: [{ name: "default", projectPath: ctx.project.projectPath, connected: ctx.bridge.isConnected, active: true }],
            note: "This server was built without a session registry, so it drives one editor.",
          };
        }
        const active = ctx.sessions.active;
        // S2: the shared-port record is computed at registration, before any
        // lockfile has been read, and connect() moves the port afterwards.
        // Recompute from the ports actually in use before reporting them.
        ctx.sessions.refreshSharedPorts();
        const editors = await Promise.all(
          ctx.sessions.list().map(async (s) => {
            const info = s.info(s === active);
            return {
              ...info,
              // The session's own host, so a project pointed elsewhere by
              // `bridge.host` is probed where it actually lives (#817).
              bridgeReachable: await isBridgeReachable(s.bridge.port, s.bridge.host),
              pluginBuildStale: s.project.projectPath
                ? (checkPluginFreshness(s.project.projectPath).stale || undefined)
                : undefined,
            };
          }),
        );
        const ambiguous = editors.filter((e) => e.portSharedWith?.length);
        return {
          editorCount: editors.length,
          activeEditor: active.name,
          editors,
          // Recomputed here rather than read from a startup snapshot. The
          // warning is a function of the CURRENT session set, and the set
          // grows at runtime through add_editor, so a value captured once at
          // startup answers a question about a server that no longer exists.
          envWarnings: envWarningsFor(ctx),
          targeting: editors.length > 1
            ? "Pass editor=\"<name>\" on any call to run it in that editor. Untargeted calls run in the active editor."
            : "One editor: every call runs in it, and no 'editor' parameter is advertised.",
          warning: ambiguous.length > 0
            ? `These sessions share a bridge port and cannot be told apart: ${ambiguous.map((e) => e.name).join(", ")}. Give each project its own 'bridge.port' in its ue-mcp.yml, or unset UE_MCP_PORT.`
            : undefined,
        };
      },
    },
    use_editor: {
      description: "Make one editor session the default target for untargeted calls. Does not change the session set and never touches any editor process. Params: editorTarget (session name, project name, or .uproject path) (#817)",
      handler: async (ctx, p) => {
        if (!ctx.sessions) throw new Error("This server drives one editor; there is nothing to switch between.");
        const target = p.editorTarget as string;
        if (!target) throw new Error("Missing 'editorTarget'");
        const session = ctx.sessions.use(target);
        return {
          success: true,
          activeEditor: session.name,
          projectPath: session.project.projectPath,
          bridgePort: session.bridge.port,
          editorConnected: session.bridge.isConnected,
        };
      },
    },
    add_editor: {
      description: "Register another project as an addressable editor session, with its own bridge connection and port. Optionally launch its editor. Every category then accepts editor=\"<name>\" to run a call there. Params: projectPath, editorName? (defaults to the project name), start? (launch the editor and wait for it to be ready), timeout? (seconds, default 300) (#817)",
      handler: async (ctx, p) => {
        if (!ctx.sessions) throw new Error("This server was built without a session registry.");
        const projectPath = p.projectPath as string;
        if (!projectPath) throw new Error("Missing 'projectPath'");
        const before = ctx.sessions.size;
        const session = ctx.sessions.register({
          projectPath,
          name: typeof p.editorName === "string" && p.editorName ? p.editorName : undefined,
        });
        const alreadyRegistered = ctx.sessions.size === before;

        // D1: build this editor's own tool graph, plugins, task registry and
        // guards BEFORE it is addressable. Without it every per-session lookup
        // missed and fell back to the first project's load, so a call or a
        // flow targeted at this editor ran the FIRST project's steps inside it.
        // A failure here is reported rather than swallowed: an editor that
        // cannot be given its own surface must not borrow another's.
        try {
          await ctx.sessions.prepare(session);
        } catch (e) {
          throw new Error(
            `Registered '${session.name}' but could not build its tool surface, so it is not safe to dispatch to: ` +
              `${e instanceof Error ? e.message : String(e)}. ` +
              `Drop it with project(action='drop_editor', editorTarget='${session.name}') and check that project's ue-mcp.yml.`,
          );
        }

        const attachResult = attach(session.project);
        let started: unknown;
        if (p.start === true) {
          const timeout = typeof p.timeout === "number" && p.timeout > 0 ? p.timeout : 300;
          started = await startEditor(session.project, timeout, ctx.onProgress);
        }
        try { await session.bridge.connect(); } catch { /* editor may not be running yet */ }

        // Same lines the startup path prints, on the same stream, because the
        // person watching the console is the one who exported the variable.
        const envWarnings = envWarningsFor(ctx);
        for (const line of envWarnings ?? []) console.error(`[ue-mcp] ${line}`);

        return {
          success: true,
          editor: session.name,
          alreadyRegistered: alreadyRegistered || undefined,
          projectName: session.project.projectName,
          projectPath: session.project.projectPath,
          bridgePort: session.bridge.port,
          editorConnected: session.bridge.isConnected,
          bridgeSetup: attachSummary(attachResult),
          started,
          editorCount: ctx.sessions.size,
          // The env vars that flatten every editor into one are reported at
          // startup from the startup session set, and at one editor there is
          // nothing to report - so a server that started on one project and
          // grew to two here had never said anything and never would.
          //
          // That is the whole failure: `UE_MCP_TEST_ENGINE_ROOT` exported for
          // the first project decides the engine tree for THIS one too, ahead
          // of its own editor.path, so a 5.6 project launches and builds
          // against a 5.8 engine and the only symptom is a build that should
          // not have worked. The list is a function of the session set, and
          // this call is the one that changes it, so it is recomputed here and
          // said out loud on the same response that created the second editor.
          envWarnings,
          hint: `Call any action with editor="${session.name}" to run it there, or project(action="use_editor", editorTarget="${session.name}") to make it the default.`,
        };
      },
    },
    drop_editor: {
      description: "Forget an editor session and close its bridge socket. The editor process is LEFT RUNNING and untouched - this detaches, it does not stop anything (use editor(stop_editor) for that). Params: editorTarget (#817)",
      handler: async (ctx, p) => {
        if (!ctx.sessions) throw new Error("This server drives one editor; there is nothing to drop.");
        const target = p.editorTarget as string;
        if (!target) throw new Error("Missing 'editorTarget'");
        const dropped = ctx.sessions.drop(target);
        return {
          success: true,
          dropped: dropped.name,
          projectPath: dropped.projectPath,
          editorLeftRunning: true,
          activeEditor: ctx.sessions.active.name,
          editorCount: ctx.sessions.size,
        };
      },
    },
    get_info: {
      description: "Read .uproject file details. Params: none",
      handler: async (ctx) => {
        ctx.project.ensureLoaded();
        return { projectName: ctx.project.projectName, engineAssociation: ctx.project.engineAssociation, contentDir: ctx.project.contentDir, uprojectContents: JSON.parse(fs.readFileSync(ctx.project.projectPath!, "utf-8")) };
      },
    },
    read_config: {
      description: "Read INI config. Params: configName (e.g. 'Engine', 'Game')",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const filePath = resolveConfigPath(ctx.project.configDir!, p.configName as string);
        if (!fs.existsSync(filePath)) throw new Error(`Config file not found: ${filePath}`);
        const sections = parseIni(fs.readFileSync(filePath, "utf-8"));
        return { path: filePath, configName: p.configName, sectionCount: Object.keys(sections).length, sections };
      },
    },
    search_config: {
      description: "Search INI files. Params: query",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const configDir = ctx.project.configDir!;
        if (!fs.existsSync(configDir)) throw new Error(`Config directory not found: ${configDir}`);
        const query = (p.query as string).toLowerCase();
        const results: Array<{ file: string; section: string; line: number; content: string }> = [];
        for (const file of findIniFiles(configDir)) {
          const lines = fs.readFileSync(file, "utf-8").split(/\r?\n/); let currentSection = "";
          for (let i = 0; i < lines.length; i++) {
            const line = lines[i].trim();
            if (line.startsWith("[") && line.endsWith("]")) { currentSection = line.slice(1, -1); continue; }
            if (line.toLowerCase().includes(query)) results.push({ file: path.basename(file), section: currentSection, line: i + 1, content: line });
          }
        }
        return { query: p.query, resultCount: results.length, results: results.slice(0, 200) };
      },
    },
    list_config_tags: {
      description: "Extract gameplay tags from config. Params: none",
      handler: async (ctx) => {
        ctx.project.ensureLoaded();
        const configDir = ctx.project.configDir!;
        const tags = new Set<string>();
        for (const file of findIniFiles(configDir)) {
          const lines = fs.readFileSync(file, "utf-8").split(/\r?\n/); let inTagSection = false;
          for (const line of lines) {
            const trimmed = line.trim();
            if (trimmed.startsWith("[") && trimmed.endsWith("]")) { inTagSection = trimmed.toLowerCase().includes("gameplaytag"); continue; }
            if (!inTagSection) continue;
            let match = trimmed.match(/Tag="?([^"]+)"?/); if (match) { tags.add(match[1]); continue; }
            match = trimmed.match(/TagName="([^"]+)"/); if (match) tags.add(match[1]);
          }
        }
        const sorted = [...tags].sort();
        return { source: "config_files", count: sorted.length, tags: sorted, tree: buildTagTree(sorted) };
      },
    },
    read_cpp_header: {
      description: "Parse a .h file. Params: headerPath",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const headerPath = p.headerPath as string;
        let resolved = headerPath;
        if (!path.isAbsolute(headerPath)) {
          const roots = findSourceRoots(ctx.project.projectDir!, ctx.project.projectName);
          const candidate = roots.map(r => path.join(r, headerPath)).find(c => fs.existsSync(c));
          resolved = candidate ?? path.join(ctx.project.projectDir!, "Source", headerPath);
        }
        if (!fs.existsSync(resolved)) throw new Error(`Header not found: ${resolved}`);
        return parseHeader(fs.readFileSync(resolved, "utf-8"), resolved);
      },
    },
    read_module: {
      description: "Read module source. Params: moduleName",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const moduleName = p.moduleName as string;
        const moduleDir = resolveModuleDir(ctx.project.projectDir!, ctx.project.projectName, moduleName);
        if (!moduleDir) {
          const tried = findSourceRoots(ctx.project.projectDir!, ctx.project.projectName);
          throw new Error(`Module '${moduleName}' not found. Searched: ${tried.length ? tried.join(", ") : "(no Source/ directories)"}`);
        }
        const headers: string[] = [], sources: string[] = [];
        collectFiles(moduleDir, headers, sources);
        const buildCs = path.join(moduleDir, `${moduleName}.Build.cs`);
        return { moduleName, path: moduleDir, headerCount: headers.length, sourceCount: sources.length, headers: headers.map(h => path.relative(moduleDir, h).replace(/\\/g, "/")), sources: sources.map(s => path.relative(moduleDir, s).replace(/\\/g, "/")), buildCs: fs.existsSync(buildCs) ? fs.readFileSync(buildCs, "utf-8") : null };
      },
    },
    list_modules: {
      description: "List C++ modules. Params: none",
      handler: async (ctx) => {
        ctx.project.ensureLoaded();
        const roots = findSourceRoots(ctx.project.projectDir!, ctx.project.projectName);
        if (roots.length === 0) throw new Error(`No Source/ directory found under ${ctx.project.projectDir}`);
        const modules: Array<{ name: string; path: string; hasBuildCs: boolean; sourceRoot: string }> = [];
        for (const root of roots) {
          for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
            if (!entry.isDirectory()) continue;
            const modDir = path.join(root, entry.name);
            modules.push({ name: entry.name, path: modDir, hasBuildCs: fs.existsSync(path.join(modDir, `${entry.name}.Build.cs`)), sourceRoot: root });
          }
        }
        return { sourceRoots: roots, moduleCount: modules.length, modules };
      },
    },
    search_cpp: {
      description: "Search .h/.cpp files. Params: query, directory?",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const roots = findSourceRoots(ctx.project.projectDir!, ctx.project.projectName);
        if (roots.length === 0) throw new Error(`No Source/ directory found under ${ctx.project.projectDir}`);
        // If directory is provided, resolve it relative to whichever root contains it.
        let searchDirs: string[] = roots;
        if (p.directory) {
          const sub = p.directory as string;
          if (path.isAbsolute(sub)) {
            if (!fs.existsSync(sub)) throw new Error(`Directory not found: ${sub}`);
            searchDirs = [sub];
          } else {
            const matches = roots.map(r => path.join(r, sub)).filter(d => fs.existsSync(d));
            if (matches.length === 0) throw new Error(`Directory '${sub}' not found under any source root: ${roots.join(", ")}`);
            searchDirs = matches;
          }
        }
        const query = (p.query as string).toLowerCase();
        const results: Array<{ file: string; line: number; content: string; sourceRoot: string }> = [];
        let stopped = false;
        function search(dir: string, root: string): void {
          if (stopped) return;
          for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
            if (stopped) return;
            const full = path.join(dir, entry.name);
            if (entry.isDirectory()) search(full, root);
            else if (/\.(h|cpp|inl)$/i.test(entry.name)) {
              const lines = fs.readFileSync(full, "utf-8").split(/\r?\n/);
              for (let i = 0; i < lines.length; i++) {
                if (lines[i].toLowerCase().includes(query)) {
                  results.push({ file: path.relative(root, full).replace(/\\/g, "/"), line: i + 1, content: lines[i].trimEnd(), sourceRoot: root });
                  if (results.length >= 500) { stopped = true; return; }
                }
              }
            }
          }
        }
        for (const d of searchDirs) {
          // Find which root this dir belongs to for relative-path reporting.
          const owningRoot = roots.find(r => d === r || d.startsWith(r + path.sep)) ?? d;
          search(d, owningRoot);
        }
        return { query: p.query, directory: p.directory ?? "(all)", resultCount: results.length, results };
      },
    },
    read_engine_header: {
      description: "Parse a .h file from the engine source tree. Params: headerPath (relative to Engine/Source, or absolute)",
      handler: async (ctx, p) => {
        const engineRoot = requireEngineRoot(ctx);
        const headerPath = p.headerPath as string;
        const resolved = path.isAbsolute(headerPath)
          ? headerPath
          : path.join(engineRoot, "Engine", "Source", headerPath);
        if (!fs.existsSync(resolved)) throw new Error(`Engine header not found: ${resolved}`);
        const content = fs.readFileSync(resolved, "utf-8");
        return { ...parseHeader(content, resolved), engineRoot };
      },
    },
    find_engine_symbol: {
      description: "Grep engine headers for a symbol. Params: symbol, maxResults?",
      handler: async (ctx, p) => {
        const engineRoot = requireEngineRoot(ctx);
        const engineSource = path.join(engineRoot, "Engine", "Source", "Runtime");
        if (!fs.existsSync(engineSource)) throw new Error(`Engine source not found: ${engineSource}`);
        const symbol = p.symbol as string;
        const maxResults = (p.maxResults as number) ?? 100;
        const results: Array<{ file: string; line: number; content: string }> = [];
        const needle = symbol;
        function scan(dir: string): void {
          if (results.length >= maxResults) return;
          for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
            if (results.length >= maxResults) return;
            const full = path.join(dir, entry.name);
            if (entry.isDirectory()) { scan(full); continue; }
            if (!/\.(h|inl)$/i.test(entry.name)) continue;
            const lines = fs.readFileSync(full, "utf-8").split(/\r?\n/);
            for (let i = 0; i < lines.length; i++) {
              if (lines[i].includes(needle)) {
                results.push({ file: path.relative(engineSource, full).replace(/\\/g, "/"), line: i + 1, content: lines[i].trimEnd() });
                if (results.length >= maxResults) return;
              }
            }
          }
        }
        scan(engineSource);
        return { symbol, engineRoot, resultCount: results.length, results };
      },
    },
    list_engine_modules: {
      description: "List modules in Engine/Source/Runtime. Params: none",
      handler: async (ctx) => {
        const engineRoot = requireEngineRoot(ctx);
        const runtimeDir = path.join(engineRoot, "Engine", "Source", "Runtime");
        if (!fs.existsSync(runtimeDir)) throw new Error(`Runtime dir not found: ${runtimeDir}`);
        const modules = fs.readdirSync(runtimeDir, { withFileTypes: true })
          .filter(e => e.isDirectory())
          .map(e => ({ name: e.name, hasBuildCs: fs.existsSync(path.join(runtimeDir, e.name, `${e.name}.Build.cs`)) }));
        return { engineRoot, moduleCount: modules.length, modules };
      },
    },
    search_engine_cpp: {
      description: "Search engine .h/.cpp/.inl files across Runtime/Editor/Developer/Plugins. Params: query, tree? (Runtime|Editor|Developer|Plugins|all - default Runtime), subdirectory?, maxResults? (default 500)",
      handler: async (ctx, p) => {
        const engineRoot: string = requireEngineRoot(ctx);
        const query = (p.query as string)?.toLowerCase();
        if (!query) throw new Error("Missing required parameter 'query'");
        const tree = (p.tree as string) ?? "Runtime";
        const maxResults = (p.maxResults as number) ?? 500;
        const subdir = p.subdirectory as string | undefined;
        const engineSource = path.join(engineRoot, "Engine", "Source");
        const roots: string[] = [];
        if (tree === "all") {
          for (const t of ["Runtime", "Editor", "Developer"]) {
            const d = path.join(engineSource, t);
            if (fs.existsSync(d)) roots.push(d);
          }
          const pluginsDir = path.join(engineRoot, "Engine", "Plugins");
          if (fs.existsSync(pluginsDir)) roots.push(pluginsDir);
        } else if (tree === "Plugins") {
          const d = path.join(engineRoot, "Engine", "Plugins");
          if (!fs.existsSync(d)) throw new Error(`Engine plugins dir not found: ${d}`);
          roots.push(d);
        } else {
          const d = path.join(engineSource, tree);
          if (!fs.existsSync(d)) throw new Error(`Engine tree '${tree}' not found: ${d}`);
          roots.push(subdir ? path.join(d, subdir) : d);
        }
        const results: Array<{ file: string; line: number; content: string }> = [];
        function scan(dir: string): boolean {
          for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
            if (results.length >= maxResults) return true;
            const full = path.join(dir, entry.name);
            if (entry.isDirectory()) {
              if (entry.name === "Intermediate" || entry.name === "Binaries") continue;
              if (scan(full)) return true;
            } else if (/\.(h|cpp|inl)$/i.test(entry.name)) {
              let content: string;
              try { content = fs.readFileSync(full, "utf-8"); } catch { continue; }
              const lines = content.split(/\r?\n/);
              for (let i = 0; i < lines.length; i++) {
                if (lines[i].toLowerCase().includes(query)) {
                  results.push({ file: path.relative(engineRoot, full).replace(/\\/g, "/"), line: i + 1, content: lines[i].trimEnd() });
                  if (results.length >= maxResults) return true;
                }
              }
            }
          }
          return false;
        }
        for (const r of roots) { if (scan(r)) break; }
        return { query: p.query, tree, subdirectory: subdir ?? "(root)", engineRoot, resultCount: results.length, results };
      },
    },
    search_tools: {
      description: "Search every ue-mcp tool + action by keyword or task INTENT (a synonym layer maps 'screenshot'->capture_scene_png, 'tile a texture'->the texture-bomb flow, etc.) and return ranked matches (tool, action, description, score). The first step before editor(execute_python); most tasks already have a dedicated action. Params: query (space-separated keywords/intent), limit? (default 20) (#704)",
      handler: async (_ctx, p) => {
        const query = (p.query as string) ?? "";
        if (!query.trim()) throw new Error("Missing 'query'");
        const graph = _ctx.getToolGraph?.();
        const limit = (p.limit as number) ?? 20;
        const results = graph ? searchToolGraph(graph, query, limit) : await searchTools(query, limit);
        return {
          query,
          resultCount: results.length,
          results,
          hint: results.length === 0 ? "No dedicated action matched. Only then consider editor(execute_python)." : undefined,
        };
      },
    },
    describe_action: {
      description:
        "Return the live parameter schema for one action: every parameter it accepts, "
        + "with type, required/optional, description, allowed values and default, plus the "
        + "bridge method it dispatches to. search_tools finds an action by keyword and hands "
        + "back only prose; this answers what to actually pass, so the first call is the "
        + "right one. name takes 'tool.action' (asset.set_property) or a bare action name, "
        + "which reports every category providing it. A name that does not resolve comes back "
        + "with the closest spellings rather than a bare failure. Reads the graph THIS editor "
        + "advertises, so injected Epic and plugin actions are included. Each action also "
        + "reports class: read (observes), mutate (changes the editor, its project on disk, or "
        + "its process) or unknown (decided by a parameter, so gated like mutate) - MCP's own "
        + "readOnlyHint is per tool, and every tool here is a category holding both, so a "
        + "harness that gates writes reads it from here. "
        + "Params: name (required), category? (return every action of one category instead of one action)",
      handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
        const { getLiveToolGraph } = await import("../tools.js");
        const graph: ToolDef[] = ctx.getToolGraph?.() ?? getLiveToolGraph();

        const category = (p.category as string | undefined)?.trim();
        if (category) {
          const tool = graph.find((t) => t.name === category.toLowerCase());
          if (!tool) {
            throw new Error(
              `Unknown category '${category}'. Available: ${graph.map((t) => t.name).join(", ")}`,
            );
          }
          return {
            tool: tool.name,
            actionCount: Object.keys(tool.actions).length,
            actions: Object.keys(tool.actions).map((a) => actionSchema(tool, a)),
          };
        }

        const name = (p.name as string | undefined)?.trim();
        if (!name) throw new Error("Missing 'name'. Pass 'tool.action', a bare action name, or use category= for a whole category.");

        const matches = resolveActionRef(name, graph);
        if (matches.length === 0) {
          const suggestions = suggestActions(name, graph);
          throw new Error(
            `Unknown action '${name}'.`
            + (suggestions.length ? ` Closest: ${suggestions.join(", ")}.` : "")
            + " project(search_tools) searches by intent when the name is not known.",
          );
        }
        const schemas = matches.map(({ tool, action }) => actionSchema(tool, action));
        // One match is the common case, and returning it bare keeps the shape
        // an agent has to read as small as the question it asked.
        return schemas.length === 1
          ? schemas[0]
          : {
              name,
              matchCount: schemas.length,
              hint: `'${name}' is provided by ${schemas.length} categories. Qualify it as '<tool>.${matches[0].action}' to get one.`,
              matches: schemas,
            };
      },
    },
    list_available_actions: {
      description:
        "Report which actions this server can serve RIGHT NOW and why the rest cannot. With no editor "
        + "attached the surface is advertised in full but most of it cannot run, and this is the line "
        + "between the two halves: an action either runs in this Node process (availability 'always') or "
        + "dispatches to a bridge method only a running editor answers (availability 'editor', with "
        + "bridgeMethod naming it). The offline half is the engine symbol index and the C++ correctness "
        + "checks, the project config, source and file readers, the surface introspection, and the "
        + "process lifecycle actions that start, stop and build. An action contributed by a plugin whose "
        + "route is undeclared reports 'unknown' and should be treated as needing an editor. "
        + "Counts come back by default, per category as well as overall; includeNames=true adds the actions "
        + "themselves, and category narrows the whole report to one. With an editor attached everything "
        + "is available and the classification still answers the question worth asking then, which is "
        + "what keeps working once the editor is stopped for a rebuild. "
        + "Params: category?, includeNames? (default false), state? (available|blocked|all, default available)",
      handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
        const { getLiveToolGraph } = await import("../tools.js");
        const graph: ToolDef[] = getLiveToolGraph();

        const category = (p.category as string | undefined)?.trim();
        if (category && !graph.some((t) => t.name === category.toLowerCase())) {
          throw new Error(
            `Unknown category '${category}'. Available: ${graph.map((t) => t.name).join(", ")}`,
          );
        }

        const state = (p.state as string | undefined)?.trim() ?? "available";
        if (state !== "available" && state !== "blocked" && state !== "all") {
          throw new Error(`'state' must be 'available', 'blocked' or 'all' (got '${state}').`);
        }

        const report = availabilityReport(graph, {
          editorConnected: ctx.bridge.isConnected,
          category,
          state,
          names: p.includeNames === true,
        });
        const target = ctx.bridge.getTarget();
        return {
          ...report,
          editorTarget: { projectPath: target.projectPath, port: target.port, portSource: target.portSource },
          hint: report.blocked > 0
            ? "editor(action='start_editor') launches the editor and blocks until its bridge answers."
            : undefined,
        };
      },
    },
    list_content_assets: {
      description:
        "List the project's assets from the package files on DISK, which is the one asset query that "
        + "works with no editor running. Takes a mount path (/Game, /Game/Characters, or a plugin's "
        + "/MyPlugin) and resolves it through the same mount table the live path uses, so an offline "
        + "listing names assets exactly as the editor would. It answers existence, layout, size and "
        + "modified time, and it deliberately does not answer class, registry tags or dependencies: "
        + "those live in the editor's asset registry and are not in the file, so asset(list) and "
        + "asset(search) remain the answer once an editor is up. maxResults stops the walk rather "
        + "than trimming the result, and truncated says when it did. "
        + "Params: contentPath? (default /Game), recursive? (default true), namePattern? "
        + "(case-insensitive substring of the asset name), maxResults? (default 1000)",
      handler: async (ctx: ToolContext, p: Record<string, unknown>) => listContent(ctx.project, {
        contentPath: p.contentPath as string | undefined,
        recursive: p.recursive as boolean | undefined,
        namePattern: p.namePattern as string | undefined,
        maxResults: p.maxResults as number | undefined,
      }),
    },
    check_install: {
      description:
        "Answer whether this project can run the bridge at all, from disk, with no editor running and "
        + "nothing compiled. Reports the project kind (a project declaring no native modules of its own "
        + "is Blueprint-only, which is NOT a blocker: UnrealBuildTool writes temporary target and module "
        + "files under Intermediate/Source/ and compiles the plugin against them), the engine that will "
        + "be used and where it was resolved from, whether the plugin is deployed, enabled in the "
        + ".uproject, compiled and up to date with its source, and whether this machine has the C++ "
        + "toolchain Unreal needs. Every problem carries a stable code, what is wrong and the exact fix, "
        + "and nextSteps is those fixes in order. Read-only: it never deploys, enables or builds "
        + "anything. Params: projectPath? (default the loaded project), skipToolchain? (skip the "
        + "toolchain probe, which shells out to vswhere or the compiler)",
      handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
        const requested = (p.projectPath as string | undefined)?.trim();
        if (!requested) ctx.project.ensureLoaded();
        const uproject = requested || ctx.project.projectPath!;
        return inspectInstall(uproject, { skipToolchain: p.skipToolchain === true });
      },
    },
    execute_python_report: {
      description: "Measurement for #704: reads this session's execute_python calls and, for each, runs its taskSummary back through search_tools to flag calls that OVERLAPPED an existing dedicated action ('you used Python for X, but tool Y does X'). Returns totalCalls, overlapping[] and an overlapRate. Params: none (#704)",
      handler: async (ctx) => {
        const entries = getWorkarounds(ctx);
        const overlapping: Array<{ taskSummary: string; suggestion: ToolSearchHit; codeSnippet: string }> = [];
        for (const e of entries) {
          const q = (e.taskSummary ?? "").trim();
          if (!q) continue;
          const hits = await searchTools(q, 1);
          if (hits.length > 0 && hits[0].score >= 4) {
            overlapping.push({ taskSummary: q, suggestion: hits[0], codeSnippet: e.code.slice(0, 120) });
          }
        }
        return {
          totalCalls: entries.length,
          withTaskSummary: entries.filter((e) => (e.taskSummary ?? "").trim()).length,
          overlappingCount: overlapping.length,
          overlapRate: entries.length ? +(overlapping.length / entries.length).toFixed(2) : 0,
          overlapping,
        };
      },
    },
    list_files: {
      description: "List files on disk under a directory, optionally filtered by extension(s). Runs in the MCP server process (no editor round-trip). Params: directory (absolute, or relative to the project dir), extensions? (e.g. ['png','exr'] or 'png'), recursive? (default false), maxResults? (default 1000) (#608)",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const dirArg = p.directory as string;
        if (!dirArg) throw new Error("Missing 'directory'");
        const base = path.isAbsolute(dirArg) ? dirArg : path.join(ctx.project.projectDir!, dirArg);
        if (!fs.existsSync(base)) throw new Error(`Directory not found: ${base}`);
        const extsRaw = p.extensions;
        const exts = (Array.isArray(extsRaw) ? extsRaw : extsRaw ? [extsRaw] : [])
          .map((e) => String(e).replace(/^\./, "").toLowerCase());
        const recursive = (p.recursive as boolean) ?? false;
        const maxResults = (p.maxResults as number) ?? 1000;
        const results: Array<{ path: string; name: string; sizeBytes: number; ext: string }> = [];
        const walk = (dir: string): void => {
          if (results.length >= maxResults) return;
          for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
            if (results.length >= maxResults) return;
            const full = path.join(dir, entry.name);
            if (entry.isDirectory()) { if (recursive) walk(full); continue; }
            const ext = path.extname(entry.name).replace(/^\./, "").toLowerCase();
            if (exts.length > 0 && !exts.includes(ext)) continue;
            let sizeBytes = 0;
            try { sizeBytes = fs.statSync(full).size; } catch { /* race */ }
            results.push({ path: full, name: entry.name, sizeBytes, ext });
          }
        };
        walk(base);
        return { directory: base, extensions: exts, recursive, count: results.length, files: results };
      },
    },
    set_config: bp("Write to INI. Params: configName, section, key, value", "set_config"),
    build: {
      // #958: this used to be dispatched over the editor bridge, so it failed
      // with ECONNREFUSED whenever the editor was down. That made it unusable
      // for its only real job: UnrealBuildTool refuses to link while an editor
      // holds the module DLLs, so a full rebuild has to happen with the editor
      // stopped. It runs UnrealBuildTool out of process instead, which needs no
      // editor at all.
      description:
        "Build the project's C++ out of process with UnrealBuildTool. Works with the editor STOPPED, which a full rebuild requires (UBT cannot link while an editor holds the module DLLs). Blocks until the build finishes and returns the compiler output. Params: configuration? (default Development), platform? (default the host platform), clean? (#958)",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const lines: string[] = [];
        const result = await buildProject(ctx.project.projectPath!, {
          onOutput: (text) => lines.push(text),
          configuration: p.configuration as string | undefined,
          platform: p.platform as string | undefined,
          clean: p.clean as boolean | undefined,
        });
        return { ...result, output: lines.join("") };
      },
    },
    generate_project_files: bp("Generate IDE project files (Visual Studio, Xcode, etc.). Params: none", "generate_project_files"),

    // v0.7.13 - native C++ authoring. Bridge handlers wrap
    // GameProjectUtils / ILiveCodingModule (same APIs used by the editor's
    // File → New C++ Class and Live Coding menus).
    create_cpp_class: {
      description: "Create a new native UCLASS in a project module. Uses the same engine template path as File → New C++ Class. Writes .h + .cpp; returns both paths plus needsEditorRestart (true unless Live Coding successfully hot-reloaded). Params: className (no prefix), parentClass? (default UObject; accepts short names like 'Actor' or /Script/<Module>.<Class> paths), moduleName? (default: first project module, use list_project_modules to pick), classDomain? ('public'|'private'|'classes', default public), subPath?",
      bridge: "create_cpp_class",
      // AddCodeToProject regenerates IDE project files synchronously - can
      // easily exceed the default 30-second cap on first use.
      timeoutMs: 300_000,
      mapParams: (p) => ({
        className: p.className,
        parentClass: p.parentClass,
        moduleName: p.moduleName,
        classDomain: p.classDomain,
        subPath: p.subPath,
      }),
    },
    list_project_modules: bp(
      paged("List native modules in the current project (name, host type, source path), in the .uproject's own declaration order. Feed moduleName from here into create_cpp_class."),
      "list_project_modules",
      (p) => ({ cursor: p.cursor, limit: p.limit }),
    ),
    list_loaded_modules: bp(
      paged("Enumerate ALL engine+project modules with runtime load state (loaded/gameModule), not just uproject-declared ones. Params: filter? (case-insensitive substring), loadedOnly? (default false) (#689)"),
      "list_loaded_modules",
      (p) => ({ filter: p.filter, loadedOnly: p.loadedOnly, cursor: p.cursor, limit: p.limit }),
    ),
    is_module_loaded: bp(
      "Report whether a named module is currently loaded in the editor. Params: moduleName (#689)",
      "is_module_loaded",
      (p) => ({ moduleName: p.moduleName }),
    ),
    list_available_plugins: bp(
      paged("List every plugin installed in this engine or project, sorted by name, with its category, version, type, whether it is enabled in THIS editor session, whether it is enabled by default, and the .uproject's current reference to it under projectReference {present, enabled}. Those two disagree after enable_plugin until the editor restarts, which is the point of reporting both. Params: filter?, pluginCategory?, enabledOnly?, limit? (default 200, max 2000)"),
      "list_available_plugins",
      (p) => ({ filter: p.filter, pluginCategory: p.pluginCategory, enabledOnly: p.enabledOnly, cursor: p.cursor, limit: p.limit }),
    ),
    enable_plugin: bp(
      "Enable a plugin in the .uproject. Plugin enablement is neither a UPROPERTY nor an INI key, it is a JSON array in the .uproject read once at startup, so set_config cannot reach it and without this a plugin-gated capability stays permanently unreachable through the bridge. Idempotent: a plugin already enabled, or enabled by default with no entry, reports existed and writes nothing. The change is a file change, so modules, classes, content and settings appear only after editor(restart_editor), which the result says. Params: pluginName",
      "enable_plugin",
      (p) => ({ pluginName: p.pluginName }),
    ),
    disable_plugin: bp(
      "Disable a plugin in the .uproject. removeReference deletes the entry outright instead of writing an explicit disable, which is the difference between handing a default-on plugin back to its default and overriding it, and the two are not the same file. Idempotent against whichever of the two was asked for. Refuses to disable the bridge itself, since that would leave no way to undo it. Takes effect on the next editor start. Params: pluginName, removeReference?",
      "disable_plugin",
      (p) => ({ pluginName: p.pluginName, removeReference: p.removeReference }),
    ),
    live_coding_compile: {
      description: "Trigger a Live Coding compile (Windows only). Hot-patches method bodies of existing UCLASSes without editor restart - the fast inner loop for UFUNCTION implementations. Does NOT reliably register brand-new UCLASSes; use build_project + editor restart for those. Params: wait? (default false - fire and return 'in_progress').",
      bridge: "live_coding_compile",
      timeoutMs: 300_000,
      mapParams: (p) => ({ wait: p.wait }),
    },
    live_coding_status: bp(
      "Report Live Coding availability/state (available, started, enabledForSession, compiling). Helps choose between live_coding_compile and build_project. Params: none",
      "live_coding_status",
      () => ({}),
    ),
    resolve_collision_profile: bp(
      "Read one collision profile's resolved per-channel responses: collisionEnabled, objectType, and every channel with Block/Overlap/Ignore. This is the project-side half of blueprint(get_component_collision) (#925): a component's ResponseArray only lists the channels it OVERRIDES, so the profile is where an inherited response actually comes from. Project trace and object channels appear under their configured names, with enumName (ECC_GameTraceChannel1) alongside so a caller can key on something stable. By default the eight engine channels plus every channel the project configured are returned; includeAllChannels=true adds the unused slots. channel narrows it to one. A profile that does not exist lists the ones that do. Params: profileName, channel?, includeAllChannels?",
      "resolve_collision_profile",
      (p) => ({ profileName: p.profileName, channel: p.channel, includeAllChannels: p.includeAllChannels }),
    ),

    write_cpp_file: {
      description:
        "Write a .h / .cpp / .inl file under the project's Source/ tree. Used to append UPROPERTYs/UFUNCTIONs or method bodies after create_cpp_class. Writes are scoped to Source/ for safety. Params: path (relative to Source/ or absolute within Source/), content (full file contents). After editing, call live_coding_compile (for existing classes) or build_project (for new classes).",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const sourceDir = path.join(ctx.project.projectDir!, "Source");
        const rel = p.path as string;
        if (!rel) throw new Error("Missing 'path' parameter");
        const content = p.content as string;
        if (typeof content !== "string") throw new Error("Missing or invalid 'content' parameter (must be a string)");

        const resolved = path.isAbsolute(rel) ? path.resolve(rel) : path.resolve(sourceDir, rel);
        const sourceAbs = path.resolve(sourceDir);
        if (!resolved.startsWith(sourceAbs + path.sep) && resolved !== sourceAbs) {
          throw new Error(`Refusing to write outside project Source/: ${resolved}`);
        }
        if (!/\.(h|cpp|inl|cs)$/i.test(resolved)) {
          throw new Error(`write_cpp_file only accepts .h/.cpp/.inl/.cs files (got '${path.extname(resolved)}')`);
        }

        const overwrote = fs.existsSync(resolved);
        fs.mkdirSync(path.dirname(resolved), { recursive: true });
        fs.writeFileSync(resolved, content, "utf-8");
        return {
          path: resolved,
          bytesWritten: Buffer.byteLength(content, "utf-8"),
          overwrote,
          hint: overwrote
            ? "Overwrote existing file. Call live_coding_compile (existing class edits) or build_project for a full rebuild."
            : "Created new file. Call generate_project_files if you also want the IDE project refreshed, then build_project.",
        };
      },
    },
    read_cpp_source: {
      description: "Read a .cpp file from the project Source/ tree. Companion to read_cpp_header for round-trip edits. Params: sourcePath (relative to Source/ or absolute).",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const sp = p.sourcePath as string;
        if (!sp) throw new Error("Missing 'sourcePath' parameter");
        let resolved = sp;
        if (!path.isAbsolute(sp)) {
          const roots = findSourceRoots(ctx.project.projectDir!, ctx.project.projectName);
          const candidate = roots.map(r => path.join(r, sp)).find(c => fs.existsSync(c));
          resolved = candidate ?? path.join(ctx.project.projectDir!, "Source", sp);
        }
        if (!fs.existsSync(resolved)) throw new Error(`File not found: ${resolved}`);
        const content = fs.readFileSync(resolved, "utf-8");
        return { path: resolved, bytes: content.length, content };
      },
    },
    write_source_file: {
      description:
        "Write a .h/.cpp/.inl into a named module's Public/Private folder (resolves the module dir for you, including plugin modules under Plugins/*/Source/ that write_cpp_file refuses). After a new file, build_project + restart; after a body edit, live_coding_compile. Params: module (module name, default the project's primary module), visibility (Public|Private, default Private), fileName, content.",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const moduleName = (p.module as string) ?? "";
        const fileName = p.fileName as string;
        if (!fileName) throw new Error("Missing 'fileName' parameter");
        const content = p.content as string;
        if (typeof content !== "string") throw new Error("Missing or invalid 'content' parameter (must be a string)");
        const visibility = ((p.visibility as string) || "Private");
        const vis = /^public$/i.test(visibility) ? "Public" : /^private$/i.test(visibility) ? "Private" : "";

        const moduleDir = resolveSourceModuleDir(ctx.project.projectDir!, ctx.project.projectName, moduleName);
        if (!moduleDir) throw new Error(`Module not found: '${moduleName || "(default)"}'. Use list_modules to see available modules.`);

        const target = vis ? path.join(moduleDir, vis, fileName) : path.join(moduleDir, fileName);
        if (!/\.(h|cpp|inl)$/i.test(target)) throw new Error(`write_source_file only accepts .h/.cpp/.inl files (got '${path.extname(target)}')`);

        const overwrote = fs.existsSync(target);
        fs.mkdirSync(path.dirname(target), { recursive: true });
        fs.writeFileSync(target, content, "utf-8");
        return {
          module: path.basename(moduleDir),
          path: target,
          bytesWritten: Buffer.byteLength(content, "utf-8"),
          overwrote,
          hint: overwrote
            ? "Overwrote existing file. live_coding_compile for body edits, build_project for structural changes."
            : "New file written. Run build_project and restart the editor for UE to register new types.",
        };
      },
    },
    read_source_file: {
      description:
        "Read a .h/.cpp/.inl from a named module's folder (companion to write_source_file; resolves plugin modules too). With no visibility it tries Public then Private then the module root. Params: module, visibility?, fileName.",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const moduleName = (p.module as string) ?? "";
        const fileName = p.fileName as string;
        if (!fileName) throw new Error("Missing 'fileName' parameter");
        const visibility = (p.visibility as string) || "";

        const moduleDir = resolveSourceModuleDir(ctx.project.projectDir!, ctx.project.projectName, moduleName);
        if (!moduleDir) throw new Error(`Module not found: '${moduleName || "(default)"}'`);

        const candidates = visibility
          ? [path.join(moduleDir, /^public$/i.test(visibility) ? "Public" : "Private", fileName)]
          : [path.join(moduleDir, "Public", fileName), path.join(moduleDir, "Private", fileName), path.join(moduleDir, fileName)];
        const found = candidates.find(c => fs.existsSync(c));
        if (!found) throw new Error(`Source file not found: ${fileName} in module '${path.basename(moduleDir)}'`);
        const content = fs.readFileSync(found, "utf-8");
        return { module: path.basename(moduleDir), path: found, bytes: content.length, content };
      },
    },
    build_engine_index: {
      description:
        "Build or refresh the engine symbol index that verify_symbols, lint_cpp_header and "
        + "suggest_build_deps read. Scans roughly 31,000 headers across Runtime, Editor, Developer "
        + "and the includable half of Engine/Plugins, and records for each symbol the header that "
        + "declares it, the module that owns it, its signature and any UE_DEPRECATED. The result is "
        + "cached per engine under the user directory and shared by every project on that engine, so "
        + "this is a one-time cost per engine install: expect several minutes cold (first touch of "
        + "each file goes through the virus scanner on Windows) and a few seconds warm. The other "
        + "actions build it on demand, so this is only needed to refresh after an engine upgrade or "
        + "to pay the cost deliberately. Params: refresh? (rebuild even when a valid cache exists)",
      timeoutMs: 1_800_000,
      handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
        const { index, source, cacheFile, buildMs } = requireEngineIndex(ctx, p.refresh === true);
        return {
          engineRoot: index.engineRoot,
          engineVersion: index.engineVersion,
          source,
          cacheFile,
          buildMs,
          builtAt: index.builtAt,
          trees: index.trees,
          headerCount: index.headerCount,
          symbolCount: index.symbolCount,
          uniqueNames: Object.keys(index.symbols).length,
        };
      },
    },
    verify_symbols: {
      description:
        "Check that engine symbols exist BEFORE writing C++ that uses them, and get back what you "
        + "need to write it: the header to #include, the owning module for Build.cs, the exact "
        + "declaration, the base class, and any UE_DEPRECATED with its version and message. Accepts "
        + "a qualified 'UGameplayStatics::GetPlayerPawn' as well as a bare type name, and covers "
        + "plugin modules (GameplayAbilities, Niagara, PCG, EnhancedInput) as well as the engine. A "
        + "name that does not resolve comes back with close spellings; a member miss on a class that "
        + "does exist says so, which separates a misspelled method from a misspelled class. The "
        + "aggregate includes[] and modules[] are the whole edit you need to make. Builds the index "
        + "on first use, which can take several minutes on a cold filesystem. "
        + "Params: names (string[] or comma-separated string, max 200)",
      timeoutMs: 1_800_000,
      handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
        const names = nameList(p.names, "names");
        const { index, source } = requireEngineIndex(ctx);
        return { indexSource: source, ...verifySymbols(index, names) };
      },
    },
    suggest_build_deps: {
      description:
        "Given the engine symbols a module uses, report which modules its Build.cs has to depend on "
        + "and which of those it does not list yet, plus the AddRange line to paste. Core and "
        + "CoreUObject are omitted because every module already has them. buildCsPath defaults to "
        + "the Build.cs owning modulePath, or the project's first module. Pair with "
        + "add_module_dependency, which performs the edit. "
        + "Params: names (string[] or comma-separated string), buildCsPath? (absolute), modulePath? "
        + "(a file or directory whose owning Build.cs to use)",
      timeoutMs: 1_800_000,
      handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
        const names = nameList(p.names, "names");
        const { index } = requireEngineIndex(ctx);
        let buildCs = (p.buildCsPath as string | undefined) ?? null;
        if (!buildCs && typeof p.modulePath === "string") {
          const start = fs.existsSync(p.modulePath) && fs.statSync(p.modulePath).isDirectory()
            ? p.modulePath
            : path.dirname(p.modulePath);
          buildCs = findBuildCs(start);
        }
        return suggestBuildDeps(index, names, buildCs);
      },
    },
    find_example_usage: {
      description:
        "Find real call sites for an engine symbol in the engine's own .cpp files, which answers "
        + "'how is this actually used' with code that compiles. Better than a signature for anything "
        + "with a non-obvious calling convention. Searches sources rather than headers on purpose: a "
        + "header gives the declaration, which verify_symbols already returns. An engine installed "
        + "from the Epic launcher ships headers WITHOUT .cpp sources, so on those there are no engine "
        + "call sites to find; the result says so via engineSourcesAvailable and falls back to inline "
        + "code in headers and to this project's own Source tree, rather than returning an empty list "
        + "that reads as 'nothing uses this'. "
        + "Params: symbol (bare or Class::Member), limit? (default 10), trees? (Runtime|Editor|Developer, default Runtime)",
      timeoutMs: 600_000,
      handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
        const symbol = (p.symbol as string | undefined)?.trim();
        if (!symbol) throw new Error("Missing 'symbol'");
        const engineRoot = requireEngineRoot(ctx);
        const trees = typeof p.trees === "string" ? [p.trees] : (p.trees as string[] | undefined);
        return findExampleUsage(engineRoot, symbol, {
          limit: (p.limit as number) ?? 10,
          trees,
          projectDir: ctx.project.projectDir,
        });
      },
    },
    class_hierarchy: {
      description:
        "Report what a class derives from and what derives from it, which is the question behind "
        + "'what should I subclass' and 'what already does this'. Ancestors are the full chain up "
        + "to the root, nearest parent first; descendants default to the direct subclasses only, "
        + "because every transitive subclass of UObject is tens of thousands of names. Every node "
        + "carries its module, its include and whether it crosses a module boundary from the "
        + "queried class, since crossing one is what forces a Build.cs dependency; "
        + "crossModuleDependencies is that list on its own. Reads the engine symbol index and no "
        + "files, so it is fast once the index exists, and builds it on first use, which can take "
        + "several minutes on a cold filesystem. A base the index cannot resolve (a template, a "
        + "macro-generated type) stops the walk and is reported as unresolvedAncestor rather than "
        + "silently ending the chain. "
        + "Params: symbol (class or struct name, prefix optional), direction? "
        + "(ancestors|descendants|both, default both), depth? (generations of descendants, default "
        + "1), limit? (max descendants, default 100)",
      timeoutMs: 1_800_000,
      handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
        const symbol = (p.symbol as string | undefined)?.trim();
        if (!symbol) throw new Error("Missing 'symbol'");
        const direction = p.direction as "ancestors" | "descendants" | "both" | undefined;
        const { index, source } = requireEngineIndex(ctx);
        return {
          indexSource: source,
          engineVersion: index.engineVersion,
          ...classHierarchy(index, symbol, {
            depth: p.depth as number | undefined,
            limit: p.limit as number | undefined,
            direction,
          }),
        };
      },
    },
    find_references: {
      description:
        "Find every line in the engine tree that names a symbol, which answers 'how is this woven "
        + "into the engine' and 'what would break if this changed'. Broader than find_callers on "
        + "purpose: a reference is a member declaration, a UPROPERTY type, a cast, a template "
        + "argument or a call, and both headers and .cpp files are searched. Comment lines and "
        + "preprocessor lines are skipped, since neither is a use. Each site reports its file, "
        + "line, text and owning module. An engine installed from the Epic launcher ships headers "
        + "WITHOUT .cpp sources, so engineSourcesAvailable says whether implementation files could "
        + "be searched at all and the note says what was searched instead. Scans files rather than "
        + "the index, so a rare name on a cold filesystem is slow. "
        + "Params: symbol (bare or Class::Member), limit? (max sites, default 40), trees? "
        + "(Runtime|Editor|Developer|Plugins|all, default Runtime), includeProject? (also search "
        + "this project's Source and Plugins, default true)",
      timeoutMs: 600_000,
      handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
        const symbol = (p.symbol as string | undefined)?.trim();
        if (!symbol) throw new Error("Missing 'symbol'");
        const engineRoot = requireEngineRoot(ctx);
        return findReferences(engineRoot, symbol, {
          limit: p.limit as number | undefined,
          trees: typeof p.trees === "string" ? [p.trees] : (p.trees as string[] | undefined),
          projectDir: ctx.project.projectDir,
          includeProject: p.includeProject !== false,
        });
      },
    },
    find_callers: {
      description:
        "Find who calls a function, and from which enclosing function, which is how to see the "
        + "conventions around a call before writing one: what is checked first, what is passed, "
        + "what is done with the result. Searches .cpp bodies first, since a mention in a header is "
        + "usually a declaration rather than a call, and excludes the function's own definition. "
        + "Each site reports file, line, text, module and, for a site in a .cpp, the Class::Method "
        + "it sits inside. An engine installed from the Epic launcher ships headers WITHOUT .cpp "
        + "sources, so on those there are no engine call sites to find: engineSourcesAvailable "
        + "reports that and the search falls back to inline code in headers and to this project's "
        + "own Source tree, rather than returning an empty list that reads as 'nothing calls this'. "
        + "Params: symbol (bare or Class::Method), limit? (max sites, default 25), trees? "
        + "(Runtime|Editor|Developer|Plugins|all, default Runtime), includeProject? (also search "
        + "this project's Source and Plugins, default true)",
      timeoutMs: 600_000,
      handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
        const symbol = (p.symbol as string | undefined)?.trim();
        if (!symbol) throw new Error("Missing 'symbol'");
        const engineRoot = requireEngineRoot(ctx);
        return findCallers(engineRoot, symbol, {
          limit: p.limit as number | undefined,
          trees: typeof p.trees === "string" ? [p.trees] : (p.trees as string[] | undefined),
          projectDir: ctx.project.projectDir,
          includeProject: p.includeProject !== false,
        });
      },
    },
    find_callees: {
      description:
        "Report what a function calls, by reading its body and looking every called name back up "
        + "in the engine index. Answers 'what does doing this properly actually involve': the "
        + "result carries each callee's module and include, and modules[] is the Build.cs cost of "
        + "writing code that does the same thing. The body is found via the index, which keeps the "
        + "search to the owning class's module rather than the whole tree, and the definition it "
        + "read is reported with its file and line range. An engine installed from the Epic "
        + "launcher ships headers WITHOUT .cpp sources, so only functions whose body is inline in a "
        + "header can be read there; engineSourcesAvailable and the note say so instead of "
        + "returning an empty list. Builds the index on first use. "
        + "Params: symbol (Class::Method, or a bare exported free function), limit? (max callees, "
        + "default 100), trees? (which trees to test for sources, default Runtime)",
      timeoutMs: 1_800_000,
      handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
        const symbol = (p.symbol as string | undefined)?.trim();
        if (!symbol) throw new Error("Missing 'symbol'");
        const { index, source } = requireEngineIndex(ctx);
        return {
          indexSource: source,
          ...findCallees(index, symbol, {
            projectDir: ctx.project.projectDir,
            limit: p.limit as number | undefined,
            trees: typeof p.trees === "string" ? [p.trees] : (p.trees as string[] | undefined),
          }),
        };
      },
    },
    symbol_context: {
      description:
        "Return the lines of engine source around a declaration, so the API surrounding a symbol "
        + "can be read without opening the file: the sibling overloads, the UPROPERTY above it, the "
        + "comment saying which of three similar methods to call. verify_symbols returns the "
        + "declaration line alone, which is the signature and nothing else; this is that line in "
        + "its neighbourhood. Accepts Class::Member as well as a bare type and resolves both "
        + "exactly as verify_symbols does. When the declaration opens a body that closes inside the "
        + "window the result ends at the closing brace instead of mid-type, and reports "
        + "bodyEndLine. Builds the index on first use, which can take several minutes on a cold "
        + "filesystem. "
        + "Params: symbol (bare or Class::Member), contextBefore? (lines before the declaration, "
        + "default 8), contextAfter? (lines after, default 40)",
      timeoutMs: 1_800_000,
      handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
        const symbol = (p.symbol as string | undefined)?.trim();
        if (!symbol) throw new Error("Missing 'symbol'");
        const { index, source } = requireEngineIndex(ctx);
        return {
          indexSource: source,
          engineRoot: index.engineRoot,
          ...symbolContext(index, symbol, {
            before: p.contextBefore as number | undefined,
            after: p.contextAfter as number | undefined,
          }),
        };
      },
    },
    lint_cpp_header: {
      description:
        "Check a header you just wrote against the engine it has to build against, and report what "
        + "the compiler would before the compiler runs. Covers the structural mistakes that produce "
        + "baffling Unreal build errors (a reflected type with no .generated.h include, a .generated.h "
        + "that is not last, a UCLASS or USTRUCT with no GENERATED_BODY, no #pragma once) and the "
        + "engine-facing ones (a symbol that does not exist, one used without its include, one whose "
        + "module is missing from Build.cs, one the engine deprecated). A forward declaration counts "
        + "as satisfying an include, since in a header it usually is. Run this after write_cpp_file "
        + "and before build_project. "
        + "Params: path (absolute, or relative to the project Source/), buildCsPath? (defaults to the owning module's)",
      timeoutMs: 1_800_000,
      handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
        ctx.project.ensureLoaded();
        const raw = p.path as string;
        if (!raw) throw new Error("Missing 'path'");
        const resolved = path.isAbsolute(raw)
          ? raw
          : path.join(ctx.project.projectDir ?? "", "Source", raw);
        if (!fs.existsSync(resolved)) throw new Error(`Header not found: ${resolved}`);
        const { index, source } = requireEngineIndex(ctx);
        const result = lintHeader(index, resolved, { buildCsPath: p.buildCsPath as string | undefined });
        return {
          indexSource: source,
          ...result,
          ok: result.findings.filter((f) => f.severity === "error").length === 0,
          errorCount: result.findings.filter((f) => f.severity === "error").length,
          warningCount: result.findings.filter((f) => f.severity === "warning").length,
        };
      },
    },
    add_module_dependency: {
      description:
        "Add a module to a target module's Build.cs dependency array. Params: moduleName (the Build.cs to edit - must exist in the project), dependency (module name to add, e.g. 'UMG'), access? ('public'|'private', default 'private'). Creates the corresponding AddRange block if missing. Rebuild required afterward.",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const moduleName = p.moduleName as string;
        const dependency = p.dependency as string;
        const access = ((p.access as string) || "private").toLowerCase();
        if (!moduleName || !dependency) throw new Error("Missing 'moduleName' and/or 'dependency'");
        if (access !== "public" && access !== "private") {
          throw new Error("'access' must be 'public' or 'private'");
        }

        const buildCs = path.join(ctx.project.projectDir!, "Source", moduleName, `${moduleName}.Build.cs`);
        if (!fs.existsSync(buildCs)) {
          throw new Error(`Build.cs not found for module '${moduleName}' at ${buildCs}`);
        }

        let content = fs.readFileSync(buildCs, "utf-8");
        const fieldName = access === "public" ? "PublicDependencyModuleNames" : "PrivateDependencyModuleNames";

        // Already present?
        const existingArrayRe = new RegExp(`${fieldName}\\.AddRange\\s*\\(\\s*new\\s+string\\s*\\[\\s*\\]\\s*\\{([\\s\\S]*?)\\}\\s*\\)\\s*;`, "m");
        const existingMatch = content.match(existingArrayRe);

        if (existingMatch) {
          const body = existingMatch[1];
          const entries = new Set<string>();
          for (const m of body.matchAll(/"([A-Za-z0-9_]+)"/g)) entries.add(m[1]);
          if (entries.has(dependency)) {
            return { status: "existed", buildCs, access, dependency };
          }
          entries.add(dependency);
          const sortedList = [...entries].sort();
          const replacement = `${fieldName}.AddRange(\n\t\t\tnew string[]\n\t\t\t{\n${sortedList.map(e => `\t\t\t\t"${e}",`).join("\n")}\n\t\t\t}\n\t\t);`;
          content = content.replace(existingArrayRe, replacement);
        } else {
          // Insert a new AddRange block before the closing brace of the ModuleRules ctor.
          const ctorCloseRe = /(\n\s*\}\s*\n\s*\})\s*$/;
          if (!ctorCloseRe.test(content)) {
            throw new Error(`Could not locate module ctor in ${buildCs} - edit manually.`);
          }
          const newBlock = `\n\t\t${fieldName}.AddRange(\n\t\t\tnew string[]\n\t\t\t{\n\t\t\t\t"${dependency}",\n\t\t\t}\n\t\t);\n`;
          content = content.replace(ctorCloseRe, `${newBlock}$1`);
        }

        fs.writeFileSync(buildCs, content, "utf-8");
        return {
          status: "updated",
          buildCs,
          access,
          dependency,
          hint: "Rebuild the project (project(build)) for the new dependency to take effect.",
        };
      },
    },

    add_cpp_member: {
      // #423: append a UPROPERTY / UFUNCTION declaration to an existing UCLASS
      // header in the right access-specifier block. The recurring trap is that
      // raw appending lands the declaration in whatever access section the
      // class happened to end in (often private:), which makes UHT reject
      // BlueprintReadWrite ("should not be used on private members"). This
      // handler inserts the requested access specifier before the declaration
      // and restores the previous one after, so the caller doesn't need to
      // know what section was active at the end of the class body.
      description:
        "Append a UPROPERTY/UFUNCTION declaration to an existing UCLASS header inside the access specifier you choose. Idempotent: if a declaration containing the same memberName is already present, returns existed:true. Params: headerPath (relative to Source/ or absolute), declaration (full multi-line UPROPERTY(...) / UFUNCTION(...) block plus its single-line member or function signature), memberName (the identifier the declaration introduces - used for idempotency), access? ('public'|'protected'|'private', default 'public').",
      handler: async (ctx, p) => {
        ctx.project.ensureLoaded();
        const headerPath = p.headerPath as string;
        const declaration = p.declaration as string;
        const memberName = p.memberName as string;
        const access = (((p.access as string) || "public").toLowerCase()) as "public" | "protected" | "private";
        if (!headerPath) throw new Error("Missing 'headerPath'");
        if (!declaration) throw new Error("Missing 'declaration'");
        if (!memberName) throw new Error("Missing 'memberName'");
        if (access !== "public" && access !== "protected" && access !== "private") {
          throw new Error("'access' must be 'public' | 'protected' | 'private'");
        }
        const sourceDir = path.join(ctx.project.projectDir!, "Source");
        const resolved = path.isAbsolute(headerPath) ? path.resolve(headerPath) : path.resolve(sourceDir, headerPath);
        const sourceAbs = path.resolve(sourceDir);
        if (!resolved.startsWith(sourceAbs + path.sep) && resolved !== sourceAbs) {
          throw new Error(`Refusing to write outside project Source/: ${resolved}`);
        }
        if (!/\.h$/i.test(resolved)) {
          throw new Error(`add_cpp_member only accepts .h files (got '${path.extname(resolved)}')`);
        }
        if (!fs.existsSync(resolved)) throw new Error(`Header not found: ${resolved}`);

        const original = fs.readFileSync(resolved, "utf-8");

        // Idempotency: does a declaration with this memberName already exist?
        // Match identifier as a whole word - tolerant of pointer/ref/const sigils.
        const wordRe = new RegExp(`\\b${memberName.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}\\b`);
        if (wordRe.test(original)) {
          return { status: "existed", path: resolved, memberName };
        }

        // Find the class's terminating "};" - last occurrence in the file is
        // the conservative choice; UCLASS headers rarely have nested types.
        const closeIdx = original.lastIndexOf("};");
        if (closeIdx < 0) {
          throw new Error(`Could not find class closing '};' in ${resolved}`);
        }

        // Walk backward from closeIdx to find the most recent access specifier.
        // Default to "private" if none found (C++ class default).
        const before = original.slice(0, closeIdx);
        const accessRe = /(^|\n)\s*(public|protected|private)\s*:\s*(\/\/[^\n]*)?\s*(?=\n)/g;
        let lastAccess: "public" | "protected" | "private" = "private";
        let m: RegExpExecArray | null;
        while ((m = accessRe.exec(before)) !== null) {
          lastAccess = m[2] as "public" | "protected" | "private";
        }

        // Indent the declaration to match the class body (one tab is the
        // convention used by UE templates).
        const indented = declaration
          .replace(/\r\n/g, "\n")
          .split("\n")
          .map(line => (line.length === 0 ? line : (line.startsWith("\t") ? line : `\t${line}`)))
          .join("\n");

        // If the requested access section already exists and is the most recent
        // one before the closing brace, we can append the declaration directly
        // without restoring a different prior access.
        const sameAsPrior = access === lastAccess;
        const insertion = sameAsPrior
          ? `\n${indented}\n`
          : `\n${access}:\n${indented}\n${lastAccess}:\n`;

        const updated = `${original.slice(0, closeIdx)}${insertion}${original.slice(closeIdx)}`;
        fs.writeFileSync(resolved, updated, "utf-8");
        return {
          status: "added",
          path: resolved,
          memberName,
          access,
          restoredPrior: sameAsPrior ? null : lastAccess,
          hint: "Call live_coding_compile to hot-reload, or build_project for a full rebuild.",
        };
      },
    },
  },
  undefined,
  {
    projectPath: z.string().optional().describe("For set_project / add_editor / check_install: path to .uproject"),
    editorName: z.string().optional().describe("For add_editor: name to address the new session by (default the project name) (#817)"),
    editorTarget: z.string().optional().describe("For use_editor / drop_editor: session name, project name, or .uproject path (#817)"),
    start: z.boolean().optional().describe("For add_editor: launch the editor for that project and wait until it is ready (#817)"),
    timeout: z.number().optional().describe("For add_editor with start: seconds to wait for readiness (default 300)"),
    configName: z.string().optional().describe("For read_config/set_config: config file name"),
    query: z.string().optional().describe("For search_config/search_cpp: search text"),
    headerPath: z.string().optional().describe("For read_cpp_header: path to .h file"),
    moduleName: z.string().optional().describe("For read_module / is_module_loaded: module name"),
    filter: z.string().optional().describe("For list_loaded_modules and list_available_plugins: case-insensitive name substring (#689)"),
    pluginName: z.string().optional().describe("enable_plugin / disable_plugin: plugin name as it appears in list_available_plugins, matched case-insensitively"),
    removeReference: z.boolean().optional().describe("disable_plugin: delete the .uproject entry entirely instead of writing an explicit disable (default false)"),
    pluginCategory: z.string().optional().describe("list_available_plugins: case-insensitive substring of the plugin's category"),
    enabledOnly: z.boolean().optional().describe("list_available_plugins: only plugins enabled in this editor session"),
    loadedOnly: z.boolean().optional().describe("For list_loaded_modules: only loaded modules (#689)"),
    limit: z.number().optional().describe("Max results: search_tools (default 20), find_example_usage (10), find_references (40), find_callers (25), find_callees (100), class_hierarchy descendants (100). The paged list actions: rows on this page (#704)"),
    // The paged list actions in this category resume on a cursor. `limit`
    // is already declared above and shared with the unpaged readers.
    cursor: CURSOR_PARAM,
    name: z.string().optional().describe("describe_action: the action to describe, as 'tool.action' or a bare action name"),
    category: z.string().optional().describe("describe_action / list_available_actions: narrow to one category instead of the whole surface"),
    includeNames: z.boolean().optional().describe("list_available_actions: list the action names, not just the counts (default false)"),
    // Stays a strict enum on purpose: listAvailableActions treats anything that
    // is neither "all" nor "available" as "blocked", so nothing rejects a typo.
    // Relaxing it would answer a request for the available half with the
    // blocked half and report success.
    state: z.enum(["available", "blocked", "all"]).optional().describe("list_available_actions: which side of the line to list when includeNames=true (default available)"),
    skipToolchain: z.boolean().optional().describe("check_install: skip the C++ toolchain probe, which shells out to vswhere or the compiler"),
    contentPath: z.string().optional().describe("list_content_assets: mount path to list, e.g. /Game or /Game/Characters (default /Game)"),
    namePattern: z.string().optional().describe("list_content_assets: case-insensitive substring the asset name must contain"),
    extensions: z.union([z.string(), z.array(z.string())]).optional().describe("For list_files: extension filter (#608)"),
    recursive: z.boolean().optional().describe("For list_files / list_content_assets: recurse into subdirectories (#608)"),
    directory: z.string().optional().describe("For search_cpp: subdirectory"),
    section: z.string().optional().describe("For set_config: INI section"),
    key: z.string().optional().describe("For set_config: INI key"),
    value: z.string().optional().describe("For set_config: INI value"),
    configuration: z.string().optional().describe("Build configuration: Development, Debug, Shipping"),
    platform: z.string().optional().describe("Target platform: Win64, Linux, Mac"),
    clean: z.boolean().optional().describe("Clean build"),
    symbol: z.string().optional().describe("Symbol name for find_engine_symbol / find_example_usage / class_hierarchy / find_references / find_callers / find_callees / symbol_context"),
    names: z.union([z.string(), z.array(z.string())]).optional().describe("verify_symbols / suggest_build_deps: engine symbol names, as an array or a comma-separated string (max 200)"),
    refresh: z.boolean().optional().describe("build_engine_index: rebuild even when a valid cache exists"),
    buildCsPath: z.string().optional().describe("suggest_build_deps / lint_cpp_header: absolute path to a .Build.cs (defaults to the one owning the target)"),
    modulePath: z.string().optional().describe("suggest_build_deps: a file or directory whose owning Build.cs to read"),
    trees: z.union([z.string(), z.array(z.string())]).optional().describe("find_example_usage / find_references / find_callers / find_callees: engine trees to search - Runtime|Editor|Developer|Plugins|all (default Runtime)"),
    // Stays a strict enum on purpose: classHierarchy tests the value against
    // "ancestors" and "descendants" by inequality, so any other string walks
    // both directions and the caller is never told its value was not understood.
    direction: z.enum(["ancestors", "descendants", "both"]).optional().describe("class_hierarchy: walk up, down, or both (default both)"),
    depth: z.number().optional().describe("class_hierarchy: generations of descendants to report (default 1; every transitive subclass of UObject is tens of thousands of names)"),
    includeProject: z.boolean().optional().describe("find_references / find_callers: also search this project's own Source and Plugins trees (default true)"),
    contextBefore: z.number().optional().describe("symbol_context: lines of source before the declaration (default 8)"),
    contextAfter: z.number().optional().describe("symbol_context: lines of source after the declaration (default 40)"),
    maxResults: z.number().optional().describe("Cap on find_engine_symbol / search_engine_cpp / list_content_assets hits (default 100 / 500 / 1000)"),
    tree: z.string().optional().describe("For search_engine_cpp: Runtime|Editor|Developer|Plugins|all (default Runtime)"),
    subdirectory: z.string().optional().describe("For search_engine_cpp: subdirectory within the chosen tree"),

    // v0.7.13 - native C++ authoring
    className: z.string().optional().describe("For create_cpp_class: new class name (no A/U prefix - handled by parent type)"),
    parentClass: z.string().optional().describe("For create_cpp_class: parent UClass. Short native names ('Actor') or /Script/<Module>.<Class> paths work. Default UObject."),
    // Stays a strict enum on purpose: create_cpp_class maps "private" and
    // "classes" to their folders and falls through to Public for everything
    // else, so a typo would write the header into the wrong folder and report
    // the class as created.
    classDomain: z.enum(["public", "private", "classes"]).optional().describe("For create_cpp_class: which folder under the module (Public/Private/Classes). Default 'public'."),
    subPath: z.string().optional().describe("For create_cpp_class: nested folder under the class domain (e.g. 'Gameplay/Abilities')."),
    wait: z.boolean().optional().describe("For live_coding_compile: block until compile finishes. Default false."),
    path: z.string().optional().describe("For write_cpp_file: path to write (relative to Source/ or absolute within Source/)."),
    content: z.string().optional().describe("For write_cpp_file: full file contents."),
    sourcePath: z.string().optional().describe("For read_cpp_source: path to .cpp (relative to Source/ or absolute)."),
    module: z.string().optional().describe("For write_source_file/read_source_file: module name (default project's primary module). Plugin modules are resolved too (#543)."),
    visibility: z.string().optional().describe("For write_source_file/read_source_file: Public or Private (default Private on write)."),
    fileName: z.string().optional().describe("For write_source_file/read_source_file: file name e.g. MyComponent.h."),
    dependency: z.string().optional().describe("For add_module_dependency: module name to add (e.g. 'UMG')."),
    declaration: z.string().optional().describe("For add_cpp_member: full UPROPERTY(...) / UFUNCTION(...) block plus the member or function signature."),
    memberName: z.string().optional().describe("For add_cpp_member: the identifier the declaration introduces (used for idempotency)."),
    // Deliberately a string, not z.enum. The MCP SDK validates arguments BEFORE
    // the tool callback runs, so a strict enum makes a typo fail at the transport
    // with a schema error, and the handler's own message, which names both valid
    // values, never reaches the caller. add_module_dependency rejects an unknown
    // access by name.
    access: z.string().optional().describe("For add_module_dependency: 'public' (PublicDependencyModuleNames) or 'private' (default)."),
    profileName: z.string().optional().describe("For resolve_collision_profile: the profile to resolve, e.g. 'Pawn', 'BlockAll', or one the project defined."),
    channel: z.string().optional().describe("For resolve_collision_profile: narrow the answer to one channel. Accepts the configured name ('Camera', 'Weapon'), the C++ enumerator ('ECC_Camera'), or the container index."),
    includeAllChannels: z.boolean().optional().describe("For resolve_collision_profile: include the unused GameTraceChannel slots as well as the engine channels and the project's own. Default false."),
  },
);
