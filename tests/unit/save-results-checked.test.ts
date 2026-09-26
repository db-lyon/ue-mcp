/**
 * A handler that saves must read whether the save happened (#931). The save
 * helpers are public API that other plugins call, so this is enforced here
 * over the bridge's own sources rather than with [[nodiscard]] on the header.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { describe, expect, it } from "vitest";

const SOURCE = path.resolve(import.meta.dirname, "../../plugin/ue_mcp_bridge/Source");
const BARE_SAVE =
  /^\s*(?:SaveAssetPackage|SaveAssetPackageChecked|UEditorAssetLibrary::SaveAsset|UEditorAssetLibrary::SaveLoadedAsset)\s*\(/;

function cppFiles(dir: string): string[] {
  return fs.readdirSync(dir, { withFileTypes: true }).flatMap((e) => {
    const full = path.join(dir, e.name);
    if (e.isDirectory()) return cppFiles(full);
    return /\.(cpp|h)$/.test(e.name) ? [full] : [];
  });
}

describe("bridge handlers check their saves", () => {
  it("never call a save helper as a bare statement", () => {
    const bare: string[] = [];
    for (const file of cppFiles(SOURCE)) {
      const lines = fs.readFileSync(file, "utf8").split(/\r?\n/);
      lines.forEach((line, i) => {
        // A line continuing an argument list is an argument, not a statement.
        const continues = /[(,]\s*$/.test(lines[i - 1] ?? "");
        if (BARE_SAVE.test(line) && !continues) {
          bare.push(`${path.relative(SOURCE, file)}:${i + 1}: ${line.trim()}`);
        }
      });
    }
    expect(bare).toEqual([]);
  });
});
