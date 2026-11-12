import { afterEach, describe, expect, it } from "vitest";
import { buildTagTree, parseIni } from "../../src/codeintel/ue-ini.js";

/**
 * project(read_config) and list_config_tags parse a project's .ini files,
 * which arrive with the project. A section, key or tag named __proto__,
 * constructor or prototype is data like any other and changes no object
 * outside the result.
 */
describe("ini and tag keys that name Object internals", () => {
  afterEach(() => {
    delete (Object.prototype as Record<string, unknown>).polluted;
  });

  it("keeps a [__proto__] section as data", () => {
    const sections = parseIni("[__proto__]\npolluted=yes\n[constructor]\nprototype=x\n[/Script/Engine.Engine]\n+polluted=no\n");
    expect(({} as Record<string, unknown>).polluted).toBeUndefined();
    expect(Object.keys(sections)).toEqual(["__proto__", "constructor", "/Script/Engine.Engine"]);
    expect(sections["__proto__"].polluted).toBe("yes");
    expect(sections["constructor"].prototype).toBe("x");
    expect(sections["/Script/Engine.Engine"].polluted).toBe("no");
  });

  it("keeps a __proto__ key as data", () => {
    const sections = parseIni("[S]\n__proto__=yes\n");
    expect(Object.getPrototypeOf({})).toBe(Object.prototype);
    expect(Object.keys(sections.S)).toEqual(["__proto__"]);
    expect(JSON.parse(JSON.stringify(sections))).toEqual(JSON.parse('{"S":{"__proto__":"yes"}}'));
  });

  it("keeps a __proto__ tag segment as data", () => {
    const tree = buildTagTree(["__proto__.polluted", "constructor.prototype.x", "Ability.Fire"]);
    expect(({} as Record<string, unknown>).polluted).toBeUndefined();
    expect(Object.keys(tree)).toEqual(["__proto__", "constructor", "Ability"]);
    expect(JSON.stringify(tree)).toBe('{"__proto__":{"polluted":{}},"constructor":{"prototype":{"x":{}}},"Ability":{"Fire":{}}}');
  });
});
