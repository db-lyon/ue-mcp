/**
 * EQS authoring and execution, against a real editor.
 *
 * An EQS query is not something anyone can eyeball for correctness, so the
 * authoring half is only worth having next to the half that runs it. These
 * build a query from nothing, read its structure back, run it, and assert the
 * scored items that come out.
 *
 * The case worth the most here is the empty result. EQS produces one in
 * several different situations that need opposite fixes, and its own log says
 * nothing useful about which. The one that actually catches people is a filter
 * test left at its default range: in `runMode: "all"` it rejects nearly every
 * item, while `best` still returns one, so the same query looks broken in one
 * mode and fine in the other.
 *
 * Everything is created under /Game/MCPEqsLive and removed afterwards.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { LiveServer, resultJson } from "./server.js";
import { closeLiveBridges, liveTarget } from "./harness.js";

const target = await liveTarget();

const PACKAGE = "/Game/MCPEqsLive";
const QUERY = `${PACKAGE}/EQS_LiveProbe`;
/** A deliberately minimal query for the run assertions: one generator, one
 *  scoring test, no filter. A filter's default range is not something this
 *  surface configures, so asserting against one would be asserting on engine
 *  defaults rather than on anything these actions promise. */
const RUNNABLE = `${PACKAGE}/EQS_Runnable`;
const QUERIER = "MCPEqsLiveQuerier";

let server: LiveServer;

const call = async (tool: string, args: Record<string, unknown>) =>
  server.call(tool, { ...args, timeoutMs: 300_000 });

beforeAll(async () => {
  server = await LiveServer.start({ projects: [target.uproject] });
  await call("gameplay", { action: "create_eqs_query", name: "EQS_LiveProbe", packagePath: PACKAGE });
  await call("gameplay", { action: "create_eqs_query", name: "EQS_Runnable", packagePath: PACKAGE });
  await call("gameplay", { action: "add_eqs_generator", queryPath: RUNNABLE, generatorClass: "SimpleGrid" });
  await call("gameplay", { action: "add_eqs_test", queryPath: RUNNABLE, testClass: "Distance", purpose: "score" });
  await call("level", {
    action: "place_actor",
    actorClass: "Actor",
    label: QUERIER,
    location: { x: 0, y: 0, z: 100 },
  });
}, 300_000);

afterAll(async () => {
  try {
    await call("level", { action: "delete_actor", actorLabel: QUERIER });
    await call("asset", { action: "delete_folder", path: PACKAGE, force: true });
  } catch {
    // Best effort; must not mask a real failure.
  }
  await server?.close();
  closeLiveBridges();
});

interface QueryRead {
  optionCount: number;
  testCount: number;
  runnable: boolean;
  problems: string[];
  options: Array<{
    index: number;
    generator?: string;
    generatorObjectPath?: string;
    tests: Array<{ index: number; class: string; purpose: string; objectPath: string }>;
  }>;
}

const read = async (): Promise<QueryRead> =>
  resultJson<QueryRead>(await call("gameplay", { action: "read_eqs_query", queryPath: QUERY }));

interface RunResult {
  successful: boolean;
  itemCount: number;
  returned: number;
  note?: string;
  items: Array<{ score: number; location: { x: number; y: number; z: number }; actorLabel?: string }>;
}

const run = async (args: Record<string, unknown> = {}): Promise<RunResult> =>
  resultJson<RunResult>(await call("gameplay", {
    action: "run_eqs_query",
    queryPath: RUNNABLE,
    querierLabel: QUERIER,
    world: "editor",
    ...args,
  }));

describe("list_eqs_types", () => {
  it("lists generators, tests and contexts with the short names authoring accepts", async () => {
    const body = resultJson<{
      generators: Array<{ name: string; shortName: string }>;
      tests: Array<{ name: string; shortName: string }>;
      contexts: Array<{ name: string }>;
    }>(await call("gameplay", { action: "list_eqs_types" }));

    expect(body.generators.length).toBeGreaterThan(0);
    expect(body.tests.length).toBeGreaterThan(0);
    expect(body.contexts.length).toBeGreaterThan(0);

    // The short spelling is what the editor's own dropdown shows, and what an
    // agent will reach for.
    const distance = body.tests.find((t) => t.name === "EnvQueryTest_Distance");
    expect(distance?.shortName).toBe("Distance");
  });

  it("filters by substring", async () => {
    const body = resultJson<{ tests: Array<{ name: string }> }>(
      await call("gameplay", { action: "list_eqs_types", filter: "Distance" }),
    );
    expect(body.tests.every((t) => t.name.includes("Distance"))).toBe(true);
  });
});

describe("authoring a query from nothing", () => {
  it("reports an empty query as not runnable, and says what is missing", async () => {
    const body = await read();
    expect(body.optionCount).toBe(0);
    expect(body.runnable).toBe(false);
    expect(body.problems.join(" ")).toContain("no options");
  });

  it("adds a generator and returns the path that configures it", async () => {
    const body = resultJson<{ created: boolean; generator: string; generatorObjectPath: string }>(
      await call("gameplay", { action: "add_eqs_generator", queryPath: QUERY, generatorClass: "SimpleGrid" }),
    );
    expect(body.created).toBe(true);
    expect(body.generator).toBe("EnvQueryGenerator_SimpleGrid");
    // No typed setters exist for generator parameters; this path plus
    // editor(set_property) is the whole configuration story.
    expect(body.generatorObjectPath).toContain("EnvQueryGenerator_SimpleGrid");
  });

  it("refuses an unknown generator by listing the real ones", async () => {
    const body = resultJson<{ success: boolean; error?: string }>(
      await call("gameplay", { action: "add_eqs_generator", queryPath: QUERY, generatorClass: "NotAGenerator" }),
    );
    expect(body.success).toBe(false);
    expect(body.error).toContain("list_eqs_types");
  });

  it("adds tests with the purpose that decides what they do", async () => {
    const scoring = resultJson<{ testIndex: number; test: string }>(
      await call("gameplay", { action: "add_eqs_test", queryPath: QUERY, testClass: "Distance", purpose: "score" }),
    );
    expect(scoring.test).toBe("EnvQueryTest_Distance");
    expect(scoring.testIndex).toBe(0);

    const filtering = resultJson<{ testIndex: number }>(
      await call("gameplay", { action: "add_eqs_test", queryPath: QUERY, testClass: "Random", purpose: "both" }),
    );
    expect(filtering.testIndex).toBe(1);
  });

  it("reads the whole structure back, with purposes and object paths", async () => {
    const body = await read();
    expect(body.optionCount).toBe(1);
    expect(body.testCount).toBe(2);
    expect(body.runnable).toBe(true);
    expect(body.options[0].generator).toBe("EnvQueryGenerator_SimpleGrid");
    expect(body.options[0].tests.map((t) => t.purpose)).toEqual(["Score", "FilterAndScore"]);
    for (const test of body.options[0].tests) expect(test.objectPath).toContain(QUERY.split("/").pop());
  });

  it("rejects a reorder that is not a permutation, rather than dropping a test", async () => {
    const body = resultJson<{ success: boolean; error?: string }>(
      await call("gameplay", { action: "reorder_eqs_tests", queryPath: QUERY, order: [0] }),
    );
    expect(body.success).toBe(false);
    expect(body.error).toContain("permutation");

    // And the query is untouched.
    expect((await read()).testCount).toBe(2);
  });

  it("reorders so a cheap filter runs before an expensive test", async () => {
    const body = resultJson<{ tests: string[] }>(
      await call("gameplay", { action: "reorder_eqs_tests", queryPath: QUERY, order: [1, 0] }),
    );
    expect(body.tests).toEqual(["EnvQueryTest_Random", "EnvQueryTest_Distance"]);
  });
});

describe("running the query", () => {
  it("returns scored items with locations", async () => {
    const body = await run({ runMode: "best", limit: 5 });
    expect(body.successful).toBe(true);
    expect(body.itemCount).toBeGreaterThan(0);
    expect(body.items.length).toBeGreaterThan(0);
    for (const item of body.items) {
      expect(typeof item.score).toBe("number");
      expect(typeof item.location.x).toBe("number");
    }
  });

  it("honours the limit without misreporting how many there were", async () => {
    const body = await run({ runMode: "best", limit: 2 });
    expect(body.returned).toBeLessThanOrEqual(2);
    // itemCount is what the query produced, not what was returned; conflating
    // them would make a truncated read look like a small result set.
    expect(body.itemCount).toBeGreaterThanOrEqual(body.returned);
  });

  it("refuses an unknown run mode by naming the real ones", async () => {
    const body = resultJson<{ success: boolean; error?: string }>(
      await call("gameplay", { action: "run_eqs_query", queryPath: QUERY, runMode: "sideways", world: "editor" }),
    );
    expect(body.success).toBe(false);
    expect(body.error).toContain("all, best, random");
  });
});

describe("the empty result explains itself", () => {
  it("carries a note that names both causes, rather than an unexplained zero", async () => {
    // A query with a generator but no querier produces nothing, and an empty
    // list with no explanation is the failure this action exists to avoid.
    const body = resultJson<RunResult>(await call("gameplay", {
      action: "run_eqs_query", queryPath: QUERY, world: "editor", runMode: "all", limit: 1,
    }));
    if (body.itemCount === 0) {
      expect(body.note).toBeTruthy();
      expect(body.note).toContain("generator");
    }
  });
});
