# Native Static Mesh Usage Progress

Last updated: 2026-08-12

## Status

| Field | Value |
| --- | --- |
| Overall | Source implementation committed; native validation pending; draft PR awaiting review |
| Pull request | [db-lyon/ue-mcp#903](https://github.com/db-lyon/ue-mcp/pull/903) |
| Branch | `codex/native-static-mesh-usage` |
| Base | `db-lyon/ue-mcp:main` at `849a194` |
| Scope | `plugin/ue_mcp_bridge` only |
| Handler | `summarize_static_mesh_usage` |
| Mutation policy | Read-only |

## Goal

Provide a native replacement for the repetitive current-level workflow of
enumerating actors, reading each static mesh component, and aggregating mesh
properties through many bridge calls or an `execute_python` script. The raw
bridge handler performs the common inspection in one bounded game-thread request.
A typed server action is intentionally deferred because this change is limited
to the UE-MCP plugin folder.

## Completed

- [x] Register `summarize_static_mesh_usage` with the level handler registry.
- [x] Support exact `editor` and `pie` world selection, including `pieInstance`.
- [x] Scan loaded actors and all owned `UStaticMeshComponent` subclasses.
- [x] Count actors, components, and placements separately.
- [x] Count a plain static mesh component as one placement.
- [x] Count ISMC and HISM placements with `GetInstanceCount()`.
- [x] Preserve zero-instance components in component totals without adding placements.
- [x] Report null-mesh components diagnostically.
- [x] Sort results by placement count descending, then mesh path ascending.
- [x] Bound returned mesh rows to 1 through 500.
- [x] Bound occurrence examples to 0 through 256 across the response.
- [x] Fail closed above 8,192 unique loaded meshes.
- [x] Report full-scan totals even when result rows are truncated.
- [x] Author plugin-local Automation coverage for registration, world validation,
  aggregation, ordering, plain and ISMC placement semantics, null meshes, and bounds.
- [x] Complete independent static review against Unreal Engine 5.8 APIs.
- [x] Open a plugin-only draft pull request.

## Validation

| Check | Result | Evidence |
| --- | --- | --- |
| Local engine-free unit suite | Passed | 83 files, 840 tests |
| Local TypeScript type check | Passed | `npx tsc --noEmit` |
| Local unity collision audit | Passed | `npm run audit:unity` |
| Local em dash audit | Passed | `npm run audit:em-dash` |
| GitHub engine-free CI | Passed | Workflow run `31600014333`: 83 files and 840 unit tests, plus 4 files and 32 multi-editor tests |
| UE 5.8 API/static review | Passed | No P0 or P1 findings |
| Native Unreal compilation | Not yet proven | Guarded local build stopped before compilation because protected engine outputs would have changed |
| Native Automation execution | Not yet run | Requires a prepared Unreal test engine or upstream CI lane that compiles the plugin |

The GitHub CI runner has no Unreal installation. Its successful build job
exercises the repository's Node and TypeScript checks; it does not replace
native Unreal compilation or execution of the new Automation tests. The
workflow's `publish` and `surface_counts` jobs were skipped, not failed.

## Changed Files

- `Source/UE_MCP_Bridge/Private/Handlers/LevelHandlers.cpp`
- `Source/UE_MCP_Bridge/Private/Handlers/LevelHandlers.h`
- `Source/UE_MCP_Bridge/Private/Handlers/LevelHandlers_Inspection.cpp`
- `Source/UE_MCP_Bridge/Private/Tests/LevelStaticMeshUsageTests.cpp`
- `Source/UE_MCP_Bridge/UE_MCP_Bridge.Build.cs`
- `NATIVE_STATIC_MESH_USAGE_PROGRESS.md`

## Current Contract

Example parameters:

```json
{
  "world": "editor",
  "maxResults": 100,
  "includeOccurrences": false,
  "maxOccurrences": 32
}
```

The result is explicitly `loadedOnly`. It summarizes the selected loaded world
and makes no claim about unloaded World Partition actors. It performs no asset
loads, saves, or mutations.

## Remaining Work

- [ ] Compile the plugin in a prepared Unreal Engine test lane.
- [ ] Run `UE.MCP.Level.SummarizeStaticMeshUsage.*` Automation tests.
- [ ] Address any upstream review comments on PR #903. None exist as of the
  last-updated date.
- [ ] Decide separately whether to expose a typed server action outside the
  plugin folder. That is intentionally not part of this plugin-only change.
- [ ] Mark the PR ready for review after native validation or maintainer approval
  of the documented validation boundary.

## Update Rules

Keep this file current whenever implementation, validation, review state, or
scope changes:

1. Update the date and overall status.
2. Move finished items into `Completed` or check them in place.
3. Record exact validation commands and outcomes without overstating coverage.
4. Add newly discovered blockers or follow-up work under `Remaining Work`.
5. Keep all project-specific context outside this plugin progress record.

## History

- 2026-08-12: Native handler, focused tests, static review, engine-free validation,
  and draft PR completed. Native Unreal compilation remains pending.
