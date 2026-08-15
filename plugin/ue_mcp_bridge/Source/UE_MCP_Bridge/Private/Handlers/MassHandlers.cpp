#include "MassHandlers.h"

#include "HandlerAssetCreate.h"
#include "HandlerJsonProperty.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "Engine/DataAsset.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

namespace
{
	static const TCHAR* ConfigClassPath = TEXT("/Script/MassSpawner.MassEntityConfigAsset");
	static const TCHAR* TraitBaseClassPath = TEXT("/Script/MassSpawner.MassEntityTraitBase");

	UClass* ResolveClass(const FString& ClassPath)
	{
		if (ClassPath.IsEmpty()) return nullptr;
		UClass* Result = LoadClass<UObject>(nullptr, *ClassPath);
		if (!Result) Result = LoadObject<UClass>(nullptr, *ClassPath);
		if (!Result)
		{
			FString ShortName = ClassPath;
			ShortName.RemoveFromEnd(TEXT("_C"));
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (It->GetName() == ShortName || It->GetName() == FPackageName::GetShortName(ClassPath))
				{
					Result = *It;
					break;
				}
			}
		}
		return Result;
	}

	bool SplitAssetPath(const TSharedPtr<FJsonObject>& Params, FString& OutName, FString& OutPackagePath)
	{
		FString AssetPath;
		Params->TryGetStringField(TEXT("assetPath"), AssetPath);
		if (AssetPath.IsEmpty())
		{
			if (!Params->TryGetStringField(TEXT("name"), OutName) || OutName.IsEmpty()) return false;
			OutPackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game"));
			return !OutPackagePath.IsEmpty();
		}

		FString PackageName = AssetPath;
		if (AssetPath.Contains(TEXT(".")))
		{
			PackageName = AssetPath.LeftChop(AssetPath.Len() - AssetPath.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd));
		}
		OutName = FPackageName::GetShortName(PackageName);
		OutPackagePath = FPaths::GetPath(PackageName);
		return !OutName.IsEmpty() && !OutPackagePath.IsEmpty();
	}

	UObject* LoadConfigAsset(const FString& Name, const FString& PackagePath)
	{
		return LoadObject<UObject>(nullptr, *(PackagePath + TEXT("/") + Name + TEXT(".") + Name));
	}

	FProperty* FindTraitsProperty(UObject* ConfigAsset, void*& OutTraitsAddress)
	{
		OutTraitsAddress = nullptr;
		if (!ConfigAsset) return nullptr;
		FStructProperty* ConfigProperty = CastField<FStructProperty>(ConfigAsset->GetClass()->FindPropertyByName(TEXT("Config")));
		if (!ConfigProperty) return nullptr;
		void* ConfigAddress = ConfigProperty->ContainerPtrToValuePtr<void>(ConfigAsset);
		FArrayProperty* TraitsProperty = CastField<FArrayProperty>(ConfigProperty->Struct->FindPropertyByName(TEXT("Traits")));
		if (!TraitsProperty) return nullptr;
		OutTraitsAddress = TraitsProperty->ContainerPtrToValuePtr<void>(ConfigAddress);
		return TraitsProperty;
	}

	bool ReadTraitClasses(UObject* ConfigAsset, TArray<UClass*>& OutClasses, FString& OutError)
	{
		void* TraitsAddress = nullptr;
		FArrayProperty* TraitsProperty = CastField<FArrayProperty>(FindTraitsProperty(ConfigAsset, TraitsAddress));
		if (!TraitsProperty) { OutError = TEXT("Mass entity config does not expose Config.Traits"); return false; }
		FObjectProperty* TraitObjectProperty = CastField<FObjectProperty>(TraitsProperty->Inner);
		if (!TraitObjectProperty) { OutError = TEXT("Mass entity config Traits is not an object array"); return false; }

		FScriptArrayHelper Helper(TraitsProperty, TraitsAddress);
		OutClasses.Reserve(Helper.Num());
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			UObject* Trait = TraitObjectProperty->GetObjectPropertyValue(Helper.GetRawPtr(Index));
			if (!Trait) { OutError = FString::Printf(TEXT("Config.Traits[%d] is null"), Index); return false; }
			OutClasses.Add(Trait->GetClass());
		}
		return true;
	}

	bool AddTrait(UObject* ConfigAsset, UClass* TraitClass, UObject*& OutTrait, FString& OutError)
	{
		OutTrait = nullptr;
		void* TraitsAddress = nullptr;
		FArrayProperty* TraitsProperty = CastField<FArrayProperty>(FindTraitsProperty(ConfigAsset, TraitsAddress));
		if (!TraitsProperty) { OutError = TEXT("Mass entity config does not expose Config.Traits"); return false; }
		FObjectProperty* TraitObjectProperty = CastField<FObjectProperty>(TraitsProperty->Inner);
		if (!TraitObjectProperty) { OutError = TEXT("Mass entity config Traits is not an object array"); return false; }

		const FName TraitName = MakeUniqueObjectName(ConfigAsset, TraitClass, TraitClass->GetFName());
		UObject* NewTrait = NewObject<UObject>(ConfigAsset, TraitClass, TraitName, RF_Transactional);
		if (!NewTrait) { OutError = FString::Printf(TEXT("Failed to construct trait %s"), *TraitClass->GetPathName()); return false; }

		FScriptArrayHelper Helper(TraitsProperty, TraitsAddress);
		const int32 NewIndex = Helper.AddValue();
		TraitObjectProperty->SetObjectPropertyValue(Helper.GetRawPtr(NewIndex), NewTrait);
		OutTrait = NewTrait;
		return true;
	}

	UObject* GetTraitAtIndex(UObject* ConfigAsset, int32 Index)
	{
		void* TraitsAddress = nullptr;
		FArrayProperty* TraitsProperty = CastField<FArrayProperty>(FindTraitsProperty(ConfigAsset, TraitsAddress));
		FObjectProperty* TraitObjectProperty = TraitsProperty ? CastField<FObjectProperty>(TraitsProperty->Inner) : nullptr;
		if (!TraitsProperty || !TraitObjectProperty || Index < 0) return nullptr;
		FScriptArrayHelper Helper(TraitsProperty, TraitsAddress);
		return Index < Helper.Num() ? TraitObjectProperty->GetObjectPropertyValue(Helper.GetRawPtr(Index)) : nullptr;
	}

	TSharedPtr<FJsonValue> ValidateAndApplyProperties(UObject* Trait, const TSharedPtr<FJsonObject>& Properties, int32 TraitIndex, int32& OutSet)
	{
		if (!Properties.IsValid()) return nullptr;
		for (const auto& Pair : Properties->Values)
		{
			// UE 5.8 stores JSON object keys in shared string storage; copy the
			// key before passing it to the FString-based property-path helper.
			const FString PropertyName(Pair.Key);
			FString Error;
			if (!MCPJsonProperty::SetDottedPropertyFromJson(Trait, PropertyName, Pair.Value, Error))
			{
				return MCPError(FString::Printf(TEXT("Trait %d property '%s' failed: %s"), TraitIndex, *PropertyName, *Error));
			}
			++OutSet;
		}
		return nullptr;
	}

	TSharedPtr<FJsonValue> MakeExistingResult(UObject* Asset, const TArray<UClass*>& ExistingClasses, bool bUpdated, int32 PropertiesSet)
	{
		TSharedPtr<FJsonObject> Result = MCPSuccess();
		MCPSetExisted(Result);
		if (bUpdated) MCPSetUpdated(Result);
		Result->SetStringField(TEXT("assetPath"), Asset->GetPathName());
		Result->SetNumberField(TEXT("traits"), ExistingClasses.Num());
		Result->SetNumberField(TEXT("propertiesSet"), PropertiesSet);
		return MCPResult(Result);
	}

	void ReleasePreviews(TArray<UObject*>& Previews)
	{
		for (UObject* Preview : Previews)
		{
			if (Preview) Preview->RemoveFromRoot();
		}
		Previews.Reset();
	}

	FString ExportTraitProperty(const FProperty* Property, const UObject* Trait)
	{
		if (!Property || !Trait) return TEXT("");
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			if (UObject* Value = ObjectProperty->GetObjectPropertyValue_InContainer(Trait))
			{
				return Value->GetPathName();
			}
			return TEXT("None");
		}
		if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
		{
			return SoftObjectProperty->GetPropertyValue_InContainer(Trait).ToSoftObjectPath().ToString();
		}

		FString Exported;
		const void* ValueAddress = Property->ContainerPtrToValuePtr<void>(Trait);
		Property->ExportTextItem_Direct(Exported, ValueAddress, nullptr, nullptr, PPF_None);
		return Exported;
	}
}
void FMassHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("ensure_mass_entity_config"), &EnsureEntityConfig);
	Registry.RegisterHandler(TEXT("read_mass_entity_config"), &ReadEntityConfig);
}

TSharedPtr<FJsonValue> FMassHandlers::ReadEntityConfig(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	UClass* ConfigClass = ResolveClass(ConfigClassPath);
	UClass* TraitBaseClass = ResolveClass(TraitBaseClassPath);
	if (!ConfigClass || !TraitBaseClass)
	{
		return MCPError(TEXT("MassSpawner is unavailable; enable MassGameplay/MassSpawner before using read_mass_entity_config"));
	}

	UObject* Asset = LoadAssetByPath<UObject>(AssetPath);
	if (!Asset) return MCPError(FString::Printf(TEXT("Mass entity config not found: %s"), *AssetPath));
	if (!Asset->IsA(ConfigClass))
	{
		return MCPError(FString::Printf(TEXT("Asset is not a MassEntityConfigAsset: %s"), *Asset->GetPathName()));
	}

	TArray<UClass*> TraitClasses;
	FString Error;
	if (!ReadTraitClasses(Asset, TraitClasses, Error)) return MCPError(Error);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), Asset->GetPathName());
	Result->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetPathName());

	TArray<TSharedPtr<FJsonValue>> TraitClassNames;
	TArray<TSharedPtr<FJsonValue>> TraitRecords;
	for (int32 Index = 0; Index < TraitClasses.Num(); ++Index)
	{
		UObject* Trait = GetTraitAtIndex(Asset, Index);
		if (!Trait || !Trait->GetClass()->IsChildOf(TraitBaseClass))
		{
			return MCPError(FString::Printf(TEXT("Config.Traits[%d] is not a UMassEntityTraitBase"), Index));
		}

		const FString ClassPath = Trait->GetClass()->GetPathName();
		TraitClassNames.Add(MakeShared<FJsonValueString>(ClassPath));
		auto Record = MakeShared<FJsonObject>();
		Record->SetNumberField(TEXT("index"), Index);
		Record->SetStringField(TEXT("classPath"), ClassPath);
		Record->SetStringField(TEXT("className"), Trait->GetClass()->GetName());

		auto Properties = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> PropertyIt(Trait->GetClass()); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			if (!Property || Property->HasAnyPropertyFlags(CPF_Transient)) continue;
			Properties->SetStringField(Property->GetName(), ExportTraitProperty(Property, Trait));
		}
		Record->SetObjectField(TEXT("properties"), Properties);
		TraitRecords.Add(MakeShared<FJsonValueObject>(Record));
	}
	Result->SetArrayField(TEXT("traitClasses"), TraitClassNames);
	Result->SetArrayField(TEXT("traits"), TraitRecords);
	Result->SetNumberField(TEXT("traitCount"), TraitClasses.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMassHandlers::EnsureEntityConfig(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	FString PackagePath;
	if (!SplitAssetPath(Params, Name, PackagePath))
	{
		return MCPError(TEXT("Provide assetPath (/Game/Folder/Name[.Name]) or name plus packagePath"));
	}
	if (!FPackageName::IsValidLongPackageName(PackagePath))
	{
		return MCPError(FString::Printf(TEXT("Invalid package path: %s"), *PackagePath));
	}

	UClass* ConfigClass = ResolveClass(ConfigClassPath);
	UClass* TraitBaseClass = ResolveClass(TraitBaseClassPath);
	if (!ConfigClass || !TraitBaseClass)
	{
		return MCPError(TEXT("MassSpawner is unavailable; enable MassGameplay/MassSpawner before using ensure_mass_entity_config"));
	}
	if (!ConfigClass->IsChildOf(UDataAsset::StaticClass()) || !TraitBaseClass->IsChildOf(UObject::StaticClass()))
	{
		return MCPError(TEXT("MassSpawner classes are incompatible with this engine version"));
	}

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("update")).ToLower();
	if (OnConflict != TEXT("skip") && OnConflict != TEXT("error") && OnConflict != TEXT("update"))
	{
		return MCPError(TEXT("onConflict must be one of: skip, error, update"));
	}

	const TSharedPtr<FJsonValue>* TraitsValue = Params->Values.Find(TEXT("traits"));
	const TArray<TSharedPtr<FJsonValue>>* TraitArray = nullptr;
	if (!TraitsValue || !(*TraitsValue).IsValid() || !(*TraitsValue)->TryGetArray(TraitArray) || !TraitArray)
	{
		return MCPError(TEXT("Missing required 'traits' array"));
	}

	struct FRequestedTrait { UClass* Class = nullptr; const TSharedPtr<FJsonObject>* Properties = nullptr; };
	TArray<FRequestedTrait> Requested;
	TSet<UClass*> SeenClasses;
	for (int32 Index = 0; Index < TraitArray->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* TraitObject = nullptr;
		if (!(*TraitArray)[Index].IsValid() || !(*TraitArray)[Index]->TryGetObject(TraitObject) || !TraitObject || !(*TraitObject).IsValid())
		{
			return MCPError(FString::Printf(TEXT("traits[%d] must be an object"), Index));
		}
		FString ClassPath;
		if (!(*TraitObject)->TryGetStringField(TEXT("class"), ClassPath) && !(*TraitObject)->TryGetStringField(TEXT("traitClass"), ClassPath))
		{
			return MCPError(FString::Printf(TEXT("traits[%d] requires class or traitClass"), Index));
		}
		UClass* TraitClass = ResolveClass(ClassPath);
		if (!TraitClass || !TraitClass->IsChildOf(TraitBaseClass) || TraitClass->HasAnyClassFlags(CLASS_Abstract))
		{
			return MCPError(FString::Printf(TEXT("traits[%d] is not a concrete UMassEntityTraitBase: %s"), Index, *ClassPath));
		}
		if (SeenClasses.Contains(TraitClass))
		{
			return MCPError(FString::Printf(TEXT("traits[%d] duplicates trait class %s; ordered traits must be unique"), Index, *ClassPath));
		}
		SeenClasses.Add(TraitClass);
		FRequestedTrait& Request = Requested.AddDefaulted_GetRef();
		Request.Class = TraitClass;
		(*TraitObject)->TryGetObjectField(TEXT("properties"), Request.Properties);
	}

	// Validate every property against a transient trait before loading or
	// creating the real config. This is deliberately done for the whole list,
	// not while appending traits, so a bad property late in the request cannot
	// leave a partially authored asset behind.
	TArray<UObject*> Previews;
	Previews.Reserve(Requested.Num());
	for (int32 Index = 0; Index < Requested.Num(); ++Index)
	{
		const FName PreviewName = MakeUniqueObjectName(GetTransientPackage(), Requested[Index].Class, Requested[Index].Class->GetFName());
		UObject* Preview = NewObject<UObject>(GetTransientPackage(), Requested[Index].Class, PreviewName, RF_Transient);
		if (!Preview)
		{
			ReleasePreviews(Previews);
			return MCPError(FString::Printf(TEXT("Failed to construct transient preview for trait %d (%s)"), Index, *Requested[Index].Class->GetPathName()));
		}
		Preview->AddToRoot();
		Previews.Add(Preview);
		int32 IgnoredProperties = 0;
		if (TSharedPtr<FJsonValue> Error = ValidateAndApplyProperties(Preview, Requested[Index].Properties ? *Requested[Index].Properties : nullptr, Index, IgnoredProperties))
		{
			ReleasePreviews(Previews);
			return Error;
		}
	}
	ReleasePreviews(Previews);

	UObject* ExistingAsset = LoadConfigAsset(Name, PackagePath);
	if (ExistingAsset && !ExistingAsset->IsA(ConfigClass))
	{
		return MCPError(FString::Printf(TEXT("Asset exists but is not a MassEntityConfigAsset: %s"), *ExistingAsset->GetPathName()));
	}
	if (ExistingAsset && OnConflict == TEXT("error"))
	{
		return MCPError(FString::Printf(TEXT("MassEntityConfigAsset '%s' already exists"), *ExistingAsset->GetPathName()));
	}
	if (ExistingAsset && OnConflict == TEXT("skip"))
	{
		TArray<UClass*> ExistingClasses; FString Error;
		if (!ReadTraitClasses(ExistingAsset, ExistingClasses, Error)) return MCPError(Error);
		return MakeExistingResult(ExistingAsset, ExistingClasses, false, 0);
	}

	bool bCreated = false;
	if (!ExistingAsset)
	{
		auto Created = MCPCreateAssetIdempotent<UObject>(Name, PackagePath, TEXT("error"), TEXT("MassEntityConfigAsset"), ConfigClass, nullptr);
		if (Created.EarlyReturn) return Created.EarlyReturn;
		ExistingAsset = Created.Asset;
		bCreated = true;
	}

	TArray<UClass*> ExistingClasses;
	FString ReadError;
	if (!ReadTraitClasses(ExistingAsset, ExistingClasses, ReadError)) return MCPError(ReadError);
	if (ExistingClasses.Num() > Requested.Num())
	{
		return MCPError(TEXT("Existing config has more traits than requested; refusing destructive replacement"));
	}
	for (int32 Index = 0; Index < ExistingClasses.Num(); ++Index)
	{
		if (ExistingClasses[Index] != Requested[Index].Class)
		{
			return MCPError(FString::Printf(TEXT("Existing trait order conflicts at index %d; refusing destructive replacement"), Index));
		}
	}

	// For an update, duplicate the existing instanced traits into the transient
	// package as a second preflight. This catches property paths that are valid
	// on the class but invalid for the currently-authored trait instance before
	// Modify() or any real property write is issued.
	if (!bCreated)
	{
		Previews.Reset();
		for (int32 Index = 0; Index < Requested.Num(); ++Index)
		{
			UObject* SourceTrait = Index < ExistingClasses.Num() ? GetTraitAtIndex(ExistingAsset, Index) : nullptr;
			UObject* Preview = SourceTrait
				? StaticDuplicateObject(SourceTrait, GetTransientPackage(), NAME_None, RF_Transient)
				: NewObject<UObject>(GetTransientPackage(), Requested[Index].Class,
					MakeUniqueObjectName(GetTransientPackage(), Requested[Index].Class, Requested[Index].Class->GetFName()), RF_Transient);
			if (!Preview)
			{
				ReleasePreviews(Previews);
				return MCPError(FString::Printf(TEXT("Failed to construct transient update preview for trait %d"), Index));
			}
			Preview->AddToRoot();
			Previews.Add(Preview);
			int32 IgnoredProperties = 0;
			if (TSharedPtr<FJsonValue> Error = ValidateAndApplyProperties(Preview, Requested[Index].Properties ? *Requested[Index].Properties : nullptr, Index, IgnoredProperties))
			{
				ReleasePreviews(Previews);
				return Error;
			}
		}
		ReleasePreviews(Previews);
	}

	// Establish the transaction before any instanced trait is appended or
	// mutated. This is required for editor undo/redo and keeps the mutation
	// boundary explicit for callers using onConflict=update.
	ExistingAsset->Modify();
	int32 PropertiesSet = 0;
	for (int32 Index = 0; Index < Requested.Num(); ++Index)
	{
		UObject* Trait = nullptr;
		if (Index < ExistingClasses.Num()) Trait = GetTraitAtIndex(ExistingAsset, Index);
		else if (!AddTrait(ExistingAsset, Requested[Index].Class, Trait, ReadError))
		{
			return MCPError(ReadError);
		}
		if (TSharedPtr<FJsonValue> Error = ValidateAndApplyProperties(Trait, Requested[Index].Properties ? *Requested[Index].Properties : nullptr, Index, PropertiesSet))
		{
			return Error;
		}
	}

	ExistingAsset->MarkPackageDirty();
	if (!SaveAssetPackage(ExistingAsset))
	{
		return MCPError(FString::Printf(TEXT("Failed to save MassEntityConfigAsset: %s"), *ExistingAsset->GetPathName()));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	if (bCreated) MCPSetCreated(Result); else { MCPSetExisted(Result); MCPSetUpdated(Result); }
	Result->SetStringField(TEXT("assetPath"), ExistingAsset->GetPathName());
	Result->SetNumberField(TEXT("traits"), Requested.Num());
	Result->SetNumberField(TEXT("propertiesSet"), PropertiesSet);
	if (bCreated) MCPSetDeleteAssetRollback(Result, ExistingAsset->GetPathName());
	return MCPResult(Result);
}
