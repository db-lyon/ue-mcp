import { z } from "zod";
import { categoryTool, type ToolDef } from "../types.js";
import { actions as epicActions, schema as epicSchema } from "./epic/dataflow.generated.js";

/**
 * Dataflow graphs, contributed entirely by Unreal's own toolsets.
 *
 * ue-mcp ships no native C++ handlers for Dataflow, so every action here is a
 * generated declaration wrapping an engine tool. The category exists anyway,
 * declared like any other: an agent authoring a Dataflow graph should reach for
 * `dataflow`, not go rummaging in the `epic` umbrella for it.
 *
 * It used to be materialised at startup, only on an engine that shipped the
 * toolset. It is declared now, and behaves like every other category on an
 * editor that cannot serve it: the action is advertised and the call explains
 * what is missing. `landscape` is advertised with no editor attached at all.
 */
export const dataflowTool: ToolDef = categoryTool(
  "dataflow",
  "Dataflow graphs: node and pin authoring, variables, comment boxes, templates, and creation of Dataflow-compatible assets (Chaos geometry and simulation graphs). Requires UE 5.8+ with the Dataflow toolsets available.",
  { ...epicActions },
  undefined,
  { ...epicSchema },
);
