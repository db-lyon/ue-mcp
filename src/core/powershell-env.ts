import * as os from "node:os";
import * as path from "node:path";

/**
 * Where PowerShell keeps its module analysis cache for processes this server starts.
 *
 * With no `PSModuleAnalysisCachePath`, PowerShell derives the cache from LOCALAPPDATA or
 * USERPROFILE. When either points at a missing or relative location (a test that swapped
 * the environment and never restored it), the cache lands in the working directory as
 * `Microsoft/Windows/PowerShell/ModuleAnalysisCache`. Pinning it under the temp folder
 * keeps it out of the repo whatever the environment says.
 */
export function powershellModuleCachePath(): string {
  return path.join(os.tmpdir(), "ue-mcp-powershell", "ModuleAnalysisCache");
}

/** `base` plus `PSModuleAnalysisCachePath`. Pass this as `env` to every PowerShell spawn. */
export function powershellEnv(base: NodeJS.ProcessEnv = process.env): NodeJS.ProcessEnv {
  return { ...base, PSModuleAnalysisCachePath: powershellModuleCachePath() };
}
