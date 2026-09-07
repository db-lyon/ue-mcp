import { describe, it, expect } from "vitest";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const SCRIPTS = path.join(REPO, "scripts");
const SRC = path.join(REPO, "src");

/** `import { a, b } from '../dist/x.js'` and the await-import form. */
function distImports(text: string): Array<{ names: string[]; module: string }> {
  const out: Array<{ names: string[]; module: string }> = [];
  const patterns = [
    /import\s*\{([^}]+)\}\s*from\s*['"]([^'"]*dist\/[^'"]+)['"]/g,
    /\{([^}]+)\}\s*=\s*await\s+import\(\s*['"]([^'"]*dist\/[^'"]+)['"]\s*\)/g,
  ];
  for (const re of patterns) {
    let m;
    while ((m = re.exec(text)) !== null) {
      const names = m[1]
        .split(",")
        .map((n) => n.split(" as ")[0].trim())
        .filter(Boolean);
      out.push({ names, module: m[2] });
    }
  }
  return out;
}

/** The names a source module exports, read from the TypeScript it is built from. */
function exportedNames(tsFile: string): Set<string> {
  const text = fs.readFileSync(tsFile, "utf8");
  const names = new Set<string>();
  const re = /^export\s+(?:async\s+)?(?:function|const|class|interface|type|enum)\s+([A-Za-z0-9_$]+)/gm;
  let m;
  while ((m = re.exec(text)) !== null) names.add(m[1]);
  // `export { a, b }` re-export lists.
  const re2 = /^export\s*\{([^}]+)\}/gm;
  while ((m = re2.exec(text)) !== null) {
    for (const n of m[1].split(",")) {
      const cleaned = n.includes(" as ") ? n.split(" as ")[1] : n;
      if (cleaned.trim()) names.add(cleaned.trim());
    }
  }
  return names;
}

/**
 * A script that imports a name the build no longer exports fails at the moment
 * somebody runs it, which for these scripts means partway through launching or
 * building an editor. Nothing else catches it: the scripts are plain JavaScript
 * and are not type-checked, and the import is resolved at runtime.
 */
describe("every name a script imports from the build still exists", () => {
  const scripts = fs
    .readdirSync(SCRIPTS)
    .filter((n) => n.endsWith(".js") || n.endsWith(".mjs"))
    .map((n) => ({ file: n, text: fs.readFileSync(path.join(SCRIPTS, n), "utf8") }));

  it("finds the scripts it is supposed to be guarding", () => {
    expect(scripts.length).toBeGreaterThan(5);
  });

  const missing: string[] = [];
  for (const { file, text } of scripts) {
    for (const { names, module } of distImports(text)) {
      const base = module.slice(module.indexOf("dist/") + "dist/".length).replace(/\.js$/, "");
      const tsFile = path.join(SRC, `${base}.ts`);
      if (!fs.existsSync(tsFile)) {
        missing.push(`${file} imports from ${module}, which has no source at src/${base}.ts`);
        continue;
      }
      const exported = exportedNames(tsFile);
      for (const name of names) {
        if (!exported.has(name)) missing.push(`${file} imports ${name} from ${module}, which does not export it`);
      }
    }
  }

  it("resolves every imported name", () => {
    expect(missing, missing.join("\n")).toEqual([]);
  });
});
