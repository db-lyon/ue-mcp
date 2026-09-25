// asset(bulk_read_properties) - read the same properties off many assets in
// one call (#909).
//
// The reporter needed voice-management and concurrency settings across 582
// SoundWave, SoundCue and MetaSoundSource assets. audio(list) does not report
// those fields, and asset(read_properties) answers one asset per call, so the
// question became a Python loop. This is the same shape as
// level(query_components), one level up: the directory and class filters, the
// property reads, the predicates and the aggregates all run in the editor, and
// only the answer crosses the wire.
//
// This file exists as its own translation unit with its own registration so
// the change lands without touching AssetHandlers.cpp.
//
// It never writes. Loading an asset to read it does not dirty its package, and
// the response says so rather than asking to be believed.

#include "AssetHandlers_BulkRead.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "HandlerEditorState.h"
#include "HandlerQuery.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"

namespace
{
	// Every one of these is here because the action it replaces was unbounded.
	constexpr int32 MCPBulkReadDefaultRowLimit = 200;
	constexpr int32 MCPBulkReadMaxRowLimit = 2000;
	constexpr int32 MCPBulkReadDefaultMaxAssets = 2000;
	constexpr int32 MCPBulkReadHardMaxAssets = 20000;
	constexpr int32 MCPBulkReadMaxProperties = 32;
	constexpr int32 MCPBulkReadMaxPredicates = 24;
	constexpr int32 MCPBulkReadMaxGroups = 200;
	constexpr int32 MCPBulkReadMaxCountByPaths = 8;
	constexpr int32 MCPBulkReadMaxCountByValues = 64;
	constexpr int32 MCPBulkReadDefaultGroupSamples = 5;
	constexpr int32 MCPBulkReadMaxGroupSamples = 25;
	constexpr int32 MCPBulkReadMaxExplicitPaths = 5000;

	/**
	 * A Blueprint asset's properties live on its generated class default
	 * object, not on the UBlueprint wrapper, so reading `bCanEverTick` off the
	 * wrapper would always report absent.
	 */
	UObject* MCPBulkReadTarget(UObject* Asset)
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
		{
			if (UClass* GeneratedClass = Blueprint->GeneratedClass)
			{
				if (UObject* CDO = GeneratedClass->GetDefaultObject())
				{
					return CDO;
				}
			}
		}
		return Asset;
	}

	/** Render one row set as pretty JSON for the outputPath variant. */
	FString MCPBulkReadSerializeRows(const TArray<TSharedPtr<FJsonValue>>& Rows)
	{
		TSharedPtr<FJsonObject> Document = MakeShared<FJsonObject>();
		Document->SetNumberField(TEXT("count"), Rows.Num());
		Document->SetArrayField(TEXT("rows"), Rows);
		FString Output;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		FJsonSerializer::Serialize(Document.ToSharedRef(), Writer);
		return Output;
	}
}

void FAssetBulkReadHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("asset"));
	// Loading hundreds of assets to read them takes longer than the default
	// handler timeout allows, and a timeout here would look like a hang.
	Registry.RegisterHandlerWithTimeout(
		TEXT("bulk_read_asset_properties"), &BulkReadAssetProperties, 300.0f);
}

TSharedPtr<FJsonValue> FAssetBulkReadHandlers::BulkReadAssetProperties(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	// ── Properties to read ──────────────────────────────────────────────────
	TArray<FString> PropertyPaths;
	{
		const TArray<TSharedPtr<FJsonValue>>* PropertyValues = nullptr;
		if (TryGetArrayParam(Params, TEXT("propertyNames"), PropertyValues) && PropertyValues)
		{
			PropertyPaths = JsonArrayToStringList(PropertyValues);
		}
	}
	if (PropertyPaths.IsEmpty())
	{
		return MCPError(TEXT("Missing required 'propertyNames' array (dotted paths into nested structs are supported, e.g. 'CullDistance.Max')"));
	}
	if (PropertyPaths.Num() > MCPBulkReadMaxProperties)
	{
		return MCPError(FString::Printf(
			TEXT("'propertyNames' exceeds the maximum of %d entries"), MCPBulkReadMaxProperties));
	}

	// ── Selection ───────────────────────────────────────────────────────────
	TArray<FString> ExplicitPaths;
	{
		const TArray<TSharedPtr<FJsonValue>>* PathValues = nullptr;
		if (TryGetArrayParam(Params, TEXT("assetPaths"), PathValues) && PathValues)
		{
			ExplicitPaths = JsonArrayToStringList(PathValues);
		}
	}
	const FString Directory = OptionalString(Params, TEXT("directory"));
	if (ExplicitPaths.IsEmpty() && Directory.IsEmpty())
	{
		return MCPError(TEXT("Pass either 'assetPaths' (explicit list) or 'directory' (with optional 'classNames')"));
	}
	if (ExplicitPaths.Num() > MCPBulkReadMaxExplicitPaths)
	{
		return MCPError(FString::Printf(
			TEXT("'assetPaths' exceeds the maximum of %d entries"), MCPBulkReadMaxExplicitPaths));
	}

	const bool bRecursive = OptionalBool(Params, TEXT("recursive"), true);
	const bool bMatchSubclasses = OptionalBool(Params, TEXT("matchSubclasses"), true);
	const int32 MaxAssets = FMath::Clamp(
		OptionalInt(Params, TEXT("maxAssets"), MCPBulkReadDefaultMaxAssets), 1, MCPBulkReadHardMaxAssets);

	TArray<UClass*> ClassFilters;
	{
		const TArray<TSharedPtr<FJsonValue>>* ClassValues = nullptr;
		if (TryGetArrayParam(Params, TEXT("classNames"), ClassValues) && ClassValues)
		{
			for (const FString& Spec : JsonArrayToStringList(ClassValues))
			{
				UClass* Resolved = MCPResolveClass(Spec, true);
				if (!Resolved)
				{
					return MCPClassNotFoundError(Spec, TEXT("classNames"));
				}
				ClassFilters.Add(Resolved);
			}
		}
	}

	// ── Predicates and aggregates ───────────────────────────────────────────
	TArray<MCPQuery::FPredicate> Predicates;
	{
		const TArray<TSharedPtr<FJsonValue>>* WhereValues = nullptr;
		TryGetArrayParam(Params, TEXT("where"), WhereValues);
		FString ParseError;
		if (!MCPQuery::ParsePredicates(WhereValues, MCPBulkReadMaxPredicates, Predicates, ParseError))
		{
			return MCPError(ParseError);
		}
	}
	const FString WhereMode = OptionalString(Params, TEXT("whereMode"), TEXT("all")).ToLower();
	if (WhereMode != TEXT("all") && WhereMode != TEXT("any"))
	{
		return MCPError(TEXT("'whereMode' must be either 'all' or 'any'"));
	}
	const bool bSuspectOnly = OptionalBool(Params, TEXT("suspectOnly"), false);
	if (bSuspectOnly)
	{
		if (WhereMode == TEXT("any") && Predicates.Num() > 0)
		{
			return MCPError(TEXT("'suspectOnly' cannot be combined with whereMode 'any'; add {field:'suspect', op:'isTrue'} to 'where' instead"));
		}
		MCPQuery::FPredicate Suspect;
		Suspect.Field = TEXT("suspect");
		Suspect.Op = TEXT("isTrue");
		Predicates.Add(MoveTemp(Suspect));
	}

	const FString GroupBy = OptionalString(Params, TEXT("groupBy"));
	const int32 GroupSamples = FMath::Clamp(
		OptionalInt(Params, TEXT("sampleLimit"), MCPBulkReadDefaultGroupSamples), 0, MCPBulkReadMaxGroupSamples);
	TArray<FString> CountByPaths;
	{
		const TArray<TSharedPtr<FJsonValue>>* CountByValues = nullptr;
		if (TryGetArrayParam(Params, TEXT("countBy"), CountByValues) && CountByValues)
		{
			CountByPaths = JsonArrayToStringList(CountByValues);
		}
	}
	if (CountByPaths.Num() > MCPBulkReadMaxCountByPaths)
	{
		return MCPError(FString::Printf(
			TEXT("'countBy' exceeds the maximum of %d paths"), MCPBulkReadMaxCountByPaths));
	}

	const bool bCountOnly = OptionalBool(Params, TEXT("countOnly"), false);
	const int32 Limit = FMath::Clamp(
		OptionalInt(Params, TEXT("limit"), MCPBulkReadDefaultRowLimit), 0, MCPBulkReadMaxRowLimit);
	const int32 StartIndex = FMath::Max(0, OptionalInt(Params, TEXT("startIndex"), 0));
	const FString OutputPath = OptionalString(Params, TEXT("outputPath"));

	// ── Gather candidate asset paths ────────────────────────────────────────
	TArray<FString> CandidatePaths;
	bool bScanTruncated = false;
	if (!ExplicitPaths.IsEmpty())
	{
		CandidatePaths = MoveTemp(ExplicitPaths);
	}
	else
	{
		FAssetRegistryModule& AssetRegistryModule =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		FARFilter Filter;
		Filter.bRecursivePaths = bRecursive;
		Filter.PackagePaths.Add(FName(*Directory));
		for (const UClass* Class : ClassFilters)
		{
			Filter.ClassPaths.Add(Class->GetClassPathName());
		}
		Filter.bRecursiveClasses = bMatchSubclasses;

		TArray<FAssetData> Found;
		AssetRegistry.GetAssets(Filter, Found);
		for (const FAssetData& Data : Found)
		{
			if (Data.AssetClassPath.GetAssetName() == FName(TEXT("ObjectRedirector")))
			{
				continue;
			}
			CandidatePaths.Add(Data.GetObjectPathString());
		}
	}
	CandidatePaths.Sort();
	if (CandidatePaths.Num() > MaxAssets)
	{
		bScanTruncated = true;
		CandidatePaths.SetNum(MaxAssets);
	}

	TArray<FString> DirtyBefore;
	MCPEditorState::CollectDirtyEditorPackageNames(DirtyBefore);

	// ── Read ────────────────────────────────────────────────────────────────
	int32 Loaded = 0;
	int32 Matched = 0;
	TArray<FString> UnloadableAssets;
	TArray<FString> ClassFilteredOut;
	TArray<TSharedPtr<FJsonValue>> Rows;
	TMap<FString, int32> GroupCounts;
	TMap<FString, TArray<FString>> GroupSamplesByKey;
	TArray<FString> GroupOrder;
	TMap<FString, TMap<FString, int32>> CountByHistograms;
	bool bGroupsTruncated = false;

	for (const FString& AssetPath : CandidatePaths)
	{
		UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
		if (!Asset)
		{
			if (UnloadableAssets.Num() < 50)
			{
				UnloadableAssets.Add(AssetPath);
			}
			continue;
		}
		++Loaded;

		// An explicit assetPaths list bypasses the registry filter, so the
		// class filter has to be applied here too or classNames would silently
		// mean nothing for that shape of request.
		if (ClassFilters.Num() > 0)
		{
			bool bClassMatches = false;
			for (UClass* Class : ClassFilters)
			{
				if (bMatchSubclasses ? Asset->IsA(Class) : (Asset->GetClass() == Class))
				{
					bClassMatches = true;
					break;
				}
			}
			if (!bClassMatches)
			{
				if (ClassFilteredOut.Num() < 50)
				{
					ClassFilteredOut.Add(AssetPath);
				}
				continue;
			}
		}

		UObject* Target = MCPBulkReadTarget(Asset);

		TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
		TArray<FString> MissingProperties;
		bool bAnyNull = false;
		for (const FString& PropertyPath : PropertyPaths)
		{
			FString ResolvedType;
			const TSharedPtr<FJsonValue> Value = MCPQuery::ReadDottedProperty(Target, PropertyPath, ResolvedType);
			if (!Value.IsValid())
			{
				// Absent is not null. "This class has no such property" and
				// "this property is unset" are different findings, and
				// collapsing them is what makes an audit unactionable.
				MissingProperties.Add(PropertyPath);
				continue;
			}
			if (Value->Type == EJson::Null)
			{
				bAnyNull = true;
			}
			Props->SetField(PropertyPath, Value);
		}

		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("assetPath"), Asset->GetPathName());
		Row->SetStringField(TEXT("assetName"), Asset->GetName());
		Row->SetStringField(TEXT("className"), Asset->GetClass()->GetName());
		Row->SetStringField(TEXT("packagePath"), Asset->GetOutermost()->GetName());
		if (Target != Asset)
		{
			Row->SetStringField(TEXT("readFrom"), Target->GetPathName());
		}
		Row->SetObjectField(TEXT("props"), Props);
		Row->SetArrayField(TEXT("missingProperties"), MCPStringListToJson(MissingProperties));
		Row->SetBoolField(TEXT("suspect"), MissingProperties.Num() > 0 || bAnyNull);

		if (!MCPQuery::EvaluateAll(Predicates, Row, WhereMode == TEXT("all")))
		{
			continue;
		}
		++Matched;

		if (!GroupBy.IsEmpty())
		{
			const FString Key = MCPQuery::ValueKey(MCPQuery::ResolvePath(Row, GroupBy));
			if (int32* Existing = GroupCounts.Find(Key))
			{
				++(*Existing);
			}
			else if (GroupCounts.Num() < MCPBulkReadMaxGroups)
			{
				GroupCounts.Add(Key, 1);
				GroupOrder.Add(Key);
			}
			else
			{
				bGroupsTruncated = true;
			}
			if (GroupCounts.Contains(Key))
			{
				TArray<FString>& Samples = GroupSamplesByKey.FindOrAdd(Key);
				if (Samples.Num() < GroupSamples)
				{
					Samples.Add(Asset->GetName());
				}
			}
		}

		for (const FString& CountPath : CountByPaths)
		{
			TMap<FString, int32>& Histogram = CountByHistograms.FindOrAdd(CountPath);
			const FString Key = MCPQuery::ValueKey(MCPQuery::ResolvePath(Row, CountPath));
			if (int32* Existing = Histogram.Find(Key))
			{
				++(*Existing);
			}
			else if (Histogram.Num() < MCPBulkReadMaxCountByValues)
			{
				Histogram.Add(Key, 1);
			}
		}

		if (!bCountOnly)
		{
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
	}

	// ── Response ────────────────────────────────────────────────────────────
	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("candidates"), CandidatePaths.Num());
	Result->SetNumberField(TEXT("loaded"), Loaded);
	Result->SetNumberField(TEXT("matched"), Matched);
	Result->SetBoolField(TEXT("countOnly"), bCountOnly);
	Result->SetBoolField(TEXT("scanTruncated"), bScanTruncated);
	if (bScanTruncated)
	{
		Result->SetStringField(TEXT("scanNote"), FString::Printf(
			TEXT("More than maxAssets (%d) candidates matched the selection; narrow 'directory' or 'classNames'."),
			MaxAssets));
	}
	Result->SetArrayField(TEXT("unloadableAssets"), MCPStringListToJson(UnloadableAssets));
	if (ClassFilteredOut.Num() > 0)
	{
		Result->SetArrayField(TEXT("classFilteredOut"), MCPStringListToJson(ClassFilteredOut));
	}

	if (!bCountOnly)
	{
		if (!OutputPath.IsEmpty())
		{
			// File output is the escape valve for a result set too large to be
			// worth paging through. The rows still exist; they just do not go
			// through the model's context to get to disk.
			const FString Absolute = FPaths::ConvertRelativePathToFull(OutputPath);
			if (!FFileHelper::SaveStringToFile(MCPBulkReadSerializeRows(Rows), *Absolute))
			{
				return MCPError(FString::Printf(TEXT("Failed to write 'outputPath': %s"), *Absolute));
			}
			Result->SetStringField(TEXT("outputPath"), Absolute);
			Result->SetNumberField(TEXT("written"), Rows.Num());
		}
		else
		{
			const int32 First = FMath::Min(StartIndex, Rows.Num());
			const int32 Last = FMath::Min(First + Limit, Rows.Num());
			TArray<TSharedPtr<FJsonValue>> Page;
			for (int32 Index = First; Index < Last; ++Index)
			{
				Page.Add(Rows[Index]);
			}
			Result->SetArrayField(TEXT("rows"), Page);
			Result->SetNumberField(TEXT("returned"), Page.Num());
			Result->SetNumberField(TEXT("startIndex"), StartIndex);
			Result->SetNumberField(TEXT("limit"), Limit);
			const int32 NextStartIndex = StartIndex + Page.Num();
			Result->SetBoolField(TEXT("hasMore"), NextStartIndex < Rows.Num());
			if (NextStartIndex < Rows.Num())
			{
				Result->SetNumberField(TEXT("nextStartIndex"), NextStartIndex);
			}
		}
	}

	if (!GroupBy.IsEmpty())
	{
		GroupOrder.Sort([&GroupCounts](const FString& A, const FString& B)
		{
			const int32 CountA = GroupCounts.FindChecked(A);
			const int32 CountB = GroupCounts.FindChecked(B);
			return CountA != CountB ? CountA > CountB : A < B;
		});
		TArray<TSharedPtr<FJsonValue>> GroupArray;
		for (const FString& Key : GroupOrder)
		{
			TSharedPtr<FJsonObject> GroupObject = MakeShared<FJsonObject>();
			GroupObject->SetStringField(TEXT("key"), Key);
			GroupObject->SetNumberField(TEXT("count"), GroupCounts.FindChecked(Key));
			if (const TArray<FString>* Samples = GroupSamplesByKey.Find(Key))
			{
				GroupObject->SetArrayField(TEXT("samples"), MCPStringListToJson(*Samples));
			}
			GroupArray.Add(MakeShared<FJsonValueObject>(GroupObject));
		}
		Result->SetStringField(TEXT("groupBy"), GroupBy);
		Result->SetArrayField(TEXT("groups"), GroupArray);
		Result->SetBoolField(TEXT("groupsTruncated"), bGroupsTruncated);
	}

	if (CountByPaths.Num() > 0)
	{
		TSharedPtr<FJsonObject> CountsObject = MakeShared<FJsonObject>();
		for (const FString& CountPath : CountByPaths)
		{
			TSharedPtr<FJsonObject> Histogram = MakeShared<FJsonObject>();
			if (const TMap<FString, int32>* Values = CountByHistograms.Find(CountPath))
			{
				for (const TPair<FString, int32>& Pair : *Values)
				{
					Histogram->SetNumberField(Pair.Key, Pair.Value);
				}
			}
			CountsObject->SetObjectField(CountPath, Histogram);
		}
		Result->SetObjectField(TEXT("counts"), CountsObject);
	}

	// The read guarantee, asserted rather than assumed. Loading an asset to
	// read it does not dirty its package; if something did, the caller sees
	// which package rather than an unexplained unsaved-changes marker.
	TArray<FString> DirtyAfter;
	MCPEditorState::CollectDirtyEditorPackageNames(DirtyAfter);
	for (const FString& Already : DirtyBefore)
	{
		DirtyAfter.Remove(Already);
	}
	Result->SetBoolField(TEXT("dirtiedPackages"), DirtyAfter.Num() > 0);
	if (DirtyAfter.Num() > 0)
	{
		Result->SetArrayField(TEXT("dirtyPackages"), MCPStringListToJson(DirtyAfter));
	}

	return MCPResult(Result);
}
