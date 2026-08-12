import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";
import { Vec3, Quat } from "../schemas.js";

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
  space: z.enum(["local", "global"]).optional(),
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
  space: z.enum(["local", "global"]).optional(),
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
  space: z.enum(["local", "global"]).optional(),
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
  side: z.enum(["source", "target"]),
  name: z.string().min(1),
  create: z.boolean().optional(),
  reset: z.boolean().optional(),
  autoAlign: z.enum(["chain_to_chain", "mesh_to_mesh", "local_axes", "global_axes"]).optional(),
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
    list:                 bp("List anim assets. Params: directory?, recursive?", "list_anim_assets"),
    create_montage:       bp("Create montage. Params: animSequencePath, name?, packagePath?", "create_anim_montage"),
    author_montages_batch: bp("Batch-author montages in one call: idempotent create, slot name, blend/rate/length properties, sections and notifies, then save. Every item reports success plus the failing stage (validate|create|slot|properties|sections|notifies|save) and error, so one bad item does not hide the rest. Newly created montages come back as a delete_asset_batch rollback. Each montage still holds the single segment create_montage builds. Params: items[] (each: name, animSequencePath, packagePath?, onConflict?, slotName?, trackIndex?, rateScale?, blendIn?, blendOut?, sequenceLength?, sections? [{sectionName, startTime?, linkedSection?}], notifies? [{notifyName, triggerTime, notifyClass?, properties?}])", "author_montages_batch", (p) => ({ items: p.items })),
    create_anim_blueprint: bp("Create AnimBP. Params: skeletonPath, name?, packagePath?, parentClass?", "create_anim_blueprint"),
    create_blendspace:    bp("Create blendspace (2D). Params: skeletonPath, name?, packagePath?, axisHorizontal?, axisVertical?", "create_blendspace"),
    create_blendspace_1d: bp("Create BlendSpace1D. Params: skeletonPath, name?, packagePath?, axisName? (default Speed), axisMin?, axisMax?, gridNum? (#459)", "create_blendspace_1d", (p) => ({ name: p.name, skeletonPath: p.skeletonPath, packagePath: p.packagePath, axisName: p.axisName, axisMin: p.axisMin, axisMax: p.axisMax, gridNum: p.gridNum, onConflict: p.onConflict })),
    populate_blendspace:  bp("One-call axis params + samples authoring for BlendSpace 1D/2D. Params: assetPath, axis? ({name?, min?, max?, gridNum?}) for axis 0, blendspaceAxes? (per-axis array), axisHorizontal?/axisVertical? + horizontalMin/horizontalMax/verticalMin/verticalMax/gridNumHorizontal/gridNumVertical (back-compat), samples ([{animationPath, x, y?}]), clearExisting? (default true) (#459)", "populate_blendspace", (p) => ({ assetPath: p.assetPath, axis: p.axis, axes: p.blendspaceAxes, axisIndex: p.axisIndex, axisHorizontal: p.axisHorizontal, axisVertical: p.axisVertical, horizontalMin: p.horizontalMin, horizontalMax: p.horizontalMax, verticalMin: p.verticalMin, verticalMax: p.verticalMax, gridNumHorizontal: p.gridNumHorizontal, gridNumVertical: p.gridNumVertical, samples: p.samples, clearExisting: p.clearExisting })),
    add_notify:           bp("Add notify. For PlayMontageNotify the notifyName is also written onto the spawned notify object so OnPlayMontageNotifyBegin broadcasts it (not 'None'), and montage branching-point markers refresh (#528). notifyProperties writes EditAnywhere fields onto the spawned notify object and therefore requires a notifyClass that resolves. Params: assetPath, notifyName, triggerTime, notifyClass?, notifyProperties?", "add_anim_notify"),
    remove_notify:        bp("Remove notify(s) by name and/or class. Pass at least one of notifyName/notifyClass; both filters AND. Idempotent: alreadyDeleted=true if no match. Params: assetPath, notifyName?, notifyClass? (#471)", "remove_anim_notify", (p) => ({ assetPath: p.assetPath, notifyName: p.notifyName, notifyClass: p.notifyClass })),
    get_skeleton_info:    bp("Read skeleton. Params: assetPath", "get_skeleton_info"),
    list_sockets:         bp("List sockets. Params: assetPath", "list_animation_sockets"),
    list_skeletal_meshes: bp("List skeletal meshes. Params: directory?, recursive?", "list_skeletal_meshes"),
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
    read_anim_graph:      bp("Read AnimBP AnimGraph nodes with properties & pins. Params: assetPath, graphName?", "read_anim_graph"),
    add_curve:            bp("Add float curve to AnimSequence. Params: assetPath, curveName, curveType?", "add_curve"),
    set_anim_curve_keys:  bp("Set float-curve key VALUES on an AnimSequence (add_curve only creates an empty named curve - it cannot set keyframe values). Adds the curve if missing, then replaces its keys. Use for authoring Distance/Speed/any float curve directly. Params: assetPath, curveName, keys ([{time, value, interp?('linear'|'constant'|'cubic')}]), interpolation? (default 'linear', applied to keys without their own interp) (#712)", "set_anim_curve_keys", (p) => ({ assetPath: p.assetPath, curveName: p.curveName, keys: p.keys, interpolation: p.interpolation })),
    apply_animation_modifier: bp("Instantiate a UAnimationModifier subclass and run it on an AnimSequence. Headline use: modifierClass='DistanceCurveModifier' bakes a Distance curve from the clip's root motion for distance matching (needs root motion baked first - see bake_root_motion_from_bone). Registers the modifier on the sequence so it re-applies on reimport. props sets the modifier's EditAnywhere fields (e.g. DistanceCurveModifier: {CurveName, Axis:'XY'|'X'|..., bStopAtEnd, StopSpeedThreshold, SampleRate}). Note: DistanceCurveModifier ships in the 'Animation Locomotion Library' plugin (off by default) - enable it first. Params: assetPath, modifierClass (short name or /Script path), props? (#712)", "apply_animation_modifier", (p) => ({ assetPath: p.assetPath, modifierClass: p.modifierClass, props: p.props })),
    set_montage_slot:     bp("Set slot name on a montage track. Params: assetPath, slotName, trackIndex?", "set_montage_slot"),
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
    apply_control_rig_edits: bp("UE 5.8 only. Apply typed Control Rig edits in one transaction; native returns unsupported_engine_version on older engines. There is no silent fallback to raw bone tracks. set_keys writes strictly ordered full per-frame transforms from normalized quaternions and preserves shortest-arc quaternion continuity. A set operation writes one full absolute transform at frame or frames. An offset operation applies translation/rotation/scale deltas across an inclusive frame range with optional edge blends. contact_lock densely constrains a translatable driver control, or an optional driven bone/socket reference, to a fixed component-space target with smooth edge blends and optional pole/control stabilization. Driver and stabilizer keys are read back transactionally. A drivenReference contact returns verification='bake_and_analyze_required'; bake it and analyze every constrained frame before accepting the bone/socket result. set_bool, set_float, and set_int key matching scalar controls; enum controls use set_int with one of the integer values reported in enumOptions. Params: sequencePath, bindingTag, operations[] where set_keys={op:'set_keys',control,keys:[{frame,transform:{translation,rotationQuaternion,scale}}],space?}, set={op:'set',control,frame|frames,transform:{translation,rotationDegrees,scale},space?}, offset={op:'offset',control,startFrame,endFrame,translationCm?,rotationDegrees?,scaleMultiplier?,space?,blendInFrames?,blendOutFrames?}, contact_lock={op:'contact_lock',control,drivenReference?,startFrame,endFrame,target:{translation,rotationQuaternion?},blendInFrames?,blendOutFrames?,stabilizeControls?,positionToleranceCm?,rotationToleranceDegrees?}, set_bool={op:'set_bool',control,frame|frames,value}, set_float={op:'set_float',control,frame|frames,value}, or set_int={op:'set_int',control,frame|frames,value}. Sequencer's current interpolation mode is retained. Returns per-operation counts, affected controls/frames, and contactQa summaries; a failed key/readback batch is undone.", "apply_control_rig_edits", (p) => ({ sequencePath: p.sequencePath, bindingTag: p.bindingTag, operations: p.operations })),
    bake_control_rig_edit: bp("UE 5.8 only. Bake the evaluated Control Rig session to a new AnimSequence asset; native returns unsupported_engine_version on older engines and has no raw-track fallback. The source LevelSequence remains unchanged. outputAssetPath is the output natural key; onConflict is skip|error (default error), never overwrite. Key reduction and Sequencer links are not supported yet, so reduceKeys/createLink must be false or omitted. Params: sequencePath, bindingTag, outputAssetPath, frameRate?, reduceKeys?, tolerance?, createLink?, onConflict?. Returns output asset metadata, frame/rate counts, status, and delete-created-asset rollback.", "bake_control_rig_edit", (p) => ({ sequencePath: p.sequencePath, bindingTag: p.bindingTag, outputAssetPath: p.outputAssetPath, frameRate: p.frameRate, reduceKeys: p.reduceKeys, tolerance: p.tolerance, createLink: p.createLink, onConflict: p.onConflict })),
    analyze_animation: bp("Cross-version, data-driven AnimSequence inspection using the native animation APIs available in the compiled engine. Samples an AnimSequence and reports deterministic numeric motion diagnostics without Python or viewport inference. Params: assetPath (required AnimSequence), skeletalMeshPath?, boneNames?, frames?, sampleRate?, loop?, outputDirectory? (must resolve under Project/Saved/Codex/AnimationQA and must not already contain artifacts). Returns source/rate/range metadata, sampled local/component transforms, root-motion and continuity metrics, and any written analysis artifacts.", "analyze_animation", (p) => ({ assetPath: p.assetPath, skeletalMeshPath: p.skeletalMeshPath, boneNames: p.boneNames, frames: p.frames, sampleRate: p.sampleRate, loop: p.loop, outputDirectory: p.outputDirectory })),
    set_root_motion:    bp("Set root motion settings on AnimSequence. Params: assetPath, enableRootMotion?, forceRootLock?, useNormalizedRootMotionScale?, rootMotionRootLock?", "set_root_motion_settings", (p) => ({ path: p.assetPath, enableRootMotion: p.enableRootMotion, forceRootLock: p.forceRootLock, useNormalizedRootMotionScale: p.useNormalizedRootMotionScale, rootMotionRootLock: p.rootMotionRootLock })),
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
    create_pose_search_database: bp("Create a PoseSearchDatabase asset (motion matching). Params: name, packagePath?, schemaPath?", "create_pose_search_database"),
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
    get_bone_transform: bp("Read a bone or socket transform on a live actor's SkeletalMeshComponent. Wraps GetBoneTransform / GetSocketTransform. Params: actorLabel, boneName (or socket name), componentName? (default: CharacterMesh0 / Mesh / first SK component), world? (auto|pie|game|editor, default auto), space? (world|component|local, default world). Returns location, rotation, scale (#420)", "get_bone_transform", (p) => ({ actorLabel: p.actorLabel, boneName: p.boneName, componentName: p.componentName, world: p.world, space: p.space })),
    list_bones: bp("List bones in a live actor's SkeletalMeshComponent ref skeleton (name, index, parent). Params: actorLabel, componentName?, world? (auto|pie|game|editor, default auto) (#420)", "list_bones", (p) => ({ actorLabel: p.actorLabel, componentName: p.componentName, world: p.world })),
    rebind_leader_pose: bp("Re-bind every secondary SkeletalMeshComponent on an actor to a body component (default CharacterMesh0 / Mesh). One-call fix for the 'character explodes after rotating the actor' failure mode. Params: actorLabel, bodyComponent? (#419)", "rebind_leader_pose", (p) => ({ actorLabel: p.actorLabel, bodyComponent: p.bodyComponent })),
    preview_animation: bp("Toggle bUpdateAnimationInEditor + VisibilityBasedAnimTickOption=AlwaysTickPoseAndRefreshBones on every SkeletalMeshComponent of an actor. Bypasses the 'cannot be edited on templates' guard for level instances. Params: actorLabel, enabled (#419/#420)", "preview_animation", (p) => ({ actorLabel: p.actorLabel, enabled: p.enabled })),
  },
  undefined,
  {
    assetPath: z.string().optional(),
    directory: z.string().optional(),
    recursive: z.boolean().optional(),
    targetTrackCount: z.number().optional().describe("Flag sequences with more than this many bone tracks"),
    includeTrackNames: z.boolean().optional().describe("Include full bone track name arrays in scan_animation_tracks results"),
    animSequencePath: z.string().optional(),
    items: z.array(z.object({
      name: z.string(),
      animSequencePath: z.string(),
      packagePath: z.string().optional(),
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
    skeletonPath: z.string().optional(),
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
    notifyProperties: z.record(z.unknown()).optional().describe("add_notify: EditAnywhere fields to set on the spawned notify object (requires notifyClass)"),
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
    limit: z.number().optional().describe("read_control_rig_graph: max nodes reported per graph (default 200) (#774)"),
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
    autoSetup: z.enum(["retarget", "full_body"]).optional().describe("configure_ik_rig: optional native rig setup pass"),
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
    autoMapMode: z.enum(["exact", "fuzzy", "clear"]).optional().describe("configure_ik_retargeter: native chain auto-map mode"),
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
    onConflict: z.string().optional().describe("Conflict policy. Existing asset actions use skip|error|overwrite; Control Rig begin/bake use skip|error and never overwrite."),
    frames: z.array(z.number()).optional().describe("Frames to read/sample. Used by read_bone_track, read_control_rig_edit, and analyze_animation."),
    // UE 5.8 Control Rig + data-driven animation editing workflow.
    sourceAnimationPath: z.string().optional().describe("begin_control_rig_edit: source AnimSequence asset path (required)."),
    controlRigPath: z.string().optional().describe("begin_control_rig_edit: verified baseline ControlRigBlueprint asset path for this target character; required when rigMode='asset'. Create and validate the rig first when the project/character has none."),
    rigMode: z.enum(["fk", "asset"]).optional().describe("begin_control_rig_edit: use 'asset' with the verified baseline controlRigPath; use 'fk' only when generated raw FK controls are the intended editing surface."),
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
    clips: z.array(z.any()).optional().describe("set_pose_search_clips: array of clip entries ({sequencePath, mirror?, disableReselection?, sampleStart?, sampleEnd?, enabled?}) or bare path strings"),
    // Motion Matching content pipeline (schema / mirror / normalization / tuning)
    sampleRate: z.number().optional().describe("Sample rate. create_pose_search_schema: schema rate (default 30); analyze_animation: optional analysis sampling rate."),
    addDefaultChannels: z.boolean().optional().describe("create_pose_search_schema: add Trajectory+Pose default channels (default true)"),
    mirrorDataTablePath: z.string().optional().describe("create_pose_search_schema: optional MirrorDataTable to bind"),
    bones: z.array(z.any()).optional().describe("add_pose_search_schema_pose_channel: [{bone, flags?, weight?}] or bone-name strings; also add_pose_search_schema_trajectory_channel reuses 'samples'"),
    weight: z.number().optional().describe("Channel weight for pose/trajectory channels"),
    expressions: z.array(z.any()).optional().describe("create_mirror_data_table: [{find, replace, method?}] find/replace bone-name rules"),
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
    space: z.string().optional().describe("Transform space. Control Rig edit actions: 'local'|'global'. get_bone_transforms: 'local'|'component'. get_bone_transform: 'world'|'component'|'local'."),
    world: z.string().optional().describe("World scope for live actor skeletal queries: auto (default, prefer PIE), pie/game, or editor"),
    animation: z.string().optional().describe("AnimSequence path for add_blend_sample / set_blend_sample"),
    sampleIndex: z.number().optional().describe("BlendSpace sample index for set_blend_sample"),
    position: z.object({ x: z.number().optional(), y: z.number().optional() }).optional().describe("BlendSpace sample position {x,y}"),
    x: z.number().optional(),
    y: z.number().optional(),
    // Live-actor skeletal operations (moved from level)
    actorLabel: z.string().optional().describe("Actor label for live skeletal queries"),
    componentName: z.string().optional().describe("SkeletalMeshComponent name (default: CharacterMesh0 / Mesh)"),
    bodyComponent: z.string().optional().describe("rebind_leader_pose: explicit body component name"),
    enabled: z.boolean().optional().describe("preview_animation: toggle on/off"),
  },
);
