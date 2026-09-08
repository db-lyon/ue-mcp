/**
 * The effect declaration, and the gates that read it.
 *
 * Every action states what it does to the addressed editor. The type requires
 * it, so an action that states nothing does not compile; these cover what the
 * type cannot say. That every value is one of the three and every one of them
 * is a person's answer rather than a guess. That the two kinds are really two,
 * so an action cannot carry both a bridge method and a handler. That the three
 * gates which used to hold three private opinions now hold none.
 *
 * The failure this replaces: a guard declared `scope: mutations` matched 542 of
 * 1090 actions, because the answer came from matching an action's NAME against
 * a list of verbs and the list was missing `write_cpp_file`, `build`, `sculpt`,
 * `place_actor` and every bare verb like `save` and `create`.
 */
import { describe, it, expect } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import { ALL_TOOLS } from "../../src/tools.js";
import {
  RAW_BRIDGE_METHODS,
  UNDECLARED_EFFECT,
  actionEffect,
  bridgeMethodEffect,
  declaredActionEffect,
  mayChangeState,
} from "../../src/action-effects.js";
import { classifyAction } from "../../src/locking.js";
import { classifyWrite } from "../../src/flow/write-methods.js";
import { refuseUntargetedCall } from "../../src/editor-gate.js";
import type { ActionEffect } from "../../src/types.js";

const EFFECTS: ActionEffect[] = ["read", "mutate", "unknown"];

function everyAction(): Array<{ tool: string; action: string; spec: (typeof ALL_TOOLS)[number]["actions"][string] }> {
  const out = [];
  for (const t of ALL_TOOLS) {
    for (const [action, spec] of Object.entries(t.actions)) out.push({ tool: t.name, action, spec });
  }
  return out;
}

describe("every action declares its effect", () => {
  it("states one of the three, on every action in ALL_TOOLS", () => {
    const missing = everyAction().filter((a) => !EFFECTS.includes(a.spec.effect));
    expect(
      missing.map((a) => `${a.tool}.${a.action}`),
      "an action with no effect cannot be gated, so it cannot be declared",
    ).toEqual([]);
    // The surface, so a change that empties it is visible rather than vacuous.
    expect(everyAction().length).toBeGreaterThan(1000);
  });

  it("states it itself, rather than inheriting a guess", () => {
    // `inferred` is reserved for actions this package does not declare: Epic's
    // wrapped engine tools, read out of a live registry, and a plugin action
    // whose manifest stayed silent. Nothing in the pristine declaration is
    // allowed to be one.
    const guessed = everyAction().filter((a) => a.spec.effectSource === "inferred");
    expect(guessed.map((a) => `${a.tool}.${a.action}`)).toEqual([]);
  });

  it("is exactly one kind, never both and never neither", () => {
    for (const { tool, action, spec } of everyAction()) {
      const key = `${tool}.${action}`;
      expect(["bridge", "handler", "registry"], key).toContain(spec.kind);
      if (spec.kind === "bridge") {
        expect(typeof spec.bridge, key).toBe("string");
        expect(spec.handler, key).toBeUndefined();
      }
      if (spec.kind === "handler") {
        expect(typeof spec.handler, key).toBe("function");
        expect(spec.bridge, key).toBeUndefined();
      }
    }
  });

  it("keeps the surface majority write-shaped", () => {
    // A declaration pass that suddenly calls most of the surface `read` has
    // been done carelessly, not well.
    const all = everyAction();
    const mutating = all.filter((a) => a.spec.effect !== "read");
    expect(mutating.length).toBeGreaterThan(all.length / 2);
  });

  it("answers for the actions a name would have got wrong", () => {
    // Each of these was classified by a verb the name happens to contain, and
    // each was wrong in the direction that verb pointed.
    const cases: Array<[string, string, ActionEffect]> = [
      // "post" and "edit" are mutating verbs. Both of these only read.
      ["level", "get_post_process_settings", "read"],
      ["level", "get_current_edit_level", "read"],
      // "bulk" is a mutating verb because most bulk_* actions write.
      ["asset", "bulk_read_properties", "read"],
      ["level", "bulk_line_trace", "read"],
      // No verb list had "unwrap", "fixup" or "mesh", so all three read as
      // unclassified and none of them was ever locked or checked out.
      ["asset", "unwrap_uvs", "mutate"],
      ["asset", "fixup_redirectors", "mutate"],
      ["asset", "mesh_boolean", "mutate"],
      // A bare verb, which a rule anchored on a trailing underscore missed.
      ["asset", "save", "mutate"],
      ["project", "build", "mutate"],
      // Writes a file under the project's Source tree.
      ["project", "write_cpp_file", "mutate"],
      // Decided by a parameter, which is what `unknown` is for.
      ["epic", "call_tool", "unknown"],
      ["editor", "execute_python", "unknown"],
    ];
    for (const [tool, action, effect] of cases) {
      expect(declaredActionEffect(tool, action), `${tool}.${action}`).toBe(effect);
    }
  });
});

describe("what a name nobody claims is treated as", () => {
  it("is a mutation, not the verb lexicon's opinion of it", () => {
    expect(UNDECLARED_EFFECT).toBe("mutate");
    // Read-looking and write-looking alike: nothing in this server carries
    // either name, so neither is vouched for.
    expect(actionEffect("someplugin", "list_widgets")).toEqual({ effect: "mutate", source: "undeclared" });
    expect(actionEffect("someplugin", "frobnicate")).toEqual({ effect: "mutate", source: "undeclared" });
    expect(bridgeMethodEffect("vendor_get_thing")).toEqual({ effect: "mutate", source: "undeclared" });
  });

  it("gates as a change everywhere", () => {
    expect(mayChangeState("mutate")).toBe(true);
    expect(mayChangeState("unknown")).toBe(true);
    expect(mayChangeState("read")).toBe(false);
    const TWO = { editors: ["a", "b"], activeEditor: "a", targetParam: "editor" };
    expect(refuseUntargetedCall({ taskName: "someplugin.list_widgets", ...TWO })).toBeTruthy();
  });
});

describe("bridge methods", () => {
  it("takes the effect of the actions that forward to it", () => {
    expect(bridgeMethodEffect("read_asset").effect).toBe("read");
    expect(bridgeMethodEffect("save_asset").effect).toBe("mutate");
  });

  it("calls a method reached by actions that disagree `unknown`", () => {
    // Every wrapped Epic tool dispatches through `epic_call_tool`, and between
    // them they read and write, so the METHOD has no effect of its own. Only
    // reachable once enrichment has run, so it is asserted on the shape rather
    // than on a live catalog: two declarations that disagree collapse.
    const conflicting = ALL_TOOLS.flatMap((t) =>
      Object.values(t.actions).filter((s) => s.kind === "bridge").map((s) => s.bridge),
    );
    const seen = new Map<string, Set<ActionEffect>>();
    for (const t of ALL_TOOLS) {
      for (const spec of Object.values(t.actions)) {
        if (spec.kind !== "bridge") continue;
        if (!seen.has(spec.bridge)) seen.set(spec.bridge, new Set());
        seen.get(spec.bridge)!.add(spec.effect);
      }
    }
    expect(conflicting.length).toBeGreaterThan(1000);
    for (const [method, effects] of seen) {
      if (effects.size > 1) expect(bridgeMethodEffect(method).effect, method).toBe("unknown");
    }
  });

  it("enumerates every method a handler calls directly", () => {
    // The one gap an index of declared actions cannot close: a handler running
    // bridge calls of its own. Two of them are reads, and defaulting those to
    // `mutate` would put asset(search) and editor(get_engine_state) in front of
    // every guard scoped to mutations for no reason. So they are listed, and
    // this fails when the source grows one that is not.
    const src = path.join(import.meta.dirname, "..", "..", "src");
    const found = new Set<string>();
    const walk = (dir: string): void => {
      for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, entry.name);
        if (entry.isDirectory()) { walk(full); continue; }
        if (!entry.name.endsWith(".ts")) continue;
        const text = fs.readFileSync(full, "utf8");
        for (const m of text.matchAll(/bridge\.call\(\s*"([a-z_][a-z0-9_]*)"/g)) found.add(m[1]);
      }
    };
    walk(src);

    const declaredSomewhere = (method: string): boolean =>
      method in RAW_BRIDGE_METHODS
      || ALL_TOOLS.some((t) =>
        Object.values(t.actions).some((s) => s.kind === "bridge" && s.bridge === method));

    const unlisted = [...found].filter((m) => !declaredSomewhere(m)).sort();
    expect(
      unlisted,
      "a handler calls these directly and nothing says what they do. Add each to "
      + "RAW_BRIDGE_METHODS in src/action-effects.ts with its effect, or they are "
      + "treated as mutations and a read among them is guarded for nothing.",
    ).toEqual([]);
  });
});

describe("the three gates agree, because they read one answer", () => {
  it("routing, locking and source control never contradict a declared read", () => {
    const TWO = { editors: ["a", "b"], activeEditor: "a", targetParam: "editor" };
    const disagreements: string[] = [];

    for (const { tool, action, spec } of everyAction()) {
      if (spec.effect !== "read") continue;
      const key = `${tool}.${action}`;
      // The routing gate lets a read run untargeted.
      if (refuseUntargetedCall({ taskName: key, ...TWO }) !== null) {
        disagreements.push(`${key}: routing gate refuses a declared read`);
      }
      // Locking never takes a lock for one.
      if (classifyAction(key, { assetPath: "/Game/Foo" }).mutates) {
        disagreements.push(`${key}: locking treats a declared read as a write`);
      }
      // Source control never checks a file out for one.
      if (spec.kind === "bridge" && classifyWrite(spec.bridge, { assetPath: "/Game/Foo" }).writes) {
        // Only when this method is a read on every action that reaches it.
        if (bridgeMethodEffect(spec.bridge).effect === "read") {
          disagreements.push(`${key}: source control checks out for a declared read`);
        }
      }
    }

    expect(disagreements).toEqual([]);
  });

  it("routing and locking never contradict a declared mutation", () => {
    // Two deliberate exceptions, and both say so out loud now rather than
    // being an omission from a verb list or a class override that made the
    // declaration lie to every other consumer.
    const NEVER_LOCKED = new Set(["asset.lock", "asset.unlock", "asset.unlock_all", "asset.list_locks"]);
    const ADDRESSES_THE_SERVER = new Set([
      "project.list_editors", "project.use_editor", "project.add_editor", "project.drop_editor",
    ]);
    const disagreements: string[] = [];

    for (const { tool, action, spec } of everyAction()) {
      if (spec.effect === "read") continue;
      const key = `${tool}.${action}`;
      if (!ADDRESSES_THE_SERVER.has(key)
        && refuseUntargetedCall({ taskName: key, ...TWO_EDITORS }) === null) {
        disagreements.push(`${key}: routing gate lets a declared change run untargeted`);
      }
      if (NEVER_LOCKED.has(key)) continue;
      if (!classifyAction(key, { assetPath: "/Game/Foo" }).mutates) {
        disagreements.push(`${key}: locking does not treat a declared change as one`);
      }
    }

    expect(disagreements).toEqual([]);
  });
});

const TWO_EDITORS = { editors: ["a", "b"], activeEditor: "a", targetParam: "editor" };
