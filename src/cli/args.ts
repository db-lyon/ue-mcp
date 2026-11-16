// @session independent: Pure argv parsing for the one-shot CLI commands.
/**
 * The argument helpers every `ue-mcp <command>` shares. Deliberately not a
 * general parser: several commands forward the rest of argv verbatim, so each
 * helper reads what it is asked for and leaves everything else alone.
 */

/** `--name value` or `--name=value`; undefined when absent. */
export function flagValue(argv: readonly string[], name: string): string | undefined {
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === name) return argv[i + 1];
    if (a.startsWith(`${name}=`)) return a.slice(name.length + 1);
  }
  return undefined;
}

/** true for --name, false for --no-name, undefined when neither. */
export function flagBool(argv: readonly string[], name: string): boolean | undefined {
  if (argv.includes(`--no-${name}`)) return false;
  if (argv.includes(`--${name}`)) return true;
  return undefined;
}

/** The first argument that is neither a flag nor the value of one of `valueFlags`. */
export function firstPositional(argv: readonly string[], valueFlags: ReadonlySet<string> = new Set()): string | undefined {
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (valueFlags.has(a)) {
      i++;
      continue;
    }
    if (!a.startsWith("-")) return a;
  }
  return undefined;
}

/** "a, b or c", for a message that lists the values a flag takes. */
export function orList(values: readonly string[]): string {
  return values.length <= 1 ? values.join("") : `${values.slice(0, -1).join(", ")} or ${values[values.length - 1]}`;
}
