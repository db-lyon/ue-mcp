/**
 * Graph diagrams: each reader's answer (fixtures shaped as the C++ handlers
 * write them) through its adapter, the focus and cap, the Mermaid text and the
 * SVG drawing, and the diagram actions over a stub bridge.
 *
 * The Mermaid text is compared with committed files under
 * fixtures/diagrams/__expected__; the SVG is compared by structure (which
 * nodes, groups, edges and labels it draws), not by coordinates, so a layout
 * change in dagre does not read as a regression.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import type { ToolContext } from "../../src/core/types.js";
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
} from "../../src/diagram/adapters.js";
import { finishDiagram } from "../../src/diagram/action.js";
import type { DiagramModel } from "../../src/diagram/model.js";
import { escapeMermaidText, mermaidIds, toMermaid } from "../../src/diagram/mermaid.js";
import { setResvgLoader } from "../../src/diagram/png.js";
import { selectView, TRUNCATED_NODE_ID } from "../../src/diagram/select.js";
import { escapeXml, toSvg } from "../../src/diagram/svg.js";
import {
  animationDiagramAction,
  blueprintDiagramAction,
  materialDiagramAction,
  MAX_RULE_READS,
} from "../../src/tools/graph-diagrams.js";
import { ALL_TOOLS } from "../../src/tools.js";

const FIXTURES = path.join(__dirname, "fixtures", "diagrams");
const fixture = (name: string): Record<string, unknown> => JSON.parse(fs.readFileSync(path.join(FIXTURES, `${name}.json`), "utf8"));

const RULE_GUID = "6A1F2C3D-4B5E-4F60-8172-93A4B5C6D7E8";

const MODELS: Record<string, () => DiagramModel> = {
  blueprint: () => blueprintGraphModel(fixture("blueprint-summary"), {
    subgraphs: [{ name: "Open Door", summary: fixture("blueprint-collapsed"), kind: "collapsed" }],
  }),
  "state-machine": () => stateMachineModel(fixture("state-machine"), new Map([[RULE_GUID, ruleExpression(fixture("transition-rule"))!]])),
  statetree: () => stateTreeModel(fixture("statetree")),
  "behavior-tree": () => behaviorTreeModel(fixture("behavior-tree")),
  material: () => materialModel([fixture("material")]),
  pcg: () => pcgModel(fixture("pcg")),
  "asset-graph": () => assetGraphModel(fixture("asset-graph")),
  "control-rig": () => controlRigModel(fixture("control-rig")),
};

/** What an SVG draws, without where. */
function svgStructure(svg: string) {
  const attrs = (name: string) => [...svg.matchAll(new RegExp(`${name}="([^"]*)"`, "g"))].map((m) => m[1]);
  return {
    nodes: attrs("data-node"),
    roles: attrs("data-role"),
    groups: attrs("data-group"),
    starts: attrs("data-start"),
    edgePaths: (svg.match(/marker-end=/g) ?? []).length,
    texts: [...svg.matchAll(/<text[^>]*>([^<]*)<\/text>/g)].map((m) => m[1]),
  };
}

afterEach(() => setResvgLoader(undefined));

describe("adapters and Mermaid, per graph kind", () => {
  for (const [name, build] of Object.entries(MODELS)) {
    it(`${name}: Mermaid text matches the committed diagram`, async () => {
      const text = toMermaid(selectView(build()));
      await expect(text).toMatchFileSnapshot(path.join(FIXTURES, "__expected__", `${name}.mmd`));
    });

    it(`${name}: the SVG draws every node, group and edge of the model`, () => {
      const view = selectView(build());
      const { svg, width, height } = toSvg(view);
      expect(svg.startsWith("<svg xmlns=\"http://www.w3.org/2000/svg\"")).toBe(true);
      expect(svg.trimEnd().endsWith("</svg>")).toBe(true);
      expect(svg).not.toMatch(/NaN|undefined/);
      expect(width).toBeGreaterThan(0);
      expect(height).toBeGreaterThan(0);
      const s = svgStructure(svg);
      expect(s.nodes).toEqual(view.nodes.map((n) => escapeXml(n.id)));
      expect(s.roles).toEqual(view.nodes.map((n) => n.role));
      expect(s.groups.sort()).toEqual(view.groups.map((g) => escapeXml(g.id)).sort());
      const anchoredEntries = view.groups.filter((g) => g.initial && view.nodes.some((n) => n.anchorOf === g.id)).length;
      expect(s.edgePaths).toBe(view.edges.length + s.starts.length + anchoredEntries);
      expect(s.texts[0]).toBe(escapeXml(view.title));
      for (const e of view.edges) if (e.label) expect(s.texts).toContain(escapeXml(e.label.length > 40 ? e.label.slice(0, 39).trimEnd() + "…" : e.label));
    });
  }
});

describe("what each adapter reads", () => {
  it("blueprint: exec and data wires, roles from the node class, the collapsed graph as a subgraph", () => {
    const m = MODELS.blueprint();
    const byLabel = new Map(m.nodes.map((n) => [n.label, n]));
    expect(byLabel.get("Event BeginPlay")?.role).toBe("entry");
    expect(byLabel.get("Branch")?.role).toBe("branch");
    expect(byLabel.get("Open Door")?.role).toBe("collapsed");
    expect(byLabel.get("Outputs")?.role).toBe("exit");
    expect(byLabel.get("Play Sound 2D")?.label).toBe("Play Sound 2D");
    expect(m.edges.filter((e) => e.kind === "exec")).toHaveLength(7);
    expect(m.edges.filter((e) => e.kind === "data").map((e) => e.label)).toEqual(["IsLocked → Condition", "IsLocked → InString"]);
    expect(m.groups).toEqual([{ id: "graph:Open Door", label: "Collapsed graph: Open Door" }]);
    expect(m.edges).toContainEqual({ from: "Mm1bY2xW3vU4tS5rQ6pO7w", to: "Tn1aB2cD3eF4gH5iJ6kL7w", kind: "child", label: "expands" });
    expect(blueprintGraphModel(fixture("blueprint-summary"), { edges: "exec" }).edges.every((e) => e.kind === "exec")).toBe(true);
  });

  it("a transition rule graph becomes one expression, literals included", () => {
    expect(ruleExpression(fixture("transition-rule"))).toBe("Speed > 10.0");
    expect(ruleExpression({ nodes: [{ id: "r", class: "AnimGraphNode_TransitionResult", pins: [{ name: "bCanEnterTransition", direction: "Input", defaultValue: "true", linkedTo: [] }] }] })).toBe("always");
    expect(ruleExpression({ nodes: [] })).toBeUndefined();
  });

  it("state machine: transition labels carry rule, blend and inertialization", () => {
    const labels = MODELS["state-machine"]().edges.map((e) => e.label);
    expect(labels).toEqual(["Speed > 10.0 · 0.2s", "0.25s", "0.1s · inertial", undefined]);
  });

  it("StateTree: parents are groups entered through their first child, terminal transitions end at the tree's outcome", () => {
    const m = MODELS.statetree();
    expect(m.initial).toEqual(["00000001-0000-0000-0000-000000000000"]);
    expect(m.groups.map((g) => [g.label, g.initial])).toEqual([
      ["Root", "00000002-0000-0000-0000-000000000000"],
      ["Combat", "00000004-0000-0000-0000-000000000000"],
    ]);
    const label = (from: string, to: string) => m.edges.find((e) => e.from === from && e.to === to)?.label;
    expect(label("00000002-0000-0000-0000-000000000000", "00000003-0000-0000-0000-000000000000")).toBe("event · AI.Alert");
    expect(m.edges.some((e) => e.label === "completed · after 1.5s")).toBe(true);
    expect(label("00000003-0000-0000-0000-000000000000", "00000002-0000-0000-0000-000000000000")).toBe("tick · if CompareFloat or CompareBool");
    expect(label("00000004-0000-0000-0000-000000000000", "__tree_failed")).toBe("failed");
    expect(label("00000004-0000-0000-0000-000000000000", "00000005-0000-0000-0000-000000000000")).toBe("succeeded");
    expect(m.notes).toEqual(["Evaluators: ObjectPropertyEvaluator.", "Global tasks: STT_Perception."]);
  });

  it("behavior tree: ROOT, numbered children, decorators and services on their node", () => {
    const m = MODELS["behavior-tree"]();
    expect(m.nodes[0]).toMatchObject({ id: "ROOT", role: "entry", detail: "blackboard: BB_Enemy" });
    expect(m.nodes.find((n) => n.id === "Root.Children[0]")?.detail).toBe("if: Has Target");
    expect(m.nodes.find((n) => n.id === "Root")?.detail).toBe("service: Set Default Focus");
    expect(m.edges.filter((e) => e.from === "Root").map((e) => e.label)).toEqual(["1", "2"]);
  });

  it("material: wires labelled with the input they feed, root inputs into the output node", () => {
    const m = MODELS.material();
    expect(m.edges).toContainEqual({ from: "expr:0", to: "expr:3", kind: "data", label: "out 4 → B" });
    expect(m.edges.filter((e) => e.to === "material").map((e) => e.label)).toEqual(["BaseColor", "Roughness"]);
    expect(m.nodes.find((n) => n.id === "expr:1")?.role).toBe("parameter");
  });

  it("PCG, asset graph and control rig: default pins unlabelled, named ones labelled", () => {
    expect(MODELS.pcg().edges.map((e) => e.label)).toEqual(["Landscape → Surface", undefined, undefined, undefined]);
    const asset = MODELS["asset-graph"]();
    expect(asset.groups.map((g) => g.label)).toEqual(["Source", "Macro"]);
    expect(asset.edges).toEqual([{ from: "B0000000-0000-0000-0000-000000000002", to: "B0000000-0000-0000-0000-000000000001", kind: "data", label: "Component → Components" }]);
    const rig = MODELS["control-rig"]();
    expect(rig.edges.map((e) => [e.kind, e.label])).toEqual([["exec", undefined], ["data", "Transform"], ["data", "Transform.Translation → Value.Translation"]]);
  });
});

describe("Mermaid escaping and ids", () => {
  it("writes every character Mermaid reads as syntax as an entity", () => {
    expect(escapeMermaidText(`On "Knock" <Door>; #1 | {x} & \`y\``)).toBe("On #quot;Knock#quot; #lt;Door#gt;#59; #35;1 #124; #123;x#125; #amp; #96;y#96;");
    expect(escapeMermaidText("two\nlines")).toBe("two lines");
  });

  it("derives ids from the reader's ids, stable across reads and unique when they fold together", () => {
    const model: DiagramModel = {
      kind: "flowchart", title: "t", direction: "LR", groups: [], edges: [], notes: [],
      nodes: [{ id: "a-b", label: "1", role: "default" }, { id: "a_b", label: "2", role: "default" }, { id: "end", label: "3", role: "default" }],
    };
    const ids = mermaidIds(model).node;
    expect([...ids.values()]).toEqual(["n_a_b", "n_a_b_2", "n_end"]);
    expect(toMermaid(model)).toBe(toMermaid(model));
  });
});

function chain(count: number): DiagramModel {
  const nodes = Array.from({ length: count }, (_, i) => ({ id: `N${i}`, label: `Node ${i}`, role: i === 0 ? "entry" as const : "default" as const }));
  const edges = nodes.slice(1).map((n, i) => ({ from: `N${i}`, to: n.id, kind: "exec" as const }));
  return { kind: "flowchart", title: "Chain", direction: "LR", nodes, edges, groups: [], notes: [] };
}

describe("focus, depth and the node cap", () => {
  it("caps a large graph from its entry, says what was cut, and draws a note node", () => {
    const view = selectView(chain(200), { maxNodes: 10 });
    expect(view.nodes.filter((n) => n.role !== "note").map((n) => n.id)).toEqual(Array.from({ length: 10 }, (_, i) => `N${i}`));
    expect(view.nodes.at(-1)).toMatchObject({ id: TRUNCATED_NODE_ID, role: "note", label: "190 more nodes not shown" });
    expect(view.notes.at(-1)).toMatch(/^Showing 10 of 200 nodes\. Pass focus/);
    expect(view.edges).toHaveLength(9);
    expect(toMermaid(view)).toContain('n_truncated["190 more nodes not shown"]');
    expect(toSvg(view).svg).toContain("Showing 10 of 200 nodes.");
  });

  it("the default cap is 80 and a request above 500 is held to 500", () => {
    expect(selectView(chain(100)).nodes.filter((n) => n.role !== "note")).toHaveLength(80);
    expect(selectView(chain(600), { maxNodes: 10_000 }).nodes.filter((n) => n.role !== "note")).toHaveLength(500);
  });

  it("focus centres on a node by title and keeps what is within depth either way", () => {
    const view = selectView(chain(50), { focus: "node 20", depth: 2 });
    expect(view.nodes.map((n) => n.id).sort()).toEqual(["N18", "N19", "N20", "N21", "N22"]);
    expect(view.notes[0]).toBe("Focused on 'Node 20': 5 of 50 nodes within 2 hops.");
  });

  it("an exact id wins over a substring, and an unknown focus names some nodes", () => {
    expect(selectView(chain(30), { focus: "N2", depth: 0 }).nodes.map((n) => n.id)).toEqual(["N2"]);
    expect(() => selectView(chain(3), { focus: "nope" })).toThrow(/focus 'nope' matches no node in Chain.*Node 0, Node 1, Node 2/);
  });

  it("focusing a StateTree parent keeps its children and drops groups with nothing left in them", () => {
    const view = selectView(MODELS.statetree(), { focus: "Combat", depth: 0 });
    expect(view.nodes.map((n) => n.label).sort()).toEqual(["Attack", "Combat", "Flee"]);
    expect(view.groups.map((g) => g.label)).toEqual(["Root", "Combat"]);
    const flee = selectView(MODELS.statetree(), { focus: "Tree succeeded", depth: 0 });
    expect(flee.groups).toEqual([]);
  });
});

describe("formats", () => {
  const tmp = () => fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-diagram-"));
  const ctxFor = (projectDir: string) => ({ project: { projectDir } }) as unknown as ToolContext;

  it("mermaid is the default, json returns the normalised graph", async () => {
    const dir = tmp();
    const m = await finishDiagram(ctxFor(dir), MODELS.pcg(), {});
    expect(m).toMatchObject({ format: "mermaid", diagramType: "flowchart", nodeCount: 5, edgeCount: 4, truncated: false });
    expect(String(m.mermaid)).toMatch(/^---\ntitle: "PCG_Forest · PCG graph"\n---\nflowchart LR\n/);
    const j = await finishDiagram(ctxFor(dir), MODELS["state-machine"](), { format: "json" });
    expect(j).toMatchObject({ format: "json", diagramType: "stateDiagram-v2" });
    expect((j.graph as { nodes: unknown[] }).nodes).toHaveLength(4);
    await expect(finishDiagram(ctxFor(dir), MODELS.pcg(), { format: "gif" })).rejects.toThrow(/format 'gif' is not one of json, mermaid, svg, png/);
  });

  it("svg is written under the project's Saved folder, or to outputPath", async () => {
    const dir = tmp();
    const r = await finishDiagram(ctxFor(dir), MODELS.material(), { format: "svg" });
    expect(r.path).toBe(path.join(dir, "Saved", "UE_MCP", "Diagrams", "M_Rock_Material_graph.svg"));
    expect(fs.readFileSync(String(r.path), "utf8")).toMatch(/^<svg /);
    const named = await finishDiagram(ctxFor(dir), MODELS.material(), { format: "svg", outputPath: path.join(dir, "out", "m.svg") });
    expect(named.path).toBe(path.join(dir, "out", "m.svg"));
    expect(fs.existsSync(path.join(dir, "out", "m.svg"))).toBe(true);
  });

  it("png comes back inline as image content and is written to a file", async () => {
    const dir = tmp();
    const r = await finishDiagram(ctxFor(dir), MODELS.statetree(), { format: "png" });
    const media = r.media as Array<Record<string, unknown>>;
    expect(media).toHaveLength(1);
    expect(media[0]).toMatchObject({ type: "image", mimeType: "image/png", path: r.path });
    const bytes = Buffer.from(String(media[0].data), "base64");
    expect(bytes.subarray(0, 8)).toEqual(Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]));
    expect(fs.readFileSync(String(r.path)).equals(bytes)).toBe(true);
  });

  it("png without the rasteriser installed writes the SVG and says why", async () => {
    setResvgLoader(async () => null);
    const dir = tmp();
    const r = await finishDiagram(ctxFor(dir), MODELS.pcg(), { format: "png" });
    expect(r.format).toBe("svg");
    expect(String(r.path)).toMatch(/\.svg$/);
    expect(r.pngUnavailable).toMatch(/@resvg\/resvg-js/);
    expect(r.media).toBeUndefined();
  });
});

describe("the diagram actions", () => {
  function stubCtx(answers: Record<string, (p: Record<string, unknown>) => unknown>) {
    const calls: Array<{ method: string; params: Record<string, unknown> }> = [];
    const ctx = {
      project: { projectDir: fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-diagram-")) },
      bridge: {
        call: async (method: string, params: Record<string, unknown>) => {
          calls.push({ method, params });
          const answer = answers[method];
          if (!answer) throw new Error(`unexpected ${method}`);
          return answer(params);
        },
      },
    } as unknown as ToolContext;
    return { ctx, calls };
  }

  it("are registered, read-only, on each graph category", () => {
    const where: Array<[string, string]> = [
      ["blueprint", "diagram"], ["animation", "diagram"], ["statetree", "diagram"], ["gameplay", "diagram_behavior_tree"],
      ["material", "diagram"], ["pcg", "diagram"], ["asset", "diagram"],
    ];
    for (const [category, action] of where) {
      const spec = ALL_TOOLS.find((t) => t.name === category)?.actions[action];
      expect(spec, `${category}.${action}`).toMatchObject({ kind: "handler", effect: "read" });
      expect(spec?.description).toMatch(/Params: .*format\?, focus\?, depth\?, maxNodes\?, outputPath\?$/);
    }
  });

  it("blueprint reads the summary and, with expandCollapsed, each collapsed graph", async () => {
    const { ctx, calls } = stubCtx({
      read_blueprint_graph_summary: (p) => (p.graphName === "Open Door" ? fixture("blueprint-collapsed") : fixture("blueprint-summary")),
    });
    const r = await blueprintDiagramAction.handler(ctx, { assetPath: "/Game/Blueprints/BP_Door", expandCollapsed: true });
    expect(calls).toEqual([
      { method: "read_blueprint_graph_summary", params: { assetPath: "/Game/Blueprints/BP_Door", graphName: "EventGraph" } },
      { method: "read_blueprint_graph_summary", params: { assetPath: "/Game/Blueprints/BP_Door", graphName: "Open Door" } },
    ]);
    expect(String((r as Record<string, unknown>).mermaid)).toContain('subgraph g_graph_Open_Door["Collapsed graph: Open Door"]');
  });

  it("a refusal from the reader fails the action with the reader's message", async () => {
    const { ctx } = stubCtx({ read_blueprint_graph_summary: () => ({ success: false, error: "Graph not found: Nope" }) });
    await expect(blueprintDiagramAction.handler(ctx, { assetPath: "/Game/BP", graphName: "Nope" })).rejects.toThrow("Graph not found: Nope");
    await expect(blueprintDiagramAction.handler(ctx, {})).rejects.toThrow(/assetPath is required/);
  });

  it("animation reads the state machine, then each transition's rule graph for its label", async () => {
    const { ctx, calls } = stubCtx({
      read_state_machine: () => fixture("state-machine"),
      read_blueprint_graph: (p) => (p.transitionGuid === RULE_GUID ? fixture("transition-rule") : { success: false, error: "no rule" }),
    });
    const r = await animationDiagramAction.handler(ctx, { assetPath: "/Game/Characters/ABP_Hero", stateMachineName: "Locomotion" }) as Record<string, unknown>;
    expect(calls.filter((c) => c.method === "read_blueprint_graph")).toHaveLength(4);
    expect(calls[1].params).toMatchObject({ transitionGuid: RULE_GUID, includePins: true, includeLinks: true, includeDefaults: true });
    expect(r.diagramType).toBe("stateDiagram-v2");
    expect(String(r.mermaid)).toContain("n_Idle --> n_Walk : Speed #gt; 10.0 · 0.2s");

    const { ctx: quiet, calls: quietCalls } = stubCtx({ read_state_machine: () => fixture("state-machine") });
    await animationDiagramAction.handler(quiet, { assetPath: "/Game/Characters/ABP_Hero", stateMachineName: "Locomotion", conditions: false });
    expect(quietCalls.map((c) => c.method)).toEqual(["read_state_machine"]);
    expect(MAX_RULE_READS).toBe(40);
  });

  it("material walks the cursor until the reader says there is no more", async () => {
    const first = { ...fixture("material"), hasMore: true, nextCursor: "c1" };
    const second = { success: true, materialPath: "/Game/Materials/M_Rock.M_Rock", hasMore: false, expressions: [{ nodeId: "4", class: "MaterialExpressionConstant", description: "0.5", inputs: [] }] };
    const { ctx, calls } = stubCtx({ read_material_graph: (p) => (p.cursor === "c1" ? second : first) });
    const r = await materialDiagramAction.handler(ctx, { assetPath: "/Game/Materials/M_Rock", format: "json" }) as { graph: { nodes: Array<{ id: string }> } };
    expect(calls.map((c) => c.params.cursor)).toEqual([undefined, "c1"]);
    expect(calls[0].params).toMatchObject({ materialPath: "/Game/Materials/M_Rock", limit: 2000 });
    expect(r.graph.nodes.map((n) => n.id)).toContain("expr:4");
  });
});
