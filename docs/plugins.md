# Plugins

ue-mcp's plugin system lets npm packages extend the server in three ways:

- **Inject** new actions into ue-mcp's built-in categories so agents discover them where they're already working.
- **Provide** entirely new top-level categories that the plugin owns end-to-end.
- **Ship native C++** that registers handlers directly with the editor bridge, opening up engine APIs that have no built-in coverage.

Most plugins use only the first shape; the other two are available when injection is the wrong fit. This page covers both sides: installing and managing plugins (consumer), and writing and publishing one (author). The author section starts at [Authoring a plugin](#authoring-a-plugin) - if you're just trying to use a plugin somebody else wrote, you can stop after [Using plugins](#using-plugins).

!!! info "Live reference"
    [`pie-studio`](https://github.com/db-lyon/pie-studio) ([npm](https://www.npmjs.com/package/pie-studio)) is the canonical native-module reference. It ships C++ handlers for PIE recording, replay, observation, and input injection, surfaced as a `pie` category it provisions via `nativeModule.category`. See [Shipping native C++](#shipping-native-c).

## Quick start

In your Unreal project directory:

```bash
ue-mcp plugin install pie-studio
```

That runs `npm install --save`, validates the plugin's manifest, deploys the native C++ module, and adds an entry to your `ue-mcp.yml`. Rebuild the UE project so the native module compiles in, then restart your MCP client (in Claude Code, `/mcp` reconnects).

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
      "name": "pie-studio",
      "version": "0.0.2",
      "actionPrefix": "",
      "status": "active",
      "categories": ["gameplay"],
      "injectedActions": 33,
      "flows": 0,
      "nativeModule": "PIE_Studio"
    }
  ]
}
```

Once `status: "active"`, the injected actions (e.g. `gameplay(action="pie_record_arm", ...)`) are callable end-to-end.

## How plugins work

A plugin is a normal npm package that ships:

- A `ue-mcp.plugin.yml` manifest declaring an `actionPrefix`, the actions it injects into which host categories, and the task classes that back them.
- Compiled task classes (one per injected action) under `dist/`, each extending `UeMcpTask` from [`ue-mcp/task`](#writing-tasks).
- Optional `knowledge/<category>.md` markdown that the server attaches to the host category's AI-facing docs at boot.
- Optional `flows:` entries that compose injected actions with built-ins.

At server start, ue-mcp:

1. Reads `plugins:` from your project's `ue-mcp.yml`.
2. Resolves each entry against `<project>/node_modules/`.
3. Loads and validates each plugin's `ue-mcp.plugin.yml`.
4. Imports its task classes and registers them with the flow runtime.
5. Merges the injected actions into the host category tools - the action shows up as `<category>(action="<prefix>_<bare>", ...)`.
6. Concatenates the plugin's knowledge files into the host categories' AI-facing docs.

The injection happens before any tool is registered with the MCP client, so by the time the agent sees the `gameplay` tool's action list, the plugin's actions are already there alongside the built-ins.

### Three shapes a plugin can take

| Shape | Manifest blocks | When to reach for it |
|-------|-----------------|----------------------|
| **A. Inject only** | `inject:` | The action belongs inside an existing category. Default choice. |
| **B. Provide a new category** | `provides:` (with or without `inject:`) | The plugin opens a whole new domain - audio middleware, build pipelines, networking layers - that doesn't fit inside any built-in category. |
| **C. Ship native C++** | `nativeModule:` (with `category:` to surface its handlers) | The plugin needs engine APIs ue-mcp's built-in handlers don't expose. The plugin ships a UE C++ module that registers handlers on the editor bridge; `nativeModule.category` surfaces them as actions with no TypeScript. |

Shape A is overwhelmingly the right answer. An action that belongs inside an existing category is best discovered where agents are already working.

Shape B is for genuinely new domains. If your plugin's actions don't fit anywhere in the built-in category list, owning a new top-level category is cleaner than forcing a misfit injection.

Shape C is for capability that can't be expressed through orchestration of existing actions. The plugin ships C++ source that compiles into the user's project alongside the bridge, and registers handlers via `UEMCP::RegisterExternalHandler` from its `StartupModule`. Native handlers participate in the same dispatch path as built-in ones. `pie-studio` is a Shape C plugin.

## Using plugins

### Installing

The supported install path is:

```bash
ue-mcp plugin install <package-name>
```

It's a thin wrapper that:

1. Runs `npm install --save <package-name>` so the package lands in `node_modules/` and is recorded in your `package.json`.
2. Validates the plugin's `ue-mcp.plugin.yml` - checks that `actionPrefix` is a legal identifier, every `inject:` target is a real registered category, every `class_path` resolves, and `minServerVersion` is satisfied.
3. Appends a `- name: <package-name>` entry to your `ue-mcp.yml`'s `plugins:` array (creating the array if needed).
4. Prints the restart instruction.

You can also install manually - `npm install --save <package-name>` and edit `ue-mcp.yml` yourself. The end state is identical.

### The `plugins:` array

The consumer surface is a single block in `ue-mcp.yml`:

```yaml
plugins:
  - name: pie-studio
  - name: some-other-plugin
    version: "^0.2.0"     # optional; npm semver range against package.json
```

Each entry resolves against the project's `node_modules/`. If `version` is omitted, whatever is currently installed loads. Order matters - see [Ordering and collisions](#ordering-and-collisions).

### Introspection

Two read-only actions on the `plugins` category:

| Action | What it returns |
|--------|-----------------|
| `plugins(action="list")` | Every plugin: name, version, prefix, status, count of injected actions and flows, host UE plugin dependency check. |
| `plugins(action="describe", name="<package>")` | Full detail for one plugin: the same fields as `list`, plus the actual injected action names, knowledge file paths, flows, and the resolved package + manifest paths on disk. |

Both reflect the live state of the server, so they're the right tool when something looks wrong - see [Troubleshooting](#troubleshooting).

### Host UE plugin dependencies

A plugin can declare a single Unreal-side dependency in its manifest:

```yaml
uePluginDependency: SomePlugin
```

This is the **`.uplugin` filename** - the same string that appears as `Plugins[].Name` in your `.uproject`. ue-mcp checks for it at server start and reports the result as `uePluginPresent` in `plugins(action="list")`.

The check is informational, not gating: the npm-side plugin loads regardless, and its injected actions appear in the host category tools. But until the UE plugin is enabled in `.uproject` and its C++ modules are built, the actions will fail at execute time with a clear error.

To enable a host UE plugin:

1. Add `{ "Name": "<DepName>", "Enabled": true }` to your `.uproject`'s `Plugins` array.
2. Build the project (e.g. `npm run build` or `editor(action="build_all")`).
3. Restart the editor.
4. Run `plugins(action="list")` to confirm `uePluginPresent: true`.

For source-distributed UE plugins, drop the source under `Plugins/<DepName>/` - either as a git submodule (recommended for size) or as a vendored copy. The `.uplugin` file inside that directory is what UE's plugin discovery walks.

### Ordering and collisions

- **Plugin vs built-in:** A plugin action can never override a built-in. Collisions are hard-skipped at load time with a warning in the server log; the built-in stays.
- **Plugin vs plugin:** First entry in `plugins:` wins. If two plugins both inject `gameplay.foo_bar`, only the earlier-listed one's version is registered. The order is intentionally stable - your `ue-mcp.yml` is the source of truth for resolution.
- **Failed plugins are skipped, not partially loaded.** If a plugin fails validation (bad manifest, missing class_path, server-version mismatch, etc.), it is dropped entirely with a loud warning. Other plugins keep loading. The host tools are never partially mutated.

### Removing a plugin

There is no separate uninstall command - `npm uninstall <package-name>` and delete the entry from `ue-mcp.yml`. On next restart, the actions are gone.

## Official plugins

| Plugin | What it does |
|--------|-------------|
| [`pie-studio`](https://github.com/db-lyon/pie-studio) | PIE recording, replay, observation, and input injection. 33 native C++ handlers injected into `gameplay`. |

## Authoring a plugin

### Quick scaffolder

```bash
ue-mcp plugin create my-thing
cd my-thing
npm install
npm run build
npm run check     # validate the manifest + task wiring
```

The scaffold is a **superset**: it ships every way a plugin can extend ue-mcp, wired and working, so you never have to discover a capability before using it. Keep the shapes you want and delete the rest.

| Shape | What it scaffolds |
|-------|-------------------|
| `inject` | An action added onto a built-in category, e.g. `project(action="<prefix>_hello")`. |
| `provides` | A brand-new top-level category the plugin owns (actions unprefixed), e.g. `<prefix>(action="greet")`. |
| `flows` | A chained, one-call orchestration (`<prefix>_demo`). |
| `nativeModule` | A compile-ready C++ handler skeleton under `ue/Plugins/<UePlugin>/`, **dormant** by default. |

The native C++ module is the one shape that is scaffolded but not active: declaring `nativeModule:` makes `ue-mcp plugin install` deploy the module and force a UE rebuild, so the manifest block ships commented out. The C++ source still lands on disk - uncomment the block in `ue-mcp.plugin.yml` and reinstall to activate it. No separate flag, no compile cost until you opt in.

The scaffold also stamps `package.json`, `tsconfig.json`, a `src/index.ts` entry, `knowledge/`, a `scripts/check.mjs` validator, `LICENSE`, and `.gitignore`.

### Package layout

```
my-plugin/
  package.json
  tsconfig.json
  ue-mcp.plugin.yml          # author declaration: actionPrefix, inject, provides, tasks, flows, nativeModule
  src/                       # author writes TypeScript here
    index.ts                 # package entry (tasks load by class_path, but `main` points here)
    tasks/
      MyAction.ts            # one UeMcpTask subclass per file, default export
    shared/                  # optional cross-task helpers (never referenced from the declaration)
  dist/                      # tsc output - what actually ships and loads
    tasks/
      MyAction.js
  ue/                        # dormant native C++ module (only deployed when nativeModule: is uncommented)
    Plugins/
      MyPlugin/
  scripts/
    check.mjs                # pre-publish validator: manifest parses, task refs resolve
  knowledge/
    gameplay.md              # one markdown file per target category
  LICENSE
  README.md
```

Conventions:

- One task class per file, default export, extending `UeMcpTask` from `ue-mcp/task`.
- `class_path` in the declaration is resolved against the plugin's `dist/` (the loader tries `dist/<path>.js` then `dist/tasks/<path>.js`).
- `src/shared/` holds helpers; never reference it from the declaration.
- Compile to `dist/` with `tsc` so users need no TypeScript toolchain.

### `package.json`

```json
{
  "name": "my-plugin",
  "version": "0.1.0",
  "description": "My custom actions for ue-mcp",
  "type": "module",
  "main": "dist/index.js",
  "files": ["dist", "ue-mcp.plugin.yml", "knowledge", "README.md"],
  "keywords": ["unreal-engine"],
  "peerDependencies": {
    "ue-mcp": ">=1.0.65"
  },
  "devDependencies": {
    "ue-mcp": "^1.0.65",
    "typescript": "^5.7.0"
  },
  "scripts": {
    "build": "tsc"
  }
}
```

`UeMcpTask` (and the types you import alongside it) come from `ue-mcp/task` - a thin, server-free entry point that the consumer's installed `ue-mcp` already provides. That's why `ue-mcp` is a **peer** dependency: the running server supplies the copy at load time, so your task extends the same base class the server uses. The matching **dev** dependency is only there to type-check your build. You never depend on the underlying flow runtime directly - it stays an implementation detail behind `ue-mcp/task`.

### `ue-mcp.plugin.yml`

This is the only file ue-mcp reads from your package. Authored once; never edited by users.

```yaml
actionPrefix: mypfx             # mandatory, lowercase, must match /^[a-z][a-z0-9_]*$/
minServerVersion: 1.0.15        # optional - the server enforces this at install and load
uePluginDependency: SomePlugin  # optional - .uplugin filename to check in .uproject

inject:
  gameplay:
    inspect_something:           # -> gameplay(action="mypfx_inspect_something")
      task: mypfx.inspect_something
      description: "Inspect some game state during a PIE session."
      schema:
        actorLabel:            { type: string, required: true }
        includeComponents:     { type: boolean }

tasks:
  mypfx.inspect_something:
    class_path: tasks/InspectSomething
    description: "Inspect game state for a given actor"
```

The key under each category is the **bare** action name. The loader prepends your `actionPrefix` to compute the injected name: `mypfx` + `inspect_something` -> `mypfx_inspect_something`. The user always sees the prefixed form.

`knowledge:` and `flows:` are optional - omit them when you have nothing to attach. A plugin can ship a single action and nothing else.

Param schemas under `schema:` accept these types: `string`, `number`, `boolean`, `object`, `array`. Non-required params become optional at the top level of the host category tool's schema.

### Providing new categories (`provides:`)

When the plugin's actions don't belong inside any built-in category, declare a `provides:` block. Each entry registers a brand-new top-level MCP category that the plugin owns. Action names are NOT prefixed inside provided categories - the category itself is the namespace.

```yaml
actionPrefix: terrain            # still required (used for any inject: entries)

provides:
  terrain_sculpt:                # -> terrain_sculpt(action="sample_density", ...)
    description: "Terrain sculpting operations"
    actions:
      sample_density:
        task: terrain_sculpt.sample_density
        description: "Sample density values along a curve through the terrain"
        schema:
          start: { type: array, required: true }
          end:   { type: array, required: true }
          steps: { type: number }

tasks:
  terrain_sculpt.sample_density:
    class_path: tasks/SampleDensity
```

Rules:

- Provided category names must match `/^[a-z][a-z0-9_]*$/`.
- A provided name may not collide with a built-in category. The CLI fails install with the offending name; the runtime loader skips the plugin with a clear status reason.
- Inter-plugin collisions resolve first-writer-wins. If two installed plugins both `provides: terrain_sculpt`, the one earlier in your `plugins:` array claims the name; the other is skipped with a warning visible in `plugins(list)`.
- Knowledge files keyed by a provided category name (`knowledge/terrain_sculpt.md`) attach to that category's AI-facing docs the same way they do for injected categories.

A plugin can mix `inject:` and `provides:` freely - whatever fits each action best.

### Shipping native C++ (`nativeModule:`)

When the plugin needs engine APIs ue-mcp's bridge doesn't already expose, ship a UE C++ module alongside the npm package. The module compiles into the user's project at install time and registers handlers on the bridge via `UEMCP::RegisterExternalHandler`.

`pie-studio` is a real-world example of this shape. Its manifest:

```yaml
actionPrefix: pie                    # used only when injecting into a built-in

nativeModule:
  uePluginName: PIE_Studio           # name of the .uplugin that gets deployed
  minBridgeApi: 1                    # gate against UEMCP_BRIDGE_API_VERSION
  source: ue/Plugins/PIE_Studio      # path inside your npm tarball
  category: pie                      # surface handlers under a pie(...) tool
  categoryDescription: "PIE record, replay, observe, and input injection"
  handlers:
    record_arm:   { description: "Arm the PIE input recorder" }
    replay_arm:   { description: "Arm the PIE input replayer" }
    inject_input:
      description: "Single-frame Enhanced Input inject"
      timeoutSeconds: 5
      schema:
        action_path: { type: string, description: "InputAction asset path (required)" }
        value_x:     { type: number }
    # ... more handlers
```

#### How handlers become MCP actions

Set `category` and ue-mcp surfaces every handler as an MCP action that dispatches to the bare bridge method your C++ registered (`record_arm` above). No TypeScript task class is involved - the C++ handler *is* the implementation. The category value picks one of two shapes:

- **A new (non-built-in) category** - as in the `pie` example above - is **provisioned as its own top-level tool** the plugin owns. Actions are **not** prefixed (the category is the namespace): `pie(action="record_arm")`. Set `categoryDescription` for the tool's summary. This is the right choice when the handlers form their own domain. Cross-plugin name collisions resolve first-wins, like `provides:`.
- **A built-in category** (e.g. `gameplay`) **injects** the handlers into that existing tool, prefixed with `actionPrefix`: handler `record_arm` becomes `gameplay(action="pie_record_arm")`. Choose this when the handlers belong inside a category that already exists.

Two rules that bite if missed:

- **Declare params under each handler's `schema:`.** The MCP SDK strips any param not in the action's schema before it reaches the bridge, so an undeclared param silently never arrives. Same field types as `inject:` schemas. Params-free handlers (status polls, list calls) need no schema. Leave params **optional** (ue-mcp forces them optional regardless): one flat schema backs every action in a category, so a required param would be forced onto unrelated actions - let your C++ handler validate and return a clear error, and note "(required)" in the param description.
- **`timeoutSeconds`** sets the bridge-call timeout for that action (default 30s). Raise it for long-running handlers.

Omit `category` entirely and handlers are still registered on the bridge but exposed as no MCP action - useful only if another task calls them internally. For an agent-facing plugin you almost always want `category`.

#### Layout inside the npm tarball

```
pie-studio/
  ue-mcp.plugin.yml
  dist/                              # tsc output (TypeScript tasks, if any)
  ue/                                # native source ships here
    Plugins/
      PIE_Studio/
        PIE_Studio.uplugin
        Source/
          PIE_Studio/
            PIE_Studio.Build.cs
            Private/
              Handlers/              # handler .cpp files
              PIE/                   # engine subsystem wrappers
              UI/                    # editor UI panels
```

Update `package.json` `files:` so the `ue/` directory ships with the published tarball:

```json
"files": ["dist", "ue", "ue-mcp.plugin.yml", "knowledge", "README.md"]
```

#### The native module

Add `UE_MCP_Bridge` to `PrivateDependencyModuleNames` in your `.Build.cs`:

```csharp
public class PIE_Studio : ModuleRules
{
    public PIE_Studio(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Json" });
        PrivateDependencyModuleNames.AddRange(new string[] { "UE_MCP_Bridge" });
    }
}
```

Register handlers from `StartupModule`:

```cpp
#include "MCPHandlerRegistration.h"

void FPIE_StudioModule::StartupModule()
{
    UEMCP::RegisterExternalHandler(
        TEXT("inject_input"),
        [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonValue>
        {
            // ... do the work, return a JSON value
            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetBoolField(TEXT("success"), true);
            return MakeShared<FJsonValueObject>(Result);
        });
}

void FPIE_StudioModule::ShutdownModule()
{
    UEMCP::UnregisterExternalHandler(TEXT("inject_input"));
}
```

The handler's method name (`inject_input`) is the bare bridge method. It's what an auto-surfaced action (`gameplay(action="pie_inject_input")`, via `nativeModule.category`) dispatches to, what a TypeScript task can address through `this.call(...)`, and what the bridge looks up on any dispatch. Register it bare - ue-mcp adds the `actionPrefix` when it surfaces the action.

#### Install flow

```bash
ue-mcp plugin install pie-studio
```

The CLI now also:

1. Reads `MCPHandlerRegistration.h` from the deployed bridge and checks that `UEMCP_BRIDGE_API_VERSION >= manifest.nativeModule.minBridgeApi`. Install fails fast if the bridge is too old, with a pointer to `ue-mcp deploy`.
2. Copies `<pkgDir>/<source>` to `<projectDir>/Plugins/<uePluginName>/`.
3. Records every copied file in `<projectDir>/.ue-mcp/native-modules.json` so `ue-mcp plugin uninstall` can clean up without nuking user edits.
4. Prints `REBUILD REQUIRED` - the user must build the UE project before launching the editor so the new module compiles in.

#### Bridge ABI versioning

`UEMCP_BRIDGE_API_VERSION` is the C++ ABI contract every native plugin compiles against. Bumps are reserved for breaking changes to the `FExternalHandlerFn` signature or the registration contract. A plugin declaring `minBridgeApi: N` refuses to load against a bridge whose version is below N. Inspect the deployed bridge's version with:

```text
project(action="get_status")
```

The response includes `bridgeApiVersion` when a bridge is deployed.

### Writing tasks

```ts
// src/tasks/InspectSomething.ts
import { UeMcpTask, type TaskResult } from "ue-mcp/task";

interface Options {
  actorLabel: string;
  includeComponents?: boolean;
}

export default class InspectSomething extends UeMcpTask<Options> {
  get taskName() { return "mypfx.inspect_something"; }

  async execute(): Promise<TaskResult> {
    const { actorLabel, includeComponents = false } = this.options;

    const details = await this.call("level.get_actor_details", {
      actorLabel,
      includeComponents,
    });
    if (!details.success) return details;

    // ... process the result ...
    return { success: true, data: details.data };
  }
}
```

Notes:

- Compose existing actions through `this.call('<category>.<action>', params)`. Don't reach into the bridge directly unless you have to - composition gives you free observability and rollback hooks. When you do need a raw bridge method that no task wraps, `UeMcpTask` gives you a typed `this.bridge.call(method, params)` and a typed `this.ctx` (`bridge`, `project`) with no casting.
- Use the **real** parameter names of the host task you're calling. Param name drift between TS and C++ is how silent failures start.
- If your task makes multi-step mutations, return a `rollback` record so users can opt into `rollback_on_failure: true` on the wrapping flow.
- Throw, don't return success-with-error-data. The runtime catches throws and turns them into structured failures.

### Knowledge files

For each category your plugin injects into, ship a short markdown file under `knowledge/`. The server attaches it to that category's AI-facing docs at boot, so the agent sees plugin-specific guidance the moment it looks at that category.

Keep it terse - one screenful per category. Concrete examples beat prose. The agent already knows how the category works; the knowledge file is just the delta the plugin introduces.

```markdown
# PIE Studio - gameplay actions

33 actions for PIE input recording, replay, observation, and injection.

Quick start:
1. `gameplay(action="pie_record_arm", sample_hz=60)` - arm the recorder
2. Press Play, do your thing, stop PIE
3. `gameplay(action="pie_replay_arm", recording_id="<id>", eject=true, time_scale=0.1)` - replay at 10%
```

### Publishing

```bash
npm run build      # tsc -> dist/
npm publish        # public registry
```

Users install with:

```bash
ue-mcp plugin install <your-package-name>
```

## Validation rules

These are enforced both at install (`ue-mcp plugin install`) and at server load:

- `actionPrefix` is mandatory and must match `/^[a-z][a-z0-9_]*$/`.
- Every `inject:` target must be a real registered category. A nonexistent target fails install with the list of valid categories.
- A plugin action may never overwrite a built-in. Collisions are hard-skipped with a warning.
- Every `provides:` category name must match `/^[a-z][a-z0-9_]*$/` and must not collide with a built-in category.
- Inter-plugin collisions resolve by `plugins:` order - first wins. Applies to both injected actions and provided category names.
- Every `inject:` and `provides:` entry must point to a task declared under `tasks:`, and every task's `class_path` must resolve under `dist/`.
- `minServerVersion` is checked at install and re-checked at load.
- `nativeModule.minBridgeApi` is checked at install (against the deployed bridge's `UEMCP_BRIDGE_API_VERSION`) and re-checked at load.
- A plugin that fails any of these is skipped entirely (never partially injected) with a loud warning. Other plugins keep loading.

## Troubleshooting

### `plugins(action="list")` returns `pluginCount: 0`

The server didn't find any `plugins:` entries, or every entry failed validation. Check:

1. `ue-mcp.yml` exists in your project root next to the `.uproject` and has a top-level `plugins:` array.
2. Each `name:` is installed under `node_modules/`. Run `npm install` if the lockfile says it should be there.
3. The server's stderr log - every validation failure prints a `[ue-mcp] warn plugin: <package>: <reason>` line at boot.

### `uePluginPresent: false`

The npm-side plugin loaded fine, but the host Unreal plugin it declares as a dependency is missing from your `.uproject`. See [Host UE plugin dependencies](#host-ue-plugin-dependencies) for the enable steps. The injected actions are still visible in the host category tools - they just won't run end-to-end until the UE plugin is enabled and built.

### `class_path '<path>' could not be resolved`

The plugin's `ue-mcp.plugin.yml` declared a task whose compiled JS file is missing from `dist/`. If you're authoring: run `npm run build` and confirm `dist/<path>.js` exists. If you're consuming: the package was published without its `dist/` directory - open an issue on the plugin's repo.

### `requires server >= <version>`

The plugin's `minServerVersion` is newer than the ue-mcp you're running. Update:

```bash
npm install ue-mcp@latest
```

Then restart your MCP client.

### Injected action appears in `plugins.describe` but not in the host category tool's action list

You restarted the editor but not the MCP server. They're separate processes - the editor restart doesn't respawn the npx-launched ue-mcp server. Reconnect MCP in your client (in Claude Code, `/mcp`).

### `nativeModule requires bridge ABI >= N`

The plugin needs a newer bridge than the one deployed in this project. Run `ue-mcp deploy` to refresh the bridge source, then `ue-mcp build` (or rebuild from the editor) before retrying. The deployed ABI is also visible in `project(action="get_status")` as `bridgeApiVersion`.

### Provided category does not show up as its own MCP tool

The plugin loaded but a name collision skipped its `provides:` entry. Run `plugins(action="describe", name="<package>")` and check the `provided` field. If it's empty, look at the server boot log for a `provides target '<category>' already claimed by '<other plugin>'` warning - earlier-listed plugins win, so reorder your `plugins:` array or drop one of the conflicting packages.

### Native module deployed but handlers come back `Unknown method`

The C++ side didn't compile in. Two common causes:

1. The user never rebuilt after install. Run `npm run build` from the project (or rebuild from the editor IDE) and confirm the new `.dll` lands under `Binaries/Win64/`.
2. The build failed silently because the deployed bridge is older than the plugin expects. Run `ue-mcp deploy` to refresh `MCPHandlerRegistration.h`, then `ue-mcp build`.

If the rebuild succeeds but `Unknown method` persists, you've hit a stale Live Coding patch: delete `<projectDir>/Binaries/Win64/*.patch_*` and rebuild clean. UBT's incremental build can otherwise shadow a freshly built DLL with a leftover patch.
