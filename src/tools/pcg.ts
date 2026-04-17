import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";
import { Vec3 } from "../schemas.js";

export const pcgTool: ToolDef = categoryTool(
  "pcg",
  "Procedural Content Generation: graphs, nodes, connections, execution, volumes.",
  {
    list_graphs:          bp("List PCG graphs. Params: directory?, recursive?", "list_pcg_graphs"),
    read_graph:           bp("Read graph structure. Params: assetPath", "read_pcg_graph"),
    read_node_settings:   bp("Read node settings. Params: assetPath, nodeName", "read_pcg_node_settings"),
    get_components:       bp("List PCG components in level", "get_pcg_components"),
    get_component_details: bp("Inspect PCG component. Params: actorLabel", "get_pcg_component_details"),
    create_graph:         bp("Create graph. Params: name, packagePath?", "create_pcg_graph"),
    add_node:             bp("Add node. Params: assetPath, nodeType, nodeName?", "add_pcg_node"),
    connect_nodes:        bp("Wire nodes. Params: assetPath, sourceNode, sourcePin, targetNode, targetPin", "connect_pcg_nodes"),
    set_node_settings:    bp("Set node params. Params: assetPath, nodeName, settings", "set_pcg_node_settings"),
    set_static_mesh_spawner_meshes: bp("Populate weighted MeshEntries on a PCGStaticMeshSpawner node (#145). Params: assetPath, nodeName, entries=[{mesh, weight?}], replace? (default true)", "set_static_mesh_spawner_meshes", (p) => ({ assetPath: p.assetPath, nodeName: p.nodeName, entries: p.entries, replace: p.replace })),
    remove_node:          bp("Remove node. Params: assetPath, nodeName", "remove_pcg_node"),
    execute:              bp("Regenerate PCG. Params: actorLabel", "execute_pcg_graph"),
    add_volume:           bp("Place PCG volume. Params: graphPath, location?, extent?", "add_pcg_volume"),
  },
  undefined,
  {
    assetPath: z.string().optional(), actorLabel: z.string().optional(),
    directory: z.string().optional(), recursive: z.boolean().optional(),
    name: z.string().optional(), packagePath: z.string().optional(),
    nodeType: z.string().optional(), nodeName: z.string().optional(),
    sourceNode: z.string().optional(), sourcePin: z.string().optional(),
    targetNode: z.string().optional(), targetPin: z.string().optional(),
    settings: z.record(z.unknown()).optional(),
    entries: z.array(z.record(z.unknown())).optional().describe("Array of {mesh, weight?} entries for set_static_mesh_spawner_meshes"),
    replace: z.boolean().optional().describe("When true (default) overwrite existing MeshEntries; when false, append"),
    graphPath: z.string().optional(),
    location: Vec3.optional(),
    extent: Vec3.optional(),
  },
);
