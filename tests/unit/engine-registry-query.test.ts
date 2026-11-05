import { describe, expect, it } from "vitest";
import { registryQueryArgs } from "../../src/editor/engine-root.js";

/**
 * A .uproject's EngineAssociation is file content. Only a GUID reaches the
 * registry lookup, and it goes to `reg` as one argv entry with no shell.
 */
describe("the registered-build registry lookup", () => {
  it("queries a GUID as a single argument", () => {
    expect(registryQueryArgs("2a5c4e8f-9b31-4a6d-8f21-77c0d9e4b105")).toEqual([
      "query", "HKCU\\SOFTWARE\\Epic Games\\Unreal Engine\\Builds", "/v", "2a5c4e8f-9b31-4a6d-8f21-77c0d9e4b105",
    ]);
    expect(registryQueryArgs("2a5c4e8f-9b31-4a6d-8f21-77c0d9e4b105")![1].split(String.fromCharCode(92))).toEqual([
      "HKCU", "SOFTWARE", "Epic Games", "Unreal Engine", "Builds",
    ]);
  });

  it("refuses anything that is not a GUID", () => {
    for (const bad of ['x" & calc & "', "5.7", "2a5c4e8f-9b31-4a6d-8f21-77c0d9e4b105&calc", "../x", ""]) {
      expect(registryQueryArgs(bad)).toBeNull();
    }
  });
});
