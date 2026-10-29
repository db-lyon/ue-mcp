# Flows

A flow is a named sequence of steps declared in YAML and run as one operation. Flows run on [`@db-lyon/flowkit`](https://github.com/db-lyon/flowkit). A step can call any action, run a shell command, run another flow, or run a task you wrote.

## Quick start

Add a `flows:` section to the `ue-mcp.yml` next to your `.uproject`:

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

Run it:

```
flow(action="run", flowName="build_and_check")
```

The config is reloaded on every `flow` call, so an edited flow runs without a server restart.

The response carries a `summary` line per step, a `steps` array with what each step answered (`data`, `success`, `attempts`, `error`), `failedStep`, `rollback`, `hookErrors` and the `runId`.

## The `flow` tool

| Action | Params | What it does |
|--------|--------|--------------|
| `run` | `flowName`, `skip?`, `params?`, `rollback_on_failure?` | Run a flow. |
| `plan` | `flowName` | Show the execution plan, the flow as declared, and its [inputs](#inputs), without running anything. |
| `list` | none | List the flows available to this project. |

`project(action="get_status")` also lists the available flows in its `flows` field.

## Tasks

Every action is a task named `<category>.<action>`: `level.place_actor`, `asset.list`, `project.build`. There are <!-- count:actions -->2114+<!-- /count --> across <!-- count:tools -->30<!-- /count --> categories. A few more are built in:

| Task | What it does |
|------|--------------|
| `shell` | Run a shell command. Options: `command`, `cwd?`, `timeout?` (ms, default 300000). |
| `editor.restart` | Restart the editor. |
| `editor.rebuild` | Stop the editor, build, start it again. Leaves the editor down when the build fails. |
| `agent`, `agent_prompt` | Class paths for [agent steps](#agent-steps). |

`editor.restart` and `editor.rebuild` go through the daemon when it is serving, so the disconnect they cause is reported as a restart or rebuild rather than a crash.

A `tasks:` entry defines a new task or overrides a built-in one:

```yaml
tasks:
  quick_list:
    class_path: asset.list        # reuse a built-in task's implementation
    description: List top-level assets
    options:
      directory: /Game/
      recursive: false
```

| Field | Description |
|-------|-------------|
| `class_path` | A registered task name (`asset.list`), a registered class path (`ue-mcp.bridge`, `shell`, `agent`, `agent_prompt`), or a path to your own [task file](#custom-tasks). Defaults to the task name. |
| `description` | Shown in listings. |
| `group` | Category for organization. |
| `options` | Default options, overridden per step. |
| `options_schema` | Declared options (`type`, `required`, `default`, `enum`, `minimum` and the other JSON Schema keywords). Step options are validated against it before the task runs. |

`class_path: ue-mcp.bridge` calls a bridge method directly. The `method` option names it and the other options are sent as its parameters.

## Steps

Step keys are whole numbers and run in numeric order. A step that fails stops the flow.

```yaml
flows:
  setup_scene:
    description: Create a basic lit scene
    steps:
      1:
        task: level.place_actor
        options:
          actorClass: DirectionalLight
          location: { x: 0, y: 0, z: 500 }
      2:
        task: level.place_actor
        options:
          actorClass: SkyAtmosphere
      3:
        flow: other_flow            # run another flow
      4:
        task: None                  # a disabled step
```

A step has exactly one of `task` or `flow`. `task: None` (or `flow: None`) disables a step, which is how an overlay file turns one off.

| Step field | Description |
|------------|-------------|
| `task` / `flow` | What the step runs. |
| `options` | Options for this step. |
| `when` | `true`, `false`, or a `${...}` reference whose truthiness decides. A falsy value skips the step without failing the flow. |
| `retries`, `retryDelay`, `retryOn` | See [Retry](#retry). |
| `ignore_failure` | See [Expected failures](#expected-failures). |
| `checks` | Preflight checks: each has `when`, `action` (`error`, `warn` or `skip`) and an optional `message`. |

| Flow field | Description |
|------------|-------------|
| `description` | Shown in listings. |
| `steps` | The steps. |
| `on_start`, `on_success`, `on_failure`, `finally` | [Hooks](#hooks). |
| `rollback_on_failure` | See [Rollback](#rollback-on-failure). |
| `on_editor_loss`, `editor_loss_timeout_seconds` | See [When the editor goes away](#when-the-editor-goes-away). |
| `options_scope` | `flat` (default): runtime `params` go to every step. `step`: each `params` key is a step selector (a task name or a step path such as `2` or `2/1`) and its value is the options for those steps only. |
| `checks` | Preflight checks for the whole flow. |

## Options and runtime parameters

Options merge in three layers. The later one wins:

```
task options  <  step options  <  runtime params
```

Pass runtime `params` to override step options for one run:

```
flow(action="run", flowName="beacon", params={ levelPath: "/Game/MyLevel" })
```

With the default `options_scope: flat`, every step receives every param, and a step ignores keys it does not use.

## Step references

`${steps.<id>.<path>}` reads the result of an earlier step:

```yaml
steps:
  1:
    task: material.create
    options: { name: M_Glow, packagePath: /Game/Materials }
  2:
    task: material.set_base_color
    options:
      assetPath: ${steps.1.path}          # whole value: the raw value is substituted
      color: { r: 255, g: 120, b: 0 }
  3:
    task: shell
    options:
      command: "echo created ${steps.material.create.path}"   # embedded: stringified
```

- `<id>` is a step number or a task name. A task name matches the most recently completed step with that name, longest prefix first.
- `<path>` is a dot path into that step's `data`.
- A reference that is the whole option value keeps its type (objects and arrays included). An embedded reference is stringified.
- An unresolvable reference fails the step.
- A nested flow has its own scope and cannot reference steps of the flow that runs it.

## Hooks

```yaml
flows:
  deploy:
    description: Build and save
    on_start:   [ { task: shell, options: { command: "echo starting" } } ]
    on_success: [ { task: shell, options: { command: "echo done" } } ]
    on_failure: [ { task: shell, options: { command: "echo failed: ${error.message}" } } ]
    finally:    [ { task: project.get_status } ]
    steps:
      1: { task: project.build }
      2: { task: asset.save }
```

- `on_start` runs before the first step. Its failure fails the flow.
- `on_success` runs after every step succeeded.
- `on_failure` runs after a step failed. `${error.message}`, `${error.name}`, `${error.stack}` and `${error.step}` resolve in it and in `finally`.
- `finally` runs last, whatever the outcome.

Hook steps take the same references, options and runtime params as main steps. A hook failure is reported in `hookErrors` and does not change the flow's outcome.

## Retry

```yaml
steps:
  1:
    task: project.build
    retries: 2            # up to 3 attempts
    retryDelay: 1000      # ms between attempts
    retryOn: "timeout"    # retry only when the error message contains this
```

Without `retryOn`, any error is retried. The attempt count is on `steps[i].attempts`.

## Expected failures

`ignore_failure: true` lets the run continue past a step whose failure is expected:

```yaml
steps:
  1:
    task: editor.stop_editor
    ignore_failure: true    # fails with alreadyStopped when no editor is running
  2: { task: editor.build_project }
  3: { task: editor.start_editor }
```

`editor(stop_editor)` with no editor running answers `success: false` with `alreadyStopped: true`, and `editor(start_editor)` on a running editor answers `success: false` with `alreadyRunning: true`. They did not do what was asked, so they fail, and the flow decides whether that matters.

- The step is still recorded as failed, with its error and data, marked `ignoredFailure: true` and printed as `FAILED (ignored)`.
- The run continues, and `success` stays true if nothing else fails. `failedStep` never names an ignored step.
- It applies after `retries` are exhausted.
- It covers a `success: false` body, a thrown error, and a `when:` expression that throws, on main steps only.

Hooks cannot do this: `on_failure` and `finally` run after the run has stopped.

## Rollback on failure

Mutating bridge handlers return a `rollback: { method, payload }` record. With `rollback_on_failure: true` on the flow (or passed to `run`), a failed run invokes the collected inverses in reverse order, best effort, and reports them in `rollback`:

```yaml
flows:
  safe_scene:
    description: Place two actors, removed again on failure
    rollback_on_failure: true
    steps:
      1: { task: level.place_actor, options: { actorClass: StaticMeshActor, label: A } }
      2: { task: level.place_actor, options: { actorClass: StaticMeshActor, label: B } }
      3: { task: some_fragile_step }
```

If step 3 fails, B and A are deleted. Steps with no inverse (`execute_command`, `shell`, some deletes) contribute nothing.

A step fails when its task throws and when the handler answers `success: false`. A handler that found the work already done answers `success: true` with a marker such as `alreadyExists` or `existed`. See [handler conventions](handler-conventions.md) for natural keys, `onConflict` and the rollback record shape.

### Partial writes

A few handlers attach a rollback record to a `success: false` answer because part of the write landed (for example `asset(rename)` on a World that moved some external actor packages). The runner collects that record with the others and, under `rollback_on_failure`, replays it first, before the inverses of earlier steps.

The record is reported on the failing step:

```
steps[i].partialWriteRollback = {
  record: { taskName, payload },
  step: '{ task: "ue-mcp.bridge", options: { method: ..., ... } }',
  replayed: true | false,
  note: ...
}
```

`replayed: true` means the runner already ran it; check `rollback.errors`. `replayed: false` means rollback was not armed: run the step shown, or call the bridge method in `record.payload.method` with the rest of the payload. The same call is appended to the step's error message.

An `ignore_failure` step contributes its record too, and a later real failure unwinds it with the rest. A nested flow's records go to its parent; the outermost flow with rollback armed runs them once. Its steps are reported on `steps[i].nestedSteps`.

## When the editor goes away

When the editor crashes, closes or restarts during a run, no further step starts until it is back, for up to `editor_loss_timeout_seconds` (default 300). A step that failed while the editor dropped has an unknown outcome. When the editor returns, a read-only step is run again to verify it; any other step stays unknown. The flow's policy then decides:

```yaml
flows:
  bake_lighting:
    on_editor_loss: resume           # resume (default) | rollback | fail
    editor_loss_timeout_seconds: 600
    steps: { ... }
```

| Policy | The step the editor dropped during |
|---|---|
| `resume` | The run continues. The step reports `data.editorLoss.outcome: unknown`. |
| `rollback` | The step fails and the run unwinds as under `rollback_on_failure`, unless the caller set `rollback_on_failure` itself. With the [git snapshot](#git-snapshot) on, files are restored while the editor is away. |
| `fail` | The step fails and the run stops. |

The result carries `editorLoss`: each event with its step, outcome, cause (`crashed`, `closed`, `restarting`, `rebuild` or `unknown`) and wait time, plus the completed steps that reported unsaved changes when the editor went away.

## Git snapshot

Rollback records cover editor state. For files on disk, turn on the git snapshot in `ue-mcp.yml`:

```yaml
git_snapshot:
  enabled: true
  paths: [Content, Config]           # default
  snapshot_dir: .ue-mcp/snapshot.git # relative to the project
  max_age_hours: 24                  # older snapshots are pruned on each run
```

At flow start the runner snapshots those paths into a separate bare git repository. On failure it restores them with `git read-tree --reset -u` and asks the editor to reload the affected packages. The project's own git history is not touched. A snapshot failure does not fail the flow. The outcome is in `snapshotRestore`.

## Running

Skip steps by number or task name:

```
flow(action="run", flowName="setup_scene", skip=["2", "3"])
```

Preview without running:

```
flow(action="plan", flowName="setup_scene")
```

`plan` returns each step with its task, type and skip status, the flow as declared under `definition`, and `inputs`.

### Inputs

`inputs` lists the parameters a run may pass in `params`, derived from the steps. A step task's parameters come from its action's schema, refined by the task's `options_schema`; a task with neither falls back to the `Params:` clause of its description.

A parameter is an input when at least one step that reads it leaves it unset. A step sets a parameter through its own `options`, its task's `options`, a nesting flow step, an alias, or the other side of a choice (`actorLabel` or `actorPath`). Routing parameters (`editor`, `timeoutMs`, `select`, `omit`) are never inputs. Nested flows and hook steps count.

| Field | Meaning |
|-------|---------|
| `name` | The key to pass in `params`. |
| `type` | The type. An array of types, with `conflict: true`, when steps disagree. |
| `conflict` | `true` when steps type it differently or share no `enum` value. |
| `description` | From the first step that describes it. |
| `enum` | The values every reading step allows. |
| `default` | The first default a step declares. |
| `required` | A reading step requires it and no step declares a default. |
| `steps` | The reading steps: `path` (`3`, `2/1` inside step 2's nested flow, `on_start/1` for a hook), `task`, and `flow`. |

Required inputs come first. A param every step sets can still be overridden at run time; `inputs` lists only what the flow leaves open.

## HTTP and live events

Turn on the flow HTTP server in `ue-mcp.yml`:

```yaml
ue-mcp:
  version: 1
  http:
    enabled: true
    port: 7723        # default
    host: 127.0.0.1   # default
```

The server logs its bearer token to stderr at startup, or uses `UE_MCP_HTTP_TOKEN`. Every route requires it, as `Authorization: Bearer <token>` or `X-UE-MCP-Token: <token>`. In daemon mode the project's daemon serves it; in process, the server does.

| Route | What it does |
|-------|--------------|
| `GET /flows` | List flows. |
| `GET /flows/<name>/plan` | Same body as `flow(plan)`. |
| `POST /flows/<name>/run` | Run a flow. Body: `params?`, `skip?`, `rollback_on_failure?`, `editor?`. |
| `GET /flows/events` | Server-Sent Events. `?runId=<id>` filters to one run. |
| `GET /health` | `{ ok, port, host }`. |

The daemon serves the same flow routes under `/v1` (for example `GET /v1/flows/<name>/plan`).

```bash
curl --no-buffer -H "Authorization: Bearer $TOKEN" http://127.0.0.1:7723/flows/events
```

Each event is one JSON object per `data:` line:

| `type` | When | Payload |
|--------|------|---------|
| `run_started` | Before any step | `runId`, `flowName`, `plan`, `timestamp` |
| `step_started` | Before each step that is not skipped | `runId`, `flowName`, `step`, `timestamp` |
| `step_completed` | After every step, skipped ones included | `runId`, `flowName`, `step`, `result: { success, skipped, duration, attempts?, error? }`, `timestamp` |
| `step_failed` | When a step errors, after `step_completed` | `runId`, `flowName`, `step`, `error: { message, name }`, `timestamp` |
| `run_completed` | After the flow, hooks and rollback | `runId`, `flowName`, `success`, `duration`, `stepCount`, `failedStep?`, `timestamp` |

`step_completed` leaves out the step's data; the `run` response has it. A `: keepalive <ts>` comment is sent every 30 seconds.

## Built-in flows

| Flow | Steps | What it does |
|------|-------|--------------|
| `niagara_fire` | 7 | Builds a fire Niagara system (spawn rate, upward velocity, HDR orange colour) at `/Game/VFX/Fire/NS_Fire` and ends with `niagara(action="validate")` to confirm it emits. Override `name` and `packagePath` with params. |
| `texture_bomb` | 14 | Builds `/Game/Materials/Functions/MF_TextureBomb`, a MaterialFunction that rotates and offsets UVs per cell so a tiled texture stops looking tiled. |
| `beacon` | 56 | Builds a shrine scene from individual actions: level, four materials, geometry, lights and camera, under `/Game/Flows/Beacon`. A reference for a long flow. |
| `neon_shrine` | 19 | The [Neon Shrine demo](neon-shrine-demo.md), one `demo(action="step", stepIndex=N)` per step. Leaves the editor on `/Game/Demo/DemoLevel`. |
| `neon_shrine_cleanup` | 1 | Removes the Neon Shrine content and switches the editor to `/Game/MCP_Home`. |
| `playtest` | 8 | Drafts a PIE test script for a target with a model and saves it once the editor accepts it. See [Playtest](#playtest). |

A flow of the same name in your `ue-mcp.yml` overrides a built-in one.

### Beacon

`beacon` is the long-form example: 56 steps, each one action. Steps 1 to 4 create the level and atmosphere, 5 to 28 author `M_Floor`, `M_Pillar`, `M_Pedestal` and the parameterized emissive `M_Glow`, 29 to 36 place the floor, pedestal, orb and five pillars, 37 to 55 add the sun, pillar lights, spotlight and fill lights, and 56 frames the viewport. Material steps address created nodes with references such as `${steps.10.expressionIndex}`. `flow(action="plan", flowName="beacon")` lists every step.

### Playtest

Drafts a [test script](pie-scripts.md) for a target:

```
flow(action="run", flowName="playtest", params={ "target": "/Game/Hero/BP_Hero", "context": "the jump lands on the ledge" })
```

| Param | |
|---|---|
| `target` | Required. An asset or class path, or a description matched against the project's Blueprints. |
| `context` | What to test. |
| `profile` | The `pie(script_values)` profile whose values the script may watch. |
| `name`, `package_path` | Where the script is saved. Default `TS_Playtest_<target>` under `/Game/PIEStudio/Scripts`. |

| Step | Task | |
|---|---|---|
| 1 | `playtest.gather` | Reads the target Blueprint, the project's input mapping contexts, and `pie(script_values)`. |
| 2 | `playtest.draft` | An [agent step](#agent-steps) that answers in the script step schema. |
| 3 | `playtest.check` | `pie(script_validate)` on the draft. A refusal's errors become the next prompt. |
| 4 to 7 | `playtest.draft`, `playtest.check` | Two more rounds, each only when the last draft was refused. |
| 8 | `playtest.save` | `pie(script_create)` with the draft that passed. |

When the third draft is refused, the flow fails with its errors and nothing is saved.

## Agent steps

A task with `class_path: agent` (multi-turn, with tools) or `agent_prompt` (one call) runs on a model. The model comes from, in order:

1. A provider a daemon extension registered with `api.models.register(provider)` (feature `models.provider`).
2. `ANTHROPIC_API_KEY`, with `UE_MCP_FLOW_MODEL` naming the model (default `claude-opus-5-5`) and `ANTHROPIC_BASE_URL` an alternative endpoint.

With neither, the step fails with `No model is configured for flow agent steps.`

```yaml
tasks:
  summarize:
    class_path: agent_prompt
    options:
      system: You describe Unreal projects in two sentences.
      maxTokens: 2000
flows:
  describe_project:
    description: Describe the project.
    steps:
      1: { task: project.get_info }
      2: { task: summarize, options: { prompt: "Describe this project: ${steps.1}" } }
```

A provider implements flowkit's model interface:

```ts
interface ModelProvider {
  complete(req: ModelRequest): Promise<ModelResponse>;
}
// ModelRequest: { system?, prompt?, messages?, model?, maxTokens?, temperature?, stop?, schema?, tools?, toolChoice?, signal? }
// ModelResponse: { text, parsed?, toolCalls?, finishReason?, usage?: { inputTokens?, outputTokens? }, model? }
```

## Custom tasks

A task is a class extending `UeMcpTask` from `ue-mcp/task`, exported as the default export or as a named export matching the file name:

```typescript
// tasks/FilteredAssets.ts
import { UeMcpTask, type TaskResult } from "ue-mcp/task";

export default class FilteredAssets extends UeMcpTask<{ directory?: string; excludePrefix?: string }> {
  get taskName() { return "filtered_assets"; }

  async execute(): Promise<TaskResult> {
    const result = await this.call("asset.list", {
      directory: this.options.directory ?? "/Game/",
      recursive: true,
    });
    if (!result.success) return result;
    const exclude = this.options.excludePrefix ?? "/Game/Developers/";
    const assets = (result.data?.assets as Array<{ path: string }> | undefined) ?? [];
    return { success: true, data: { assets: assets.filter((a) => !a.path.startsWith(exclude)) } };
  }
}
```

```yaml
tasks:
  filtered_assets:
    class_path: tasks/FilteredAssets
    description: Asset list without developer folders
    options:
      excludePrefix: /Game/Developers/

flows:
  scan:
    description: List project assets
    steps:
      1: { task: filtered_assets }
```

A `tasks:` entry that reuses a built-in name (for example `asset.list`) with your own `class_path` replaces that task everywhere, including `this.call("asset.list")` from your own class. Give a wrapper its own name, as above, when it calls the task it wraps.

### Class path resolution

A file `class_path` resolves from the folder that holds `ue-mcp.yml`. Dots in the path are read as folder separators, so write `tasks/FilteredAssets` or `tasks.FilteredAssets`, with no `./` and no extension. The loader tries, in order:

1. `<project>/tasks/FilteredAssets.ts`
2. `<project>/tasks/FilteredAssets.js`
3. `<project>/tasks/FilteredAssets/index.ts`
4. `<project>/tasks/FilteredAssets/index.js`

A `.ts` file needs a Node version that runs TypeScript directly; otherwise compile to `.js`. A loaded class is cached for the session, so restart the server after editing a task file.

### UeMcpTask reference

```typescript
abstract class UeMcpTask<TOpts = Record<string, unknown>> {
  protected ctx: FlowContext;           // bridge, project, session, registry, logger
  protected options: TOpts;             // merged options for this run
  protected logger: Logger;

  protected get bridge(): IBridge;      // this.ctx.bridge

  abstract get taskName(): string;
  abstract execute(): Promise<TaskResult>;

  protected validate(): void;           // called before execute; throw to fail

  protected call(taskName: string, options?: Record<string, unknown>): Promise<TaskResult>;
  protected resolve(taskName: string, options?: Record<string, unknown>): Promise<BaseTask>;  // instance, not run

  run(): Promise<TaskResult>;           // called by the runner: validate, execute, catch errors
}

interface TaskResult {
  success: boolean;
  data?: Record<string, unknown>;
  error?: Error;
  rollback?: { taskName: string; payload: Record<string, unknown> };
  duration?: number;                    // set by run()
}
```

`this.call` composes other tasks and keeps their rollback records and observability. `this.bridge.call(method, params)` calls a bridge method that no task wraps.

## Config layers

Flows and tasks merge from these layers, later ones winning:

1. Built-in tasks and flows.
2. Flows and tasks from loaded [plugins](plugins.md).
3. `~/.ue-mcp/config.yml`.
4. `ue-mcp.yml`.
5. `ue-mcp.{env}.yml`, selected by `UE_MCP_ENV` or the `env:` key in the `ue-mcp:` block.
6. `ue-mcp.local.yml`.

Objects merge recursively; scalars are replaced. See [Config layering](config-file.md#config-layering).

```yaml
# ue-mcp.local.yml
tasks:
  shell:
    options:
      timeout: 600000
```
