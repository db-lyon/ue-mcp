import { McpError, ErrorCode } from "../core/errors.js";
import { coerceAssetPathValue, normalizeUnrealAssetPath, splitAssetPath, PATH_FORMAT_HELP } from "../surface/asset-path.js";

/**
 * One parameter contract for the whole `widget` category (#798).
 *
 * The canonical names are `assetPath` (a Widget Blueprint or Editor Utility
 * package path), `widgetName` and `parentWidgetName`, and `input` for the
 * arguments of an `epic_*` action. The older spellings (`path`,
 * `widgetBlueprintPath`, `widgetDisplayName`, `parentWidget`) are aliases in
 * each handler's C++ spec, which the registry renames before the handler runs.
 *
 * What stays here is what an alias cannot say: the asset path's value repair,
 * the create actions' `name` + `packagePath` spelling, and `widgetBlueprint`,
 * the engine tools' own name for the asset, which also takes a `{refPath}`
 * object a string alias cannot accept.
 */

/** Spellings of `assetPath` the C++ specs declare, in priority order. */
const ASSET_PATH_KEYS = ["assetPath", "path", "widgetBlueprintPath"] as const;

/** Each group is one parameter, so the editor reading any member is enough. */
export const WIDGET_PARAM_GROUPS: readonly (readonly string[])[] = [
  [...ASSET_PATH_KEYS, "widgetBlueprint", "name", "packagePath"],
  ["widgetName", "widgetDisplayName"],
  ["parentWidgetName", "parentWidget"],
];

/**
 * Actions that also take the older asset name plus package path spelling.
 * Their handlers accept either (assetPath wins); this list is what lets the
 * normalizer compose the older spelling into assetPath, and refuse a name
 * that contradicts it.
 */
const NAME_AND_PACKAGE_ACTIONS = new Set([
  "create",
  "create_utility_widget",
  "create_utility_blueprint",
]);

function invalid(message: string): McpError {
  return new McpError(ErrorCode.INVALID_PARAMS, message);
}

function firstDefined(
  params: Record<string, unknown>,
  keys: readonly string[],
): { key: string; value: unknown } | undefined {
  for (const key of keys) {
    const value = params[key];
    if (value !== undefined && value !== null && value !== "") return { key, value };
  }
  return undefined;
}

function repairedAssetPath(key: string, value: unknown): string {
  const raw = coerceAssetPathValue(value);
  if (raw === undefined) {
    throw invalid(`${key} could not be read as an asset path. ${PATH_FORMAT_HELP}`);
  }
  return normalizeUnrealAssetPath(raw, key);
}

/**
 * Normalize one `widget` call. Runs for every action in the category, native
 * and injected alike, before the action's own parameter mapping.
 */
export function normalizeWidgetParams(params: Record<string, unknown>): Record<string, unknown> {
  const out = { ...params };
  const action = typeof params.action === "string" ? params.action : "";
  const splitsAssetPath = NAME_AND_PACKAGE_ACTIONS.has(action);
  const wrapsEngineTool = action.startsWith("epic_");

  // ── Asset path ──────────────────────────────────────────────────────────
  // Repaired under the name it was sent as; the registry resolves the alias.
  let assetPath: string | undefined;
  const given = firstDefined(out, ASSET_PATH_KEYS);
  if (given) {
    assetPath = repairedAssetPath(given.key, given.value);
    out[given.key] = assetPath;
  } else if (!wrapsEngineTool && firstDefined(out, ["widgetBlueprint"])) {
    // An engine tool reads widgetBlueprint itself; a native handler reads assetPath.
    assetPath = repairedAssetPath("widgetBlueprint", out.widgetBlueprint);
    delete out.widgetBlueprint;
    out.assetPath = assetPath;
  } else if (splitsAssetPath
    && typeof out.name === "string" && out.name.trim() !== ""
    && typeof out.packagePath === "string" && out.packagePath.trim() !== "") {
    // The pre-#798 create spelling, composed so the create actions answer
    // to `assetPath` like the rest of the category.
    assetPath = normalizeUnrealAssetPath(
      `${(out.packagePath as string).trim()}/${(out.name as string).trim()}`,
      "packagePath + name",
    );
    out.assetPath = assetPath;
  }
  // An engine tool fills its asset reference from assetPath, and no registry
  // alias runs for it.
  if (wrapsEngineTool && assetPath !== undefined) out.assetPath = assetPath;

  if (assetPath !== undefined && splitsAssetPath) {
    const split = splitAssetPath(assetPath);
    const givenName = typeof out.name === "string" ? out.name.trim() : "";
    if (givenName !== "" && givenName !== split.name) {
      throw invalid(
        `Conflicting parameters: assetPath '${assetPath}' names the asset '${split.name}', but name is '${givenName}'. ` +
        "assetPath already carries the asset name. Pass assetPath alone, or pass name together with packagePath.",
      );
    }
    out.name = split.name;
    out.packagePath = split.packagePath;
  } else if (splitsAssetPath && typeof out.name === "string" && out.name.trim() !== "") {
    // A bare name with no packagePath is still valid: the bridge supplies the
    // default package. Only reject a name that cannot be an asset name at all,
    // which is otherwise reported by the editor as a generic creation failure.
    const name = out.name.trim();
    if (name.includes("/") || name.includes(".")) {
      throw invalid(
        `name '${out.name as string}' is not a bare asset name. ` +
        "Pass the full location as assetPath (for example '/Game/UI/WBP_Example'), or a name without '/' or '.' plus packagePath.",
      );
    }
    out.name = name;
  }

  return out;
}
