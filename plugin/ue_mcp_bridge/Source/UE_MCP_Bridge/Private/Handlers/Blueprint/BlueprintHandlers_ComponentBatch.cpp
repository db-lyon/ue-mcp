// Translation-unit partition of FBlueprintHandlers. Registration and specs
// live in BlueprintHandlers.cpp.
#include "BlueprintHandlers.h"
#include "BlueprintHandlers_Internal.h"
#include "BlueprintHandlers_ComponentProperty.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"

namespace MCPBlueprintComponentBatch
{
struct FItem
{
	FString Path, ComponentName;
	TSharedPtr<FJsonObject> Properties;
	TSharedPtr<FJsonObject> Previous = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
	UBlueprint* Blueprint = nullptr;
	UActorComponent* Source = nullptr;
	UActorComponent* Proposed = nullptr;
	bool bFailed = false;
	bool bSkipped = false;
	bool bTouched = false;
};

inline void Fail(FItem& Item, const TCHAR* Stage, const FString& Error)
{
	Item.bFailed = true;
	Item.Row->SetBoolField(TEXT("success"), false);
	Item.Row->SetStringField(TEXT("stage"), Stage);
	Item.Row->SetStringField(TEXT("error"), Error);
}

inline void Skip(FItem& Item, const FString& Reason)
{
	Item.bSkipped = true;
	Item.Row->SetBoolField(TEXT("success"), true);
	Item.Row->SetBoolField(TEXT("skipped"), true);
	Item.Row->SetStringField(TEXT("stage"), TEXT("selection"));
	Item.Row->SetStringField(TEXT("reason"), Reason);
}

inline TArray<FString> PropertyNames(const FItem& Item)
{
	TArray<FString> Names;
	for (const auto& Pair : Item.Properties->Values) Names.Add(FString(*Pair.Key));
	Names.Sort();
	return Names;
}

inline bool ReadValues(FItem& Item, UActorComponent* Component, const TCHAR* Field, bool bCompare)
{
	auto Values = MakeShared<FJsonObject>();
	auto Texts = MakeShared<FJsonObject>();
	bool bValid = true;
	for (const FString& Name : PropertyNames(Item))
	{
		FString Text, Error;
		TSharedPtr<FJsonValue> Replay;
		MCPBlueprintComponentProperty::FAddress Address;
		if (!MCPBlueprintComponentProperty::Read(Component, Name, Text, Replay, Error) ||
			!MCPBlueprintComponentProperty::Resolve(Component, Name, Address, Error))
		{
			Fail(Item, TEXT("readback"), Error);
			bValid = false;
			continue;
		}
		Values->SetField(Name, FMCPJsonSerializer::SerializeValue(Address.Value, Address.Property));
		Texts->SetStringField(Name, Text);
		MCPBlueprintComponentProperty::FAddress Expected;
		// Property equality keeps maps independent of iteration order and
		// compares instanced subobjects by contents rather than scratch paths.
		if (bCompare &&
			(!MCPBlueprintComponentProperty::Resolve(Item.Proposed, Name, Expected, Error) ||
			 !Address.Property->SameType(Expected.Property) ||
			 !Address.Property->Identical(Address.Value, Expected.Value, PPF_DeepComparison | PPF_DeepCompareDSOsOnly)))
		{
			Fail(Item, TEXT("readback"), FString::Printf(TEXT("'%s' differs after compilation; Blueprint was not saved"), *Name));
			bValid = false;
		}
	}
	Item.Row->SetObjectField(Field, Values);
	if (FString(Field) == TEXT("values")) Item.Row->SetObjectField(TEXT("valuesText"), Texts);
	return bValid;
}
}

TSharedPtr<FJsonValue> FBlueprintHandlers::SetComponentPropertiesBatch(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPBlueprintComponentBatch;
	// Read every declared parameter before validation, including mode-specific
	// keys, so the handler-spec contract can account for the complete bag.
	const TArray<TSharedPtr<FJsonValue>>* ItemsParam = nullptr;
	const bool bItems = TryGetArrayParam(Params, TEXT("items"), ItemsParam);
	FString Directory = OptionalString(Params, TEXT("directory"));
	const FString ClassFilter = OptionalString(Params, TEXT("classFilter"));
	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	const TSharedPtr<FJsonObject>* SharedProperties = nullptr;
	const bool bProperties = TryGetObjectParam(Params, TEXT("properties"), SharedProperties);
	const bool bRecursive = OptionalBool(Params, TEXT("recursive"), true);
	const int32 MaxBlueprints = OptionalInt(Params, TEXT("maxBlueprints"), 200);
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	if (MaxBlueprints < 1 || MaxBlueprints > 2000) return MCPError(TEXT("maxBlueprints must be between 1 and 2000"));
	if (bItems == !Directory.IsEmpty()) return MCPError(TEXT("Pass items OR directory + componentName + properties"));
	if (bItems && (!ClassFilter.IsEmpty() || !ComponentName.IsEmpty() || bProperties || HasParam(Params, TEXT("recursive"))))
		return MCPError(TEXT("classFilter, componentName, properties and recursive are directory-mode parameters"));
	if (bItems && (ItemsParam->Num() == 0 || ItemsParam->Num() > 10000))
		return MCPError(TEXT("items must contain between 1 and 10000 entries"));

	UClass* FilterClass = nullptr;
	TArray<TSharedPtr<FJsonValue>> Requests;
	if (bItems) Requests = *ItemsParam;
	else
	{
		Directory.TrimStartAndEndInline();
		while (Directory.Len() > 1 && Directory.EndsWith(TEXT("/"))) Directory.LeftChopInline(1);
		if (!FPackageName::IsValidLongPackageName(Directory)) return MCPError(TEXT("directory must be a valid mount-rooted content path"));
		if (ComponentName.IsEmpty() || !bProperties || (*SharedProperties)->Values.Num() == 0)
			return MCPError(TEXT("Directory mode requires componentName and a non-empty properties object"));
		if (!ClassFilter.IsEmpty())
		{
			FilterClass = MCPResolveClassOfType(ClassFilter, AActor::StaticClass());
			if (!FilterClass) return MCPError(TEXT("Cannot resolve Blueprint actor classFilter: ") + ClassFilter);
		}
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		Registry.SearchAllAssets(true);
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*Directory));
		Filter.bRecursivePaths = bRecursive;
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);
		// Bound candidate loads, before filtering. Refuse overflow rather than
		// applying a truncated selection whose remainder a caller might miss.
		if (Assets.Num() > MaxBlueprints) return MCPError(TEXT("Directory exceeds maxBlueprints; narrow directory or raise the limit"));
		Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.GetObjectPathString() < B.GetObjectPathString(); });
		for (const FAssetData& Asset : Assets)
		{
			auto Request = MakeShared<FJsonObject>();
			Request->SetStringField(TEXT("assetPath"), Asset.GetObjectPathString());
			Request->SetStringField(TEXT("componentName"), ComponentName);
			Request->SetObjectField(TEXT("properties"), *SharedProperties);
			Requests.Add(MakeShared<FJsonValueObject>(Request));
		}
	}

	TArray<FItem> Items;
	TMap<UBlueprint*, TArray<int32>> Groups;
	TArray<UBlueprint*> Order;
	TArray<TStrongObjectPtr<UBlueprint>> KeepBlueprints;
	TSet<FString> CandidatePackages;
	for (const TSharedPtr<FJsonValue>& Value : Requests)
	{
		const int32 Index = Items.AddDefaulted();
		FItem& Item = Items[Index];
		Item.Row->SetNumberField(TEXT("index"), Index);
		Item.Row->SetBoolField(TEXT("success"), false);
		Item.Row->SetBoolField(TEXT("saved"), false);
		Item.Row->SetObjectField(TEXT("values"), MakeShared<FJsonObject>());
		const TSharedPtr<FJsonObject>* Request = nullptr;
		const TSharedPtr<FJsonObject>* Properties = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Request) || !Request)
		{
			Fail(Item, TEXT("validate"), TEXT("Each item must be an object"));
			continue;
		}
		(*Request)->TryGetStringField(TEXT("assetPath"), Item.Path);
		(*Request)->TryGetStringField(TEXT("componentName"), Item.ComponentName);
		Item.Row->SetStringField(TEXT("assetPath"), Item.Path);
		Item.Row->SetStringField(TEXT("componentName"), Item.ComponentName);
		const bool bValidProperties = (*Request)->TryGetObjectField(TEXT("properties"), Properties) && Properties && (*Properties)->Values.Num() > 0;
		if (bValidProperties) Item.Properties = *Properties;
		if (Item.Path.IsEmpty())
		{
			Fail(Item, TEXT("validate"), TEXT("Each item requires assetPath"));
			continue;
		}
		CandidatePackages.Add(FPackageName::ObjectPathToPackageName(Item.Path).ToLower());
		if (CandidatePackages.Num() > MaxBlueprints)
			return MCPError(TEXT("Explicit items exceed maxBlueprints; no Blueprint was changed"));
		Item.Blueprint = LoadBlueprint(Item.Path);
		if (!Item.Blueprint)
		{
			Fail(Item, TEXT("load"), TEXT("Blueprint not found: ") + Item.Path);
			continue;
		}
		if (!Groups.Contains(Item.Blueprint))
		{
			Order.Add(Item.Blueprint);
			KeepBlueprints.Emplace(Item.Blueprint);
		}
		Groups.FindOrAdd(Item.Blueprint).Add(Index);
		if (Item.ComponentName.IsEmpty() || !bValidProperties)
			Fail(Item, TEXT("validate"), TEXT("Each item requires componentName and a non-empty properties object"));
	}
	if (Order.Num() > MaxBlueprints) return MCPError(TEXT("Explicit items exceed maxBlueprints; no Blueprint was changed"));

	int32 Compiled = 0, Saved = 0;
	for (UBlueprint* Blueprint : Order)
	{
		const TArray<int32>& Group = Groups.FindChecked(Blueprint);
		bool bValid = true;
		TMap<UActorComponent*, UActorComponent*> ScratchBySource;
		TArray<TStrongObjectPtr<UActorComponent>> KeepScratch;
		for (int32 Index : Group)
		{
			FItem& Item = Items[Index];
			if (Item.bFailed) { bValid = false; continue; }
			if (FilterClass && (!Blueprint->GeneratedClass || !Blueprint->GeneratedClass->IsChildOf(FilterClass)))
			{
				Skip(Item, TEXT("class_filter"));
				continue;
			}
			if (auto Blocked = MCPAssetWriteBlockedError(Blueprint, Item.Path, TEXT("set these component properties")))
			{
				Fail(Item, TEXT("write_guard"), Blocked->AsObject()->GetStringField(TEXT("error")));
				bValid = false;
				continue;
			}
			bool bInherited = false;
			TArray<FString> Available;
			Item.Source = ResolveComponentTemplate(Blueprint, Item.ComponentName, false, bInherited, Available);
			Item.Row->SetBoolField(TEXT("inherited"), bInherited);
			if (!Item.Source)
			{
				// A missing component is an explicit refusal for items, and an
				// observable non-match when selecting a directory.
				if (!bItems) Skip(Item, TEXT("component_not_found_or_ambiguous"));
				else { Fail(Item, TEXT("resolve"), TEXT("Component not found or ambiguous. Available: ") + FString::Join(Available, TEXT(", "))); bValid = false; }
				continue;
			}
			UActorComponent*& Scratch = ScratchBySource.FindOrAdd(Item.Source);
			if (!Scratch)
			{
				const FName ScratchName = MakeUniqueObjectName(GetTransientPackage(), Item.Source->GetClass(), TEXT("MCPComponentBatchValidation"));
				Scratch = DuplicateObject<UActorComponent>(Item.Source, GetTransientPackage(), ScratchName);
				if (Scratch)
				{
					Scratch->ClearFlags(RF_Public | RF_Standalone);
					Scratch->SetFlags(RF_Transient);
				}
				KeepScratch.Emplace(Scratch);
			}
			if (!Scratch) { Fail(Item, TEXT("validate"), TEXT("Cannot create transient validation copy")); bValid = false; continue; }
			for (const FString& Name : PropertyNames(Item))
			{
				FString Previous, Error;
				TSharedPtr<FJsonValue> Replay;
				if (!MCPBlueprintComponentProperty::Read(Item.Source, Name, Previous, Replay, Error) ||
					!MCPBlueprintComponentProperty::Apply(Scratch, Name, Item.Properties->TryGetField(Name), Error))
				{
					Fail(Item, TEXT("validate"), Name + TEXT(": ") + Error);
					Item.Row->SetStringField(TEXT("propertyName"), Name);
					bValid = false;
					break;
				}
				Item.Previous->SetField(Name, Replay);
			}
			Item.Row->SetObjectField(TEXT("previousValues"), Item.Previous);
			if (!Item.bFailed && !ReadValues(Item, Item.Source, TEXT("values"), false)) bValid = false;
		}
		if (!bValid)
		{
			for (int32 Index : Group) if (!Items[Index].bFailed && !Items[Index].bSkipped)
				Fail(Items[Index], TEXT("preflight"), TEXT("Another item for this Blueprint failed validation; no changes were applied"));
			continue;
		}
		bool bHasTargets = false;
		for (int32 Index : Group)
		{
			FItem& Item = Items[Index];
			if (Item.bSkipped) continue;
			bHasTargets = true;
			UActorComponent* Scratch = ScratchBySource.FindChecked(Item.Source);
			Item.Proposed = Scratch;
			ReadValues(Item, Scratch, TEXT("proposedValues"), false);
			if (bDryRun)
			{
				Item.Row->SetBoolField(TEXT("success"), true);
				Item.Row->SetStringField(TEXT("stage"), TEXT("dry_run"));
			}
		}
		if (bDryRun || !bHasTargets) continue;

		Blueprint->Modify();
		for (int32 Index : Group)
		{
			FItem& Item = Items[Index];
			if (Item.bSkipped) continue;
			bool bInherited = false;
			TArray<FString> Available;
			UActorComponent* Template = ResolveComponentTemplate(Blueprint, Item.ComponentName, true, bInherited, Available);
			if (!Template) { Fail(Item, TEXT("apply"), TEXT("Cannot obtain writable component template")); bValid = false; break; }
			Template->Modify();
			Item.bTouched = true;
			for (const FString& Name : PropertyNames(Item))
			{
				FString Error;
				if (!MCPBlueprintComponentProperty::Apply(Template, Name, Item.Properties->TryGetField(Name), Error))
				{
					Fail(Item, TEXT("apply"), Name + TEXT(": ") + Error);
					bValid = false;
					break;
				}
			}
			Template->PostEditChange();
			if (!bValid) break;
		}
		if (bValid)
		{
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			++Compiled;
			if (Blueprint->Status == BS_Error)
			{
				bValid = false;
				for (int32 Index : Group) if (!Items[Index].bSkipped)
					Fail(Items[Index], TEXT("compile"), TEXT("Blueprint compilation failed; in-memory edits remain and were not saved"));
			}
		}
		// Reacquire after compile: old CDOs, property addresses and component
		// pointers may have been reinstanced by the compiler.
		for (int32 Index : Group)
		{
			FItem& Item = Items[Index];
			if (Item.bSkipped) continue;
			bool bInherited = false;
			TArray<FString> Available;
			UActorComponent* Template = ResolveComponentTemplate(Blueprint, Item.ComponentName, false, bInherited, Available);
			if (!ReadValues(Item, Template, TEXT("values"), bValid)) bValid = false;
		}
		if (bValid)
		{
			FString Error;
			const bool bSaved = SaveAssetPackageChecked(Blueprint, Error);
			if (bSaved) ++Saved;
			for (int32 Index : Group)
			{
				FItem& Item = Items[Index];
				if (Item.bSkipped) continue;
				Item.Row->SetBoolField(TEXT("saved"), bSaved);
				if (!bSaved) Fail(Item, TEXT("save"), Error);
				else
				{
					Item.Row->SetBoolField(TEXT("success"), true);
					Item.Row->SetStringField(TEXT("stage"), TEXT("complete"));
				}
			}
		}
		else for (int32 Index : Group) if (!Items[Index].bFailed && !Items[Index].bSkipped)
			Fail(Items[Index], TEXT("finalize"), TEXT("Another item for this Blueprint failed; in-memory edits may remain and were not saved"));
	}

	auto Result = MCPSuccess();
	TArray<TSharedPtr<FJsonValue>> Rows, RollbackItems;
	int32 Failed = 0, Skipped = 0;
	for (FItem& Item : Items)
	{
		Rows.Add(MakeShared<FJsonValueObject>(Item.Row));
		if (Item.bFailed) ++Failed;
		if (Item.bSkipped) ++Skipped;
		if (Item.bTouched)
		{
			auto Undo = MakeShared<FJsonObject>();
			Undo->SetStringField(TEXT("assetPath"), Item.Path);
			Undo->SetStringField(TEXT("componentName"), Item.ComponentName);
			Undo->SetObjectField(TEXT("properties"), Item.Previous);
			RollbackItems.Add(MakeShared<FJsonValueObject>(Undo));
		}
	}
	Result->SetBoolField(TEXT("success"), Failed == 0);
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetArrayField(TEXT("results"), Rows);
	Result->SetNumberField(TEXT("failed"), Failed);
	Result->SetNumberField(TEXT("skipped"), Skipped);
	Result->SetNumberField(TEXT("blueprintsCompiled"), Compiled);
	Result->SetNumberField(TEXT("blueprintsSaved"), Saved);
	if (RollbackItems.Num() > 0)
	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("items"), RollbackItems);
		Payload->SetNumberField(TEXT("maxBlueprints"), MaxBlueprints);
		MCPSetRollback(Result, TEXT("set_component_properties_batch"), Payload);
	}
	return MCPResult(Result);
}
