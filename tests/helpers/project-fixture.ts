/**
 * Throwaway Unreal projects on disk: a temp root, a `.uproject` per project
 * inside it, and removal of the lot when the test is done.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import type { EditorSession, SessionRegistry } from "../../src/sessions/session.js";
import type { ToolContext } from "../../src/core/types.js";

export interface MakeProjectOptions {
  /** Create the project's Content directory too. */
  content?: boolean;
  /** EngineAssociation written into the descriptor. Defaults to 5.6. */
  engine?: string;
}

export class ProjectFixture {
  readonly root: string;

  /**
   * `realpath` resolves the temp root, for tests that compare paths against
   * ones the code under test resolved itself (macOS /var is a symlink).
   */
  constructor(prefix = "ue-mcp-fixture-", opts: { realpath?: boolean } = {}) {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
    this.root = opts.realpath ? fs.realpathSync(dir) : dir;
  }

  /** `<root>/<name>/<name>.uproject`, returned as that file's path. */
  makeProject(name: string, opts: MakeProjectOptions = {}): string {
    const dir = path.join(this.root, name);
    fs.mkdirSync(opts.content ? path.join(dir, "Content") : dir, { recursive: true });
    const uproject = path.join(dir, `${name}.uproject`);
    fs.writeFileSync(
      uproject,
      JSON.stringify({ FileVersion: 3, EngineAssociation: opts.engine ?? "5.6" }),
      "utf-8",
    );
    return uproject;
  }

  cleanup(): void {
    fs.rmSync(this.root, { recursive: true, force: true });
  }
}

/** A context bound to one registered session, the way dispatch binds one. */
export function sessionToolContext(sessions: SessionRegistry, session: EditorSession): ToolContext {
  return { bridge: session.guarded, project: session.project, session, sessions };
}
