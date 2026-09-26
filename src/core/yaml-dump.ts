import yaml from "js-yaml";

/**
 * Dump a JS object to YAML preserving the ue-mcp.yml convention of unquoted
 * integer keys (e.g. flow step ordering: `1:`, `2:`, `3:`).
 *
 * Background: JavaScript object keys are always strings. When js-yaml reads
 *
 *   steps:
 *     1: ...
 *     2: ...
 *
 * the integer keys become string keys "1", "2" in JS. On dump, js-yaml then
 * quotes them as `'1':`, `'2':` so YAML would parse them back as strings,
 * preserving the JS round-trip. But the flowkit step convention is unquoted
 * integer keys, so the round-trip damages user-authored files.
 *
 * Fix: after dump, unquote any line where the key is a pure-integer string
 * (with optional list-item bullet). Pattern is narrow enough to avoid
 * collateral damage on values; matches only at line-start, after optional
 * leading whitespace and an optional `- ` list bullet.
 *
 * Caveat: if a user genuinely wants a string-typed key like `'1':`, this
 * round-trip will rewrite it to `1:`. In ue-mcp.yml the convention is
 * integer keys, so this is the right trade-off.
 */
export function dumpYaml(obj: unknown): string {
  const raw = yaml.dump(obj, { indent: 2 });
  return raw.replace(/^(\s*(?:-\s+)?)'(\d+)':/gm, "$1$2:");
}
