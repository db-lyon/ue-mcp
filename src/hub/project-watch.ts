// @session per-session: Watches one project's folders for changes while a run plays.
/**
 * Whether project files changed while a run played, whoever saved them: the
 * person, a chat, a fix loop. A run that saw a change is tainted, since it
 * tested a project that changed under it (RunRecord.tainted).
 *
 * The watch covers what a run tests: Content, Config, Source, Plugins and the
 * .uproject. Saved, Intermediate, Binaries and DerivedDataCache are left
 * out: a run, a build and the editor write there on their own. Each change
 * moves a counter on; a run notes the counter when the hub sees it start and
 * compares when it ends.
 */
import * as fs from "node:fs";
import * as path from "node:path";

const ROOTS = ["Content", "Config", "Source", "Plugins"];
/** Folders whose changes are the editor's or a build's own. */
const IGNORED = /(^|[\\/])(Saved|Intermediate|Binaries|DerivedDataCache|\.git|\.vs)([\\/]|$)/i;

export class ProjectWatch {
  private watchers: fs.FSWatcher[] = [];
  private seq = 0;
  /** The counter when each run was seen starting. */
  private readonly started = new Map<string, number>();

  constructor(private readonly projectDir: string, private readonly log?: { warn(message: string, detail?: unknown): void }) {}

  start(): void {
    if (this.watchers.length) return;
    for (const root of ROOTS) {
      const dir = path.join(this.projectDir, root);
      if (!fs.existsSync(dir)) continue;
      try {
        const w = fs.watch(dir, { recursive: true }, (_e, name) => {
          if (!name || !IGNORED.test(String(name))) this.seq++;
        });
        w.on("error", () => undefined);
        w.unref?.();
        this.watchers.push(w);
      } catch (e) {
        this.log?.warn(`hub: ${root} cannot be watched for changes during runs`, e);
      }
    }
    try {
      const w = fs.watch(this.projectDir, (_e, name) => {
        if (name && String(name).toLowerCase().endsWith(".uproject")) this.seq++;
      });
      w.on("error", () => undefined);
      w.unref?.();
      this.watchers.push(w);
    } catch {
      // The project folder itself: without it only the .uproject goes unwatched.
    }
  }

  stop(): void {
    for (const w of this.watchers) w.close();
    this.watchers = [];
    this.started.clear();
  }

  /** A change seen now, for tests. */
  touched(): void {
    this.seq++;
  }

  /** A run the hub saw start. */
  runStarted(runId: string): void {
    if (!this.started.has(runId)) this.started.set(runId, this.seq);
  }

  /** A run ended: whether project files changed since it started. Null for a run not seen starting. */
  runEnded(runId: string): boolean | null {
    const at = this.started.get(runId);
    if (at === undefined) return null;
    this.started.delete(runId);
    return this.seq > at;
  }
}
