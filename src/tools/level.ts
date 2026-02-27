import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const levelTools: ToolDef[] = [
  bt("get_world_outliner", "Get all actors in the current level, optionally filtered by class or name.", {
    classFilter: z.string().optional().describe("Filter by actor class (e.g. 'StaticMeshActor', 'PointLight')"),
    nameFilter: z.string().optional().describe("Filter by actor name/label (substring match)"),
  }),
  bt("place_actor", "Place an actor in the current level.", {
    actorClass: z.string().describe("Actor class to spawn (e.g. 'StaticMeshActor', 'PointLight')"),
    label: z.string().optional().describe("Display label in the outliner"),
    location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
    rotation: z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() }).optional(),
    scale: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
    properties: z.record(z.unknown()).optional().describe("Properties to set after spawning"),
  }),
  bt("delete_actor", "Delete an actor from the current level by name or label.", {
    actorLabel: z.string().describe("Actor label to delete"),
  }),
  bt("get_actor_details", "Get detailed information about a specific actor: components, properties, tags.", {
    actorLabel: z.string().describe("Actor label"),
  }),
  bt("move_actor", "Move/rotate/scale an actor in the level.", {
    actorLabel: z.string().describe("Actor label"),
    location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
    rotation: z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() }).optional(),
    scale: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
  }),
  bt("select_actors", "Select actors in the editor by label.", {
    actorLabels: z.array(z.string()).describe("Array of actor labels to select"),
  }),
  bt("get_selected_actors", "Get the currently selected actors in the editor.", {}),
  bt("add_component_to_actor", "Add a component to an existing actor in the level.", {
    actorLabel: z.string().describe("Actor label"),
    componentClass: z.string().describe("Component class name"),
    componentName: z.string().optional().describe("Name for the component"),
  }),
  bt("set_component_property", "Set a property on a component of an actor in the level.", {
    actorLabel: z.string().describe("Actor label"),
    componentName: z.string().describe("Component name"),
    propertyName: z.string().describe("Property name"),
    value: z.unknown().describe("Property value"),
  }),

  bt("get_current_level", "Get information about the currently loaded level.", {}),
  bt("load_level", "Load a level asset.", { levelPath: z.string().describe("Path to the level asset") }),
  bt("save_current_level", "Save the current level.", {}),
  bt("list_levels", "List level assets in a directory.", {
    directory: z.string().optional().describe("Directory to search"),
    recursive: z.boolean().optional(),
  }),
  bt("create_new_level", "Create a new level asset.", {
    levelPath: z.string().describe("Path for the new level"),
    templateLevel: z.string().optional().describe("Template level to base from"),
  }),
];
