/**
 * Which project a hook invocation is about (#817, plan 6.3).
 *
 * `ue-mcp hook` is a one-shot process: stdin carries the payload, argv carries
 * the event, and there is no session registry to ask, because the registry only
 * exists inside a running server. Until now the only signal it had was its own
 * cwd, which is the agent's working directory and has no reason to be the
 * project the tool call ran against. With more than one editor registered that
 * stopped being a detail: a call made against editor B would be judged by
 * whatever ue-mcp.yml happened to sit above the agent's cwd, which could be
 * project A's, or nothing at all.
 *
 * So the payload is read first and the project comes from the call itself
 * wherever the call said. The silent no-op is preserved exactly: when nothing
 * resolves to a ue-mcp project, the hook stays quiet. A hook running outside
 * its own project is stale, and nudging an unrelated repo is worse than
 * missing a nudge.
 */
import * as fs from "node:fs";
import * as path from "node:path";

/** The fields of a hook payload this cares about. Everything else passes through. */
export interface HookPayload {
  tool_name?: string;
  tool_input?: Record<string, unknown>;
  /** The agent's working directory, as the client reports it. */
  cwd?: string;
  /** Some clients name the project root directly. */
  project_dir?: string;
  projectDir?: string;
  [key: string]: unknown;
}

export interface HookProject {
  /** Directory to look for a ue-mcp.yml above, or null when nothing named one. */
  dir: string | null;
  /** Where that came from, for the diagnostic log. */
  source: string;
}

/**
 * The directory a hook payload is about.
 *
 * Order is most specific first: a project path the call itself carried, then an
 * editor the call addressed by name, then the client's reported cwd, then this
 * process's cwd. Every step is best-effort; a step that cannot answer hands on
 * to the next rather than failing the hook.
 */
export async function hookProjectDir(
  payload: HookPayload,
  processCwd: string = process.cwd(),
  resolveEditor: (name: string) => Promise<string> = defaultResolveEditor,
): Promise<HookProject> {
  const input = payload.tool_input ?? {};

  // A call that named a project named it exactly. `project(set_project)` and
  // `project(add_editor)` both carry projectPath.
  const explicit =
    str(input.projectPath) ?? str(payload.project_dir) ?? str(payload.projectDir);
  if (explicit) {
    const dir = directoryOf(explicit);
    if (dir) return { dir, source: "payload project path" };
  }

  // A call that addressed an editor named a session, which resolves through the
  // same MCP client config the server was started from (see editor-flag.ts).
  const editor = str(input.editor);
  if (editor) {
    try {
      const dir = directoryOf(await resolveEditor(editor));
      if (dir) return { dir, source: `payload editor '${editor}'` };
    } catch {
      // An editor this machine's configs do not know is not a reason to fail;
      // fall through to the cwd signals below.
    }
  }

  const payloadCwd = str(payload.cwd);
  if (payloadCwd && isDirectory(payloadCwd)) return { dir: payloadCwd, source: "payload cwd" };

  return { dir: processCwd, source: "process cwd" };
}

/**
 * Resolving an editor name reads every MCP client config on the machine, and
 * almost no hook invocation needs it. This is a one-shot process on the
 * tool-call path, so the import is deferred to the calls that name an editor.
 */
async function defaultResolveEditor(name: string): Promise<string> {
  const { resolveEditorFlag } = await import("./editor-flag.js");
  return resolveEditorFlag(name);
}

/** The project directory for a .uproject path, a directory, or null. */
function directoryOf(target: string): string | null {
  const resolved = path.resolve(target);
  if (resolved.toLowerCase().endsWith(".uproject")) return path.dirname(resolved);
  return isDirectory(resolved) ? resolved : null;
}

function isDirectory(target: string): boolean {
  try {
    return fs.statSync(target).isDirectory();
  } catch {
    return false;
  }
}

function str(v: unknown): string | null {
  return typeof v === "string" && v.trim() !== "" ? v.trim() : null;
}

/**
 * Walk up from `dir` looking for a ue-mcp.yml, and report whether the feedback
 * hook should stay silent.
 *
 * True means silent, and it is the answer for three different situations, all
 * of which want the same behaviour: the config disables feedback, the config is
 * malformed, or there is no ue-mcp project above this directory at all. The
 * last is the one that matters here and it is deliberate: a hook that fires in
 * an unrelated repo is a stale install talking, and it is preserved unchanged
 * from before hooks knew about sessions.
 */
export async function feedbackDisabledForDir(dir: string | null): Promise<boolean> {
  if (!dir) return true;
  try {
    const yaml = (await import("js-yaml")).default;
    let cursor = dir;
    for (let i = 0; i < 32; i++) {
      const ymlPath = path.join(cursor, "ue-mcp.yml");
      if (fs.existsSync(ymlPath)) {
        try {
          const doc = yaml.load(fs.readFileSync(ymlPath, "utf-8")) as
            | { "ue-mcp"?: { disable?: unknown } }
            | null;
          const block = doc && typeof doc === "object" ? doc["ue-mcp"] : undefined;
          const list = block && Array.isArray(block.disable) ? block.disable : [];
          return list.includes("feedback");
        } catch {
          // Malformed config: don't nudge, safer to no-op.
          return true;
        }
      }
      const parent = path.dirname(cursor);
      if (parent === cursor) break;
      cursor = parent;
    }
    return true;
  } catch {
    return true;
  }
}
