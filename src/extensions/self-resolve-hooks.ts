// @session independent: Module resolution hooks run in the loader thread; no editor.
/**
 * Module resolution hooks that answer a plugin's `ue-mcp` and `ue-mcp/*`
 * imports with the running server's own package, so a plugin needs no copy
 * of ue-mcp in node_modules and extends the same base classes the server runs.
 */

interface ResolveContext {
  parentURL?: string;
  conditions?: string[];
  importAttributes?: Record<string, string>;
}
type NextResolve = (specifier: string, context?: ResolveContext) => Promise<unknown>;

let selfUrl: string | undefined;

/** `selfUrl` is a file URL inside the ue-mcp package, so a self-reference resolves. */
export function initialize(data: { selfUrl?: string } | undefined): void {
  selfUrl = data?.selfUrl;
}

export function isServerSpecifier(specifier: string): boolean {
  return specifier === "ue-mcp" || specifier.startsWith("ue-mcp/");
}

export async function resolve(specifier: string, context: ResolveContext, nextResolve: NextResolve): Promise<unknown> {
  if (selfUrl && isServerSpecifier(specifier)) return nextResolve(specifier, { ...context, parentURL: selfUrl });
  return nextResolve(specifier, context);
}
