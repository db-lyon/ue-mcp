import { describe, expect, it } from "vitest";
import { removeUProjectPluginEntry, uprojectListsPlugin } from "../../src/config/uproject-plugins.js";

const entry = (name: string, eol: string, extra = "") => `\t\t{${eol}\t\t\t"Name": "${name}",${extra}${eol}\t\t\t"Enabled": true${eol}\t\t}`;
const file = (names: string[], eol = "\n") =>
  ["{", '\t"FileVersion": 3,', '\t"Description": "a } and a [ in a string",', '\t"Plugins": [', names.map((n) => entry(n, eol)).join(`,${eol}`), "\t]", "}"].join(eol);

describe("removeUProjectPluginEntry", () => {
  it("removes a middle, first or last entry and leaves the rest byte for byte", () => {
    for (const eol of ["\n", "\r\n"]) {
      expect(removeUProjectPluginEntry(file(["A", "X", "B"], eol), "X")).toEqual({ text: file(["A", "B"], eol), removed: 1 });
      expect(removeUProjectPluginEntry(file(["X", "A", "B"], eol), "X")).toEqual({ text: file(["A", "B"], eol), removed: 1 });
      expect(removeUProjectPluginEntry(file(["A", "B", "X"], eol), "X")).toEqual({ text: file(["A", "B"], eol), removed: 1 });
    }
  });

  it("empties the array when the entry was the only one, and removes duplicates", () => {
    const only = removeUProjectPluginEntry(file(["X"]), "X");
    expect(only.removed).toBe(1);
    expect(JSON.parse(only.text).Plugins).toEqual([]);
    expect(removeUProjectPluginEntry(file(["X", "A", "X"]), "X")).toEqual({ text: file(["A"]), removed: 2 });
  });

  it("leaves a file without the entry, or without Plugins, unchanged", () => {
    expect(removeUProjectPluginEntry(file(["A"]), "X")).toEqual({ text: file(["A"]), removed: 0 });
    const bare = '{\r\n\t"FileVersion": 3\r\n}';
    expect(removeUProjectPluginEntry(bare, "X")).toEqual({ text: bare, removed: 0 });
  });

  it("matches the name exactly, not a key elsewhere in an entry", () => {
    const text = ["{", '\t"Plugins": [', entry("A", "\n", ' "Note": "X",'), "\t]", "}"].join("\n");
    expect(removeUProjectPluginEntry(text, "X").removed).toBe(0);
    expect(uprojectListsPlugin(text, "A")).toBe(true);
    expect(uprojectListsPlugin(text, "X")).toBe(false);
  });

  it("refuses a file that is not JSON", () => {
    expect(() => removeUProjectPluginEntry("{ nope", "X")).toThrow();
  });
});
