import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const pcgTools: ToolDef[] = [
  bt("list_pcg_graphs", "List PCG graph assets in a directory.", {
    directory: z.string().optional(), recursive: z.boolean().optional(),
  }),
  bt("read_pcg_graph", "Read a PCG graph's full structure: nodes, edges, parameters.", {
    assetPath: z.string().describe("Path to the PCG graph asset"),
  }),
  bt("read_pcg_node_settings", "Read detailed settings of a specific PCG node.", {
    assetPath: z.string(), nodeName: z.string(),
  }),
  bt("get_pcg_components", "List all PCG components in the current level.", {}),
  bt("get_pcg_component_details", "Deep inspect a specific PCG component.", {
    actorLabel: z.string(),
  }),
  bt("create_pcg_graph", "Create a new PCG graph asset.", {
    name: z.string(), packagePath: z.string().optional(),
  }),
  bt("add_pcg_node", "Add a node to a PCG graph.", {
    assetPath: z.string(), nodeType: z.string(), nodeName: z.string().optional(),
  }),
  bt("connect_pcg_nodes", "Connect two PCG nodes via their pins.", {
    assetPath: z.string(), sourceNode: z.string(), sourcePin: z.string(),
    targetNode: z.string(), targetPin: z.string(),
  }),
  bt("set_pcg_node_settings", "Set parameters on an existing PCG node (partial update).", {
    assetPath: z.string(), nodeName: z.string(), settings: z.record(z.unknown()),
  }),
  bt("remove_pcg_node", "Remove a node from a PCG graph.", {
    assetPath: z.string(), nodeName: z.string(),
  }),
  bt("execute_pcg_graph", "Trigger regeneration of a PCG component in the level.", {
    actorLabel: z.string(),
  }),
  bt("add_pcg_volume", "Place a PCG volume in the level with a graph and bounds.", {
    graphPath: z.string(),
    location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
    extent: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
  }),
];
