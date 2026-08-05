/**
 * Per-session equivalents of the process-wide environment variables
 * (#817, plan 6.7).
 *
 * The rule under test, for every one of them: the env var stays the global
 * default and wins, the config key is what one project uses to differ when it
 * is unset, and beyond one editor a set variable says so by name.
 */
import { describe, it, expect, beforeEach, afterEach } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { readUeMcpConfig } from "../../src/project.js";
import { EditorBridge } from "../../src/bridge.js";
import { collapsingEnvWarnings } from "../../src/session-env.js";
import { getFeedbackMode, setFeedbackMode } from "../../src/user-state.js";

let root: string;
const saved: Record<string, string | undefined> = {};

function setEnv(name: string, value: string | undefined): void {
  if (!(name in saved)) saved[name] = process.env[name];
  if (value === undefined) delete process.env[name];
  else process.env[name] = value;
}

function project(name: string, files: Record<string, string>): string {
  const dir = path.join(root, name);
  fs.mkdirSync(dir, { recursive: true });
  for (const [file, body] of Object.entries(files)) {
    fs.writeFileSync(path.join(dir, file), body, "utf-8");
  }
  return dir;
}

beforeEach(() => {
  root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-cfg-")));
  setEnv("UE_MCP_USER_STATE", path.join(root, "state.json"));
  setEnv("UE_MCP_HOST", undefined);
  setEnv("UE_MCP_ENV", undefined);
  setEnv("UE_MCP_PORT", undefined);
});

afterEach(() => {
  for (const [name, value] of Object.entries(saved)) {
    if (value === undefined) delete process.env[name];
    else process.env[name] = value;
    delete saved[name];
  }
  fs.rmSync(root, { recursive: true, force: true });
});

describe("bridge.host", () => {
  it("points one session at its own host", () => {
    const bridge = new EditorBridge();
    bridge.setConfigHost("10.0.0.9");
    expect(bridge.host).toBe("10.0.0.9");
  });

  it("does not override UE_MCP_HOST, which is the global default", () => {
    setEnv("UE_MCP_HOST", "192.168.1.4");
    const bridge = new EditorBridge();
    bridge.setConfigHost("10.0.0.9");
    expect(bridge.host).toBe("192.168.1.4");
  });

  it("leaves the loopback default alone when unset", () => {
    const bridge = new EditorBridge();
    bridge.setConfigHost(undefined);
    expect(bridge.host).toBe("127.0.0.1");
  });
});

describe("editor.path and editor.buildToolPath", () => {
  it("are read from the project's config", () => {
    const dir = project("Alpha", {
      "ue-mcp.yml": "ue-mcp:\n  editor:\n    path: D:/UE_5.7/UnrealEditor.exe\n    buildToolPath: D:/UE_5.7/Build.bat\n",
    });
    expect(readUeMcpConfig(dir).editor).toEqual({
      path: "D:/UE_5.7/UnrealEditor.exe",
      buildToolPath: "D:/UE_5.7/Build.bat",
    });
  });
});

describe("env overlay", () => {
  it("merges the overlay a project names itself", () => {
    const dir = project("Beta", {
      "ue-mcp.yml": "ue-mcp:\n  env: staging\n  bridge:\n    port: 9000\n",
      "ue-mcp.staging.yml": "ue-mcp:\n  bridge:\n    port: 9500\n",
    });
    expect(readUeMcpConfig(dir).bridge?.port).toBe(9500);
  });

  it("lets UE_MCP_ENV win, so a single-project user is unchanged", () => {
    const dir = project("Gamma", {
      "ue-mcp.yml": "ue-mcp:\n  env: staging\n  bridge:\n    port: 9000\n",
      "ue-mcp.staging.yml": "ue-mcp:\n  bridge:\n    port: 9500\n",
      "ue-mcp.ci.yml": "ue-mcp:\n  bridge:\n    port: 9900\n",
    });
    setEnv("UE_MCP_ENV", "ci");
    expect(readUeMcpConfig(dir).bridge?.port).toBe(9900);
  });

  it("keeps the local layer above the overlay", () => {
    const dir = project("Delta", {
      "ue-mcp.yml": "ue-mcp:\n  env: staging\n",
      "ue-mcp.staging.yml": "ue-mcp:\n  bridge:\n    port: 9500\n",
      "ue-mcp.local.yml": "ue-mcp:\n  bridge:\n    port: 9600\n",
    });
    expect(readUeMcpConfig(dir).bridge?.port).toBe(9600);
  });

  it("merges nothing extra when no overlay is named", () => {
    const dir = project("Epsilon", { "ue-mcp.yml": "ue-mcp:\n  bridge:\n    port: 9000\n" });
    expect(readUeMcpConfig(dir).bridge?.port).toBe(9000);
  });
});

describe("feedback mode", () => {
  it("prefers one project's mode over the user-wide preference", () => {
    const alpha = path.join(root, "Alpha");
    const beta = path.join(root, "Beta");
    setFeedbackMode("interactive");
    setFeedbackMode("defer", alpha);

    expect(getFeedbackMode(alpha)).toBe("defer");
    expect(getFeedbackMode(beta)).toBe("interactive");
    expect(getFeedbackMode()).toBe("interactive");
  });

  it("falls back to the user preference once a project's mode is cleared", () => {
    const alpha = path.join(root, "Alpha");
    setFeedbackMode("auto-approve");
    setFeedbackMode("defer", alpha);
    setFeedbackMode(undefined, alpha);
    expect(getFeedbackMode(alpha)).toBe("auto-approve");
  });
});

describe("collapsing env warnings", () => {
  it("say nothing at one editor", () => {
    expect(collapsingEnvWarnings(["Alpha"], { UE_MCP_PORT: "9877" })).toEqual([]);
  });

  it("name the variable, the editors, and the key that separates them", () => {
    const lines = collapsingEnvWarnings(["Alpha", "Beta"], { UE_MCP_PORT: "9877" });
    expect(lines).toHaveLength(1);
    expect(lines[0]).toContain("UE_MCP_PORT=9877");
    expect(lines[0]).toContain("Alpha, Beta");
    expect(lines[0]).toContain("bridge.port");
  });

  it("cover every variable with a per-session equivalent", () => {
    const lines = collapsingEnvWarnings(["Alpha", "Beta"], {
      UE_MCP_PORT: "9877",
      UE_MCP_HOST: "10.0.0.1",
      UE_EDITOR_PATH: "D:/UnrealEditor.exe",
      UE_BUILD_TOOL_PATH: "D:/Build.bat",
      UE_MCP_ENV: "ci",
      UE_MCP_CONTEXT_STRATEGY: "lean",
      UE_MCP_FEEDBACK_MODE: "defer",
    });
    expect(lines).toHaveLength(7);
  });

  it("say nothing about variables nobody set", () => {
    expect(collapsingEnvWarnings(["Alpha", "Beta"], {})).toEqual([]);
  });
});
