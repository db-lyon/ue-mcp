import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const networkingTool: ToolDef = categoryTool(
  "networking",
  "Networking and replication: actor replication, property replication, net relevancy, dormancy.",
  {
    set_replicates:        bp("set_replicates"),
    set_property_replicated: bp("set_property_replicated"),
    configure_net_frequency: bp("configure_net_update_frequency"),
    set_dormancy:          bp("set_net_dormancy"),
    set_net_load_on_client: bp("set_net_load_on_client"),
    set_always_relevant:   bp("set_always_relevant"),
    set_only_relevant_to_owner: bp("set_only_relevant_to_owner"),
    configure_cull_distance: bp("configure_net_cull_distance"),
    set_priority:          bp("set_net_priority"),
    set_replicate_movement: bp("set_replicate_movement"),
    get_info:              bp("get_networking_info"),
  },
  `- set_replicates: Enable actor replication. Params: blueprintPath, replicates?
- set_property_replicated: Mark variable as replicated. Params: blueprintPath, propertyName, replicated?, replicationCondition?, repNotify?
- configure_net_frequency: Set update frequency. Params: blueprintPath, netUpdateFrequency?, minNetUpdateFrequency?
- set_dormancy: Set net dormancy. Params: blueprintPath, dormancy (DORM_Never|DORM_Awake|DORM_DormantAll|DORM_DormantPartial|DORM_Initial)
- set_net_load_on_client: Control client loading. Params: blueprintPath, loadOnClient?
- set_always_relevant: Always network relevant. Params: blueprintPath, alwaysRelevant?
- set_only_relevant_to_owner: Only relevant to owner. Params: blueprintPath, onlyRelevantToOwner?
- configure_cull_distance: Net cull distance. Params: blueprintPath, netCullDistanceSquared?
- set_priority: Net priority. Params: blueprintPath, netPriority?
- set_replicate_movement: Replicate movement. Params: blueprintPath, replicateMovement?
- get_info: Get networking info. Params: blueprintPath`,
  {
    blueprintPath: z.string().optional(),
    propertyName: z.string().optional(),
    replicates: z.boolean().optional(),
    replicated: z.boolean().optional(),
    replicationCondition: z.string().optional(),
    repNotify: z.boolean().optional(),
    netUpdateFrequency: z.number().optional(),
    minNetUpdateFrequency: z.number().optional(),
    dormancy: z.string().optional(),
    loadOnClient: z.boolean().optional(),
    alwaysRelevant: z.boolean().optional(),
    onlyRelevantToOwner: z.boolean().optional(),
    netCullDistanceSquared: z.number().optional(),
    netPriority: z.number().optional(),
    replicateMovement: z.boolean().optional(),
  },
);
