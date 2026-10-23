# Native asset import, inspection and round-trip export

These workflows use the asset and material categories. Paths such as `/Game/Props/T_Normal.T_Normal` are object paths; imports take a disk `filePath` and a content folder `packagePath`.

## Texture batches

`asset(import_texture_batch)` accepts per-file presets and overrides:

```json
{
  "packagePath": "/Game/Props/Textures",
  "save": true,
  "dryRun": false,
  "items": [
    {"filePath": "C:/Art/T_Color.png", "preset": "color", "lodGroup": "Weapon"},
    {"filePath": "C:/Art/T_Normal.png", "preset": "normal", "lodGroup": "WeaponNormalMap"},
    {"filePath": "C:/Art/T_RMA.png", "preset": "masks", "lodGroup": "WeaponSpecular", "maxTextureSize": 2048}
  ]
}
```

The normal preset uses Normalmap compression, linear sampling and `flipGreenChannel=false`. The masks preset uses Masks compression and linear sampling. The color preset uses Default compression and sRGB. All presets disable green-channel flipping. No preset changes the LOD group or streaming setting unless supplied explicitly.

Each item can carry `name`, `packagePath`, `replaceExisting` (default true), `preset`, and `settings`. The supported settings are `compressionSettings`, `lodGroup`, `sRGB`, `neverStream`, `flipGreenChannel`, and `maxTextureSize`. They may also appear directly on the item. Precedence is preset, then `settings`, then direct fields. Compression and LOD names accept the engine prefixes `TC_` and `TEXTUREGROUP_`. Unknown enum names are refused. `maxTextureSize` is an integer from 0 to 32768; 0 leaves size selection to the source and platform settings.

The batch validates all entries before importing any. `dryRun=true` performs this preflight without importing or saving. Import-time failures can still leave a partial batch; inspect each item's `success`, `importedAssets`, `readBack` and `saved`. Saving occurs after settings are applied. Rollback deletes only newly created textures, subject to referencer checks. Replaced source pixels need a backup to restore.

`asset(set_texture_settings)` accepts one `assetPath` or 1-500 `assetPaths`, plus the same preset and settings. It preflights every texture before applying the common settings. `save` defaults false, preserving unsaved edits unless requested; `dryRun` defaults false. Each result includes `previousSettings`, `requestedSettings` and actual `readBack`. Dry-run read-back describes the unchanged asset. Its rollback restores the original settings, using the same save choice.

`asset(get_texture_info)` accepts the same single or batch selector. It reports `maxTextureSize`, compression, LOD group, sRGB, streaming, green-channel flipping, dimensions, pixel format and mip count.

## Skeletal imports and mesh summaries

```json
{
  "filePath": "C:/Art/SK_Prop.fbx",
  "packagePath": "/Game/Props",
  "name": "SK_Prop",
  "importer": "interchange",
  "importUniformScale": 100,
  "replaceExisting": true,
  "importMaterials": false,
  "importTextures": false,
  "createPhysicsAsset": false,
  "save": true
}
```

Use this argument object with `asset(import_skeletal_mesh)`. `importer=auto` selects Interchange on 5.8 and later and the legacy FBX importer on earlier versions. Interchange receives an explicit pipeline stack containing scale, mesh type, skeleton and material/texture settings. An unavailable pipeline class or property produces an error before import. The pipeline stack uses the import task's options field, as described in the [engine API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/InterchangeEngine/UInterchangePipelineStackOverrid-).

Set `skeletonPath` to target an existing skeleton; omit it to request a new one. A supplied but missing skeleton is an error. `importMorphTargets` defaults true, `createPhysicsAsset` false, and material/texture import true. `save` defaults false. `dryRun=true` validates the source, destination and options without importing; it does not predict geometry or bounds.

Results include every imported asset and `meshes`, whose entries contain bounds, material slots, skeleton and sockets. The existing `boxExtent`, `boundsRadius`, `morphTargets`, `numLODs` and `skeleton` fields describe the first imported mesh. Rollback deletes only newly imported assets, subject to referencer checks. Import may change companion assets or shared skeletons; those edits and replaced assets require backup restoration.

`asset(get_mesh_info)` already summarizes static and skeletal meshes. It now also includes `sockets` and `socketCount`, using the same mesh-local and skeleton socket details as `asset(list_sockets)`.

For source versus built render geometry, use [static mesh inspection](static-mesh-inspection.md).
`asset(get_static_mesh_geometry)` captures all standard corner attributes and pages an immutable snapshot, including UV seams, colors and material groups. Set `dumpToFile=true` to export the complete snapshot in one call to a new JSON file.

Assigning another skeleton to an existing skeletal mesh is already covered by `asset(set_property)`:

```json
{
  "assetPath": "/Game/Props/SK_Prop.SK_Prop",
  "propertyName": "Skeleton",
  "value": "/Game/Props/NewSkeleton.NewSkeleton",
  "save": true
}
```

This assigns the reference; it does not retarget geometry or reconcile bone hierarchies. Read the mesh summary afterward. To create a skeleton from an existing mesh, use `animation(create_skeleton)`.

## Export for a DCC round-trip

`asset(export)` already exports static or skeletal meshes to an `.fbx` output path. `asset(export_texture)` already writes a Texture2D to PNG. `asset(export)` now writes MaterialInstance parameters to `.json`, including parent lineage, scalar/vector/texture overrides and effective inherited values. Layer associations and parameter indexes remain in the JSON.

For example, export `/Game/Props/MI_Prop.MI_Prop` with `outputPath="Saved/Export/MI_Prop.json"` and `format="json"`. Relative paths resolve against the project directory. `replaceExisting` defaults true; false refuses an existing file. `dryRun=true` on `asset(export)` validates the path and asset-specific format rules without writing. Registered exporter availability is checked only when exporting. Results report the output path, class and file size, plus the exported material data in `readBack`. The source asset is unchanged; overwritten disk files need a backup to restore.

## Asset Registry queries

`asset(list)` supports these additions while retaining cursor pagination:

```json
{
  "directory": "/Game",
  "classPaths": ["/Script/Engine.AnimSequence", "/Script/Engine.AnimMontage"],
  "recursiveClasses": true,
  "nameRegex": "pick|grab|loot|search",
  "regexIgnoreCase": true,
  "includeMetadata": true,
  "limit": 100
}
```

The name regex is limited to 512 characters. Use conventional regular-expression syntax; the server checks its syntax and the editor matches it with its regex implementation. `classFilter` retains its existing substring behavior and combines with `classPaths` when both are supplied. `includeMetadata` adds `metadata.skeletonPath` and `metadata.playLength` (seconds, or null when the registry has no length tag). These are registry tags, without loading each animation. They can lag unsaved edits.

Find skeletal meshes sharing a skeleton by using `classPaths=["/Script/Engine.SkeletalMesh"]` and `skeletonPath`. Package paths and object paths for the skeleton are accepted. All filters participate in cursor identity, so a cursor cannot silently resume a different query.

`asset(get_referencers)` already accepts multiple `packages`. Add `includeDetails=true` to report Blueprint paths, maps, external actor packages and reference depth. `maxDepth` defaults 2 and ranges from 1 to 8; only Blueprint referencers are followed beyond the direct references. Each package appears once through one shortest route. `includeMeshInfo=true` adds mesh summaries and static mesh collision setup to `detailsByPackage`.

The query uses saved hard and soft package references. Save newly created or changed Blueprints with `asset(save)` before querying; compiling a Blueprint does not persist its package dependencies. Runtime-only references and unsaved changes are not included. Standard map external-actor folders are resolved against registered World assets; unmatched external actor packages are reported as `unresolvedExternalActors`. Traversal caps are reported as `truncated`; the requested depth also limits coverage. No map needs to be loaded.

## Parameterized master materials

Master-material authoring is already covered by `material(create)`, `add_expression`, `set_expression_value`, `connect_expressions`, `connect_to_property`, `recompile`, `create_instance`, and `batch_set_instances`. `build_material` can also build a parameterized master and its instances in one call (`textureParameters`, `parameters`, `nodes`, `instances`); the step-by-step recipe below gives control over each node.

Create the material first, then add these nodes. Give each node the listed `name` and use `parameterName` on parameter nodes. Keep each returned `expressionIndex` for setting defaults.

| Name | expressionType | Default or setting |
| --- | --- | --- |
| BaseColor | TextureSampleParameter2D | Color texture; SamplerType `SAMPLERTYPE_Color` |
| Normal | TextureSampleParameter2D | Normal texture; SamplerType `SAMPLERTYPE_Normal` |
| RMA | TextureSampleParameter2D | Linear packed texture; SamplerType `SAMPLERTYPE_Masks` |
| Tint | VectorParameter | defaultValue `{r:1,g:1,b:1,a:1}` |
| UVScale | ScalarParameter | defaultValue 1 |
| RoughnessScale | ScalarParameter | defaultValue 1 |
| MetallicScale | ScalarParameter | defaultValue 1 |
| UV | TextureCoordinate | First UV channel |
| ScaledUV | Multiply | UV to A, UVScale to B |
| TintedColor | Multiply | BaseColor to A, Tint to B |
| ScaledRoughness | Multiply | RMA R to A, RoughnessScale to B |
| ScaledMetallic | Multiply | RMA G to A, MetallicScale to B |

For each texture node, call `set_expression_value` with `texturePath`. Set its sampler in a separate call using `propertyName="SamplerType"` and `value="SAMPLERTYPE_Color"`, `"SAMPLERTYPE_Normal"` or `"SAMPLERTYPE_Masks"`. This reflection setter requires the enum identifier, including its prefix. Virtual textures require the corresponding variant, such as `SAMPLERTYPE_VirtualColor`.

Connect ScaledUV to the Coordinates input of all three texture nodes. Connect TintedColor to BaseColor, Normal's RGB output to Normal, ScaledRoughness to Roughness, ScaledMetallic to Metallic, and RMA's B output to AmbientOcclusion. This channel order is RMA; ORM uses a different order. Recompile, then verify using `read_graph` and `list_parameters`.

Create each instance with `parentPath` pointing to the master. Assign textures with `set_parameter` or a single `batch_set_instances` call:

```json
{
  "instances": [{
    "assetPath": "/Game/Props/MI_Prop.MI_Prop",
    "parameters": [
      {"name": "BaseColor", "type": "texture", "value": "/Game/Props/Textures/T_Color.T_Color"},
      {"name": "Normal", "type": "texture", "value": "/Game/Props/Textures/T_Normal.T_Normal"},
      {"name": "RMA", "type": "texture", "value": "/Game/Props/Textures/T_RMA.T_RMA"},
      {"name": "Tint", "type": "vector", "value": {"r": 0.8, "g": 0.7, "b": 0.6, "a": 1}},
      {"name": "UVScale", "type": "scalar", "value": 2},
      {"name": "RoughnessScale", "type": "scalar", "value": 0.9},
      {"name": "MetallicScale", "type": "scalar", "value": 0}
    ]
  }]
}
```

Read the resolved values with `material(read_instance)`. These existing mutations save their results and return their own rollback descriptors; they do not offer a combined dry run for the entire graph recipe.
