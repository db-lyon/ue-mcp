import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";
import { Vec3, Rotator } from "../schemas.js";
import { CURSOR_PARAM, paged } from "../pagination.js";

export const foliageTool: ToolDef = categoryTool(
  "foliage",
  "Foliage instances, types, procedural spawners, sampling and settings. Every FoliageType tunable (Density, Radius, ScaleX/Y/Z, AlignToNormal, GroundSlopeAngle, CullDistance, CastShadow) is a plain property: use set_settings, batch_set_settings_where or asset(set_property), not a typed action. Grass on landscape layers is authored elsewhere and deliberately has no action here: material(add_expression, expressionType=\"LandscapeGrassOutput\") adds the slot node, material(list_expressions) reads it back, asset(create_asset_by_class, className=\"LandscapeGrassType\") makes the grass type, and asset(set_property) fills its GrassVarieties.",
  {
    list_types:    bp("read", paged("List foliage types in the level, one row per type per InstancedFoliageActor (foliageActorPath says which), sorted by that pair."), "list_foliage_types"),
    get_settings:  bp("read", "Read foliage type settings. Params: foliageTypeName", "get_foliage_type_settings"),
    sample:        bp("read", "Query instances in region. Params: center, radius, foliageType?", "sample_foliage"),
    create_type:   bp("mutate", "Create foliage type from mesh. Params: meshPath, name?, packagePath?", "create_foliage_type"),
    set_settings:  bp("mutate", "Modify ONE foliage type's settings. For the same edit across every type matching a condition, use batch_set_settings_where instead of calling this in a loop. Params: foliageTypeName OR foliageTypePath, settings", "set_foliage_type_settings"),
    batch_set_settings_where: {
      kind: "bridge",
      effect: "mutate",
      description: "Write settings to every FoliageType whose CURRENT property values match a predicate, with the predicate evaluated in the editor. set_settings is one asset per call and cannot express a condition at all, so 'include_in_hlod=false on every type whose cull distance is set' meant reading 201 assets out, computing the 36 matches client-side, and writing them back one at a time. Candidates come from foliageTypePaths[] (explicit), directory (scan FoliageType assets) or fromLevel=true (types placed in the open level). Predicate fields read the asset's own properties by dotted path, so CullDistance.Max works and a bare name is treated the same as props.<name>; operators are the same vocabulary as level(query_components). dryRun DEFAULTS TO TRUE because this writes to many assets at once, and the preview still resolves every setting path so an unwritable property is reported before the commit. Written values are read back rather than echoed, so a value the property coerced is visible. The inverse is asset(bulk_set_properties) carrying the previous value of every property on every type written, which is the only action that takes DIFFERENT values per asset - two matched types can have had different previous values for the same setting. It is marked lossy when a matched FoliageType lives inside a level rather than as an asset (no assetPath addresses it), and omitted entirely above that action's 500-item batch limit, with the reason stated rather than a record that would restore part of the write. Params: settings (object of propertyName -> value, dotted paths supported), where ([{field, op, value}], required), whereMode? (all|any), one of foliageTypePaths[]/directory (+ recursive?)/fromLevel, propertyNames? (extra properties to report without filtering on them), dryRun? (default TRUE), save? (default true), maxTypes? (#988)",
      bridge: "batch_set_foliage_settings_where",
      timeoutMs: 300_000,
      mapParams: (p) => ({
        settings: p.settings, where: p.where, whereMode: p.whereMode,
        foliageTypePaths: p.foliageTypePaths, directory: p.directory, recursive: p.recursive,
        fromLevel: p.fromLevel, propertyNames: p.propertyNames,
        dryRun: p.dryRun, save: p.save, maxTypes: p.maxTypes,
      }),
    },
    add_instances: bp("mutate", "PAINT foliage into the open level. Nothing else on this surface could place a single instance, so create_type produced an asset that never appeared anywhere. Two modes: transforms[] places each entry exactly, center+radius+count scatters uniformly over a disc. Each candidate is traced onto the geometry beneath it (projectToGround, default true) and then run through the FoliageType's OWN placement rules (applyTypeRules, default true) so the random scale between ScaleX/Y/Z, the random yaw and pitch, align-to-normal within AlignMaxAngle, the Z offset and the CollisionWithWorld test all apply - which is what makes a painted instance behave like one painted by hand. A candidate that hits nothing, or that the type's ground-slope or height range rejects, is reported in skippedCandidates with the reason rather than silently dropped. The type is added to the level's palette if it is not there already. Returns the InstancedFoliageActor it wrote to, and rolls back by removing exactly the transforms it placed. Params: foliageTypePath, transforms? (array of {location,rotation?,scale?}), center?, radius?, count?, seed?, projectToGround?, traceUp?, traceDown?, applyTypeRules?, skipCollision?", "add_foliage_instances"),
    remove_instances: bp("mutate", "Remove foliage instances by index, by exact transform, inside a sphere, or all of a type. The rollback captures each removed instance's world transform and replays it as an add, so this is recoverable up to 2000 instances; the base component an instance was painted onto is not restored. dryRun reports what would go without touching anything. Scope to one InstancedFoliageActor with actorPath when a World Partition map holds the same type on several. Params: foliageTypePath, instanceIndices? (int array from get_instances), transforms? (match by location within matchTolerance), center?, radius?, all?, matchTolerance?, actorPath?, dryRun?", "remove_foliage_instances"),
    get_instances: bp("read", "Read placed foliage instances with their world transforms, paged. sample only ever returned counts, so there was no way to verify a paint or find what to remove. Filter by type and/or by a sphere; index is the position inside the named actor's info for that type, which is exactly what remove_instances takes. Params: foliageTypePath?, center?, radius?, limit?, startIndex?, includeTransforms?", "get_foliage_instances"),
    add_type_to_level: bp("mutate", "Put a FoliageType asset into the open level's palette, allocating the instanced component that holds its instances. create_type only makes the asset; until this runs the type is invisible to the level and to the foliage editor. Idempotent: a type already in the level reports existed and allocates nothing. Params: foliageTypePath", "add_foliage_type_to_level"),
    remove_type_from_level: bp("mutate", "Take a FoliageType out of the open level's palette. REFUSES while the type still has instances unless force=true, because removing it destroys them and their transforms are not captured - remove them first with remove_instances, whose rollback does capture them. The FoliageType asset itself is untouched; asset(delete) deletes it. Params: foliageTypePath, force?", "remove_foliage_type_from_level"),
    read_spawner: bp("read", "Read a ProceduralFoliageSpawner: its tile settings, the foliage types it will spawn resolved to real asset paths, and every volume in the open level bound to it with its bounds and whether it has produced anything. The volume half is the part nobody can read off the asset and the usual reason a simulation appears to do nothing. Returns objectPath for the scalar tunables, which are plain properties. Params: spawnerPath", "read_procedural_foliage_spawner"),
    set_spawner_types: bp("mutate", "Set which foliage types a ProceduralFoliageSpawner spawns. This needs an action rather than a property write: FoliageTypes is a private array whose element object pointer is private too, and every write has to be followed by RefreshInstance or the cached type the simulation actually reads stays stale. Validates every path before writing anything, reports unchanged when the list already matched, reads the result back rather than echoing it, and rolls back to the previous list. Params: spawnerPath, foliageTypePaths, mode? (replace|add|remove), save?", "set_procedural_foliage_spawner_types"),
    simulate_procedural: {
      kind: "bridge",
      effect: "mutate",
      description: "Run the procedural foliage simulation and place what it generates. Address one volume with actorLabel or actorPath, or every volume in the open level bound to one spawner with spawnerPath. Validates every target first (spawner set, type list non-empty, volume bounds non-zero) so a half-simulated set of volumes is not possible, then generates desired instances and traces and places each one under the same type rules as add_instances. Reports generated, placed and skipped per volume. Place the volume itself with level(spawn_volume, volumeType=\"ProceduralFoliageVolume\"), which builds the cube brush a bare spawn leaves empty. Params: actorLabel OR actorPath OR spawnerPath, clearExisting?, skipCollision?",
      bridge: "simulate_procedural_foliage",
      // The C++ side registers 600s for this: a tile simulation over a
      // large volume plus a world trace per generated point outlives the
      // 30s default, and a client that gives up first reports a failure
      // for work the editor went on to finish.
      timeoutMs: 600_000,
      mapParams: (p) => ({ actorLabel: p.actorLabel, actorPath: p.actorPath, spawnerPath: p.spawnerPath, clearExisting: p.clearExisting, skipCollision: p.skipCollision }),
    },
    clear_procedural: bp("mutate", "Remove every instance a procedural foliage component spawned, matched on the component's own procedural GUID so hand-painted instances of the same type are left alone. Reports alreadyRemoved when the component had produced nothing. The inverse re-runs the simulation, which reproduces the cleared content only while the spawner's seed, tile settings and type list are unchanged. Params: actorLabel OR actorPath OR spawnerPath", "clear_procedural_foliage", (p) => ({ actorLabel: p.actorLabel, actorPath: p.actorPath, spawnerPath: p.spawnerPath })),
  },
  undefined,
  {
    foliageTypePath: z.string().optional().describe("Full FoliageType asset path"),
    foliageTypePaths: z.array(z.string()).optional().describe("batch_set_settings_where: explicit FoliageType asset paths (#988)"),
    directory: z.string().optional().describe("batch_set_settings_where: content directory to scan for FoliageType assets (#988)"),
    recursive: z.boolean().optional().describe("batch_set_settings_where: scan directory recursively (default true)"),
    fromLevel: z.boolean().optional().describe("batch_set_settings_where: take the foliage types placed in the open level (#988)"),
    where: z.array(z.object({
      field: z.string().describe("Dot path into the asset's own properties, e.g. CullDistance.Max or props.CullDistance.Max"),
      op: z.string().optional().describe("eq (default), ne, lt, lte, gt, gte, contains, notContains, startsWith, endsWith, in, notIn, exists, notExists, isNull, isNotNull, isTrue, isFalse"),
      value: z.unknown().optional(),
    })).optional().describe("batch_set_settings_where: predicate over each type's current values, evaluated in the editor (#988)"),
    whereMode: z.string().optional().describe("batch_set_settings_where: all (default) or any"),
    propertyNames: z.array(z.string()).optional().describe("batch_set_settings_where: extra properties to report on each row without filtering on them"),
    dryRun: z.boolean().optional().describe("batch_set_settings_where: preview without writing (defaults to TRUE)"),
    save: z.boolean().optional().describe("batch_set_settings_where: save each changed FoliageType package (default true)"),
    maxTypes: z.number().optional().describe("batch_set_settings_where: refuse to scan more than this many types (default 2000)"),
    foliageTypeName: z.string().optional(),
    foliageType: z.string().optional(),
    center: Vec3.optional(),
    radius: z.number().optional(),
    count: z.number().optional(),
    density: z.number().optional(),
    meshPath: z.string().optional(),
    name: z.string().optional(),
    packagePath: z.string().optional(),
    settings: z.record(z.unknown()).optional(),

    // Instance placement and removal (V12).
    transforms: z.array(z.object({
      location: Vec3,
      rotation: Rotator.partial().optional(),
      scale: Vec3.optional(),
    })).optional().describe("add_instances: place one instance per entry at exactly this transform. remove_instances: remove the instance nearest each location, within matchTolerance."),
    seed: z.number().optional().describe("add_instances: random seed for scatter placement, so a rerun places the same points (default 0)."),
    projectToGround: z.boolean().optional().describe("add_instances: trace each candidate down onto the geometry beneath it (default true). false traces only a centimetre, which places on whatever is exactly at the point."),
    traceUp: z.number().optional().describe("add_instances: how far above each candidate the ground trace starts, in centimetres (default 10000)."),
    traceDown: z.number().optional().describe("add_instances: how far below each candidate the ground trace ends, in centimetres (default 100000)."),
    applyTypeRules: z.boolean().optional().describe("add_instances: run the FoliageType's own placement rules - random scale, random yaw and pitch, align to normal, Z offset, ground slope, height range, CollisionWithWorld (default true). false places the raw transform."),
    skipCollision: z.boolean().optional().describe("add_instances / simulate_procedural: skip the type's CollisionWithWorld test, so instances may overlap existing geometry (default false)."),
    instanceIndices: z.array(z.number()).optional().describe("remove_instances: instance indices from foliage(get_instances). Valid only while nothing else edits that type on that actor."),
    all: z.boolean().optional().describe("remove_instances: remove every instance of the named type in the open level."),
    matchTolerance: z.number().optional().describe("remove_instances: how close an instance has to be to a transforms[] location to count as a match, in centimetres (default 1)."),
    actorPath: z.string().optional().describe("Object path of an InstancedFoliageActor (remove_instances) or of a ProceduralFoliageVolume (simulate_procedural / clear_procedural)."),
    actorLabel: z.string().optional().describe("Editor label of a ProceduralFoliageVolume, or of any actor carrying a ProceduralFoliageComponent."),
    limit: z.number().optional().describe("get_instances: maximum instances to return (default 200, max 20000). list_types: rows on this page (default 200, max 2000)."),
    // list_types resumes on a cursor. `limit` is already declared above.
    cursor: CURSOR_PARAM,
    startIndex: z.number().optional().describe("get_instances: skip this many matching instances before returning any."),
    includeTransforms: z.boolean().optional().describe("get_instances: return each instance's location, rotation and scale (default true). false returns indices only."),
    force: z.boolean().optional().describe("remove_type_from_level: accept that removing the type destroys its instances, whose transforms are not recoverable."),

    // Procedural foliage (V12).
    spawnerPath: z.string().optional().describe("ProceduralFoliageSpawner asset path. asset(create_asset_by_class, className=\"ProceduralFoliageSpawner\") creates one."),
    mode: z.string().optional().describe("set_spawner_types: replace (default, the list becomes exactly foliageTypePaths), add, or remove."),
    clearExisting: z.boolean().optional().describe("simulate_procedural: remove the content the component spawned previously before simulating again (default true)."),
  },
);
