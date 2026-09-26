/**
 * The files `ue-mcp plugin create` writes: a plugin scaffold covering every
 * extension shape, with a dormant native C++ module and a pre-publish check.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { packageVersion } from "./core/package-root.js";
import { readEnv } from "./core/env.js";

export function deriveDefaultPrefix(pkgName: string): string {
  const normalized = pkgName.replace(/[^a-z0-9]+/gi, "_").toLowerCase();
  const parts = normalized.split("_").filter(Boolean);
  const seed = parts.length > 0 ? parts.map((s) => s[0]).join("").slice(0, 4) : "plg";
  return /^[a-z]/.test(seed) ? seed : `p${seed}`;
}

/**
 * Scaffold a plugin that is a SUPERSET of every extension shape ue-mcp
 * supports, so an author never has to discover a capability before using it:
 *
 *   - inject:   add prefixed actions onto a built-in category
 *   - provides: own a brand-new top-level category (unprefixed actions)
 *   - flows:    chain tasks/actions into one orchestrated call
 *   - nativeModule: ship C++ handlers on the bridge - scaffolded but DORMANT
 *
 * The native C++ module is the one capability that cannot be a live default:
 * declaring `nativeModule:` makes `ue-mcp plugin install` deploy the module and
 * force a UE rebuild. So the C++ skeleton is written to disk under
 * ue/Plugins/<UePlugin>/, but its manifest block is commented out. Uncommenting
 * three lines activates it - no separate "style" flag, no compile tax until the
 * author opts in. The author deletes whatever shape they don't want.
 */
export function writeScaffold(dir: string, pkgName: string, prefix: string): void {
  const helloClass = "Hello";
  const greetClass = "Greet";
  const uePlugin = deriveUePluginName(pkgName);
  const nativeCategory = `${prefix}_native`;
  // The scaffold's tasks import `ue-mcp/task`, which only exists from the
  // version that introduced it. Default the floor to the running server's
  // version so a freshly scaffolded plugin declares a dependency that can
  // actually resolve the import; an explicit env override still wins.
  const minServer = readEnv("pluginMinServer") ?? packageVersion();
  const year = new Date().getFullYear();

  const pkgJson = {
    name: pkgName,
    version: "0.1.0",
    description: `${pkgName} - ue-mcp plugin`,
    type: "module",
    main: "dist/index.js",
    // `ue` ships the dormant native C++ source so authors can activate it
    // without re-vendoring; `LICENSE` is included for npm publish hygiene.
    files: ["dist", "ue", "ue-mcp.plugin.yml", "knowledge", "skills", "README.md", "LICENSE"],
    keywords: ["ue-mcp-plugin", "unreal-engine"],
    author: "",
    license: "MIT",
    repository: { type: "git", url: "" },
    peerDependencies: {
      "ue-mcp": `>=${minServer}`,
    },
    devDependencies: {
      "ue-mcp": `^${minServer}`,
      "js-yaml": "^4.1.0",
      "@types/js-yaml": "^4.0.9",
      typescript: "^5.7.0",
    },
    scripts: {
      build: "tsc",
      check: "node scripts/check.mjs && ue-mcp plugin check-skills .",
      prepublishOnly: "npm run build && npm run check",
    },
  };
  fs.writeFileSync(path.join(dir, "package.json"), JSON.stringify(pkgJson, null, 2) + "\n");

  const tsconfig = {
    compilerOptions: {
      target: "ES2022",
      module: "Node16",
      moduleResolution: "Node16",
      outDir: "dist",
      rootDir: "src",
      strict: true,
      esModuleInterop: true,
      declaration: true,
      sourceMap: true,
      skipLibCheck: true,
    },
    include: ["src/**/*"],
  };
  fs.writeFileSync(path.join(dir, "tsconfig.json"), JSON.stringify(tsconfig, null, 2) + "\n");

  const manifestYaml =
`actionPrefix: ${prefix}
minServerVersion: ${minServer}

# This scaffold is a SUPERSET: it shows every way a plugin can extend ue-mcp.
# Keep the shapes you need and delete the rest.
#   1) inject   - add actions onto a built-in category (names are prefixed)
#   2) provides - own a brand-new top-level category (names are NOT prefixed)
#   3) flows    - chain tasks/actions into one orchestrated call
#   4) nativeModule (commented, at the bottom) - ship C++ handlers on the bridge

# 1) inject: '${prefix}_hello' lands on the built-in 'project' tool, reached as
#    project(action="${prefix}_hello").
inject:
  project:
    hello:
      task: ${prefix}.hello
      description: "Greet by name - a stand-in inject action this scaffold ships with"
      schema:
        name: { type: string, description: "Who to greet (default: world)" }

# 2) provides: a category the plugin owns. Actions are NOT prefixed - the
#    category name is the namespace, e.g. ${prefix}(action="greet").
provides:
  ${prefix}:
    description: "${pkgName} - example plugin-owned category"
    actions:
      greet:
        task: ${prefix}.greet
        description: "Greet by name from a plugin-owned category"
        schema:
          name: { type: string, description: "Who to greet (default: world)" }

tasks:
  ${prefix}.hello:
    class_path: tasks/${helloClass}
  ${prefix}.greet:
    class_path: tasks/${greetClass}

# 3) flows: chain tasks/actions into one call. This one just runs the hello
#    task; expand 'steps' to orchestrate real multi-step work.
flows:
  ${prefix}_demo:
    description: "Example flow: run the hello task"
    steps:
      1:
        task: ${prefix}.hello
        options:
          name: world

# 4) nativeModule: ship C++ handlers that register on the bridge. DORMANT by
#    default - the C++ skeleton already lives under ue/Plugins/${uePlugin}/, but
#    it is not deployed or compiled until you UNCOMMENT this block. Activating it
#    makes 'ue-mcp plugin install' deploy the module and require a UE rebuild.
#    The handlers then surface unprefixed under a '${nativeCategory}' category,
#    e.g. ${nativeCategory}(action="echo").
# nativeModule:
#   uePluginName: ${uePlugin}
#   minBridgeApi: 1
#   source: ue/Plugins/${uePlugin}
#   category: ${nativeCategory}
#   categoryDescription: "${pkgName} native C++ handlers"
#   handlers:
#     echo:
#       description: "Echo a name back from native C++ (required: name)"
#       schema:
#         name: { type: string, description: "Name to echo" }
`;
  fs.writeFileSync(path.join(dir, "ue-mcp.plugin.yml"), manifestYaml);

  // ── TypeScript: index stub + the two example tasks ──────────────────────
  fs.mkdirSync(path.join(dir, "src", "tasks"), { recursive: true });

  const indexTs =
`// Entry point for the npm package. ue-mcp loads tasks by their manifest
// class_path, not through this file, so it can stay empty - but package.json's
// "main" points here, so the build must emit it.
export {};
`;
  fs.writeFileSync(path.join(dir, "src", "index.ts"), indexTs);

  const helloTs =
`import { UeMcpTask, type TaskResult } from "ue-mcp/task";

interface Options {
  name?: string;
}

/** Backs project(action="${prefix}_hello") and the ${prefix}_demo flow. */
export default class ${helloClass} extends UeMcpTask<Options> {
  get taskName() { return "${prefix}.hello"; }

  async execute(): Promise<TaskResult> {
    const who = this.options.name ?? "world";
    return { success: true, data: { greeting: \`hello, \${who}\` } };
  }
}
`;
  fs.writeFileSync(path.join(dir, "src", "tasks", `${helloClass}.ts`), helloTs);

  const greetTs =
`import { UeMcpTask, type TaskResult } from "ue-mcp/task";

interface Options {
  name?: string;
}

/**
 * Backs ${prefix}(action="greet") - the plugin-owned category. Swap the body
 * for real work; compose built-ins with \`this.call("level.get_outliner", {})\`.
 */
export default class ${greetClass} extends UeMcpTask<Options> {
  get taskName() { return "${prefix}.greet"; }

  async execute(): Promise<TaskResult> {
    const who = this.options.name ?? "world";
    return { success: true, data: { greeting: \`greetings, \${who}\` } };
  }
}
`;
  fs.writeFileSync(path.join(dir, "src", "tasks", `${greetClass}.ts`), greetTs);

  // ── Pre-publish sanity check ────────────────────────────────────────────
  fs.mkdirSync(path.join(dir, "scripts"), { recursive: true });
  fs.writeFileSync(path.join(dir, "scripts", "check.mjs"), CHECK_SCRIPT);

  // ── Knowledge ───────────────────────────────────────────────────────────
  fs.mkdirSync(path.join(dir, "knowledge"), { recursive: true });
  fs.writeFileSync(
    path.join(dir, "knowledge", "project.md"),
    `# ${pkgName} - project actions\n\nDescribe in one screen what your plugin contributes to the project category.\n`,
  );

  // ── Skills ──────────────────────────────────────────────────────────────
  fs.mkdirSync(path.join(dir, "skills", "workflow"), { recursive: true });
  fs.writeFileSync(
    path.join(dir, "skills", "workflow", "SKILL.md"),
    `---\nname: workflow\ndescription: Use when working with ${pkgName}. Say here when Claude should load this guide.\n---\n\n`
      + `# ${pkgName} workflow\n\n1. \`${prefix}(action="greet")\` to check the plugin is loaded.\n`,
  );

  // ── Dormant native C++ module ────────────────────────────────────────────
  writeNativeSkeleton(dir, uePlugin, pkgName);

  // ── README / LICENSE / .gitignore ────────────────────────────────────────
  const readme =
`# ${pkgName}

A ue-mcp plugin. Install with:

\`\`\`bash
ue-mcp plugin install ${pkgName}
\`\`\`

## What this scaffold ships

It is a **superset** - every way a plugin can extend ue-mcp, wired and working.
Keep what you want, delete the rest.

- **inject** -> \`project(action="${prefix}_hello")\` (an action added onto a built-in category)
- **provides** -> \`${prefix}(action="greet")\` (a new top-level category this plugin owns)
- **flows** -> \`${prefix}_demo\` (a chained, one-call orchestration)
- **skills** -> \`skills/workflow/SKILL.md\`, installed into the project's \`.claude/skills/\` under this plugin's prefix
- **nativeModule** -> a C++ handler skeleton under \`ue/Plugins/${uePlugin}/\`, **dormant** until you activate it

## Activate the native C++ module

The C++ source is already on disk but inert. To turn it on:

1. Uncomment the \`nativeModule:\` block at the bottom of \`ue-mcp.plugin.yml\`.
2. Reinstall (\`ue-mcp plugin install ${pkgName}\`) - this deploys the module into
   your project's \`Plugins/\` and requires a UE rebuild before the editor starts.

The handler then surfaces as \`${prefix}_native(action="echo")\`.

## Develop

\`\`\`bash
npm install
npm run build
npm run check      # validate the manifest + task wiring
\`\`\`

See https://ue-mcp.com/docs/plugins/ for the full author contract.
`;
  fs.writeFileSync(path.join(dir, "README.md"), readme);

  fs.writeFileSync(
    path.join(dir, "LICENSE"),
    `MIT License

Copyright (c) ${year} ${pkgName} authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
`,
  );

  fs.writeFileSync(
    path.join(dir, ".gitignore"),
    `node_modules/\ndist/\n*.tgz\n.DS_Store\n`,
  );
}

/**
 * Derive a UE-legal module/plugin name (PascalCase, leading letter) from the
 * npm package name. "voxel-plugin-tools" -> "VoxelPluginTools".
 */
export function deriveUePluginName(pkgName: string): string {
  const parts = pkgName.replace(/[^a-z0-9]+/gi, " ").trim().split(/\s+/).filter(Boolean);
  const pascal = parts.map((p) => p.charAt(0).toUpperCase() + p.slice(1)).join("");
  return /^[A-Za-z]/.test(pascal) ? pascal : `Plugin${pascal}`;
}

/**
 * Write a minimal, compile-ready UE C++ module under ue/Plugins/<uePlugin>/.
 * It registers one example handler ("echo") on the bridge via the public
 * UEMCP::RegisterExternalHandler API. Inert until the manifest's nativeModule:
 * block is uncommented.
 */
function writeNativeSkeleton(dir: string, uePlugin: string, pkgName: string): void {
  const base = path.join(dir, "ue", "Plugins", uePlugin);
  const moduleDir = path.join(base, "Source", uePlugin);
  const publicDir = path.join(moduleDir, "Public");
  const privateDir = path.join(moduleDir, "Private");
  const handlersDir = path.join(privateDir, "Handlers");
  fs.mkdirSync(handlersDir, { recursive: true });
  fs.mkdirSync(publicDir, { recursive: true });

  const uplugin = {
    FileVersion: 3,
    Version: 1,
    VersionName: "0.1.0",
    FriendlyName: uePlugin,
    Description: `Native C++ handlers for the ${pkgName} ue-mcp plugin`,
    Category: "Editor",
    CreatedBy: "",
    EnabledByDefault: true,
    CanContainContent: false,
    Modules: [
      { Name: uePlugin, Type: "Editor", LoadingPhase: "PostEngineInit" },
    ],
    Plugins: [
      { Name: "UE_MCP_Bridge", Enabled: true },
    ],
  };
  fs.writeFileSync(path.join(base, `${uePlugin}.uplugin`), JSON.stringify(uplugin, null, 2) + "\n");

  fs.writeFileSync(
    path.join(moduleDir, `${uePlugin}.Build.cs`),
`using UnrealBuildTool;

public class ${uePlugin} : ModuleRules
{
\tpublic ${uePlugin}(ReadOnlyTargetRules Target) : base(Target)
\t{
\t\tPCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

\t\tPublicDependencyModuleNames.AddRange(new string[]
\t\t{
\t\t\t"Core",
\t\t\t"CoreUObject",
\t\t\t"Engine",
\t\t\t"Json",
\t\t\t"JsonUtilities",
\t\t});

\t\t// UE_MCP_Bridge exposes UEMCP::RegisterExternalHandler (MCPHandlerRegistration.h).
\t\tPrivateDependencyModuleNames.AddRange(new string[]
\t\t{
\t\t\t"UE_MCP_Bridge",
\t\t});
\t}
}
`,
  );

  fs.writeFileSync(
    path.join(publicDir, `${uePlugin}Module.h`),
`#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(Log${uePlugin}, Log, All);

class F${uePlugin}Module : public IModuleInterface
{
public:
\tvirtual void StartupModule() override;
\tvirtual void ShutdownModule() override;
};
`,
  );

  fs.writeFileSync(
    path.join(privateDir, `${uePlugin}Module.cpp`),
`#include "${uePlugin}Module.h"
#include "Modules/ModuleManager.h"
#include "MCPHandlerRegistration.h"
#include "Handlers/ExampleHandlers.h"

DEFINE_LOG_CATEGORY(Log${uePlugin});
IMPLEMENT_MODULE(F${uePlugin}Module, ${uePlugin})

void F${uePlugin}Module::StartupModule()
{
\t// Bare method names. When this module is surfaced via nativeModule.category
\t// in ue-mcp.plugin.yml, ue-mcp routes <category>(action="echo") to "echo".
\tUEMCP::RegisterExternalHandler(TEXT("echo"), &FExampleHandlers::Echo);

\tUE_LOG(Log${uePlugin}, Log, TEXT("[${uePlugin}] Registered 1 handler"));
}

void F${uePlugin}Module::ShutdownModule()
{
\tUEMCP::UnregisterExternalHandler(TEXT("echo"));
}
`,
  );

  fs.writeFileSync(
    path.join(handlersDir, "ExampleHandlers.h"),
`#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/** Example external handler registered on the UE-MCP bridge. */
class FExampleHandlers
{
public:
\t/** Echo a 'name' param back. Bridge method: "echo". */
\tstatic TSharedPtr<FJsonValue> Echo(const TSharedPtr<FJsonObject>& Params);
};
`,
  );

  fs.writeFileSync(
    path.join(handlersDir, "ExampleHandlers.cpp"),
`#include "Handlers/ExampleHandlers.h"

TSharedPtr<FJsonValue> FExampleHandlers::Echo(const TSharedPtr<FJsonObject>& Params)
{
\tFString Name = TEXT("world");
\tif (Params.IsValid())
\t{
\t\tParams->TryGetStringField(TEXT("name"), Name);
\t}

\tTSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
\tResult->SetBoolField(TEXT("success"), true);
\tResult->SetStringField(TEXT("echo"), FString::Printf(TEXT("hello, %s"), *Name));
\treturn MakeShared<FJsonValueObject>(Result);
}
`,
  );
}

/**
 * Standalone pre-publish check shipped into scaffolded plugins as
 * scripts/check.mjs. Verifies the manifest parses, every action's task:
 * resolves to a tasks: entry, and every tasks: class_path points at a real
 * source/built file. No build step required - runs on plain node.
 */
const CHECK_SCRIPT =
`#!/usr/bin/env node
// Sanity-check a ue-mcp plugin: manifest parses, task references resolve, and
// every class_path points at a real source or built file.
import fs from "node:fs";
import path from "node:path";
import yaml from "js-yaml";

const root = process.cwd();
const manifestPath = path.join(root, "ue-mcp.plugin.yml");
let errors = 0;
const fail = (m) => { console.error("  x " + m); errors++; };

if (!fs.existsSync(manifestPath)) {
  console.error("ue-mcp.plugin.yml not found");
  process.exit(1);
}
const m = yaml.load(fs.readFileSync(manifestPath, "utf8")) ?? {};
const tasks = m.tasks ?? {};

// Collect every task reference from inject + provides + flows.
const refs = [];
for (const [cat, actions] of Object.entries(m.inject ?? {}))
  for (const [a, def] of Object.entries(actions))
    if (def?.task) refs.push(["inject " + cat + "." + a, def.task]);
for (const [cat, spec] of Object.entries(m.provides ?? {}))
  for (const [a, def] of Object.entries(spec?.actions ?? {}))
    if (def?.task) refs.push(["provides " + cat + "." + a, def.task]);
for (const [fname, fdef] of Object.entries(m.flows ?? {}))
  for (const [s, step] of Object.entries(fdef?.steps ?? {}))
    if (step?.task) refs.push(["flow " + fname + "." + s, step.task]);

for (const [where, task] of refs)
  if (!tasks[task]) fail(where + " references task '" + task + "' with no tasks: entry");

for (const [name, def] of Object.entries(tasks)) {
  const seg = String(def.class_path).replace(/\\./g, "/");
  const candidates = [
    path.join(root, "src", seg + ".ts"),
    path.join(root, "dist", seg + ".js"),
  ];
  if (!candidates.some((c) => fs.existsSync(c)))
    fail("task '" + name + "' class_path '" + def.class_path + "' resolves to no src/*.ts or dist/*.js");
}

if (m.nativeModule?.source && !fs.existsSync(path.join(root, m.nativeModule.source)))
  fail("nativeModule.source '" + m.nativeModule.source + "' does not exist");

if (errors) {
  console.error("\\n" + errors + " problem(s) found.");
  process.exit(1);
}
console.log("ue-mcp plugin check: OK");
`;
