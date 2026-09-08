import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";
import { Vec3 } from "../schemas.js";
import { PAGINATION_SCHEMA, paged } from "../pagination.js";

export const landscapeTool: ToolDef = categoryTool(
  "landscape",
  "Landscape terrain: info, layers, sculpting, weight painting, materials, splines, proxies.",
  {
    get_info:          bp("read", "Get landscape setup. Params: none", "get_landscape_info"),
    list_layers:       bp("read", "List paint layers. Params: none", "list_landscape_layers"),
    sample:            bp("read", "Read the surface height AND every paint-layer weight at one world XY. Height and weights come from the landscape's own height and weight data (the merged result of every edit layer), the same data landscape(paint_layer) writes, so they are exact and do not depend on built collision. Layers come back with weight (0..1), weight255 (the weightmap byte the editor shows), the LayerInfo path and its physical material, plus dominantLayer and totalWeight - which is what makes reading a paint weight one call instead of rendering the weightmap to a render target and reading that back. Also returns quad, quadExtent and inBounds, so a zero weight OFF the landscape reads as out of bounds rather than as a measured zero, and traceHeight/normal from a confirmation collision trace (absent when collision is unbuilt or the proxy is streamed out). Position accepts x + y, point {x, y}, or worldX + worldY. Params: x + y (or point, or worldX + worldY), actorLabel? OR actorPath? (which landscape, when the map has several), layerName? (one layer only), includeLayers? (default true) (#939)", "sample_landscape", (p) => ({ x: p.x, y: p.y, point: p.point, worldX: p.worldX, worldY: p.worldY, actorLabel: p.actorLabel, actorPath: p.actorPath, layerName: p.layerName, includeLayers: p.includeLayers })),
    sculpt:            bp("mutate", "Raise, lower or flatten terrain with a circular brush. mode=raise|lower|flatten; amount is world centimetres at full strength (raise/lower), and flatten pulls toward the height under the brush centre. falloff (0..1) is the smoothstepped soft edge. Runs in one transaction and leaves the level dirty and unsaved. Writes into an edit layer (editLayer by name, else editLayerIndex, default 0) - required on UE 5.8, where a write without one is regenerated away by the layer system. Idempotent: a brush that rounds to the height already there reports unchanged=true and updated=false rather than claiming it moved ground. The rollback record carries the exact previous heights over the brush rectangle and replays through set_height_region; above rollbackMaxVertices the response says rollbackOmitted with the reason instead of a record that would restore part of the footprint. Params: center ({x,y} world space), radius (default 500), mode? (default raise), amount? (default 100), falloff? (default 0.5), actorLabel? OR actorPath?, editLayer?, editLayerIndex?, maxVertices?, rollbackMaxVertices? (#742)", "sculpt_landscape", (p) => ({ center: p.center, radius: p.radius, mode: p.mode, amount: p.amount, falloff: p.falloff, actorLabel: p.actorLabel, actorPath: p.actorPath, editLayer: p.editLayer, editLayerIndex: p.editLayerIndex, maxVertices: p.maxVertices, rollbackMaxVertices: p.rollbackMaxVertices })),
    paint_layer:       bp("mutate", "Paint a weight layer with a circular brush. The layer must already have a LayerInfo on this landscape (see add_layer_info) - an unregistered name errors and lists the layers that ARE registered instead of silently painting nothing. Weights are written as given - the engine no longer renormalises other layers for you, so set them explicitly if they must sum to 1. Writes into an edit layer (editLayer by name, else editLayerIndex, default 0). Idempotent: a brush that changes no weight reports unchanged=true. The rollback record carries the exact previous weights over the brush rectangle and replays through set_layer_weight_region, with the same rollbackOmitted cap as the region writes. Params: layerName, center ({x,y} world space), radius (default 500), strength? (0..1, default 1), falloff? (default 0.5), actorLabel? OR actorPath?, editLayer?, editLayerIndex?, maxVertices?, rollbackMaxVertices? (#742)", "paint_landscape_layer", (p) => ({ layerName: p.layerName, center: p.center, radius: p.radius, strength: p.strength, falloff: p.falloff, actorLabel: p.actorLabel, actorPath: p.actorPath, editLayer: p.editLayer, editLayerIndex: p.editLayerIndex, maxVertices: p.maxVertices, rollbackMaxVertices: p.rollbackMaxVertices })),
    list_splines:      bp("read", "Read landscape splines. Params: none", "list_landscape_splines"),
    get_component:     bp("read", "Inspect component. Params: componentIndex", "get_landscape_component"),
    set_material:      bp("mutate", "Set landscape material. Params: materialPath", "set_landscape_material"),
    add_layer_info:    bp("mutate", "Register paint layer (creates LayerInfo asset + binds to active landscape). Idempotent: a layer already registered reports existed and emits no record, so a replay cannot delete a layer it did not create. Rolls back through remove_layer on the parent Landscape actor, which un-registers the layer; marked lossy when the LayerInfo ASSET was created here, because remove_layer deliberately leaves it on disk. Params: layerName, packagePath?, landscapeName?", "add_landscape_layer_info"),
    create_layer_info: bp("mutate", "Standalone LayerInfo asset creation - no landscape required. Params: layerName, name? (default LI_<layerName>), packagePath? (default /Game/Landscape/LayerInfos), physMaterial? (asset path), hardness? (#251)", "create_landscape_layer_info", (p) => ({ layerName: p.layerName, name: p.name, packagePath: p.packagePath, physMaterial: p.physMaterial, hardness: p.hardness, onConflict: p.onConflict })),
    create:            bp("mutate", "Spawn a new ALandscape with a flat heightmap. Defaults match the Editor's Landscape Mode 'create new' (8x8 components, 63 quads/subsection, 2 subsections/component = 1016x1016 quads). Params: location? (Vec3), scale? (Vec3, default 100,100,100), componentCountX? (default 8), componentCountY? (default 8), subsectionSizeQuads? (one of 7|15|31|63|127|255, default 63), numSubsections? (1|2, default 2), heightOffset? (uint16, default 32768 = mid-elevation), label? (#303)", "create_landscape", (p) => ({ location: p.location, scale: p.scale, componentCountX: p.componentCountX, componentCountY: p.componentCountY, subsectionSizeQuads: p.subsectionSizeQuads, numSubsections: p.numSubsections, heightOffset: p.heightOffset, label: p.label })),
    get_material_usage_summary: bp("read", "Per-proxy summary: landscape/hole material paths + component/grass/nanite counts. Params: none (#150)", "get_landscape_material_usage_summary"),
    list_proxies:      bp("read", paged("Enumerate loaded World Partition LandscapeStreamingProxy actors with per-proxy objectPath and worldBounds (origin/extent), sorted by object path, plus loadedProxies + parentLandscapes counts. Unloaded proxies are not spawned as actors, so only loaded ones appear - use this to confirm a proxy is streamed in before trusting a layer/height readback (#733)."), "list_landscape_proxies"),
    find_proxy_at:     bp("read", "Resolve which loaded LandscapeStreamingProxy covers a world X/Y. Returns found/loaded + label, or loaded:false when the covering proxy is streamed out (so a 0-weight readback there is ambiguous, not real). Params: worldX, worldY (#733)", "find_landscape_proxy_at", (p) => ({ worldX: p.worldX, worldY: p.worldY })),
    // ── V1 region surface ────────────────────────────────────────────────
    // Every one of these addresses the landscape as a RECTANGLE of vertices
    // rather than as a circular brush, because that is the shape a caller can
    // describe, verify and undo. The rectangle is spelled the same way in all
    // of them: region {minX,minY,maxX,maxY} in landscape vertex indices, the
    // same object with space "world" for centimetres, center + radius for the
    // brush form, or nothing at all for the whole landscape.
    get_height_region: {
      ...bp("read", "Read the raw uint16 heights over a rectangle of landscape vertices, with min/max/mean reported in raw units AND in world Z so the numbers mean something without a second call. Heights come back as a plain array below arrayEncodingLimit and as a little-endian base64 blob above it, and that blob feeds straight back into set_height_region, so a read-modify-write round trip needs no reformatting. Reports hasUnloadedComponentsInRegion, because on a World Partition map an unloaded component reads as absent rather than as flat ground. Reads the merged surface unless editLayer or editLayerIndex names one, in which case it reads that layer's own contribution. Params: actorLabel?, actorPath?, region?, space?, center?, radius?, maxVertices?, editLayer?, editLayerIndex?, includeHeights?, encoding?, arrayEncodingLimit?", "get_landscape_height_region"),
      timeoutMs: 120_000,
    },
    set_height_region: {
      ...bp("mutate", "Write heights over a rectangle of landscape vertices. Accepts heightsBase64 (the blob get_height_region hands back), heights as one number per vertex row-major from (minX,minY), or height / rawHeight to fill the whole rectangle with one value; heightSpace picks whether the numbers are raw uint16 (32768 = the actor's own Z) or world centimetres. Idempotent: a write that changes nothing reports unchanged true and updated false rather than claiming an edit. The rollback record carries the exact previous heights, and when the rectangle is above rollbackMaxVertices the response says rollbackOmitted with the reason instead of emitting a record that would restore only part of it. Requires an edit layer, since a write with none is regenerated away by the layer system. Params: actorLabel?, actorPath?, region?, space?, center?, radius?, maxVertices?, heightsBase64?, heights?, height?, rawHeight?, heightSpace?, editLayer?, editLayerIndex?, rollbackMaxVertices?", "set_landscape_height_region"),
      timeoutMs: 300_000,
    },
    get_height_at_point:  bp("read", "Surface height under one world XY, as both the raw uint16 and the world Z, plus the landscape vertex it was read at. Reads the nearest vertex rather than interpolating, so it can differ from a physics trace by up to half a quad of slope, and it says so. Position accepts x + y, point {x, y}, or worldX + worldY. Params: x?, y?, point?, worldX?, worldY?, actorLabel?, actorPath?, editLayer?, editLayerIndex?", "get_landscape_height_at_point"),
    get_normal_at_point:  bp("read", "World-space surface normal under one world XY, computed by central difference over the neighbouring vertices in world centimetres and rotated into world space, so a non-uniform landscape scale is accounted for. Returns the normal, the slope in degrees and the vertex it was read at. Params: x?, y?, point?, worldX?, worldY?, actorLabel?, actorPath?", "get_landscape_normal_at_point"),
    get_slope_at_point:   bp("read", "Slope under one world XY in degrees from horizontal, in radians, and as a grade percentage, plus the downhill direction as a unit vector. The downhill direction is what routes a road or a river; a slope number alone never is. Params: x?, y?, point?, worldX?, worldY?, actorLabel?, actorPath?", "get_landscape_slope_at_point"),
    get_slope_map: {
      ...bp("read", "Per-vertex slope in degrees over a rectangle, plus min/max/mean and a fixed nine-bucket ten-degree histogram. The buckets are fixed on purpose: they are what a caller reasons about (anything under 15 degrees is buildable) and a configurable bin count would make two runs incomparable. The per-vertex array is returned below arrayEncodingLimit and omitted with a reason above it. Params: actorLabel?, actorPath?, region?, space?, center?, radius?, maxVertices?, includeSlopes?, arrayEncodingLimit?", "get_landscape_slope_map"),
      timeoutMs: 120_000,
    },
    sculpt_region: {
      ...bp("mutate", "Apply one shaping operator over a rectangle: raise, lower, flatten, smooth, mountain, valley, ridge, plateau, crater or terrace. One action rather than ten, because the region resolution, falloff, strength blend, previous-height capture and rollback record are identical for every shape and only the per-vertex kernel differs. amount is world centimetres at full strength, shape picks a radial (ellipse) or edge-relative (rect) falloff, and sharpness controls how peaked the dome operators are. flatten and plateau take targetHeight in world Z, or flattenTo as mean / center / min / max; smooth takes iterations; terrace takes steps; ridge takes ridgeAngle in degrees; crater takes rimPosition and rimRatio. Reports verticesClampedToHeightRange when the shape ran past the uint16 range, since that is a flat-topped mountain rather than a failure. Params: operator, actorLabel?, actorPath?, region?, space?, center?, radius?, maxVertices?, amount?, strength?, falloff?, sharpness?, shape?, targetHeight?, flattenTo?, iterations?, steps?, ridgeAngle?, rimPosition?, rimRatio?, editLayer?, editLayerIndex?, rollbackMaxVertices?", "sculpt_landscape_region"),
      timeoutMs: 300_000,
    },
    apply_erosion: {
      ...bp("mutate", "Run hydraulic or thermal erosion over a rectangle. Hydraulic rains on every vertex, routes water to lower four-neighbours, carries sediment to capacity and deposits the rest, then re-deposits whatever is still suspended so the pass does not quietly remove material every run; thermal slumps anything steeper than talusAngle toward it. This is a CPU grid model over the region on the game thread, not the Landscape Mode erosion tool, and the response says so. Cost is vertices times iterations and is refused above maxWork rather than blocking the editor for an unbounded time. Params: actorLabel?, actorPath?, erosionType?, region?, space?, center?, radius?, maxVertices?, iterations?, maxWork?, talusAngle?, strength?, rainAmount?, evaporation?, sedimentCapacity?, erosionRate?, depositionRate?, editLayer?, editLayerIndex?, rollbackMaxVertices?", "apply_landscape_erosion"),
      timeoutMs: 600_000,
    },
    import_heightmap: {
      ...bp("mutate", "Write a 16-bit heightmap file into a region of the landscape. format is png16 (16-bit greyscale PNG) or raw16 (headerless little-endian uint16), inferred from the extension when omitted; an 8-bit image is refused with its real format rather than stretched, because a 256-step heightmap terraces visibly. A raw16 file carries no dimensions, so width and height are required for it. When the image and the region disagree the call refuses and prints the region that WOULD fit, unless resample is true, which bilinearly fits it and says detail was averaged away. minHeight and maxHeight remap the image's 0 and 65535 onto a world Z band, which is the control a heightmap authored elsewhere actually needs. A relative filePath resolves under the project's Saved directory. Params: filePath/sourcePath, actorLabel?, actorPath?, format?, region?, space?, center?, radius?, maxVertices?, width?, height?, resample?, minHeight?, maxHeight?, editLayer?, editLayerIndex?, rollbackMaxVertices?", "import_landscape_heightmap"),
      timeoutMs: 600_000,
    },
    export_heightmap: {
      ...bp("mutate", "Write a region's heights out as a 16-bit greyscale PNG or a headerless little-endian uint16 file, for inspection or for a round trip through an external terrain tool. Reports the exact byte count, the region and the height range in raw units and world Z, so the file can be re-imported at the right scale. Exports the merged surface unless editLayer or editLayerIndex names one. No inverse is emitted: the bridge has no delete-file method, and overwriting a file whose previous contents were never read is not undoable, so the response says rollbackOmitted with which of the two cases applied. Params: filePath/outputPath, actorLabel?, actorPath?, format?, region?, space?, center?, radius?, maxVertices?, editLayer?, editLayerIndex?, overwrite?", "export_landscape_heightmap"),
      timeoutMs: 300_000,
    },
    analyze_terrain: {
      ...bp("read", "Height and slope distribution over a region, plus largestFlatArea: the biggest axis-aligned rectangle whose every vertex is within slopeThresholdDegrees, in quad indices AND world coordinates with its mean Z. That last part is the answer to where a building fits, which a flat-vertex COUNT never is - ten thousand scattered flat vertices and one flat plateau report the same number. The height histogram takes histogramBins; the slope histogram is fixed at nine ten-degree bands so two runs stay comparable. Params: actorLabel?, actorPath?, region?, space?, center?, radius?, maxVertices?, histogramBins?, slopeThresholdDegrees?", "analyze_landscape_terrain"),
      timeoutMs: 120_000,
    },
    get_layer_weight_region: {
      ...bp("read", "Read one paint layer's 0..255 weights over a rectangle, with min/max/mean and the painted vertex count and fraction. An unregistered layerName is refused with the layers that ARE registered, so a typo and a landscape whose material never declared the layer read differently. Weights come back as an array below arrayEncodingLimit and as base64 above it, in the exact form set_layer_weight_region takes back. Params: layerName, actorLabel?, actorPath?, region?, space?, center?, radius?, maxVertices?, editLayer?, editLayerIndex?, includeWeights?, encoding?, arrayEncodingLimit?", "get_landscape_layer_weight_region"),
      timeoutMs: 120_000,
    },
    set_layer_weight_region: {
      ...bp("mutate", "Write one paint layer's weights over a rectangle. Accepts weightsBase64 (one uint8 per vertex, the form the getter returns), weights as one number per vertex, or weight / strength as a single 0..1 fraction to fill the rectangle. Idempotent, with the same unchanged/updated reporting as set_height_region, and the rollback record carries the exact previous weights up to rollbackMaxVertices. Weights are written as given: the engine no longer renormalises the other layers for you, so set them explicitly if they must sum to 1. Params: layerName, actorLabel?, actorPath?, region?, space?, center?, radius?, maxVertices?, weightsBase64?, weights?, weight?, strength?, editLayer?, editLayerIndex?, rollbackMaxVertices?", "set_landscape_layer_weight_region"),
      timeoutMs: 300_000,
    },
    layer_exists:  bp("read", "Is this paint layer registered on the landscape, and does it have a LayerInfo asset to store weights in? Returns exists, hasLayerInfo, its layerInfoPath, and the full layer list. The two flags are separate because a material can declare a layer that has no LayerInfo assigned, which looks present and fails every paint. Params: layerName, actorLabel?, actorPath?", "landscape_layer_exists"),
    add_layer:     bp("mutate", "Register a paint layer on the landscape, creating the ULandscapeLayerInfoObject asset that its weight data needs and binding it. The counterpart to remove_layer, and the same handler add_layer_info calls. Idempotent: a layer already registered is reported with its existing asset path rather than duplicated. Params: layerName, landscapeName?, packagePath?", "add_landscape_layer_info"),
    remove_layer:  bp("mutate", "Unregister a paint layer and drop its weight data across every component of the landscape. Idempotent: removing a layer that is not there reports alreadyAbsent with the layers that are, rather than failing. NOT undoable through the bridge - the weights are destroyed without being read back first, and the response says so with the reason, so export them with get_layer_weight_region first if they matter. The ULandscapeLayerInfoObject asset itself is left on disk, so add_layer brings the layer back empty. Params: layerName, actorLabel?, actorPath?", "remove_landscape_layer"),
    get_holes: {
      ...bp("read", "Read the landscape visibility mask over a rectangle or at a single world XY, as booleans plus the hole vertex count and fraction. A vertex counts as a hole once its visibility weight passes the engine's own two-thirds threshold, so this action and the renderer agree about what a hole is. A hole only renders, and only stops collision, if the landscape material routes a Landscape Visibility Mask node into opacity mask. Params: actorLabel?, actorPath?, region?, space?, center?, radius?, x?, y?, point?, worldX?, worldY?, maxVertices?, includeMask?, arrayEncodingLimit?", "get_landscape_holes"),
      timeoutMs: 120_000,
    },
    set_holes: {
      ...bp("mutate", "Punch or fill the landscape visibility mask over a rectangle or at a single world XY. Accepts hole as a single boolean for the whole target, holes as one boolean per vertex, or weightsBase64 to restore exact previous visibility weights (which is the form its own rollback record carries). Unlike every other region action this one does NOT default to the whole landscape when no target is given: that would punch the entire terrain into a hole on a call that forgot a parameter, so it refuses instead. Params: actorLabel?, actorPath?, region?, space?, center?, radius?, x?, y?, point?, worldX?, worldY?, maxVertices?, hole?, holes?, weightsBase64?, editLayer?, editLayerIndex?, rollbackMaxVertices?", "set_landscape_holes"),
      timeoutMs: 300_000,
    },
    // ── V17 real-world terrain, the core half ────────────────────────────
    plan_real_world: {
      ...bp("read", "Turn a real-world extent plus a heightmap file into the exact landscape(create) and landscape(import_heightmap) parameters that reproduce it in engine at true scale. Creates nothing: it is arithmetic against engine invariants a caller cannot restate correctly by hand, namely that a landscape's vertex count is componentCount x subsectionSizeQuads x numSubsections + 1 with subsectionSizeQuads restricted to 7/15/31/63/127/255, and that its uint16 heights only mean metres through the actor's Z scale. Size the tile with realWorldSizeMeters {x,y}, or with boundsLatLon {minLat,minLon,maxLat,maxLon} which is projected on a local tangent plane at the box centre latitude (an approximation, not a datum reprojection, and the response says so). minElevationMeters and maxElevationMeters state what the image's value range means; elevationEncoding full spans 0..65535 across that band, data measures the image's own range and needs sourcePath. Returns the chosen resolution, every legal alternative with its error, the resulting metres per quad, the vertical precision in centimetres, and warnings for resampling, anisotropy, quantisation and exaggeration. Fetching DEM tiles over the network is deliberately NOT part of this: provider APIs, keys and licences change independently of the engine, so that half belongs in a plugin. Params: minElevationMeters, maxElevationMeters, realWorldSizeMeters?, boundsLatLon?, sourcePath?, filePath?, format?, width?, height?, metersPerQuad?, elevationEncoding?, verticalExaggeration?, maxComponents?, location?", "plan_real_world_landscape"),
      timeoutMs: 120_000,
    },
    project_geo_coordinates: bp("read", "Convert between latitude/longitude and a landscape's world space, in both directions, against the same boundsLatLon the landscape was planned for. Each entry in points is either {lat, lon}, answered with the world X/Y and the sampled surface Z so the point lands ON the terrain, or {x, y}, answered with the geographic coordinate that world position corresponds to - which is what turns analyze_terrain's largestFlatArea back into a real place. northAt says which end of the landscape's Y axis is north, defaulting to minY because a DEM raster's first row is its northern edge and that is how import_heightmap lays one down; getting it wrong mirrors every placement about the middle of the map and nothing about the result looks wrong. Params: boundsLatLon, points, actorLabel?, actorPath?, northAt?, sampleHeight?", "project_geo_coordinates"),
    refresh_physical_material_collision: {
      ...bp("mutate", "UE 5.8+: safely refresh physical-material collision data in memory on loaded World Partition LandscapeStreamingProxy actors after a LayerInfo PhysMaterial change. Requires complete registered collision coverage and no pending landscape edit-layer work. Preserves and verifies every raw, complex-live, and simple-live height sample, builds material data before one collision recreation, and fails the whole matched batch on any unsafe result. Filters combine: actorLabels[], guids[], and bounds {min,max}; omitting them targets every loaded proxy up to maxActors (default 256, hard max 1024). Unloaded proxies are untouched; pin them first with level(load_actor_descs). Refuses PIE/SIE and non-World-Partition maps. Persistence is deliberately unsupported because Landscape PreSave can mutate edit-layer collision data; this action never saves packages. Params: actorLabels? (string[]), guids? (string[]), bounds? ({min, max}), maxActors? (default 256, max 1024). No inverse: this recomputes derived collision and physical-material data from weightmaps it never writes, so nothing restores the previous build and the response says rollbackPossible=false. 'save' is deliberately absent from this category's schema and from what this action forwards, so persistence cannot be requested through the tool at all; the handler's own refusal of a save=true is there for a direct bridge caller. Returns loaded/matched/refreshed/failed counts and exact affected package paths.", "refresh_landscape_physical_material_collision", (p) => ({ actorLabels: p.actorLabels, guids: p.guids, bounds: p.bounds, maxActors: p.maxActors })),
      timeoutMs: 600_000,
    },
  },
  undefined,
  {
    x: z.number().optional().describe("sample: world-space X of the position to sample (#939)"),
    y: z.number().optional().describe("sample: world-space Y of the position to sample (#939)"),
    // Same shape as `center`, deliberately: a caller who has {x, y} should not
    // have to invent a z, and one who has a full {x, y, z} should not be
    // rejected for carrying one. Only x and y select the sample.
    point: z.record(z.number()).optional().describe("sample: world-space position as an object {x, y} (a z is accepted and ignored, since the surface height is what this answers) (#939)"),
    worldX: z.number().optional().describe("find_proxy_at / sample: world-space X (#733, #939)"),
    worldY: z.number().optional().describe("find_proxy_at / sample: world-space Y (#733, #939)"),
    includeLayers: z.boolean().optional().describe("sample: include per-paint-layer weights (default true) (#939)"),
    radius: z.number().optional(),
    strength: z.number().optional().describe("paint_layer / set_layer_weight_region: paint weight as a 0..1 fraction (default 1). sculpt_region / apply_landscape_erosion: how hard the operator blends into what is already there, 0..1"),
    center: z.record(z.number()).optional().describe("sculpt/paint_layer: brush centre {x, y} in world space (#742)"),
    mode: z.string().optional().describe("sculpt: raise | lower | flatten (#742)"),
    amount: z.number().optional().describe("sculpt: world centimetres to move at full brush strength (#742)"),
    actorLabel: z.string().optional().describe("sample/sculpt/paint_layer: which Landscape actor, when the level has more than one (#742)"),
    actorPath: z.string().optional().describe("sample/sculpt/paint_layer: full object path of the Landscape actor. The unambiguous selector, and it wins over actorLabel when both are given. Editor labels are NOT unique, and a label matching several landscapes is refused with the candidates rather than sculpting one of them (#983)"),
    editLayer: z.string().optional().describe("sculpt/paint_layer: edit layer NAME to write into; without one the write targets the merged heightmap and is regenerated away (#742)"),
    editLayerIndex: z.number().optional().describe("sculpt/paint_layer: edit layer index (default 0) when editLayer is not given (#742)"),
    maxVertices: z.number().optional().describe("sculpt/paint_layer: refuse a brush covering more than this many vertices (default 4,000,000) (#742)"),
    falloff: z.number().optional(),
    layerName: z.string().optional(),
    materialPath: z.string().optional(),
    filePath: z.string().optional(),
    componentIndex: z.number().optional(),
    name: z.string().optional(),
    packagePath: z.string().optional(),
    physMaterial: z.string().optional(),
    hardness: z.number().optional(),
    onConflict: z.string().optional(),
    location: Vec3.optional(),
    scale: Vec3.optional(),
    componentCountX: z.number().optional(),
    componentCountY: z.number().optional(),
    subsectionSizeQuads: z.number().optional(),
    numSubsections: z.number().optional(),
    heightOffset: z.number().optional(),
    label: z.string().optional(),
    actorLabels: z.array(z.string().min(1)).min(1).max(256).optional().describe("refresh_physical_material_collision: exact editor labels of loaded LandscapeStreamingProxy actors"),
    guids: z.array(z.string().min(1).max(64)).min(1).max(256).optional().describe("refresh_physical_material_collision: actor GUIDs of loaded LandscapeStreamingProxy actors"),
    bounds: z.object({ min: Vec3, max: Vec3 }).optional().describe("refresh_physical_material_collision: world-space AABB intersecting proxies to refresh"),
    maxActors: z.number().int().min(1).max(1024).optional().describe("refresh_physical_material_collision: refuse more matches than this (default 256)"),

    /* ── V1 region surface ────────────────────────────────────────────────
     * One rectangle spelling shared by every region action. `region` is
     * declared with passthrough because the rollback records these actions
     * emit carry the region back with its width, vertexCount, source and
     * clamp report attached; a strict object would reject its own undo. */
    region: z.object({
      minX: z.number(), minY: z.number(), maxX: z.number(), maxY: z.number(),
    }).passthrough().optional().describe("region actions: the rectangle to work over, as landscape vertex indices by default or world centimetres when space is \"world\". Omit it for the whole landscape, except on set_holes which refuses to default"),
    space: z.string().optional().describe("region actions: \"quad\" (landscape vertex indices, the default) or \"world\" (centimetres)"),
    encoding: z.string().optional().describe("get_height_region / get_layer_weight_region: \"array\", \"base64\", or \"auto\" (default) which picks by arrayEncodingLimit"),
    arrayEncodingLimit: z.number().optional().describe("region reads: return the per-vertex array only up to this many values, and summarise above it (default 16384)"),
    includeHeights: z.boolean().optional().describe("get_height_region: include the per-vertex heights (default true)"),
    includeSlopes: z.boolean().optional().describe("get_slope_map: include the per-vertex slope array (default true)"),
    includeWeights: z.boolean().optional().describe("get_layer_weight_region: include the per-vertex weights (default true)"),
    includeMask: z.boolean().optional().describe("get_holes: include the per-vertex hole mask (default true)"),
    heights: z.array(z.number()).optional().describe("set_height_region: one height per vertex, row-major from (minX,minY), width = maxX-minX+1"),
    heightsBase64: z.string().optional().describe("set_height_region: little-endian uint16 blob, row-major - the exact form get_height_region hands back"),
    height: z.number().optional().describe("set_height_region: one world-Z value to fill the whole region with. import_heightmap: the source image height in pixels, required for raw16"),
    rawHeight: z.number().optional().describe("set_height_region: one raw uint16 value (32768 = the landscape actor's own Z) to fill the whole region with"),
    heightSpace: z.string().optional().describe("set_height_region: \"raw\" (uint16, default) or \"world\" (centimetres) for the values given"),
    rollbackMaxVertices: z.number().optional().describe("region writes: carry the previous data as a rollback record only up to this many vertices, and report rollbackOmitted with a reason above it (default 262144 for heights, 524288 for weights)"),
    operator: z.string().optional().describe("sculpt_region: raise | lower | flatten | smooth | mountain | valley | ridge | plateau | crater | terrace"),
    shape: z.string().optional().describe("sculpt_region: \"ellipse\" (radial falloff from the region centre, default) or \"rect\" (falloff toward the region edges)"),
    sharpness: z.number().optional().describe("sculpt_region: how peaked the dome operators are, 0.1..8 (default 1.5)"),
    targetHeight: z.number().optional().describe("sculpt_region: world Z that flatten and plateau pull toward, overriding flattenTo"),
    flattenTo: z.string().optional().describe("sculpt_region: mean (default), center, min or max, when flatten has no targetHeight"),
    steps: z.number().optional().describe("sculpt_region: number of terrace steps across the region's own height range (default 8)"),
    ridgeAngle: z.number().optional().describe("sculpt_region: degrees the ridge crest runs at within the region (default 0)"),
    rimPosition: z.number().optional().describe("sculpt_region: where the crater rim sits as a fraction of the radius, 0.1..0.95 (default 0.75)"),
    rimRatio: z.number().optional().describe("sculpt_region: crater rim height as a fraction of the bowl depth (default 0.35)"),
    iterations: z.number().optional().describe("sculpt_region (smooth) / apply_erosion: how many passes to run"),
    erosionType: z.string().optional().describe("apply_erosion: \"hydraulic\" (water carves channels and deposits silt, the default) or \"thermal\" (steep slopes slump toward a talus angle)"),
    maxWork: z.number().optional().describe("apply_erosion: refuse more than this many vertex-iterations, since the pass blocks the game thread (default 40,000,000)"),
    talusAngle: z.number().optional().describe("apply_erosion (thermal): the slope in degrees material stops slumping at (default 35)"),
    rainAmount: z.number().optional().describe("apply_erosion (hydraulic): water added per vertex per iteration, in world centimetres (default 0.5)"),
    evaporation: z.number().optional().describe("apply_erosion (hydraulic): fraction of water lost per iteration, 0.01..1 (default 0.5)"),
    sedimentCapacity: z.number().optional().describe("apply_erosion (hydraulic): how much sediment a unit of water can hold (default 0.6)"),
    erosionRate: z.number().optional().describe("apply_erosion (hydraulic): how fast under-loaded water cuts into the terrain, 0..1 (default 0.3)"),
    depositionRate: z.number().optional().describe("apply_erosion (hydraulic): how fast over-loaded water drops its sediment, 0..1 (default 0.3)"),
    format: z.string().optional().describe("import_heightmap / export_heightmap / plan_real_world: \"png16\" (16-bit greyscale PNG) or \"raw16\" (headerless little-endian uint16); inferred from the file extension when omitted"),
    sourcePath: z.string().optional().describe("import_heightmap / plan_real_world: the heightmap file to read; a relative path resolves under the project's Saved directory"),
    outputPath: z.string().optional().describe("export_heightmap: where to write the heightmap; a relative path resolves under the project's Saved directory"),
    overwrite: z.boolean().optional().describe("export_heightmap: allow replacing an existing file (default true)"),
    resample: z.boolean().optional().describe("import_heightmap: bilinearly fit an image whose size does not match the region, instead of refusing (default false)"),
    width: z.number().optional().describe("import_heightmap / plan_real_world: the source image width in pixels, required for raw16 which carries no dimensions"),
    minHeight: z.number().optional().describe("import_heightmap: the world Z the image's value 0 maps to"),
    maxHeight: z.number().optional().describe("import_heightmap: the world Z the image's value 65535 maps to"),
    histogramBins: z.number().optional().describe("analyze_terrain: bins in the height histogram, 2..256 (default 16); the slope histogram is fixed at nine ten-degree bands"),
    slopeThresholdDegrees: z.number().optional().describe("analyze_terrain: at or below this slope a vertex counts as flat, which is what largestFlatArea is measured against (default 10)"),
    weights: z.array(z.number()).optional().describe("set_layer_weight_region: one 0..255 weight per vertex, row-major from (minX,minY)"),
    weightsBase64: z.string().optional().describe("set_layer_weight_region / set_holes: one uint8 per vertex, base64 - the form the matching getter and the rollback record both use"),
    weight: z.number().optional().describe("set_layer_weight_region: a single value to fill the region with; 0..1 is read as a fraction and anything above it as the raw 0..255 the engine stores"),
    holes: z.array(z.boolean()).optional().describe("set_holes: one boolean per vertex, row-major from (minX,minY)"),
    hole: z.boolean().optional().describe("set_holes: punch (true, the default) or fill (false) the whole target"),
    landscapeName: z.string().optional().describe("add_layer / add_layer_info: internal name of the landscape proxy to register the layer on, when the level has more than one"),

    /* ── V17 real-world terrain ───────────────────────────────────────── */
    realWorldSizeMeters: z.record(z.number()).optional().describe("plan_real_world: the ground footprint {x, y} in metres that the heightmap covers"),
    boundsLatLon: z.object({
      minLat: z.number(), minLon: z.number(), maxLat: z.number(), maxLon: z.number(),
    }).optional().describe("plan_real_world / project_geo_coordinates: the geographic box in decimal degrees. Pass the SAME box to both, so placement uses the identical mapping the landscape was built with"),
    minElevationMeters: z.number().optional().describe("plan_real_world: the real-world elevation the heightmap's low value stands for"),
    maxElevationMeters: z.number().optional().describe("plan_real_world: the real-world elevation the heightmap's high value stands for"),
    elevationEncoding: z.string().optional().describe("plan_real_world: \"full\" (the image spans 0..65535 across the elevation band, the default) or \"data\" (only the values present in the image span it, which needs sourcePath so the range can be measured)"),
    verticalExaggeration: z.number().optional().describe("plan_real_world: multiply real elevations by this, 0.01..100 (default 1). Anything but 1 means heights are no longer real metres, and the plan warns about it"),
    metersPerQuad: z.number().optional().describe("plan_real_world: the ground distance one landscape quad should cover. Omit it to size the landscape at one vertex per source image sample"),
    maxComponents: z.number().optional().describe("plan_real_world: reject any configuration needing more than this many landscape components (default 1024)"),
    points: z.array(z.object({
      lat: z.number().optional(),
      lon: z.number().optional(),
      x: z.number().optional(),
      y: z.number().optional(),
      name: z.string().optional(),
    })).optional().describe("project_geo_coordinates: entries to convert. {lat, lon} goes to world space and samples the surface height; {x, y} goes back to geographic coordinates. An optional name is echoed back"),
    northAt: z.string().optional().describe("project_geo_coordinates: which end of the landscape's Y axis is north, \"minY\" (default, matching how import_heightmap lays a DEM down) or \"maxY\""),
    sampleHeight: z.boolean().optional().describe("project_geo_coordinates: read the landscape surface height at each projected point (default true)"),
    // cursor + limit for the paged list actions. Declared once: the MCP layer
    // strips a key the category never declares, so a paged action whose
    // category omits these silently returns page one forever.
    ...PAGINATION_SCHEMA,
  },
);
