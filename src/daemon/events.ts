/**
 * The daemon's event stream: every event gets the next id, a bounded window is
 * kept, and a reader resumes from the last id it saw (SSE Last-Event-ID). The
 * UI holds no state of its own; this is what it rebuilds from.
 */

export interface DaemonEvent {
  id: number;
  type: string;
  at: string;
  data: Record<string, unknown>;
}

export class EventLog {
  private nextId = 1;
  private readonly kept: DaemonEvent[] = [];
  private readonly listeners = new Set<(e: DaemonEvent) => void>();

  constructor(private readonly capacity = 1000) {}

  publish(type: string, data: Record<string, unknown> = {}): DaemonEvent {
    const event: DaemonEvent = { id: this.nextId++, type, at: new Date().toISOString(), data };
    this.kept.push(event);
    if (this.kept.length > this.capacity) this.kept.splice(0, this.kept.length - this.capacity);
    for (const l of this.listeners) {
      try {
        l(event);
      } catch {
        // A broken subscriber must not stop the others.
      }
    }
    return event;
  }

  /** Events after `lastId`, oldest first. `gap` is true when some were already dropped. */
  since(lastId: number): { events: DaemonEvent[]; gap: boolean } {
    const events = this.kept.filter((e) => e.id > lastId);
    const oldest = this.kept[0]?.id ?? this.nextId;
    return { events, gap: lastId > 0 && lastId < oldest - 1 };
  }

  get lastId(): number {
    return this.nextId - 1;
  }

  subscribe(listener: (e: DaemonEvent) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }
}
