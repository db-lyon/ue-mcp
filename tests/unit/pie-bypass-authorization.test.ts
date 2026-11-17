/**
 * The Blueprint-error bypass names who authorized it, and the bridge refuses a
 * source it does not recognize. The recognized sources are declared once, as the
 * enum on pie_start_ignoring_blueprint_errors' authorizationSource in C++; the
 * server's editor(play_in_editor_ignore_blueprint_errors) handler may only send
 * one of them.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import type { HandlerSpecs } from "../../src/surface/handler-spec.js";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const RECORDING = JSON.parse(fs.readFileSync(path.join(ROOT, "tests", "golden", "handler-specs.json"), "utf8")) as {
  handlers: HandlerSpecs;
};
const EDITOR_TOOL = fs.readFileSync(path.join(ROOT, "src", "tools", "editor.ts"), "utf8");

describe("the Blueprint-error bypass authorization", () => {
  const declared = RECORDING.handlers.pie_start_ignoring_blueprint_errors?.params
    .find((p) => p.name === "authorizationSource")?.enum ?? [];

  it("is declared as an enum by the bridge", () => {
    expect(declared.length).toBeGreaterThan(0);
  });

  it("is only ever sent as a source the bridge declares", () => {
    const sent = [...EDITOR_TOOL.matchAll(/authorizationSource\s*=\s*"([a-z_]+)"/g)].map((m) => m[1]);
    expect(sent.length).toBeGreaterThan(0);
    for (const source of sent) expect(declared, source).toContain(source);
  });
});
