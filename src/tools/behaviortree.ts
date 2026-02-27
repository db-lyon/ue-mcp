import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const behaviorTreeTools: ToolDef[] = [
  bt("list_behavior_trees", "List Behavior Tree and Blackboard assets in a directory.", {
    directory: z.string().optional(), recursive: z.boolean().optional(),
  }),
  bt("get_behavior_tree_info", "Get information about a Behavior Tree: blackboard asset, keys.", {
    assetPath: z.string(),
  }),
  bt("create_blackboard", "Create a new BlackboardData asset.", {
    name: z.string(), packagePath: z.string().optional(),
  }),
  bt("create_behavior_tree", "Create a new Behavior Tree asset with optional blackboard.", {
    name: z.string(), packagePath: z.string().optional(),
    blackboardPath: z.string().optional(),
  }),
];
