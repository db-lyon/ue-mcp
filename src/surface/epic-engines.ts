/**
 * Which engines a wrapped Epic tool exists on. Each generated `epic_*` action
 * names the recorded engines that carry it; a call to one on a recorded engine
 * outside that list is refused before it is sent.
 */
import type { ToolDef } from "../core/types.js";
import { engineMinor, type EpicToolRef } from "./epic-input.js";
import { EPIC_CATALOG_ENGINES } from "../tools/epic/catalog-engines.js";

export { engineMinor };

/** The structured refusal, shaped like the plugin's own unsupported_engine_version answer. */
export interface EngineVersionRefusal {
  success: false;
  errorCode: "unsupported_engine_version";
  error: string;
  engineVersion: string;
  availableOn: string[];
}

/**
 * The refusal for calling `tool` on an editor running `engineVersion`, or null
 * to send it. An engine with no recorded catalog is never refused: nothing says
 * what its registry holds, so the registry answers for itself.
 */
export function epicEngineRefusal(
  task: string,
  tool: EpicToolRef | undefined,
  engineVersion: string | null | undefined,
  recorded: readonly string[] = EPIC_CATALOG_ENGINES,
): EngineVersionRefusal | null {
  // The tool's own engines and every engine a successor of it covers.
  const engines = tool?.engines
    ? [...tool.engines, ...(tool.successors ?? []).flatMap((s) => s.engines)]
    : undefined;
  const editor = engineMinor(engineVersion);
  if (!tool || !engines || !editor || !recorded.includes(editor) || engines.includes(editor)) return null;
  return {
    success: false,
    errorCode: "unsupported_engine_version",
    error:
      `${task} wraps the engine tool '${tool.name}', which the UE ${editor} toolset registry does not have, `
      + `so the call was not sent. It is available on UE ${engines.join(", ")}.`,
    engineVersion: editor,
    availableOn: [...engines],
  };
}

/** The same refusal looked up by task name ("category.action") in a tool graph. */
export function epicEngineRefusalFor(
  graph: readonly ToolDef[],
  task: string,
  engineVersion: string | null | undefined,
): EngineVersionRefusal | null {
  const dot = task.indexOf(".");
  const spec = graph.find((t) => t.name === task.slice(0, dot))?.actions[task.slice(dot + 1)];
  return spec?.kind === "bridge" ? epicEngineRefusal(task, spec.epicTool, engineVersion) : null;
}
