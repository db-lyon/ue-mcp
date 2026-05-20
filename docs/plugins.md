# Plugins

ue-mcp's plugin system lets npm packages add new actions to ue-mcp's built-in categories. A plugin that contributes voxel-terrain helpers injects them into `pcg` and `landscape`, so the agent already working in those categories discovers the new actions exactly where they're relevant — there is no separate tool to open.

This page covers both sides: installing and managing plugins (consumer), and writing and publishing one (author). The author section starts at [Authoring a plugin](#authoring-a-plugin) — if you're just trying to use a plugin somebody else wrote, you can stop after [Using plugins](#using-plugins).

!!! info "Live reference"
    [`ue-mcp-plugin-voxel-plugin`](https://github.com/db-lyon/ue-mcp-plugin-voxel-plugin) ([npm](https://www.npmjs.com/package/ue-mcp-plugin-voxel-plugin)) is the canonical reference. It ships three injected actions and two flows over the [Voxel Plugin](https://voxelplugin.com), and every example below mirrors its real source.

## Quick start

In your Unreal project directory:

```bash
ue-mcp plugin install ue-mcp-plugin-voxel-plugin
```

That runs `npm install --save`, validates the plugin's manifest, and adds an entry to your `ue-mcp.yml`. Restart your MCP client (in Claude Code, `/mcp` reconnects); the next time the server boots it will load the plugin, inject its actions into the host categories, and merge its knowledge into the AI-facing docs.

Verify with the introspection tool:

```text
plugins(action="list")
```

```json
{
  "pluginCount": 1,
  "active": 1,
  "plugins": [
    {
      "name": "ue-mcp-plugin-voxel-plugin",
      "version": "0.1.0",
      "actionPrefix": "voxel",
      "status": "active",
      "categories": ["pcg", "landscape"],
      "injectedActions": 3,
      "flows": 2,
      "uePluginDependency": "Voxel",
      "uePluginPresent": true
    }
  ]
}
```

Once `status: "active"` and `uePluginPresent: true`, the injected actions (e.g. `pcg(action="voxel_scatter_meshes", ...)`) are callable end-to-end.

## How plugins work

A plugin is a normal npm package that ships:

- A `ue-mcp.plugin.yml` manifest declaring an `actionPrefix`, the actions it injects into which host categories, and the task classes that back them.
- Compiled task classes (one per injected action) under `dist/`, each extending `BaseTask` from [`@db-lyon/flowkit`](https://github.com/db-lyon/flowkit).
- Optional `knowledge/<category>.md` markdown that the server attaches to the host category's AI-facing docs at boot.
- Optional `flows:` entries that compose injected actions with built-ins.

At server start, ue-mcp:

1. Reads `plugins:` from your project's `ue-mcp.yml`.
2. Resolves each entry against `<project>/node_modules/`.
3. Loads and validates each plugin's `ue-mcp.plugin.yml`.
4. Imports its task classes and registers them with the flow runtime.
5. Merges the injected actions into the host category tools — the action shows up as `<category>(action="<prefix>_<bare>", ...)`.
6. Concatenates the plugin's knowledge files into the host categories' AI-facing docs.

The injection happens before any tool is registered with the MCP client, so by the time the agent sees the `pcg` tool's action list, the plugin's actions are already there alongside the built-ins.

### Why injection, not a standalone tool

A standalone "voxel" tool would be opaque to the agent — it has no reason to open a category called `voxel` while working on terrain. If the action lives inside `pcg` as `pcg(action="voxel_scatter_meshes")`, the agent already working in PCG discovers it exactly when relevant. Capability appears at the point of need, not behind a door the agent has to know to open.

## Using plugins

### Installing

The supported install path is:

```bash
ue-mcp plugin install <package-name>
```

It's a thin wrapper that:

1. Runs `npm install --save <package-name>` so the package lands in `node_modules/` and is recorded in your `package.json`.
2. Validates the plugin's `ue-mcp.plugin.yml` — checks that `actionPrefix` is a legal identifier, every `inject:` target is a real registered category, every `class_path` resolves, and `minServerVersion` is satisfied.
3. Appends a `- name: <package-name>` entry to your `ue-mcp.yml`'s `plugins:` array (creating the array if needed).
4. Prints the restart instruction.

You can also install manually — `npm install --save <package-name>` and edit `ue-mcp.yml` yourself. The end state is identical.

### The `plugins:` array

The consumer surface is a single block in `ue-mcp.yml`:

```yaml
plugins:
  - name: ue-mcp-plugin-voxel-plugin
  - name: ue-mcp-plugin-some-other-thing
    version: "^0.2.0"     # optional; npm semver range against package.json
```

Each entry resolves against the project's `node_modules/`. If `version` is omitted, whatever is currently installed loads. Order matters — see [Ordering and collisions](#ordering-and-collisions).

### Introspection

Two read-only actions on the `plugins` category:

| Action | What it returns |
|--------|-----------------|
| `plugins(action="list")` | Every plugin: name, version, prefix, status, count of injected actions and flows, host UE plugin dependency check. |
| `plugins(action="describe", name="<package>")` | Full detail for one plugin: the same fields as `list`, plus the actual injected action names, knowledge file paths, flows, and the resolved package + manifest paths on disk. |

Both reflect the live state of the server, so they're the right tool when something looks wrong — see [Troubleshooting](#troubleshooting).

### Host UE plugin dependencies

A plugin can declare a single Unreal-side dependency in its manifest:

```yaml
uePluginDependency: Voxel
```

This is the **`.uplugin` filename** — the same string that appears as `Plugins[].Name` in your `.uproject`. ue-mcp checks for it at server start and reports the result as `uePluginPresent` in `plugins(action="list")`.

The check is informational, not gating: the npm-side plugin loads regardless, and its injected actions appear in the host category tools. But until the UE plugin is enabled in `.uproject` and its C++ modules are built, the actions will fail at execute time with a clear error.

To enable a host UE plugin:

1. Add `{ "Name": "<DepName>", "Enabled": true }` to your `.uproject`'s `Plugins` array.
2. Build the project (e.g. `npm run build` from a Vale-style project, or `editor(action="build_all")`).
3. Restart the editor.
4. Run `plugins(action="list")` to confirm `uePluginPresent: true`.

For source-distributed UE plugins (like Voxel Plugin), drop the source under `Plugins/<DepName>/` — either as a git submodule (recommended for size) or as a vendored copy. The `.uplugin` file inside that directory is what UE's plugin discovery walks.

### Ordering and collisions

- **Plugin vs built-in:** A plugin action can never override a built-in. Collisions are hard-skipped at load time with a warning in the server log; the built-in stays.
- **Plugin vs plugin:** First entry in `plugins:` wins. If two plugins both inject `pcg.foo_bar`, only the earlier-listed one's version is registered. The order is intentionally stable — your `ue-mcp.yml` is the source of truth for resolution.
- **Failed plugins are skipped, not partially loaded.** If a plugin fails validation (bad manifest, missing class_path, server-version mismatch, etc.), it is dropped entirely with a loud warning. Other plugins keep loading. The host tools are never partially mutated.

### Removing a plugin

There is no separate uninstall command — `npm uninstall <package-name>` and delete the entry from `ue-mcp.yml`. On next restart, the actions are gone.

## Available plugins

The reference plugin is `ue-mcp-plugin-voxel-plugin` ([source](https://github.com/db-lyon/ue-mcp-plugin-voxel-plugin), [npm](https://www.npmjs.com/package/ue-mcp-plugin-voxel-plugin)). Search npm for [`ue-mcp-plugin`](https://www.npmjs.com/search?q=keywords%3Aue-mcp-plugin) (the convention keyword) to discover others as the ecosystem grows.

## Authoring a plugin

### Quick scaffolder

```bash
ue-mcp plugin create ue-mcp-plugin-my-thing
cd ue-mcp-plugin-my-thing
npm install
npm run build
```

That stamps a working package with `ue-mcp.plugin.yml`, `tsconfig.json`, an example task in `src/tasks/`, and CI scaffolding. From there, replace the example with your own actions and publish.

### Package layout

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
- `class_path` in the declaration is resolved against the plugin's `dist/` (the loader tries `dist/<path>.js` then `dist/tasks/<path>.js`).
- `src/shared/` holds helpers; never reference it from the declaration.
- Compile to `dist/` with `tsc` so users need no TypeScript toolchain.
- The npm package name should start with `ue-mcp-plugin-` so it's discoverable on the registry.

### `package.json`

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

The `ue-mcp-plugin` keyword is the registry signal. The peer-dep on `@db-lyon/flowkit` is what gives `BaseTask` its shape — your tasks must extend the same class the server uses, so a peer dep (not a regular dep) is what keeps the two copies in sync.

### `ue-mcp.plugin.yml`

This is the only file ue-mcp reads from your package. Authored once; never edited by users.

```yaml
actionPrefix: voxel              # mandatory, lowercase, must match /^[a-z][a-z0-9_]*$/
minServerVersion: 1.0.15         # optional - the server enforces this at install and load
uePluginDependency: Voxel        # optional - .uplugin filename to check in .uproject

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
      description: "Bake a voxel-terrain region to a Landscape heightmap. Params: bounds, resolution?"
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
    description: "Scatter, then bake — a full voxel→landscape pass"
    rollback_on_failure: true
    steps:
      1: { task: voxel.scatter_meshes }
      2: { task: voxel.bake_heightmap }
```

The key under each category is the **bare** action name. The loader prepends your `actionPrefix` to compute the injected name: `voxel` + `scatter_meshes` → `voxel_scatter_meshes`. The user always sees the prefixed form.

Param schemas under `schema:` accept these types: `string`, `number`, `boolean`, `object`, `array`. Non-required params become optional at the top level of the host category tool's schema.

### Writing tasks

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

- Compose existing actions through `this.call('<category>.<action>', params)`. Don't reach into the bridge directly unless you have to — composition gives you free observability and rollback hooks.
- If your task makes multi-step mutations, return a `rollback` record so users can opt into `rollback_on_failure: true` on the wrapping flow.
- Throw, don't return success-with-error-data. The runtime catches throws and turns them into structured failures.

### Knowledge files

For each category your plugin injects into, ship a short markdown file under `knowledge/`. The server attaches it to that category's AI-facing docs at boot, so the agent sees plugin-specific guidance the moment it looks at that category.

Keep it terse — one screenful per category. Concrete examples beat prose. The agent already knows how the category works; the knowledge file is just the delta the plugin introduces.

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

### Publishing

```bash
npm run build      # tsc → dist/
npm publish        # public registry
```

Tag your package with the `ue-mcp-plugin` keyword in `package.json` so it shows up in npm searches for the convention. Users install with:

```bash
ue-mcp plugin install ue-mcp-plugin-<your-name>
```

## Validation rules

These are enforced both at install (`ue-mcp plugin install`) and at server load:

- `actionPrefix` is mandatory and must match `/^[a-z][a-z0-9_]*$/`.
- Every `inject:` target must be a real registered category. A nonexistent target fails install with the list of valid categories.
- A plugin action may never overwrite a built-in. Collisions are hard-skipped with a warning.
- Inter-plugin collisions resolve by `plugins:` order — first wins.
- Every `inject:` entry must point to a task declared under `tasks:`, and every task's `class_path` must resolve under `dist/`.
- `minServerVersion` is checked at install and re-checked at load.
- A plugin that fails any of these is skipped entirely (never partially injected) with a loud warning. Other plugins keep loading.

## Troubleshooting

### `plugins(action="list")` returns `pluginCount: 0`

The server didn't find any `plugins:` entries, or every entry failed validation. Check:

1. `ue-mcp.yml` exists in your project root next to the `.uproject` and has a top-level `plugins:` array.
2. Each `name:` is installed under `node_modules/`. Run `npm install` if the lockfile says it should be there.
3. The server's stderr log — every validation failure prints a `[ue-mcp] warn plugin: <package>: <reason>` line at boot.

### `uePluginPresent: false`

The npm-side plugin loaded fine, but the host Unreal plugin it declares as a dependency is missing from your `.uproject`. See [Host UE plugin dependencies](#host-ue-plugin-dependencies) for the enable steps. The injected actions are still visible in the host category tools — they just won't run end-to-end until the UE plugin is enabled and built.

### `class_path '<path>' could not be resolved`

The plugin's `ue-mcp.plugin.yml` declared a task whose compiled JS file is missing from `dist/`. If you're authoring: run `npm run build` and confirm `dist/<path>.js` exists. If you're consuming: the package was published without its `dist/` directory — open an issue on the plugin's repo.

### `requires server >= <version>`

The plugin's `minServerVersion` is newer than the ue-mcp you're running. Update:

```bash
npm install ue-mcp@latest
```

Then restart your MCP client.

### Injected action appears in `plugins.describe` but not in the host category tool's action list

You restarted the editor but not the MCP server. They're separate processes — the editor restart doesn't respawn the npx-launched ue-mcp server. Reconnect MCP in your client (in Claude Code, `/mcp`).
