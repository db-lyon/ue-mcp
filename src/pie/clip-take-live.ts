// @session independent: Pure accumulation of one take's samples; each clip watcher owns its own.
/**
 * The take in flight of a clip recording, as notes and keys (#2097).
 *
 * While a take records, pie_clip_status carries `take.live`: the newest samples, each a frame and one raw value per
 * armed track, with `from` the absolute index of the first. LiveTake folds successive windows into what the clip
 * would get if the take stopped now: a Boolean track's notes from the value's edges (the last one `open` while the
 * key is still down), and an axis track's keys wherever the value changes. Saving the take reduces the keys further
 * with FRichCurve, so the saved clip is the authority; this is for drawing the take while it is played.
 */

export interface LiveNote {
  start: number;
  length: number;
  /** Still held at the newest sample. */
  open?: boolean;
}

export interface LiveKey {
  t: number;
  v: number | number[];
}

export interface LiveTrack {
  action: string;
  type: string;
  notes: LiveNote[];
  keys: LiveKey[];
}

/** What a live take adds in one step: each track's notes as they now stand, and the keys appended since the last step. */
export interface LiveTakeDelta {
  /** Samples folded in so far. */
  samples: number;
  /** The newest sample's frame. */
  frame: number;
  tracks: Array<{ action: string; type: string; notes: LiveNote[]; keys: LiveKey[]; keysFrom: number }>;
}

type Body = Record<string, unknown>;

const EPS = 1e-4;
const isDown = (v: unknown) => (typeof v === "number" ? v : Array.isArray(v) ? Number(v[0]) : 0) !== 0;
const same = (a: unknown, b: unknown) => {
  const x = Array.isArray(a) ? a : [a];
  const y = Array.isArray(b) ? b : [b];
  return x.length === y.length && x.every((n, i) => Math.abs(Number(n) - Number(y[i])) <= EPS);
};

export class LiveTake {
  readonly tracks = new Map<string, LiveTrack & { lastValue?: unknown; noteStart: number }>();
  /** Absolute index of the next sample not yet folded in. */
  next = 0;
  frame = 0;

  /** Folds in a `take.live` window and returns what changed, or null when it held nothing new. */
  apply(live: Body): LiveTakeDelta | null {
    const from = typeof live.from === "number" ? live.from : 0;
    const frames = Array.isArray(live.frames) ? (live.frames as unknown[]).map(Number) : [];
    const tracks = Array.isArray(live.tracks) ? (live.tracks as Body[]) : [];
    // Samples this window starts after are lost to a slow poll; take them as missing and carry on from what is here.
    const skip = Math.max(0, this.next - from);
    if (skip >= frames.length) return null;
    const keysBefore = new Map([...this.tracks].map(([a, t]) => [a, t.keys.length]));
    for (const t of tracks) {
      const action = String(t.action ?? "");
      if (!action) continue;
      let track = this.tracks.get(action);
      if (!track) {
        track = { action, type: String(t.type ?? "bool"), notes: [], keys: [], noteStart: -1 };
        this.tracks.set(action, track);
        keysBefore.set(action, 0);
      }
      const values = Array.isArray(t.values) ? (t.values as unknown[]) : [];
      for (let i = skip; i < frames.length && i < values.length; i++) {
        const f = frames[i];
        const v = values[i];
        if (track.type === "bool") {
          const down = isDown(v);
          const last = track.notes[track.notes.length - 1];
          if (down && track.noteStart < 0) {
            track.noteStart = f;
            track.notes.push({ start: f, length: 1, open: true });
          } else if (down && last?.open) {
            last.length = Math.max(1e-3, f + 1 - last.start);
          } else if (!down && track.noteStart >= 0) {
            if (last?.open) {
              last.length = Math.max(1e-3, f - last.start);
              delete last.open;
            }
            track.noteStart = -1;
          }
        } else if (track.lastValue === undefined || !same(track.lastValue, v)) {
          track.keys.push({ t: f, v: Array.isArray(v) ? v.map(Number) : Number(v) });
          track.lastValue = v;
        }
      }
    }
    this.next = from + frames.length;
    this.frame = frames[frames.length - 1];
    return {
      samples: this.next,
      frame: this.frame,
      tracks: [...this.tracks.values()].map((t) => {
        const keysFrom = keysBefore.get(t.action) ?? 0;
        return { action: t.action, type: t.type, notes: t.notes.map((n) => ({ ...n })), keys: t.keys.slice(keysFrom), keysFrom };
      }),
    };
  }

  /** The whole take so far, for a client that joins while it records. */
  snapshot(): { samples: number; frame: number; tracks: LiveTrack[] } {
    return {
      samples: this.next,
      frame: this.frame,
      tracks: [...this.tracks.values()].map((t) => ({ action: t.action, type: t.type, notes: t.notes.map((n) => ({ ...n })), keys: [...t.keys] })),
    };
  }
}
