# Authoring a plugin

Everything you need to build and publish a plugin. If you only want to *use* a plugin somebody else wrote, see [Using plugins](plugins-using.md) instead.

Two capabilities have their own pages: [Native C++ modules](plugins-native-modules.md) (shipping engine APIs the bridge doesn't expose) and [Guards](plugins-guards.md) (gating every bridge call).

## Quick scaffolder

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

The native C++ module is the one shape that is scaffolded but not active: declaring `nativeModule:` makes `ue-mcp plugin install` deploy the module and force a UE rebuild, so the manifest block ships commented out. The C++ source still lands on disk - uncomment the block in `ue-mcp.plugin.yml` and reinstall to activate it. No separate flag, no compile cost until you opt in. See [Native C++ modules](plugins-native-modules.md).

The scaffold also stamps `package.json`, `tsconfig.json`, a `src/index.ts` entry, `knowledge/`, a `scripts/check.mjs` validator, `LICENSE`, and `.gitignore`.

## Package layout

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
  skills/
    my-workflow/
      SKILL.md               # Claude Code skill, copied into the project on install
  LICENSE
  README.md
```

Conventions:

- One task class per file, default export, extending `UeMcpTask` from `ue-mcp/task`.
- `class_path` in the declaration is resolved against the plugin's `dist/` (the loader tries `dist/<path>.js` then `dist/tasks/<path>.js`).
- `src/shared/` holds helpers; never reference it from the declaration.
- Compile to `dist/` with `tsc` so users need no TypeScript toolchain.

## `package.json`

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

## `ue-mcp.plugin.yml`

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

`type` is optional. Omit it for a param that genuinely has no single type, and the param accepts any JSON value:

```yaml
        value: { description: "Value to write (number, bool, string, or enum name)" }
```

Give a list instead when you know the alternatives, and the param compiles to a union of exactly those:

```yaml
        value: { type: [number, boolean, string] }
```

## Providing new categories (`provides:`)

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

A plugin can mix `inject:` and `provides:` freely - whatever fits each action best. To back actions with C++ instead of TypeScript tasks, see [Native C++ modules](plugins-native-modules.md).

## Writing tasks

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

A task can also be a [guard](plugins-guards.md) that gates every bridge call, not just an action's implementation.

## Knowledge files

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

## Skills

A plugin can ship Claude Code skills: one directory per skill under `skills/`, each holding a `SKILL.md`. No manifest key is needed. Add `skills` to `files` in `package.json` so it ships.

```markdown
---
name: my-workflow
description: Use when importing a character with MyPlugin. Claude reads this line to decide whether to load the skill.
---

# Importing a character

1. `myplugin(action="import", path="...")`
2. `animation(action="list_skeletal_meshes")` to confirm the skeleton
```

`ue-mcp plugin install` copies every skill directory into the project's `.claude/skills/`, and `ue-mcp plugin uninstall` removes them. `ue-mcp plugin update`, `ue-mcp init` and `ue-mcp deploy` refresh them and delete any your new version stopped shipping. Which package installed each skill is recorded in `.ue-mcp/skills.json`.

A skill name is owned by the first package that installed it. A second package shipping the same name is refused with a warning, and so is a name the user already has in `.claude/skills/`. Prefix skill names with your plugin's name.

Check your skills before publishing:

```bash
ue-mcp plugin check-skills .
```

It fails on a `SKILL.md` without a `description:`, a `name:` that differs from its directory, or a `category(action="x")` call that does not exist, counting the actions your plugin adds. A call into another plugin's category cannot be checked and is listed as unverified. The scaffold's `npm run check` runs it.

## Publishing

```bash
npm run build      # tsc -> dist/
npm publish        # public registry
```

Users install with:

```bash
ue-mcp plugin install <your-package-name>
```

Before publishing, confirm your plugin passes every [validation rule](plugins-troubleshooting.md#validation-rules).
