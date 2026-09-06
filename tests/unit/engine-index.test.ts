/**
 * Symbol recognition and ranking for the engine index.
 *
 * The index is a regex pass over C++ headers, which means every rule in it is
 * a guess that was wrong on some real header until it was not. These tests
 * pin the cases that drove each rule, against small fixtures rather than
 * against an installed engine, so they run in CI on a machine with no Unreal
 * on it and so a failure points at a rule rather than at a 31,000-file scan.
 *
 * The end-to-end behaviour against a real engine is asserted in the live tests.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, it, expect } from "vitest";
import {
  includePathFor,
  indexCacheFile,
  isDefinitionTail,
  isPrivateHeader,
  loadEngineIndex,
  lookupSymbol,
  moduleFor,
  readCachedIndex,
  scanHeader,
  splitQualified,
  unprefixed,
  type EngineIndex,
  type EngineSymbol,
} from "../../src/engine-index.js";

const ACTOR_H = "Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h";
const ASC_H =
  "Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Public/AbilitySystemComponent.h";

describe("moduleFor", () => {
  it("reads the module out of an engine tree path", () => {
    expect(moduleFor(ACTOR_H)).toBe("Engine");
    expect(moduleFor("Engine/Source/Runtime/Core/Public/Math/Vector.h")).toBe("Core");
    expect(moduleFor("Engine/Source/Editor/UnrealEd/Public/Editor.h")).toBe("UnrealEd");
  });

  it("reads it from the segment after a plugin's own Source/", () => {
    // This is also the name that goes in a Build.cs dependency list, which is
    // the only reason the field exists.
    expect(moduleFor(ASC_H)).toBe("GameplayAbilities");
    expect(
      moduleFor("Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraComponent.h"),
    ).toBe("Niagara");
  });
});

describe("includePathFor", () => {
  it("returns the part below Public, Classes or Internal", () => {
    expect(includePathFor(ACTOR_H)).toBe("GameFramework/Actor.h");
    expect(includePathFor(ASC_H)).toBe("AbilitySystemComponent.h");
    expect(includePathFor("Engine/Source/Runtime/Core/Public/Math/Vector.h")).toBe("Math/Vector.h");
  });

  it("falls back to the full path for a header nothing can include", () => {
    const priv = "Engine/Source/Runtime/Engine/Private/Secret.h";
    expect(includePathFor(priv)).toBe(priv);
    expect(isPrivateHeader(priv)).toBe(true);
    expect(isPrivateHeader(ACTOR_H)).toBe(false);
  });
});

describe("isDefinitionTail", () => {
  it("accepts what really follows a definition", () => {
    expect(isDefinitionTail("")).toBe(true);
    expect(isDefinitionTail(" : public UObject")).toBe(true);
    expect(isDefinitionTail(" final : public UObject")).toBe(true);
    expect(isDefinitionTail(" : public TSharedFromThis<FFoo, ESPMode::ThreadSafe>")).toBe(true);
  });

  it("rejects a use of the type dressed as a declaration", () => {
    // `class AActor* GetActor() const` begins a line with `class` and names a
    // type. Without this test AActor resolved to SimpleConstructionScript.h.
    expect(isDefinitionTail("* GetComponentEditorActorInstance() const")).toBe(false);
    expect(isDefinitionTail("& GetRef()")).toBe(false);
    expect(isDefinitionTail(" = FVector2D")).toBe(false);
  });
});

describe("unprefixed", () => {
  it("strips the engine type prefix so a type can match its header name", () => {
    expect(unprefixed("AActor")).toBe("Actor");
    expect(unprefixed("UGameplayStatics")).toBe("GameplayStatics");
    expect(unprefixed("FHitResult")).toBe("HitResult");
  });

  it("leaves a name alone when the leading letter is part of the word", () => {
    expect(unprefixed("Frustum")).toBe("Frustum");
    expect(unprefixed("Actor")).toBe("Actor");
  });
});

describe("scanHeader", () => {
  it("finds a class with its base and its export macro", () => {
    const [symbol] = scanHeader(`class ENGINE_API AActor : public UObject\n{\n};\n`, ACTOR_H);
    expect(symbol).toMatchObject({
      name: "AActor",
      kind: "class",
      module: "Engine",
      include: "GameFramework/Actor.h",
      exported: true,
      parent: "UObject",
      line: 1,
    });
  });

  it("skips a forward declaration, which says nothing about where a type lives", () => {
    const found = scanHeader(`class AActor;\nclass FOther;\n`, ACTOR_H);
    expect(found).toEqual([]);
  });

  it("skips a return type that merely begins a line with `class`", () => {
    const source = `struct FThing\n{\n\tclass AActor* GetActor() const;\n};\n`;
    expect(scanHeader(source, ACTOR_H).map((s) => s.name)).toEqual(["FThing"]);
  });

  it("finds a struct, an enum and a namespace-scope alias", () => {
    const source = [
      `struct FHitResult`,
      `{`,
      `};`,
      `enum class ENetRole : uint8`,
      `{`,
      `};`,
      `using FVector = UE::Math::TVector<double>;`,
    ].join("\n");
    const kinds = Object.fromEntries(scanHeader(source, ACTOR_H).map((s) => [s.name, s.kind]));
    expect(kinds).toEqual({ FHitResult: "struct", ENetRole: "enum", FVector: "alias" });
  });

  it("ignores an indented alias, which is a member of some template", () => {
    // `using FVector = FVector2D;` inside a spatial-index class must not become
    // the answer to "where does FVector live".
    const source = `class TIndex\n{\n\tusing FVector = FVector2D;\n};\n`;
    expect(scanHeader(source, ACTOR_H).map((s) => s.name)).toEqual(["TIndex"]);
  });

  it("records a deprecation with its version and message", () => {
    const source = [
      `UE_DEPRECATED(5.1, "Use FNewThing instead.")`,
      `struct FOldThing`,
      `{`,
      `};`,
    ].join("\n");
    const [symbol] = scanHeader(source, ACTOR_H);
    expect(symbol.deprecated).toEqual({ version: "5.1", message: "Use FNewThing instead." });
  });

  it("does not attach a deprecation to an unrelated declaration below it", () => {
    const source = [
      `UE_DEPRECATED(5.1, "gone")`,
      `struct FOldThing {};`,
      ``,
      ``,
      `struct FFreshThing`,
      `{`,
      `};`,
    ].join("\n");
    const fresh = scanHeader(source, ACTOR_H).find((s) => s.name === "FFreshThing");
    expect(fresh?.deprecated).toBeUndefined();
  });

  it("finds an exported free function", () => {
    const [symbol] = scanHeader(`ENGINE_API FString LexToString(const FVector& V);\n`, ACTOR_H);
    expect(symbol).toMatchObject({ name: "LexToString", kind: "function", exported: true });
  });
});

describe("lookupSymbol ranking", () => {
  const index = (symbols: EngineSymbol[]): EngineIndex => ({
    engineRoot: "C:/Engine",
    engineVersion: "5.8.0+0",
    builtAt: "now",
    trees: ["Runtime"],
    headerCount: 0,
    symbolCount: symbols.length,
    symbols: { [symbols[0].name]: symbols },
  });

  const symbol = (over: Partial<EngineSymbol>): EngineSymbol => ({
    name: "AActor",
    kind: "class",
    header: "Engine/Source/Runtime/Engine/Classes/Other/Thing.h",
    module: "Engine",
    include: "Other/Thing.h",
    line: 1,
    signature: "class AActor",
    exported: true,
    ...over,
  });

  it("puts the header named after the type first", () => {
    // AActor is mentioned in dozens of exported public headers and defined in
    // exactly one, so this has to outweigh every other signal.
    const ranked = lookupSymbol(
      index([
        symbol({ header: "Engine/Source/Runtime/Engine/Classes/Engine/SimpleConstructionScript.h" }),
        symbol({ header: ACTOR_H, include: "GameFramework/Actor.h", exported: false }),
      ]),
      "AActor",
    );
    expect(ranked[0].include).toBe("GameFramework/Actor.h");
  });

  it("never answers with a reflection stub", () => {
    // Including UObject/NoExportTypes.h gets the caller an incomplete type.
    const ranked = lookupSymbol(
      index([
        symbol({ name: "FVector", include: "UObject/NoExportTypes.h", header: "Engine/Source/Runtime/CoreUObject/Public/UObject/NoExportTypes.h" }),
        symbol({ name: "FVector", include: "Math/MathFwd.h", header: "Engine/Source/Runtime/Core/Public/Math/MathFwd.h", kind: "alias", exported: false }),
      ]),
      "FVector",
    );
    expect(ranked[0].include).toBe("Math/MathFwd.h");
  });

  it("prefers a type over a function of the same name", () => {
    // FGameplayTag is both a struct and a constructor declaration carrying the
    // module's API macro; the struct is what the caller means.
    const ranked = lookupSymbol(
      index([
        symbol({ name: "FGameplayTag", kind: "function", include: "GameplayTagContainer.h" }),
        symbol({ name: "FGameplayTag", kind: "struct", include: "GameplayTagContainer.h", exported: false }),
      ]),
      "FGameplayTag",
    );
    expect(ranked[0].kind).toBe("struct");
  });

  it("prefers a public header over a private one", () => {
    const ranked = lookupSymbol(
      index([
        symbol({ header: "Engine/Source/Runtime/Engine/Private/Thing.h", include: "Engine/Source/Runtime/Engine/Private/Thing.h" }),
        symbol({ header: "Engine/Source/Runtime/Engine/Public/Thing.h", include: "Thing.h" }),
      ]),
      "AActor",
    );
    expect(ranked[0].include).toBe("Thing.h");
  });

  it("returns nothing for a name it does not hold", () => {
    expect(lookupSymbol(index([symbol({})]), "FNotHere")).toEqual([]);
  });
});

describe("splitQualified", () => {
  it("splits a qualified member from its class", () => {
    expect(splitQualified("UGameplayStatics::GetPlayerPawn")).toEqual({
      className: "UGameplayStatics",
      member: "GetPlayerPawn",
    });
  });

  it("leaves a bare name alone", () => {
    expect(splitQualified("AActor")).toEqual({ member: "AActor" });
  });
});

/* ── the cache, and the copy kept in memory ────────────────────────── */

/**
 * The disk cache turns a minutes-long scan into a file read, and that was
 * treated as cheap enough to repeat on every call. For a full engine that
 * file is around 80 MB, so repeating it cost about three quarters of a second
 * and a couple of hundred megabytes of fresh heap each time, and it evicted
 * the filesystem cache that the file-reading actions beside it depend on.
 * What is pinned here is that a second load re-uses the table rather than
 * rebuilding it from the bytes, and that it still notices a rebuilt file, a
 * deleted one, and an explicit refresh.
 */
describe("loadEngineIndex", () => {
  const HEADER = "Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h";
  let temp: string;
  let engine: string;
  let previousState: string | undefined;

  beforeAll(() => {
    previousState = process.env.UE_MCP_USER_STATE;
    temp = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-engine-index-cache-"));
    // Redirected, so a run cannot read or write the developer's own index.
    process.env.UE_MCP_USER_STATE = path.join(temp, "state.json");

    engine = path.join(temp, "Engine_5.8");
    const header = path.join(engine, HEADER);
    fs.mkdirSync(path.dirname(header), { recursive: true });
    fs.writeFileSync(header, `#pragma once\nclass ENGINE_API AActor\n{\n};\n`, "utf-8");
    const version = path.join(engine, "Engine", "Build", "Build.version");
    fs.mkdirSync(path.dirname(version), { recursive: true });
    fs.writeFileSync(version, JSON.stringify({ MajorVersion: 5, MinorVersion: 8, PatchVersion: 0, Changelist: 1 }));
  });

  afterAll(() => {
    if (previousState === undefined) delete process.env.UE_MCP_USER_STATE;
    else process.env.UE_MCP_USER_STATE = previousState;
    fs.rmSync(temp, { recursive: true, force: true });
  });

  it("builds once, then serves the same table without re-reading the file", () => {
    const built = loadEngineIndex(engine, { trees: ["Runtime"] });
    expect(built.source).toBe("built");
    expect(built.cacheFile).toBe(indexCacheFile(engine, ["Runtime"]));
    expect(fs.existsSync(built.cacheFile!)).toBe(true);

    const again = loadEngineIndex(engine, { trees: ["Runtime"] });
    expect(again.source).toBe("cache");
    // Identity, not equality: a re-read would parse an equal table out of the
    // bytes, and parsing the bytes is the cost this exists to avoid.
    expect(again.index).toBe(built.index);
    // The bytes on disk still say the same thing, which is what makes the
    // shortcut safe rather than merely fast.
    expect(readCachedIndex(engine, ["Runtime"])?.symbolCount).toBe(built.index.symbolCount);
  });

  it("rebuilds on refresh, and keeps the rebuilt table", () => {
    const first = loadEngineIndex(engine, { trees: ["Runtime"] });
    const refreshed = loadEngineIndex(engine, { trees: ["Runtime"], refresh: true });
    expect(refreshed.source).toBe("built");
    expect(refreshed.index).not.toBe(first.index);
    expect(loadEngineIndex(engine, { trees: ["Runtime"] }).index).toBe(refreshed.index);
  });

  it("notices a cache file written by something else", () => {
    const held = loadEngineIndex(engine, { trees: ["Runtime"] }).index;
    const file = indexCacheFile(engine, ["Runtime"]);
    const edited = { ...held, symbolCount: held.symbolCount + 1 };
    fs.writeFileSync(file, JSON.stringify(edited));

    const reloaded = loadEngineIndex(engine, { trees: ["Runtime"] });
    expect(reloaded.source).toBe("cache");
    expect(reloaded.index).not.toBe(held);
    expect(reloaded.index.symbolCount).toBe(held.symbolCount + 1);
  });

  it("rebuilds when the cache file is gone", () => {
    const held = loadEngineIndex(engine, { trees: ["Runtime"] }).index;
    fs.rmSync(indexCacheFile(engine, ["Runtime"]), { force: true });
    const rebuilt = loadEngineIndex(engine, { trees: ["Runtime"] });
    expect(rebuilt.source).toBe("built");
    expect(rebuilt.index).not.toBe(held);
  });
});
