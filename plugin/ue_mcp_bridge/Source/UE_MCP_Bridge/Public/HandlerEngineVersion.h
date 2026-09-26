#pragma once

// Engine version gates and the shims for APIs renamed between UE 5.4 and 5.8.
// Headers that moved live in MCPEngineCompat.h.

#include "CoreMinimal.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"

// Engine API version gates. One macro per supported minor version, so a gate reads the
// same everywhere and nobody writes a second scheme. The supported range is
// UE 5.4 through 5.8; 5.4 is the floor, which is why UE_MCP_HAS_5_4_API is
// true for every engine the plugin builds against and exists only so a gate
// can name the floor explicitly instead of leaving it implied.
#define UE_MCP_HAS_5_4_API ((ENGINE_MAJOR_VERSION > 5) || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4))

// True on UE 5.5+ (and any future 6.x). Used to gate APIs introduced in 5.5
// that don't exist in 5.4: StateTreeEditingSubsystem, FExpressionInputIterator,
// AActor::Get/SetNetUpdateFrequency, UWidgetBlueprint::WidgetVariableNameToGuidMap,
// UPCGEditorGraphNodeBase, UIKRetargeterController::AssignIKRigToAllOps, etc.
#define UE_MCP_HAS_5_5_API ((ENGINE_MAJOR_VERSION > 5) || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5))

// True on UE 5.6+ (and any future 6.x). The gate between 5.5 and 5.7, kept so
// an API that arrived in 5.6 is gated by name rather than by an open-coded
// ENGINE_MINOR_VERSION test.
#define UE_MCP_HAS_5_6_API ((ENGINE_MAJOR_VERSION > 5) || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6))

// True on UE 5.7+. Gates EFindObjectFlags (the bool bExactClass overloads are
// deprecated there) and UPoseSearchDatabase's non-templated
// GetDatabaseAnimationAsset.
#define UE_MCP_HAS_5_7_API ((ENGINE_MAJOR_VERSION > 5) || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7))

// True on UE 5.8+. Gates EGetObjectsFlags and
// FStringTable::ImportStringsFromCSVFile; the bool / ImportStrings forms they
// replace are deprecated in 5.8 and warn on every user build, but do not exist
// before it. Also gates the one-argument UMaterial::SetMaterialUsage (5.7 has
// only the bNeedsRecompile form) and FCoreDelegates::ApplicationHeartbeat
// (added in 5.8; the status module carries its own copy of this macro because
// it must not depend on this one).
#define UE_MCP_HAS_5_8_API ((ENGINE_MAJOR_VERSION > 5) || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8))

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
