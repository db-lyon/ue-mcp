/**
 * Reader answers to diagram models, one function per graph reader.
 *
 * Each takes the JSON the bridge action returns, exactly as it arrives, and
 * nothing else, so every one is tested against a fixture of that answer. The
 * field names here are the C++ handlers' (BlueprintHandlers.cpp,
 * AnimationHandlers_StateMachine.cpp, StateTreeHandlers.cpp and so on); a
 * reader that renames a field breaks its fixture test, not a user's diagram.
 */
import type { DiagramEdge, DiagramGroup, DiagramModel, DiagramNode, EdgeKind, NodeRole } from "./model.js";
import { clip } from "./model.js";

type Json = Record<string, unknown>;

const obj = (v: unknown): Json => (v && typeof v === "object" && !Array.isArray(v) ? (v as Json) : {});
const arr = (v: unknown): Json[] => (Array.isArray(v) ? v.filter((x) => x && typeof x === "object") as Json[] : []);
const str = (v: unknown): string => (typeof v === "string" ? v : typeof v === "number" ? String(v) : "");
const num = (v: unknown): number | undefined => (typeof v === "number" && Number.isFinite(v) ? v : undefined);

/** `/Game/AI/BT_Enemy.BT_Enemy` -> `BT_Enemy`. */
export function assetName(path: string): string {
  const last = path.split("/").pop() ?? path;
  return last.split(".")[0] || path;
}

/** A node class without the engine's family prefix: `K2Node_CallFunction` -> `CallFunction`. */
function shortClass(cls: string): string {
  return cls.replace(/^(K2Node_|AnimGraphNode_|EdGraphNode_|MaterialExpression|PCGSettings|RigVM|BTComposite_|BTTask_|BTDecorator_|BTService_)/, "") || cls;
}

/** The first line of a node title; FullTitle carries a second line for the target. */
function firstLine(text: string): string {
  return text.split(/\r?\n/)[0].trim();
}

const GENERIC_PINS = new Set([
  "", "then", "execute", "exec", "output", "outputpin", "returnvalue", "result", "value", "self", "in", "out", "input",
  "executecontext", "pose", "outpose", "inpose",
]);
const isGeneric = (pin: string) => GENERIC_PINS.has(pin.trim().toLowerCase());

/** A wire's label: the pin names that say something, `From -> To`, or just one of them. */
function pinLabel(fromPin: string, toPin: string): string | undefined {
  const parts = [fromPin, toPin].filter((p) => !isGeneric(p));
  return parts.length ? parts.join(" → ") : undefined;
}

/** A class worth showing under a title: not one the title already says, and not the ubiquitous function call. */
function classDetail(cls: string, title: string): string | undefined {
  const short = shortClass(cls);
  if (!short || short === "CallFunction" || title.toLowerCase().includes(short.toLowerCase())) return undefined;
  return short;
}

function blueprintRole(cls: string, title = ""): NodeRole {
  // A collapsed graph's tunnel pair: Inputs is where it starts, Outputs where it ends.
  if (cls === "K2Node_Tunnel" && /^outputs?$/i.test(title.trim())) return "exit";
  if (/^K2Node_(Event|CustomEvent|FunctionEntry|InputAction|InputKey|InputAxisEvent|EnhancedInputAction|ComponentBoundEvent|ActorBoundEvent|Tunnel)$/.test(cls)) return "entry";
  if (/^K2Node_FunctionResult$/.test(cls) || /AnimGraphNode_Root$|TransitionResult$|StateResult$/.test(cls)) return "exit";
  if (/^K2Node_(IfThenElse|Switch\w*|ExecutionSequence|MultiGate|DoOnceMultiInput|Select)$/.test(cls)) return "branch";
  if (/^K2Node_(Composite|MacroInstance)$/.test(cls)) return "collapsed";
  if (/^K2Node_(VariableGet|VariableSet|Self|Literal)$/.test(cls)) return "variable";
  if (/StateMachine$/.test(cls)) return "composite";
  return "default";
}

export interface BlueprintGraphOptions {
  /** Which wires to draw. Default all. */
  edges?: "exec" | "data" | "all";
  /** Graphs drawn as subgraphs beside the main one: collapsed graphs and functions, each a read_graph_summary answer. */
  subgraphs?: Array<{ name: string; summary: unknown; kind?: "collapsed" | "function" }>;
}

/** blueprint(read_graph_summary): nodes plus execEdges and dataEdges. Also an AnimBP's AnimGraph. */
export function blueprintGraphModel(summary: unknown, options: BlueprintGraphOptions = {}): DiagramModel {
  const s = obj(summary);
  const which = options.edges ?? "all";
  const nodes: DiagramNode[] = [];
  const edges: DiagramEdge[] = [];
  const groups: DiagramGroup[] = [];
  const known = new Set<string>();

  const add = (answer: Json, group?: string) => {
    for (const n of arr(answer.nodes)) {
      const id = str(n.id);
      if (!id || known.has(id)) continue;
      known.add(id);
      const cls = str(n.class);
      const title = firstLine(str(n.title)) || shortClass(cls);
      nodes.push({ id, label: clip(title, 60), detail: classDetail(cls, title), role: blueprintRole(cls, title), group });
    }
    const wires: Array<[Json[], EdgeKind]> = [];
    if (which !== "data") wires.push([arr(answer.execEdges), "exec"]);
    if (which !== "exec") wires.push([arr(answer.dataEdges), "data"]);
    for (const [list, kind] of wires) {
      for (const e of list) {
        const from = str(e.from), to = str(e.to);
        if (!known.has(from) || !known.has(to)) continue;
        const label = kind === "exec" ? (isGeneric(str(e.fromPin)) ? undefined : str(e.fromPin)) : pinLabel(str(e.fromPin), str(e.toPin));
        edges.push({ from, to, kind, ...(label ? { label } : {}) });
      }
    }
  };

  // A title or class filter hides nodes but never edges, so add() keeps only
  // the edges whose two ends were listed.
  add(s);

  for (const sub of options.subgraphs ?? []) {
    const groupId = `graph:${sub.name}`;
    groups.push({ id: groupId, label: `${sub.kind === "function" ? "Function" : "Collapsed graph"}: ${sub.name}` });
    const before = nodes.length;
    add(obj(sub.summary), groupId);
    // The collapsed node in the outer graph opens onto its graph's entry.
    const owner = nodes.slice(0, before).find((n) => n.role === "collapsed" && n.label === sub.name);
    const entry = nodes.slice(before).find((n) => n.role === "entry");
    if (owner && entry) edges.push({ from: owner.id, to: entry.id, kind: "child", label: "expands" });
  }

  const path = str(s.blueprintPath) || str(s.path);
  const graphName = str(s.graphName) || "EventGraph";
  return {
    kind: "flowchart",
    title: `${assetName(path) || "Blueprint"} · ${graphName}`,
    direction: "LR",
    nodes,
    edges,
    groups,
    notes: s.filtered === true ? ["The graph was read with a title or class filter, so only matching nodes are drawn."] : [],
  };
}

/**
 * A transition rule graph (blueprint read_graph with transitionGuid, pins and
 * links on) as one readable expression: what feeds the result node's
 * bCanEnterTransition pin, nested. Undefined when the rule is empty.
 */
export function ruleExpression(graph: unknown, maxLength = 48): string | undefined {
  const nodes = arr(obj(graph).nodes);
  const byId = new Map(nodes.map((n) => [str(n.id), n]));
  const result = nodes.find((n) => /TransitionResult/.test(str(n.class)));
  if (!result) return undefined;
  const pins = (n: Json) => arr(n.pins);
  const isInput = (p: Json) => /^input$/i.test(str(p.direction));
  const resultPin = pins(result).find((p) => isInput(p) && /CanEnterTransition/i.test(str(p.name))) ?? pins(result).find(isInput);
  if (!resultPin) return undefined;

  const visiting = new Set<string>();
  const render = (id: string, depth: number): string => {
    const n = byId.get(id);
    if (!n) return "?";
    if (depth > 3 || visiting.has(id)) return "…";
    visiting.add(id);
    const title = firstLine(str(n.title)) || shortClass(str(n.class));
    const args: string[] = [];
    for (const p of pins(n)) {
      if (!isInput(p) || /^exec$/i.test(str(p.type)) || /^(self|WorldContextObject|__WorldContext)$/i.test(str(p.name))) continue;
      const link = arr(p.linkedTo)[0];
      if (link) args.push(render(str(link.nodeId), depth + 1));
      else if (str(p.defaultValue) !== "") args.push(str(p.defaultValue));
    }
    visiting.delete(id);
    if (!args.length) return title;
    if (args.length === 2 && /^(<|>|<=|>=|==|!=|\+|-|\*|\/|%|AND|OR|XOR|NAND|NOR)$/i.test(title)) return `${args[0]} ${title} ${args[1]}`;
    if (args.length === 1 && /^NOT$/i.test(title)) return `NOT ${args[0]}`;
    return `${title}(${args.join(", ")})`;
  };

  const link = arr(resultPin.linkedTo)[0];
  if (link) return clip(render(str(link.nodeId), 0), maxLength);
  const literal = str(resultPin.defaultValue).toLowerCase();
  if (literal === "true") return "always";
  return undefined;
}

/** animation(read_state_machine): states and transitions. `conditions` maps transitionGuid to a rule expression. */
export function stateMachineModel(answer: unknown, conditions: Map<string, string> = new Map()): DiagramModel {
  const a = obj(answer);
  const nodes: DiagramNode[] = [];
  const names = new Set<string>();
  for (const s of arr(a.states)) {
    const name = str(s.name);
    if (!name || names.has(name)) continue;
    names.add(name);
    const anim = str(s.animAsset);
    nodes.push({ id: name, label: name, role: "state", ...(anim ? { detail: assetName(anim) } : {}) });
  }
  const edges: DiagramEdge[] = [];
  for (const t of arr(a.transitions)) {
    const from = str(t.fromState), to = str(t.toState);
    if (!names.has(from) || !names.has(to)) continue;
    const parts: string[] = [];
    const rule = conditions.get(str(t.transitionGuid));
    if (rule) parts.push(rule);
    const blend = num(t.blendDuration);
    if (blend !== undefined && blend > 0) parts.push(`${Math.round(blend * 1000) / 1000}s`);
    if (str(t.logicType) === "Inertialization") parts.push("inertial");
    edges.push({ from, to, kind: "transition", ...(parts.length ? { label: parts.join(" · ") } : {}) });
  }
  const entry = str(a.entryState);
  return {
    kind: "state",
    title: `${assetName(str(a.assetPath)) || "AnimBlueprint"} · ${str(a.stateMachineName) || "State machine"}`,
    direction: "LR",
    nodes,
    edges,
    groups: [],
    ...(entry && names.has(entry) ? { initial: [entry] } : {}),
    notes: [],
  };
}

/** animation(read_control_rig_graph): RigVM models, their nodes and pin-path links. */
export function controlRigModel(answer: unknown): DiagramModel {
  const a = obj(answer);
  const models = arr(a.models);
  const nodes: DiagramNode[] = [];
  const edges: DiagramEdge[] = [];
  const groups: DiagramGroup[] = [];
  const notes: string[] = [];
  for (const m of models) {
    const modelName = str(m.name) || "RigGraph";
    const group = models.length > 1 ? `model:${modelName}` : undefined;
    if (group) groups.push({ id: group, label: modelName });
    const paths: string[] = [];
    for (const n of arr(m.nodes)) {
      const nodePath = str(n.nodePath) || str(n.name);
      if (!nodePath) continue;
      paths.push(nodePath);
      const cls = str(n.class);
      const isEntry = /Event|Entry/i.test(nodePath) && /UnitNode|Event/i.test(cls);
      nodes.push({
        id: `${modelName}/${nodePath}`,
        label: clip(str(n.title) || str(n.name) || nodePath, 60),
        detail: shortClass(cls) || undefined,
        role: isEntry ? "entry" : /Variable/i.test(cls) ? "variable" : /Collapse|FunctionReference/i.test(cls) ? "collapsed" : "default",
        group,
      });
    }
    // Longest node path first, so `A.B` wins over `A` for the pin path `A.B.Pin`.
    paths.sort((x, y) => y.length - x.length);
    const split = (pinPath: string): [string, string] | undefined => {
      const owner = paths.find((p) => pinPath.startsWith(`${p}.`));
      return owner ? [owner, pinPath.slice(owner.length + 1)] : undefined;
    };
    for (const l of arr(m.links)) {
      const from = split(str(l.source)), to = split(str(l.target));
      if (!from || !to) continue;
      const exec = /ExecuteContext|^Execute$/i.test(from[1]) || /ExecuteContext|^Execute$/i.test(to[1]);
      const label = exec ? undefined : pinLabel(from[1], to[1]);
      edges.push({ from: `${modelName}/${from[0]}`, to: `${modelName}/${to[0]}`, kind: exec ? "exec" : "data", ...(label ? { label } : {}) });
    }
    if (m.nodesTruncated === true) notes.push(`${modelName} hit the reader's node limit; only the nodes it returned are drawn.`);
  }
  return {
    kind: "flowchart",
    title: `${assetName(str(a.assetPath)) || "Control Rig"} · Rig graph`,
    direction: "LR",
    nodes,
    edges,
    groups,
    notes,
  };
}

/** asset(read_graph): any EdGraph-backed asset, every graph with nodes and linked pins. */
export function assetGraphModel(answer: unknown): DiagramModel {
  const a = obj(answer);
  const graphs = arr(a.graphs);
  const nodes: DiagramNode[] = [];
  const edges: DiagramEdge[] = [];
  const groups: DiagramGroup[] = [];
  const known = new Set<string>();
  for (const graph of graphs) {
    const graphName = str(graph.name) || "Graph";
    const group = graphs.length > 1 ? `graph:${graphName}` : undefined;
    if (group) groups.push({ id: group, label: graphName });
    for (const n of arr(graph.nodes)) {
      const id = str(n.nodeGuid) || str(n.path) || str(n.name);
      if (!id || known.has(id)) continue;
      known.add(id);
      const cls = str(n.class);
      const title = firstLine(str(n.title)) || str(n.name);
      nodes.push({ id, label: clip(title, 60), detail: classDetail(cls, title), role: blueprintRole(cls, title), group });
    }
  }
  for (const graph of graphs) {
    for (const n of arr(graph.nodes)) {
      const from = str(n.nodeGuid) || str(n.path) || str(n.name);
      for (const p of arr(n.pins)) {
        if (!/^output$/i.test(str(p.direction))) continue;
        const kind: EdgeKind = /exec/i.test(str(p.type)) ? "exec" : "data";
        for (const l of arr(p.linkedTo)) {
          const to = str(l.nodeGuid) || str(l.nodePath) || str(l.nodeName);
          if (!known.has(from) || !known.has(to)) continue;
          const label = pinLabel(str(p.name), str(l.pinName));
          edges.push({ from, to, kind, ...(label ? { label } : {}) });
        }
      }
    }
  }
  return {
    kind: "flowchart",
    title: `${assetName(str(a.assetPath)) || "Graph"}${str(a.assetClass) ? ` · ${str(a.assetClass)}` : ""}`,
    direction: "LR",
    nodes,
    edges,
    groups,
    notes: a.truncated === true ? [str(a.truncatedNote) || "The reader stopped at its node limit; only the nodes it returned are drawn."] : [],
  };
}

/** gameplay(read_behavior_tree_graph): the walked tree with decorators and services. */
export function behaviorTreeModel(answer: unknown): DiagramModel {
  const a = obj(answer);
  const nodes: DiagramNode[] = [];
  const edges: DiagramEdge[] = [];
  const nameOf = (n: Json) => str(n.nodeName) || shortClass(str(n.class)) || str(n.name);
  const attached = (list: Json[], prefix: string) => list.map((d) => `${prefix} ${nameOf(d)}`);

  const idOf = (n: Json) => str(n.path) || `node${nodes.length}`;
  const walk = (n: Json, decorators: Json[], id = idOf(n)): string => {
    const label = nameOf(n);
    const description = firstLine(str(n.staticDescription));
    const lines = [
      ...attached(decorators, "if:"),
      ...(description && description !== label ? [clip(description, 46)] : []),
      ...attached(arr(n.services), "service:"),
    ];
    const kind = str(n.kind);
    nodes.push({ id, label, role: kind === "composite" ? "composite" : "task", ...(lines.length ? { detail: lines.join("\n") } : {}) });
    arr(n.children).forEach((c, i) => {
      const child = obj(c.child);
      if (!Object.keys(child).length) return;
      // The edge before the subtree, so the text reads top-down like the tree.
      const childId = idOf(child);
      edges.push({ from: id, to: childId, kind: "child", label: String(i + 1) });
      walk(child, arr(c.decorators), childId);
    });
    return id;
  };

  const root = obj(a.root);
  const rootDecorators = arr(a.rootDecorators);
  nodes.push({ id: "ROOT", label: "ROOT", role: "entry", ...(str(a.blackboardAsset) ? { detail: `blackboard: ${assetName(str(a.blackboardAsset))}` } : {}) });
  if (Object.keys(root).length) {
    const top = idOf(root);
    edges.push({ from: "ROOT", to: top, kind: "child" });
    walk(root, rootDecorators, top);
  }
  return {
    kind: "flowchart",
    title: `${str(a.name) || assetName(str(a.assetPath)) || "Behavior Tree"} · Behavior Tree`,
    direction: "TB",
    nodes,
    edges,
    groups: [],
    notes: Object.keys(root).length ? [] : ["The tree has no root node yet."],
  };
}

/**
 * material(read_graph): expressions with their connected inputs, and the root
 * inputs under `connections`. `pages` is every page the cursor walked, first
 * page first; the root connections are read from the first.
 */
export function materialModel(pages: unknown[]): DiagramModel {
  const first = obj(pages[0]);
  const expressions = pages.flatMap((p) => arr(obj(p).expressions));
  const materialPath = str(first.materialPath);
  const nodes: DiagramNode[] = [];
  const edges: DiagramEdge[] = [];
  const known = new Set<string>();
  for (const e of expressions) {
    const index = str(e.nodeId);
    if (index === "" || known.has(index)) continue;
    known.add(index);
    const cls = shortClass(str(e.class));
    const label = str(e.parameterName) || str(e.name) || firstLine(str(e.description)) || cls;
    nodes.push({
      id: `expr:${index}`,
      label: clip(label, 48),
      detail: cls !== label ? `${cls} #${index}` : `#${index}`,
      role: str(e.parameterName) ? "parameter" : cls === "Custom" || /FunctionCall/.test(cls) ? "function" : "default",
    });
  }
  for (const e of expressions) {
    const to = `expr:${str(e.nodeId)}`;
    for (const input of arr(e.inputs)) {
      const source = num(input.connectedExpressionIndex);
      if (source === undefined || !known.has(String(source))) continue;
      const out = num(input.connectedOutputIndex) ?? 0;
      const name = str(input.inputName);
      const label = [out > 0 ? `out ${out}` : "", name].filter(Boolean).join(" → ");
      edges.push({ from: `expr:${source}`, to, kind: "data", ...(label ? { label } : {}) });
    }
  }
  const root = "material";
  nodes.push({ id: root, label: assetName(materialPath) || "Material", detail: "Material output", role: "output" });
  for (const [property, value] of Object.entries(obj(first.connections))) {
    const c = obj(value);
    const source = num(c.expressionIndex);
    if (source === undefined || !known.has(String(source))) continue;
    const out = num(c.outputIndex) ?? 0;
    edges.push({ from: `expr:${source}`, to: root, kind: "data", label: out > 0 ? `out ${out} → ${property}` : property });
  }
  const last = obj(pages[pages.length - 1]);
  return {
    kind: "flowchart",
    title: `${assetName(materialPath) || "Material"} · Material graph`,
    direction: "LR",
    nodes,
    edges,
    groups: [],
    notes: last.hasMore === true ? ["The material has more expressions than were read; only the pages read are drawn."] : [],
  };
}

/** pcg(read_graph): nodes, the implicit input and output nodes, and labelled edges. */
export function pcgModel(answer: unknown): DiagramModel {
  const a = obj(answer);
  const nodes: DiagramNode[] = [];
  const known = new Set<string>();
  const addNode = (n: Json, role: NodeRole) => {
    const id = str(n.name);
    if (!id || known.has(id)) return;
    known.add(id);
    const title = str(n.title) || id;
    nodes.push({ id, label: clip(title, 60), role, ...(title !== id && role === "default" ? { detail: id } : {}) });
  };
  if (Object.keys(obj(a.inputNode)).length) addNode(obj(a.inputNode), "entry");
  for (const n of arr(a.nodes)) addNode(n, "default");
  if (Object.keys(obj(a.outputNode)).length) addNode(obj(a.outputNode), "exit");
  const edges: DiagramEdge[] = [];
  for (const e of arr(a.edges)) {
    const from = str(e.from), to = str(e.to);
    if (!known.has(from) || !known.has(to)) continue;
    const label = pinLabel(str(e.fromPin), str(e.toPin));
    edges.push({ from, to, kind: "data", ...(label ? { label } : {}) });
  }
  return {
    kind: "flowchart",
    title: `${str(a.name) || assetName(str(a.path)) || "PCG graph"} · PCG graph`,
    direction: "LR",
    nodes,
    edges,
    groups: [],
    notes: [],
  };
}

const TRIGGER_WORDS: Record<string, string> = {
  OnStateSucceeded: "succeeded",
  OnStateFailed: "failed",
  OnTick: "tick",
  OnEvent: "event",
};

function structLabel(node: Json): string {
  const name = str(node.structName) || assetName(str(node.structType));
  return name.replace(/^StateTree/, "").replace(/(Condition|Task)$/, "") || name;
}

/** The transition's trigger, event, conditions and delay as one label. */
function transitionLabel(t: Json): string | undefined {
  const parts: string[] = [];
  const triggers = str(t.trigger).split("|").filter((x) => x && x !== "None");
  if (triggers.includes("OnStateSucceeded") && triggers.includes("OnStateFailed")) parts.push("completed");
  else for (const tr of triggers) parts.push(TRIGGER_WORDS[tr] ?? tr);
  if (str(t.eventTag)) parts.push(str(t.eventTag));
  const conditions = arr(t.conditions);
  if (conditions.length) {
    const expr = conditions.map((c, i) => (i === 0 ? structLabel(c) : `${str(c.operand) === "Or" ? "or" : "and"} ${structLabel(c)}`)).join(" ");
    parts.push(`if ${expr}`);
  }
  if (t.bDelayTransition === true && (num(t.delayDuration) ?? 0) > 0) parts.push(`after ${num(t.delayDuration)}s`);
  if (t.bEnabled === false) parts.push("disabled");
  return parts.length ? parts.join(" · ") : undefined;
}

/** statetree(read): the state hierarchy, tasks, enter conditions and transitions. */
export function stateTreeModel(answer: unknown): DiagramModel {
  const a = obj(answer);
  const nodes: DiagramNode[] = [];
  const groups: DiagramGroup[] = [];
  const edges: DiagramEdge[] = [];
  const known = new Set<string>();
  const pending: Array<{ from: string; t: Json; next?: string }> = [];
  const idOf = (s: Json) => str(s.id) || str(s.path) || str(s.name);

  const walk = (s: Json, parent: string | undefined, next: string | undefined) => {
    const id = idOf(s);
    if (!id || known.has(id)) return;
    known.add(id);
    const children = arr(s.children);
    const type = str(s.type);
    const lines: string[] = [];
    if (type && type !== "State" && type !== "Group") lines.push(type === "LinkedAsset" && str(s.linkedAsset) ? `linked: ${assetName(str(s.linkedAsset))}` : type.toLowerCase());
    for (const c of arr(s.enterConditions)) lines.push(`enter if: ${structLabel(c)}`);
    for (const t of arr(s.tasks)) lines.push(`task: ${structLabel(t)}`);
    if (s.bEnabled === false) lines.push("disabled");
    const detail = lines.length ? lines.join("\n") : undefined;
    if (children.length) {
      const firstEnabled = children.find((c) => c.bEnabled !== false) ?? children[0];
      groups.push({ id, label: str(s.name) || id, ...(parent ? { parent } : {}), initial: idOf(firstEnabled) });
      nodes.push({ id, label: str(s.name) || id, role: "composite", group: id, anchorOf: id, ...(detail ? { detail } : {}) });
    } else {
      nodes.push({ id, label: str(s.name) || id, role: type === "Linked" || type === "LinkedAsset" || type === "Subtree" ? "collapsed" : "state", ...(parent ? { group: parent } : {}), ...(detail ? { detail } : {}) });
    }
    for (const t of arr(s.transitions)) pending.push({ from: id, t, next });
    children.forEach((c, i) => walk(c, id, children[i + 1] ? idOf(children[i + 1]) : next));
  };
  const roots = arr(a.subTrees);
  roots.forEach((r, i) => walk(r, undefined, roots[i + 1] ? idOf(roots[i + 1]) : undefined));

  const terminal = (id: string, label: string) => {
    if (!known.has(id)) {
      known.add(id);
      nodes.push({ id, label, role: "exit" });
    }
    return id;
  };
  for (const { from, t, next } of pending) {
    const type = str(t.transitionType);
    let to: string | undefined;
    if (type === "GotoState") to = str(t.targetStateId);
    else if (type === "NextState" || type === "NextSelectableState") to = next;
    else if (type === "Succeeded") to = terminal("__tree_succeeded", "Tree succeeded");
    else if (type === "Failed") to = terminal("__tree_failed", "Tree failed");
    if (!to || !known.has(to)) continue;
    const label = transitionLabel(t);
    edges.push({ from, to, kind: "transition", ...(label ? { label } : {}) });
  }

  const notes: string[] = [];
  const evaluators = arr(a.evaluators).map(structLabel).filter(Boolean);
  const globalTasks = arr(a.globalTasks).map(structLabel).filter(Boolean);
  if (evaluators.length) notes.push(`Evaluators: ${evaluators.join(", ")}.`);
  if (globalTasks.length) notes.push(`Global tasks: ${globalTasks.join(", ")}.`);
  const first = roots[0] ? idOf(roots[0]) : undefined;
  return {
    kind: "state",
    title: `${assetName(str(a.assetPath)) || "StateTree"} · StateTree`,
    direction: "TB",
    nodes,
    edges,
    groups,
    ...(first ? { initial: [first] } : {}),
    notes,
  };
}
