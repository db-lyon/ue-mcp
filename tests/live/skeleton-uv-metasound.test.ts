/**
 * Skeleton editing, UV authoring and MetaSound introspection, live (V3, V2, T13).
 *
 * The three share a property: each reaches state that no reflection path
 * exposes, so a unit test can only assert the schema and never the effect.
 * Bone translation retargeting lives in the skeleton's private BoneTree, UV
 * channels are mesh-description attributes rather than UPROPERTYs, and a
 * MetaSound's graph is a document behind a builder API.
 *
 * The fixture is built by DUPLICATING engine assets into /Game, because the
 * bridge refuses to mutate /Engine and the test project ships no skeletal mesh
 * of its own. The skeletal mesh's Skeleton reference is repointed at the
 * duplicated skeleton, so every write these cases make lands on an asset this
 * file created and deletes.
 *
 * The MetaSound fixture is authored rather than duplicated, because authoring it
 * IS the thing under test: the seven read actions only mean anything if what
 * audio(metasound_author) wrote is what they read back.
 *
 * Two defects these cases pin, both of which reported success while doing
 * nothing:
 *
 * - A MetaSoundSource created without UMetaSoundSourceFactory has a completely
 *   blank document: no interfaces, no dependencies, no graph pages. It loads,
 *   it is a valid MetaSoundSource, and there is nothing in it. So the fixture
 *   asserts the document declares interfaces, which is false for any asset that
 *   skipped the factory.
 * - UMetaSoundBuilderBase::BuildAndOverwriteMetaSound cannot write to an asset
 *   ("Not permissible to overwrite MetaSound asset, only transient MetaSound"),
 *   and says so by doing nothing. Authoring now attaches its builder to the
 *   asset's own document through the document builder registry, so the cases
 *   read the graph back after a build and expect the nodes to be there.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { LiveServer, resultJson } from "./server.js";
import { closeLiveBridges, liveTarget } from "./harness.js";

const target = await liveTarget();
let server: LiveServer;

const ROOT = "/Game/MCPLive/SkelUV";
const STATIC_MESH = `${ROOT}/SM_MCPUVCube`;
/** The MetaSound fixture. Package form; the object form is what the actions
 *  resolve to and what the reads report back as `path`. */
const METASOUND = `${ROOT}/MS_MCPProbe`;
const METASOUND_OBJECT = `${METASOUND}.MS_MCPProbe`;
const SKELETAL_MESH = `${ROOT}/SKM_MCPProbe`;
const SKELETON = `${ROOT}/SK_MCPProbe`;
/** Where the fixture came from, and the skeleton a compatibility registration
 *  can point at without writing to it. */
const SOURCE_CUBE = "/Engine/BasicShapes/Cube";
const SOURCE_SKELETAL_MESH = "/Engine/EngineMeshes/SkeletalCube";
const SOURCE_SKELETON = "/Engine/EngineMeshes/SkeletalCube_Skeleton";

const call = async (tool: string, args: Record<string, unknown>) =>
  server.call(tool, { ...args, timeoutMs: 300_000 });

const asset = async (action: string, args: Record<string, unknown> = {}) =>
  resultJson<Record<string, any>>(await call("asset", { action, ...args }));

const animation = async (action: string, args: Record<string, unknown> = {}) =>
  resultJson<Record<string, any>>(await call("animation", { action, ...args }));

/** Remove the fixture, whoever left it. Run before the build as well as after,
 *  so a run never inherits the channel count or curve table a previous one left. */
const wipeFixture = async (): Promise<void> => {
  for (const assetPath of [STATIC_MESH, SKELETAL_MESH, SKELETON, METASOUND]) {
    try {
      await call("asset", { action: "delete", assetPath, force: true });
    } catch {
      // Absent is the desired state; delete reports it rather than throwing.
    }
  }
};

beforeAll(async () => {
  server = await LiveServer.start({ projects: [target.uproject] });
  await wipeFixture();

  await asset("duplicate", { sourcePath: SOURCE_CUBE, destinationPath: STATIC_MESH });
  await asset("duplicate", { sourcePath: SOURCE_SKELETON, destinationPath: SKELETON });
  await asset("duplicate", { sourcePath: SOURCE_SKELETAL_MESH, destinationPath: SKELETAL_MESH });
  // A duplicated skeletal mesh still points at the ENGINE skeleton, and every
  // skeleton write below would then be refused as a protected mount.
  await asset("set_property", {
    assetPath: SKELETAL_MESH,
    propertyName: "Skeleton",
    value: `${SKELETON}.SK_MCPProbe`,
  });
}, 600_000);

afterAll(async () => {
  try {
    await wipeFixture();
    await call("asset", { action: "delete_folder", path: ROOT, force: true });
  } catch {
    // Leftovers are noise, not a failure.
  }
  await server?.close();
  closeLiveBridges();
}, 300_000);

// ---------------------------------------------------------------------------
// V2 - UV mapping
// ---------------------------------------------------------------------------

interface UvRead {
  success?: boolean;
  channelCount?: number;
  lightmapCoordinateIndex?: number;
  buildSettings?: { bGenerateLightmapUVs?: boolean; dstLightmapIndex?: number };
  channels?: Array<{
    channel?: number;
    isLightmapChannel?: boolean;
    islandCount?: number;
    seamEdgeCount?: number;
    bounds?: { uMin?: number; uMax?: number; vMin?: number; vMax?: number; withinUnitSquare?: boolean };
  }>;
}

const readUvs = async (): Promise<UvRead> =>
  resultJson<UvRead>(await call("asset", { action: "read_uv_channels", assetPath: STATIC_MESH }));

describe("UV channels", () => {
  it("measures every channel, which get_mesh_geometry could not", async () => {
    const body = await readUvs();
    expect(body.success).not.toBe(false);
    expect(body.channelCount ?? 0).toBeGreaterThan(0);
    expect(body.channels).toHaveLength(body.channelCount ?? 0);

    // get_mesh_geometry peeked at one channel off RENDER data. This reads the
    // mesh description, so it can report islands and seams at all.
    for (const channel of body.channels ?? []) {
      expect(channel.bounds, `channel ${channel.channel} has no bounds`).toBeTruthy();
      expect(typeof channel.islandCount).toBe("number");
      expect(typeof channel.seamEdgeCount).toBe("number");
    }
    // Which channel the lightmap uses is the fact every lightmap question
    // starts from, and it is on the mesh rather than on the UVs.
    expect(typeof body.lightmapCoordinateIndex).toBe("number");
  }, 300_000);

  it("adds a channel, and the read reflects it", async () => {
    const before = await readUvs();
    const body = resultJson<{ success?: boolean; previousChannelCount?: number; newChannelCount?: number }>(
      await call("asset", { action: "set_uv_channel_count", assetPath: STATIC_MESH, op: "add", save: true }),
    );
    expect(body.success).not.toBe(false);
    expect(body.previousChannelCount).toBe(before.channelCount);
    expect(body.newChannelCount).toBe((before.channelCount ?? 0) + 1);

    const after = await readUvs();
    expect(after.channelCount).toBe((before.channelCount ?? 0) + 1);
  }, 300_000);

  it("dryRun changes nothing", async () => {
    const before = await readUvs();
    const body = resultJson<{ success?: boolean; dryRun?: boolean }>(
      await call("asset", { action: "set_uv_channel_count", assetPath: STATIC_MESH, op: "add", dryRun: true }),
    );
    expect(body.success).not.toBe(false);
    expect(body.dryRun).toBe(true);

    const after = await readUvs();
    expect(after.channelCount).toBe(before.channelCount);
  }, 300_000);

  it("reports UV health as named issues rather than a bare score", async () => {
    const body = resultJson<{
      success?: boolean;
      issues?: Array<{ code?: string; severity?: string; message?: string; channel?: number }>;
      errorCount?: number;
      warningCount?: number;
      healthy?: boolean;
    }>(await call("asset", { action: "check_uvs", assetPath: STATIC_MESH }));

    expect(body.success).not.toBe(false);
    expect(Array.isArray(body.issues)).toBe(true);
    // The empty channel just added is an error, and it has to be named as one:
    // a health check that answers with a number is not actionable.
    expect(body.errorCount ?? 0).toBeGreaterThan(0);
    expect(body.healthy).toBe(false);
    for (const issue of body.issues ?? []) {
      expect(issue.code, "an issue with no code").toBeTruthy();
      expect(issue.severity, `${issue.code} has no severity`).toBeTruthy();
      expect((issue.message ?? "").length).toBeGreaterThan(10);
    }
    expect((body.issues ?? []).map((i) => i.code)).toContain("channel_empty");
  }, 300_000);

  it("transforms a channel and the measured bounds move with it", async () => {
    // The assertion that makes this more than a dispatch check: a translate of
    // 0.25 in U has to show up as a 0.25 shift in the channel's own bounds.
    const before = await readUvs();
    const beforeBounds = (before.channels ?? []).find((c) => c.channel === 0)?.bounds;
    expect(beforeBounds, "channel 0 has no bounds to compare against").toBeTruthy();

    const body = resultJson<{
      success?: boolean;
      updated?: boolean;
      transformedVertexInstances?: number;
      channelAfter?: { bounds?: { uMin?: number; uMax?: number; vMin?: number } };
    }>(await call("asset", {
      action: "transform_uvs",
      assetPath: STATIC_MESH,
      channel: 0,
      translate: { u: 0.25, v: 0 },
      save: true,
    }));

    expect(body.success).not.toBe(false);
    expect(body.updated).toBe(true);
    expect(body.transformedVertexInstances ?? 0).toBeGreaterThan(0);
    expect(body.channelAfter?.bounds?.uMin).toBeCloseTo((beforeBounds!.uMin ?? 0) + 0.25, 4);
    expect(body.channelAfter?.bounds?.uMax).toBeCloseTo((beforeBounds!.uMax ?? 0) + 0.25, 4);
    // V is untouched, so a transform that moved everything would be caught.
    expect(body.channelAfter?.bounds?.vMin).toBeCloseTo(beforeBounds!.vMin ?? 0, 4);
  }, 300_000);

  it("unwrap dryRun reports the method it would use and writes nothing", async () => {
    const before = await readUvs();
    const body = resultJson<{ success?: boolean; dryRun?: boolean; written?: boolean; method?: string; error?: string }>(
      await call("asset", { action: "unwrap_uvs", assetPath: STATIC_MESH, channel: 0, dryRun: true }),
    );
    if (body.success === false) {
      // Geometry Script is reached by reflection and may be absent; the
      // degradation has to name itself rather than fail anonymously.
      expect(body.error).toContain("geometry");
      return;
    }
    expect(body.written).toBe(false);
    expect(body.method).toBeTruthy();

    const after = await readUvs();
    expect(after.channelCount).toBe(before.channelCount);
  }, 300_000);

  it("exports a layout image whose island count matches the read", async () => {
    const read = await readUvs();
    const islands = (read.channels ?? []).find((c) => c.channel === 0)?.islandCount;

    const body = resultJson<{
      success?: boolean;
      outputPath?: string;
      imageSize?: number;
      fileSizeBytes?: number;
      channelStats?: { islandCount?: number; channel?: number };
    }>(await call("asset", { action: "export_uv_layout", assetPath: STATIC_MESH, channel: 0 }));

    expect(body.success).not.toBe(false);
    expect(body.outputPath).toContain(".png");
    expect(body.imageSize ?? 0).toBeGreaterThan(0);
    expect(body.fileSizeBytes ?? 0).toBeGreaterThan(0);
    // The image is drawn from the same measurement read_uv_channels reports, so
    // a picture that disagreed with the numbers would be caught here.
    expect(body.channelStats?.channel).toBe(0);
    expect(body.channelStats?.islandCount).toBe(islands);
  }, 300_000);

  it("generate_lightmap_uvs rebuilds rather than only setting a flag", async () => {
    // The whole reason this is a handler and not a property write: the build
    // settings mean nothing until UStaticMesh::Build runs.
    const body = resultJson<{
      success?: boolean;
      rebuilt?: boolean;
      unchanged?: boolean;
      destinationChannelProduced?: boolean;
      resultingLightmapCoordinateIndex?: number;
    }>(await call("asset", { action: "generate_lightmap_uvs", assetPath: STATIC_MESH, save: true }));

    expect(body.success).not.toBe(false);
    expect(body.rebuilt).toBe(true);
    expect(body.destinationChannelProduced).toBe(true);

    const after = await readUvs();
    expect(after.buildSettings?.bGenerateLightmapUVs).toBe(true);
    expect(after.lightmapCoordinateIndex).toBe(body.resultingLightmapCoordinateIndex);
  }, 300_000);
});

// ---------------------------------------------------------------------------
// V3 - skeleton editing
// ---------------------------------------------------------------------------

describe("skeleton editing sessions", () => {
  it("opens a session over a working copy and cancels without touching disk", async () => {
    const begun = resultJson<{
      success?: boolean;
      sessionTag?: string;
      skeletonPath?: string;
      boneCount?: number;
      bones?: string[];
      created?: boolean;
    }>(await call("animation", { action: "begin_skeleton_edit", skeletalMeshPath: SKELETAL_MESH }));

    expect(begun.success).not.toBe(false);
    expect(begun.sessionTag).toBeTruthy();
    expect(begun.boneCount ?? 0).toBeGreaterThan(0);
    expect(begun.bones?.length).toBe(begun.boneCount);
    // The session has to resolve the skeleton too: every skeleton-scoped action
    // below addresses that path rather than the mesh.
    expect(begun.skeletonPath).toContain("SK_MCPProbe");

    const cancelled = resultJson<{ success?: boolean; cancelled?: boolean; committed?: boolean }>(
      await call("animation", { action: "cancel_skeleton_edit", sessionTag: begun.sessionTag }),
    );
    expect(cancelled.success).not.toBe(false);
    expect(cancelled.cancelled).toBe(true);
    expect(cancelled.committed).toBe(false);
  }, 300_000);

  it("cancelling a closed session reports alreadyClosed, so a rollback replays", async () => {
    const body = resultJson<{ success?: boolean; alreadyClosed?: boolean; cancelled?: boolean }>(
      await call("animation", { action: "cancel_skeleton_edit", sessionTag: "MCPNoSuchSession" }),
    );
    expect(body.success).not.toBe(false);
    expect(body.alreadyClosed).toBe(true);
    expect(body.cancelled).toBe(false);
  }, 300_000);

  it("validates the whole batch before mutating anything", async () => {
    const begun = resultJson<{ sessionTag?: string; bones?: string[] }>(
      await call("animation", { action: "begin_skeleton_edit", skeletalMeshPath: SKELETAL_MESH }),
    );
    const root = (begun.bones ?? [])[0];
    expect(root, "the fixture skeleton reported no bones").toBeTruthy();

    // Entry two is impossible. Entry one must not have been applied.
    const body = resultJson<{ success?: boolean; error?: string; errorCode?: string }>(
      await call("animation", {
        action: "edit_skeleton_bones",
        sessionTag: begun.sessionTag,
        edits: [
          { op: "add", bone: "MCPProbeBone", parent: root },
          { op: "reparent", bone: "NotABoneAtAll", parent: root },
        ],
      }),
    );
    expect(body.success).toBe(false);
    expect(body.errorCode).toBe("bone_not_found");
    expect(body.error).toContain("NotABoneAtAll");

    const cancelled = resultJson<{ discardedEditCount?: number }>(
      await call("animation", { action: "cancel_skeleton_edit", sessionTag: begun.sessionTag }),
    );
    // Nothing was applied, so there is nothing to discard. A batch that had
    // half-applied would report one here.
    expect(cancelled.discardedEditCount).toBe(0);
  }, 300_000);
});

describe("skeleton properties with no UPROPERTY", () => {
  it("writes and restores bone translation retargeting", async () => {
    const changed = resultJson<{
      success?: boolean;
      updated?: boolean;
      bonesChanged?: number;
      bonesInspected?: number;
      bones?: Array<{ bone?: string; priorMode?: string; mode?: string; changed?: boolean }>;
      rollback?: { method?: string; payload?: { restore?: Array<{ bone: string; mode: string }> } };
    }>(await call("animation", { action: "set_bone_retargeting", skeletonPath: SKELETON, mode: "Skeleton" }));

    expect(changed.success).not.toBe(false);
    expect(changed.updated).toBe(true);
    expect(changed.bonesChanged ?? 0).toBeGreaterThan(0);
    expect(changed.bonesInspected).toBe(changed.bones?.length);
    for (const bone of changed.bones ?? []) {
      expect(bone.mode).toBe("Skeleton");
      expect(bone.priorMode).toBe("Animation");
    }

    // The rollback is a per-bone restore replayed through this same action.
    const restore = changed.rollback?.payload?.restore;
    expect(changed.rollback?.method).toBe("set_bone_retargeting");
    expect(restore?.length).toBe(changed.bones?.length);

    const back = resultJson<{ success?: boolean; bones?: Array<{ mode?: string }> }>(
      await call("animation", { action: "set_bone_retargeting", skeletonPath: SKELETON, restore }),
    );
    expect(back.success).not.toBe(false);
    for (const bone of back.bones ?? []) expect(bone.mode).toBe("Animation");

    const again = resultJson<{ alreadyInMode?: boolean; bonesChanged?: number }>(
      await call("animation", { action: "set_bone_retargeting", skeletonPath: SKELETON, mode: "Animation" }),
    );
    expect(again.alreadyInMode).toBe(true);
    expect(again.bonesChanged).toBe(0);
  }, 300_000);

  it("authors a blend profile, which is a subobject set_property cannot create", async () => {
    const created = resultJson<{
      success?: boolean;
      created?: boolean;
      entryCount?: number;
      entries?: Array<{ bone?: string; scale?: number }>;
      blendProfiles?: string[];
      saved?: boolean;
    }>(await call("animation", {
      action: "author_blend_profile",
      skeletonPath: SKELETON,
      profileName: "MCPLiveProfile",
      entries: [{ bone: "Bone01", scale: 0.5 }],
    }));

    expect(created.success).not.toBe(false);
    expect(created.created).toBe(true);
    expect(created.entryCount).toBe(1);
    expect(created.entries?.[0]?.scale).toBe(0.5);
    expect(created.blendProfiles).toContain("MCPLiveProfile");
    expect(created.saved).toBe(true);

    const removed = resultJson<{
      success?: boolean;
      deleted?: boolean;
      removedEntryCount?: number;
      removedEntries?: Array<{ bone?: string; scale?: number }>;
      blendProfiles?: string[];
      rollback?: { method?: string; payload?: { operation?: string } };
    }>(await call("animation", {
      action: "author_blend_profile",
      skeletonPath: SKELETON,
      profileName: "MCPLiveProfile",
      operation: "remove",
    }));

    expect(removed.deleted).toBe(true);
    // The removal reports what it destroyed, which is the only way its inverse
    // can rebuild the profile.
    expect(removed.removedEntryCount).toBe(1);
    expect(removed.removedEntries?.[0]?.bone).toBe("Bone01");
    expect(removed.blendProfiles).not.toContain("MCPLiveProfile");
    expect(removed.rollback?.payload?.operation).toBe("upsert");
  }, 300_000);

  it("curve metadata edits are idempotent in both directions", async () => {
    const add = resultJson<{
      success?: boolean;
      curvesAdded?: number;
      alreadyUpToDate?: boolean;
      curveMetadata?: Array<{ curve?: string; material?: boolean; morphTarget?: boolean }>;
    }>(await call("animation", { action: "edit_curve_metadata", skeletonPath: SKELETON, add: ["MCPLiveCurve"] }));
    expect(add.success).not.toBe(false);
    expect(add.curvesAdded).toBe(1);
    expect((add.curveMetadata ?? []).map((c) => c.curve)).toContain("MCPLiveCurve");

    const again = resultJson<{ curvesAdded?: number; curvesAlreadyPresent?: number; alreadyUpToDate?: boolean }>(
      await call("animation", { action: "edit_curve_metadata", skeletonPath: SKELETON, add: ["MCPLiveCurve"] }),
    );
    expect(again.curvesAdded).toBe(0);
    expect(again.curvesAlreadyPresent).toBe(1);
    expect(again.alreadyUpToDate).toBe(true);

    const flags = resultJson<{
      success?: boolean;
      flagsChanged?: number;
      curveMetadata?: Array<{ curve?: string; material?: boolean; morphTarget?: boolean }>;
    }>(await call("animation", {
      action: "edit_curve_metadata",
      skeletonPath: SKELETON,
      flags: [{ curve: "MCPLiveCurve", morphTarget: true }],
    }));
    expect(flags.success).not.toBe(false);
    expect(flags.flagsChanged).toBe(1);
    expect((flags.curveMetadata ?? []).find((c) => c.curve === "MCPLiveCurve")?.morphTarget).toBe(true);

    const removed = resultJson<{ curvesRemoved?: number }>(
      await call("animation", { action: "edit_curve_metadata", skeletonPath: SKELETON, remove: ["MCPLiveCurve"] }),
    );
    expect(removed.curvesRemoved).toBe(1);

    const removeAgain = resultJson<{ success?: boolean; curvesRemoved?: number; curvesAlreadyAbsent?: number }>(
      await call("animation", { action: "edit_curve_metadata", skeletonPath: SKELETON, remove: ["MCPLiveCurve"] }),
    );
    expect(removeAgain.success).not.toBe(false);
    expect(removeAgain.curvesRemoved).toBe(0);
    expect(removeAgain.curvesAlreadyAbsent).toBe(1);
  }, 300_000);

  it("registers and unregisters a compatible skeleton, closing the loop diff opens", async () => {
    // asset(diff) computes hierarchyCompatible specifically to answer "can
    // these register as compatible skeletons" and had no way to act on it.
    const unregistered = resultJson<{ success?: boolean; changed?: number; compatibleSkeletons?: string[] }>(
      await call("animation", {
        action: "register_compatible_skeleton",
        skeletonPath: SKELETON,
        compatibleSkeletonPath: SOURCE_SKELETON,
        remove: true,
      }),
    );
    expect(unregistered.success).not.toBe(false);
    expect((unregistered.compatibleSkeletons ?? []).join(" ")).not.toContain("SkeletalCube_Skeleton");

    const registered = resultJson<{
      success?: boolean;
      changed?: number;
      compatibleSkeletons?: string[];
      results?: Array<{ registered?: boolean; engineReportsCompatible?: boolean; boneCount?: number }>;
    }>(await call("animation", {
      action: "register_compatible_skeleton",
      skeletonPath: SKELETON,
      compatibleSkeletonPath: SOURCE_SKELETON,
    }));

    expect(registered.success).not.toBe(false);
    expect(registered.changed).toBe(1);
    expect((registered.compatibleSkeletons ?? []).join(" ")).toContain("SkeletalCube_Skeleton");
    // The engine's own compatibility check travels with the answer, so a
    // registration the engine will not honour is visible rather than silent.
    expect(registered.results?.[0]?.registered).toBe(true);
    expect(registered.results?.[0]?.engineReportsCompatible).toBe(true);
    expect(registered.results?.[0]?.boneCount ?? 0).toBeGreaterThan(0);

    const replay = resultJson<{ alreadyRegistered?: boolean; changed?: number }>(
      await call("animation", {
        action: "register_compatible_skeleton",
        skeletonPath: SKELETON,
        compatibleSkeletonPath: SOURCE_SKELETON,
      }),
    );
    expect(replay.alreadyRegistered).toBe(true);
    expect(replay.changed).toBe(0);
  }, 300_000);
});

// ---------------------------------------------------------------------------
// T13 - MetaSound introspection
// ---------------------------------------------------------------------------

describe("MetaSound introspection", () => {
  it("lists the node classes an add_node call can name", async () => {
    const body = resultJson<{
      success?: boolean;
      nodeClasses?: Array<{ name?: string; namespace?: string; variant?: string }>;
      count?: number;
    }>(await call("audio", { action: "metasound_list_node_classes", filter: "sine" }));

    expect(body.success).not.toBe(false);
    // The listing exists so a caller can name a class; an entry with no
    // namespace or variant cannot be handed back to metasound_add_node.
    const sine = (body.nodeClasses ?? []).find((c) => c.name === "Sine");
    expect(sine, "the curated class list has no Sine").toBeTruthy();
    expect(sine?.namespace).toBeTruthy();
    expect(sine?.variant).toBeTruthy();
  }, 300_000);

  it("names the asset that is missing rather than failing anonymously", async () => {
    const body = resultJson<{ success?: boolean; error?: string }>(
      await call("audio", { action: "metasound_read_document", assetPath: "/Game/MCPNoSuchMetaSound" }),
    );
    expect(body.success).toBe(false);
    expect(body.error).toContain("/Game/MCPNoSuchMetaSound");
  }, 300_000);

  it("refuses a path that is not a MetaSound, naming what it found instead", async () => {
    // Resolution fails before the document is touched, which is what makes
    // this case safe to run while the default-page assert below is unfixed.
    const body = resultJson<{ success?: boolean; error?: string }>(
      await call("audio", { action: "metasound_read_document", assetPath: STATIC_MESH }),
    );
    expect(body.success).toBe(false);
    expect(body.error).toContain("StaticMesh");
    expect(body.error).toContain("MetaSound");
  }, 300_000);

  it("distinguishes a path that names no MetaSound from one that does", async () => {
    // This case described the OLD behaviour, where metasound_get_graph
    // answered success for a path naming nothing and the only usable signal
    // was an assetExists flag. That was the bug. The handler now refuses, and
    // the refusal is worth asserting in detail: an error that says only "not
    // found" leaves a caller unable to tell a typo from an unsaved asset, so
    // it reports both halves of the lookup it actually tried.
    const missing = resultJson<{
      success?: boolean;
      error?: string;
      packageExistsOnDisk?: boolean;
      registryMatched?: boolean;
      reason?: string;
    }>(await call("audio", { action: "metasound_get_graph", assetPath: "/Game/MCPNoSuchMetaSound" }));

    expect(missing.success).toBe(false);
    expect(missing.reason).toBe("missing");
    expect(missing.packageExistsOnDisk).toBe(false);
    expect(missing.registryMatched).toBe(false);
    // The message names the path and both spellings it resolved, so the caller
    // can see whether the object-name half was the problem.
    expect(missing.error).toContain("/Game/MCPNoSuchMetaSound");
    expect(missing.error).toContain("Asset Registry");
  }, 300_000);

  // -------------------------------------------------------------------------
  // The seven reads, against a MetaSound this file authors.
  //
  // Authored once and shared, because the point is the round trip: every id
  // asserted below is an id an authoring call handed out.
  // -------------------------------------------------------------------------
  interface AuthorResult {
    success?: boolean;
    error?: string;
    path?: string;
    nodes?: number;
    errors?: number;
    elements?: Array<{ kind?: string; ref?: string; ok?: boolean; error?: string }>;
  }

  interface ReadDocument {
    success?: boolean;
    error?: string;
    path?: string;
    source?: string;
    pageId?: string;
    readDefaultPage?: boolean;
    hasActiveBuilder?: boolean;
    documentVersion?: string;
    interfaces?: string[];
    pages?: string[];
    nodeCount?: number;
    connectionCount?: number;
    graphInputs?: Array<{ nodeId?: string; name?: string; dataType?: string; default?: unknown }>;
    graphOutputs?: Array<{ nodeId?: string; name?: string; dataType?: string; incomingConnections?: number }>;
    nodes?: Array<{
      nodeId?: string;
      name?: string;
      class?: { nodeClassName?: string; nodeNamespace?: string; nodeVariant?: string };
      incomingConnections?: number;
      outgoingConnections?: number;
    }>;
    connections?: Array<{
      fromNodeId?: string;
      fromOutput?: string;
      toNodeId?: string;
      toInput?: string;
      fromDataType?: string;
      toDataType?: string;
      graphInput?: string;
      graphOutput?: string;
      typeMismatch?: boolean;
      dangling?: boolean;
    }>;
    variables?: unknown[];
  }

  /** The graph input the fixture exposes, and the oscillator it drives. */
  const GRAPH_INPUT = "Pitch";
  /** One authoring run backs all seven read cases. */
  let authored: AuthorResult;
  /** The node id metasound_add_node returned, which every id assertion echoes. */
  let addedNodeId = "";

  const readDoc = async (args: Record<string, unknown> = {}): Promise<ReadDocument> =>
    resultJson<ReadDocument>(
      await call("audio", { action: "metasound_read_document", assetPath: METASOUND_OBJECT, ...args }),
    );

  beforeAll(async () => {
    // Stamp the whole graph in one call: a Float graph input driving a Sine
    // oscillator whose Audio output drives the mono graph output. Variant
    // matters - "UE.Sine" with no variant does not resolve, "UE.Sine.Audio"
    // does, and metasound_list_node_classes is where that comes from.
    authored = resultJson<AuthorResult>(
      await call("audio", {
        action: "metasound_author",
        name: "MS_MCPProbe",
        packagePath: ROOT,
        format: "mono",
        oneShot: true,
        inputs: [{ name: GRAPH_INPUT, dataType: "Float", default: 440 }],
        nodes: [{ id: "osc", class: "Sine", namespace: "UE", variant: "Audio" }],
        connections: [
          { from: `input:${GRAPH_INPUT}`, to: "osc:Frequency" },
          { from: "osc:Audio", to: "audioOut:0" },
        ],
      }),
    );

    // A second node added by hand, so the identity contract is asserted against
    // an id a caller was actually handed rather than one only the read invented.
    const added = resultJson<{ success?: boolean; nodeId?: string; error?: string }>(
      await call("audio", {
        action: "metasound_add_node",
        assetPath: METASOUND_OBJECT,
        nodeClassName: "Saw",
        nodeNamespace: "UE",
        nodeVariant: "Audio",
      }),
    );
    addedNodeId = added.nodeId ?? "";

    await call("audio", { action: "metasound_build", assetPath: METASOUND_OBJECT });
  }, 600_000);

  it("authors a graph into an asset whose document is actually initialized", async () => {
    expect(authored.success, authored.error).not.toBe(false);
    expect(authored.errors ?? 0, JSON.stringify(authored.elements)).toBe(0);
    expect(authored.nodes ?? 0).toBeGreaterThan(0);

    const body = await readDoc();
    expect(body.success, body.error).not.toBe(false);
    // The regression this pins: an asset created without UMetaSoundSourceFactory
    // holds no interfaces, no pages and no nodes, and every read of it correctly
    // reports there is nothing to read. A real source declares UE.Source plus an
    // output format, and has exactly one default page.
    expect((body.interfaces ?? []).length, "the document declares no interfaces at all").toBeGreaterThan(0);
    expect((body.pages ?? []).length).toBeGreaterThan(0);
    expect(body.readDefaultPage).toBe(true);
    expect(body.pageId).toBeTruthy();
  }, 300_000);

  it("reads back the nodes and connections that were authored, by id", async () => {
    const body = await readDoc();
    expect(body.success, body.error).not.toBe(false);

    const sine = (body.nodes ?? []).find((n) => n.class?.nodeClassName === "Sine");
    expect(sine, `no Sine node in ${JSON.stringify(body.nodes)}`).toBeTruthy();
    expect(sine?.nodeId).toBeTruthy();
    expect(sine?.class?.nodeNamespace).toBe("UE");
    expect(sine?.class?.nodeVariant).toBe("Audio");

    // The graph input keeps the name and type it was authored with, and its
    // default survives the round trip.
    const pitch = (body.graphInputs ?? []).find((i) => i.name === GRAPH_INPUT);
    expect(pitch, `no ${GRAPH_INPUT} graph input`).toBeTruthy();
    expect(pitch?.dataType).toBe("Float");
    expect(Number(pitch?.default)).toBeCloseTo(440, 3);

    // Both authored edges are present, and each one is shaped as the argument
    // list of the write action that would recreate it.
    const edges = body.connections ?? [];
    expect(body.connectionCount ?? 0).toBeGreaterThanOrEqual(2);

    const intoSine = edges.find((e) => e.toNodeId === sine?.nodeId && e.toInput === "Frequency");
    expect(intoSine, `nothing drives Frequency: ${JSON.stringify(edges)}`).toBeTruthy();
    expect(intoSine?.graphInput).toBe(GRAPH_INPUT);
    expect(intoSine?.fromDataType).toBe("Float");
    expect(intoSine?.toDataType).toBe("Float");

    const outOfSine = edges.find((e) => e.fromNodeId === sine?.nodeId && e.fromOutput === "Audio");
    expect(outOfSine, `Sine drives nothing: ${JSON.stringify(edges)}`).toBeTruthy();
    expect(outOfSine?.graphOutput, "the Sine output does not reach a graph output").toBeTruthy();
    expect(outOfSine?.fromDataType).toBe("Audio");
    expect(outOfSine?.toDataType).toBe("Audio");

    // No edge crosses data types and none dangles.
    for (const edge of edges) {
      expect(edge.typeMismatch, JSON.stringify(edge)).not.toBe(true);
      expect(edge.dangling, JSON.stringify(edge)).not.toBe(true);
    }
  }, 300_000);

  it("hands back the same node id metasound_add_node returned", async () => {
    // The identity contract, asserted at both ends: the id a write handed out
    // is an id a read reports, and the same id can be handed straight back to
    // an action that takes a nodeId.
    expect(addedNodeId, "metasound_add_node returned no nodeId").toBeTruthy();

    const body = await readDoc();
    const byId = (body.nodes ?? []).find((n) => n.nodeId === addedNodeId);
    expect(byId, `${addedNodeId} is not in ${JSON.stringify((body.nodes ?? []).map((n) => n.nodeId))}`).toBeTruthy();
    expect(byId?.class?.nodeClassName).toBe("Saw");

    const inspected = resultJson<{ success?: boolean; nodeId?: string; error?: string }>(
      await call("audio", {
        action: "metasound_inspect_node",
        assetPath: METASOUND_OBJECT,
        nodeId: addedNodeId,
      }),
    );
    expect(inspected.success, inspected.error).not.toBe(false);
    expect(inspected.nodeId).toBe(addedNodeId);
  }, 300_000);

  it("lists connections in metasound_connect's own parameter names", async () => {
    const body = resultJson<{
      success?: boolean;
      error?: string;
      connections?: ReadDocument["connections"];
      count?: number;
      totalInGraph?: number;
      malformed?: number;
    }>(await call("audio", { action: "metasound_list_connections", assetPath: METASOUND_OBJECT }));

    expect(body.success, body.error).not.toBe(false);
    expect(body.count ?? 0).toBeGreaterThanOrEqual(2);
    expect(body.totalInGraph).toBe(body.count);
    expect(body.malformed).toBe(0);
    for (const edge of body.connections ?? []) {
      expect(edge.fromNodeId).toBeTruthy();
      expect(edge.fromOutput).toBeTruthy();
      expect(edge.toNodeId).toBeTruthy();
      expect(edge.toInput).toBeTruthy();
    }

    // Narrowing to one node returns a subset, not everything.
    const doc = await readDoc();
    const sineId = (doc.nodes ?? []).find((n) => n.class?.nodeClassName === "Sine")?.nodeId;
    const outbound = resultJson<{ success?: boolean; count?: number; connections?: ReadDocument["connections"] }>(
      await call("audio", {
        action: "metasound_list_connections",
        assetPath: METASOUND_OBJECT,
        nodeId: sineId,
        direction: "out",
      }),
    );
    expect(outbound.success).not.toBe(false);
    expect(outbound.count ?? 0).toBeGreaterThan(0);
    for (const edge of outbound.connections ?? []) {
      expect(edge.fromNodeId).toBe(sineId);
    }
  }, 300_000);

  it("reports a graph with no variables as normal rather than as an error", async () => {
    const body = resultJson<{ success?: boolean; error?: string; variables?: unknown[]; count?: number; note?: string }>(
      await call("audio", { action: "metasound_list_variables", assetPath: METASOUND_OBJECT }),
    );
    expect(body.success, body.error).not.toBe(false);
    expect(Array.isArray(body.variables)).toBe(true);
    expect(body.count).toBe((body.variables ?? []).length);
    // The fixture declares none, and the note has to say that is fine rather
    // than leaving an empty array to read as a failure.
    expect(body.count).toBe(0);
    expect(body.note ?? "").toContain("no variables");
  }, 300_000);

  it("finds a node by class name and reports what did not match", async () => {
    const hit = resultJson<{
      success?: boolean;
      error?: string;
      nodes?: ReadDocument["nodes"];
      matched?: number;
      searched?: number;
    }>(await call("audio", { action: "metasound_search_nodes", assetPath: METASOUND_OBJECT, query: "sine" }));

    expect(hit.success, hit.error).not.toBe(false);
    expect(hit.matched ?? 0).toBe(1);
    expect(hit.searched ?? 0).toBeGreaterThan(1);
    expect(hit.nodes?.[0]?.class?.nodeClassName).toBe("Sine");

    const miss = resultJson<{ success?: boolean; matched?: number; note?: string }>(
      await call("audio", {
        action: "metasound_search_nodes",
        assetPath: METASOUND_OBJECT,
        query: "__no_such_node__",
      }),
    );
    expect(miss.success).not.toBe(false);
    expect(miss.matched).toBe(0);
    // An empty result names the call that would widen it rather than stopping.
    expect(miss.note ?? "").toContain("metasound_read_document");
  }, 300_000);

  it("inspects one node down to vertex types, defaults and connection state", async () => {
    const doc = await readDoc();
    const sineId = (doc.nodes ?? []).find((n) => n.class?.nodeClassName === "Sine")?.nodeId;
    expect(sineId).toBeTruthy();

    const body = resultJson<{
      success?: boolean;
      error?: string;
      nodeId?: string;
      name?: string;
      inputs?: Array<{
        name?: string;
        dataType?: string;
        connected?: boolean;
        defaultIsSet?: boolean;
        default?: unknown;
        defaultSource?: string;
      }>;
      outputs?: Array<{ name?: string; dataType?: string; connected?: boolean }>;
      incoming?: unknown[];
      outgoing?: unknown[];
    }>(await call("audio", { action: "metasound_inspect_node", assetPath: METASOUND_OBJECT, nodeId: sineId }));

    expect(body.success, body.error).not.toBe(false);
    expect(body.nodeId).toBe(sineId);

    const frequency = (body.inputs ?? []).find((i) => i.name === "Frequency");
    expect(frequency, `Sine has no Frequency input: ${JSON.stringify(body.inputs)}`).toBeTruthy();
    expect(frequency?.dataType).toBe("Float");
    // Connection state is measured, not assumed: the fixture wired this one.
    expect(frequency?.connected).toBe(true);

    const audioOut = (body.outputs ?? []).find((o) => o.name === "Audio");
    expect(audioOut?.dataType).toBe("Audio");
    expect(audioOut?.connected).toBe(true);

    expect((body.incoming ?? []).length).toBeGreaterThan(0);
    expect((body.outgoing ?? []).length).toBeGreaterThan(0);
  }, 300_000);

  it("reports a graph input's authored default when the input node is inspected", async () => {
    // Same round-trip contract as metasound_read_document's graphInputs, one
    // level down. A graph input node carries no per-node input literal, so the
    // value has to be read off the root graph class interface; reading only the
    // node reported no default for a value the write had stored.
    const doc = await readDoc();
    const pitchId = (doc.graphInputs ?? []).find((i) => i.name === GRAPH_INPUT)?.nodeId;
    expect(pitchId, `no ${GRAPH_INPUT} graph input node id`).toBeTruthy();

    const body = resultJson<{
      success?: boolean;
      error?: string;
      inputs?: Array<{ name?: string; default?: unknown; defaultIsSet?: boolean; defaultSource?: string }>;
    }>(await call("audio", { action: "metasound_inspect_node", assetPath: METASOUND_OBJECT, nodeId: pitchId }));

    expect(body.success, body.error).not.toBe(false);
    const vertex = (body.inputs ?? []).find((i) => i.defaultIsSet === true);
    expect(vertex, `no input vertex carries a default: ${JSON.stringify(body.inputs)}`).toBeTruthy();
    expect(vertex?.defaultSource).toBe("graphInput");
    expect(Number(vertex?.default)).toBeCloseTo(440, 3);
  }, 300_000);

  it("lists a node's pins and counts the ones nothing drives", async () => {
    const doc = await readDoc();
    const sineId = (doc.nodes ?? []).find((n) => n.class?.nodeClassName === "Sine")?.nodeId;

    const body = resultJson<{
      success?: boolean;
      error?: string;
      inputs?: Array<{ name?: string; dataType?: string }>;
      outputs?: Array<{ name?: string }>;
      inputCount?: number;
      outputCount?: number;
      unconnectedInputs?: number;
    }>(await call("audio", { action: "metasound_list_node_pins", assetPath: METASOUND_OBJECT, nodeId: sineId }));

    expect(body.success, body.error).not.toBe(false);
    expect(body.inputCount).toBe((body.inputs ?? []).length);
    expect(body.outputCount).toBe((body.outputs ?? []).length);
    expect(body.inputCount ?? 0).toBeGreaterThan(1);
    // Frequency is wired, so the unconnected count is short of the total by at
    // least one. A count equal to the total would mean the edge never landed.
    expect(body.unconnectedInputs ?? 0).toBeLessThan(body.inputCount ?? 0);

    // Filtering by data type returns only that type, on both sides.
    const floats = resultJson<{ success?: boolean; inputs?: Array<{ dataType?: string }> }>(
      await call("audio", {
        action: "metasound_list_node_pins",
        assetPath: METASOUND_OBJECT,
        nodeId: sineId,
        dataType: "Float",
        direction: "inputs",
      }),
    );
    expect(floats.success).not.toBe(false);
    expect((floats.inputs ?? []).length).toBeGreaterThan(0);
    for (const pin of floats.inputs ?? []) {
      expect(pin.dataType).toBe("Float");
    }
  }, 300_000);

  it("diagnoses the graph, naming the node in each problem", async () => {
    const body = resultJson<{
      success?: boolean;
      error?: string;
      problems?: string[];
      runnable?: boolean;
      nodeCount?: number;
      connectionCount?: number;
      unconnectedGraphOutputs?: number;
    }>(await call("audio", { action: "metasound_validate", assetPath: METASOUND_OBJECT }));

    expect(body.success, body.error).not.toBe(false);
    expect(Array.isArray(body.problems)).toBe(true);
    expect(body.runnable).toBe((body.problems ?? []).length === 0);
    expect(body.nodeCount ?? 0).toBeGreaterThan(0);

    const problems = body.problems ?? [];
    const doc = await readDoc();
    const sineId = (doc.nodes ?? []).find((n) => n.class?.nodeClassName === "Sine")?.nodeId ?? "";

    // The wired node must NOT be reported as orphaned: that is the assertion
    // separating a real diagnosis from one that flags everything.
    expect(problems.filter((p) => p.includes(sineId) && p.includes("orphaned"))).toEqual([]);

    // The unwired one must be, and by id, so a caller can act on it.
    expect(addedNodeId).toBeTruthy();
    expect(
      problems.some((p) => p.includes(addedNodeId)),
      `no problem names the unwired node ${addedNodeId}: ${JSON.stringify(problems)}`,
    ).toBe(true);
  }, 300_000);

  it("refuses a node id that is not in the graph, listing the ones that are", async () => {
    const body = resultJson<{ success?: boolean; error?: string }>(
      await call("audio", {
        action: "metasound_inspect_node",
        assetPath: METASOUND_OBJECT,
        nodeId: "00000000000000000000000000000000",
      }),
    );
    expect(body.success).toBe(false);
    // A bad id is only actionable if the refusal hands back an id that works.
    expect(body.error).toContain("00000000000000000000000000000000");
    expect(body.error).toContain(addedNodeId);
  }, 300_000);

  it("refuses a page the document does not hold, listing the pages it does", async () => {
    const body = resultJson<{ success?: boolean; error?: string }>(
      await call("audio", {
        action: "metasound_read_document",
        assetPath: METASOUND_OBJECT,
        pageId: "11111111111111111111111111111111",
      }),
    );
    expect(body.success).toBe(false);
    expect(body.error).toContain("11111111111111111111111111111111");
    expect(body.error).toContain("Pages present");
  }, 300_000);

  // ---------------------------------------------------------------------------
  // The edit half, on the same fixture, LAST because it mutates it.
  //
  // These two exist for one reason: the edit actions walked the document with
  // FMetasoundFrontendGraphClass::GetConstDefaultGraph(), which check()s rather
  // than returning null on a document with no default page, and a check is a
  // fatal assert that takes the editor down. The read path hit exactly that,
  // twice. Both cases below reach a page walk, so a return of any kind is the
  // assertion that matters and the field contents are the second half.
  // ---------------------------------------------------------------------------
  it("removes a node, capturing the class it removed off the page it read", async () => {
    expect(addedNodeId, "metasound_add_node returned no nodeId").toBeTruthy();

    const removed = resultJson<{
      success?: boolean;
      error?: string;
      alreadyDeleted?: boolean;
      nodeClassName?: string;
      source?: string;
      pendingBuild?: boolean;
      pageId?: string;
    }>(
      await call("audio", {
        action: "metasound_remove_node",
        assetPath: METASOUND_OBJECT,
        nodeId: addedNodeId,
      }),
    );

    expect(removed.success, removed.error).not.toBe(false);
    expect(removed.alreadyDeleted).toBe(false);
    // The rollback payload comes from walking the page for the node's class, so
    // this being right is what says the walk ran rather than being skipped.
    expect(removed.nodeClassName).toBe("Saw");
    expect(removed.pageId).toBeTruthy();
    // One document, edited and saved in place: nothing is left pending a build.
    expect(removed.pendingBuild).toBe(false);
    expect(removed.source).toBeTruthy();

    const after = await readDoc();
    expect((after.nodes ?? []).some((n) => n.nodeId === addedNodeId)).toBe(false);

    // A replayed step must not fail on its own prior success, and the replay is
    // also the path that builds the "valid ids are" listing off the page.
    const replay = resultJson<{ success?: boolean; alreadyDeleted?: boolean; error?: string }>(
      await call("audio", {
        action: "metasound_remove_node",
        assetPath: METASOUND_OBJECT,
        nodeId: addedNodeId,
      }),
    );
    expect(replay.success, replay.error).not.toBe(false);
    expect(replay.alreadyDeleted).toBe(true);
  }, 300_000);

  it("answers for a variable this graph never declared instead of asserting", async () => {
    // The variable branch of metasound_remove_member is the third page walk, and
    // a graph with no variables is the case that exercises it end to end.
    const body = resultJson<{
      success?: boolean;
      error?: string;
      alreadyDeleted?: boolean;
      note?: string;
    }>(
      await call("audio", {
        action: "metasound_remove_member",
        assetPath: METASOUND_OBJECT,
        memberKind: "variable",
        name: "MCPNoSuchVariable",
      }),
    );

    expect(body.success, body.error).not.toBe(false);
    expect(body.alreadyDeleted).toBe(true);
    expect(body.note ?? "").toContain("MCPNoSuchVariable");
  }, 300_000);
});
