// @session independent: Pure shaping of a rollback record.
import type { RollbackRecord } from "@db-lyon/flowkit";

/**
 * Turn a handler's `rollback: { method, payload }` into a replayable record.
 *
 * The record's taskName is what the flow runner looks up, and it looks up
 * `<tool>.<action>` names - not bridge method names. Naming the bridge method
 * directly (`set_variable_properties`) missed the task map, fell through to a
 * filesystem probe for a module of that name, and threw; every handler-emitted
 * rollback failed the same way at ~90 sites.
 *
 * So route through the generic bridge task instead and carry the method in the
 * payload. That class path IS registered, and the payload is already in the
 * bridge's own param names because the handler built it - which is also why it
 * must not go back through an action's mapParams.
 */
export function liftRollback(rollback: unknown): RollbackRecord | undefined {
  if (!rollback || typeof rollback !== "object") return undefined;
  const rb = rollback as { method?: unknown; payload?: unknown };
  if (typeof rb.method !== "string" || rb.method.length === 0) return undefined;
  const payload =
    rb.payload && typeof rb.payload === "object"
      ? (rb.payload as Record<string, unknown>)
      : {};
  return { taskName: "ue-mcp.bridge", payload: { ...payload, method: rb.method } };
}
