/**
 * The diagram action of each category with a graph reader. Each reads its
 * graph through the existing reader over the bridge and hands the answer to
 * the matching adapter (src/diagram/adapters.ts); rendering is shared.
 */
import type { HandlerActionSpec, ToolContext } from "../core/types.js";
import { DIAGRAM_HELP, diagramAction, readFromBridge } from "../diagram/action.js";
import {
  assetGraphModel,
  behaviorTreeModel,
  blueprintGraphModel,
  controlRigModel,
  materialModel,
  pcgModel,
  ruleExpression,
  stateMachineModel,
  stateTreeModel,
} from "../diagram/adapters.js";

/** Transition rules read for labels, at most; each is one bridge read. */
export const MAX_RULE_READS = 40;
/** Pages of a material read for one diagram (2000 expressions each). */
const MAX_MATERIAL_PAGES = 5;

const text = (v: unknown): string | undefined => (typeof v === "string" && v.trim() ? v.trim() : undefined);

function requirePath(p: Record<string, unknown>, ...keys: string[]): string {
  for (const key of keys) {
    const value = text(p[key]);
    if (value) return value;
  }
  throw new Error(`${keys[0]} is required: the asset whose graph to draw`);
}

async function blueprintModel(ctx: ToolContext, p: Record<string, unknown>, defaultGraph: string) {
  const assetPath = requirePath(p, "assetPath", "path");
  const graphName = text(p.graphName) ?? defaultGraph;
  const summary = await readFromBridge(ctx, "read_blueprint_graph_summary", { assetPath, graphName });
  const subgraphs: Array<{ name: string; summary: unknown; kind: "collapsed" | "function" }> = [];
  const failed: string[] = [];
  const readSub = async (name: string, kind: "collapsed" | "function") => {
    try {
      subgraphs.push({ name, kind, summary: await readFromBridge(ctx, "read_blueprint_graph_summary", { assetPath, graphName: name }) });
    } catch (e) {
      failed.push(`${name} (${e instanceof Error ? e.message : String(e)})`);
    }
  };
  if (p.expandCollapsed === true) {
    const nodes = Array.isArray(summary.nodes) ? summary.nodes as Array<Record<string, unknown>> : [];
    for (const n of nodes) {
      if (n.class === "K2Node_Composite" && typeof n.title === "string" && n.title.trim()) await readSub(n.title.split(/\r?\n/)[0].trim(), "collapsed");
    }
  }
  for (const name of Array.isArray(p.includeGraphs) ? p.includeGraphs : []) {
    if (typeof name === "string" && name.trim()) await readSub(name.trim(), "function");
  }
  const edges = p.edges === "exec" || p.edges === "data" ? p.edges : "all";
  const model = blueprintGraphModel(summary, { edges, subgraphs });
  if (failed.length) model.notes.push(`Could not read: ${failed.join("; ")}.`);
  return model;
}

export const blueprintDiagramAction: HandlerActionSpec = diagramAction({
  description:
    "Draw a Blueprint graph as a diagram: nodes, exec wires (solid) and data wires (dashed), labelled with the pins they join, " +
    "events and function entries marked, read through read_graph_summary. edges: exec | data | all (default all); exec with focus " +
    "on an event traces its execution flow as get_execution_flow does. expandCollapsed draws each collapsed graph as a subgraph " +
    "beside the node that holds it; includeGraphs [names] adds function or macro graphs as subgraphs. " +
    `${DIAGRAM_HELP} Params: assetPath, graphName? (default EventGraph), edges?, expandCollapsed?, includeGraphs?, format?, focus?, depth?, maxNodes?, outputPath?`,
  build: (ctx, p) => blueprintModel(ctx, p, "EventGraph"),
});

export const animationDiagramAction: HandlerActionSpec = diagramAction({
  description:
    "Draw an Animation Blueprint graph as a diagram. With stateMachineName (or graphKind state_machine) a state machine as a " +
    "stateDiagram-v2: states with their animation asset, transitions labelled with their rule (read from each transition's rule " +
    `graph, up to ${MAX_RULE_READS}; conditions=false skips it), blend time and inertialization. graphKind anim_graph (the default ` +
    "without stateMachineName) draws graphName (default AnimGraph) as a flowchart; graphKind control_rig draws a Control Rig's " +
    `RigVM models. ${DIAGRAM_HELP} Params: assetPath, stateMachineName?, graphKind? (state_machine|anim_graph|control_rig), ` +
    `graphName?, conditions?, format?, focus?, depth?, maxNodes?, outputPath?`,
  build: async (ctx, p) => {
    const assetPath = requirePath(p, "assetPath", "path");
    const stateMachineName = text(p.stateMachineName);
    const kind = text(p.graphKind) ?? (stateMachineName ? "state_machine" : "anim_graph");
    if (kind === "control_rig") {
      const graphName = text(p.graphName);
      return controlRigModel(await readFromBridge(ctx, "read_control_rig_graph", {
        assetPath, includePins: false, includeLinks: true, limit: 2000, ...(graphName ? { graphName } : {}),
      }));
    }
    if (kind === "anim_graph") return blueprintModel(ctx, p, "AnimGraph");
    if (kind !== "state_machine") throw new Error(`graphKind '${kind}' is not one of state_machine, anim_graph, control_rig`);
    if (!stateMachineName) throw new Error("stateMachineName is required to draw a state machine; blueprint(list_graphs) or animation(read_anim_graph) names them");

    const answer = await readFromBridge(ctx, "read_state_machine", { assetPath, stateMachineName });
    const transitions = Array.isArray(answer.transitions) ? answer.transitions as Array<Record<string, unknown>> : [];
    const conditions = new Map<string, string>();
    const notes: string[] = [];
    if (p.conditions !== false) {
      for (const t of transitions.slice(0, MAX_RULE_READS)) {
        const guid = text(t.transitionGuid);
        if (!guid) continue;
        try {
          const rule = ruleExpression(await readFromBridge(ctx, "read_blueprint_graph", {
            assetPath, transitionGuid: guid, includePins: true, includeLinks: true, includeDefaults: true, limit: 200,
          }));
          if (rule) conditions.set(guid, rule);
        } catch {
          // A rule that cannot be read leaves its transition unlabelled, not the diagram undrawn.
        }
      }
      if (transitions.length > MAX_RULE_READS) notes.push(`Rules are shown for the first ${MAX_RULE_READS} of ${transitions.length} transitions.`);
    }
    const model = stateMachineModel(answer, conditions);
    model.notes.push(...notes);
    return model;
  },
});

export const stateTreeDiagramAction: HandlerActionSpec = diagramAction({
  description:
    "Draw a StateTree as a stateDiagram-v2: the state hierarchy as nested states (a parent entered through its first child), each " +
    "state's tasks and enter conditions, and every transition labelled with its trigger, event tag, conditions and delay. " +
    "Succeeded and Failed transitions end at Tree succeeded / Tree failed; NextState goes to the next sibling. Evaluators and " +
    `global tasks are listed in notes. ${DIAGRAM_HELP} Params: assetPath, format?, focus?, depth?, maxNodes?, outputPath?`,
  build: async (ctx, p) => stateTreeModel(await readFromBridge(ctx, "read_state_tree", { assetPath: requirePath(p, "assetPath", "path") })),
});

export const behaviorTreeDiagramAction: HandlerActionSpec = diagramAction({
  description:
    "Draw a Behavior Tree as a top-down flowchart from ROOT: composites and tasks, each child edge numbered in execution order, " +
    "decorators (if: ...) and services listed on the node they guard, node ids being the nodePath read_behavior_tree_graph " +
    `reports. ${DIAGRAM_HELP} Params: assetPath, format?, focus?, depth?, maxNodes?, outputPath?`,
  build: async (ctx, p) => behaviorTreeModel(await readFromBridge(ctx, "read_behavior_tree_graph", { assetPath: requirePath(p, "assetPath", "path") })),
});

export const materialDiagramAction: HandlerActionSpec = diagramAction({
  description:
    "Draw a material's expression graph as a left-to-right flowchart into the material output: parameters marked, every wire " +
    "labelled with the input it feeds (and the output index when not the first), each root input (BaseColor, Roughness, ...) " +
    `labelled on its wire into the output node. Node ids are expr:<nodeId>, the index the other material actions take. ${DIAGRAM_HELP} ` +
    `Params: assetPath (or materialPath), format?, focus?, depth?, maxNodes?, outputPath?`,
  build: async (ctx, p) => {
    const materialPath = requirePath(p, "assetPath", "materialPath", "path");
    const pages: Array<Record<string, unknown>> = [];
    let cursor: string | undefined;
    do {
      const page = await readFromBridge(ctx, "read_material_graph", { materialPath, limit: 2000, ...(cursor ? { cursor } : {}) });
      pages.push(page);
      cursor = page.hasMore === true && typeof page.nextCursor === "string" ? page.nextCursor : undefined;
    } while (cursor && pages.length < MAX_MATERIAL_PAGES);
    return materialModel(pages);
  },
});

export const pcgDiagramAction: HandlerActionSpec = diagramAction({
  description:
    "Draw a PCG graph as a left-to-right flowchart from its input node to its output node, every edge labelled with the pins it " +
    `joins when they are not the default In/Out. ${DIAGRAM_HELP} Params: assetPath, format?, focus?, depth?, maxNodes?, outputPath?`,
  build: async (ctx, p) => pcgModel(await readFromBridge(ctx, "read_pcg_graph", { assetPath: requirePath(p, "assetPath", "path") })),
});

export const assetGraphDiagramAction: HandlerActionSpec = diagramAction({
  description:
    "Draw the node graph of any EdGraph-backed asset read_graph can read (the types without a category of their own, " +
    "CustomizableObject among them) as a flowchart, one subgraph per graph when it has several, wires labelled with their pins. " +
    "maxNodes here caps the drawing; the read itself takes up to 500 nodes per graph. " +
    `${DIAGRAM_HELP} Params: assetPath, graphName?, format?, focus?, depth?, maxNodes?, outputPath?`,
  build: async (ctx, p) => {
    const graphName = text(p.graphName);
    return assetGraphModel(await readFromBridge(ctx, "read_asset_graph", {
      assetPath: requirePath(p, "assetPath", "path"), includePins: true, ...(graphName ? { graphName } : {}),
    }));
  },
});
