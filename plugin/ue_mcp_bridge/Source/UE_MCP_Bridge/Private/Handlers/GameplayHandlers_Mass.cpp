// Mass Entity trait-list CRUD and Zone Graph lane queries (T18).
//
// AUDIT, before anything here was written. What already shipped:
//
//   ensure_mass_entity_config  MassHandlers.cpp:194 - creates the
//                              MassEntityConfigAsset and authors its ordered
//                              trait list, with per-trait property writes.
//   read_mass_entity_config    MassHandlers.cpp:195 - reads the traits back.
//
// So "entity config creation" and "trait add/update/read" were already done.
// Config LISTING is also already done: a MassEntityConfigAsset is an ordinary
// asset, so asset(list) / asset(search) with the class name finds them, and
// Config.Parent (config inheritance) is a plain UPROPERTY that
// asset(set_property, propertyName="Config.Parent") writes.
//
// What was genuinely missing, and is what this file adds:
//
//   * Trait REMOVAL and REORDER. ensure_mass_entity_config refuses to shrink
//     or reorder on purpose ("refusing destructive replacement"), and no
//     generic remove-array-element action exists, so once a trait was on a
//     config there was no way to take it off. asset(set_property) cannot help:
//     Config.Traits is an Instanced TArray and a property write can neither
//     mint nor destroy the subobjects it holds.
//   * Trait and processor type DISCOVERY. ensure_mass_entity_config takes a
//     trait class path and there was no way to learn which ones exist.
//   * Structural VALIDATION of a config, including the parent chain.
//   * Zone Graph LANE QUERIES. The built graph lives in
//     AZoneGraphData::ZoneStorage as a set of parallel index arrays: a lane
//     names a half-open range into LanePoints and another into LaneLinks. A
//     raw property dump of that struct is technically reachable through
//     editor(get_property) and is useless, because answering "which lane is
//     nearest this point" or "what does lane 7 connect to" means resolving the
//     indirection. That resolution is what query_zone_graph does.
//
// NOT built here, deliberately, because a property write or an existing action
// already reaches it:
//
//   * Zone shape authoring. AZoneShape is spawned by level(spawn_actor) with
//     the class path, its Points array is an EditAnywhere BlueprintReadWrite
//     UPROPERTY on UZoneShapeComponent that editor(set_property) writes, and
//     SetShapeType / SetTags / SetReverseLaneProfile / SetPolygonRoutingType
//     are BlueprintCallable, so editor(invoke_object_function) calls them.
//   * Trait property tuning. ensure_mass_entity_config already writes trait
//     properties, and read_mass_entity_config reports them.
//   * Processor tuning. ProcessingPhase, ExecutionOrder, ExecutionFlags and
//     the rest are config UPROPERTYs on the processor CDO; list_mass_types
//     returns each processor's CDO objectPath for editor(set_property).
//
// MassGameplay, MassEntity and ZoneGraph are all reached BY REFLECTION and are
// not Build.cs dependencies. None of the three is enabled in the test project,
// and a hard dependency would take the whole bridge down wherever they are
// off. Every entry point resolves its classes first and fails with a message
// naming the plugin to enable.

#include "GameplayHandlers.h"

#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Math/NumericLimits.h"
#include "Math/UnrealMathUtility.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

namespace MassZoneGraph_Internal
{
	static const TCHAR* MassConfigClassPath    = TEXT("/Script/MassSpawner.MassEntityConfigAsset");
	static const TCHAR* MassTraitBaseClassPath = TEXT("/Script/MassSpawner.MassEntityTraitBase");
	static const TCHAR* MassProcessorClassPath = TEXT("/Script/MassEntity.MassProcessor");
	static const TCHAR* ZoneGraphDataClassPath = TEXT("/Script/ZoneGraph.ZoneGraphData");
	static const TCHAR* ZoneShapeClassPath     = TEXT("/Script/ZoneGraph.ZoneShape");
	static const TCHAR* ZoneGraphSettingsPath  = TEXT("/Script/ZoneGraph.ZoneGraphSettings");

	/** Resolve a /Script class path without linking the module that owns it.
	 *  Returns null when the plugin providing it is not enabled or loaded,
	 *  which is the answer every caller here turns into a named error. */
	static UClass* ResolveOptionalClass(const TCHAR* ClassPath)
	{
		if (UClass* Found = FindObject<UClass>(nullptr, ClassPath))
		{
			return Found;
		}
		return LoadObject<UClass>(nullptr, ClassPath);
	}

	/** True for the skeleton, reinstanced and trashed classes that
	 *  TObjectIterator sees but nobody can author against. */
	static bool IsUsableConcreteClass(const UClass* Class)
	{
		if (!Class) return false;
		if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) return false;
		const FString Name = Class->GetName();
		return !Name.StartsWith(TEXT("SKEL_"))
			&& !Name.StartsWith(TEXT("REINST_"))
			&& !Name.StartsWith(TEXT("TRASHCLASS_"))
			&& !Name.Contains(TEXT("HOTRELOADED_"));
	}

	/** The module a native class came from, as "/Script/MassSpawner". */
	static FString OwningModuleName(const UClass* Class)
	{
		const UPackage* Package = Class ? Class->GetOutermost() : nullptr;
		return Package ? Package->GetName() : FString();
	}

	/** Locate FMassEntityConfig::Traits on a config asset.
	 *  Returns the array property and the address of the live array, or null
	 *  with OutError set. Deliberately named differently from the helper in
	 *  MassHandlers.cpp: this module is a unity build, so two anonymous-
	 *  namespace helpers sharing a name in one blob is a redefinition. */
	static FArrayProperty* FindConfigTraitsArray(UObject* ConfigAsset, void*& OutTraitsAddr, void*& OutConfigAddr, FString& OutError)
	{
		OutTraitsAddr = nullptr;
		OutConfigAddr = nullptr;
		if (!ConfigAsset) { OutError = TEXT("no config asset"); return nullptr; }

		FStructProperty* ConfigProperty = CastField<FStructProperty>(ConfigAsset->GetClass()->FindPropertyByName(TEXT("Config")));
		if (!ConfigProperty)
		{
			OutError = TEXT("Mass entity config does not expose a Config struct");
			return nullptr;
		}
		OutConfigAddr = ConfigProperty->ContainerPtrToValuePtr<void>(ConfigAsset);

		FArrayProperty* TraitsProperty = CastField<FArrayProperty>(ConfigProperty->Struct->FindPropertyByName(TEXT("Traits")));
		if (!TraitsProperty)
		{
			OutError = TEXT("Mass entity config does not expose Config.Traits");
			return nullptr;
		}
		if (!CastField<FObjectProperty>(TraitsProperty->Inner))
		{
			OutError = TEXT("Mass entity config Config.Traits is not an object array");
			return nullptr;
		}
		OutTraitsAddr = TraitsProperty->ContainerPtrToValuePtr<void>(OutConfigAddr);
		return TraitsProperty;
	}

	/** The config asset a Config struct points at as its Parent, or null. */
	static UObject* ReadConfigParent(UObject* ConfigAsset)
	{
		if (!ConfigAsset) return nullptr;
		FStructProperty* ConfigProperty = CastField<FStructProperty>(ConfigAsset->GetClass()->FindPropertyByName(TEXT("Config")));
		if (!ConfigProperty) return nullptr;
		const void* ConfigAddr = ConfigProperty->ContainerPtrToValuePtr<void>(ConfigAsset);
		FObjectPropertyBase* ParentProperty = CastField<FObjectPropertyBase>(ConfigProperty->Struct->FindPropertyByName(TEXT("Parent")));
		if (!ParentProperty) return nullptr;
		return ParentProperty->GetObjectPropertyValue(ParentProperty->ContainerPtrToValuePtr<void>(ConfigAddr));
	}

	/** Every live trait object on one config, in order. Entries may be null:
	 *  a null trait is a real problem the validator reports rather than a
	 *  reason to fail the read. */
	static bool ReadConfigTraitObjects(UObject* ConfigAsset, TArray<UObject*>& OutTraits, FString& OutError)
	{
		void* TraitsAddr = nullptr;
		void* ConfigAddr = nullptr;
		FArrayProperty* TraitsProperty = FindConfigTraitsArray(ConfigAsset, TraitsAddr, ConfigAddr, OutError);
		if (!TraitsProperty) return false;

		FObjectProperty* Inner = CastField<FObjectProperty>(TraitsProperty->Inner);
		FScriptArrayHelper Helper(TraitsProperty, TraitsAddr);
		OutTraits.Reset(Helper.Num());
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			OutTraits.Add(Inner->GetObjectPropertyValue(Helper.GetRawPtr(Index)));
		}
		return true;
	}

	/** One trait as a rollback-ready {class, properties} spec: exactly the
	 *  shape ensure_mass_entity_config's traits[] takes, with every value in
	 *  UE export-text form, which the JSON property setter re-imports. */
	static TSharedPtr<FJsonObject> TraitToEnsureSpec(UObject* Trait)
	{
		TSharedPtr<FJsonObject> Spec = MakeShared<FJsonObject>();
		if (!Trait) return Spec;
		Spec->SetStringField(TEXT("class"), Trait->GetClass()->GetPathName());

		TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Trait->GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property) continue;
			if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient)) continue;
			// Only WRITABLE editable fields belong in a rollback spec. A
			// VisibleAnywhere or EditConst field would be replayed into
			// ensure_mass_entity_config, which would reject it and turn the
			// undo into a second failure.
			if (!Property->HasAnyPropertyFlags(CPF_Edit)) continue;
			if (Property->HasAnyPropertyFlags(CPF_EditConst)) continue;
			if (MCPPropertyIsFixedArray(Property)) continue;
			Properties->SetField(Property->GetName(), MCPExportPropertyValue(Property, Trait));
		}
		Spec->SetObjectField(TEXT("properties"), Properties);
		return Spec;
	}

	/** Read an int-typed UPROPERTY off a struct element by name. */
	static int32 StructInt(const UScriptStruct* Struct, const void* Elem, const TCHAR* Name, int32 Fallback = 0)
	{
		if (!Struct || !Elem) return Fallback;
		const FNumericProperty* Numeric = CastField<FNumericProperty>(Struct->FindPropertyByName(FName(Name)));
		if (!Numeric || !Numeric->IsInteger()) return Fallback;
		return static_cast<int32>(Numeric->GetSignedIntPropertyValue_InContainer(Elem));
	}

	/** Read a float/double UPROPERTY off a struct element by name. */
	static double StructFloat(const UScriptStruct* Struct, const void* Elem, const TCHAR* Name, double Fallback = 0.0)
	{
		if (!Struct || !Elem) return Fallback;
		const FNumericProperty* Numeric = CastField<FNumericProperty>(Struct->FindPropertyByName(FName(Name)));
		if (!Numeric) return Fallback;
		// There is no GetFloatingPointPropertyValue_InContainer, only the
		// signed/unsigned integer pair has one, so resolve the element address
		// first rather than assuming the symmetry.
		const void* ValueAddr = Numeric->ContainerPtrToValuePtr<void>(Elem);
		if (Numeric->IsFloatingPoint()) return Numeric->GetFloatingPointPropertyValue(ValueAddr);
		if (Numeric->IsInteger()) return static_cast<double>(Numeric->GetSignedIntPropertyValue(ValueAddr));
		return Fallback;
	}

	/** Read the uint32 payload of a nested FZoneGraphTagMask field. */
	static uint32 StructTagMask(const UScriptStruct* Struct, const void* Elem, const TCHAR* Name)
	{
		if (!Struct || !Elem) return 0;
		const FStructProperty* MaskProperty = CastField<FStructProperty>(Struct->FindPropertyByName(FName(Name)));
		if (!MaskProperty) return 0;
		const void* MaskAddr = MaskProperty->ContainerPtrToValuePtr<void>(Elem);
		const FNumericProperty* Bits = CastField<FNumericProperty>(MaskProperty->Struct->FindPropertyByName(TEXT("Mask")));
		if (!Bits || !Bits->IsInteger()) return 0;
		return static_cast<uint32>(Bits->GetUnsignedIntPropertyValue_InContainer(MaskAddr));
	}

	/** Bit index -> tag name, read off the ZoneGraphSettings CDO. Tags is a
	 *  C-style fixed array of FZoneGraphTagInfo (ArrayDim == MaxTags), so each
	 *  entry is one element of ONE FProperty, not a separate property. */
	static void ReadZoneGraphTagNames(TMap<uint8, FString>& OutNames)
	{
		UClass* SettingsClass = ResolveOptionalClass(ZoneGraphSettingsPath);
		if (!SettingsClass) return;
		const UObject* Settings = SettingsClass->GetDefaultObject();
		if (!Settings) return;

		const FProperty* TagsProperty = SettingsClass->FindPropertyByName(TEXT("Tags"));
		const FStructProperty* TagsStruct = CastField<FStructProperty>(TagsProperty);
		if (!TagsStruct) return;

		for (int32 Index = 0; Index < TagsStruct->ArrayDim; ++Index)
		{
			const void* Info = TagsStruct->ContainerPtrToValuePtr<void>(Settings, Index);
			const FNameProperty* NameProperty = CastField<FNameProperty>(TagsStruct->Struct->FindPropertyByName(TEXT("Name")));
			const FStructProperty* TagProperty = CastField<FStructProperty>(TagsStruct->Struct->FindPropertyByName(TEXT("Tag")));
			if (!NameProperty || !TagProperty) continue;

			const FName TagName = NameProperty->GetPropertyValue_InContainer(Info);
			if (TagName.IsNone()) continue;

			const void* TagAddr = TagProperty->ContainerPtrToValuePtr<void>(Info);
			const FNumericProperty* BitProperty = CastField<FNumericProperty>(TagProperty->Struct->FindPropertyByName(TEXT("Bit")));
			if (!BitProperty || !BitProperty->IsInteger()) continue;
			const uint8 Bit = static_cast<uint8>(BitProperty->GetUnsignedIntPropertyValue_InContainer(TagAddr));
			if (Bit >= 32) continue;
			OutNames.Add(Bit, TagName.ToString());
		}
	}

	/** A tag mask expanded into names, falling back to "Tag<bit>" for a bit
	 *  the project settings never named. */
	static TArray<TSharedPtr<FJsonValue>> TagMaskToJson(uint32 Mask, const TMap<uint8, FString>& TagNames)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (uint8 Bit = 0; Bit < 32; ++Bit)
		{
			if ((Mask & (uint32(1) << Bit)) == 0) continue;
			const FString* Named = TagNames.Find(Bit);
			Out.Add(MakeShared<FJsonValueString>(Named ? *Named : FString::Printf(TEXT("Tag%u"), static_cast<uint32>(Bit))));
		}
		return Out;
	}

	/** Every reflected piece of one AZoneGraphData's built graph, resolved out
	 *  of the parallel arrays into something a caller can act on. */
	struct FZoneGraphView
	{
		AActor* Actor = nullptr;
		FTransform ActorTransform = FTransform::Identity;

		FArrayProperty* LanesProperty = nullptr;
		void*           LanesAddr = nullptr;
		FArrayProperty* ZonesProperty = nullptr;
		void*           ZonesAddr = nullptr;
		FArrayProperty* PointsProperty = nullptr;
		void*           PointsAddr = nullptr;
		FArrayProperty* ProgressionsProperty = nullptr;
		void*           ProgressionsAddr = nullptr;
		FArrayProperty* LinksProperty = nullptr;
		void*           LinksAddr = nullptr;

		int32 LaneCount = 0;
		int32 ZoneCount = 0;
		int32 PointCount = 0;

		bool IsValid() const { return LanesProperty != nullptr && PointsProperty != nullptr; }

		FVector PointAt(int32 Index) const
		{
			if (!PointsProperty || Index < 0) return FVector::ZeroVector;
			FScriptArrayHelper Helper(PointsProperty, PointsAddr);
			if (!Helper.IsValidIndex(Index)) return FVector::ZeroVector;
			const FVector Local = *reinterpret_cast<const FVector*>(Helper.GetRawPtr(Index));
			return ActorTransform.TransformPosition(Local);
		}

		double ProgressionAt(int32 Index) const
		{
			if (!ProgressionsProperty || Index < 0) return 0.0;
			FScriptArrayHelper Helper(ProgressionsProperty, ProgressionsAddr);
			if (!Helper.IsValidIndex(Index)) return 0.0;
			const FNumericProperty* Inner = CastField<FNumericProperty>(ProgressionsProperty->Inner);
			return Inner ? Inner->GetFloatingPointPropertyValue(Helper.GetRawPtr(Index)) : 0.0;
		}
	};

	/** Open a view onto one data actor's ZoneStorage, or leave it invalid. */
	static FZoneGraphView OpenZoneGraphView(AActor* DataActor)
	{
		FZoneGraphView View;
		if (!DataActor) return View;
		View.Actor = DataActor;
		View.ActorTransform = DataActor->GetActorTransform();

		FStructProperty* StorageProperty = CastField<FStructProperty>(DataActor->GetClass()->FindPropertyByName(TEXT("ZoneStorage")));
		if (!StorageProperty) return View;
		void* StorageAddr = StorageProperty->ContainerPtrToValuePtr<void>(DataActor);
		const UScriptStruct* Storage = StorageProperty->Struct;

		auto BindArray = [Storage, StorageAddr](const TCHAR* Name, FArrayProperty*& OutProperty, void*& OutAddr, int32& OutNum)
		{
			OutProperty = CastField<FArrayProperty>(Storage->FindPropertyByName(FName(Name)));
			OutAddr = OutProperty ? OutProperty->ContainerPtrToValuePtr<void>(StorageAddr) : nullptr;
			OutNum = 0;
			if (OutProperty && OutAddr)
			{
				FScriptArrayHelper Helper(OutProperty, OutAddr);
				OutNum = Helper.Num();
			}
		};

		int32 Ignored = 0;
		BindArray(TEXT("Lanes"), View.LanesProperty, View.LanesAddr, View.LaneCount);
		BindArray(TEXT("Zones"), View.ZonesProperty, View.ZonesAddr, View.ZoneCount);
		BindArray(TEXT("LanePoints"), View.PointsProperty, View.PointsAddr, View.PointCount);
		BindArray(TEXT("LanePointProgressions"), View.ProgressionsProperty, View.ProgressionsAddr, Ignored);
		BindArray(TEXT("LaneLinks"), View.LinksProperty, View.LinksAddr, Ignored);

		// The point array must really be FVector before any element is read as
		// one, or a layout change in a future engine would be read as garbage
		// rather than reported.
		if (View.PointsProperty)
		{
			const FStructProperty* Inner = CastField<FStructProperty>(View.PointsProperty->Inner);
			if (!Inner || Inner->Struct != TBaseStructure<FVector>::Get())
			{
				View.PointsProperty = nullptr;
				View.PointCount = 0;
			}
		}
		return View;
	}

	/** One lane, with its point range and link range already resolved. */
	static TSharedPtr<FJsonObject> DescribeLane(
		const FZoneGraphView& View,
		int32 LaneIndex,
		const TMap<uint8, FString>& TagNames,
		bool bIncludePoints)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		if (!View.IsValid()) return Out;
		FScriptArrayHelper Lanes(View.LanesProperty, View.LanesAddr);
		if (!Lanes.IsValidIndex(LaneIndex)) return Out;

		const FStructProperty* LaneInner = CastField<FStructProperty>(View.LanesProperty->Inner);
		if (!LaneInner) return Out;
		const UScriptStruct* LaneStruct = LaneInner->Struct;
		const void* Lane = Lanes.GetRawPtr(LaneIndex);

		const int32 PointsBegin = StructInt(LaneStruct, Lane, TEXT("PointsBegin"));
		const int32 PointsEnd   = StructInt(LaneStruct, Lane, TEXT("PointsEnd"));
		const int32 LinksBegin  = StructInt(LaneStruct, Lane, TEXT("LinksBegin"));
		const int32 LinksEnd    = StructInt(LaneStruct, Lane, TEXT("LinksEnd"));

		Out->SetNumberField(TEXT("laneIndex"), LaneIndex);
		Out->SetNumberField(TEXT("zoneIndex"), StructInt(LaneStruct, Lane, TEXT("ZoneIndex")));
		Out->SetNumberField(TEXT("width"), StructFloat(LaneStruct, Lane, TEXT("Width")));
		Out->SetNumberField(TEXT("pointCount"), FMath::Max(0, PointsEnd - PointsBegin));
		Out->SetNumberField(TEXT("linkCount"), FMath::Max(0, LinksEnd - LinksBegin));
		Out->SetArrayField(TEXT("tags"), TagMaskToJson(StructTagMask(LaneStruct, Lane, TEXT("Tags")), TagNames));

		// Lane length comes from the progression array, which stores the
		// running distance at each point, so the last minus the first is the
		// polyline length without re-measuring it.
		if (PointsEnd > PointsBegin)
		{
			Out->SetNumberField(TEXT("length"), View.ProgressionAt(PointsEnd - 1) - View.ProgressionAt(PointsBegin));
			Out->SetObjectField(TEXT("startPoint"), MCPVec3ToJsonObject(View.PointAt(PointsBegin)));
			Out->SetObjectField(TEXT("endPoint"), MCPVec3ToJsonObject(View.PointAt(PointsEnd - 1)));
		}
		else
		{
			Out->SetNumberField(TEXT("length"), 0.0);
		}

		if (bIncludePoints && PointsEnd > PointsBegin)
		{
			TArray<TSharedPtr<FJsonValue>> Points;
			Points.Reserve(PointsEnd - PointsBegin);
			for (int32 Index = PointsBegin; Index < PointsEnd; ++Index)
			{
				Points.Add(MakeShared<FJsonValueObject>(MCPVec3ToJsonObject(View.PointAt(Index))));
			}
			Out->SetArrayField(TEXT("points"), Points);
		}

		if (bIncludePoints && View.LinksProperty && LinksEnd > LinksBegin)
		{
			FScriptArrayHelper Links(View.LinksProperty, View.LinksAddr);
			const FStructProperty* LinkInner = CastField<FStructProperty>(View.LinksProperty->Inner);
			TArray<TSharedPtr<FJsonValue>> LinkArray;
			for (int32 Index = LinksBegin; LinkInner && Index < LinksEnd && Links.IsValidIndex(Index); ++Index)
			{
				const void* Link = Links.GetRawPtr(Index);
				TSharedPtr<FJsonObject> LinkObj = MakeShared<FJsonObject>();
				LinkObj->SetNumberField(TEXT("destLaneIndex"), StructInt(LinkInner->Struct, Link, TEXT("DestLaneIndex")));
				if (const FProperty* TypeProperty = LinkInner->Struct->FindPropertyByName(TEXT("Type")))
				{
					LinkObj->SetField(TEXT("type"), MCPExportPropertyValue(TypeProperty, Link));
				}
				LinkObj->SetNumberField(TEXT("flags"), StructInt(LinkInner->Struct, Link, TEXT("Flags")));
				LinkArray.Add(MakeShared<FJsonValueObject>(LinkObj));
			}
			Out->SetArrayField(TEXT("links"), LinkArray);
		}
		return Out;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// list_mass_types - what ensure_mass_entity_config's traits[].class accepts
// ─────────────────────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::ListMassTypes(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MassZoneGraph_Internal;

	const FString Kind = OptionalString(Params, TEXT("kind"), TEXT("all")).ToLower();
	if (Kind != TEXT("all") && Kind != TEXT("traits") && Kind != TEXT("processors"))
	{
		return MCPError(FString::Printf(
			TEXT("unknown kind '%s'; valid: all, traits, processors"), *Kind));
	}

	const FString Filter = OptionalString(Params, TEXT("filter"));
	const int32 Limit = FMath::Clamp(OptionalInt(Params, TEXT("limit"), 200), 1, 2000);

	UClass* TraitBase = ResolveOptionalClass(MassTraitBaseClassPath);
	UClass* ProcessorBase = ResolveOptionalClass(MassProcessorClassPath);
	if (!TraitBase && !ProcessorBase)
	{
		return MCPError(TEXT("Mass is unavailable: neither /Script/MassSpawner.MassEntityTraitBase nor /Script/MassEntity.MassProcessor resolved. Enable the MassGameplay and MassEntity plugins, restart the editor, then retry."));
	}

	const bool bWantTraits = (Kind == TEXT("all") || Kind == TEXT("traits"));
	const bool bWantProcessors = (Kind == TEXT("all") || Kind == TEXT("processors"));

	TArray<TSharedPtr<FJsonValue>> Traits;
	TArray<TSharedPtr<FJsonValue>> Processors;
	TArray<FString> Unavailable;
	if (bWantTraits && !TraitBase) Unavailable.Add(TEXT("MassSpawner (traits): enable the MassGameplay plugin"));
	if (bWantProcessors && !ProcessorBase) Unavailable.Add(TEXT("MassEntity (processors): enable the MassEntity plugin"));

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!IsUsableConcreteClass(Class)) continue;

		const FString ClassName = Class->GetName();
		if (!Filter.IsEmpty() && !ClassName.Contains(Filter, ESearchCase::IgnoreCase)) continue;

		const bool bIsTrait = bWantTraits && TraitBase && Class->IsChildOf(TraitBase);
		const bool bIsProcessor = bWantProcessors && ProcessorBase && Class->IsChildOf(ProcessorBase);
		if (!bIsTrait && !bIsProcessor) continue;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("className"), ClassName);
		Entry->SetStringField(TEXT("classPath"), Class->GetPathName());
		Entry->SetStringField(TEXT("module"), OwningModuleName(Class));
		Entry->SetStringField(TEXT("parentClass"), Class->GetSuperClass() ? Class->GetSuperClass()->GetName() : FString());

		// The CDO's objectPath is the target for editor(set_property): every
		// tunable on a processor is a config UPROPERTY on its default object,
		// so no typed setter is shipped for any of them.
		if (const UObject* CDO = Class->GetDefaultObject())
		{
			Entry->SetStringField(TEXT("objectPath"), CDO->GetPathName());
			auto Report = [&Entry, CDO, Class](const TCHAR* PropertyName, const TCHAR* Field)
			{
				if (const FProperty* Property = Class->FindPropertyByName(FName(PropertyName)))
				{
					Entry->SetField(Field, MCPExportPropertyValue(Property, CDO));
				}
			};
			if (bIsTrait)
			{
				Report(TEXT("ValidTargetConfig"), TEXT("validTargetConfig"));
			}
			else
			{
				Report(TEXT("ProcessingPhase"), TEXT("processingPhase"));
				Report(TEXT("ExecutionFlags"), TEXT("executionFlags"));
				Report(TEXT("bAutoRegisterWithProcessingPhases"), TEXT("autoRegister"));
				Report(TEXT("ExecutionOrder"), TEXT("executionOrder"));
			}
		}

		if (bIsTrait)
		{
			if (Traits.Num() < Limit) Traits.Add(MakeShared<FJsonValueObject>(Entry));
		}
		else if (Processors.Num() < Limit)
		{
			Processors.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("kind"), Kind);
	if (bWantTraits)
	{
		Result->SetArrayField(TEXT("traits"), Traits);
		Result->SetNumberField(TEXT("traitCount"), Traits.Num());
	}
	if (bWantProcessors)
	{
		Result->SetArrayField(TEXT("processors"), Processors);
		Result->SetNumberField(TEXT("processorCount"), Processors.Num());
	}
	if (Unavailable.Num() > 0)
	{
		Result->SetArrayField(TEXT("unavailable"), MCPStringListToJson(Unavailable));
	}
	Result->SetNumberField(TEXT("limit"), Limit);
	// Native classes only exist in the reflection database once their module is
	// loaded. Saying so is the difference between "this project has no traits"
	// and "the plugin providing them has not been pulled in yet".
	Result->SetStringField(TEXT("note"),
		TEXT("Enumerates LOADED classes only. A Mass module that nothing has referenced yet contributes nothing here. Tune any listed type through editor(set_property) at its objectPath; there are no typed setters."));
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// remove_mass_trait - the half of trait CRUD ensure_mass_entity_config refuses
// ─────────────────────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::RemoveMassTrait(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MassZoneGraph_Internal;

	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	UClass* ConfigClass = ResolveOptionalClass(MassConfigClassPath);
	UClass* TraitBase = ResolveOptionalClass(MassTraitBaseClassPath);
	if (!ConfigClass || !TraitBase)
	{
		return MCPError(TEXT("MassSpawner is unavailable; enable MassGameplay/MassSpawner before using remove_mass_trait"));
	}

	UObject* Asset = LoadAssetByPath<UObject>(AssetPath);
	if (!Asset) return MCPError(FString::Printf(TEXT("Mass entity config not found: %s"), *AssetPath));
	if (!Asset->IsA(ConfigClass))
	{
		return MCPError(FString::Printf(TEXT("Asset is not a MassEntityConfigAsset: %s"), *Asset->GetPathName()));
	}

	const FString TraitClassSpec = OptionalString(Params, TEXT("traitClass"));
	const bool bHasIndex = Params->HasField(TEXT("index"));
	if (TraitClassSpec.IsEmpty() && !bHasIndex)
	{
		return MCPError(TEXT("Provide traitClass (idempotent: removes the first trait of that class) or index (positional: removes whatever currently sits there)"));
	}

	FString Error;
	TArray<UObject*> Traits;
	if (!ReadConfigTraitObjects(Asset, Traits, Error)) return MCPError(Error);

	// Capture the whole prior list before anything changes, so the rollback
	// payload can restore it exactly rather than approximately.
	TArray<TSharedPtr<FJsonValue>> PriorSpecs;
	TArray<FString> PriorClassNames;
	PriorSpecs.Reserve(Traits.Num());
	for (UObject* Trait : Traits)
	{
		PriorSpecs.Add(MakeShared<FJsonValueObject>(TraitToEnsureSpec(Trait)));
		PriorClassNames.Add(Trait ? Trait->GetClass()->GetPathName() : FString(TEXT("None")));
	}

	int32 TargetIndex = INDEX_NONE;
	if (!TraitClassSpec.IsEmpty())
	{
		UClass* Wanted = MCPResolveClassOfType(TraitClassSpec, TraitBase);
		if (!Wanted)
		{
			return MCPError(FString::Printf(
				TEXT("traitClass '%s' is not a UMassEntityTraitBase subclass. List the ones this editor has loaded with gameplay(list_mass_types, kind=\"traits\")."),
				*TraitClassSpec));
		}
		for (int32 Index = 0; Index < Traits.Num(); ++Index)
		{
			if (Traits[Index] && Traits[Index]->GetClass()->IsChildOf(Wanted))
			{
				TargetIndex = Index;
				break;
			}
		}
	}
	else
	{
		const int32 Requested = OptionalInt(Params, TEXT("index"), -1);
		if (Requested < 0)
		{
			return MCPError(FString::Printf(TEXT("index must be 0 or greater; got %d"), Requested));
		}
		if (Requested < Traits.Num()) TargetIndex = Requested;
	}

	if (TargetIndex == INDEX_NONE)
	{
		// Nothing to remove. Report that plainly rather than erroring, so a
		// replayed call is a no-op instead of a failure.
		auto Result = MCPSuccess();
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("alreadyRemoved"), true);
		Result->SetStringField(TEXT("assetPath"), Asset->GetPathName());
		Result->SetNumberField(TEXT("traitCount"), Traits.Num());
		Result->SetArrayField(TEXT("traitClasses"), MCPStringListToJson(PriorClassNames));
		Result->SetStringField(TEXT("rollbackUnavailable"),
			TEXT("nothing was removed, so there is nothing to undo"));
		return MCPResult(Result);
	}

	UObject* Removed = Traits[TargetIndex];
	const FString RemovedClassPath = Removed ? Removed->GetClass()->GetPathName() : FString(TEXT("None"));
	const FString RemovedClassName = Removed ? Removed->GetClass()->GetName() : FString(TEXT("None"));

	void* TraitsAddr = nullptr;
	void* ConfigAddr = nullptr;
	FArrayProperty* TraitsProperty = FindConfigTraitsArray(Asset, TraitsAddr, ConfigAddr, Error);
	if (!TraitsProperty) return MCPError(Error);

	const bool bShouldActuallyTransact = GEditor != nullptr;
	FScopedTransaction Transaction(
		NSLOCTEXT("UEMCP", "RemoveMassTrait", "Remove Mass Entity Config Trait"),
		bShouldActuallyTransact);
	Asset->Modify();
	{
		FScriptArrayHelper Helper(TraitsProperty, TraitsAddr);
		Helper.RemoveValues(TargetIndex, 1);
	}
	Asset->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetExisted(Result);
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("alreadyRemoved"), false);
	Result->SetStringField(TEXT("assetPath"), Asset->GetPathName());
	Result->SetNumberField(TEXT("removedIndex"), TargetIndex);
	Result->SetStringField(TEXT("removedClass"), RemovedClassPath);
	Result->SetStringField(TEXT("removedClassName"), RemovedClassName);
	Result->SetNumberField(TEXT("traitCount"), Traits.Num() - 1);

	FString SaveReason;
	if (!SaveAssetPackageChecked(Asset, SaveReason))
	{
		Transaction.Cancel();
		return MCPError(FString::Printf(TEXT("Removed the trait but could not save %s: %s"), *Asset->GetPathName(), *SaveReason));
	}

	// ensure_mass_entity_config only ever APPENDS: it refuses an existing
	// class that differs from the requested one at the same index. That makes
	// it an exact inverse when the removed trait was the last one, and no
	// inverse at all when it was not, because the restored list would collide
	// at the first index. Say which case this is rather than handing back a
	// rollback that would fail.
	const bool bRemovedLast = (TargetIndex == Traits.Num() - 1);
	if (bRemovedLast)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), Asset->GetPathName());
		Payload->SetArrayField(TEXT("traits"), PriorSpecs);
		MCPSetRollback(Result, TEXT("ensure_mass_entity_config"), Payload);
		Result->SetBoolField(TEXT("rollbackAvailable"), true);
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackAvailable"), false);
		Result->SetStringField(TEXT("rollbackUnavailable"), FString::Printf(
			TEXT("ensure_mass_entity_config can only append, so restoring index %d in place takes two calls: ensure_mass_entity_config with the full prior traits list re-adds %s at the end, then reorder_mass_traits puts it back at %d. The prior list is in priorTraits."),
			TargetIndex, *RemovedClassName, TargetIndex));
		Result->SetArrayField(TEXT("priorTraits"), PriorSpecs);
	}
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// reorder_mass_traits - trait order is template build order, and nothing else
//                       could change it
// ─────────────────────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::ReorderMassTraits(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MassZoneGraph_Internal;

	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	UClass* ConfigClass = ResolveOptionalClass(MassConfigClassPath);
	if (!ConfigClass)
	{
		return MCPError(TEXT("MassSpawner is unavailable; enable MassGameplay/MassSpawner before using reorder_mass_traits"));
	}

	UObject* Asset = LoadAssetByPath<UObject>(AssetPath);
	if (!Asset) return MCPError(FString::Printf(TEXT("Mass entity config not found: %s"), *AssetPath));
	if (!Asset->IsA(ConfigClass))
	{
		return MCPError(FString::Printf(TEXT("Asset is not a MassEntityConfigAsset: %s"), *Asset->GetPathName()));
	}

	FString Error;
	TArray<UObject*> Traits;
	if (!ReadConfigTraitObjects(Asset, Traits, Error)) return MCPError(Error);

	const TArray<TSharedPtr<FJsonValue>>* OrderArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("order"), OrderArray) || !OrderArray)
	{
		return MCPError(FString::Printf(
			TEXT("Missing required parameter 'order': the current trait indices in the order wanted, a full permutation of 0..%d"),
			FMath::Max(0, Traits.Num() - 1)));
	}

	// Validate the WHOLE permutation before touching the array, so a bad entry
	// at position nine cannot leave the first eight applied.
	if (OrderArray->Num() != Traits.Num())
	{
		return MCPError(FString::Printf(
			TEXT("order has %d entries but the config has %d traits; it must be a full permutation of 0..%d"),
			OrderArray->Num(), Traits.Num(), FMath::Max(0, Traits.Num() - 1)));
	}
	TArray<int32> Order;
	TSet<int32> Seen;
	Order.Reserve(OrderArray->Num());
	for (int32 Slot = 0; Slot < OrderArray->Num(); ++Slot)
	{
		double Raw = 0.0;
		if (!(*OrderArray)[Slot].IsValid() || !(*OrderArray)[Slot]->TryGetNumber(Raw))
		{
			return MCPError(FString::Printf(TEXT("order[%d] is not a number"), Slot));
		}
		const int32 From = FMath::RoundToInt(Raw);
		if (From < 0 || From >= Traits.Num())
		{
			return MCPError(FString::Printf(
				TEXT("order[%d] is %d, outside 0..%d"), Slot, From, FMath::Max(0, Traits.Num() - 1)));
		}
		if (Seen.Contains(From))
		{
			return MCPError(FString::Printf(TEXT("order[%d] repeats index %d; every index must appear exactly once"), Slot, From));
		}
		Seen.Add(From);
		Order.Add(From);
	}

	bool bIdentity = true;
	for (int32 Slot = 0; Slot < Order.Num(); ++Slot)
	{
		if (Order[Slot] != Slot) { bIdentity = false; break; }
	}

	TArray<FString> ClassNames;
	ClassNames.Reserve(Order.Num());
	for (int32 Slot = 0; Slot < Order.Num(); ++Slot)
	{
		UObject* Trait = Traits[Order[Slot]];
		ClassNames.Add(Trait ? Trait->GetClass()->GetPathName() : FString(TEXT("None")));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), Asset->GetPathName());
	Result->SetNumberField(TEXT("traitCount"), Traits.Num());
	Result->SetArrayField(TEXT("traitClasses"), MCPStringListToJson(ClassNames));

	if (bIdentity)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetStringField(TEXT("rollbackUnavailable"), TEXT("the order already matched; nothing changed"));
		return MCPResult(Result);
	}

	void* TraitsAddr = nullptr;
	void* ConfigAddr = nullptr;
	FArrayProperty* TraitsProperty = FindConfigTraitsArray(Asset, TraitsAddr, ConfigAddr, Error);
	if (!TraitsProperty) return MCPError(Error);
	FObjectProperty* Inner = CastField<FObjectProperty>(TraitsProperty->Inner);

	const bool bShouldActuallyTransact = GEditor != nullptr;
	FScopedTransaction Transaction(
		NSLOCTEXT("UEMCP", "ReorderMassTraits", "Reorder Mass Entity Config Traits"),
		bShouldActuallyTransact);
	Asset->Modify();
	{
		FScriptArrayHelper Helper(TraitsProperty, TraitsAddr);
		for (int32 Slot = 0; Slot < Order.Num(); ++Slot)
		{
			Inner->SetObjectPropertyValue(Helper.GetRawPtr(Slot), Traits[Order[Slot]]);
		}
	}
	Asset->MarkPackageDirty();

	MCPSetExisted(Result);
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("unchanged"), false);

	FString SaveReason;
	if (!SaveAssetPackageChecked(Asset, SaveReason))
	{
		Transaction.Cancel();
		return MCPError(FString::Printf(TEXT("Reordered the traits but could not save %s: %s"), *Asset->GetPathName(), *SaveReason));
	}

	// The inverse permutation puts every trait back where it started.
	TArray<int32> Inverse;
	Inverse.SetNum(Order.Num());
	for (int32 Slot = 0; Slot < Order.Num(); ++Slot) Inverse[Order[Slot]] = Slot;
	TArray<TSharedPtr<FJsonValue>> InverseJson;
	InverseJson.Reserve(Inverse.Num());
	for (int32 Value : Inverse) InverseJson.Add(MakeShared<FJsonValueNumber>(Value));

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), Asset->GetPathName());
	Payload->SetArrayField(TEXT("order"), InverseJson);
	MCPSetRollback(Result, TEXT("reorder_mass_traits"), Payload);
	Result->SetBoolField(TEXT("rollbackAvailable"), true);
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// validate_mass_entity_config - structural audit over the whole parent chain
// ─────────────────────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::ValidateMassEntityConfig(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MassZoneGraph_Internal;

	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	UClass* ConfigClass = ResolveOptionalClass(MassConfigClassPath);
	UClass* TraitBase = ResolveOptionalClass(MassTraitBaseClassPath);
	if (!ConfigClass || !TraitBase)
	{
		return MCPError(TEXT("MassSpawner is unavailable; enable MassGameplay/MassSpawner before using validate_mass_entity_config"));
	}

	UObject* Asset = LoadAssetByPath<UObject>(AssetPath);
	if (!Asset) return MCPError(FString::Printf(TEXT("Mass entity config not found: %s"), *AssetPath));
	if (!Asset->IsA(ConfigClass))
	{
		return MCPError(FString::Printf(TEXT("Asset is not a MassEntityConfigAsset: %s"), *Asset->GetPathName()));
	}

	TArray<TSharedPtr<FJsonValue>> Problems;
	int32 ErrorCount = 0;
	int32 WarningCount = 0;
	auto AddProblem = [&Problems, &ErrorCount, &WarningCount](
		const TCHAR* Severity, const TCHAR* Code, const FString& Message,
		const FString& Source, int32 TraitIndex, const FString& ObjectPath)
	{
		TSharedPtr<FJsonObject> Problem = MakeShared<FJsonObject>();
		Problem->SetStringField(TEXT("severity"), Severity);
		Problem->SetStringField(TEXT("code"), Code);
		Problem->SetStringField(TEXT("message"), Message);
		Problem->SetStringField(TEXT("config"), Source);
		if (TraitIndex >= 0) Problem->SetNumberField(TEXT("traitIndex"), TraitIndex);
		if (!ObjectPath.IsEmpty()) Problem->SetStringField(TEXT("objectPath"), ObjectPath);
		Problems.Add(MakeShared<FJsonValueObject>(Problem));
		if (FCString::Strcmp(Severity, TEXT("error")) == 0) ++ErrorCount; else ++WarningCount;
	};

	// Walk the parent chain, capping the depth so a cycle is reported rather
	// than hung on. FMassEntityConfig resolves derived traits through this
	// chain, so a duplicate class on an ancestor is a real conflict.
	TArray<UObject*> Chain;
	TSet<UObject*> Visited;
	UObject* Cursor = Asset;
	while (Cursor)
	{
		if (Visited.Contains(Cursor))
		{
			AddProblem(TEXT("error"), TEXT("parent_cycle"),
				FString::Printf(TEXT("Config.Parent chain revisits %s; a cycle makes the entity template unbuildable"), *Cursor->GetPathName()),
				Cursor->GetPathName(), -1, FString());
			break;
		}
		Visited.Add(Cursor);
		Chain.Add(Cursor);
		if (Chain.Num() > 32)
		{
			AddProblem(TEXT("error"), TEXT("parent_chain_too_deep"),
				TEXT("Config.Parent chain exceeded 32 links; walk stopped"),
				Cursor->GetPathName(), -1, FString());
			break;
		}
		Cursor = ReadConfigParent(Cursor);
	}

	TMap<UClass*, FString> ClassOrigin;
	TArray<TSharedPtr<FJsonValue>> TraitRecords;
	int32 TotalTraits = 0;

	for (UObject* Config : Chain)
	{
		FString ReadError;
		TArray<UObject*> Traits;
		if (!ReadConfigTraitObjects(Config, Traits, ReadError))
		{
			AddProblem(TEXT("error"), TEXT("traits_unreadable"), ReadError, Config->GetPathName(), -1, FString());
			continue;
		}

		for (int32 Index = 0; Index < Traits.Num(); ++Index)
		{
			UObject* Trait = Traits[Index];
			++TotalTraits;

			if (!Trait)
			{
				AddProblem(TEXT("error"), TEXT("null_trait"),
					FString::Printf(TEXT("Config.Traits[%d] is null; a null entry fails asset validation on save"), Index),
					Config->GetPathName(), Index, FString());
				continue;
			}

			UClass* TraitClass = Trait->GetClass();
			if (!TraitClass->IsChildOf(TraitBase))
			{
				AddProblem(TEXT("error"), TEXT("wrong_trait_type"),
					FString::Printf(TEXT("Config.Traits[%d] is a %s, not a UMassEntityTraitBase"), Index, *TraitClass->GetName()),
					Config->GetPathName(), Index, Trait->GetPathName());
			}
			else if (!IsUsableConcreteClass(TraitClass))
			{
				AddProblem(TEXT("error"), TEXT("stale_trait_class"),
					FString::Printf(TEXT("Config.Traits[%d] is a %s, which is abstract, deprecated or reinstanced"), Index, *TraitClass->GetName()),
					Config->GetPathName(), Index, Trait->GetPathName());
			}

			if (const FString* Origin = ClassOrigin.Find(TraitClass))
			{
				const bool bSameAsset = (*Origin == Config->GetPathName());
				AddProblem(bSameAsset ? TEXT("error") : TEXT("warning"), TEXT("duplicate_trait_class"),
					FString::Printf(TEXT("%s appears twice (already provided by %s); traits are combined uniquely, so the second copy is redundant"),
						*TraitClass->GetName(), **Origin),
					Config->GetPathName(), Index, Trait->GetPathName());
			}
			else
			{
				ClassOrigin.Add(TraitClass, Config->GetPathName());
			}

			// An unset object/class reference on a trait is the usual reason a
			// Mass agent spawns and renders nothing, and no other action
			// reports it.
			for (TFieldIterator<FProperty> It(TraitClass); It; ++It)
			{
				const FProperty* Property = *It;
				if (!Property || Property->HasAnyPropertyFlags(CPF_Transient)) continue;
				// Only fields the details panel exposes. An internal cache being
				// null is not something the author can act on.
				if (!Property->HasAnyPropertyFlags(CPF_Edit)) continue;
				if (MCPPropertyIsFixedArray(Property)) continue;

				// FSoftObjectProperty derives from FObjectPropertyBase, and the
				// base accessor RESOLVES a soft pointer, which would load every
				// asset a trait names just to audit it. Test the soft case
				// first so the audit stays a read.
				bool bUnset = false;
				if (const FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(Property))
				{
					bUnset = SoftProperty->GetPropertyValue_InContainer(Trait).ToSoftObjectPath().IsNull();
				}
				else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
				{
					bUnset = ObjectProperty->GetObjectPropertyValue_InContainer(Trait) == nullptr;
				}
				if (bUnset)
				{
					AddProblem(TEXT("warning"), TEXT("unset_reference"),
						FString::Printf(TEXT("%s.%s is unset; the trait will build a template that references nothing"),
							*TraitClass->GetName(), *Property->GetName()),
						Config->GetPathName(), Index, Trait->GetPathName());
				}
			}

			TSharedPtr<FJsonObject> Record = MakeShared<FJsonObject>();
			Record->SetStringField(TEXT("config"), Config->GetPathName());
			Record->SetNumberField(TEXT("index"), Index);
			Record->SetStringField(TEXT("classPath"), TraitClass->GetPathName());
			Record->SetStringField(TEXT("className"), TraitClass->GetName());
			Record->SetStringField(TEXT("objectPath"), Trait->GetPathName());
			TraitRecords.Add(MakeShared<FJsonValueObject>(Record));
		}
	}

	if (TotalTraits == 0)
	{
		AddProblem(TEXT("warning"), TEXT("empty_config"),
			TEXT("No traits on this config or any ancestor; the entity template would be empty"),
			Asset->GetPathName(), -1, FString());
	}

	TArray<FString> ChainPaths;
	for (UObject* Config : Chain) ChainPaths.Add(Config->GetPathName());

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), Asset->GetPathName());
	Result->SetArrayField(TEXT("parentChain"), MCPStringListToJson(ChainPaths));
	Result->SetArrayField(TEXT("traits"), TraitRecords);
	Result->SetNumberField(TEXT("traitCount"), TotalTraits);
	Result->SetArrayField(TEXT("problems"), Problems);
	Result->SetNumberField(TEXT("problemCount"), Problems.Num());
	Result->SetNumberField(TEXT("errorCount"), ErrorCount);
	Result->SetNumberField(TEXT("warningCount"), WarningCount);
	Result->SetBoolField(TEXT("valid"), ErrorCount == 0);
	// Be explicit about the ceiling. The engine's own build-time check is a
	// plain C++ method, not a UFUNCTION, and reaching it would mean linking
	// MassSpawner, which would take the bridge down wherever Mass is off.
	Result->SetStringField(TEXT("note"),
		TEXT("Structural audit over the asset and its Config.Parent chain. It does not run the engine's own FMassEntityConfig::ValidateEntityTemplate, which is a C++-only API that would require linking MassSpawner. Fix a reported trait through editor(set_property) at its objectPath."));
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// query_zone_graph - resolve the built graph's parallel index arrays
// ─────────────────────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::QueryZoneGraph(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MassZoneGraph_Internal;

	const FString QueryMode = OptionalString(Params, TEXT("queryMode"), TEXT("summary")).ToLower();
	if (QueryMode != TEXT("summary") && QueryMode != TEXT("lanes") && QueryMode != TEXT("lane") && QueryMode != TEXT("nearest"))
	{
		return MCPError(FString::Printf(
			TEXT("unknown queryMode '%s'; valid: summary, lanes, lane, nearest"), *QueryMode));
	}

	UClass* DataClass = ResolveOptionalClass(ZoneGraphDataClassPath);
	if (!DataClass)
	{
		return MCPError(TEXT("ZoneGraph is unavailable: /Script/ZoneGraph.ZoneGraphData did not resolve. Enable the ZoneGraph plugin and restart the editor."));
	}

	UWorld* World = ResolveWorldFromParams(Params, TEXT("editor"));
	if (!World) return MCPError(TEXT("No world available; pass world=editor or world=pie with a running session"));

	const int32 Limit = FMath::Clamp(OptionalInt(Params, TEXT("limit"), 50), 1, 5000);
	const FString ActorToken = OptionalString(Params, TEXT("actorPath"), OptionalString(Params, TEXT("actorLabel")));

	// Tag filtering is by NAME, because the bit index a tag occupies is a
	// project-settings detail no caller should have to know.
	TSet<FString> WantedTags;
	const TArray<TSharedPtr<FJsonValue>>* TagArray = nullptr;
	if (Params->TryGetArrayField(TEXT("tags"), TagArray) && TagArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *TagArray)
		{
			FString Tag;
			if (Value.IsValid() && Value->TryGetString(Tag) && !Tag.IsEmpty())
			{
				WantedTags.Add(Tag.ToLower());
			}
		}
	}

	TMap<uint8, FString> TagNames;
	ReadZoneGraphTagNames(TagNames);

	TArray<AActor*> DataActors;
	int32 ShapeCount = 0;
	UClass* ShapeClass = ResolveOptionalClass(ZoneShapeClassPath);
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;
		if (Actor->IsA(DataClass))
		{
			if (!ActorToken.IsEmpty()
				&& !Actor->GetPathName().Equals(ActorToken, ESearchCase::IgnoreCase)
				&& !Actor->GetActorNameOrLabel().Equals(ActorToken, ESearchCase::IgnoreCase))
			{
				continue;
			}
			DataActors.Add(Actor);
		}
		else if (ShapeClass && Actor->IsA(ShapeClass))
		{
			++ShapeCount;
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("queryMode"), QueryMode);
	Result->SetStringField(TEXT("worldPath"), World->GetPathName());
	Result->SetNumberField(TEXT("zoneShapeActorCount"), ShapeCount);

	if (DataActors.Num() == 0)
	{
		Result->SetArrayField(TEXT("data"), TArray<TSharedPtr<FJsonValue>>());
		Result->SetNumberField(TEXT("dataActorCount"), 0);
		Result->SetNumberField(TEXT("laneCount"), 0);
		Result->SetStringField(TEXT("note"), ShapeCount > 0
			? TEXT("This world has ZoneShape actors but no built ZoneGraphData, so there are no lanes to query yet. ZoneGraph only builds on demand: turn on bBuildZoneGraphWhileEditing with editor(set_property) at objectPath /Script/ZoneGraph.Default__ZoneGraphSettings, then move or re-edit a shape to trigger the build.")
			: TEXT("This world has no ZoneShape actors and no ZoneGraphData. Place shapes first with level(spawn_actor, actorClass=\"/Script/ZoneGraph.ZoneShape\") and author their Points with editor(set_property)."));
		return MCPResult(Result);
	}

	FVector QueryLocation = FVector::ZeroVector;
	if (QueryMode == TEXT("nearest"))
	{
		if (auto Err = RequireVec3(Params, TEXT("location"), QueryLocation)) return Err;
	}
	const double SearchRadius = OptionalNumber(Params, TEXT("radius"), 100000.0);
	const int32 WantedLane = OptionalInt(Params, TEXT("laneIndex"), -1);
	if (QueryMode == TEXT("lane") && WantedLane < 0)
	{
		return MCPError(TEXT("queryMode 'lane' needs laneIndex (0 or greater); read the available indices with queryMode 'lanes'"));
	}

	TArray<TSharedPtr<FJsonValue>> DataEntries;
	int32 TotalLanes = 0;
	int32 ReportedLanes = 0;
	int32 BestLaneIndex = INDEX_NONE;
	FString BestLaneActor;
	FVector BestPoint = FVector::ZeroVector;
	double BestDistance = TNumericLimits<double>::Max();

	for (AActor* DataActor : DataActors)
	{
		const FZoneGraphView View = OpenZoneGraphView(DataActor);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("actorPath"), DataActor->GetPathName());
		Entry->SetStringField(TEXT("actorLabel"), DataActor->GetActorNameOrLabel());

		if (!View.IsValid())
		{
			Entry->SetStringField(TEXT("error"), TEXT("ZoneStorage did not expose the expected Lanes/LanePoints arrays on this engine version"));
			DataEntries.Add(MakeShared<FJsonValueObject>(Entry));
			continue;
		}

		Entry->SetNumberField(TEXT("laneCount"), View.LaneCount);
		Entry->SetNumberField(TEXT("zoneCount"), View.ZoneCount);
		Entry->SetNumberField(TEXT("lanePointCount"), View.PointCount);
		TotalLanes += View.LaneCount;

		FScriptArrayHelper Lanes(View.LanesProperty, View.LanesAddr);
		const FStructProperty* LaneInner = CastField<FStructProperty>(View.LanesProperty->Inner);
		const UScriptStruct* LaneStruct = LaneInner ? LaneInner->Struct : nullptr;

		auto LanePassesTagFilter = [&](int32 LaneIndex) -> bool
		{
			if (WantedTags.Num() == 0) return true;
			const uint32 Mask = StructTagMask(LaneStruct, Lanes.GetRawPtr(LaneIndex), TEXT("Tags"));
			for (uint8 Bit = 0; Bit < 32; ++Bit)
			{
				if ((Mask & (uint32(1) << Bit)) == 0) continue;
				const FString* Named = TagNames.Find(Bit);
				const FString Name = Named ? *Named : FString::Printf(TEXT("Tag%u"), static_cast<uint32>(Bit));
				if (WantedTags.Contains(Name.ToLower())) return true;
			}
			return false;
		};

		if (QueryMode == TEXT("summary"))
		{
			// Which tags this data actually uses, so a caller can filter with
			// a name it knows exists rather than guessing at the settings.
			uint32 Union = 0;
			for (int32 LaneIndex = 0; LaneStruct && LaneIndex < View.LaneCount; ++LaneIndex)
			{
				Union |= StructTagMask(LaneStruct, Lanes.GetRawPtr(LaneIndex), TEXT("Tags"));
			}
			Entry->SetArrayField(TEXT("tagsInUse"), TagMaskToJson(Union, TagNames));
		}
		else if (QueryMode == TEXT("lanes"))
		{
			TArray<TSharedPtr<FJsonValue>> LaneArray;
			for (int32 LaneIndex = 0; LaneIndex < View.LaneCount && ReportedLanes < Limit; ++LaneIndex)
			{
				if (!LanePassesTagFilter(LaneIndex)) continue;
				LaneArray.Add(MakeShared<FJsonValueObject>(DescribeLane(View, LaneIndex, TagNames, false)));
				++ReportedLanes;
			}
			Entry->SetArrayField(TEXT("lanes"), LaneArray);
		}
		else if (QueryMode == TEXT("lane"))
		{
			if (WantedLane < View.LaneCount)
			{
				Entry->SetObjectField(TEXT("lane"), DescribeLane(View, WantedLane, TagNames, true));
				++ReportedLanes;
			}
			else
			{
				Entry->SetStringField(TEXT("note"), FString::Printf(
					TEXT("laneIndex %d is outside 0..%d on this data actor"), WantedLane, FMath::Max(0, View.LaneCount - 1)));
			}
		}
		else // nearest
		{
			for (int32 LaneIndex = 0; LaneIndex < View.LaneCount; ++LaneIndex)
			{
				if (!LanePassesTagFilter(LaneIndex)) continue;
				const int32 PointsBegin = StructInt(LaneStruct, Lanes.GetRawPtr(LaneIndex), TEXT("PointsBegin"));
				const int32 PointsEnd   = StructInt(LaneStruct, Lanes.GetRawPtr(LaneIndex), TEXT("PointsEnd"));
				for (int32 P = PointsBegin; P + 1 < PointsEnd; ++P)
				{
					const FVector A = View.PointAt(P);
					const FVector B = View.PointAt(P + 1);
					const FVector Closest = FMath::ClosestPointOnSegment(QueryLocation, A, B);
					const double Distance = FVector::Dist(QueryLocation, Closest);
					if (Distance < BestDistance)
					{
						BestDistance = Distance;
						BestLaneIndex = LaneIndex;
						BestLaneActor = DataActor->GetPathName();
						BestPoint = Closest;
					}
				}
			}
			// Keep the per-actor entry so a caller can see which data actors
			// were searched even when the winner came from another one.
			Entry->SetBoolField(TEXT("searched"), true);
		}

		DataEntries.Add(MakeShared<FJsonValueObject>(Entry));
	}

	Result->SetArrayField(TEXT("data"), DataEntries);
	Result->SetNumberField(TEXT("dataActorCount"), DataActors.Num());
	Result->SetNumberField(TEXT("laneCount"), TotalLanes);
	if (QueryMode == TEXT("lanes") || QueryMode == TEXT("lane"))
	{
		Result->SetNumberField(TEXT("reportedLanes"), ReportedLanes);
		if (QueryMode == TEXT("lanes") && ReportedLanes >= Limit)
		{
			Result->SetBoolField(TEXT("truncated"), true);
		}
	}
	if (QueryMode == TEXT("nearest"))
	{
		Result->SetObjectField(TEXT("queryLocation"), MCPVec3ToJsonObject(QueryLocation));
		Result->SetNumberField(TEXT("radius"), SearchRadius);
		if (BestLaneIndex != INDEX_NONE && BestDistance <= SearchRadius)
		{
			Result->SetBoolField(TEXT("found"), true);
			Result->SetNumberField(TEXT("laneIndex"), BestLaneIndex);
			Result->SetStringField(TEXT("actorPath"), BestLaneActor);
			Result->SetNumberField(TEXT("distance"), BestDistance);
			Result->SetObjectField(TEXT("nearestPoint"), MCPVec3ToJsonObject(BestPoint));
		}
		else
		{
			Result->SetBoolField(TEXT("found"), false);
			Result->SetStringField(TEXT("note"), BestLaneIndex == INDEX_NONE
				? TEXT("No lane had two or more points to measure against, so nothing could be nearest.")
				: FString::Printf(TEXT("Nearest lane was %.1f units away, past the radius of %.1f. Raise radius to accept it."), BestDistance, SearchRadius));
		}
	}
	if (TotalLanes == 0)
	{
		Result->SetStringField(TEXT("note"),
			TEXT("ZoneGraphData exists but holds no lanes, so the graph has not been built from the shapes. Turn on bBuildZoneGraphWhileEditing with editor(set_property) at objectPath /Script/ZoneGraph.Default__ZoneGraphSettings, then move or re-edit a ZoneShape to trigger the build."));
	}
	return MCPResult(Result);
}
