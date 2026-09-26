import { describe, it, expect, afterEach } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import {
  configLayerFiles,
  readConfigDoc,
  ueMcpBlockOf,
  writeConfigDoc,
} from "../../src/ue-mcp-config.js";

const dirs: string[] = [];
function tempDir(): string {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-config-"));
  dirs.push(dir);
  return dir;
}

afterEach(() => {
  for (const dir of dirs.splice(0)) fs.rmSync(dir, { recursive: true, force: true });
});

describe("ue-mcp-config", () => {
  it("reads an absent, empty or non-mapping file as {}", () => {
    const dir = tempDir();
    expect(readConfigDoc(path.join(dir, "missing.yml"))).toEqual({});
    fs.writeFileSync(path.join(dir, "empty.yml"), "");
    expect(readConfigDoc(path.join(dir, "empty.yml"))).toEqual({});
    fs.writeFileSync(path.join(dir, "list.yml"), "- a\n- b\n");
    expect(readConfigDoc(path.join(dir, "list.yml"))).toEqual({});
  });

  it("throws on a file that does not parse, unless told what to do instead", () => {
    const file = path.join(tempDir(), "bad.yml");
    fs.writeFileSync(file, "ue-mcp: [unclosed\n");
    expect(() => readConfigDoc(file)).toThrow();
    const seen: unknown[] = [];
    expect(readConfigDoc(file, (e) => seen.push(e))).toEqual({});
    expect(seen).toHaveLength(1);
  });

  it("round-trips a document, keeping integer step keys unquoted", () => {
    const file = path.join(tempDir(), "nested", "ue-mcp.yml");
    writeConfigDoc(file, { "ue-mcp": { version: 1 }, flows: { f: { steps: { "1": { task: "x" } } } } });
    expect(fs.readFileSync(file, "utf-8")).toContain("\n      1:");
    expect(ueMcpBlockOf(readConfigDoc(file))).toEqual({ version: 1 });
  });

  it("lists the cascade low to high, with the overlay only when one is named", () => {
    const dir = tempDir();
    expect(configLayerFiles(dir).map((l) => l.target)).toEqual(["global", "project", "local"]);
    const layers = configLayerFiles(dir, "ci");
    expect(layers.map((l) => l.target)).toEqual(["global", "project", "env", "local"]);
    expect(layers[2].file).toBe(path.join(dir, "ue-mcp.ci.yml"));
  });
});
