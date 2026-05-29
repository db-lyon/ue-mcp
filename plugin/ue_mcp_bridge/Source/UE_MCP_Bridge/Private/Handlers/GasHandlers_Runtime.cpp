// Runtime GAS control: apply a GameplayEffect, and get/set attributes on a
// live actor's AbilitySystemComponent. These are the agnostic "affect a stat"
// test stimuli - they drive the game's OWN effects and attributes rather than
// assuming a damage pipeline, so they work for any GAS game. Non-GAS games set
// reflection-exposed stats via level.set_actor_property or call their own
// functions via editor.invoke_function instead.

#include "GasHandlers.h"
#include "HandlerUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameFramework/Actor.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AttributeSet.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"

namespace
{
	// Resolve the world for this call. Defaults to "auto" (prefer PIE), since
	// runtime GAS control is almost always exercised during Play-In-Editor.
	UWorld* ResolveRuntimeWorld(const TSharedPtr<FJsonObject>& Params)
	{
		return ResolveWorldScope(OptionalString(Params, TEXT("world"), TEXT("auto")));
	}

	// Find the actor (by label or name) in the resolved world and return its
	// AbilitySystemComponent. On any failure writes a structured error to
	// OutError and returns nullptr.
	UAbilitySystemComponent* ResolveASC(
		const TSharedPtr<FJsonObject>& Params,
		AActor*& OutActor,
		TSharedPtr<FJsonValue>& OutError)
	{
		FString ActorLabel;
		if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel))
		{
			OutError = Err;
			return nullptr;
		}

		UWorld* World = ResolveRuntimeWorld(Params);
		if (!World)
		{
			OutError = MCPError(TEXT("No world available. For PIE actors, start Play-In-Editor first."));
			return nullptr;
		}

		AActor* Actor = FindActorByLabelOrName(World, ActorLabel);
		if (!Actor)
		{
			OutError = MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
			return nullptr;
		}
		OutActor = Actor;

		UAbilitySystemComponent* ASC =
			UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Actor);
		if (!ASC)
		{
			OutError = MCPError(FString::Printf(
				TEXT("Actor '%s' has no AbilitySystemComponent (not a GAS actor)"), *ActorLabel));
			return nullptr;
		}
		return ASC;
	}

	// Resolve a FGameplayAttribute by name against the ASC's spawned attribute
	// sets. Accepts a bare property name ("Health") or qualified forms
	// ("HealthSet.Health" / "HealthSet:Health"). Returns an invalid attribute
	// on miss; writes the matched set name to OutSetName on hit.
	FGameplayAttribute FindAttributeByName(
		UAbilitySystemComponent* ASC,
		const FString& Name,
		FString& OutSetName)
	{
		for (const UAttributeSet* Set : ASC->GetSpawnedAttributes())
		{
			if (!Set) continue;
			UClass* SetClass = Set->GetClass();
			const FString SetName = SetClass->GetName();
			for (TFieldIterator<FProperty> It(SetClass); It; ++It)
			{
				FStructProperty* SProp = CastField<FStructProperty>(*It);
				if (!SProp || SProp->Struct != FGameplayAttributeData::StaticStruct()) continue;
				const FString PropName = SProp->GetName();
				if (PropName == Name
					|| (SetName + TEXT(".") + PropName) == Name
					|| (SetName + TEXT(":") + PropName) == Name)
				{
					OutSetName = SetName;
					return FGameplayAttribute(SProp);
				}
			}
		}
		return FGameplayAttribute();
	}

	// Append one attribute's name/base/current to a JSON object.
	void WriteAttributeRow(
		TSharedPtr<FJsonObject> Obj,
		UAbilitySystemComponent* ASC,
		const FGameplayAttribute& Attr)
	{
		Obj->SetStringField(TEXT("attribute"), Attr.GetName());
		Obj->SetNumberField(TEXT("baseValue"), ASC->GetNumericAttributeBase(Attr));
		Obj->SetNumberField(TEXT("currentValue"), ASC->GetNumericAttribute(Attr));
	}

	// Resolve a UClass deriving from Base from a content path or short class name.
	// Handles native classes, Blueprint generated classes (path + "_C"), and a
	// Blueprint-asset fallback. Returns nullptr unless the result is a Base subclass.
	UClass* ResolveClassDeriving(const FString& Spec, UClass* Base)
	{
		auto Ok = [Base](UClass* C) { return C && Base && C->IsChildOf(Base); };

		if (Spec.Contains(TEXT("/")))
		{
			if (UClass* C = LoadObject<UClass>(nullptr, *Spec); Ok(C)) return C;
			FString AssetName;
			Spec.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			const FString ClassPath = Spec + TEXT(".") + AssetName + TEXT("_C");
			if (UClass* C = LoadObject<UClass>(nullptr, *ClassPath); Ok(C)) return C;
			if (UBlueprint* BP = LoadAssetByPath<UBlueprint>(Spec))
			{
				if (Ok(BP->GeneratedClass)) return BP->GeneratedClass;
			}
			return nullptr;
		}

		UClass* C = FindClassByShortName(Spec);
		return Ok(C) ? C : nullptr;
	}
}

TSharedPtr<FJsonValue> FGasHandlers::ApplyEffect(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FString EffectSpec;
	if (auto Err = RequireStringAlt(Params, TEXT("effectClass"), TEXT("effectPath"), EffectSpec)) return Err;

	AActor* Actor = nullptr;
	TSharedPtr<FJsonValue> Err;
	UAbilitySystemComponent* ASC = ResolveASC(Params, Actor, Err);
	if (!ASC) return Err;

	UClass* EffectClass = ResolveClassDeriving(EffectSpec, UGameplayEffect::StaticClass());
	if (!EffectClass)
	{
		return MCPError(FString::Printf(
			TEXT("GameplayEffect class not found: %s (pass a content path or class name)"), *EffectSpec));
	}

	const float Level = static_cast<float>(OptionalNumber(Params, TEXT("level"), 1.0));

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddInstigator(Actor, Actor);
	FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(EffectClass, Level, Context);
	if (!SpecHandle.IsValid() || !SpecHandle.Data.IsValid())
	{
		return MCPError(TEXT("Failed to build a GameplayEffectSpec for the effect"));
	}

	// SetByCaller magnitudes: { "<tag-or-name>": <number> }. Prefer a gameplay
	// tag when the key resolves to one; otherwise use the FName overload.
	const TSharedPtr<FJsonObject>* SetByCaller = nullptr;
	TArray<FString> AppliedKeys;
	if (Params->TryGetObjectField(TEXT("setByCaller"), SetByCaller) && SetByCaller && (*SetByCaller).IsValid())
	{
		for (const auto& KV : (*SetByCaller)->Values)
		{
			double Mag = 0.0;
			if (!KV.Value.IsValid() || !KV.Value->TryGetNumber(Mag)) continue;
			const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*KV.Key), /*ErrorIfNotFound*/ false);
			if (Tag.IsValid())
			{
				SpecHandle.Data->SetSetByCallerMagnitude(Tag, static_cast<float>(Mag));
			}
			else
			{
				SpecHandle.Data->SetSetByCallerMagnitude(FName(*KV.Key), static_cast<float>(Mag));
			}
			AppliedKeys.Add(KV.Key);
		}
	}

	const FActiveGameplayEffectHandle Active = ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("effect"), EffectClass->GetPathName());
	Result->SetNumberField(TEXT("level"), Level);
	Result->SetBoolField(TEXT("applied"), Active.WasSuccessfullyApplied());
	// Duration/Infinite effects produce a live handle; instant effects don't.
	Result->SetBoolField(TEXT("durationActive"), Active.IsValid());
	if (AppliedKeys.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Keys;
		for (const FString& K : AppliedKeys) Keys.Add(MakeShared<FJsonValueString>(K));
		Result->SetArrayField(TEXT("setByCaller"), Keys);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::SetAttribute(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FString AttrName;
	if (auto Err = RequireString(Params, TEXT("attribute"), AttrName)) return Err;

	double NewValue = 0.0;
	if (!Params->TryGetNumberField(TEXT("value"), NewValue))
	{
		return MCPError(TEXT("Missing required parameter 'value'"));
	}

	AActor* Actor = nullptr;
	TSharedPtr<FJsonValue> Err;
	UAbilitySystemComponent* ASC = ResolveASC(Params, Actor, Err);
	if (!ASC) return Err;

	FString SetName;
	const FGameplayAttribute Attr = FindAttributeByName(ASC, AttrName, SetName);
	if (!Attr.IsValid())
	{
		return MCPError(FString::Printf(
			TEXT("Attribute '%s' not found on '%s'. Use get_attribute with no 'attribute' to list available ones."),
			*AttrName, *Actor->GetActorLabel()));
	}

	const float OldBase = ASC->GetNumericAttributeBase(Attr);
	// SetNumericAttributeBase recalculates CurrentValue through the aggregator,
	// so dependent modifiers stay consistent (unlike a raw property write).
	ASC->SetNumericAttributeBase(Attr, static_cast<float>(NewValue));

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("attributeSet"), SetName);
	Result->SetStringField(TEXT("attribute"), Attr.GetName());
	Result->SetNumberField(TEXT("previousBaseValue"), OldBase);
	Result->SetNumberField(TEXT("baseValue"), ASC->GetNumericAttributeBase(Attr));
	Result->SetNumberField(TEXT("currentValue"), ASC->GetNumericAttribute(Attr));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::GetAttribute(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	AActor* Actor = nullptr;
	TSharedPtr<FJsonValue> Err;
	UAbilitySystemComponent* ASC = ResolveASC(Params, Actor, Err);
	if (!ASC) return Err;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());

	const FString AttrName = OptionalString(Params, TEXT("attribute"));
	if (!AttrName.IsEmpty())
	{
		FString SetName;
		const FGameplayAttribute Attr = FindAttributeByName(ASC, AttrName, SetName);
		if (!Attr.IsValid())
		{
			return MCPError(FString::Printf(
				TEXT("Attribute '%s' not found on '%s'"), *AttrName, *Actor->GetActorLabel()));
		}
		Result->SetStringField(TEXT("attributeSet"), SetName);
		WriteAttributeRow(Result, ASC, Attr);
		return MCPResult(Result);
	}

	// No attribute named: enumerate every attribute across all spawned sets.
	TArray<TSharedPtr<FJsonValue>> Rows;
	for (const UAttributeSet* Set : ASC->GetSpawnedAttributes())
	{
		if (!Set) continue;
		UClass* SetClass = Set->GetClass();
		const FString SetName = SetClass->GetName();
		for (TFieldIterator<FProperty> It(SetClass); It; ++It)
		{
			FStructProperty* SProp = CastField<FStructProperty>(*It);
			if (!SProp || SProp->Struct != FGameplayAttributeData::StaticStruct()) continue;
			const FGameplayAttribute Attr(SProp);
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("attributeSet"), SetName);
			WriteAttributeRow(Row, ASC, Attr);
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
	}
	Result->SetArrayField(TEXT("attributes"), Rows);
	Result->SetNumberField(TEXT("count"), Rows.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::InitAsc(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	AActor* Actor = nullptr;
	TSharedPtr<FJsonValue> Err;
	UAbilitySystemComponent* ASC = ResolveASC(Params, Actor, Err);
	if (!ASC) return Err;

	// Establish owner/avatar so abilities activate and effect contexts target
	// correctly. Safe to call again; a game's own pawn may also init the ASC.
	ASC->InitAbilityActorInfo(Actor, Actor);

	// Optionally guarantee an attribute set exists on the ASC. This is what lets
	// a bridge-authored test actor have live attributes without shipping an init
	// DataTable: spawn the set (with its default values) and register it if it
	// isn't already present. GetOrCreateAttributeSubobject is protected, so use
	// the public GetAttributeSet + AddSpawnedAttribute pair.
	FString CreatedSet;
	const FString AttrSetSpec = OptionalString(Params, TEXT("attributeSet"));
	if (!AttrSetSpec.IsEmpty())
	{
		UClass* AttrSetClass = ResolveClassDeriving(AttrSetSpec, UAttributeSet::StaticClass());
		if (!AttrSetClass)
		{
			return MCPError(FString::Printf(
				TEXT("AttributeSet class not found: %s (pass a content path or class name)"), *AttrSetSpec));
		}
		const UAttributeSet* Existing = ASC->GetAttributeSet(AttrSetClass);
		if (!Existing)
		{
			UAttributeSet* NewSet = NewObject<UAttributeSet>(Actor, AttrSetClass);
			ASC->AddSpawnedAttribute(NewSet);
			CreatedSet = NewSet->GetClass()->GetName();
		}
		else
		{
			CreatedSet = Existing->GetClass()->GetName();
		}
	}

	// Count attributes now live across all spawned sets.
	int32 AttrCount = 0;
	for (const UAttributeSet* Set : ASC->GetSpawnedAttributes())
	{
		if (!Set) continue;
		for (TFieldIterator<FProperty> It(Set->GetClass()); It; ++It)
		{
			FStructProperty* SProp = CastField<FStructProperty>(*It);
			if (SProp && SProp->Struct == FGameplayAttributeData::StaticStruct()) ++AttrCount;
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetBoolField(TEXT("initialized"), true);
	if (!CreatedSet.IsEmpty()) Result->SetStringField(TEXT("attributeSet"), CreatedSet);
	Result->SetNumberField(TEXT("attributeCount"), AttrCount);
	return MCPResult(Result);
}
