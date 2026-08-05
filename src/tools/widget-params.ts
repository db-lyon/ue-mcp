import { McpError, ErrorCode } from "../errors.js";
import { coerceAssetPathValue, normalizeUnrealAssetPath, splitAssetPath, PATH_FORMAT_HELP } from "../asset-path.js";

/**
 * One parameter contract for the whole `widget` category (#798).
 *
 * Before this module every action invented its own spelling: some took
 * `assetPath`, some took `path`, the create actions took `name` +
 * `packagePath`, the injected `epic_*` actions wanted `widgetBlueprint`
 * nested inside `input`, and anything the shared schema did not declare was
 * stripped before dispatch. A caller could not tell from the schema which
 * spelling an action wanted, so agents probed the API until something stuck.
 *
 * The rule now, for every action in the category without exception:
 *
 *   - `assetPath` is the canonical name for a Widget Blueprint (or Editor
 *     Utility asset). It is an Unreal package path: `/Game/UI/WBP_Example`.
 *   - `widgetName` is the canonical name for a widget inside the tree.
 *   - `parentWidgetName` is the canonical name for its parent panel.
 *   - `input` is the canonical envelope for `epic_*` tool arguments, and the
 *     canonical names above are folded into it when a wrapped tool needs them.
 *
 * Legacy and engine-side spellings keep working. They are declared in the
 * tool schema (so the transport cannot strip them) and folded into the
 * canonical name here, in one place, before any per-action mapping runs.
 */

/** Legacy or engine-side spellings of the canonical `assetPath`, in priority order. */
const ASSET_PATH_ALIASES = ["assetPath", "path", "widgetBlueprintPath", "widgetBlueprint"] as const;

/** Legacy or engine-side spellings of the canonical `widgetName`. */
const WIDGET_NAME_ALIASES = ["widgetName", "widgetDisplayName"] as const;

/** Legacy or engine-side spellings of the canonical `parentWidgetName`. */
const PARENT_WIDGET_ALIASES = ["parentWidgetName", "parentWidget"] as const;

/**
 * Actions whose bridge handler is addressed by asset name plus package path
 * rather than by one asset path. They accept the canonical `assetPath` like
 * every other action; this list is what lets the normalizer split it back
 * apart for the handler, and recombine the older spelling into it.
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

/**
 * Normalize one `widget` call. Runs for every action in the category, native
 * and injected alike, before the action's own parameter mapping.
 */
export function normalizeWidgetParams(params: Record<string, unknown>): Record<string, unknown> {
  const out = { ...params };
  const action = typeof params.action === "string" ? params.action : "";
  const splitsAssetPath = NAME_AND_PACKAGE_ACTIONS.has(action);

  // ── Asset path ──────────────────────────────────────────────────────────
  const alias = firstDefined(out, ASSET_PATH_ALIASES);
  let assetPath: string | undefined;
  if (alias) {
    const raw = coerceAssetPathValue(alias.value);
    if (raw === undefined) {
      throw invalid(`${alias.key} could not be read as an asset path. ${PATH_FORMAT_HELP}`);
    }
    assetPath = normalizeUnrealAssetPath(raw, alias.key);
  } else if (splitsAssetPath
    && typeof out.name === "string" && out.name.trim() !== ""
    && typeof out.packagePath === "string" && out.packagePath.trim() !== "") {
    // The pre-#798 create spelling. Compose it into the canonical form so the
    // create actions answer to `assetPath` like the rest of the category.
    assetPath = normalizeUnrealAssetPath(
      `${(out.packagePath as string).trim()}/${(out.name as string).trim()}`,
      "packagePath + name",
    );
  }

  if (assetPath !== undefined) {
    out.assetPath = assetPath;
    // Mirrored for bridge handlers (and plugin-injected actions) that read the
    // older `path` spelling. A request carries both; `assetPath` is the
    // documented one. A `path` field in a widget response is a separate,
    // handler-defined thing and is unaffected by this alias.
    out.path = assetPath;

    if (splitsAssetPath) {
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
    }
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

  // ── Widget identity ─────────────────────────────────────────────────────
  const widget = firstDefined(out, WIDGET_NAME_ALIASES);
  if (widget && typeof widget.value === "string") out.widgetName = widget.value;

  const parent = firstDefined(out, PARENT_WIDGET_ALIASES);
  if (parent && typeof parent.value === "string") out.parentWidgetName = parent.value;

  return out;
}
