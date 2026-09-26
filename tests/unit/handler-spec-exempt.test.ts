/**
 * A contract-exempt spec is held to its handler's source (#1057).
 *
 * UE.MCP.Bridge.HandlerSpec.Contract proves a spec'd handler reads exactly
 * what it declares by calling it with values that name nothing. A handler whose
 * values would reach a create, spawn, save or run first is registered with
 * MCPSpec::ContractExempt(reason) and is not called. Its spec still generates
 * the surface, so it is checked here instead: the names its C++ body reads,
 * through HandlerUtils.h helpers and the functions it hands its Params to,
 * must be exactly the names it declares.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { handlerParamReads, paramReadsInBody, readAllSources } from "../../scripts/lib/handler-param-reads.mjs";
import { readRegistrations } from "../../scripts/audit-handler-conventions.mjs";
import type { HandlerSpecs } from "../../src/surface/handler-spec.js";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const SNAPSHOT = JSON.parse(fs.readFileSync(path.join(ROOT, "tests", "golden", "handler-specs.json"), "utf8")) as {
  handlers: HandlerSpecs;
};
const REGISTRATIONS = readRegistrations();
const SOURCES = readAllSources();

const sorted = (names: Iterable<string>) => [...names].sort();

describe("the source reader", () => {
  const helpers = new Map([["Helpers.cpp", `
TSharedPtr<FJsonValue> ReadSelector(const TSharedPtr<FJsonObject>& Bag, FSelector& Out)
{
	Out.Prefix = OptionalString(Bag, TEXT("labelPrefix"));
	Out.Tag = OptionalString(Bag, TEXT("tag"));
	return nullptr;
}

static FString ReadNamed(const TSharedPtr<FJsonObject>& Bag, const TCHAR* Key)
{
	return OptionalString(Bag, Key);
}

static void ReadHidden(const TSharedPtr<FJsonObject>& Bag, const FString& Role)
{
	OptionalString(Bag, *(Role + TEXT("Node")));
}
`]]);

  it("reads the HandlerUtils.h helpers by name and position", () => {
    const body = `{
		FString A; if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), A)) return Err;
		const bool bDry = OptionalBool(Params, TEXT("dryRun"), false);
		// OptionalString(Params, TEXT("commentedOut"));
		MCPReadParamsAhead(Params, { TEXT("settings"), TEXT("propertyName") });
		UWorld* World = ResolveWorldFromParams(Params);
		const FString Label = OptionalString(Params, TEXT("label"), TEXT("defaultLabel"));
		Other->TryGetStringField(TEXT("notParams"), A);
	}`;
    const reads = paramReadsInBody(body, "Params", new Map());
    expect(sorted(reads.keys)).toEqual(["assetPath", "dryRun", "label", "path", "pieInstance", "propertyName", "settings", "world"]);
    expect(reads.opaque).toEqual([]);
  });

  it("follows the bag into a function it is handed to, binding that function's key parameters", () => {
    const body = `{
		FSelector Sel;
		if (auto Err = ReadSelector(Params, Sel)) return Err;
		const FString Mode = ReadNamed(Params, TEXT("mode"));
	}`;
    const reads = paramReadsInBody(body, "Params", helpers);
    expect(sorted(reads.keys)).toEqual(["labelPrefix", "mode", "tag"]);
    expect(reads.opaque).toEqual([]);
  });

  it("reads an actor selector's keys, including overridden ones", () => {
    const body = `{
		FMCPActorSelector Target;
		Target.LabelKey = TEXT("targetLabel");
		Target.PathKey = TEXT("targetPath");
		AActor* A = MCPResolveActor(World, Params, Err);
		AActor* B = MCPResolveActor(World, Params, Err, Target);
	}`;
    expect(sorted(paramReadsInBody(body, "Params", new Map()).keys)).toEqual(["actorLabel", "actorPath", "targetLabel", "targetPath"]);
  });

  it("reports a key it cannot resolve, and a function it cannot find, as opaque", () => {
    const body = `{
		OptionalString(Params, KeyFromSomewhere);
		ReadHidden(Params, TEXT("source"));
		Mystery(Params);
	}`;
    const reads = paramReadsInBody(body, "Params", helpers);
    expect(reads.opaque).toHaveLength(3);
    expect(reads.opaque.join("\n")).toMatch(/KeyFromSomewhere/);
    expect(reads.opaque.join("\n")).toMatch(/Mystery \(definition not found\)/);
  });

  it("agrees with the contract test on nearly every handler that test calls", () => {
    // Calibration, not the gate: the contract test is the authority for these.
    // A handler whose read depends on the path a call takes, or on a key built
    // at runtime, can disagree; those are exactly the ones the reader reports.
    let readable = 0;
    let agree = 0;
    for (const [method, spec] of Object.entries(SNAPSHOT.handlers)) {
      if (spec.contractExempt) continue;
      const reads = handlerParamReads(method, { registrations: REGISTRATIONS, sources: SOURCES });
      if (!reads || reads.opaque.length > 0) continue;
      readable++;
      if (sorted(reads.keys).join() === sorted(spec.params.map((p) => p.name)).join()) agree++;
    }
    expect(readable).toBeGreaterThan(700);
    expect(agree / readable).toBeGreaterThan(0.98);
  });
});

describe("a contract-exempt spec", () => {
  const exempt = Object.entries(SNAPSHOT.handlers).filter(([, spec]) => spec.contractExempt);

  it("exists in the recording, so this check is exercised", () => {
    expect(exempt.length).toBeGreaterThan(0);
  });

  it.each(exempt)("%s reads exactly what it declares", (method, spec) => {
    const reads = handlerParamReads(method, { registrations: REGISTRATIONS, sources: SOURCES });
    expect(reads, `${method}: its handler body was not found, so nothing holds it to its spec`).not.toBeNull();
    expect(reads!.opaque, `${method}: reads the source cannot resolve; read them through a helper with a literal key`).toEqual([]);
    expect(sorted(reads!.keys), method).toEqual(sorted(spec.params.map((p) => p.name)));
  });
});
