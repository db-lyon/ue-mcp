# Mesh studio render integration handoff

The TypeScript surface and editor-down golden include the studio render actions. The handler-spec entries were prepared from the C++ declarations for offline validation; the lead must compare them with a fresh live recording after building the plugin.

## Verify the native contract

After building the plugin and starting the repository test project with a rendering RHI, run:

```sh
npm run specs:record
npm run specs:generate
npm run generate:metadata
npm run golden:record
npm run golden:record -- --connected
npm run coverage:behavioural
```

Review every generated diff against this branch. The `items` key is shared with the existing dialog-response action: the category schema advertises both typed array shapes and each action validates its own contract. Count markers in files outside the batch worker's writable scopes still require the lead's review.

## Rendering verification

```sh
npm run test:live -- --files mesh-studio-render
```

The live file declares `render` and covers the server wait path, PNG dimensions and channel type, assembly bounds, world and package state, partial failures, cancellation and unsafe ids. Native tests decode the actual RGBA pixels and require transparent background, opaque subject and lit colour, plus independent actor, world-context and dirty-package checks.

Run the following prefix through the editor's normal Automation queue with a rendering RHI:

```text
Automation RunTests UE.MCP.Editor.MeshStudio
```

This includes `RenderAndClean` and `CancelAndClean`. Both tests are latent and require real engine frames. The synchronous `editor(run_automation_tests)` handler holds the game thread while draining commands, so `npm run test:automation` abandons these tests. Use a driver that yields real frames.

Also verify a skeletal mesh, texture-heavy meshes, all three capture modes, portrait and landscape aspects, a pre-dirtied map, PIE or map-change cancellation and shutdown during warmup. Streaming virtual-texture page convergence remains outside the checks.

Native compilation, real rendering and connected golden verification remain with the lead. Offline command results are recorded in the untracked PR draft and validation report.
