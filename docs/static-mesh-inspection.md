# Static mesh geometry inspection

`asset.get_static_mesh_geometry` captures a StaticMesh LOD without building,
modifying or saving it. Choose the representation explicitly:

| `geometrySource` | Captured data |
| --- | --- |
| `sourceModel` | The authored source LOD's MeshDescription, before build scale, recomputed normals, lightmap UV generation or reduction. A missing description is an error, including a generated LOD with only render data. |
| `renderData` | The built CPU render LOD buffers. For a mesh with valid Nanite data this is the fallback render LOD, not Nanite cluster geometry. Missing CPU buffers are errors. |

For a full comparison or local evidence file, use `dumpToFile` to avoid paging
every element through MCP. The existing `get_mesh_geometry` remains available for per-section render data
on static and skeletal meshes. This capture action supports static meshes only.

## Reading a capture

Start at offset zero without `snapshotId`:

```json
{
  "action": "get_static_mesh_geometry",
  "assetPath": "/Game/Props/SM_Prop",
  "geometrySource": "sourceModel",
  "elementType": "triangles",
  "pageSize": 512
}
```

Keep the returned `snapshotId`. Read all pages of each `elementType` table with
that ID, the same asset path, source and LOD. Continue using `nextOffset` until
`hasMore` is false. Start each subsequent table at offset zero with the same ID.
`elementOffset` is a table offset, not an element ID. IDs can be sparse; join by
ID, not array position.

The normal response `select`/`omit` projection can omit metadata on later pages
after retaining it from the first response.

| Table | Each element |
| --- | --- |
| `vertices` | `id`, `position` in asset-local centimeters. Includes unreferenced vertices. |
| `corners` | `id`, `vertexId`, `normal`, `tangent`, `binormalSign`, `color`, and `uvs` ordered by channel. Includes unreferenced source vertex instances. Render corner IDs are the render vertex IDs. |
| `triangles` | `id`, three `cornerIds` in original winding order, `groupId`, and `polygonId` (`-1` for render data). |

Every stored UV channel is returned. Source colors preserve the float RGBA
attribute without clamping, and presence does not prove the artist painted it.
Render colors preserve stored RGBA8 bytes normalized by 255 without sRGB
conversion. An absent render color buffer produces `color: null` and
`metadata.hasColorAttribute: false`, rather than invented white colors.
`metadata.renderColorVertexCount` reports the native render color buffer's
vertex count, including zero. It is `null` for `sourceModel` captures, which do
not inspect a render buffer. No
position welding, corner merging, winding reversal or attribute synthesis occurs.

`metadata.groups` describes source polygon groups or render sections. Source
group IDs are not material indices: each group reports its imported slot name
and every matching material slot index. Empty or ambiguous matches are preserved;
more than 16 matches is refused. Render groups carry their actual material index.
A render triangle outside every section retains `groupId: -1`.

Metadata also records material slot names and paths, the selected source LOD's
build settings (or null), Nanite settings, the custom complex collider path,
collision LOD, trace flags, double-sided geometry, profile and simple shape count.
It is a settings summary, not collision shape geometry, material graph contents,
or a capture of the referenced collider. Reflected settings use Unreal's JSON
property naming. Current settings do not prove which settings built the render
buffers. `sourceModel` addresses the numbered source models, not a separate
Nanite high-resolution source. The geometry contract covers standard static-mesh vertex and
corner attributes; custom MeshDescription attributes, edge hardness and polygon
boundary loops are outside this format. Triangle IDs, polygon IDs and group IDs
are retained for correlation.

## Exporting a complete snapshot

Use one call to capture and export all tables:

```json
{
  "action": "get_static_mesh_geometry",
  "assetPath": "/Game/Props/SM_Prop",
  "geometrySource": "sourceModel",
  "dumpToFile": true,
  "outputPath": "Evidence/SM_Prop-source.json"
}
```

Or include an existing `snapshotId` to export that exact capture after paging it.
Omit `elementType`, `elementOffset` and `pageSize`; export always includes every
table. Relative output paths resolve under Saved/ and cannot traverse outside it.
Explicit absolute paths are accepted. The destination must end in `.json` and
must not exist. Omitting `outputPath` selects
`Saved/UE_MCP/MeshGeometry/<snapshotId>.json`. There is no overwrite mode.

The JSON file contains the capture identity/revision/time, counts, full metadata,
`vertices`, `corners`, `triangles`, `exportedUtc` and a final
`exportComplete: true` marker. The MCP response contains a compact receipt with
the resolved output path, bytes, counts and identity, without geometry arrays.
`currentAssetStateChecked` distinguishes a fresh capture from a cached export.
The file remains usable after the in-memory capture expires.

Serialization uses a 64 KiB write buffer, one row of at most 16 Ki characters,
and one bounded metadata UTF-8 conversion of at most 1 MiB (256 Ki characters).
The snapshot's existing metadata string is reused, and no full-file JSON string
or second geometry copy is allocated. The file has a hard 1 GiB limit. A limit
or write/flush failure returns an error and removes the sibling `.partial`
staging file. If cleanup itself fails, `partialOutputPath` identifies the
remaining incomplete file; it is never reported as a completed export.
Serialization checks a 240-second deadline between bounded row batches and
refuses further output if exceeded; the action's bridge/client timeout is
300 seconds. An operating-system I/O call can still block independently.

The staging file is flushed and closed before publication. Windows uses an
atomic same-directory move that refuses replacement; POSIX uses an atomic
hard-link creation that refuses an existing name, then removes the staging
name. Filesystems that do not support that operation return
`export_publish_failed`; no copying fallback can expose a partial destination.
Concurrent destination creation is refused without replacing its contents.
Process termination may leave an unpublished `.partial` file. The complete
file publication is atomic; crash/power-loss durability still depends on the
filesystem. Parent directories created for the requested output are retained.

## Snapshot lifetime and limits

Each first call copies all three tables once. Subsequent pages never load or
re-read the asset: they remain valid representations of `capturedUtc` even after
an edit, rename, unload or deletion. `currentAssetStateChecked` is true only on
the initial capture. To inspect an edit, begin a new capture without `snapshotId`
and discard the old pages. Source and render captures are separate operations,
not an atomic pair.

`contentRevision` fingerprints the returned geometry and metadata for this
schema/engine, excluding capture time and ID. It is not an asset package revision
or a guarantee that current content still matches. `snapshotId` identifies the
immutable capture and must be used for pagination.

- A page contains at most 512 elements (default 256).
- A capture permits at most 1,000,000 elements per table, eight UV channels and
  128 MiB of retained data. Allocation is checked from counts before copying
  geometry. Limits refuse the whole capture; they never truncate loose elements
  or attributes. Use a smaller LOD when needed.
- Metadata permits 4,096 materials/groups and 256 Ki characters of serialized
  metadata. A metadata-limit error also returns no capture.
- The editor keeps at most four captures and 256 MiB in this cache, evicting the
  least recently read first. Building a new capture can temporarily add 128 MiB
  before eviction. Unreal's asset loading and lazy source-description cache are
  separate from these bridge limits.
- Captures expire after 300 seconds without a successful page read. Expired
  storage is reclaimed on the next inspection call; no UObject is retained.
- An expired, evicted, unknown or previous-process ID returns
  `snapshot_unavailable`; a mismatched path/source/LOD returns
  `snapshot_mismatch`. Neither condition silently starts a new capture.
- Nonzero offsets without an ID, invalid pages, missing source/render data,
  incomplete CPU buffers, non-finite attributes and invalid topology are errors.
  An in-progress mesh compile is refused instead of forcing compilation.

These are editor asset reads, not component instance colors, deformed geometry,
collision baking, texture residency, or proof that all Nanite triangles are in
the fallback mesh.
