// Mirror of src/core/powershell-env.ts for the plain-node scripts, which cannot import TypeScript.
// Pins PowerShell's module analysis cache under the temp folder so it never lands in the repo.
import os from "node:os";
import path from "node:path";

export function powershellEnv(base = process.env) {
  return { ...base, PSModuleAnalysisCachePath: path.join(os.tmpdir(), "ue-mcp-powershell", "ModuleAnalysisCache") };
}
