#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FAnimationHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// Existing read-only queries
	static TSharedPtr<FJsonValue> ListAnimAssets(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListSkeletalMeshes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetSkeletonInfo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListSockets(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetPhysicsAssetInfo(const TSharedPtr<FJsonObject>& Params);

	// Read handlers for animation asset types
	static TSharedPtr<FJsonValue> ReadAnimBlueprint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadAnimMontage(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadAnimSequence(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ScanAnimationTracks(const TSharedPtr<FJsonObject>& Params);

	// Read handlers for blendspace
	static TSharedPtr<FJsonValue> ReadBlendspace(const TSharedPtr<FJsonObject>& Params);

	// Create handlers for animation asset types
	static TSharedPtr<FJsonValue> CreateAnimBlueprint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateMontage(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateBlendspace(const TSharedPtr<FJsonObject>& Params);
	// #248: add a sample to a BlendSpace.
	static TSharedPtr<FJsonValue> AddBlendSample(const TSharedPtr<FJsonObject>& Params);
	// #272: move an existing sample to new coordinates / swap its animation.
	static TSharedPtr<FJsonValue> SetBlendSample(const TSharedPtr<FJsonObject>& Params);
	// #459: one-call axis-params + samples authoring for BlendSpace1D/2D.
	static TSharedPtr<FJsonValue> PopulateBlendspace(const TSharedPtr<FJsonObject>& Params);
	// #459 partner: explicit BlendSpace1D creation (defaults grid for 1D).
	static TSharedPtr<FJsonValue> CreateBlendspace1D(const TSharedPtr<FJsonObject>& Params);

	// Notify handlers
	static TSharedPtr<FJsonValue> AddAnimNotify(const TSharedPtr<FJsonObject>& Params);
	// #471: per-name removal so migration scripts can prune obsolete notifies
	// without scanning through Python's AnimationLibrary.
	static TSharedPtr<FJsonValue> RemoveAnimNotify(const TSharedPtr<FJsonObject>& Params);

	// Animation sequence authoring
	static TSharedPtr<FJsonValue> CreateSequence(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetBoneKeyframes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> BakeKeyframesBatch(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetBoneTransforms(const TSharedPtr<FJsonObject>& Params);
	// #656: compare an animation/pose asset's curve names against a skeletal
	// mesh's morph target names and report matches/mismatches.
	static TSharedPtr<FJsonValue> CompareCurvesToMorphTargets(const TSharedPtr<FJsonObject>& Params);

	// Montage editing
	static TSharedPtr<FJsonValue> SetMontageSequence(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetMontageProperties(const TSharedPtr<FJsonObject>& Params);

	// State machine authoring
	static TSharedPtr<FJsonValue> CreateStateMachine(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddState(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddTransition(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetStateAnimation(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetTransitionBlend(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadStateMachine(const TSharedPtr<FJsonObject>& Params);

	// AnimGraph inspection (#23 / #91)
	static TSharedPtr<FJsonValue> ReadAnimGraph(const TSharedPtr<FJsonObject>& Params);
	// #657: deep-dump the FAnimNode_* struct of anim graph nodes (PoseDriver
	// PoseTargets/PoseAsset/RBF params, etc.) that read_anim_graph omits.
	static TSharedPtr<FJsonValue> InspectAnimNodes(const TSharedPtr<FJsonObject>& Params);

	// Float curve authoring (#79 / #24)
	static TSharedPtr<FJsonValue> AddCurve(const TSharedPtr<FJsonObject>& Params);

	// Montage slot & section editing (#78, #27)
	static TSharedPtr<FJsonValue> SetMontageSlot(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddMontageSection(const TSharedPtr<FJsonObject>& Params);

	// IK Rig (#93)
	static TSharedPtr<FJsonValue> CreateIKRig(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadIKRig(const TSharedPtr<FJsonObject>& Params);

	// Control Rig (#11)
	static TSharedPtr<FJsonValue> ListControlRigVariables(const TSharedPtr<FJsonObject>& Params);
	// #619 per-element Control Rig hierarchy metadata (name, type, index, parent)
	static TSharedPtr<FJsonValue> ReadControlRigHierarchy(const TSharedPtr<FJsonObject>& Params);

	// v0.7.11 — depth
	static TSharedPtr<FJsonValue> SetRootMotionSettings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddVirtualBone(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveVirtualBone(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateAnimComposite(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListAnimModifiers(const TSharedPtr<FJsonObject>& Params);

	// v0.7.11 — issue fixes
	static TSharedPtr<FJsonValue> CreateIKRetargeter(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadIKRetargeter(const TSharedPtr<FJsonObject>& Params);
	// #701/#703: IK rig/retargeter authoring tail + batch retarget bake.
	static TSharedPtr<FJsonValue> SetIKRigMesh(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetIKRetargeterRig(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AutoAlignRetargetPose(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ResetRetargetPose(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> BatchRetargetAnimations(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetAnimBlueprintSkeleton(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadBoneTrack(const TSharedPtr<FJsonObject>& Params);

	// v1.0.0-rc.2 — animation authoring gaps (#153, #154)
	static TSharedPtr<FJsonValue> SetSequenceProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> BakeRootMotionFromBone(const TSharedPtr<FJsonObject>& Params);

	// v0.7.15 — PoseSearch (motion matching)
	static TSharedPtr<FJsonValue> CreatePoseSearchDatabase(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetPoseSearchSchema(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddPoseSearchSequence(const TSharedPtr<FJsonObject>& Params);
	// #684: bulk clip-list authoring with per-entry flags (mirror/reselection/sampling).
	static TSharedPtr<FJsonValue> SetPoseSearchClips(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> BuildPoseSearchIndex(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadPoseSearchDatabase(const TSharedPtr<FJsonObject>& Params);

	// Motion Matching content pipeline: schema, mirror table, normalization set,
	// database tuning (AnimationHandlers_MotionMatching.cpp).
	static TSharedPtr<FJsonValue> CreatePoseSearchSchema(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddPoseSearchSchemaPoseChannel(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddPoseSearchSchemaTrajectoryChannel(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadPoseSearchSchema(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateMirrorDataTable(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadMirrorDataTable(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreatePoseSearchNormalizationSet(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetPoseSearchDatabaseSettings(const TSharedPtr<FJsonObject>& Params);
	// Motion Matching runtime AnimGraph nodes.
	static TSharedPtr<FJsonValue> AddMotionMatchingNode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddPoseHistoryNode(const TSharedPtr<FJsonObject>& Params);
	// Drive the MM node's Database from a ChooserTable (runtime database selection).
	static TSharedPtr<FJsonValue> SetMotionMatchingChooser(const TSharedPtr<FJsonObject>& Params);

	// #419/#420 — live-actor skeletal reads + rebind + preview (moved from Level)
	static TSharedPtr<FJsonValue> GetBoneTransform(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListBones(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RebindLeaderPose(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PreviewAnimation(const TSharedPtr<FJsonObject>& Params);
};
