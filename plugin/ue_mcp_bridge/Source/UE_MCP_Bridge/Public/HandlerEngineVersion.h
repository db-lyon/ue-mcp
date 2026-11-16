#pragma once

// The one owner of engine-version gating: every version gate and every feature
// macro the plugin compiles against is defined here, and nowhere else.
//
// Supported engines: UE <!-- count:engineRange -->5.4 to 5.9<!-- /count -->
// (stamped by `npm run generate:metadata` from scripts/lib/engine-range.mjs).
// The oldest of them is the floor, so there is no gate for it: code that needs
// the floor needs nothing.
//
// Rules, held by `npm run audit:engine-gates` and its unit test:
// - ENGINE_MAJOR_VERSION / ENGINE_MINOR_VERSION and UE_VERSION_OLDER_THAN are
//   read in this file only. Elsewhere a gate names one of the macros below.
// - A feature macro is #defined here, never in a .cpp: the module is a unity
//   build, so a #define in one .cpp leaks into the files after it in the same
//   blob, and a second definition there is a redefinition.
// - A gate tests "at least this engine", so a later major version keeps the
//   newer path.
// - An action compiled out on an engine answers MCPUnsupportedEngineError,
//   naming the engines that have it and what to use instead.
//
// API shims for renamed engine calls are at the end; headers that moved live
// in MCPEngineCompat.h.

#include "CoreMinimal.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"

/** True on Major.Minor and every later engine, a later major version included. */
#define UE_MCP_ENGINE_AT_LEAST(Major, Minor) \
	((ENGINE_MAJOR_VERSION > (Major)) || (ENGINE_MAJOR_VERSION == (Major) && ENGINE_MINOR_VERSION >= (Minor)))

// ── Version gates, one per minor version above the floor ─────────────────────

// StateTreeEditingSubsystem, FExpressionInputIterator,
// AActor::Get/SetNetUpdateFrequency, UWidgetBlueprint::WidgetVariableNameToGuidMap,
// UPCGEditorGraphNodeBase, UIKRetargeterController::AssignIKRigToAllOps, etc.
#define UE_MCP_HAS_5_5_API UE_MCP_ENGINE_AT_LEAST(5, 5)

#define UE_MCP_HAS_5_6_API UE_MCP_ENGINE_AT_LEAST(5, 6)

// EFindObjectFlags (the bool bExactClass overloads are deprecated there) and
// UPoseSearchDatabase's non-templated GetDatabaseAnimationAsset.
#define UE_MCP_HAS_5_7_API UE_MCP_ENGINE_AT_LEAST(5, 7)

// EGetObjectsFlags and FStringTable::ImportStringsFromCSVFile; the bool /
// ImportStrings forms they replace are deprecated in 5.8 and warn on every user
// build, but do not exist before it. Also the one-argument
// UMaterial::SetMaterialUsage (5.7 has only the bNeedsRecompile form) and
// FCoreDelegates::ApplicationHeartbeat. The status module cannot depend on this
// module, so it carries a copy of this one definition, held identical to this
// line by tests/unit/engine-gates.test.ts.
#define UE_MCP_HAS_5_8_API UE_MCP_ENGINE_AT_LEAST(5, 8)

// The Unreal MCP plugin's out-of-line FModelContextProtocolToolRequestId
// constructors (inline before 5.9).
#define UE_MCP_HAS_5_9_API UE_MCP_ENGINE_AT_LEAST(5, 9)

/** "5.4" for this engine, for messages that name the running engine. */
#define UE_MCP_ENGINE_VERSION_STRING FString::Printf(TEXT("%d.%d"), ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION)

// ── Feature macros ───────────────────────────────────────────────────────────

// StructUtils moved into CoreUObject, and PerPlatformProperties out of Engine (5.5).
#define UE_MCP_HAS_CORE_STRUCT_UTILS UE_MCP_HAS_5_5_API

// StateTree.
#define UE_MCP_HAS_STATETREE_STATE_DESCRIPTION UE_MCP_HAS_5_6_API
#define UE_MCP_HAS_STATETREE_STATE_CUSTOM_TICK_RATE UE_MCP_HAS_5_6_API
#define UE_MCP_HAS_STATETREE_COMPILER_TOKENIZED_MESSAGES UE_MCP_HAS_5_6_API
#define UE_MCP_HAS_STATETREE_EXECUTION_RUNTIME_DATA UE_MCP_HAS_5_7_API
#define UE_MCP_HAS_STATETREE_GENERAL_PROPERTY_BINDING UE_MCP_HAS_5_7_API
// FStateTreeEditorNode::InitializeAs(Outer, Struct) and ReallocInstanceData;
// on 5.7 and older the same two cases are handled by hand.
#define UE_MCP_HAS_STATETREE_NODE_OUTER_INIT UE_MCP_HAS_5_8_API
// UStateTree::GetStateHandleFromGameplayTag; before it, request_transition
// takes targetStateId and says why.
#define UE_MCP_HAS_STATETREE_TAG_STATE_LOOKUP UE_MCP_HAS_5_8_API

// PCG.
// StaticFindObject's EFindObjectFlags overload.
#define UE_MCP_HAS_STATIC_FIND_OBJECT_FLAGS UE_MCP_HAS_5_7_API
// UPCGSubsystem::ForAllRegisteredLocalComponents is exported on every
// supported engine; set this to 0 for one where it is not, and
// get_pcg_component_details reports localComponentsKnown:false.
#define UE_MCP_HAS_PCG_LOCAL_COMPONENT_QUERY 1
// PCG Assemblies: UPCGLevelToAsset and UPCGDataAsset.
#define UE_MCP_HAS_PCG_LEVEL_TO_ASSET UE_MCP_HAS_5_5_API
// UPCGBasePointData replaced UPCGPointData as the point container.
#define UE_MCP_HAS_PCG_BASE_POINT_DATA UE_MCP_HAS_5_6_API
// UPCGComponent::ForEachConstManagedResource and AreManagedResourcesAccessible
// are public; older engines fall back to the owner's PCG-tagged components.
#define UE_MCP_HAS_PCG_MANAGED_RESOURCE_ITERATION UE_MCP_HAS_5_7_API
// Per-node execution inspection (PCGGraphExecutionInspection.h).
#define UE_MCP_HAS_PCG_NODE_INSPECTION UE_MCP_HAS_5_8_API

// World Partition: FWorldPartitionActorDescInstance and the ActorDescInstance
// traversal helpers, and the editor loaded-region adapters that need them.
#define UE_MCP_HAS_ACTOR_DESC_INSTANCE_API UE_MCP_HAS_5_5_API
#define UE_MCP_HAS_LOADED_REGION_API UE_MCP_HAS_ACTOR_DESC_INSTANCE_API

// Animation: UPoseSearchDatabase's GetNumAnimationAssets /
// GetDatabaseAnimationAsset pair. 5.4 exposes the clip list as the
// AnimationAssets array of FInstancedStruct with GetAnimationAssetBase.
#define UE_MCP_HAS_POSESEARCH_DATABASE_ASSET_API UE_MCP_HAS_5_5_API

// Profiling: timing regions reach a trace from 5.7. Below that the bracket
// still measures wall clock; it just does not appear in a trace.
#define UE_MCP_HAS_TRACE_REGIONS (UE_MCP_HAS_5_7_API && MISCTRACE_ENABLED)

// Checkpoint diff: SDetailsDiff, the details-panel diff widget.
#define UE_MCP_HAS_DETAILS_DIFF UE_MCP_HAS_5_5_API

// Machine learning: the NNE runtime interfaces, when Build.cs found the NNE
// module (WITH_UEMCP_NNE), and which optional runtime headers it ships.
#if defined(WITH_UEMCP_NNE) && WITH_UEMCP_NNE && UE_MCP_HAS_5_5_API
#define UE_MCP_NNE_AVAILABLE 1
#else
#define UE_MCP_NNE_AVAILABLE 0
#endif
#if UE_MCP_NNE_AVAILABLE && __has_include("NNERuntimeGPU.h")
#define UE_MCP_NNE_HAS_GPU 1
#else
#define UE_MCP_NNE_HAS_GPU 0
#endif
#if UE_MCP_NNE_AVAILABLE && __has_include("NNERuntimeRDG.h")
#define UE_MCP_NNE_HAS_RDG 1
#else
#define UE_MCP_NNE_HAS_RDG 0
#endif
#if UE_MCP_NNE_AVAILABLE && __has_include("NNERuntimeNPU.h")
#define UE_MCP_NNE_HAS_NPU 1
#else
#define UE_MCP_NNE_HAS_NPU 0
#endif

// Control Rig: the Blueprint factory header, which not every engine ships.
#if __has_include("ControlRigBlueprintFactory.h")
#define UE_MCP_HAS_CONTROL_RIG_BLUEPRINT_FACTORY 1
#else
#define UE_MCP_HAS_CONTROL_RIG_BLUEPRINT_FACTORY 0
#endif
// UControlRigBlueprint, declared in ControlRigBlueprintLegacy.h from 5.7 and in
// ControlRigBlueprint.h before it.
#if __has_include("ControlRigBlueprintLegacy.h")
#define UE_MCP_HAS_CONTROL_RIG_BLUEPRINT_LEGACY_HEADER 1
#else
#define UE_MCP_HAS_CONTROL_RIG_BLUEPRINT_LEGACY_HEADER 0
#endif
#if UE_MCP_HAS_CONTROL_RIG_BLUEPRINT_LEGACY_HEADER || __has_include("ControlRigBlueprint.h")
#define UE_MCP_HAS_CONTROL_RIG_BLUEPRINT_TYPE 1
#else
#define UE_MCP_HAS_CONTROL_RIG_BLUEPRINT_TYPE 0
#endif

// Fab: linking against the Fab plugin's own C++ API. Off unless a build
// defines it; without it the bridge drives Fab through reflection only.
#ifndef WITH_FAB_PLUGIN
#define WITH_FAB_PLUGIN 0
#endif

// Sequencer: a skeletal animation section's PlayRate is an
// FMovieSceneTimeWarpVariant where that header exists, a plain float before.
#if __has_include("Variants/MovieSceneTimeWarpVariant.h")
#define UE_MCP_SKELANIM_PLAYRATE_IS_TIMEWARP 1
#else
#define UE_MCP_SKELANIM_PLAYRATE_IS_TIMEWARP 0
#endif

// ── Engine API shims ─────────────────────────────────────────────────────────
//
// Where an engine API was renamed or wrapped between 5.4 and 5.8, the shim goes
// here rather than in a handler, so two handlers cannot end up with two
// spellings of the same rule. Headers that moved live in MCPEngineCompat.h.

/** UStaticMesh Nanite settings. 5.5 added the accessor pair; 5.4 has only the
 *  member, which 5.7 in turn deprecated direct access to. */
inline FMeshNaniteSettings MCPGetNaniteSettings(const UStaticMesh* Mesh)
{
	if (!Mesh) return FMeshNaniteSettings();
#if UE_MCP_HAS_5_5_API
	return Mesh->GetNaniteSettings();
#else
	return Mesh->NaniteSettings;
#endif
}

/** Counterpart to MCPGetNaniteSettings. The caller still owns Modify(). */
inline void MCPSetNaniteSettings(UStaticMesh* Mesh, const FMeshNaniteSettings& InSettings)
{
	if (!Mesh) return;
#if UE_MCP_HAS_5_5_API
	Mesh->SetNaniteSettings(InSettings);
#else
	Mesh->NaniteSettings = InSettings;
#endif
}

/** Per-component Nanite opt-outs. 5.5 wrapped the bitfields in accessors. */
inline bool MCPIsDisallowNanite(const UStaticMeshComponent* Component)
{
	if (!Component) return false;
#if UE_MCP_HAS_5_5_API
	return Component->IsDisallowNanite();
#else
	return Component->bDisallowNanite != 0;
#endif
}

inline bool MCPIsForceDisableNanite(const UStaticMeshComponent* Component)
{
	if (!Component) return false;
#if UE_MCP_HAS_5_5_API
	return Component->IsForceDisableNanite();
#else
	return Component->bForceDisableNanite != 0;
#endif
}

/** An IK rig's retarget root bone. 5.4 spells the accessor GetRetargetRoot();
 *  later engines renamed the same accessor over the same FName to GetPelvis().
 *  The first overload drops out of the overload set when GetPelvis does not
 *  exist and the second when GetRetargetRoot does not, so whichever the engine
 *  has is called, without this header including the IK Rig module. */
template <typename RigType>
inline auto MCPIKRigRetargetRootBoneImpl(const RigType* Rig, int) -> decltype(Rig->GetPelvis())
{
	return Rig->GetPelvis();
}

template <typename RigType>
inline auto MCPIKRigRetargetRootBoneImpl(const RigType* Rig, long) -> decltype(Rig->GetRetargetRoot())
{
	return Rig->GetRetargetRoot();
}

template <typename RigType>
inline FName MCPIKRigRetargetRootBone(const RigType* Rig)
{
	return Rig ? MCPIKRigRetargetRootBoneImpl(Rig, 0) : NAME_None;
}
