# Mesh studio render integration handoff

Implementation is under `plugin/`, `src/`, `tests/` and `docs/`. The worker did not build, launch an editor, deploy, record live specs/goldens, run tests, commit or push. Existing unrelated worktree files were left in place.

## Required generated artifacts

The C++ registrations are the source of truth for `render_mesh_studio`, `get_mesh_studio_render` and `cancel_mesh_studio_render`. The TypeScript declarations use `specBp` and need a fresh recording before importing the server; otherwise `makeSpecBp` correctly rejects the missing recorded contracts. No hand-authored snapshot or generated-schema substitute was added.

After the lead deploys/builds the plugin and starts the repository test project with a real rendering RHI:

```sh
npm run specs:record
npm run specs:generate
npm run generate:metadata
npm run golden:record
npm run golden:record -- --connected
npm run coverage:behavioural
```

Review the generated diffs. `generate:metadata` also changes files outside this worker's writable scopes; the lead owns those changes. The recording commands contact the verified `tests/ue_mcp` editor. Never record against another project.

## Verification for the lead

```sh
npx tsc --noEmit
npm run test:unit
npm run test:live -- --files mesh-studio-render
npm run audit
```

The live file declares `render` and covers the server's wait path, PNG dimensions/channel type, assembly bounds, unchanged world/package state, partial failures, cancellation and unsafe ids. Native tests decode the actual RGBA pixels and require transparent background, opaque subject and lit colour, plus independent actor, world-context and dirty-package checks. They do not skip a failed capture.

Run `UE.MCP.Editor.MeshStudio.RenderAndClean` and `UE.MCP.Editor.MeshStudio.CancelAndClean` using the editor's normal Automation queue, for example the console command:

```text
Automation RunTests UE.MCP.Editor.MeshStudio
```

These two tests are latent and require real engine frames with a rendering RHI. The existing synchronous `editor(run_automation_tests)` handler drains commands while holding the game thread and cannot run them: `npm run test:automation` will abandon them. The lead must integrate an automation driver that yields real frames, or otherwise account for that runner limitation before merging. This worker did not change the shared runner or claim its full suite passes.

Check on the launcher engine specified for this lane. In addition to the automated cases, verify a skeletal mesh, texture-heavy meshes, all three capture modes, portrait/landscape aspects, a deliberately pre-dirtied map, PIE/map-change cancellation and shutdown while a job is warming up. Streaming virtual-texture pages are not separately verified. No runtime or compiler evidence was collected by the worker.

## PR template draft

### What changed

Added `editor(render_mesh_studio)` for batch PNG reference captures of meshes and assemblies under isolated studio lighting, plus progress/cancellation actions. The job waits across editor frames, returns per-item outcomes and verifies cleanup and an unchanged dirty-package set.

### Why

Icon reference capture needed transient mesh staging, lighting, framing, texture readiness and alpha reconstruction in project scripts. A separate preview world keeps capture actors and lights out of the open level.

### Verification

Worker verification is limited to source review, whitespace validation, the unity-collision audit and the source parameter-read audit (all eight render parameters plus jobId on the status/cancel actions; no opaque reads). Build, generated artifacts and test execution remain with the lead; enter the actual results here after running them.

A syntax-only TypeScript parse could not run because this isolated worktree has no installed `typescript` package. Dependencies were not installed, since that would write outside the permitted scopes.

### Notes for the release

Features: Added studio-lit batch mesh reference captures with assembly framing, transparent RGBA output, per-item manifests and verified preview cleanup.

Suggested conventional commit subject: `feat(editor): render isolated studio mesh references`
