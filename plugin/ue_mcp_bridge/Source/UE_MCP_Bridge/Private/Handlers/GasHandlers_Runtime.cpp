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
#include "Abilities/GameplayAbility.h"
#include "GameplayAbilitySpec.h"
#include "GameplayTagContainer.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

namespace MCPGas
{

int32 AdoptOwnerAttributeSets(
	UAbilitySystemComponent* ASC,
	AActor* Actor,
	TArray<FString>& OutAdoptedClassNames)
{
	if (!ASC || !Actor) return 0;

	TArray<UObject*> Subobjects;
	GetObjectsWithOuter(Actor, Subobjects);

	int32 Adopted = 0;
	for (UObject* Object : Subobjects)
	{
		UAttributeSet* Set = Cast<UAttributeSet>(Object);
		if (!IsValid(Set)) continue;
		// Already registered: leave the existing entry alone. Re-adding would
		// not duplicate it, but skipping keeps the "newly registered" count
		// honest, and that count is what the result reports.
		if (ASC->GetSpawnedAttributes().Contains(Set)) continue;
		ASC->AddSpawnedAttribute(Set);
		OutAdoptedClassNames.Add(Set->GetClass()->GetName());
		++Adopted;
	}
	return Adopted;
}

FStructProperty* FindAttributeDataProperty(UClass* SetClass, const FString& Name)
{
	if (!SetClass) return nullptr;
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
			return SProp;
		}
	}
	return nullptr;
}

FString ListAttributeDataPropertyNames(UClass* SetClass)
{
	TArray<FString> Names;
	if (SetClass)
	{
		for (TFieldIterator<FProperty> It(SetClass); It; ++It)
		{
			FStructProperty* SProp = CastField<FStructProperty>(*It);
			if (SProp && SProp->Struct == FGameplayAttributeData::StaticStruct())
			{
				Names.Add(SProp->GetName());
			}
		}
	}
	return Names.Num() > 0 ? FString::Join(Names, TEXT(", ")) : FString(TEXT("(none)"));
}

}

namespace
{
	// Resolve the world for this call. Defaults to "auto" (prefer PIE), since
	// runtime GAS control is almost always exercised during Play-In-Editor.
	UWorld* ResolveRuntimeWorld(const TSharedPtr<FJsonObject>& Params)
	{
		return ResolveWorldScope(OptionalString(Params, TEXT("world"), TEXT("auto")));
	}

	// Find the actor in the resolved world and return its
	// AbilitySystemComponent. On any failure writes a structured error to
	// OutError and returns nullptr.
	UAbilitySystemComponent* ResolveASC(
		const TSharedPtr<FJsonObject>& Params,
		AActor*& OutActor,
		TSharedPtr<FJsonValue>& OutError)
	{
		FString ActorLabel;
		if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel))
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

		// #956: label, internal name, or full object path, in that fixed order.
		// A verification actor spawned into the editor world often has no label
		// worth guessing, so the path has to be a first-class way to name it.
		// #983: actorPath is its own parameter now, and a label that names more
		// than one actor is refused rather than answered from one of them.
		FMCPActorSelector ActorSel;
		ActorSel.Match = EMCPActorMatch::LabelNameOrPath;
		ActorSel.WorldLabel = World->IsPlayInEditor() ? TEXT("PIE") : TEXT("editor");
		AActor* Actor = MCPResolveActor(World, Params, OutError, ActorSel);
		if (!Actor) return nullptr;
		ActorLabel = Actor->GetActorLabel();
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

	// ── Registered attribute sets (#956) ──────────────────────────────
	//
	// UAbilitySystemComponent::GetAttributeSet / GetSet<T>() search
	// SpawnedAttributes, and SpawnedAttributes IS the registry the gameplay
	// code consults. Nothing else is. An actor's own UPROPERTY pointer to a
	// CreateDefaultSubobject-created set is NOT proof the ASC knows about it.
	//
	// Those two only agree once InitializeComponent has run. It is
	// InitializeComponent that scans the owner's default subobjects and calls
	// AddSpawnedAttribute on every UAttributeSet it finds, and an actor spawned
	// into the pure editor world never reaches it: there is no BeginPlay and no
	// InitializeComponent in a world that has not begun play. So in the editor
	// world the ASC has ZERO registered sets while the actor plainly has one.
	//
	// The trap that follows, and the reason this comment is this long: the
	// obvious fallback at that point is "the ASC has none, so make one", which
	// constructs a SECOND, disconnected instance next to the actor's own. Every
	// read and write then lands on an object no gameplay code will ever look
	// at, no error is reported, and the verification says the change worked. So
	// we adopt the actor's EXISTING subobject into the ASC instead, which is
	// exactly what InitializeComponent would have done, and we only ever hand
	// back what GetAttributeSet returns afterwards.

	/**
	 * The instance the gameplay code consults, and nothing else.
	 *
	 * Always answered by ASC->GetAttributeSet (the non-template form of
	 * ASC->GetSet<T>()). When the ASC has not registered one and adoption is
	 * allowed, the actor's own subobject is registered first and the ASC is
	 * asked again, so the pointer that comes back is still the registered one.
	 * A new set is never constructed here.
	 */
	UAttributeSet* ResolveRegisteredAttributeSet(
		UAbilitySystemComponent* ASC,
		AActor* Actor,
		UClass* SetClass,
		bool bAllowAdopt,
		bool& bOutAdopted,
		TArray<FString>& OutAdoptedClassNames,
		TSharedPtr<FJsonValue>& OutError)
	{
		bOutAdopted = false;
		if (const UAttributeSet* Registered = ASC->GetAttributeSet(SetClass))
		{
			// const_cast is the only way to a mutable registered set: the ASC
			// hands out const pointers and keeps no non-const accessor. The
			// object is not const, only the view of it.
			return const_cast<UAttributeSet*>(Registered);
		}

		if (bAllowAdopt && MCPGas::AdoptOwnerAttributeSets(ASC, Actor, OutAdoptedClassNames) > 0)
		{
			if (const UAttributeSet* Registered = ASC->GetAttributeSet(SetClass))
			{
				bOutAdopted = true;
				return const_cast<UAttributeSet*>(Registered);
			}
		}

		OutError = MCPError(FString::Printf(
			TEXT("No '%s' is registered on '%s' AbilitySystemComponent%s. ")
			TEXT("A set the actor owns is only registered once InitializeComponent runs, which never happens in a world that has not begun play. ")
			TEXT("Start PIE, or call gas(action=\"init_asc\") with attributeSet=\"%s\" to register it."),
			*SetClass->GetName(),
			*Actor->GetActorLabel(),
			bAllowAdopt ? TEXT(", and the actor owns no instance of it either") : TEXT(" (registerOwnerSets was false)"),
			*SetClass->GetName()));
		return nullptr;
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

	// Captured before anything resolves, because the rollback record has to name
	// the same world scope this call ran against rather than re-guessing it.
	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("auto"));

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
			AppliedKeys.Add(FString(*KV.Key));
		}
	}

	// Which effects were already live, so the apply below can be told apart from
	// a STACK onto one of them. A GameplayEffect whose StackingType is not None
	// does not produce a new active effect when one of its class is already on
	// the ASC: ApplyGameplayEffectSpecToSelf returns the EXISTING handle with an
	// incremented stack count. Without this the rollback would remove that whole
	// effect, destroying stacks that were there before the call.
	TSet<FString> HandlesBeforeApply;
	{
		const FGameplayEffectQuery BeforeQuery;
		for (const FActiveGameplayEffectHandle& Live : ASC->GetActiveEffects(BeforeQuery))
		{
			HandlesBeforeApply.Add(Live.ToString());
		}
	}

	const FActiveGameplayEffectHandle Active = ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data);
	const bool bStackedOntoExisting = Active.IsValid() && HandlesBeforeApply.Contains(Active.ToString());
	int32 StackCountAfter = 0;
	if (const FActiveGameplayEffect* AppliedEffect = ASC->GetActiveGameplayEffect(Active))
	{
		StackCountAfter = AppliedEffect->Spec.GetStackCount();
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("effect"), EffectClass->GetPathName());
	Result->SetNumberField(TEXT("level"), Level);
	Result->SetBoolField(TEXT("applied"), Active.WasSuccessfullyApplied());
	// Duration/Infinite effects produce a live handle; instant effects don't.
	Result->SetBoolField(TEXT("durationActive"), Active.IsValid());
	// The handle is what gas(remove_effect) addresses, so it is reported here
	// rather than only inside the rollback record a caller may never read.
	if (Active.IsValid())
	{
		Result->SetStringField(TEXT("effectHandle"), Active.ToString());
		Result->SetNumberField(TEXT("stackCount"), StackCountAfter);
		Result->SetBoolField(TEXT("stackedOntoExisting"), bStackedOntoExisting);
	}
	// Idempotency: an effect the ASC refused - application requirements not met,
	// an immunity tag - changed nothing at all, and saying so is what stops a
	// retry after a timeout from reading as a second successful application.
	// Applying the SAME effect twice is deliberately two applications, because
	// that is what stacking means in GAS; the flag reports refusal, not replay.
	Result->SetBoolField(TEXT("unchanged"), !Active.WasSuccessfullyApplied());
	if (AppliedKeys.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Keys;
		for (const FString& K : AppliedKeys) Keys.Add(MakeShared<FJsonValueString>(K));
		Result->SetArrayField(TEXT("setByCaller"), Keys);
	}

	// The inverse is removing the effect this call put on the ASC, addressed by
	// the handle it just returned. Only a duration or infinite effect HAS a
	// handle: an instant effect executes its modifiers straight into the
	// attribute base and leaves no active effect behind, so no record is emitted
	// for that case rather than one that would remove somebody else's effect.
	if (Active.IsValid())
	{
		TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
		RollbackPayload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		RollbackPayload->SetStringField(TEXT("effectHandle"), Active.ToString());
		RollbackPayload->SetStringField(TEXT("world"), WorldScope);
		if (bStackedOntoExisting)
		{
			// This call added ONE stack to an effect that was already there.
			// Removing the handle outright would take the stacks that predate it
			// as well, so the record undoes exactly the stack this call added.
			RollbackPayload->SetNumberField(TEXT("stacksToRemove"), 1);
		}
		MCPSetRollback(Result, TEXT("remove_effect"), RollbackPayload);
		Result->SetBoolField(TEXT("rollbackLossy"), bStackedOntoExisting);
		if (bStackedOntoExisting)
		{
			Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("This did not create a new active effect: an effect of this class was already on the ASC and its "
					 "stack count went to %d. The inverse removes ONE stack rather than the whole effect, so the "
					 "stacks that predate this call survive - but the duration refresh a stack application performs "
					 "does not come back."),
				StackCountAfter));
		}
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), Active.WasSuccessfullyApplied()
			? TEXT("This effect executed instantly: its modifiers were folded into the attribute base values and no active effect remains to remove. "
				   "There is no inverse call. Restore the individual attributes with gas(set_attribute) if the previous values were captured first.")
			: TEXT("The effect was refused and nothing changed, so there is nothing to undo."));
	}
	return MCPResult(Result);
}

// gas(remove_effect): take an active GameplayEffect back off a live ASC.
//
// The inverse of gas(apply_effect), and the reason that action can offer one at
// all. Addressed by the handle apply_effect returned, which is the only way to
// remove exactly the effect a given call added; effectClass is the fallback for
// a caller that never held the handle, and it removes every active effect of
// that class on the actor.
TSharedPtr<FJsonValue> FGasHandlers::RemoveEffect(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("auto"));

	AActor* Actor = nullptr;
	TSharedPtr<FJsonValue> Err;
	UAbilitySystemComponent* ASC = ResolveASC(Params, Actor, Err);
	if (!ASC) return Err;

	const FString HandleText = OptionalString(Params, TEXT("effectHandle"));
	const FString EffectSpec = OptionalString(Params, TEXT("effectClass"),
		OptionalString(Params, TEXT("effectPath")));
	if (HandleText.IsEmpty() && EffectSpec.IsEmpty())
	{
		return MCPError(TEXT(
			"Pass 'effectHandle' (what gas(apply_effect) reported for a duration or infinite effect) "
			"or 'effectClass' (removes every active effect of that class on this actor). "
			"gas(get_active_effects) lists the handles currently on an actor."));
	}

	// -1 is the engine's own "remove the whole effect regardless of stacks".
	const int32 StacksToRemove = static_cast<int32>(OptionalNumber(Params, TEXT("stacksToRemove"), -1.0));

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());

	TArray<FActiveGameplayEffectHandle> ToRemove;
	if (!HandleText.IsEmpty())
	{
		// A malformed handle is REFUSED, never coerced. FCString::Atoi turns
		// "abc", "null" and " " into 0, and 0 is a legitimately reachable handle,
		// so parsing without this guard would resolve a garbled record onto a
		// real, unrelated active effect and remove it reporting success.
		if (!HandleText.IsNumeric())
		{
			return MCPError(FString::Printf(
				TEXT("'effectHandle' is '%s', which is not a handle. A handle is the integer string gas(apply_effect) "
					 "and gas(get_active_effects) report. Pass that exactly, or use 'effectClass' instead."),
				*HandleText));
		}
		Result->SetStringField(TEXT("effectHandle"), HandleText);
		// Matched by comparing the ASC's own live handles as text rather than by
		// rebuilding one from an int: the FActiveGameplayEffectHandle(int32)
		// constructor is deprecated in 5.8 and produces a handle with a null
		// owning ASC, and a handle that matches nothing has to read as "already
		// gone" rather than as a lookup that silently landed somewhere.
		const FGameplayEffectQuery Query;
		for (const FActiveGameplayEffectHandle& Candidate : ASC->GetActiveEffects(Query))
		{
			if (Candidate.ToString() == HandleText)
			{
				ToRemove.Add(Candidate);
				break;
			}
		}
	}
	else
	{
		UClass* EffectClass = ResolveClassDeriving(EffectSpec, UGameplayEffect::StaticClass());
		if (!EffectClass)
		{
			return MCPError(FString::Printf(
				TEXT("GameplayEffect class not found: %s (pass a content path or class name)"), *EffectSpec));
		}
		Result->SetStringField(TEXT("effect"), EffectClass->GetPathName());
		const FGameplayEffectQuery Query;
		for (const FActiveGameplayEffectHandle& Candidate : ASC->GetActiveEffects(Query))
		{
			const FActiveGameplayEffect* Active = ASC->GetActiveGameplayEffect(Candidate);
			if (Active && Active->Spec.Def && Active->Spec.Def->GetClass() == EffectClass)
			{
				ToRemove.Add(Candidate);
			}
		}
	}

	if (ToRemove.Num() == 0)
	{
		// Replaying a rollback after the effect has already expired must not
		// fail the flow: no matching active effect IS the state asked for.
		Result->SetNumberField(TEXT("removed"), 0);
		Result->SetBoolField(TEXT("alreadyRemoved"), true);
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Nothing was removed, so there is nothing to restore."));
		return MCPResult(Result);
	}

	// The class and level have to be read BEFORE the effects go, or the rollback
	// can only re-apply the effect at a level it never had.
	FString RemovedClassPath;
	double RemovedLevel = 1.0;
	int32 DistinctClasses = 0;
	TSet<FString> SeenClasses;
	int32 Removed = 0;
	for (const FActiveGameplayEffectHandle& Handle : ToRemove)
	{
		const FActiveGameplayEffect* Active = ASC->GetActiveGameplayEffect(Handle);
		const UGameplayEffect* Def = Active ? Active->Spec.Def : nullptr;
		const FString ClassPath = Def ? Def->GetClass()->GetPathName() : FString();
		const double Level = Active ? (double)Active->Spec.GetLevel() : 1.0;
		if (!ASC->RemoveActiveGameplayEffect(Handle, StacksToRemove)) continue;
		++Removed;
		if (!ClassPath.IsEmpty() && !SeenClasses.Contains(ClassPath))
		{
			SeenClasses.Add(ClassPath);
			++DistinctClasses;
			RemovedClassPath = ClassPath;
			RemovedLevel = Level;
		}
	}

	Result->SetNumberField(TEXT("removed"), Removed);
	Result->SetBoolField(TEXT("alreadyRemoved"), Removed == 0);
	if (Removed > 0) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);

	if (Removed == 1 && DistinctClasses == 1)
	{
		TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
		RollbackPayload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		RollbackPayload->SetStringField(TEXT("effectClass"), RemovedClassPath);
		RollbackPayload->SetNumberField(TEXT("level"), RemovedLevel);
		RollbackPayload->SetStringField(TEXT("world"), WorldScope);
		MCPSetRollback(Result, TEXT("apply_effect"), RollbackPayload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Re-applying the effect class at the level it was removed at gives a NEW active effect with a fresh handle. "
				 "Its remaining duration, stack count, SetByCaller magnitudes and original instigator are not restored, "
				 "because none of those survive removal."));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), Removed == 0
			? TEXT("The ASC refused every removal, so nothing changed and there is nothing to restore.")
			: FString::Printf(
				TEXT("%d active effects were removed in one call. A single gas(apply_effect) re-applies one effect, "
					 "so no inverse is emitted for a multi-effect removal; re-apply them individually if that is what you want."),
				Removed));
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

	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("auto"));

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
	// Read back rather than trusting the request: the set's own clamping can
	// land a different number, and the comparison decides both the idempotency
	// marker and whether a rollback record is needed at all.
	const float StoredBase = ASC->GetNumericAttributeBase(Attr);
	const bool bChanged = StoredBase != OldBase;

	// The attribute is named back QUALIFIED with the set it was resolved on.
	// FindAttributeByName accepts "SetName.Property", and a bare property name
	// is ambiguous the moment two attribute sets on the actor declare one with
	// the same name - "Health" is the obvious case - so the inverse has to say
	// which set it means or it can land on the wrong one.
	const FString QualifiedAttribute = SetName.IsEmpty()
		? Attr.GetName() : (SetName + TEXT(".") + Attr.GetName());

	auto Result = MCPSuccess();
	if (bChanged) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);
	Result->SetBoolField(TEXT("unchanged"), !bChanged);
	Result->SetStringField(TEXT("qualifiedAttribute"), QualifiedAttribute);
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("attributeSet"), SetName);
	Result->SetStringField(TEXT("attribute"), Attr.GetName());
	Result->SetNumberField(TEXT("previousBaseValue"), OldBase);
	Result->SetNumberField(TEXT("baseValue"), StoredBase);
	Result->SetNumberField(TEXT("currentValue"), ASC->GetNumericAttribute(Attr));
	if (bChanged)
	{
		// The base value the aggregator held before this call, written back
		// through the same path. SetNumericAttributeBase recomputes the current
		// value from the base and the modifiers that are still active, so
		// restoring the base restores both - the modifiers were never touched.
		TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
		RollbackPayload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		RollbackPayload->SetStringField(TEXT("attribute"), QualifiedAttribute);
		RollbackPayload->SetNumberField(TEXT("value"), OldBase);
		RollbackPayload->SetStringField(TEXT("world"), WorldScope);
		MCPSetRollback(Result, TEXT("set_attribute"), RollbackPayload);
		Result->SetBoolField(TEXT("rollbackLossy"), false);
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The attribute already held this base value - or the set's own clamping refused the write - so "
				 "nothing changed and there is nothing to undo."));
	}
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

// ── Live attribute values on the REGISTERED set (#956) ───────────────────────
//
// gas(get_attribute) / gas(set_attribute) walk whatever the ASC already has
// registered and match by attribute name. That is the right shape once a game
// has begun play and its sets are registered. It answers nothing at all for an
// actor spawned into the editor world for a verification pass, because nothing
// is registered there yet.
//
// These two name the attribute set explicitly, resolve the instance THROUGH THE
// ASC, and register the actor's own subobject when the ASC has not (see the
// long note on ResolveRegisteredAttributeSet above for why constructing a new
// set instead is the trap that silently reads and writes an object no gameplay
// code consults). Both report the registered instance's object path, so a
// caller can prove which object was touched.

namespace
{
	/** Params shared by both live-attribute actions. */
	struct FLiveAttributeRequest
	{
		AActor* Actor = nullptr;
		UAbilitySystemComponent* ASC = nullptr;
		UAttributeSet* Set = nullptr;
		FGameplayAttribute Attribute;
		bool bAdopted = false;
		TArray<FString> AdoptedClassNames;
	};

	/** Everything both actions do before they diverge. */
	bool ResolveLiveAttributeRequest(
		const TSharedPtr<FJsonObject>& Params,
		FLiveAttributeRequest& Out,
		TSharedPtr<FJsonValue>& OutError)
	{
		FString SetSpec;
		if (auto Err = RequireString(Params, TEXT("attributeSet"), SetSpec))
		{
			OutError = Err;
			return false;
		}
		FString AttrName;
		if (auto Err = RequireString(Params, TEXT("attribute"), AttrName))
		{
			OutError = Err;
			return false;
		}

		Out.ASC = ResolveASC(Params, Out.Actor, OutError);
		if (!Out.ASC) return false;

		UClass* SetClass = ResolveClassDeriving(SetSpec, UAttributeSet::StaticClass());
		if (!SetClass)
		{
			OutError = MCPError(FString::Printf(
				TEXT("AttributeSet class not found: %s (pass a content path or a class name)"), *SetSpec));
			return false;
		}

		const bool bAllowAdopt = OptionalBool(Params, TEXT("registerOwnerSets"), true);
		Out.Set = ResolveRegisteredAttributeSet(
			Out.ASC, Out.Actor, SetClass, bAllowAdopt, Out.bAdopted, Out.AdoptedClassNames, OutError);
		if (!Out.Set) return false;

		// Match against the REGISTERED instance's class, not the requested one:
		// the registered set may be a subclass of what the caller named, and its
		// own properties are the ones the aggregator reads.
		FStructProperty* AttrProp = MCPGas::FindAttributeDataProperty(Out.Set->GetClass(), AttrName);
		if (!AttrProp)
		{
			OutError = MCPError(FString::Printf(
				TEXT("Attribute '%s' not found on '%s'. Available: %s"),
				*AttrName,
				*Out.Set->GetClass()->GetName(),
				*MCPGas::ListAttributeDataPropertyNames(Out.Set->GetClass())));
			return false;
		}
		Out.Attribute = FGameplayAttribute(AttrProp);
		return true;
	}

	/** Identity + both values of the attribute, read off the registered set. */
	void WriteLiveAttributeFields(TSharedPtr<FJsonObject> Obj, const FLiveAttributeRequest& Req)
	{
		Obj->SetStringField(TEXT("actorLabel"), Req.Actor->GetActorLabel());
		Obj->SetStringField(TEXT("actorPath"), Req.Actor->GetPathName());
		Obj->SetStringField(TEXT("attributeSet"), Req.Set->GetClass()->GetName());
		// The proof of which object was touched. A second, disconnected instance
		// would show a different path here.
		Obj->SetStringField(TEXT("attributeSetInstance"), Req.Set->GetPathName());
		Obj->SetStringField(TEXT("attribute"), Req.Attribute.GetName());
		Obj->SetBoolField(TEXT("registeredOnAsc"), true);
		Obj->SetBoolField(TEXT("registeredByThisCall"), Req.bAdopted);
		if (Req.AdoptedClassNames.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Adopted;
			for (const FString& Name : Req.AdoptedClassNames)
			{
				Adopted.Add(MakeShared<FJsonValueString>(Name));
			}
			Obj->SetArrayField(TEXT("registeredSets"), Adopted);
		}

		// Read straight off the registered instance. GetNumericValue is the
		// engine's own accessor for the current value of the attribute data.
		Obj->SetNumberField(TEXT("currentValue"), Req.Attribute.GetNumericValue(Req.Set));
		if (const FGameplayAttributeData* Data = Req.Attribute.GetGameplayAttributeData(Req.Set))
		{
			Obj->SetNumberField(TEXT("baseValue"), Data->GetBaseValue());
		}
		// The aggregator's view of the same attribute. Identical to the values
		// above until an active GameplayEffect is modifying it, and the pair is
		// what tells you an effect really landed.
		Obj->SetNumberField(TEXT("aggregatorBaseValue"), Req.ASC->GetNumericAttributeBase(Req.Attribute));
		Obj->SetNumberField(TEXT("aggregatorCurrentValue"), Req.ASC->GetNumericAttribute(Req.Attribute));
	}
}

TSharedPtr<FJsonValue> FGasHandlers::GetLiveAttributeValue(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FLiveAttributeRequest Req;
	TSharedPtr<FJsonValue> Err;
	if (!ResolveLiveAttributeRequest(Params, Req, Err)) return Err;

	auto Result = MCPSuccess();
	WriteLiveAttributeFields(Result, Req);
	// This reads a value, but with registerOwnerSets left at its default it may
	// first register the actor's own attribute sets on the ASC, which is a
	// change to the live world - which is why the action is classified as a
	// mutation. The flag says whether this call was the one that did it.
	Result->SetBoolField(TEXT("unchanged"), !Req.bAdopted);
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), Req.bAdopted
		? TEXT("This call registered the actor's own attribute set(s) on its AbilitySystemComponent, which is exactly what "
			   "InitializeComponent does at BeginPlay. No action in this surface un-registers one, and un-registering would "
			   "not be a restoration anyway: the set belongs to the actor and the engine would register it again the moment "
			   "the world begins play. Pass registerOwnerSets false to read without registering.")
		: TEXT("Nothing was registered and no value was written, so there is nothing to undo."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::SetLiveAttributeValue(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	double NewValue = 0.0;
	if (!Params->TryGetNumberField(TEXT("value"), NewValue))
	{
		return MCPError(TEXT("Missing required parameter 'value'"));
	}

	// "current" writes the FGameplayAttributeData's current value in place,
	// which is what verifying a mid-combat state needs. "base" goes through the
	// ASC so the aggregator recomputes the current value from it, which is what
	// a durable change needs. They are not interchangeable, so the caller says.
	const FString ValueType = OptionalString(Params, TEXT("valueType"), TEXT("current")).ToLower();
	if (ValueType != TEXT("current") && ValueType != TEXT("base"))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown valueType '%s'. Use \"current\" (write the attribute data in place) or \"base\" (write through the ASC aggregator)."),
			*ValueType));
	}

	FLiveAttributeRequest Req;
	TSharedPtr<FJsonValue> Err;
	if (!ResolveLiveAttributeRequest(Params, Req, Err)) return Err;

	const float PreviousCurrent = Req.Attribute.GetNumericValue(Req.Set);
	const float PreviousBase = Req.ASC->GetNumericAttributeBase(Req.Attribute);

	if (ValueType == TEXT("base"))
	{
		// Recalculates the current value through the aggregator, so active
		// modifiers stay consistent.
		Req.ASC->SetNumericAttributeBase(Req.Attribute, static_cast<float>(NewValue));
	}
	else
	{
		// SetNumericValueChecked takes a mutable reference because the set's
		// PreAttributeChange is allowed to clamp the value, so the number that
		// lands can differ from the number asked for. That is why the result
		// reports what was actually stored rather than echoing the request.
		float Applied = static_cast<float>(NewValue);
		Req.Attribute.SetNumericValueChecked(Applied, Req.Set);
	}

	// Read back rather than trusting the request: PreAttributeChange is allowed
	// to clamp, and a write that clamped to the value already there changed
	// nothing at all.
	const float StoredCurrent = Req.Attribute.GetNumericValue(Req.Set);
	const float StoredBase = Req.ASC->GetNumericAttributeBase(Req.Attribute);
	const bool bValueChanged = ValueType == TEXT("base")
		? StoredBase != PreviousBase : StoredCurrent != PreviousCurrent;

	auto Result = MCPSuccess();
	// Registering the actor's own sets is a change to the live world too, so a
	// call that only did that is still not "unchanged".
	const bool bChanged = bValueChanged || Req.bAdopted;
	if (bChanged) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);
	Result->SetBoolField(TEXT("unchanged"), !bChanged);
	Result->SetBoolField(TEXT("valueChanged"), bValueChanged);
	WriteLiveAttributeFields(Result, Req);
	Result->SetStringField(TEXT("valueType"), ValueType);
	Result->SetNumberField(TEXT("requestedValue"), NewValue);
	Result->SetNumberField(TEXT("previousCurrentValue"), PreviousCurrent);
	Result->SetNumberField(TEXT("previousBaseValue"), PreviousBase);

	if (!bValueChanged)
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), Req.bAdopted
			? TEXT("The attribute already held this value - or the set's own PreAttributeChange clamped the write back "
				   "to it - so no value has to be restored. The attribute set registration this call performed is not "
				   "undone either; it is what InitializeComponent does at BeginPlay.")
			: TEXT("The attribute already held this value, or the set's own PreAttributeChange clamped the write back "
				   "to it, so nothing changed and there is nothing to undo."));
		return MCPResult(Result);
	}

	// The inverse writes the value this call overwrote, through the same
	// valueType: "base" goes back through the aggregator and "current" back into
	// the attribute data, and the two are not interchangeable, so the record
	// carries the one that was used. The set is named by its REGISTERED class
	// path, which is what was actually written - it can be a subclass of what
	// the caller asked for.
	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	RollbackPayload->SetStringField(TEXT("actorPath"), Req.Actor->GetPathName());
	RollbackPayload->SetStringField(TEXT("attributeSet"), Req.Set->GetClass()->GetPathName());
	RollbackPayload->SetStringField(TEXT("attribute"), Req.Attribute.GetName());
	RollbackPayload->SetStringField(TEXT("valueType"), ValueType);
	RollbackPayload->SetNumberField(TEXT("value"),
		ValueType == TEXT("base") ? PreviousBase : PreviousCurrent);
	RollbackPayload->SetStringField(TEXT("world"), OptionalString(Params, TEXT("world"), TEXT("auto")));
	MCPSetRollback(Result, TEXT("set_live_attribute_value"), RollbackPayload);
	Result->SetBoolField(TEXT("rollbackLossy"), Req.bAdopted);
	if (Req.bAdopted)
	{
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The value comes back, but this call also registered the actor's own attribute set(s) on its "
				 "AbilitySystemComponent to reach it, and that registration is not undone. It is what "
				 "InitializeComponent would have done at BeginPlay, so it is left in place."));
	}
	return MCPResult(Result);
}

// #587 get_asc_state - introspect a live ASC: granted ability specs (class,
// level, input id, active state, dynamic source tags) plus the ASC's owned
// gameplay tags. The read half of #587 (input injection lives in pie-studio).
TSharedPtr<FJsonValue> FGasHandlers::GetAscState(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	AActor* Actor = nullptr;
	TSharedPtr<FJsonValue> Err;
	UAbilitySystemComponent* ASC = ResolveASC(Params, Actor, Err);
	if (!ASC) return Err;

	// Granted / activatable ability specs.
	TArray<TSharedPtr<FJsonValue>> Abilities;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("class"), Spec.Ability ? Spec.Ability->GetClass()->GetName() : TEXT("None"));
		A->SetNumberField(TEXT("level"), Spec.Level);
		A->SetNumberField(TEXT("inputID"), Spec.InputID);
		A->SetStringField(TEXT("handle"), Spec.Handle.ToString());
		A->SetBoolField(TEXT("active"), Spec.IsActive());
		A->SetNumberField(TEXT("activeCount"), Spec.ActiveCount);

		TArray<TSharedPtr<FJsonValue>> DynTags;
		for (const FGameplayTag& T : Spec.GetDynamicSpecSourceTags())
		{
			DynTags.Add(MakeShared<FJsonValueString>(T.ToString()));
		}
		A->SetArrayField(TEXT("dynamicTags"), DynTags);
		Abilities.Add(MakeShared<FJsonValueObject>(A));
	}

	// Owned gameplay tags currently on the ASC.
	FGameplayTagContainer Owned;
	ASC->GetOwnedGameplayTags(Owned);
	TArray<TSharedPtr<FJsonValue>> OwnedJson;
	for (const FGameplayTag& T : Owned)
	{
		OwnedJson.Add(MakeShared<FJsonValueString>(T.ToString()));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetArrayField(TEXT("abilities"), Abilities);
	Result->SetNumberField(TEXT("abilityCount"), Abilities.Num());
	Result->SetArrayField(TEXT("ownedTags"), OwnedJson);
	Result->SetNumberField(TEXT("ownedTagCount"), OwnedJson.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::InitAsc(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	AActor* Actor = nullptr;
	TSharedPtr<FJsonValue> Err;
	UAbilitySystemComponent* ASC = ResolveASC(Params, Actor, Err);
	if (!ASC) return Err;

	// Read before the init, so the result can say whether this call is what put
	// the ASC into the state it is now in, or whether it was already there.
	const FGameplayAbilityActorInfo* ExistingInfo = ASC->AbilityActorInfo.Get();
	const bool bActorInfoAlreadySet = ExistingInfo != nullptr
		&& ExistingInfo->OwnerActor.Get() == Actor
		&& ExistingInfo->AvatarActor.Get() == Actor;

	// Establish owner/avatar so abilities activate and effect contexts target
	// correctly. Safe to call again; a game's own pawn may also init the ASC.
	ASC->InitAbilityActorInfo(Actor, Actor);

	// #956: register the actor's OWN attribute set subobjects first, the way
	// InitializeComponent would have at BeginPlay. Skipping this step and going
	// straight to "the ASC has none, so construct one" is what produced a
	// second, disconnected instance next to the actor's own: reads and writes
	// then landed on an object the gameplay code never consults, and nothing
	// reported an error. Adoption is idempotent and costs one hash walk.
	TArray<FString> AdoptedSets;
	MCPGas::AdoptOwnerAttributeSets(ASC, Actor, AdoptedSets);

	// Optionally guarantee an attribute set exists on the ASC. This is what lets
	// a bridge-authored test actor have live attributes without shipping an init
	// DataTable. A set is only constructed when the actor genuinely owns none of
	// that class, and the result says which of the two happened.
	FString CreatedSet;
	bool bConstructedSet = false;
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
			bConstructedSet = true;
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
	if (!CreatedSet.IsEmpty())
	{
		Result->SetStringField(TEXT("attributeSet"), CreatedSet);
		// The caller needs to know which happened. A constructed set starts at
		// its class defaults; an adopted one carries whatever the actor has.
		Result->SetBoolField(TEXT("attributeSetConstructed"), bConstructedSet);
	}
	if (AdoptedSets.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Adopted;
		for (const FString& Name : AdoptedSets) Adopted.Add(MakeShared<FJsonValueString>(Name));
		Result->SetArrayField(TEXT("registeredOwnerSets"), Adopted);
	}
	Result->SetNumberField(TEXT("attributeCount"), AttrCount);

	// Idempotency: initialising an ASC that is already initialised for this
	// actor, with every set it needs already registered, moves nothing. Saying
	// so is what lets a replayed flow step tell "already done" from "done now".
	const bool bChanged = !bActorInfoAlreadySet || AdoptedSets.Num() > 0 || bConstructedSet;
	Result->SetBoolField(TEXT("unchanged"), !bChanged);
	if (bChanged) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);
	Result->SetBoolField(TEXT("actorInfoAlreadySet"), bActorInfoAlreadySet);

	// No inverse. InitAbilityActorInfo has no counterpart that returns an ASC to
	// "never initialised", and it is what a game's own pawn does at BeginPlay
	// anyway, so undoing it would put the ASC into a state the engine treats as
	// a bug rather than into the state it was in. Registering the actor's own
	// attribute sets is likewise what InitializeComponent does. The one part
	// that IS an addition - a set CONSTRUCTED because the actor owned none - is
	// named here so a caller can decide what to do about it.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), bConstructedSet
		? FString::Printf(TEXT(
			"InitAbilityActorInfo has no inverse: there is no call that returns an AbilitySystemComponent to "
			"uninitialised, and it is what BeginPlay would have done. This call also CONSTRUCTED a '%s' the actor did "
			"not own, which stays on the ASC; it lives only in memory, so ending PIE or reloading the level clears it."),
			*CreatedSet)
		: TEXT("InitAbilityActorInfo has no inverse: there is no call that returns an AbilitySystemComponent to "
			   "uninitialised, and it is what BeginPlay would have done. Registering the actor's own attribute sets is "
			   "the same story - InitializeComponent does it at BeginPlay, so it is left in place."));
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// Granting, diagnosis and the reason an activation failed.
//
// Everything above operates on attributes and effects. What was missing is the
// half that decides whether an authored ability does anything at all: an
// ability the ASC has never been given cannot activate, and when activation
// does fail GAS reports it only to the log, as a line that names neither the
// ability nor the reason in a form anything can read back.
//
// These are deliberately NOT property writes. An ability's configuration is
// reachable through asset(set_property) on its Blueprint CDO, because that
// action resolves a Blueprint path to the generated class default object and
// walks nested properties. What no property write can do is call GiveAbility,
// read the active effect container, or ask the ability whether it would
// activate right now and why not.
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	/**
	 * Resolve a UGameplayAbility subclass from a content path or a class name.
	 *
	 * Accepts a Blueprint path ("/Game/Abilities/GA_Fireball"), the generated
	 * class path ("/Game/Abilities/GA_Fireball.GA_Fireball_C") and a native
	 * class name ("UGameplayAbility"), because a caller who just created the
	 * asset has the first spelling and a caller reading get_asc_state has the
	 * second.
	 */
	UClass* ResolveAbilityClass(const FString& Spec, TSharedPtr<FJsonValue>& OutError)
	{
		if (Spec.IsEmpty())
		{
			OutError = MCPError(TEXT("Missing ability class"));
			return nullptr;
		}

		// A generated-class path loads directly.
		if (UClass* Direct = LoadObject<UClass>(nullptr, *Spec))
		{
			if (Direct->IsChildOf(UGameplayAbility::StaticClass())) return Direct;
			OutError = MCPError(FString::Printf(
				TEXT("'%s' is a %s, not a GameplayAbility"), *Spec, *Direct->GetName()));
			return nullptr;
		}

		// A Blueprint path: take its generated class.
		TSharedPtr<FJsonValue> Ignored;
		if (UGameplayAbility* CDO = LoadBlueprintCDO<UGameplayAbility>(Spec, Ignored))
		{
			return CDO->GetClass();
		}

		// A bare "_C"-less content path, then a native class name.
		if (UClass* Generated = LoadObject<UClass>(nullptr, *(Spec + TEXT("_C"))))
		{
			if (Generated->IsChildOf(UGameplayAbility::StaticClass())) return Generated;
		}
		if (UClass* Native = FindObject<UClass>(nullptr, *Spec))
		{
			if (Native->IsChildOf(UGameplayAbility::StaticClass())) return Native;
		}

		OutError = MCPError(FString::Printf(
			TEXT("Could not resolve '%s' to a GameplayAbility class. Pass the Blueprint path "
				 "(/Game/Abilities/GA_Fireball), its generated class path (…_C), or a native class name."),
			*Spec));
		return nullptr;
	}

	/** One ability spec, as JSON. Shared by grant, revoke and the tracer. */
	TSharedPtr<FJsonObject> DescribeSpec(const FGameplayAbilitySpec& Spec)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		// The int is private; ToString is the public identity.
		Out->SetStringField(TEXT("handle"), Spec.Handle.ToString());
		Out->SetStringField(TEXT("abilityClass"), Spec.Ability ? Spec.Ability->GetClass()->GetPathName() : TEXT("None"));
		Out->SetNumberField(TEXT("level"), Spec.Level);
		Out->SetNumberField(TEXT("inputID"), Spec.InputID);
		Out->SetBoolField(TEXT("active"), Spec.IsActive());
		return Out;
	}
}

TSharedPtr<FJsonValue> FGasHandlers::GrantAbility(const TSharedPtr<FJsonObject>& Params)
{
	FString AbilitySpec;
	if (auto Err = RequireString(Params, TEXT("abilityClass"), AbilitySpec)) return Err;

	AActor* Actor = nullptr;
	TSharedPtr<FJsonValue> Error;
	UAbilitySystemComponent* ASC = ResolveASC(Params, Actor, Error);
	if (!ASC) return Error;

	UClass* AbilityClass = ResolveAbilityClass(AbilitySpec, Error);
	if (!AbilityClass) return Error;

	const int32 Level = static_cast<int32>(OptionalNumber(Params, TEXT("level"), 1.0));
	const int32 InputID = static_cast<int32>(OptionalNumber(Params, TEXT("inputId"), -1.0));

	// Granting is server-authoritative in GAS. On a client ASC GiveAbility is a
	// no-op that logs and returns an invalid handle, which would otherwise read
	// as success here.
	if (!ASC->IsOwnerActorAuthoritative())
	{
		return MCPError(FString::Printf(
			TEXT("'%s' ASC is not authoritative, and GiveAbility only runs on the authority. "
				 "Target the server/primary PIE world (world=\"pie\", pieInstance=0)."),
			*Actor->GetActorLabel()));
	}

	// Idempotent: granting the same class twice would leave two specs and two
	// handles for what the caller thinks of as one ability.
	for (const FGameplayAbilitySpec& Existing : ASC->GetActivatableAbilities())
	{
		if (Existing.Ability && Existing.Ability->GetClass() == AbilityClass)
		{
			auto Already = MCPSuccess();
			MCPSetExisted(Already);
			Already->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
			Already->SetObjectField(TEXT("spec"), DescribeSpec(Existing));
			Already->SetStringField(TEXT("note"), TEXT("This ability class was already granted; the existing spec is returned."));
			return MCPResult(Already);
		}
	}

	FGameplayAbilitySpec Spec(AbilityClass, Level, InputID, Actor);
	const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(Spec);
	if (!Handle.IsValid())
	{
		return MCPError(FString::Printf(
			TEXT("GiveAbility refused '%s' on '%s'."), *AbilityClass->GetName(), *Actor->GetActorLabel()));
	}

	const FGameplayAbilitySpec* Granted = ASC->FindAbilitySpecFromHandle(Handle);
	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	if (Granted) Result->SetObjectField(TEXT("spec"), DescribeSpec(*Granted));
	Result->SetNumberField(TEXT("activatableCount"), ASC->GetActivatableAbilities().Num());

	// Undoing a grant is a revoke of the same class on the same actor.
	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	RollbackPayload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	RollbackPayload->SetStringField(TEXT("abilityClass"), AbilityClass->GetPathName());
	MCPSetRollback(Result, TEXT("revoke_ability"), RollbackPayload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::RevokeAbility(const TSharedPtr<FJsonObject>& Params)
{
	FString AbilitySpec;
	if (auto Err = RequireString(Params, TEXT("abilityClass"), AbilitySpec)) return Err;

	AActor* Actor = nullptr;
	TSharedPtr<FJsonValue> Error;
	UAbilitySystemComponent* ASC = ResolveASC(Params, Actor, Error);
	if (!ASC) return Error;

	UClass* AbilityClass = ResolveAbilityClass(AbilitySpec, Error);
	if (!AbilityClass) return Error;

	if (!ASC->IsOwnerActorAuthoritative())
	{
		return MCPError(TEXT("ClearAbility only runs on the authority. Target the server/primary PIE world."));
	}

	// The level and input binding have to be read BEFORE the specs go, or the
	// rollback can only restore the ability at defaults it never had.
	TArray<FGameplayAbilitySpecHandle> ToClear;
	int32 RevokedLevel = 1;
	int32 RevokedInputID = INDEX_NONE;
	for (const FGameplayAbilitySpec& Existing : ASC->GetActivatableAbilities())
	{
		if (Existing.Ability && Existing.Ability->GetClass() == AbilityClass)
		{
			ToClear.Add(Existing.Handle);
			RevokedLevel = Existing.Level;
			RevokedInputID = Existing.InputID;
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("abilityClass"), AbilityClass->GetPathName());
	Result->SetNumberField(TEXT("revoked"), ToClear.Num());

	// Idempotent, and it says which happened: a caller replaying a rollback
	// should not get an error for work already undone.
	if (ToClear.Num() == 0)
	{
		Result->SetBoolField(TEXT("alreadyRevoked"), true);
		return MCPResult(Result);
	}

	for (const FGameplayAbilitySpecHandle& Handle : ToClear) ASC->ClearAbility(Handle);
	Result->SetNumberField(TEXT("activatableCount"), ASC->GetActivatableAbilities().Num());

	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	RollbackPayload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	RollbackPayload->SetStringField(TEXT("abilityClass"), AbilityClass->GetPathName());
	RollbackPayload->SetNumberField(TEXT("level"), RevokedLevel);
	RollbackPayload->SetNumberField(TEXT("inputId"), RevokedInputID);
	MCPSetRollback(Result, TEXT("grant_ability"), RollbackPayload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::GetActiveEffects(const TSharedPtr<FJsonObject>& Params)
{
	AActor* Actor = nullptr;
	TSharedPtr<FJsonValue> Error;
	UAbilitySystemComponent* ASC = ResolveASC(Params, Actor, Error);
	if (!ASC) return Error;

	// Everything, rather than a query: the caller is diagnosing, and a filter
	// that hid the effect they were looking for would be the whole problem.
	const FGameplayEffectQuery Query;
	TArray<FActiveGameplayEffectHandle> Handles = ASC->GetActiveEffects(Query);

	TArray<TSharedPtr<FJsonValue>> Effects;
	for (const FActiveGameplayEffectHandle& Handle : Handles)
	{
		const FActiveGameplayEffect* Active = ASC->GetActiveGameplayEffect(Handle);
		if (!Active) continue;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		const UGameplayEffect* Def = Active->Spec.Def;
		Entry->SetStringField(TEXT("effectClass"), Def ? Def->GetClass()->GetPathName() : TEXT("None"));
		// The int is private; ToString is the public identity.
		Entry->SetStringField(TEXT("handle"), Handle.ToString());
		Entry->SetNumberField(TEXT("level"), Active->Spec.GetLevel());
		Entry->SetNumberField(TEXT("stackCount"), Active->Spec.GetStackCount());
		Entry->SetNumberField(TEXT("duration"), Active->GetDuration());
		Entry->SetNumberField(TEXT("timeRemaining"), Active->GetTimeRemaining(ASC->GetWorld()->GetTimeSeconds()));
		Entry->SetBoolField(TEXT("inhibited"), Active->bIsInhibited);

		// Which actor caused this, which is the question asked when an effect
		// is present and nobody knows where it came from.
		if (const AActor* Instigator = Active->Spec.GetEffectContext().GetInstigator())
		{
			Entry->SetStringField(TEXT("instigator"), Instigator->GetActorLabel());
		}

		FGameplayTagContainer GrantedTags;
		Active->Spec.GetAllGrantedTags(GrantedTags);
		TArray<TSharedPtr<FJsonValue>> TagList;
		for (const FGameplayTag& Tag : GrantedTags) TagList.Add(MakeShared<FJsonValueString>(Tag.ToString()));
		Entry->SetArrayField(TEXT("grantedTags"), TagList);

		Effects.Add(MakeShared<FJsonValueObject>(Entry));
	}

	FGameplayTagContainer Owned;
	ASC->GetOwnedGameplayTags(Owned);
	TArray<TSharedPtr<FJsonValue>> OwnedList;
	for (const FGameplayTag& Tag : Owned) OwnedList.Add(MakeShared<FJsonValueString>(Tag.ToString()));

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetNumberField(TEXT("effectCount"), Effects.Num());
	Result->SetArrayField(TEXT("activeEffects"), Effects);
	Result->SetArrayField(TEXT("ownedTags"), OwnedList);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::TraceAbilityActivation(const TSharedPtr<FJsonObject>& Params)
{
	FString AbilitySpec;
	if (auto Err = RequireString(Params, TEXT("abilityClass"), AbilitySpec)) return Err;

	AActor* Actor = nullptr;
	TSharedPtr<FJsonValue> Error;
	UAbilitySystemComponent* ASC = ResolveASC(Params, Actor, Error);
	if (!ASC) return Error;

	UClass* AbilityClass = ResolveAbilityClass(AbilitySpec, Error);
	if (!AbilityClass) return Error;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("abilityClass"), AbilityClass->GetPathName());

	// This only reads unless `activate` is passed, which is why the action is
	// classified "unknown" rather than as a mutation. Both markers are set here
	// so that every early return below - and there are several - reports the
	// truth for the path it took: nothing fired, nothing to undo.
	Result->SetBoolField(TEXT("unchanged"), true);
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Nothing was activated, so there is nothing to undo."));

	TArray<TSharedPtr<FJsonValue>> Blockers;
	const auto Block = [&Blockers](const FString& Reason)
	{
		Blockers.Add(MakeShared<FJsonValueString>(Reason));
	};

	// 1. Granted at all. This is the single most common reason an authored
	//    ability does nothing, and it produces no log line worth reading.
	const FGameplayAbilitySpec* Spec = nullptr;
	for (const FGameplayAbilitySpec& Candidate : ASC->GetActivatableAbilities())
	{
		if (Candidate.Ability && Candidate.Ability->GetClass() == AbilityClass)
		{
			Spec = &Candidate;
			break;
		}
	}
	if (!Spec)
	{
		Block(TEXT("not granted: this ability class is not in the ASC's activatable abilities. Call gas(grant_ability) first."));
		Result->SetBoolField(TEXT("granted"), false);
		Result->SetBoolField(TEXT("wouldActivate"), false);
		Result->SetArrayField(TEXT("blockedBy"), Blockers);
		return MCPResult(Result);
	}

	Result->SetBoolField(TEXT("granted"), true);
	Result->SetObjectField(TEXT("spec"), DescribeSpec(*Spec));

	// Spec->Ability is a TObjectPtr, so the two branches have no common type
	// without naming it. The primary instance is preferred because an
	// instanced ability's live state is what decides whether it can activate;
	// a non-instanced one only ever has the CDO.
	UGameplayAbility* Ability = Spec->GetPrimaryInstance();
	if (!Ability) Ability = Spec->Ability;
	if (!Ability)
	{
		Block(TEXT("the spec has no ability instance or CDO to interrogate."));
		Result->SetBoolField(TEXT("wouldActivate"), false);
		Result->SetArrayField(TEXT("blockedBy"), Blockers);
		return MCPResult(Result);
	}

	// 2. Already running, and not allowed to retrigger. Read through
	//    reflection because the flag is protected on UGameplayAbility; it is a
	//    UPROPERTY, so this is the supported way in rather than a workaround.
	bool bRetrigger = false;
	if (const FBoolProperty* RetriggerProp = CastField<FBoolProperty>(
			Ability->GetClass()->FindPropertyByName(TEXT("bRetriggerInstancedAbility"))))
	{
		bRetrigger = RetriggerProp->GetPropertyValue_InContainer(Ability);
	}
	if (Spec->IsActive() && !bRetrigger)
	{
		Block(TEXT("already active, and bRetriggerInstancedAbility is false."));
	}

	// 3. Tags: the ability's own blockers, and what the ASC currently owns.
	FGameplayTagContainer OwnedTags;
	ASC->GetOwnedGameplayTags(OwnedTags);

	const auto ReportTags = [&](const TCHAR* Field, const FGameplayTagContainer& Tags)
	{
		if (Tags.IsEmpty()) return;
		TArray<TSharedPtr<FJsonValue>> List;
		for (const FGameplayTag& Tag : Tags) List.Add(MakeShared<FJsonValueString>(Tag.ToString()));
		Result->SetArrayField(Field, List);
	};

	// CanActivateAbility reports the tags it refused over through a single
	// OptionalRelevantTags container. The engine does not separate "the owner
	// has a tag that blocks this" from "the ability requires a tag the owner
	// lacks", so neither does this: the tags are reported as what they are,
	// the ones relevant to the refusal, rather than sorted into two buckets on
	// a guess.
	FGameplayTagContainer RelevantTags;
	const FGameplayAbilityActorInfo* ActorInfo = ASC->AbilityActorInfo.Get();

	// 3a. The ASC has to have been initialised before anything can activate.
	//     An actor spawned into the editor world never ran InitAbilityActorInfo,
	//     because there is no BeginPlay in a world that has not begun play, so
	//     CanActivateAbility refuses with no tag to point at. Without this the
	//     result read "wouldActivate: false, blockedBy: []", which is the exact
	//     self-contradiction this action exists to stop a caller hitting.
	const bool bAscInitialised = ActorInfo != nullptr
		&& ActorInfo->OwnerActor.IsValid()
		&& ActorInfo->AvatarActor.IsValid();
	if (!bAscInitialised)
	{
		Block(TEXT("the ASC has no valid actor info: InitAbilityActorInfo has not run. "
				   "In PIE that happens at BeginPlay; in the editor world call gas(init_asc) first."));
	}
	Result->SetBoolField(TEXT("ascInitialized"), bAscInitialised);
	const bool bCanActivate = Ability->CanActivateAbility(
		Spec->Handle,
		ActorInfo,
		/*SourceTags*/ nullptr,
		/*TargetTags*/ nullptr,
		&RelevantTags);

	ReportTags(TEXT("ownedTags"), OwnedTags);
	ReportTags(TEXT("relevantTags"), RelevantTags);

	if (!bCanActivate && !RelevantTags.IsEmpty())
	{
		Block(FString::Printf(
			TEXT("refused over tags: %s. Compare against ownedTags, and against the ability's "
				 "ActivationRequiredTags / ActivationBlockedTags."),
			*RelevantTags.ToStringSimple()));
	}

	// 4. Cooldown, reported with the time left rather than as a bare "no".
	//    CheckCooldown returns TRUE when the ability is free to use, so a
	//    false here is the cooldown being active.
	FGameplayTagContainer CooldownTags;
	if (!Ability->CheckCooldown(Spec->Handle, ActorInfo, &CooldownTags))
	{
		float Remaining = 0.f;
		float Duration = 0.f;
		Ability->GetCooldownTimeRemainingAndDuration(Spec->Handle, ActorInfo, Remaining, Duration);
		Result->SetNumberField(TEXT("cooldownRemaining"), Remaining);
		Result->SetNumberField(TEXT("cooldownDuration"), Duration);
		Block(FString::Printf(TEXT("on cooldown: %.2fs of %.2fs remaining."), Remaining, Duration));
	}

	// 5. Cost, which is the other half of "it just does not fire".
	FGameplayTagContainer CostTags;
	if (!Ability->CheckCost(Spec->Handle, ActorInfo, &CostTags))
	{
		const UGameplayEffect* CostEffect = Ability->GetCostGameplayEffect();
		Block(FString::Printf(
			TEXT("cost not met%s."),
			CostEffect ? *FString::Printf(TEXT(" (%s)"), *CostEffect->GetClass()->GetName()) : TEXT("")));
	}

	// A verdict of "will not activate" with an empty reason list is worse than
	// no answer, so the last resort names itself as the last resort.
	if (!bCanActivate && Blockers.Num() == 0)
	{
		Block(TEXT("CanActivateAbility refused without reporting a reason. The remaining "
				   "explanation is the ability's own CanActivateAbility override, or a "
				   "Blueprint-side check inside it."));
	}

	Result->SetBoolField(TEXT("wouldActivate"), bCanActivate && Blockers.Num() == 0);
	Result->SetArrayField(TEXT("blockedBy"), Blockers);
	if (bCanActivate && Blockers.Num() == 0)
	{
		Result->SetStringField(TEXT("note"), TEXT("Nothing is blocking activation right now."));
	}

	// Optionally prove it, rather than only predicting it.
	if (OptionalBool(Params, TEXT("activate"), false))
	{
		const bool bActivated = ASC->TryActivateAbility(Spec->Handle);
		Result->SetBoolField(TEXT("activated"), bActivated);
		if (bActivated)
		{
			Result->SetBoolField(TEXT("unchanged"), false);
			// Activating an ability RUNS it: its cost is committed, its cooldown
			// effect is applied, its tags are granted and whatever it does to the
			// world has already happened. Cancelling it afterwards stops the rest
			// of the ability, it does not put those back, so no inverse is
			// offered rather than one that would only look like an undo.
			Result->SetStringField(TEXT("rollbackNote"),
				TEXT("The ability activated and has already committed its cost, applied its cooldown and run its effects. "
					 "There is no call that un-activates it: cancelling an ability ends what is still running rather than "
					 "restoring what it already did. Capture the state first with gas(capture_gas_state) if you need a diff."));
		}
		if (!bActivated && Blockers.Num() == 0)
		{
			Result->SetStringField(TEXT("activationNote"), TEXT(
				"TryActivateAbility refused even though no blocker was found. That usually means the "
				"ability's own CanActivateAbility override, or a Blueprint-side check, refused it."));
		}
	}

	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// Shared resolution, exported for the other GAS translation units.
//
// The four helpers above are file-local, and the module is a unity build, so a
// second copy of any of them in another .cpp is a redefinition on some grouping
// (and the grouping shifts with file count, file order and the adaptive-unity
// working set, so the duplicate builds clean locally and breaks elsewhere).
// These thin wrappers are the one exported spelling. They forward rather than
// move the definitions, so the actions already verified against a live editor
// keep calling exactly the code they were verified against.
// ─────────────────────────────────────────────────────────────────────────────

namespace MCPGas
{

UAbilitySystemComponent* ResolveActorASC(
	const TSharedPtr<FJsonObject>& Params,
	AActor*& OutActor,
	TSharedPtr<FJsonValue>& OutError)
{
	return ResolveASC(Params, OutActor, OutError);
}

UClass* ResolveGameplayAbilityClass(const FString& Spec, TSharedPtr<FJsonValue>& OutError)
{
	return ResolveAbilityClass(Spec, OutError);
}

UClass* ResolveClassDerivingFrom(const FString& Spec, UClass* Base)
{
	return ResolveClassDeriving(Spec, Base);
}

TSharedPtr<FJsonObject> DescribeAbilitySpec(const FGameplayAbilitySpec& Spec)
{
	return DescribeSpec(Spec);
}

}
