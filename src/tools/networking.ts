import { categoryTool, type ToolDef } from "../types.js";
import { specBp, schema as specSchema } from "./specs/networking.generated.js";

export const networkingTool: ToolDef = categoryTool(
  "networking",
  "Networking and replication: actor replication, property replication, net relevancy, dormancy.",
  {
    set_replicates:        specBp("mutate", "Enable or disable actor replication on a Blueprint's CDO. Reports existed=true and unchanged=true when the class already had this value. Rolls back through this same action with the previous flag, with nothing lost.", "set_replicates"),
    set_property_replicated: specBp("mutate", "Mark a Blueprint variable as replicated. replicationType is 'None' | 'Replicated' | 'RepNotify'; repNotify=true is shorthand for RepNotify and replicated=true for Replicated (#768). Reports existed=true and unchanged=true when the variable already had that type. Rolls back through this same action with the type it had; marked lossy when the variable carried a ReplicationCondition other than COND_None, because making a variable replicated resets that and this action has no parameter to restore one.", "set_property_replicated"),
    configure_net_frequency: specBp("mutate", "Set update frequency. Reports unchanged=true when both frequencies already held these values, and otherwise rolls back to the pair that was there - the record carries both regardless of which one was asked for, so an inverse cannot leave the two inconsistent.", "configure_net_update_frequency"),
    set_dormancy:          specBp("mutate", "Set net dormancy on a Blueprint's CDO: DORM_Never | DORM_Awake | DORM_DormantAll | DORM_DormantPartial | DORM_Initial. An unrecognised spelling is REFUSED and the valid five are named, where it used to leave the value alone and still report success. Reports existed=true and unchanged=true when the class already had that dormancy. Rolls back through this same action with the previous one, with nothing lost.", "set_net_dormancy"),
    set_net_load_on_client: specBp("mutate", "Control whether the actor is loaded on clients (bNetLoadOnClient). A class with no such property reports a warning and unchanged=true rather than the existed it used to claim for a write that never happened. Reports existed=true when the class already had this value, and otherwise rolls back through this same action with the previous flag.", "set_net_load_on_client"),
    set_always_relevant:   specBp("mutate", "Set bAlwaysRelevant on a Blueprint's CDO. Reports existed=true and unchanged=true when the class already had this value, and otherwise rolls back through this same action with the previous flag, with nothing lost.", "set_always_relevant"),
    set_only_relevant_to_owner: specBp("mutate", "Set bOnlyRelevantToOwner on a Blueprint's CDO. Reports existed=true and unchanged=true when the class already had this value, and otherwise rolls back through this same action with the previous flag, with nothing lost.", "set_only_relevant_to_owner"),
    configure_cull_distance: specBp("mutate", "Net cull distance. Reports unchanged=true when the value is already set, and otherwise rolls back to the previous NetCullDistanceSquared read off the CDO before the write. A class with no writable NetCullDistanceSquared reports a warning rather than a silent success.", "configure_net_cull_distance"),
    set_priority:          specBp("mutate", "Set NetPriority on a Blueprint's CDO. Reports existed=true and unchanged=true when the value is already nearly equal to the one asked for, and otherwise rolls back to the float the property held, with nothing lost.", "set_net_priority"),
    set_replicate_movement: specBp("mutate", "Set replicated movement on a Blueprint's CDO. Reports existed=true and unchanged=true when the class already had this value, and otherwise rolls back through this same action with the previous flag, with nothing lost.", "set_replicate_movement"),
    get_info:              specBp("read", "Get networking info.", "get_networking_info"),
  },
  {
    // #1057: every key the networking handlers declare, generated from their
    // C++ registrations.
    ...specSchema,
  },
);
