// @session per-session: Reads and writes the hub's text files in the project directory it is handed.
/**
 * The hub's files in the project: <project>/PlaytestHub/{clips,arrangements,
 * suites}/<id>.json, versioned and merged with the rest of the project. Each
 * is written by fileText (src/contract/hub.ts), so the same value always
 * gives the same bytes. Files are read from disk on every answer, a parse
 * kept only while the file's size and time stand, so a pull or a merge is
 * seen at once. A file that does not read is a problem the overview lists;
 * it is never rewritten or dropped.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import {
  HUB_DIR,
  HUB_FOLDERS,
  HUB_ID,
  arrangementOf,
  clipOf,
  fileText,
  suiteOf,
  type Arrangement,
  type Clip,
  type FileProblem,
  type HubFinding,
  type HubKind,
  type Suite,
} from "./contract.js";
import { writeFileAtomic } from "./atomic-write.js";

type Value<K extends HubKind> = K extends "clip" ? Clip : K extends "arrangement" ? Arrangement : Suite;

interface Parsed {
  mtimeMs: number;
  size: number;
  value: Clip | Arrangement | Suite | null;
  findings: HubFinding[];
}

export interface Listing<T> {
  items: Map<string, T>;
  problems: FileProblem[];
}

function parse(kind: HubKind, id: string, text: string): { value: Clip | Arrangement | Suite | null; findings: HubFinding[] } {
  let json: unknown;
  try {
    json = JSON.parse(text);
  } catch (e) {
    return { value: null, findings: [{ path: "", message: `not JSON: ${e instanceof Error ? e.message : String(e)}`, blocks: true }] };
  }
  const r = kind === "clip" ? clipOf(json, id) : kind === "arrangement" ? arrangementOf(json, id) : suiteOf(json, id);
  const value = "clip" in r ? r.clip : "arrangement" in r ? r.arrangement : r.suite;
  return { value: r.findings.some((f) => f.blocks) ? null : value, findings: r.findings };
}

export class HubFiles {
  private readonly cache = new Map<string, Parsed>();

  constructor(readonly projectDir: string) {}

  dir(kind: HubKind): string {
    return path.join(this.projectDir, HUB_DIR, HUB_FOLDERS[kind]);
  }

  file(kind: HubKind, id: string): string {
    return path.join(this.dir(kind), `${id}.json`);
  }

  private read(kind: HubKind, id: string): Parsed | null {
    const file = this.file(kind, id);
    let st: fs.Stats;
    try {
      st = fs.statSync(file);
    } catch {
      this.cache.delete(file);
      return null;
    }
    const had = this.cache.get(file);
    if (had && had.mtimeMs === st.mtimeMs && had.size === st.size) return had;
    const p: Parsed = { mtimeMs: st.mtimeMs, size: st.size, ...parse(kind, id, fs.readFileSync(file, "utf8")) };
    this.cache.set(file, p);
    return p;
  }

  /** Every file of a kind that reads, by id, and those that do not. */
  list<K extends HubKind>(kind: K): Listing<Value<K>> {
    const items = new Map<string, Value<K>>();
    const problems: FileProblem[] = [];
    let names: string[] = [];
    try {
      names = fs.readdirSync(this.dir(kind)).filter((n) => n.endsWith(".json")).sort();
    } catch {
      return { items, problems };
    }
    for (const name of names) {
      const id = name.slice(0, -5);
      if (!HUB_ID.test(id)) {
        problems.push({ kind, id, file: path.join(HUB_DIR, HUB_FOLDERS[kind], name), findings: [{ path: "", message: `${name}: a file name is an id of lowercase letters, digits and dashes`, blocks: true }] });
        continue;
      }
      const p = this.read(kind, id);
      if (!p) continue;
      if (p.value) items.set(id, p.value as Value<K>);
      else problems.push({ kind, id, file: path.join(HUB_DIR, HUB_FOLDERS[kind], name), findings: p.findings });
    }
    return { items, problems };
  }

  /** One file's value; null when it is absent or does not read (`problem` says why). */
  get<K extends HubKind>(kind: K, id: string): { value: Value<K> | null; findings: HubFinding[] } | null {
    if (!HUB_ID.test(id)) return null;
    const p = this.read(kind, id);
    return p ? { value: p.value as Value<K> | null, findings: p.findings } : null;
  }

  exists(kind: HubKind, id: string): boolean {
    return HUB_ID.test(id) && fs.existsSync(this.file(kind, id));
  }

  write(kind: "clip", v: Clip): void;
  write(kind: "arrangement", v: Arrangement): void;
  write(kind: "suite", v: Suite): void;
  write(kind: HubKind, v: Clip | Arrangement | Suite): void {
    const file = this.file(kind, v.id);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    const text = kind === "clip" ? fileText("clip", v as Clip) : kind === "arrangement" ? fileText("arrangement", v as Arrangement) : fileText("suite", v as Suite);
    if (fs.existsSync(file) && fs.readFileSync(file, "utf8") === text) return;
    writeFileAtomic(file, text);
  }

  delete(kind: HubKind, id: string): boolean {
    const file = this.file(kind, id);
    if (!fs.existsSync(file)) return false;
    fs.rmSync(file);
    this.cache.delete(file);
    return true;
  }

  /** A free id for a new file: the slug, or the slug with -2, -3 and so on. */
  freeId(kind: HubKind, slug: string): string {
    if (!this.exists(kind, slug)) return slug;
    for (let n = 2; ; n++) if (!this.exists(kind, `${slug}-${n}`)) return `${slug}-${n}`;
  }
}
