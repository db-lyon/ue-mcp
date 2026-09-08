/**
 * Granting and diagnosis, against a real editor.
 *
 * These four actions call engine functions rather than writing properties, so
 * there is nothing to assert about them off a live ASC: GiveAbility, the
 * active-effect container, and CanActivateAbility only mean anything on a real
 * AbilitySystemComponent in a real world.
 *
 * The case that matters most is the tracer's answer BEFORE anything is set up.
 * An ability that was authored but never granted is the most common reason a
 * GAS setup does nothing, and it is silent: the engine logs nothing worth
 * reading and the ability simply never fires. What the tracer must never do is
 * what it did on its first run against a live editor, which is report
 * "wouldActivate: false" with an empty reason list.
 *
 * This works in the editor world rather than in PIE, deliberately. An actor
 * spawned into a world that has not begun play has an uninitialised ASC, which
 * is the state an agent authoring GAS content is actually in, and the state
 * the diagnostics have to explain rather than trip over.
 *
 * Everything is created under /Game/MCPGasLive and removed afterwards.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { LiveServer, resultJson } from "./server.js";
import { closeLiveBridges, liveTarget } from "./harness.js";

const target = await liveTarget();

const PACKAGE = "/Game/MCPGasLive";
const ABILITY = `${PACKAGE}/GA_LiveProbe`;
const ACTOR = "MCPGasLiveProbeActor";

let server: LiveServer;

const call = async (tool: string, args: Record<string, unknown>) =>
  server.call(tool, { ...args, timeoutMs: 300_000 });

beforeAll(async () => {
  server = await LiveServer.start({ projects: [target.uproject] });

  await call("gas", { action: "create_ability", name: "GA_LiveProbe", packagePath: PACKAGE });
  await call("level", { action: "place_actor", actorClass: "Actor", label: ACTOR });
  await call("level", {
    action: "add_component",
    actorLabel: ACTOR,
    componentClass: "AbilitySystemComponent",
    componentName: "ASC",
  });
}, 300_000);

afterAll(async () => {
  // Leave the project as it was found, whatever happened above.
  try {
    await call("gas", { action: "revoke_ability", actorLabel: ACTOR, abilityClass: ABILITY, world: "editor" });
    await call("level", { action: "delete_actor", actorLabel: ACTOR });
    await call("asset", { action: "delete_folder", path: PACKAGE, force: true });
  } catch {
    // Cleanup is best effort; a failure here must not mask a test failure.
  }
  await server?.close();
  closeLiveBridges();
});

interface Trace {
  granted: boolean;
  ascInitialized?: boolean;
  wouldActivate: boolean;
  blockedBy: string[];
  spec?: { handle: string; level: number; active: boolean };
}

const trace = async (): Promise<Trace> =>
  resultJson<Trace>(await call("gas", {
    action: "trace_ability_activation",
    actorLabel: ACTOR,
    abilityClass: ABILITY,
    world: "editor",
  }));

describe("trace_ability_activation", () => {
  it("never reports a refusal with no reason", async () => {
    // The invariant. Every other case here is a specific instance of it, and
    // the first live run of this action violated it.
    const body = await trace();
    if (!body.wouldActivate) expect(body.blockedBy.length).toBeGreaterThan(0);
  });

  it("says an ungranted ability is ungranted, and what to call", async () => {
    const body = await trace();
    expect(body.granted).toBe(false);
    expect(body.wouldActivate).toBe(false);
    expect(body.blockedBy.join(" ")).toContain("not granted");
    expect(body.blockedBy.join(" ")).toContain("grant_ability");
  });
});

describe("grant_ability", () => {
  it("grants, and returns the spec plus a rollback that undoes it", async () => {
    const body = resultJson<{
      created: boolean;
      spec: { handle: string; level: number };
      activatableCount: number;
      rollback: { method: string; payload: { abilityClass: string } };
    }>(await call("gas", { action: "grant_ability", actorLabel: ACTOR, abilityClass: ABILITY, world: "editor" }));

    expect(body.created).toBe(true);
    expect(body.spec.level).toBe(1);
    expect(body.activatableCount).toBeGreaterThan(0);
    expect(body.rollback.method).toBe("revoke_ability");
  });

  it("is idempotent, returning the same spec rather than a second one", async () => {
    // Two specs for what the caller thinks of as one ability is the failure
    // this guards: the second handle is invisible until something misbehaves.
    const body = resultJson<{ existed: boolean; created: boolean; spec: { handle: string } }>(
      await call("gas", { action: "grant_ability", actorLabel: ACTOR, abilityClass: ABILITY, world: "editor" }),
    );
    expect(body.existed).toBe(true);
    expect(body.created).toBe(false);
  });

  it("changes the tracer's answer from ungranted to granted", async () => {
    const body = await trace();
    expect(body.granted).toBe(true);
    expect(body.spec?.handle).toBeTruthy();
    expect(body.blockedBy.join(" ")).not.toContain("not granted");
  });

  it("explains an uninitialised ASC instead of refusing silently", async () => {
    // An actor in the editor world never ran InitAbilityActorInfo, so it
    // cannot activate. Saying which, and what to call, is the whole point.
    const body = await trace();
    expect(body.ascInitialized).toBe(false);
    expect(body.wouldActivate).toBe(false);
    expect(body.blockedBy.join(" ")).toContain("init_asc");
  });
});

describe("get_active_effects", () => {
  it("reports an empty effect list without erroring on a bare ASC", async () => {
    const body = resultJson<{ effectCount: number; activeEffects: unknown[]; ownedTags: unknown[] }>(
      await call("gas", { action: "get_active_effects", actorLabel: ACTOR, world: "editor" }),
    );
    expect(body.effectCount).toBe(0);
    expect(body.activeEffects).toEqual([]);
    expect(Array.isArray(body.ownedTags)).toBe(true);
  });
});

describe("revoke_ability", () => {
  it("revokes what was granted, then reports the replay as already done", async () => {
    const first = resultJson<{ revoked: number }>(
      await call("gas", { action: "revoke_ability", actorLabel: ACTOR, abilityClass: ABILITY, world: "editor" }),
    );
    expect(first.revoked).toBe(1);

    // A rollback has to be safe to replay, so the second call is a report
    // rather than a failure.
    const second = resultJson<{ revoked: number; alreadyRevoked: boolean }>(
      await call("gas", { action: "revoke_ability", actorLabel: ACTOR, abilityClass: ABILITY, world: "editor" }),
    );
    expect(second.revoked).toBe(0);
    expect(second.alreadyRevoked).toBe(true);
  });

  it("leaves the tracer saying ungranted again", async () => {
    const body = await trace();
    expect(body.granted).toBe(false);
    expect(body.blockedBy.join(" ")).toContain("not granted");
  });
});

describe("errors name the problem", () => {
  // The bridge reports a handler failure as {success:false, error} in the
  // payload rather than through the MCP isError flag, so that is what these
  // read. An error message is part of the deliverable here: a bad one costs
  // more agent turns than the action saves.
  interface Failure { success: boolean; error?: string }

  it("refuses an ability class that does not resolve, and says what it accepts", async () => {
    const body = resultJson<Failure>(await call("gas", {
      action: "grant_ability",
      actorLabel: ACTOR,
      abilityClass: "/Game/Nope/GA_DoesNotExist",
      world: "editor",
    }));
    expect(body.success).toBe(false);
    // Naming the accepted spellings is the difference between one more turn
    // and three.
    expect(body.error).toContain("GameplayAbility");
    expect(body.error).toContain("Blueprint path");
  });

  it("names the actor it could not find, and how to find the real one", async () => {
    const body = resultJson<Failure>(await call("gas", {
      action: "get_active_effects",
      actorLabel: "ThisActorDoesNotExistAnywhere",
      world: "editor",
    }));
    expect(body.success).toBe(false);
    expect(body.error).toContain("ThisActorDoesNotExistAnywhere");
    expect(body.error).toContain("get_outliner");
  });
});

/* ─────────────────────────────────────────────────────────────────────────
 * T4 remainder: input binding, gameplay cues, attribute audit.
 * T5 remainder: snapshot and diff.
 *
 * These run after the blocks above, which leave the probe ability revoked, so
 * anything needing it granted grants it again for itself.
 *
 * Two side effects worth naming. The gameplay tags created below are written
 * into the test project's tag config and there is no delete-tag action to undo
 * that; they are namespaced under MCPLive so they are recognisable, and the
 * test project is the only target this file will run against. And a cue tag
 * with no notify is created ON PURPOSE: an uncovered cue link is the case
 * validate_cue_coverage exists to report, so the test asserts the report
 * rather than building a notify to make it go away.
 * ───────────────────────────────────────────────────────────────────────── */

const EFFECT = `${PACKAGE}/GE_LiveProbe`;
const ATTR_SET = `${PACKAGE}/AS_LiveProbe`;
const CUE_TAG = "GameplayCue.MCPLive.Probe";
const NON_CUE_TAG = "MCPLive.NotACue";

interface CallFailure { success: boolean; error?: string }

describe("bind_ability_input", () => {
  it("refuses to bind an ability that was never granted, and names the fix", async () => {
    const body = resultJson<CallFailure>(await call("gas", {
      action: "bind_ability_input",
      actorLabel: ACTOR, abilityClass: ABILITY, inputId: 3, world: "editor",
    }));
    expect(body.success).toBe(false);
    expect(body.error).toContain("not granted");
    expect(body.error).toContain("grant_ability");
  });

  it("binds a granted ability, and rolls back to the id that was there", async () => {
    await call("gas", { action: "grant_ability", actorLabel: ACTOR, abilityClass: ABILITY, world: "editor" });

    const body = resultJson<{
      unchanged: boolean; previousInputId: number; inputId: number;
      spec: { inputID: number };
      rollback: { method: string; payload: { inputId: number } };
    }>(await call("gas", {
      action: "bind_ability_input",
      actorLabel: ACTOR, abilityClass: ABILITY, inputId: 3, world: "editor",
    }));

    expect(body.unchanged).toBe(false);
    expect(body.previousInputId).toBe(-1);
    expect(body.inputId).toBe(3);
    expect(body.spec.inputID).toBe(3);
    // Not "revoke and re-grant": the inverse of a rebind is the previous id.
    expect(body.rollback.method).toBe("bind_ability_input");
    expect(body.rollback.payload.inputId).toBe(-1);
  });

  it("reports a repeat as unchanged rather than as a second write", async () => {
    const body = resultJson<{ unchanged: boolean; previousInputId: number }>(await call("gas", {
      action: "bind_ability_input",
      actorLabel: ACTOR, abilityClass: ABILITY, inputId: 3, world: "editor",
    }));
    expect(body.unchanged).toBe(true);
    expect(body.previousInputId).toBe(3);
  });

  it("shows the binding through get_asc_state, so a reader can find it", async () => {
    const body = resultJson<{ abilities?: Array<{ inputID: number }>; specs?: Array<{ inputID: number }> }>(
      await call("gas", { action: "get_asc_state", actorLabel: ACTOR, world: "editor" }),
    );
    const rows = body.abilities ?? body.specs ?? [];
    expect(rows.some((row) => row.inputID === 3)).toBe(true);
  });
});

describe("clear_ability_input", () => {
  it("unbinds, then reports the replay as already done", async () => {
    const first = resultJson<{ previousInputId: number; inputId: number; unchanged: boolean }>(
      await call("gas", { action: "clear_ability_input", actorLabel: ACTOR, abilityClass: ABILITY, world: "editor" }),
    );
    expect(first.previousInputId).toBe(3);
    expect(first.inputId).toBe(-1);
    expect(first.unchanged).toBe(false);

    const second = resultJson<{ unchanged: boolean; rollback: { payload: { inputId: number } } }>(
      await call("gas", { action: "clear_ability_input", actorLabel: ACTOR, abilityClass: ABILITY, world: "editor" }),
    );
    expect(second.unchanged).toBe(true);
    expect(second.rollback.payload.inputId).toBe(-1);
  });
});

describe("send_ability_input", () => {
  it("says an input reached nothing rather than reporting a bare success", async () => {
    // The failure mode this guards: a success with no fields, which reads as
    // "the ability fired" when nothing is bound to that id at all.
    const body = resultJson<{ matchedSpecCount: number; unchanged: boolean; note: string }>(
      await call("gas", {
        action: "send_ability_input",
        actorLabel: ACTOR, inputEvent: "pressed", inputId: 99, world: "editor",
      }),
    );
    expect(body.matchedSpecCount).toBe(0);
    expect(body.unchanged).toBe(true);
    expect(body.note).toContain("bind_ability_input");
  });

  it("refuses an unbound ability by name and says which call binds it", async () => {
    const body = resultJson<CallFailure>(await call("gas", {
      action: "send_ability_input",
      actorLabel: ACTOR, inputEvent: "pressed", abilityClass: ABILITY, world: "editor",
    }));
    expect(body.success).toBe(false);
    expect(body.error).toContain("unbound");
    expect(body.error).toContain("bind_ability_input");
  });

  it("presses a bound ability and reports the specs it reached, before and after", async () => {
    await call("gas", {
      action: "bind_ability_input", actorLabel: ACTOR, abilityClass: ABILITY, inputId: 7, world: "editor",
    });

    const body = resultJson<{
      matchedSpecCount: number;
      matchedSpecs: Array<{ inputPressedBefore: boolean }>;
      specsAfter: Array<{ inputPressedAfter: boolean }>;
      rollback: { method: string; payload: { inputEvent: string } };
    }>(await call("gas", {
      action: "send_ability_input",
      actorLabel: ACTOR, inputEvent: "pressed", abilityClass: ABILITY, world: "editor",
    }));

    expect(body.matchedSpecCount).toBe(1);
    expect(body.matchedSpecs[0].inputPressedBefore).toBe(false);
    expect(body.specsAfter).toHaveLength(1);
    // Released undoes pressed, and that is a real inverse: the ASC keeps the
    // pressed bit per spec.
    expect(body.rollback.method).toBe("send_ability_input");
    expect(body.rollback.payload.inputEvent).toBe("released");
  });

  it("confirm has no inverse and says so instead of emitting a fake rollback", async () => {
    const body = resultJson<{ success: boolean; rollback?: unknown; rollbackNote: string }>(
      await call("gas", { action: "send_ability_input", actorLabel: ACTOR, inputEvent: "confirm", world: "editor" }),
    );
    expect(body.success).toBe(true);
    expect(body.rollback).toBeUndefined();
    expect(body.rollbackNote).toContain("No rollback");
  });

  it("lists the valid events for an unknown one", async () => {
    const body = resultJson<CallFailure>(await call("gas", {
      action: "send_ability_input", actorLabel: ACTOR, inputEvent: "wiggle", world: "editor",
    }));
    expect(body.success).toBe(false);
    expect(body.error).toContain("pressed");
    expect(body.error).toContain("cancel");
  });
});

describe("add_effect_cue", () => {
  beforeAll(async () => {
    await call("gas", { action: "create_effect", name: "GE_LiveProbe", packagePath: PACKAGE });
    await call("reflection", { action: "create_tag", tag: CUE_TAG, comment: "ue-mcp live test" });
    await call("reflection", { action: "create_tag", tag: NON_CUE_TAG, comment: "ue-mcp live test" });
  }, 300_000);

  it("refuses an unregistered tag rather than storing an invalid one", async () => {
    // Writing an unregistered tag through a plain property write succeeds and
    // stores nothing usable, which is the silent failure this action exists to
    // stop. The error has to name the call that creates the tag.
    const body = resultJson<CallFailure>(await call("gas", {
      action: "add_effect_cue", effectPath: EFFECT, cueTag: "GameplayCue.MCPLive.NeverRegistered",
    }));
    expect(body.success).toBe(false);
    expect(body.error).toContain("not registered");
    expect(body.error).toContain("create_tag");
  });

  it("refuses a registered tag that is outside the GameplayCue root", async () => {
    const body = resultJson<CallFailure>(await call("gas", {
      action: "add_effect_cue", effectPath: EFFECT, cueTag: NON_CUE_TAG,
    }));
    expect(body.success).toBe(false);
    expect(body.error).toContain("GameplayCue");
  });

  it("links the cue and warns that nothing answers it yet", async () => {
    const body = resultJson<{
      created: boolean; cueCount: number; notifyMatchedBy: string; coverageWarning: string;
      rollback: { method: string; payload: { cueTag: string } };
    }>(await call("gas", { action: "add_effect_cue", effectPath: EFFECT, cueTag: CUE_TAG }));

    expect(body.created).toBe(true);
    expect(body.cueCount).toBeGreaterThan(0);
    // The half a property write cannot do. A link with no notify is a cue that
    // silently does nothing, and saying so is the point.
    expect(body.notifyMatchedBy).toBe("none");
    expect(body.coverageWarning).toContain("no GameplayCueNotify");
    expect(body.rollback.method).toBe("remove_effect_cue");
    expect(body.rollback.payload.cueTag).toBe(CUE_TAG);
  });

  it("is idempotent on the tag rather than adding a second entry", async () => {
    // Two entries for one tag would fire the cue twice and nothing in the
    // editor shows that.
    const body = resultJson<{ existed: boolean; unchanged: boolean; cueCount: number }>(
      await call("gas", { action: "add_effect_cue", effectPath: EFFECT, cueTag: CUE_TAG }),
    );
    expect(body.existed).toBe(true);
    expect(body.unchanged).toBe(true);
    expect(body.cueCount).toBe(1);
  });

  it("reports a level-range change as an update, not as a new link", async () => {
    const body = resultJson<{ updated: boolean; unchanged: boolean; cueCount: number; rollback: { method: string } }>(
      await call("gas", { action: "add_effect_cue", effectPath: EFFECT, cueTag: CUE_TAG, minLevel: 1, maxLevel: 5 }),
    );
    expect(body.updated).toBe(true);
    expect(body.unchanged).toBe(false);
    expect(body.cueCount).toBe(1);
    // Undoing an update restores the previous levels; removing the link would
    // delete something the caller already had.
    expect(body.rollback.method).toBe("add_effect_cue");
  });
});

describe("validate_cue_coverage", () => {
  it("names the uncovered link and the effect it is on", async () => {
    const body = resultJson<{
      cueLinks: number; uncoveredCueLinks: number;
      problems: Array<{ kind: string; subject: string; detail: string }>;
    }>(await call("gas", { action: "validate_cue_coverage", effectPath: EFFECT }));

    expect(body.cueLinks).toBeGreaterThan(0);
    expect(body.uncoveredCueLinks).toBeGreaterThan(0);
    const uncovered = body.problems.filter((p) => p.kind === "cue_without_notify");
    expect(uncovered.length).toBeGreaterThan(0);
    expect(uncovered.some((p) => p.detail.includes(CUE_TAG))).toBe(true);
  });

  it("scans the project without erroring when no effect is named", async () => {
    const body = resultJson<{ effectClassesScanned: number; notifyClasses: number; problemCount: number }>(
      await call("gas", { action: "validate_cue_coverage", directory: "/Game" }),
    );
    expect(body.effectClassesScanned).toBeGreaterThanOrEqual(1);
    expect(typeof body.notifyClasses).toBe("number");
    expect(typeof body.problemCount).toBe("number");
  });
});

describe("remove_effect_cue", () => {
  it("unlinks, then reports the replay as already done", async () => {
    const first = resultJson<{ removed: number; cueCount: number; rollback: { method: string } }>(
      await call("gas", { action: "remove_effect_cue", effectPath: EFFECT, cueTag: CUE_TAG }),
    );
    expect(first.removed).toBe(1);
    expect(first.cueCount).toBe(0);
    expect(first.rollback.method).toBe("add_effect_cue");

    const second = resultJson<{ removed: number; alreadyRemoved: boolean }>(
      await call("gas", { action: "remove_effect_cue", effectPath: EFFECT, cueTag: CUE_TAG }),
    );
    expect(second.removed).toBe(0);
    expect(second.alreadyRemoved).toBe(true);
  });
});

describe("audit_attributes", () => {
  beforeAll(async () => {
    await call("gas", { action: "create_attribute_set", name: "AS_LiveProbe", packagePath: PACKAGE });
    await call("gas", {
      action: "add_attribute", attributeSetPath: ATTR_SET, attributeName: "Health", defaultValue: 100,
    });
    await call("gas", {
      action: "add_attribute", attributeSetPath: ATTR_SET, attributeName: "MaxHealth", defaultValue: 100,
    });
  }, 300_000);

  it("says a Blueprint attribute set cannot clamp, and why", async () => {
    // The one hard fact in this area, and the reason there is no
    // configure_attribute_clamping: PreAttributeChange is a plain C++ virtual,
    // so a Blueprint set has no way to override it and never will.
    const body = resultJson<{
      attributeSets: Array<{
        isBlueprint: boolean; clamping: string; clampingReason: string;
        attributes: Array<{ attribute: string; hasMaxAttribute: boolean; classification: string }>;
      }>;
    }>(await call("gas", { action: "audit_attributes", attributeSet: ATTR_SET }));

    expect(body.attributeSets).toHaveLength(1);
    const set = body.attributeSets[0];
    expect(set.isBlueprint).toBe(true);
    expect(set.clamping).toBe("impossible");
    expect(set.clampingReason).toContain("PreAttributeChange");
    // The Max pairing convention, read off the set rather than assumed.
    const health = set.attributes.find((a) => a.attribute === "Health");
    expect(health?.hasMaxAttribute).toBe(true);
  });

  it("names both ways to address it when neither is given", async () => {
    const body = resultJson<CallFailure>(await call("gas", { action: "audit_attributes" }));
    expect(body.success).toBe(false);
    expect(body.error).toContain("attributeSet");
    expect(body.error).toContain("actorPath");
  });

  it("refuses a class that does not exist, and lists what it accepts", async () => {
    const body = resultJson<CallFailure>(await call("gas", {
      action: "audit_attributes", attributeSet: "/Game/Nope/AS_DoesNotExist",
    }));
    expect(body.success).toBe(false);
    expect(body.error).toContain("AttributeSet");
    expect(body.error).toContain("content path");
  });
});

describe("capture_gas_state and compare_gas_states", () => {
  let firstId = "";

  it("captures every section of an actor's ability system state", async () => {
    const body = resultJson<{
      created: boolean; snapshotId: string;
      snapshot: {
        actorPath: string; world: string; ascInitialized: boolean;
        abilities: unknown[]; effects: unknown[]; attributes: unknown[]; ownedTags: unknown[];
        blockedAbilityTags: unknown[];
      };
      rollback: { method: string };
    }>(await call("gas", { action: "capture_gas_state", actorLabel: ACTOR, world: "editor" }));

    expect(body.created).toBe(true);
    expect(body.snapshotId).toBeTruthy();
    expect(body.snapshot.world).toBe("editor");
    // Every section present, so a diff never has to guess whether an absent
    // array means "empty" or "not captured".
    expect(Array.isArray(body.snapshot.abilities)).toBe(true);
    expect(Array.isArray(body.snapshot.effects)).toBe(true);
    expect(Array.isArray(body.snapshot.attributes)).toBe(true);
    expect(Array.isArray(body.snapshot.ownedTags)).toBe(true);
    expect(Array.isArray(body.snapshot.blockedAbilityTags)).toBe(true);
    expect(body.rollback.method).toBe("delete_gas_snapshot");
    firstId = body.snapshotId;
  });

  it("reports no change when nothing happened between two readings", async () => {
    const body = resultJson<{ changed: boolean; changeCount: number; summary: string }>(
      await call("gas", { action: "compare_gas_states", beforeId: firstId, afterId: firstId }),
    );
    expect(body.changed).toBe(false);
    expect(body.changeCount).toBe(0);
    expect(body.summary).toContain("No change");
  });

  it("names the change rather than handing back two blobs", async () => {
    // The whole point of the ticket: revoke an ability between two captures and
    // be told "ability_revoked", with the class it was.
    await call("gas", { action: "revoke_ability", actorLabel: ACTOR, abilityClass: ABILITY, world: "editor" });

    const body = resultJson<{
      diff: {
        changed: boolean; changeCount: number; summary: string;
        changes: Array<{ kind: string; subject: string; detail: string }>;
        changesByKind: Record<string, number>;
      };
    }>(await call("gas", {
      action: "capture_gas_state", actorLabel: ACTOR, world: "editor", compareWith: firstId,
    }));

    expect(body.diff.changed).toBe(true);
    expect(body.diff.changeCount).toBeGreaterThan(0);
    const revoked = body.diff.changes.find((c) => c.kind === "ability_revoked");
    expect(revoked).toBeDefined();
    expect(revoked?.subject).toContain("GA_LiveProbe");
    expect(body.diff.changesByKind["ability_revoked"]).toBe(1);
    expect(body.diff.summary).toContain("ability_revoked");
  });

  it("reports the grant in the other direction too", async () => {
    const before = resultJson<{ snapshotId: string }>(
      await call("gas", { action: "capture_gas_state", actorLabel: ACTOR, world: "editor" }),
    );
    await call("gas", {
      action: "grant_ability", actorLabel: ACTOR, abilityClass: ABILITY, level: 2, world: "editor",
    });

    const body = resultJson<{ diff: { changes: Array<{ kind: string; detail: string }> } }>(
      await call("gas", {
        action: "capture_gas_state", actorLabel: ACTOR, world: "editor", compareWith: before.snapshotId,
      }),
    );
    const granted = body.diff.changes.find((c) => c.kind === "ability_granted");
    expect(granted).toBeDefined();
    // The detail carries the level, so the caller does not have to open the row.
    expect(granted?.detail).toContain("level 2");
  });

  it("keeps the capture when compareWith names nothing, and says so", async () => {
    // Losing a reading over a stale id would be the wrong trade: the capture is
    // the expensive half.
    const body = resultJson<{ created: boolean; snapshotId: string; diff?: unknown; compareWarning: string }>(
      await call("gas", {
        action: "capture_gas_state", actorLabel: ACTOR, world: "editor", compareWith: "gas-does-not-exist",
      }),
    );
    expect(body.created).toBe(true);
    expect(body.snapshotId).toBeTruthy();
    expect(body.diff).toBeUndefined();
    expect(body.compareWarning).toContain("gas-does-not-exist");
  });

  it("refuses a comparison with only one side, and names the one-call route", async () => {
    const body = resultJson<CallFailure>(await call("gas", { action: "compare_gas_states", beforeId: firstId }));
    expect(body.success).toBe(false);
    expect(body.error).toContain("capture_gas_state");
    expect(body.error).toContain("compareWith");
  });

  it("names the stored ids when one does not resolve", async () => {
    const body = resultJson<CallFailure>(await call("gas", {
      action: "compare_gas_states", beforeId: "gas-nope", afterId: firstId,
    }));
    expect(body.success).toBe(false);
    expect(body.error).toContain("gas-nope");
    expect(body.error).toContain("Stored ids");
  });

  it("lists what it holds, and drops one without losing its contents", async () => {
    const listed = resultJson<{ count: number; snapshots: Array<{ snapshotId: string; actorPath: string }> }>(
      await call("gas", { action: "list_gas_snapshots" }),
    );
    expect(listed.count).toBeGreaterThan(0);
    expect(listed.snapshots.some((s) => s.snapshotId === firstId)).toBe(true);

    const deleted = resultJson<{
      updated: boolean; deletedSnapshot: { actorPath: string }; rollbackNote: string;
    }>(await call("gas", { action: "delete_gas_snapshot", snapshotId: firstId }));
    expect(deleted.updated).toBe(true);
    // Handing the body back is what makes the deletion recoverable at all, and
    // the note says the rollback re-reads rather than restores.
    expect(deleted.deletedSnapshot.actorPath).toBeTruthy();
    expect(deleted.rollbackNote).toContain("Lossy");

    const again = resultJson<{ alreadyDeleted: boolean; unchanged: boolean }>(
      await call("gas", { action: "delete_gas_snapshot", snapshotId: firstId }),
    );
    expect(again.alreadyDeleted).toBe(true);
    expect(again.unchanged).toBe(true);
  });
});
