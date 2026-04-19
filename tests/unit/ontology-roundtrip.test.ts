/**
 * Cross-projector round-trip regression: every projector's emitted
 * fragment must parse cleanly through our in-process parser. If this
 * ever fails we have broken the emitter/parser contract and anything
 * downstream (composer, selectors, MCP query surface) is unreliable.
 */

import { describe, it, expect } from "vitest";
import { categoryTool, bp, type ToolDef } from "../../src/types.js";
import {
  createHandlerRegistryProjector,
  createWorkaroundProjector,
  createPluginProjector,
  createEngineSymbolProjector,
  createInvocationProjector,
  createProjectConfigProjector,
  parseKant,
} from "../../src/ontology/index.js";
import { serializeFragment } from "../../src/ontology/emit.js";
import type { WorkaroundEntry } from "../../src/workaround-tracker.js";
import type { InvocationEntry } from "../../src/invocation-tracker.js";

function fakeTools(): ToolDef[] {
  return [
    categoryTool(
      "gas",
      "GAS.",
      { create_ability: bp("Create", "create_ability") },
      undefined,
      undefined,
      { requires: ["GameplayAbilities"] },
    ),
    categoryTool("editor", "Editor.", {
      execute_python: {
        bridge: "execute_python",
        classification: "destructive",
        approval: "explicit",
        risk: "catastrophic",
        requires: ["PythonScriptPlugin"],
      },
    }),
  ];
}

function workarounds(): WorkaroundEntry[] {
  return [
    { code: "unreal.log('hi')", timestamp: "2026-04-19T10:00:00Z", resultSnippet: "ok" },
  ];
}

function invocations(): InvocationEntry[] {
  return [
    { sequence: 1, tool: "x", action: "y", status: "ok", durationMs: 1, timestamp: "t1" },
  ];
}

describe("ontology round-trip contract", () => {
  it("HandlerRegistryProjector output parses", () => {
    const proj = createHandlerRegistryProjector(fakeTools());
    const text = serializeFragment(proj.project(undefined as never));
    expect(() => parseKant(text, "handler-registry")).not.toThrow();
  });

  it("WorkaroundProjector output parses", () => {
    const proj = createWorkaroundProjector(workarounds);
    const text = serializeFragment(proj.project(undefined as never));
    expect(() => parseKant(text, "workarounds")).not.toThrow();
  });

  it("PluginProjector output parses (null engine root)", () => {
    const proj = createPluginProjector();
    const text = serializeFragment(proj.project({ engineRoot: null, projectDir: null }));
    expect(() => parseKant(text, "plugins")).not.toThrow();
  });

  it("EngineSymbolProjector output parses (null engine root)", () => {
    const proj = createEngineSymbolProjector();
    const text = serializeFragment(proj.project({ engineRoot: null }));
    expect(() => parseKant(text, "engine-symbols")).not.toThrow();
  });

  it("InvocationProjector output parses", () => {
    const proj = createInvocationProjector(invocations);
    const text = serializeFragment(proj.project(undefined as never));
    expect(() => parseKant(text, "invocations")).not.toThrow();
  });

  it("ProjectConfigProjector output parses (no project)", () => {
    const proj = createProjectConfigProjector();
    const text = serializeFragment(proj.project({ projectDir: null, projectPath: null }));
    expect(() => parseKant(text, "project-config")).not.toThrow();
  });
});
