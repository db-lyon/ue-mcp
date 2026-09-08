// Shared PoseSearch schema validity, used by the schema authoring actions and
// by the database actions that depend on a usable schema.
//
// UPoseSearchDatabase indexes through its UPoseSearchSchema, and the index
// build refuses a schema it cannot sample: "BuildIndex Failed because of
// invalid Schema", which the editor's asset check then reports against the
// database. A schema is samplable only once it has a skeleton to read bones
// from and at least one finalized feature channel to write into the pose
// vector - SchemaCardinality is the width that comes out of Finalize, and a
// schema with no channels finalizes to zero. Both halves are authored by
// animation(create_pose_search_schema) and neither was checked anywhere, so a
// database could be pointed at a schema that could never index.
//
// The database handlers and the schema handlers live in different translation
// units of a unity build, so this is a header rather than a file-local copy in
// each.
#pragma once

#include "CoreMinimal.h"
#include "UObject/UnrealType.h"
#include "PoseSearch/PoseSearchSchema.h"

namespace MCPPoseSearch
{
	/** Recompute FinalizedChannels and SchemaCardinality after a channel or
	 *  skeleton edit. Finalize() itself is private; PostEditChangeProperty is
	 *  the public door onto it. */
	inline void Finalize(UPoseSearchSchema* Schema)
	{
		if (!Schema) return;
		FPropertyChangedEvent EmptyEvent(nullptr);
		Schema->PostEditChangeProperty(EmptyEvent);
	}

	/** Empty when the schema can index. Otherwise the missing halves, named the
	 *  way the actions that add them are named. */
	inline FString DescribeProblems(const UPoseSearchSchema* Schema)
	{
		if (!Schema)
		{
			return TEXT("no schema is set");
		}

		TArray<FString> Problems;

		bool bHasSkeleton = false;
		for (const FPoseSearchRoledSkeleton& Roled : Schema->GetRoledSkeletons())
		{
			if (Roled.Skeleton)
			{
				bHasSkeleton = true;
				break;
			}
		}
		if (!bHasSkeleton)
		{
			Problems.Add(TEXT("it has no skeleton, so there are no bones to sample "
				"(animation(create_pose_search_schema) takes skeletonPath)"));
		}

		if (Schema->GetChannels().Num() == 0 || Schema->SchemaCardinality <= 0)
		{
			Problems.Add(TEXT("it has no feature channels, so every pose would encode to nothing "
				"(animation(add_pose_search_schema_pose_channel) / "
				"animation(add_pose_search_schema_trajectory_channel), or create the schema with "
				"addDefaultChannels)"));
		}

		return FString::Join(Problems, TEXT(", and "));
	}

	inline bool IsUsable(const UPoseSearchSchema* Schema)
	{
		return DescribeProblems(Schema).IsEmpty();
	}

	/** Empty when the database can take clips. Otherwise the refusal, in the
	 *  words the caller needs: adding clips is what turns a database that is
	 *  merely empty into one the editor's asset check rejects. Both clip-writing
	 *  actions ask this, so the sentence is written once. */
	inline FString DescribeClipRefusal(const UPoseSearchSchema* Schema, const FString& DatabasePath)
	{
		const FString Problems = DescribeProblems(Schema);
		if (Problems.IsEmpty()) return FString();
		return FString::Printf(
			TEXT("Database '%s' cannot take clips because %s. Clips on a database that cannot index make the asset ")
			TEXT("fail the editor's own validation (\"BuildIndex Failed because of invalid Schema\"). Set a usable ")
			TEXT("schema first with animation(set_pose_search_schema), or create databases with ")
			TEXT("animation(create_pose_search_database, skeletonPath=...) which authors one."),
			*DatabasePath, *Problems);
	}
}
