/**
 * Guards, built from what somebody declared.
 *
 * A guard used to be a task whose NAME encoded everything: `guard.sandbox.
 * beforeWrite` meant a guard called sandbox, running before, applying only to
 * writes. Ordering was unreachable, a guard wanting both hooks was two tasks
 * that did not know they were related, and a misspelling produced an ordinary
 * task nobody ever called.
 *
 * These cover the behaviour that mattered under that scheme, plus what the
 * declaration can now express that a name could not.
 */
import { describe, it, expect, beforeEach } from "vitest";
import { BaseTask, TaskRegistry, type TaskResult, type TaskConstructor } from "@db-lyon/flowkit";
import { assertNoLegacyGuardTasks, buildGuards } from "../../src/flow/guards.js";
import { GuardsSchema } from "../../src/flow/guard-schema.js";
import { GuardRegistry, makeCallContext, type CallContext } from "../../src/flow/guard.js";
import { GuardedBridge } from "../../src/flow/guarded-bridge.js";
import { ErrorCode, McpError } from "../../src/errors.js";
import type { IBridge } from "../../src/bridge.js";
import type { ToolContext } from "../../src/types.js";

const target = { projectPath: null, port: 0, portSource: "default" as const, verified: true };

function fakeBridge(result: unknown = { ok: true }) {
  const calls: string[] = [];
  const bridge: IBridge & { calls: string[] } = {
    calls,
    isConnected: true,
    connect: async () => {},
    retargetProject: () => target,
    getTarget: () => target,
    call: async (method: string) => {
      calls.push(method);
      return result;
    },
  };
  return bridge;
}

/** Every /Game/* path exists on disk; anything else is a not-yet-created asset. */
const resolveExisting = (cp: string) =>
  cp.startsWith("/Game/") ? `C:/proj/Content/${cp.slice(6)}.uasset` : null;

const ctx = { bridge: fakeBridge(), project: {} } as unknown as ToolContext;

/** What each hook was handed, so the option shape can be asserted. */
let seen: Array<Record<string, unknown>>;
beforeEach(() => {
  seen = [];
});

function hookTask(outcome: TaskResult): TaskConstructor {
  class Stub extends BaseTask {
    get taskName() {
      return "hook-stub";
    }
    async execute(): Promise<TaskResult> {
      seen.push(this.options as Record<string, unknown>);
      return outcome;
    }
  }
  return Stub as unknown as TaskConstructor;
}

const ALLOW: TaskResult = { success: true };
const DENY: TaskResult = { success: false, error: new Error("checked out by alice") };

function registryWith(entries: Record<string, TaskResult>): TaskRegistry {
  const reg = new TaskRegistry();
  for (const [name, outcome] of Object.entries(entries)) reg.register(name, hookTask(outcome));
  return reg;
}

function callCtx(method: string, params: Record<string, unknown> = {}): CallContext {
  return makeCallContext(method, params, undefined, fakeBridge(), resolveExisting);
}

const deps = (registry: TaskRegistry) => ({ registry, ctx, rawBridge: fakeBridge() });
const SOURCE = { label: "ue-mcp.yml" };

/** Parse as the loader would, so tests exercise the real defaults. */
const declare = (raw: unknown) => GuardsSchema.parse(raw);

describe("a declaration becomes a guard", () => {
  it("builds one guard per declaration, whatever its hooks", async () => {
    const registry = registryWith({ check: ALLOW, audit: ALLOW });
    const guards = await buildGuards(
      declare({
        policy: { before: { class_path: "check" } },
        trail: { after: { class_path: "audit" } },
        both: { before: { class_path: "check" }, after: { class_path: "audit" } },
      }),
      deps(registry),
      SOURCE,
    );

    expect(guards.map((g) => g.name)).toEqual(["policy", "trail", "both"]);
    // The name a guard could not carry before: one guard, both hooks.
    const both = guards.find((g) => g.name === "both")!;
    expect(both.before).toBeTypeOf("function");
    expect(both.after).toBeTypeOf("function");
  });

  it("carries the order the declaration asked for", async () => {
    const registry = registryWith({ check: ALLOW });
    const guards = await buildGuards(
      declare({
        late: { order: 50, before: { class_path: "check" } },
        early: { order: -10, before: { class_path: "check" } },
      }),
      deps(registry),
      SOURCE,
    );
    expect(guards.map((g) => [g.name, g.order])).toEqual([
      ["late", 50],
      ["early", -10],
    ]);
  });

  it("defaults to running on every call", async () => {
    const registry = registryWith({ check: ALLOW });
    const [guard] = await buildGuards(
      declare({ policy: { before: { class_path: "check" } } }),
      deps(registry),
      SOURCE,
    );
    expect(guard.appliesTo, "an unscoped guard should see every call").toBeUndefined();
  });

  it("binds the writes scope to the bridge's own write classification", async () => {
    const registry = registryWith({ check: ALLOW });
    const [guard] = await buildGuards(
      declare({ policy: { scope: "writes", before: { class_path: "check" } } }),
      deps(registry),
      SOURCE,
    );
    // A declared read is not a write.
    expect(await guard.appliesTo!(callCtx("read_asset", { assetPath: "/Game/Foo" }))).toBe(false);
    // A write to a path that does not exist yet creates rather than modifies.
    expect(await guard.appliesTo!(callCtx("save_asset", { assetPath: "/Other/New" }))).toBe(false);
    // A write to an existing asset is what the scope claims.
    expect(await guard.appliesTo!(callCtx("save_asset", { assetPath: "/Game/Foo" }))).toBe(true);
  });
});

describe("standing in front of every mutation", () => {
  it("sees a mutation that names no asset, which the writes scope cannot", async () => {
    const registry = registryWith({ check: ALLOW });
    const [mutations] = await buildGuards(
      declare({ freeze: { scope: "mutations", before: { class_path: "check" } } }),
      deps(registry),
      SOURCE,
    );
    const [writes] = await buildGuards(
      declare({ p4: { scope: "writes", before: { class_path: "check" } } }),
      deps(registry),
      SOURCE,
    );

    // Spawning an actor changes the world and writes no content path, so it is
    // invisible to the source-control question and squarely inside "block
    // everything that mutates".
    const spawn = callCtx("spawn_actor", { classPath: "/Script/Engine.StaticMeshActor" });
    expect(await mutations.appliesTo!(spawn)).toBe(true);
    expect(await writes.appliesTo!(spawn)).toBe(false);
  });

  it("sees a create, which modifies nothing that exists yet", async () => {
    const registry = registryWith({ check: ALLOW });
    const [mutations] = await buildGuards(
      declare({ freeze: { scope: "mutations", before: { class_path: "check" } } }),
      deps(registry),
      SOURCE,
    );
    const [writes] = await buildGuards(
      declare({ p4: { scope: "writes", before: { class_path: "check" } } }),
      deps(registry),
      SOURCE,
    );

    const create = callCtx("save_asset", { assetPath: "/Other/BrandNew" });
    expect(await mutations.appliesTo!(create)).toBe(true);
    expect(await writes.appliesTo!(create), "a create is not a modification").toBe(false);
  });

  it("leaves reads alone", async () => {
    const registry = registryWith({ check: ALLOW });
    const [guard] = await buildGuards(
      declare({ freeze: { scope: "mutations", before: { class_path: "check" } } }),
      deps(registry),
      SOURCE,
    );

    // Real bridge methods whose actions declare read. The names matter now:
    // the verdict is what the action forwarding to each one declared, so a
    // method nothing declares is not a read however it is spelled.
    for (const read of ["read_asset", "list_assets", "get_world_outliner", "get_actor_details"]) {
      expect(await guard.appliesTo!(callCtx(read, {})), read).toBe(false);
    }
  });

  it("covers the mutations a user would expect it to", async () => {
    const registry = registryWith({ check: ALLOW });
    const [guard] = await buildGuards(
      declare({ freeze: { scope: "mutations", before: { class_path: "check" } } }),
      deps(registry),
      SOURCE,
    );

    for (const m of [
      // Named-asset writes.
      "save_asset", "delete_asset", "import_fbx", "compile_blueprint",
      // Mutations that name no asset, which the writes scope cannot see.
      "spawn_actor", "destroy_actor", "set_actor_property", "start_pie",
      "stop_editor", "execute_python", "respond_to_dialog",
      // Bare verbs. A rule matching a trailing underscore missed every one.
      "save", "create", "compile", "duplicate", "load", "sculpt",
      // Verbs a hand-written list did not think of.
      "write_cpp_file", "write_source_file", "build", "use_editor",
      "drop_editor", "bulk_rename", "batch_translate", "edit_user_defined_enum",
      "reorder_enum_values", "metasound_author", "place_actor", "unlock",
      "force_reload", "update_datatable_row", "reimport", "request_editor_shutdown",
    ]) {
      expect(await guard.appliesTo!(callCtx(m, {})), m).toBe(true);
    }
  });

  it("fails closed on a verb nobody has seen before", async () => {
    // The point of the scope is to be exhaustive. Listing mutating verbs was
    // the wrong way round: the list is open-ended, and every verb missing from
    // it was a mutation nobody guarded.
    const registry = registryWith({ check: ALLOW });
    const [guard] = await buildGuards(
      declare({ freeze: { scope: "mutations", before: { class_path: "check" } } }),
      deps(registry),
      SOURCE,
    );

    for (const m of ["frobnicate_widget", "teleport_pawn", "zzz_unknown_operation"]) {
      expect(await guard.appliesTo!(callCtx(m, {})), m).toBe(true);
    }
  });

  it("fails closed on a method that only LOOKS like a read", async () => {
    // The half the read-verb rule could not do. Inverting the guess closed the
    // direction where a mutating verb was missing from a list, and left this
    // one wide open: a method nothing in this server declares was waved
    // through on the strength of its first word. A plugin calling the bridge
    // with a method of its own, or a flow step naming one, is exactly that
    // case, and "get" at the front of it vouches for nothing.
    const registry = registryWith({ check: ALLOW });
    const [guard] = await buildGuards(
      declare({ freeze: { scope: "mutations", before: { class_path: "check" } } }),
      deps(registry),
      SOURCE,
    );

    for (const m of ["get_something_nobody_declared", "list_vendor_widgets", "read_remote_state"]) {
      expect(await guard.appliesTo!(callCtx(m, {})), m).toBe(true);
    }
  });

  it("scopes to the reads alone, which is the complement", async () => {
    const registry = registryWith({ check: ALLOW });
    const [guard] = await buildGuards(
      declare({ audit: { scope: "reads", before: { class_path: "check" } } }),
      deps(registry),
      SOURCE,
    );

    expect(await guard.appliesTo!(callCtx("read_asset", {}))).toBe(true);
    expect(await guard.appliesTo!(callCtx("get_world_outliner", {}))).toBe(true);
    expect(await guard.appliesTo!(callCtx("save_asset", {}))).toBe(false);
    expect(await guard.appliesTo!(callCtx("spawn_actor", {}))).toBe(false);
    // A method nothing declares counts as a change, so it is NOT a read. This
    // is the direction that matters: a mutation slipping into this scope is
    // one the guard was told to ignore.
    expect(await guard.appliesTo!(callCtx("vendor_get_thing", {}))).toBe(false);
    // `unknown` is not a read either.
    expect(await guard.appliesTo!(callCtx("execute_python", {}))).toBe(false);
  });

  it("scopes to the escape hatches alone, which no verb list could name", async () => {
    const registry = registryWith({ check: ALLOW });
    const [guard] = await buildGuards(
      declare({ approve: { scope: "unknown", before: { class_path: "check" } } }),
      deps(registry),
      SOURCE,
    );

    // Arbitrary code, arbitrary console commands, a wrapped third-party tool,
    // and reflected invocation. What they do is decided by an argument.
    for (const m of [
      "execute_python", "execute_command", "epic_call_tool",
      "invoke_object_function", "invoke_function", "invoke_static_function",
    ]) {
      expect(await guard.appliesTo!(callCtx(m, {})), m).toBe(true);
    }
    // An ordinary edit is not an escape hatch, which is the point: this scope
    // exists so a project can gate the calls that can do anything without
    // gating the ones that do a known thing.
    expect(await guard.appliesTo!(callCtx("save_asset", {}))).toBe(false);
    expect(await guard.appliesTo!(callCtx("spawn_actor", {}))).toBe(false);
    expect(await guard.appliesTo!(callCtx("read_asset", {}))).toBe(false);
    // Nor is a method nobody declares: unrecognised is a change, not an
    // unknowable. Conflating the two would put every plugin's own bridge call
    // in front of a human-approval gate.
    expect(await guard.appliesTo!(callCtx("vendor_frobnicate", {}))).toBe(false);
  });

  it("keeps the escape hatches inside the mutations scope too", async () => {
    // `unknown` gates as a change everywhere, so a project that guards
    // mutations does not have to also remember to guard unknown.
    const registry = registryWith({ check: ALLOW });
    const [guard] = await buildGuards(
      declare({ freeze: { scope: "mutations", before: { class_path: "check" } } }),
      deps(registry),
      SOURCE,
    );
    expect(await guard.appliesTo!(callCtx("execute_python", {}))).toBe(true);
    expect(await guard.appliesTo!(callCtx("epic_call_tool", {}))).toBe(true);
  });

  it("tells one wrapped engine tool from another, though they share a method", async () => {
    // All 830 wrapped tools dispatch through `epic_call_tool`, because the
    // plugin registers one reflective handler for Unreal's whole registry
    // rather than one per tool. Judging the method alone made every scope
    // wrong about 830 of the 1920 actions on this surface: `mutations` fired
    // on every wrapped read, `reads` matched none of them, and `unknown`
    // matched all of them, so a guard meant to put a person in front of
    // arbitrary code stopped `epic_list_attributes` too.
    const registry = registryWith({ check: ALLOW });
    const [mutations] = await buildGuards(
      declare({ freeze: { scope: "mutations", before: { class_path: "check" } } }), deps(registry), SOURCE,
    );
    const [reads] = await buildGuards(
      declare({ audit: { scope: "reads", before: { class_path: "check" } } }), deps(registry), SOURCE,
    );
    const [hatches] = await buildGuards(
      declare({ approve: { scope: "unknown", before: { class_path: "check" } } }), deps(registry), SOURCE,
    );
    const call = (tool: string) => callCtx("epic_call_tool", { toolset: "X", tool });

    const read = call("GASToolsets.AttributeSetToolset.ListAttributes");
    expect(await mutations.appliesTo!(read)).toBe(false);
    expect(await reads.appliesTo!(read)).toBe(true);
    expect(await hatches.appliesTo!(read), "a wrapped read is not an escape hatch").toBe(false);

    const write = call("UMGToolSet.UMGToolSet.AddWidget");
    expect(await mutations.appliesTo!(write)).toBe(true);
    expect(await reads.appliesTo!(write)).toBe(false);
    expect(await hatches.appliesTo!(write)).toBe(false);

    // Arbitrary code stays unknown: its deciding argument is a program.
    const python = callCtx("execute_python", { code: "x" });
    expect(await hatches.appliesTo!(python)).toBe(true);

    // A tool name this build does not carry falls back to the method, which
    // has no single effect, so it is treated as a change.
    const stranger = call("SomeFutureToolset.SomeFutureToolset.Whatever");
    expect(await mutations.appliesTo!(stranger)).toBe(true);
    expect(await reads.appliesTo!(stranger)).toBe(false);
  });

  it("lets through the reads a HANDLER makes on its own", async () => {
    // These belong to no ActionSpec: a handler calls them directly, so the
    // index of declared actions cannot see them and the default above would
    // put both in front of every mutation guard. They are enumerated instead,
    // which is the whole reason that table exists.
    const registry = registryWith({ check: ALLOW });
    const [guard] = await buildGuards(
      declare({ freeze: { scope: "mutations", before: { class_path: "check" } } }),
      deps(registry),
      SOURCE,
    );

    expect(await guard.appliesTo!(callCtx("search_assets", {}))).toBe(false);
    expect(await guard.appliesTo!(callCtx("get_engine_state", {}))).toBe(false);
    // The mutating half of the same table still counts.
    expect(await guard.appliesTo!(callCtx("acquire_lock", {}))).toBe(true);
    expect(await guard.appliesTo!(callCtx("execute_python", {}))).toBe(true);
  });

  it("still lets a shaped read through", async () => {
    const registry = registryWith({ check: ALLOW });
    const [guard] = await buildGuards(
      declare({ freeze: { scope: "mutations", before: { class_path: "check" } } }),
      deps(registry),
      SOURCE,
    );

    // "bulk" describes the shape of a call, not what it does, and both of
    // these declare read. Under the old verb rule this worked by accident: the
    // rule looked past "bulk" to the next segment, so it happened to agree
    // here and would have been wrong for any shaped read whose second segment
    // was not itself a read verb.
    expect(await guard.appliesTo!(callCtx("bulk_read_asset_properties", {}))).toBe(false);
    expect(await guard.appliesTo!(callCtx("bulk_line_trace", {}))).toBe(false);
  });

  it("blocks one end to end, through the bridge a call actually takes", async () => {
    const registry = registryWith({ check: DENY });
    const guards = await buildGuards(
      declare({ freeze: { scope: "mutations", before: { class_path: "check" } } }),
      deps(registry),
      SOURCE,
    );
    const guardRegistry = new GuardRegistry();
    for (const g of guards) guardRegistry.register(g);

    const inner = fakeBridge();
    const bridge = new GuardedBridge(inner, guardRegistry, resolveExisting);

    await expect(bridge.call("spawn_actor", {})).rejects.toThrow(/blocked \(spawn_actor\)/);
    await expect(bridge.call("read_asset", { assetPath: "/Game/Foo" })).resolves.toEqual({ ok: true });
    expect(inner.calls, "only the read should have reached the editor").toEqual(["read_asset"]);
  });
});

describe("what a hook is told about the call", () => {
  it("hands it the method, the params and the resolved paths", async () => {
    const registry = registryWith({ check: ALLOW });
    const [guard] = await buildGuards(
      declare({ policy: { before: { class_path: "check" } } }),
      deps(registry),
      SOURCE,
    );

    await guard.before!(callCtx("save_asset", { assetPath: "/Game/Foo" }));

    expect(seen).toHaveLength(1);
    // The flattened keys are the contract a guard written against UeMcpTask
    // reads, and they have to keep their exact meaning. Asserted by inclusion
    // rather than by equality: `UeMcpGuard` reads two reserved keys alongside
    // them, and a guard that never heard of those is unaffected by their being
    // there.
    expect(seen[0]).toMatchObject({
      method: "save_asset",
      params: { assetPath: "/Game/Foo" },
      paths: ["C:/proj/Content/Foo.uasset"],
    });
  });

  it("adds the result for an after hook only", async () => {
    const registry = registryWith({ audit: ALLOW });
    const [guard] = await buildGuards(
      declare({ trail: { after: { class_path: "audit" } } }),
      deps(registry),
      SOURCE,
    );

    await guard.after!(callCtx("spawn_actor"), { spawned: 1 });

    expect(seen[0]).toMatchObject({ result: { spawned: 1 } });
  });

  it("hands it the declared options alongside the call", async () => {
    const registry = registryWith({ check: ALLOW });
    const [guard] = await buildGuards(
      declare({
        policy: { before: { class_path: "check", options: { allow: ["/Game/Sandbox/"] } } },
      }),
      deps(registry),
      SOURCE,
    );

    await guard.before!(callCtx("spawn_actor"));

    expect(seen[0]).toMatchObject({ allow: ["/Game/Sandbox/"], method: "spawn_actor" });
  });

  it("lets the call win a key the declaration also set", async () => {
    const registry = registryWith({ check: ALLOW });
    const [guard] = await buildGuards(
      declare({ policy: { before: { class_path: "check", options: { method: "stale" } } } }),
      deps(registry),
      SOURCE,
    );

    await guard.before!(callCtx("the_real_one"));

    expect(seen[0].method).toBe("the_real_one");
  });
});

describe("denying a call", () => {
  it("throws a WRITE_BLOCKED error naming the method and the files", async () => {
    const registry = registryWith({ check: DENY });
    const [guard] = await buildGuards(
      declare({ policy: { scope: "writes", before: { class_path: "check" } } }),
      deps(registry),
      SOURCE,
    );

    await expect(guard.before!(callCtx("save_asset", { assetPath: "/Game/Foo" })))
      .rejects.toMatchObject({ code: ErrorCode.WRITE_BLOCKED });
    await expect(guard.before!(callCtx("save_asset", { assetPath: "/Game/Foo" })))
      .rejects.toThrow(/save_asset.*Foo\.uasset.*checked out by alice/s);
  });

  it("omits the file list when the call touches nothing on disk", async () => {
    const registry = registryWith({ check: DENY });
    const [guard] = await buildGuards(
      declare({ policy: { before: { class_path: "check" } } }),
      deps(registry),
      SOURCE,
    );

    await expect(guard.before!(callCtx("list_assets"))).rejects.toThrow(/blocked \(list_assets\):/);
  });

  it("treats a hook that threw as a denial, because it did not approve", async () => {
    const reg = new TaskRegistry();
    reg.register(
      "explodes",
      class extends BaseTask {
        get taskName() {
          return "explodes";
        }
        async execute(): Promise<TaskResult> {
          throw new Error("policy service unreachable");
        }
      } as unknown as TaskConstructor,
    );
    const [guard] = await buildGuards(
      declare({ policy: { before: { class_path: "explodes" } } }),
      deps(reg),
      SOURCE,
    );

    await expect(guard.before!(callCtx("spawn_actor"))).rejects.toThrow(
      /guard 'policy' errored.*policy service unreachable/s,
    );
  });
});

describe("after a call that already happened", () => {
  it("does not fail it when the hook reports failure", async () => {
    const registry = registryWith({ audit: DENY });
    const [guard] = await buildGuards(
      declare({ trail: { after: { class_path: "audit" } } }),
      deps(registry),
      SOURCE,
    );

    await expect(guard.after!(callCtx("spawn_actor"), { ok: true })).resolves.toBeUndefined();
  });

  it("does not fail it when the hook throws", async () => {
    const reg = new TaskRegistry();
    reg.register(
      "explodes",
      class extends BaseTask {
        get taskName() {
          return "explodes";
        }
        async execute(): Promise<TaskResult> {
          throw new Error("audit sink down");
        }
      } as unknown as TaskConstructor,
    );
    const [guard] = await buildGuards(
      declare({ trail: { after: { class_path: "explodes" } } }),
      deps(reg),
      SOURCE,
    );

    await expect(guard.after!(callCtx("spawn_actor"), { ok: true })).resolves.toBeUndefined();
  });

  it("replaces the result when the hook returns data", async () => {
    const reg = new TaskRegistry();
    reg.register("redact", hookTask({ success: true, data: { redacted: true } }));
    const [guard] = await buildGuards(
      declare({ trail: { after: { class_path: "redact" } } }),
      deps(reg),
      SOURCE,
    );

    expect(await guard.after!(callCtx("list_assets"), { secret: 1 })).toEqual({ redacted: true });
  });
});

describe("a declaration that cannot work stops the server", () => {
  it("refuses a hook whose class_path resolves to nothing, naming the source", async () => {
    await expect(
      buildGuards(
        declare({ policy: { before: { class_path: "nope.not.here" } } }),
        deps(new TaskRegistry()),
        { label: "ue-mcp.yml" },
      ),
    ).rejects.toThrow(/ue-mcp\.yml.*guard 'policy'.*nope\.not\.here/s);
  });

  it("says why starting without it would be worse", async () => {
    await expect(
      buildGuards(
        declare({ policy: { after: { class_path: "nope" } } }),
        deps(new TaskRegistry()),
        SOURCE,
      ),
    ).rejects.toThrow(/ungated/i);
  });

  it("names the plugin when a plugin declared it", async () => {
    await expect(
      buildGuards(
        declare({ policy: { before: { class_path: "nope" } } }),
        deps(new TaskRegistry()),
        { label: "ue-mcp-perforce" },
      ),
    ).rejects.toThrow(/ue-mcp-perforce/);
  });
});

describe("the declaration schema", () => {
  it("refuses a guard with neither hook, which would never run", () => {
    expect(() => declare({ policy: { scope: "writes" } })).toThrow(/before hook, an after hook, or both/);
  });

  it("refuses a scope it does not have", () => {
    expect(() => declare({ policy: { scope: "beforeWrite", before: { class_path: "x" } } })).toThrow();
  });

  it("refuses a hook with no class_path", () => {
    expect(() => declare({ policy: { before: {} } })).toThrow();
  });

  it("refuses a name that could not identify a guard", () => {
    expect(() => declare({ "my guard": { before: { class_path: "x" } } })).toThrow(/guard name/i);
  });

  it("defaults scope to all and order to zero", () => {
    const parsed = declare({ policy: { before: { class_path: "x" } } });
    expect(parsed.policy).toMatchObject({ scope: "all", order: 0 });
  });
});

describe("through the bridge a call actually takes", () => {
  it("denies the write before it reaches the editor", async () => {
    const registry = registryWith({ check: DENY });
    const guards = await buildGuards(
      declare({ policy: { scope: "writes", before: { class_path: "check" } } }),
      deps(registry),
      SOURCE,
    );
    const guardRegistry = new GuardRegistry();
    for (const g of guards) guardRegistry.register(g);

    const inner = fakeBridge();
    const bridge = new GuardedBridge(inner, guardRegistry, resolveExisting);

    await expect(bridge.call("save_asset", { assetPath: "/Game/Foo" })).rejects.toThrow(McpError);
    expect(inner.calls, "the editor was asked despite the denial").toEqual([]);
  });

  it("lets a read through a write-scoped guard untouched", async () => {
    const registry = registryWith({ check: DENY });
    const guards = await buildGuards(
      declare({ policy: { scope: "writes", before: { class_path: "check" } } }),
      deps(registry),
      SOURCE,
    );
    const guardRegistry = new GuardRegistry();
    for (const g of guards) guardRegistry.register(g);

    const inner = fakeBridge();
    const bridge = new GuardedBridge(inner, guardRegistry, resolveExisting);

    await expect(bridge.call("read_asset", { assetPath: "/Game/Foo" })).resolves.toEqual({ ok: true });
    expect(inner.calls).toEqual(["read_asset"]);
  });
});

/**
 * The migration must not reintroduce the failure the declaration model exists
 * to prevent. A task still named like a guard is discovered by nothing, so it
 * would sit in a config gating nothing while looking configured.
 */
describe("a task still named like a guard is fatal", () => {
  it("refuses the old spelling and says what to write instead", () => {
    expect(() =>
      assertNoLegacyGuardTasks(["guard.sourcecontrol.beforeWrite", "deploy"], { label: "ue-mcp-perforce" }),
    ).toThrow(/guards:/);
  });

  it("names the file or plugin that declared it", () => {
    expect(() => assertNoLegacyGuardTasks(["guard.policy.before"], { label: "ue-mcp.yml" })).toThrow(
      /ue-mcp\.yml/,
    );
  });

  it("says what is at stake rather than only that it is wrong", () => {
    expect(() => assertNoLegacyGuardTasks(["guard.policy.before"], { label: "x" })).toThrow(/ungated/);
  });

  it("translates the phase into the scope and hook it meant", () => {
    let message = "";
    try {
      assertNoLegacyGuardTasks(["guard.sourcecontrol.beforeWrite"], { label: "x" });
    } catch (e) {
      message = (e as Error).message;
    }
    expect(message).toContain("scope: writes");
    expect(message).toContain("before:");

    let after = "";
    try {
      assertNoLegacyGuardTasks(["guard.audit.after"], { label: "x" });
    } catch (e) {
      after = (e as Error).message;
    }
    expect(after).toContain("scope: all");
    expect(after).toContain("after:");
  });

  it("says nothing about ordinary tasks", () => {
    expect(() =>
      assertNoLegacyGuardTasks(["deploy", "guardian", "guard_rail", "my.guard.helper"], { label: "x" }),
    ).not.toThrow();
  });
});
