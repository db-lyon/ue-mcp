/**
 * Every module under src/ has been placed (#817, plan 6.8).
 *
 * Multi-editor work is a per-module question: does this thing have to be told
 * which editor, or is one answer right for the whole process? Answering it
 * module by module was how the per-session state, the CLI targeting and the
 * config layering got done, and the risk on the other side of that work is a
 * new module that nobody asks the question about, quietly holding one editor's
 * state for all of them.
 *
 * So the answer is written down here, once per module, and a module in neither
 * list fails the build. The lists are:
 *
 *   PER_SESSION           Holds per-editor state, is built once per session, or
 *                         is handed a session / project / bridge and answers
 *                         differently for each. It has to be told which editor.
 *   SESSION_INDEPENDENT   Pure logic over its arguments, shared declarations,
 *                         process-wide infrastructure, or user and machine
 *                         scoped state that is deliberately not per editor.
 *
 * An entry is an exact path, or a directory prefix when every module under it
 * has the same answer for the same reason. Every module must match exactly one.
 */
import { describe, it, expect } from "vitest";
import { readdirSync, statSync } from "node:fs";
import { dirname, join, relative, sep } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const srcDir = join(here, "..", "..", "src");

/** Handed a session, a project or a bridge, or holding state keyed by one. */
const PER_SESSION: Record<string, string> = {
  "bridge.ts": "One socket, port and lockfile per editor.",
  "session.ts": "The registry of editors itself.",
  "session-surface.ts": "One tool graph per editor, and the union advertised from them.",
  "session-env.ts": "Reports which environment variables flatten the registered editors.",
  "editor-gate.ts": "Routes a call to an editor and refuses an untargeted change.",
  "editor-control.ts": "Starts, stops and builds one project's editor process.",
  "editor-target.ts": "Resolves one project's bridge from its own port lockfile.",
  "editor-flag.ts": "Turns --editor into the project a one-shot command acts on.",
  "engine-observer.ts": "Observes the editor process that has one project open.",
  "project.ts": "One ProjectContext per session, with that project's config cascade.",
  "project-switch.ts": "Moves one session's project and socket together.",
  "port.ts": "Derives a bridge port from one project's root path.",
  "requested-port.ts": "Publishes one project's resolved port pin into that project's own Saved directory.",
  "deployer.ts": "Attaches and deploys the bridge into one project.",
  "plugin-freshness.ts": "Compares deployed against compiled inside one project.",
  "epic-cache.ts": "Catalog cache keyed by project directory.",
  "epic-enrich.ts": "Enriches one session's graph from that editor's catalog.",
  "locking.ts": "The lock registry lives in the addressed editor's bridge, under that session's owner id.",
  "index.ts": "Builds the registry and dispatches each call into the editor it addressed.",
  "hook-handler.ts": "Runs against the project the hook payload named.",
  "hook-session.ts": "Resolves which project a hook invocation is about.",
  "hook-installer.ts": "Installs the hook for one project.",
  "workaround-tracker.ts": "The workaround stack is partitioned per session, because it is posted publicly.",
  "privacy-scrub.ts": "Redacts the other registered roots and session names.",
  "feedback-routing.ts": "Routes using the addressed session's plugin set.",
  "feedback-deferred.ts": "Deferred entries record the editor they came from and are filtered by it.",
  "user-state.ts": "Keyed by absolute project root: installed hooks, and the per-project feedback mode.",
  "mcp-client-config.ts": "Writes the project positionals a server is started with.",
  "doctor.ts": "Reports every project the configured invocation names.",
  "init.ts": "Writes one project's config, chosen with --editor.",
  "update.ts": "Updates one project's deployed bridge, chosen with --editor.",
  "uninstall-hooks.ts": "Removes one project's hooks, chosen with --editor.",
  "resolve.ts": "Runs the issue workflow against one project, chosen with --editor.",
  "build-cli.ts": "Builds one project, chosen with --editor.",
  "deploy-cli.ts": "Deploys into one project, chosen with --editor.",
  "context-cli.ts": "Writes one project's context strategy, chosen with --editor.",
  "plugin-cli.ts": "Installs and inspects plugins for one project, chosen with --editor.",
  "feedback-cli.ts": "Scopes the deferred queue and the feedback mode with --editor.",
  "skills.ts": "Installs and removes agent skills inside one project directory.",

  "flow/context.ts": "Carries the session a flow step runs in.",
  "flow/flow-tool.ts": "Resolves the registry and config of the addressed editor.",
  "flow/registry.ts": "One task registry per session, built from that project's graph.",
  "flow/task-factory.ts": "Builds tasks that dispatch on the context's session bridge.",
  "flow/bridge-task.ts": "Calls the addressed session's bridge.",
  "flow/guard.ts": "One guard registry per session; a project's guards do not veto another's calls.",
  "flow/guarded-bridge.ts": "Wraps one session's bridge in that session's guards.",
  "flow/task-guards.ts": "Discovers guards from one session's registry, against that session's raw bridge.",
  "flow/loader.ts": "Loads each project's own ue-mcp.yml.",
  "flow/git-snapshot.ts": "Snapshots the repository holding one project, through its bridge.",
  "flow/http-server.ts": "Resolves an editor per request and refuses an untargeted run.",

  "plugin/loader.ts": "Loads each project's plugins into that session's graph.",
  "plugin/resolver.ts": "Resolves packages from one project's node_modules.",
  "plugin/native-deploy.ts": "Deploys a native module into one project.",
  "plugin/plugin-config-store.ts": "Reads and writes one project's config layers.",
  "plugin/bridge-api.ts": "Reads the bridge ABI version deployed in one project.",
};

/** No editor concept, or one deliberate answer for the whole process. */
const SESSION_INDEPENDENT: Record<string, string> = {
  "action-class.ts": "A static classification of the action surface; the same answer in every editor.",
  "asset-path.ts": "Pure Unreal path handling.",
  "auth.ts": "GitHub identity for feedback authorship: per user, not per project.",
  "auth-cli.ts": "Runs that per-user device flow.",
  "login-cli.ts": "Registry login, per user.",
  "registry-auth.ts": "The plugin registry token, per user.",
  "registry-catalog.ts": "The plugin registry itself, which no project owns.",
  "github-app.ts": "Posts to the ue-mcp tracker, never to the user's project.",
  "secret-scrub.ts": "Pure redaction with no context to scope.",
  "client-quirks.ts": "About the connected MCP client, not about any editor.",
  "config-parser.ts": "Pure ini utility over whatever directory it is handed.",
  "cpp-parser.ts": "Pure header inspection over whatever file it is handed.",
  "function-args.ts": "Pure argument coercion.",
  "errors.ts": "Shared error codes and the McpError shape.",
  "schemas.ts": "Shared schema declarations.",
  "types.ts": "Shared declarations. The session-aware helpers here take the session and hold nothing.",
  "task.ts": "The public task-authoring surface, which is types.",
  "tools.ts": "The pristine tool declaration plus the union the server advertises; per-session graphs are clones of it, built in session-surface.",
  "tool-search.ts": "Searches the graph it is handed.",
  "lean-context.ts": "Pure transforms over a graph. The strategy is one answer per transport, since there is one transport.",
  "instructions.ts": "The initialize payload is sent once per process and cannot be renegotiated.",
  "global-config.ts": "The user-global config layer, which applies to every project by definition.",
  "log.ts": "Process-wide stderr logging.",
  "version-check.ts": "One npm check per process; the upgrade notice is emitted once.",
  "yaml-dump.ts": "Pure YAML serialization of the value it is handed.",

  "flow/events.ts": "One process-wide event bus. Which editor a run belongs to rides on the event, not here.",
  "flow/schema.ts": "Shared flow schema declarations.",
  "flow/rollback.ts": "Pure shaping of a rollback record.",
  "flow/write-methods.ts": "Pure classification of a bridge method name.",
  "flow/index.ts": "Re-export barrel with no behaviour.",

  "plugin/manifest.ts": "Parses and validates a plugin manifest file.",
  "plugin/injection.ts": "Builds an injection plan from a manifest.",
  "plugin/provision.ts": "Builds a provided category from a manifest.",
  "plugin/plugin-groups.ts": "Pure group logic over the config it is handed.",
  "plugin/version.ts": "Semver comparison for the minServerVersion gate.",
};

/** Directories where every module has the same answer for the same reason. */
const PER_SESSION_DIRS: Record<string, string> = {
  "tools/": "Every category tool is cloned per session and dispatches against the addressed editor.",
};

const SESSION_INDEPENDENT_DIRS: Record<string, string> = {
  "ui/": "Terminal rendering for the CLI. No editor is involved.",
};

function listModules(dir: string): string[] {
  const out: string[] = [];
  for (const entry of readdirSync(dir)) {
    const full = join(dir, entry);
    if (statSync(full).isDirectory()) {
      out.push(...listModules(full));
      continue;
    }
    if (entry.endsWith(".ts") && !entry.endsWith(".d.ts")) {
      out.push(relative(srcDir, full).split(sep).join("/"));
    }
  }
  return out.sort();
}

function matches(module: string, exact: Record<string, string>, dirs: Record<string, string>): boolean {
  if (module in exact) return true;
  return Object.keys(dirs).some((prefix) => module.startsWith(prefix));
}

describe("module inventory", () => {
  const modules = listModules(srcDir);

  it("finds the modules to place", () => {
    expect(modules.length).toBeGreaterThan(100);
    expect(modules).toContain("index.ts");
    expect(modules).toContain("tools/asset.ts");
  });

  it("places every module in exactly one list", () => {
    const unplaced: string[] = [];
    const doubled: string[] = [];
    for (const module of modules) {
      const per = matches(module, PER_SESSION, PER_SESSION_DIRS);
      const independent = matches(module, SESSION_INDEPENDENT, SESSION_INDEPENDENT_DIRS);
      if (per && independent) doubled.push(module);
      else if (!per && !independent) unplaced.push(module);
    }

    if (unplaced.length > 0) {
      throw new Error(
        `${unplaced.length} module(s) under src/ have not been placed:\n` +
          unplaced.map((m) => `  ${m}`).join("\n") +
          `\n\nAdd each to PER_SESSION or SESSION_INDEPENDENT in ` +
          `tests/unit/module-inventory.test.ts with the reason. The question is ` +
          `whether it has to be told which editor it is acting on (#817).`,
      );
    }
    expect(doubled, "listed in both lists").toEqual([]);
    expect(unplaced).toEqual([]);
  });

  it("lists no module that no longer exists", () => {
    const present = new Set(modules);
    const ghosts = [...Object.keys(PER_SESSION), ...Object.keys(SESSION_INDEPENDENT)].filter(
      (m) => !present.has(m),
    );
    expect(ghosts, "listed but absent from src/").toEqual([]);
  });

  it("gives a reason for every placement", () => {
    const entries = [
      ...Object.entries(PER_SESSION),
      ...Object.entries(SESSION_INDEPENDENT),
      ...Object.entries(PER_SESSION_DIRS),
      ...Object.entries(SESSION_INDEPENDENT_DIRS),
    ];
    const empty = entries.filter(([, reason]) => reason.trim().length < 20);
    expect(empty.map(([m]) => m), "placed with no usable reason").toEqual([]);
  });

  it("covers every directory prefix it claims", () => {
    for (const prefix of [...Object.keys(PER_SESSION_DIRS), ...Object.keys(SESSION_INDEPENDENT_DIRS)]) {
      expect(
        modules.some((m) => m.startsWith(prefix)),
        `${prefix} claims a directory with no modules in it`,
      ).toBe(true);
    }
  });
});
