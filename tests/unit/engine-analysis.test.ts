/**
 * The relational half of the engine reader: hierarchy, references, callers,
 * callees and declaration context.
 *
 * Every one of these is a regex pass over C++, so every rule in it is a guess
 * that was wrong on some real file until it was not. They are held here
 * against a fixture engine tree written to a temp directory rather than
 * against an installed engine, so they run in CI on a machine with no Unreal
 * on it and so a failure points at a rule rather than at a 31,000-file scan.
 *
 * Two fixture engines, because the difference between them is the single most
 * important thing these functions have to report. `SOURCED` is a source build
 * and has .cpp files. `HEADERS_ONLY` is what the Epic launcher installs: the
 * same headers with no sources at all. An answer that cannot tell those apart
 * says "nothing calls this" when the truth is "this install cannot answer
 * that", and the two call for opposite next steps.
 *
 * The end-to-end behaviour against a real engine is asserted in the live tests.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { buildEngineIndex, type EngineIndex } from "../../src/engine-index.js";
import {
  baseTypeName,
  callsIn,
  childIndex,
  classHierarchy,
  engineSourcesAvailable,
  extractBody,
  findCallees,
  findCallers,
  findReferences,
  looksLikeCall,
  moduleDirFor,
  symbolContext,
  treeRoots,
} from "../../src/engine-analysis.js";

/* ── the fixture engine ────────────────────────────────────────────── */

const RUNTIME = "Engine/Source/Runtime";
const GAS =
  "Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities";

/** The headers both fixture engines share. */
const HEADERS: Record<string, string> = {
  [`${RUNTIME}/CoreUObject/Public/UObject/Object.h`]: [
    `#pragma once`,
    `class COREUOBJECT_API UObject`,
    `{`,
    `};`,
  ].join("\n"),

  [`${RUNTIME}/Engine/Classes/GameFramework/Actor.h`]: [
    `#pragma once`,
    `#include "UObject/Object.h"`,
    ``,
    `class ENGINE_API AActor : public UObject`,
    `{`,
    `public:`,
    `\tvirtual void BeginPlay();`,
    ``,
    `\t/** Where the actor is, in world space. */`,
    `\tFVector GetActorLocation() const`,
    `\t{`,
    `\t\treturn RootComponent->GetComponentLocation();`,
    `\t}`,
    `};`,
  ].join("\n"),

  [`${RUNTIME}/Engine/Classes/GameFramework/Pawn.h`]: [
    `#pragma once`,
    `#include "GameFramework/Actor.h"`,
    ``,
    `class ENGINE_API APawn : public AActor`,
    `{`,
    `public:`,
    `\tvirtual void PossessedBy(AController* NewController);`,
    `};`,
  ].join("\n"),

  [`${RUNTIME}/Engine/Classes/GameFramework/Character.h`]: [
    `#pragma once`,
    `#include "GameFramework/Pawn.h"`,
    ``,
    `class ENGINE_API ACharacter : public APawn`,
    `{`,
    `};`,
  ].join("\n"),

  // A subclass in a plugin module, which is the case the module fields exist
  // for: reaching it from the engine module is a Build.cs edit.
  [`${GAS}/Public/GASCharacter.h`]: [
    `#pragma once`,
    `#include "GameFramework/Character.h"`,
    ``,
    `class GAMEPLAYABILITIES_API AGASCharacter : public ACharacter`,
    `{`,
    `};`,
  ].join("\n"),

  // Its base is deliberately absent from the fixture, so the ancestor walk has
  // a name it cannot resolve.
  [`${GAS}/Public/AbilitySystemComponent.h`]: [
    `#pragma once`,
    ``,
    `class GAMEPLAYABILITIES_API UAbilitySystemComponent : public UActorComponent`,
    `{`,
    `};`,
  ].join("\n"),
};

/** The .cpp files only the source-build fixture has. */
const SOURCES: Record<string, string> = {
  [`${RUNTIME}/Engine/Private/Actor.cpp`]: [
    `#include "GameFramework/Actor.h"`,
    ``,
    `void AActor::BeginPlay()`,
    `{`,
    `\tSetActorTickEnabled(true);`,
    `\tRegisterAllComponents();`,
    `}`,
    ``,
    `void AActor::Tick(float DeltaSeconds)`,
    `{`,
    `\tGetActorLocation();`,
    `}`,
  ].join("\n"),

  [`${RUNTIME}/Engine/Private/Pawn.cpp`]: [
    `#include "GameFramework/Pawn.h"`,
    ``,
    `void APawn::PossessedBy(AController* NewController)`,
    `{`,
    `\t// BeginPlay() in a comment is not a call.`,
    `\tBeginPlay();`,
    `\tGetActorLocation();`,
    `}`,
  ].join("\n"),
};

/** A project of the user's own, which is searched alongside the engine. */
const PROJECT_SOURCES: Record<string, string> = {
  "Source/MyGame/Private/MyActor.cpp": [
    `#include "MyActor.h"`,
    ``,
    `void AMyActor::BeginPlay()`,
    `{`,
    `\tSuper::BeginPlay();`,
    `}`,
  ].join("\n"),
};

function write(root: string, files: Record<string, string>): void {
  for (const [rel, content] of Object.entries(files)) {
    const full = path.join(root, rel);
    fs.mkdirSync(path.dirname(full), { recursive: true });
    fs.writeFileSync(full, content, "utf-8");
  }
}

let temp: string;
/** A source build: headers and .cpp files. */
let SOURCED: string;
/** A launcher install: the same headers, no sources anywhere. */
let HEADERS_ONLY: string;
let PROJECT: string;
let sourcedIndex: EngineIndex;
let headersOnlyIndex: EngineIndex;

beforeAll(() => {
  temp = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-engine-analysis-"));
  SOURCED = path.join(temp, "SourceBuild");
  HEADERS_ONLY = path.join(temp, "LauncherInstall");
  PROJECT = path.join(temp, "MyGame");

  write(SOURCED, HEADERS);
  write(SOURCED, SOURCES);
  write(HEADERS_ONLY, HEADERS);
  write(PROJECT, PROJECT_SOURCES);

  const trees = ["Runtime", "Plugins"];
  sourcedIndex = buildEngineIndex(SOURCED, trees);
  headersOnlyIndex = buildEngineIndex(HEADERS_ONLY, trees);
});

afterAll(() => {
  fs.rmSync(temp, { recursive: true, force: true });
});

/* ── path and name helpers ─────────────────────────────────────────── */

describe("moduleDirFor", () => {
  it("gives the module directory of an engine header", () => {
    expect(moduleDirFor(`${RUNTIME}/Engine/Classes/GameFramework/Actor.h`)).toBe(
      "Engine/Source/Runtime/Engine",
    );
    expect(moduleDirFor(`${RUNTIME}/Core/Public/Math/Vector.h`)).toBe("Engine/Source/Runtime/Core");
  });

  it("gives it for a plugin, whose layout is different", () => {
    // A plugin's module directory is the one below its own Source/, which is
    // what holds the Private folder a definition would be in.
    expect(moduleDirFor(`${GAS}/Public/AbilitySystemComponent.h`)).toBe(GAS);
  });
});

describe("baseTypeName", () => {
  it("strips a namespace and template arguments off a base class name", () => {
    expect(baseTypeName("UObject")).toBe("UObject");
    expect(baseTypeName("UE::Math::TVector")).toBe("TVector");
    expect(baseTypeName("TSharedFromThis<FFoo, ESPMode::ThreadSafe>")).toBe("TSharedFromThis");
  });
});

describe("treeRoots", () => {
  it("resolves Plugins to Engine/Plugins, which is not under Engine/Source", () => {
    expect(treeRoots(HEADERS_ONLY, ["Runtime", "Plugins"]).map((d) => path.relative(HEADERS_ONLY, d).replace(/\\/g, "/")))
      .toEqual(["Engine/Source/Runtime", "Engine/Plugins"]);
  });

  it("skips a tree this install does not have", () => {
    expect(treeRoots(HEADERS_ONLY, ["Editor"])).toEqual([]);
  });
});

describe("engineSourcesAvailable", () => {
  it("separates a source build from a launcher install", () => {
    // The whole degradation story hangs off this one answer.
    expect(engineSourcesAvailable(SOURCED)).toBe(true);
    expect(engineSourcesAvailable(HEADERS_ONLY)).toBe(false);
  });
});

/* ── class hierarchy ───────────────────────────────────────────────── */

describe("classHierarchy", () => {
  it("walks the whole ancestor chain, nearest parent first", () => {
    const result = classHierarchy(sourcedIndex, "ACharacter");
    expect(result.found).toBe(true);
    expect(result.ancestors.map((a) => a.name)).toEqual(["APawn", "AActor", "UObject"]);
    expect(result.ancestors.map((a) => a.depth)).toEqual([1, 2, 3]);
  });

  it("reports the module of every node, and which ones cross a boundary", () => {
    // Crossing a module boundary is what forces a Build.cs change, so it is
    // the fact worth surfacing rather than leaving to be inferred.
    const result = classHierarchy(sourcedIndex, "ACharacter");
    const uobject = result.ancestors.find((a) => a.name === "UObject");
    expect(uobject?.module).toBe("CoreUObject");
    expect(uobject?.crossesModule).toBe(true);
    expect(result.ancestors.find((a) => a.name === "APawn")?.crossesModule).toBe(false);
    expect(result.crossModuleDependencies).toContain("CoreUObject");
    expect(result.modules).toContain("Engine");
  });

  it("carries the include of each node, so the answer is actionable", () => {
    const result = classHierarchy(sourcedIndex, "ACharacter", { direction: "ancestors" });
    expect(result.ancestors[0].include).toBe("GameFramework/Pawn.h");
    expect(result.descendants).toEqual([]);
  });

  it("says which base it could not resolve rather than ending the chain quietly", () => {
    // A base behind a template or a macro is out of reach of a regex index.
    // "the chain ends here" and "I could not follow it" are different facts.
    const result = classHierarchy(sourcedIndex, "UAbilitySystemComponent");
    expect(result.ancestors).toEqual([]);
    expect(result.unresolvedAncestor).toBe("UActorComponent");
  });

  it("returns direct subclasses by default", () => {
    const result = classHierarchy(sourcedIndex, "AActor");
    expect(result.descendants.map((d) => d.name)).toEqual(["APawn"]);
    expect(result.directDescendantCount).toBe(1);
  });

  it("goes deeper only when asked, and reaches into plugin modules", () => {
    const result = classHierarchy(sourcedIndex, "AActor", { depth: 3 });
    expect(result.descendants.map((d) => d.name)).toEqual(["APawn", "ACharacter", "AGASCharacter"]);
    expect(result.descendants.map((d) => d.depth)).toEqual([1, 2, 3]);
    const gas = result.descendants.find((d) => d.name === "AGASCharacter");
    expect(gas?.module).toBe("GameplayAbilities");
    expect(gas?.crossesModule).toBe(true);
    expect(result.crossModuleDependencies).toContain("GameplayAbilities");
  });

  it("says when the descendant list was cut short", () => {
    const result = classHierarchy(sourcedIndex, "AActor", { depth: 3, limit: 1 });
    expect(result.descendants).toHaveLength(1);
    expect(result.truncated).toBe(true);
    expect(result.note).toMatch(/limit/);
  });

  it("answers a name it does not hold with found:false and a reason", () => {
    const result = classHierarchy(sourcedIndex, "ANotAThing");
    expect(result.found).toBe(false);
    expect(result.descendants).toEqual([]);
    expect(result.note).toMatch(/verify_symbols/);
  });

  it("inverts the index once and reuses it", () => {
    // The children table is memoised against the index object, so a second
    // query does not pay a second pass over every symbol.
    expect(childIndex(sourcedIndex)).toBe(childIndex(sourcedIndex));
    expect(childIndex(sourcedIndex).get("AActor")?.map((s) => s.name)).toEqual(["APawn"]);
  });
});

/* ── references ────────────────────────────────────────────────────── */

describe("findReferences", () => {
  it("finds a type named in headers and in sources", () => {
    const result = findReferences(SOURCED, "AActor", { trees: ["Runtime"], includeProject: false });
    const files = new Set(result.sites.map((s) => s.file));
    expect(files).toContain(`${RUNTIME}/Engine/Classes/GameFramework/Pawn.h`);
    expect(files).toContain(`${RUNTIME}/Engine/Private/Actor.cpp`);
    expect(result.engineSourcesAvailable).toBe(true);
    expect(result.modules).toEqual(["Engine"]);
  });

  it("skips comments and include lines, which are not uses", () => {
    const result = findReferences(SOURCED, "AActor", { trees: ["Runtime"], includeProject: false });
    expect(result.sites.some((s) => s.text.startsWith("#include"))).toBe(false);
    expect(result.sites.some((s) => s.text.startsWith("//"))).toBe(false);
  });

  it("degrades to headers on a launcher install and says that is what happened", () => {
    // Without the flag and the note, an answer with no .cpp sites reads as
    // "nothing references this".
    const result = findReferences(HEADERS_ONLY, "AActor", { trees: ["Runtime"], includeProject: false });
    expect(result.engineSourcesAvailable).toBe(false);
    expect(result.note).toMatch(/launcher/);
    expect(result.siteCount).toBeGreaterThan(0);
    expect(result.sites.every((s) => s.kind === "header")).toBe(true);
  });

  it("stops at its limit and says so", () => {
    const result = findReferences(SOURCED, "AActor", { trees: ["Runtime"], includeProject: false, limit: 1 });
    expect(result.sites).toHaveLength(1);
    expect(result.truncated).toBe(true);
  });

  it("searches the project's own tree when there is one", () => {
    const result = findReferences(HEADERS_ONLY, "BeginPlay", {
      trees: ["Runtime"],
      projectDir: PROJECT,
    });
    expect(result.sites.some((s) => s.kind === "project" && s.file.includes("MyActor.cpp"))).toBe(true);
  });
});

/* ── callers ───────────────────────────────────────────────────────── */

describe("findCallers", () => {
  it("finds a call and names the function it sits in", () => {
    const result = findCallers(SOURCED, "AActor::BeginPlay", { trees: ["Runtime"], includeProject: false });
    const site = result.sites.find((s) => s.file.endsWith("Pawn.cpp"));
    expect(site?.caller).toBe("APawn::PossessedBy");
    expect(site?.module).toBe("Engine");
    expect(result.callers).toContain("APawn::PossessedBy");
  });

  it("does not report the function's own definition as a caller", () => {
    const result = findCallers(SOURCED, "AActor::BeginPlay", { trees: ["Runtime"], includeProject: false });
    expect(result.sites.some((s) => s.text.includes("void AActor::BeginPlay"))).toBe(false);
  });

  it("ignores a call written inside a comment", () => {
    const result = findCallers(SOURCED, "AActor::BeginPlay", { trees: ["Runtime"], includeProject: false });
    expect(result.sites.some((s) => s.text.startsWith("//"))).toBe(false);
  });

  it("reads sources rather than the declaration in the header", () => {
    const result = findCallers(SOURCED, "AActor::BeginPlay", { trees: ["Runtime"], includeProject: false });
    expect(result.sites.every((s) => s.kind === "source")).toBe(true);
  });

  it("falls back to headers on a launcher install rather than reporting nothing", () => {
    // There are no engine .cpp files to search here at all. An empty list
    // would read as "nothing calls this", which is not what happened.
    const result = findCallers(HEADERS_ONLY, "AActor::GetComponentLocation", {
      trees: ["Runtime"],
      includeProject: false,
    });
    expect(result.engineSourcesAvailable).toBe(false);
    expect(result.note).toMatch(/launcher/);
    expect(result.sites.some((s) => s.kind === "header" && s.text.includes("GetComponentLocation"))).toBe(true);
  });

  it("counts a Super:: call, which reads like a definition and is not one", () => {
    // `void AMyActor::BeginPlay()` and `Super::BeginPlay();` are the same
    // shape apart from the semicolon, and treating both as definitions hid
    // every override's call to its parent.
    const result = findCallers(SOURCED, "BeginPlay", { trees: ["Runtime"], projectDir: PROJECT });
    const site = result.sites.find((s) => s.kind === "project");
    expect(site?.text).toBe("Super::BeginPlay();");
    expect(result.sites.some((s) => s.text.includes("void AMyActor::BeginPlay"))).toBe(false);
  });

  it("does not count a declaration as a call to itself", () => {
    // `virtual void BeginPlay();` is a call shape with a return type in front
    // of it. On a launcher install, where headers are all there is, counting
    // it made every answer the declaration the caller already had.
    const result = findCallers(HEADERS_ONLY, "BeginPlay", { trees: ["Runtime"], includeProject: false });
    expect(result.sites.some((s) => s.text.includes("virtual void BeginPlay"))).toBe(false);
  });
});

describe("looksLikeCall", () => {
  it("reads a bare, bracketed or member-accessed name as a call", () => {
    expect(looksLikeCall("BeginPlay();", 0)).toBe(true);
    expect(looksLikeCall("if (BeginPlay())", "if (".length)).toBe(true);
    expect(looksLikeCall("Comp->BeginPlay();", "Comp->".length)).toBe(true);
    expect(looksLikeCall("Super::BeginPlay();", "Super::".length)).toBe(true);
    expect(looksLikeCall("return BeginPlay();", "return ".length)).toBe(true);
  });

  it("reads a return type in front of the name as a declaration", () => {
    expect(looksLikeCall("virtual void BeginPlay();", "virtual void ".length)).toBe(false);
    expect(looksLikeCall("FVector* GetActor();", "FVector* ".length)).toBe(false);
  });
});

/* ── callees ───────────────────────────────────────────────────────── */

describe("extractBody", () => {
  it("takes a body by matching braces from the definition line", () => {
    const lines = [`void Foo()`, `{`, `\tif (a) { b(); }`, `}`, `void After() {}`];
    expect(extractBody(lines, 1)).toEqual({ body: lines.slice(0, 4), endLine: 4 });
  });

  it("returns nothing for a declaration, which has no body to take", () => {
    expect(extractBody([`virtual void BeginPlay();`, `int x;`], 1)).toBeNull();
  });

  it("is not fooled by a brace inside a string or a comment", () => {
    const lines = [`void Foo()`, `{`, `\tPrint("}");`, `\t// }`, `}`];
    expect(extractBody(lines, 1)?.endLine).toBe(5);
  });
});

describe("callsIn", () => {
  it("separates a member call from a scoped one and a free one", () => {
    const calls = callsIn(`Comp->GetLocation(); UGameplayStatics::GetPlayerPawn(); MakeThing();`, "Foo");
    expect(calls.get("GetLocation")?.call).toBe("member");
    expect(calls.get("GetPlayerPawn")?.call).toBe("scoped");
    expect(calls.get("MakeThing")?.call).toBe("free");
  });

  it("does not count a control-flow keyword or the function itself as a call", () => {
    const calls = callsIn(`if (x) { while (y) { Foo(); Bar(); } } return Baz();`, "Foo");
    expect([...calls.keys()].sort()).toEqual(["Bar", "Baz"]);
  });

  it("counts repeats and ignores calls written in comments or strings", () => {
    const calls = callsIn(`Tick(); Tick(); // Skipped()\nPrint("AlsoSkipped()");`, "Foo");
    expect(calls.get("Tick")?.count).toBe(2);
    expect(calls.has("Skipped")).toBe(false);
    expect(calls.has("AlsoSkipped")).toBe(false);
  });
});

describe("findCallees", () => {
  it("reads the body out of the owning module's sources", () => {
    const result = findCallees(sourcedIndex, "AActor::BeginPlay");
    expect(result.found).toBe(true);
    expect(result.definition?.file).toBe(`${RUNTIME}/Engine/Private/Actor.cpp`);
    expect(result.definition?.kind).toBe("source");
    expect(result.callees.map((c) => c.name).sort()).toEqual([
      "RegisterAllComponents",
      "SetActorTickEnabled",
    ]);
  });

  it("resolves each callee back through the index, which is the Build.cs cost", () => {
    const result = findCallees(sourcedIndex, "APawn::PossessedBy");
    const beginPlay = result.callees.find((c) => c.name === "GetActorLocation");
    expect(beginPlay?.call).toBe("free");
    expect(result.callees.map((c) => c.name)).toContain("BeginPlay");
  });

  it("reads an inline body from a header when the install has no sources", () => {
    const result = findCallees(headersOnlyIndex, "AActor::GetActorLocation");
    expect(result.found).toBe(true);
    expect(result.definition?.kind).toBe("header");
    expect(result.engineSourcesAvailable).toBe(false);
    expect(result.callees.find((c) => c.name === "GetComponentLocation")?.call).toBe("member");
    expect(result.note).toMatch(/inline/);
  });

  it("explains a body it could not find instead of returning an empty list", () => {
    const result = findCallees(headersOnlyIndex, "APawn::PossessedBy");
    expect(result.found).toBe(false);
    expect(result.callees).toEqual([]);
    expect(result.note).toMatch(/launcher/);
  });

  it("says when the owning type is not in the index at all", () => {
    const result = findCallees(sourcedIndex, "UNotAThing::DoStuff");
    expect(result.found).toBe(false);
    expect(result.note).toMatch(/verify_symbols/);
  });
});

/* ── symbol context ────────────────────────────────────────────────── */

describe("symbolContext", () => {
  it("returns the declaration in its neighbourhood, ending at the closing brace", () => {
    const result = symbolContext(sourcedIndex, "AActor", { before: 2, after: 40 });
    expect(result.found).toBe(true);
    expect(result.include).toBe("GameFramework/Actor.h");
    expect(result.module).toBe("Engine");
    expect(result.declarationLine).toBe(4);
    expect(result.bodyEndLine).toBe(14);
    // The point of the action: the API around the declaration, not the
    // declaration line on its own.
    expect(result.text).toContain("virtual void BeginPlay();");
    expect(result.text).toContain("Where the actor is, in world space.");
  });

  it("windows rather than truncating mid-type when the body runs past the limit", () => {
    const result = symbolContext(sourcedIndex, "AActor", { before: 0, after: 2 });
    expect(result.bodyEndLine).toBeUndefined();
    expect(result.startLine).toBe(4);
    expect(result.endLine).toBe(6);
  });

  it("resolves a qualified member to its line inside the class", () => {
    const result = symbolContext(sourcedIndex, "AActor::BeginPlay", { before: 1, after: 1 });
    expect(result.kind).toBe("member");
    expect(result.declarationLine).toBe(7);
    expect(result.text).toContain("BeginPlay");
  });

  it("says which half of a qualified name was wrong", () => {
    const missingMember = symbolContext(sourcedIndex, "AActor::NoSuchMethod");
    expect(missingMember.found).toBe(false);
    expect(missingMember.note).toMatch(/No member/);

    const missingType = symbolContext(sourcedIndex, "FNotAThing");
    expect(missingType.found).toBe(false);
    expect(missingType.note).toMatch(/not in the index/);
  });
});
