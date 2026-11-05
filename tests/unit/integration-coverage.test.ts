/**
 * The integration-coverage gate: every user-facing surface that is not a
 * bridge action (tests/coverage/integration-surfaces.json) has a test that
 * drives the real thing and asserts on it, a reasoned exemption, or a row in
 * the gap list that may only shrink. Bridge actions are gated separately, by
 * tests/unit/behavioural-coverage.test.ts.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, describe, expect, it } from "vitest";
import {
  buildSurfaceInventory,
  readInputs,
  scanSurfaceClaims,
  serializeSurfaceInventory,
  surfaceProblems,
  type Surface,
  type SurfaceInventory,
} from "../../scripts/integration-coverage.js";
import { INTEGRATION_INVENTORY_FILE, REPO_ROOT } from "../../scripts/lib/behavioural-ratchet.mjs";

const inputs = readInputs();
const inventory = buildSurfaceInventory(inputs);

describe("the committed integration inventory", () => {
  it("has no unexplained surface, unknown claim, stale gap or stale exemption", () => {
    expect(surfaceProblems(inventory, inputs)).toEqual([]);
  });

  it("is what the generator produces from the tree as it stands", () => {
    const committed = fs.readFileSync(path.join(REPO_ROOT, INTEGRATION_INVENTORY_FILE), "utf8");
    expect(
      committed === serializeSurfaceInventory(inventory),
      `${INTEGRATION_INVENTORY_FILE} is stale. Run npm run coverage:integration and commit what it changes.`,
    ).toBe(true);
  });

  it("reports the covered share", () => {
    const s = inventory.summary;
    console.log(
      `integration coverage: ${s.covered}/${s.surfaces - s.exempt} non-exempt surfaces (${s.coveredShare}%), ` +
        `${s.exempt} exempt, ${s.gaps} grandfathered gaps`,
    );
    expect(s.covered + s.exempt + s.gaps + s.unexplained).toBe(s.surfaces);
  });
});

const scratch = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-icov-"));
afterAll(() => fs.rmSync(scratch, { recursive: true, force: true }));

describe("the claim scanner", () => {
  const dir = path.join(scratch, "integration");
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(
    path.join(dir, "a.test.ts"),
    `
      import { describe, expect, it } from "vitest";
      import { covers } from "../helpers/covers.js";
      describe("init", () => {
        covers("cli.init.yes");
        it("writes the config", async () => {
          const r = await runCli(["init", "--yes"]);
          expect(fs.existsSync("ue-mcp.yml")).toBe(true);
        });
        it("only checks the call answered", async () => {
          covers("cli.only-ok");
          const r = await call();
          expect(r.ok).toBe(true);
        });
      });
      it("claims its own surface", () => {
        covers("cli.status", "cli.status.json");
        expect(report.kind).toBe("source");
      });
      it("does not inherit a sibling describe's claim", () => {
        expect(1).toBe(1);
      });
    `,
  );
  const { credits, claims } = scanSurfaceClaims([{ suite: "integration", dir }], scratch);

  it("credits every asserting test in a describe that claims a surface", () => {
    expect(credits.get("cli.init.yes")?.map((r) => r.test)).toEqual(["writes the config"]);
  });

  it("credits a claim in the test body, several ids at once", () => {
    expect(credits.get("cli.status")?.map((r) => r.test)).toEqual(["claims its own surface"]);
    expect(credits.get("cli.status.json")?.map((r) => r.test)).toEqual(["claims its own surface"]);
  });

  it("does not credit a test that asserts only that the call answered", () => {
    expect(credits.has("cli.only-ok")).toBe(false);
  });

  it("records every claim, for the unknown-id check", () => {
    expect(claims.map((c) => c.id).sort()).toEqual(["cli.init.yes", "cli.only-ok", "cli.status", "cli.status.json"]);
    expect(claims[0].file).toBe("integration/a.test.ts");
  });
});

function fake(rows: Array<[string, SurfaceInventory["surfaces"][number]["status"]]>, unknownClaims: SurfaceInventory["unknownClaims"] = []): {
  inv: SurfaceInventory;
  manifest: Surface[];
} {
  const manifest = rows.map(([id]) => ({ id, group: "cli", summary: "a surface a user drives" }));
  return {
    manifest,
    inv: {
      note: "",
      summary: { surfaces: rows.length, covered: 0, exempt: 0, gaps: 0, unexplained: 0, coveredShare: 0 },
      unknownClaims,
      surfaces: rows.map(([id, status]) => ({
        id,
        group: "cli",
        summary: "a surface a user drives",
        status,
        tests: status === "covered" ? [{ suite: "integration" as const, file: "tests/integration/x.test.ts", test: "t" }] : [],
      })),
    },
  };
}

describe("the integration gate", () => {
  const reason = "Needs a signed-in GitHub account and a real issue; no local fake can stand in.";

  it("refuses a new surface with no test, exemption or gap row", () => {
    const { inv, manifest } = fake([["cli.new", "unexplained"]]);
    expect(surfaceProblems(inv, { manifest, exemptions: {}, ratchet: [] }).join("\n")).toMatch(/cli\.new/);
  });

  it("refuses a claim on an id the manifest does not list", () => {
    const { inv, manifest } = fake([], [{ id: "cli.typo", file: "tests/integration/x.test.ts" }]);
    expect(surfaceProblems(inv, { manifest, exemptions: {}, ratchet: [] }).join("\n")).toMatch(/covers\("cli\.typo"\)/);
  });

  it("refuses a gap row once the surface is covered, so the list shrinks", () => {
    const { inv, manifest } = fake([["cli.now", "covered"]]);
    expect(surfaceProblems(inv, { manifest, exemptions: {}, ratchet: ["cli.now"] }).join("\n")).toMatch(/now covered/);
  });

  it("refuses a gap row the manifest no longer has", () => {
    const { inv, manifest } = fake([]);
    expect(surfaceProblems(inv, { manifest, exemptions: {}, ratchet: ["cli.gone"] }).join("\n")).toMatch(/no longer has/);
  });

  it("refuses an exemption without a reason worth the name, and one for a covered surface", () => {
    const short = fake([["cli.quiet", "exempt"]]);
    expect(surfaceProblems(short.inv, { manifest: short.manifest, exemptions: { "cli.quiet": "hard" }, ratchet: [] }).join("\n")).toMatch(/reason for cli\.quiet/);
    const cov = fake([["cli.done", "covered"]]);
    expect(surfaceProblems(cov.inv, { manifest: cov.manifest, exemptions: { "cli.done": reason }, ratchet: [] }).join("\n")).toMatch(/Drop the exemption/);
  });

  it("refuses a malformed or duplicated id", () => {
    const { inv } = fake([]);
    const manifest = [
      { id: "Bad Id", group: "cli", summary: "a surface a user drives" },
      { id: "cli.twice", group: "cli", summary: "a surface a user drives" },
      { id: "cli.twice", group: "cli", summary: "a surface a user drives" },
    ];
    const text = surfaceProblems(inv, { manifest, exemptions: {}, ratchet: [] }).join("\n");
    expect(text).toMatch(/"Bad Id" is not a dotted surface id/);
    expect(text).toMatch(/cli\.twice more than once/);
  });

  it("accepts a reasoned exemption and a still-open gap", () => {
    const { inv, manifest } = fake([["cli.exempt", "exempt"], ["cli.gap", "gap"]]);
    expect(surfaceProblems(inv, { manifest, exemptions: { "cli.exempt": reason }, ratchet: ["cli.gap"] })).toEqual([]);
  });
});
