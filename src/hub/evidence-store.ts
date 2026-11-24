// @session independent: An interface over the run history; the implementation holds the state.
/**
 * The hub's evidence: the runs every status is worked out from, each with
 * what it tested. The hub reads and writes runs only through this interface.
 * Its one implementation is the local run index (run-index.ts), so every
 * status is this computer's view; a store shared by a team (a folder CI
 * writes, a service) would implement the same interface and answer with a
 * wider scope.
 */
import type { EvidenceScope, QaRun, RunProvenance, RunRecord, RunSource, TrackerLink } from "./contract.js";
import type { RunTimes } from "./ports.js";

/** Who started a run when the hub did it: a plan's run. */
export interface RunTag {
  source: RunSource;
  planRunId: string;
}

export interface EvidenceStore {
  /** Whose runs these are, for the label every status carries. */
  readonly scope: EvidenceScope["store"];
  /** A run as the hub has it now; null for what is no run of a check (a draft, inline steps, a manual run). */
  record(run: QaRun, times?: RunTimes): RunRecord | null;
  /** A run ue-mcp recorded that the hub does not hold, kept unless the hub already has a finished record of it. */
  backfill(run: QaRun, times: RunTimes): void;
  /** Mark a run the hub started as a plan's, now or when its record is first made. */
  tag(runId: string, tag: RunTag): void;
  /**
   * A script moved: check `from` is now `to` (a redirector ue-mcp reported).
   * From then on `to`'s runs include `from`'s, so its history follows it;
   * `from`'s own runs stay its own, for a checkout where it has not moved.
   */
  alias(from: string, to: string): void;
  /** Where each moved check went, by check id. */
  aliases(): ReadonlyMap<string, string>;
  /** What a run tested. Returns the record, or undefined for a run not held. */
  setProvenance(runId: string, provenance: RunProvenance): RunRecord | undefined;
  /** Project files changed while the run played (RunRecord.tainted). */
  setTainted(runId: string): RunRecord | undefined;
  /** A run's tracker links replaced; an empty list removes them. */
  setLinks(runId: string, links: TrackerLink[]): RunRecord | undefined;
  get(runId: string): RunRecord | undefined;
  /** Every run, newest first. */
  all(): RunRecord[];
  /** One check's runs, newest first, with the runs of every check aliased to it. */
  byCheck(checkId: string): RunRecord[];
  /** Every check id that has runs. */
  checkIds(): string[];
  /** A page of runs, newest first, of one check or of all; `before` is a run id or an ISO time. */
  page(o: { check?: string | null; limit: number; before?: string | null }): { runs: RunRecord[]; more: boolean };
}
