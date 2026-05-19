# Authoring a ue-mcp plugin

A ue-mcp plugin is a normal npm package that injects new actions into existing built-in categories (`pcg`, `landscape`, etc.). The agent discovers them at the point of need: nothing else changes about how the agent picks tools.

This page is the author contract. The user-facing side (installing, removing, listing) is just `ue-mcp plugin install <name>`, plus a `plugins:` array in the project's `ue-mcp.yml` — see [Configuration → Plugins](configuration.md#plugins) for the consumer view.

**Live reference:** [`ue-mcp-plugin-voxel-plugin`](https://github.com/db-lyon/ue-mcp-plugin-voxel-plugin) ([npm](https://www.npmjs.com/package/ue-mcp-plugin-voxel-plugin)) ships three actions (`pcg.voxel_scatter_meshes`, `pcg.voxel_spawn_stamps`, `landscape.voxel_bake_heightmap`) and two flows. Every example in this page mirrors that package — clone it for a working starting point.

## Why injection, not a standalone tool

A standalone tool is opaque to the agent — it has no reason to open a `voxel` category during terrain work. If the action lands inside `pcg` as `pcg(action="voxel_scatter_meshes")`, the agent already working in PCG discovers it exactly when relevant. Capability appears at the point of need, not behind a door the agent has to know to open.

## Package layout

```
ue-mcp-plugin-<your-name>/
  package.json
  tsconfig.json
  ue-mcp.plugin.yml          # author declaration: actionPrefix, inject, knowledge, tasks, flows
  src/                       # author writes TypeScript here
    tasks/
      MyAction.ts            # one BaseTask subclass per file, default export
    shared/                  # optional cross-task helpers (never referenced from the declaration)
  dist/                      # tsc output - what actually ships and loads
    tasks/
      MyAction.js
  knowledge/
    pcg.md                   # one markdown file per target category
  README.md
```

Conventions:

- One task class per file, default export, extending `BaseTask` from `@db-lyon/flowkit`.
- `class_path` in the declaration is resolved against the plugin's `dist/` (try `dist/<path>.js`, then `dist/tasks/<path>.js`).
- `src/shared/` holds helpers; never reference it from the declaration.
- Compile to `dist/` with `tsc` so users need no TypeScript toolchain.
- The npm package name should start with `ue-mcp-plugin-` so it's discoverable on the registry.

## `package.json`

```json
{
  "name": "ue-mcp-plugin-voxel-plugin",
  "version": "0.1.0",
  "description": "Voxel Plugin actions for ue-mcp",
  "type": "module",
  "main": "dist/index.js",
  "files": ["dist", "ue-mcp.plugin.yml", "knowledge", "README.md"],
  "keywords": ["ue-mcp-plugin", "unreal-engine", "voxel"],
  "peerDependencies": {
    "@db-lyon/flowkit": "~0.5.2"
  },
  "devDependencies": {
    "@db-lyon/flowkit": "~0.5.2",
    "typescript": "^5.7.0"
  },
  "scripts": {
    "build": "tsc"
  }
}
```

The `ue-mcp-plugin` keyword is the registry signal. The peer-dep on `@db-lyon/flowkit` is what gives `BaseTask` its shape - your tasks must extend the same class the server uses.

## `ue-mcp.plugin.yml`

This is the only file ue-mcp reads from your package. Authored once; never edited by users.

```yaml
actionPrefix: voxel              # mandatory, lowercase, must match /^[a-z][a-z0-9_]*$/
minServerVersion: 1.0.15         # optional - the server enforces this at install and load
uePluginDependency: Voxel        # optional - the installer warns if missing from .uproject

inject:
  pcg:
    scatter_meshes:              # → pcg(action="voxel_scatter_meshes")
      task: voxel.scatter_meshes
      description: "Scatter meshes on a voxel terrain. Params: graphPath, mesh, density?"
      schema:
        graphPath: { type: string, required: true }
        mesh:      { type: string, required: true }
        density:   { type: number }

  landscape:
    bake_heightmap:              # → landscape(action="voxel_bake_heightmap")
      task: voxel.bake_heightmap
      description: "Bake a voxel terrain region to a landscape heightmap. Params: bounds, resolution?"
      schema:
        bounds:     { type: object, required: true }
        resolution: { type: number }

knowledge:
  pcg: knowledge/pcg.md
  landscape: knowledge/landscape.md

tasks:
  voxel.scatter_meshes:  { class_path: tasks/ScatterMeshes }
  voxel.bake_heightmap:  { class_path: tasks/BakeHeightmap }

flows:
  voxel_full_setup:
    description: "Full voxel scatter setup"
    rollback_on_failure: true
    steps:
      1: { task: voxel.scatter_meshes }
      2: { task: voxel.bake_heightmap }
```

The key under each category is the *bare* action name. The loader prepends your `actionPrefix` to compute the injected name: `voxel` + `scatter_meshes` → `voxel_scatter_meshes`. The user always sees the prefixed form.

Param schemas under `schema:` accept these types: `string`, `number`, `boolean`, `object`, `array`. Non-required params become optional at the top level of the host category tool's schema.

## Writing tasks

```ts
// src/tasks/ScatterMeshes.ts
import { BaseTask, type TaskResult } from "@db-lyon/flowkit";

interface ScatterMeshesOpts {
  graphPath: string;
  mesh: string;
  density?: number;
}

export default class ScatterMeshes extends BaseTask<ScatterMeshesOpts> {
  get taskName() { return "voxel.scatter_meshes"; }

  async execute(): Promise<TaskResult> {
    // Compose existing MCP actions via `this.call('<category>.<action>', ...)`.
    // The bridge, project, and logger are available on `this.ctx` exactly the
    // same way as for built-in tasks.
    const created = await this.call("pcg.create_graph", { path: this.options.graphPath });
    if (!created.success) return created;

    const node = await this.call("pcg.add_node", {
      graphPath: this.options.graphPath,
      nodeType: "VoxelSampler",
    });
    if (!node.success) return node;

    return { success: true, data: { graphPath: this.options.graphPath } };
  }
}
```

Notes:
- Compose existing actions through `this.call('<category>.<action>', params)`. Don't reach into the bridge directly unless you need to - composition gives you free observability and rollback hooks.
- If your task makes multi-step mutations, return a `rollback` record so users can opt into `rollback_on_failure: true` for safety. The plugin scaffolder turns this on by default for multi-step flows.

## Knowledge files

For each category your plugin injects into, ship a short markdown file under `knowledge/`. The server attaches it to that category's AI-facing docs, so the agent sees plugin-specific guidance the moment it looks at that category.

Keep it terse. One screenful per category. Concrete examples beat prose - the agent does not need a tutorial.

```markdown
# Voxel Plugin - PCG actions

`voxel_scatter_meshes` builds a PCG graph wired to a Voxel Sampler.
Use it when the user wants meshes scattered on a voxel terrain rather
than the standard landscape.

Typical sequence:
1. `pcg(action="create_graph", ...)` if no graph exists
2. `pcg(action="voxel_scatter_meshes", graphPath=..., mesh=...)`
3. `pcg(action="execute", ...)` to materialise the result
```

## Validation rules (enforced at install and at load)

- `actionPrefix` is mandatory and must be a lowercase identifier.
- Every `inject:` target must be a real registered category. A nonexistent target fails at install with the valid-target list.
- A plugin action may never overwrite a built-in. Collisions are hard-skipped with a warning.
- Inter-plugin collisions resolve by `plugins:` order in `ue-mcp.yml` - first wins.
- `minServerVersion` is checked at install and re-checked at load.
- A plugin that fails any of these is skipped entirely (never partially injected) with a loud warning.

## Publishing

```bash
npm run build      # tsc → dist/
npm publish        # public registry
```

Once published, users install with:

```bash
ue-mcp plugin install ue-mcp-plugin-<your-name>
```

The CLI runs `npm install --save`, validates your manifest, adds an entry under `plugins:` in `ue-mcp.yml`, and prints the restart instruction. Injected actions appear on the next server start.

## Quick start

```bash
ue-mcp plugin create ue-mcp-plugin-my-thing
cd ue-mcp-plugin-my-thing
npm install
npm run build
```

That stamps the standard layout and gets you to a publishable package as quickly as possible.
