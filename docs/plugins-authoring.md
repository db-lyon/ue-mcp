# Authoring a plugin

How to build and publish a plugin. To use one, see [Using plugins](plugins-using.md). [Native C++ modules](plugins-native-modules.md) and [Guards](plugins-guards.md) have their own pages.

## Scaffold

```bash
ue-mcp plugin create my-thing    # [--dir path]
cd my-thing
npm install
npm run build
npm run check     # validate the manifest, task wiring and skills
```

The scaffold shows every way a plugin can extend ue-mcp. Keep what you need and delete the rest.

| Shape | What it scaffolds |
|-------|-------------------|
| `inject` | `project(action="<prefix>_hello")` on a built-in category. |
| `provides` | A category the plugin owns: `<prefix>(action="greet")`. |
| `flows` | A flow, `<prefix>_demo`. |
| `nativeModule` | A C++ handler skeleton under `ue/Plugins/<UePlugin>/` and its recorded contract in `handler-specs.json`. The manifest block is commented out, so nothing is deployed or compiled until you uncomment it and reinstall. |

It also writes `package.json`, `tsconfig.json`, `src/index.ts`, `knowledge/`, `skills/`, `scripts/check.mjs`, `README.md`, `LICENSE` and `.gitignore`.

## Package layout

```
my-plugin/
  package.json
  tsconfig.json
  ue-mcp.plugin.yml          # the manifest
  src/
    index.ts                 # package entry
    tasks/
      MyAction.ts            # one UeMcpTask subclass per file, default export
  dist/                      # tsc output; this is what loads
  knowledge/
    gameplay.md              # declared under knowledge:
  skills/
    import/
      SKILL.md
  ue/Plugins/MyPlugin/       # native source, only with nativeModule:
  scripts/check.mjs
```

`class_path` resolves inside the package, trying `dist/<path>.js`, `dist/tasks/<path>.js`, `<path>.js` and `dist/<path>/index.js`. Ship compiled JavaScript so users need no TypeScript toolchain.

## `package.json`

```json
{
  "name": "ue-mcp-my-plugin",
  "version": "0.1.0",
  "type": "module",
  "main": "dist/index.js",
  "files": ["dist", "ue-mcp.plugin.yml", "knowledge", "skills", "README.md"],
  "devDependencies": {
    "ue-mcp": "^1.4.11",
    "typescript": "^5.7.0"
  },
  "scripts": { "build": "tsc" }
}
```

Import `UeMcpTask` from `ue-mcp/task`. The running server answers every `ue-mcp` and `ue-mcp/*` import a plugin makes with its own copy, so the dev dependency is only for type-checking. Do not declare a `peerDependencies` range on `ue-mcp`: npm rejects prereleases outside a range's exact `major.minor.patch`, so the install fails with ERESOLVE. Use `minServerVersion` in the manifest as the version gate. Do not import `@db-lyon/flowkit` directly.

## `ue-mcp.plugin.yml`

The only file ue-mcp reads from your package.

```yaml
actionPrefix: mypfx             # required, matches /^[a-z][a-z0-9_]*$/
minServerVersion: 1.4.11        # optional, enforced at install and load
uePluginDependency: SomePlugin  # optional, .uplugin name checked in the .uproject

inject:
  level:
    inspect_actor:              # -> level(action="mypfx_inspect_actor")
      task: mypfx.inspect_actor
      description: "Summarize one placed actor."
      effect: read              # read | mutate | unknown
      schema:
        actorLabel:        { type: string, required: true }
        includeProperties: { type: boolean }

tasks:
  mypfx.inspect_actor:
    class_path: tasks/InspectActor
    description: "Summarize one placed actor"

knowledge:
  level: knowledge/level.md
```

| Key | |
|-----|---|
| `actionPrefix` | Prepended to every injected action: `mypfx` + `inspect_actor` is `mypfx_inspect_actor`. |
| `minServerVersion` | Lowest ue-mcp version the plugin runs on. |
| `uePluginDependency` | See [Host UE plugin dependencies](plugins-using.md#host-ue-plugin-dependencies). |
| `inject` | `<built-in category>: { <bare action>: { task, description?, effect?, schema? } }`. |
| `provides` | See [Providing categories](#providing-categories). |
| `tasks` | `<task name>: { class_path, description? }`. Every `inject` and `provides` action names one. |
| `flows` | See [Flows](#flows). |
| `knowledge` | `<category>: <path>`. See [Knowledge files](#knowledge-files). |
| `guards` | See [Guards](plugins-guards.md). |
| `nativeModule` | See [Native C++ modules](plugins-native-modules.md). |
| `registry` | See [The registry listing](#the-registry-listing). |

`effect` says what the action does to the editor. [Guard scopes](plugins-guards.md#scopes) are read from it. When it is omitted, the effect is inferred from the action name.

### Parameter schemas

`schema:` fields take `type`, `required` and `description`. `type` is one of `string`, `number`, `boolean`, `object`, `array`, a list of them (a union), or omitted to accept any JSON value:

```yaml
        value: { type: [number, boolean, string] }
        raw:   { description: "Any JSON value" }
```

`required: true` is reported in the action's signature and `describe_action`, and a call without it is refused before your task runs.

Never name a parameter `action`, `timeoutMs`, `select`, `omit`, `editor` or `toEditor`. The dispatcher consumes those before any handler runs, so the loader drops an action that declares one and reports it under `degraded`. A native C++ handler declares typed parameters in C++ instead; see [Typed contracts](plugins-native-modules.md#typed-contracts).

### Source control provider plugins

The core `source_control` category works with whichever provider the editor has configured, through `ISourceControlProvider`, and never checks a provider's name. Provider-only features (Perforce shelves and streams, Git branches and stashes) belong in a provider plugin that injects into `source_control` under its own prefix:

```yaml
actionPrefix: p4

inject:
  source_control:
    shelve:                      # -> source_control(action="p4_shelve")
      task: p4.shelve
      description: "Shelve the files of a pending changelist."
      effect: mutate
      schema:
        changelist: { type: string, required: true }
```

Leave checkout, add, delete, revert, sync, submit and checkout on write to core. See [Source control](plugins-guards.md#source-control).

## Providing categories

`provides:` registers a top-level category the plugin owns. Its actions are not prefixed:

```yaml
actionPrefix: terrain            # still required

provides:
  terrain_sculpt:                # -> terrain_sculpt(action="sample_density")
    description: "Terrain sculpting operations"
    actions:
      sample_density:
        task: terrain_sculpt.sample_density
        description: "Sample density along a line through the terrain"
        effect: read
        schema:
          start: { type: array, required: true }
          end:   { type: array, required: true }
          steps: { type: number }

tasks:
  terrain_sculpt.sample_density:
    class_path: tasks/SampleDensity
```

The name must match `/^[a-z][a-z0-9_]*$/` and must not be a built-in category. Between plugins, the first in `plugins:` claims a name.

## Writing tasks

```ts
// src/tasks/InspectActor.ts
import { UeMcpTask, type TaskResult } from "ue-mcp/task";

export default class InspectActor extends UeMcpTask<{ actorLabel: string; includeProperties?: boolean }> {
  get taskName() { return "mypfx.inspect_actor"; }

  async execute(): Promise<TaskResult> {
    const details = await this.call("level.get_actor_details", {
      actorLabel: this.options.actorLabel,
      includeProperties: this.options.includeProperties ?? false,
    });
    if (!details.success) return details;
    return { success: true, data: details.data };
  }
}
```

- Compose actions with `this.call("<category>.<action>", params)`, using the action's real parameter names. `this.bridge.call(method, params)` reaches a bridge method no action wraps.
- Throw on failure; the runtime turns a throw into a failed result.
- A multi-step mutation can return a `rollback: { taskName, payload }` record so a flow with `rollback_on_failure` can undo it.

The full base class is in the [UeMcpTask reference](flows.md#uemcptask-reference).

## Flows

A plugin's `flows:` are added to every project that loads it:

```yaml
flows:
  mypfx_inspect_all:
    description: "Inspect the hero and the camera"
    group: inspect               # optional; default is the name prefix, here "mypfx"
    steps:
      1: { task: level.mypfx_inspect_actor, options: { actorLabel: Hero } }
      2: { task: level.mypfx_inspect_actor, options: { actorLabel: Camera } }
```

Steps take `task`, `flow`, `options`, `retries`, `retryDelay` and `retryOn`. A flow with more than one step has `rollback_on_failure: true` unless it says otherwise. Users can turn groups off with [`ue-mcp plugin config`](plugins-using.md#flow-groups).

## Knowledge files

A knowledge file is markdown attached to a category's AI-facing docs at server start. Declare each one under `knowledge:`; a file that is not declared is not read. Keep it to one screen of what the plugin adds:

```markdown
# Sample Recorder - gameplay actions

1. `gameplay(action="rec_record_arm", sample_hz=60)` arms the recorder.
2. Play, then stop PIE.
3. `gameplay(action="rec_replay_arm", recording_id="<id>")` replays it.
```

## Skills

A plugin can ship Claude Code skills: one directory per skill under `skills/`, each with a `SKILL.md`. No manifest key is needed; add `skills` to `files` in `package.json`.

```markdown
---
name: import
description: Use when importing a character with MyPlugin.
---

# Importing a character

1. `myplugin(action="import", path="...")`
2. `animation(action="list_skeletal_meshes")` to confirm the skeleton
```

`ue-mcp plugin install` copies each skill into `.claude/skills/` and `uninstall` removes them. `update`, `ue-mcp init` and `ue-mcp deploy` refresh them and delete skills the new version no longer ships. Ownership is recorded in `.ue-mcp/skills.json`.

A skill installs under the package's prefix: the package name without `ue-mcp-`, with a scope folded in. `skills/import/` in `ue-mcp-meshy` installs as `meshy-import`, and in `@studio/ue-mcp-foo` as `studio-foo-import`. The installer sets `name:` to match. A name that already starts with the prefix is not prefixed again. A skill directory the user created under the same name is never overwritten.

```bash
ue-mcp plugin check-skills .
```

prints the name each skill installs as and fails on a `SKILL.md` without `description:` or with a `category(action="x")` call that does not exist, counting your plugin's own actions. Calls into another plugin's category are listed as unverified. The scaffold's `npm run check` runs it.

## Publishing

```bash
npm run build
npm publish
```

Users then run `ue-mcp plugin install <your-package>`. Check the [validation rules](plugins-troubleshooting.md#validation-rules) first.

### The registry listing

`ue-mcp plugin publish [dir]` creates or updates your listing on [plugins.ue-mcp.com](https://plugins.ue-mcp.com). Log in once with `ue-mcp login`.

- **Slug.** A listing that already names your package keeps its slug. `--slug` sets a new listing's slug; changing an existing one also needs `--rename`.
- **Derived on every publish.** `provides` from the manifest: `tools` for `inject:`, `provides:` or a native module, `flows` for a non-empty `flows:`, `skills` when `skills/` has one. `ueVersions` from `nativeModule.supportedEngineVersions`.
- **Curated text.** Set it in the manifest's `registry:` block, or with `--tagline`, `--description` and `--category`. `--tags-from-keywords` takes the tags from `package.json` `keywords`. A field set neither way keeps the listing's value.

  ```yaml
  registry:
    tagline: Voxel terrain authoring for ue-mcp
    description: Create, stamp and query voxel terrain from the editor.
    category: landscape
    tags: [voxel, terrain]
  ```

- **README.** `--readme snapshot` (default) stores your README.md. `--readme live` stores none, and the listing renders the README from your public repository.
- **Repository.** `--repo <url>` overrides the repository from `package.json`. `--private` or `--public` marks whether that repository is public.
- **`--dry-run`** prints each field that would change, the slug first, and posts nothing.

In CI:

```yaml
- run: npm publish
- run: npx ue-mcp plugin publish .
  env:
    UE_MCP_PUBLISH_TOKEN: ${{ secrets.UE_MCP_PUBLISH_TOKEN }}
```

Mint the token on the registry's account page. `--token <t>` passes one on the command line.
