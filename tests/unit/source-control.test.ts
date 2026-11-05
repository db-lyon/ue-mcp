/**
 * source_control: the editor's own provider behind one category (#1357).
 *
 * The provider calls only run in an editor, so these hold the declaration and
 * the C++ source to the contract: every action reaches its spec'd handler, the
 * no-provider refusal is one result, and checkout on write stays opt-in.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { ALL_TOOLS } from "../../src/tools.js";
import { sourceControlTool } from "../../src/tools/source_control.js";
import { declaredActionEffect } from "../../src/surface/action-effects.js";
import { REGISTERED_HANDLER_TIMEOUT_SECONDS } from "../../src/bridge/bridge-timeouts.js";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const BRIDGE = path.join(ROOT, "plugin", "ue_mcp_bridge", "Source", "UE_MCP_Bridge");
const read = (rel: string) => fs.readFileSync(path.join(BRIDGE, rel), "utf8");
const HANDLERS = read("Private/Handlers/SourceControl/SourceControlHandlers.cpp");

/** The body of one FSourceControlHandlers method, up to the next top-level definition. */
function body(method: string): string {
  const start = HANDLERS.indexOf(`FSourceControlHandlers::${method}(`);
  expect(start, method).toBeGreaterThan(-1);
  const next = HANDLERS.indexOf("\nTSharedPtr<FJsonValue> FSourceControlHandlers::", start + 1);
  const end = next === -1 ? HANDLERS.indexOf("\nvoid FSourceControlHandlers::RegisterHandlers", start) : next;
  return HANDLERS.slice(start, end);
}

const ACTIONS: Record<string, { bridge: string; effect: "read" | "mutate"; params: string[]; method: string }> = {
  get_provider: { bridge: "source_control_get_provider", effect: "read", params: [], method: "GetProvider" },
  get_status: { bridge: "source_control_get_status", effect: "read", params: ["paths", "limit"], method: "GetStatus" },
  list_changes: { bridge: "source_control_list_changes", effect: "read", params: ["paths", "limit", "includeUntracked"], method: "ListChanges" },
  checkout: { bridge: "source_control_checkout", effect: "mutate", params: ["paths"], method: "Checkout" },
  add: { bridge: "source_control_add", effect: "mutate", params: ["paths"], method: "MarkForAdd" },
  revert: { bridge: "source_control_revert", effect: "mutate", params: ["paths", "dryRun", "discardUnsaved", "restoreFrom"], method: "Revert" },
  diff: { bridge: "source_control_diff", effect: "read", params: ["path", "openEditor", "contextLines", "maxLines"], method: "Diff" },
  submit: { bridge: "source_control_submit", effect: "mutate", params: ["paths", "description"], method: "Submit" },
  set_checkout_mode: { bridge: "source_control_set_checkout_mode", effect: "mutate", params: ["mode"], method: "SetCheckoutMode" },
  mark_for_delete: { bridge: "source_control_mark_for_delete", effect: "mutate", params: ["paths"], method: "MarkForDelete" },
  sync: { bridge: "source_control_sync", effect: "mutate", params: ["paths"], method: "Sync" },
  history: { bridge: "source_control_history", effect: "read", params: ["path", "limit"], method: "History" },
  connect: { bridge: "source_control_connect", effect: "mutate", params: ["password"], method: "Connect" },
};

/** The new provider operations and the engine operation each runs. */
const OPERATIONS: Record<string, { method: string; operation: string }> = {
  mark_for_delete: { method: "MarkForDelete", operation: "FDelete" },
  sync: { method: "Sync", operation: "FSync" },
  history: { method: "History", operation: "FUpdateStatus" },
  connect: { method: "Connect", operation: "FConnect" },
};

describe("source_control", () => {
  it("is one category tool in ALL_TOOLS, named apart from the p4 plugin's category", () => {
    expect(ALL_TOOLS.filter((t) => t.name === "source_control")).toHaveLength(1);
    expect(sourceControlTool.name).not.toBe("p4");
    expect(Object.keys(sourceControlTool.actions).sort()).toEqual(Object.keys(ACTIONS).sort());
  });

  it.each(Object.entries(ACTIONS))("%s dispatches to its spec'd handler and forwards the bag as sent", (name, want) => {
    const action = sourceControlTool.actions[name];
    expect(action.kind).toBe("bridge");
    expect(action.bridge).toBe(want.bridge);
    expect(action.mapParams).toBeUndefined();
    expect(action.kind === "bridge" && action.paramSpec?.map((p) => p.name)).toEqual(want.params);
    expect(declaredActionEffect("source_control", name)).toBe(want.effect);
  });

  it("registers every bridge method in C++ under the same name", () => {
    for (const want of Object.values(ACTIONS)) {
      expect(HANDLERS).toMatch(new RegExp(`TEXT\\("${want.bridge}"\\),\\s*&${want.method}\\b`));
    }
  });

  it("refuses once, before touching files, when no provider is configured", () => {
    expect(HANDLERS).toContain("source_control_not_enabled");
    expect(HANDLERS).toContain("source_control_unavailable");
    for (const [name, want] of Object.entries(ACTIONS)) {
      // connect needs an enabled provider, not a connected one, so it has its own refusal.
      if (name === "get_provider" || name === "set_checkout_mode" || name === "connect") continue;
      expect(body(want.method), name).toContain("RequireProvider(Provider)");
    }
    // get_provider answers enabled=false instead of failing.
    expect(body("GetProvider")).not.toContain("RequireProvider");
    expect(body("GetProvider")).toContain('SetBoolField(TEXT("enabled"), bEnabled)');
  });

  it("reverts only after reporting unsaved editor edits, and a dry run changes nothing", () => {
    const revert = body("Revert");
    expect(revert.indexOf("if (bDryRun)")).toBeLessThan(revert.indexOf("RevertAndReloadPackages"));
    expect(revert.indexOf("!bDiscardUnsaved")).toBeLessThan(revert.indexOf("RevertAndReloadPackages"));
    expect(sourceControlTool.actions.revert.description).toMatch(/dryRun=true/);
    expect(sourceControlTool.actions.revert.description).toMatch(/discardUnsaved=true/);
  });

  it("rolls checkout and add back by reverting exactly what the call opened", () => {
    expect(body("Checkout")).toContain('MCPSetRollback(Result, TEXT("source_control_revert"), Payload)');
    expect(body("MarkForAdd")).toContain('MCPSetRollback(Result, TEXT("source_control_revert"), Payload)');
    expect(body("SetCheckoutMode")).toContain('MCPSetRollback(Result, TEXT("source_control_set_checkout_mode"), Payload)');
    expect(body("MarkForDelete")).toContain('MCPSetRollback(Result, TEXT("source_control_revert"), Payload)');
  });

  it.each(Object.entries(OPERATIONS))("%s runs its engine operation only where the provider says it can", (name, want) => {
    const method = body(want.method);
    expect(method, name).toContain(`ISourceControlOperation::Create<${want.operation}>()`);
    expect(method, name).toMatch(/if \(auto Err = RequireOperation\(/);
    // The gate comes before the provider is asked to run anything.
    expect(method.indexOf("RequireOperation("), name).toBeLessThan(method.indexOf("Execute("));
  });

  it("asks CanExecuteOperation only on engines that have it, and never names a provider in code", () => {
    expect(HANDLERS).toMatch(/#define UE_MCP_SCC_HAS_CAN_EXECUTE \(ENGINE_MAJOR_VERSION > 5 \|\| \(ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3\)\)/);
    for (const call of HANDLERS.matchAll(/CanExecuteOperation\(/g)) {
      const guard = HANDLERS.lastIndexOf("#if UE_MCP_SCC_HAS_CAN_EXECUTE", call.index);
      const close = HANDLERS.indexOf("#endif", guard);
      expect(guard, "CanExecuteOperation outside the version guard").toBeGreaterThan(-1);
      expect(close).toBeGreaterThan(call.index!);
    }
    // Behaviour follows the provider's answers, not its name.
    expect(HANDLERS).not.toMatch(/GetName\(\)\s*(==|!=|\.Equals|\.ToString\(\)\s*==)/);
  });

  it("get_provider reports which source_control actions the provider can run", () => {
    const provider = body("GetProvider");
    expect(provider).toContain('SetObjectField(TEXT("operations"), Operations)');
    for (const action of ["checkout", "add", "mark_for_delete", "revert", "sync", "submit", "history", "connect"]) {
      expect(provider, action).toContain(`Operations->SetBoolField(TEXT("${action}")`);
      expect(sourceControlTool.actions[action], action).toBeDefined();
    }
  });

  it("refuses mark_for_delete and sync over unsaved editor edits before running the operation", () => {
    for (const method of ["MarkForDelete", "Sync"]) {
      const b = body(method);
      expect(b.indexOf("unsavedInEditor"), method).toBeGreaterThan(-1);
      expect(b.indexOf("unsavedInEditor"), method).toBeLessThan(b.indexOf("Provider->Execute("));
    }
    expect(body("Sync")).toContain("UPackageTools::ReloadPackages");
  });

  it("clears the provider's progress window before answering, so the dialog gate does not refuse the next call", () => {
    for (const want of Object.values(ACTIONS)) {
      expect(body(want.method), want.method).toContain("const MCPSourceControlState::FSettleProviderWindows Settle;");
    }
    const settle = HANDLERS.slice(HANDLERS.indexOf("struct FSettleProviderWindows"));
    expect(settle.indexOf("DestroyWindowsImmediately()")).toBeLessThan(settle.indexOf("CaptureNow()"));
  });

  it("sends a connect password to the provider and never back on the result", () => {
    const connect = body("Connect");
    expect(connect).toContain("Operation->SetPassword(Password)");
    expect(connect).not.toMatch(/SetStringField\([^)]*Password\)/);
    expect(connect).toContain('SetBoolField(TEXT("passwordSent")');
  });

  it("keeps the long provider calls inside a client timeout at least as long as the server's", () => {
    for (const method of ["source_control_get_status", "source_control_list_changes", "source_control_diff", "source_control_submit", "source_control_history", "source_control_connect"]) {
      expect(REGISTERED_HANDLER_TIMEOUT_SECONDS[method], method).toBe(120);
    }
    expect(REGISTERED_HANDLER_TIMEOUT_SECONDS.source_control_sync).toBe(300);
  });
});

describe("checkout on write", () => {
  it("is off unless the session or UE_MCP_CHECKOUT_MODE turns it on", () => {
    expect(HANDLERS).toMatch(/GetEnvironmentVariable\(TEXT\("UE_MCP_CHECKOUT_MODE"\)\)\.Equals\(TEXT\("auto"\)/);
    expect(sourceControlTool.actions.set_checkout_mode.description).toMatch(/manual \(the default\)/);
  });

  it("is asked before a read-only package is refused, and the refusal names source_control(checkout)", () => {
    const save = read("Public/HandlerPackageSave.h");
    const tryAt = save.indexOf("MCPSourceControl::TryAutoCheckout(PackageFileName, CheckoutNote)");
    expect(tryAt).toBeGreaterThan(-1);
    expect(tryAt).toBeLessThan(save.indexOf("is read-only on disk. Check it out with source_control(checkout)"));
    expect(read("Private/Handlers/Level/LevelHandlers_Save.cpp")).toContain("MCPSourceControl::TryAutoCheckout(FileName, CheckoutNote)");
  });

  it("reports what a dispatch checked out on its result", () => {
    const registry = read("Private/HandlerRegistry.cpp");
    expect(registry).toContain("MCPSourceControl::AutoCheckoutMark()");
    expect(registry.match(/MCPSourceControl::AttachAutoCheckouts\(Result, CheckoutMark\)/g)).toHaveLength(2);
    expect(HANDLERS).toContain('SetArrayField(TEXT("autoCheckedOut")');
  });
});
