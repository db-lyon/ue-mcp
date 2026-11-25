// @session independent: The set model, its text form and ops: pure functions over their arguments. No state.
/**
 * The Playtest Hub's set (#2113): its tracks and scenes as Ableton Live's
 * Session and Arrangement views show them. Tracks are the grid's columns and
 * the Arrangement's rows; scenes are the grid's rows, each holding at most one
 * clip slot per track. Pure data and functions: the model, its text form, the
 * ops both views and agents edit it with, and their validation.
 */
import {
  ARRANGEMENT_RATE,
  applyArrangementOps,
  arrangementEnd,
  arrangementLayout,
  rankClipCandidates,
  targetKey,
  type Arrangement,
  type ArrangementFinding,
  type ArrangementOp,
  type ArrangementValidateOptions,
  type InstanceKind,
} from "./hub-arrangement.js";

/** The colors a track, scene, slot or instance may take, like Ableton's clip palette. */
export const SET_COLORS = ["red", "orange", "yellow", "lime", "green", "teal", "cyan", "blue", "indigo", "violet", "pink", "gray"] as const;
export type SetColor = (typeof SET_COLORS)[number];
/** The most remote clients a track may drive. */
export const SET_PLAYER_MAX = 15;

/** One cell of the Session grid that holds a clip. */
export interface Slot {
  track: string;
  kind: InstanceKind;
  target: string;
  name?: string;
  color?: SetColor;
}

export interface SetTrack {
  id: string;
  title: string;
  order: number;
  color?: SetColor;
  /** 0 the editor's own player, N remote client N. */
  player: number;
  mute: boolean;
  solo: boolean;
  arrangement: Arrangement;
}

export interface SetScene {
  id: string;
  title: string;
  order: number;
  color?: SetColor;
  /** The tracks the scene plays, in order: a composition of tracks (phase 1). */
  tracks: string[];
  /** Phase 2, the Session grid: its clip slots, at most one per track. */
  slots: Slot[];
}

export interface SetModel {
  tracks: SetTrack[];
  scenes: SetScene[];
}

const isObj = (v: unknown): v is Record<string, unknown> => !!v && typeof v === "object" && !Array.isArray(v);
const isColor = (v: unknown): v is SetColor => typeof v === "string" && (SET_COLORS as readonly string[]).includes(v);

/** A slot from JSON, or null. */
export function slotOf(v: unknown): Slot | null {
  if (!isObj(v) || typeof v.track !== "string" || !v.track || (v.kind !== "clip" && v.kind !== "script") || typeof v.target !== "string" || !v.target.startsWith("/")) return null;
  return {
    track: v.track,
    kind: v.kind,
    target: targetKey(v.target),
    ...(typeof v.name === "string" && v.name.trim() ? { name: v.name.trim().slice(0, 200) } : {}),
    ...(isColor(v.color) ? { color: v.color } : {}),
  };
}

export const setColorOf = (v: unknown): SetColor | undefined => (isColor(v) ? v : undefined);

/** The tracks a run plays: none muted, and only the soloed ones when any is soloed. */
export function audibleTracks<T extends Pick<SetTrack, "mute" | "solo">>(tracks: readonly T[]): T[] {
  const solo = tracks.some((t) => t.solo);
  return tracks.filter((t) => (solo ? t.solo : !t.mute));
}

// ---------- Text form ----------

const quote = (s: string): string => (/[\s"\\]/.test(s) || s === "" ? `"${s.replace(/\\/g, "\\\\").replace(/"/g, '\\"')}"` : s);
const r3 = (n: number): number => Math.round(n * 1000) / 1000;

/** The set as text: tracks with their instances, then scenes with their slots, in order. */
export function setToText(m: SetModel, rate = ARRANGEMENT_RATE): string {
  const lines = [`set rate=${rate}`];
  for (const t of [...m.tracks].sort((a, b) => a.order - b.order)) {
    let line = `track ${t.id} ${quote(t.title)}`;
    if (t.color) line += ` color=${t.color}`;
    line += ` player=${t.player}`;
    if (t.mute) line += " mute";
    if (t.solo) line += " solo";
    lines.push(line);
    for (const i of t.arrangement.instances) {
      let l = `  ${i.kind} ${i.id} ${i.target} ${r3(i.start)}`;
      if (i.in !== undefined) l += ` in=${r3(i.in)}`;
      if (i.out !== undefined) l += ` out=${r3(i.out)}`;
      if (i.length !== undefined) l += ` length=${r3(i.length)}`;
      if (i.name) l += ` name=${quote(i.name)}`;
      if (i.color) l += ` color=${i.color}`;
      lines.push(l);
    }
  }
  for (const s of [...m.scenes].sort((a, b) => a.order - b.order)) {
    lines.push(`scene ${s.id} ${quote(s.title)}${s.color ? ` color=${s.color}` : ""}${s.tracks.length ? ` tracks=${s.tracks.join(",")}` : ""}`);
    for (const sl of s.slots) lines.push(`  slot ${sl.track} ${sl.kind} ${sl.target}${sl.name ? ` name=${quote(sl.name)}` : ""}${sl.color ? ` color=${sl.color}` : ""}`);
  }
  return lines.join("\n");
}

/** Split a line into words, keeping "quoted strings" whole. */
export function setWords(line: string): string[] {
  const out: string[] = [];
  let cur = "";
  let inQ = false;
  let had = false;
  for (let n = 0; n < line.length; n++) {
    const c = line[n];
    if (inQ) {
      if (c === "\\" && n + 1 < line.length) cur += line[++n];
      else if (c === '"') inQ = false;
      else cur += c;
    } else if (c === '"') {
      inQ = true;
      had = true;
    } else if (/\s/.test(c)) {
      if (cur || had) out.push(cur);
      cur = "";
      had = false;
    } else cur += c;
  }
  if (cur || had) out.push(cur);
  return out;
}

// ---------- Ops ----------

/** A set op. `track`, `scene` and `toTrack` name a track or scene by id or by its exact title. */
export type SetOp =
  | { op: "track.add"; title: string; color?: string; player?: number; at?: number }
  | { op: "track.delete" | "scene.delete" | "scene.duplicate"; track?: string; scene?: string }
  | { op: "track.rename" | "scene.rename"; track?: string; scene?: string; title: string }
  | { op: "track.color" | "scene.color"; track?: string; scene?: string; color: string | null }
  | { op: "track.move" | "scene.move"; track?: string; scene?: string; to: number }
  | { op: "track.mute" | "track.solo"; track: string; on: boolean }
  | { op: "track.player"; track: string; player: number }
  | { op: "scene.add"; title: string; color?: string; at?: number }
  | { op: "scene.capture"; scene: string; at?: number }
  | { op: "scene.track"; scene: string; track: string; remove?: boolean; at?: number }
  | { op: "slot.set"; scene: string; track: string; kind: InstanceKind; target: string }
  | { op: "slot.clear"; scene: string; track: string }
  | { op: "slot.unique"; scene: string; track: string }
  | { op: "slot.move" | "slot.copy"; scene: string; track: string; toScene: string; toTrack: string }
  | { op: "slot.rename"; scene: string; track: string; name: string | null }
  | { op: "slot.color"; scene: string; track: string; color: string | null }
  | { op: "instance"; track: string; edit: ArrangementOp }
  | { op: "instance.copy"; track: string; id: string; toTrack: string; at?: number; remove?: boolean }
  | { op: "instance.color"; track: string; id: string; color: string | null }
  /** Point an instance at another clip, keeping its place, name and color. A new `kind` drops the trim, which the old kind's frames gave. */
  | { op: "instance.retarget"; track: string; id: string; target: string; kind?: InstanceKind }
  | { op: "instance.unique"; track: string; id: string };

const OP_WORDS = ["track", "scene", "slot", "add", "duplicate", "copy", "move", "trim", "rename", "color", "delete", "unique", "retarget"];

/** Ops from their text form, one per line (or separated by ";"), as the issue's op table gives them. */
export function setOpsFromText(text: string): { ops: SetOp[]; findings: ArrangementFinding[] } {
  const ops: SetOp[] = [];
  const findings: ArrangementFinding[] = [];
  text.split(/\r?\n|;/).forEach((raw, n) => {
    const line = raw.trim();
    if (!line || line.startsWith("#")) return;
    const w = setWords(line);
    const bad = (expected: string, candidates?: string[]) => findings.push({ id: `op ${n + 1}`, field: "op", message: `op ${n + 1} (${line}): expected ${expected}`, candidates, blocks: true });
    const kv = (from: number) => Object.fromEntries(w.slice(from).filter((x) => x.includes("=")).map((x) => [x.slice(0, x.indexOf("=")), x.slice(x.indexOf("=") + 1)]));
    const num = (s: string | undefined): number | undefined => (s !== undefined && s !== "" && Number.isFinite(Number(s)) ? Number(s) : undefined);
    const after = (word: string, from = 0): string | undefined => {
      const at = w.indexOf(word, from);
      return at >= 0 ? w[at + 1] : undefined;
    };
    const ref = (s: string | undefined): { track: string; id: string } | null => {
      if (!s) return null;
      const at = s.lastIndexOf("/");
      return at > 0 ? { track: s.slice(0, at), id: s.slice(at + 1) } : null;
    };
    const head = w[0];
    if (head === "track") {
      const v = w[1];
      const t = w[2];
      switch (v) {
        case "add": {
          const o = kv(3);
          if (!w[2] || w[2].includes("=")) return bad('track add "Name" [color=..] [player=N] [at=N]');
          return void ops.push({ op: "track.add", title: w[2], ...(o.color ? { color: o.color } : {}), ...(num(o.player) !== undefined ? { player: num(o.player) } : {}), ...(num(o.at) !== undefined ? { at: num(o.at) } : {}) });
        }
        case "delete":
          return t ? void ops.push({ op: "track.delete", track: t }) : bad("track delete <track>");
        case "rename":
          return t && w[3] !== undefined ? void ops.push({ op: "track.rename", track: t, title: w[3] }) : bad('track rename <track> "Name"');
        case "color":
          return t && w[3] ? void ops.push({ op: "track.color", track: t, color: w[3] === "none" ? null : w[3] }) : bad("track color <track> <color|none>");
        case "move":
          return t && num(after("to")) !== undefined ? void ops.push({ op: "track.move", track: t, to: num(after("to"))! }) : bad("track move <track> to <position>");
        case "mute":
        case "solo":
          return t && (w[3] === "on" || w[3] === "off" || w[3] === undefined) ? void ops.push({ op: v === "mute" ? "track.mute" : "track.solo", track: t, on: w[3] !== "off" }) : bad(`track ${v} <track> on|off`);
        case "player":
          return t && num(w[3]) !== undefined ? void ops.push({ op: "track.player", track: t, player: num(w[3])! }) : bad("track player <track> <N>");
        default:
          return bad("track add|delete|rename|color|move|mute|solo|player ...", rankClipCandidates(v ?? "", ["add", "delete", "rename", "color", "move", "mute", "solo", "player"]));
      }
    }
    if (head === "scene") {
      const v = w[1];
      const s = w[2];
      switch (v) {
        case "add":
        case "insert": {
          const o = kv(3);
          if (!w[2] || w[2].includes("=")) return bad(`scene ${v} "Name" [at <position>]`);
          const at = num(after("at", 3)) ?? num(o.at);
          return void ops.push({ op: "scene.add", title: w[2], ...(o.color ? { color: o.color } : {}), ...(at !== undefined ? { at } : {}) });
        }
        case "delete":
        case "duplicate":
          return s ? void ops.push({ op: v === "delete" ? "scene.delete" : "scene.duplicate", scene: s }) : bad(`scene ${v} <scene>`);
        case "rename":
          return s && w[3] !== undefined ? void ops.push({ op: "scene.rename", scene: s, title: w[3] }) : bad('scene rename <scene> "Name"');
        case "color":
          return s && w[3] ? void ops.push({ op: "scene.color", scene: s, color: w[3] === "none" ? null : w[3] }) : bad("scene color <scene> <color|none>");
        case "move":
          return s && num(after("to")) !== undefined ? void ops.push({ op: "scene.move", scene: s, to: num(after("to"))! }) : bad("scene move <scene> to <position>");
        case "capture":
          return s ? void ops.push({ op: "scene.capture", scene: s, ...(num(after("at")) !== undefined ? { at: num(after("at")) } : {}) }) : bad("scene capture <scene> [at <frame>]");
        case "track":
          return s && (w[3] === "add" || w[3] === "remove") && w[4] ? void ops.push({ op: "scene.track", scene: s, track: w[4], ...(w[3] === "remove" ? { remove: true } : {}), ...(num(after("at", 4)) !== undefined ? { at: num(after("at", 4)) } : {}) }) : bad("scene track <scene> add|remove <track> [at <position>]");
        default:
          return bad("scene add|insert|delete|rename|color|duplicate|move|track|capture ...", rankClipCandidates(v ?? "", ["add", "insert", "delete", "rename", "color", "duplicate", "move", "track", "capture"]));
      }
    }
    if (head === "slot") {
      const [, v, s, t] = w;
      if (!s || !t) return bad(`slot ${v ?? "<verb>"} <scene> <track> ...`);
      switch (v) {
        case "set":
          return (w[4] === "clip" || w[4] === "script") && w[5] ? void ops.push({ op: "slot.set", scene: s, track: t, kind: w[4], target: w[5] }) : bad("slot set <scene> <track> <clip|script> <path>");
        case "clear":
        case "unique":
          return void ops.push({ op: v === "clear" ? "slot.clear" : "slot.unique", scene: s, track: t });
        case "move":
        case "copy":
          return w[4] === "to" && w[5] && w[6] ? void ops.push({ op: v === "move" ? "slot.move" : "slot.copy", scene: s, track: t, toScene: w[5], toTrack: w[6] }) : bad(`slot ${v} <scene> <track> to <scene> <track>`);
        case "rename":
          return void ops.push({ op: "slot.rename", scene: s, track: t, name: w[4] ?? null });
        case "color":
          return w[4] ? void ops.push({ op: "slot.color", scene: s, track: t, color: w[4] === "none" ? null : w[4] }) : bad("slot color <scene> <track> <color|none>");
        default:
          return bad("slot set|clear|move|copy|rename|color|unique ...", rankClipCandidates(v ?? "", ["set", "clear", "move", "copy", "rename", "color", "unique"]));
      }
    }
    if (head === "add") {
      if (!w[1] || (w[2] !== "clip" && w[2] !== "script") || !w[3]) return bad("add <track> <clip|script> <path> [at <frame>]");
      const o = kv(4);
      return void ops.push({ op: "instance", track: w[1], edit: { op: "add", kind: w[2], target: w[3], at: num(after("at", 4)), in: num(o.in), out: num(o.out), length: num(o.length), name: o.name } });
    }
    const r = ref(w[1]);
    if (!r) return bad(`${head} <track>/<instance> ...`, rankClipCandidates(head, OP_WORDS));
    switch (head) {
      case "duplicate":
        return void ops.push({ op: "instance", track: r.track, edit: { op: "duplicate", id: r.id, at: num(after("at")) } });
      case "copy":
        return w[2] === "to" && w[3] ? void ops.push({ op: "instance.copy", track: r.track, id: r.id, toTrack: w[3], at: num(after("at", 3)) }) : bad("copy <track>/<instance> to <track> [at <frame>]");
      case "move": {
        if (w[2] === "by" && num(w[3]) !== undefined) return void ops.push({ op: "instance", track: r.track, edit: { op: "move", id: r.id, by: num(w[3]) } });
        if (w[2] === "to" && num(w[3]) !== undefined) return void ops.push({ op: "instance", track: r.track, edit: { op: "move", id: r.id, to: num(w[3]) } });
        if (w[2] === "to" && w[3]) return void ops.push({ op: "instance.copy", track: r.track, id: r.id, toTrack: w[3], at: num(after("at", 3)), remove: true });
        return bad("move <track>/<instance> to <frame> | by <frames> | to <track> [at <frame>]");
      }
      case "trim": {
        const o = kv(2);
        const v = (k: string): number | null | undefined => (o[k] === undefined ? undefined : o[k] === "-" ? null : num(o[k]));
        return Object.keys(o).length ? void ops.push({ op: "instance", track: r.track, edit: { op: "trim", id: r.id, in: v("in"), out: v("out"), length: v("length") } }) : bad("trim <track>/<instance> in=N out=N | length=N");
      }
      case "rename":
        return void ops.push({ op: "instance", track: r.track, edit: { op: "rename", id: r.id, name: w[2] ?? null } });
      case "color":
        return w[2] ? void ops.push({ op: "instance.color", track: r.track, id: r.id, color: w[2] === "none" ? null : w[2] }) : bad("color <track>/<instance> <color|none>");
      case "delete":
        return void ops.push({ op: "instance", track: r.track, edit: { op: "delete", id: r.id } });
      case "unique":
        return void ops.push({ op: "instance.unique", track: r.track, id: r.id });
      case "retarget": {
        const kind = w[2] === "clip" || w[2] === "script" ? w[2] : undefined;
        const target = kind ? w[3] : w[2];
        return target ? void ops.push({ op: "instance.retarget", track: r.track, id: r.id, target, ...(kind ? { kind } : {}) }) : bad("retarget <track>/<instance> [clip|script] <path>");
      }
      default:
        return bad("an op from the op table", rankClipCandidates(head, OP_WORDS));
    }
  });
  return { ops, findings };
}

/** Ops from JSON: a list of op objects, or op lines as text. */
export function setOpsOf(v: unknown): { ops: SetOp[]; findings: ArrangementFinding[] } {
  if (typeof v === "string") return setOpsFromText(v);
  if (!Array.isArray(v) || v.length === 0) return { ops: [], findings: [{ id: "ops", field: "ops", message: "ops must be a non-empty list of op objects, or op lines as text", blocks: true }] };
  const bad = v.findIndex((o) => !isObj(o) || typeof o.op !== "string");
  if (bad >= 0) return { ops: [], findings: [{ id: `op ${bad + 1}`, field: "op", message: `op ${bad + 1} must be an object with op`, blocks: true }] };
  return { ops: v as SetOp[], findings: [] };
}

export interface SetEditOptions extends ArrangementValidateOptions {
  /** A fresh id for a new track or scene from its title. */
  newId: (title: string) => string;
  /** Asset paths the caller has just made (make unique's copies), taken as clips without being read. */
  made?: ReadonlySet<string>;
}

export type SetEditResult =
  | { ok: true; set: SetModel; changed: string[]; findings: ArrangementFinding[] }
  | { ok: false; finding: ArrangementFinding };

/**
 * Apply set ops in order, all or none. A track or scene is named by id or by
 * its exact title (case-insensitive); a name that matches nothing fails with
 * the closest ones as candidates. `slot.unique` and `instance.unique` need a
 * copy of the clip asset, which the caller makes first and turns into
 * `slot.set` and `instance.retarget`; here they fail. A clip a slot or
 * instance is pointed at must be one `factsOf` knows, or, when the library
 * cannot be listed, is taken on trust.
 */
export function applySetOps(m: SetModel, ops: readonly SetOp[], o: SetEditOptions): SetEditResult {
  const set: SetModel = {
    tracks: m.tracks.map((t) => ({ ...t, arrangement: { rate: t.arrangement.rate, instances: t.arrangement.instances.map((i) => ({ ...i })) } })),
    scenes: m.scenes.map((s) => ({ ...s, tracks: [...s.tracks], slots: s.slots.map((x) => ({ ...x })) })),
  };
  const changed: string[] = [];
  const warnings: ArrangementFinding[] = [];
  for (let n = 0; n < ops.length; n++) {
    const op = ops[n] as SetOp & Record<string, unknown>;
    const fail = (id: string, field: string, message: string, candidates?: string[]): SetEditResult => ({ ok: false, finding: { id, field, message: `op ${n + 1}: ${message}`, candidates, blocks: true } });
    const findT = (name: unknown) => pick(set.tracks, name);
    const findS = (name: unknown) => pick(set.scenes, name);
    const noT = (name: unknown) => fail(String(name), "track", `no track ${String(name)}`, rankClipCandidates(String(name), set.tracks.flatMap((t) => [t.id, t.title])));
    const noS = (name: unknown) => fail(String(name), "scene", `no scene ${String(name)}`, rankClipCandidates(String(name), set.scenes.flatMap((s) => [s.id, s.title])));
    const color = (c: unknown): SetColor | undefined | false => (c === null || c === undefined ? undefined : isColor(c) ? c : false);
    const badColor = (c: unknown) => fail(String(c), "color", `'${String(c)}' is not a color`, rankClipCandidates(String(c), [...SET_COLORS]));
    switch (op.op) {
      case "track.add":
      case "scene.add": {
        const title = typeof op.title === "string" ? op.title.trim() : "";
        if (!title) return fail("title", "title", "a name is needed");
        const c = color(op.color);
        if (c === false) return badColor(op.color);
        const list = op.op === "track.add" ? set.tracks : set.scenes;
        const id = o.newId(title);
        const at = typeof op.at === "number" ? Math.max(0, Math.min(list.length, Math.floor(op.at))) : list.length;
        if (op.op === "track.add") {
          const player = typeof op.player === "number" ? op.player : 0;
          if (!validPlayer(player)) return fail(id, "player", `player must be 0 or a remote client from 1 to ${SET_PLAYER_MAX}`);
          insertAt(set.tracks, at, { id, title, order: 0, ...(c ? { color: c } : {}), player, mute: false, solo: false, arrangement: { rate: ARRANGEMENT_RATE, instances: [] } });
        } else insertAt(set.scenes, at, { id, title, order: 0, ...(c ? { color: c } : {}), tracks: [], slots: [] });
        changed.push(id);
        break;
      }
      case "track.delete": {
        const t = findT(op.track);
        if (!t) return noT(op.track);
        set.tracks = set.tracks.filter((x) => x !== t);
        for (const s of set.scenes) {
          s.slots = s.slots.filter((x) => x.track !== t.id);
          s.tracks = s.tracks.filter((x) => x !== t.id);
        }
        changed.push(t.id);
        break;
      }
      case "scene.delete": {
        const s = findS(op.scene);
        if (!s) return noS(op.scene);
        set.scenes = set.scenes.filter((x) => x !== s);
        changed.push(s.id);
        break;
      }
      case "scene.duplicate": {
        const s = findS(op.scene);
        if (!s) return noS(op.scene);
        const id = o.newId(s.title);
        insertAt(set.scenes, sorted(set.scenes).indexOf(s) + 1, { ...s, id, order: 0, tracks: [...s.tracks], slots: s.slots.map((x) => ({ ...x })) });
        changed.push(id);
        break;
      }
      case "track.rename":
      case "scene.rename": {
        const x = op.op === "track.rename" ? findT(op.track) : findS(op.scene);
        if (!x) return op.op === "track.rename" ? noT(op.track) : noS(op.scene);
        const title = typeof op.title === "string" ? op.title.trim() : "";
        if (!title) return fail(x.id, "title", "a name is needed");
        x.title = title.slice(0, 200);
        changed.push(x.id);
        break;
      }
      case "track.color":
      case "scene.color": {
        const x = op.op === "track.color" ? findT(op.track) : findS(op.scene);
        if (!x) return op.op === "track.color" ? noT(op.track) : noS(op.scene);
        const c = color(op.color);
        if (c === false) return badColor(op.color);
        if (c) x.color = c;
        else delete x.color;
        changed.push(x.id);
        break;
      }
      case "track.move":
      case "scene.move": {
        const isT = op.op === "track.move";
        const x = isT ? findT(op.track) : findS(op.scene);
        if (!x) return isT ? noT(op.track) : noS(op.scene);
        const list = isT ? set.tracks : set.scenes;
        const order = sorted(list as Array<{ order: number }>).filter((y) => y !== x);
        order.splice(Math.max(0, Math.min(order.length, Math.floor(Number(op.to)))), 0, x);
        order.forEach((y, i) => (y.order = i));
        changed.push(x.id);
        break;
      }
      case "track.mute":
      case "track.solo": {
        const t = findT(op.track);
        if (!t) return noT(op.track);
        if (op.op === "track.mute") t.mute = op.on !== false;
        else t.solo = op.on !== false;
        changed.push(t.id);
        break;
      }
      case "track.player": {
        const t = findT(op.track);
        if (!t) return noT(op.track);
        if (!validPlayer(op.player)) return fail(t.id, "player", `player must be 0 (the editor's own player) or a remote client from 1 to ${SET_PLAYER_MAX}`);
        t.player = op.player as number;
        changed.push(t.id);
        break;
      }
      case "scene.track": {
        const s = findS(op.scene);
        if (!s) return noS(op.scene);
        const t = findT(op.track);
        if (!t) return noT(op.track);
        s.tracks = s.tracks.filter((x) => x !== t.id);
        if (!op.remove) s.tracks.splice(typeof op.at === "number" ? Math.max(0, Math.min(s.tracks.length, Math.floor(op.at))) : s.tracks.length, 0, t.id);
        changed.push(s.id);
        break;
      }
      case "scene.capture": {
        const s = findS(op.scene);
        if (!s) return noS(op.scene);
        for (const sl of s.slots) {
          const t = set.tracks.find((x) => x.id === sl.track);
          if (!t) continue;
          const at = typeof op.at === "number" ? op.at : arrangementEnd(arrangementLayout(t.arrangement, o.factsOf));
          const r = applyArrangementOps(t.arrangement, [{ op: "add", kind: sl.kind, target: sl.target, at, ...(sl.name ? { name: sl.name } : {}) }], o);
          if (!r.ok) return { ok: false, finding: { ...r.finding, id: `${t.id}/${r.finding.id}`, message: `op ${n + 1}: ${t.title}: ${r.finding.message}` } };
          t.arrangement = r.arrangement;
          const added = r.arrangement.instances.find((i) => i.id === r.changed[0]);
          if (added && sl.color) added.color = sl.color;
          changed.push(`${t.id}/${r.changed[0]}`);
        }
        break;
      }
      case "slot.set":
      case "slot.clear":
      case "slot.rename":
      case "slot.color": {
        const s = findS(op.scene);
        if (!s) return noS(op.scene);
        const t = findT(op.track);
        if (!t) return noT(op.track);
        const at = s.slots.findIndex((x) => x.track === t.id);
        if (op.op === "slot.set") {
          if ((op.kind !== "clip" && op.kind !== "script") || typeof op.target !== "string" || !op.target.startsWith("/")) return fail(`${s.id}/${t.id}`, "target", "slot set needs kind clip or script and an asset path");
          const target = targetKey(op.target);
          if (!o.factsOf(target, op.kind) && !o.made?.has(target)) {
            const pool = o.library?.(op.kind);
            if (pool) return fail(target, "target", `no ${op.kind === "clip" ? "timeline clip" : "step-list clip"} at ${target}`, rankClipCandidates(target, pool));
          }
          const slot: Slot = { track: t.id, kind: op.kind, target };
          if (at >= 0) s.slots[at] = slot;
          else s.slots.push(slot);
        } else {
          if (at < 0) return fail(`${s.id}/${t.id}`, "slot", `${s.title} has no clip on ${t.title}`);
          if (op.op === "slot.clear") s.slots.splice(at, 1);
          else if (op.op === "slot.rename") {
            if (typeof op.name === "string" && op.name.trim()) s.slots[at].name = op.name.trim().slice(0, 200);
            else delete s.slots[at].name;
          } else {
            const c = color(op.color);
            if (c === false) return badColor(op.color);
            if (c) s.slots[at].color = c;
            else delete s.slots[at].color;
          }
        }
        changed.push(`${s.id}/${t.id}`);
        break;
      }
      case "slot.move":
      case "slot.copy": {
        const s = findS(op.scene);
        if (!s) return noS(op.scene);
        const t = findT(op.track);
        if (!t) return noT(op.track);
        const s2 = findS(op.toScene);
        if (!s2) return noS(op.toScene);
        const t2 = findT(op.toTrack);
        if (!t2) return noT(op.toTrack);
        const from = s.slots.findIndex((x) => x.track === t.id);
        if (from < 0) return fail(`${s.id}/${t.id}`, "slot", `${s.title} has no clip on ${t.title}`);
        const slot = { ...s.slots[from], track: t2.id };
        if (op.op === "slot.move") s.slots.splice(from, 1);
        const to = s2.slots.findIndex((x) => x.track === t2.id);
        if (to >= 0) s2.slots[to] = slot;
        else s2.slots.push(slot);
        changed.push(`${s2.id}/${t2.id}`);
        break;
      }
      case "instance": {
        const t = findT(op.track);
        if (!t) return noT(op.track);
        const r = applyArrangementOps(t.arrangement, [op.edit as ArrangementOp], o);
        if (!r.ok) return { ok: false, finding: { ...r.finding, id: `${t.id}/${r.finding.id}`, message: `op ${n + 1}: ${t.title}: ${r.finding.message.replace(/^op 1: /, "")}` } };
        t.arrangement = r.arrangement;
        warnings.push(...r.findings.map((f) => ({ ...f, id: `${t.id}/${f.id}` })));
        changed.push(...r.changed.map((c) => `${t.id}/${c}`));
        break;
      }
      case "instance.copy": {
        const t = findT(op.track);
        if (!t) return noT(op.track);
        const t2 = findT(op.toTrack);
        if (!t2) return noT(op.toTrack);
        const src = t.arrangement.instances.find((i) => i.id === op.id);
        if (!src) return fail(`${t.id}/${String(op.id)}`, "id", `${t.title} has no instance ${String(op.id)}`, rankClipCandidates(String(op.id), t.arrangement.instances.map((i) => i.id)));
        if (op.remove) t.arrangement = { ...t.arrangement, instances: t.arrangement.instances.filter((i) => i !== src) };
        const at = typeof op.at === "number" ? op.at : op.remove && t2 !== t ? src.start : undefined;
        const r = applyArrangementOps(t2.arrangement, [{ op: "add", kind: src.kind, target: src.target, at, in: src.in, out: src.out, length: src.length, name: src.name }], o);
        if (!r.ok) return { ok: false, finding: { ...r.finding, id: `${t2.id}/${r.finding.id}`, message: `op ${n + 1}: ${t2.title}: ${r.finding.message.replace(/^op 1: /, "")}` } };
        t2.arrangement = r.arrangement;
        const added = t2.arrangement.instances.find((i) => i.id === r.changed[0])!;
        if (src.color) added.color = src.color;
        changed.push(`${t2.id}/${added.id}`);
        break;
      }
      case "instance.color":
      case "instance.retarget": {
        const t = findT(op.track);
        if (!t) return noT(op.track);
        const inst = t.arrangement.instances.find((i) => i.id === op.id);
        if (!inst) return fail(`${t.id}/${String(op.id)}`, "id", `${t.title} has no instance ${String(op.id)}`, rankClipCandidates(String(op.id), t.arrangement.instances.map((i) => i.id)));
        if (op.op === "instance.color") {
          const c = color(op.color);
          if (c === false) return badColor(op.color);
          if (c) inst.color = c;
          else delete inst.color;
        } else {
          if (op.kind !== undefined && op.kind !== "clip" && op.kind !== "script") return fail(`${t.id}/${inst.id}`, "kind", "kind must be clip or script");
          if (typeof op.target !== "string" || !op.target.startsWith("/")) return fail(`${t.id}/${inst.id}`, "target", "retarget needs an asset path");
          const kind: InstanceKind = op.kind ?? inst.kind;
          const target = targetKey(op.target);
          if (!o.factsOf(target, kind) && !o.made?.has(target)) {
            const pool = o.library?.(kind);
            if (pool) return fail(target, "target", `no ${kind === "clip" ? "timeline clip" : "step-list clip"} at ${target}`, rankClipCandidates(target, pool));
          }
          inst.target = target;
          if (kind !== inst.kind) {
            inst.kind = kind;
            delete inst.in;
            delete inst.out;
            delete inst.length;
          }
        }
        changed.push(`${t.id}/${inst.id}`);
        break;
      }
      case "slot.unique":
      case "instance.unique":
        return fail(String(op.track), "op", "make unique copies the clip asset; send it to the hub, which does that first");
      default:
        return fail("op", "op", `'${String((op as { op?: unknown }).op)}' is not a set op`);
    }
  }
  sorted(set.tracks).forEach((t, i) => (t.order = i));
  sorted(set.scenes).forEach((s, i) => (s.order = i));
  return { ok: true, set, changed, findings: warnings };
}

const validPlayer = (p: unknown): p is number => typeof p === "number" && Number.isInteger(p) && p >= 0 && p <= SET_PLAYER_MAX;

function sorted<T extends { order: number }>(list: readonly T[]): T[] {
  return [...list].sort((a, b) => a.order - b.order);
}

/** Put x at position `at` of the list's order, renumbering. */
function insertAt<T extends { order: number }>(list: T[], at: number, x: T): void {
  const order = sorted(list);
  order.splice(at, 0, x);
  order.forEach((y, i) => (y.order = i));
  list.push(x);
}

/** A track or scene by id, else by exact title, ignoring case. */
function pick<T extends { id: string; title: string }>(list: readonly T[], name: unknown): T | undefined {
  if (typeof name !== "string") return undefined;
  return list.find((x) => x.id === name) ?? list.find((x) => x.title.toLowerCase() === name.toLowerCase());
}
