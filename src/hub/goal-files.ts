// @session per-session: Reads and writes one project's goal files under that project's data folder.
/**
 * The goal store on this computer: plain JSON files under
 * <dataDir>/hub/ (dataDir is ~/.ue-mcp/data/<project>), one file per
 * record, each written whole through a temporary file and a rename
 * (atomic-write.ts), so a reader or a restart never sees half a record:
 *
 *   store.json              { schema, origin }: the store's random id, written once
 *   goals/<id>.json         a goal or milestone
 *   checks/<check id>.json  what a check says about itself
 *   signoffs/<id>.json      a sign-off
 *   rules/health.json       the project's health rule
 *   imports/<id>.json       a one-time import: QA/Goals, sign-offs.json
 *
 * Each file holds one HubRecord: the record's newest version, with its
 * schema, rev, time and origin. A change writes the next version over it; a
 * removal writes a version with `deleted` and no data, so the id is never
 * taken again and a hosted copy can learn of the removal. Ids are
 * encodeURIComponent'd into file names (a check id holds ":" and "/").
 *
 * Plain files rather than a database: ue-mcp runs on Node 20, which has no
 * built-in SQLite, and a native module would need a build per platform.
 */
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import { HUB_SCHEMA, type AnyHubRecord, type HubRecord, type HubRecordType } from "./contract.js";
import { writeFileAtomic } from "./atomic-write.js";
import type { GoalStore, GoalStoreChange } from "./goal-store.js";

const FOLDERS: Record<HubRecordType, string> = { goal: "goals", check: "checks", signoff: "signoffs", rules: "rules", import: "imports" };
const TYPES = Object.keys(FOLDERS) as HubRecordType[];

const fileName = (id: string): string => `${encodeURIComponent(id)}.json`;

/** A record's envelope as read from a file, or why it is not one. The data's own shape is the reader's to check (goals.ts recordDataOf). */
function envelopeOf(v: unknown, type: HubRecordType, id: string): AnyHubRecord | string {
  if (!v || typeof v !== "object" || Array.isArray(v)) return "it is not a JSON object";
  const r = v as Partial<HubRecord>;
  if (typeof r.schema === "number" && r.schema > HUB_SCHEMA) return `it was written by a newer ue-mcp (schema ${r.schema})`;
  if (r.schema !== HUB_SCHEMA || r.type !== type || r.id !== id) return "it is not this folder's record";
  if (!Number.isInteger(r.rev) || (r.rev as number) < 1 || typeof r.at !== "string" || typeof r.origin !== "string") return "it has no version";
  if (r.deleted !== true && (!r.data || typeof r.data !== "object")) return "it has no data";
  return r as AnyHubRecord;
}

export class GoalFiles implements GoalStore {
  readonly scope = "local" as const;
  readonly origin: string;
  private readonly dir: string;
  /** Every record held, removed ones included, by type and id. */
  private readonly records = new Map<HubRecordType, Map<string, AnyHubRecord>>();
  private readonly problems: string[] = [];

  constructor(dataDir: string, private readonly now: () => string = () => new Date().toISOString()) {
    this.dir = path.join(dataDir, "hub");
    this.origin = this.readOrigin();
    for (const type of TYPES) this.records.set(type, this.readFolder(type));
  }

  private readOrigin(): string {
    const file = path.join(this.dir, "store.json");
    try {
      const saved = JSON.parse(fs.readFileSync(file, "utf-8")) as { origin?: unknown };
      if (typeof saved.origin === "string" && saved.origin) return saved.origin;
    } catch {
      // A new store.
    }
    const origin = crypto.randomUUID();
    fs.mkdirSync(this.dir, { recursive: true });
    writeFileAtomic(file, `${JSON.stringify({ schema: HUB_SCHEMA, origin }, null, 2)}\n`);
    return origin;
  }

  private readFolder(type: HubRecordType): Map<string, AnyHubRecord> {
    const out = new Map<string, AnyHubRecord>();
    const folder = path.join(this.dir, FOLDERS[type]);
    let names: string[];
    try {
      names = fs.readdirSync(folder).filter((n) => n.endsWith(".json"));
    } catch {
      return out;
    }
    for (const name of names) {
      const where = `${FOLDERS[type]}/${name}`;
      let id: string;
      try {
        id = decodeURIComponent(name.slice(0, -".json".length));
      } catch {
        this.problems.push(`${where} is not a record's file name`);
        continue;
      }
      try {
        const r = envelopeOf(JSON.parse(fs.readFileSync(path.join(folder, name), "utf-8")), type, id);
        if (typeof r === "string") this.problems.push(`${where} was not read: ${r}`);
        else out.set(id, r);
      } catch (e) {
        this.problems.push(`${where} was not read: ${e instanceof Error ? e.message : String(e)}`);
      }
    }
    return out;
  }

  list<T extends HubRecordType>(type: T): HubRecord<T>[] {
    return [...this.records.get(type)!.values()].filter((r) => !r.deleted) as unknown as HubRecord<T>[];
  }

  get<T extends HubRecordType>(type: T, id: string): HubRecord<T> | undefined {
    const r = this.records.get(type)!.get(id);
    return r && !r.deleted ? (r as unknown as HubRecord<T>) : undefined;
  }

  held(type: HubRecordType, id: string): boolean {
    return this.records.get(type)!.has(id);
  }

  write(changes: GoalStoreChange[]): void {
    const at = this.now();
    for (const c of changes) {
      const had = this.records.get(c.type)!.get(c.id);
      if (!had && c.data === null) continue;
      const base = { schema: HUB_SCHEMA, type: c.type, id: c.id, rev: (had?.rev ?? 0) + 1, at, origin: this.origin };
      this.put((c.data === null ? { ...base, deleted: true } : { ...base, data: c.data }) as AnyHubRecord);
    }
  }

  adopt(records: AnyHubRecord[]): void {
    for (const r of records) {
      const had = this.records.get(r.type)!.get(r.id);
      this.put({ ...r, rev: Math.max(r.rev, (had?.rev ?? 0) + 1) } as AnyHubRecord);
    }
  }

  private put(r: AnyHubRecord): void {
    const folder = path.join(this.dir, FOLDERS[r.type]);
    fs.mkdirSync(folder, { recursive: true });
    writeFileAtomic(path.join(folder, fileName(r.id)), `${JSON.stringify(r, null, 2)}\n`);
    this.records.get(r.type)!.set(r.id, r);
  }

  warnings(): string[] {
    return [...this.problems];
  }
}
