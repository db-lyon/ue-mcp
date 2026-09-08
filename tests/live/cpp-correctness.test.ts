/**
 * The C++ correctness actions, against a real installed engine.
 *
 * These read the engine tree and the project's own sources in the server
 * process, so unlike the rest of this suite they do not need a running editor.
 * They do need a real engine, which is why they live here rather than in the
 * unit tests: the recognition rules are pinned against fixtures in
 * `tests/unit/engine-index.test.ts`, and what is asserted HERE is that those
 * rules land on the right answer when pointed at 31,000 real headers.
 *
 * The index cache is redirected to a gitignored directory under the repo
 * rather than to the per-user one, so a run cannot be affected by whatever the
 * developer's own cache happens to hold, and so the build cost is paid once
 * across runs instead of once per run. The first run on a machine takes
 * several minutes; every run after it is seconds.
 *
 * Every case here is a read. Nothing writes to the project.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { LiveServer, resultJson, REPO_ROOT } from "./server.js";

/** Persisted between runs: rebuilding a 31,000-header index per run is minutes. */
const CACHE_HOME = path.join(REPO_ROOT, ".engine-index-cache");
const TEST_PROJECT = path.join(REPO_ROOT, "tests", "ue_mcp", "ue_mcp.uproject");

/** Long enough for a cold build on a machine that has never scanned the tree. */
const COLD = 1_800_000;

/** The symbol the usage cases search for, warmed once in `beforeAll`. */
const USAGE_SYMBOL = "GetPlayerPawn";

let server: LiveServer;
let scratch: string;

beforeAll(async () => {
  fs.mkdirSync(CACHE_HOME, { recursive: true });
  scratch = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-cpp-lint-"));
  server = await LiveServer.start({
    projects: [TEST_PROJECT],
    env: { UE_MCP_USER_STATE: path.join(CACHE_HOME, "state.json") },
  });
  // Pay the build once for the file, so each case measures a query.
  await server.call("project", { action: "build_engine_index", timeoutMs: COLD });
  // And pay the usage search's own one-time cost here for the same reason.
  // On a launcher install there are no engine .cpp files, so the fallback has
  // to consider every header in the Runtime tree, and a header this machine
  // has never opened costs milliseconds of first-touch I/O whatever the code
  // does with it: about 3 ms each, measured, against 11,000 headers. That is
  // tens of seconds the first time and under a second every time after, since
  // the OS keeps the pages. It belongs in the hook, which is already where
  // this file pays one-time costs, rather than inside a per-case timeout,
  // where it is what used to make these two cases fail on a cold disk.
  await server.call("project", {
    action: "find_example_usage", symbol: USAGE_SYMBOL, limit: 5, timeoutMs: COLD,
  });
}, COLD);

afterAll(async () => {
  await server?.close();
  if (scratch) fs.rmSync(scratch, { recursive: true, force: true });
});

interface Verdict {
  name: string;
  found: boolean;
  kind?: string;
  module?: string;
  include?: string;
  signature?: string;
  suggestions?: string[];
}
interface VerifyBody {
  missing: string[];
  includes: string[];
  modules: string[];
  symbols: Verdict[];
}

const verify = async (names: string[]): Promise<VerifyBody> =>
  resultJson<VerifyBody>(await server.call("project", { action: "verify_symbols", names, timeoutMs: COLD }));

describe("the index answers from a real engine", () => {
  it("reports what it scanned, and serves the second call from cache", async () => {
    const body = resultJson<{
      source: string; headerCount: number; symbolCount: number; trees: string[]; engineVersion: string;
    }>(await server.call("project", { action: "build_engine_index", timeoutMs: COLD }));

    expect(body.source).toBe("cache");
    expect(body.trees).toContain("Plugins");
    expect(body.headerCount).toBeGreaterThan(10_000);
    expect(body.symbolCount).toBeGreaterThan(50_000);
    expect(body.engineVersion).toMatch(/^\d+\.\d+/);
  });

  it("resolves an engine class to the header you would actually include", async () => {
    // AActor is named in dozens of exported public headers and defined in one.
    const [actor] = (await verify(["AActor"])).symbols;
    expect(actor.found).toBe(true);
    expect(actor.kind).toBe("class");
    expect(actor.module).toBe("Engine");
    expect(actor.include).toBe("GameFramework/Actor.h");
  });

  it("resolves plugin types, which is most of what gets asked", async () => {
    // Gameplay Abilities, Niagara and PCG are plugins. An index of
    // Engine/Source alone answers none of these.
    const body = await verify(["UAbilitySystemComponent", "UGameplayAbility", "UNiagaraComponent"]);
    expect(body.missing).toEqual([]);
    const byName = new Map(body.symbols.map((s) => [s.name, s]));
    expect(byName.get("UAbilitySystemComponent")?.module).toBe("GameplayAbilities");
    expect(byName.get("UAbilitySystemComponent")?.include).toBe("AbilitySystemComponent.h");
    expect(byName.get("UNiagaraComponent")?.module).toBe("Niagara");
  });

  it("resolves a qualified member to its declaration", async () => {
    const [hit] = (await verify(["UGameplayStatics::GetPlayerPawn"])).symbols;
    expect(hit.found).toBe(true);
    expect(hit.kind).toBe("member");
    expect(hit.include).toBe("Kismet/GameplayStatics.h");
    expect(hit.signature).toContain("GetPlayerPawn");
  });

  it("separates a misspelled member from a misspelled class", async () => {
    const [hit] = (await verify(["UGameplayStatics::NoSuchMethodHere"])).symbols;
    expect(hit.found).toBe(false);
    expect(hit.suggestions?.join(" ")).toContain("UGameplayStatics exists");
  });

  it("says nothing rather than noise for a name with no neighbour", async () => {
    // Containment matching used to answer this with "Area", because "areal" is
    // a substring of it.
    const [hit] = (await verify(["FZzzNotARealTypeAtAll"])).symbols;
    expect(hit.found).toBe(false);
    expect(hit.suggestions ?? []).toEqual([]);
  });

  it("aggregates a batch into the whole edit a caller has to make", async () => {
    const body = await verify(["AActor", "UAbilitySystemComponent", "FHitResult"]);
    expect(body.missing).toEqual([]);
    expect(body.includes).toEqual(
      ["AbilitySystemComponent.h", "Engine/HitResult.h", "GameFramework/Actor.h"],
    );
    expect(body.modules).toEqual(["Engine", "GameplayAbilities"]);
  });
});

describe("suggest_build_deps against the real Build.cs", () => {
  const buildCs = path.join(REPO_ROOT, "tests", "ue_mcp", "Source", "ue_mcp", "ue_mcp.Build.cs");

  it("reports only the modules the file does not already list", async () => {
    const body = resultJson<{
      modules: string[]; missing: string[]; edit?: string;
      buildCs: { publicDeps: string[] };
    }>(await server.call("project", {
      action: "suggest_build_deps",
      names: ["AActor", "FGameplayTag", "UAbilitySystemComponent"],
      buildCsPath: buildCs,
      timeoutMs: COLD,
    }));

    // Engine and GameplayTags are already declared; GameplayAbilities is not.
    expect(body.buildCs.publicDeps).toContain("Engine");
    expect(body.modules).toContain("Engine");
    expect(body.missing).toEqual(["GameplayAbilities"]);
    expect(body.edit).toContain('"GameplayAbilities"');
  });

  it("omits Core and CoreUObject, which every module already has", async () => {
    const body = resultJson<{ modules: string[] }>(await server.call("project", {
      action: "suggest_build_deps",
      names: ["FVector", "UObject"],
      buildCsPath: buildCs,
      timeoutMs: COLD,
    }));
    expect(body.modules).not.toContain("Core");
    expect(body.modules).not.toContain("CoreUObject");
  });
});

describe("lint_cpp_header", () => {
  const write = (name: string, body: string): string => {
    const file = path.join(scratch, name);
    fs.writeFileSync(file, body);
    return file;
  };

  const lint = async (file: string): Promise<{
    ok: boolean; errorCount: number; findings: Array<{ rule: string; message: string }>;
    missingIncludes: string[]; missingModules: string[];
  }> => resultJson(await server.call("project", {
    action: "lint_cpp_header",
    path: file,
    buildCsPath: path.join(REPO_ROOT, "tests", "ue_mcp", "Source", "ue_mcp", "ue_mcp.Build.cs"),
    timeoutMs: COLD,
  }));

  it("catches the mistakes an agent actually makes, in one pass", async () => {
    const file = write("BadHeader.h", [
      `#include "CoreMinimal.h"`,
      `#include "BadHeader.generated.h"`,
      `#include "GameFramework/Actor.h"`,
      ``,
      `UCLASS()`,
      `class ABadActor : public AActor`,
      `{`,
      `\tUPROPERTY()`,
      `\tUAbilitySystemComponent* AbilitySystem;`,
      ``,
      `\tvoid DoThing(const FHitResult& Hit);`,
      `};`,
    ].join("\n"));

    const body = await lint(file);
    const rules = body.findings.map((f) => f.rule);

    expect(body.ok).toBe(false);
    // The generated header must be last, or UnrealHeaderTool fails with an
    // error that names neither this file nor this line.
    expect(rules).toContain("generated-include-not-last");
    expect(rules).toContain("missing-generated-body");
    expect(rules).toContain("missing-pragma-once");
    expect(body.missingIncludes).toContain("AbilitySystemComponent.h");
    expect(body.missingIncludes).toContain("Engine/HitResult.h");
    expect(body.missingModules).toContain("GameplayAbilities");
  });

  it("passes a header that is correct", async () => {
    const file = write("GoodHeader.h", [
      `#pragma once`,
      ``,
      `#include "CoreMinimal.h"`,
      `#include "GameFramework/Actor.h"`,
      `#include "GoodHeader.generated.h"`,
      ``,
      `UCLASS()`,
      `class AGoodActor : public AActor`,
      `{`,
      `\tGENERATED_BODY()`,
      `};`,
    ].join("\n"));

    const body = await lint(file);
    expect(body.findings.map((f) => f.message)).toEqual([]);
    expect(body.ok).toBe(true);
  });

  it("accepts a forward declaration in place of an include", async () => {
    // In a header a forward declaration is usually the right call, so
    // demanding the include would be wrong advice.
    const file = write("ForwardHeader.h", [
      `#pragma once`,
      ``,
      `#include "CoreMinimal.h"`,
      `#include "GameFramework/Actor.h"`,
      `#include "ForwardHeader.generated.h"`,
      ``,
      `class UAbilitySystemComponent;`,
      ``,
      `UCLASS()`,
      `class AForwardActor : public AActor`,
      `{`,
      `\tGENERATED_BODY()`,
      `\tUAbilitySystemComponent* Ability;`,
      `};`,
    ].join("\n"));

    const body = await lint(file);
    expect(body.missingIncludes).not.toContain("AbilitySystemComponent.h");
  });

  it("does not demand an include for a symbol named only in a comment", async () => {
    const file = write("CommentHeader.h", [
      `#pragma once`,
      ``,
      `#include "CoreMinimal.h"`,
      `#include "GameFramework/Actor.h"`,
      `#include "CommentHeader.generated.h"`,
      ``,
      `// Later this will hold a UAbilitySystemComponent.`,
      `UCLASS()`,
      `class ACommentActor : public AActor`,
      `{`,
      `\tGENERATED_BODY()`,
      `};`,
    ].join("\n"));

    const body = await lint(file);
    expect(body.missingIncludes).toEqual([]);
    expect(body.ok).toBe(true);
  });
});

describe("find_example_usage", () => {
  interface Usage {
    siteCount: number;
    engineSourcesAvailable: boolean;
    filesScanned: number;
    truncated?: boolean;
    note?: string;
    sites: Array<{ file: string; line: number; text: string; kind: string }>;
  }

  const usage = async (symbol: string): Promise<Usage> =>
    resultJson<Usage>(await server.call("project", {
      action: "find_example_usage", symbol, limit: 5, timeoutMs: COLD,
    }));

  it("finds uses, and says which kind of install it was able to search", async () => {
    // An engine installed from the Epic launcher ships headers and no .cpp at
    // all, which is the common case. Both outcomes are correct; what must not
    // happen is an empty list that reads as "nothing uses this symbol".
    const body = await usage(USAGE_SYMBOL);
    expect(typeof body.engineSourcesAvailable).toBe("boolean");

    if (body.engineSourcesAvailable) {
      expect(body.siteCount).toBeGreaterThan(0);
      expect(body.sites.some((s) => s.file.endsWith(".cpp"))).toBe(true);
      expect(body.note).toBeUndefined();
    } else {
      // A launcher install cannot answer fully, and has to say so rather than
      // implying the symbol is unused.
      expect(body.note).toMatch(/without \.cpp sources/);
    }
  });

  it("never returns a comment or a declaration as an example of use", async () => {
    const body = await usage(USAGE_SYMBOL);
    for (const site of body.sites) {
      expect(site.text).toContain(USAGE_SYMBOL);
      expect(site.text.startsWith("//")).toBe(false);
      expect(site.text).not.toMatch(/^\s*(?:class|struct|virtual|static|template)\b/);
    }
  });

  it("reads the Runtime tree once, and finishes inside its budget", async () => {
    // The number of files read IS the cost of this action, so it is asserted
    // rather than inferred from a stopwatch. On a launcher install that is the
    // whole Runtime tree, because "used nowhere in a header" cannot be
    // established without looking at every header. What must not happen is
    // that the number grows: walking a tree twice, or reading headers on an
    // install that does have sources, both show up here as a jump in it.
    const body = await usage(USAGE_SYMBOL);
    if (body.engineSourcesAvailable) {
      // A source build has real call sites, so the scan stops at the limit
      // long before it runs out of tree.
      expect(body.filesScanned).toBeGreaterThan(0);
    } else {
      // A launcher install has to read the headers out to prove the absence.
      expect(body.filesScanned).toBeGreaterThan(1_000);
    }
    expect(body.filesScanned).toBeLessThan(40_000);
    // The budget is generous enough that one tree never reaches it. If this
    // fires, the search got slower rather than larger.
    expect(body.truncated).toBeUndefined();
  });
});
