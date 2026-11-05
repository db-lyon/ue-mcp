# Collection and input editing

For a single nested field in a struct array, `asset(set_property)` accepts a key selector:

```json
{"action":"set_property","assetPath":"/Game/Data/DA_Weapons","propertyName":"WeaponProfiles[WeaponId=Rifle].DisplayName","value":"Service rifle","save":false}
```

The selector must match exactly one element. `asset(read_properties)` accepts the same path. See [Property paths](tool-reference.md#property-paths) for the selector rules. Use `asset(bulk_set_properties)` for several keyed field writes with preflight and read-back.

## Update or clone a collection entry

`asset(edit_collection_entry)` selects an existing struct with `entryKey`. For a struct array, also supply `keyField`. For a map, the key is converted using the map's reflected key type. Omit `newKey` to update the selected entry; supply an unused `newKey` to clone it. Existing destinations and ambiguous array keys are refused.

```json
{"action":"edit_collection_entry","assetPath":"/Game/Data/DA_Weapons","propertyName":"WeaponProfiles","keyField":"WeaponId","entryKey":"Rifle","newKey":"Carbine","overrides":{"DisplayName":"Carbine","Ammo.MagazineCapacity":5},"dryRun":true}
```

```json
{"action":"edit_collection_entry","assetPath":"/Game/Data/DA_Poses","propertyName":"Profiles","entryKey":"Weapon_Rifle","newKey":"Weapon_Carbine","overrides":{"Muzzle.Translation":{"X":0,"Y":82.5,"Z":-3.8}},"save":false}
```

Overrides accept nested objects and dotted paths through structs. Omitted fields retain their values; arrays supplied as overrides replace that field. Object references remain references. Cloning structs containing instanced subobjects is refused. The collection path must stay within the data asset or its owned subobjects; use the existing property actions for Blueprint defaults.

The whole edit is validated before mutation. `dryRun=true` returns `proposedValue` without changing or saving the asset. A write returns `value` read from the asset, its entry count, and persistence fields. `save` defaults to true. Its rollback restores the previous whole collection using the same save policy, so replay it before subsequent collection edits.

## Clone DataTable rows

Use `asset(read_datatable)` with `rowFilter` and `columns`, or `asset(get_datatable_row)` for a named row. `asset(clone_datatable_rows)` copies existing rows directly and applies field overrides without exporting the table:

```json
{"action":"clone_datatable_rows","assetPath":"/Game/Data/DT_Recipes","clones":[{"sourceRow":"Recipe_Rifle","rowName":"Recipe_Carbine","overrides":{"DisplayName":"Carbine","Count":5}},{"sourceRow":"Recycle_Rifle","rowName":"Recycle_Carbine"}],"save":false}
```

Every source must exist before the call; a later clone cannot use a destination created by the same batch. Every destination must be unused, including case-insensitive row-name collisions. Rows containing instanced subobjects are refused. The complete batch is preflighted before mutation. `dryRun=true` returns `proposedRows`; a write returns `rows` with actual fields, `clonedCount`, `rowCount`, and persistence fields. `save` defaults to true. Editor undo covers the transaction. Rollback calls `remove_datatable_row` with the cloned `rowNames` and the same `save` policy. It is marked lossy because row change hook side effects are not reversed. Replay it before further edits to the cloned rows. The removal action accepts either one `rowName` or an array of `rowNames`; batch or unsaved removals use editor undo to restore deleted values.

## Per-mapping input settings

`gameplay(read_imc)` reports each row's `mappingIndex`, `settingBehavior`, local `playerMappableKeySettings` and `effectivePlayerMappableKeySettings`. The local value is null when no override subobject exists. Effective settings follow `InheritSettingsFromAction`, `OverrideSettings`, or `IgnoreSettings`.

```json
{"action":"set_player_mappable_settings","imcPath":"/Game/Input/IMC_Vehicle","mappingIndex":0,"settingBehavior":"OverrideSettings","playerMappableKeySettings":{"Name":"ControllerSteering","DisplayName":"Controller steering","DisplayCategory":"Vehicle"},"save":false}
```

Supply reflected settings fields in `playerMappableKeySettings`, or use the existing `mappingName`, `displayName` and `displayCategory` parameters. Omitted fields retain their values. A new settings object needs a non-empty `Name` or `mappingName`. Set `playerMappableKeySettings` to null to clear the local object. Setting only the behavior preserves the local object. `dryRun` validates without writing; `save` defaults to true. Rollback addresses the same mapping index, so replay it before reordering mappings. Clearing an override may recreate its object on rollback; clearing a custom settings subclass requires editor undo.

InputAction-level settings use `inputActionPath` instead of `imcPath` and `mappingIndex`. To duplicate an InputAction together with its triggers, modifiers and settings, use `asset(duplicate)` with `sourcePath`, `destinationPath` and `onConflict="error"`, then inspect it with `gameplay(read_input_action)`.

## Static editor functions

`reflection(reflect_class)` describes a project's reflected functions. Call a static BlueprintCallable function with `editor(invoke_static_function)` using `className`, `functionName` and an `args` object keyed by reflected parameter names. The class can be a component or another reflected class, as well as a function library. Use the reflected C++ function spelling. Return and output parameters appear under `returnValues`. These calls can have arbitrary side effects; persistence and undo depend on the called function.
