import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const reflectionTools: ToolDef[] = [
  bt("reflect_class",
    "Reflect a UClass from the running editor: parent chain, properties with types and defaults, " +
    "functions with signatures, class flags, and implemented interfaces.",
    {
      className: z.string().describe("Class name (e.g. 'Character', 'Actor', 'PlayerController')"),
      includeInherited: z.boolean().optional().describe("Include inherited properties/functions. Default: false"),
    }),

  bt("reflect_struct",
    "Reflect a UScriptStruct from the running editor: fields with types.",
    { structName: z.string().describe("Struct name (e.g. 'Vector', 'HitResult', or full path)") }),

  bt("reflect_enum",
    "Reflect a UEnum from the running editor: all values with display names.",
    { enumName: z.string().describe("Enum name (e.g. 'ECollisionChannel', 'EMovementMode')") }),

  bt("list_classes",
    "List classes known to the editor, optionally filtered by parent class.",
    {
      parentFilter: z.string().optional().describe("Parent class to filter by (e.g. 'Actor', 'ActorComponent')"),
      limit: z.number().optional().describe("Maximum results. Default: 100"),
    }),

  bt("list_gameplay_tags",
    "Get the full GameplayTag hierarchy from the running editor.",
    { filter: z.string().optional().describe("Filter tags by prefix") }),

  bt("create_gameplay_tag",
    "Add a new gameplay tag to the project.",
    {
      tag: z.string().describe("Tag to add (e.g. 'Combat.Damage.Fire')"),
      comment: z.string().optional().describe("Developer comment"),
    }),
];
