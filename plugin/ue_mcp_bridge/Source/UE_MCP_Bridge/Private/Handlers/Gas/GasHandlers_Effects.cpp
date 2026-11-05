// GameplayEffect authoring: duration policy, effect components and granted tags (#1423).
//
// UGameplayEffect::GEComponents is a protected Instanced array whose entries must be
// outered to the effect, and FInheritedTagContainer has to be recombined against the
// parent effect before the effect's granted-tag cache means anything. Neither is
// reachable through a plain property write, so both are handlers.

#include "GasHandlers.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "Engine/Blueprint.h"
#include "GameplayEffect.h"
#include "GameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayTagsManager.h"

namespace MCPGasEffect
{
	static FArrayProperty* ComponentsProperty()
	{
		return FindFProperty<FArrayProperty>(UGameplayEffect::StaticClass(), TEXT("GEComponents"));
	}

	static TArray<TSharedPtr<FJsonValue>> TagsToJson(const FGameplayTagContainer& Tags)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FGameplayTag& Tag : Tags)
		{
			Out.Add(MakeShared<FJsonValueString>(Tag.ToString()));
		}
		return Out;
	}

	/** Reads a string array of registered tags. Unknown tags are refused, since a stored invalid tag grants nothing. */
	static bool ReadRegisteredTags(const TSharedPtr<FJsonObject>& Params, const TCHAR* Field, FGameplayTagContainer& Out, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!TryGetArrayParam(Params, Field, Values) || !Values) return true;
		TArray<FString> Unknown;
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FString Name;
			if (!Value.IsValid() || !Value->TryGetString(Name) || Name.IsEmpty()) continue;
			const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*Name), /*ErrorIfNotFound*/ false);
			if (Tag.IsValid()) Out.AddTag(Tag);
			else Unknown.Add(Name);
		}
		if (Unknown.Num() > 0)
		{
			OutError = FString::Printf(
				TEXT("%s names tags that are not registered: %s. Register each with reflection(action=\"create_tag\") first."),
				Field, *FString::Join(Unknown, TEXT(", ")));
			return false;
		}
		return true;
	}

	static TSharedPtr<FJsonObject> DescribeComponent(const UGameplayEffectComponent* Component)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Component->GetName());
		Obj->SetStringField(TEXT("class"), Component->GetClass()->GetName());
		Obj->SetStringField(TEXT("path"), Component->GetPathName());
		if (const UTargetTagsGameplayEffectComponent* TargetTags = Cast<UTargetTagsGameplayEffectComponent>(Component))
		{
			const FInheritedTagContainer& Config = TargetTags->GetConfiguredTargetTagChanges();
			Obj->SetArrayField(TEXT("addedTags"), TagsToJson(Config.Added));
			Obj->SetArrayField(TEXT("removedTags"), TagsToJson(Config.Removed));
			Obj->SetArrayField(TEXT("combinedTags"), TagsToJson(Config.CombinedTags));
		}
		return Obj;
	}
}

bool FGasHandlers::ParseDurationPolicy(const FString& In, EGameplayEffectDurationType& Out)
{
	const FString Value = In.TrimStartAndEnd();
	if (Value.Equals(TEXT("Instant"), ESearchCase::IgnoreCase)) { Out = EGameplayEffectDurationType::Instant; return true; }
	if (Value.Equals(TEXT("Infinite"), ESearchCase::IgnoreCase)) { Out = EGameplayEffectDurationType::Infinite; return true; }
	if (Value.Equals(TEXT("HasDuration"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("Duration"), ESearchCase::IgnoreCase))
	{
		Out = EGameplayEffectDurationType::HasDuration;
		return true;
	}
	return false;
}

FString FGasHandlers::DurationPolicyToString(EGameplayEffectDurationType Policy)
{
	switch (Policy)
	{
	case EGameplayEffectDurationType::Instant:     return TEXT("Instant");
	case EGameplayEffectDurationType::Infinite:    return TEXT("Infinite");
	case EGameplayEffectDurationType::HasDuration: return TEXT("HasDuration");
	default:                                       return TEXT("Unknown");
	}
}

TArray<UGameplayEffectComponent*> FGasHandlers::GetEffectComponents(const UGameplayEffect* Effect)
{
	TArray<UGameplayEffectComponent*> Out;
	FArrayProperty* ArrayProp = MCPGasEffect::ComponentsProperty();
	FObjectPropertyBase* Inner = ArrayProp ? CastField<FObjectPropertyBase>(ArrayProp->Inner) : nullptr;
	if (!Effect || !Inner) return Out;
	FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Effect));
	for (int32 i = 0; i < Helper.Num(); ++i)
	{
		if (UGameplayEffectComponent* Component = Cast<UGameplayEffectComponent>(Inner->GetObjectPropertyValue(Helper.GetRawPtr(i))))
		{
			Out.Add(Component);
		}
	}
	return Out;
}

TSharedPtr<FJsonObject> FGasHandlers::DescribeEffect(const UGameplayEffect* Effect)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	if (!Effect) return Obj;
	Obj->SetStringField(TEXT("durationPolicy"), DurationPolicyToString(Effect->DurationPolicy));
	float Seconds = 0.f;
	if (Effect->DurationPolicy == EGameplayEffectDurationType::HasDuration
		&& Effect->DurationMagnitude.GetStaticMagnitudeIfPossible(1.f, Seconds))
	{
		Obj->SetNumberField(TEXT("durationMagnitude"), Seconds);
	}
	TArray<TSharedPtr<FJsonValue>> Components;
	for (const UGameplayEffectComponent* Component : GetEffectComponents(Effect))
	{
		Components.Add(MakeShared<FJsonValueObject>(MCPGasEffect::DescribeComponent(Component)));
	}
	Obj->SetArrayField(TEXT("components"), Components);
	Obj->SetArrayField(TEXT("grantedTags"), MCPGasEffect::TagsToJson(Effect->GetGrantedTags()));
	return Obj;
}

TSharedPtr<FJsonValue> FGasHandlers::AddEffectComponent(const TSharedPtr<FJsonObject>& Params)
{
	FString EffectPath;
	if (auto Err = RequireString(Params, TEXT("effectPath"), EffectPath)) return Err;
	FString ComponentClassName;
	if (auto Err = RequireString(Params, TEXT("componentClass"), ComponentClassName)) return Err;
	const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
	TryGetObjectParam(Params, TEXT("properties"), PropertiesPtr);
	const TSharedPtr<FJsonObject> Properties = PropertiesPtr ? *PropertiesPtr : nullptr;

	UClass* ComponentClass = MCPResolveClassOfType(ComponentClassName, UGameplayEffectComponent::StaticClass());
	if (!ComponentClass || !ComponentClass->IsChildOf(UGameplayEffectComponent::StaticClass()))
	{
		return MCPError(FString::Printf(
			TEXT("'%s' is not a GameplayEffectComponent class. Examples: TargetTagsGameplayEffectComponent, AssetTagsGameplayEffectComponent, AdditionalEffectsGameplayEffectComponent."),
			*ComponentClassName));
	}
	if (ComponentClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return MCPError(FString::Printf(TEXT("'%s' is abstract or deprecated and cannot be added."), *ComponentClass->GetName()));
	}

	TSharedPtr<FJsonValue> CdoErr;
	UGameplayEffect* GE = LoadBlueprintCDO<UGameplayEffect>(EffectPath, CdoErr);
	if (!GE) return CdoErr;

	// Every property name is resolved before anything is written.
	TArray<TPair<FProperty*, TSharedPtr<FJsonValue>>> Writes;
	if (Properties.IsValid())
	{
		TArray<FString> Unknown;
		for (const auto& Field : Properties->Values)
		{
			const FString Key(Field.Key);
			FProperty* Prop = MCPJsonProperty::FindMemberProperty(ComponentClass, Key);
			if (Prop) Writes.Emplace(Prop, Field.Value);
			else Unknown.Add(Key);
		}
		if (Unknown.Num() > 0)
		{
			return MCPError(FString::Printf(TEXT("%s has no property named %s."),
				*ComponentClass->GetName(), *FString::Join(Unknown, TEXT(", "))));
		}
	}

	UGameplayEffectComponent* Component = nullptr;
	for (UGameplayEffectComponent* Existing : GetEffectComponents(GE))
	{
		if (Existing->GetClass() == ComponentClass)
		{
			Component = Existing;
			break;
		}
	}
	const bool bExisted = Component != nullptr;
	if (!Component)
	{
		// The same construction UGameplayEffect::AddComponent<T> performs, for a class only known at runtime.
		Component = NewObject<UGameplayEffectComponent>(GE, ComponentClass, NAME_None,
			GE->GetMaskedFlags(RF_PropagateToSubObjects) | RF_Transactional);
	}

	for (const TPair<FProperty*, TSharedPtr<FJsonValue>>& Write : Writes)
	{
		FString WriteError;
		if (!MCPJsonProperty::SetJsonOnPropertyWithOwner(Write.Key, Write.Key->ContainerPtrToValuePtr<void>(Component), Write.Value, Component, WriteError))
		{
			if (!bExisted) Component->MarkAsGarbage();
			return MCPError(FString::Printf(TEXT("Could not write %s.%s: %s"),
				*ComponentClass->GetName(), *Write.Key->GetName(), *WriteError));
		}
	}

	if (!bExisted)
	{
		FArrayProperty* ArrayProp = MCPGasEffect::ComponentsProperty();
		FObjectPropertyBase* Inner = ArrayProp ? CastField<FObjectPropertyBase>(ArrayProp->Inner) : nullptr;
		if (!Inner)
		{
			Component->MarkAsGarbage();
			return MCPError(TEXT("UGameplayEffect has no GEComponents array in this engine version."));
		}
		FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(GE));
		const int32 Index = Helper.AddValue();
		Inner->SetObjectPropertyValue(Helper.GetRawPtr(Index), Component);
	}

	const bool bChanged = !bExisted || Writes.Num() > 0;
	auto Result = MCPSuccess();
	if (bChanged)
	{
		// Recomputes every component's cached contribution, granted tags among them.
		GE->OnGameplayEffectChanged();
		GE->MarkPackageDirty();
		FString SaveError;
		const bool bSaved = SaveAssetPackageChecked(GE, SaveError);
		MCPNoteSaveOutcome(Result, EffectPath, bSaved, SaveError);
	}

	if (bExisted) MCPSetExisted(Result); else MCPSetCreated(Result);
	if (bExisted && bChanged) MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("unchanged"), !bChanged);
	Result->SetStringField(TEXT("effectPath"), EffectPath);
	Result->SetObjectField(TEXT("component"), MCPGasEffect::DescribeComponent(Component));
	Result->SetNumberField(TEXT("propertiesWritten"), Writes.Num());
	Result->SetObjectField(TEXT("effect"), DescribeEffect(GE));
	if (bExisted)
	{
		MCPSetNoRollback(Result, TEXT("The component already existed, so removing it would undo more than this call did, and the property values it overwrote are not recorded. Write them back with editor(set_property) on the component path."));
	}
	else
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("effectPath"), EffectPath);
		Payload->SetStringField(TEXT("componentClass"), ComponentClass->GetPathName());
		MCPSetRollback(Result, TEXT("remove_effect_component"), Payload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::RemoveEffectComponent(const TSharedPtr<FJsonObject>& Params)
{
	FString EffectPath;
	if (auto Err = RequireString(Params, TEXT("effectPath"), EffectPath)) return Err;
	FString ComponentClassName;
	if (auto Err = RequireString(Params, TEXT("componentClass"), ComponentClassName)) return Err;

	UClass* ComponentClass = MCPResolveClassOfType(ComponentClassName, UGameplayEffectComponent::StaticClass());
	if (!ComponentClass || !ComponentClass->IsChildOf(UGameplayEffectComponent::StaticClass()))
	{
		return MCPError(FString::Printf(TEXT("'%s' is not a GameplayEffectComponent class."), *ComponentClassName));
	}

	TSharedPtr<FJsonValue> CdoErr;
	UGameplayEffect* GE = LoadBlueprintCDO<UGameplayEffect>(EffectPath, CdoErr);
	if (!GE) return CdoErr;

	FArrayProperty* ArrayProp = MCPGasEffect::ComponentsProperty();
	FObjectPropertyBase* Inner = ArrayProp ? CastField<FObjectPropertyBase>(ArrayProp->Inner) : nullptr;
	if (!Inner)
	{
		return MCPError(TEXT("UGameplayEffect has no GEComponents array in this engine version."));
	}
	FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(GE));
	UGameplayEffectComponent* Removed = nullptr;
	for (int32 i = 0; i < Helper.Num(); ++i)
	{
		UGameplayEffectComponent* Component = Cast<UGameplayEffectComponent>(Inner->GetObjectPropertyValue(Helper.GetRawPtr(i)));
		if (Component && Component->GetClass() == ComponentClass)
		{
			Removed = Component;
			Helper.RemoveValues(i, 1);
			break;
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("effectPath"), EffectPath);
	Result->SetStringField(TEXT("componentClass"), ComponentClass->GetName());
	if (!Removed)
	{
		Result->SetBoolField(TEXT("alreadyRemoved"), true);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetObjectField(TEXT("effect"), DescribeEffect(GE));
		MCPSetNoRollback(Result, TEXT("The effect had no component of that class, so nothing changed."));
		return MCPResult(Result);
	}

	Removed->MarkAsGarbage();
	GE->OnGameplayEffectChanged();
	GE->MarkPackageDirty();
	FString SaveError;
	const bool bSaved = SaveAssetPackageChecked(GE, SaveError);
	MCPNoteSaveOutcome(Result, EffectPath, bSaved, SaveError);
	Result->SetBoolField(TEXT("alreadyRemoved"), false);
	Result->SetBoolField(TEXT("unchanged"), false);
	Result->SetObjectField(TEXT("effect"), DescribeEffect(GE));

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("effectPath"), EffectPath);
	Payload->SetStringField(TEXT("componentClass"), ComponentClass->GetPathName());
	MCPSetRollback(Result, TEXT("add_effect_component"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"), TEXT("The rollback adds a component of the same class back with default property values; the removed component's values are not recorded."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::SetEffectGrantedTags(const TSharedPtr<FJsonObject>& Params)
{
	FString EffectPath;
	if (auto Err = RequireString(Params, TEXT("effectPath"), EffectPath)) return Err;
	const TArray<TSharedPtr<FJsonValue>>* TagValues = nullptr;
	if (!TryGetArrayParam(Params, TEXT("tags"), TagValues))
	{
		return MCPError(TEXT("Missing required parameter 'tags': an array of registered gameplay tags, written whole. Pass [] to grant none."));
	}
	const bool bHasRemoved = HasParam(Params, TEXT("removedTags"));

	FGameplayTagContainer Added;
	FGameplayTagContainer Removed;
	FString TagError;
	if (!MCPGasEffect::ReadRegisteredTags(Params, TEXT("tags"), Added, TagError)) return MCPError(TagError);
	if (!MCPGasEffect::ReadRegisteredTags(Params, TEXT("removedTags"), Removed, TagError)) return MCPError(TagError);

	TSharedPtr<FJsonValue> CdoErr;
	UGameplayEffect* GE = LoadBlueprintCDO<UGameplayEffect>(EffectPath, CdoErr);
	if (!GE) return CdoErr;

	UTargetTagsGameplayEffectComponent* Component = const_cast<UTargetTagsGameplayEffectComponent*>(GE->FindComponent<UTargetTagsGameplayEffectComponent>());
	const bool bExisted = Component != nullptr;
	FInheritedTagContainer Previous;
	if (bExisted)
	{
		Previous = Component->GetConfiguredTargetTagChanges();
		if (!bHasRemoved) Removed = Previous.Removed;
	}

	const bool bUnchanged = bExisted
		&& Previous.Added.HasAllExact(Added) && Added.HasAllExact(Previous.Added)
		&& Previous.Removed.HasAllExact(Removed) && Removed.HasAllExact(Previous.Removed);

	auto Result = MCPSuccess();
	if (!bUnchanged)
	{
		if (!Component)
		{
			Component = &GE->AddComponent<UTargetTagsGameplayEffectComponent>();
		}
		FInheritedTagContainer Mods;
		Mods.Added = Added;
		Mods.Removed = Removed;
		// Recombines against the parent effect and refreshes the effect's granted-tag cache.
		Component->SetAndApplyTargetTagChanges(Mods);
		GE->OnGameplayEffectChanged();
		GE->MarkPackageDirty();
		FString SaveError;
		const bool bSaved = SaveAssetPackageChecked(GE, SaveError);
		MCPNoteSaveOutcome(Result, EffectPath, bSaved, SaveError);
	}

	if (bExisted) MCPSetExisted(Result); else MCPSetCreated(Result);
	if (bExisted && !bUnchanged) MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("componentCreated"), !bExisted);
	Result->SetBoolField(TEXT("unchanged"), bUnchanged);
	Result->SetStringField(TEXT("effectPath"), EffectPath);
	Result->SetObjectField(TEXT("component"), MCPGasEffect::DescribeComponent(Component));
	Result->SetArrayField(TEXT("grantedTags"), MCPGasEffect::TagsToJson(GE->GetGrantedTags()));
	Result->SetStringField(TEXT("durationPolicy"), DurationPolicyToString(GE->DurationPolicy));
	if (GE->DurationPolicy == EGameplayEffectDurationType::Instant)
	{
		Result->SetStringField(TEXT("warning"),
			TEXT("This effect is Instant, and an Instant effect grants no tags. Recreate it with durationPolicy Infinite or HasDuration, or set DurationPolicy on its class defaults."));
	}

	if (bUnchanged)
	{
		MCPSetNoRollback(Result, TEXT("The granted tags already matched, so nothing changed."));
	}
	else
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("effectPath"), EffectPath);
		Payload->SetArrayField(TEXT("tags"), MCPGasEffect::TagsToJson(Previous.Added));
		Payload->SetArrayField(TEXT("removedTags"), MCPGasEffect::TagsToJson(Previous.Removed));
		MCPSetRollback(Result, TEXT("set_effect_granted_tags"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), !bExisted);
		if (!bExisted)
		{
			Result->SetStringField(TEXT("rollbackNote"),
				TEXT("The rollback clears the tags but leaves the Grant Tags to Target Actor component this call added, now empty."));
		}
	}
	return MCPResult(Result);
}
