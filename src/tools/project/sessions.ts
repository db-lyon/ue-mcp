import { checkPluginFreshness } from "../../editor/bridge-freshness.js";
import { checkBridgeParity, deployedPlugin } from "../../bridge/bridge-parity.js";
import * as fs from "node:fs";
import { deploy, deploySummary, attach, attachSummary } from "../../editor/deployer.js";
import { collapsingEnvWarnings } from "../../config/session-env.js";
import { startEditor, isBridgeReachable, connectedEditorOf } from "../../editor/editor-control.js";
import { detectProjectHolders } from "../../editor/project-holders.js";
import { readDeployedBridgeApiVersion } from "../../extensions/bridge-api.js";
import { CLIENT_PROTOCOL_VERSION, describeProtocolMismatch } from "../../bridge/bridge.js";
import { readLogState, readEngineSnapshot } from "../../editor/engine-observer.js";
import { switchProject, isTargetDiverged } from "../../sessions/project-switch.js";
import { ueMcpConfigRejections, describeConfigRejections } from "../../config/project.js";
import type { ToolContext, ActionSpec } from "../../core/types.js";
import { toolGraphOf } from "../../surface/target-params.js";
import { startProgress } from "../../cli/ui/progress.js";

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

/** Editor sessions: status, the project this session drives, and the editors the server holds. */
export const sessionActions: Record<string, ActionSpec> = {
  get_status: {
    kind: "handler",
    effect: "read",
    description: "Check server mode and editor connection. pluginBuildStale reports the compiled bridge being older than its source, read from disk. deployedPlugin is what the binary that answered says about itself: when it was built, and how many methods this server advertises that it does not register, which is what 'Unknown method' on a real action means. answeringPid is the editor process the bridge is connected to; projectEditors and projectEditorsWarning appear when several editor processes (headless included) hold the project, and every response then carries the answering pid. Params: none (#785, #1002, #1021, #1150)",
    handler: async (ctx) => {
      const flows = ctx.getFlows?.() ?? [];
      const bridgeApiVersion = ctx.project.projectDir
        ? readDeployedBridgeApiVersion(ctx.project.projectDir)
        : null;
      // #785: surface staleness on the first call agents make, so an
      // "Unknown method" later is read as a stale build rather than a
      // missing feature.
      const freshness = checkPluginFreshness(ctx.project.projectPath ?? null);

      // #1021: staleness is a timestamp comparison and says nothing about
      // the handlers themselves. A session reported pluginBuildStale:false
      // while an advertised method was absent, and every discovery of that
      // came one failed call at a time. The handshake carries the method
      // list the running binary registered, so the surface is compared
      // against it here instead of being taken on trust.
      const parity = checkBridgeParity(toolGraphOf(ctx), ctx.bridge.capabilities);

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

      // #1150: with several editors on one project, name the one answering.
      const answeringPid = connectedEditorOf(ctx.bridge)?.pid ?? null;
      const holders = await detectProjectHolders(ctx.project.projectDir, ctx.project.projectPath, answeringPid);

      return {
        engine: offlineEngine ?? undefined,
        pluginBuildStale: freshness.checked ? freshness.stale : undefined,
        pluginBuildWarning: freshness.stale ? freshness.message : undefined,
        // Absent when there is nothing to say, so a healthy session's status
        // is exactly what it was. Present, it names methods that will come
        // back "Unknown method" before one is called.
        // What the binary that ANSWERED says about itself, as opposed to
        // pluginBuildStale and bridgeApiVersion above, which are read off
        // the source and the header on disk. architecture.md already draws
        // that line; this is the running-binary side of it, in one place
        // rather than as more sibling flags.
        deployedPlugin: deployedPlugin(ctx.bridge.capabilities, parity),
        mode: ctx.bridge.isConnected ? "live" : "disconnected",
        editorConnected: ctx.bridge.isConnected,
        answeringPid: answeringPid ?? undefined,
        editorTarget: {
          projectPath: target.projectPath,
          port: target.port,
          portSource: target.portSource,
        },
        // Absent unless more than one editor process holds this project.
        projectEditors: holders ? { count: holders.count, editors: holders.editors } : undefined,
        projectEditorsWarning: holders?.warning,
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
    kind: "handler",
    effect: "mutate",
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
    kind: "handler",
    effect: "read",
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
    kind: "handler",
    effect: "mutate",
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
    kind: "handler",
    effect: "mutate",
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
        started = await startEditor(session.project, timeout, ctx.onProgress, { openDisplay: startProgress });
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
    kind: "handler",
    effect: "mutate",
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
    kind: "handler",
    effect: "read",
    description: "Read .uproject file details. Params: none",
    handler: async (ctx) => {
      ctx.project.ensureLoaded();
      return { projectName: ctx.project.projectName, engineAssociation: ctx.project.engineAssociation, contentDir: ctx.project.contentDir, uprojectContents: JSON.parse(fs.readFileSync(ctx.project.projectPath!, "utf-8")) };
    },
  },
};
