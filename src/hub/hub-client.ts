// @session per-session: Holds the one hub of the project a daemon serves, rebuilt when a client configures it.
/**
 * Keeps the project's hub and what clients have configured it with
 * (api.hub.configure). Configuring or un-configuring rebuilds the hub around
 * the merged options, a later client's members replacing an earlier one's: its
 * files and queue are on disk, so nothing is lost, and the hub's timers start
 * again with it.
 *
 * A registry beside it lets the hub category's actions, which run inside the
 * daemon's runtime and know only their project, find the daemon's hub.
 */
import type { DaemonHub } from "./daemon-hub.js";
import type { Hub } from "./hub.js";
import type { HubClientOptions } from "./ports.js";

export class HubHolder {
  private readonly clients = new Set<HubClientOptions>();
  private current: DaemonHub | null = null;

  constructor(private readonly build: (client: () => HubClientOptions) => DaemonHub) {}

  /** The options of every client, a later one's members over an earlier one's. */
  private merged(): HubClientOptions {
    return Object.assign({}, ...this.clients) as HubClientOptions;
  }

  /** The hub, built on first use. */
  get(): Hub {
    return (this.current ??= this.build(() => this.merged())).hub;
  }

  /** A client brings its own pieces. Returns the function that takes them back. */
  configure(options: HubClientOptions): () => void {
    const mine = { ...options };
    this.clients.add(mine);
    this.rebuild();
    return () => {
      if (this.clients.delete(mine)) this.rebuild();
    };
  }

  private rebuild(): void {
    if (!this.current) return;
    this.current.dispose();
    this.current = null;
    this.get();
  }

  /** The daemon is stopping: the hub's run on the editor stops and PIE it started ends (Hub.shutdown). Never builds a hub. */
  async shutdown(): Promise<void> {
    await this.current?.hub.shutdown();
  }

  dispose(): void {
    this.current?.dispose();
    this.current = null;
  }
}

const holders = new Map<string, HubHolder>();

/** The daemon registers its project's holder; a second registration for the project replaces the first. */
export function registerHub(projectDir: string, holder: HubHolder): () => void {
  const key = projectDir.toLowerCase();
  holders.set(key, holder);
  return () => {
    if (holders.get(key) === holder) holders.delete(key);
  };
}

/** The hub of a project, or null where no daemon runs it (a server in process has none). */
export function hubOf(projectDir: string): Hub | null {
  return holders.get(projectDir.toLowerCase())?.get() ?? null;
}
