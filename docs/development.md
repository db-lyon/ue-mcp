# Development

## Prerequisites

- [Node.js](https://nodejs.org/) 18+
- Unreal Engine 5.4–5.8 (for live testing)
- A UE project to test against

## Setup

```bash
git clone https://github.com/db-lyon/ue-mcp.git
cd ue-mcp
npm install
```

## Resolving an Issue with Claude Code

From inside your `ue-mcp` clone:

```bash
npx ue-mcp resolve 16
```

This:

1. Fetches issue #16 from `db-lyon/ue-mcp` via `gh`.
2. Creates a `resolve/16` branch from `origin/main` in your current checkout.
3. Pipes a generated prompt (issue body + repo conventions) into `claude --print --dangerously-skip-permissions`.
4. Claude reads code, implements the fix, runs `npx tsc --noEmit`, and commits.
5. The script then pushes the branch and opens a PR against `db-lyon/ue-mcp`.

Requires `gh` and `claude` CLIs, write access to the repo (or a fork to push to), and you must run it from inside a `ue-mcp` clone - it operates on the working tree, not a temp clone.

## Building

```bash
npx tsc                # TypeScript -> dist/ (what the server ships as)
npm run build          # UE C++ plugin build (requires editor closed)
```

`npx tsc` emits the TypeScript server into `dist/`. `npm run build` is the C++ plugin build that runs Unreal's build tool against the test project and requires the editor to be closed first.

The build script only ever builds the `ue_mcpEditor` target against the bundled `tests/ue_mcp/ue_mcp.uproject`, and it refuses to start if that resolves anywhere else. It also passes `-NoEngineChanges`, so Unreal itself aborts the build and prints the offending file list if the build would overwrite a file that already exists under the engine tree. That is what keeps a test build from invalidating the outputs of a shared source engine you use for other work.

Engine selection order is `UE_MCP_TEST_ENGINE_ROOT`, then `UE_BUILD_TOOL_PATH`, then the default install locations. A pinned root that has no build tool is an error rather than a silent fallback to an engine you did not ask for.

Two optional environment variables tune the guard:

```powershell
# Roots the build and run scripts must never touch, whatever else is set.
$env:UE_MCP_PROTECTED_ENGINE_ROOTS = 'D:\UE-Primary;D:\UE-Release'

# Pin the engine used for test builds and editor launches.
$env:UE_MCP_TEST_ENGINE_ROOT = 'D:\UE-Test'

npm run build
```

`UE_MCP_PROTECTED_ENGINE_ROOTS` uses the platform path-list delimiter (`;` on Windows, `:` on macOS and Linux). A selected engine equal to or nested under a protected root is rejected, including when it arrives through `UE_BUILD_TOOL_PATH` or `UE_EDITOR_PATH`.

A fresh engine may need to create its engine-side outputs once. For that bootstrap only, set `UE_MCP_ALLOW_TEST_ENGINE_CHANGES=true` to drop `-NoEngineChanges`. Protected roots stay forbidden even with this opt-in, and the build prints a warning naming the variable so an opt-in left in your environment is visible. Use `npm run up:build` to chain stop-build-start during plugin iteration.

## Running

```bash
# Build and run
npm run up:build

# Run (assumes already built)
npm run up

# Dev mode (tsx, no build step)
npm run dev

# Direct
node dist/index.js C:/path/to/MyGame.uproject

# Interactive setup (also available via npx ue-mcp init)
node dist/index.js init C:/path/to/MyGame.uproject
```

## Project Structure

```
src/
├── index.ts              # Entry point, tool registration, MCP server
├── tools.ts              # ALL_TOOLS registry (consumed by index.ts and tests)
├── types.ts              # ToolDef, ActionSpec, categoryTool() factory
├── bridge.ts             # EditorBridge - WebSocket JSON-RPC client
├── project.ts            # ProjectContext - paths, INI, C++ parsing
├── deployer.ts           # Plugin deployment
├── editor-control.ts     # Editor process management
├── instructions.ts       # AI-facing server instructions
├── github-app.ts         # GitHub App auth for feedback submission (bot fallback)
├── auth.ts               # GitHub OAuth device flow + ~/.ue-mcp/auth.json token cache
├── init.ts / update.ts / resolve.ts / hook-handler.ts  # CLI subcommands
├── flow/                 # Flow engine (registry, loader, task factory, HTTP)
└── tools/                # <!-- count:tools -->26<!-- /count --> tool category implementations
    ├── project.ts
    ├── asset.ts
    ├── blueprint.ts
    ├── level.ts
    ├── material.ts
    ├── animation.ts
    ├── landscape.ts
    ├── pcg.ts
    ├── foliage.ts
    ├── niagara.ts
    ├── audio.ts
    ├── widget.ts
    ├── editor.ts
    ├── reflection.ts
    ├── gameplay.ts
    ├── statetree.ts
    ├── gas.ts
    ├── networking.ts
    ├── demo.ts
    └── feedback.ts

plugin/ue_mcp_bridge/     # C++ bridge plugin (deployed to UE projects)
└── Source/UE_MCP_Bridge/
    ├── UE_MCP_Bridge.Build.cs
    └── Private/
        ├── BridgeServer.cpp/.h
        ├── HandlerRegistry.cpp/.h
        ├── GameThreadExecutor.cpp/.h
        └── Handlers/          # 24 C++ handler groups

tests/smoke/               # Smoke tests (require live editor)
tests/unit/                # Pure-TypeScript unit tests (no editor needed)
scripts/                   # Build and run scripts
docs/                      # Documentation (MkDocs Material)
```

## Testing

### Unit Tests

Pure-TypeScript tests under `tests/unit/`. No editor required.

```bash
npm run test:unit
```

These also run in CI on every PR.

### Smoke Tests

Smoke tests run against a **live editor** and verify tool functionality end-to-end.

```bash
# Specific suite
npm run test:level
npm run test:blueprint
npm run test:material
# ... 16 suites total - see scripts in package.json

# All suites (Vitest)
npm test

# Full smoke test runner - exercises every registered handler
npm run test:smoke
```

!!! warning "Smoke tests require the test project"
    The smoke runner targets `tests/ue_mcp/ue_mcp.uproject` only. Real mutations execute against the connected editor (creating blueprints, deleting assets, modifying the level). **Never run smoke tests against a real project.** After connecting, the runner asks the editor which project it has open and aborts before sending anything if the answer is not `tests/ue_mcp`. Non-loopback hosts are refused outright.

!!! note "Prerequisites"
    - Editor running with the test project
    - Bridge connected (`project(action="get_status")` returns `editorConnected: true`)

#### How the harness finds the bridge

The editor binds a per-project port, not a fixed one, and publishes the port it
actually bound to `tests/ue_mcp/Saved/UE_MCP_Bridge/port.json`. Both harnesses
(`scripts/smoke-test.js` and the Vitest suites) resolve the endpoint through
`scripts/bridge-target.mjs`, which probes in this order:

1. The port recorded in that lockfile.
2. The port derived from the project path, which is what the bridge binds when
   nothing is in its way.
3. The legacy fixed port `9877`, for older bridges.

Start the editor and run the tests; no environment variable is needed. When
nothing answers, the failure names the lockfile path it read, the state that
file was in (missing, malformed, or stale with a dead pid), and every port it
tried. `--port` on the runner and `UE_MCP_TEST_PORT` for the Vitest suites still
pin the port for unusual setups, and neither weakens the project check above.

### Live tests

The live tests drives a real editor through the shipped server: the advertised
surface with an editor attached, per-path dispatch, addressing, gating, and the
records the bridge publishes.

```bash
npm run test:live
```

It attaches to an editor that is **already running** and never starts or stops
one. The preflight finds the bridge, asks the editor which project it has open,
prints the target, and aborts with the ports it tried when nothing answers, so
"no editor" is one message rather than a wall of failed assertions. Like the
smoke runner, it drives `tests/ue_mcp` and refuses every other project.

- **One editor is enough.** Cases that need more than one editor use a second
  session for a throwaway project whose editor is not running: a session is
  registered for every project regardless of editor state, and the session
  count is what arms targeting, gating and the union refusal.
- **The leak assertions need the parameter echo**, which can only be armed when
  the editor process starts. Launch the editor with `UE_MCP_PARAM_ECHO=1` in its
  environment to include them; without it they skip and the runner says so.
- `tests/live/matrix.ts` records, case by case, where each assertion lives, and
  `tests/live/coverage.test.ts` checks every reference still resolves.

### Test Suites

| Suite | What It Tests |
|-------|---------------|
| `level` | Actor CRUD, selection, components, volumes, lights |
| `asset` | Asset listing, search, CRUD, import |
| `blueprint` | BP reading, creation, graph editing, compilation |
| `material` | Material creation, parameters, instances |
| `editor` | Console, PIE, viewport, undo/redo |
| `reflection` | Class/struct/enum reflection, gameplay tags |
| `animation` | Anim BP, montages, skeletons |
| `landscape` | Landscape info, sculpting, painting |
| `gameplay` | Physics, collision, navigation, AI |
| `audio` | Sound listing, playback |
| `niagara` | Niagara system inspection and authoring |
| `pcg` | PCG graph listing and authoring |
| `foliage` | Foliage types |
| `widget` | Widget blueprint creation, tree manipulation, slot properties |
| `networking` | Replication config |
| `gas` | GAS component inspection |

## Adding a New Tool

### TypeScript Side

1. Create `src/tools/myfeature.ts`:

```typescript
import { categoryTool, bp, type ToolDef } from '../types.js';
import { z } from 'zod';

export const myfeatureTool: ToolDef = categoryTool(
  'myfeature',
  'Description of this tool category',
  {
    my_action: bp('my_cpp_handler_method'),
    local_action: {
      handler: async (ctx, params) => {
        // local implementation
        return { result: 'done' };
      },
    },
  },
  '- my_action: Does something. Params: foo, bar\n- local_action: Does something locally.',
  {
    foo: z.string().optional().describe('Description of foo'),
  },
);
```

2. Register it in `src/index.ts`.

### C++ Side

1. Create handler files in `plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/`
2. Register handlers in `BridgeServer.cpp`
3. Each handler receives `TSharedPtr<FJsonObject>` params and returns `TSharedPtr<FJsonValue>`

## C++ Plugin Development

The plugin source lives in `plugin/ue_mcp_bridge/`. When you modify C++ handler code:

1. Edit the source in `plugin/ue_mcp_bridge/`
2. The deployer copies the plugin to the target project on server start
3. In the editor, use **Live Coding** (Ctrl+Alt+F11) or `editor(action="hot_reload")` to reload

For a full editor restart: `editor(action="restart_editor")`

### File-local helpers and the unity build

UBT compiles a module as a **unity build**: several `.cpp` files are concatenated into one translation unit (`Module.UE_MCP_Bridge.2.cpp` and friends). An anonymous namespace is per translation unit, so two handler files can each define a file-local helper of the same name and both compile in isolation. When the grouping puts them in the same blob, the two anonymous namespaces merge and the second definition becomes a redefinition:

```
AssetHandlers_BulkUpsert.cpp(53,6): error C2084: function
'`anonymous-namespace'::IsProtectedAssetPath' already has a body
```

The grouping is not stable. It shifts with file count, file order, and the adaptive-unity working set, which UBT derives from `git status`. A duplicate can therefore build clean on the machine that wrote it and fail on the next machine to compile the same source.

When two handler files need the same helper, **put it in a shared header** (`Public/HandlerUtils.h` for anything asset- or actor-shaped) rather than copying it. Copies drift as well as collide: the guardrail this rule came from had four copies, two of which enforced weaker rules than the others.

```bash
npm run audit:unity      # duplicate file-local definitions, per module
```

The audit runs as part of `npm run audit` and as a unit test, so CI gates on it. Genuine overloads pass, because the signature is the key, not the name. To reproduce the worst-case grouping by hand, build with every file in one blob:

```bash
# in UE_MCP_Bridge.Build.cs, temporarily:
#   NumIncludedBytesPerUnityCPPOverride = 256 * 1024 * 1024;
Build.bat ue_mcpEditor Win64 Development -Project="...\ue_mcp.uproject" -DisableAdaptiveUnity
```

`-DisableAdaptiveUnity` matters: adaptive unity pulls locally-modified files out of their blob into standalone translation units, which is exactly what hides a collision from the person who just introduced it.

## Dependencies

### Runtime
- `@modelcontextprotocol/sdk` - MCP protocol implementation
- `ws` - WebSocket client
- `zod` - Schema validation

### Dev
- `typescript` - Type checking
- `tsx` - TypeScript execution (dev mode)
- `vitest` - Test runner
