/**
 * Editor sessions - one server, an arbitrary number of editors (#817).
 *
 * A session binds four things that must always describe the SAME editor:
 * the resolved project, the bridge socket, the port lockfile that socket is
 * discovered through, and the guard pipeline wrapped around it. Keeping them
 * in one object is what makes "path resolution and bridge calls target the
 * same project" a structural property instead of an invariant nothing
 * enforces (#818).
 *
 * Sessions are keyed by resolved project root, lowercased with forward
 * slashes - the same normalization the bridge port derivation uses, so a
 * project reached through a trailing slash, a backslash path, or different
 * drive-letter casing resolves to one session rather than two competing for
 * one editor.
 *
 * A server started with no project keeps exactly one session with no project
 * bound, on the legacy fixed port, which is the documented "attach to
 * whatever answers 9877" path. A server started with one project keeps one
 * session and behaves as it always has: nothing here is conditional on the
 * count except targeting itself.
 */
import * as path from "node:path";
import { EditorBridge } from "./bridge.js";
import { ProjectContext } from "./project.js";
import { GuardedBridge } from "./flow/guarded-bridge.js";
import { GuardRegistry } from "./flow/guard.js";
import { makeResolveExistingFile } from "./flow/task-guards.js";
import { normalizeProjectRoot } from "./port.js";
import { McpError, ErrorCode } from "./errors.js";
import { warn } from "./log.js";
import { newLockOwnerId } from "./locking.js";

/** Key used for the session that has no project bound. */
export const DEFAULT_SESSION_KEY = "";
/** Name of the session that has no project bound. */
export const DEFAULT_SESSION_NAME = "default";

/** One editor's reported state, as `project(list_editors)` returns it. */
export interface EditorSessionInfo {
  name: string;
  projectName: string | null;
  projectPath: string | null;
  /** Port this session will connect on (lockfile value once the editor published one). */
  port: number;
  connected: boolean;
  /** True for the session untargeted calls fall through to. */
  active: boolean;
  /** Other session names resolving to the same port. Targeting is ambiguous while non-empty. */
  portSharedWith?: string[];
}

/**
 * Normalize a .uproject path or a project directory to the registry key.
 * Accepts either form because every caller has one or the other, and a
 * session addressed by directory must find the session registered by file.
 */
export function sessionKeyFor(projectPathOrDir: string): string {
  const resolved = path.resolve(projectPathOrDir);
  const dir = resolved.toLowerCase().endsWith(".uproject") ? path.dirname(resolved) : resolved;
  return normalizeProjectRoot(dir);
}

export class EditorSession {
  /** Raw bridge: connection lifecycle, port, lockfile. */
  readonly bridge: EditorBridge;
  /** What tools and tasks see - the guard pipeline wrapped around `bridge`. */
  readonly guarded: GuardedBridge;
  /** Names of other sessions that resolved to the same port. */
  portSharedWith: string[] = [];
  /**
   * Who this editor's asset locks belong to (#817).
   *
   * The lock registry lives in the bridge, which is per editor, so the holder
   * has to be per editor as well. One id shared across sessions would make a
   * lock taken in one editor read as re-entrant in another, which defeats the
   * point of taking it.
   */
  readonly lockOwnerId: string = newLockOwnerId();

  constructor(
    public name: string,
    /** Resolved project root. Moves when the session's project moves. */
    public key: string,
    readonly project: ProjectContext,
    /** This session's own guard pipeline. Guards from one project's plugins
     *  must not veto another project's calls, so each session has its own. */
    readonly guards: GuardRegistry,
  ) {
    this.bridge = new EditorBridge();
    // Order matters: setConfigPort marks the port as config-pinned, which is
    // the only thing stopping setProjectContext from overwriting an explicit
    // `bridge.port` with the derived per-project one.
    this.bridge.setConfigPort(this.project.config.bridge?.port);
    this.bridge.setConfigHost(this.project.config.bridge?.host);
    this.bridge.setProjectContext(this.project.projectPath);
    this.guarded = new GuardedBridge(
      this.bridge,
      guards,
      makeResolveExistingFile(this.project),
      this,
    );
  }

  get projectDir(): string | null {
    return this.project.projectDir;
  }

  get hasProject(): boolean {
    return this.project.isLoaded;
  }

  info(active: boolean): EditorSessionInfo {
    return {
      name: this.name,
      projectName: this.project.projectName,
      projectPath: this.project.projectPath,
      port: this.bridge.port,
      connected: this.bridge.isConnected,
      active,
      portSharedWith: this.portSharedWith.length > 0 ? [...this.portSharedWith] : undefined,
    };
  }
}

export interface RegisterSessionInput {
  /** .uproject file or the directory holding one. Omit for the project-less default session. */
  projectPath?: string | null;
  /** Addressable handle. Defaults to the project name, de-duplicated. */
  name?: string;
  /** Make this the session untargeted calls fall through to. */
  makeActive?: boolean;
}

/**
 * The set of editors this server drives. Ordered: the first registered
 * session is the default target, and stays so until `use` moves it.
 */
export class SessionRegistry {
  private readonly byKey = new Map<string, EditorSession>();
  private activeKey: string | null = null;
  /** Fired whenever the session set changes, so the caller can re-advertise. */
  onCountChanged?: (count: number) => void;
  /** Held while a compound edit is mid-flight, so observers see one change. */
  private suppressNotify = false;

  /**
   * `guards` is the pipeline the FIRST session gets, so a caller that already
   * built one (the server, which populates it after the task registries exist)
   * keeps working unchanged at one editor. Every session after the first gets
   * its own, because a guard declared by one project's plugins has no business
   * running on another project's calls.
   */
  constructor(private readonly guards: GuardRegistry = new GuardRegistry()) {}

  private guardsForNewSession(): GuardRegistry {
    return this.byKey.size === 0 ? this.guards : new GuardRegistry();
  }

  get size(): number {
    return this.byKey.size;
  }

  list(): EditorSession[] {
    return [...this.byKey.values()];
  }

  /** The session untargeted calls fall through to. Throws only when nothing is registered. */
  get active(): EditorSession {
    const session = this.activeKey !== null ? this.byKey.get(this.activeKey) : undefined;
    if (session) return session;
    const first = this.byKey.values().next().value as EditorSession | undefined;
    if (!first) {
      throw new McpError(ErrorCode.PROJECT_NOT_LOADED, "No editor session is registered.");
    }
    this.activeKey = first.key;
    return first;
  }

  /**
   * Register a project as a session. Idempotent per project root: registering
   * a path that is already bound returns the existing session rather than a
   * second one competing for the same editor.
   */
  register(input: RegisterSessionInput = {}): EditorSession {
    const project = new ProjectContext();
    let key = DEFAULT_SESSION_KEY;
    if (input.projectPath) {
      project.setProject(input.projectPath);
      key = sessionKeyFor(project.projectPath!);
    }

    const existing = this.byKey.get(key);
    if (existing) {
      if (input.makeActive) this.activeKey = key;
      return existing;
    }

    const name = this.uniqueName(input.name ?? project.projectName ?? DEFAULT_SESSION_NAME);
    const session = new EditorSession(name, key, project, this.guardsForNewSession());
    this.byKey.set(key, session);
    if (input.makeActive || this.activeKey === null) this.activeKey = key;
    this.noteSharedPorts(session);
    if (!this.suppressNotify) this.onCountChanged?.(this.byKey.size);
    return session;
  }

  /**
   * Resolve a target to a session. An empty target is the active session, so
   * every existing single-editor caller keeps landing where it always did.
   * Accepts a session name, a project name, a .uproject path, or a project
   * directory - whichever the caller happens to hold.
   */
  resolve(target?: unknown): EditorSession {
    if (target === undefined || target === null || target === "") return this.active;
    if (typeof target !== "string") {
      throw new McpError(ErrorCode.INVALID_PARAMS, `'editor' must be a session name or project path, got ${typeof target}`);
    }
    const found = this.find(target);
    if (found) return found;
    throw new McpError(
      ErrorCode.NOT_FOUND,
      `No editor session named '${target}'. Registered: ${this.list().map((s) => s.name).join(", ")}. ` +
        `Use project(action='list_editors') to see them, or project(action='add_editor', projectPath=...) to register one.`,
    );
  }

  /**
   * Re-file a session after its project moved (`project(set_project)`).
   *
   * switchProject moves the ProjectContext and the socket together; this moves
   * the registry entry with them, since sessions are keyed by project root. A
   * session named after the project it just left is renamed too, so the handle
   * a caller sees still describes what it addresses.
   *
   * The editor of the project being left is never touched: this detaches, it
   * does not stop anything.
   */
  rekey(session: EditorSession): EditorSession {
    const previousKey = session.key;
    const nextKey = session.project.projectPath ? sessionKeyFor(session.project.projectPath) : DEFAULT_SESSION_KEY;
    if (nextKey === previousKey) return session;

    const occupant = this.byKey.get(nextKey);
    if (occupant && occupant !== session) {
      throw new McpError(
        ErrorCode.INVALID_PARAMS,
        `'${occupant.name}' is already registered for that project; two sessions cannot address one editor.`,
      );
    }

    this.byKey.delete(previousKey);
    session.key = nextKey;
    this.byKey.set(nextKey, session);
    if (this.activeKey === previousKey) this.activeKey = nextKey;

    const projectName = session.project.projectName;
    if (projectName && projectName.toLowerCase() !== session.name.toLowerCase()) {
      session.name = this.uniqueName(projectName, session);
    }

    session.portSharedWith = [];
    for (const other of this.byKey.values()) {
      other.portSharedWith = other.portSharedWith.filter((n) => n !== session.name);
    }
    this.noteSharedPorts(session);
    return session;
  }

  /** Resolve without throwing. */
  find(target: string): EditorSession | undefined {
    const needle = target.trim();
    if (!needle) return undefined;
    const lower = needle.toLowerCase();
    for (const s of this.byKey.values()) {
      if (s.name.toLowerCase() === lower) return s;
    }
    for (const s of this.byKey.values()) {
      if ((s.project.projectName ?? "").toLowerCase() === lower) return s;
    }
    // Path form: .uproject file or project directory, either slash style.
    try {
      const key = sessionKeyFor(needle);
      const byPath = this.byKey.get(key);
      if (byPath) return byPath;
    } catch {
      // Not a usable path - fall through to "not found".
    }
    return undefined;
  }

  /** Move the default target. Returns the newly active session. */
  use(target: string): EditorSession {
    const session = this.resolve(target);
    this.activeKey = session.key;
    return session;
  }

  /**
   * Forget a session and close its socket. This NEVER touches the editor
   * process: dropping a session detaches this server from that editor and
   * leaves it running. Stopping an editor is editor(stop_editor), which is a
   * separate, explicitly targeted action.
   */
  drop(target: string): { name: string; projectPath: string | null } {
    const session = this.resolve(target);
    if (this.byKey.size === 1) {
      throw new McpError(
        ErrorCode.INVALID_PARAMS,
        `'${session.name}' is the only registered session; at least one must remain. Register another with project(action='add_editor') first.`,
      );
    }
    this.forget(session);
    this.onCountChanged?.(this.byKey.size);
    return { name: session.name, projectPath: session.project.projectPath };
  }

  /** Close a session's socket and remove it. Never touches the editor process. */
  private forget(session: EditorSession): void {
    session.bridge.disconnect();
    this.byKey.delete(session.key);
    for (const other of this.byKey.values()) {
      other.portSharedWith = other.portSharedWith.filter((n) => n !== session.name);
    }
    if (this.activeKey === session.key) {
      const next = this.byKey.values().next().value as EditorSession | undefined;
      this.activeKey = next ? next.key : null;
    }
  }

  /**
   * Two sessions on one port cannot be told apart: whichever editor answers
   * serves both, so a call targeted at one can execute in the other. That is
   * the exact failure multi-editor exists to prevent, so it is recorded on
   * both sessions, reported by list_editors, and refused by the lifecycle
   * actions rather than being left to surface as a mystery later.
   */
  private noteSharedPorts(session: EditorSession): void {
    for (const other of this.byKey.values()) {
      if (other === session || other.bridge.port !== session.bridge.port) continue;
      other.portSharedWith.push(session.name);
      session.portSharedWith.push(other.name);
      warn(
        "session",
        `sessions '${other.name}' and '${session.name}' both resolve to port ${session.bridge.port}, ` +
          `so calls cannot be routed between them. Give each project its own 'bridge.port' in its ue-mcp.yml, ` +
          `or unset UE_MCP_PORT so each derives its own.`,
      );
    }
  }

  private uniqueName(base: string, ignore?: EditorSession): string {
    const taken = new Set(
      [...this.byKey.values()].filter((s) => s !== ignore).map((s) => s.name.toLowerCase()),
    );
    if (!taken.has(base.toLowerCase())) return base;
    for (let i = 2; ; i++) {
      const candidate = `${base}-${i}`;
      if (!taken.has(candidate.toLowerCase())) return candidate;
    }
  }
}
