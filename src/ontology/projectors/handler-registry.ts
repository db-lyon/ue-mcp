/**
 * HandlerRegistryProjector
 *
 * Projects ue-mcp's category tools and their actions into
 * /UE/Mediation/Registry/Tools/** as addressable context points.
 *
 * Each action gets:
 *   - meaning / purpose
 *   - bridge binding (if any)
 *   - classification / approval / risk signals (heuristic until
 *     handlers declare these explicitly)
 *
 * The heuristic is intentionally simple and wrong at the edges. The
 * intent is to establish the shape; handlers will graduate to
 * explicit metadata in a follow-up pass.
 */

import type { ActionSpec, ToolDef } from "../../types.js";
import type { KantFragment, KantPoint, KantSignal, Projector } from "../types.js";

const MEDIATION_BASE = "/UE/Mediation/Registry";

type Classification = "read" | "introspect" | "mutate" | "create" | "destructive";
type Approval = "auto" | "advisory" | "required" | "explicit";
type Risk = "trivial" | "minor" | "significant" | "severe" | "catastrophic";

const CLASSIFICATION_SCORE: Record<Classification, number> = {
  read: 0.0,
  introspect: 0.2,
  mutate: 0.5,
  create: 0.7,
  destructive: 1.0,
};

const APPROVAL_SCORE: Record<Approval, number> = {
  auto: 0.0,
  advisory: 0.33,
  required: 0.66,
  explicit: 1.0,
};

const RISK_SCORE: Record<Risk, number> = {
  trivial: 0.0,
  minor: 0.25,
  significant: 0.5,
  severe: 0.75,
  catastrophic: 1.0,
};

function signal(value: number, marker: string): KantSignal {
  return { kind: "signal", value, marker };
}

const READ_PREFIXES = ["get_", "list_", "find_", "read_", "describe_", "search_", "query_", "inspect_"];
const INTROSPECT_PREFIXES = ["validate_", "diagnose_", "explain_", "preview_", "summarize_", "analyze_", "check_"];
const DESTRUCTIVE_PREFIXES = ["delete_", "destroy_", "remove_", "drop_", "purge_", "clear_", "reset_"];
const CREATE_PREFIXES = ["create_", "add_", "spawn_", "make_", "build_", "new_", "import_", "generate_"];

function classifyAction(actionName: string): Classification {
  const n = actionName.toLowerCase();
  if (READ_PREFIXES.some(p => n.startsWith(p))) return "read";
  if (INTROSPECT_PREFIXES.some(p => n.startsWith(p))) return "introspect";
  if (DESTRUCTIVE_PREFIXES.some(p => n.startsWith(p))) return "destructive";
  if (CREATE_PREFIXES.some(p => n.startsWith(p))) return "create";
  return "mutate";
}

function approvalFor(c: Classification): Approval {
  switch (c) {
    case "read":
    case "introspect":
      return "auto";
    case "mutate":
      return "advisory";
    case "create":
      return "required";
    case "destructive":
      return "explicit";
  }
}

function riskFor(c: Classification): Risk {
  switch (c) {
    case "read":
      return "trivial";
    case "introspect":
      return "minor";
    case "mutate":
      return "significant";
    case "create":
      return "significant";
    case "destructive":
      return "severe";
  }
}

function actionPoint(actionName: string, spec: ActionSpec): KantPoint {
  const classification = classifyAction(actionName);
  const approval = approvalFor(classification);
  const risk = riskFor(classification);

  const fields: Record<string, string | KantSignal> = {
    classification: signal(CLASSIFICATION_SCORE[classification], classification),
    approval: signal(APPROVAL_SCORE[approval], approval),
    risk: signal(RISK_SCORE[risk], risk),
  };
  if (spec.bridge) fields.bridge = spec.bridge;
  if (spec.timeoutMs !== undefined) fields.timeoutMs = String(spec.timeoutMs);
  fields.handlerKind = spec.handler ? "ts" : spec.bridge ? "bridge" : "none";

  return {
    meaning: `Action ${actionName}`,
    purpose: spec.description ?? `Action ${actionName}`,
    fields,
  };
}

function toolPoint(tool: ToolDef): KantPoint {
  const actionChildren: Record<string, KantPoint> = {};
  for (const [actionName, spec] of Object.entries(tool.actions)) {
    actionChildren[actionName] = actionPoint(actionName, spec);
  }

  return {
    meaning: `Tool ${tool.name}`,
    purpose: tool.description.split("\n")[0],
    fields: {
      name: tool.name,
      actionCount: Object.keys(tool.actions).length,
    },
    children: {
      Actions: {
        meaning: "Actions",
        purpose: `Actions exposed by the ${tool.name} category tool`,
        children: actionChildren,
      },
    },
  };
}

export function createHandlerRegistryProjector(tools: readonly ToolDef[]): Projector {
  return {
    name: "handler-registry",
    basePath: MEDIATION_BASE,
    triggerEvents: ["startup", "handler-registry-changed"],
    project(): KantFragment {
      const toolsChildren: Record<string, KantPoint> = {};
      for (const tool of tools) {
        toolsChildren[tool.name] = toolPoint(tool);
      }

      return {
        basePath: MEDIATION_BASE,
        producer: "handler-registry",
        producedAt: new Date().toISOString(),
        points: {
          Tools: {
            meaning: "Category Tools",
            purpose: "ue-mcp MCP tool surface projected as addressable points",
            fields: {
              toolCount: tools.length,
              actionCount: tools.reduce((n, t) => n + Object.keys(t.actions).length, 0),
            },
            children: toolsChildren,
          },
        },
      };
    },
  };
}
