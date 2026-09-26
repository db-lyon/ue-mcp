import { z } from "zod";
import type { ToolDef } from "../core/types.js";
import { categoryTool } from "../category-tool.js";
import { paged } from "../pagination.js";
import { SESSION_ID } from "../lock-owner.js";
import { McpError, ErrorCode } from "../core/errors.js";
import type { EditorSession } from "../session.js";
import type { ToolContext } from "../core/types.js";
import { actions as epicActions, schema as epicSchema } from "./epic/asset.generated.js";
import { specBp, schema as specSchema } from "./specs/asset.generated.js";
import { specBp as gameplaySpecBp, schema as gameplaySpecSchema } from "./specs/gameplay.generated.js";
import { borrowSchema } from "../handler-spec.js";

/**
 * Who a lock belongs to: the addressed editor, or this process when there is
 * no session behind the call (#817).
 *
 * The lock registry lives in the bridge, which is per editor, so the owner has
 * to match whatever `withAssetLocks` used on the dispatch path or an explicit
 * asset(unlock) would not match the lock asset(lock) took. Both read this.
 */
function lockOwner(ctx: ToolContext, params: Record<string, unknown>): string {
  const explicit = params.sessionId;
  if (typeof explicit === "string" && explicit.trim() !== "") return explicit;
  return ctx.session?.lockOwnerId ?? SESSION_ID;
}

/**
 * `asset(migrate)`, with a second editor as the destination (#817, plan 6.5).
 *
 * Migration is the one action with two editors in it: the call runs in the
 * editor holding the source assets and its output lands in another project
 * entirely. Naming that project as a path worked, but left two things to the
 * caller that the server already knows: which directory it is, and the fact
 * that the destination editor will not see the new packages until its asset
 * registry is rescanned. A destination editor answers both.
 *
 * The migrate call itself is unchanged and still goes to the source editor's
 * bridge; `toEditor` never reaches it.
 */
async function migrateAssets(
  ctx: ToolContext,
  p: Record<string, unknown>,
): Promise<unknown> {
  const requested = typeof p.toEditor === "string" ? p.toEditor.trim() : "";
  const explicitDir = typeof p.destinationContentDir === "string" ? p.destinationContentDir.trim() : "";

  let destination: EditorSession | undefined;
  let destinationContentDir = explicitDir;

  if (requested) {
    if (explicitDir) {
      throw new McpError(
        ErrorCode.INVALID_PARAMS,
        "Pass 'toEditor' or 'destinationContentDir', not both: they name the same thing and " +
          "there is no safe answer when they disagree.",
      );
    }
    if (!ctx.sessions) {
      throw new McpError(
        ErrorCode.INVALID_PARAMS,
        "'toEditor' addresses another editor this server drives, and there is no session registry here. " +
          "Pass 'destinationContentDir' with the target project's Content folder instead.",
      );
    }
    destination = ctx.sessions.resolve(requested);
    if (ctx.session && destination === ctx.session) {
      throw new McpError(
        ErrorCode.INVALID_PARAMS,
        `'${destination.name}' is the editor this call runs in, so there is nothing to migrate between. ` +
          "Address the source editor with 'editor' and the destination with 'toEditor'.",
      );
    }
    const dir = destination.project.contentDir;
    if (!dir) {
      throw new McpError(
        ErrorCode.INVALID_PARAMS,
        `Editor '${destination.name}' has no project bound, so it has no Content directory to migrate into.`,
      );
    }
    destinationContentDir = dir;
  }

  const result = (await ctx.bridge.call("migrate", {
    assetPaths: p.assetPaths,
    assetPath: p.assetPath,
    destinationContentDir,
    includeDependencies: p.includeDependencies,
    onConflict: p.onConflict,
    allowDirty: p.allowDirty,
    dryRun: p.dryRun,
  })) as Record<string, unknown>;

  if (!destination) return result;

  const out: Record<string, unknown> = {
    ...(result && typeof result === "object" ? result : { result }),
    destination: {
      editor: destination.name,
      project: destination.project.projectPath,
      contentDir: destinationContentDir,
    },
  };
  out.rescan = p.dryRun === true
    ? { attempted: false, reason: "dryRun copied nothing, so there is nothing to rescan." }
    : await rescanDestination(destination, contentPathsOf(p));
  return out;
}

/** The content directories a migrate landed in, derived from what it was asked to move. */
function contentPathsOf(p: Record<string, unknown>): string[] {
  const raw = [
    ...(Array.isArray(p.assetPaths) ? p.assetPaths : []),
    ...(typeof p.assetPath === "string" ? [p.assetPath] : []),
  ].filter((v): v is string => typeof v === "string" && v.startsWith("/"));

  const dirs = new Set<string>();
  for (const assetPath of raw) {
    const withoutObject = assetPath.split(".")[0];
    const slash = withoutObject.lastIndexOf("/");
    dirs.add(slash > 0 ? withoutObject.slice(0, slash) : withoutObject);
  }
  // Nothing recognisable to narrow by: rescan the whole game root rather than
  // leave the destination editor blind to what just landed in it.
  if (dirs.size === 0) return ["/Game"];
  return [...dirs].slice(0, 32);
}

/**
 * Make the migrated packages visible in the destination editor.
 *
 * Unreal does not notice files that appeared under Content while it was
 * running, so without this the assets are on disk and absent from every
 * registry query until someone restarts or rescans by hand. `diagnose_registry`
 * with `reconcile` is a forced synchronous scan of one path, which is exactly
 * the operation needed and already exists on the bridge.
 *
 * Best-effort by design: the migration itself has already succeeded, so a
 * destination editor that is closed or on an older plugin is reported, not
 * turned into a failure of a copy that worked.
 */
async function rescanDestination(
  destination: EditorSession,
  contentPaths: string[],
): Promise<Record<string, unknown>> {
  if (!destination.bridge.isConnected) {
    return {
      attempted: false,
      reason:
        `Editor '${destination.name}' is not connected, so its asset registry could not be rescanned. ` +
        "The files are on disk; that editor will see them when it next starts.",
    };
  }
  const scanned: string[] = [];
  const failed: Array<{ path: string; error: string }> = [];
  for (const contentPath of contentPaths) {
    try {
      await destination.guarded.call("diagnose_registry", {
        path: contentPath,
        recursive: true,
        reconcile: true,
      });
      scanned.push(contentPath);
    } catch (e) {
      failed.push({ path: contentPath, error: e instanceof Error ? e.message : String(e) });
    }
  }
  return failed.length === 0
    ? { attempted: true, editor: destination.name, scanned }
    : { attempted: true, editor: destination.name, scanned, failed };
}

export const assetTool: ToolDef = categoryTool(
  "asset",
  "Asset management: list, search, read, CRUD, import meshes/textures, datatables, stringtables.",
  {
    list: specBp("read", "List assets via the AssetRegistry (sees /Game and every mounted plugin root). Cursor-paginated, so a large folder is walked deterministically instead of dropping the bridge on one oversized response (#790): every page carries totalMatched, hasMore and a nextCursor to pass back. The row offset this used to page with is refused, because a row number cannot report that the folder changed underneath it. maxResults is a deprecated spelling of limit.", "list_assets"),
    search: {
      kind: "handler",
      effect: "read",
      description: paged("Search by name/class/path. maxResults is a deprecated spelling of limit and sizes the page when limit is omitted. With extra content roots configured and no directory, this searches each root and pages ONE ROOT AT A TIME, so a cursor has to be passed back together with the directory it came from. Params: query, directory?, maxResults?, searchAll?"),
      handler: async (ctx, p) => {
        const { action: _, maxResults, ...rest } = p;
        // One page size, whichever spelling the caller used. The bridge reads
        // `limit` only, so `maxResults` is resolved here rather than in C++.
        const limit = (p.limit as number | undefined) ?? (maxResults as number | undefined);
        const call = { ...rest, ...(limit !== undefined ? { limit } : {}) };
        const roots = ctx.project.config.contentRoots;
        // If no directory specified and contentRoots configured, search each root and merge
        if (!p.directory && roots && roots.length > 0) {
          if (typeof p.cursor === "string" && p.cursor !== "") {
            throw new McpError(
              ErrorCode.INVALID_PARAMS,
              `A cursor names one content root, and this call searches ${roots.length} of them (${roots.join(", ")}). `
              + "Pass 'directory' set to the root the cursor came from, alongside the cursor, to continue that root. "
              + "Each root's own nextCursor is reported per root on the first page.",
            );
          }
          const cap = limit ?? 50;
          const allResults: Array<Record<string, unknown>> = [];
          const perRoot: Array<Record<string, unknown>> = [];
          for (const root of roots) {
            const res = await ctx.bridge.call("search_assets", { ...call, directory: root }) as Record<string, unknown>;
            if (res.results && Array.isArray(res.results)) {
              allResults.push(...(res.results as Array<Record<string, unknown>>));
            }
            // Per root, because one cursor cannot address several of them. A
            // root left unsearched because the cap was already reached says so,
            // rather than reading as a root with nothing in it.
            perRoot.push({
              directory: root,
              count: res.count ?? 0,
              total: res.total,
              hasMore: res.hasMore === true,
              nextCursor: res.nextCursor,
            });
            if (allResults.length >= cap) break;
          }
          const searched = perRoot.length;
          return {
            query: p.query ?? "",
            searchScope: roots,
            roots: perRoot,
            rootsSearched: searched,
            rootsUnsearched: roots.length - searched,
            resultCount: Math.min(allResults.length, cap),
            results: allResults.slice(0, cap),
            hasMore: allResults.length > cap || perRoot.some((r) => r.hasMore === true) || searched < roots.length,
            success: true,
          };
        }
        return ctx.bridge.call("search_assets", call);
      },
    },
    read:           specBp("read", "Read asset via reflection.", "read_asset"),
    read_properties: specBp("read", "Read asset properties with values. Blueprint paths resolve to the generated-class CDO (#568). propertyName accepts dotted/indexed paths into nested structs, array elements, and instanced subobjects (e.g. `Config.Traits[1].Params.Field`); landing on an array of subobjects also lists each element's index+class (#527). expandDepth inlines the properties of subobjects OWNED by this asset, so a data asset's nested payload comes back in ONE call instead of a reference you have to chase (#755); references to OTHER assets are marked expandable rather than followed, unless expandExternal=true. Capped by maxExpandedObjects with expansionTruncated reported.", "read_asset_properties"),
    list_properties: specBp("read", "List reflected properties on any asset.", "read_asset_properties"),
    get_properties: specBp("read", "Read property values on any asset; the same read as read_properties with includeValues on by default. propertyName accepts dotted/indexed paths into nested structs, array elements, and instanced subobjects. valueFormat='json' returns structured values.", "get_asset_properties"),
    duplicate:      specBp("mutate", "Duplicate asset.", "duplicate_asset"),
    rename:         specBp("mutate", "Rename asset: pass assetPath and newName, or sourcePath and destinationPath. World Partition levels are detected and their __ExternalActors__/__ExternalObjects__ packages migrate atomically alongside the .umap, source-side redirectors get fixed up, and the active editor world is swapped to blank if it matches the source (#409). Refuses if any package is dirty - save first. If a prior rename left externals orphaned at the old path, re-running reconciles them. Rollback descriptor is emitted even on partial failure so the inverse rename can recover. `force=true` lets the call merge into a destination with pre-existing externals (used by rollback). For batches of 3+ scene-referenced non-world assets use bulk_rename instead.", "rename_asset"),
    bulk_rename:    specBp("mutate", "Batched rename using IAssetTools::RenameAssets - single transaction with one redirector-fixup pass (matches Content Browser drag). Use this over looped rename for scene-referenced assets. World assets are rejected (status=rejected_world); use rename_asset which handles WP externals atomically (#409). The fix-up pass cannot see a referencer that is not loaded, so each renamed item reports redirectorLeft and the result carries redirectorsLeft/redirectorsRemoved plus redirectorPackages, which feeds straight into fixup_redirectors (#908). A batch that would leave another object of a multi-object package at the old path is refused with reason=package_objects_left_behind and splitPackages naming each one; nothing moves (#1137).", "bulk_rename_assets"),
    fixup_redirectors: specBp("mutate", "Clean up ObjectRedirectors left at the old paths after a move, bounded to exactly the packages you name. Preflights each redirector, its destination, and its hard AND soft referencers, then reports packagesToLoad/packagesToSave before touching anything; dryRun=true stops there. The real run loads only those referencers (a soft reference in an unloaded package is what the rename's own fix-up pass misses), rewrites them, saves them, RE-QUERIES the asset registry, and deletes a redirector only when nothing references it any more, reporting kept vs deleted per package with the referencers that survived. Naming a whole content root is refused unless allowProjectWide=true, so this can never turn into a project-wide resave. Protected mounts are refused outright. dryRun is the way to preview: save=false only skips the explicit save pass, because the editor's own fix-up writes what it can regardless (#908).", "fixup_redirectors"),
    move:           specBp("mutate", "Move asset: pass sourcePath and destinationPath. The same call as rename.", "move_asset"),
    delete:         specBp("mutate", "Delete asset. force defaults to false and is a REAL guard: the bridge asks the Asset Registry for referencers first and refuses with success=false, reason='has_referencers' and the full referencers list rather than destroying an asset other packages point at (#976). force=true takes the force-delete path, which also auto-closes open asset editors (#278). On a delete the editor attempted and could not finish, the reason is open_in_editor / has_referencers / in_memory_referenced / package_read_only / package_dirty / unknown, with referencers, inMemoryReferencers, packageReadOnly and packageDirty diagnostics (#601).", "delete_asset"),
    delete_batch:   specBp("mutate", "Batch-delete assets. Per-path status is deleted | absent | protected | refused | failed, plus reason+referencers on refused and failed entries. A referenced asset is refused per entry with force=false and the rest of the batch still runs, so one guarded asset never hides behind a bare failed count (#976, #278). Result carries deleted/absent/refused/failed/protected/total.", "delete_asset_batch"),
    create_data_asset: specBp("mutate", "Create UDataAsset instance of custom class. className accepts the C++ spelling with or without the A/U/F/E prefix (UMyConfig and MyConfig both resolve), a /Script/Module.ClassName path, or a loaded class name; a failed lookup lists the spellings tried and the closest matches (#823).", "create_data_asset"),
    create_asset_by_class: specBp("mutate", "Create an asset of ANY concrete UObject class (not just UDataAsset) - physical-material subclasses, curves, settings objects. className accepts the C++ spelling with or without the A/U/F/E prefix, a /Script/Module.ClassName path, or a loaded class name (#823). Actors/components and specialized assets (Blueprint/Material) have dedicated actions (#726).", "create_asset_by_class"),
    read_graph: specBp("read", "Read the NODE GRAPH of any EdGraph-backed asset: every graph in it, every node, and every node's pins with what each pin is connected to. Reflection cannot reach topology: UEdGraphNode declares its Pins array with no UPROPERTY, and UEdGraphPin is a plain class rather than a UObject, so no set_property, reflect_instance or find_object can read, break or make a connection (#1059). For the EdGraph types with no category of their own, CustomizableObject (Mutable) among them. A Blueprint, a PCG graph or a Material is REFUSED here and pointed at blueprint(read_graph) / blueprint(get_connections) / pcg(read_graph) / material(read_graph), which own those types and address them in their own terms. Each node reports name, path, class, title, position and comment; each pin reports name, pinId, direction, type, defaultValue and linkedTo[] naming the node and pin at the other end. Node settings themselves are read with reflection(action='reflect_instance') on the node path this returns, which already works; this reports what that cannot see. connectionCount is per graph and counts each wire once; nodesReported is how many nodes this call emitted, which maxNodes can cut below a graph's own nodeCount. A node reported with hasNoPins is broken: AllocateDefaultPins never ran for it, so it can never be wired and the editor cannot draw it. Each node also reports nodeGuid. To change the graph use connect_graph_pins, disconnect_graph_pins, add_graph_node and remove_graph_node, which go through the graph's own schema. An asset with no graph answers with an empty list and says so rather than failing.", "read_asset_graph"),
    connect_graph_pins: specBp("mutate", "Connect two pins in any EdGraph-backed asset with no category of its own, CustomizableObject (Mutable) among them, through the graph's own schema (TryCreateConnection), so the graph type's rules decide what is legal. A refusal returns the schema's own message with reason='schema_disallowed'. Address each end by node (nodeGuid, node path, name or unique title from read_graph) and pinId, which is the reliable form because pin names repeat on some node types; pin name plus direction also works. A pinId alone needs no node. brokenLinks lists any link the schema replaced. Rollback: disconnect_graph_pins. A Blueprint, Material or PCG graph is refused and pointed at its own category.", "connect_graph_pins"),
    disconnect_graph_pins: specBp("mutate", "Break links in an EdGraph-backed asset through its schema. With a target pin, breaks that one link (BreakSinglePinLink); without one, breaks every link on the source pin (BreakPinLinks). Pins are addressed as in connect_graph_pins. A link that does not exist is reported with wasConnected=false and nothing changes. Rollback: connect_graph_pins when exactly one link was broken; otherwise brokenLinks names each one.", "disconnect_graph_pins"),
    add_graph_node: specBp("mutate", "Add a node to an EdGraph-backed asset's graph through the schema's own node action, so its pins are allocated and it is parented to the graph. nodeClass is the class the action spawns (a CustomizableObjectNode class for Mutable, a SoundNode class for a SoundCue); actionName picks a menu entry by name ('category|name' also works) when several actions spawn the same class. When nothing matches, the error lists availableActions. spawnMode=direct constructs nodeClass itself (CreateNewGuid, PostPlacedNewNode, AllocateDefaultPins), and auto does that only for a schema that publishes no node actions. Returns nodeGuid, nodePath and every pin with its pinId. Rollback: remove_graph_node.", "add_graph_node"),
    remove_graph_node: specBp("mutate", "Remove a node from an EdGraph-backed asset's graph: the schema breaks its links, then the node is destroyed. A node the graph type forbids deleting (a root or output node) is refused. brokenLinks names every link it had so they can be re-made. Rollback: add_graph_node with the same class and position when exactly one schema action spawns it (lossy: a fresh default node); otherwise rollbackPossible=false.", "remove_graph_node"),
    create_customizable_object: specBp("mutate", "Create a Mutable CustomizableObject through Mutable's own factory, so it holds its Source graph with the Base Object node and can be authored with add_graph_node and connect_graph_pins at once. An asset made with create_asset_by_class has no graph. Returns objectPath, graphName, rootNode (nodeGuid and pins with pinIds) and every graph node. onConflict=skip returns an existing CustomizableObject as existed; error refuses. Fails with 'Mutable plugin not available' when the plugin is off. Rollback: delete_asset.", "create_customizable_object"),
    compile_customizable_object: {
      ...specBp("mutate", "Compile a Mutable CustomizableObject synchronously, without saving or gathering references, and report the outcome: state, compiled, and the LogMutable errors and warnings the compile wrote. A failed compile returns success=false with the first error. Fails with 'Mutable plugin not available' when the Mutable plugin is not enabled.", "compile_customizable_object"),
      timeoutMs: 600_000,
    },
    create_subobject: specBp("mutate", "Create a named subobject OWNED by an existing asset, inside that asset's package, and return its object path for later property writes. This is the missing half of editing a data asset whose payload is named subobjects referenced from a struct array: set_property, bulk_set_properties and append_array_elements can edit the entries, and this makes a new one. Component classes are accepted, unlike create_asset_by_class; Actors are refused because they are spawned into a level. className takes a /Script/Module.Class path, which is how you name a plugin class the Python `unreal` module never exposes. The new object is created RF_Standalone and the package is saved in the same call, so it survives the garbage collection that used to eat a fresh object within one call and is reachable by path from the next one. properties are applied to a throwaway instance first, so a bad path or value creates nothing. outer='asset' (default) gives '<asset>.<name>'; outer='package' puts it beside the asset in the package (#975).", "create_subobject"),
    bulk_upsert_data_assets: specBp("mutate", "Create or update up to 500 UDataAsset instances in ONE call. Every descriptor is first applied to a transient copy, so a bad class, property path, or value rejects the whole batch before a package is touched; nothing is half-written by a typo. Per-item status is created | updated | unchanged | skipped | failed (dryRun reports wouldCreate | wouldUpdate | wouldRemainUnchanged | wouldSkip), each with its own error when it failed. Replaying the same request returns unchanged, and only changed packages are saved. Emits a rollback descriptor that restores prior values and deletes what it created.", "bulk_upsert_data_assets"),
    save:           specBp("mutate", "Save one asset, or every dirty asset under /Game when assetPath is omitted. force=true saves regardless of the dirty flag - several edits (OFPA level actors, some subsystem property writes) never mark their package dirty, so a dirty-only save skipped them and still reported success. Returns the package name plus on-disk file path, size and mtime so the write can be verified rather than trusted (#768). A failed save returns the engine's reason in error and saveDiagnostics, and when the engine refused a reference to a private object, illegalReference names that object, the object and property holding it, and how to clear it (#1120).", "save_asset"),
    save_all_dirty: specBp("mutate", "Flush every dirty package to disk in one call. Reports the packages it attempted, which ones reached disk (with file path, size and mtime) and which are still dirty afterwards, because a bare savedAll boolean has come back true while packages were never written (#768).", "save_all_dirty"),
    set_mesh_material:    specBp("mutate", "Assign material to static mesh slot.", "set_mesh_material"),
    set_mesh_materials_batch: specBp("mutate", "Assign materials across many meshes and slots in one call, so an N mesh x M slot kit costs one round trip instead of N*M. StaticMesh and SkeletalMesh both work. Address a slot by slotName (survives a reimport reordering slot indices) or by slotIndex (default 0); passing both is rejected when they disagree. Every submitted assignment returns its own index/ok/status/error, status being ok|updated|unchanged|invalid|protected|duplicate|not_found|slot_not_found|failed|skipped. Default is all-or-nothing: any preflight rejection aborts before a mesh is touched. Pass continueOnError to apply the assignments that did pass and keep the rejects reported alongside them. Each mesh is written and saved once no matter how many of its slots the batch names, and the rollback payload restores only the writes that landed (#822).", "set_mesh_materials_batch"),
    recenter_pivot:       specBp("mutate", "Move static mesh pivot to geometry center. With assetPaths, the first mesh sets the reference pivot for all of them.", "recenter_pivot"),
    import_static_mesh:   specBp("mutate", "Import from FBX, OBJ, or GLB/glTF (glTF routes through Interchange) (#549). importUniformScale=100 fixes metre-authored FBX (#687).", "import_static_mesh"),
    import_skeletal_mesh: specBp("mutate", "Import skeletal mesh from FBX. importUniformScale=100 fixes metre-authored FBX (Blender FBX_SCALE_ALL) that lands 100x too small on a cm skeleton (#687). Returns post-import readback: boxExtent, morphTargets[], numLODs, skeleton (#678).", "import_skeletal_mesh"),
    import_animation:     specBp("mutate", "Import anim from FBX. importSettings sets any FbxAnimSequenceImportData or FbxImportUI field by UPROPERTY name or dotted path, e.g. {bSnapToClosestFrameBoundary: true} for clips whose length is not frame-aligned; a bool may drop its b prefix. An unknown key fails before anything is imported (#1134).", "import_animation"),
    import_file:          specBp("mutate", "Import any source file through a chosen factory, unattended. factoryClass names a UFactory subclass by name or /Script path, including one a plugin ships (resolved at runtime; omit it to let the editor pick by extension). factoryProperties sets that factory's properties by UPROPERTY name or dotted path before the import runs, which is how a factory that raises its own options dialog is told to use the supplied options instead, since unattended mode cancels the dialog and the import with it. The file extension is checked against the factory's formats first. An unknown property fails before anything is imported (#1096).", "import_file"),
    import_texture:       specBp("mutate", "Import image. sRGB/compressionSettings/lodGroup/neverStream are applied at import time (folded in, no second call needed) (#661).", "import_texture"),
    create_render_target_2d: specBp("mutate", "Create and persist a TextureRenderTarget2D asset. Render format is applied before resource initialization.", "create_render_target_2d"),
    read_skeletal_mesh_build_settings: specBp("read", "Read a SkeletalMesh's per-LOD FSkeletalMeshBuildSettings without changing anything. lodIndex and allLods are mutually exclusive. Returns assetPath, lodCount and lods[] of {lodIndex, beforeBuildSettings, afterBuildSettings (identical on a read), beforeOptimizeForInstancing, afterOptimizeForInstancing, changed}, where each settings object carries bRecomputeNormals, bRecomputeTangents, bUseMikkTSpace, bComputeWeightedNormals, bRemoveDegenerates, bUseHighPrecisionTangentBasis, bUseHighPrecisionSkinWeights, bUseFullPrecisionUVs, bUseBackwardsCompatibleF16TruncUVs, bOptimizeForInstancing, thresholdPosition, thresholdTangentNormal, thresholdUV, morphThresholdPosition and boneInfluenceLimit.", "read_skeletal_mesh_build_settings"),
    set_skeletal_mesh_optimize_for_instancing: specBp("mutate", "Write bOptimizeForInstancing on a SkeletalMesh's LOD build settings, which reorders the skin data so instanced draws of the mesh batch. Only LODs whose current value differs are touched, and the asset is only rebuilt and saved when at least one changes, so a call that asks for the value already set is a no-op. Read the current state with read_skeletal_mesh_build_settings first. lodIndex and allLods are mutually exclusive. Returns assetPath, lodCount and lods[] of {lodIndex, beforeBuildSettings, afterBuildSettings, beforeOptimizeForInstancing, afterOptimizeForInstancing, changed}.", "set_skeletal_mesh_optimize_for_instancing"),
    read_skeletal_mesh_skin_weights: specBp("read", "Read existing per-vertex skin influences from one SkeletalMesh source LOD and profile. The caller must name 1-256 source MeshDescription vertex IDs, so a read cannot dump a whole mesh. Each influence returns boneName, boneIndex, normalized weight and exact uint16 rawWeight. Render data may use 8-bit weights unless high-precision skin weights are enabled. Generated LODs without source geometry are refused.", "read_skeletal_mesh_skin_weights"),
    set_skeletal_mesh_skin_weights: specBp("mutate", "Replace skin influences on explicitly selected source MeshDescription vertices in one existing SkeletalMesh LOD/profile. The complete batch is validated before mutation: invalid or duplicate vertices/bones, non-finite/negative/out-of-range weights and all-zero sets fail without a write. Unreal's FBoneWeights normalizes, quantizes to uint16 source weights and prunes ordinary edits to its influence limit; actual before/after values are read back. Other source vertices and source profiles are untouched. Rebuilding may refresh derived render data and dependent generated LODs. A changed mesh is rebuilt and saved; an identical request is a no-op. Rollback payloads use restoreRawWeights with exact uint16 rawWeight values.", "set_skeletal_mesh_skin_weights"),
    read_cloth_data:      specBp("read", "Read Chaos cloth data on a skeletal mesh: per clothing asset, its configs (reflected properties), LOD count, and per-LOD point-weight-map summary (name, target, vertex count, min/max - including the MaxDistances mask). Also reports which render sections each clothing asset is bound to (boundSections, bound), every section of every LOD (sections: lodIndex, sectionIndex, materialIndex, usesCloth, clothingAsset, assetLodIndex) and unboundClothingAssets: an asset bound to no section never simulates (#595/#1139).", "read_cloth_data"),
    bind_cloth_to_section: specBp("mutate", "Bind a clothing asset to one render section so it simulates, as the skeletal mesh editor's section clothing dropdown does. Whatever the section was bound to is unbound first. lodIndex and sectionIndex are validated against the mesh, assetLodIndex against the clothing asset. Saves the mesh; rolls back to the previous binding (#1139).", "bind_cloth_to_section"),
    unbind_cloth_from_section: specBp("mutate", "Remove the clothing asset bound to one render section. The engine unbinds per LOD, so other sections of that LOD bound to the same asset are released too and listed in releasedSections. A section with no binding is a no-op. Saves the mesh; rolls back by rebinding (#1139).", "unbind_cloth_from_section"),
    set_cloth_config:     specBp("mutate", "Set properties on a clothing asset's Chaos cloth config via reflection (#595).", "set_cloth_config"),
    export_texture:       specBp("mutate", "Export a Texture2D to a PNG on disk (for inspection or external diffing) (#697).", "export_texture"),
    compare_textures:     specBp("read", "Compare two Texture2D assets by dimensions, pixel format, and source-content identity (FTextureSource id) - tells you whether an authored texture actually changed without offline pixel-diffing (#697).", "compare_textures"),
    import_texture_batch: specBp("mutate", "Import many textures in one call - the loop stays inside the editor (no per-file bridge round-trip), so this finishes far faster than N import_texture calls. Per-item result records mirror import_texture. Returns requested/imported/failed counts + items[] (#430).", "import_texture_batch"),
    reimport:             specBp("mutate", "Reimport asset from source file.", "reimport_asset"),
    read_datatable:       specBp("read", "Read DataTable rows. A large table overflows the response inline: pass outputPath to write the rows (after rowFilter) to a file as a JSON array instead. The response then carries outputPath (resolved absolute path), rowCount, bytes, rowStruct and totalRowCount, with no rows or rowNames. A relative outputPath resolves under the project's Saved/, missing folders are created and an existing file is overwritten.", "read_datatable"),
    create_datatable:     specBp("mutate", "Create DataTable.", "create_datatable"),
    reimport_datatable:   specBp("mutate", "Reimport DataTable from JSON.", "reimport_datatable"),
    set_datatable_row:    specBp("mutate", "Append or overwrite a single DataTable row. row carries row-struct fields, and a partial update merges with the existing row. Idempotent; rollback restores the prior row (#437).", "set_datatable_row"),
    add_datatable_row:    specBp("mutate", "Alias for set_datatable_row (#437).", "add_datatable_row"),
    update_datatable_row: specBp("mutate", "Alias for set_datatable_row; partial update merges with existing row (#437).", "update_datatable_row"),
    remove_datatable_row: specBp("mutate", "Remove a single DataTable row. Idempotent (alreadyDeleted=true if missing) (#437).", "remove_datatable_row"),
    get_datatable_row:    specBp("read", "Read one DataTable row's fields without dumping the whole table (#535).", "get_datatable_row"),
    set_datatable_cell:   specBp("mutate", "Write a single field on a single existing row (merges, leaves other cells untouched). Errors if the row doesn't exist (#535).", "set_datatable_cell"),
    rename_datatable_row: specBp("mutate", "Rename a row key, preserving its values (#535).", "rename_datatable_row"),
    fill_datatable_from_json: specBp("mutate", "Bulk-upsert rows from a {rowName: {field: value}} object without touching unrelated rows (non-destructive, unlike reimport_datatable) (#535).", "fill_datatable_from_json"),
    create_curvetable:    specBp("mutate", "Create CurveTable asset.", "create_curvetable"),
    read_curvetable:      specBp("read", "Read CurveTable rows and keys.", "read_curvetable"),
    list_curvetable_rows: specBp("read", "Alias for read_curvetable.", "list_curvetable_rows"),
    import_curvetable:    specBp("mutate", "Import CurveTable from JSON/CSV string or file.", "import_curvetable"),
    add_curvetable_row:   specBp("mutate", "Add CurveTable row.", "add_curvetable_row"),
    remove_curvetable_row: specBp("mutate", "Remove CurveTable row. Idempotent if missing.", "remove_curvetable_row"),
    rename_curvetable_row: specBp("mutate", "Rename CurveTable row.", "rename_curvetable_row"),
    get_curvetable_keys:  specBp("read", "Read keys from one CurveTable row.", "get_curvetable_keys"),
    set_curvetable_keys:  specBp("mutate", "Replace keys on one CurveTable row.", "set_curvetable_keys"),
    add_curvetable_key:   specBp("mutate", "Add or update one key on a CurveTable row.", "add_curvetable_key"),
    list_textures:        specBp("read", "List textures, cursor-paginated. maxResults is a deprecated spelling of limit.", "list_textures"),
    get_texture_info:     specBp("read", "Get texture details.", "get_texture_info"),
    set_texture_settings: specBp("mutate", "Set texture settings, at the top level or inside settings.", "set_texture_settings"),
    create_stringtable:   specBp("mutate", "Create a StringTable asset.", "create_stringtable"),
    read_stringtable:     specBp("read", "Read StringTable entries and keys.", "read_stringtable"),
    list_stringtable_keys: specBp("read", "List StringTable keys.", "list_stringtable_keys"),
    get_stringtable_entry: specBp("read", "Read one StringTable entry.", "get_stringtable_entry"),
    set_stringtable_entry: specBp("mutate", "Create or update one StringTable entry. Pass sourceString, or value as a second spelling of it.", "set_stringtable_entry"),
    remove_stringtable_entry: specBp("mutate", "Remove one StringTable entry. Idempotent (alreadyDeleted=true if missing).", "remove_stringtable_entry"),
    import_stringtable:   specBp("mutate", "Import StringTable entries from CSV.", "import_stringtable"),
    import_stringtable_csv: specBp("mutate", "Import or refresh a String Table from a CSV that is the canonical source, using Unreal's own String Table CSV importer. The CSV is parsed into a throwaway table and checked against expectedKeys FIRST, so a malformed file or a key set that does not match leaves the asset untouched and comes back with missingKeys/unexpectedKeys instead. replaceExisting=true prunes entries the CSV no longer carries, which is what makes the CSV canonical rather than additive; the prune runs after a successful merge so a bad parse can never empty the table. Returns addedKeys, updatedKeys, removedKeys, entry counts and an explicit persisted/saved plus persistError, so a write that did not reach disk is never reported as a success. A relative csvPath is read against the project directory (#978).", "import_stringtable_csv"),
    add_input_mapping:    gameplaySpecBp("mutate", "Append an Enhanced Input key mapping to an InputMappingContext (InputAction + key by name string e.g. 'Mouse2D','LeftMouseButton'). Idempotent on (action,key). For modifiers/triggers use gameplay(set_mapping_modifiers). Same as gameplay(add_imc_mapping) (#525). Returns saved, persisted, packageDirty, packageName, and persistError when not persisted.", "add_imc_mapping"),
    remove_input_mapping: gameplaySpecBp("mutate", "Remove an IMC key mapping. Same as gameplay(remove_imc_mapping) (#525). Returns saved, persisted, packageDirty, packageName, and persistError when not persisted.", "remove_imc_mapping"),
    list_input_mappings:  gameplaySpecBp("read", "List an IMC's key->action bindings with triggers/modifiers. Same as gameplay(read_imc) (#525).", "read_imc"),
    add_socket:           specBp("mutate", "Add socket to StaticMesh or SkeletalMesh. SkeletalMesh writes mesh-local sockets by default; pass a Skeleton asset path to edit skeleton-level sockets. Idempotent on socket name; pass onConflict='update' to overwrite an existing socket's transform with the supplied relativeLocation/relativeRotation/relativeScale (#412).", "add_socket"),
    remove_socket:        specBp("mutate", "Remove socket by name.", "remove_socket"),
    list_sockets:         specBp("read", "List sockets on a mesh (StaticMesh or SkeletalMesh). SkeletalMesh results include mesh-local sockets plus assigned Skeleton sockets, each with source='mesh' or source='skeleton'.", "list_asset_sockets"),
    set_socket_transform: specBp("mutate", "Update an existing socket's relative transform on StaticMesh or SkeletalMesh. Pass any subset of relativeLocation/relativeRotation/relativeScale; omitted fields stay at their current values. Errors if the socket does not exist (use add_socket to create). Common after FBX import when SOCKET_* empties land with scale=(100,100,100) (#412).", "set_socket_transform"),
    set_property:         specBp("mutate", "Set a UPROPERTY on any loaded asset (Material, DataAsset, DataTable, SubsurfaceProfile, etc.) using a dotted path. Blueprint paths resolve to the generated-class CDO so you can author its defaults + Instanced sub-object arrays (#568). Walks nested structs, array elements by index, and instanced subobjects internally - no more read-modify-write copies (e.g. `settings.mean_free_path_distance` on a UMaterial, or `Config.Traits[1].Params.Field` on a config asset #527). Value goes through MCPJsonProperty::SetJsonOnProperty so JSON null clears object refs, structs accept {x,y,z}, arrays/maps round-trip. TMap values take { \"Key\": value } or, for struct keys, [{ key: {...}, value: ... }]; a write that cannot store every entry fails and leaves the old value untouched (#820). The write is saved to the package by default and the result reports persisted plus the package name; a write that could not reach disk (save=false, a protected mount, a read-only file, a refused save) comes back with persisted=false and persistError naming the reason instead of a bare success that reverts on the next editor start (#931) (#420).", "set_asset_property"),
    append_array_elements: specBp("mutate", "Append one or more JSON values to a reflected TArray without replacing existing entries. Supports dotted property paths plus native and user-defined USTRUCT elements. All elements are validated before mutation; returns appended indices and rollback data. The append is saved to the package by default and reports persisted / persistError the same way set_property does (#931).", "append_asset_array_elements"),
    bulk_set_properties:  specBp("mutate", "Set dotted UPROPERTY paths on as many as 500 assets in one preflighted batch. Every asset, path, and value is validated before anything is mutated, and every submitted item comes back with its own ok/status/error, so a bad path in item 300 never hides the other 499 verdicts. Default is all-or-nothing: any preflight rejection aborts before a single UObject is touched. Pass continueOnError to apply the items that did pass and keep the rejects reported alongside them. Returns per-property readback, aggregate counts, targeted save results, and a replayable rollback payload covering only the writes that landed.", "bulk_set_asset_properties"),
    bulk_read_properties: {
      ...specBp("read", "Read the SAME properties off many assets in one call, filtered and aggregated in the editor. read_properties answers one asset per call, which turns a library-wide question (concurrency settings across 582 sound assets, cull distances across 201 foliage types) into a loop. Select with assetPaths or with directory narrowed by classNames; propertyNames accepts dotted paths into nested structs (e.g. 'CullDistance.Max'), and a Blueprint path reads its generated-class CDO. Predicates, groupBy and countBy all evaluate here: filtering happens before anything crosses the wire, with the same operators as level(query_components). Absent and null are reported separately, because 'this class has no such property' and 'this property is unset' are different findings; suspect is true for either, and suspectOnly returns just those rows. Never writes, and reports dirtiedPackages (#909).", "bulk_read_asset_properties"),
      timeoutMs: 300_000,
    },
    set_texture_settings_by_type: specBp("mutate", "Apply the canonical (compressionSettings, sRGB, LOD group) combo to every texture in each group: normal -> Normalmap, grayscale -> Grayscale, baseColor -> Default sRGB, hdr -> HDR (#421).", "set_texture_settings_by_type"),
    create_interchange_pipeline: specBp("mutate", "One-call factory for a UInterchangeGenericAssetsPipeline asset with the 15-property mesh-import boilerplate already applied (RecomputeNormals=false, MikkTSpace=true, HighPrecisionTangents=true, BuildNanite=false, CreatePhysicsAsset=false, etc.) (#421).", "create_interchange_pipeline"),
    reload_package:       specBp("mutate", "Force reload an asset package from disk.", "reload_package"),
    health_check:         specBp("read", "Diagnose stuck-unloadable asset. Returns onDisk/inRegistry/isLoaded/canLoad/isStuck flags so an agent can detect the half-shutdown state where load returns null but the file exists (#279).", "asset_health_check"),
    force_reload:         specBp("mutate", "Aggressive reload from disk: closes open editors, reloads the package (rebuilding a Blueprint's class and CDO so container properties come back fresh, not just scalars), and reports objectReplaced. Refuses a dirty package unless discardUnsaved=true, and fails loudly when the editor would not release the old object rather than serving stale values (#279/#820).", "force_reload_asset"),
    export:               specBp("mutate", "Export asset to disk file (Texture2D → PNG, StaticMesh → FBX, etc.).", "export_asset"),
    search_fts:           specBp("read", "Ranked asset search (token-scored over name/class/path). Every match is scored and the ranked list is paged, so the top page is a page rather than the whole answer. maxResults is a deprecated spelling of limit.", "search_assets_fts"),
    reindex_fts:          specBp("mutate", "Rescan a content path so newly added assets on disk become searchable by search_fts.", "reindex_assets_fts"),
    get_referencers:      specBp("read", "Reverse dependency lookup (what references this) (#150). Returns {referencersByPackage, totalReferencers}.", "get_asset_referencers"),
    get_dependencies:     specBp("read", "Forward dependency lookup (what packages this asset references) (#588). Returns {dependenciesByPackage, totalDependencies}.", "get_asset_dependencies"),
    list_skeleton_bones:  specBp("read", "List bones (names + rest-pose local and component-space transforms) from a SkeletalMesh or Skeleton asset, no live actor needed (#593). Returns {bones, boneCount, sourceKind}.", "list_skeleton_bones"),
    get_primary_asset_ids: specBp("read", "Enumerate AssetManager-registered FPrimaryAssetIds (verify a primary-asset registration) (#579). Returns {primaryAssetIds:[{primaryAssetId, type, name, assetPath}], count, total}.", "get_primary_asset_ids"),
    // v1.0.0-rc.2 - #155 (asset gaps)
    set_sk_material_slots: specBp("mutate", "Set materials on a USkeletalMesh by slot name or slotIndex (bypasses the blueprint override-materials path that UE's ICH silently reverts).", "set_sk_material_slots"),
    diagnose_registry:    specBp("read", "Scan a content path and compare disk vs AssetRegistry (including in-memory pending-kill entries). Returns onDiskCount, inMemoryIncludedCount, ghostCount and paths.", "diagnose_registry"),
    get_mesh_bounds:      specBp("read", "Get StaticMesh OR SkeletalMesh bounding box. Returns min, max, boxExtent, boxCenter, meshKind (#193/#351).", "get_mesh_bounds"),
    get_mesh_info:        specBp("read", "One-call mesh QA: bounds + material slots + skeleton + LOD/vertex counts. Works for both UStaticMesh and USkeletalMesh. Returns meshKind, boundsOrigin, boundsExtent, heightM, lodCount, vertexCount, skeletonPath (skeletal only), materialSlots:[{index, slotName, materialPath, isDefaultFallback}], materialCount (#431).", "get_mesh_info"),
    read_import_sources:  specBp("read", "Read AssetImportData source filenames on an imported asset (StaticMesh, SkeletalMesh, Texture, Animation, etc.). Returns sources[] of {relativeFilename, absolutePath, timestamp, fileHash, displayLabelName} (#270).", "read_import_sources"),
    get_mesh_collision:   specBp("read", "Inspect StaticMesh collision setup. Returns collisionTraceFlag, hasSimple/ComplexCollision, element counts (#177).", "get_mesh_collision"),
    get_mesh_geometry:    specBp("read", "Read actual vertex data off a StaticMesh OR SkeletalMesh: per-section positions, uvs, normals and triangle indices, from the engine's render data (no ProceduralMeshComponent plugin needed). Triangle indices are section-local, so they index that section's own positions array; add baseVertexIndex for LOD-global indices. Over 20000 vertices inline is refused - pass dumpToFile to write the full data to a JSON file instead, or narrow with sectionIndex (#948/#926/#953).", "get_mesh_geometry"),
    measure_mesh_geometry: specBp("read", "Measure a StaticMesh OR SkeletalMesh LOD: bounds, dimensions, surfaceArea, volume, triangleCount, vertexCount, isClosed, isManifold, boundaryEdgeCount, nonManifoldEdgeCount. surfaceArea and volume are NAMED fields, never a positional pair (the engine's get_mesh_volume_area returns them in the order opposite to its name, #938). Vertices are welded by position before topology analysis so a UV seam does not read as a hole (#938/#953).", "measure_mesh_geometry"),
    read_uv_channels:     specBp("read", "Report every UV channel of a StaticMesh or SkeletalMesh LOD: bounds, UV area, unit-square coverage, overlap fraction and the triangles involved, island count, degenerate islands, seam edges, out-of-range and flipped triangles, plus which channel LightMapCoordinateIndex points at and the LOD's lightmap build settings. This is the verification half of the UV surface: every other UV action reports the same channel block after it writes, so a change can be checked rather than trusted. Overlap and coverage are RASTERISED at rasterSize rather than tested exactly, and the result says so via overlapMethod.", "read_uv_channels"),
    set_uv_channel_count: specBp("mutate", "Add, remove, resize or copy a UV channel. Channel count is a mesh-description attribute with no UPROPERTY, so asset(set_property) cannot reach it. Setting the count it already has returns existed=true and skips the rebuild. Growing has an exact inverse and the rollback restores it; remove and copy-over-existing DESTROY coordinates, so those report rollbackRestoresChannelCountOnly rather than pretending the undo is complete.", "set_uv_channel_count"),
    unwrap_uvs:           specBp("mutate", "Auto-unwrap and pack islands into one channel via Geometry Script, adding the channel if it does not exist. It converts the LOD to a DynamicMesh and back, so the WHOLE LOD is rewritten rather than only its UVs; the result says so. Pass backupToChannel for a lossless rollback, otherwise only the channel count is restorable. Without the Geometry Script plugin it returns reason='geometry_scripting_unavailable' naming what to enable instead of failing opaquely.", "unwrap_uvs"),
    transform_uvs:        specBp("mutate", "One action for every UV transform: the whole channel, chosen islands, triangles facing a direction, or one material slot, plus flips. The filter is the only thing that varies, so the maths lives in one place rather than in four near-identical actions. Edits the mesh description directly, touching nothing but the UV channel, and emits an EXACT inverse as its rollback. A zero scale component is refused because it has no inverse; an identity transform returns existed=true without rebuilding.", "transform_uvs"),
    generate_lightmap_uvs: specBp("mutate", "Apply the lightmap build settings AND run UStaticMesh::Build AND read the result back. Deliberately not a setter: bGenerateLightmapUVs, SrcLightmapIndex, DstLightmapIndex and LightMapCoordinateIndex are plain UPROPERTYs that asset(set_property) already writes, and writing them does NOTHING until the mesh rebuilds. That rebuild is the whole point. Returns the channel the build actually produced with its overlap and island report, and fails loudly when the channel did not appear. StaticMesh only. Rollback restores the settings, not the generated coordinates.", "generate_lightmap_uvs"),
    export_uv_layout:     specBp("mutate", "Render one UV channel to a PNG under Saved/UVLayouts: white wireframe, one hue per island, red where two triangles share a texel, yellow unit-square border. Returns the file path plus the same channel statistics read_uv_channels reports, so the picture and the numbers describe the same rasterisation. Use it to SEE why a lightmap bake is wrong instead of inferring it from counts.", "export_uv_layout"),
    check_uvs:            specBp("read", "One-call UV health report, the same idiom as measure_mesh_geometry. Flags a missing lightmap channel, a LightMapCoordinateIndex that disagrees with the build's DstLightmapIndex, overlap in the lightmap channel over budget, lightmap UVs outside 0..1, empty or degenerate channels and islands, and flipped triangles, each with a severity and the action that fixes it. Out-of-range UVs OUTSIDE the lightmap channel are reported as info rather than a fault, because tiling is legitimate there.", "check_uvs"),
    apply_mesh_simplify:  specBp("mutate", "Reduce a StaticMesh's triangle count while keeping its silhouette, through Geometry Script. Nine strategies because 'simplify' means different things: a target triangleCount or vertexCount, a geometric tolerance (the furthest the surface may drift), an edgeLength, a fast cluster-based edgeLength, a structural collapse of coplanar regions (planar) or of PolyGroup faces (polygroup), and the editor's own reducer (editorTriangleCount, editorVertexCount). NOT reachable through asset(set_property): a StaticMesh's LOD reduction settings build a NEW LOD, they never rewrite LOD 0's source geometry, and no UPROPERTY means 'collapse this mesh to 500 triangles'. Writes a SEPARATE asset by default (outputPath, or '<assetPath>_Simplified'), whose rollback is a complete delete; inPlace=true overwrites the source and reports that its edit has no inverse, with backupPath as the escape. Idempotent: a mesh already at or under the target reports changed=false and is not rewritten. Needs the Geometry Script engine plugin, and without it returns reason='geometry_scripting_unavailable' naming what to enable.", "apply_mesh_simplify"),
    apply_mesh_remesh:    specBp("mutate", "Rebuild a StaticMesh's triangulation at a uniform or adaptive density. Different from apply_mesh_simplify: simplify only removes triangles, remesh splits AND collapses AND flips edges to reach an even edge length, which is what a mesh needs before deformation, baking, or a convex decomposition that would otherwise follow the original triangulation's bias. Writes a SEPARATE asset by default ('<assetPath>_Remeshed'). Deliberately NOT idempotent and says so in repeatIsIdempotent: remeshing a remeshed mesh keeps moving vertices, so a repeat is a second edit rather than a no-op, which is why the separate-output default matters here more than anywhere else. Needs the Geometry Script engine plugin.", "apply_mesh_remesh"),
    apply_mesh_mirror:    specBp("mutate", "Reflect a StaticMesh across a plane, optionally cutting away the far side first and welding the seam. This is the 'model half of it and mirror' workflow, and it is real geometry rather than a negative component scale, which inverts the winding and lights wrong; no UPROPERTY on a StaticMesh mirrors its source geometry. Name the plane with axis=x|y|z through planeOrigin, or axis=custom with an explicit planeNormal. Writes a SEPARATE asset by default ('<assetPath>_Mirrored'). NOT idempotent and says so: mirroring a mirrored mesh doubles it again rather than returning the original. Needs the Geometry Script engine plugin.", "apply_mesh_mirror"),
    apply_mesh_hole_fill: specBp("mutate", "Close every open boundary loop on a StaticMesh so it becomes watertight, which is what a boolean, a voxel operation, a convex decomposition and a physics conversion all require and the most common reason those fail. WELDS FIRST by default: a great many 'holes' are not holes but duplicated vertices along a seam that no fill can close, and running the fill alone on such a mesh reports zero holes filled while the mesh stays open. Genuinely idempotent: a mesh with nothing left to fill reports existed=true and is not rewritten. Reports filledHoles, failedHoleFills, weldedOpenEdges and the before/after open-border-edge counts, so 'watertight now' can be verified rather than assumed. Needs the Geometry Script engine plugin.", "apply_mesh_hole_fill"),
    generate_mesh_collision: specBp("mutate", "Build simple collision shapes for a StaticMesh from its own geometry, or clear them. Eight methods from axis-aligned boxes to a full convex decomposition. NOT reachable through asset(set_property): UBodySetup AggGeom is a UPROPERTY, but what has to go into it is the OUTPUT of a decomposition solver running over the mesh, and there is no value a caller could supply; the shape count and trace flag stay ordinary property writes and asset(get_mesh_collision) is still the read half. Idempotent: generation from the same mesh with the same options is deterministic, so a repeat reports changed=false without rewriting the asset, compared by a structural signature (shape counts per kind, hull vertex counts, trace flag) rather than byte-for-byte. op='clear' is the remove half and needs no plugin at all. A generation that produces zero shapes is reported as a failure naming apply_mesh_hole_fill, not as a success that quietly left the mesh with no collision.", "generate_mesh_collision"),
    apply_mesh_fracture:  specBp("mutate", "Cut a StaticMesh with planes and write each resulting piece out as its own StaticMesh asset, ready to be placed, simulated or destroyed individually. Three patterns: slice (parallel cuts along one axis), grid (cuts along all three) and random (seeded planes through the bounds). WHAT THIS IS NOT: it is not Chaos destruction and produces no UGeometryCollection, because every engine entry point for that carries no UFUNCTION and reflection cannot reach it; the result says so in producesGeometryCollection=false rather than leaving it to be discovered. The source asset is never modified, so the rollback is an exact delete of the pieces it wrote. Seeded, therefore repeatable: onConflict='error' (the default) refuses rather than writing over pieces already there. dryRun lists the cut planes and every path it would write.", "apply_mesh_fracture"),
    audit_hygiene:        specBp("read", "One read-only sweep answering the questions a project accumulates answers to and never gets asked: what nothing references (unreferenced), what references nothing (brokenReferences), what exists twice (duplicates), what breaks the naming convention (naming), and what renames left behind (redirectors). asset(get_referencers) and asset(get_dependencies) answer the first two for ONE named package; this is the project-wide form, which is the one an agent needs. The unreferenced section carries a caveat to read before acting on it: an asset loaded by name from an INI setting, from C++ with a hardcoded path, or from a soft reference resolved at runtime has no package dependency and appears there while being very much in use, so treat it as candidates to review rather than a delete list. Maps, World Partition external actors and Asset Manager primary assets are already excluded for that reason. Duplicate detection by content matches class, file size and saved package hash, which finds a file copied verbatim but NOT a copy saved under a different name (the name is inside the file); duplicateMethod='name' is what catches an asset imported twice into two folders. Naming rules match on the asset's class name, so WidgetBlueprint and AnimBlueprint carry their own prefixes rather than inheriting Blueprint's, and the effective table comes back in rulesApplied. Counts are always complete; only the listings are capped by maxIssues.", "audit_asset_hygiene"),
    bulk_fix_hygiene:     specBp("mutate", "The batched fix-up for the two audit_hygiene findings a machine can act on without deciding something a person should: a name that breaks the convention (fix='naming') and an asset nothing references (fix='unreferenced'). Same preflight shape as bulk_set_properties: every candidate is validated, every candidate gets a status back including the rejected ones, and a failed preflight aborts before anything is touched unless continueOnError. THREE SAFETY RULES that are not the shape: dryRun DEFAULTS TO TRUE and the dry run names every asset with its exact destination; fix='unreferenced' MOVES assets into quarantineFolder by default rather than deleting them, and that move has an exact inverse this call emits as its rollback; and findings are recomputed here rather than taken from an audit, so an asset that gained a referencer since then is skipped with the referencer named. maxFixes caps the batch far below the audit's scan ceiling. Worlds are refused outright, because moving one without migrating its external-actor packages in the same batch orphans every actor in the level; use asset(rename). fix='redirectors' is refused too and points at asset(fixup_redirectors), which already does it properly.", "fix_asset_hygiene"),
    mesh_boolean:         specBp("mutate", "Boolean CSG between two StaticMeshes: union, subtract, intersect, trimInside, trimOutside, newPolyGroupInside, newPolyGroupOutside. The target is the mesh being cut and the tool is what cuts it, each placed by its own optional transform. Writes to a SEPARATE output asset by default (outputPath, or '<targetPath>_<Operation>' when omitted); inPlace=true opts into overwriting the target and is the only destructive form. An existing outputPath is refused unless onConflict='replace'. An empty result is refused and nothing is written unless allowEmptyResult=true, because two meshes that never overlap otherwise report success having deleted everything. Returns triangle and vertex counts for both inputs and the result, plus the written asset's triangles, vertices, LOD count, material slots and bounds, so the operation can be verified rather than trusted. dryRun runs the boolean and reports those counts without writing. Materials and simple collision are copied from the target by default. Needs the Geometry Script engine plugin: without it the call returns reason='geometry_scripting_unavailable' naming what to enable, rather than failing opaquely (#916).", "mesh_boolean"),
    migrate: {
      kind: "handler",
      effect: "mutate",
      description:
        "Copy assets and their dependencies into ANOTHER project's Content directory - the scripted form of the content browser's Migrate (#760). " +
        "destinationContentDir is the TARGET project's Content folder. " +
        "While this server drives more than one editor, a 'toEditor' parameter is offered as well: name the destination editor and its Content folder is resolved for you and its asset registry rescanned afterwards, so the assets are visible there without a manual rescan (#817). " +
        "The call runs in the editor holding the SOURCE assets, so it pushes assets out of the project it is attached to. " +
        "Unsaved or never-saved assets are refused, because migrate copies files and would otherwise silently omit your edits. " +
        "Every asset is resolved before anything is copied, and the destination is checked for the packages afterwards rather than reporting success on the call returning. " +
        "Params: assetPaths (string[]) or assetPath, toEditor OR destinationContentDir, includeDependencies? (default true), onConflict? (skip|overwrite, default skip), allowDirty?, dryRun?",
      destinationEditor: true,
      handler: async (ctx, p) => migrateAssets(ctx, p),
    },
    move_folder:          specBp("mutate", "Move/rename entire content folder with redirector fixup in one transaction. Refused with reason=package_objects_left_behind, before anything moves, when a package in the folder holds an object the move would leave at the old path (#1137) (#192).", "move_folder"),
    create_folder:        specBp("mutate", "Create empty content browser folder(s); path and paths combine. Returns per-path created/existed/failed (#212).", "create_folder"),
    delete_folder:        specBp("mutate", "Delete content browser folder(s) - counterpart to delete_asset, which leaves the parent directory entry behind as an orphan. Empty folders only by default; pass force=true to also delete any assets still inside (Content Browser 'Delete folder' equivalent). A World Partition map's __ExternalActors__ and __ExternalObjects__ packages for the folder go with it and are counted in externalPackagesDeleted; packages still loaded are kept and reported. Per-path status (deleted/absent/failed) with reason (invalid_path/protected_path/not_empty/delete_failed) and a sample of contained assets on not_empty entries.", "delete_folder"),
    set_mesh_nav:         specBp("mutate", "Set StaticMesh nav contribution (#167).", "set_mesh_nav"),
    create_user_defined_enum: specBp("mutate", "Create a UserDefinedEnum content asset, optionally pre-populated with values (#686).", "create_user_defined_enum"),
    list_enum_values:     specBp("read", "List a UEnum's enumerators (index, authored short name, display name, value). Works on native and UserDefinedEnum assets (#686).", "list_enum_values"),
    edit_user_defined_enum: specBp("mutate", "Author a UserDefinedEnum content asset. op=add_value appends an enumerator (authored name is auto-assigned; pass displayName - or name - to set the editable display text). op=rename_value sets a new displayName on the enumerator resolved by index or name (matches short or display name). op=remove_value deletes it. Recompiles dependents automatically. Native UEnums are not editable (#686).", "edit_user_defined_enum"),
    create_user_defined_struct: specBp("mutate", "Create a UserDefinedStruct content asset, optionally pre-populated with fields (#735).", "create_user_defined_struct"),
    list_struct_fields:   specBp("read", "List a UserDefinedStruct's members (index, internal name, friendly/display name, GUID, type label). Use this to find the GUID for a stable rename/retype. Native structs are not editable (#735).", "list_struct_fields"),
    edit_user_defined_struct: specBp("mutate", "Author a UserDefinedStruct content asset. op=add_field appends a member (type via MakePinType string; pass fieldName for its display name). op=rename_field sets a new newDisplayName on the member resolved by fieldGuid or fieldName - the member GUID is preserved so existing Blueprint pins and DataTable rows survive. op=set_field_type changes a member's type. op=remove_field deletes it. Recompiles dependents automatically. Native structs are not editable (#735).", "edit_user_defined_struct"),
    rename_struct_field:  specBp("mutate", "Rename a UserDefinedStruct field's display name while preserving its member GUID, so Blueprint pins and DataTable rows keyed off it survive. The same edit as edit_user_defined_struct(op=rename_field). fieldName matches the friendly or internal name (#735).", "rename_struct_field"),
    // Per-asset exclusive locking for concurrent agents. The lock registry
    // lives in the bridge (the shared editor), keyed by asset path with a TTL
    // so a crashed session never wedges an asset. sessionId defaults to this
    // server process; pass it explicitly to coordinate across processes.
    lock: {
      kind: "handler",
      effect: "mutate",
      description: "Acquire an exclusive lock on an asset for this editor. Returns acquired=true, or acquired=false with holder{sessionId,ttlSecondsRemaining} when another session holds it. Params: assetPath, ttlSeconds? (default 300), sessionId?",
      handler: async (ctx, p) => ctx.bridge.call("acquire_lock", {
        path: p.assetPath ?? p.path,
        sessionId: lockOwner(ctx, p),
        ttlSeconds: p.ttlSeconds,
      }),
    },
    unlock: {
      kind: "handler",
      effect: "mutate",
      description: "Release an asset lock held by this editor (or force=true to break any holder's lock). Params: assetPath, force?, sessionId?",
      handler: async (ctx, p) => ctx.bridge.call("release_lock", {
        path: p.assetPath ?? p.path,
        sessionId: lockOwner(ctx, p),
        force: p.force,
      }),
    },
    list_locks:           specBp("read", "List all currently-held asset locks with holder session id, acquiredAt, and ttlSecondsRemaining.", "list_locks"),
    unlock_all: {
      kind: "handler",
      effect: "mutate",
      description: "Release every lock held by one session in a single call, returning the number released. Defaults to the addressed editor's own session; pass sessionId to clear a different one (for example after a crashed session left assets wedged). Params: sessionId?",
      handler: async (ctx, p) => ctx.bridge.call("release_session_locks", {
        sessionId: lockOwner(ctx, p),
      }),
    },
    diff:                 specBp("read", "Semantic structural diff between two assets, dispatching on the asset's class. Blueprints: parent class, variables, functions, components, per-graph node and connection deltas. Skeleton and SkeletalMesh: raw bone additions and removals, reparenting (bone, fromParent, toParent), raw-index changes (bone, fromIndex, toIndex), and declared virtual-bone additions and removals, with hierarchyCompatible and editorCompatible reported SEPARATELY. That separation is the point: it answers whether two skeletons are bone-compatible enough to register as Compatible Skeletons or whether a retarget is required, because appending bones (virtual ones especially) leaves the shared hierarchy intact while reparenting an existing bone does not (#879). Deliberately OUT of scope and reported as such: reference-pose transforms (referencePoseCompared=false), export names (exportNamesCompared=false), sockets and retarget sources; structureScope spells the boundary out in the result. Both paths must be the same class. Other asset types report that diffing is not supported yet rather than failing opaquely.", "diff_asset"),
    ...epicActions,
  },
  {
    ...epicSchema,
    // #1057: every key a spec'd handler declares, generated from its C++
    // registration. A key listed again below is shared with hand-written
    // actions, and tests/unit/handler-specs.test.ts holds the two to one type.
    ...specSchema,
    // Spec'd keys whose zod refinements (bounds, integer, non-empty) the spec
    // types cannot carry. Each matches its spec's type, which
    // tests/unit/handler-specs.test.ts holds it to.
    width: z.number().int().min(1).max(8192).optional().describe("create_render_target_2d: pixel width, 1-8192 (default 512)"),
    height: z.number().int().min(1).max(8192).optional().describe("create_render_target_2d: pixel height, 1-8192 (default 512)"),
    targetGamma: z.number().min(0).optional().describe("create_render_target_2d: target gamma (default 0 uses engine behavior)"),
    elements: z.array(z.unknown()).min(1).optional().describe("append_array_elements: one or more values to append after full prevalidation"),
    assetLodIndex: z.number().int().min(0).optional().describe("bind_cloth_to_section: which LOD inside the clothing asset to bind (default: lodIndex, clamped to the asset's LOD count) (#1139)"),
    sectionIndex: z.number().int().min(0).optional().describe("get_mesh_geometry / measure_mesh_geometry: restrict to one render section; omit for every section. bind_cloth_to_section / unbind_cloth_from_section: the render section (required)"),
    uvChannel: z.number().int().min(0).optional().describe("get_mesh_geometry: which UV channel to return (default 0)"),
    assignments: z.array(z.object({
      assetPath: z.string().min(1),
      materialPath: z.string().min(1),
      slotName: z.string().optional(),
      slotIndex: z.number().int().optional(),
    })).min(1).max(500).optional().describe("set_mesh_materials_batch entries: [{assetPath, materialPath, slotName? | slotIndex?}] (max 500). slotName is preferred for imported kits because slot indices are not stable across reimports"),
    // Three handlers share this key with three element shapes, and one key has
    // one type, so each element's fields are checked by its handler.
    items: z.array(z.record(z.unknown())).min(1).max(500).optional().describe("Batch entries (max 500): import_texture_batch takes {filePath, packagePath?, name?, replaceExisting?}; bulk_set_properties takes {assetPath, properties}; bulk_upsert_data_assets takes {name, packagePath, className, properties?}"),
    expandDepth: z.number().int().min(0).max(5).optional().describe("read_properties / get_properties: inline owned subobjects to this depth, 0-5 (default 0) (#755)"),
    maxExpandedObjects: z.number().int().positive().optional().describe("read_properties / get_properties: cap on expanded objects (default 64) (#755)"),
    lodIndex: z.number().int().min(0).optional().describe("get_mesh_geometry / measure_mesh_geometry: which LOD to read (default 0). mesh_boolean: which LOD of each input mesh to read (default 0). skeletal mesh build-setting and skin-weight actions: which source LOD to target (default 0). UV actions: which LOD to act on (default 0). bind_cloth_to_section / unbind_cloth_from_section: the mesh LOD holding the section (required)"),
    vertexIndices: z.array(z.number().int().min(0)).min(1).max(256).optional().describe("read_skeletal_mesh_skin_weights: source MeshDescription vertex IDs to read (1-256)"),
    profileName: z.string().min(1).optional().describe("skeletal mesh skin-weight actions: existing skin-weight profile; omit or pass 'default' for the default profile"),
    // The influence entries have no field declaration a spec can carry, so the
    // handler checks them, before anything is written.
    edits: z.array(z.object({
      vertexIndex: z.number().int().min(0),
      influences: z.array(z.record(z.unknown())).min(1).max(64),
    })).min(1).max(256).optional().describe("set_skeletal_mesh_skin_weights: selected source vertices and their complete replacement influences, each {boneName, weight? (0 to 1) | rawWeight? (1 to 65535)}"),
    // The IMC actions dispatch to gameplay handlers, so their keys come from
    // that spec. `key` is also the StringTable entry key, so it names both.
    ...borrowSchema(gameplaySpecSchema, ["mappingContext", "inputAction", "imcPath", "inputActionPath", "mappingIndex"]),
    key: z.string().optional().describe("Key name for add_input_mapping or StringTable entry key (e.g. 'Mouse2D', 'LeftMouseButton') (#525)"),
    // Hand-written actions only: search, migrate and the locks.
    searchAll: z.boolean().optional().describe("Search all content roots (plugins, engine content) not just /Game/"),
    exportName: z.string().optional(),
    allowDirty: z.boolean().optional().describe("migrate: migrate the on-disk version of an asset with unsaved edits (#760)"),
    destinationContentDir: z.string().optional().describe("migrate: the TARGET project's Content folder (#760)"),
    includeDependencies: z.boolean().optional().describe("migrate: also copy referenced assets (default true) (#760)"),
    // lock / unlock / unlock_all all default this to the server process's own
    // session id; it is only passed explicitly to coordinate across processes.
    sessionId: z.string().optional().describe("lock / unlock / unlock_all: owning session id (defaults to this server process)"),
    ttlSeconds: z.number().optional().describe("lock: seconds before the lock auto-expires (default 300)"),
  },
);
