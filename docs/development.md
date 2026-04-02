# Development

## Prerequisites

- [Node.js](https://nodejs.org/) 18+
- Unreal Engine 5.4–5.7 (for live testing)
- A UE project to test against

## Setup

```bash
git clone https://github.com/db-lyon/ue-mcp.git
cd ue-mcp
npm install
```

## Building

```bash
npm run build          # esbuild → dist/
```

The build uses esbuild to compile TypeScript to JavaScript in `dist/`.

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
├── types.ts              # ToolDef, ActionSpec, categoryTool() factory
├── bridge.ts             # EditorBridge — WebSocket JSON-RPC client
├── project.ts            # ProjectContext — paths, INI, C++ parsing
├── deployer.ts           # Plugin deployment
├── editor-control.ts     # Editor process management
├── instructions.ts       # AI-facing server instructions
├── github-app.ts         # GitHub App auth for feedback submission
└── tools/                # 19 tool category implementations
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
        └── Handlers/          # 21 C++ handler groups

tests/smoke/               # Smoke tests (require live editor)
scripts/                   # Build and run scripts
docs/                      # Documentation (MkDocs Material)
```

## Testing

### Smoke Tests

Smoke tests run against a live editor and verify tool functionality end-to-end.

```bash
# All suites
npm test

# Specific suite
npm run test:level
npm run test:blueprint
npm run test:material
# ... etc for all 16 suites

# Full smoke test runner
npm run test:smoke
```

!!! note "Prerequisites"
    - Editor running with the test project
    - Bridge connected (`project(action="get_status")`)

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
| `gameplay` | Physics, collision, navigation |
| `audio` | Sound listing, playback |
| `niagara` | Niagara system inspection |
| `pcg` | PCG graph listing |
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

## Dependencies

### Runtime
- `@modelcontextprotocol/sdk` — MCP protocol implementation
- `ws` — WebSocket client
- `zod` — Schema validation

### Dev
- `typescript` — Type checking
- `tsx` — TypeScript execution (dev mode)
- `vitest` — Test runner
