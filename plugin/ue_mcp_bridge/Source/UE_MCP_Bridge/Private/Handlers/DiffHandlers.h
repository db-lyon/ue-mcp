#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

/**
 * Semantic revision diffing for graph-based binary assets. Blueprint and
 * StateTree assets are opaque to git, so an agent's edits are unreviewable:
 * there is no way to answer "what did this change between two versions".
 *
 * Phase 1 (here): Blueprint structural diff between two loadable asset paths.
 * Covers parent class, variables, functions/macros, components, and per-graph node and
 * connection deltas, keyed on the stable node GUID so a moved or edited node is
 * matched rather than reported as remove+add. Output is a structured JSON delta
 * plus a human-readable summary.
 *
 * Follow-ups (documented, not yet wired): loading an arbitrary source-control
 * revision into a transient package so the "to" side can be a depot revision
 * rather than a second on-disk asset (needs a live SCC spike), then StateTree
 * assets on the same diff spine.
 */
class FDiffHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// Diff two Blueprints loaded from asset paths. Params: assetPath (base/A),
	// otherPath (compare/B). Reports changes as A -> B.
	static TSharedPtr<FJsonValue> DiffBlueprint(const TSharedPtr<FJsonObject>& Params);

	// Diff two exact USkeleton or USkeletalMesh assets without emitting full
	// bone dumps. Compares raw bone names, parent names, raw indices, and
	// declared virtual-bone records. Reference-pose transforms, export names,
	// sockets, retarget sources, and other metadata are outside this structural
	// diff. Both inputs must have the same exact asset class.
	static TSharedPtr<FJsonValue> DiffSkeleton(const TSharedPtr<FJsonObject>& Params);

	// Type-routing entry point. Blueprints, Skeletons, and SkeletalMeshes
	// delegate to their respective semantic diff handlers; unsupported classes
	// fail clearly.
	static TSharedPtr<FJsonValue> DiffAsset(const TSharedPtr<FJsonObject>& Params);
};
