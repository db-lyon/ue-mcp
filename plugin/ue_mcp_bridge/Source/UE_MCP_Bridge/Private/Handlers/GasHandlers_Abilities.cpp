// GAS ability wiring: input binding, gameplay cue linking, cue coverage, and
// the attribute-set audit.
//
// ── Why each of these is a handler and not a property write ──────────────────
//
// The house rule is that `asset(set_property)` and `editor(set_property)` reach
// any UPROPERTY on any asset, CDO or nested subobject, so a handler that only
// writes a property is redundant. Every action here fails that test for a
// stated reason:
//
//   bind_ability_input / clear_ability_input / send_ability_input
//       FGameplayAbilitySpec lives in a fast-array serialiser on a LIVE
//       AbilitySystemComponent. There is no asset to aim a property write at,
//       and a write that skipped MarkAbilitySpecDirty would not replicate.
//       AbilityLocalInputPressed / AbilityLocalInputReleased are engine calls.
//
//   add_effect_cue / remove_effect_cue
//       UGameplayEffect::GameplayCues IS a UPROPERTY array, so a property write
//       could reach it. It would also happily store a tag the tag manager has
//       never heard of, a tag outside the GameplayCue root (which the cue
//       system will never route), or a tag no notify answers - three ways to
//       author a cue that silently does nothing, which is exactly the failure
//       this ticket exists to stop. The handler checks all three and says which
//       one it hit.
//
//   validate_cue_coverage
//       A reference walk across every GameplayEffect and every cue notify in
//       the project, resolving each cue tag through the tag hierarchy the way
//       the cue system routes it. No property read answers that.
//
//   audit_attribute_set
//       Reports what is PROVABLE about an attribute set: whether it can clamp
//       at all, which attributes replicate, and which look like meta
//       attributes. Deliberately not a "configure_attribute_clamping" setter,
//       because UE 5.8 has no data-driven clamp: UAttributeSet::PreAttributeChange
//       and PreAttributeBaseChange are plain C++ virtuals, not UFUNCTIONs
//       (AttributeSet.h:220 and :230), and FAttributeMetaData's MinValue /
//       MaxValue (AttributeSet.h:283-286) only seed initial values. A Blueprint
//       AttributeSet therefore cannot clamp, and an action claiming to
//       configure clamping on one would be a lie. This says so instead, and
//       optionally MEASURES an existing native clamp rather than predicting it.

#include "GasHandlers.h"
#include "HandlerUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AttributeSet.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayAbilitySpec.h"
#include "GameplayEffect.h"
#include "GameplayCueManager.h"
#include "GameplayCueNotify_Actor.h"
#include "GameplayCueNotify_Static.h"
#include "GameplayCueSet.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"

namespace
{
	/** Every helper in this file carries the MCPGasAbil prefix on purpose: the
	 *  module is a unity build, so a file-local name that collides with one in
	 *  another handler .cpp is a redefinition on whichever grouping puts the two
	 *  in the same blob, and the grouping is not stable across machines. */

	/** A string array as a JSON array field. */
	void MCPGasAbilSetStrings(
		TSharedPtr<FJsonObject> Obj,
		const TCHAR* Field,
		const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> List;
		for (const FString& Value : Values) List.Add(MakeShared<FJsonValueString>(Value));
		Obj->SetArrayField(Field, List);
	}

	/**
	 * The root every gameplay cue tag has to sit under.
	 *
	 * UGameplayCueSet::BaseGameplayCueTag() (GameplayCueSet.h:98) is what the
	 * cue system matches against, so a tag outside it is never routed no matter
	 * how correct it looks in the effect asset.
	 */
	FGameplayTag MCPGasAbilCueRoot()
	{
		return UGameplayCueSet::BaseGameplayCueTag();
	}

	/**
	 * Resolve a gameplay cue tag, refusing an unregistered one by name.
	 *
	 * RequestGameplayTag with ErrorIfNotFound=false returns an INVALID tag for
	 * a name nothing registered (GameplayTagsManager.h:375), and an invalid tag
	 * written into an effect is the silent failure this action exists to stop:
	 * the asset saves, the effect applies, and no cue ever fires.
	 */
	FGameplayTag MCPGasAbilResolveCueTag(const FString& TagName, TSharedPtr<FJsonValue>& OutError)
	{
		UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
		const FGameplayTag Tag = Manager.RequestGameplayTag(FName(*TagName), /*ErrorIfNotFound*/ false);
		if (!Tag.IsValid())
		{
			// Name a few real cue tags rather than only saying "no". A caller
			// who mistyped one needs the spelling, and a caller who has none
			// needs to know that creating the tag is a separate call.
			FGameplayTagContainer All;
			Manager.RequestAllGameplayTags(All, /*OnlyIncludeDictionaryTags*/ true);
			const FGameplayTag Root = MCPGasAbilCueRoot();
			TArray<FString> Examples;
			for (const FGameplayTag& Candidate : All)
			{
				if (Root.IsValid() && !Candidate.MatchesTag(Root)) continue;
				Examples.Add(Candidate.ToString());
				if (Examples.Num() >= 12) break;
			}
			OutError = MCPError(FString::Printf(
				TEXT("Gameplay tag '%s' is not registered, so writing it into the effect would store an ")
				TEXT("invalid tag and the cue would never fire. Register it first with ")
				TEXT("reflection(action=\"create_tag\", tag=\"%s\"). Registered cue tags: %s"),
				*TagName,
				*TagName,
				Examples.Num() > 0 ? *FString::Join(Examples, TEXT(", ")) : TEXT("(none under the GameplayCue root)")));
			return FGameplayTag();
		}

		const FGameplayTag Root = MCPGasAbilCueRoot();
		if (Root.IsValid() && !Tag.MatchesTag(Root))
		{
			OutError = MCPError(FString::Printf(
				TEXT("'%s' is a registered tag but it is not under '%s', and the gameplay cue system only ")
				TEXT("routes tags under that root, so nothing would ever handle it. Use a tag like '%s.%s'."),
				*Tag.ToString(),
				*Root.ToString(),
				*Root.ToString(),
				*Tag.GetTagName().ToString()));
			return FGameplayTag();
		}
		return Tag;
	}

	/** One cue notify class discovered in the project. */
	struct FMCPGasAbilCueNotify
	{
		FGameplayTag Tag;
		FString ClassPath;
		bool bBlueprint = false;
	};

	/**
	 * Every gameplay cue notify class in the project, keyed by its tag.
	 *
	 * Answered from the asset registry's class hierarchy rather than from the
	 * cue manager's object library, because the editor object library is
	 * populated by a scan that may not have run yet
	 * (UGameplayCueManager::InitializeEditorObjectLibrary, GameplayCueManager.h:251)
	 * and an audit that reports "no notifies exist" because an index was cold
	 * would be worse than no audit. The cue manager's own count is reported
	 * alongside, because a difference between the two IS a finding: an asset
	 * the manager has not indexed will not answer a cue at runtime.
	 *
	 * GetDerivedClassNames (IAssetRegistry.h:664) returns Blueprint generated
	 * class paths as well as native ones, so both halves come from one query.
	 */
	void MCPGasAbilCollectCueNotifies(
		TMap<FGameplayTag, FMCPGasAbilCueNotify>& OutByTag,
		TArray<FString>& OutNotifiesWithNoTag,
		int32& OutDuplicateTagCount,
		TArray<FString>& OutUnloadable)
	{
		FAssetRegistryModule& Module =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& Registry = Module.Get();

		TArray<FTopLevelAssetPath> Bases;
		Bases.Add(AGameplayCueNotify_Actor::StaticClass()->GetClassPathName());
		Bases.Add(UGameplayCueNotify_Static::StaticClass()->GetClassPathName());

		TSet<FTopLevelAssetPath> Excluded;
		TSet<FTopLevelAssetPath> Derived;
		Registry.GetDerivedClassNames(Bases, Excluded, Derived);
		// GetDerivedClassNames answers "derived by", so the bases themselves
		// are added explicitly. They are abstract in practice but a project may
		// register a notify directly on one.
		for (const FTopLevelAssetPath& Base : Bases) Derived.Add(Base);

		for (const FTopLevelAssetPath& ClassPath : Derived)
		{
			UClass* Class = FindObject<UClass>(nullptr, *ClassPath.ToString());
			if (!Class) Class = LoadObject<UClass>(nullptr, *ClassPath.ToString());
			if (!Class)
			{
				if (OutUnloadable.Num() < 25) OutUnloadable.Add(ClassPath.ToString());
				continue;
			}
			if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_NewerVersionExists | CLASS_Deprecated)) continue;

			UObject* CDO = Class->GetDefaultObject();
			if (!CDO) continue;

			// GameplayCueTag is a public UPROPERTY on both notify bases
			// (GameplayCueNotify_Actor.h:108, GameplayCueNotify_Static.h:62),
			// read through reflection so one code path covers both without a
			// cast per base class.
			FGameplayTag Tag;
			if (const FStructProperty* TagProp = CastField<FStructProperty>(
					Class->FindPropertyByName(TEXT("GameplayCueTag"))))
			{
				if (TagProp->Struct == FGameplayTag::StaticStruct())
				{
					Tag = *TagProp->ContainerPtrToValuePtr<FGameplayTag>(CDO);
				}
			}

			if (!Tag.IsValid())
			{
				// A notify with no tag is unreachable: nothing can ever route
				// to it. Worth reporting rather than silently skipping.
				if (OutNotifiesWithNoTag.Num() < 50) OutNotifiesWithNoTag.Add(Class->GetPathName());
				continue;
			}

			FMCPGasAbilCueNotify Entry;
			Entry.Tag = Tag;
			Entry.ClassPath = Class->GetPathName();
			Entry.bBlueprint = Class->ClassGeneratedBy != nullptr;

			if (OutByTag.Contains(Tag)) ++OutDuplicateTagCount;
			else OutByTag.Add(Tag, Entry);
		}
	}

	/**
	 * Which notify would answer this cue tag, and how.
	 *
	 * The cue system falls back to a parent tag when nothing is registered on
	 * the exact tag, so "no exact notify" is not the same as "no cue". Reporting
	 * the parent match as a match, and naming which parent, is the difference
	 * between a useful audit and a wall of false positives.
	 */
	const FMCPGasAbilCueNotify* MCPGasAbilFindNotifyForTag(
		const TMap<FGameplayTag, FMCPGasAbilCueNotify>& ByTag,
		const FGameplayTag& CueTag,
		FString& OutMatchedBy,
		FString& OutMatchedTag)
	{
		if (const FMCPGasAbilCueNotify* Exact = ByTag.Find(CueTag))
		{
			OutMatchedBy = TEXT("exact");
			OutMatchedTag = CueTag.ToString();
			return Exact;
		}
		// GetGameplayTagParents (GameplayTagContainer.h:153) returns the tag and
		// every ancestor, nearest first.
		const FGameplayTagContainer Parents = CueTag.GetGameplayTagParents();
		for (const FGameplayTag& Parent : Parents)
		{
			if (Parent == CueTag) continue;
			if (const FMCPGasAbilCueNotify* Hit = ByTag.Find(Parent))
			{
				OutMatchedBy = TEXT("parent");
				OutMatchedTag = Parent.ToString();
				return Hit;
			}
		}
		OutMatchedBy = TEXT("none");
		OutMatchedTag.Reset();
		return nullptr;
	}

	/**
	 * A FGameplayAttribute by name, searched across every loaded attribute set
	 * class. Accepts "Health" and "HealthSet.Health".
	 *
	 * GasHandlers.cpp has a file-local function that does the same job; this one
	 * cannot call it (it is static, so unity-build aside it has internal
	 * linkage) and must not share its name (unity build, C2084).
	 */
	FGameplayAttribute MCPGasAbilFindAttributeAcrossSets(const FString& Name)
	{
		FString SetFilter;
		FString AttrName = Name;
		if (Name.Contains(TEXT("."))) Name.Split(TEXT("."), &SetFilter, &AttrName);

		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (Class == UAttributeSet::StaticClass()) continue;
			if (!Class->IsChildOf(UAttributeSet::StaticClass())) continue;
			if (!SetFilter.IsEmpty() && !Class->GetName().Contains(SetFilter)) continue;
			for (TFieldIterator<FProperty> P(Class); P; ++P)
			{
				FStructProperty* SProp = CastField<FStructProperty>(*P);
				if (SProp
					&& SProp->Struct == FGameplayAttributeData::StaticStruct()
					&& SProp->GetName() == AttrName)
				{
					return FGameplayAttribute(SProp);
				}
			}
		}
		return FGameplayAttribute();
	}

	/** Granted specs sharing an InputID, other than the one being bound. */
	TArray<FString> MCPGasAbilInputIdConflicts(
		UAbilitySystemComponent* ASC,
		int32 InputID,
		UClass* IgnoreClass)
	{
		TArray<FString> Conflicts;
		if (InputID == INDEX_NONE) return Conflicts;
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (!Spec.Ability) continue;
			if (Spec.Ability->GetClass() == IgnoreClass) continue;
			if (Spec.InputID == InputID) Conflicts.Add(Spec.Ability->GetClass()->GetPathName());
		}
		return Conflicts;
	}

	/** Locate the granted spec for a class, or explain that it is not granted. */
	FGameplayAbilitySpec* MCPGasAbilRequireGrantedSpec(
		UAbilitySystemComponent* ASC,
		AActor* Actor,
		UClass* AbilityClass,
		TSharedPtr<FJsonValue>& OutError)
	{
		FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(AbilityClass);
		if (!Spec)
		{
			OutError = MCPError(FString::Printf(
				TEXT("'%s' is not granted on '%s', so there is no ability spec to bind input to. ")
				TEXT("Call gas(action=\"grant_ability\", abilityClass=\"%s\") first, then bind."),
				*AbilityClass->GetName(),
				*Actor->GetActorLabel(),
				*AbilityClass->GetPathName()));
		}
		return Spec;
	}

	/** Authority guard shared by every spec write. */
	TSharedPtr<FJsonValue> MCPGasAbilRequireAuthority(UAbilitySystemComponent* ASC, AActor* Actor)
	{
		if (ASC->IsOwnerActorAuthoritative()) return nullptr;
		return MCPError(FString::Printf(
			TEXT("'%s' ASC is not authoritative. An ability spec is replicated from the authority, so a ")
			TEXT("write here would be overwritten by the next update and would look like it worked. ")
			TEXT("Target the server/primary PIE world (world=\"pie\", pieInstance=0)."),
			*Actor->GetActorLabel()));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Input binding
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FGasHandlers::BindAbilityInput(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FString AbilitySpecName;
	if (auto Err = RequireString(Params, TEXT("abilityClass"), AbilitySpecName)) return Err;

	if (!Params->HasField(TEXT("inputId")))
	{
		return MCPError(TEXT(
			"Missing required parameter 'inputId'. It is the integer an ability is activated by, the same "
			"value gas(send_ability_input) and UAbilitySystemComponent::AbilityLocalInputPressed take. Pass "
			"-1 to leave the ability unbound, or use gas(clear_ability_input) to unbind one."));
	}
	const int32 InputID = static_cast<int32>(OptionalNumber(Params, TEXT("inputId"), -1.0));

	AActor* Actor = nullptr;
	TSharedPtr<FJsonValue> Error;
	UAbilitySystemComponent* ASC = MCPGas::ResolveActorASC(Params, Actor, Error);
	if (!ASC) return Error;

	UClass* AbilityClass = MCPGas::ResolveGameplayAbilityClass(AbilitySpecName, Error);
	if (!AbilityClass) return Error;

	if (auto Err = MCPGasAbilRequireAuthority(ASC, Actor)) return Err;

	FGameplayAbilitySpec* Spec = MCPGasAbilRequireGrantedSpec(ASC, Actor, AbilityClass, Error);
	if (!Spec) return Error;

	const int32 PreviousInputID = Spec->InputID;
	const bool bChanged = PreviousInputID != InputID;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("abilityClass"), AbilityClass->GetPathName());
	Result->SetNumberField(TEXT("previousInputId"), PreviousInputID);
	Result->SetNumberField(TEXT("inputId"), InputID);
	Result->SetBoolField(TEXT("unchanged"), !bChanged);

	if (bChanged)
	{
		Spec->InputID = InputID;
		// The spec container is a fast-array serialiser; without this the write
		// is local and the next replication update discards it.
		ASC->MarkAbilitySpecDirty(*Spec);
		MCPSetUpdated(Result);
	}

	// AbilityLocalInputPressed activates EVERY spec carrying the InputID, so a
	// shared id is a real behaviour, not an error. Some games use it on purpose.
	// Saying which other abilities share it costs one field and saves the turn
	// spent wondering why two abilities fired.
	const TArray<FString> Conflicts = MCPGasAbilInputIdConflicts(ASC, InputID, AbilityClass);
	MCPGasAbilSetStrings(Result, TEXT("sharedWith"), Conflicts);
	if (Conflicts.Num() > 0)
	{
		Result->SetStringField(TEXT("note"), FString::Printf(
			TEXT("inputId %d is also bound to %d other ability(ies) on this actor. Sending that input will ")
			TEXT("activate all of them."),
			InputID, Conflicts.Num()));
	}

	Result->SetObjectField(TEXT("spec"), MCPGas::DescribeAbilitySpec(*Spec));

	// The inverse restores the exact id that was there, which is not always -1:
	// rebinding an already-bound ability has to put the old binding back.
	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	RollbackPayload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	RollbackPayload->SetStringField(TEXT("abilityClass"), AbilityClass->GetPathName());
	RollbackPayload->SetNumberField(TEXT("inputId"), PreviousInputID);
	MCPSetRollback(Result, TEXT("bind_ability_input"), RollbackPayload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::ClearAbilityInput(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FString AbilitySpecName;
	if (auto Err = RequireString(Params, TEXT("abilityClass"), AbilitySpecName)) return Err;

	AActor* Actor = nullptr;
	TSharedPtr<FJsonValue> Error;
	UAbilitySystemComponent* ASC = MCPGas::ResolveActorASC(Params, Actor, Error);
	if (!ASC) return Error;

	UClass* AbilityClass = MCPGas::ResolveGameplayAbilityClass(AbilitySpecName, Error);
	if (!AbilityClass) return Error;

	if (auto Err = MCPGasAbilRequireAuthority(ASC, Actor)) return Err;

	FGameplayAbilitySpec* Spec = MCPGasAbilRequireGrantedSpec(ASC, Actor, AbilityClass, Error);
	if (!Spec) return Error;

	const int32 PreviousInputID = Spec->InputID;
	const bool bChanged = PreviousInputID != INDEX_NONE;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("abilityClass"), AbilityClass->GetPathName());
	Result->SetNumberField(TEXT("previousInputId"), PreviousInputID);
	Result->SetNumberField(TEXT("inputId"), INDEX_NONE);
	// Replaying a rollback must be safe, so an already-unbound ability is a
	// report rather than a failure.
	Result->SetBoolField(TEXT("unchanged"), !bChanged);

	if (bChanged)
	{
		Spec->InputID = INDEX_NONE;
		ASC->MarkAbilitySpecDirty(*Spec);
		MCPSetUpdated(Result);
	}

	Result->SetObjectField(TEXT("spec"), MCPGas::DescribeAbilitySpec(*Spec));

	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	RollbackPayload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	RollbackPayload->SetStringField(TEXT("abilityClass"), AbilityClass->GetPathName());
	RollbackPayload->SetNumberField(TEXT("inputId"), PreviousInputID);
	MCPSetRollback(Result, TEXT("bind_ability_input"), RollbackPayload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::SendAbilityInput(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	const FString Event = OptionalString(Params, TEXT("inputEvent"), TEXT("pressed")).ToLower();
	if (Event != TEXT("pressed") && Event != TEXT("released")
		&& Event != TEXT("confirm") && Event != TEXT("cancel"))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown inputEvent '%s'. Valid values: pressed | released | confirm | cancel."), *Event));
	}

	AActor* Actor = nullptr;
	TSharedPtr<FJsonValue> Error;
	UAbilitySystemComponent* ASC = MCPGas::ResolveActorASC(Params, Actor, Error);
	if (!ASC) return Error;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("inputEvent"), Event);

	// Confirm and cancel are ASC-wide target-actor events with no InputID, and
	// no inverse: a confirmed target is confirmed. Say that rather than emit a
	// rollback that rolls nothing back.
	if (Event == TEXT("confirm") || Event == TEXT("cancel"))
	{
		if (Event == TEXT("confirm")) ASC->InputConfirm();
		else ASC->InputCancel();

		Result->SetBoolField(TEXT("unchanged"), false);
		Result->SetStringField(TEXT("rollbackNote"), TEXT(
			"No rollback: confirm and cancel are one-shot events delivered to whatever targeting actors "
			"were listening. There is no call that un-confirms one. Re-sending is another event, not a "
			"repeated state change."));
		return MCPResult(Result);
	}

	// pressed / released need an InputID. Accepting abilityClass as well is
	// what makes this callable straight after bind_ability_input without the
	// caller having to remember the number it chose.
	int32 InputID = INDEX_NONE;
	const FString AbilitySpecName = OptionalString(Params, TEXT("abilityClass"));
	if (!AbilitySpecName.IsEmpty())
	{
		UClass* AbilityClass = MCPGas::ResolveGameplayAbilityClass(AbilitySpecName, Error);
		if (!AbilityClass) return Error;
		FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(AbilityClass);
		if (!Spec)
		{
			return MCPError(FString::Printf(
				TEXT("'%s' is not granted on '%s', so it has no input binding to send to. ")
				TEXT("Call gas(grant_ability) then gas(bind_ability_input)."),
				*AbilityClass->GetName(), *Actor->GetActorLabel()));
		}
		if (Spec->InputID == INDEX_NONE)
		{
			return MCPError(FString::Printf(
				TEXT("'%s' is granted on '%s' but its inputID is -1, meaning unbound, so no input event ")
				TEXT("can reach it. Call gas(action=\"bind_ability_input\", abilityClass=\"%s\", inputId=<n>) first."),
				*AbilityClass->GetName(), *Actor->GetActorLabel(), *AbilityClass->GetPathName()));
		}
		InputID = Spec->InputID;
		Result->SetStringField(TEXT("abilityClass"), AbilityClass->GetPathName());
	}
	else if (Params->HasField(TEXT("inputId")))
	{
		InputID = static_cast<int32>(OptionalNumber(Params, TEXT("inputId"), -1.0));
	}
	else
	{
		return MCPError(TEXT(
			"Pass either 'inputId' (the integer the ability is bound to) or 'abilityClass' (whose binding "
			"is looked up on this actor). A 'pressed' or 'released' event is addressed by InputID; only "
			"inputEvent=confirm and inputEvent=cancel need neither."));
	}

	Result->SetNumberField(TEXT("inputId"), InputID);

	// Which specs this reaches, and whether their pressed state actually
	// changes. FGameplayAbilitySpec::InputPressed (GameplayAbilitySpec.h:219) is
	// the state the engine itself keeps, so "already pressed" is measured, not
	// assumed.
	const bool bWantPressed = Event == TEXT("pressed");
	TArray<TSharedPtr<FJsonValue>> Matched;
	bool bAnyStateChange = false;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.InputID != InputID) continue;
		TSharedPtr<FJsonObject> Row = MCPGas::DescribeAbilitySpec(Spec);
		Row->SetBoolField(TEXT("inputPressedBefore"), Spec.InputPressed != 0);
		Row->SetBoolField(TEXT("activeBefore"), Spec.IsActive());
		Matched.Add(MakeShared<FJsonValueObject>(Row));
		if ((Spec.InputPressed != 0) != bWantPressed) bAnyStateChange = true;
	}

	if (Matched.Num() == 0)
	{
		// Not an error: the ASC will accept the event and nothing will listen.
		// Saying so is the whole answer the caller needs.
		Result->SetArrayField(TEXT("matchedSpecs"), Matched);
		Result->SetNumberField(TEXT("matchedSpecCount"), 0);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetStringField(TEXT("note"), FString::Printf(
			TEXT("No granted ability on '%s' has inputID %d, so this event reached nothing. Bind one with ")
			TEXT("gas(bind_ability_input), or read gas(get_asc_state) for the ids currently in use."),
			*Actor->GetActorLabel(), InputID));
		return MCPResult(Result);
	}

	if (bWantPressed) ASC->AbilityLocalInputPressed(InputID);
	else ASC->AbilityLocalInputReleased(InputID);

	// Re-read after the call so the result reports what happened rather than
	// what was asked for.
	TArray<TSharedPtr<FJsonValue>> After;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.InputID != InputID) continue;
		TSharedPtr<FJsonObject> Row = MCPGas::DescribeAbilitySpec(Spec);
		Row->SetBoolField(TEXT("inputPressedAfter"), Spec.InputPressed != 0);
		Row->SetBoolField(TEXT("activeAfter"), Spec.IsActive());
		After.Add(MakeShared<FJsonValueObject>(Row));
	}

	Result->SetArrayField(TEXT("matchedSpecs"), Matched);
	Result->SetNumberField(TEXT("matchedSpecCount"), Matched.Num());
	Result->SetArrayField(TEXT("specsAfter"), After);
	Result->SetBoolField(TEXT("unchanged"), !bAnyStateChange);

	// Pressed and released are a genuine inverse pair: the ASC keeps
	// InputPressed per spec and releasing restores it.
	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	RollbackPayload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	RollbackPayload->SetNumberField(TEXT("inputId"), InputID);
	RollbackPayload->SetStringField(TEXT("inputEvent"), bWantPressed ? TEXT("released") : TEXT("pressed"));
	MCPSetRollback(Result, TEXT("send_ability_input"), RollbackPayload);
	Result->SetStringField(TEXT("rollbackNote"), TEXT(
		"The rollback restores the ASC's InputPressed state, which is what this call changed. It does NOT "
		"undo an ability that activated as a result: use gas(trace_ability_activation) to see whether one "
		"did, and the ability's own cancel path to end it."));

	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// Gameplay cues
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FGasHandlers::AddEffectCue(const TSharedPtr<FJsonObject>& Params)
{
	FString EffectPath;
	if (auto Err = RequireStringAlt(Params, TEXT("effectPath"), TEXT("effectClass"), EffectPath)) return Err;

	FString CueTagName;
	if (auto Err = RequireString(Params, TEXT("cueTag"), CueTagName)) return Err;

	TSharedPtr<FJsonValue> Error;
	const FGameplayTag CueTag = MCPGasAbilResolveCueTag(CueTagName, Error);
	if (!CueTag.IsValid()) return Error;

	UGameplayEffect* GE = LoadBlueprintCDO<UGameplayEffect>(EffectPath, Error);
	if (!GE) return Error;

	const float MinLevel = static_cast<float>(OptionalNumber(Params, TEXT("minLevel"), 0.0));
	const float MaxLevel = static_cast<float>(OptionalNumber(Params, TEXT("maxLevel"), 0.0));

	const FString MagnitudeAttributeName = OptionalString(Params, TEXT("magnitudeAttribute"));
	FGameplayAttribute MagnitudeAttribute;
	if (!MagnitudeAttributeName.IsEmpty())
	{
		MagnitudeAttribute = MCPGasAbilFindAttributeAcrossSets(MagnitudeAttributeName);
		if (!MagnitudeAttribute.IsValid())
		{
			return MCPError(FString::Printf(
				TEXT("magnitudeAttribute '%s' did not resolve to a gameplay attribute. Use ")
				TEXT("'SetName.Attribute' or a unique attribute name, and note the AttributeSet has to be ")
				TEXT("compiled and loaded for its properties to be visible. Omit the parameter to let the ")
				TEXT("cue take its magnitude from the effect level instead."),
				*MagnitudeAttributeName));
		}
	}

	// Idempotent on the TAG, because a tag appearing in two cue entries would
	// fire the cue twice for one effect and nothing in the editor shows that.
	bool bExisted = false;
	bool bUpdated = false;
	float PreviousMin = 0.f;
	float PreviousMax = 0.f;
	FString PreviousMagnitude;
	for (FGameplayEffectCue& Cue : GE->GameplayCues)
	{
		if (!Cue.GameplayCueTags.HasTagExact(CueTag)) continue;
		bExisted = true;
		PreviousMin = Cue.MinLevel;
		PreviousMax = Cue.MaxLevel;
		PreviousMagnitude = Cue.MagnitudeAttribute.IsValid() ? Cue.MagnitudeAttribute.GetName() : FString();

		if (!FMath::IsNearlyEqual(Cue.MinLevel, MinLevel)
			|| !FMath::IsNearlyEqual(Cue.MaxLevel, MaxLevel)
			|| Cue.MagnitudeAttribute != MagnitudeAttribute)
		{
			Cue.MinLevel = MinLevel;
			Cue.MaxLevel = MaxLevel;
			Cue.MagnitudeAttribute = MagnitudeAttribute;
			bUpdated = true;
		}
		break;
	}

	if (!bExisted)
	{
		FGameplayEffectCue NewCue;
		NewCue.GameplayCueTags.AddTag(CueTag);
		NewCue.MinLevel = MinLevel;
		NewCue.MaxLevel = MaxLevel;
		NewCue.MagnitudeAttribute = MagnitudeAttribute;
		GE->GameplayCues.Add(NewCue);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("effectPath"), EffectPath);
	Result->SetStringField(TEXT("cueTag"), CueTag.ToString());
	Result->SetNumberField(TEXT("minLevel"), MinLevel);
	Result->SetNumberField(TEXT("maxLevel"), MaxLevel);
	Result->SetNumberField(TEXT("cueCount"), GE->GameplayCues.Num());
	if (MagnitudeAttribute.IsValid())
	{
		Result->SetStringField(TEXT("magnitudeAttribute"), MagnitudeAttribute.GetName());
	}

	if (bExisted && !bUpdated)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("unchanged"), true);
	}
	else if (bExisted)
	{
		MCPSetUpdated(Result);
		Result->SetBoolField(TEXT("unchanged"), false);
	}
	else
	{
		MCPSetCreated(Result);
		Result->SetBoolField(TEXT("unchanged"), false);
	}

	if (!bExisted || bUpdated)
	{
		GE->MarkPackageDirty();
		FString SaveReason;
		if (!SaveAssetPackageChecked(GE, SaveReason))
		{
			Result->SetStringField(TEXT("saveWarning"), SaveReason);
		}
	}

	// The half a property write cannot do: say whether anything will answer
	// this cue. A tag with no notify is a cue that silently does nothing, and
	// that is the single most common way an authored cue disappoints.
	{
		TMap<FGameplayTag, FMCPGasAbilCueNotify> ByTag;
		TArray<FString> NoTag;
		TArray<FString> Unloadable;
		int32 Duplicates = 0;
		MCPGasAbilCollectCueNotifies(ByTag, NoTag, Duplicates, Unloadable);

		FString MatchedBy;
		FString MatchedTag;
		const FMCPGasAbilCueNotify* Notify = MCPGasAbilFindNotifyForTag(ByTag, CueTag, MatchedBy, MatchedTag);
		Result->SetStringField(TEXT("notifyMatchedBy"), MatchedBy);
		if (Notify)
		{
			Result->SetStringField(TEXT("notifyClass"), Notify->ClassPath);
			Result->SetStringField(TEXT("notifyMatchedTag"), MatchedTag);
		}
		else
		{
			Result->SetStringField(TEXT("coverageWarning"), FString::Printf(
				TEXT("The link is written, but no GameplayCueNotify answers '%s' or any of its parent tags, ")
				TEXT("so applying this effect will fire nothing visible. Create one with ")
				TEXT("gas(action=\"create_cue\"), set its GameplayCueTag to '%s' via asset(set_property), ")
				TEXT("then re-run gas(validate_cue_coverage)."),
				*CueTag.ToString(), *CueTag.ToString()));
		}
	}

	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	RollbackPayload->SetStringField(TEXT("effectPath"), EffectPath);
	RollbackPayload->SetStringField(TEXT("cueTag"), CueTag.ToString());
	if (bExisted)
	{
		// Undoing an UPDATE means restoring the previous levels, not removing
		// the cue: remove_effect_cue would delete a link the caller already had.
		RollbackPayload->SetNumberField(TEXT("minLevel"), PreviousMin);
		RollbackPayload->SetNumberField(TEXT("maxLevel"), PreviousMax);
		if (!PreviousMagnitude.IsEmpty())
		{
			RollbackPayload->SetStringField(TEXT("magnitudeAttribute"), PreviousMagnitude);
		}
		MCPSetRollback(Result, TEXT("add_effect_cue"), RollbackPayload);
		if (PreviousMagnitude.IsEmpty() && MagnitudeAttribute.IsValid())
		{
			Result->SetStringField(TEXT("rollbackNote"), TEXT(
				"Lossy: the cue had no MagnitudeAttribute before this call and the rollback cannot clear "
				"one, because add_effect_cue reads an absent magnitudeAttribute as 'leave it alone'. Clear "
				"it with asset(set_property) on the effect's GameplayCues array if that matters."));
		}
	}
	else
	{
		MCPSetRollback(Result, TEXT("remove_effect_cue"), RollbackPayload);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::RemoveEffectCue(const TSharedPtr<FJsonObject>& Params)
{
	FString EffectPath;
	if (auto Err = RequireStringAlt(Params, TEXT("effectPath"), TEXT("effectClass"), EffectPath)) return Err;

	FString CueTagName;
	if (auto Err = RequireString(Params, TEXT("cueTag"), CueTagName)) return Err;

	TSharedPtr<FJsonValue> Error;
	UGameplayEffect* GE = LoadBlueprintCDO<UGameplayEffect>(EffectPath, Error);
	if (!GE) return Error;

	// Deliberately NOT routed through the cue-tag validator: removing a tag that
	// is no longer registered is exactly the cleanup a caller needs to be able
	// to do, and refusing it would strand the effect. Resolve leniently and fall
	// back to a name comparison.
	const FGameplayTag CueTag =
		UGameplayTagsManager::Get().RequestGameplayTag(FName(*CueTagName), /*ErrorIfNotFound*/ false);

	int32 Removed = 0;
	float PreviousMin = 0.f;
	float PreviousMax = 0.f;
	FString PreviousMagnitude;
	for (int32 Index = GE->GameplayCues.Num() - 1; Index >= 0; --Index)
	{
		FGameplayEffectCue& Cue = GE->GameplayCues[Index];
		const bool bMatches = CueTag.IsValid()
			? Cue.GameplayCueTags.HasTagExact(CueTag)
			: Cue.GameplayCueTags.ToStringSimple().Contains(CueTagName);
		if (!bMatches) continue;

		PreviousMin = Cue.MinLevel;
		PreviousMax = Cue.MaxLevel;
		PreviousMagnitude = Cue.MagnitudeAttribute.IsValid() ? Cue.MagnitudeAttribute.GetName() : FString();

		if (CueTag.IsValid()) Cue.GameplayCueTags.RemoveTag(CueTag);
		else Cue.GameplayCueTags.Reset();
		++Removed;

		// An entry with no tags left fires nothing and would show as a blank
		// row in the editor, so it goes rather than lingering.
		if (Cue.GameplayCueTags.IsEmpty()) GE->GameplayCues.RemoveAt(Index);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("effectPath"), EffectPath);
	Result->SetStringField(TEXT("cueTag"), CueTagName);
	Result->SetNumberField(TEXT("removed"), Removed);
	Result->SetNumberField(TEXT("cueCount"), GE->GameplayCues.Num());

	if (Removed == 0)
	{
		// Idempotent: replaying a rollback must not fail.
		Result->SetBoolField(TEXT("alreadyRemoved"), true);
		Result->SetBoolField(TEXT("unchanged"), true);
		TSharedPtr<FJsonObject> NoopPayload = MakeShared<FJsonObject>();
		NoopPayload->SetStringField(TEXT("effectPath"), EffectPath);
		NoopPayload->SetStringField(TEXT("cueTag"), CueTagName);
		MCPSetRollback(Result, TEXT("remove_effect_cue"), NoopPayload);
		Result->SetStringField(TEXT("rollbackNote"), TEXT(
			"Nothing was removed, so the rollback is the same no-op call. It is emitted rather than "
			"omitted so a flow that replays this step behaves identically either way."));
		return MCPResult(Result);
	}

	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("unchanged"), false);
	GE->MarkPackageDirty();
	FString SaveReason;
	if (!SaveAssetPackageChecked(GE, SaveReason)) Result->SetStringField(TEXT("saveWarning"), SaveReason);

	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	RollbackPayload->SetStringField(TEXT("effectPath"), EffectPath);
	RollbackPayload->SetStringField(TEXT("cueTag"), CueTagName);
	RollbackPayload->SetNumberField(TEXT("minLevel"), PreviousMin);
	RollbackPayload->SetNumberField(TEXT("maxLevel"), PreviousMax);
	if (!PreviousMagnitude.IsEmpty())
	{
		RollbackPayload->SetStringField(TEXT("magnitudeAttribute"), PreviousMagnitude);
	}
	MCPSetRollback(Result, TEXT("add_effect_cue"), RollbackPayload);
	if (Removed > 1)
	{
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(TEXT(
			"Lossy: %d cue entries carried this tag and the rollback restores ONE, with the levels of the "
			"last one removed. Duplicated tags in one effect are themselves a fault, so the restored "
			"single entry is usually what was wanted; check the effect if it was not."), Removed));
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::ValidateCueCoverage(const TSharedPtr<FJsonObject>& Params)
{
	const FString Directory = OptionalString(Params, TEXT("directory"), TEXT("/Game"));
	const FString SingleEffect = OptionalString(Params, TEXT("effectPath"));
	const int32 MaxEffects = FMath::Clamp(OptionalInt(Params, TEXT("maxEffects"), 500), 1, 5000);

	TMap<FGameplayTag, FMCPGasAbilCueNotify> NotifiesByTag;
	TArray<FString> NotifiesWithNoTag;
	TArray<FString> UnloadableNotifyClasses;
	int32 DuplicateNotifyTags = 0;
	MCPGasAbilCollectCueNotifies(
		NotifiesByTag, NotifiesWithNoTag, DuplicateNotifyTags, UnloadableNotifyClasses);

	// Every GameplayEffect class in the project, native and Blueprint, from the
	// asset registry's class hierarchy. Same query shape as the notify scan, so
	// a Blueprint effect and a C++ effect are found by one code path.
	FAssetRegistryModule& Module =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = Module.Get();

	TArray<FTopLevelAssetPath> Bases;
	Bases.Add(UGameplayEffect::StaticClass()->GetClassPathName());
	TSet<FTopLevelAssetPath> Excluded;
	TSet<FTopLevelAssetPath> DerivedEffects;
	Registry.GetDerivedClassNames(Bases, Excluded, DerivedEffects);

	TArray<FString> EffectClassPaths;
	if (!SingleEffect.IsEmpty())
	{
		EffectClassPaths.Add(SingleEffect);
	}
	else
	{
		for (const FTopLevelAssetPath& ClassPath : DerivedEffects)
		{
			const FString AsString = ClassPath.ToString();
			// A directory filter over content paths. Native effects live under
			// /Script and are only in scope when the caller asks for /Script or
			// gives no directory narrower than that.
			if (!Directory.IsEmpty() && !AsString.StartsWith(Directory)) continue;
			EffectClassPaths.Add(AsString);
		}
		EffectClassPaths.Sort();
	}

	bool bTruncated = false;
	if (EffectClassPaths.Num() > MaxEffects)
	{
		EffectClassPaths.SetNum(MaxEffects);
		bTruncated = true;
	}

	TArray<TSharedPtr<FJsonValue>> EffectRows;
	TArray<TSharedPtr<FJsonValue>> Problems;
	TArray<FString> UnloadableEffects;
	TSet<FGameplayTag> ReferencedTags;
	int32 EffectsWithCues = 0;
	int32 TotalCueLinks = 0;
	int32 UncoveredLinks = 0;

	const auto AddProblem = [&Problems](const FString& Kind, const FString& Subject, const FString& Detail)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("kind"), Kind);
		Row->SetStringField(TEXT("subject"), Subject);
		Row->SetStringField(TEXT("detail"), Detail);
		Problems.Add(MakeShared<FJsonValueObject>(Row));
	};

	for (const FString& ClassPath : EffectClassPaths)
	{
		UClass* Class = MCPGas::ResolveClassDerivingFrom(ClassPath, UGameplayEffect::StaticClass());
		if (!Class)
		{
			if (UnloadableEffects.Num() < 25) UnloadableEffects.Add(ClassPath);
			continue;
		}
		const UGameplayEffect* GE = Class->GetDefaultObject<UGameplayEffect>();
		if (!GE) continue;
		if (GE->GameplayCues.Num() == 0) continue;

		++EffectsWithCues;
		TSharedPtr<FJsonObject> EffectRow = MakeShared<FJsonObject>();
		EffectRow->SetStringField(TEXT("effectClass"), Class->GetPathName());

		TArray<TSharedPtr<FJsonValue>> CueRows;
		for (const FGameplayEffectCue& Cue : GE->GameplayCues)
		{
			if (Cue.GameplayCueTags.IsEmpty())
			{
				AddProblem(
					TEXT("cue_entry_with_no_tag"),
					Class->GetPathName(),
					TEXT("A GameplayCues entry carries no tag, so it can never fire. Remove it, or give it a tag with gas(add_effect_cue)."));
				continue;
			}
			for (const FGameplayTag& Tag : Cue.GameplayCueTags)
			{
				++TotalCueLinks;
				ReferencedTags.Add(Tag);

				TSharedPtr<FJsonObject> CueRow = MakeShared<FJsonObject>();
				CueRow->SetStringField(TEXT("cueTag"), Tag.ToString());
				CueRow->SetNumberField(TEXT("minLevel"), Cue.MinLevel);
				CueRow->SetNumberField(TEXT("maxLevel"), Cue.MaxLevel);
				if (Cue.MagnitudeAttribute.IsValid())
				{
					CueRow->SetStringField(TEXT("magnitudeAttribute"), Cue.MagnitudeAttribute.GetName());
				}

				const FGameplayTag Root = MCPGasAbilCueRoot();
				if (Root.IsValid() && !Tag.MatchesTag(Root))
				{
					CueRow->SetStringField(TEXT("matchedBy"), TEXT("none"));
					++UncoveredLinks;
					AddProblem(
						TEXT("cue_tag_outside_root"),
						Class->GetPathName(),
						FString::Printf(
							TEXT("'%s' is not under '%s'. The cue system only routes tags under that root, so this link is inert."),
							*Tag.ToString(), *Root.ToString()));
					CueRows.Add(MakeShared<FJsonValueObject>(CueRow));
					continue;
				}

				FString MatchedBy;
				FString MatchedTag;
				const FMCPGasAbilCueNotify* Notify =
					MCPGasAbilFindNotifyForTag(NotifiesByTag, Tag, MatchedBy, MatchedTag);
				CueRow->SetStringField(TEXT("matchedBy"), MatchedBy);
				if (Notify)
				{
					CueRow->SetStringField(TEXT("notifyClass"), Notify->ClassPath);
					CueRow->SetStringField(TEXT("notifyTag"), MatchedTag);
				}
				else
				{
					++UncoveredLinks;
					AddProblem(
						TEXT("cue_without_notify"),
						Class->GetPathName(),
						FString::Printf(
							TEXT("'%s' has no GameplayCueNotify on it or on any parent tag, so applying this effect fires nothing. Create one with gas(create_cue) and set its GameplayCueTag."),
							*Tag.ToString()));
				}
				CueRows.Add(MakeShared<FJsonValueObject>(CueRow));
			}
		}
		EffectRow->SetArrayField(TEXT("cues"), CueRows);
		EffectRows.Add(MakeShared<FJsonValueObject>(EffectRow));
	}

	// A notify nothing references is not broken, but it is usually a rename
	// that only got done on one side.
	TArray<TSharedPtr<FJsonValue>> NotifyRows;
	int32 OrphanNotifies = 0;
	for (const auto& Pair : NotifiesByTag)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("cueTag"), Pair.Key.ToString());
		Row->SetStringField(TEXT("notifyClass"), Pair.Value.ClassPath);
		Row->SetStringField(TEXT("source"), Pair.Value.bBlueprint ? TEXT("blueprint") : TEXT("native"));
		const bool bReferenced = ReferencedTags.Contains(Pair.Key);
		Row->SetBoolField(TEXT("referencedByAnEffect"), bReferenced);
		if (!bReferenced) ++OrphanNotifies;
		NotifyRows.Add(MakeShared<FJsonValueObject>(Row));
	}
	for (const FString& Path : NotifiesWithNoTag)
	{
		AddProblem(
			TEXT("notify_without_tag"),
			Path,
			TEXT("This GameplayCueNotify has no GameplayCueTag, so nothing can ever route to it. Set its GameplayCueTag with asset(set_property)."));
	}
	if (DuplicateNotifyTags > 0)
	{
		AddProblem(
			TEXT("duplicate_notify_tags"),
			TEXT("(project)"),
			FString::Printf(
				TEXT("%d notify class(es) share a cue tag with another. Which one answers is not defined by this audit; the cue manager picks one."),
				DuplicateNotifyTags));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("directory"), SingleEffect.IsEmpty() ? Directory : SingleEffect);
	Result->SetNumberField(TEXT("effectClassesScanned"), EffectClassPaths.Num());
	Result->SetNumberField(TEXT("effectsWithCues"), EffectsWithCues);
	Result->SetNumberField(TEXT("cueLinks"), TotalCueLinks);
	Result->SetNumberField(TEXT("uncoveredCueLinks"), UncoveredLinks);
	Result->SetNumberField(TEXT("notifyClasses"), NotifiesByTag.Num());
	Result->SetNumberField(TEXT("orphanNotifies"), OrphanNotifies);
	Result->SetArrayField(TEXT("effects"), EffectRows);
	Result->SetArrayField(TEXT("notifies"), NotifyRows);
	Result->SetArrayField(TEXT("problems"), Problems);
	Result->SetNumberField(TEXT("problemCount"), Problems.Num());
	Result->SetBoolField(TEXT("truncated"), bTruncated);
	MCPGasAbilSetStrings(Result, TEXT("unloadableEffectClasses"), UnloadableEffects);
	MCPGasAbilSetStrings(Result, TEXT("unloadableNotifyClasses"), UnloadableNotifyClasses);

	// What the cue MANAGER has indexed, which is what actually answers a cue at
	// runtime. A notify that exists on disk but is missing from the manager's
	// set will not fire, and that difference is invisible from the asset scan
	// alone, so it is reported rather than assumed away.
	if (UGameplayCueManager* CueManager = UAbilitySystemGlobals::Get().GetGameplayCueManager())
	{
		int32 IndexedRuntime = 0;
		if (const UGameplayCueSet* RuntimeSet = CueManager->GetRuntimeCueSet())
		{
			IndexedRuntime = RuntimeSet->GameplayCueData.Num();
		}
		Result->SetNumberField(TEXT("cueManagerRuntimeEntries"), IndexedRuntime);
#if WITH_EDITOR
		int32 IndexedEditor = 0;
		if (const UGameplayCueSet* EditorSet = CueManager->GetEditorCueSet())
		{
			IndexedEditor = EditorSet->GameplayCueData.Num();
		}
		Result->SetNumberField(TEXT("cueManagerEditorEntries"), IndexedEditor);
		if (IndexedEditor == 0 && NotifiesByTag.Num() > 0)
		{
			Result->SetStringField(TEXT("cueManagerNote"), TEXT(
				"The cue manager's editor library is empty while cue notify classes exist on disk. That "
				"library is populated by a scan on editor startup, so an empty one usually means the scan "
				"has not run yet rather than that the notifies are broken. The coverage above comes from "
				"the asset registry and is unaffected."));
		}
#endif
	}
	else
	{
		Result->SetStringField(TEXT("cueManagerNote"), TEXT(
			"No GameplayCueManager exists yet. It is created on demand, so this is normal before any cue "
			"has been routed. Coverage above comes from the asset registry and is unaffected."));
	}

	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// Attribute set audit: clamping and meta attributes
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FGasHandlers::AuditAttributeSet(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	const FString SetSpec = OptionalString(Params, TEXT("attributeSet"));
	const bool bHasActor = Params->HasField(TEXT("actorLabel")) || Params->HasField(TEXT("actorPath"));
	const bool bProbeClamping = OptionalBool(Params, TEXT("probeClamping"), false);

	if (SetSpec.IsEmpty() && !bHasActor)
	{
		return MCPError(TEXT(
			"Pass 'attributeSet' (an AttributeSet content path or class name) to audit a class, or "
			"'actorLabel'/'actorPath' to audit the sets registered on a live actor's AbilitySystemComponent. "
			"Passing both audits that one set on that actor, which is what probeClamping needs."));
	}

	AActor* Actor = nullptr;
	UAbilitySystemComponent* ASC = nullptr;
	if (bHasActor)
	{
		TSharedPtr<FJsonValue> Error;
		ASC = MCPGas::ResolveActorASC(Params, Actor, Error);
		if (!ASC) return Error;
	}

	// This audit deliberately does NOT register the actor's own attribute set
	// subobjects the way get_live_attribute_value does. Adopting a subobject is
	// a change to a live component, and this action is classified as a read: a
	// read that quietly mutated the editor would be gated as safe to land in
	// any editor session, which is the wrong answer. So it reports what IS
	// registered, and when nothing is, it names the call that registers one.

	// Which set classes are in scope.
	TArray<UClass*> SetClasses;
	TMap<UClass*, UAttributeSet*> InstanceByClass;
	if (!SetSpec.IsEmpty())
	{
		UClass* SetClass = MCPGas::ResolveClassDerivingFrom(SetSpec, UAttributeSet::StaticClass());
		if (!SetClass)
		{
			return MCPError(FString::Printf(
				TEXT("AttributeSet class not found: '%s'. Pass a content path (/Game/GAS/AS_Health), its ")
				TEXT("generated class path (…_C), or a native class name (UAttributeSet subclass)."),
				*SetSpec));
		}
		SetClasses.Add(SetClass);
		if (ASC)
		{
			if (const UAttributeSet* Registered = ASC->GetAttributeSet(SetClass))
			{
				InstanceByClass.Add(SetClass, const_cast<UAttributeSet*>(Registered));
			}
		}
	}
	else
	{
		for (const UAttributeSet* Set : ASC->GetSpawnedAttributes())
		{
			if (!IsValid(Set)) continue;
			SetClasses.AddUnique(Set->GetClass());
			InstanceByClass.Add(Set->GetClass(), const_cast<UAttributeSet*>(Set));
		}
		if (SetClasses.Num() == 0)
		{
			auto Empty = MCPSuccess();
			Empty->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
			Empty->SetNumberField(TEXT("attributeSetCount"), 0);
			Empty->SetArrayField(TEXT("attributeSets"), TArray<TSharedPtr<FJsonValue>>());
			Empty->SetStringField(TEXT("note"), TEXT(
				"This actor's AbilitySystemComponent has no REGISTERED attribute set. In a world that has "
				"not begun play that is normal even when the actor owns one, because the scan that "
				"registers them runs in InitializeComponent. Pass 'attributeSet' to audit the class "
				"itself, call gas(init_asc) to register the actor's sets, or wire one onto the "
				"Blueprint with gas(set_asc_defaults)."));
			return MCPResult(Empty);
		}
	}

	TArray<TSharedPtr<FJsonValue>> SetRows;
	for (UClass* SetClass : SetClasses)
	{
		// The one hard, provable fact about clamping. PreAttributeChange and
		// PreAttributeBaseChange are plain C++ virtuals on UAttributeSet
		// (AttributeSet.h:220 and :230), not UFUNCTIONs, so a Blueprint
		// AttributeSet has no way to override either. There is no data-driven
		// clamp anywhere in GameplayAbilities: FAttributeMetaData's MinValue /
		// MaxValue (AttributeSet.h:283-286) only seed initial values through
		// InitFromMetaDataTable and are never consulted again.
		const bool bIsBlueprint = SetClass->ClassGeneratedBy != nullptr;

		TSharedPtr<FJsonObject> SetRow = MakeShared<FJsonObject>();
		SetRow->SetStringField(TEXT("attributeSetClass"), SetClass->GetPathName());
		SetRow->SetBoolField(TEXT("isBlueprint"), bIsBlueprint);
		SetRow->SetStringField(TEXT("clamping"), bIsBlueprint ? TEXT("impossible") : TEXT("possible-in-cpp"));
		SetRow->SetStringField(TEXT("clampingReason"), bIsBlueprint
			? TEXT("Blueprint AttributeSet. UAttributeSet::PreAttributeChange and PreAttributeBaseChange "
				   "are plain C++ virtuals rather than UFUNCTIONs, so a Blueprint cannot override either, "
				   "and UE 5.8 ships no data-driven clamp: an FAttributeMetaData row's MinValue/MaxValue "
				   "only seed the initial value. Health WILL go negative and past its maximum. The two "
				   "routes that work: author a C++ AttributeSet (project(write_cpp_file) plus a rebuild) "
				   "and override PreAttributeChange, or bound the change at its source so nothing "
				   "over-applies in the first place.")
			: TEXT("Native AttributeSet, so it CAN override PreAttributeChange / PreAttributeBaseChange. "
				   "Whether it does is not visible through reflection, because those are plain virtuals "
				   "with no UFUNCTION entry to look up. Pass probeClamping=true against a live actor to "
				   "measure it instead of guessing."));

		UAttributeSet** Found = InstanceByClass.Find(SetClass);
		UAttributeSet* Instance = Found ? *Found : nullptr;
		if (Instance) SetRow->SetStringField(TEXT("instancePath"), Instance->GetPathName());

		// Build the name set first so the Max-pair convention can be tested.
		TSet<FString> AttributeNames;
		for (TFieldIterator<FProperty> It(SetClass); It; ++It)
		{
			const FStructProperty* SProp = CastField<FStructProperty>(*It);
			if (SProp && SProp->Struct == FGameplayAttributeData::StaticStruct())
			{
				AttributeNames.Add(SProp->GetName());
			}
		}

		TArray<TSharedPtr<FJsonValue>> AttributeRows;
		for (TFieldIterator<FProperty> It(SetClass); It; ++It)
		{
			FStructProperty* SProp = CastField<FStructProperty>(*It);
			if (!SProp || SProp->Struct != FGameplayAttributeData::StaticStruct()) continue;

			const FString Name = SProp->GetName();
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("attribute"), Name);
			Row->SetStringField(TEXT("qualifiedName"), SetClass->GetName() + TEXT(".") + Name);

			// Provable facts.
			const bool bReplicated = SProp->HasAnyPropertyFlags(CPF_Net);
			Row->SetBoolField(TEXT("replicated"), bReplicated);
			const FName RepNotify = SProp->RepNotifyFunc;
			Row->SetStringField(TEXT("repNotifyFunction"),
				RepNotify.IsNone() ? FString() : RepNotify.ToString());
			// The GAS convention is OnRep_<Attribute> calling
			// GAMEPLAYATTRIBUTE_REPNOTIFY. Its absence on a replicated attribute
			// means predicted values are never corrected on the client.
			const FString ExpectedOnRep = TEXT("OnRep_") + Name;
			const bool bHasOnRep = SetClass->FindFunctionByName(FName(*ExpectedOnRep)) != nullptr;
			Row->SetBoolField(TEXT("hasOnRepFunction"), bHasOnRep);

			const FString MaxName = TEXT("Max") + Name;
			const bool bHasMaxPair = AttributeNames.Contains(MaxName);
			Row->SetBoolField(TEXT("hasMaxAttribute"), bHasMaxPair);
			if (bHasMaxPair) Row->SetStringField(TEXT("maxAttribute"), MaxName);

			// Values, from the instance when there is one and from the CDO
			// otherwise. Named so the caller knows which they got.
			const UObject* ValueSource = Instance ? static_cast<const UObject*>(Instance)
												  : static_cast<const UObject*>(SetClass->GetDefaultObject());
			if (ValueSource)
			{
				const FGameplayAttributeData* Data = SProp->ContainerPtrToValuePtr<FGameplayAttributeData>(ValueSource);
				Row->SetNumberField(TEXT("baseValue"), Data->GetBaseValue());
				Row->SetNumberField(TEXT("currentValue"), Data->GetCurrentValue());
				Row->SetStringField(TEXT("valuesReadFrom"), Instance ? TEXT("live-instance") : TEXT("class-default"));
			}

			// Meta-attribute inference, labelled as an inference. A meta
			// attribute is one that exists only to carry a value into
			// PostGameplayEffectExecute (Damage, Healing) rather than to hold
			// state, and the engine has no flag for that. Two things are
			// provable: it does not replicate (a meta attribute must not, since
			// it is consumed on the authority), and it has no Max pair. The rest
			// is the naming convention, so it is reported as "likely" with the
			// evidence beside it rather than asserted.
			const bool bLikelyMeta = !bReplicated && !bHasMaxPair && !bHasOnRep;
			Row->SetBoolField(TEXT("likelyMetaAttribute"), bLikelyMeta);
			Row->SetStringField(TEXT("classification"),
				bLikelyMeta ? TEXT("likely-meta") : (bReplicated ? TEXT("replicated-state") : TEXT("local-state")));
			if (bReplicated && !bHasOnRep)
			{
				Row->SetStringField(TEXT("warning"), FString::Printf(TEXT(
					"'%s' is marked replicated but the class has no %s function, so a client's predicted "
					"value is never corrected from the server. The GAS convention is an OnRep_ that calls "
					"GAMEPLAYATTRIBUTE_REPNOTIFY."), *Name, *ExpectedOnRep));
			}

			// The measurement, opt-in. Calling a game's own PreAttributeChange
			// is the only way to learn whether it clamps, and it runs project
			// code: a poorly written override that dereferences its owning ASC
			// would fault. So it never runs against a class default object,
			// only against a set actually registered on a live actor (which has
			// an owner), and only when the caller asks for it.
			if (bProbeClamping && Instance)
			{
				const FGameplayAttribute Attribute(SProp);

				float LowProbe = -1.0e9f;
				Instance->PreAttributeChange(Attribute, LowProbe);
				float HighProbe = 1.0e9f;
				Instance->PreAttributeChange(Attribute, HighProbe);

				float LowBaseProbe = -1.0e9f;
				Instance->PreAttributeBaseChange(Attribute, LowBaseProbe);
				float HighBaseProbe = 1.0e9f;
				Instance->PreAttributeBaseChange(Attribute, HighBaseProbe);

				TSharedPtr<FJsonObject> Probe = MakeShared<FJsonObject>();
				Probe->SetBoolField(TEXT("clampsLow"), LowProbe > -1.0e9f);
				Probe->SetBoolField(TEXT("clampsHigh"), HighProbe < 1.0e9f);
				Probe->SetNumberField(TEXT("lowResult"), LowProbe);
				Probe->SetNumberField(TEXT("highResult"), HighProbe);
				Probe->SetBoolField(TEXT("baseClampsLow"), LowBaseProbe > -1.0e9f);
				Probe->SetBoolField(TEXT("baseClampsHigh"), HighBaseProbe < 1.0e9f);
				Probe->SetNumberField(TEXT("baseLowResult"), LowBaseProbe);
				Probe->SetNumberField(TEXT("baseHighResult"), HighBaseProbe);
				Probe->SetStringField(TEXT("method"), TEXT(
					"Measured: PreAttributeChange and PreAttributeBaseChange were called on the live "
					"registered set with -1e9 and +1e9. A returned value that moved is a clamp, and the "
					"bound it moved to is the clamp's edge AT THIS MOMENT - a clamp written against "
					"MaxHealth reports whatever MaxHealth currently is."));
				Row->SetObjectField(TEXT("clampProbe"), Probe);
			}
			else if (bProbeClamping && !Instance)
			{
				Row->SetStringField(TEXT("clampProbeSkipped"), TEXT(
					"probeClamping needs a live registered attribute set, because the probe calls the "
					"project's own PreAttributeChange and an override that reads its owning ASC would "
					"fault on a class default object. Pass actorLabel or actorPath."));
			}

			AttributeRows.Add(MakeShared<FJsonValueObject>(Row));
		}

		SetRow->SetNumberField(TEXT("attributeCount"), AttributeRows.Num());
		SetRow->SetArrayField(TEXT("attributes"), AttributeRows);
		SetRows.Add(MakeShared<FJsonValueObject>(SetRow));
	}

	auto Result = MCPSuccess();
	if (Actor)
	{
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	}
	Result->SetNumberField(TEXT("attributeSetCount"), SetRows.Num());
	Result->SetArrayField(TEXT("attributeSets"), SetRows);
	Result->SetBoolField(TEXT("clampingProbed"), bProbeClamping);
	Result->SetStringField(TEXT("scope"), TEXT(
		"Reads only. There is deliberately no configure_attribute_clamping counterpart: UE 5.8 has no "
		"data-driven attribute clamp, so a setter would have nothing to write. Everything else about an "
		"attribute set that IS a property - default values, replication condition, the init DataTable - is "
		"reachable through asset(set_property) on the set's Blueprint CDO."));
	return MCPResult(Result);
}
