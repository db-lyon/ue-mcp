import { describe, it, expect, beforeEach, afterEach } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import {
  createEngineSymbolProjector,
  parse,
  compose,
  select,
} from "../../src/ontology/index.js";
import { serializeFragment } from "../../src/ontology/index.js";

function writeHeader(engineRoot: string, relPath: string, content: string): void {
  const full = path.join(engineRoot, "Engine", "Source", relPath);
  fs.mkdirSync(path.dirname(full), { recursive: true });
  fs.writeFileSync(full, content, "utf-8");
}

function project(engineRoot: string | null, input: Partial<Parameters<ReturnType<typeof createEngineSymbolProjector>["project"]>[0]> = {}) {
  const proj = createEngineSymbolProjector();
  const frag = proj.project({ engineRoot, ...input });
  const parsed = parse(serializeFragment(frag), "engine-symbols");
  return compose([{ priority: 1, fragment: parsed }]);
}

describe("ontology: EngineSymbolProjector", () => {
  let tmpDir: string;
  let engineRoot: string;

  beforeEach(() => {
    tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-eng-"));
    engineRoot = path.join(tmpDir, "UE_5.7");
    fs.mkdirSync(path.join(engineRoot, "Engine", "Source", "Runtime"), { recursive: true });
  });

  afterEach(() => {
    fs.rmSync(tmpDir, { recursive: true, force: true });
  });

  it("returns an empty index when engineRoot is null", () => {
    const view = project(null);
    const idx = view.root.children!.Engine.children!.Symbols.children!.Index;
    expect(idx.fields!.symbolCount).toBe(0);
    expect(idx.children ?? {}).toEqual({});
  });

  it("extracts UCLASS-style declarations and infers module from path", () => {
    writeHeader(
      engineRoot,
      "Runtime/Engine/Public/GameFramework/Actor.h",
      [
        "#pragma once",
        "UCLASS()",
        "class ENGINE_API AActor : public UObject",
        "{",
        "    GENERATED_BODY()",
        "};",
      ].join("\n"),
    );
    writeHeader(
      engineRoot,
      "Runtime/Engine/Public/Components/ActorComponent.h",
      [
        "#pragma once",
        "UCLASS()",
        "class ENGINE_API UActorComponent : public UObject",
        "{",
        "};",
      ].join("\n"),
    );

    const view = project(engineRoot);
    const syms = view.root.children!.Engine.children!.Symbols.children!.Index.children!;
    expect(syms.AActor).toBeDefined();
    expect(syms.AActor.fields!.module).toBe("Engine");
    expect(syms.AActor.fields!.parent).toBe("UObject");
    expect(syms.AActor.fields!.file).toBe("Engine/Source/Runtime/Engine/Public/GameFramework/Actor.h");
    expect((syms.AActor.fields!.kind as { marker?: string }).marker).toBe("class");

    expect(syms.UActorComponent.fields!.parent).toBe("UObject");
  });

  it("extracts struct and enum class declarations", () => {
    writeHeader(
      engineRoot,
      "Runtime/Core/Public/Math/Vector.h",
      [
        "USTRUCT()",
        "struct CORE_API FVector",
        "{",
        "};",
        "",
        "UENUM()",
        "enum class ESomeEnum : uint8",
        "{",
        "    One,",
        "    Two,",
        "};",
      ].join("\n"),
    );

    const view = project(engineRoot);
    const syms = view.root.children!.Engine.children!.Symbols.children!.Index.children!;
    expect((syms.FVector.fields!.kind as { marker?: string }).marker).toBe("struct");
    expect((syms.ESomeEnum.fields!.kind as { marker?: string }).marker).toBe("enum");
  });

  it("skips forward declarations that end with a semicolon", () => {
    writeHeader(
      engineRoot,
      "Runtime/Engine/Public/Fwd.h",
      [
        "class AActor;",
        "struct FSomeStruct;",
      ].join("\n"),
    );
    const view = project(engineRoot);
    const syms = view.root.children!.Engine.children!.Symbols.children!.Index.children ?? {};
    expect(syms.AActor).toBeUndefined();
    expect(syms.FSomeStruct).toBeUndefined();
  });

  it("deduplicates by name (first occurrence wins)", () => {
    writeHeader(
      engineRoot,
      "Runtime/Engine/Public/One.h",
      ["class AActor : public UObject", "{", "};"].join("\n"),
    );
    writeHeader(
      engineRoot,
      "Runtime/Engine/Public/Two.h",
      ["class AActor : public FSomethingElse", "{", "};"].join("\n"),
    );
    const view = project(engineRoot);
    const idx = view.root.children!.Engine.children!.Symbols.children!.Index;
    expect(idx.fields!.symbolCount).toBe(1);
    expect(idx.fields!.rawSymbolCount).toBe(2);
    expect(idx.children!.AActor.fields!.parent).toBe("UObject");
  });

  it("selector queries subclasses via @parent= predicate", () => {
    writeHeader(
      engineRoot,
      "Runtime/Engine/Public/Character.h",
      ["class ACharacter : public APawn", "{", "};"].join("\n"),
    );
    writeHeader(
      engineRoot,
      "Runtime/Engine/Public/Pawn.h",
      ["class APawn : public AActor", "{", "};"].join("\n"),
    );
    writeHeader(
      engineRoot,
      "Runtime/Engine/Public/Movement.h",
      ["class UCharacterMovementComponent : public UActorComponent", "{", "};"].join("\n"),
    );

    const view = project(engineRoot);
    const childrenOfAPawn = select(view.root, "/UE/Engine/Symbols/Index/*@parent=APawn");
    expect(childrenOfAPawn.map((m) => m.path.split("/").pop())).toEqual(["ACharacter"]);
  });

  it("truncates at maxSymbols with truncated=1 signal", () => {
    for (let i = 0; i < 50; i++) {
      writeHeader(
        engineRoot,
        `Runtime/Test/Public/File${i}.h`,
        [`class ATest${i} : public AActor`, "{", "};"].join("\n"),
      );
    }
    const view = project(engineRoot, { maxSymbols: 10 });
    const idx = view.root.children!.Engine.children!.Symbols.children!.Index;
    expect(idx.fields!.truncated).toBe(1);
    expect(idx.fields!.symbolCount).toBe(10);
  });
});
