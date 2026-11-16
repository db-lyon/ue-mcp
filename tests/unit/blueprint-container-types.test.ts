import { describe, expect, it, vi } from "vitest";
import { z } from "zod";
import { blueprintTool } from "../../src/tools/blueprint.js";
import type { ToolContext } from "../../src/surface/tool-types.js";

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
  it("add_variable forwards a map spec as varType", async () => {
    const [method, params] = await route({ action: "add_variable", assetPath: BP, name: "TeamScores", varType: MAP });
    expect(method).toBe("add_variable");
    expect(params).toMatchObject({ assetPath: BP, name: "TeamScores", varType: MAP });
  });

  it("add_local_variable forwards an array spec as varType", async () => {
    const [method, params] = await route({ action: "add_local_variable", assetPath: BP, functionName: "F", name: "L", varType: "int[]" });
    expect(method).toBe("add_local_variable");
    expect(params).toMatchObject({ assetPath: BP, functionName: "F", name: "L", varType: "int[]" });
  });

  it("add_function_parameter forwards a set spec as parameterType", async () => {
    const [method, params] = await route({ action: "add_function_parameter", assetPath: BP, functionName: "F", parameterName: "P", parameterType: "set<Name>" });
    expect(method).toBe("add_function_parameter");
    expect(params).toMatchObject({ assetPath: BP, functionName: "F", parameterName: "P", parameterType: "set<Name>" });
  });

  it("add_event_dispatcher forwards typed parameters untouched", async () => {
    const parameters = [{ name: "NewTeam", type: "enum:/Game/Enums/E_Team.E_Team" }, { name: "Scores", type: MAP }];
    const [method, params] = await route({ action: "add_event_dispatcher", blueprintPath: BP, name: "OnTeamChanged", parameters });
    expect(method).toBe("add_event_dispatcher");
    expect(params).toMatchObject({ blueprintPath: BP, name: "OnTeamChanged", parameters });
  });

  it("add_variable forwards category, tooltip and instanceEditable (#1209)", async () => {
    const [method, params] = await route({
      action: "add_variable", assetPath: BP, name: "Scores", varType: "map<int,enum:/Game/E.E>",
      category: "Scalability", tooltip: "Per level", instanceEditable: true,
    });
    expect(method).toBe("add_variable");
    expect(params).toMatchObject({ category: "Scalability", tooltip: "Per level", instanceEditable: true });
  });

  it("set_variable_default forwards propagateToInstances (#1209)", async () => {
    const [method, params] = await route({ action: "set_variable_default", assetPath: BP, name: "Scores", value: "()", propagateToInstances: true });
    expect(method).toBe("set_variable_default");
    expect(params).toMatchObject({ name: "Scores", value: "()", propagateToInstances: true });
  });

  it("documents the container syntax on every action that takes a type", () => {
    for (const action of ["add_variable", "add_local_variable", "add_function_parameter", "add_event_dispatcher"]) {
      const description = blueprintTool.actions[action].description ?? "";
      expect(description, action).toMatch(/set<Name>|set<Type>/);
      expect(description, action).toMatch(/map</);
    }
  });
});

describe("blueprint list_subclasses (#1211)", () => {
  it("forwards className, directory, includeTransitive and limit", async () => {
    const [method, params] = await route({ action: "list_subclasses", className: "Character", directory: "/Game/AI", includeTransitive: false, limit: 50 });
    expect(method).toBe("list_subclasses");
    expect(params).toMatchObject({ className: "Character", directory: "/Game/AI", includeTransitive: false, limit: 50 });
  });
});

describe("blueprint CDO component paths (#1293)", () => {
  it("set_class_default forwards a component-qualified propertyName", async () => {
    const [method, params] = await route({ action: "set_class_default", assetPath: BP, propertyName: "CharacterMovement.bUseRVOAvoidance", value: false });
    expect(method).toBe("set_class_default");
    expect(params).toMatchObject({ propertyName: "CharacterMovement.bUseRVOAvoidance", value: false });
  });

  it("get_variable_default forwards includeChildren", async () => {
    const [method, params] = await route({ action: "get_variable_default", assetPath: BP, name: "CharacterMovement.PerchRadiusThreshold", includeChildren: true });
    expect(method).toBe("get_blueprint_variable_default");
    expect(params).toMatchObject({ name: "CharacterMovement.PerchRadiusThreshold", includeChildren: true });
  });
});
