import { describe, expect, it } from "vitest";
import { pieTool } from "../../src/tools/pie.js";
import { readHandlerFile } from "../../scripts/lib/cpp-registrations.mjs";

/** The body of one FPIEHandlers method in PIEHandlers_Script.cpp. */
function method(source: string, name: string): string {
  const start = source.indexOf(`TSharedPtr<FJsonValue> FPIEHandlers::${name}(`);
  expect(start, `${name} is defined`).toBeGreaterThan(-1);
  const next = source.indexOf("TSharedPtr<FJsonValue> FPIEHandlers::", start + 1);
  return source.slice(start, next < 0 ? undefined : next);
}

describe("script_create and script_update save with the check's findings as warnings", () => {
  const source = readHandlerFile("PIEHandlers_Script.cpp");

  it.each(["PieScriptCreate", "PieScriptUpdate"])("%s refuses only what cannot be written and answers warnings", (name) => {
    const body = method(source, name);
    expect(body).toContain("RefuseUnsavableScript(Issues, Warnings)");
    expect(body).toContain('Result->SetArrayField(TEXT("warnings"), ScriptIssuesToJson(Warnings))');
    expect(body).not.toContain("ScriptIssuesErrorObject(");
  });

  it("script_validate still reports every finding as an error", () => {
    const body = method(source, "PieScriptValidate");
    expect(body).toContain('SetBoolField(TEXT("valid"), Issues.Num() == 0)');
    expect(body).toContain('SetArrayField(TEXT("errors"), ScriptIssuesToJson(Issues))');
    expect(body).not.toContain("RefuseUnsavableScript");
  });

  it("script_run does not run the static check", () => {
    const body = method(source, "PieScriptRun");
    expect(body).not.toContain("CheckTestScript");
    expect(body).not.toContain("ValidateScriptSteps");
  });

  it("only parse failures and a missing map block a save", () => {
    const validation = readHandlerFile("PIEScriptValidation.cpp");
    expect(validation.match(/bBlocksSave = true/g)).toHaveLength(2);
    expect(source.match(/bBlocksSave = true/g)).toHaveLength(1);
    expect(source.indexOf("bBlocksSave = true")).toBeGreaterThan(source.indexOf("Map not found"));
  });

  it("the action descriptions state the contract", () => {
    expect(pieTool.actions.script_create.description).toMatch(/Always saves, returning .* as warnings/);
    expect(pieTool.actions.script_update.description).toMatch(/saves with its findings as warnings/);
  });
});
