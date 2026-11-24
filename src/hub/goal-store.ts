// @session independent: An interface over the project's goal records; the implementation holds the state.
/**
 * The hub's goal store: goals and milestones with their graph, owner,
 * priority and due date, what each check says about itself, sign-offs, the
 * project's health rule, and the record of a one-time import. One store per
 * project, outside the game repo. The hub reads and writes these records only
 * through this interface (goals.ts holds the rules over them), the way it
 * reads runs only through an EvidenceStore.
 *
 * Its one implementation keeps the records on this computer (goal-files.ts).
 * A hosted store would implement the same interface over the same records:
 * a record never changes shape on the way, and ids are unique across
 * computers.
 */
import type { AnyHubRecord, EvidenceScope, HubRecord, HubRecordData, HubRecordType } from "./contract.js";

/** One record's next version: its data, or null to remove it. */
export type GoalStoreChange = { [T in HubRecordType]: { type: T; id: string; data: HubRecordData[T] | null } }[HubRecordType];

export interface GoalStore {
  /** Whose records these are, for the label every status carries. */
  readonly scope: EvidenceScope["store"];
  /** This store's id, on every version it writes. */
  readonly origin: string;
  /** The live records of one type, each at its newest version. */
  list<T extends HubRecordType>(type: T): HubRecord<T>[];
  /** One live record, or undefined for none or a removed one. */
  get<T extends HubRecordType>(type: T, id: string): HubRecord<T> | undefined;
  /** Whether the store has ever held this id, removed ones included, so a new id never takes an old one's place. */
  held(type: HubRecordType, id: string): boolean;
  /** Write each change as the record's next version. */
  write(changes: GoalStoreChange[]): void;
  /** Take records written in another store (an import) as their next version here, keeping their data, time and origin. */
  adopt(records: AnyHubRecord[]): void;
  /** Files or entries the store could not read, each named. */
  warnings(): string[];
}
