import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const editorTools: ToolDef[] = [
  bt("editor_execute", "Execute a console command in the editor.", {
    command: z.string().describe("Console command to execute"),
  }, "execute_command"),

  bt("execute_python", "Execute arbitrary Python code inside the editor's Python environment. This is the escape hatch for anything not covered by a dedicated tool.", {
    code: z.string().describe("Python code to execute"),
  }),

  bt("set_property", "Set a property value on any UObject by path.", {
    objectPath: z.string().describe("Object path"),
    propertyName: z.string().describe("Property name"),
    value: z.unknown().describe("New value"),
  }),

  bt("play_in_editor", "Start, stop, or query Play-In-Editor (PIE) state.", {
    action: z.enum(["start", "stop", "status"]).describe("PIE action"),
  }, "pie_control"),

  bt("get_runtime_value", "Get a runtime property value from an actor during PIE.", {
    actorLabel: z.string().describe("Actor label"),
    propertyName: z.string().describe("Property name to read"),
  }),

  bt("hot_reload", "Trigger a live code / hot reload of C++ modules.", {}),
  bt("undo", "Undo the last editor transaction.", {}),
  bt("redo", "Redo the last undone editor transaction.", {}),
];
