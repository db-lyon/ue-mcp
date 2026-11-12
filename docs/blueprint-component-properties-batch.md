# Batch component template properties

`blueprint(set_component_properties_batch)` writes several properties on
component templates across several Blueprint assets. Each Blueprint is compiled
once and saved once, even when several items address components in that same
Blueprint. Saves target that Blueprint's package; unrelated dirty packages are
not saved.

Use explicit items for a known set of assets:

```json
{
  "action": "set_component_properties_batch",
  "items": [
    {
      "assetPath": "/Game/AI/BP_Guard",
      "componentName": "Combat",
      "properties": {
        "AttackMontage": "/Game/Animation/AM_Attack.AM_Attack",
        "BlockMontage": "/Game/Animation/AM_Block.AM_Block",
        "FinisherMontage": null
      }
    },
    {
      "assetPath": "/Game/AI/BP_Raider",
      "componentName": "Combat",
      "properties": {
        "AttackMontage": "/Game/Animation/AM_Attack.AM_Attack",
        "BlockMontage": "/Game/Animation/AM_Block.AM_Block"
      }
    }
  ],
  "dryRun": true
}
```

Alternatively, apply a shared map to Blueprint assets under a directory:

```json
{
  "action": "set_component_properties_batch",
  "directory": "/Game/AI/Enemies",
  "classFilter": "/Script/Engine.Character",
  "componentName": "CharacterMovement",
  "properties": {
    "MaxWalkSpeed": 450,
    "bOrientRotationToMovement": true,
    "RotationRate.Yaw": 360
  },
  "recursive": true,
  "maxBlueprints": 200,
  "dryRun": false
}
```

`items` and directory selection are mutually exclusive. `classFilter`,
`componentName`, `properties` and `recursive` at the top level belong to directory
selection. `classFilter` accepts a native or Blueprint actor class and includes
subclasses. `recursive` defaults to true. The Asset Registry's Blueprint assets
are selected; maps and their level scripts are not enumerated.

`maxBlueprints` defaults to 200 and accepts 1 through 2000. Directory mode bounds
the candidate count before loading and class/component filtering. Overflow
refuses the call before any changes, rather than applying a truncated selection.
Explicit mode accepts up to 10000 items, subject to the Blueprint limit.

## Resolution and values

Component resolution is shared with `set_component_property`: own SCS templates,
inherited SCS templates through the child's InheritableComponentHandler (ICH),
then native default subobjects through the Blueprint's class default object.
Use the SCS variable name, instance name, or native component property name such
as `CharacterMovement`. An unambiguous component class name or class path is
also accepted as a fallback by both setters. A class matching several components
is refused; use a component name to disambiguate it.

Values use the single setter's parser: JSON numbers, booleans, strings, object
references, structs, arrays, sets, maps and Unreal export text. `null` clears
object/class/interface references. Dotted property names descend through structs,
such as `RotationRate.Yaw`. Skeletal mesh aliases use `SetSkeletalMeshAsset` so
the native skinned-asset state stays consistent.

## Validation, results and saves

Every Blueprint's items are validated on transient component copies before its
first edit. A failed property or missing explicit component refuses all items
for that Blueprint. Other Blueprints can still succeed. A read-only or protected
package is refused using the same write guard as the single setter, before any
template changes or ICH creation. Dry runs use this guard too.

`dryRun=true` validates values without editing templates, creating ICH overrides,
compiling or saving. It reports current `values` and `proposedValues`.

The response contains `results` in input order (sorted asset path order for
directory selection), `failed`, `skipped`, `blueprintsCompiled`,
`blueprintsSaved` and `dryRun`. Top-level `success` is false when any item fails.
Each item includes `index`, `assetPath`, `componentName`, `success`, `stage`,
`saved` and `values`. Resolved items also include `previousValues`,
`proposedValues`, `valuesText` and `inherited` when available. `values` contains
typed JSON read back from the template; `valuesText` contains Unreal export text.
Readback after compilation resolves fresh templates, since compilation can
replace the old class default object.

| Stage | Meaning |
| --- | --- |
| `selection` | Directory non-match, reported with `skipped=true` and `reason` |
| `load` | Blueprint could not be loaded |
| `write_guard` | Read-only or protected package |
| `resolve` | Explicit component missing or class selector ambiguous |
| `validate` | Malformed item, unknown property, or value parsing failure |
| `preflight` | Another item for this Blueprint failed before editing |
| `dry_run` | Validation succeeded; no changes or saves |
| `apply` | Assignment to the writable template failed |
| `compile` | Compiler reported an error; no save attempted |
| `readback` | Compiled value could not be read or differs from the proposed value |
| `save` | Targeted save failed; `error` states why |
| `finalize` | Another item failed after editing; this Blueprint was not saved |
| `complete` | Compilation, readback and targeted save succeeded |

A missing or ambiguous component in directory mode is a selection skip with
reason `component_not_found_or_ambiguous`; it does not fail the whole batch.
Compiler, readback and save failures can leave in-memory edits. Results make
those failures explicit with `saved=false`. There is no batch-wide transaction
or automatic rollback, and successful Blueprints remain saved if another fails.
Even an unchanged valid write compiles and saves its Blueprint once.

## Rollback

When templates were edited, the result includes:

```json
{
  "rollback": {
    "method": "set_blueprint_component_properties_batch",
    "payload": {
      "items": [
        {
          "assetPath": "/Game/AI/BP_Guard.BP_Guard",
          "componentName": "Combat",
          "properties": { "AttackMontage": "None" }
        }
      ],
      "maxBlueprints": 200
    }
  }
}
```

Flows replay `rollback.method` through the raw bridge task with `rollback.payload`.
For a manual tool call, use `blueprint`, `action=set_component_properties_batch`,
and the fields of `rollback.payload` as arguments. The payload includes touched items even when
compilation or saving failed. It retains empty values, null references and
structured map values. Dry runs and preflight refusals emit no rollback.

Rollback restores requested property values and saves them through the same
batch action. It does not remove a newly created ICH override, restore a prior
package dirty flag, reverse unrelated editor side effects, or restore disk
contents independently of existing unsaved changes in the target package.
