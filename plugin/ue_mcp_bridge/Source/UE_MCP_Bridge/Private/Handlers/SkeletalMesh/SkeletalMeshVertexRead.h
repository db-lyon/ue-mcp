#pragma once

// Selection, paging and dump code shared by the two skinned vertex reads:
// asset(read_skeletal_mesh_vertices) over source geometry in the bind pose and
// level(get_skinned_vertices) over a placed component's render geometry as
// posed (#1567, #1452). Both validate every selector before reading a vertex,
// page the same way and write the same dump.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HandlerUtils.h"
#include "ReferenceSkeleton.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

/** Object.Field as an int32, refusing a fraction, a non-number or an overflow.
 *  Context prefixes the field in the error. */
inline TSharedPtr<FJsonValue> MCPParseFiniteInteger(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	int32& OutValue,
	const FString& Context)
{
	double Number = 0.0;
	if (!Object.IsValid() || !Object->TryGetNumberField(Field, Number)
		|| !FMath::IsFinite(Number)
		|| !FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number))
		|| Number < static_cast<double>(MIN_int32)
		|| Number > static_cast<double>(MAX_int32))
	{
		return MCPError(FString::Printf(TEXT("%s.%s must be a finite integer"), *Context, Field));
	}
	OutValue = static_cast<int32>(Number);
	return nullptr;
}

namespace MCPVertexRead
{
	constexpr int32 DefaultLimit = 2000;
	constexpr int32 MaxLimit = 20000;

	struct FIncludes
	{
		bool bPositions = true;
		bool bWeights = true;
		bool bTriangles = false;
	};

	/** include: any of positions, weights, triangles (default positions and weights). */
	inline TSharedPtr<FJsonValue> ParseIncludes(const TSharedPtr<FJsonObject>& Params, FIncludes& Out)
	{
		Out = FIncludes();
		if (!HasParam(Params, TEXT("include"))) return nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!TryGetArrayParam(Params, TEXT("include"), Values) || !Values || Values->IsEmpty())
		{
			return MCPError(TEXT("include must be a non-empty array of positions, weights, triangles"));
		}
		Out.bPositions = false;
		Out.bWeights = false;
		for (int32 Index = 0; Index < Values->Num(); ++Index)
		{
			FString Name;
			if (!(*Values)[Index].IsValid() || !(*Values)[Index]->TryGetString(Name))
			{
				return MCPError(FString::Printf(TEXT("include[%d] must be a string"), Index));
			}
			if (Name.Equals(TEXT("positions"), ESearchCase::IgnoreCase)) Out.bPositions = true;
			else if (Name.Equals(TEXT("weights"), ESearchCase::IgnoreCase)) Out.bWeights = true;
			else if (Name.Equals(TEXT("triangles"), ESearchCase::IgnoreCase)) Out.bTriangles = true;
			else
			{
				return MCPError(FString::Printf(
					TEXT("include[%d]='%s' is not one of positions, weights, triangles"), Index, *Name));
			}
		}
		return nullptr;
	}

	/** boneNames, includeChildBones and minWeight: a vertex is kept by its summed
	 *  weight on the selected bones. */
	struct FBoneSelection
	{
		bool bActive = false;
		TArray<bool> Bones;
		TArray<FString> Names;
		bool bHasMinWeight = false;
		double MinWeight = 0.0;

		bool IsSelected(int32 BoneIndex) const { return Bones.IsValidIndex(BoneIndex) && Bones[BoneIndex]; }
		bool Keeps(float SummedWeight) const
		{
			return SummedWeight > 0.0f && !(bHasMinWeight && SummedWeight < MinWeight);
		}
	};

	inline TSharedPtr<FJsonValue> ParseBoneSelection(
		const TSharedPtr<FJsonObject>& Params,
		const FReferenceSkeleton& RefSkeleton,
		const FString& MeshPath,
		FBoneSelection& Out)
	{
		Out = FBoneSelection();
		const int32 BoneCount = RefSkeleton.GetNum();
		Out.bActive = HasParam(Params, TEXT("boneNames"));
		if (Out.bActive)
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!TryGetArrayParam(Params, TEXT("boneNames"), Values) || !Values || Values->IsEmpty())
			{
				return MCPError(TEXT("boneNames must be a non-empty array of bone names; omit it to keep every vertex"));
			}
			Out.Bones.Init(false, BoneCount);
			for (int32 InputIndex = 0; InputIndex < Values->Num(); ++InputIndex)
			{
				FString BoneName;
				if (!(*Values)[InputIndex].IsValid() || !(*Values)[InputIndex]->TryGetString(BoneName) || BoneName.IsEmpty())
				{
					return MCPError(FString::Printf(TEXT("boneNames[%d] must be a non-empty string"), InputIndex));
				}
				const int32 BoneIndex = RefSkeleton.FindBoneIndex(FName(*BoneName));
				if (BoneIndex == INDEX_NONE)
				{
					return MCPError(FString::Printf(
						TEXT("boneNames[%d]='%s' is not a bone of '%s'"), InputIndex, *BoneName, *MeshPath));
				}
				Out.Bones[BoneIndex] = true;
			}
			if (OptionalBool(Params, TEXT("includeChildBones"), false))
			{
				// Parents precede children in a reference skeleton.
				for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
				{
					const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
					if (ParentIndex != INDEX_NONE && Out.Bones[ParentIndex]) Out.Bones[BoneIndex] = true;
				}
			}
			for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
			{
				if (Out.Bones[BoneIndex]) Out.Names.Add(RefSkeleton.GetBoneName(BoneIndex).ToString());
			}
		}
		else if (HasParam(Params, TEXT("includeChildBones")) || HasParam(Params, TEXT("minWeight")))
		{
			return MCPError(TEXT("includeChildBones and minWeight select by bone and need boneNames"));
		}

		Out.bHasMinWeight = HasParam(Params, TEXT("minWeight"));
		if (Out.bHasMinWeight)
		{
			if (!TryGetNumberParam(Params, TEXT("minWeight"), Out.MinWeight)
				|| !FMath::IsFinite(Out.MinWeight) || Out.MinWeight <= 0.0 || Out.MinWeight > 1.0)
			{
				return MCPError(TEXT("minWeight must be a finite number with 0 < minWeight <= 1"));
			}
		}
		return nullptr;
	}

	/** box: {min, max}, inclusive, in whatever space the caller documents. */
	inline TSharedPtr<FJsonValue> ParseBox(const TSharedPtr<FJsonObject>& Params, bool& bOutByBox, FBox& OutBox)
	{
		bOutByBox = false;
		OutBox = FBox(ForceInit);
		if (!HasParam(Params, TEXT("box"))) return nullptr;
		const TSharedPtr<FJsonObject>* BoxObject = nullptr;
		const TSharedPtr<FJsonObject>* MinObject = nullptr;
		const TSharedPtr<FJsonObject>* MaxObject = nullptr;
		FVector Min = FVector::ZeroVector;
		FVector Max = FVector::ZeroVector;
		if (!TryGetObjectParam(Params, TEXT("box"), BoxObject) || !BoxObject || !(*BoxObject).IsValid()
			|| !(*BoxObject)->TryGetObjectField(TEXT("min"), MinObject) || !MinObject
			|| !(*BoxObject)->TryGetObjectField(TEXT("max"), MaxObject) || !MaxObject
			|| !ReadVec3FieldsStrict(*MinObject, Min) || !ReadVec3FieldsStrict(*MaxObject, Max))
		{
			return MCPError(TEXT("box must be {min: {x,y,z}, max: {x,y,z}} with every axis given"));
		}
		if (Min.X > Max.X || Min.Y > Max.Y || Min.Z > Max.Z)
		{
			return MCPError(TEXT("box.min must not exceed box.max on any axis"));
		}
		OutBox = FBox(Min, Max);
		bOutByBox = true;
		return nullptr;
	}

	/** The page: OffsetKey and limit inline, or dumpToFile with outputPath. */
	struct FPage
	{
		bool bDump = false;
		FString OutputPath;
		int32 Offset = 0;
		int32 Limit = DefaultLimit;
		int32 First = 0;
		int32 End = 0;

		/** Fixes First and End once the selection is known. */
		void Resolve(int32 SelectedCount)
		{
			First = bDump ? 0 : FMath::Min(Offset, SelectedCount);
			End = bDump
				? SelectedCount
				: static_cast<int32>(FMath::Min<int64>(static_cast<int64>(First) + Limit, SelectedCount));
		}
	};

	inline TSharedPtr<FJsonValue> ParsePage(const TSharedPtr<FJsonObject>& Params, const TCHAR* OffsetKey, FPage& Out)
	{
		Out = FPage();
		Out.bDump = OptionalBool(Params, TEXT("dumpToFile"), false);
		Out.OutputPath = OptionalString(Params, TEXT("outputPath"), TEXT(""));
		if (!Out.bDump && !Out.OutputPath.IsEmpty())
		{
			return MCPError(TEXT("outputPath needs dumpToFile=true"));
		}
		if (HasParam(Params, OffsetKey))
		{
			if (TSharedPtr<FJsonValue> Error = MCPParseFiniteInteger(Params, OffsetKey, Out.Offset, TEXT("params"))) return Error;
			if (Out.Offset < 0) return MCPError(FString::Printf(TEXT("%s must be 0 or more"), OffsetKey));
		}
		if (HasParam(Params, TEXT("limit")))
		{
			if (TSharedPtr<FJsonValue> Error = MCPParseFiniteInteger(Params, TEXT("limit"), Out.Limit, TEXT("params"))) return Error;
			if (Out.Limit < 1 || Out.Limit > MaxLimit)
			{
				return MCPError(FString::Printf(TEXT("limit must be between 1 and %d; use dumpToFile for more"), MaxLimit));
			}
		}
		return nullptr;
	}

	/** One triangle as three vertex indices, with the group it draws in. */
	struct FTriangle
	{
		int32 Index = INDEX_NONE;
		int32 Corners[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };
		int32 Group = INDEX_NONE;
	};

	/** Emits each triangle whose three corners are selected. A triangle belongs to
	 *  the page holding its earliest corner, so paging returns each one exactly
	 *  once. ForEach(Visit) calls Visit(const FTriangle&) per triangle; Ordinals
	 *  maps a vertex index to its place in the selection or INDEX_NONE. Returns
	 *  how many triangles the whole selection holds. */
	template <typename ForEachFn>
	inline int32 CollectTriangles(
		ForEachFn&& ForEach,
		const TArray<int32>& Ordinals,
		const FPage& Page,
		const TCHAR* GroupKey,
		TArray<TSharedPtr<FJsonValue>>& OutTriangles)
	{
		int32 SelectedTriangleCount = 0;
		ForEach([&](const FTriangle& Triangle)
		{
			int32 Owner = MAX_int32;
			for (const int32 Corner : Triangle.Corners)
			{
				const int32 Ordinal = Ordinals.IsValidIndex(Corner) ? Ordinals[Corner] : INDEX_NONE;
				if (Ordinal == INDEX_NONE) return;
				Owner = FMath::Min(Owner, Ordinal);
			}
			++SelectedTriangleCount;
			if (Owner < Page.First || Owner >= Page.End) return;
			auto Object = MakeShared<FJsonObject>();
			Object->SetNumberField(TEXT("triangleIndex"), Triangle.Index);
			TArray<TSharedPtr<FJsonValue>> CornerValues;
			for (const int32 Corner : Triangle.Corners) CornerValues.Add(MakeShared<FJsonValueNumber>(Corner));
			Object->SetArrayField(TEXT("vertexIndices"), CornerValues);
			Object->SetNumberField(GroupKey, Triangle.Group);
			OutTriangles.Add(MakeShared<FJsonValueObject>(Object));
		});
		return SelectedTriangleCount;
	}

	/** selectedVertexCount, selectedBones and selectedBounds on the summary. */
	inline void SetSelectionSummary(
		const TSharedPtr<FJsonObject>& Result,
		int32 VertexCount,
		int32 SelectedCount,
		const FBoneSelection& Bones,
		const FBox& SelectedBounds)
	{
		Result->SetNumberField(TEXT("vertexCount"), VertexCount);
		Result->SetNumberField(TEXT("selectedVertexCount"), SelectedCount);
		if (Bones.bActive)
		{
			Result->SetArrayField(TEXT("selectedBones"), MCPStringListToJson(Bones.Names));
		}
		if (SelectedCount > 0)
		{
			auto Bounds = MakeShared<FJsonObject>();
			Bounds->SetObjectField(TEXT("min"), MCPVec3ToJsonObject(SelectedBounds.Min));
			Bounds->SetObjectField(TEXT("max"), MCPVec3ToJsonObject(SelectedBounds.Max));
			Bounds->SetObjectField(TEXT("center"), MCPVec3ToJsonObject(SelectedBounds.GetCenter()));
			Bounds->SetObjectField(TEXT("extent"), MCPVec3ToJsonObject(SelectedBounds.GetExtent()));
			Result->SetObjectField(TEXT("selectedBounds"), Bounds);
		}
	}

	/** Returns the page inline, or writes the whole selection to a file and
	 *  returns only the summary. OffsetKey is the page-start parameter's name;
	 *  DefaultDumpPath is relative to Saved/. */
	inline TSharedPtr<FJsonValue> Finish(
		const TSharedPtr<FJsonObject>& Result,
		const TArray<TSharedPtr<FJsonValue>>& Vertices,
		const TArray<TSharedPtr<FJsonValue>>& Triangles,
		bool bTriangles,
		const FPage& Page,
		const TCHAR* OffsetKey,
		int32 SelectedCount,
		const FString& DefaultDumpPath,
		const TCHAR* What)
	{
		if (Page.bDump)
		{
			const TSharedRef<FJsonObject> Dump = MakeShared<FJsonObject>();
			Dump->Values = Result->Values;
			Dump->SetArrayField(TEXT("vertices"), Vertices);
			if (bTriangles) Dump->SetArrayField(TEXT("triangles"), Triangles);

			const FString RequestedPath = Page.OutputPath.IsEmpty() ? DefaultDumpPath : Page.OutputPath;
			const FString ResolvedPath = FPaths::ConvertRelativePathToFull(MCPResolveDumpPath(RequestedPath));
			FString JsonText;
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
			if (!FJsonSerializer::Serialize(Dump, Writer))
			{
				return MCPError(FString::Printf(TEXT("Failed to serialize the %s"), What));
			}
			FString WriteError;
			if (!MCPWriteDumpFile(ResolvedPath, JsonText, What, WriteError))
			{
				return MCPError(WriteError);
			}
			Result->SetStringField(TEXT("outputPath"), ResolvedPath);
			Result->SetNumberField(TEXT("count"), Vertices.Num());
			return MCPResult(Result);
		}

		// The page echoes the key it was asked with: offset/nextOffset, startIndex/nextStartIndex.
		FString NextKey(OffsetKey);
		NextKey = TEXT("next") + NextKey.Left(1).ToUpper() + NextKey.Mid(1);
		Result->SetNumberField(OffsetKey, Page.First);
		Result->SetNumberField(TEXT("count"), Vertices.Num());
		Result->SetBoolField(TEXT("hasMore"), Page.End < SelectedCount);
		if (Page.End < SelectedCount) Result->SetNumberField(NextKey, Page.End);
		Result->SetArrayField(TEXT("vertices"), Vertices);
		if (bTriangles) Result->SetArrayField(TEXT("triangles"), Triangles);
		return MCPResult(Result);
	}
}
