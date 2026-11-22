/**
 * `ue-mcp surface`: the neutral counting rule and the argv split. The rule is
 * what lets two servers be compared on numbers anyone can reproduce, so every
 * clause of it in docs/verify.md has a case here.
 */
import { describe, expect, it } from "vitest";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { countToolActions, enumeratedValues, estimateTokens, formatSurface, measureSurface } from "../../src/cli/surface-measure.js";
import { parseSurfaceArgs } from "../../src/cli/surface-cli.js";

const obj = (properties: Record<string, unknown>) => ({ type: "object", properties });

describe("countToolActions", () => {
  it("counts one per enum value of an action parameter", () => {
    expect(countToolActions(obj({ action: { type: "string", enum: ["a", "b", "c"] } }))).toEqual({ actionParam: "action", actions: 3 });
  });

  it("reads operation and command too, in that order after action", () => {
    expect(countToolActions(obj({ operation: { enum: ["x", "y"] } }))).toEqual({ actionParam: "operation", actions: 2 });
    expect(countToolActions(obj({ command: { enum: ["x"] } }))).toEqual({ actionParam: "command", actions: 1 });
    expect(countToolActions(obj({ command: { enum: ["x"] }, action: { enum: ["p", "q"] } }))).toEqual({ actionParam: "action", actions: 2 });
  });

  it("falls through to the next name when the first has no values", () => {
    expect(countToolActions(obj({ action: { type: "string" }, operation: { enum: ["x", "y"] } }))).toEqual({ actionParam: "operation", actions: 2 });
  });

  it("unions enum and const across anyOf and oneOf branches, once each", () => {
    const schema = obj({ action: { anyOf: [{ enum: ["a", "b"] }, { type: "string" }, { const: "c" }, { enum: ["a"] }] } });
    expect(countToolActions(schema)).toEqual({ actionParam: "action", actions: 3 });
    expect(enumeratedValues({ oneOf: [{ const: "x" }, { const: "y" }] })).toEqual(["x", "y"]);
  });

  it("counts a tool with no enumerated action parameter as one", () => {
    expect(countToolActions(obj({ path: { type: "string" } }))).toEqual({ actionParam: null, actions: 1 });
    expect(countToolActions(obj({ action: { type: "string" } }))).toEqual({ actionParam: null, actions: 1 });
    expect(countToolActions(undefined)).toEqual({ actionParam: null, actions: 1 });
    // A nested action enum is not top level, so it does not count.
    expect(countToolActions(obj({ args: obj({ action: { enum: ["a", "b"] } }) }))).toEqual({ actionParam: null, actions: 1 });
  });
});

describe("measureSurface", () => {
  it("sums tools, actions and compact JSON sizes", () => {
    const tools = [
      { name: "one", description: "d", inputSchema: obj({ action: { enum: ["a", "b"] } }) },
      { name: "two", inputSchema: obj({}) },
    ];
    const r = measureSurface(tools, { server: { name: "s", version: "1" }, protocolVersion: "p", instructions: "hi" });
    expect(r.tools).toBe(2);
    expect(r.actions).toBe(3);
    const schemas = JSON.stringify(tools.map((t) => t.inputSchema));
    expect(r.schemaBytes).toBe(Buffer.byteLength(schemas));
    expect(r.schemaTokens).toBe(Math.ceil(schemas.length / 4));
    expect(r.toolsListBytes).toBe(Buffer.byteLength(JSON.stringify(tools)));
    expect(r.instructionsBytes).toBe(2);
    expect(r.perTool.map((t) => t.actions)).toEqual([2, 1]);
    expect(formatSurface(r)).toContain("actions        3");
    expect(formatSurface(r, { perTool: true })).toMatch(/one\s+2\s+action/);
  });

  it("estimates tokens as characters over four, rounded up", () => {
    expect(estimateTokens("")).toBe(0);
    expect(estimateTokens("abcde")).toBe(2);
    expect(estimateTokens(8)).toBe(2);
  });

  it("reproduces the advertised counts from the editor-down golden recording", () => {
    const golden = JSON.parse(readFileSync(join(dirname(fileURLToPath(import.meta.url)), "..", "golden", "editor-down.json"), "utf8")) as {
      tools: Array<{ name: string; description: string; inputSchema: unknown }>;
    };
    const r = measureSurface(golden.tools);
    expect(r.tools).toBe(golden.tools.length);
    // Every category tool carries an action enum, so each counts more than one.
    expect(r.perTool.every((t) => t.actionParam === "action" && t.actions > 1)).toBe(true);
    expect(r.actions).toBeGreaterThan(3000);
  });
});

describe("parseSurfaceArgs", () => {
  it("takes the command after --, leaving its flags alone", () => {
    const a = parseSurfaceArgs(["--json", "--", "npx", "-y", "some-server", "--json"]);
    expect(a).toMatchObject({ json: true, command: ["npx", "-y", "some-server", "--json"], timeoutMs: 180_000 });
  });

  it("takes the first bare word as the command when there is no separator", () => {
    expect(parseSurfaceArgs(["--timeout", "30", "node", "server.js"])).toMatchObject({ command: ["node", "server.js"], timeoutMs: 30_000 });
  });

  it("collects repeated --env pairs", () => {
    expect(parseSurfaceArgs(["--env", "A=1", "--env=B=x=y", "--", "srv"])).toMatchObject({ env: { A: "1", B: "x=y" } });
  });

  it("accepts --url alone and refuses it beside a command", () => {
    expect(parseSurfaceArgs(["--url", "http://127.0.0.1:9/mcp"])).toMatchObject({ url: "http://127.0.0.1:9/mcp", command: [] });
    expect(parseSurfaceArgs(["--url", "http://x/mcp", "--", "srv"])).toHaveProperty("error");
  });

  it("refuses a missing target, a bad --env, a bad --timeout and a bad URL", () => {
    expect(parseSurfaceArgs([])).toHaveProperty("error");
    expect(parseSurfaceArgs(["--env", "NOEQUALS", "--", "srv"])).toHaveProperty("error");
    expect(parseSurfaceArgs(["--timeout", "soon", "--", "srv"])).toHaveProperty("error");
    expect(parseSurfaceArgs(["--url", "not a url"])).toHaveProperty("error");
  });
});
