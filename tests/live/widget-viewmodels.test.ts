// @live-editor headless
/**
 * Viewmodel (UMG MVVM) authoring on a Widget Blueprint: a Blueprint viewmodel
 * is added, bound to two widgets (one binding through a conversion function
 * with a literal argument), compiled, edited, read back and taken apart again,
 * every step checked through list_widget_viewmodels and
 * list_widget_viewmodel_bindings. tests/ue_mcp enables the ModelViewViewModel
 * plugin these actions need.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/Viewmodels";
const VM = `${DIR}/BP_MCPTestScoreVM`;
const WBP = `${DIR}/WBP_MCPTestViewmodels`;

type Json = Record<string, unknown>;
type Binding = {
  bindingId: string;
  sourcePath: string;
  destinationPath: string;
  bindingMode: string;
  executionMode: string;
  enabled: boolean;
  includeInCompile: boolean;
  conversionFunction?: string;
  conversionDirection?: string;
  conversionArguments?: Record<string, { path?: string; literal?: string }>;
  errors: string[];
};
type ViewModel = { viewModelName: string; viewModelId: string; viewModelClass: string; creationType: string; bindingCount: number };

const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function viewModels(): Promise<ViewModel[]> {
  return (await call("list_widget_viewmodels", { assetPath: WBP })).viewModels as ViewModel[];
}

async function bindings(): Promise<Binding[]> {
  return (await call("list_widget_viewmodel_bindings", { assetPath: WBP })).bindings as Binding[];
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: WBP, force: true });
  await callBridge(bridge, "delete_asset", { assetPath: VM, force: true });
  await call("create_blueprint", { assetPath: VM, parentClass: "MVVMViewModelBase" });
  await call("add_variable", { assetPath: VM, name: "Score", varType: "int" });
  await call("add_variable", { assetPath: VM, name: "Health", varType: "float" });
  await call("compile_blueprint", { assetPath: VM });
  await call("create_widget_blueprint", { assetPath: WBP });
  await call("add_widget", { assetPath: WBP, widgetClass: "VerticalBox", widgetName: "Root" });
  await call("add_widget", { assetPath: WBP, widgetClass: "TextBlock", widgetName: "ScoreText", parentWidgetName: "Root" });
  await call("add_widget", { assetPath: WBP, widgetClass: "ProgressBar", widgetName: "HealthBar", parentWidgetName: "Root" });
}, 240_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: WBP, force: true });
    await callBridge(bridge, "delete_asset", { assetPath: VM, force: true });
    disconnectBridge();
  }
});

describe("widget viewmodels", () => {
  let healthBinding = "";

  it("adds a viewmodel, idempotently, and lists it", async () => {
    expect(await viewModels()).toEqual([]);

    const added = await call("add_widget_viewmodel", { assetPath: WBP, viewModelClass: VM, viewModelName: "ScoreVM" });
    expect(added.created).toBe(true);
    expect(added.viewModelName).toBe("ScoreVM");
    expect(String(added.viewModelClass)).toContain("BP_MCPTestScoreVM");
    expect((added.rollback as Json).method).toBe("remove_widget_viewmodel");

    const again = await call("add_widget_viewmodel", { assetPath: WBP, viewModelClass: VM, viewModelName: "ScoreVM" });
    expect(again.existed).toBe(true);
    expect(again.rollback).toBeUndefined();

    const listed = await viewModels();
    expect(listed).toHaveLength(1);
    // The engine picks the first creation type the class allows, as the editor's own Add does.
    expect(listed[0]).toMatchObject({ viewModelName: "ScoreVM", creationType: added.creationType, bindingCount: 0 });
    expect(["Manual", "CreateInstance"]).toContain(listed[0].creationType);
  }, 120_000);

  it("adds a plain binding and one through a conversion function, and reads both back", async () => {
    const plain = await call("add_widget_viewmodel_binding", {
      assetPath: WBP,
      sourcePath: "ScoreVM.Health",
      destinationPath: "HealthBar.Percent",
      bindingMode: "OneTimeToDestination",
    });
    expect(plain.created).toBe(true);
    healthBinding = String(plain.bindingId);
    expect(healthBinding).toMatch(/^[0-9A-F-]{36}$/i);

    const replay = await call("add_widget_viewmodel_binding", {
      assetPath: WBP,
      sourcePath: "ScoreVM.Health",
      destinationPath: "HealthBar.Percent",
      bindingMode: "OneTimeToDestination",
    });
    expect(replay.existed).toBe(true);
    expect(replay.bindingId).toBe(healthBinding);

    const converted = await call("add_widget_viewmodel_binding", {
      assetPath: WBP,
      sourcePath: "ScoreVM.Score",
      destinationPath: "ScoreText.Text",
      bindingMode: "OneTimeToDestination",
      conversionFunction: "KismetTextLibrary.Conv_IntToText",
      conversionArguments: { bUseGrouping: false },
    });
    expect(converted.created).toBe(true);

    const rows = await bindings();
    expect(rows).toHaveLength(2);
    const health = rows.find((b) => b.bindingId === healthBinding);
    expect(health).toMatchObject({
      sourcePath: "ScoreVM.Health",
      destinationPath: "HealthBar.Percent",
      bindingMode: "OneTimeToDestination",
      executionMode: "Default",
      enabled: true,
      includeInCompile: true,
    });
    const score = rows.find((b) => b.bindingId !== healthBinding)!;
    expect(score.destinationPath).toBe("ScoreText.Text");
    expect(score.conversionFunction).toMatch(/KismetTextLibrary:Conv_IntToText$/);
    expect(score.conversionDirection).toBe("sourceToDestination");
    expect(score.conversionArguments?.Value?.path).toBe("ScoreVM.Score");
    expect(score.conversionArguments?.bUseGrouping?.literal?.toLowerCase()).toBe("false");

    expect((await viewModels())[0].bindingCount).toBe(1);
  }, 120_000);

  it("compiles the widget blueprint with no binding errors", async () => {
    const compiled = await call("compile_widget_viewmodels", { assetPath: WBP });
    expect(compiled.compiled).toBe(true);
    expect(compiled.bindingErrorCount).toBe(0);
    expect(compiled.bindings as Binding[]).toHaveLength(2);
    expect(["upToDate", "upToDateWithWarnings"]).toContain(compiled.compileStatus);
  }, 180_000);

  it("updates a binding, reports a no-op as unchanged, and rolls the update back", async () => {
    const updated = await call("update_widget_viewmodel_binding", {
      assetPath: WBP,
      bindingId: healthBinding,
      executionMode: "Tick",
      enabled: false,
    });
    expect(updated.updated).toBe(true);
    let health = (await bindings()).find((b) => b.bindingId === healthBinding)!;
    expect(health.executionMode).toBe("Tick");
    expect(health.enabled).toBe(false);

    const noop = await call("update_widget_viewmodel_binding", { assetPath: WBP, bindingId: healthBinding, executionMode: "Tick" });
    expect(noop.unchanged).toBe(true);

    const rollback = updated.rollback as { method: string; payload: Json };
    expect(rollback.method).toBe("update_widget_viewmodel_binding");
    await call(rollback.method, rollback.payload);
    health = (await bindings()).find((b) => b.bindingId === healthBinding)!;
    expect(health.executionMode).toBe("Default");
    expect(health.enabled).toBe(true);
  }, 120_000);

  it("removes the viewmodel and its rollback reconnects the bindings that named it", async () => {
    const removed = await call("remove_widget_viewmodel", { assetPath: WBP, viewModelName: "ScoreVM" });
    expect(removed.deleted).toBe(true);
    expect(removed.bindingsLeftDangling as string[]).toContain(healthBinding);
    expect(await viewModels()).toEqual([]);

    const again = await call("remove_widget_viewmodel", { assetPath: WBP, viewModelName: "ScoreVM" });
    expect(again.alreadyDeleted).toBe(true);

    const rollback = removed.rollback as { method: string; payload: Json };
    expect(rollback.method).toBe("add_widget_viewmodel");
    await call(rollback.method, rollback.payload);
    expect((await viewModels()).map((v) => v.viewModelName)).toEqual(["ScoreVM"]);
    const health = (await bindings()).find((b) => b.bindingId === healthBinding)!;
    expect(health.sourcePath).toBe("ScoreVM.Health");
  }, 120_000);

  it("removes a binding and recreates it from the rollback", async () => {
    const removed = await call("remove_widget_viewmodel_binding", { assetPath: WBP, bindingId: healthBinding });
    expect(removed.deleted).toBe(true);
    expect((await bindings()).some((b) => b.bindingId === healthBinding)).toBe(false);

    const again = await call("remove_widget_viewmodel_binding", { assetPath: WBP, bindingId: healthBinding });
    expect(again.alreadyDeleted).toBe(true);

    const rollback = removed.rollback as { method: string; payload: Json };
    expect(rollback.method).toBe("add_widget_viewmodel_binding");
    const recreated = await call(rollback.method, rollback.payload);
    expect(recreated.created).toBe(true);
    const rows = await bindings();
    expect(rows.find((b) => b.bindingId === recreated.bindingId)).toMatchObject({
      sourcePath: "ScoreVM.Health",
      destinationPath: "HealthBar.Percent",
      bindingMode: "OneTimeToDestination",
    });
  }, 120_000);

  it("refuses a path that names nothing on the widget blueprint", async () => {
    const r = await callBridge(bridge, "add_widget_viewmodel_binding", {
      assetPath: WBP,
      sourcePath: "NoSuchVM.Score",
      destinationPath: "ScoreText.Text",
    });
    expect(succeeded(r)).toBe(false);
    expect(JSON.stringify(r.result ?? r.error)).toContain("NoSuchVM");
    expect(await bindings()).toHaveLength(2);
  }, 60_000);
});
