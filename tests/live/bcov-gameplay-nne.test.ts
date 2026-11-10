// @live-editor headless
/**
 * Neural Network Engine: the registered runtimes (LiteRT on UE 5.9), a model
 * asset imported from a one-node ONNX file written here, and a timed CPU
 * inference whose outputs are checked against the model's arithmetic. Needs
 * the NNERuntimeORT plugin, which the test project enables.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/NNE";
const MODEL = `${DIR}/NNE_BCovRelu`;
const ORT_CPU = "NNERuntimeORTCpu";

type Json = Record<string, unknown>;
type Output = { name: string; shape: number[]; values: number[] };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let scratch = "";
let onnxFile = "";

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

// ── A minimal ONNX model: y = Relu(x), x and y float [1, 4]. ──────────────
function varint(value: number): number[] {
  const out: number[] = [];
  let n = value;
  while (n > 127) {
    out.push((n & 0x7f) | 0x80);
    n = Math.floor(n / 128);
  }
  out.push(n);
  return out;
}
const bytesField = (num: number, payload: number[]): number[] => [...varint((num << 3) | 2), ...varint(payload.length), ...payload];
const varintField = (num: number, value: number): number[] => [...varint(num << 3), ...varint(value)];
const text = (s: string): number[] => [...Buffer.from(s, "utf8")];

function valueInfo(name: string, dims: number[]): number[] {
  const shape = dims.flatMap((d) => bytesField(1, varintField(1, d)));
  const tensorType = [...varintField(1, 1 /* FLOAT */), ...bytesField(2, shape)];
  return [...bytesField(1, text(name)), ...bytesField(2, bytesField(1, tensorType))];
}

function reluModel(): Buffer {
  const node = [...bytesField(1, text("x")), ...bytesField(2, text("y")), ...bytesField(3, text("relu")), ...bytesField(4, text("Relu"))];
  const graph = [...bytesField(1, node), ...bytesField(2, text("bcov_relu")), ...bytesField(11, valueInfo("x", [1, 4])), ...bytesField(12, valueInfo("y", [1, 4]))];
  const opset = [...bytesField(1, []), ...varintField(2, 13)];
  return Buffer.from([...varintField(1, 7), ...bytesField(2, text("ue-mcp-test")), ...bytesField(7, graph), ...bytesField(8, opset)]);
}

beforeAll(async () => {
  bridge = await getBridge();
  scratch = fs.mkdtempSync(path.join(os.tmpdir(), "bcov-nne-"));
  onnxFile = path.join(scratch, "bcov_relu.onnx");
  fs.writeFileSync(onnxFile, reluModel());
  await callBridge(bridge, "delete_asset", { assetPath: MODEL, force: true });
}, 120_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await callBridge(bridge, "delete_asset", { assetPath: MODEL, force: true });
  } finally {
    if (scratch) fs.rmSync(scratch, { recursive: true, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("NNE runtimes", () => {
  it("nne_list_runtimes reports the ONNX Runtime CPU runtime and the runtime plugins", async () => {
    const res = await call("nne_list_runtimes");
    expect(res.nneAvailable).toBe(true);
    const runtimes = res.runtimes as Array<{ name: string; cpu: boolean }>;
    expect(runtimes.find((r) => r.name === ORT_CPU)?.cpu, JSON.stringify(runtimes)).toBe(true);
    const plugins = res.runtimePlugins as Array<{ plugin: string; installed: boolean; enabled: boolean }>;
    expect(plugins.find((p) => p.plugin === "NNERuntimeORT")).toMatchObject({ installed: true, enabled: true });
  }, 60_000);

  it("reports NNERuntimeLiteRT's plugin state, and its runtime exactly when the registry has it", async () => {
    const res = await call("nne_list_runtimes");
    const liteRT = res.liteRT as Json;
    const listed = (res.runtimes as Array<{ name: string; cpu: boolean }>).find((r) => r.name === "NNERuntimeLiteRT");
    // The launcher's 5.9 build ships the plugin with the LiteRT library compiled out, so it
    // can be enabled and still register nothing; the report must agree with the registry.
    expect(liteRT.runtimeRegistered).toBe(listed !== undefined);
    expect(liteRT.cpu).toBe(listed?.cpu === true);
    if (liteRT.installed === true && liteRT.enabled === true && listed === undefined) {
      expect(String(liteRT.note)).toContain("registered no runtime");
    }
  }, 60_000);
});

describe("NNE model import and inference", () => {
  it("nne_import_model creates an NNEModelData for the chosen runtime", async () => {
    const res = await call("nne_import_model", { sourceFile: onnxFile, assetPath: MODEL, targetRuntimes: [ORT_CPU] });
    expect(res.created).toBe(true);
    expect(res.fileType).toBe("onnx");
    expect(res.targetRuntimes).toEqual([ORT_CPU]);
    expect((res.rollback as Json).method).toBe("delete_asset");
    const compat = (res.compatibility as Array<{ runtime: string; canCreateCpuModel?: boolean }>).find((c) => c.runtime === ORT_CPU);
    expect(compat?.canCreateCpuModel).toBe(true);
  }, 120_000);

  it("a second import reports existed, onConflict=error refuses, and an unregistered runtime is refused", async () => {
    const again = await call("nne_import_model", { sourceFile: onnxFile, assetPath: MODEL });
    expect(again.existed).toBe(true);
    const refused = await callBridge(bridge, "nne_import_model", { sourceFile: onnxFile, assetPath: MODEL, onConflict: "error" });
    expect(succeeded(refused)).toBe(false);
    const unknown = await callBridge(bridge, "nne_import_model", { sourceFile: onnxFile, assetPath: `${DIR}/NNE_BCovNever`, targetRuntimes: ["NoSuchRuntime"] });
    expect(succeeded(unknown)).toBe(false);
  }, 120_000);

  it("nne_run_inference runs the model on ONNX Runtime CPU and times each run", async () => {
    const res = await call("nne_run_inference", { assetPath: MODEL, runtime: ORT_CPU, inputs: [{ values: [-1, 2, -3, 4] }], iterations: 3 });
    expect(res.runtime).toBe(ORT_CPU);
    expect((res.inputDescs as Array<{ shape: number[] }>)[0].shape).toEqual([1, 4]);
    const out = (res.outputs as Output[])[0];
    expect(out.shape).toEqual([1, 4]);
    expect(out.values.map((v) => Math.round(v * 1000) / 1000)).toEqual([0, 2, 0, 4]);
    const timings = res.timings as Record<string, number>;
    expect(timings.runCount).toBe(3);
    expect(timings.runTotalMs).toBeGreaterThanOrEqual(0);
    expect(timings.createModelMs).toBeGreaterThanOrEqual(0);
  }, 120_000);

  it("without runtime it picks a CPU runtime that accepts the model", async () => {
    const res = await call("nne_run_inference", { assetPath: MODEL, inputs: [{ values: [5, -5, 0.5, -0.5] }] });
    expect(String(res.runtime).length).toBeGreaterThan(0);
    expect((res.outputs as Output[])[0].values.map((v) => Math.round(v * 1000) / 1000)).toEqual([5, 0, 0.5, 0]);
  }, 120_000);

  it("refuses inputs that do not match the model", async () => {
    const count = await callBridge(bridge, "nne_run_inference", { assetPath: MODEL, runtime: ORT_CPU, inputs: [] });
    expect(succeeded(count)).toBe(false);
    const size = await callBridge(bridge, "nne_run_inference", { assetPath: MODEL, runtime: ORT_CPU, inputs: [{ values: [1, 2, 3] }] });
    expect(succeeded(size)).toBe(false);
  }, 60_000);
});
