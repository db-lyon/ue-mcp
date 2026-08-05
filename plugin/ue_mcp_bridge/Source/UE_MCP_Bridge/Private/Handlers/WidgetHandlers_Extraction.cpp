#include "WidgetHandlers.h"

#include "AssetToolsModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/NamedSlotInterface.h"
#include "Components/PanelWidget.h"
#include "EditorAssetLibrary.h"
#include "HandlerUtils.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditorUtils.h"
#include "WidgetBlueprintFactory.h"

namespace
{
FString MakeExtractionWidgetBlueprintObjectPath(const FString& InAssetPath)
{
	FString ObjectPath = InAssetPath;
	ObjectPath.TrimStartAndEndInline();
	ObjectPath.RemoveFromStart(TEXT("WidgetBlueprint'"));
	ObjectPath.RemoveFromEnd(TEXT("'"));

	const int32 LastSlashIndex = ObjectPath.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	const int32 DotIndex = ObjectPath.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	if (DotIndex <= LastSlashIndex)
	{
		const FString AssetName = FPackageName::GetLongPackageAssetName(ObjectPath);
		if (!AssetName.IsEmpty())
		{
			ObjectPath += TEXT(".") + AssetName;
		}
	}
	return ObjectPath;
}

UWidgetBlueprint* LoadWidgetBlueprintForExtraction(const FString& AssetPath)
{
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	// A missing destination is the normal create path. StaticLoadObject on a
	// non-existent package can force a blocking package search, so only use the
	// fallback for explicit object/generated-class paths supplied by callers.
	if (!LoadedAsset && AssetPath.Contains(TEXT(".")))
	{
		const FString ObjectPath = MakeExtractionWidgetBlueprintObjectPath(AssetPath);
		LoadedAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
	}

	if (UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(LoadedAsset))
	{
		return WidgetBlueprint;
	}

	if (UClass* LoadedClass = Cast<UClass>(LoadedAsset))
	{
		return Cast<UWidgetBlueprint>(LoadedClass->ClassGeneratedBy);
	}

	return nullptr;
}

struct FExtractedWidgetPlanEntry
{
	UWidget* SourceWidget = nullptr;
	FString SourceName;
	FString DestinationName;
	FString ParentDestinationName;
	int32 ChildIndex = INDEX_NONE;
	/** Set instead of ParentDestinationName when the widget lives in a named slot. */
	FString NamedSlotHostDestinationName;
	FName NamedSlotName = NAME_None;
};

bool ParseDestinationPath(const FString& RequestedPath, FString& OutPackagePath, FString& OutAssetName, FString& OutAssetPath, FString& OutError)
{
	FString PackageName = RequestedPath;
	int32 DotIndex = INDEX_NONE;
	if (PackageName.FindChar(TEXT('.'), DotIndex))
	{
		PackageName.LeftInline(DotIndex);
	}
	PackageName.RemoveFromEnd(TEXT("/"));

	if (!FPackageName::IsValidLongPackageName(PackageName))
	{
		OutError = FString::Printf(TEXT("destinationAssetPath '%s' is not a valid long package name"), *RequestedPath);
		return false;
	}

	OutAssetName = FPackageName::GetLongPackageAssetName(PackageName);
	OutPackagePath = FPackageName::GetLongPackagePath(PackageName);
	if (OutAssetName.IsEmpty() || OutPackagePath.IsEmpty())
	{
		OutError = FString::Printf(TEXT("destinationAssetPath '%s' must include an asset name"), *RequestedPath);
		return false;
	}

	OutAssetPath = PackageName;
	return true;
}

void BuildPlan(UWidget* SourceRoot, const FString& DestinationRootName, TArray<FExtractedWidgetPlanEntry>& OutPlan)
{
	auto AddPlanEntry = [&](UWidget* Widget, const bool bIsRoot)
	{
		if (!Widget)
		{
			return;
		}

		FExtractedWidgetPlanEntry& Entry = OutPlan.AddDefaulted_GetRef();
		Entry.SourceWidget = Widget;
		Entry.SourceName = Widget->GetName();
		Entry.DestinationName = bIsRoot ? DestinationRootName : Entry.SourceName;
		if (!bIsRoot)
		{
			if (UPanelWidget* Parent = Widget->GetParent())
			{
				Entry.ParentDestinationName = Parent == SourceRoot ? DestinationRootName : Parent->GetName();
				Entry.ChildIndex = Parent->GetChildIndex(Widget);
			}
		}
	};

	AddPlanEntry(SourceRoot, true);
	UWidgetTree::ForWidgetAndChildren(SourceRoot, [&](UWidget* Widget)
	{
		if (Widget != SourceRoot)
		{
			AddPlanEntry(Widget, false);
		}
	});

	// Named slot content has no UPanelWidget parent, so record its host and
	// slot separately. Without this the plan describes it as unparented and
	// the idempotency check cannot tell one slot from another.
	for (FExtractedWidgetPlanEntry& Entry : OutPlan)
	{
		if (Entry.SourceWidget == SourceRoot || !Entry.ParentDestinationName.IsEmpty())
		{
			continue;
		}
		for (const FExtractedWidgetPlanEntry& HostEntry : OutPlan)
		{
			INamedSlotInterface* NamedSlotHost = Cast<INamedSlotInterface>(HostEntry.SourceWidget);
			if (!NamedSlotHost || HostEntry.SourceWidget == Entry.SourceWidget)
			{
				continue;
			}
			TArray<FName> SlotNames;
			NamedSlotHost->GetSlotNames(SlotNames);
			for (const FName& SlotName : SlotNames)
			{
				if (NamedSlotHost->GetContentForSlot(SlotName) == Entry.SourceWidget)
				{
					Entry.NamedSlotHostDestinationName = HostEntry.DestinationName;
					Entry.NamedSlotName = SlotName;
					break;
				}
			}
			if (!Entry.NamedSlotHostDestinationName.IsEmpty())
			{
				break;
			}
		}
	}
}

TArray<TSharedPtr<FJsonValue>> BuildMappingJson(const TArray<FExtractedWidgetPlanEntry>& Plan)
{
	TArray<TSharedPtr<FJsonValue>> Mapping;
	Mapping.Reserve(Plan.Num());
	for (const FExtractedWidgetPlanEntry& Entry : Plan)
	{
		TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("sourceName"), Entry.SourceName);
		Item->SetStringField(TEXT("destinationName"), Entry.DestinationName);
		Item->SetStringField(TEXT("class"), Entry.SourceWidget->GetClass()->GetPathName());
		if (!Entry.ParentDestinationName.IsEmpty())
		{
			Item->SetStringField(TEXT("parent"), Entry.ParentDestinationName);
			Item->SetNumberField(TEXT("childIndex"), Entry.ChildIndex);
		}
		else if (!Entry.NamedSlotHostDestinationName.IsEmpty())
		{
			Item->SetStringField(TEXT("parent"), Entry.NamedSlotHostDestinationName);
			Item->SetStringField(TEXT("namedSlot"), Entry.NamedSlotName.ToString());
		}
		Mapping.Add(MakeShared<FJsonValueObject>(Item));
	}
	return Mapping;
}

bool DestinationMatchesPlan(UWidgetBlueprint* Destination, const TArray<FExtractedWidgetPlanEntry>& Plan)
{
	if (!Destination || !Destination->WidgetTree || Plan.IsEmpty())
	{
		return false;
	}

	TArray<UWidget*> DestinationWidgets;
	Destination->WidgetTree->GetAllWidgets(DestinationWidgets);
	if (DestinationWidgets.Num() != Plan.Num() || !Destination->WidgetTree->RootWidget ||
		Destination->WidgetTree->RootWidget->GetName() != Plan[0].DestinationName)
	{
		return false;
	}

	for (const FExtractedWidgetPlanEntry& Entry : Plan)
	{
		UWidget* Existing = Destination->WidgetTree->FindWidget(FName(*Entry.DestinationName));
		if (!Existing || Existing->GetClass() != Entry.SourceWidget->GetClass())
		{
			return false;
		}

		if (!Entry.ParentDestinationName.IsEmpty())
		{
			UPanelWidget* ExistingParent = Existing->GetParent();
			if (!ExistingParent || ExistingParent->GetName() != Entry.ParentDestinationName ||
				ExistingParent->GetChildIndex(Existing) != Entry.ChildIndex)
			{
				return false;
			}
		}
		else if (!Entry.NamedSlotHostDestinationName.IsEmpty())
		{
			UWidget* ExistingHost = Destination->WidgetTree->FindWidget(FName(*Entry.NamedSlotHostDestinationName));
			INamedSlotInterface* ExistingNamedSlotHost = Cast<INamedSlotInterface>(ExistingHost);
			if (!ExistingNamedSlotHost || ExistingNamedSlotHost->GetContentForSlot(Entry.NamedSlotName) != Existing)
			{
				return false;
			}
		}
	}
	return true;
}

/** True when Candidate sits in a named slot of one of the other widgets in Set. */
bool IsNamedSlotContentOfAny(UWidget* Candidate, const TSet<UWidget*>& Set)
{
	if (!Candidate)
	{
		return false;
	}
	for (UWidget* Host : Set)
	{
		if (!Host || Host == Candidate)
		{
			continue;
		}
		if (INamedSlotInterface* NamedSlotHost = Cast<INamedSlotInterface>(Host))
		{
			if (NamedSlotHost->ContainsContent(Candidate))
			{
				return true;
			}
		}
	}
	return false;
}

UClass* ResolveDestinationParentClass(const FString& RequestedClass)
{
	if (RequestedClass.IsEmpty())
	{
		return UUserWidget::StaticClass();
	}
	if (UClass* Resolved = FindClassByShortName(RequestedClass))
	{
		return Resolved;
	}
	return LoadObject<UClass>(nullptr, *RequestedClass);
}
}

TSharedPtr<FJsonValue> FWidgetHandlers::ExtractWidgetSubtree(const TSharedPtr<FJsonObject>& Params)
{
	FString SourceAssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("sourceAssetPath"), TEXT("sourcePath"), SourceAssetPath)) return Err;

	FString SourceWidgetName;
	if (auto Err = RequireStringAlt(Params, TEXT("sourceWidgetName"), TEXT("widgetName"), SourceWidgetName)) return Err;

	FString RequestedDestinationPath;
	if (auto Err = RequireStringAlt(Params, TEXT("destinationAssetPath"), TEXT("destinationPath"), RequestedDestinationPath)) return Err;

	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), true);
	const FString RequestedParentClass = OptionalString(Params, TEXT("destinationParentClass"), TEXT("UserWidget"));

	UWidgetBlueprint* Source = LoadWidgetBlueprintForExtraction(SourceAssetPath);
	if (!Source || !Source->WidgetTree)
	{
		return MCPError(FString::Printf(TEXT("Failed to load source WidgetBlueprint at '%s'"), *SourceAssetPath));
	}

	UWidget* SourceRoot = Source->WidgetTree->FindWidget(FName(*SourceWidgetName));
	if (!SourceRoot)
	{
		return MCPError(FString::Printf(TEXT("Source widget '%s' was not found in '%s'"), *SourceWidgetName, *SourceAssetPath));
	}

	FString DestinationPackagePath;
	FString DestinationAssetName;
	FString DestinationAssetPath;
	FString PathError;
	if (!ParseDestinationPath(RequestedDestinationPath, DestinationPackagePath, DestinationAssetName, DestinationAssetPath, PathError))
	{
		return MCPError(PathError);
	}

	if (Source->GetOutermost()->GetName() == DestinationAssetPath)
	{
		return MCPError(TEXT("Source and destination WidgetBlueprints must be different assets"));
	}

	UClass* ParentClass = ResolveDestinationParentClass(RequestedParentClass);
	if (!ParentClass || !ParentClass->IsChildOf(UUserWidget::StaticClass()))
	{
		return MCPError(FString::Printf(TEXT("destinationParentClass '%s' is not a UUserWidget subclass"), *RequestedParentClass));
	}

	const FString DestinationRootName = OptionalString(Params, TEXT("destinationRootName"), SourceRoot->GetName());
	if (DestinationRootName.IsEmpty() || !FName::IsValidXName(DestinationRootName, INVALID_OBJECTNAME_CHARACTERS))
	{
		return MCPError(FString::Printf(TEXT("destinationRootName '%s' is not a valid UObject name"), *DestinationRootName));
	}

	TArray<FExtractedWidgetPlanEntry> Plan;
	BuildPlan(SourceRoot, DestinationRootName, Plan);
	if (Plan.IsEmpty())
	{
		return MCPError(TEXT("The selected source subtree is empty"));
	}

	TSet<FString> DestinationNames;
	for (const FExtractedWidgetPlanEntry& Entry : Plan)
	{
		if (DestinationNames.Contains(Entry.DestinationName))
		{
			return MCPError(FString::Printf(TEXT("Deterministic name mapping collides at '%s'"), *Entry.DestinationName));
		}
		DestinationNames.Add(Entry.DestinationName);
	}

	UWidgetBlueprint* ExistingDestination = LoadWidgetBlueprintForExtraction(DestinationAssetPath);
	if (ExistingDestination && ExistingDestination->ParentClass != ParentClass)
	{
		return MCPError(FString::Printf(TEXT("Destination parent class is '%s', expected '%s'"),
			*GetNameSafe(ExistingDestination->ParentClass), *ParentClass->GetName()));
	}

	if (ExistingDestination && ExistingDestination->WidgetTree && ExistingDestination->WidgetTree->RootWidget)
	{
		if (DestinationMatchesPlan(ExistingDestination, Plan))
		{
			auto Result = MCPSuccess();
			MCPSetExisted(Result);
			Result->SetBoolField(TEXT("dryRun"), bDryRun);
			Result->SetStringField(TEXT("sourceAssetPath"), Source->GetPathName());
			Result->SetStringField(TEXT("destinationAssetPath"), ExistingDestination->GetPathName());
			Result->SetArrayField(TEXT("nameMapping"), BuildMappingJson(Plan));
			Result->SetNumberField(TEXT("widgetCount"), Plan.Num());
			return MCPResult(Result);
		}
		return MCPError(FString::Printf(TEXT("Destination '%s' is not empty and does not exactly match the requested subtree"), *DestinationAssetPath));
	}

	if (ExistingDestination && ExistingDestination->WidgetTree)
	{
		TArray<UWidget*> ExistingWidgets;
		ExistingDestination->WidgetTree->GetAllWidgets(ExistingWidgets);
		if (!ExistingWidgets.IsEmpty())
		{
			return MCPError(FString::Printf(TEXT("Destination '%s' contains orphaned widgets; extraction requires an empty destination"), *DestinationAssetPath));
		}
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetStringField(TEXT("sourceAssetPath"), Source->GetPathName());
	Result->SetStringField(TEXT("sourceWidgetName"), SourceRoot->GetName());
	Result->SetStringField(TEXT("destinationAssetPath"), DestinationAssetPath);
	Result->SetStringField(TEXT("destinationParentClass"), ParentClass->GetPathName());
	Result->SetArrayField(TEXT("nameMapping"), BuildMappingJson(Plan));
	Result->SetNumberField(TEXT("widgetCount"), Plan.Num());
	int32 PlannedBindingCount = 0;
	for (const FDelegateEditorBinding& Binding : Source->Bindings)
	{
		const FString TargetName = Binding.ObjectName == SourceRoot->GetName()
			? DestinationRootName
			: Binding.ObjectName;
		if (DestinationNames.Contains(TargetName))
		{
			++PlannedBindingCount;
		}
	}
	Result->SetNumberField(TEXT("bindingCount"), PlannedBindingCount);

	if (bDryRun)
	{
		Result->SetStringField(TEXT("note"), TEXT("Dry run - source and destination were not modified. Re-run with dryRun=false to extract."));
		return MCPResult(Result);
	}

	FString ExportedText;
	TArray<UWidget*> WidgetsToExport;
	WidgetsToExport.Reserve(Plan.Num());
	for (const FExtractedWidgetPlanEntry& Entry : Plan)
	{
		WidgetsToExport.Add(Entry.SourceWidget);
	}
	FWidgetBlueprintEditorUtils::ExportWidgetsToText(WidgetsToExport, ExportedText);
	if (ExportedText.IsEmpty())
	{
		return MCPError(TEXT("Unreal's widget serializer produced no data for the selected subtree"));
	}

	const bool bSourceDirtyBefore = Source->GetOutermost()->IsDirty();
	bool bCreatedDestination = false;
	UWidgetBlueprint* Destination = ExistingDestination;
	if (!Destination)
	{
		UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
		Factory->ParentClass = ParentClass;
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		Destination = Cast<UWidgetBlueprint>(AssetTools.CreateAsset(
			DestinationAssetName, DestinationPackagePath, UWidgetBlueprint::StaticClass(), Factory));
		if (!Destination)
		{
			return MCPError(FString::Printf(TEXT("Failed to create destination WidgetBlueprint '%s'"), *DestinationAssetPath));
		}
		bCreatedDestination = true;
	}

	// ImportWidgetsFromText reparents every imported widget into the
	// destination's WidgetTree. A WidgetBlueprint saved without one (a broken
	// or partially authored asset) would dereference null here, so refuse it
	// with a message instead of taking the editor down.
	if (!Destination->WidgetTree)
	{
		if (bCreatedDestination)
		{
			UEditorAssetLibrary::DeleteAsset(DestinationAssetPath);
		}
		return MCPError(FString::Printf(TEXT("Destination '%s' has no widget tree to import into"), *DestinationAssetPath));
	}

	Destination->Modify();
	Destination->WidgetTree->Modify();
	TSet<UWidget*> ImportedWidgets;
	TMap<FName, UWidgetSlotPair*> PastedExtraSlotData;
	FWidgetBlueprintEditorUtils::ImportWidgetsFromText(Destination, ExportedText, ImportedWidgets, PastedExtraSlotData);
	if (ImportedWidgets.Num() != Plan.Num())
	{
		if (bCreatedDestination)
		{
			UEditorAssetLibrary::DeleteAsset(DestinationAssetPath);
		}
		return MCPError(FString::Printf(TEXT("Widget import count mismatch: expected %d, imported %d"), Plan.Num(), ImportedWidgets.Num()));
	}

	UWidget* ImportedRoot = nullptr;
	for (UWidget* Imported : ImportedWidgets)
	{
		if (Imported && Imported->GetName() == SourceRoot->GetName())
		{
			ImportedRoot = Imported;
			break;
		}
	}
	if (!ImportedRoot)
	{
		// Fall back to topology when the importer had to rename the root away
		// from its source name. GetParent() only reports a UPanelWidget
		// parent, so content sitting in a named slot also reports no parent
		// and would otherwise look like a second root: ask the named-slot
		// hosts in the imported set before treating a widget as unparented.
		for (UWidget* Imported : ImportedWidgets)
		{
			if (!Imported)
			{
				continue;
			}
			if (UWidget* ImportedParent = Imported->GetParent())
			{
				if (ImportedWidgets.Contains(ImportedParent))
				{
					continue;
				}
			}
			else if (IsNamedSlotContentOfAny(Imported, ImportedWidgets))
			{
				continue;
			}

			if (ImportedRoot)
			{
				ImportedRoot = nullptr;
				break;
			}
			ImportedRoot = Imported;
		}
	}
	if (!ImportedRoot)
	{
		if (bCreatedDestination)
		{
			UEditorAssetLibrary::DeleteAsset(DestinationAssetPath);
		}
		return MCPError(TEXT("Imported subtree root could not be identified"));
	}

	if (ImportedRoot->GetName() != DestinationRootName)
	{
		ImportedRoot->Rename(*DestinationRootName, Destination->WidgetTree, REN_DontCreateRedirectors | REN_NonTransactional);
		if (ImportedRoot->GetDisplayLabel().Equals(SourceRoot->GetName()))
		{
			ImportedRoot->SetDisplayLabel(DestinationRootName);
		}
	}
	Destination->WidgetTree->RootWidget = ImportedRoot;

	int32 CopiedBindings = 0;
	for (const FDelegateEditorBinding& SourceBinding : Source->Bindings)
	{
		FString TargetName = SourceBinding.ObjectName;
		if (TargetName == SourceRoot->GetName())
		{
			TargetName = DestinationRootName;
		}
		if (!DestinationNames.Contains(TargetName))
		{
			continue;
		}
		FDelegateEditorBinding CopiedBinding = SourceBinding;
		CopiedBinding.ObjectName = TargetName;
		Destination->Bindings.AddUnique(CopiedBinding);
		++CopiedBindings;
	}

	// WidgetVariableNameToGuidMap keeps external references stable when a
	// widget variable is later renamed. It landed in 5.5, so gate on the
	// shared macro rather than an ad-hoc version expression.
#if UE_MCP_HAS_5_5_API
	for (const FExtractedWidgetPlanEntry& Entry : Plan)
	{
		const FName WidgetName(*Entry.DestinationName);
		if (!Destination->WidgetVariableNameToGuidMap.Contains(WidgetName))
		{
			Destination->WidgetVariableNameToGuidMap.Add(WidgetName, FGuid::NewGuid());
		}
	}
#endif

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Destination);
	FKismetEditorUtilities::CompileBlueprint(Destination);
	if (Destination->Status == BS_Error)
	{
		if (bCreatedDestination)
		{
			UEditorAssetLibrary::DeleteAsset(DestinationAssetPath);
		}
		return MCPError(TEXT("Destination WidgetBlueprint failed to compile; a newly created destination was removed"));
	}

	if (!UEditorAssetLibrary::SaveAsset(Destination->GetPathName(), false))
	{
		if (bCreatedDestination)
		{
			UEditorAssetLibrary::DeleteAsset(DestinationAssetPath);
		}
		return MCPError(TEXT("Destination WidgetBlueprint compiled but could not be saved"));
	}

	if (bCreatedDestination)
	{
		MCPSetCreated(Result);
		// Only offer the destructive inverse for an asset this call authored.
		// Filling in a destination the caller already had on disk must not
		// hand FlowRunner a rollback that deletes it.
		MCPSetDeleteAssetRollback(Result, Destination->GetPathName());
	}
	else
	{
		MCPSetUpdated(Result);
	}
	Result->SetStringField(TEXT("destinationAssetPath"), Destination->GetPathName());
	Result->SetNumberField(TEXT("bindingCount"), CopiedBindings);
	Result->SetBoolField(TEXT("sourcePackageDirtyBefore"), bSourceDirtyBefore);
	Result->SetBoolField(TEXT("sourcePackageDirtyAfter"), Source->GetOutermost()->IsDirty());
	Result->SetBoolField(TEXT("sourceUnchanged"), bSourceDirtyBefore == Source->GetOutermost()->IsDirty());
	return MCPResult(Result);
}
