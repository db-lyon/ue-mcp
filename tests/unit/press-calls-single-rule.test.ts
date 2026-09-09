import { describe, it, expect } from "vitest";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const SRC = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..", "src");

function tsFiles(dir: string): string[] {
  return fs.readdirSync(dir, { withFileTypes: true }).flatMap((e) => {
    const full = path.join(dir, e.name);
    if (e.isDirectory()) return tsFiles(full);
    return e.isFile() && e.name.endsWith(".ts") ? [full] : [];
  });
}

/**
 * Whether a caller is handed the calls that press a dialog's buttons is ONE
 * rule, and it lives in DialogGuard.handsOverPressCalls. Every other site may
 * forward a decision it was given, and may not make one.
 *
 * Three routes had each grown their own copy, and two of them ignored whether
 * there was anybody to elicit from, so interactive on an unattended client
 * handed the buttons to the agent - which is the leak defer exists to close.
 * Copies drift, so the property is asserted rather than the copies audited.
 */
describe("the press-calls rule has exactly one implementation", () => {
  const decides = /pressCalls:\s*(.+)$/;

  it("is decided only by the guard, and forwarded everywhere else", () => {
    const offenders: string[] = [];
    for (const file of tsFiles(SRC)) {
      if (path.basename(file) === "dialog-guard.ts") continue;
      const lines = fs.readFileSync(file, "utf-8").split("\n");
      lines.forEach((line, i) => {
        const m = decides.exec(line.trim());
        if (!m) return;
        // Trailing punctuation from an inline object or argument list.
        const value = m[1].replace(/[\s,}]+$/, "").trim();
        const forwarded = value === "opts.pressCalls" || value === "pressCalls";
        const delegated = value.startsWith("DialogGuard.handsOverPressCalls(");
        // A declaration of the option itself, not a decision about it.
        const declaration = /^boolean/.test(value);
        if (!forwarded && !delegated && !declaration) {
          offenders.push(`${path.relative(SRC, file)}:${i + 1}  ${line.trim()}`);
        }
      });
    }
    expect(offenders, `these decide press calls themselves instead of asking the guard:\n${offenders.join("\n")}`)
      .toEqual([]);
  });

  it("is not decided in the editor lifecycle at all", () => {
    // The lifecycle actions have no dialog behaviour, so editor-control has
    // nothing to decide about press calls and must not reach the rule either.
    const control = fs.readFileSync(path.join(SRC, "editor-control.ts"), "utf-8");
    expect(control).not.toContain("handsOverPressCalls");
    expect(control).not.toMatch(/pressCalls:/);
  });
});
