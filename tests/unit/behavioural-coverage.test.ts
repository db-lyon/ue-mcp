/**
 * The behavioural-coverage gate (#1223).
 *
 * Every registered bridge action has a release-gated test that dispatches it
 * and asserts on the result, a reasoned exemption, or a row in the gap list
 * that may only shrink. A new action with none of the three fails here, so a
 * gap is a visible row rather than a silence.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, describe, expect, it } from "vitest";
import {
  buildInventory,
  inventoryProblems,
  scanAutomationTests,
  scanLiveTests,
  serializeInventory,
  type Inventory,
} from "../../scripts/behavioural-coverage.js";
import {
  INVENTORY_FILE,
  REPO_ROOT,
  ratchetGrowth,
  readExemptions,
  readRatchet,
} from "../../scripts/lib/behavioural-ratchet.mjs";

const inputs = { exemptions: readExemptions(), ratchet: readRatchet() };
const inventory = buildInventory(inputs);

describe("the committed inventory", () => {
  it("has no unexplained action, stale gap or stale exemption", () => {
    expect(inventoryProblems(inventory, inputs)).toEqual([]);
  });

  it("is what the generator produces from the tree as it stands", () => {
    const committed = fs.readFileSync(path.join(REPO_ROOT, INVENTORY_FILE), "utf8");
    expect(
      committed === serializeInventory(inventory),
      `${INVENTORY_FILE} is stale. Run npm run coverage:behavioural and commit what it changes.`,
    ).toBe(true);
  });

  it("reports the covered share of non-exempt actions", () => {
    const s = inventory.summary;
    console.log(
      `behavioural coverage: ${s.covered}/${s.actions - s.exempt} non-exempt actions (${s.coveredShare}%), ` +
        `${s.exempt} exempt, ${s.gaps} grandfathered gaps`,
    );
    expect(s.covered + s.exempt + s.gaps + s.unexplained).toBe(s.actions);
    expect(s.covered).toBeGreaterThan(0);
  });
});

// ---------------------------------------------------------------------------
// What the scanner credits
// ---------------------------------------------------------------------------

const scratch = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-bcov-"));
afterAll(() => fs.rmSync(scratch, { recursive: true, force: true }));

function fixtureDir(name: string, files: Record<string, string>): string {
  const dir = path.join(scratch, name);
  fs.mkdirSync(dir, { recursive: true });
  for (const [file, text] of Object.entries(files)) fs.writeFileSync(path.join(dir, file), text);
  return dir;
}

const METHODS = new Set(["read_thing", "write_thing", "make_fixture", "list_things", "forwarded_thing", "category_thing", "skipped_thing"]);
const ACTIONS = new Map([["asset", new Map([["read", "category_thing"]])]]);

describe("the live-test scanner", () => {
  const dir = fixtureDir("live", {
    "a.test.ts": `
      import { beforeAll, describe, expect, it } from "vitest";
      beforeAll(async () => { await callBridge(bridge, "make_fixture", {}); });
      async function survive(method: string, params: object) { return callBridge(bridge, method, params); }
      const readIt = async () => callBridge(bridge, "list_things", {});

      it("reads a thing back", async () => {
        const r = await callBridge(bridge, "read_thing", { assetPath: "/Game/X" });
        expect(r.result.value).toBe(3);
      });
      it("only checks that the call answered", async () => {
        const r = await callBridge(bridge, "write_thing", {});
        expect(r.ok, r.error).toBe(true);
      });
      it("asserts through a helper", async () => {
        expect((await readIt()).result.count).toBe(1);
        await survive("forwarded_thing", {});
      });
      it("goes through the category tool", async () => {
        const r = await server.call("asset", { action: "read", assetPath: "/Game/X" });
        expect(r.isError).toBe(false);
      });
      it.skip("is skipped while it fails", async () => {
        expect((await callBridge(bridge, "skipped_thing", {})).result.value).toBe(1);
      });
      test.todo("is not written yet", async () => {
        expect((await callBridge(bridge, "skipped_thing", {})).result.value).toBe(2);
      });
      describe("a group", () => {
        beforeAll(async () => { await callBridge(bridge, "write_thing", {}); });
        it("asserts on what its hook wrote", () => { expect(state).toEqual({}); });
      });
    `,
  });
  const found = scanLiveTests(METHODS, ACTIONS, dir);

  it("credits a test body that dispatches the action and asserts on the result", () => {
    expect(found.get("read_thing")?.map((r) => r.test)).toEqual(["reads a thing back"]);
  });

  it("does not credit a test marked skip or todo, which never runs", () => {
    expect(found.has("skipped_thing")).toBe(false);
  });

  it("does not credit a file-level fixture", () => {
    expect(found.has("make_fixture")).toBe(false);
  });

  it("does not count `.ok` as an assertion about behaviour", () => {
    expect(found.get("write_thing")?.map((r) => r.test)).not.toContain("only checks that the call answered");
  });

  it("follows helpers in the same file, including ones handed the method by name", () => {
    expect(found.get("list_things")?.map((r) => r.test)).toEqual(["asserts through a helper"]);
    expect(found.get("forwarded_thing")?.map((r) => r.test)).toEqual(["asserts through a helper"]);
  });

  it("resolves a category action to its bridge method", () => {
    expect(found.get("category_thing")?.map((r) => r.test)).toEqual(["goes through the category tool"]);
  });

  it("credits a describe-scoped hook to the describe whose tests assert", () => {
    expect(found.get("write_thing")?.map((r) => r.test)).toEqual(["a group"]);
  });
});

describe("the automation-test scanner", () => {
  const dir = fixtureDir("cpp", {
    "Tests.cpp": `
      IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReads, "UE.MCP.Thing.Reads", Flags)
      IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegistered, "UE.MCP.Thing.Registration", Flags)
      IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNamesOnly, "UE.MCP.Thing.NamesOnly", Flags)
      namespace { constexpr const TCHAR* ListMethod = TEXT("list_things"); }
      bool FReads::RunTest(const FString& Parameters)
      {
        TSharedPtr<FJsonValue> R = Registry.ExecuteHandler(TEXT("read_thing"), Params);
        Registry.ExecuteHandler(ListMethod, Params);
        TestEqual(TEXT("value"), Value, 3);
        return true;
      }
      bool FRegistered::RunTest(const FString& Parameters)
      {
        TSharedPtr<FJsonValue> R = Registry.ExecuteHandler(TEXT("write_thing"), Params);
        TestFalse(TEXT("refused"), Ok);
        return true;
      }
      bool FNamesOnly::RunTest(const FString& Parameters)
      {
        TestTrue(TEXT("registered"), Registry.HasHandler(TEXT("make_fixture")));
        return true;
      }
    `,
  });
  const found = scanAutomationTests(METHODS, new Map(), dir);

  it("credits a dispatch by literal or by named constant in an asserting RunTest", () => {
    expect(found.get("read_thing")?.map((r) => r.test)).toEqual(["UE.MCP.Thing.Reads"]);
    expect(found.get("list_things")?.map((r) => r.test)).toEqual(["UE.MCP.Thing.Reads"]);
  });

  it("does not credit registration or validation suites", () => {
    expect(found.has("write_thing")).toBe(false);
  });

  it("does not credit an action only named in HasHandler", () => {
    expect(found.has("make_fixture")).toBe(false);
  });
});

// ---------------------------------------------------------------------------
// What the gate refuses
// ---------------------------------------------------------------------------

function fakeInventory(rows: Array<[string, Inventory["actions"][number]["status"]]>): Inventory {
  return {
    note: "",
    summary: { actions: rows.length, covered: 0, exempt: 0, gaps: 0, unexplained: 0, coveredShare: 0 },
    actions: rows.map(([method, status]) => ({
      method,
      category: "asset",
      registeredIn: "AssetHandlers.cpp",
      status,
      tests: status === "covered" ? [{ suite: "live" as const, file: "tests/live/x.test.ts", test: "t" }] : [],
    })),
  };
}

describe("the gate", () => {
  const reason = "No editor-side state this action changes is observable from a test.";

  it("refuses a new action with no test, exemption or gap row", () => {
    const problems = inventoryProblems(fakeInventory([["new_action", "unexplained"]]), { exemptions: {}, ratchet: [] });
    expect(problems.join("\n")).toMatch(/new_action/);
  });

  it("refuses a gap row once the action is covered, so the list shrinks", () => {
    const problems = inventoryProblems(fakeInventory([["now_covered", "covered"]]), { exemptions: {}, ratchet: ["now_covered"] });
    expect(problems.join("\n")).toMatch(/now covered/);
  });

  it("refuses a gap row for an action that is no longer registered", () => {
    const problems = inventoryProblems(fakeInventory([]), { exemptions: {}, ratchet: ["gone"] });
    expect(problems.join("\n")).toMatch(/no longer registered/);
  });

  it("refuses an exemption without a reason worth the name", () => {
    const problems = inventoryProblems(fakeInventory([["quiet", "exempt"]]), { exemptions: { quiet: "internal" }, ratchet: [] });
    expect(problems.join("\n")).toMatch(/reason for quiet/);
  });

  it("refuses an exemption for an action a test now covers", () => {
    const problems = inventoryProblems(fakeInventory([["covered", "covered"]]), { exemptions: { covered: reason }, ratchet: [] });
    expect(problems.join("\n")).toMatch(/Drop the exemption/);
  });

  it("refuses a mutating action in the gap list", () => {
    const problems = inventoryProblems(fakeInventory([["writes_thing", "gap"]]), {
      exemptions: {},
      ratchet: ["writes_thing"],
      mutating: new Set(["writes_thing"]),
    });
    expect(problems.join("\n")).toMatch(/mutating action\(s\): writes_thing/);
  });

  it("accepts a reasoned exemption and a still-open read-only gap", () => {
    const problems = inventoryProblems(fakeInventory([["exempt_one", "exempt"], ["gap_one", "gap"]]), {
      exemptions: { exempt_one: reason },
      ratchet: ["gap_one"],
      mutating: new Set(),
    });
    expect(problems).toEqual([]);
  });

  it("names every gap row added since the baseline", () => {
    expect(ratchetGrowth(["a", "b"], ["a", "c", "b", "d"])).toEqual(["c", "d"]);
    expect(ratchetGrowth(["a", "b"], ["a"])).toEqual([]);
  });
});
