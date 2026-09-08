import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const networkingTool: ToolDef = categoryTool(
  "networking",
  "Networking and replication: actor replication, property replication, net relevancy, dormancy.",
  {
    set_replicates:        bp("mutate", "Enable or disable actor replication on a Blueprint's CDO. Reports existed=true and unchanged=true when the class already had this value. Rolls back through this same action with the previous flag, with nothing lost. Params: blueprintPath, replicates?", "set_replicates"),
    set_property_replicated: bp("mutate", "Mark a Blueprint variable as replicated. replicationType is 'None' | 'Replicated' | 'RepNotify'; repNotify=true is shorthand for RepNotify and replicated=true for Replicated. Params: blueprintPath, variableName (alias: propertyName), replicationType? | replicated? | repNotify? (#768). Reports existed=true and unchanged=true when the variable already had that type. Rolls back through this same action with the type it had; marked lossy when the variable carried a ReplicationCondition other than COND_None, because making a variable replicated resets that and this action has no parameter to restore one.", "set_property_replicated", (p) => ({
      blueprintPath: p.blueprintPath,
      variableName: p.variableName ?? p.propertyName,
      replicationType: p.replicationType
        ?? (p.repNotify ? "RepNotify" : p.replicated === true ? "Replicated" : p.replicated === false ? "None" : undefined),
    })),
    configure_net_frequency: bp("mutate", "Set update frequency. Reports unchanged=true when both frequencies already held these values, and otherwise rolls back to the pair that was there - the record carries both regardless of which one was asked for, so an inverse cannot leave the two inconsistent. Params: blueprintPath, netUpdateFrequency?, minNetUpdateFrequency?", "configure_net_update_frequency"),
    set_dormancy:          bp("mutate", "Set net dormancy on a Blueprint's CDO: DORM_Never | DORM_Awake | DORM_DormantAll | DORM_DormantPartial | DORM_Initial. An unrecognised spelling is REFUSED and the valid five are named, where it used to leave the value alone and still report success. Reports existed=true and unchanged=true when the class already had that dormancy. Rolls back through this same action with the previous one, with nothing lost. Params: blueprintPath, dormancy", "set_net_dormancy"),
    set_net_load_on_client: bp("mutate", "Control whether the actor is loaded on clients (bNetLoadOnClient). A class with no such property reports a warning and unchanged=true rather than the existed it used to claim for a write that never happened. Reports existed=true when the class already had this value, and otherwise rolls back through this same action with the previous flag. Params: blueprintPath, loadOnClient?", "set_net_load_on_client"),
    set_always_relevant:   bp("mutate", "Set bAlwaysRelevant on a Blueprint's CDO. Reports existed=true and unchanged=true when the class already had this value, and otherwise rolls back through this same action with the previous flag, with nothing lost. Params: blueprintPath, alwaysRelevant?", "set_always_relevant"),
    set_only_relevant_to_owner: bp("mutate", "Set bOnlyRelevantToOwner on a Blueprint's CDO. Reports existed=true and unchanged=true when the class already had this value, and otherwise rolls back through this same action with the previous flag, with nothing lost. Params: blueprintPath, onlyRelevantToOwner?", "set_only_relevant_to_owner"),
    configure_cull_distance: bp("mutate", "Net cull distance. Reports unchanged=true when the value is already set, and otherwise rolls back to the previous NetCullDistanceSquared read off the CDO before the write. A class with no writable NetCullDistanceSquared reports a warning rather than a silent success. Params: blueprintPath, netCullDistanceSquared?", "configure_net_cull_distance"),
    set_priority:          bp("mutate", "Set NetPriority on a Blueprint's CDO. Reports existed=true and unchanged=true when the value is already nearly equal to the one asked for, and otherwise rolls back to the float the property held, with nothing lost. Params: blueprintPath, netPriority?", "set_net_priority"),
    set_replicate_movement: bp("mutate", "Set replicated movement on a Blueprint's CDO. Reports existed=true and unchanged=true when the class already had this value, and otherwise rolls back through this same action with the previous flag, with nothing lost. Params: blueprintPath, replicateMovement?", "set_replicate_movement"),
    get_info:              bp("read", "Get networking info. Params: blueprintPath", "get_networking_info"),
  },
  undefined,
  {
    blueprintPath: z.string().optional(),
    propertyName: z.string().optional(),
    variableName: z.string().optional().describe("set_property_replicated: Blueprint variable name (propertyName is accepted too) (#768)"),
    replicationType: z.string().optional().describe("set_property_replicated: None | Replicated | RepNotify (#768)"),
    replicates: z.boolean().optional(),
    replicated: z.boolean().optional(),
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
