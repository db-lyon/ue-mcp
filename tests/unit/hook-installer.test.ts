import { describe, it, expect, beforeEach, afterEach } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import {
  installClaudeHooks,
  uninstallClaudeHooks,
  uninstallAllRegisteredHooks,
} from "../../src/hook-installer.js";

let tmpRoot: string;
let projectDir: string;
let settingsPath: string;

beforeEach(() => {
  tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-hook-test-"));
  projectDir = path.join(tmpRoot, "project");
  fs.mkdirSync(projectDir);
  settingsPath = path.join(projectDir, ".claude", "settings.json");
});

afterEach(() => {
  fs.rmSync(tmpRoot, { recursive: true, force: true });
});

function readJson<T = unknown>(p: string): T {
  return JSON.parse(fs.readFileSync(p, "utf-8")) as T;
}

interface ClaudeSettings {
  hooks?: {
    PostToolUse?: Array<{ matcher: string; hooks: unknown[] }>;
  };
}

interface UeMcpConfig {
  installedHooks?: string[];
}

describe("hook installer", () => {
  it("installs the matcher into a fresh settings.json", () => {
    installClaudeHooks(settingsPath, projectDir);
    const settings = readJson<ClaudeSettings>(settingsPath);
    expect(settings.hooks?.PostToolUse).toHaveLength(1);
    expect(settings.hooks?.PostToolUse?.[0].matcher).toBe("mcp__ue-mcp__editor");
  });

  it("registers the install path in .ue-mcp.json", () => {
    installClaudeHooks(settingsPath, projectDir);
    const cfg = readJson<UeMcpConfig>(path.join(projectDir, ".ue-mcp.json"));
    expect(cfg.installedHooks).toContain(path.resolve(settingsPath));
  });

  it("is idempotent on repeated install — no duplicate matcher, no duplicate registry entry", () => {
    installClaudeHooks(settingsPath, projectDir);
    installClaudeHooks(settingsPath, projectDir);
    const settings = readJson<ClaudeSettings>(settingsPath);
    expect(settings.hooks?.PostToolUse).toHaveLength(1);
    const cfg = readJson<UeMcpConfig>(path.join(projectDir, ".ue-mcp.json"));
    expect(cfg.installedHooks?.filter((p) => p === path.resolve(settingsPath))).toHaveLength(1);
  });

  it("preserves unrelated hook entries on install and uninstall", () => {
    fs.mkdirSync(path.dirname(settingsPath), { recursive: true });
    fs.writeFileSync(
      settingsPath,
      JSON.stringify({
        hooks: {
          PostToolUse: [{ matcher: "some-other-tool", hooks: [{ type: "command", command: "echo hi" }] }],
        },
      }),
    );
    installClaudeHooks(settingsPath, projectDir);
    let settings = readJson<ClaudeSettings>(settingsPath);
    expect(settings.hooks?.PostToolUse).toHaveLength(2);

    uninstallClaudeHooks(settingsPath, projectDir);
    settings = readJson<ClaudeSettings>(settingsPath);
    expect(settings.hooks?.PostToolUse).toHaveLength(1);
    expect(settings.hooks?.PostToolUse?.[0].matcher).toBe("some-other-tool");
  });

  it("uninstall removes matcher and unregisters from .ue-mcp.json", () => {
    installClaudeHooks(settingsPath, projectDir);
    const removed = uninstallClaudeHooks(settingsPath, projectDir);
    expect(removed).toBe(true);
    const settings = readJson<ClaudeSettings>(settingsPath);
    expect(settings.hooks?.PostToolUse).toBeUndefined();
    const cfg = readJson<UeMcpConfig>(path.join(projectDir, ".ue-mcp.json"));
    expect(cfg.installedHooks ?? []).not.toContain(path.resolve(settingsPath));
  });

  it("uninstall is idempotent — returns false when nothing to remove", () => {
    fs.mkdirSync(path.dirname(settingsPath), { recursive: true });
    fs.writeFileSync(settingsPath, JSON.stringify({ hooks: {} }));
    const removed = uninstallClaudeHooks(settingsPath, projectDir);
    expect(removed).toBe(false);
  });

  it("uninstall is safe when settings file does not exist", () => {
    const removed = uninstallClaudeHooks(settingsPath, projectDir);
    expect(removed).toBe(false);
  });

  it("uninstallAllRegisteredHooks removes every registered site", () => {
    const projectSettings = path.join(projectDir, ".claude", "settings.json");
    const globalSettings = path.join(tmpRoot, "global", "settings.json");
    fs.mkdirSync(path.dirname(globalSettings), { recursive: true });
    installClaudeHooks(projectSettings, projectDir);
    installClaudeHooks(globalSettings, projectDir);

    const result = uninstallAllRegisteredHooks(projectDir);
    expect(result.removed.sort()).toEqual(
      [path.resolve(projectSettings), path.resolve(globalSettings)].sort(),
    );

    expect(readJson<ClaudeSettings>(projectSettings).hooks).toBeUndefined();
    expect(readJson<ClaudeSettings>(globalSettings).hooks).toBeUndefined();
    const cfg = readJson<UeMcpConfig>(path.join(projectDir, ".ue-mcp.json"));
    expect(cfg.installedHooks).toBeUndefined();
  });
});
