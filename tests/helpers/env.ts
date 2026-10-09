/**
 * Put process.env back to a saved copy, in place.
 *
 * Never `process.env = saved`: on Windows that swaps the case-insensitive
 * environment for a plain object, and every later lookup in the worker
 * (`SystemRoot` against a `SYSTEMROOT` key) quietly misses.
 */
export function restoreEnv(saved: NodeJS.ProcessEnv): void {
  for (const k of Object.keys(process.env)) if (!(k in saved)) delete process.env[k];
  for (const [k, v] of Object.entries(saved)) {
    if (v === undefined) delete process.env[k];
    else process.env[k] = v;
  }
}
