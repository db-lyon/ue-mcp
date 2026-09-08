/**
 * The house conventions, enforced instead of merely documented.
 *
 * Three properties make a handler safe to drive from an agent:
 *
 *   idempotency  it says whether it actually changed anything, so a replayed
 *                flow step, or a retry after a timeout that in fact succeeded,
 *                does not double-apply and does not report success for a no-op.
 *   rollback     it emits the call that undoes it, which is what the flow
 *                engine's `rollback_on_failure` consumes. A mutation with no
 *                rollback silently makes every flow containing it unrecoverable.
 *   reachability it is callable from the TS surface at all.
 *
 * The repo already had 198 rollback sites and 101 idempotency markers, so the
 * conventions were real. What it did not have was anything that FAILED when a
 * new handler skipped one, which is how the backlog below accumulated.
 *
 * ## Why this is a ratchet and not a flat rule
 *
 * 359 of 547 existing mutations emit no rollback. Writing one blanket
 * assertion would mean either failing the suite on day one or allowlisting 359
 * handlers with invented reasons, and an allowlist entry whose reason was not
 * actually considered is worse than no entry: it looks like a decision.
 *
 * So the baseline is recorded as a number and this fails when the number gets
 * WORSE. A new handler must carry both markers, because the counts are pinned;
 * fixing an old one lowers the baseline and the test tells you to commit the
 * lower number. The debt is visible, it cannot grow, and it shrinks whenever
 * anyone touches that area.
 *
 * A source-level audit cannot prove a rollback is CORRECT, only that one is
 * emitted. Correctness is what the live tests asserts. Catching the omission is
 * still most of the value: the common failure is not a wrong rollback, it is
 * no rollback at all.
 */
import { describe, it, expect } from "vitest";
import { auditHandlers, stripComments } from "../../scripts/audit-handler-conventions.mjs";
import { ALL_TOOLS } from "../../src/tools.js";
import { classifyActionClass } from "../../src/action-class.js";

/**
 * The counts as they stand. Lower these when you fix one; never raise them.
 *
 * A raise means a newly added handler skipped a convention, which is exactly
 * what this file exists to stop.
 */
const BASELINE = {
  // What a mutation owes its caller is an ANSWER, and two of these three counts
  // are now zero because every mutation gives one. They are flat rules below,
  // not ratchets: there is no debt left to ratchet down.
  //
  // The journey is worth recording, because the numbers were quoted in review
  // while none of them were being enforced. 356 and 129 were measured by a
  // scanner that could not see a marker emitted inside a shared helper and
  // collided two classes with the same method name. Fixing the scanner put the
  // real figures at 135 and 21. Fixing the shebang that stopped this file from
  // LOADING is what made any of it enforced at all.
  //
  // `mutationsWithoutRollback` stays a number rather than a rule, and stays
  // non-zero, because a rollback is not always possible and pretending
  // otherwise would mean emitting inverses that do not invert. What is not
  // allowed is silence: see mutationsSilentOnRollback.
  mutationsWithoutRollback: 127,
  // Zero, and held there by a flat assertion. Every mutation with no inverse
  // now says so in its own result body with the reason, which is the half a
  // caller can act on.
  mutationsSilentOnRollback: 0,
  // Zero on the same terms. A handler that cannot read whether it changed
  // anything says that too, through MCPSetIdempotencyUnobservable, rather than
  // inventing an `unchanged` flag about somebody else's code.
  mutationsWithoutIdempotency: 0,
  orphanedHandlers: 22,
};

/**
 * Bridge methods with no TS action.
 *
 * A capability the bridge has and no agent can reach. Triaged rather than
 * bulk-allowlisted: each of these is either infrastructure that is called from
 * code rather than authored, or a genuine hole worth wiring up. The ones
 * marked HOLE are real work, listed so they are not mistaken for decisions.
 */
const KNOWN_ORPHANS: Record<string, string> = {
  // Infrastructure: reached from code, never authored by a caller.
  // These two carry the same reasons as tests/unit/drift.test.ts's CPP_ONLY.
  bulk_restore_data_assets:
    "Inverse of bulk_upsert_data_assets, reached only through the rollback descriptor that call emits.",
  migrate:
    "asset(migrate) is a TS handler that resolves the destination editor and rescans its registry, "
    + "so it calls this from code rather than declaring it as an action's bridge.",
  acquire_lock: "asset(lock) drives this; the raw handler is the lock primitive.",
  release_lock: "as above.",
  release_session_locks: "called on session teardown, not by a caller.",
  build_project: "editor(build_project) is a local handler that shells out to UBT instead.",
  execute_python: "editor(execute_python) wraps this with the workaround tracker and the search gate.",
  request_editor_shutdown: "editor(stop_editor) drives it through the lifecycle path.",
  save_current_level: "level(save) supersedes it and reports per-package results.",
  search_assets: "asset(search) supersedes it.",
  delete_datatable_row: "asset(remove_datatable_row) is the shipped spelling.",
  get_applied_imcs: "gameplay(get_applied_imcs) reaches it under the get_input_mapping_contexts name.",
  add_instances: "level(add_hismc_instances) is the shipped spelling.",
  add_ismc_instances: "as above.",
  list_sockets: "asset(list_sockets) reaches it under a different bridge name.",

  // Alias registrations: a SECOND RegisterHandler line pointing at the same
  // C++ function a shipped action already calls. Reachable, not missing. These
  // were first annotated as holes; reading each function pointer showed
  // otherwise, which is why "unreferenced by name" is not the same question as
  // "unreachable".
  add_material_function_expression:
    "Alias of AddMaterialFunctionExpression; material(add_function_expression) calls it as add_expression_in_function.",
  connect_material_function_expressions:
    "Alias of ConnectMaterialFunctionExpressions; material(connect_function_expressions) calls it as connect_expressions_in_function.",
  list_material_function_expressions:
    "Alias of ListMaterialFunctionExpressions; material(list_function_expressions) calls it as list_expressions_in_function.",
  populate_blendspace_1d:
    "Alias of PopulateBlendspace, which already branches on UBlendSpace1D; animation(populate_blendspace) calls it.",
  remove_animation_notify:
    "Alias of RemoveAnimNotify; animation(remove_notify) calls it as remove_anim_notify. CRUD is complete.",
  add_force:
    "Alias of AddImpulse; gameplay(add_impulse) with mode='force' calls it.",
  place_skeletal_actor:
    "Alias of SpawnSkeletalMeshActor; level(spawn_skeletal_mesh_actor) calls it.",
  add_curve:
    "Alias of AddCurve; animation(add_curve) calls it under the identical name.",
};

/**
 * There is no allowlist any more, and that is the point.
 *
 * This file used to carry a `NO_INVERSE` map: four mutations exempted from the
 * rollback rule with the reason written HERE, in the test, where no caller can
 * read it. That was the right shape while the reasons had nowhere else to live,
 * and the wrong shape once `MCPSetNoRollback` existed, because a caller holding
 * the result body still could not tell a considered decision from an oversight.
 *
 * All four now state it in the handler, which is where the caller reads it, so
 * the audit sees the statement and the exemption is redundant. An exemption
 * list that outlives its reason is how a convention quietly stops applying.
 */

/** bridge method -> the TS tool+action that reaches it. */
function bridgeToAction(): Map<string, { tool: string; action: string }> {
  const out = new Map<string, { tool: string; action: string }>();
  for (const tool of ALL_TOOLS) {
    for (const [action, spec] of Object.entries(tool.actions)) {
      if (spec.bridge) out.set(spec.bridge, { tool: tool.name, action });
    }
  }
  return out;
}

type Row = {
  action: string; method: string; class: string;
  idempotent: boolean; rollback: boolean; bodyFound: boolean;
  /** True when the body emits `rollbackPossible`, i.e. it CONSIDERED the inverse. */
  declaresNoRollback: boolean;
  /** True when the body emits `idempotencyObservable`, i.e. it cannot measure its own effect and said so. */
  declaresNoIdempotency: boolean;
  /** The TS action name, which is what the read-verb test above reads. */
  tsAction?: string;
};

function audit(): Row[] {
  const byBridge = bridgeToAction();
  const rows = auditHandlers((bridgeName: string) => {
    const hit = byBridge.get(bridgeName);
    if (!hit) return "orphan";
    return classifyActionClass(hit.tool, hit.action).class;
  }) as Row[];
  // The audit keys on the bridge method name; the read-verb test has to read
  // the TS spelling, which is what a caller actually types and is not always
  // the same word (landscape(export_heightmap) is export_landscape_heightmap
  // on the bridge).
  for (const row of rows) row.tsAction = byBridge.get(row.action)?.action;
  return rows;
}

/**
 * Read verbs that settle the question no matter what else the name contains.
 *
 * `classifyActionClass` scans the WHOLE action name for a mutating verb and is
 * deliberately generous, because it answers a different question: while this
 * server drives more than one editor, an unaddressed call falls through to the
 * active session, so over-classifying costs an explicit target and nothing
 * else. Erring that way is right at the gate.
 *
 * It is wrong here. `editor(get_frame_timing)` matches on nothing it does,
 * `level(get_relative_transform)` reads a transform, `editor(list_dirty_packages)`
 * lists, and `asset(read_import_sources)` reads - and each was being counted as
 * a mutation owing a rollback. Ten actions of pure invented debt, and debt that
 * is not real is worse than no ledger, because the next person spends a day
 * writing inverses for reads.
 *
 * So a LEADING read verb wins, and only a leading one. `save_level` starts with
 * `save` and stays a mutation; `get_post_process_settings` does not become a
 * mutation because `process` appears later in it. The list is short on purpose:
 * `run`, `check`, `validate` and `analyze` really can change things, so they are
 * not here.
 */
const READ_LED = /^(get|list|read|inspect|describe)_/;

const isMutation = (r: Row): boolean =>
  (r.class === "mutate" || r.class === "unknown") && !READ_LED.test(r.tsAction ?? r.action);

const NL_C = String.fromCharCode(10);

describe("the audit's own marker scan", () => {
  // This file audits handlers for a convention. Its scan was itself violating
  // the thing it checks: it matched raw source, so prose counted as code. Every
  // idempotency marker is an ordinary English word, and a rollbackNote saying a
  // value was left "unchanged" earned the credit. Fixing that without testing it
  // would repeat the original mistake one level up.

  it("does not let a comment stand in for a marker", () => {
    const prose = "// the value is left unchanged and the rest is skipped" + NL_C;
    expect(stripComments(prose)).not.toContain("unchanged");
    expect(stripComments("/* nested structs are skipped */")).not.toContain("skipped");
  });

  it("keeps the markers that really do live in string literals", () => {
    // These are code, not prose, and a scan that dropped them would report
    // false violations for every handler using the hand-rolled form.
    const code = `Result->SetBoolField(TEXT("alreadyRemoved"), true);`;
    expect(stripComments(code)).toContain("alreadyRemoved");
    expect(stripComments(`SetBoolField(TEXT("changed"), b); // changed`)).toContain(
      `SetBoolField(TEXT("changed")`,
    );
  });

  it("is not fooled by a comment marker inside a string, or a quote inside a comment", () => {
    // The reason this cannot be a regex: it has to know which quotes it is in.
    expect(stripComments(`FString Url = TEXT("http://x/unchanged");`)).toContain("unchanged");
    expect(stripComments(`// he said "unchanged" here` + NL_C + "int x;")).not.toContain("unchanged");
    // An escaped quote does not end its literal, so the tail is still code.
    expect(stripComments(`TEXT("a\\"b") // alreadySet`)).not.toContain("alreadySet");
  });
});

describe("handler conventions", () => {
  const rows = audit();
  const mutations = rows.filter(isMutation);

  it("finds a body for every registered handler", () => {
    // A handler whose body cannot be located is not audited at all, so this
    // guards the audit itself rather than the handlers. The cap is 0 because
    // the measured number is 0: a tolerance of six unreadable bodies is six
    // handlers that could skip every convention in this file without failing
    // anything, and the whole point of the ratchet is that the slack is real.
    const missing = rows.filter((r) => !r.bodyFound).map((r) => `${r.action} (${r.method})`);
    expect(
      missing.length,
      `The audit could not locate these handler bodies, so their conventions are unchecked:\n  `
        + missing.join("\n  "),
    ).toBe(0);
  });

  it("answers the rollback question in the result body, not in this file", () => {
    // What the deleted NO_INVERSE allowlist used to assert, asked the right way
    // round. The four handlers it exempted now carry their reason in the
    // response, so the property to hold is that no mutation needs an exemption
    // here at all: every one of them either emits an inverse or says why it
    // cannot, and a caller can read the answer without reading this repo.
    const unanswered = mutations
      .filter((r) => !r.rollback && !r.declaresNoRollback)
      .map((r) => r.action);
    expect(
      unanswered,
      "A mutation must answer the rollback question in its own result body. There is no "
        + "allowlist to add it to: emit MCPSetRollback, or MCPSetNoRollback with the reason.",
    ).toEqual([]);
  });

  it("does not add a mutation without a rollback", () => {
    const without = mutations
      .filter((r) => !r.rollback)
      .map((r) => r.action);
    expect(
      without.length,
      `Mutations with no rollback: ${without.length}, baseline ${BASELINE.mutationsWithoutRollback}.\n`
        + `A mutation must emit MCPSetRollback(Result, "<inverse>", Payload) so the flow engine's\n`
        + `rollback_on_failure can undo it. If this went UP, a new handler skipped it.\n`
        + `If it went DOWN, lower the baseline in this file and commit that.`,
    ).toBeLessThanOrEqual(BASELINE.mutationsWithoutRollback);
  });

  it("does not add a mutation that stays SILENT about its inverse", () => {
    // The rollback ratchet above cannot tell a mutation that forgot from one
    // that decided. Both emit no MCPSetRollback, and until this existed an
    // honest `rollbackPossible: false` with a note explaining why read exactly
    // like an omission - so the 149 mutations with no rollback looked like 149
    // holes when 109 of them are considered decisions.
    //
    // That distinction is the one a CALLER can already see in the result body,
    // which is the argument for auditing it: a handler that says "there is no
    // inverse, here is why" has finished the job, and one that says nothing has
    // not. This is the assertion nothing in the repo made.
    const silent = mutations
      .filter((r) => !r.rollback && !r.declaresNoRollback)
      .map((r) => r.action);
    expect(
      silent.length,
      [
        `Mutations that emit neither a rollback nor rollbackPossible: ${silent.length}, `
          + `baseline ${BASELINE.mutationsSilentOnRollback}.`,
        `A mutation must either emit MCPSetRollback(Result, "<inverse>", Payload), or set`,
        `rollbackPossible:false with a note saying WHY there is no inverse. Saying nothing`,
        `leaves the caller unable to tell a considered decision from an oversight.`,
        `Offenders:`,
        ...silent.map((a) => `  ${a}`),
      ].join(String.fromCharCode(10)),
    ).toBe(BASELINE.mutationsSilentOnRollback);
  });

  it("does not add a mutation without an idempotency marker", () => {
    // `declaresNoIdempotency` satisfies this the way `declaresNoRollback`
    // satisfies the silence test above: a handler that CANNOT read whether it
    // changed anything, and says so with the reason, has answered. The cases
    // are handlers dispatching somebody else's code, where a fabricated
    // `unchanged: false` would be a claim rather than a reading.
    const without = mutations
      .filter((r) => !r.idempotent && !r.declaresNoIdempotency)
      .map((r) => r.action);
    expect(
      without.length,
      `Mutations with no idempotency marker: ${without.length}, baseline `
        + `${BASELINE.mutationsWithoutIdempotency}.\n`
        + `A mutation must report whether it actually changed anything, via MCPSetCreated /\n`
        + `MCPSetExisted / MCPSetUpdated or an explicit already* field, so a retry after a\n`
        + `timeout that in fact succeeded does not double-apply.`,
    ).toBe(BASELINE.mutationsWithoutIdempotency);
  });

  it("does not add an unreachable handler", () => {
    const orphans = rows.filter((r) => r.class === "orphan").map((r) => r.action);
    const untriaged = orphans.filter((a) => !(a in KNOWN_ORPHANS));
    expect(
      untriaged,
      `These C++ handlers are registered but no TS action reaches them, and they are not in\n`
        + `KNOWN_ORPHANS. A capability nobody can call is not shipped. Either surface it as an\n`
        + `action, or add it to KNOWN_ORPHANS with a real reason:\n  ` + untriaged.join("\n  "),
    ).toEqual([]);
    expect(orphans.length).toBeLessThanOrEqual(BASELINE.orphanedHandlers);
  });

  it("holds every handler this session added to the full convention", () => {
    // The ratchet protects the past. These are the ones written or corrected
    // in the session that introduced this file, and they are held to the rule
    // rather than to the baseline.
    const SESSION_HANDLERS = [
      "grant_ability", "revoke_ability",
      "add_eqs_generator", "add_eqs_test", "remove_eqs_test",
      "remove_eqs_option", "reorder_eqs_tests",
    ];
    const byAction = new Map(rows.map((r) => [r.action, r]));
    const offenders: string[] = [];
    for (const action of SESSION_HANDLERS) {
      const row = byAction.get(action);
      if (!row) {
        offenders.push(`${action}: not registered`);
        continue;
      }
      if (!row.rollback) offenders.push(`${action}: no rollback`);
      if (!row.idempotent) offenders.push(`${action}: no idempotency marker`);
    }
    expect(offenders, offenders.join("\n  ")).toEqual([]);
  });
});
