// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd project handler. */
export const handlerSpecs: HandlerSpecs = {
  "create_cpp_class": {
    "category": "project",
    "params": [
      {
        "name": "className",
        "type": "string",
        "required": true,
        "description": "Class name without its prefix; the parent decides A or U"
      },
      {
        "name": "parentClass",
        "type": "string",
        "required": false,
        "description": "Parent class as a short name (Actor) or /Script/<Module>.<Class> path (default UObject)"
      },
      {
        "name": "moduleName",
        "type": "string",
        "required": false,
        "description": "Project module to add it to (default the first; list_project_modules names them)"
      },
      {
        "name": "classDomain",
        "type": "string",
        "required": false,
        "description": "public | private | classes (default public)"
      },
      {
        "name": "subPath",
        "type": "string",
        "required": false,
        "description": "Folder under the domain folder, e.g. Gameplay/Abilities (default its root)"
      }
    ]
  },
  "disable_plugin": {
    "category": "project",
    "params": [
      {
        "name": "pluginName",
        "type": "string",
        "required": true,
        "description": "Plugin name as its .uplugin spells it, any case"
      },
      {
        "name": "removeReference",
        "type": "boolean",
        "required": false,
        "description": "Delete the .uproject entry instead of writing an explicit disable (default false)"
      }
    ]
  },
  "enable_plugin": {
    "category": "project",
    "params": [
      {
        "name": "pluginName",
        "type": "string",
        "required": true,
        "description": "Plugin name as its .uplugin spells it, any case"
      }
    ]
  },
  "generate_project_files": {
    "category": "project",
    "params": [],
    "contractExempt": "runs the project file generator"
  },
  "list_available_plugins": {
    "category": "project",
    "params": [
      {
        "name": "filter",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring of the plugin name or friendly name"
      },
      {
        "name": "pluginCategory",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring of the plugin category"
      },
      {
        "name": "enabledOnly",
        "type": "boolean",
        "required": false,
        "description": "Only plugins enabled in this editor session (default false)"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "number",
        "required": false,
        "description": "Rows per page, a whole number (default 200, max 2000)"
      }
    ]
  },
  "list_project_modules": {
    "category": "project",
    "params": [
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "number",
        "required": false,
        "description": "Rows per page, a whole number (default 200, max 2000)"
      }
    ]
  },
  "live_coding_compile": {
    "category": "project",
    "params": [
      {
        "name": "wait",
        "type": "boolean",
        "required": false,
        "description": "Block until the compile finishes (default false returns in_progress)"
      }
    ],
    "contractExempt": "Starts a Live Coding compile of the running editor whatever it is sent"
  },
  "live_coding_status": {
    "category": "project",
    "params": []
  },
  "set_config": {
    "category": "project",
    "params": [
      {
        "name": "configName",
        "type": "string",
        "required": false,
        "description": "Config to write: Engine, Game, Input... or a file name ending .ini (default DefaultEngine.ini)",
        "aliases": [
          "configFile"
        ]
      },
      {
        "name": "section",
        "type": "string",
        "required": true,
        "description": "INI section"
      },
      {
        "name": "key",
        "type": "string",
        "required": true,
        "description": "INI key"
      },
      {
        "name": "value",
        "type": "string",
        "required": true,
        "description": "INI value"
      }
    ],
    "contractExempt": "writes an INI file under the project's Config folder"
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  create_cpp_class: "Params: className, parentClass?, moduleName?, classDomain?, subPath?",
  disable_plugin: "Params: pluginName, removeReference?",
  enable_plugin: "Params: pluginName",
  generate_project_files: "Params: none",
  list_available_plugins: "Params: filter?, pluginCategory?, enabledOnly?, cursor?, limit?",
  list_project_modules: "Params: cursor?, limit?",
  live_coding_compile: "Params: wait?",
  live_coding_status: "Params: none",
  set_config: "Params: configName? (or configFile), section, key, value",
};

/** Every key the spec'd project handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  classDomain: z.string().optional().describe("public | private | classes (default public)"),
  className: z.string().optional().describe("Class name without its prefix; the parent decides A or U"),
  configFile: z.string().optional().describe("Alias for configName"),
  configName: z.string().optional().describe("Config to write: Engine, Game, Input... or a file name ending .ini (default DefaultEngine.ini)"),
  cursor: z.string().optional().describe("Resume a paged read: the nextCursor from the previous page, unmodified"),
  enabledOnly: z.boolean().optional().describe("Only plugins enabled in this editor session (default false)"),
  filter: z.string().optional().describe("Case-insensitive substring of the plugin name or friendly name"),
  key: z.string().optional().describe("INI key"),
  limit: z.number().optional().describe("Rows per page, a whole number (default 200, max 2000)"),
  moduleName: z.string().optional().describe("Project module to add it to (default the first; list_project_modules names them)"),
  parentClass: z.string().optional().describe("Parent class as a short name (Actor) or /Script/<Module>.<Class> path (default UObject)"),
  pluginCategory: z.string().optional().describe("Case-insensitive substring of the plugin category"),
  pluginName: z.string().optional().describe("Plugin name as its .uplugin spells it, any case"),
  removeReference: z.boolean().optional().describe("Delete the .uproject entry instead of writing an explicit disable (default false)"),
  section: z.string().optional().describe("INI section"),
  subPath: z.string().optional().describe("Folder under the domain folder, e.g. Gameplay/Abilities (default its root)"),
  value: z.string().optional().describe("INI value"),
  wait: z.boolean().optional().describe("Block until the compile finishes (default false returns in_progress)"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
