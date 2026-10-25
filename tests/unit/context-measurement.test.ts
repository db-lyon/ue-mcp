/**
 * The context measurement format: the validator exported from ue-mcp/daemon
 * and schemas/context-measurement.schema.json accept and refuse the same
 * documents, and scopeBucket buckets category counts.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { Ajv } from "ajv";
import { describe, expect, it } from "vitest";
import { parseContextMeasurement, scopeBucket } from "../../src/daemon-api.js";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const SCHEMA = JSON.parse(fs.readFileSync(path.join(ROOT, "schemas", "context-measurement.schema.json"), "utf8")) as object;
const validate = new Ajv({ allErrors: true, strict: true }).compile(SCHEMA);

const good = () => ({
  version: 1,
  measuredAt: "2026-10-25T12:30:00.000Z",
  ueMcpVersion: "1.4.11",
  client: { name: "studio", version: "0.2.0" },
  runs: [
    {
      model: { vendor: "anthropic", model: "claude-opus-5-5" },
      scope: "1-3",
      strategy: "micro",
      seedTokens: 2207,
      tasks: [
        { id: "place-light", totalTokens: 18000, modelCalls: 6, discoveryCalls: 2, passed: true },
        { id: "bp-variable", totalTokens: 24000, modelCalls: 9, discoveryCalls: 3, passed: false },
      ],
    },
    { model: { vendor: "openai", model: "gpt-x" }, scope: "9+", strategy: "full", seedTokens: 55708, tasks: [] },
  ],
});

type Doc = Record<string, any>;
const variants: Array<[string, (d: Doc) => unknown]> = [
  ["a minimal document", (d) => ({ version: 1, measuredAt: d.measuredAt, ueMcpVersion: "1.0.0", runs: [] })],
  ["an offset timestamp", (d) => ({ ...d, measuredAt: "2026-10-25T12:30:00+02:00" })],
  ["the wrong version", (d) => ({ ...d, version: 2 })],
  ["a date without a time", (d) => ({ ...d, measuredAt: "2026-10-25" })],
  ["no ueMcpVersion", ({ ueMcpVersion: _v, ...d }) => d],
  ["an empty ueMcpVersion", (d) => ({ ...d, ueMcpVersion: "" })],
  ["an unknown top-level key", (d) => ({ ...d, notes: "x" })],
  ["a client without a version", (d) => ({ ...d, client: { name: "studio" } })],
  ["runs that are not an array", (d) => ({ ...d, runs: {} })],
  ["an unknown scope", (d) => ({ ...d, runs: [{ ...d.runs[0], scope: "2-5" }] })],
  ["an unknown strategy", (d) => ({ ...d, runs: [{ ...d.runs[0], strategy: "tiny" }] })],
  ["fractional seed tokens", (d) => ({ ...d, runs: [{ ...d.runs[0], seedTokens: 1.5 }] })],
  ["negative model calls", (d) => ({ ...d, runs: [{ ...d.runs[0], tasks: [{ ...d.runs[0].tasks[0], modelCalls: -1 }] }] })],
  ["a string passed flag", (d) => ({ ...d, runs: [{ ...d.runs[0], tasks: [{ ...d.runs[0].tasks[0], passed: "yes" }] }] })],
  ["a task with an extra key", (d) => ({ ...d, runs: [{ ...d.runs[0], tasks: [{ ...d.runs[0].tasks[0], cost: 1 }] }] })],
  ["a model without a vendor", (d) => ({ ...d, runs: [{ ...d.runs[0], model: { model: "m" } }] })],
  ["a task that is not an object", (d) => ({ ...d, runs: [{ ...d.runs[0], tasks: [7] }] })],
  ["a document that is not an object", () => [1, 2]],
  ["null", () => null],
];

describe("parseContextMeasurement and the JSON Schema", () => {
  it("accept a complete document", () => {
    const doc = good();
    expect(validate(doc), JSON.stringify(validate.errors)).toBe(true);
    expect(parseContextMeasurement(doc)).toEqual({ ok: true, value: doc });
  });

  for (const [name, make] of variants) {
    it(`agree on ${name}`, () => {
      const doc = make(good());
      const schemaOk = validate(doc) as boolean;
      const parsed = parseContextMeasurement(doc);
      expect(parsed.ok, `schema ${schemaOk ? "accepts" : "refuses"}, validator says ${JSON.stringify(parsed)}`).toBe(schemaOk);
      if (!parsed.ok) expect(parsed.errors.length).toBeGreaterThan(0);
    });
  }

  it("reports every problem by path", () => {
    const parsed = parseContextMeasurement({ version: 1, measuredAt: "x", ueMcpVersion: "", runs: [{ scope: "1-3", strategy: "lean", seedTokens: 1, tasks: [{ id: "", totalTokens: 1, modelCalls: 1, discoveryCalls: 0, passed: true }] }] });
    expect(parsed.ok).toBe(false);
    if (!parsed.ok) {
      expect(parsed.errors).toEqual([
        "$.measuredAt must be an ISO 8601 date-time",
        "$.ueMcpVersion must be a non-empty string",
        "$.runs[0].model must be an object",
        "$.runs[0].tasks[0].id must be a non-empty string",
      ]);
    }
  });
});

describe("scopeBucket", () => {
  it("buckets a category count", () => {
    expect([1, 3, 4, 8, 9, 30].map(scopeBucket)).toEqual(["1-3", "1-3", "4-8", "4-8", "9+", "9+"]);
    expect(() => scopeBucket(0)).toThrow(RangeError);
    expect(() => scopeBucket(2.5)).toThrow(RangeError);
  });
});
