import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";
import { Vec3 } from "../schemas.js";
import { PAGINATION_SCHEMA, paged } from "../pagination.js";
import { actions as epicActions, schema as epicSchema } from "./epic/pcg.generated.js";

export const pcgTool: ToolDef = categoryTool(
  "pcg",
  "Procedural Content Generation: graphs, nodes, connections, execution, volumes.",
  {
    list_graphs:          bp("read", paged("List PCG graphs, sorted by object path. Lists the whole project; page through it with cursor/limit. Params: none"), "list_pcg_graphs"),
    read_graph:           bp("read", "Read graph structure. Params: assetPath", "read_pcg_graph"),
    read_node_settings:   bp("read", "Read node settings. Params: assetPath, nodeName", "read_pcg_node_settings"),
    get_components:       bp("read", "List PCG components in level. Params: none", "get_pcg_components"),
    get_component_details: bp("read", "Inspect PCG component. Params: actorLabel OR actorPath (#983)", "get_pcg_component_details"),
    create_graph:         bp("mutate", "Create graph. Idempotent by path: an existing graph is reported rather than replaced. Params: name, packagePath? (default /Game/PCG), onConflict? (skip|error)", "create_pcg_graph"),
    add_node:             bp("mutate", "Add node. nodeName is a RESULT, not an input: the engine assigns the name and this action reports it back for connect_nodes and remove_node. Params: assetPath, nodeType, posX?, posY?", "add_pcg_node"),
    connect_nodes:        bp("mutate", "Wire nodes. Params: assetPath, sourceNode, sourcePin, targetNode, targetPin. Returns edgeVerified=true after confirming the UPCGEdge persisted; surfaces an error if AddEdge succeeded but no edge object was instantiated (#304).", "connect_pcg_nodes"),
    disconnect_nodes:     bp("mutate", "Remove a wired edge between two PCG nodes. Params: assetPath, sourceNode, targetNode, sourcePin? (default: any), targetPin? (default: any). Returns removedEdges count (#346).", "disconnect_pcg_nodes", (p) => ({ assetPath: p.assetPath, sourceNode: p.sourceNode, targetNode: p.targetNode, sourcePin: p.sourcePin, targetPin: p.targetPin })),
    set_node_settings:    bp("mutate", "Set node params. Pass a settings object of {propertyPath: value} (dotted paths and nested structs supported), or propertyName + propertyValue for a single write. Reports previousProperties and rolls back to them. Params: assetPath, nodeName, settings OR propertyName+propertyValue", "set_pcg_node_settings"),
    set_static_mesh_spawner_meshes: bp("mutate", "Populate weighted MeshEntries on a PCGStaticMeshSpawner node (#145). Params: assetPath, nodeName, entries=[{mesh, weight?}], replace? (default true)", "set_static_mesh_spawner_meshes", (p) => ({ assetPath: p.assetPath, nodeName: p.nodeName, entries: p.entries, replace: p.replace })),
    remove_node:          bp("mutate", "Remove node. Params: assetPath, nodeName", "remove_pcg_node"),
    execute:              bp("mutate", "Regenerate PCG. Params: actorLabel OR actorPath, seed? (writes the component Seed before generating) (#983)", "execute_pcg_graph"),
    force_regenerate:     bp("mutate", "Force a stuck PCG component to regenerate (clears graph ref, re-sets, cleanup+generate). Params: actorLabel OR actorPath (#146/#983)", "force_regenerate_pcg"),
    cleanup:              bp("mutate", "Cleanup a PCG component (remove spawned content). Params: actorLabel OR actorPath, removeComponents? (default true) (#146)", "cleanup_pcg", (p) => ({ actorLabel: p.actorLabel, actorPath: p.actorPath, removeComponents: p.removeComponents })),
    toggle_graph:         bp("mutate", "Toggle a PCG component's graph assignment to force reinit (no generate). Params: actorLabel OR actorPath, graphPath? (#146)", "toggle_pcg_graph", (p) => ({ actorLabel: p.actorLabel, actorPath: p.actorPath, graphPath: p.graphPath })),
    add_volume:           bp("mutate", "Place PCG volume. Idempotent by editor label when one is given. Params: graphPath, location?, extent?, label?, onConflict? (skip|error)", "add_pcg_volume"),
    import_graph:         bp("mutate", "Bulk-author a PCG graph from JSON. Params: assetPath, nodes=[{name,class,posX?,posY?,settings?}], connections=[{from,fromPin?,to,toPin?}], replace? (default false). One call replaces N add_node + M connect_nodes + K set_node_settings (#213).", "import_pcg_graph", (p) => ({ assetPath: p.assetPath, nodes: p.nodes, connections: p.connections, replace: p.replace })),
    export_graph:         bp("read", "Export a PCG graph as JSON. Params: assetPath, includeSettings? (default true). Round-trip safe with import_graph (#213).", "export_pcg_graph", (p) => ({ assetPath: p.assetPath, includeSettings: p.includeSettings })),
    ...epicActions,
  },
  undefined,
  {
    ...epicSchema,
    assetPath: z.string().optional(), actorLabel: z.string().optional(),
    actorPath: z.string().optional().describe("Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given. Editor labels are NOT unique, and a label matching several actors is refused with the candidates rather than resolved at random (#983)"),
    name: z.string().optional(), packagePath: z.string().optional(),
    nodeType: z.string().optional(), nodeName: z.string().optional(),
    // Each of the seven below is read by name in the C++ and was undeclared here,
    // so the MCP layer stripped it before the bridge saw it and the handler
    // reported success for the part of the call that never happened: a node
    // placed at the origin, a single-property write refused as "missing
    // settings", a seed that never reached the component, and a volume that
    // spawned a duplicate instead of reporting the one already there.
    posX: z.number().optional().describe("add_node: graph editor X position for the new node"),
    posY: z.number().optional().describe("add_node: graph editor Y position for the new node"),
    propertyName: z.string().optional().describe("set_node_settings: write ONE property instead of passing a settings object. Pair with propertyValue"),
    propertyValue: z.string().optional().describe("set_node_settings: the value for propertyName, as UE export text"),
    seed: z.number().optional().describe("execute: write the component's Seed before generating. Reported back as previousSeed so the generation can be reproduced"),
    label: z.string().optional().describe("add_volume: editor label for the spawned PCG volume. Also the idempotency key - an existing actor with this label is reported rather than duplicated"),
    onConflict: z.string().optional().describe("create_graph / add_volume: skip (default, report the existing one) | error"),
    sourceNode: z.string().optional(), sourcePin: z.string().optional(),
    targetNode: z.string().optional(), targetPin: z.string().optional(),
    settings: z.record(z.unknown()).optional(),
    entries: z.array(z.record(z.unknown())).optional().describe("Array of {mesh, weight?} entries for set_static_mesh_spawner_meshes"),
    replace: z.boolean().optional().describe("set_static_mesh_spawner_meshes: overwrite existing MeshEntries (default true). import_graph: wipe existing user nodes first (default false)."),
    graphPath: z.string().optional(),
    location: Vec3.optional(),
    extent: Vec3.optional(),
    removeComponents: z.boolean().optional().describe("cleanup: remove managed spawned components (default true)"),
    nodes: z.array(z.record(z.unknown())).optional().describe("import_graph: [{name, class, posX?, posY?, settings?}]"),
    connections: z.array(z.record(z.unknown())).optional().describe("import_graph: [{from, fromPin?, to, toPin?}]"),
    includeSettings: z.boolean().optional().describe("export_graph: include per-node editable settings in the response (default true)"),
    // cursor + limit for the paged list actions. Declared once: the MCP layer
    // strips a key the category never declares, so a paged action whose
    // category omits these silently returns page one forever.
    ...PAGINATION_SCHEMA,
  },
);
