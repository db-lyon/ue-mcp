import { describe, it, expect } from "vitest";
import * as os from "node:os";
import * as path from "node:path";
import { powershellEnv, powershellModuleCachePath } from "../../src/core/powershell-env.js";
import { powershellEnv as scriptPowershellEnv } from "../../scripts/lib/powershell-env.mjs";

describe("powershellEnv", () => {
  it("pins the module analysis cache under the temp folder", () => {
    const cache = powershellModuleCachePath();
    expect(path.isAbsolute(cache)).toBe(true);
    expect(cache.startsWith(os.tmpdir())).toBe(true);
    expect(powershellEnv({}).PSModuleAnalysisCachePath).toBe(cache);
  });

  it("holds even when the environment points at nothing usable", () => {
    const env = powershellEnv({ USERPROFILE: "", LOCALAPPDATA: "relative" });
    expect(env.PSModuleAnalysisCachePath).toBe(powershellModuleCachePath());
    expect(env.LOCALAPPDATA).toBe("relative");
  });

  it("overrides an inherited cache path and leaves the base untouched", () => {
    const base = { PSModuleAnalysisCachePath: "somewhere" };
    expect(powershellEnv(base).PSModuleAnalysisCachePath).toBe(powershellModuleCachePath());
    expect(base.PSModuleAnalysisCachePath).toBe("somewhere");
  });

  it("matches the scripts mirror", () => {
    expect(scriptPowershellEnv({}).PSModuleAnalysisCachePath).toBe(powershellModuleCachePath());
  });
});
