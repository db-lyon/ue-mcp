# Flows

Flows let you define multi-step workflows in YAML and run them as a single operation. They're powered by [`@db-lyon/flowkit`](https://github.com/db-lyon/flowkit) and are fully customizable — you can chain built-in tasks, run shell commands, override tasks with your own implementations, or compose tasks together.

## Quick Start

Create a `ue-mcp.yml` in your Unreal project root (next to the `.uproject`):

```yaml
ue-mcp:
  version: 1

flows:
  build_and_check:
    description: Build the project and verify the editor is connected
    steps:
      1:
        task: project.build
        options:
          configuration: Development
      2:
        task: project.get_status
```

Run it from the AI:

```
flow(action="run", flowName="build_and_check")
```

That's it. The config is **hot-reloaded on every call** — edit the YAML and run again without restarting the MCP server.

## Concepts

### Tasks

A task is a named unit of work. UE-MCP ships with **<!-- count:actions -->449+<!-- /count --> built-in tasks** across <!-- count:tools -->19<!-- /count --> categories - every action available through the MCP tools is also a flow task.

Tasks are defined in the `tasks:` section of your config:

```yaml
tasks:
  project.build:
    class_path: ue-mcp.bridge
    group: project
    description: "Build C++ project. Params: configuration?, platform?, clean?"
    options:
      method: build_project
```

The fields:

| Field | Required | Description |
|-------|----------|-------------|
| `class_path` | Yes | How the task is resolved — a registered name, a built-in class path, or a path to your own `.js`/`.ts` file |
| `description` | No | Human-readable description |
| `group` | No | Category for organization |
| `options` | No | Default options passed to the task (can be overridden per-step) |

You rarely need to define tasks yourself - the built-in defaults cover all <!-- count:actions -->449+<!-- /count --> actions. You define tasks when you want to **override** or **add** custom ones.

### Flows

A flow is an ordered sequence of steps. Each step runs a task or a nested flow:

```yaml
flows:
  setup_scene:
    description: Create a basic lit scene
    steps:
      1:
        task: level.place_actor
        options:
          className: DirectionalLight
          location: { x: 0, y: 0, z: 500 }
      2:
        task: level.place_actor
        options:
          className: SkyAtmosphere
      3:
        task: level.place_actor
        options:
          className: ExponentialHeightFog
```

Steps execute in numeric order. If a step fails, the flow stops.

### Step Types

A step must have exactly one of `task` or `flow`:

```yaml
steps:
  1:
    task: asset.list                   # Run a task
    options:
      directory: /Game/
  2:
    flow: setup_scene                  # Run another flow (nested)
  3:
    task: shell                        # Run a shell command
    options:
      command: npm run build
  4:
    task: None                         # Skip marker (no-op)
```

### Option Merging

Options are merged in two layers:

1. **Task definition** — default options in the `tasks:` section
2. **Step** — per-step overrides in the `flows:` section

Step options win:

```yaml
tasks:
  asset.list:
    class_path: asset.list
    options:
      recursive: true        # default

flows:
  quick_scan:
    description: List top-level game assets
    steps:
      1:
        task: asset.list
        options:
          directory: /Game/
          recursive: false    # overrides the default
```

## Built-in Tasks

Every MCP action is registered as a task using its `category.action` name. Some examples:

| Task | What it does |
|------|-------------|
| `project.get_status` | Check server mode and editor connection |
| `project.build` | Build the C++ project |
| `asset.list` | List assets in a directory |
| `asset.search` | Search by name, class, or path |
| `blueprint.read` | Read a blueprint's structure |
| `blueprint.compile` | Compile a blueprint |
| `level.place_actor` | Spawn an actor in the level |
| `material.create` | Create a material asset |
| `editor.execute_console` | Run a console command |
| `editor.start_editor` | Launch the Unreal Editor |
| `shell` | Run a shell command |

See the full list in `dist/ue-mcp.default.yml` or run `flow(action="list")`.

### Task Types

Built-in tasks fall into two categories:

- **Bridge tasks** — forwarded to the C++ plugin over WebSocket. Defined with `class_path: ue-mcp.bridge` and a `method` option.
- **Handler tasks** — executed locally in Node.js (filesystem operations like config parsing, asset directory scanning).

The `shell` task is also built in — it runs a command via `child_process`:

```yaml
steps:
  1:
    task: shell
    options:
      command: npm run lint
      cwd: /path/to/project      # optional, defaults to cwd
      timeout: 300000             # optional, defaults to 5 minutes
```

## Runtime Parameters

Hardcoding every option in YAML gets tedious. Pass `params` at call time to override step options for that run:

```
flow(action="run", flowName="beacon", params={
  levelPath: "/Game/MyCustomLevel",
  configuration: "Shipping"
})
```

Runtime `params` merge into every step's options with **highest priority**:

```
taskDef.options  <  step.options  <  runtime params
```

So a step with `options: { levelPath: "/Game/Flows/Beacon" }` in the YAML will use `/Game/MyCustomLevel` if you pass `params: { levelPath: "/Game/MyCustomLevel" }` at runtime.

Params apply to every step — steps that don't use a given key simply ignore it. This makes flows fully parameterizable without templating syntax.

## Step References

When one step needs the output of an earlier step, reference it with `${steps.<id>.<path>}`:

```yaml
flows:
  build_and_open:
    description: Build, then open the packaged artifact
    steps:
      1:
        task: project.build
        options:
          configuration: Development
      2:
        task: asset.list
        options:
          directory: ${steps.1.outputDir}       # whole-value → raw type preserved
      3:
        task: editor.execute_console
        options:
          command: "echo built ${steps.project.build.version}"  # embedded → stringified
```

- **`<id>`** — step number (`1`) or task name (`project.build`). For task names that contain dots, the longest prefix that matches a step wins.
- **`<path>`** — dot path into that step's `result.data`.
- If a task name appears in multiple steps, references resolve to the **most recently completed** one.
- If the whole option value is a single `${...}` reference, the raw value is substituted (objects and arrays round-trip). Embedded references inside a larger string are stringified.
- References that can't be resolved fail the step.

Precedence (highest wins):

```
taskDef.options  <  step.options  <  runtime params
```

References in any of those layers resolve at step-execution time against already-completed steps in the same flow. Nested flows have their own scope — a nested step cannot reference a step in the enclosing flow.

## Flow-level Hooks

A flow can attach steps that run around the main sequence, keyed by outcome:

```yaml
flows:
  deploy:
    description: Build and push the plugin
    on_start:   [ { task: editor.execute_console, options: { command: "echo starting" } } ]
    on_success: [ { task: editor.execute_console, options: { command: "echo done ${steps.build.version}" } } ]
    on_failure: [ { task: editor.execute_console, options: { command: "echo failed: ${error.message}" } } ]
    finally:    [ { task: project.get_status } ]
    steps:
      1: { task: project.build }
      2: { task: asset.save }
```

- **`on_start`** — before the first step. Failure aborts the flow.
- **`on_success`** — after all steps succeed.
- **`on_failure`** — after any step fails. The `${error.message|name|stack|step}` namespace resolves inside this phase.
- **`finally`** — after `on_success` / `on_failure`, regardless of outcome.

Hook steps share the full execution model — same references, same option merging, same runtime params. Hook failures appear in `result.hookErrors` but don't change the primary success/failure outcome.

## Per-step Retry

A step can retry itself on failure:

```yaml
steps:
  1:
    task: project.build
    retries: 2            # up to 3 total attempts
    retryDelay: 1000      # ms between attempts
    retryOn: "timeout"    # only retry when error message contains this substring
```

Omit `retryOn` to retry on any error. The actual attempt count surfaces on `result.steps[i].attempts`.

## Rollback on Failure

Mutating bridge handlers emit a `rollback: { method, payload }` record on success. When a flow sets `rollback_on_failure: true` (or the caller passes it) and a later step fails, the runner invokes the collected inverses **in reverse order**, best-effort, and reports the outcome in `result.rollback`:

```yaml
flows:
  safe_scene:
    description: Place pillars with automatic cleanup on failure
    rollback_on_failure: true
    steps:
      1: { task: level.place_actor, options: { actorClass: StaticMeshActor, label: A } }
      2: { task: level.place_actor, options: { actorClass: StaticMeshActor, label: B } }
      3: { task: some_fragile_step }
```

If step 3 fails: the `delete_actor` inverses for B and A run, leaving the level as it was. Handlers without an inverse (`execute_console`, `shell`, some deletes) simply don't contribute records; their steps are left as-is when rollback runs.

Conventions for handlers — natural keys, the `onConflict: skip|update|error` option, and rollback record shape — live in [docs/handler-conventions.md](handler-conventions.md).

## Git Snapshot Safety Net

Per-handler rollback covers in-memory state (selection, PIE, unsaved actors). For anything that touched disk (new `.uasset` files, modified `.ini` config, deleted packages), enable the opt-in git snapshot. On flow start the runner snapshots `Content/` and `Config/` into a shadow bare git repo, and on failure runs `git read-tree --reset -u` to restore, then asks the editor to reload affected packages.

Enable it in `ue-mcp.yml`:

```yaml
git_snapshot:
  enabled: true
  paths: [Content, Config]          # defaults shown
  snapshot_dir: .ue-mcp/snapshot.git # relative to project root
  max_age_hours: 24                  # prune older snapshot refs on each run
```

The shadow repo is completely separate from any project-level git — your real history isn't touched. Snapshot failure doesn't fail the flow; handler-level rollbacks still apply. Restore outcomes surface in `result.snapshotRestore`.

## Skipping Steps

Pass step names or numbers in the `skip` array:

```
flow(action="run", flowName="setup_scene", skip=["2", "SkyAtmosphere"])
```

## Execution Plan

Preview what a flow will do without running it:

```
flow(action="plan", flowName="setup_scene")
```

Returns each step with its task name, type, and skip status.

## Built-in Flows

UE-MCP ships with a default flow you can run out of the box.

### Beacon

A 56-step demo that builds a complete shrine scene from scratch — geometry, materials, lighting, atmosphere, and camera.

```
flow(action="run", flowName="beacon")
```

What it creates:

| Steps | Category | What |
|-------|----------|------|
| 1–4 | level | New level, SkyAtmosphere, ExponentialHeightFog, SkyLight |
| 5–7 | material | **M_Floor** — dark stone base color |
| 8–16 | material | **M_Pillar** — brushed metallic (Metallic=1, Roughness=0.3) |
| 17–19 | material | **M_Pedestal** — warm stone |
| 20–28 | material | **M_Glow** — parameterized emissive (VectorParameter × 50 → EmissiveColor) |
| 29 | level | Floor slab (scaled Cube with M_Floor) |
| 30 | level | Center pedestal (Cylinder with M_Pedestal) |
| 31 | level | Glowing orb (Sphere with M_Glow) |
| 32–36 | level | 5 pillars in a pentagon (Cubes with M_Pillar) |
| 37–39 | level | Sunset directional light |
| 40–49 | level | 5 colored point lights at pillar tops (cyan, magenta, gold, green, violet) |
| 50–51 | level | Center spotlight pointing down at orb |
| 52–55 | level | Warm and cool fill lights |
| 56 | editor | Viewport camera framing the scene |

Preview the execution plan without running:

```
flow(action="plan", flowName="beacon")
```

The beacon flow is defined in `dist/ue-mcp.default.yml`. Users can override any of its steps by redefining the `beacon` flow in their project's `ue-mcp.yml`.

---

## Customization

### Overriding a Built-in Task

To change how a built-in task behaves, redefine it in your `ue-mcp.yml`. Your definition merges on top of the defaults (your fields win):

```yaml
tasks:
  asset.list:
    class_path: ./tasks/FilteredAssetList.js
    description: Asset list with custom filtering
```

The built-in `asset.list` is now replaced by your class. The dynamic loader will import `./tasks/FilteredAssetList.js` from your project root.

### Writing a Custom Task

Create a file that exports a class extending `BaseTask`:

```typescript
// tasks/FilteredAssetList.ts
import { BaseTask, type TaskResult } from '@db-lyon/flowkit';

export default class FilteredAssetList extends BaseTask {
  get taskName() {
    return 'asset.filtered_list';
  }

  async execute(): Promise<TaskResult> {
    // Call the original asset.list via the registry
    const result = await this.call('asset.list', {
      directory: (this.options as any).directory ?? '/Game/',
      recursive: true,
    });

    if (result.success && result.data?.assets) {
      const exclude = (this.options as any).excludePrefix ?? '/Game/Developers/';
      result.data.assets = (result.data.assets as any[])
        .filter(a => !a.path?.startsWith(exclude));
    }

    return result;
  }
}
```

Register it in your config:

```yaml
tasks:
  asset.list:
    class_path: ./tasks/FilteredAssetList
    description: Asset list that filters out developer content
    options:
      excludePrefix: /Game/Developers/
```

Key points:

- **Export as default** — the loader looks for a default export, or a named export matching the filename.
- **Must extend `BaseTask`** — the registry validates this at load time.
- **`this.options`** — receives the merged options (task defaults + step overrides).
- **`this.ctx`** — the shared context with `bridge` (editor WebSocket) and `project` (path resolution).
- **`this.call(name, opts)`** — resolve and execute another task by name. The original built-in task is still in the registry even when you override it via YAML `class_path`.
- **`this.resolve(name, opts)`** — like `call()` but returns the task instance without running it, in case you need to inspect or configure it first.

### Extending a Bridge Task

If your custom task needs to call the editor, extend `BridgeTask`:

```typescript
// tasks/SafeBuild.ts
import { BaseTask, type TaskResult } from '@db-lyon/flowkit';

export default class SafeBuild extends BaseTask {
  get taskName() {
    return 'safe_build';
  }

  async execute(): Promise<TaskResult> {
    // Check status first
    const status = await this.call('project.get_status');
    if (!status.success || !status.data?.connected) {
      return {
        success: false,
        error: new Error('Editor not connected — cannot build'),
      };
    }

    // Run the actual build
    return this.call('project.build', {
      configuration: (this.options as any).configuration ?? 'Development',
    });
  }
}
```

```yaml
tasks:
  safe_build:
    class_path: ./tasks/SafeBuild
    description: Build with connection check

flows:
  safe_build_flow:
    description: Safely build the project
    steps:
      1:
        task: safe_build
        options:
          configuration: Shipping
```

### Composing Tasks

A custom task can orchestrate multiple tasks:

```typescript
// tasks/FullSetup.ts
import { BaseTask, type TaskResult } from '@db-lyon/flowkit';

export default class FullSetup extends BaseTask {
  get taskName() {
    return 'full_setup';
  }

  async execute(): Promise<TaskResult> {
    // Place a bunch of actors
    const actors = [
      { className: 'DirectionalLight', location: { x: 0, y: 0, z: 500 } },
      { className: 'SkyAtmosphere' },
      { className: 'ExponentialHeightFog' },
      { className: 'SkyLight' },
    ];

    const placed = [];
    for (const actor of actors) {
      const result = await this.call('level.place_actor', actor);
      if (!result.success) return result;
      placed.push(result.data);
    }

    return {
      success: true,
      data: { placed, count: placed.length },
    };
  }
}
```

### Wrapping a Task (Programmatic)

If you're building on top of ue-mcp in code, the registry supports wrapping any registered task:

```typescript
import { buildFlowRegistry } from 'ue-mcp';

const registry = buildFlowRegistry(tools);

// Wrap asset.list with logging
registry.wrap('asset.list', (Original) => {
  return class extends Original {
    get taskName() { return 'asset.list:logged'; }

    async execute() {
      console.log(`Listing assets with options:`, this.options);
      const result = await super.execute();
      console.log(`Found ${result.data?.assets?.length ?? 0} assets`);
      return result;
    }
  };
});
```

Multiple wraps compose — each layer sees the previously wrapped version as its parent:

```typescript
// First wrap adds logging
registry.wrap('asset.list', (Original) => class extends Original { /* log */ });

// Second wrap adds caching — it wraps the logged version
registry.wrap('asset.list', (Original) => class extends Original { /* cache */ });
```

---

## Config Layering

Configuration is loaded with [`@db-lyon/flowkit`'s config loader](https://github.com/db-lyon/flowkit), which supports layered YAML files:

| Layer | File | Purpose |
|-------|------|---------|
| 1 (base) | Built-in defaults | All <!-- count:actions -->449+<!-- /count --> tasks, no flows |
| 2 | `ue-mcp.yml` | Your project config |
| 3 | `ue-mcp.{env}.yml` | Environment overlay (set `UE_MCP_ENV`) |
| 4 | `ue-mcp.local.yml` | Local-only overrides (gitignore this) |

Each layer deep-merges on top of the previous. Later layers win for scalar values; objects merge recursively.

**Example:** keep your shared flows in `ue-mcp.yml` and machine-specific overrides in `ue-mcp.local.yml`:

```yaml
# ue-mcp.local.yml — not committed
tasks:
  shell:
    options:
      timeout: 600000    # slow machine, need longer builds
```

### Environment Overlays

Set the `UE_MCP_ENV` environment variable to load an environment-specific layer:

```bash
UE_MCP_ENV=ci npx ue-mcp /path/to/project.uproject
```

This loads `ue-mcp.ci.yml` on top of `ue-mcp.yml`.

## Hot Reload

The config is **reloaded from disk on every flow call**. Edit `ue-mcp.yml`, save, and run the flow again — no server restart needed. This makes it easy to iterate on flow definitions.

## Dynamic Class Loading

When you set `class_path` to a file path (e.g., `./tasks/MyTask`), the registry resolves it relative to the current working directory. It tries these candidates in order:

1. `{cwd}/tasks/MyTask.ts`
2. `{cwd}/tasks/MyTask.js`
3. `{cwd}/tasks/MyTask/index.ts`
4. `{cwd}/tasks/MyTask/index.js`

The loaded module must export a class that extends `BaseTask`, either as the default export or as a named export matching the filename.

Dynamically loaded classes are cached — the file is only imported once per class path per session.

## BaseTask Reference

All custom tasks extend `BaseTask` from `@db-lyon/flowkit`:

```typescript
abstract class BaseTask<TOpts = Record<string, unknown>> {
  protected ctx: TaskContext;           // Shared context (bridge, project, registry)
  protected options: TOpts;             // Merged options for this execution
  protected logger: Logger;             // Scoped logger

  abstract get taskName(): string;      // Descriptive name for logging
  abstract execute(): Promise<TaskResult>;  // Your task logic

  protected validate(): void;           // Option validation (override, called before execute)

  // Composition — resolve/run other tasks from within your task
  protected resolve(taskName: string, options?: Record<string, unknown>): Promise<BaseTask>;
  protected call(taskName: string, options?: Record<string, unknown>): Promise<TaskResult>;

  // Lifecycle — called by the engine, not by you
  run(): Promise<TaskResult>;           // validate → execute → catch errors → return result
}
```

### TaskResult

```typescript
interface TaskResult {
  success: boolean;
  data?: Record<string, unknown>;
  error?: Error;
  duration?: number;      // Set automatically by run()
}
```

### TaskContext

In ue-mcp, the context includes:

| Property | Type | Description |
|----------|------|-------------|
| `bridge` | `IBridge` | WebSocket connection to the Unreal Editor |
| `project` | `ProjectContext` | Path resolution, project info |
| `registry` | `TaskRegistry` | Task registry for resolving other tasks |
| `logger` | `Logger` | Structured logger |
