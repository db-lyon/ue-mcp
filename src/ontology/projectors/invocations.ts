/**
 * InvocationProjector
 *
 * Projects the session invocation log into /UE/Audit/Invocations.
 * Fuses with flowkit: flow tasks dispatch through the MCP server's
 * category-tool registration, so every flow-task call lands here as
 * a first-class audit entry alongside direct agent invocations.
 *
 * Each entry becomes a named child under Log/entry_NNNN with fields
 * recording the tool, action, status, duration, timestamp, and
 * (when applicable) the error code and message snippet.
 */

import type { InvocationEntry, InvocationStatus } from "../../invocation-tracker.js";
import type { KantFragment, KantPoint, KantSignal, Projector } from "../types.js";

const AUDIT_BASE = "/UE/Audit/Invocations";

const STATUS_SCORE: Record<InvocationStatus, number> = {
  ok: 0,
  error: 1,
  requires_unmet: 0.5,
};

function statusSignal(s: InvocationStatus): KantSignal {
  return { kind: "signal", value: STATUS_SCORE[s], marker: s };
}

function trimSnippet(s: string | undefined, max = 200): string | undefined {
  if (!s) return undefined;
  return s.length > max ? `${s.slice(0, max - 3)}...` : s;
}

export function createInvocationProjector(
  getInvocations: () => readonly InvocationEntry[],
): Projector {
  return {
    name: "invocations",
    basePath: AUDIT_BASE,
    triggerEvents: ["manual", "flow-completed"],
    project(): KantFragment {
      const entries = getInvocations();
      const children: Record<string, KantPoint> = {};

      let okCount = 0;
      let errCount = 0;
      let reqCount = 0;
      for (const e of entries) {
        if (e.status === "ok") okCount += 1;
        else if (e.status === "error") errCount += 1;
        else if (e.status === "requires_unmet") reqCount += 1;

        const fields: Record<string, string | number | KantSignal> = {
          sequence: e.sequence,
          tool: e.tool,
          actionName: e.action,
          status: statusSignal(e.status),
          durationMs: e.durationMs,
          timestamp: e.timestamp,
        };
        const errCode = trimSnippet(e.errorCode, 80);
        if (errCode) fields.errorCode = errCode;
        const errSnippet = trimSnippet(e.errorSnippet);
        if (errSnippet) fields.errorSnippet = errSnippet;

        children[`entry_${String(e.sequence).padStart(6, "0")}`] = {
          meaning: `Invocation ${e.sequence}`,
          purpose: `${e.tool}.${e.action} (${e.status})`,
          fields,
        };
      }

      return {
        basePath: AUDIT_BASE,
        producer: "invocations",
        producedAt: new Date().toISOString(),
        points: {
          Log: {
            meaning: "Invocation Log",
            purpose: "Bounded ring of MCP tool dispatches from the current session",
            fields: {
              entryCount: entries.length,
              okCount,
              errorCount: errCount,
              requiresUnmetCount: reqCount,
            },
            children,
          },
        },
      };
    },
  };
}
