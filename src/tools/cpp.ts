import * as fs from "node:fs";
import * as path from "node:path";
import { z } from "zod";
import type { ToolDef } from "../types.js";

const UCLASS_RE = /UCLASS\(([^)]*)\)\s*class\s+(?:\w+_API\s+)?(\w+)\s*(?::\s*public\s+([\w:,\s]+))?\s*\{/g;
const USTRUCT_RE = /USTRUCT\(([^)]*)\)\s*struct\s+(?:\w+_API\s+)?(\w+)\s*(?::\s*public\s+(\w+))?\s*\{/g;
const UENUM_RE = /UENUM\(([^)]*)\)\s*enum\s+(?:class\s+)?(\w+)/g;
const UPROPERTY_RE = /UPROPERTY\(([^)]*)\)\s*(?:(?:TArray|TMap|TSet|TSubclassOf|TSoftObjectPtr|TObjectPtr|TWeakObjectPtr)<[^>]+>|[\w:*&]+)\s+(\w+)/g;
const UFUNCTION_RE = /UFUNCTION\(([^)]*)\)\s*(?:virtual\s+)?(?:static\s+)?([\w:*&<>]+)\s+(\w+)\s*\(/g;
const ENUM_VALUE_RE = /(\w+)\s*(?:=\s*[^,]+)?\s*(?:UMETA\(([^)]*)\))?\s*,?/g;

export const cppTools: ToolDef[] = [
  {
    name: "read_cpp_header",
    description:
      "Parse a C++ header file and extract UCLASS, USTRUCT, UENUM declarations with their " +
      "UPROPERTY and UFUNCTION members. Works on any .h file in the project Source directory.",
    schema: {
      headerPath: z.string().describe("Path to the .h file (relative to Source, or absolute)"),
    },
    handler: async (ctx, params) => {
      ctx.project.ensureLoaded();
      const resolved = resolveHeaderPath(ctx.project.projectDir!, params.headerPath as string);
      if (!fs.existsSync(resolved)) throw new Error(`Header not found: ${resolved}`);
      const content = fs.readFileSync(resolved, "utf-8");
      return parseHeader(content, resolved);
    },
  },
  {
    name: "read_module",
    description:
      "Read a UE C++ module: list all headers in the module's source directory, " +
      "plus the Build.cs configuration.",
    schema: {
      moduleName: z.string().describe("Module name (e.g. 'MyGame', 'MyGameEditor')"),
    },
    handler: async (ctx, params) => {
      ctx.project.ensureLoaded();
      const sourceDir = path.join(ctx.project.projectDir!, "Source");
      const moduleName = params.moduleName as string;
      const moduleDir = path.join(sourceDir, moduleName);

      if (!fs.existsSync(moduleDir)) throw new Error(`Module directory not found: ${moduleDir}`);

      const headers: string[] = [];
      const sources: string[] = [];
      collectFiles(moduleDir, headers, sources);

      const buildCs = path.join(moduleDir, `${moduleName}.Build.cs`);
      const buildCsContent = fs.existsSync(buildCs)
        ? fs.readFileSync(buildCs, "utf-8")
        : null;

      return {
        moduleName,
        path: moduleDir,
        headerCount: headers.length,
        sourceCount: sources.length,
        headers: headers.map((h) => path.relative(moduleDir, h).replace(/\\/g, "/")),
        sources: sources.map((s) => path.relative(moduleDir, s).replace(/\\/g, "/")),
        buildCs: buildCsContent,
      };
    },
  },
  {
    name: "list_modules",
    description:
      "List all C++ modules in the project's Source directory with their Build.cs files.",
    schema: {},
    handler: async (ctx) => {
      ctx.project.ensureLoaded();
      const sourceDir = path.join(ctx.project.projectDir!, "Source");
      if (!fs.existsSync(sourceDir)) throw new Error(`Source directory not found: ${sourceDir}`);

      const modules: Array<{ name: string; path: string; hasBuildCs: boolean }> = [];

      for (const entry of fs.readdirSync(sourceDir, { withFileTypes: true })) {
        if (!entry.isDirectory()) continue;
        const buildCs = path.join(sourceDir, entry.name, `${entry.name}.Build.cs`);
        modules.push({
          name: entry.name,
          path: path.join(sourceDir, entry.name),
          hasBuildCs: fs.existsSync(buildCs),
        });
      }

      return { sourceDir, moduleCount: modules.length, modules };
    },
  },
  {
    name: "search_cpp",
    description:
      "Search C++ source files (.h, .cpp) in the project for a text string. " +
      "Returns matching lines with file paths and line numbers.",
    schema: {
      query: z.string().describe("Text to search for (case-insensitive)"),
      directory: z.string().optional().describe("Subdirectory to search in (relative to Source)"),
    },
    handler: async (ctx, params) => {
      ctx.project.ensureLoaded();
      const sourceDir = path.join(ctx.project.projectDir!, "Source");
      const searchDir = params.directory
        ? path.join(sourceDir, params.directory as string)
        : sourceDir;

      if (!fs.existsSync(searchDir)) throw new Error(`Directory not found: ${searchDir}`);

      const query = (params.query as string).toLowerCase();
      const results: Array<{ file: string; line: number; content: string }> = [];

      function search(dir: string): void {
        for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
          const full = path.join(dir, entry.name);
          if (entry.isDirectory()) {
            search(full);
          } else if (/\.(h|cpp|inl)$/i.test(entry.name)) {
            const lines = fs.readFileSync(full, "utf-8").split(/\r?\n/);
            for (let i = 0; i < lines.length; i++) {
              if (lines[i].toLowerCase().includes(query)) {
                results.push({
                  file: path.relative(sourceDir, full).replace(/\\/g, "/"),
                  line: i + 1,
                  content: lines[i].trimEnd(),
                });
                if (results.length >= 500) return;
              }
            }
          }
        }
      }

      search(searchDir);
      return { query: params.query, directory: params.directory ?? "(all)", resultCount: results.length, results };
    },
  },
];

function resolveHeaderPath(projectDir: string, headerPath: string): string {
  if (path.isAbsolute(headerPath)) return headerPath;
  const sourceDir = path.join(projectDir, "Source");
  return path.join(sourceDir, headerPath);
}

function collectFiles(dir: string, headers: string[], sources: string[]): void {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      collectFiles(full, headers, sources);
    } else if (entry.name.endsWith(".h")) {
      headers.push(full);
    } else if (entry.name.endsWith(".cpp")) {
      sources.push(full);
    }
  }
}

function parseHeader(content: string, filePath: string) {
  const classes: unknown[] = [];
  const structs: unknown[] = [];
  const enums: unknown[] = [];

  for (const m of content.matchAll(UCLASS_RE)) {
    classes.push({ name: m[2], specifiers: m[1].trim(), parent: m[3]?.trim() ?? null });
  }
  for (const m of content.matchAll(USTRUCT_RE)) {
    structs.push({ name: m[2], specifiers: m[1].trim(), parent: m[3]?.trim() ?? null });
  }

  for (const m of content.matchAll(UENUM_RE)) {
    const enumName = m[2];
    const afterEnum = content.slice(m.index! + m[0].length);
    const braceStart = afterEnum.indexOf("{");
    const braceEnd = afterEnum.indexOf("}");
    const values: Array<{ name: string; meta?: string }> = [];
    if (braceStart >= 0 && braceEnd > braceStart) {
      const body = afterEnum.slice(braceStart + 1, braceEnd);
      for (const vm of body.matchAll(ENUM_VALUE_RE)) {
        if (vm[1] && !vm[1].startsWith("//"))
          values.push({ name: vm[1], meta: vm[2]?.trim() || undefined });
      }
    }
    enums.push({ name: enumName, specifiers: m[1].trim(), values });
  }

  const properties: unknown[] = [];
  for (const m of content.matchAll(UPROPERTY_RE)) {
    properties.push({ name: m[2], specifiers: m[1].trim() });
  }

  const functions: unknown[] = [];
  for (const m of content.matchAll(UFUNCTION_RE)) {
    functions.push({ name: m[3], returnType: m[2], specifiers: m[1].trim() });
  }

  return {
    path: filePath,
    classes,
    structs,
    enums,
    properties,
    functions,
  };
}
