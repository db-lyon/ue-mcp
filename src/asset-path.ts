import { McpError, ErrorCode } from "./errors.js";

/**
 * Shared Unreal asset path handling (#798).
 *
 * Callers spell an asset path several defensible ways: with a `.uasset`
 * suffix, with the object suffix Unreal prints in the content browser, with
 * backslashes, or wrapped in the `{ refPath }` object reference that the
 * engine's own toolsets use. The bridge wants exactly one of those forms.
 * Repairing the rest at the boundary is cheaper than making every caller
 * discover the rule by trial and error, and a rejection that names the field
 * and shows the accepted shape is cheaper than a generic failure.
 */

/** Guidance appended to every path rejection, so one error is enough to fix the call. */
export const PATH_FORMAT_HELP =
  "Expected an Unreal package path such as '/Game/UI/WBP_Example'. " +
  "A '.uasset' suffix, an object suffix such as '.WBP_Example', and backslashes are accepted and normalized. " +
  "A filesystem path or a path that does not start with '/' is not.";

function invalid(message: string): McpError {
  return new McpError(ErrorCode.INVALID_PARAMS, message);
}

/**
 * Fold the accepted spellings of an Unreal asset path into the one form the
 * bridge wants: a long package path, no extension, no object suffix.
 *
 *   `/Game/UI/WBP_Foo.uasset`      -> `/Game/UI/WBP_Foo`
 *   `/Game/UI/WBP_Foo.WBP_Foo`     -> `/Game/UI/WBP_Foo`
 *   `/Game/UI/WBP_Foo.WBP_Foo_C`   -> `/Game/UI/WBP_Foo`
 *   `\Game\UI\WBP_Foo`             -> `/Game/UI/WBP_Foo`
 *   `/Game/UI/`                    -> rejected (no asset name)
 *
 * Rejects, naming the offending field, what cannot be repaired without
 * guessing: empty values, filesystem paths, and relative paths.
 */
export function normalizeUnrealAssetPath(raw: unknown, field = "assetPath"): string {
  if (typeof raw !== "string") {
    throw invalid(`${field} must be a string. ${PATH_FORMAT_HELP}`);
  }

  let value = raw.trim().replace(/\\/g, "/");
  if (value === "") {
    throw invalid(`${field} must not be empty. ${PATH_FORMAT_HELP}`);
  }

  if (/^[A-Za-z]:\//.test(value) || value.startsWith("file:")) {
    throw invalid(`${field} '${raw}' is a filesystem path. ${PATH_FORMAT_HELP}`);
  }

  value = value.replace(/\/{2,}/g, "/");
  while (value.length > 1 && value.endsWith("/")) value = value.slice(0, -1);

  if (!value.startsWith("/")) {
    throw invalid(`${field} '${raw}' is not a mount-rooted path. ${PATH_FORMAT_HELP}`);
  }

  // Everything from the first dot of the final segment is an extension or an
  // object suffix. The bridge addresses the package, not the object.
  const cut = value.lastIndexOf("/");
  const dir = value.slice(0, cut);
  let leaf = value.slice(cut + 1);
  const dot = leaf.indexOf(".");
  if (dot >= 0) leaf = leaf.slice(0, dot);

  if (leaf === "") {
    throw invalid(`${field} '${raw}' has no asset name. ${PATH_FORMAT_HELP}`);
  }

  const normalized = `${dir}/${leaf}`;
  if (normalized.split("/").filter(Boolean).length < 2) {
    throw invalid(`${field} '${raw}' is missing a mount point. ${PATH_FORMAT_HELP}`);
  }
  return normalized;
}

/** Split a normalized package path into the package directory and the asset name. */
export function splitAssetPath(assetPath: string): { packagePath: string; name: string } {
  const cut = assetPath.lastIndexOf("/");
  return { packagePath: assetPath.slice(0, cut), name: assetPath.slice(cut + 1) };
}

/**
 * Resolve the destination of a create action from either spelling: the
 * canonical `assetPath` (or its `path` alias), or the older `packagePath`
 * plus `name` pair. Returns the normalized package path.
 *
 * Throws INVALID_PARAMS naming both spellings when neither is usable, so the
 * caller is never told to supply an internal field name it cannot see in the
 * schema.
 */
export function resolveCreateAssetPath(params: Record<string, unknown>): string {
  for (const field of ["assetPath", "path"]) {
    const value = coerceAssetPathValue(params[field]);
    if (value !== undefined) return normalizeUnrealAssetPath(value, field);
  }

  const name = typeof params.name === "string" ? params.name.trim() : "";
  const packagePath = typeof params.packagePath === "string" ? params.packagePath.trim() : "";
  if (name !== "" && packagePath !== "") {
    return normalizeUnrealAssetPath(`${packagePath}/${name}`, "packagePath + name");
  }

  throw invalid(
    "Missing required parameter 'assetPath'. " +
    `${PATH_FORMAT_HELP} A 'name' plus 'packagePath' pair is accepted as the same thing.`,
  );
}

/**
 * Pull an asset path out of any value a caller might use for it: a plain
 * string, Unreal's `{ refPath }` object reference (what the wrapped engine
 * toolsets use), or either of those serialized as JSON by a client that
 * flattens object arguments to strings.
 */
export function coerceAssetPathValue(value: unknown): string | undefined {
  if (typeof value === "string") {
    const trimmed = value.trim();
    if (trimmed.startsWith("{")) {
      try {
        return coerceAssetPathValue(JSON.parse(trimmed));
      } catch {
        return trimmed;
      }
    }
    return trimmed === "" ? undefined : trimmed;
  }
  if (value && typeof value === "object") {
    const rec = value as Record<string, unknown>;
    for (const key of ["refPath", "assetPath", "path"]) {
      const inner = rec[key];
      if (typeof inner === "string" && inner.trim() !== "") return inner.trim();
    }
  }
  return undefined;
}
