import { describe, it, expect, afterEach } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { readPluginsList } from "../../src/plugin/plugins-list.js";

let dir: string | undefined;
afterEach(() => {
  if (dir) fs.rmSync(dir, { recursive: true, force: true });
  dir = undefined;
});

function config(text: string): string {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-plugins-list-"));
  const file = path.join(dir, "ue-mcp.yml");
  fs.writeFileSync(file, text);
  return file;
}

describe("readPluginsList", () => {
  it("reads a missing file or key as empty", () => {
    expect(readPluginsList(path.join(os.tmpdir(), "no-such-ue-mcp.yml"))).toEqual([]);
    expect(readPluginsList(config("project: {}\n"))).toEqual([]);
  });

  it("keeps the entries the flow schema accepts and skips the rest", () => {
    const file = config([
      "plugins:",
      "  - name: ue-mcp-plugin-a",
      "  - name: ue-mcp-plugin-b",
      "    version: 1.2.0",
      "  - name: ''",
      "  - version: 1.0.0",
      "  - just-a-string",
      "",
    ].join("\n"));
    expect(readPluginsList(file)).toEqual([
      { name: "ue-mcp-plugin-a" },
      { name: "ue-mcp-plugin-b", version: "1.2.0" },
    ]);
  });
});
