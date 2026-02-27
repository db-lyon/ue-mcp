import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const pcgTool: ToolDef = categoryTool(
  "pcg",
  "Procedural Content Generation: graphs, nodes, connections, execution, volumes.",
  {
    list_graphs:          bp("list_pcg_graphs"),
    read_graph:           bp("read_pcg_graph"),
    read_node_settings:   bp("read_pcg_node_settings"),
    get_components:       bp("get_pcg_components"),
    get_component_details: bp("get_pcg_component_details"),
    create_graph:         bp("create_pcg_graph"),
    add_node:             bp("add_pcg_node"),
    connect_nodes:        bp("connect_pcg_nodes"),
    set_node_settings:    bp("set_pcg_node_settings"),
    remove_node:          bp("remove_pcg_node"),
    execute:              bp("execute_pcg_graph"),
    add_volume:           bp("add_pcg_volume"),
  },
  `- list_graphs: List PCG graphs. Params: directory?, recursive?
- read_graph: Read graph structure. Params: assetPath
- read_node_settings: Read node settings. Params: assetPath, nodeName
- get_components: List PCG components in level
- get_component_details: Inspect component. Params: actorLabel
- create_graph: Create graph. Params: name, packagePath?
- add_node: Add node. Params: assetPath, nodeType, nodeName?
- connect_nodes: Wire nodes. Params: assetPath, sourceNode, sourcePin, targetNode, targetPin
- set_node_settings: Set node params. Params: assetPath, nodeName, settings
- remove_node: Remove node. Params: assetPath, nodeName
- execute: Regenerate PCG. Params: actorLabel
- add_volume: Place PCG volume. Params: graphPath, location?, extent?`,
  {
    assetPath: z.string().optional(), actorLabel: z.string().optional(),
    directory: z.string().optional(), recursive: z.boolean().optional(),
    name: z.string().optional(), packagePath: z.string().optional(),
    nodeType: z.string().optional(), nodeName: z.string().optional(),
    sourceNode: z.string().optional(), sourcePin: z.string().optional(),
    targetNode: z.string().optional(), targetPin: z.string().optional(),
    settings: z.record(z.unknown()).optional(),
    graphPath: z.string().optional(),
    location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
    extent: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
  },
);
