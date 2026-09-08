/**
 * `findExampleUsage`, against fixture engine trees rather than an installed
 * one.
 *
 * This is the action that reads the most files per call, so what is pinned
 * here is as much about what it does NOT read as about what it returns. Two
 * fixture engines, for the same reason `engine-analysis.test.ts` has two: a
 * source build has .cpp files and a launcher install has none, and the
 * fallback between them is the whole reason the function is shaped the way it
 * is.
 *
 * The reads are issued in parallel batches, so the order of the sites is the
 * one thing a scheduling change could quietly break. It is asserted
 * explicitly, and asserted again on a repeat call.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { findExampleUsage } from "../../src/cpp-correctness.js";

const RUNTIME = "Engine/Source/Runtime";

/** Headers both fixture engines share. */
const HEADERS: Record<string, string> = {
  [`${RUNTIME}/Engine/Classes/Kismet/GameplayStatics.h`]: [
    `#pragma once`,
    ``,
    `class ENGINE_API UGameplayStatics`,
    `{`,
    `public:`,
    `\t// GetPlayerPawn() named in a comment is not an example of using it.`,
    `\tstatic APawn* GetPlayerPawn(const UObject* WorldContext, int32 PlayerIndex);`,
    `};`,
  ].join("\n"),

  // Inline implementation in a header, which is what the launcher-install
  // fallback exists to find.
  [`${RUNTIME}/Engine/Public/PawnHelpers.h`]: [
    `#pragma once`,
    `#include "Kismet/GameplayStatics.h"`,
    ``,
    `inline APawn* FirstPawn(const UObject* World)`,
    `{`,
    `\treturn UGameplayStatics::GetPlayerPawn(World, 0);`,
    `}`,
  ].join("\n"),

  // Nothing in here mentions the symbol. It is what proves the byte prefilter
  // rules a file out instead of decoding and splitting it.
  [`${RUNTIME}/Core/Public/Math/Vector.h`]: [
    `#pragma once`,
    ``,
    `struct CORE_API FVector`,
    `{`,
    `};`,
  ].join("\n"),
};

/** The .cpp files only the source-build fixture has. */
const SOURCES: Record<string, string> = {
  [`${RUNTIME}/Engine/Private/PlayerCameraManager.cpp`]: [
    `#include "Kismet/GameplayStatics.h"`,
    ``,
    `void AManager::Update()`,
    `{`,
    `\tAPawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);`,
    `}`,
  ].join("\n"),

  [`${RUNTIME}/Engine/Private/HUD.cpp`]: [
    `#include "Kismet/GameplayStatics.h"`,
    ``,
    `void AHUD::Draw()`,
    `{`,
    `\tDrawFor(UGameplayStatics::GetPlayerPawn(this, 0));`,
    `}`,
  ].join("\n"),
};

/** The user's own project, which is searched alongside the engine. */
const PROJECT_SOURCES: Record<string, string> = {
  "Source/MyGame/Private/MyActor.cpp": [
    `#include "Kismet/GameplayStatics.h"`,
    ``,
    `void AMyActor::BeginPlay()`,
    `{`,
    `\tGetPlayerPawn(this, 0);`,
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

beforeAll(() => {
  temp = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-example-usage-"));
  SOURCED = path.join(temp, "SourceBuild");
  HEADERS_ONLY = path.join(temp, "LauncherInstall");
  PROJECT = path.join(temp, "MyGame");

  write(SOURCED, HEADERS);
  write(SOURCED, SOURCES);
  write(HEADERS_ONLY, HEADERS);
  write(PROJECT, PROJECT_SOURCES);
});

afterAll(() => {
  fs.rmSync(temp, { recursive: true, force: true });
});

describe("findExampleUsage on a source build", () => {
  it("answers from .cpp files, which is what an example should be", async () => {
    const result = await findExampleUsage(SOURCED, "UGameplayStatics::GetPlayerPawn");

    expect(result.engineSourcesAvailable).toBe(true);
    expect(result.note).toBeUndefined();
    expect(result.sites.map((s) => s.file)).toEqual([
      `${RUNTIME}/Engine/Private/HUD.cpp`,
      `${RUNTIME}/Engine/Private/PlayerCameraManager.cpp`,
    ]);
    expect(result.sites.every((s) => s.kind === "source")).toBe(true);
  });

  it("does not read the headers it would only fall back to", async () => {
    // Two .cpp files exist and three headers do. Reading five would mean the
    // fallback ran on an install that never needed it.
    const result = await findExampleUsage(SOURCED, "GetPlayerPawn");
    expect(result.filesScanned).toBe(2);
  });
});

describe("findExampleUsage on a launcher install", () => {
  it("falls back to inline code in headers, and says why", async () => {
    const result = await findExampleUsage(HEADERS_ONLY, "GetPlayerPawn");

    expect(result.engineSourcesAvailable).toBe(false);
    // Without this the empty .cpp result reads as "nothing uses this symbol".
    expect(result.note).toMatch(/without \.cpp sources/);
    expect(result.sites).toHaveLength(1);
    expect(result.sites[0].file).toBe(`${RUNTIME}/Engine/Public/PawnHelpers.h`);
    expect(result.sites[0].kind).toBe("header");
    expect(result.sites[0].text).toContain("UGameplayStatics::GetPlayerPawn(World, 0)");
  });

  it("returns neither the declaration nor the comment above it", async () => {
    const result = await findExampleUsage(HEADERS_ONLY, "GetPlayerPawn");
    const texts = result.sites.map((s) => s.text);
    expect(texts.some((t) => t.startsWith("//"))).toBe(false);
    expect(texts.some((t) => t.startsWith("static"))).toBe(false);
  });

  it("searches the project's own sources too", async () => {
    const result = await findExampleUsage(HEADERS_ONLY, "GetPlayerPawn", { projectDir: PROJECT });
    const own = result.sites.find((s) => s.kind === "project");
    expect(own?.file).toBe("Source/MyGame/Private/MyActor.cpp");
  });
});

describe("what the search costs", () => {
  it("stops as soon as it has the limit, rather than finishing the tree", async () => {
    const result = await findExampleUsage(SOURCED, "GetPlayerPawn", { limit: 1 });
    expect(result.siteCount).toBe(1);
    expect(result.filesScanned).toBe(1);
  });

  it("returns the same sites in the same order on a repeat call", async () => {
    // Reads are issued in parallel and consumed in list order. If that ever
    // slips, the answer starts depending on which read finished first.
    const first = await findExampleUsage(SOURCED, "GetPlayerPawn");
    const second = await findExampleUsage(SOURCED, "GetPlayerPawn");
    expect(second.sites).toEqual(first.sites);
  });

  it("says so when it stopped on its time budget", async () => {
    // A budget of zero is spent before the first batch, which is the only way
    // to reach this branch without a tree the size of an engine.
    const result = await findExampleUsage(SOURCED, "GetPlayerPawn", { budgetMs: 0 });
    expect(result.truncated).toBe(true);
    expect(result.note).toMatch(/time budget/);
    expect(result.siteCount).toBe(0);
  });

  it("leaves truncated unset on a search that finished", async () => {
    const result = await findExampleUsage(SOURCED, "GetPlayerPawn");
    expect(result.truncated).toBeUndefined();
  });
});
