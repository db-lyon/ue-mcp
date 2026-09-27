import { readFileSync, readdirSync } from "node:fs";
import path from "node:path";
import { describe, expect, it } from "vitest";

// UE 5.8 uses shared-string keys, so an FString pair reference over a JSON
// object map binds to a temporary. Bind with `const auto&` and convert in the
// body. A typed pair loop is allowed only over a TArray declared in the file.
const typedPairLoop =
  /for\s*\(\s*const\s+TPair\s*<\s*FString\s*,\s*TSharedPtr\s*<\s*FJsonValue\s*>\s*>\s*&?\s*\w+\s*:\s*([^)\n]+?)\s*\)/g;

function isDeclaredPairArray(source: string, name: string): boolean {
  const decl = new RegExp(
    String.raw`TArray\s*<\s*TPair\s*<\s*FString\s*,\s*TSharedPtr\s*<\s*FJsonValue\s*>\s*>\s*>\s*&?\s*\b${name}\b`,
  );
  return decl.test(source);
}

function findTypedPairLoopsOverMaps(source: string): number[] {
  const lines: number[] = [];
  for (const match of source.matchAll(typedPairLoop)) {
    const range = match[1];
    const name = /(\w+)\s*$/.exec(range)?.[1];
    const safe = name !== undefined && name !== "Values" && isDeclaredPairArray(source, name);
    if (!safe) lines.push(source.slice(0, match.index).split("\n").length);
  }
  return lines;
}

describe("JSON object range loops", () => {
  it("flags map loops, including through an alias, and allows declared pair arrays", () => {
    const sample = [
      "for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Obj->Values) {}",
      "const auto& Map = Obj->Values;",
      "for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Map) {}",
      "TArray<TPair<FString, TSharedPtr<FJsonValue>>> Writes;",
      "for (const TPair<FString, TSharedPtr<FJsonValue>>& W : Writes) {}",
      "for (const auto& P : Obj->Values) {}",
    ].join("\n");
    expect(findTypedPairLoopsOverMaps(sample)).toEqual([1, 3]);
  });

  it("finds no typed FString pair loop over a JSON object map in the plugin", () => {
    const root = path.resolve("plugin/ue_mcp_bridge/Source");
    const violations: string[] = [];
    for (const file of readdirSync(root, { recursive: true, encoding: "utf8" })) {
      if (!/\.(cpp|h|inl)$/.test(file)) continue;
      const source = readFileSync(path.join(root, file), "utf8");
      for (const line of findTypedPairLoopsOverMaps(source)) violations.push(`${file}:${line}`);
    }
    expect(violations).toEqual([]);
  });
});
