/**
 * WorkaroundProjector
 *
 * Projects the session workaround log into /UE/Audit/Workarounds/**.
 * Each execute_python call becomes a named child point keyed by the
 * 1-based sequence number within the session. Fields record the
 * timestamp, the first line of the submitted code (for quick scan),
 * and the result snippet if captured.
 *
 * This is ue-mcp's first audit projector. The pattern generalizes: a
 * session-local in-memory log becomes a queryable .kant layer,
 * replacing ad-hoc state readout with composable context.
 */

import type { WorkaroundEntry } from "../../workaround-tracker.js";
import type { KantFragment, KantPoint, Projector } from "../types.js";

const AUDIT_BASE = "/UE/Audit/Workarounds";

function firstLine(code: string): string {
  const lines = code.split(/\r?\n/);
  for (const l of lines) {
    const t = l.trim();
    if (t) return t.length > 140 ? `${t.slice(0, 137)}...` : t;
  }
  return "";
}

export function createWorkaroundProjector(
  getWorkarounds: () => readonly WorkaroundEntry[],
): Projector {
  return {
    name: "workarounds",
    basePath: AUDIT_BASE,
    triggerEvents: ["manual", "flow-completed"],
    project(): KantFragment {
      const entries = getWorkarounds();
      const entryChildren: Record<string, KantPoint> = {};

      entries.forEach((entry, i) => {
        const seq = i + 1;
        const point: KantPoint = {
          meaning: `Workaround ${seq}`,
          purpose: "execute_python call recorded as a gap in native tool coverage",
          fields: {
            sequence: seq,
            timestamp: entry.timestamp,
            firstLine: firstLine(entry.code),
            codeLength: entry.code.length,
          },
        };
        if (entry.resultSnippet) {
          (point.fields as Record<string, string | number>).resultSnippet =
            entry.resultSnippet.length > 200
              ? `${entry.resultSnippet.slice(0, 197)}...`
              : entry.resultSnippet;
        }
        entryChildren[`entry_${String(seq).padStart(4, "0")}`] = point;
      });

      return {
        basePath: AUDIT_BASE,
        producer: "workarounds",
        producedAt: new Date().toISOString(),
        points: {
          Log: {
            meaning: "Workaround Log",
            purpose: "Append-only record of execute_python calls in the current session",
            fields: {
              entryCount: entries.length,
            },
            children: entryChildren,
          },
        },
      };
    },
  };
}
