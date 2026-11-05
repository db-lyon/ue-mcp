# Development

## Prerequisites

- [Node.js](https://nodejs.org/) 20 or newer
- Unreal Engine 5.4 to 5.8, for building the plugin and live testing

## Setup

```bash
git clone https://github.com/db-lyon/ue-mcp.git
cd ue-mcp
npm install
```

## Resolving an issue with Claude Code

From inside a `ue-mcp` clone:

```bash
npx ue-mcp resolve 16
```

This fetches issue #16 from the repository the `origin` remote names, creates a `resolve/16` branch from `origin/main`, and runs `claude --print --dangerously-skip-permissions` with the issue and the repo conventions. Claude implements the fix, runs `npx tsc --noEmit` and commits. The command then pushes the branch and opens a pull request. It needs the `gh` and `claude` CLIs and push access to `origin`, and works on the current checkout.

## Building

```bash
npx tsc                  # TypeScript -> dist/
node scripts/deploy.mjs  # sync plugin/ -> tests/ue_mcp/Plugins/UE_MCP_Bridge/
npm run build            # build the C++ plugin in the test project
npm run up:build         # stop the test editor, build, relaunch
```

`npm run build` builds only the `ue_mcpEditor` target of `tests/ue_mcp/ue_mcp.uproject` and refuses any other project. The test editor holds the module DLLs, so close it first or use `npm run up:build`. Neither command runs the deployer: run `node scripts/deploy.mjs` after editing `plugin/`.

The build passes `-NoEngineChanges`, so Unreal aborts and prints the offending files if the build would overwrite anything already in the engine tree. Engine selection order is `UE_MCP_TEST_ENGINE_ROOT`, then `UE_BUILD_TOOL_PATH`, then the default install locations. A pinned root with no build tool is an error.

```powershell
# Roots the build and run scripts must never touch, whatever else is set.
$env:UE_MCP_PROTECTED_ENGINE_ROOTS = 'D:\UE-Primary;D:\UE-Release'

# Pin the engine used for test builds and editor launches.
$env:UE_MCP_TEST_ENGINE_ROOT = 'D:\UE-Test'
```

`UE_MCP_PROTECTED_ENGINE_ROOTS` uses the platform path-list delimiter (`;` on Windows, `:` elsewhere). An engine equal to or under a protected root is rejected, including one named by `UE_BUILD_TOOL_PATH` or `UE_EDITOR_PATH`.

A fresh engine may need to create its engine-side outputs once. For that bootstrap only, `UE_MCP_ALLOW_TEST_ENGINE_CHANGES=true` drops `-NoEngineChanges`. Protected roots stay forbidden, and the build prints a warning while the variable is set.

If new handlers answer `Unknown method` after a successful build, delete `tests/ue_mcp/Plugins/UE_MCP_Bridge/`, redeploy and build again.

## Running

```bash
npm run up                                     # launch the editor on tests/ue_mcp
npm run dev                                    # server from source through tsx
node dist/index.js C:/path/to/MyGame.uproject  # server from dist/
node dist/index.js init C:/path/to/MyGame.uproject
node dist/index.js --help                      # every CLI command
```

## Project structure

```
src/
├── index.ts              # `ue-mcp` bin: CLI command, stdio shim or in-process server
├── init.ts, update.ts, deploy-cli.ts  # Other package bins, thin wrappers over cli/
├── task.ts, guard-task.ts, daemon-api.ts  # `ue-mcp/task`, `ue-mcp/guard`, `ue-mcp/daemon` exports
├── tools.ts              # ALL_TOOLS registry
├── daemon/               # Daemon, stdio shim, discovery, extensions, UI bundles, HTTP routes
├── server/               # Runtime shared by clients, McpServer per client, context projection
├── core/                 # Errors, logging, env, shared types, package root, version check
├── config/               # ProjectContext, ue-mcp.yml layers, user state, .uproject discovery
├── bridge/               # EditorBridge WebSocket client, ports, lockfiles, timeouts
├── editor/               # Editor start/stop/build, engine resolution, deploy, dialog guard
├── sessions/             # Registry of editors and each session's surface
├── surface/              # categoryTool, action specs, schemas, signatures, parameter routing
│   └── context/          # micro/lean strategies, instructions, tool search
├── dispatch/             # Per-call routing, editor gate, locks, offline and Python gates
├── codeintel/            # Engine symbol index, C++ parsing, .ini reading, Content index
├── checkpoints/          # Checkpoint shadow repository and diffs
├── pie/                  # PIE test script watching and events
├── feedback/             # Feedback routing and filing, GitHub auth, scrubbers
├── extensions/           # ue-mcp plugin packages, registry, skills, scaffolding
├── integrations/claude-code/  # Hooks and MCP client config
├── cli/                  # CLI commands, --editor parsing, terminal UI (cli/ui/)
├── flow/                 # Flow engine (registry, loader, task factory, HTTP)
└── tools/                # <!-- count:tools -->30<!-- /count --> category tools, one file each
    ├── project/          # project category, split by action group
    ├── epic/             # Generated epic_* actions (npm run epic:generate)
    └── specs/            # Generated parameter specs (npm run specs:generate)

plugin/ue_mcp_bridge/     # C++ bridge plugin
└── Source/
    ├── UE_MCP_Bridge/    # WebSocket server, registry, handlers
    │   └── Private/Handlers/<Category>/
    ├── UE_MCP_BridgeStatus/   # Early-loading status writer
    └── UE_MCP_BridgeRuntime/  # PIE input, remote control, capture

tests/unit/                # Unit tests (no editor)
tests/multi-editor/        # Multi-session tests (no editor)
tests/integration/         # CLI, server and daemon in sandboxes (no editor)
tests/golden/              # Recorded advertised surface and handler specs
tests/live/, tests/smoke/  # Need an editor on tests/ue_mcp
tests/coverage/            # Behavioural and integration coverage inventories
scripts/                   # Build, run, test and release scripts
docs/                      # Documentation
```

## Testing

`CLAUDE.md` at the repo root holds the full test and release workflow. In short:

| Command | What it runs | Editor |
|---------|--------------|--------|
| `npm test` | Unit, multi-editor and integration suites | No |
| `npm run test:unit` | `tests/unit`, including the golden surface check | No |
| `npm run test:integration` | Real CLI, server and daemon in hermetic sandboxes | No |
| `npm run test:automation` | The plugin's `UE.MCP.*` C++ suite, through the bridge | Yes |
| `npm run test:live` | `tests/live` through the shipped server, sharded across editors it launches | Launches its own |
| `npm run test:smoke` | Every registered handler | Yes |
| `npm run test:suites` | The Vitest suites in `tests/smoke` (`-- level` filters by file name) | Yes |

CI runs the unit and integration suites on every pull request.

!!! warning "Editor tests target the test project only"
    Smoke, suite and automation runs execute real mutations (creating blueprints, deleting assets, modifying the level). They target `tests/ue_mcp/ue_mcp.uproject` only: after connecting, the runner asks the editor which project it has open and aborts before sending anything if it is not `tests/ue_mcp`. Non-loopback hosts are refused.

### How the harness finds the bridge

The editor publishes the port it bound to `tests/ue_mcp/Saved/UE_MCP_Bridge/port.json`. `scripts/bridge-target.mjs`, used by `scripts/smoke-test.js` and the Vitest suites, tries:

1. The port in that lockfile.
2. The port derived from the project path.
3. The fixed port `9877`.

No environment variable is needed. When nothing answers, the failure names the lockfile, its state (missing, malformed, or stale with a dead pid) and every port tried. `--port` on the smoke runner and `UE_MCP_TEST_PORT` for the suites pin the port; neither weakens the project check.

### Automation tests

`npm run test:automation -- --filter <prefix>` narrows the C++ suite, for example `--filter UE.MCP.Level.PlaceVolume` (native volume placement) or `--filter UE.MCP.Blueprint.TransactionalNodes` (graph node flags). `--verbose` prints every error line.

### Live tests

Each shard of `npm run test:live` is a copy of `tests/ue_mcp` with its own editor and port, launched with the kind its files declare (`// @live-editor headless`, `render`, `interactive`, `base`, `scm`, `none`, `lifecycle`). Files are split by measured time, the shard count follows free memory, and the run fails over 10 minutes or when a test passes its budget. Every shard is checked to have its own project open before anything is sent.

- `--attach` drives the `tests/ue_mcp` editor that is already running and never starts or stops it.
- `--lifecycle` runs the editor lifecycle cases, which stop, crash, restart and rebuild the test editor.
- The leak assertions need the parameter echo. Shard editors have it; in attach mode, launch the editor with `UE_MCP_PARAM_ECHO=1`.
- `tests/live/crash-class-inputs.test.ts` drives inputs that have crashed the editor and follows each call with a liveness probe.
- `tests/live/matrix.ts` records where each case's assertion lives, and `tests/live/coverage.test.ts` checks each reference resolves.

## Adding an action or category

### TypeScript side

A category is a file in `src/tools/` built with `categoryTool(name, summary, actions, extraSchema?, options?)`, added to `ALL_TOOLS` in `src/tools.ts`:

```typescript
import type { ToolDef } from "../core/types.js";
import { categoryTool } from "../surface/category-tool.js";
import { specBp, schema as specSchema } from "./specs/myfeature.generated.js";

export const myfeatureTool: ToolDef = categoryTool(
  "myfeature",
  "What this category covers.",
  {
    // Bridge action: parameters come from the C++ spec.
    my_action: specBp("mutate", "What it does.", "my_cpp_method"),
    // Local action: runs in Node.js.
    local_action: {
      kind: "handler",
      effect: "read",
      description: "What it does. Params: none",
      handler: async (ctx, params) => ({ result: "done" }),
    },
  },
  { ...specSchema },
);
```

A category handler that calls another action in its own category routes that call through the active `ToolDef.handler` from `ctx.getToolGraph()`, so session-injected actions and the shared timeout, path repair, parameter folding and result projection apply. Use the exported base tool only when the context has no tool graph, such as a direct unit test.

### C++ side

1. Add the handler in `plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/<Category>/`. It is static, takes `const TSharedPtr<FJsonObject>& Params` and returns `TSharedPtr<FJsonValue>`.
2. Register it with a [parameter spec](architecture.md#parameter-specs) in the group's `RegisterHandlers`. A new group is added to `HandlerCatalog.cpp`.
3. Follow [Handler Conventions](handler-conventions.md) for idempotency and rollback.
4. Deploy, build, then `npm run specs:record` against `tests/ue_mcp` and `npm run specs:generate`.

Every new action needs a behavioural test (see the behavioural coverage gate in `CLAUDE.md`).

## C++ plugin notes

### Iterating

1. Edit the source in `plugin/ue_mcp_bridge/`.
2. Run `node scripts/deploy.mjs`.
3. Rebuild with `npm run up:build`, or reload with Live Coding (Ctrl+Alt+F11) or `editor(action="hot_reload")`.

### Struct names

Native USTRUCTs are registered without the leading `F`: `FTableRowBase` is the `UScriptStruct` named `TableRowBase`, path `/Script/Engine.TableRowBase`. `MCPResolveScriptStruct` in `HandlerClassResolve.h` is the shared lookup for `reflection(reflect_struct)` and `asset(create_datatable)`. It tries the literal spelling, then strips one leading `F`, including from the leaf of a `/Script/Module.FName` path. Only `reflect_struct` also tries adding one `F`. A short name shared by several loaded structs is an error listing the qualified candidates.

### Mesh edge connectivity

Use `FMeshDescription::GetEdgeConnectedTriangleIDs`, as UV island inspection does. It returns an array view without copying; `GetEdgeConnectedTriangles` is deprecated.

### JSON object range loops

Iterate `FJsonObject::Values` with `const auto&` and convert the key to `FString` inside the body if needed. UE 5.8 changed the key type, so an explicit `TPair<FString, TSharedPtr<FJsonValue>>` loop variable trips Clang's `-Wrange-loop-construct`, an error on Linux. `tests/unit/json-object-range-loops.test.ts` enforces this.

### Blueprint graph node flags

Construct persisted Blueprint and animation graph nodes with `RF_Transactional`. `UEdGraph::AddNode` does not set it, and without it opening the asset runs a transactional repair that dirties the Blueprint. Use `NewObject<TNode>(Graph, NAME_None, RF_Transactional)`, or `NewObject<UEdGraphNode>(Graph, NodeClass, NAME_None, RF_Transactional)` when the class is resolved at runtime. The flag makes node edits eligible for editor transactions; it does not wrap bridge calls in undo transactions. `UE.MCP.Blueprint.TransactionalNodes` covers the construction paths.

### File-local helpers and the unity build

UBT compiles a module as a **unity build**: several `.cpp` files are concatenated into one translation unit. Two handler files can each define a file-local helper of the same name in an anonymous namespace and compile alone. When the grouping puts them in one blob, the namespaces merge and the second definition is a redefinition:

```
AssetHandlers_BulkUpsert.cpp(53,6): error C2084: function
'`anonymous-namespace'::IsProtectedAssetPath' already has a body
```

The grouping shifts with file count, file order and the adaptive-unity working set, which UBT derives from `git status`, so a duplicate can build on one machine and fail on the next.

Put a helper two files need in a shared header (one of the `Public/Handler*.h` headers that `HandlerUtils.h` gathers), never a copy. Copies also drift apart.

```bash
npm run audit:unity      # duplicate file-local definitions, per module
```

The audit runs in `npm run audit` and as a unit test, so CI gates on it. Overloads pass, because the signature is the key. To reproduce the worst-case grouping, build with every file in one blob:

```bash
# in UE_MCP_Bridge.Build.cs, temporarily:
#   NumIncludedBytesPerUnityCPPOverride = 256 * 1024 * 1024;
Build.bat ue_mcpEditor Win64 Development -Project="...\ue_mcp.uproject" -DisableAdaptiveUnity
```

`-DisableAdaptiveUnity` matters: adaptive unity moves locally modified files out of their blob, which hides a collision from the person who introduced it.

## Dependencies

Runtime: `@modelcontextprotocol/sdk` (MCP), `ws` (WebSocket), `zod` (schemas), `@db-lyon/flowkit` (flow engine), `js-yaml` (config and flows), `isomorphic-git` (checkpoints), `@anthropic-ai/sdk` (flow agent steps when no extension provides a model).

Development: `typescript`, `tsx`, `vitest`, `husky`, `ajv`, `node-pty`.
