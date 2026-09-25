import { describe, expect, it, vi } from "vitest";
import { z } from "zod";
import { blueprintTool } from "../../src/tools/blueprint.js";
import type { ToolContext } from "../../src/types.js";

// Container specs are parsed by the bridge (ParsePinTypeSpec). The server must
// forward them verbatim, including the comma inside map<K,V>.
const BP = "/Game/Blueprints/BP_Actor";
const MAP = "map<enum:/Game/Enums/E_Team.E_Team,float>";

async function route(input: Record<string, unknown>) {
  const call = vi.fn().mockResolvedValue({ success: true });
  const ctx = { bridge: { call } } as unknown as ToolContext;
  const parsed = z.object(blueprintTool.schema).parse(input);
  await blueprintTool.handler(ctx, parsed);
  return call.mock.calls[0] as [string, Record<string, unknown>];
}

describe("blueprint container type specs", () => {
  it("add_variable forwards a map spec as type", async () => {
    const [method, params] = await route({ action: "add_variable", assetPath: BP, name: "TeamScores", varType: MAP });
    expect(method).toBe("add_variable");
    expect(params).toMatchObject({ path: BP, name: "TeamScores", type: MAP });
  });

  it("add_local_variable forwards an array spec as varType", async () => {
    const [method, params] = await route({ action: "add_local_variable", assetPath: BP, functionName: "F", name: "L", varType: "int[]" });
    expect(method).toBe("add_local_variable");
    expect(params).toMatchObject({ assetPath: BP, functionName: "F", name: "L", varType: "int[]" });
  });

  it("add_function_parameter forwards a set spec as parameterType", async () => {
    const [method, params] = await route({ action: "add_function_parameter", assetPath: BP, functionName: "F", parameterName: "P", parameterType: "set<Name>" });
    expect(method).toBe("add_function_parameter");
    expect(params).toMatchObject({ path: BP, functionName: "F", parameterName: "P", parameterType: "set<Name>" });
  });

  it("add_event_dispatcher forwards typed parameters untouched", async () => {
    const parameters = [{ name: "NewTeam", type: "enum:/Game/Enums/E_Team.E_Team" }, { name: "Scores", type: MAP }];
    const [method, params] = await route({ action: "add_event_dispatcher", blueprintPath: BP, name: "OnTeamChanged", parameters });
    expect(method).toBe("add_event_dispatcher");
    expect(params).toMatchObject({ blueprintPath: BP, name: "OnTeamChanged", parameters });
  });

  it("documents the container syntax on every action that takes a type", () => {
    for (const action of ["add_variable", "add_local_variable", "add_function_parameter", "add_event_dispatcher"]) {
      const description = blueprintTool.actions[action].description ?? "";
      expect(description, action).toMatch(/set<Name>|set<Type>/);
      expect(description, action).toMatch(/map</);
    }
  });
});
