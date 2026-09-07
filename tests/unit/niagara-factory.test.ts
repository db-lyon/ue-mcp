import { describe, it, expect } from "vitest";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const HANDLERS = path.join(
  REPO,
  "plugin",
  "ue_mcp_bridge",
  "Source",
  "UE_MCP_Bridge",
  "Private",
  "Handlers",
);

/**
 * Every handler source, not only the ones named after Niagara.
 *
 * Scoping this to Niagara*.cpp left the rule off any other file that builds a
 * Niagara object, and at least one does: the demo handlers construct them
 * while assembling a scene.
 */
function handlerSources(): Array<{ file: string; text: string }> {
  return fs
    .readdirSync(HANDLERS)
    .filter((n) => n.endsWith(".cpp"))
    .map((n) => ({ file: n, text: fs.readFileSync(path.join(HANDLERS, n), "utf8") }));
}

/**
 * A Niagara system and a Niagara emitter have to be built by their editor
 * factories. A bare NewObject produces an object that looks right and then
 * takes the editor down inside AddEmitterToSystem, because the factory is what
 * sets up the emitter handle and the compiled script data that call reads.
 *
 * The rule was a note somebody had to have read. This is the check, so a
 * NewObject that reintroduces the crash fails here instead of in the engine.
 */
describe("Niagara systems and emitters are built by their factories", () => {
  const sources = handlerSources();

  it("scans every handler source, not only the ones named after Niagara", () => {
    expect(sources.length, "no handler sources found").toBeGreaterThan(10);
    expect(sources.map((s) => s.file)).toContain("DemoHandlers.cpp");
  });

  const BANNED = [
    "NewObject<UNiagaraSystem>",
    "NewObject<UNiagaraEmitter>",
    "NewObject< UNiagaraSystem >",
    "NewObject< UNiagaraEmitter >",
  ];

  it("has a non-empty list of banned constructions", () => {
    // The list drives the assertion below. Emptying it used to remove the
    // checks rather than fail them, so the suite stayed green with the rule
    // gone and only the test count moved.
    expect(BANNED.length).toBeGreaterThanOrEqual(4);
  });

  it("never constructs either type directly", () => {
    const offenders: string[] = [];
    for (const { file, text } of sources) {
      // Whitespace inside the type argument is not meaningful to the compiler,
      // so it is not meaningful here either.
      const flattened = text.replace(/NewObject\s*<\s*/g, "NewObject<").replace(/\s*>/g, ">");
      for (const banned of BANNED) {
        const needle = banned.replace(/\s+/g, "");
        if (flattened.includes(needle)) offenders.push(`${file}: ${banned}`);
      }
    }
    expect(
      offenders,
      "a bare NewObject for a Niagara system or emitter crashes the editor inside "
        + "AddEmitterToSystem. Use the editor factory instead.",
    ).toEqual([]);
  });

  it("still reaches both factories, so the check is guarding something live", () => {
    const all = sources.map((s) => s.text).join("\n");
    expect(all).toContain("NiagaraSystemFactoryNew");
    expect(all).toContain("NiagaraEmitterFactoryNew");
  });

  it("leaves sub-object creation alone, which is correct with NewObject", () => {
    // Renderers, scripts and simulation stages are owned sub-objects and have
    // no factory. Banning NewObject outright would be wrong, so the rule names
    // the two types that crash.
    const all = sources.map((s) => s.text).join("\n");
    expect(all).toMatch(/NewObject<UNiagara(RendererProperties|Script)/);
  });
});
