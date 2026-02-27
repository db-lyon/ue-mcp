import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const widgetTool: ToolDef = categoryTool(
  "widget",
  "UMG Widget Blueprints, Editor Utility Widgets, and Editor Utility Blueprints.",
  {
    read_tree:         bp("read_widget_tree"),
    get_details:       bp("get_widget_details"),
    set_property:      bp("set_widget_property"),
    list:              bp("list_widget_blueprints"),
    read_animations:   bp("read_widget_animations"),
    create:            bp("create_widget_blueprint"),
    create_utility_widget:    bp("create_editor_utility_widget"),
    run_utility_widget:       bp("run_editor_utility_widget"),
    create_utility_blueprint: bp("create_editor_utility_blueprint"),
    run_utility_blueprint:    bp("run_editor_utility_blueprint"),
  },
  `- read_tree: Read widget hierarchy. Params: assetPath
- get_details: Inspect widget. Params: assetPath, widgetName
- set_property: Set widget property. Params: assetPath, widgetName, propertyName, value
- list: List Widget BPs. Params: directory?, recursive?
- read_animations: Read UMG animations. Params: assetPath
- create: Create Widget BP. Params: name, packagePath?, parentClass?
- create_utility_widget: Create editor panel. Params: name, packagePath?
- run_utility_widget: Open editor panel. Params: assetPath
- create_utility_blueprint: Create editor script. Params: name, packagePath?
- run_utility_blueprint: Run editor script. Params: assetPath`,
  {
    assetPath: z.string().optional(),
    widgetName: z.string().optional(),
    propertyName: z.string().optional(),
    value: z.unknown().optional(),
    directory: z.string().optional(),
    recursive: z.boolean().optional(),
    name: z.string().optional(),
    packagePath: z.string().optional(),
    parentClass: z.string().optional(),
  },
);
