import * as fs from "node:fs";
import * as path from "node:path";

// Regex-based C++ header inspection. Not a full parser — extracts UCLASS /
// USTRUCT / UENUM / UPROPERTY / UFUNCTION declarations well enough to feed
// agents looking for the shape of a module.

const UCLASS_RE = /UCLASS\(([^)]*)\)\s*class\s+(?:\w+_API\s+)?(\w+)\s*(?::\s*public\s+([\w:,\s]+))?\s*\{/g;
const USTRUCT_RE = /USTRUCT\(([^)]*)\)\s*struct\s+(?:\w+_API\s+)?(\w+)\s*(?::\s*public\s+(\w+))?\s*\{/g;
const UENUM_RE = /UENUM\(([^)]*)\)\s*enum\s+(?:class\s+)?(\w+)/g;
const UPROPERTY_RE = /UPROPERTY\(([^)]*)\)\s*(?:(?:TArray|TMap|TSet|TSubclassOf|TSoftObjectPtr|TObjectPtr|TWeakObjectPtr)<[^>]+>|[\w:*&]+)\s+(\w+)/g;
const UFUNCTION_RE = /UFUNCTION\(([^)]*)\)\s*(?:virtual\s+)?(?:static\s+)?([\w:*&<>]+)\s+(\w+)\s*\(/g;
const ENUM_VALUE_RE = /(\w+)\s*(?:=\s*[^,]+)?\s*(?:UMETA\(([^)]*)\))?\s*,?/g;

export interface ParsedHeader {
  path: string;
  classes: unknown[];
  structs: unknown[];
  enums: unknown[];
  properties: unknown[];
  functions: unknown[];
}

export function parseHeader(content: string, filePath: string): ParsedHeader {
  const classes: unknown[] = [], structs: unknown[] = [], enums: unknown[] = [];
  for (const m of content.matchAll(UCLASS_RE)) classes.push({ name: m[2], specifiers: m[1].trim(), parent: m[3]?.trim() ?? null });
  for (const m of content.matchAll(USTRUCT_RE)) structs.push({ name: m[2], specifiers: m[1].trim(), parent: m[3]?.trim() ?? null });
  for (const m of content.matchAll(UENUM_RE)) {
    const enumName = m[2];
    const afterEnum = content.slice(m.index! + m[0].length);
    const braceStart = afterEnum.indexOf("{");
    const braceEnd = afterEnum.indexOf("}");
    const values: Array<{ name: string; meta?: string }> = [];
    if (braceStart >= 0 && braceEnd > braceStart) {
      for (const vm of afterEnum.slice(braceStart + 1, braceEnd).matchAll(ENUM_VALUE_RE))
        if (vm[1] && !vm[1].startsWith("//")) values.push({ name: vm[1], meta: vm[2]?.trim() || undefined });
    }
    enums.push({ name: enumName, specifiers: m[1].trim(), values });
  }
  const properties: unknown[] = [], functions: unknown[] = [];
  for (const m of content.matchAll(UPROPERTY_RE)) properties.push({ name: m[2], specifiers: m[1].trim() });
  for (const m of content.matchAll(UFUNCTION_RE)) functions.push({ name: m[3], returnType: m[2], specifiers: m[1].trim() });
  return { path: filePath, classes, structs, enums, properties, functions };
}

/** Walk a module tree and collect .h / .cpp files. Mutates the arrays in-place. */
export function collectFiles(dir: string, headers: string[], sources: string[]): void {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) collectFiles(full, headers, sources);
    else if (entry.name.endsWith(".h")) headers.push(full);
    else if (entry.name.endsWith(".cpp")) sources.push(full);
  }
}

/**
 * Returns every Source/ directory under projectDir that holds at least one
 * <Module>.Build.cs file. Handles both the standard <ProjectDir>/Source/
 * layout and the nested <ProjectDir>/<ProjectName>/Source/ layout (#257).
 */
export function findSourceRoots(projectDir: string, projectName: string | null): string[] {
  const roots: string[] = [];
  const candidates = [
    path.join(projectDir, "Source"),
    projectName ? path.join(projectDir, projectName, "Source") : null,
  ].filter((c): c is string => c !== null);
  for (const c of candidates) {
    if (fs.existsSync(c) && fs.statSync(c).isDirectory()) roots.push(c);
  }
  return roots;
}

/** Find the source root that owns a given moduleName (a subdir holding <module>.Build.cs). */
export function resolveModuleDir(projectDir: string, projectName: string | null, moduleName: string): string | null {
  for (const root of findSourceRoots(projectDir, projectName)) {
    const modDir = path.join(root, moduleName);
    if (fs.existsSync(path.join(modDir, `${moduleName}.Build.cs`))) return modDir;
  }
  return null;
}
