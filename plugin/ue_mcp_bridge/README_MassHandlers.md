# Mass entity config handler

`ensure_mass_entity_config` creates or idempotently extends a
`UMassEntityConfigAsset` without requiring a hard Mass module dependency in
the bridge. The handler resolves the Mass classes at runtime, so the bridge
continues to load when Mass is not enabled.

`read_mass_entity_config` is the corresponding read-only audit operation. It
returns `assetPath`, `assetClass`, `traitCount`, an ordered `traitClasses`
array, and ordered `traits` records. Each record contains `index`, `classPath`,
`className`, and a `properties` object. Object and soft-object properties are
reported as object paths, so a StateTree assignment can be verified exactly.

`read_state_tree` returns `schemaClass` and `schemaPath` plus `subTrees`. Each state record
contains ordered `tasks`, `enterConditions`, and `transitions`; node records
include `structType`, `instanceProperties`, and `nodeProperties` (including
`bConsideredForCompletion`). Transition records include `trigger`,
`transitionType`, `targetStateId`, `targetStatePath`, `bDelayTransition`,
`delayDuration`, and `delayRandomVariance`. The response also includes exact
`editorBindings` records with source/target struct IDs and paths.

## Skeletal mesh instancing build flag

`set_skeletal_mesh_optimize_for_instancing` edits only the requested skeletal
mesh asset's LOD build settings. Parameters are `assetPath`, required boolean
`enabled`, and either `lodIndex` (default `0`) or `allLods: true`. It validates
the mesh and LOD selection before mutation, is idempotent, calls `Modify()`
before changing settings, and saves only that mesh package. The response
contains `updated`, `saved`, `changedLods`, and ordered `lods[]` records with
`lodIndex`, `beforeOptimizeForInstancing`, `afterOptimizeForInstancing`, and
`changed`. Each record also includes complete `beforeBuildSettings` and
`afterBuildSettings` objects, including `bOptimizeForInstancing` and the
other UE 5.8 skeletal build flags.

`read_skeletal_mesh_build_settings` is the read-only counterpart. It accepts
the same LOD selection parameters and returns `assetPath`, `lodCount`, and
`lods[]` with the exact current `bOptimizeForInstancing` value and complete
`afterBuildSettings` object for each LOD.

Example:

```json
{
  "assetPath": "/Game/Mass/WorkerAI.WorkerAI",
  "onConflict": "update",
  "traits": [
    {
      "class": "/Script/MassAIBehavior.MassStateTreeTrait",
      "properties": {
        "StateTree": "/Game/Mass/WorkerAIStateTree.WorkerAIStateTree"
      }
    }
  ]
}
```

The MMO persistent-pawn validation config uses this exact four-trait list:

```json
{
  "assetPath": "/Game/MMOValidation/MassPersistentPawn/DA_MassPersistentPawnWorkerAI.DA_MassPersistentPawnWorkerAI",
  "onConflict": "update",
  "traits": [
    { "class": "/Script/MMOHumanoidMassRuntime.MMOHumanoidMassEntityTrait" },
    { "class": "/Script/MassNavMeshNavigation.MassNavMeshNavigationTrait" },
    { "class": "/Script/MassMovement.MassMovementTrait" },
    {
      "class": "/Script/MassAIBehavior.MassStateTreeTrait",
      "properties": {
        "StateTree": "/Game/MMOValidation/MassPersistentPawn/ST_MassPersistentPawnWorkerAI.ST_MassPersistentPawnWorkerAI"
      }
    }
  ]
}
```

Traits are ordered and unique. Existing assets may be extended only when the
requested list has the existing list as an exact prefix. Conflicting order,
duplicate classes, replacement, and removal are rejected. `onConflict` accepts
`skip`, `error`, or `update` (default). Trait properties use the bridge's
recursive JSON property conversion, including object and soft-object paths.

## Native asset validation

`validate_assets` performs synchronous validation through
`UEditorValidatorSubsystem::ValidateAssetsWithSettings`; it does not invoke a
console command or save assets. Use `assetPaths` (an array) or `assetPath` for
exact package/object paths, or retain the backward-compatible recursive
`directory` form (default `/Game/`). Explicit package paths must resolve to
exactly one asset; missing and ambiguous paths fail before validation starts.

The successful response includes `selectionMode`, `selection`,
`validationUsecase`, `requested`, `checked`, `valid`, `invalid`, `skipped`,
`warnings`, `unableToValidate`, `externalObjects`, `assetLimitReached`, and an
overall `result`. Ordered `assets[]` details include `objectPath`, package and
asset names, per-asset result, `errors`, `warnings`, and tokenized `messages`;
any non-asset tokenized messages are returned in `validatorMessages`.
