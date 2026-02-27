import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const widgetTools: ToolDef[] = [
  bt("read_widget_tree", "Read the widget hierarchy of a Widget Blueprint.", { assetPath: z.string() }),
  bt("get_widget_details", "Get detailed information about a specific widget.", {
    assetPath: z.string(), widgetName: z.string(),
  }),
  bt("set_widget_property", "Set a property on a widget in a Widget Blueprint.", {
    assetPath: z.string(), widgetName: z.string(), propertyName: z.string(), value: z.unknown(),
  }),
  bt("list_widget_blueprints", "List Widget Blueprints in a directory.", {
    directory: z.string().optional(), recursive: z.boolean().optional(),
  }),
  bt("read_widget_animations", "Read all UMG animations in a Widget Blueprint with their tracks and keyframes.", {
    assetPath: z.string(),
  }),
  bt("create_widget_blueprint", "Create a new Widget Blueprint asset.", {
    name: z.string(), packagePath: z.string().optional(), parentClass: z.string().optional(),
  }),
  bt("create_editor_utility_widget", "Create an Editor Utility Widget Blueprint — a UMG panel that runs inside the editor.", {
    name: z.string(), packagePath: z.string().optional(),
  }),
  bt("run_editor_utility_widget", "Open an Editor Utility Widget as a docked tab in the editor.", {
    assetPath: z.string(),
  }),
  bt("create_editor_utility_blueprint", "Create an Editor Utility Blueprint — a headless editor automation script.", {
    name: z.string(), packagePath: z.string().optional(),
  }),
  bt("run_editor_utility_blueprint", "Execute an Editor Utility Blueprint (headless editor script).", {
    assetPath: z.string(),
  }),
];
