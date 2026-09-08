/**
 * AI perception and BehaviorTree runtime, against a real editor (T10, T11).
 *
 * Both surfaces exist because reflection cannot reach what they read.
 * PerceptualData is a bare C++ TMap rather than a UPROPERTY, HasAnyActiveStimulus
 * and GetYoungestStimulusAge are plain methods rather than UFUNCTIONs, and the
 * BehaviorTreeComponent's instance stack is protected. So a unit test can assert
 * the schema and nothing else: only a running world says whether any of it works.
 *
 * The half that matters most is the end-to-end one. A perception setup can be
 * authored, compiled and placed without ever sensing anything, and every call
 * along the way returns success. So the hearing case here injects a noise event
 * and then asserts the perceiver actually heard it, with the per-sense breakdown
 * showing which sense carried the detection and which did not.
 *
 * Everything runs in Play-In-Editor, because the editor world has no AI system
 * at all. PIE is started in beforeAll and stopped in afterAll; the editor process
 * itself is never started or stopped, which is this suite's rule.
 *
 * Everything is created under /Game/MCPAILive and removed afterwards.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { LiveServer, resultJson } from "./server.js";
import { closeLiveBridges, liveTarget } from "./harness.js";

const target = await liveTarget();

const PACKAGE = "/Game/MCPAILive";
/** The controller the pawn auto-possesses: one Sight sense at its defaults and
 *  one Hearing sense tuned to hear neutrals, which is what the noise case needs. */
const CONTROLLER = `${PACKAGE}/BP_MCPAIController`;
/** A second, untouched controller for the authoring cases, so removing a sense
 *  never disturbs the one PIE is running. */
const PROBE = `${PACKAGE}/BP_MCPPerceptionProbe`;
const BLACKBOARD = `${PACKAGE}/BB_MCPAILive`;
const TREE = `${PACKAGE}/BT_MCPAILive`;
const PAWN = "MCPAILivePawn";
const TARGET = "MCPAILiveTarget";

let server: LiveServer;

const call = async (tool: string, args: Record<string, unknown>) =>
  server.call(tool, { ...args, timeoutMs: 300_000 });

const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

/**
 * Answer the editor's own PIE prompts.
 *
 * `pie_control start` only REQUESTS a play session; the editor raises its
 * unresolved-Blueprint-error confirmation on a later tick, where it blocks the
 * game thread and every ordinary handler times out. list_dialogs and
 * respond_to_dialog are the two that run anyway, which is what makes this
 * recoverable rather than a wedged editor. The project accumulates errored
 * Blueprints from other test runs, so the prompt is normal here and is not
 * something this file should assert about.
 */
const answerPiePrompt = async (): Promise<boolean> => {
  const listed = resultJson<{ dialogs?: Array<{ title?: string; buttons?: string[] }> }>(
    await call("editor", { action: "list_dialogs" }),
  );
  let answered = false;
  for (const dialog of listed.dialogs ?? []) {
    const proceed = (dialog.buttons ?? []).find((b) => /play in editor/i.test(b));
    if (!proceed) continue;
    await call("editor", { action: "respond_to_dialog", buttonLabel: proceed });
    answered = true;
  }
  return answered;
};

interface PieConfig {
  numClients?: number;
  netMode?: string;
  runUnderOneProcess?: boolean;
  launchSeparateServer?: boolean;
}

/** Whatever the project's PIE settings were before this file changed them. */
let priorPieConfig: PieConfig | null = null;

/**
 * One standalone PIE world, and nothing else.
 *
 * The test project ships netMode=Client with launchSeparateServer, which spins
 * up a second PIE world alongside the first. Deleting fixture actors or assets
 * while that client is still mid LoadMap crashes the editor outright, and the
 * cases here have nothing to say about multiplayer: one game world with an AI
 * system in it is the whole requirement. The previous settings are read first
 * and put back in afterAll, so this file borrows the configuration rather than
 * changing it.
 */
const usePieStandalone = async (): Promise<void> => {
  priorPieConfig = resultJson<PieConfig>(await call("editor", { action: "get_pie_config" }));
  await call("editor", {
    action: "configure_pie",
    netMode: "standalone",
    numClients: 1,
    launchSeparateServer: false,
    runUnderOneProcess: true,
  });
};

const restorePieConfig = async (): Promise<void> => {
  if (!priorPieConfig) return;
  await call("editor", {
    action: "configure_pie",
    netMode: priorPieConfig.netMode,
    numClients: priorPieConfig.numClients,
    launchSeparateServer: priorPieConfig.launchSeparateServer,
    runUnderOneProcess: priorPieConfig.runUnderOneProcess,
  });
};

/** PIE, running. Perception and BehaviorTrees exist only in a game world, and a
 *  session that ended between cases would otherwise report as a handler fault. */
const ensurePie = async (): Promise<void> => {
  const playing = async () =>
    resultJson<{ isPlaying?: boolean }>(await call("editor", { action: "play_in_editor", pieAction: "status" }))
      .isPlaying === true;

  // Before anything else: a modal left standing blocks the game thread, and
  // every call below it would report as a timeout rather than as the prompt.
  await answerPiePrompt();
  if (await playing()) return;
  await call("editor", { action: "play_in_editor", pieAction: "start" });

  for (let attempt = 0; attempt < 20; attempt++) {
    await sleep(1000);
    await answerPiePrompt();
    if (await playing()) return;
  }
  throw new Error("PIE did not start within 20s; the runtime cases need a game world.");
};

/**
 * Remove the fixture assets, whoever left them.
 *
 * Run before the build as well as after it: several cases here turn on a sense
 * config being CREATED rather than found (configure_sense applies its settings
 * only on the create branch), so a fixture surviving an aborted run would make
 * this file pass or fail on what the previous run left behind. Asset by asset,
 * because delete_folder refuses a folder whose assets reference each other.
 */
const wipeFixtures = async (): Promise<void> => {
  for (const assetPath of [TREE, BLACKBOARD, CONTROLLER, PROBE]) {
    try {
      await call("asset", { action: "delete", assetPath, force: true });
    } catch {
      // Absent is the desired state, and delete says so rather than failing.
    }
  }
  try {
    await call("asset", { action: "delete_folder", path: PACKAGE, force: true });
  } catch {
    // An empty folder left behind is noise, not a failure.
  }
};

beforeAll(async () => {
  server = await LiveServer.start({ projects: [target.uproject] });
  await usePieStandalone();
  await wipeFixtures();

  // -- The perceiving controller ---------------------------------------------
  await call("blueprint", { action: "create", assetPath: CONTROLLER, parentClass: "AIController" });
  await call("gameplay", { action: "add_perception", blueprintPath: CONTROLLER, senses: ["Sight"] });
  // Hearing is configured rather than added, because configure_sense applies its
  // settings only on the branch that creates the config. Neutrals have to be
  // detectable or a noise from an unaffiliated pawn is filtered out before range
  // is even considered.
  await call("gameplay", {
    action: "configure_sense",
    blueprintPath: CONTROLLER,
    senseType: "Hearing",
    settings: {
      HearingRange: 5000,
      DetectionByAffiliation: { bDetectEnemies: true, bDetectNeutrals: true, bDetectFriendlies: true },
    },
  });

  await call("blueprint", { action: "create", assetPath: PROBE, parentClass: "AIController" });
  await call("gameplay", { action: "add_perception", blueprintPath: PROBE, senses: ["Sight", "Hearing"] });

  // -- The tree the agent runs ----------------------------------------------
  await call("gameplay", { action: "create_blackboard", name: "BB_MCPAILive", packagePath: PACKAGE });
  await call("gameplay", { action: "add_blackboard_key", blackboardPath: BLACKBOARD, keyName: "MCPFlag", keyType: "Bool" });
  await call("gameplay", { action: "add_blackboard_key", blackboardPath: BLACKBOARD, keyName: "MCPCount", keyType: "Int" });
  await call("gameplay", {
    action: "create_behavior_tree",
    name: "BT_MCPAILive",
    packagePath: PACKAGE,
    blackboardPath: BLACKBOARD,
  });
  const root = resultJson<{ guid?: string }>(
    await call("gameplay", {
      action: "add_bt_node",
      assetPath: TREE,
      nodeClass: "BTComposite_Selector",
      parent: "root",
      nodeName: "MCPRoot",
    }),
  );
  // Addressed by guid, not by name: a display name is not unique and the add
  // refuses an ambiguous parent rather than guessing.
  await call("gameplay", {
    action: "add_bt_node",
    assetPath: TREE,
    nodeClass: "BTTask_Wait",
    parent: root.guid,
    nodeName: "MCPWait",
  });

  // -- The agent and something for it to notice ------------------------------
  await call("level", { action: "place_actor", actorClass: "DefaultPawn", label: PAWN, location: { x: 0, y: 0, z: 200 } });
  await call("level", { action: "set_actor_property", actorLabel: PAWN, propertyName: "AutoPossessAI", value: "PlacedInWorldOrSpawned" });
  await call("level", {
    action: "set_actor_property",
    actorLabel: PAWN,
    propertyName: "AIControllerClass",
    value: `${CONTROLLER}.BP_MCPAIController_C`,
  });
  await call("level", { action: "place_actor", actorClass: "DefaultPawn", label: TARGET, location: { x: 300, y: 0, z: 200 } });

  await ensurePie();
}, 600_000);

afterAll(async () => {
  try {
    // Stop, and WAIT for the session to be gone. Deleting an actor while a PIE
    // world is still tearing down is what took the editor with it.
    await call("editor", { action: "play_in_editor", pieAction: "stop" });
    for (let attempt = 0; attempt < 20; attempt++) {
      await sleep(1000);
      const status = resultJson<{ isPlaying?: boolean }>(
        await call("editor", { action: "play_in_editor", pieAction: "status" }),
      );
      if (status.isPlaying !== true) break;
    }
    await sleep(1500);
    await call("level", { action: "delete_actor", actorLabel: PAWN });
    await call("level", { action: "delete_actor", actorLabel: TARGET });
    await wipeFixtures();
    await restorePieConfig();
  } catch {
    // Cleanup is best effort and must not mask a real failure.
  }
  await server?.close();
  closeLiveBridges();
}, 300_000);

// ---------------------------------------------------------------------------
// T10 - AI perception
// ---------------------------------------------------------------------------

interface PerceptionRead {
  success?: boolean;
  source?: string;
  component?: string;
  componentObjectPath?: string;
  componentFoundOn?: string;
  senseCount?: number;
  senses?: Array<{ senseType?: string; objectPath?: string; startsEnabled?: boolean; parameters?: Record<string, string> }>;
  problems?: string[];
  perceivable?: boolean;
  error?: string;
}

describe("read_perception", () => {
  it("reads a Blueprint's component template with an objectPath per sense", async () => {
    const body = resultJson<PerceptionRead>(
      await call("gameplay", { action: "read_perception", blueprintPath: CONTROLLER }),
    );
    expect(body.success).not.toBe(false);
    expect(body.source).toBe("blueprint");
    expect(body.senseCount).toBe(2);

    // The objectPath is the entire reason there are no per-parameter setters:
    // it is what editor(set_property) writes a sense tunable at.
    for (const sense of body.senses ?? []) {
      expect(sense.objectPath, `${sense.senseType} has no objectPath`).toBeTruthy();
      expect(sense.parameters, `${sense.senseType} dumped no parameters`).toBeTruthy();
    }
    const hearing = (body.senses ?? []).find((s) => s.senseType === "Hearing");
    expect(hearing?.parameters?.HearingRange).toContain("5000");
  }, 300_000);

  it("finds the live component on the controller when asked about the pawn", async () => {
    // A caller selects the pawn; the AIPerceptionComponent is on its controller.
    // Failing with "no component on MCPAILivePawn" is the dead end this avoids.
    await ensurePie();
    const body = resultJson<PerceptionRead>(
      await call("gameplay", { action: "read_perception", actorLabel: PAWN, world: "pie" }),
    );
    expect(body.success).not.toBe(false);
    expect(body.source).toBe("actor");
    expect(body.componentFoundOn).toContain("controller");
    expect(body.problems).toEqual([]);
    expect(body.perceivable).toBe(true);
  }, 300_000);

  it("refuses a target it cannot name, rather than reading nothing", async () => {
    const body = resultJson<PerceptionRead>(await call("gameplay", { action: "read_perception" }));
    expect(body.success).toBe(false);
    expect(body.error).toContain("blueprintPath");
  }, 120_000);
});

describe("remove_sense", () => {
  it("destroys an instanced subobject set_property cannot touch, with a rollback", async () => {
    const body = resultJson<{
      success?: boolean;
      alreadyRemoved?: boolean;
      removedSenseType?: string;
      remainingSenses?: number;
      rollbackAvailable?: boolean;
      rollback?: { method?: string };
    }>(await call("gameplay", { action: "remove_sense", blueprintPath: PROBE, senseType: "Hearing" }));

    expect(body.success).not.toBe(false);
    expect(body.alreadyRemoved).toBe(false);
    expect(body.removedSenseType).toBe("Hearing");
    expect(body.remainingSenses).toBe(1);
    expect(body.rollbackAvailable).toBe(true);
    expect(body.rollback?.method).toBe("configure_ai_perception_sense");
  }, 300_000);

  it("reports alreadyRemoved on a replay instead of failing", async () => {
    const body = resultJson<{ success?: boolean; alreadyRemoved?: boolean }>(
      await call("gameplay", { action: "remove_sense", blueprintPath: PROBE, senseType: "Hearing" }),
    );
    expect(body.success).not.toBe(false);
    expect(body.alreadyRemoved).toBe(true);
  }, 300_000);
});

describe("perception at runtime", () => {
  it("reads the perceived sets off PerceptualData, which has no UPROPERTY", async () => {
    await ensurePie();
    const body = resultJson<{
      success?: boolean;
      world?: string;
      configuredSenses?: string[];
      currentlyPerceivedCount?: number;
      knownCount?: number;
      note?: string;
    }>(await call("gameplay", { action: "get_perceived_actors", actorLabel: PAWN, world: "pie" }));

    expect(body.success).not.toBe(false);
    expect(body.world).toBe("pie");
    expect(body.configuredSenses).toHaveLength(2);
    // An empty answer is the normal one before a stimulus, and it has to explain
    // itself or the caller has nothing to act on.
    if ((body.currentlyPerceivedCount ?? 0) === 0 && (body.knownCount ?? 0) === 0) {
      expect(body.note).toContain("report_noise_event");
    }
  }, 300_000);

  it("names the real senses when senseType is wrong", async () => {
    await ensurePie();
    const body = resultJson<{ success?: boolean; error?: string }>(
      await call("gameplay", { action: "get_perceived_actors", actorLabel: PAWN, senseType: "NotASense", world: "pie" }),
    );
    expect(body.success).toBe(false);
    expect(body.error).toContain("Hearing");
    expect(body.error).toContain("Sight");
  }, 300_000);

  it("reports who could receive a noise before anything ticks", async () => {
    await ensurePie();
    const body = resultJson<{
      success?: boolean;
      listenersConfiguredForSense?: number;
      listeners?: Array<{ actorLabel?: string; distance?: number; hearingRange?: number; withinHearingRange?: boolean }>;
      note?: string;
    }>(await call("gameplay", {
      action: "report_noise_event",
      senseType: "hearing",
      location: { x: 300, y: 0, z: 200 },
      loudness: 10,
      instigatorLabel: TARGET,
      world: "pie",
    }));

    expect(body.success).not.toBe(false);
    // An event nobody could hear has to be distinguishable from a silent one.
    expect(body.listenersConfiguredForSense).toBe(1);
    const listener = (body.listeners ?? [])[0];
    expect(listener?.actorLabel).toBe(PAWN);
    expect(listener?.withinHearingRange).toBe(true);
  }, 300_000);

  it("refuses a damage event with no amount rather than reporting a zero one", async () => {
    await ensurePie();
    const body = resultJson<{ success?: boolean; error?: string }>(
      await call("gameplay", { action: "report_noise_event", senseType: "damage", targetLabel: TARGET, world: "pie" }),
    );
    expect(body.success).toBe(false);
    expect(body.error).toContain("amount");
  }, 300_000);

  it("hears the injected noise, and says which sense carried it", async () => {
    // The end-to-end case: inject, let the AI system update, then read the
    // detection back. Everything before this can pass on a setup that senses
    // nothing at all.
    await ensurePie();
    await call("gameplay", {
      action: "report_noise_event",
      senseType: "hearing",
      location: { x: 300, y: 0, z: 200 },
      loudness: 10,
      instigatorLabel: TARGET,
      world: "pie",
    });
    await sleep(2500);

    const body = resultJson<{
      success?: boolean;
      perceives?: boolean;
      currentlySensed?: boolean;
      everSensed?: boolean;
      distance?: number;
      youngestStimulusAgeSeconds?: number;
      senses?: Array<{ senseType?: string; hasActiveStimulus?: boolean; registered?: boolean }>;
    }>(await call("gameplay", {
      action: "check_perception",
      perceiverLabel: PAWN,
      targetLabel: TARGET,
      world: "pie",
    }));

    expect(body.success).not.toBe(false);
    expect(body.perceives).toBe(true);
    expect(body.everSensed).toBe(true);
    expect(body.distance).toBeGreaterThan(200);
    expect(body.distance).toBeLessThan(400);

    const bySense = Object.fromEntries((body.senses ?? []).map((s) => [s.senseType, s]));
    expect(bySense.Hearing?.hasActiveStimulus).toBe(true);
    // Sight keeps its default affiliation filter, so it rejects a neutral pawn.
    // A per-sense breakdown that reported both would be useless for diagnosis.
    expect(bySense.Sight?.hasActiveStimulus).toBe(false);

    const perceived = resultJson<{ currentlyPerceivedCount?: number; currentlyPerceived?: Array<{ actorLabel?: string }> }>(
      await call("gameplay", { action: "get_perceived_actors", actorLabel: PAWN, senseType: "Hearing", world: "pie" }),
    );
    expect(perceived.currentlyPerceivedCount).toBeGreaterThan(0);
    expect((perceived.currentlyPerceived ?? []).map((a) => a.actorLabel)).toContain(TARGET);
  }, 300_000);
});

// ---------------------------------------------------------------------------
// T11 - BehaviorTree runtime
// ---------------------------------------------------------------------------

interface AgentList {
  success?: boolean;
  agents?: Array<{
    brainOwnerLabel?: string;
    pawnLabel?: string;
    isBehaviorTree?: boolean;
    running?: boolean;
    currentTree?: string;
    activeNode?: string;
    blackboardKeyCount?: number;
  }>;
  count?: number;
  brainsFound?: number;
  actorsScanned?: number;
  note?: string;
}

describe("run_behavior_tree", () => {
  it("starts a tree through the controller, which is what creates the component", async () => {
    await ensurePie();
    const body = resultJson<{
      success?: boolean;
      started?: boolean;
      alreadyRunning?: boolean;
      running?: boolean;
      startRoute?: string;
      currentTree?: string;
      blackboardAsset?: string;
      rollback?: { method?: string };
      note?: string;
    }>(await call("gameplay", { action: "run_behavior_tree", actorLabel: PAWN, assetPath: TREE, world: "pie" }));

    expect(body.success).not.toBe(false);
    expect(body.started).toBe(true);
    expect(body.running).toBe(true);
    expect(body.startRoute).toContain("RunBehaviorTree");
    expect(body.currentTree).toContain("BT_MCPAILive");
    // The controller route also initialises the blackboard, which is the reason
    // it is preferred over StartTree on an existing component.
    expect(body.blackboardAsset).toContain("BB_MCPAILive");
    expect(body.rollback?.method).toBe("stop_behavior_tree");
  }, 300_000);

  it("does not restart a tree that is already the running root", async () => {
    await ensurePie();
    const body = resultJson<{ started?: boolean; alreadyRunning?: boolean; existed?: boolean }>(
      await call("gameplay", { action: "run_behavior_tree", actorLabel: PAWN, assetPath: TREE, world: "pie" }),
    );
    expect(body.alreadyRunning).toBe(true);
    expect(body.started).toBe(false);
    expect(body.existed).toBe(true);
  }, 300_000);

  it("refuses an executionMode it does not have", async () => {
    const body = resultJson<{ success?: boolean; error?: string }>(
      await call("gameplay", {
        action: "run_behavior_tree",
        actorLabel: PAWN,
        assetPath: TREE,
        executionMode: "sideways",
        world: "pie",
      }),
    );
    expect(body.success).toBe(false);
    expect(body.error).toContain("looped");
  }, 300_000);
});

describe("get_bt_runtime", () => {
  it("reports the executing node and the composites above it", async () => {
    await ensurePie();
    await sleep(1500);
    const body = resultJson<{
      success?: boolean;
      running?: boolean;
      paused?: boolean;
      inSubtree?: boolean;
      currentTree?: string;
      rootTree?: string;
      activeNode?: { nodeName?: string; nodeClass?: string; kind?: string; taskStatus?: string };
      ancestors?: Array<{ nodeName?: string; nodeClass?: string }>;
    }>(await call("gameplay", { action: "get_bt_runtime", actorLabel: PAWN, world: "pie" }));

    expect(body.success).not.toBe(false);
    expect(body.running).toBe(true);
    expect(body.paused).toBe(false);
    // Current versus root is what makes a running subtree visible at all.
    expect(body.currentTree).toBe(body.rootTree);
    expect(body.inSubtree).toBe(false);

    // The whole point: an authored tree that never ticks is otherwise invisible.
    expect(body.activeNode?.nodeName).toBe("MCPWait");
    expect(body.activeNode?.nodeClass).toBe("BTTask_Wait");
    expect(body.activeNode?.taskStatus).toBe("active");
    expect((body.ancestors ?? []).map((a) => a.nodeName)).toContain("MCPRoot");
  }, 300_000);
});

describe("list_ai_agents", () => {
  it("finds the running agent with its pawn, tree and active node", async () => {
    await ensurePie();
    const body = resultJson<AgentList>(
      await call("gameplay", { action: "list_ai_agents", world: "pie", behaviorTreeOnly: true }),
    );
    expect(body.success).not.toBe(false);
    expect(body.count).toBeGreaterThan(0);

    const agent = (body.agents ?? []).find((a) => a.pawnLabel === PAWN);
    expect(agent, "the possessed pawn's agent is not listed").toBeTruthy();
    expect(agent?.isBehaviorTree).toBe(true);
    expect(agent?.running).toBe(true);
    expect(agent?.currentTree).toContain("BT_MCPAILive");
    expect(agent?.blackboardKeyCount).toBe(3);
  }, 300_000);

  it("explains a zero result rather than returning a bare empty list", async () => {
    await ensurePie();
    const body = resultJson<AgentList>(
      await call("gameplay", { action: "list_ai_agents", world: "pie", classFilter: "NoSuchAgentClass" }),
    );
    expect(body.success).not.toBe(false);
    expect(body.count).toBe(0);
    expect(body.actorsScanned).toBeGreaterThan(0);
    expect(body.note).toBeTruthy();
  }, 300_000);
});

describe("live blackboard", () => {
  it("dumps every key with a typed value, not only the engine's description", async () => {
    await ensurePie();
    const body = resultJson<{
      success?: boolean;
      keyCount?: number;
      blackboardAsset?: string;
      keys?: Array<{ key?: string; type?: string; typedValue?: unknown; described?: string }>;
      debugInfo?: string;
    }>(await call("gameplay", { action: "get_live_blackboard", actorLabel: PAWN, world: "pie" }));

    expect(body.success).not.toBe(false);
    expect(body.keyCount).toBe(3);
    expect(body.blackboardAsset).toContain("BB_MCPAILive");

    const byName = Object.fromEntries((body.keys ?? []).map((k) => [k.key, k]));
    expect(byName.MCPFlag?.type).toBe("Bool");
    expect(byName.MCPFlag?.typedValue).toBe(false);
    expect(byName.MCPCount?.type).toBe("Int");
    expect(byName.MCPCount?.typedValue).toBe(0);
    // SelfActor is written by the engine when the tree starts, so it also proves
    // the blackboard was initialised rather than merely allocated.
    expect(byName.SelfActor?.type).toBe("Object");
  }, 300_000);

  it("writes through the typed accessor and reports the previous value", async () => {
    await ensurePie();
    const body = resultJson<{
      success?: boolean;
      changed?: boolean;
      alreadySet?: boolean;
      previous?: { typedValue?: unknown };
      current?: { typedValue?: unknown };
      rollback?: { method?: string };
    }>(await call("gameplay", { action: "set_live_blackboard", actorLabel: PAWN, key: "MCPCount", value: 7, world: "pie" }));

    expect(body.success).not.toBe(false);
    expect(body.changed).toBe(true);
    expect(body.previous?.typedValue).toBe(0);
    expect(body.current?.typedValue).toBe(7);
    expect(body.rollback?.method).toBe("set_live_blackboard");
  }, 300_000);

  it("reports alreadySet rather than notifying observers a second time", async () => {
    // A raw property write would not notify at all; a redundant notify would
    // re-evaluate every observing decorator for nothing.
    await ensurePie();
    const body = resultJson<{ success?: boolean; changed?: boolean; alreadySet?: boolean }>(
      await call("gameplay", { action: "set_live_blackboard", actorLabel: PAWN, key: "MCPCount", value: 7, world: "pie" }),
    );
    expect(body.success).not.toBe(false);
    expect(body.changed).toBe(false);
    expect(body.alreadySet).toBe(true);
  }, 300_000);

  it("lists the real keys when the name is wrong", async () => {
    await ensurePie();
    const body = resultJson<{ success?: boolean; error?: string }>(
      await call("gameplay", { action: "set_live_blackboard", actorLabel: PAWN, key: "NoSuchKey", value: 1, world: "pie" }),
    );
    expect(body.success).toBe(false);
    expect(body.error).toContain("MCPCount");
    expect(body.error).toContain("case-sensitive");
  }, 300_000);
});

describe("stop_behavior_tree", () => {
  it("pauses, is idempotent, and resumes", async () => {
    await ensurePie();
    const paused = resultJson<{ success?: boolean; paused?: boolean; alreadyPaused?: boolean }>(
      await call("gameplay", { action: "stop_behavior_tree", actorLabel: PAWN, mode: "pause", world: "pie" }),
    );
    expect(paused.success).not.toBe(false);
    expect(paused.paused).toBe(true);
    expect(paused.alreadyPaused).toBe(false);

    const again = resultJson<{ alreadyPaused?: boolean }>(
      await call("gameplay", { action: "stop_behavior_tree", actorLabel: PAWN, mode: "pause", world: "pie" }),
    );
    expect(again.alreadyPaused).toBe(true);

    const resumed = resultJson<{ success?: boolean; running?: boolean }>(
      await call("gameplay", { action: "stop_behavior_tree", actorLabel: PAWN, mode: "resume", world: "pie" }),
    );
    expect(resumed.success).not.toBe(false);
    expect(resumed.running).toBe(true);
  }, 300_000);

  it("stops, and a second stop reports alreadyStopped", async () => {
    await ensurePie();
    const stopped = resultJson<{ success?: boolean; running?: boolean; alreadyStopped?: boolean }>(
      await call("gameplay", { action: "stop_behavior_tree", actorLabel: PAWN, mode: "stop", world: "pie" }),
    );
    expect(stopped.success).not.toBe(false);
    expect(stopped.running).toBe(false);
    expect(stopped.alreadyStopped).toBe(false);

    const again = resultJson<{ success?: boolean; alreadyStopped?: boolean }>(
      await call("gameplay", { action: "stop_behavior_tree", actorLabel: PAWN, mode: "stop", world: "pie" }),
    );
    expect(again.success).not.toBe(false);
    expect(again.alreadyStopped).toBe(true);
  }, 300_000);

  it("lists the real modes when the mode is wrong", async () => {
    const body = resultJson<{ success?: boolean; error?: string }>(
      await call("gameplay", { action: "stop_behavior_tree", actorLabel: PAWN, mode: "obliterate", world: "pie" }),
    );
    expect(body.success).toBe(false);
    expect(body.error).toContain("pause");
    expect(body.error).toContain("resume");
  }, 300_000);
});
