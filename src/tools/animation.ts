import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";
import { Vec3, Quat } from "../schemas.js";
import { PAGINATION_SCHEMA, paged } from "../pagination.js";

// Keep the Control Rig operation schema self-contained so adding it does not
// redirect JSON-schema references used by older animation actions.
const ControlRigVec3 = z.object({
  x: z.number().finite(),
  y: z.number().finite(),
  z: z.number().finite(),
}).strict();
const ControlRigRotator = z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() }).strict();
const ControlRigQuaternion = z.object({
  x: z.number().finite(),
  y: z.number().finite(),
  z: z.number().finite(),
  w: z.number().finite(),
}).strict().refine(
  (rotation) => Math.abs(Math.hypot(rotation.x, rotation.y, rotation.z, rotation.w) - 1) <= 1e-3,
  { message: "rotationQuaternion must be normalized" },
);

const ControlRigKeyTransform = z.object({
  translation: ControlRigVec3,
  rotationDegrees: ControlRigRotator,
  scale: ControlRigVec3,
}).strict();

const ControlRigSetEdit = z.object({
  op: z.literal("set"),
  control: z.string().min(1),
  frame: z.number().int().optional(),
  frames: z.array(z.number().int()).min(1).optional(),
  transform: ControlRigKeyTransform,
  // Deliberately a string, not z.enum. The MCP SDK validates arguments BEFORE
  // the tool callback runs, so a strict enum makes a typo fail at the transport
  // with a schema error, and the handler's own message, which names every valid
  // value, never reaches the caller. The handler validates it and says what is
  // valid, which is the answer a caller can act on.
  space: z.string().optional().describe("Coordinate space for the written transform: local | component | global. global is an alias for component. Default local"),
}).strict().refine(
  (edit) => (edit.frame === undefined) !== (edit.frames === undefined),
  { message: "set operations require exactly one of frame or frames" },
);

const ControlRigTransformKey = z.object({
  frame: z.number().int(),
  transform: z.object({
    translation: ControlRigVec3,
    rotationQuaternion: ControlRigQuaternion,
    scale: ControlRigVec3,
  }).strict(),
}).strict();

const ControlRigSetKeysEdit = z.object({
  op: z.literal("set_keys"),
  control: z.string().min(1),
  keys: z.array(ControlRigTransformKey).min(1),
  // String rather than z.enum for the reason recorded on ControlRigSetEdit.space.
  space: z.string().optional().describe("Coordinate space for the written transforms: local | component | global. global is an alias for component. Default local"),
}).strict().refine(
  (edit) => edit.keys.every((key, index) => index === 0 || key.frame > edit.keys[index - 1].frame),
  { message: "set_keys frames must be strictly increasing" },
);

const ControlRigOffsetEdit = z.object({
  op: z.literal("offset"),
  control: z.string().min(1),
  startFrame: z.number().int(),
  endFrame: z.number().int(),
  translationCm: ControlRigVec3.optional(),
  rotationDegrees: ControlRigRotator.optional(),
  scaleMultiplier: ControlRigVec3.optional(),
  // String rather than z.enum for the reason recorded on ControlRigSetEdit.space.
  space: z.string().optional().describe("Coordinate space for the offset: local | component | global. global is an alias for component. Default local"),
  blendInFrames: z.number().int().nonnegative().optional(),
  blendOutFrames: z.number().int().nonnegative().optional(),
}).strict()
  .refine((edit) => edit.endFrame >= edit.startFrame, {
    message: "offset endFrame must be greater than or equal to startFrame",
  })
  .refine(
    (edit) => edit.translationCm !== undefined || edit.rotationDegrees !== undefined || edit.scaleMultiplier !== undefined,
    { message: "offset operations require translationCm, rotationDegrees, or scaleMultiplier" },
  );

const ControlRigContactTarget = z.object({
  translation: ControlRigVec3,
  rotationQuaternion: ControlRigQuaternion.optional(),
}).strict();

const ControlRigContactLockEdit = z.object({
  op: z.literal("contact_lock"),
  control: z.string().min(1),
  drivenReference: z.string().min(1).optional(),
  startFrame: z.number().int(),
  endFrame: z.number().int(),
  target: ControlRigContactTarget,
  blendInFrames: z.number().int().nonnegative().optional(),
  blendOutFrames: z.number().int().nonnegative().optional(),
  stabilizeControls: z.array(z.string().min(1)).max(8).optional(),
  positionToleranceCm: z.number().finite().positive().max(100).optional(),
  rotationToleranceDegrees: z.number().finite().positive().max(180).optional(),
}).strict().superRefine((edit, context) => {
  if (edit.endFrame < edit.startFrame) {
    context.addIssue({ code: z.ZodIssueCode.custom, path: ["endFrame"], message: "contact_lock endFrame must be at least startFrame" });
    return;
  }

  const intervalCount = edit.endFrame - edit.startFrame;
  const blendIn = edit.blendInFrames ?? 0;
  const blendOut = edit.blendOutFrames ?? 0;
  if (blendIn + blendOut > intervalCount) {
    context.addIssue({
      code: z.ZodIssueCode.custom,
      path: ["blendOutFrames"],
      message: "contact_lock blends must leave at least one fully constrained frame",
    });
  }

  const controlKey = edit.control.toLowerCase();
  const stabilizerKeys = (edit.stabilizeControls ?? []).map((control) => control.toLowerCase());
  if (stabilizerKeys.includes(controlKey)) {
    context.addIssue({
      code: z.ZodIssueCode.custom,
      path: ["stabilizeControls"],
      message: "contact_lock control cannot also be a stabilizer",
    });
  }
  if (new Set(stabilizerKeys).size !== stabilizerKeys.length) {
    context.addIssue({
      code: z.ZodIssueCode.custom,
      path: ["stabilizeControls"],
      message: "contact_lock stabilizers must be unique",
    });
  }

  const frameCount = intervalCount + 1;
  if (frameCount * (stabilizerKeys.length + 1) > 100_000) {
    context.addIssue({
      code: z.ZodIssueCode.custom,
      path: ["endFrame"],
      message: "contact_lock is limited to 100000 control-frame cells",
    });
  }
});

const ControlRigSetBoolEdit = z.object({
  op: z.literal("set_bool"),
  control: z.string().min(1),
  frame: z.number().int().optional(),
  frames: z.array(z.number().int()).min(1).optional(),
  value: z.boolean(),
}).strict().refine(
  (edit) => (edit.frame === undefined) !== (edit.frames === undefined),
  { message: "set_bool operations require exactly one of frame or frames" },
);

const ControlRigSetFloatEdit = z.object({
  op: z.literal("set_float"),
  control: z.string().min(1),
  frame: z.number().int().optional(),
  frames: z.array(z.number().int()).min(1).optional(),
  value: z.number().finite(),
}).strict().refine(
  (edit) => (edit.frame === undefined) !== (edit.frames === undefined),
  { message: "set_float operations require exactly one of frame or frames" },
);

const ControlRigSetIntEdit = z.object({
  op: z.literal("set_int"),
  control: z.string().min(1),
  frame: z.number().int().optional(),
  frames: z.array(z.number().int()).min(1).optional(),
  value: z.number().int(),
}).strict().refine(
  (edit) => (edit.frame === undefined) !== (edit.frames === undefined),
  { message: "set_int operations require exactly one of frame or frames" },
);

const ControlRigEditOperation = z.union([
  ControlRigSetEdit,
  ControlRigSetKeysEdit,
  ControlRigOffsetEdit,
  ControlRigContactLockEdit,
  ControlRigSetBoolEdit,
  ControlRigSetFloatEdit,
  ControlRigSetIntEdit,
]);

const IKRigAuthoringChain = z.object({
  name: z.string().min(1),
  startBone: z.string().min(1),
  endBone: z.string().min(1),
  goal: z.string().min(1).optional(),
}).strict();

const IKRigFullBodyGoal = z.object({
  name: z.string().min(1),
  bone: z.string().min(1),
  positionAlpha: z.number().finite().min(0).max(1).optional(),
  rotationAlpha: z.number().finite().min(0).max(1).optional(),
  chainDepth: z.number().int().nonnegative().optional(),
  strengthAlpha: z.number().finite().min(0).max(1).optional(),
  pullChainAlpha: z.number().finite().min(0).max(1).optional(),
  pinRotation: z.number().finite().min(0).max(1).optional(),
}).strict();

const IKRigFullBodySettings = z.object({
  solverIndex: z.number().int().nonnegative().optional(),
  rootBone: z.string().min(1),
  enabled: z.boolean().optional(),
  goals: z.array(IKRigFullBodyGoal).min(1).max(256),
}).strict();

const IKRigExclusion = z.object({
  bone: z.string().min(1),
  excluded: z.boolean(),
}).strict();

const IKRetargetChainMapping = z.object({
  targetChain: z.string().min(1),
  sourceChain: z.string().min(1).nullable().optional(),
}).strict();

const IKRetargetPose = z.object({
  // Strings rather than z.enum for the reason recorded on
  // ControlRigSetEdit.space: the handler rejects an unknown value by name and
  // a strict enum would replace that message with a transport schema error.
  side: z.string().min(1).describe("Which retarget skeleton the pose belongs to: source | target"),
  name: z.string().min(1),
  create: z.boolean().optional(),
  reset: z.boolean().optional(),
  autoAlign: z.string().optional().describe("Native auto-align method: chain_to_chain | mesh_to_mesh | local_axes | global_axes. Requires both preview meshes"),
  bones: z.array(z.string().min(1)).max(10_000).optional(),
  rotationOffsets: z.array(z.object({
    bone: z.string().min(1),
    rotationQuaternion: ControlRigQuaternion,
  }).strict()).max(10_000).optional(),
  rootOffsetZ: z.number().finite().optional(),
  snapBoneToGround: z.string().min(1).optional(),
}).strict().refine(
  (pose) => pose.rootOffsetZ === undefined || pose.snapBoneToGround === undefined,
  { message: "retarget pose rootOffsetZ and snapBoneToGround are mutually exclusive" },
);
/**
 * Element schemas for the Motion Matching authoring arrays (#936).
 *
 * These were `z.array(z.any())`, which serialises to an array schema with no
 * `items`; VS Code refuses to load a tool that carries one. Each object stays
 * open (`.passthrough()`) so nothing a caller sends today is dropped, and the
 * entries that the handler also accepts as a bare string keep that spelling
 * through a union rather than an untyped element.
 */

/** set_pose_search_clips: one clip entry. The handler accepts any of the four path spellings. */
const PoseSearchClipEntry = z.object({
  sequencePath: z.string().optional().describe("Animation asset path (aliases: asset, assetPath, animationPath)"),
  asset: z.string().optional().describe("Alias for sequencePath"),
  assetPath: z.string().optional().describe("Alias for sequencePath"),
  animationPath: z.string().optional().describe("Alias for sequencePath"),
  mirror: z.string().optional().describe("'original' | 'mirrored' | 'both'"),
  disableReselection: z.boolean().optional().describe("Disallow reselecting poses from the same asset"),
  sampleStart: z.number().optional().describe("Sampling range start in seconds"),
  sampleEnd: z.number().optional().describe("Sampling range end in seconds"),
  enabled: z.boolean().optional().describe("Include the clip in the database"),
}).passthrough();

/** add_pose_search_schema_pose_channel: one sampled bone. */
const PoseSearchBoneEntry = z.object({
  bone: z.string().describe("Bone name"),
  flags: z.array(z.string()).optional().describe("Any of: velocity, position, rotation, phase"),
  weight: z.number().optional().describe("Per-bone weight (default 1)"),
}).passthrough();

/** create_mirror_data_table: one find/replace bone-name rule. */
const MirrorFindReplaceExpression = z.object({
  find: z.string().describe("Bone-name fragment to match"),
  replace: z.string().describe("Replacement fragment"),
  method: z.string().optional().describe("suffix (default) | prefix | regex"),
}).passthrough();

export const animationTool: ToolDef = categoryTool(
  "animation",
  "Animation assets, skeletons, montages, blendspaces, anim blueprints, physics assets.",
  {
    read_anim_blueprint:  bp("Read AnimBP structure. Params: assetPath", "read_anim_blueprint"),
    read_montage:         bp("Read montage. Params: assetPath", "read_anim_montage", (p) => ({ assetPath: p.assetPath })),
    read_sequence:        bp("Read anim sequence. Params: assetPath", "read_anim_sequence", (p) => ({ assetPath: p.assetPath })),
    scan_animation_tracks: bp("Scan AnimSequence bone-track counts. Params: directory?, recursive?, assetPaths?, skeletonPath?, targetTrackCount?, includeTrackNames?", "scan_animation_tracks"),
    read_blendspace:      bp("Read blendspace. Params: assetPath", "read_blendspace", (p) => ({ assetPath: p.assetPath })),
    add_blend_sample:     bp("Append a sample to a BlendSpace. Params: assetPath, animation (AnimSequence path), position {x,y} (or flat x,y) (#248)", "add_blend_sample", (p) => ({ assetPath: p.assetPath, animation: p.animation, position: p.position, x: p.x, y: p.y })),
    set_blend_sample:     bp("Move an existing BlendSpace sample or swap its animation. Params: assetPath, sampleIndex, position? {x,y} (or flat x,y), animation? (#272)", "set_blend_sample", (p) => ({ assetPath: p.assetPath, sampleIndex: p.sampleIndex, position: p.position, x: p.x, y: p.y, animation: p.animation })),
    list:                 bp(paged("List anim assets (AnimSequence, AnimMontage, AnimBlueprint, BlendSpace). directory scopes the read to one folder and is now honoured; it was advertised and ignored, so a scoped call used to get the whole project back. Params: directory?, recursive?"), "list_anim_assets"),
    create_montage:       bp("Create montage. Params: animSequencePath, name?, packagePath?", "create_anim_montage"),
    author_montages_batch: bp("Batch-author montages in one call: idempotent create, slot name, blend/rate/length properties, sections and notifies, then save. Every item reports success plus the failing stage (validate|create|slot|properties|sections|notifies|save) and error, so one bad item does not hide the rest. Newly created montages come back as a delete_asset_batch rollback. Each montage still holds the single segment create_montage builds. Params: items[] (each: name, animSequencePath, packagePath?, onConflict?, slotName?, trackIndex?, rateScale?, blendIn?, blendOut?, sequenceLength?, sections? [{sectionName, startTime?, linkedSection?}], notifies? [{notifyName, triggerTime, notifyClass?, properties?}])", "author_montages_batch", (p) => ({ items: p.items })),
    create_anim_blueprint: bp("Create AnimBP. Params: skeletonPath, name?, packagePath?, parentClass?", "create_anim_blueprint"),
    create_blendspace:    bp("Create blendspace (2D). Params: skeletonPath, name?, packagePath?, axisHorizontal?, axisVertical?", "create_blendspace"),
    create_blendspace_1d: bp("Create BlendSpace1D. Params: skeletonPath, name?, packagePath?, axisName? (default Speed), axisMin?, axisMax?, gridNum? (#459)", "create_blendspace_1d", (p) => ({ name: p.name, skeletonPath: p.skeletonPath, packagePath: p.packagePath, axisName: p.axisName, axisMin: p.axisMin, axisMax: p.axisMax, gridNum: p.gridNum, onConflict: p.onConflict })),
    populate_blendspace:  bp("One-call axis params + samples authoring for BlendSpace 1D/2D. Params: assetPath, axis? ({name?, min?, max?, gridNum?}) for axis 0, blendspaceAxes? (per-axis array), axisHorizontal?/axisVertical? + horizontalMin/horizontalMax/verticalMin/verticalMax/gridNumHorizontal/gridNumVertical (back-compat), samples ([{animationPath, x, y?}]), clearExisting? (default true) (#459)", "populate_blendspace", (p) => ({ assetPath: p.assetPath, axis: p.axis, axes: p.blendspaceAxes, axisIndex: p.axisIndex, axisHorizontal: p.axisHorizontal, axisVertical: p.axisVertical, horizontalMin: p.horizontalMin, horizontalMax: p.horizontalMax, verticalMin: p.verticalMin, verticalMax: p.verticalMax, gridNumHorizontal: p.gridNumHorizontal, gridNumVertical: p.gridNumVertical, samples: p.samples, clearExisting: p.clearExisting })),
    add_notify:           bp("Add notify. For PlayMontageNotify the notifyName is also written onto the spawned notify object so OnPlayMontageNotifyBegin broadcasts it (not 'None'), and montage branching-point markers refresh (#528/#880). On a montage the PlayMontageNotify classes are added as BRANCHING POINT notifies, which is the only tick type UAnimNotify_PlayMontageNotify::BranchingPointNotify runs at, so OnPlayMontageNotifyBegin broadcasts without a montage reload; pass branchingPoint to force it either way, and read branchingPointMarkerCount in the response to see what the montage cached. notifyProperties writes EditAnywhere fields onto the spawned notify object and therefore requires a notifyClass that resolves. Params: assetPath, notifyName, triggerTime, notifyClass?, notifyProperties?", "add_anim_notify"),
    remove_notify:        bp("Remove notify(s) by name and/or class. Pass at least one of notifyName/notifyClass; both filters AND. Idempotent: alreadyDeleted=true if no match. Params: assetPath, notifyName?, notifyClass? (#471)", "remove_anim_notify", (p) => ({ assetPath: p.assetPath, notifyName: p.notifyName, notifyClass: p.notifyClass })),
    get_skeleton_info:    bp("Read skeleton. Params: assetPath", "get_skeleton_info"),
    create_skeleton:      bp("Create a real USkeleton from a SkeletalMesh through the Unreal skeleton factory. The factory assigns the new skeleton to that mesh, then both packages are saved. An existing destination is an onConflict=skip idempotency hit only when the mesh already points at it and the reference skeleton has exactly the same bone count, names in index order, and parent indexes; reference-pose transforms are not compared. Otherwise the action refuses to repurpose it. If either save fails, it restores and saves the mesh's previous skeleton reference, then deletes the new skeleton when safe; an incomplete cleanup returns a machine-readable recovery descriptor. Returns the previous skeleton and a lossy rollback that restores the mesh association; delete the created skeleton separately only after confirming nothing references it. Params: name, skeletalMeshPath, packagePath? (default /Game), onConflict? (skip|error)", "create_skeleton", (p) => ({ name: p.name, skeletalMeshPath: p.skeletalMeshPath, packagePath: p.packagePath, onConflict: p.onConflict })),
    list_sockets:         bp("List sockets. Params: assetPath", "list_animation_sockets"),
    list_skeletal_meshes: bp(paged("List skeletal meshes. directory scopes the read to one folder and is now honoured; it was advertised and ignored, so a scoped call used to get the whole project back. Params: directory?, recursive?"), "list_skeletal_meshes"),
    get_physics_asset:    bp("Read physics asset. Params: assetPath", "get_physics_asset_info"),
    create_sequence:      bp("Create blank AnimSequence. Params: name, skeletonPath, packagePath?, numFrames?, frameRate?", "create_sequence"),
    set_bone_keyframes:   bp("Set bone transform keyframes. Params: assetPath, boneName, keyframes", "set_bone_keyframes"),
    bake_keyframes_batch: bp("Bake per-bone keyframe arrays for many bones into an AnimSequence in one call. Auto-creates each bone track first (set_bone_keyframes silently leaves a T-pose if the track is missing), wraps the batch in one transaction, and raises if any bone fails instead of reporting hollow success (#540). Params: assetPath, tracks ([{bone, keyframes:[{location,rotation{x,y,z,w},scale?}]}]), save? (default true)", "bake_keyframes_batch", (p) => ({ assetPath: p.assetPath, tracks: p.tracks, save: p.save })),
    get_bone_transforms:  bp("Read reference pose transforms for one, many, or ALL bones. Omit boneNames to return every bone with index/parentIndex/location/rotation/scale. With boneNames, returns only the named bones. Params: skeletonPath, boneNames? (omit = all bones), space? ('local' default, or 'component' for composed parent-chain transforms - retarget-chain / anatomical-scale work) (#245)", "get_bone_transforms"),
    inspect_anim_nodes:   bp("Deep-dump the FAnimNode_* struct of anim graph nodes (PoseDriver PoseTargets/PoseAsset/RBF params/source bones, etc.) that read_anim_graph omits because it skips the 'Node' property. Params: assetPath, graphName? (default AnimGraph), nodeClass? (substring filter, e.g. 'PoseDriver') (#657)", "inspect_anim_nodes", (p) => ({ assetPath: p.assetPath, graphName: p.graphName, nodeClass: p.nodeClass })),
    compare_curves_to_morph_targets: bp("Compare an AnimSequence/PoseAsset's curve names against a SkeletalMesh's morph target names. Returns curves[], morphTargets[], matched[], curvesWithoutMorph[], morphsWithoutCurve[] - verify authored curves drive morphs without Python. Params: animPath (AnimSequence or PoseAsset), skeletalMeshPath (#656)", "compare_curves_to_morph_targets", (p) => ({ animPath: p.animPath, skeletalMeshPath: p.skeletalMeshPath })),
    set_montage_sequence: bp("Replace the animation sequence in a montage slot. With segmentIndex, replaces only that one segment; without it, replaces every segment in the slot. Params: assetPath, animSequencePath, slotIndex? (default 0), segmentIndex? (#626)", "set_montage_sequence", (p) => ({ assetPath: p.assetPath, animSequencePath: p.animSequencePath, slotIndex: p.slotIndex, segmentIndex: p.segmentIndex })),
    set_montage_properties: bp("Set montage properties. Params: assetPath, sequenceLength?, rateScale?, blendIn?, blendOut?", "set_montage_properties"),
    create_state_machine: bp("Create state machine in AnimBP. Params: assetPath, name?, graphName?", "create_state_machine"),
    add_state:            bp("Add state to a state machine. Params: assetPath, stateMachineName, stateName", "add_state"),
    add_transition:       bp("Add directed transition between states. Params: assetPath, stateMachineName, fromState, toState", "add_transition"),
    set_state_animation:  bp("Assign anim asset to state. Params: assetPath, stateMachineName, stateName, animAssetPath", "set_state_animation"),
    set_transition_blend: bp("Set blend type/duration on transition. Params: assetPath, stateMachineName, fromState, toState, blendDuration?, blendLogic?", "set_transition_blend"),
    set_transition_condition: bp("Set a transition's 'can enter transition' condition from a bool variable, keyed by transition (not graph name - every rule graph is named 'Transition' so blueprint graph tools can only reach the first). Wires VariableGet(bool) -> bCanEnterTransition, replacing any prior condition. Identify the transition by transitionGuid (from add_transition/read_state_machine) OR fromState+toState. Params: assetPath, stateMachineName, variableName (existing bool var), transitionGuid? OR fromState?+toState?, negate? (default false) (#707)", "set_transition_condition", (p) => ({ assetPath: p.assetPath, stateMachineName: p.stateMachineName, variableName: p.variableName, transitionGuid: p.transitionGuid, fromState: p.fromState, toState: p.toState, negate: p.negate })),
    read_state_machine:   bp("Read state machine topology. Params: assetPath, stateMachineName", "read_state_machine"),
    set_state_machine_entry: bp("Point the state machine's ENTRY node at a state, which is the only thing that gives a machine an initial state. create_state_machine and add_state never wired it, so a machine authored through the bridge compiled with no entry and produced the reference pose at runtime. There is no property behind this: it is the pin link from UAnimationStateMachineGraph::EntryNode to the state's input pin. An omitted or empty stateName clears the link instead, so set and clear are one pair. Reports previousEntryState, and unchanged=true on a replay. Params: assetPath, stateMachineName, stateName?", "set_state_machine_entry", (p) => ({ assetPath: p.assetPath, stateMachineName: p.stateMachineName, stateName: p.stateName })),
    remove_state:         bp("Remove a state from a state machine, together with every transition that touched it and the graphs they own - a transition whose endpoint is gone fails the blueprint compile. Returns removedTransitions so they can be replayed, and warns when the removed state was the entry state. Idempotent: alreadyDeleted=true when the state is not there. Params: assetPath, stateMachineName, stateName", "remove_state", (p) => ({ assetPath: p.assetPath, stateMachineName: p.stateMachineName, stateName: p.stateName })),
    remove_transition:    bp("Remove a transition and its rule graph. Address it by transitionGuid (from add_transition or read_state_machine) or by fromState plus toState, which can match several and removes all of them. A rule graph SHARED with another transition is left in place and counted in sharedRuleGraphsKept. Idempotent: alreadyDeleted=true when nothing matched. Params: assetPath, stateMachineName, transitionGuid?, fromState?, toState?", "remove_transition", (p) => ({ assetPath: p.assetPath, stateMachineName: p.stateMachineName, transitionGuid: p.transitionGuid, fromState: p.fromState, toState: p.toState })),
    remove_state_machine: bp("Remove a state machine node and everything inside it: its states, its transitions, each of their bound graphs, and the machine's own graph. Leaving a bound graph behind after its node is gone is what makes the next compile assert, so the teardown is explicit rather than left to the node. Idempotent: alreadyDeleted=true when the machine is not there. Params: assetPath, stateMachineName", "remove_state_machine", (p) => ({ assetPath: p.assetPath, stateMachineName: p.stateMachineName })),
    read_anim_graph:      bp("Read AnimBP AnimGraph nodes with properties & pins. Params: assetPath, graphName?", "read_anim_graph"),
    add_curve:            bp("Add float curve to AnimSequence. Params: assetPath, curveName, curveType?", "add_curve"),
    remove_curve:         bp("Remove a float curve from an AnimSequence through the animation data controller, so the model, the compressed data and the editor stay in step. Reports removedKeyCount, which is what the rollback cannot restore: add_curve puts back an empty curve of the same name and the keys have to be replayed with set_anim_curve_keys. Idempotent: alreadyDeleted=true when there is no such curve, and the miss lists the curves that do exist. Params: assetPath, curveName", "remove_anim_curve", (p) => ({ assetPath: p.assetPath, curveName: p.curveName })),
    set_anim_curve_keys:  bp("Set float-curve key VALUES on an AnimSequence (add_curve only creates an empty named curve - it cannot set keyframe values). Adds the curve if missing, then replaces its keys. Use for authoring Distance/Speed/any float curve directly. Params: assetPath, curveName, keys ([{time, value, interp?('linear'|'constant'|'cubic')}]), interpolation? (default 'linear', applied to keys without their own interp) (#712)", "set_anim_curve_keys", (p) => ({ assetPath: p.assetPath, curveName: p.curveName, keys: p.keys, interpolation: p.interpolation })),
    apply_animation_modifier: bp("Instantiate a UAnimationModifier subclass and run it on an AnimSequence. Headline use: modifierClass='DistanceCurveModifier' bakes a Distance curve from the clip's root motion for distance matching (needs root motion baked first - see bake_root_motion_from_bone). Registers the modifier on the sequence so it re-applies on reimport. props sets the modifier's EditAnywhere fields (e.g. DistanceCurveModifier: {CurveName, Axis:'XY'|'X'|..., bStopAtEnd, StopSpeedThreshold, SampleRate}). Note: DistanceCurveModifier ships in the 'Animation Locomotion Library' plugin (off by default) - enable it first. Params: assetPath, modifierClass (short name or /Script path), props? (#712)", "apply_animation_modifier", (p) => ({ assetPath: p.assetPath, modifierClass: p.modifierClass, props: p.props })),
    set_montage_slot:     bp("Set slot name on a montage track. Params: assetPath, slotName, trackIndex?", "set_montage_slot"),
    remove_montage_section: bp("Remove a composite section from a montage through UAnimMontage::DeleteAnimCompositeSection, then clear every OTHER section whose next-section link pointed at it, because a montage that jumps to a section which no longer exists stops dead. The cleared ones come back in clearedNextLinks so they can be re-pointed with asset(set_property) on CompositeSections[i].NextSectionName. Idempotent: alreadyDeleted=true when there is no such section, and the miss lists the sections that exist. Params: assetPath, sectionName", "remove_montage_section", (p) => ({ assetPath: p.assetPath, sectionName: p.sectionName })),
    add_notify_state:     bp("Add a windowed notify (a UAnimNotifyState) to a sequence or montage: the form that spans a duration and fires NotifyBegin, NotifyTick and NotifyEnd, which is what a combo window, a hit window or a timed particle effect is built from. add_notify only ever writes the instant form, so this was unreachable. notifyStateClass takes a class name, a bare suffix ('TimedParticleEffect' resolves AnimNotifyState_TimedParticleEffect), or a full path, and an unresolved one is refused rather than silently dropped. notifyProperties are validated against the class before anything is written. Returns objectPath for further editor(set_property) writes, and the full notifyStates list on the asset. Params: assetPath, notifyName, notifyStateClass, triggerTime, duration, notifyProperties?, branchingPoint?", "add_anim_notify_state", (p) => ({ assetPath: p.assetPath, notifyName: p.notifyName, notifyStateClass: p.notifyStateClass, triggerTime: p.triggerTime, duration: p.duration, notifyProperties: p.notifyProperties, branchingPoint: p.branchingPoint })),
    remove_notify_state:  bp("Remove windowed notifies by name and/or class; both filters apply together and at least one is required. This is a separate action rather than a flag on remove_notify because that handler's class filter only ever inspects FAnimNotifyEvent::Notify, so a notify STATE is invisible to it. Idempotent: alreadyDeleted=true when nothing matched. Params: assetPath, notifyName?, notifyStateClass?", "remove_anim_notify_state", (p) => ({ assetPath: p.assetPath, notifyName: p.notifyName, notifyStateClass: p.notifyStateClass })),
    set_sync_markers:     bp("Author an AnimSequence's sync markers: apply the list, refresh the sequence's marker index, register the names on the skeleton, then read back what actually landed. A plain property write reaches AuthoredSyncMarkers and leaves it inert, because the runtime reads UniqueMarkerNames and the index built by RefreshSyncMarkerDataFromAuthored, and the editor only offers marker names the skeleton has seen. markerMode 'replace' (default) makes the array the whole list, so an empty array clears them; 'merge' adds or moves only the named ones. The whole batch is validated against the clip length before anything is written. Params: assetPath, markers?, removeMarkers?, markerMode?", "set_sync_markers", (p) => ({ assetPath: p.assetPath, markers: p.markers, removeMarkers: p.removeMarkers, markerMode: p.markerMode })),
    add_montage_section:  bp("Add composite section to montage. Pass segmentIndex (with slotName or slotIndex) to anchor the section to a specific segment: its startTime is taken from that segment and it stays linked, so inserting a segment ahead of it moves the marker with its animation. Without segmentIndex the section is a bare absolute-time marker. Params: assetPath, sectionName, startTime?, linkedSection?, segmentIndex?, slotName?, slotIndex? (#826)", "add_montage_section"),
    add_montage_segment:  bp("Append (or insert) an animation segment into a montage slot's anim track. This is the only way to get more than one animation into a montage: create_montage builds exactly one segment, set_montage_sequence replaces rather than appends, and add_montage_section only writes a time marker with no animation behind it. Creates the named slot when it does not exist. Validates that the source shares the montage's skeleton and matches the track's additive type, then relays out the segments, refreshes linked sections and notifies, and rewrites the montage length. Params: assetPath, animSequencePath, slotName? (created if absent), slotIndex? (default 0, used when slotName is omitted), startPos? (trim into the source, default 0), endPos? (default source play length), playRate? (default 1, negative reverses), loopCount? (default 1), insertIndex? (default appends) (#826)", "add_montage_segment", (p) => ({ assetPath: p.assetPath, animSequencePath: p.animSequencePath, slotName: p.slotName, slotIndex: p.slotIndex, startPos: p.startPos, endPos: p.endPos, playRate: p.playRate, loopCount: p.loopCount, insertIndex: p.insertIndex })),
    remove_montage_segment: bp("Remove a segment from a montage slot by index, then relay out the remaining segments and rewrite the montage length. Idempotent: alreadyDeleted=true when the slot already holds no segments. Params: assetPath, segmentIndex, slotName?, slotIndex? (default 0) (#826)", "remove_montage_segment", (p) => ({ assetPath: p.assetPath, segmentIndex: p.segmentIndex, slotName: p.slotName, slotIndex: p.slotIndex })),
    list_montage_segments: bp("List every slot's segments on a montage so a caller can address them by index: animation path, startPos/endPos trim, playRate, loopCount, track position and length per segment, plus the sections with the slot and segment each one links to. Params: assetPath, slotName? (filter to one slot) (#826)", "list_montage_segments", (p) => ({ assetPath: p.assetPath, slotName: p.slotName })),
    create_ik_rig:        bp("Create IKRigDefinition asset, optionally with retargetRoot + chains[]. Params: name, skeletalMeshPath, packagePath?, retargetRoot?, chains?: [{name, startBone, endBone, goal?}]", "create_ik_rig"),
    read_ik_rig:          bp("Read an IK Rig's preview mesh, skeleton roots/bones, ancestry-validated chains and goal assignments, concrete goals, exclusions, and structured solver/FBIK effector state. Params: assetPath", "read_ik_rig"),
    configure_ik_rig:     bp("UE 5.8 only. Author an existing IK Rig through UIKRigController with strict bone, ancestry, goal, and setting validation, native readback, one transaction, and checked save; older engines return unsupported_engine_version. autoSetup='retarget' installs the native retarget definition; 'full_body' installs the retarget definition then Full Body IK before requested desired-state upserts. Params: rigPath, autoSetup? ('retarget'|'full_body'), retargetRoot?, rootMotionBone?, chains?: [{name,startBone,endBone,goal?}], fullBodyIK?: {solverIndex?,rootBone,enabled?,goals:[{name,bone,positionAlpha?,rotationAlpha?,chainDepth?,strengthAlpha?,pullChainAlpha?,pinRotation?}]}, exclusions?: [{bone,excluded}].", "configure_ik_rig", (p) => ({ rigPath: p.rigPath, autoSetup: p.autoSetup, retargetRoot: p.retargetRoot, rootMotionBone: p.rootMotionBone, chains: p.chains, fullBodyIK: p.fullBodyIK, exclusions: p.exclusions })),
    list_control_rig_variables: bp("List ControlRig variables and hierarchy. Params: assetPath", "list_control_rig_variables"),
    read_control_rig_graph: bp("Read a Control Rig's RigVM models: every graph with its nodes (name, node path, class), each node's pins (name, pin path, cppType, direction, execute flag, default value, nested sub-pins) and the links between them, plus full member-variable metadata (type, subtype, array-ness, default, public/read-only). list_control_rig_variables only ever reported a node COUNT, which is not enough to verify solver wiring (#774). Params: assetPath, graphName? (substring filter), includePins? (default true), includeDefaults? (default true), includeLinks? (default true), limit? (nodes per graph, default 200)", "read_control_rig_graph", (p) => ({ assetPath: p.assetPath, graphName: p.graphName, includePins: p.includePins, includeDefaults: p.includeDefaults, includeLinks: p.includeLinks, limit: p.limit })),
    read_control_rig_hierarchy: bp("Read a Control Rig's per-element hierarchy metadata: each element's name, type (Bone|Control|Null|Curve...), index, and parent. Params: assetPath (#619)", "read_control_rig_hierarchy", (p) => ({ assetPath: p.assetPath })),
    begin_control_rig_edit: bp("UE 5.8 only. Create a Sequencer Control Rig editing session over a source AnimSequence; native returns unsupported_engine_version on older engines. Baseline first: before this call, reuse or create a Control Rig for the target character, bind/import the exact target skeleton, add the intended controls, author Forward Solve, add Backward/Inverse Solve, verify it with read_control_rig_hierarchy/read_control_rig_graph, and pass an unchanged source round-trip. For a new baseline, the bundled Epic 5.8 controlrig actions include epic_create, epic_import_bones_from_asset, epic_add_control, and epic_add_backward_solve_graph; epic_create alone is not a usable rig. There is no silent fallback to raw bone-key authoring. rigMode='fk' uses UFKControlRig only when generated FK controls are sufficient; rigMode='asset' requires the verified controlRigPath and rejects rigs without inverse execution. bindingTag is the stable natural key for replay. onConflict is skip|error (default error); existing sessions are never modified. layered defaults false. startFrame is inclusive and endFrame is exclusive. Params: sequencePath, skeletalMeshPath, sourceAnimationPath, rigMode ('fk'|'asset'), controlRigPath?, layered?, startFrame?, endFrame?, displayRate?, bindingTag?, onConflict?. Returns the resolved bindingTag/binding GUID, rig, frame range, controls and created/existed status.", "begin_control_rig_edit", (p) => ({ sequencePath: p.sequencePath, skeletalMeshPath: p.skeletalMeshPath, sourceAnimationPath: p.sourceAnimationPath, rigMode: p.rigMode, controlRigPath: p.controlRigPath, layered: p.layered, startFrame: p.startFrame, endFrame: p.endFrame, displayRate: p.displayRate, bindingTag: p.bindingTag, onConflict: p.onConflict })),
    read_control_rig_edit: bp("UE 5.8 only. Read transform, bool, float/scale-float, and integer/enum controls from a Control Rig editing session without changing editor state; native returns unsupported_engine_version on older engines and has no silent fallback. Params: sequencePath, bindingTag, controlNames?, frames?, space? ('local'|'global'). Scalar samples return value instead of transform. Control metadata includes native controlType, animatable, and enum path/options where applicable. Returns session identity, layered mode, range/rate, filtered control metadata, and requested frame samples.", "read_control_rig_edit", (p) => ({ sequencePath: p.sequencePath, bindingTag: p.bindingTag, controlNames: p.controlNames, frames: p.frames, space: p.space })),
    apply_control_rig_edits: bp("UE 5.8 only. Apply typed Control Rig edits in one transaction; native returns unsupported_engine_version on older engines. There is no silent fallback to raw bone tracks. set_keys writes strictly ordered full per-frame transforms from normalized quaternions and preserves shortest-arc quaternion continuity. A set operation writes one full absolute transform at frame or frames. An offset operation applies translation/rotation/scale deltas across an inclusive frame range with optional edge blends. contact_lock densely constrains a translatable driver control, or an optional driven bone/socket reference, to a fixed component-space target with smooth edge blends and optional pole/control stabilization. Driver and stabilizer keys are read back transactionally. A drivenReference contact returns verification='bake_and_analyze_required'; bake it and analyze every constrained frame before accepting the bone/socket result. set_bool, set_float, and set_int key matching scalar controls; enum controls use set_int with one of the integer values reported in enumOptions. Params: sequencePath, bindingTag, operations[] (each element is one of: set_keys={op:'set_keys',control,keys:[{frame,transform:{translation,rotationQuaternion,scale}}],space?}, set={op:'set',control,frame|frames,transform:{translation,rotationDegrees,scale},space?}, offset={op:'offset',control,startFrame,endFrame,translationCm?,rotationDegrees?,scaleMultiplier?,space?,blendInFrames?,blendOutFrames?}, contact_lock={op:'contact_lock',control,drivenReference?,startFrame,endFrame,target:{translation,rotationQuaternion?},blendInFrames?,blendOutFrames?,stabilizeControls?,positionToleranceCm?,rotationToleranceDegrees?}, set_bool={op:'set_bool',control,frame|frames,value}, set_float={op:'set_float',control,frame|frames,value}, or set_int={op:'set_int',control,frame|frames,value}). Sequencer's current interpolation mode is retained. Returns per-operation counts, affected controls/frames, and contactQa summaries; a failed key/readback batch is undone.", "apply_control_rig_edits", (p) => ({ sequencePath: p.sequencePath, bindingTag: p.bindingTag, operations: p.operations })),
    bake_control_rig_edit: bp("UE 5.8 only. Bake the evaluated Control Rig session to a new AnimSequence asset; native returns unsupported_engine_version on older engines and has no raw-track fallback. The source LevelSequence remains unchanged. outputAssetPath is the output natural key; onConflict is skip|error (default error), never overwrite. Key reduction and Sequencer links are not supported yet, so reduceKeys/createLink must be false or omitted. Params: sequencePath, bindingTag, outputAssetPath, frameRate?, reduceKeys?, tolerance?, createLink?, onConflict?. Returns output asset metadata, frame/rate counts, status, and delete-created-asset rollback.", "bake_control_rig_edit", (p) => ({ sequencePath: p.sequencePath, bindingTag: p.bindingTag, outputAssetPath: p.outputAssetPath, frameRate: p.frameRate, reduceKeys: p.reduceKeys, tolerance: p.tolerance, createLink: p.createLink, onConflict: p.onConflict })),
    analyze_animation: bp("Cross-version, data-driven AnimSequence inspection using the native animation APIs available in the compiled engine. Samples an AnimSequence and reports deterministic numeric motion diagnostics without Python or viewport inference. Params: assetPath (required AnimSequence), skeletalMeshPath?, boneNames?, frames?, sampleRate?, loop?, outputDirectory? (must resolve under Project/Saved/Codex/AnimationQA and must not already contain artifacts). Returns source/rate/range metadata, sampled local/component transforms, root-motion and continuity metrics, and any written analysis artifacts.", "analyze_animation", (p) => ({ assetPath: p.assetPath, skeletalMeshPath: p.skeletalMeshPath, boneNames: p.boneNames, frames: p.frames, sampleRate: p.sampleRate, loop: p.loop, outputDirectory: p.outputDirectory })),
    set_root_motion:    bp("Set root motion settings on AnimSequence. Params: assetPath, enableRootMotion?, forceRootLock?, useNormalizedRootMotionScale?, rootMotionRootLock?", "set_root_motion_settings", (p) => ({ path: p.assetPath, enableRootMotion: p.enableRootMotion, forceRootLock: p.forceRootLock, useNormalizedRootMotionScale: p.useNormalizedRootMotionScale, rootMotionRootLock: p.rootMotionRootLock })),
    begin_skeleton_edit: bp("Open a batched bone-editing session over a skeletal mesh's reference skeleton. NOTHING is written until commit_skeleton_edit: a per-edit commit would re-derive the reference skeleton once per change and can leave a half-edited hierarchy, which is why this mirrors the begin/apply/bake lifecycle the Control Rig actions already use. sessionTag is the stable key every later call addresses and defaults to Skel_<MeshName>; reopening the same tag on the same mesh is idempotent. Returns the current bone list, the baseline bone count, and how many bones have skinned vertices. Params: skeletalMeshPath, sessionTag?", "begin_skeleton_edit", (p) => ({ skeletalMeshPath: p.skeletalMeshPath, sessionTag: p.sessionTag })),
    edit_skeleton_bones: bp("Apply a whole batch of hierarchy edits to an open session, validating every entry against the state its predecessors leave BEFORE mutating anything, so a bad entry at position nine does not leave the first eight applied. Removing a bone that has children, or that mesh sections skin vertices to, is refused with the dependents listed unless removeChildren or force says otherwise; a reparent that would form a cycle names the offending bone. Returns a per-edit changed/alreadyApplied row and an inverse-edit rollback. Params: sessionTag? OR skeletalMeshPath?, edits[] (each one of {op:'add',bone,parent,transform?} | {op:'remove',bone,removeChildren?} | {op:'rename',bone,newName} | {op:'reparent',bone,parent} | {op:'set_transform',bone,transform,moveChildren?}), force?", "edit_skeleton_bones", (p) => ({ sessionTag: p.sessionTag, skeletalMeshPath: p.skeletalMeshPath, edits: p.edits, force: p.force })),
    commit_skeleton_edit: bp("Write the session's batched edits into the skeletal mesh and its skeleton in one transaction, and save both packages. This is the ONLY call that touches the assets. A session with no pending edits closes without dirtying anything. The rollback is flagged lossy on purpose: skin weights and dependent-asset fix-ups cannot be reversed by replaying inverse edits. Params: sessionTag? OR skeletalMeshPath?", "commit_skeleton_edit", (p) => ({ sessionTag: p.sessionTag, skeletalMeshPath: p.skeletalMeshPath })),
    cancel_skeleton_edit: bp("Discard an open session's working copy without writing; the assets on disk are untouched. Cancelling a session that is not open reports alreadyClosed rather than failing, so replaying a rollback is safe. Returns the discarded edits so they can be re-sent after a fix. Params: sessionTag? OR skeletalMeshPath?", "cancel_skeleton_edit", (p) => ({ sessionTag: p.sessionTag, skeletalMeshPath: p.skeletalMeshPath })),
    set_bone_retargeting: bp("Set each bone's translation retargeting mode, which lives in the skeleton's private BoneTree and has no addressable UPROPERTY, so set_property cannot reach it. Omitting bones applies to every bone; includeChildren uses the engine's own recursive setter. Returns the prior and new mode per bone, and a per-bone restore rollback that replays through this same action. Params: skeletonPath, mode (Animation|Skeleton|AnimationScaled|AnimationRelative|OrientAndScale), bones? OR bone?, includeChildren?, restore? ([{bone, mode}])", "set_bone_retargeting", (p) => ({ skeletonPath: p.skeletonPath, mode: p.mode, bones: p.bones, bone: p.bone, includeChildren: p.includeChildren, restore: p.restore })),
    author_blend_profile: bp("Create a blend profile if it is absent and write its per-bone scales. A UBlendProfile is a per-skeleton subobject, so asset(set_property) can neither create it nor address the per-bone map. remove deletes the profile and reports every entry it destroyed, so the inverse can rebuild it. Returns the full entry list plus the skeleton's profile names. Params: skeletonPath, profileName, operation? (upsert|remove|rename), newProfileName?, mode? (TimeFactor|WeightFactor|BlendMask), entries? ([{bone, scale, recursive?}]), removeEntries? (bone names)", "author_blend_profile", (p) => ({ skeletonPath: p.skeletonPath, profileName: p.profileName, operation: p.operation, newProfileName: p.newProfileName, mode: p.mode, entries: p.entries, removeEntries: p.removeEntries })),
    edit_curve_metadata: bp("Author the skeleton curve metadata that compare_curves_to_morph_targets could only read, including the material and morph-target flags that drive curves into materials and morphs. The whole batch validates first, and every operation is idempotent: adding an existing curve or removing an absent one reports rather than errors. Returns the full curve metadata table after the edit. Params: skeletonPath, add? (curve names), remove? (curve names), rename? ([{from, to}]), flags? ([{curve, material?, morphTarget?}])", "edit_curve_metadata", (p) => ({ skeletonPath: p.skeletonPath, add: p.add, remove: p.remove, rename: p.rename, flags: p.flags })),
    register_compatible_skeleton: bp("Register, or with remove=true unregister, another skeleton as compatible, so its animations are usable on this one. This closes the loop asset(diff) opens: diff two skeletons for the hierarchy delta, then act on it. The engine call has been exercised in this repo's tests for a long time and was simply never shipped as an action. Returns the compatible list before and after and, per entry, whether the engine's own editor compatibility check agrees plus the source's bone count. Params: skeletonPath, compatibleSkeletonPath? OR compatibleSkeletonPaths?, remove?", "register_compatible_skeleton", (p) => ({ skeletonPath: p.skeletonPath, compatibleSkeletonPath: p.compatibleSkeletonPath, compatibleSkeletonPaths: p.compatibleSkeletonPaths, remove: p.remove })),
    add_virtual_bone:   bp("Add virtual bone. Params: skeletonPath, sourceBone, targetBone", "add_virtual_bone"),
    remove_virtual_bone: bp("Remove virtual bone. Params: skeletonPath, virtualBoneName", "remove_virtual_bone"),
    create_composite:   bp("Create AnimComposite. Params: name, skeletonPath, packagePath?", "create_anim_composite"),
    list_modifiers:     bp("List applied animation modifiers. Params: assetPath", "list_anim_modifiers", (p) => ({ path: p.assetPath })),
    create_ik_retargeter: bp("Create IKRetargeter asset and (default) initialize the UE 5.7 ops stack: assigns sourceRig+targetRig to all ops, runs AutoMapChains. Returns chainsMapped count. Params: name, packagePath?, sourceRig?, targetRig?, autoMapChains? (default true) (#246)", "create_ik_retargeter", (p) => ({ name: p.name, packagePath: p.packagePath, sourceRig: p.sourceRig, targetRig: p.targetRig, autoMapChains: p.autoMapChains, onConflict: p.onConflict })),
    read_ik_retargeter: bp("Read an IK Retargeter's source/target rigs and preview meshes, flattened and per-op chain mappings, typed op stack, and all named/current pose offsets when the compiled engine exposes them. Params: assetPath (#246)", "read_ik_retargeter", (p) => ({ assetPath: p.assetPath })),
    configure_ik_retargeter: bp("UE 5.8 only. Configure an existing IK Retargeter through UIKRetargeterController with the correct default-op and per-op rig assignment order, auto/manual chain mappings, named pose authoring, processor validation, native readback, transaction rollback, and checked save; older engines return unsupported_engine_version. Whole-pose auto-align resets that pose first: create a new pose or pass pose.reset=true to acknowledge replacement, then manual offsets are applied. Params: retargeterPath, sourceRig?, targetRig?, sourcePreviewMesh?, targetPreviewMesh?, ensureDefaultOps? (default true), autoMapMode? ('exact'|'fuzzy'|'clear'), forceRemap? (default false), chainMappings?: [{targetChain,sourceChain?:string|null}], pose?: {side,name,create?,reset?,autoAlign?,bones?,rotationOffsets?:[{bone,rotationQuaternion}],rootOffsetZ?,snapBoneToGround?}.", "configure_ik_retargeter", (p) => ({ retargeterPath: p.retargeterPath, sourceRig: p.sourceRig, targetRig: p.targetRig, sourcePreviewMesh: p.sourcePreviewMesh, targetPreviewMesh: p.targetPreviewMesh, ensureDefaultOps: p.ensureDefaultOps, autoMapMode: p.autoMapMode, forceRemap: p.forceRemap, chainMappings: p.chainMappings, pose: p.pose })),
    set_ik_rig_mesh:      bp("Set the preview/source skeletal mesh on an EXISTING IK Rig. Params: rigPath, meshPath (#701)", "set_ik_rig_mesh", (p) => ({ rigPath: p.rigPath, meshPath: p.meshPath })),
    set_ik_retargeter_rig: bp("Set the source or target IK Rig on an EXISTING IK Retargeter. Params: retargeterPath, rigPath, side? (source|target, default target) (#703)", "set_ik_retargeter_rig", (p) => ({ retargeterPath: p.retargeterPath, rigPath: p.rigPath, side: p.side })),
    auto_align_retarget_pose: bp("Auto-align all bones of the source/target retarget pose (chain-to-chain) - fixes a retargeter that outputs a static reference pose. Params: retargeterPath, side? (source|target, default target) (#701)", "auto_align_retarget_pose", (p) => ({ retargeterPath: p.retargeterPath, side: p.side })),
    reset_retarget_pose:  bp("Reset the current retarget pose (all bones) to the reference pose. Params: retargeterPath, side? (source|target, default target) (#701)", "reset_retarget_pose", (p) => ({ retargeterPath: p.retargeterPath, side: p.side })),
    batch_retarget_animations: bp("Bake validated source AnimSequences onto the target skeleton through an IK Retargeter (RunBatchRetarget), save every output, and roll back newly created outputs if the batch is incomplete or unsavable. Overwrite is rejected. Returns mapping completeness and every unmapped target chain so partial retargets are explicit; pass requireCompleteMapping=true only when the target should have no intentional extra chains. Params: retargeterPath, sourceMesh, targetMesh, animPaths[], outputPath? (default: alongside source), prefix?, suffix? (default _Retargeted), overwrite? (must be false), requireCompleteMapping? (default false) (#701)", "batch_retarget_animations", (p) => ({ retargeterPath: p.retargeterPath, sourceMesh: p.sourceMesh, targetMesh: p.targetMesh, animPaths: p.animPaths, outputPath: p.outputPath, prefix: p.prefix, suffix: p.suffix, overwrite: p.overwrite, requireCompleteMapping: p.requireCompleteMapping })),
    set_anim_blueprint_skeleton: bp("Set target skeleton on AnimBP. Params: assetPath, skeletonPath", "set_anim_blueprint_skeleton"),
    read_bone_track:    bp("Read bone transform samples from AnimSequence. Params: assetPath, boneName, frames?: [int]", "read_bone_track"),
    create_pose_search_database: bp("Create a PoseSearchDatabase asset (motion matching). Pass skeletonPath and it authors the matching PoseSearchSchema (<name>_Schema, default channels) alongside it, which is what makes the database indexable at all; pass schemaPath to reuse an existing one. A schema that cannot index (no skeleton, or no feature channels) is refused rather than assigned, because the editor then reports the DATABASE as the invalid asset. Without either, the database is created empty and unindexable and says so in note. Params: name, packagePath?, skeletonPath?, schemaPath?, onConflict? (#833)", "create_pose_search_database", (p) => ({ name: p.name, packagePath: p.packagePath, skeletonPath: p.skeletonPath, schemaPath: p.schemaPath, onConflict: p.onConflict })),
    set_pose_search_schema:      bp("Set the Schema on an existing PoseSearchDatabase. Params: assetPath, schemaPath", "set_pose_search_schema", (p) => ({ assetPath: p.assetPath, schemaPath: p.schemaPath })),
    add_pose_search_sequence:    bp("Append an AnimSequence/AnimComposite/AnimMontage/BlendSpace to a PoseSearchDatabase, with optional per-clip flags. Params: assetPath, sequencePath, mirror? ('original'|'mirrored'|'both'), disableReselection?, sampleStart?, sampleEnd?, enabled? (#684)", "add_pose_search_sequence", (p) => ({ assetPath: p.assetPath, sequencePath: p.sequencePath, mirror: p.mirror, disableReselection: p.disableReselection, sampleStart: p.sampleStart, sampleEnd: p.sampleEnd, enabled: p.enabled })),
    set_pose_search_clips:       bp("Author the whole clip list of a PoseSearchDatabase in one call (the 'duplicate a stock PSD, swap its clips' pipeline step). Replaces the list by default. Each clip carries per-entry flags. Params: assetPath, clips ([{sequencePath, mirror? ('original'|'mirrored'|'both'), disableReselection?, sampleStart?, sampleEnd?, enabled?}] - a bare string path also works), clearExisting? (default true). Follow with build_pose_search_index (#684)", "set_pose_search_clips", (p) => ({ assetPath: p.assetPath, clips: p.clips, clearExisting: p.clearExisting })),
    build_pose_search_index:     bp("Build (or rebuild) the search index. Params: assetPath, wait? (default true)", "build_pose_search_index", (p) => ({ assetPath: p.assetPath, wait: p.wait })),
    read_pose_search_database:   bp("Inspect a PoseSearchDatabase: schema, animation entries, cost biases, tags. Params: assetPath", "read_pose_search_database", (p) => ({ assetPath: p.assetPath })),
    set_pose_search_database_settings: bp("Tune a PoseSearchDatabase: cost biases, KD-tree neighbours, search mode, PCA components, normalization set. Params: assetPath, continuingPoseCostBias?, baseCostBias?, loopingCostBias?, kdTreeQueryNumNeighbors?, numberOfPrincipalComponents?, poseSearchMode? ('bruteforce'|'pcakdtree'|'vptree'|'eventonly'), normalizationSetPath? (motion matching)", "set_pose_search_database_settings", (p) => ({ assetPath: p.assetPath, continuingPoseCostBias: p.continuingPoseCostBias, baseCostBias: p.baseCostBias, loopingCostBias: p.loopingCostBias, kdTreeQueryNumNeighbors: p.kdTreeQueryNumNeighbors, numberOfPrincipalComponents: p.numberOfPrincipalComponents, poseSearchMode: p.poseSearchMode, normalizationSetPath: p.normalizationSetPath })),
    create_pose_search_schema:   bp("Create a PoseSearchSchema (the feature definition a database indexes against). Binds a skeleton (and optional mirror table) and, by default, adds Trajectory+Pose default channels so the schema is immediately buildable. Refine with add_pose_search_schema_*_channel. Params: name, skeletonPath, packagePath?, mirrorDataTablePath?, sampleRate?, addDefaultChannels? (default true) (motion matching)", "create_pose_search_schema", (p) => ({ name: p.name, skeletonPath: p.skeletonPath, packagePath: p.packagePath, mirrorDataTablePath: p.mirrorDataTablePath, sampleRate: p.sampleRate, addDefaultChannels: p.addDefaultChannels, onConflict: p.onConflict })),
    add_pose_search_schema_pose_channel: bp("Add a Pose feature channel to a schema (samples named bones for velocity/position/rotation/phase). Params: schemaPath, bones ([{bone, flags?:['velocity','position','rotation','phase'], weight?}] - a bare bone-name string defaults to position), weight? (motion matching)", "add_pose_search_schema_pose_channel", (p) => ({ schemaPath: p.schemaPath, bones: p.bones, weight: p.weight })),
    add_pose_search_schema_trajectory_channel: bp("Add a Trajectory feature channel to a schema (past/future motion samples). Params: schemaPath, samples ([{offset (seconds; negative=history, positive=prediction), flags?:['position','velocity','facingDirection','velocityDirection', ...XY variants], weight?}]), weight? (motion matching)", "add_pose_search_schema_trajectory_channel", (p) => ({ schemaPath: p.schemaPath, samples: p.samples, weight: p.weight })),
    read_pose_search_schema:     bp("Inspect a PoseSearchSchema: skeleton(s), mirror table, sample rate, feature channels. Params: schemaPath (motion matching)", "read_pose_search_schema", (p) => ({ schemaPath: p.schemaPath })),
    create_mirror_data_table:    bp("Create a MirrorDataTable for a skeleton (needed for mirrored poses in motion matching / mirror nodes). Auto-derives bone-pair rows from find/replace expressions (defaults to UE mannequin _l/_r suffix swap). Params: name, skeletonPath, packagePath?, expressions? ([{find, replace, method?:'suffix'|'prefix'|'regex'}]), mirrorAxis? (X|Y|Z, default X), mirrorRootMotion? (default true)", "create_mirror_data_table", (p) => ({ name: p.name, skeletonPath: p.skeletonPath, packagePath: p.packagePath, expressions: p.expressions, mirrorAxis: p.mirrorAxis, mirrorRootMotion: p.mirrorRootMotion, onConflict: p.onConflict })),
    read_mirror_data_table:      bp("Inspect a MirrorDataTable: skeleton and bone-pair rows (name -> mirroredName). Params: assetPath (motion matching)", "read_mirror_data_table", (p) => ({ assetPath: p.assetPath })),
    create_pose_search_normalization_set: bp("Create a PoseSearchNormalizationSet grouping databases so they normalize their cost space together (consistent blending across a locomotion set). Assign it via set_pose_search_database_settings(normalizationSetPath). Params: name, packagePath?, databases? ([PoseSearchDatabase paths]) (motion matching)", "create_pose_search_normalization_set", (p) => ({ name: p.name, packagePath: p.packagePath, databases: p.databases, onConflict: p.onConflict })),
    add_motion_matching_node:    bp("Add a Motion Matching node to an AnimBP AnimGraph and point it at a PoseSearchDatabase (the runtime node that searches the database each frame). Connects its output to the Output Pose by default. For chooser-driven database selection, bind an anim-node function that calls SetDatabasesToSearch. Params: assetPath (AnimBP), databasePath, graphName? (default AnimGraph), connectToOutput? (default true), blendTime? (motion matching)", "add_motion_matching_node", (p) => ({ assetPath: p.assetPath, databasePath: p.databasePath, graphName: p.graphName, connectToOutput: p.connectToOutput, blendTime: p.blendTime })),
    add_pose_history_node:       bp("Add a Pose History (PoseSearchHistoryCollector) node to an AnimBP AnimGraph - the Motion Matching node needs it in the graph to query pose/trajectory history. Defaults to self-generated trajectory (no external trajectory pin needed) and inserts itself into the pose chain feeding the Output Pose. Params: assetPath (AnimBP), graphName? (default AnimGraph), poseCount?, samplingInterval?, generateTrajectory? (default true), trajectoryHistoryCount?, trajectoryPredictionCount?, insertBeforeOutput? (default true) (motion matching)", "add_pose_history_node", (p) => ({ assetPath: p.assetPath, graphName: p.graphName, poseCount: p.poseCount, samplingInterval: p.samplingInterval, generateTrajectory: p.generateTrajectory, trajectoryHistoryCount: p.trajectoryHistoryCount, trajectoryPredictionCount: p.trajectoryPredictionCount, insertBeforeOutput: p.insertBeforeOutput })),
    set_motion_matching_chooser: bp("Drive the Motion Matching node's Database from a ChooserTable so the database is selected at runtime by character state. Wires a thread-safe EvaluateChooser (result typed to PoseSearchDatabase) into the MM node's Database pin. contextSource selects what the chooser reads its columns from: 'self' (default, the anim instance - choosers branching on AnimBP variables) or 'pawn' (the owning pawn via TryGetPawnOwner - choosers branching on character/pawn state). Params: assetPath (AnimBP), chooserPath (ChooserTable), graphName? (default AnimGraph), contextSource? ('self'|'pawn') (motion matching)", "set_motion_matching_chooser", (p) => ({ assetPath: p.assetPath, chooserPath: p.chooserPath, graphName: p.graphName, contextSource: p.contextSource })),
    // #713 - distance-matching graph authoring
    add_sequence_evaluator:   bp("Add a Sequence Evaluator node (explicit-time player) to an AnimBP graph - the node distance matching drives by setting its ExplicitTime each frame. graphName can be the top-level AnimGraph or a state's inner graph (pass the state name). Defaults bTeleportToExplicitTime=false so time advances and root motion extracts. Connects to the Output Pose by default. Returns nodeGuid for bind_anim_node_function. Params: assetPath (AnimBP), sequencePath? (AnimSequence to evaluate), graphName? (default AnimGraph), explicitTime?, shouldLoop?, teleportToExplicitTime? (default false), connectToOutput? (default true) (#713)", "add_sequence_evaluator", (p) => ({ assetPath: p.assetPath, sequencePath: p.sequencePath, graphName: p.graphName, explicitTime: p.explicitTime, shouldLoop: p.shouldLoop, teleportToExplicitTime: p.teleportToExplicitTime, connectToOutput: p.connectToOutput })),
    bind_anim_node_function:  bp("Bind a thread-safe anim-node function to an anim graph node's update slot - the mechanism distance matching uses to advance a Sequence Evaluator's explicit time each frame (function calls AnimDistanceMatchingLibrary::DistanceMatchToTarget / AdvanceTimeByDistanceMatching). The function must already exist on the AnimBP (create it as a BlueprintThreadSafe function first). Identify the node by nodeGuid (from add_sequence_evaluator / add_*_node). Params: assetPath (AnimBP), nodeGuid, functionName, graphName? (default AnimGraph), binding? ('update' (default)|'becomeRelevant'|'initialUpdate') (#713)", "bind_anim_node_function", (p) => ({ assetPath: p.assetPath, nodeGuid: p.nodeGuid, functionName: p.functionName, graphName: p.graphName, binding: p.binding })),
    // v1.0.0-rc.2 - #153, #154 (animation authoring gaps)
    set_sequence_properties: bp("Batch-set properties on AnimSequence assets. If a path is a Montage and resolveFromMontages is true (default), resolves to its first AnimSequence. Params: assetPaths[], properties{enableRootMotion?, forceRootLock?, useNormalizedRootMotionScale?, rootMotionRootLock?}, resolveFromMontages?", "set_sequence_properties"),
    bake_root_motion_from_bone: bp("Bake delta translation from a source bone (e.g. pelvis) onto the root bone across the whole sequence; compensates the source bone so world-space position is unchanged. Params: assetPath, sourceBone, rootBone? (default 'root'), axes? (default ['x','y']), interpolation? ('linear'|'per_frame', default 'linear')", "bake_root_motion_from_bone"),
    // #419/#420 - live-actor skeletal operations (moved from level for proper domain alignment)
    get_bone_transform: bp("Read a bone or socket transform on a live actor's SkeletalMeshComponent. Wraps GetBoneTransform / GetSocketTransform. Params: actorLabel OR actorPath, boneName (or socket name), componentName? (default: CharacterMesh0 / Mesh / first SK component), world? (auto|pie|game|editor, default auto), space? (world|component|local, default world). Returns location, rotation, scale (#420)", "get_bone_transform", (p) => ({ actorLabel: p.actorLabel, actorPath: p.actorPath, boneName: p.boneName, componentName: p.componentName, world: p.world, space: p.space })),
    list_bones: bp(paged("List bones in a live actor's SkeletalMeshComponent ref skeleton (name, index, parent), in reference-skeleton order so parents precede children. Params: actorLabel OR actorPath, componentName?, world? (auto|pie|game|editor, default auto) (#420)"), "list_bones", (p) => ({ actorLabel: p.actorLabel, actorPath: p.actorPath, componentName: p.componentName, world: p.world, cursor: p.cursor, limit: p.limit })),
    rebind_leader_pose: bp("Re-bind every secondary SkeletalMeshComponent on an actor to a body component (default CharacterMesh0 / Mesh). One-call fix for the 'character explodes after rotating the actor' failure mode. Params: actorLabel OR actorPath, bodyComponent? (#419)", "rebind_leader_pose", (p) => ({ actorLabel: p.actorLabel, actorPath: p.actorPath, bodyComponent: p.bodyComponent })),
    // #922/#923/#926 - evaluated pose reads. Everything above this line reads
    // either raw local-space tracks or the reference pose; these evaluate.
    sample_pose: bp("Evaluate an AnimSequence (or a BlendSpace at a blend position) and return COMPOSED bone transforms, which read_bone_track (raw local space) and get_bone_transforms (reference pose only) cannot give. Params: assetPath, boneNames? (omit for every bone), frames? or times? (omit for every sampled key), space? (component default | local | world), skeletalMeshPath? (evaluate with that mesh's proportions), incorporateRootMotion? (default true), blendPosition? {x,y,z} (BlendSpace only). Returns samples:[{frame, time, bones:[{name, location, rotation, scale}]}]. A montage is refused with a pointer to its segments, because a montage has no pose of its own (#923/#926/#922)", "sample_pose", (p) => ({ assetPath: p.assetPath ?? p.path, boneNames: p.boneNames, frames: p.frames, times: p.times, space: p.space, skeletalMeshPath: p.skeletalMeshPath, incorporateRootMotion: p.incorporateRootMotion, blendPosition: p.blendPosition })),
    get_live_bone_transforms: bp("Read the EVALUATED pose off a live SkeletalMeshComponent in the editor world or PIE, for many bones at once. This is the read that tells 'standing' from 'prone' when the reference pose cannot (#922). Also returns componentTransform and an evaluation block (animationMode, animInstanceClass, componentSpaceTransformCount) so a clean component transform sitting on a dead anim instance is visible in one call. Params: actorLabel OR actorPath, componentName? (default CharacterMesh0 / Mesh), boneNames? (omit for every bone, max 1000), space? (world default | component | local), world? (auto|pie|game|editor) (#922/#926)", "get_live_bone_transforms", (p) => ({ actorLabel: p.actorLabel, actorPath: p.actorPath, componentName: p.componentName, boneNames: p.boneNames, space: p.space, world: p.world })),
    measure_natural_speed: bp("Measure a locomotion clip's planted-foot speed, in cm/s, by evaluating the pose and tracking the lowest foot's horizontal travel while it is in contact. Every retarget changes natural speed by the target skeleton's leg-length ratio, so a BlendSpace built on retargeted clips has to re-measure per clip per character (#923). Params: assetPath, footBones[] (e.g. ['foot_l','foot_r']), contactThreshold? (contact height in cm; omit to derive it from the clip), skeletalMeshPath?, frames?/times?, blendPosition? (BlendSpace only). Returns naturalSpeed, plantedDistance, plantedTime, contactThreshold, per-foot breakdown", "measure_natural_speed", (p) => ({ assetPath: p.assetPath ?? p.path, footBones: p.footBones, contactThreshold: p.contactThreshold, skeletalMeshPath: p.skeletalMeshPath, frames: p.frames, times: p.times, blendPosition: p.blendPosition })),
    preview_animation: bp("Toggle bUpdateAnimationInEditor + VisibilityBasedAnimTickOption=AlwaysTickPoseAndRefreshBones on every SkeletalMeshComponent of an actor. Bypasses the 'cannot be edited on templates' guard for level instances. Params: actorLabel OR actorPath, enabled (#419/#420)", "preview_animation", (p) => ({ actorLabel: p.actorLabel, actorPath: p.actorPath, enabled: p.enabled })),
  },
  undefined,
  {
    assetPath: z.string().optional(),
    path: z.string().optional().describe("Alias for assetPath, accepted by sample_pose and measure_natural_speed"),
    force: z.boolean().optional().describe("edit_skeleton_bones: remove a bone despite dependents, which are listed in the refusal"),
    mode: z.string().optional().describe("set_bone_retargeting: Animation|Skeleton|AnimationScaled|AnimationRelative|OrientAndScale. author_blend_profile: TimeFactor|WeightFactor|BlendMask"),
    bone: z.string().optional().describe("set_bone_retargeting: a single bone, as an alternative to bones[]"),
    sessionTag: z.string().optional().describe("Skeleton edit lifecycle: the stable key addressing an open session (defaults to Skel_<MeshName>)"),
    edits: z.array(z.record(z.unknown())).optional().describe("edit_skeleton_bones: the batch of add/remove/rename/reparent/set_transform operations"),
    includeChildren: z.boolean().optional().describe("set_bone_retargeting: apply recursively down the bone tree"),
    restore: z.array(z.record(z.unknown())).optional().describe("set_bone_retargeting: per-bone modes to restore, which is how its own rollback replays"),
    profileName: z.string().optional().describe("author_blend_profile: the blend profile to create or edit"),
    newProfileName: z.string().optional().describe("author_blend_profile: the new name when operation=rename"),
    operation: z.string().optional().describe("author_blend_profile: upsert (default) | remove | rename"),
    entries: z.array(z.record(z.unknown())).optional().describe("author_blend_profile: per-bone scales as [{bone, scale, recursive?}]"),
    removeEntries: z.array(z.string()).optional().describe("author_blend_profile: bone names to drop from the profile"),
    add: z.array(z.string()).optional().describe("edit_curve_metadata: curve names to add"),
    remove: z.union([z.boolean(), z.array(z.string())]).optional().describe("edit_curve_metadata: curve names to remove. register_compatible_skeleton: true to unregister instead of register"),
    rename: z.array(z.record(z.string())).optional().describe("edit_curve_metadata: [{from, to}] renames"),
    flags: z.array(z.record(z.unknown())).optional().describe("edit_curve_metadata: [{curve, material?, morphTarget?}] flag writes"),
    compatibleSkeletonPath: z.string().optional().describe("register_compatible_skeleton: the skeleton to mark compatible"),
    compatibleSkeletonPaths: z.array(z.string()).optional().describe("register_compatible_skeleton: several at once"),
    directory: z.string().optional(),
    recursive: z.boolean().optional(),
    targetTrackCount: z.number().optional().describe("Flag sequences with more than this many bone tracks"),
    includeTrackNames: z.boolean().optional().describe("Include full bone track name arrays in scan_animation_tracks results"),
    animSequencePath: z.string().optional(),
    items: z.array(z.object({
      name: z.string(),
      animSequencePath: z.string(),
      packagePath: z.string().optional(),
      // Stays a strict enum on purpose. The montage creation path this forwards
      // to compares against "error" and treats every other value as "skip", so
      // nothing downstream rejects a typo. Relaxing it would turn a clean
      // schema rejection into a silent skip that reports success.
      onConflict: z.enum(["skip", "error"]).optional(),
      slotName: z.string().optional(),
      trackIndex: z.number().int().nonnegative().optional(),
      rateScale: z.number().optional(),
      blendIn: z.number().nonnegative().optional(),
      blendOut: z.number().nonnegative().optional(),
      sequenceLength: z.number().positive().optional(),
      sections: z.array(z.object({
        sectionName: z.string(),
        startTime: z.number().nonnegative().optional(),
        linkedSection: z.string().optional(),
      })).optional(),
      notifies: z.array(z.object({
        notifyName: z.string(),
        triggerTime: z.number().nonnegative(),
        notifyClass: z.string().optional(),
        properties: z.record(z.unknown()).optional(),
      })).optional(),
    })).optional().describe("author_montages_batch: montage authoring specifications"),
    skeletonPath: z.string().optional().describe("USkeleton asset path; the target of set_bone_retargeting, author_blend_profile, edit_curve_metadata and register_compatible_skeleton"),
    name: z.string().optional(),
    packagePath: z.string().optional(),
    axisHorizontal: z.string().optional(),
    axisVertical: z.string().optional(),
    horizontalMin: z.number().optional(),
    horizontalMax: z.number().optional(),
    verticalMin: z.number().optional(),
    verticalMax: z.number().optional(),
    gridNumHorizontal: z.number().optional(),
    gridNumVertical: z.number().optional(),
    axis: z.record(z.unknown()).optional().describe("populate_blendspace: axis params for axis 0 (#459)"),
    blendspaceAxes: z.array(z.record(z.unknown())).optional().describe("populate_blendspace: per-axis params array (alias for backwards-compat) (#459)"),
    axisIndex: z.number().optional().describe("populate_blendspace: axis index when using 'axis' on a 2D blendspace (#459)"),
    axisName: z.string().optional().describe("create_blendspace_1d axis display name (#459)"),
    axisMin: z.number().optional(),
    axisMax: z.number().optional(),
    gridNum: z.number().optional(),
    samples: z.array(z.record(z.unknown())).optional().describe("populate_blendspace: [{animationPath, x, y?}] (#459)"),
    clearExisting: z.boolean().optional().describe("populate_blendspace: clear existing samples before adding new ones (default true) (#459)"),
    notifyName: z.string().optional(),
    triggerTime: z.number().optional(),
    notifyClass: z.string().optional(),
    notifyProperties: z.record(z.unknown()).optional().describe("add_notify / add_notify_state: EditAnywhere fields to set on the spawned notify object. add_notify requires notifyClass and add_notify_state requires notifyStateClass, because the values need an object to land on"),
    notifyStateClass: z.string().optional().describe("add_notify_state / remove_notify_state: a UAnimNotifyState subclass, as a class name, a bare suffix ('TimedParticleEffect'), or a full path"),
    duration: z.number().optional().describe("add_notify_state: length of the notify window in seconds, which is what distinguishes a notify state from an instant notify"),
    markers: z.array(z.object({ name: z.string(), time: z.number() })).optional().describe("set_sync_markers: [{name, time}] sync markers, in seconds"),
    removeMarkers: z.array(z.string()).optional().describe("set_sync_markers: marker names to drop, applied after markers"),
    markerMode: z.string().optional().describe("set_sync_markers: 'replace' (default; the markers array becomes the whole list, and an empty array clears them) or 'merge' (add or move only the named markers)"),
    branchingPoint: z.boolean().optional().describe("add_notify: force the montage notify's tick type. Branching-point notifies are the only ones UAnimNotify_PlayMontageNotify::BranchingPointNotify runs, so OnPlayMontageNotifyBegin needs one; the PlayMontageNotify classes default to true on a montage, everything else to the engine's queued tick (#880)"),
    slotIndex: z.number().optional(),
    segmentIndex: z.number().optional().describe("set_montage_sequence: replace only this segment index within the slot (#626). remove_montage_segment: segment to remove. add_montage_section: segment to anchor the section to (#826)"),
    // Montage segment authoring params (#826)
    startPos: z.number().optional().describe("add_montage_segment: trim start inside the source animation (default 0)"),
    endPos: z.number().optional().describe("add_montage_segment: trim end inside the source animation (default: source play length)"),
    playRate: z.number().optional().describe("add_montage_segment: segment play rate, negative plays in reverse (default 1)"),
    loopCount: z.number().int().min(1).optional().describe("add_montage_segment: how many times the segment repeats (default 1)"),
    insertIndex: z.number().int().min(0).optional().describe("add_montage_segment: position within the slot's segment list (default: append)"),
    sequenceLength: z.number().optional(),
    rateScale: z.number().optional(),
    blendIn: z.number().optional(),
    blendOut: z.number().optional(),
    numFrames: z.number().optional(),
    frameRate: z.number().optional().describe("Frames per second for create_sequence or bake_control_rig_edit."),
    boneName: z.string().optional(),
    boneNames: z.array(z.string()).optional(),
    parentClass: z.string().optional().describe("Parent AnimInstance class name for create_anim_blueprint"),
    // State machine params
    stateMachineName: z.string().optional(),
    stateName: z.string().optional(),
    fromState: z.string().optional(),
    toState: z.string().optional(),
    animAssetPath: z.string().optional().describe("Path to animation sequence or blendspace"),
    blendDuration: z.number().optional(),
    blendLogic: z.string().optional().describe("Standard or Inertialization"),
    variableName: z.string().optional().describe("Bool variable name for set_transition_condition (#707)"),
    transitionGuid: z.string().optional().describe("Transition GUID selector for set_transition_condition (#707)"),
    negate: z.boolean().optional().describe("Negate the bool condition in set_transition_condition (#707)"),
    graphName: z.string().optional().describe("Target graph name (default: AnimGraph). read_control_rig_graph: substring filter over model names (#774)"),
    includePins: z.boolean().optional().describe("read_control_rig_graph: include each node's pins (default true) (#774)"),
    includeDefaults: z.boolean().optional().describe("read_control_rig_graph: include pin default values (default true) (#774)"),
    includeLinks: z.boolean().optional().describe("read_control_rig_graph: include pin-to-pin links (default true) (#774)"),
    // The shared cursor and limit, declared once for every paged action in this
    // category: list, list_skeletal_meshes, list_bones, and read_control_rig_graph
    // for its own per-graph node cap (default 200) (#774). Undeclared keys are
    // stripped, so an action that documents `cursor` without this would return
    // an unpaged first page and call it a success.
    ...PAGINATION_SCHEMA,
    // Curve params (#79 / #24)
    curveName: z.string().optional().describe("Curve name for add_curve"),
    curveType: z.string().optional().describe("Curve type (default: float)"),
    // #712 - direct float-curve values + animation modifiers
    keys: z.array(z.object({ time: z.number(), value: z.number(), interp: z.string().optional() })).optional().describe("set_anim_curve_keys: [{time, value, interp?}] keyframes"),
    modifierClass: z.string().optional().describe("apply_animation_modifier: UAnimationModifier subclass (short name e.g. 'DistanceCurveModifier' or /Script path)"),
    props: z.record(z.any()).optional().describe("apply_animation_modifier: modifier EditAnywhere property values"),
    // #713 - distance-matching graph authoring
    nodeGuid: z.string().optional().describe("bind_anim_node_function: target anim graph node GUID (from add_sequence_evaluator / add_*_node)"),
    functionName: z.string().optional().describe("bind_anim_node_function: thread-safe anim-node function name to bind"),
    binding: z.string().optional().describe("bind_anim_node_function: 'update' (default), 'becomeRelevant', or 'initialUpdate'"),
    explicitTime: z.number().optional().describe("add_sequence_evaluator: initial ExplicitTime"),
    shouldLoop: z.boolean().optional().describe("add_sequence_evaluator: bShouldLoop"),
    teleportToExplicitTime: z.boolean().optional().describe("add_sequence_evaluator: bTeleportToExplicitTime (default false so time advances / root motion extracts)"),
    // Montage slot & section params (#78, #27)
    slotName: z.string().optional().describe("Slot name for set_montage_slot. add_montage_segment: target slot, created when absent. remove_montage_segment / list_montage_segments / add_montage_section: target slot (#826)"),
    trackIndex: z.number().optional().describe("Slot track index (default: 0)"),
    sectionName: z.string().optional().describe("Section name for add_montage_section"),
    startTime: z.number().optional().describe("Start time for montage section"),
    linkedSection: z.string().optional().describe("Next section name to link to"),
    // IK Rig params (#93)
    skeletalMeshPath: z.string().optional().describe("SkeletalMesh asset path. Used by IK Rig creation, curve/morph comparison, Control Rig edit setup, and animation analysis."),
    animPath: z.string().optional().describe("compare_curves_to_morph_targets: AnimSequence or PoseAsset path (#656)"),
    nodeClass: z.string().optional().describe("inspect_anim_nodes: node class substring filter, e.g. PoseDriver (#657)"),
    enableRootMotion: z.boolean().optional(),
    forceRootLock: z.boolean().optional(),
    useNormalizedRootMotionScale: z.boolean().optional(),
    rootMotionRootLock: z.string().optional().describe("RefPose|AnimFirstFrame|Zero"),
    sourceBone: z.string().optional(),
    targetBone: z.string().optional(),
    virtualBoneName: z.string().optional(),
    retargetRoot: z.string().optional().describe("Retarget root bone name for IK Rig"),
    rootMotionBone: z.string().min(1).optional().describe("configure_ik_rig: root-motion bone name"),
    // String rather than z.enum for the reason recorded on ControlRigSetEdit.space.
    autoSetup: z.string().optional().describe("configure_ik_rig: optional native rig setup pass: retarget | full_body"),
    fullBodyIK: IKRigFullBodySettings.optional().describe("configure_ik_rig: Full Body IK solver and desired goal/effector settings"),
    exclusions: z.array(IKRigExclusion).max(2_048).optional().describe("configure_ik_rig: desired per-bone solver exclusions"),
    sourceRig: z.string().optional().describe("Source IKRig path for create_ik_retargeter"),
    targetRig: z.string().optional().describe("Target IKRig path for create_ik_retargeter"),
    rigPath: z.string().optional().describe("IK Rig path for set_ik_rig_mesh / set_ik_retargeter_rig (#701/#703)"),
    meshPath: z.string().optional().describe("Skeletal mesh path for set_ik_rig_mesh (#701)"),
    retargeterPath: z.string().optional().describe("IK Retargeter path (#701/#703)"),
    sourcePreviewMesh: z.string().min(1).optional().describe("configure_ik_retargeter: source preview SkeletalMesh path"),
    targetPreviewMesh: z.string().min(1).optional().describe("configure_ik_retargeter: target preview SkeletalMesh path"),
    ensureDefaultOps: z.boolean().optional().describe("configure_ik_retargeter: ensure the complete UE 5.8 default operation stack; defaults true"),
    // String rather than z.enum for the reason recorded on ControlRigSetEdit.space.
    autoMapMode: z.string().optional().describe("configure_ik_retargeter: native chain auto-map mode: exact | fuzzy | clear"),
    forceRemap: z.boolean().optional().describe("configure_ik_retargeter: replace existing mappings during auto-map; defaults false"),
    chainMappings: z.array(IKRetargetChainMapping).max(10_000).optional().describe("configure_ik_retargeter: explicit target-to-source chain overrides; null or omitted source clears"),
    pose: IKRetargetPose.optional().describe("configure_ik_retargeter: named source or target pose authoring"),
    side: z.string().optional().describe("source|target for retargeter rig/pose actions (#701/#703)"),
    sourceMesh: z.string().optional().describe("batch_retarget_animations: source skeletal mesh (#701)"),
    targetMesh: z.string().optional().describe("batch_retarget_animations: target skeletal mesh (#701)"),
    animPaths: z.array(z.string()).optional().describe("batch_retarget_animations: AnimSequence paths to bake (#701)"),
    prefix: z.string().optional().describe("batch_retarget_animations: output name prefix (#701)"),
    suffix: z.string().optional().describe("batch_retarget_animations: output name suffix (#701)"),
    overwrite: z.boolean().optional().describe("batch_retarget_animations: overwrite existing outputs (#701)"),
    requireCompleteMapping: z.boolean().optional().describe("batch_retarget_animations: reject any unmapped target chain; default false"),
    outputPath: z.string().optional().describe("batch_retarget_animations: destination folder for baked assets (#701)"),
    autoMapChains: z.boolean().optional().describe("create_ik_retargeter: assign rigs to ops + AutoMapChains after creation (default true)"),
    onConflict: z.string().optional().describe("Conflict policy. Existing asset actions use skip|error|overwrite; Control Rig begin/bake and create_skeleton use skip|error and never overwrite."),
    frames: z.array(z.number()).optional().describe("Frames to read/sample. Used by read_bone_track, read_control_rig_edit, and analyze_animation."),
    // UE 5.8 Control Rig + data-driven animation editing workflow.
    sourceAnimationPath: z.string().optional().describe("begin_control_rig_edit: source AnimSequence asset path (required)."),
    controlRigPath: z.string().optional().describe("begin_control_rig_edit: verified baseline ControlRigBlueprint asset path for this target character; required when rigMode='asset'. Create and validate the rig first when the project/character has none."),
    // String rather than z.enum for the reason recorded on ControlRigSetEdit.space.
    rigMode: z.string().optional().describe("begin_control_rig_edit: fk | asset. Use 'asset' with the verified baseline controlRigPath; use 'fk' only when generated raw FK controls are the intended editing surface. Default fk."),
    layered: z.boolean().optional().describe("begin_control_rig_edit: keep the source animation track active under the Control Rig layer; defaults false."),
    startFrame: z.number().int().optional().describe("begin_control_rig_edit: optional inclusive edit range start frame."),
    endFrame: z.number().int().optional().describe("begin_control_rig_edit: optional exclusive edit range end frame."),
    displayRate: z.number().positive().optional().describe("begin_control_rig_edit: optional LevelSequence display rate in frames per second."),
    bindingTag: z.string().min(1).optional().describe("Stable Control Rig edit-session natural key used by begin/read/apply/bake."),
    controlNames: z.array(z.string().min(1)).min(1).optional().describe("read_control_rig_edit: optional controls to sample; omit to read every control."),
    operations: z.array(ControlRigEditOperation).min(1).optional().describe("apply_control_rig_edits: typed transform/bool/float/int edit operations, including quaternion set_keys."),
    outputAssetPath: z.string().optional().describe("bake_control_rig_edit: required destination AnimSequence asset path."),
    reduceKeys: z.literal(false).optional().describe("bake_control_rig_edit: key reduction is not supported yet; omit or pass false."),
    tolerance: z.number().nonnegative().optional().describe("bake_control_rig_edit: key-reduction tolerance."),
    createLink: z.literal(false).optional().describe("bake_control_rig_edit: Sequencer links are not supported yet; omit or pass false."),
    loop: z.boolean().optional().describe("analyze_animation: include end-to-start loop continuity metrics."),
    outputDirectory: z.string().optional().describe("analyze_animation: optional directory under Project/Saved/Codex/AnimationQA for deterministic artifacts; relative values resolve under that root."),
    chains: z.array(IKRigAuthoringChain).max(256).optional().describe("IK retarget chains for create_ik_rig or configure_ik_rig"),
    keyframes: z.array(z.object({
      frame: z.number(),
      location: Vec3.optional(),
      rotation: Quat.optional(),
      scale: Vec3.optional(),
    })).optional(),
    tracks: z.array(z.object({
      bone: z.string(),
      keyframes: z.array(z.object({
        frame: z.number().optional(),
        location: Vec3.optional(),
        rotation: Quat.optional(),
        scale: Vec3.optional(),
      })),
    })).optional().describe("Per-bone keyframe arrays for bake_keyframes_batch (#540)"),
    save: z.boolean().optional().describe("bake_keyframes_batch: save the asset after baking (default true)"),
    // PoseSearch (v0.7.15)
    schemaPath: z.string().optional().describe("Path to a UPoseSearchSchema asset"),
    sequencePath: z.string().optional().describe("Animation path for PoseSearch graph actions, or LevelSequence path for the UE 5.8 Control Rig edit workflow."),
    wait: z.boolean().optional().describe("build_pose_search_index: block until the async build resolves (default true)"),
    // #684 per-clip flags + bulk clip authoring
    mirror: z.string().optional().describe("PoseSearch clip mirror option: 'original' | 'mirrored' | 'both'"),
    disableReselection: z.boolean().optional().describe("PoseSearch clip: disallow reselecting poses from the same asset"),
    sampleStart: z.number().optional().describe("PoseSearch clip sampling range start (seconds); [0,0] = whole clip"),
    sampleEnd: z.number().optional().describe("PoseSearch clip sampling range end (seconds); [0,0] = whole clip"),
    clips: z.array(z.union([PoseSearchClipEntry, z.string()])).optional().describe("set_pose_search_clips: array of clip entries ({sequencePath, mirror?, disableReselection?, sampleStart?, sampleEnd?, enabled?}) or bare path strings"),
    // Motion Matching content pipeline (schema / mirror / normalization / tuning)
    sampleRate: z.number().optional().describe("Sample rate. create_pose_search_schema: schema rate (default 30); analyze_animation: optional analysis sampling rate."),
    addDefaultChannels: z.boolean().optional().describe("create_pose_search_schema: add Trajectory+Pose default channels (default true)"),
    mirrorDataTablePath: z.string().optional().describe("create_pose_search_schema: optional MirrorDataTable to bind"),
    bones: z.array(z.union([PoseSearchBoneEntry, z.string()])).optional().describe("add_pose_search_schema_pose_channel: [{bone, flags?, weight?}] or bone-name strings; also add_pose_search_schema_trajectory_channel reuses 'samples'"),
    weight: z.number().optional().describe("Channel weight for pose/trajectory channels"),
    expressions: z.array(MirrorFindReplaceExpression).optional().describe("create_mirror_data_table: [{find, replace, method?}] find/replace bone-name rules"),
    mirrorAxis: z.string().optional().describe("create_mirror_data_table: X, Y or Z (default X)"),
    mirrorRootMotion: z.boolean().optional().describe("create_mirror_data_table: mirror root motion (default true)"),
    databases: z.array(z.string()).optional().describe("create_pose_search_normalization_set: PoseSearchDatabase paths to normalize together"),
    continuingPoseCostBias: z.number().optional().describe("set_pose_search_database_settings: bias to keep playing the current clip"),
    baseCostBias: z.number().optional().describe("set_pose_search_database_settings: flat cost added to every pose"),
    loopingCostBias: z.number().optional().describe("set_pose_search_database_settings: bias for looping clips"),
    kdTreeQueryNumNeighbors: z.number().optional().describe("set_pose_search_database_settings: KD-tree neighbours to consider"),
    numberOfPrincipalComponents: z.number().optional().describe("set_pose_search_database_settings: PCA components for PCAKDTree mode"),
    poseSearchMode: z.string().optional().describe("set_pose_search_database_settings: bruteforce | pcakdtree | vptree | eventonly"),
    normalizationSetPath: z.string().optional().describe("set_pose_search_database_settings: PoseSearchNormalizationSet to assign"),
    databasePath: z.string().optional().describe("add_motion_matching_node: PoseSearchDatabase the node searches"),
    connectToOutput: z.boolean().optional().describe("add_motion_matching_node: wire the node to the Output Pose (default true)"),
    blendTime: z.number().optional().describe("add_motion_matching_node: inertial blend time"),
    poseCount: z.number().optional().describe("add_pose_history_node: number of history poses to retain"),
    samplingInterval: z.number().optional().describe("add_pose_history_node: seconds between history samples"),
    generateTrajectory: z.boolean().optional().describe("add_pose_history_node: self-generate trajectory (default true)"),
    trajectoryHistoryCount: z.number().optional().describe("add_pose_history_node: generated trajectory history samples"),
    trajectoryPredictionCount: z.number().optional().describe("add_pose_history_node: generated trajectory prediction samples"),
    insertBeforeOutput: z.boolean().optional().describe("add_pose_history_node: splice into the pose chain feeding Output Pose (default true)"),
    chooserPath: z.string().optional().describe("set_motion_matching_chooser: ChooserTable that selects the database"),
    contextSource: z.string().optional().describe("set_motion_matching_chooser: chooser context object - 'self' (anim instance, default) or 'pawn' (owning pawn)"),
    // #153 / #154
    assetPaths: z.array(z.string()).optional().describe("Asset paths (batch) for set_sequence_properties"),
    properties: z.record(z.any()).optional().describe("Property dict for set_sequence_properties"),
    resolveFromMontages: z.boolean().optional().describe("Resolve AnimMontage inputs to first anim reference (default true)"),
    rootBone: z.string().optional().describe("Root bone name for bake_root_motion_from_bone (default 'root')"),
    axes: z.array(z.string()).optional().describe("Axes to bake ('x','y','z') for bake_root_motion_from_bone"),
    interpolation: z.string().optional().describe("bake_root_motion_from_bone: 'linear' (default) or 'per_frame'"),
    space: z.string().optional().describe("Transform space. Control Rig edit actions: 'local'|'component'|'global', where 'global' is an alias for 'component'. get_bone_transforms: 'local'|'component'. get_bone_transform: 'world'|'component'|'local'."),
    world: z.string().optional().describe("World scope for live actor skeletal queries: auto (default, prefer PIE), pie/game, or editor"),
    animation: z.string().optional().describe("AnimSequence path for add_blend_sample / set_blend_sample"),
    sampleIndex: z.number().optional().describe("BlendSpace sample index for set_blend_sample"),
    position: z.object({ x: z.number().optional(), y: z.number().optional() }).optional().describe("BlendSpace sample position {x,y}"),
    x: z.number().optional(),
    y: z.number().optional(),
    // Live-actor skeletal operations (moved from level)
    actorLabel: z.string().optional().describe("Actor label for live skeletal queries"),
    actorPath: z.string().optional().describe("Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given. Editor labels are NOT unique, and a label matching several actors is refused with the candidates rather than resolved at random (#983)"),
    componentName: z.string().optional().describe("SkeletalMeshComponent name (default: CharacterMesh0 / Mesh)"),
    bodyComponent: z.string().optional().describe("rebind_leader_pose: explicit body component name"),
    enabled: z.boolean().optional().describe("preview_animation: toggle on/off"),
    // #922/#923/#926 - evaluated pose reads
    times: z.array(z.number()).optional().describe("sample_pose / measure_natural_speed: sample times in seconds; pass either this or 'frames'"),
    incorporateRootMotion: z.boolean().optional().describe("sample_pose: fold the clip's root motion into the returned pose (default true, matching the engine). measure_natural_speed always excludes it, because it measures travel relative to the planted foot"),
    blendPosition: z.object({ x: z.number().optional(), y: z.number().optional(), z: z.number().optional() }).optional().describe("sample_pose / measure_natural_speed: BlendSpace blend input, in the blendspace's own axis units"),
    footBones: z.array(z.string()).optional().describe("measure_natural_speed: the foot bones to track, e.g. ['foot_l','foot_r']"),
    contactThreshold: z.number().optional().describe("measure_natural_speed: height in cm below which a foot counts as planted; omit to derive it from the clip's own lowest foot height, which is what makes the measurement scale-independent across skeletons"),
  },
);
