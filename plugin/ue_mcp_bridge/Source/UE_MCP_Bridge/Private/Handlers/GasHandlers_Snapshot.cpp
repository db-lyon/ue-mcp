// GAS state snapshots, and a diff that names the change.
//
// The loop this exists for: capture an actor's whole ability-system state, do
// something (apply an effect, activate an ability, run a few frames of PIE),
// capture again, and be told exactly what moved. `get_asc_state`,
// `get_active_effects` and `get_attribute` each answer one slice of that
// question and none of them answers "what changed", which is the question
// actually being asked when a GAS setup misbehaves.
//
// ── Two design decisions worth stating ──────────────────────────────────────
//
// 1. The diff reports CHANGES, not two blobs. Handing back both snapshots and
//    letting the caller compare them is what a caller can already do; the value
//    is in the comparison. Every entry in `changes[]` has a `kind` naming what
//    happened ("effect_applied", "attribute_changed", "tag_gained"), the
//    subject it happened to, before/after values where they exist, and a
//    human-readable `detail`. A caller can branch on `kind` or read `summary`.
//
// 2. Time is not a change. An active effect's time remaining moves every frame,
//    so counting it as a change would make every diff report churn and bury the
//    one thing that actually happened. Effects still active in both snapshots
//    are reported under `stillActive[]` with their time remaining on both
//    sides, and the wall-clock gap is reported once as `timeElapsedSeconds`.
//
// The store is in-process and lives as long as the editor session. That is the
// right lifetime for "capture, act, compare" and the wrong one for anything
// durable, so `capture_gas_state` also RETURNS the snapshot it stored: a caller
// that needs it to outlive the session keeps the object and passes it back
// through `beforeSnapshot` / `afterSnapshot` instead of by id.

#include "GasHandlers.h"
#include "HandlerUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayAbilitySpec.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "UObject/UnrealType.h"

namespace
{
	/** Unity build: every file-local name here is prefixed so it cannot collide
	 *  with a helper in another handler translation unit (C2084). */

	/** How many snapshots the session keeps before evicting the oldest. */
	constexpr int32 MCPGasSnapMaxStored = 64;

	struct FMCPGasSnapStore
	{
		TMap<FString, TSharedPtr<FJsonObject>> ById;
		/** Insertion order, so eviction is oldest-first and the listing is stable. */
		TArray<FString> Order;
	};

	FMCPGasSnapStore& MCPGasSnapGetStore()
	{
		// Function-local static rather than a file-scope object: handlers run on
		// the game thread only (MCP_CHECK_GAME_THREAD asserts it), so no lock is
		// needed, but construction order across a unity blob is not something to
		// depend on.
		static FMCPGasSnapStore Store;
		return Store;
	}

	void MCPGasSnapPut(const FString& Id, TSharedPtr<FJsonObject> Snapshot, TArray<FString>& OutEvicted)
	{
		FMCPGasSnapStore& Store = MCPGasSnapGetStore();
		if (!Store.ById.Contains(Id)) Store.Order.Add(Id);
		Store.ById.Add(Id, Snapshot);

		while (Store.Order.Num() > MCPGasSnapMaxStored)
		{
			const FString Oldest = Store.Order[0];
			Store.Order.RemoveAt(0);
			Store.ById.Remove(Oldest);
			OutEvicted.Add(Oldest);
		}
	}

	/** A tag container as a JSON array of tag strings. */
	void MCPGasSnapSetTags(
		TSharedPtr<FJsonObject> Obj,
		const TCHAR* Field,
		const FGameplayTagContainer& Tags)
	{
		TArray<TSharedPtr<FJsonValue>> List;
		for (const FGameplayTag& Tag : Tags) List.Add(MakeShared<FJsonValueString>(Tag.ToString()));
		Obj->SetArrayField(Field, List);
	}

	/**
	 * Build the snapshot for the actor named in Params.
	 *
	 * Shared by capture_gas_state and by compare_gas_states when the caller
	 * omits `after`, which is the ergonomic that makes the whole loop one call
	 * shorter: capture, act, compare against the id and let the comparison take
	 * the second reading itself.
	 */
	TSharedPtr<FJsonObject> MCPGasSnapBuild(
		const TSharedPtr<FJsonObject>& Params,
		TSharedPtr<FJsonValue>& OutError)
	{
		AActor* Actor = nullptr;
		UAbilitySystemComponent* ASC = MCPGas::ResolveActorASC(Params, Actor, OutError);
		if (!ASC) return nullptr;

		// An actor spawned into the editor world has no registered attribute
		// set until something adopts its subobjects, and a snapshot showing
		// zero attributes for an actor that plainly has one is a wrong answer
		// rather than a missing one. Same default, and same opt-out, as
		// get_live_attribute_value.
		TArray<FString> Adopted;
		if (OptionalBool(Params, TEXT("registerOwnerSets"), true))
		{
			MCPGas::AdoptOwnerAttributeSets(ASC, Actor, Adopted);
		}

		UWorld* World = ASC->GetWorld();
		TSharedPtr<FJsonObject> Snap = MakeShared<FJsonObject>();
		Snap->SetStringField(TEXT("capturedAt"), FDateTime::UtcNow().ToIso8601());
		Snap->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Snap->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		Snap->SetStringField(TEXT("world"),
			World ? (World->IsPlayInEditor() ? TEXT("pie") : TEXT("editor")) : TEXT("none"));
		Snap->SetNumberField(TEXT("worldTimeSeconds"), World ? World->GetTimeSeconds() : 0.0);
		Snap->SetBoolField(TEXT("authoritative"), ASC->IsOwnerActorAuthoritative());

		// InitAbilityActorInfo state, because "nothing works" on an
		// uninitialised ASC and a diff that shows it flipping to true is often
		// the whole explanation.
		const FGameplayAbilityActorInfo* ActorInfo = ASC->AbilityActorInfo.Get();
		const bool bAscInitialised = ActorInfo != nullptr
			&& ActorInfo->OwnerActor.IsValid()
			&& ActorInfo->AvatarActor.IsValid();
		Snap->SetBoolField(TEXT("ascInitialized"), bAscInitialised);

		// ── Granted abilities ───────────────────────────────────────────
		TArray<TSharedPtr<FJsonValue>> Abilities;
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			TSharedPtr<FJsonObject> Row = MCPGas::DescribeAbilitySpec(Spec);
			Row->SetNumberField(TEXT("activeCount"), Spec.ActiveCount);
			Row->SetBoolField(TEXT("inputPressed"), Spec.InputPressed != 0);
			MCPGasSnapSetTags(Row, TEXT("dynamicTags"), Spec.GetDynamicSpecSourceTags());
			Abilities.Add(MakeShared<FJsonValueObject>(Row));
		}
		Snap->SetArrayField(TEXT("abilities"), Abilities);

		// ── Active effects ──────────────────────────────────────────────
		const FGameplayEffectQuery Query;
		TArray<TSharedPtr<FJsonValue>> Effects;
		for (const FActiveGameplayEffectHandle& Handle : ASC->GetActiveEffects(Query))
		{
			const FActiveGameplayEffect* Active = ASC->GetActiveGameplayEffect(Handle);
			if (!Active) continue;

			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			const UGameplayEffect* Def = Active->Spec.Def;
			Row->SetStringField(TEXT("effectClass"), Def ? Def->GetClass()->GetPathName() : TEXT("None"));
			// The handle's int is private; ToString is the public identity, and
			// it is what makes "this exact application" distinguishable from a
			// second application of the same class.
			Row->SetStringField(TEXT("handle"), Handle.ToString());
			Row->SetNumberField(TEXT("level"), Active->Spec.GetLevel());
			Row->SetNumberField(TEXT("stackCount"), Active->Spec.GetStackCount());
			Row->SetNumberField(TEXT("duration"), Active->GetDuration());
			Row->SetNumberField(TEXT("timeRemaining"),
				World ? Active->GetTimeRemaining(World->GetTimeSeconds()) : 0.0);
			Row->SetBoolField(TEXT("inhibited"), Active->bIsInhibited);
			if (const AActor* Instigator = Active->Spec.GetEffectContext().GetInstigator())
			{
				Row->SetStringField(TEXT("instigator"), Instigator->GetActorLabel());
			}
			FGameplayTagContainer GrantedTags;
			Active->Spec.GetAllGrantedTags(GrantedTags);
			MCPGasSnapSetTags(Row, TEXT("grantedTags"), GrantedTags);
			Effects.Add(MakeShared<FJsonValueObject>(Row));
		}
		Snap->SetArrayField(TEXT("effects"), Effects);

		// ── Attributes, base and current ────────────────────────────────
		// Both halves, because they answer different questions: base is what a
		// durable change wrote, current is what the aggregator computed after
		// every active modifier. A diff that showed only one would hide exactly
		// the case an agent is usually chasing (a buff applied but not stacking).
		TArray<TSharedPtr<FJsonValue>> Attributes;
		for (const UAttributeSet* Set : ASC->GetSpawnedAttributes())
		{
			if (!IsValid(Set)) continue;
			UClass* SetClass = Set->GetClass();
			const FString SetName = SetClass->GetName();
			for (TFieldIterator<FProperty> It(SetClass); It; ++It)
			{
				FStructProperty* SProp = CastField<FStructProperty>(*It);
				if (!SProp || SProp->Struct != FGameplayAttributeData::StaticStruct()) continue;

				const FGameplayAttribute Attribute(SProp);
				TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("key"), SetName + TEXT(".") + SProp->GetName());
				Row->SetStringField(TEXT("set"), SetName);
				Row->SetStringField(TEXT("attribute"), SProp->GetName());
				Row->SetNumberField(TEXT("baseValue"), ASC->GetNumericAttributeBase(Attribute));
				Row->SetNumberField(TEXT("currentValue"), ASC->GetNumericAttribute(Attribute));
				Attributes.Add(MakeShared<FJsonValueObject>(Row));
			}
		}
		Snap->SetArrayField(TEXT("attributes"), Attributes);

		// ── Tags, with their counts ─────────────────────────────────────
		// The count matters: two effects each granting Status.Stunned leave the
		// tag present once and counted twice, so removing one does not remove
		// the tag. A diff over presence alone would report "no change" for a
		// removal that really happened.
		FGameplayTagContainer Owned;
		ASC->GetOwnedGameplayTags(Owned);
		TArray<TSharedPtr<FJsonValue>> Tags;
		for (const FGameplayTag& Tag : Owned)
		{
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("tag"), Tag.ToString());
			Row->SetNumberField(TEXT("count"), ASC->GetTagCount(Tag));
			Tags.Add(MakeShared<FJsonValueObject>(Row));
		}
		Snap->SetArrayField(TEXT("ownedTags"), Tags);

		// Which tags are currently blocking ability activation, straight off the
		// ASC's own container rather than inferred from the abilities.
		MCPGasSnapSetTags(Snap, TEXT("blockedAbilityTags"), ASC->GetBlockedAbilityTags());

		if (Adopted.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> AdoptedList;
			for (const FString& Name : Adopted) AdoptedList.Add(MakeShared<FJsonValueString>(Name));
			Snap->SetArrayField(TEXT("registeredOwnerSets"), AdoptedList);
		}

		return Snap;
	}

	/* ── Diff helpers ─────────────────────────────────────────────────── */

	double MCPGasSnapNumber(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, double Fallback = 0.0)
	{
		double Value = Fallback;
		return Obj.IsValid() && Obj->TryGetNumberField(Field, Value) ? Value : Fallback;
	}

	FString MCPGasSnapString(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field)
	{
		FString Value;
		return Obj.IsValid() && Obj->TryGetStringField(Field, Value) ? Value : FString();
	}

	bool MCPGasSnapBool(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, bool Fallback = false)
	{
		bool Value = Fallback;
		return Obj.IsValid() && Obj->TryGetBoolField(Field, Value) ? Value : Fallback;
	}

	/** Index one snapshot array by a key field, so both sides can be walked once. */
	void MCPGasSnapIndex(
		const TSharedPtr<FJsonObject>& Snapshot,
		const TCHAR* ArrayField,
		const TCHAR* KeyField,
		TMap<FString, TSharedPtr<FJsonObject>>& OutByKey,
		TArray<FString>& OutOrder)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Snapshot.IsValid() || !Snapshot->TryGetArrayField(ArrayField, Array) || !Array) return;
		for (const TSharedPtr<FJsonValue>& Value : *Array)
		{
			const TSharedPtr<FJsonObject>* Row = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Row) || !Row) continue;
			const FString Key = MCPGasSnapString(*Row, KeyField);
			if (Key.IsEmpty() || OutByKey.Contains(Key)) continue;
			OutByKey.Add(Key, *Row);
			OutOrder.Add(Key);
		}
	}

	/** A tag array (either the {tag,count} form or a bare string array) as counts. */
	void MCPGasSnapTagCounts(
		const TSharedPtr<FJsonObject>& Snapshot,
		const TCHAR* ArrayField,
		TMap<FString, int32>& OutCounts,
		TArray<FString>& OutOrder)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Snapshot.IsValid() || !Snapshot->TryGetArrayField(ArrayField, Array) || !Array) return;
		for (const TSharedPtr<FJsonValue>& Value : *Array)
		{
			if (!Value.IsValid()) continue;
			FString Tag;
			int32 Count = 1;
			const TSharedPtr<FJsonObject>* Row = nullptr;
			if (Value->TryGetObject(Row) && Row)
			{
				Tag = MCPGasSnapString(*Row, TEXT("tag"));
				Count = static_cast<int32>(MCPGasSnapNumber(*Row, TEXT("count"), 1.0));
			}
			else
			{
				Value->TryGetString(Tag);
			}
			if (Tag.IsEmpty() || OutCounts.Contains(Tag)) continue;
			OutCounts.Add(Tag, Count);
			OutOrder.Add(Tag);
		}
	}

	/** Values are floats out of a JSON round trip, so exact equality is wrong. */
	bool MCPGasSnapSameNumber(double A, double B)
	{
		return FMath::IsNearlyEqual(A, B, 1.0e-4);
	}

	/** Resolve `before` / `after`: a stored id, or an inline snapshot object. */
	TSharedPtr<FJsonObject> MCPGasSnapResolveSide(
		const TSharedPtr<FJsonObject>& Params,
		const TCHAR* IdField,
		const TCHAR* ObjectField,
		FString& OutSource,
		TSharedPtr<FJsonValue>& OutError)
	{
		const TSharedPtr<FJsonObject>* Inline = nullptr;
		if (Params->TryGetObjectField(ObjectField, Inline) && Inline && (*Inline).IsValid())
		{
			OutSource = TEXT("inline");
			return *Inline;
		}

		const FString Id = OptionalString(Params, IdField);
		if (Id.IsEmpty()) return nullptr;

		FMCPGasSnapStore& Store = MCPGasSnapGetStore();
		if (TSharedPtr<FJsonObject>* Found = Store.ById.Find(Id))
		{
			OutSource = Id;
			return *Found;
		}

		// Name what is actually there. A stale id after an editor restart is the
		// common case and the message has to say that, not just "not found".
		const FString Known = Store.Order.Num() > 0
			? FString::Join(Store.Order, TEXT(", "))
			: FString(TEXT("(none)"));
		OutError = MCPError(FString::Printf(
			TEXT("No GAS snapshot with id '%s'. The store is per editor session and is emptied by a ")
			TEXT("restart, and it keeps the %d most recent. Stored ids: %s. Capture one with ")
			TEXT("gas(capture_gas_state), or pass the snapshot object itself as '%s'."),
			*Id, MCPGasSnapMaxStored, *Known, ObjectField));
		return nullptr;
	}

	/**
	 * Defined further down, after the handlers that only read. Declared here so
	 * capture_gas_state can call it: both anonymous-namespace blocks in this
	 * file are the same namespace, so one declaration and one definition is all
	 * it takes, and the definition stays next to compare_gas_states where it is
	 * read.
	 */
	TSharedPtr<FJsonObject> MCPGasSnapDiff(
		const TSharedPtr<FJsonObject>& Before,
		const TSharedPtr<FJsonObject>& After,
		const FString& BeforeSource,
		const FString& AfterSource);

	/** One change row. */
	void MCPGasSnapAddChange(
		TArray<TSharedPtr<FJsonValue>>& Changes,
		TMap<FString, int32>& ByKind,
		const FString& Kind,
		const FString& Subject,
		const FString& Detail,
		TSharedPtr<FJsonObject> Before = nullptr,
		TSharedPtr<FJsonObject> After = nullptr)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("kind"), Kind);
		Row->SetStringField(TEXT("subject"), Subject);
		Row->SetStringField(TEXT("detail"), Detail);
		if (Before.IsValid()) Row->SetObjectField(TEXT("before"), Before);
		if (After.IsValid()) Row->SetObjectField(TEXT("after"), After);
		Changes.Add(MakeShared<FJsonValueObject>(Row));
		ByKind.FindOrAdd(Kind) += 1;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// capture / list / delete
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FGasHandlers::CaptureGasState(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	TSharedPtr<FJsonValue> Error;
	TSharedPtr<FJsonObject> Snapshot = MCPGasSnapBuild(Params, Error);
	if (!Snapshot.IsValid()) return Error;

	FString Id = OptionalString(Params, TEXT("snapshotId"));
	if (Id.IsEmpty())
	{
		Id = FString::Printf(TEXT("gas-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12));
	}

	FMCPGasSnapStore& Store = MCPGasSnapGetStore();
	const bool bOverwrote = Store.ById.Contains(Id);

	Snapshot->SetStringField(TEXT("snapshotId"), Id);

	TArray<FString> Evicted;
	MCPGasSnapPut(Id, Snapshot, Evicted);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("snapshotId"), Id);
	Result->SetObjectField(TEXT("snapshot"), Snapshot);
	Result->SetNumberField(TEXT("storedSnapshots"), Store.Order.Num());
	Result->SetBoolField(TEXT("unchanged"), false);
	if (bOverwrote) MCPSetUpdated(Result);
	else MCPSetCreated(Result);

	if (Evicted.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> EvictedList;
		for (const FString& Gone : Evicted) EvictedList.Add(MakeShared<FJsonValueString>(Gone));
		Result->SetArrayField(TEXT("evicted"), EvictedList);
	}

	// The other half of the loop, in one call: capture, and diff against the
	// earlier reading in the same breath. This lives here rather than on
	// compare_gas_states because capturing touches the live ASC (it registers
	// the actor's attribute sets in a world that has not begun play), so it
	// belongs on the action gated as a mutation.
	const FString CompareWith = OptionalString(Params, TEXT("compareWith"));
	if (!CompareWith.IsEmpty())
	{
		FMCPGasSnapStore& CompareStore = MCPGasSnapGetStore();
		if (TSharedPtr<FJsonObject>* Earlier = CompareStore.ById.Find(CompareWith))
		{
			Result->SetObjectField(TEXT("diff"), MCPGasSnapDiff(*Earlier, Snapshot, CompareWith, Id));
		}
		else
		{
			const FString Known = CompareStore.Order.Num() > 0
				? FString::Join(CompareStore.Order, TEXT(", "))
				: FString(TEXT("(none)"));
			// The snapshot was still taken and stored, so this is a warning
			// rather than a failure: losing the capture over a bad id would
			// throw away the reading the caller came for.
			Result->SetStringField(TEXT("compareWarning"), FString::Printf(
				TEXT("compareWith='%s' names no stored snapshot, so no diff was produced. The capture "
					 "itself succeeded and is stored as '%s'. Stored ids: %s."),
				*CompareWith, *Id, *Known));
		}
	}

	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	RollbackPayload->SetStringField(TEXT("snapshotId"), Id);
	MCPSetRollback(Result, TEXT("delete_gas_snapshot"), RollbackPayload);
	if (bOverwrote)
	{
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(TEXT(
			"Lossy: id '%s' already held a snapshot and this call replaced it. The rollback deletes the "
			"new one; it cannot restore the old one, which is gone. Let the id be generated, or pick an "
			"unused one, when both readings matter."), *Id));
	}
	else
	{
		Result->SetStringField(TEXT("rollbackNote"), TEXT(
			"The rollback deletes the stored copy. It cannot un-read the world, and does not need to: "
			"capturing a snapshot changes nothing about the actor."));
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::ListGasSnapshots(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	const FString ActorFilter = OptionalString(Params, TEXT("actorPath"));
	const bool bIncludeSnapshots = OptionalBool(Params, TEXT("includeSnapshots"), false);

	FMCPGasSnapStore& Store = MCPGasSnapGetStore();
	TArray<TSharedPtr<FJsonValue>> Rows;
	for (const FString& Id : Store.Order)
	{
		TSharedPtr<FJsonObject>* Found = Store.ById.Find(Id);
		if (!Found || !(*Found).IsValid()) continue;
		const TSharedPtr<FJsonObject>& Snapshot = *Found;

		const FString ActorPath = MCPGasSnapString(Snapshot, TEXT("actorPath"));
		if (!ActorFilter.IsEmpty() && ActorPath != ActorFilter) continue;

		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("snapshotId"), Id);
		Row->SetStringField(TEXT("capturedAt"), MCPGasSnapString(Snapshot, TEXT("capturedAt")));
		Row->SetStringField(TEXT("actorLabel"), MCPGasSnapString(Snapshot, TEXT("actorLabel")));
		Row->SetStringField(TEXT("actorPath"), ActorPath);
		Row->SetStringField(TEXT("world"), MCPGasSnapString(Snapshot, TEXT("world")));
		Row->SetNumberField(TEXT("worldTimeSeconds"), MCPGasSnapNumber(Snapshot, TEXT("worldTimeSeconds")));

		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		Row->SetNumberField(TEXT("abilityCount"),
			Snapshot->TryGetArrayField(TEXT("abilities"), Array) && Array ? Array->Num() : 0);
		Row->SetNumberField(TEXT("effectCount"),
			Snapshot->TryGetArrayField(TEXT("effects"), Array) && Array ? Array->Num() : 0);
		Row->SetNumberField(TEXT("attributeCount"),
			Snapshot->TryGetArrayField(TEXT("attributes"), Array) && Array ? Array->Num() : 0);

		if (bIncludeSnapshots) Row->SetObjectField(TEXT("snapshot"), Snapshot);
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("count"), Rows.Num());
	Result->SetNumberField(TEXT("storedSnapshots"), Store.Order.Num());
	Result->SetNumberField(TEXT("capacity"), MCPGasSnapMaxStored);
	Result->SetArrayField(TEXT("snapshots"), Rows);
	Result->SetStringField(TEXT("lifetime"), TEXT(
		"Snapshots live in the editor process and are lost on restart. Keep the object returned by "
		"capture_gas_state if a comparison has to survive one."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::DeleteGasSnapshot(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FString Id;
	if (auto Err = RequireString(Params, TEXT("snapshotId"), Id)) return Err;

	FMCPGasSnapStore& Store = MCPGasSnapGetStore();
	TSharedPtr<FJsonObject>* Found = Store.ById.Find(Id);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("snapshotId"), Id);

	if (!Found)
	{
		// Replaying capture's rollback must be safe.
		Result->SetBoolField(TEXT("alreadyDeleted"), true);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetNumberField(TEXT("storedSnapshots"), Store.Order.Num());
		TSharedPtr<FJsonObject> NoopPayload = MakeShared<FJsonObject>();
		NoopPayload->SetStringField(TEXT("snapshotId"), Id);
		MCPSetRollback(Result, TEXT("delete_gas_snapshot"), NoopPayload);
		Result->SetStringField(TEXT("rollbackNote"), TEXT(
			"Nothing was deleted, so the rollback is the same no-op call."));
		return MCPResult(Result);
	}

	// Hand the content back before dropping it. That is what makes this
	// reversible at all: the caller can pass the returned object straight to
	// compare_gas_states as beforeSnapshot / afterSnapshot. Copied out of the
	// map FIRST: `Found` points into the map's storage and Remove invalidates it.
	TSharedPtr<FJsonObject> Deleted = *Found;
	Result->SetObjectField(TEXT("deletedSnapshot"), Deleted);
	Store.ById.Remove(Id);
	Store.Order.Remove(Id);

	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("unchanged"), false);
	Result->SetNumberField(TEXT("storedSnapshots"), Store.Order.Num());

	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	RollbackPayload->SetStringField(TEXT("snapshotId"), Id);
	RollbackPayload->SetStringField(TEXT("actorPath"), MCPGasSnapString(Deleted, TEXT("actorPath")));
	MCPSetRollback(Result, TEXT("capture_gas_state"), RollbackPayload);
	Result->SetStringField(TEXT("rollbackNote"), TEXT(
		"Lossy: the rollback re-captures the actor's CURRENT state under the same id, which is not the "
		"state that was recorded. Nothing is actually lost, though - the deleted snapshot is returned "
		"above as deletedSnapshot and can be passed back to compare_gas_states as beforeSnapshot."));

	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// compare: the diff, and the read that serves it
// ─────────────────────────────────────────────────────────────────────────────


// The diff itself, shared by compare_gas_states and by capture_gas_state's
// compareWith. One implementation, so the two can never disagree about what
// counts as a change.
namespace
{
	TSharedPtr<FJsonObject> MCPGasSnapDiff(
		const TSharedPtr<FJsonObject>& Before,
		const TSharedPtr<FJsonObject>& After,
		const FString& BeforeSource,
		const FString& AfterSource)
	{
		TArray<TSharedPtr<FJsonValue>> Changes;
		TMap<FString, int32> ByKind;
		TArray<TSharedPtr<FJsonValue>> Warnings;
		const auto Warn = [&Warnings](const FString& Text)
		{
			Warnings.Add(MakeShared<FJsonValueString>(Text));
		};

		// Comparing two different actors is legitimate (player against enemy) and
		// is also a common mistake, so it is flagged rather than refused or hidden.
		const FString BeforeActor = MCPGasSnapString(Before, TEXT("actorPath"));
		const FString AfterActor = MCPGasSnapString(After, TEXT("actorPath"));
		if (!BeforeActor.IsEmpty() && !AfterActor.IsEmpty() && BeforeActor != AfterActor)
		{
			Warn(FString::Printf(
				TEXT("The two snapshots are of DIFFERENT actors ('%s' and '%s'). Every difference below is a "
					 "difference between two actors, not a change over time."),
				*BeforeActor, *AfterActor));
		}
		const FString BeforeWorld = MCPGasSnapString(Before, TEXT("world"));
		const FString AfterWorld = MCPGasSnapString(After, TEXT("world"));
		if (!BeforeWorld.IsEmpty() && !AfterWorld.IsEmpty() && BeforeWorld != AfterWorld)
		{
			Warn(FString::Printf(
				TEXT("The snapshots come from different worlds ('%s' and '%s'). PIE and the editor world hold "
					 "separate actors and separate ability system state."),
				*BeforeWorld, *AfterWorld));
		}

		// ── ASC initialisation ──────────────────────────────────────────────
		const bool bInitBefore = MCPGasSnapBool(Before, TEXT("ascInitialized"));
		const bool bInitAfter = MCPGasSnapBool(After, TEXT("ascInitialized"));
		if (bInitBefore != bInitAfter)
		{
			MCPGasSnapAddChange(Changes, ByKind,
				bInitAfter ? TEXT("asc_initialized") : TEXT("asc_deinitialized"),
				TEXT("AbilitySystemComponent"),
				bInitAfter
					? TEXT("InitAbilityActorInfo has now run: abilities can activate where before they could not.")
					: TEXT("The ASC's actor info is no longer valid, so nothing on it can activate."));
		}

		// ── Abilities, keyed by class ───────────────────────────────────────
		TMap<FString, TSharedPtr<FJsonObject>> AbilBefore, AbilAfter;
		TArray<FString> AbilBeforeOrder, AbilAfterOrder;
		MCPGasSnapIndex(Before, TEXT("abilities"), TEXT("abilityClass"), AbilBefore, AbilBeforeOrder);
		MCPGasSnapIndex(After, TEXT("abilities"), TEXT("abilityClass"), AbilAfter, AbilAfterOrder);

		int32 AbilitiesUnchanged = 0;
		for (const FString& Key : AbilAfterOrder)
		{
			const TSharedPtr<FJsonObject>& NowRow = AbilAfter[Key];
			TSharedPtr<FJsonObject>* WasFound = AbilBefore.Find(Key);
			if (!WasFound)
			{
				MCPGasSnapAddChange(Changes, ByKind, TEXT("ability_granted"), Key,
					FString::Printf(TEXT("Granted at level %d, inputID %d."),
						static_cast<int32>(MCPGasSnapNumber(NowRow, TEXT("level"), 1.0)),
						static_cast<int32>(MCPGasSnapNumber(NowRow, TEXT("inputID"), -1.0))),
					nullptr, NowRow);
				continue;
			}
			const TSharedPtr<FJsonObject>& WasRow = *WasFound;
			bool bAbilityChanged = false;

			const int32 WasLevel = static_cast<int32>(MCPGasSnapNumber(WasRow, TEXT("level"), 1.0));
			const int32 NowLevel = static_cast<int32>(MCPGasSnapNumber(NowRow, TEXT("level"), 1.0));
			if (WasLevel != NowLevel)
			{
				bAbilityChanged = true;
				MCPGasSnapAddChange(Changes, ByKind, TEXT("ability_level_changed"), Key,
					FString::Printf(TEXT("Level %d -> %d."), WasLevel, NowLevel), WasRow, NowRow);
			}

			const int32 WasInput = static_cast<int32>(MCPGasSnapNumber(WasRow, TEXT("inputID"), -1.0));
			const int32 NowInput = static_cast<int32>(MCPGasSnapNumber(NowRow, TEXT("inputID"), -1.0));
			if (WasInput != NowInput)
			{
				bAbilityChanged = true;
				MCPGasSnapAddChange(Changes, ByKind, TEXT("ability_input_changed"), Key,
					FString::Printf(TEXT("inputID %d -> %d%s."), WasInput, NowInput,
						NowInput == INDEX_NONE ? TEXT(" (now unbound)") : TEXT("")),
					WasRow, NowRow);
			}

			const bool bWasActive = MCPGasSnapBool(WasRow, TEXT("active"));
			const bool bNowActive = MCPGasSnapBool(NowRow, TEXT("active"));
			if (bWasActive != bNowActive)
			{
				bAbilityChanged = true;
				MCPGasSnapAddChange(Changes, ByKind,
					bNowActive ? TEXT("ability_activated") : TEXT("ability_ended"), Key,
					bNowActive
						? FString::Printf(TEXT("Now running (%d instance(s))."),
							static_cast<int32>(MCPGasSnapNumber(NowRow, TEXT("activeCount"), 0.0)))
						: FString(TEXT("No longer running.")),
					WasRow, NowRow);
			}

			if (!bAbilityChanged) ++AbilitiesUnchanged;
		}
		for (const FString& Key : AbilBeforeOrder)
		{
			if (AbilAfter.Contains(Key)) continue;
			MCPGasSnapAddChange(Changes, ByKind, TEXT("ability_revoked"), Key,
				TEXT("No longer in the ASC's activatable abilities."), AbilBefore[Key], nullptr);
		}

		// ── Effects, keyed by the handle of one application ─────────────────
		// Handle, not class: the same effect applied twice is two applications, and
		// collapsing them by class would report "no change" when one of the two
		// expired.
		TMap<FString, TSharedPtr<FJsonObject>> FxBefore, FxAfter;
		TArray<FString> FxBeforeOrder, FxAfterOrder;
		MCPGasSnapIndex(Before, TEXT("effects"), TEXT("handle"), FxBefore, FxBeforeOrder);
		MCPGasSnapIndex(After, TEXT("effects"), TEXT("handle"), FxAfter, FxAfterOrder);

		TArray<TSharedPtr<FJsonValue>> StillActive;
		for (const FString& Key : FxAfterOrder)
		{
			const TSharedPtr<FJsonObject>& NowRow = FxAfter[Key];
			const FString EffectClass = MCPGasSnapString(NowRow, TEXT("effectClass"));
			TSharedPtr<FJsonObject>* WasFound = FxBefore.Find(Key);
			if (!WasFound)
			{
				MCPGasSnapAddChange(Changes, ByKind, TEXT("effect_applied"), EffectClass,
					FString::Printf(TEXT("Applied at level %d, %d stack(s), duration %.2fs."),
						static_cast<int32>(MCPGasSnapNumber(NowRow, TEXT("level"), 1.0)),
						static_cast<int32>(MCPGasSnapNumber(NowRow, TEXT("stackCount"), 1.0)),
						MCPGasSnapNumber(NowRow, TEXT("duration"))),
					nullptr, NowRow);
				continue;
			}
			const TSharedPtr<FJsonObject>& WasRow = *WasFound;

			const int32 WasStacks = static_cast<int32>(MCPGasSnapNumber(WasRow, TEXT("stackCount"), 1.0));
			const int32 NowStacks = static_cast<int32>(MCPGasSnapNumber(NowRow, TEXT("stackCount"), 1.0));
			if (WasStacks != NowStacks)
			{
				MCPGasSnapAddChange(Changes, ByKind, TEXT("effect_stack_changed"), EffectClass,
					FString::Printf(TEXT("Stacks %d -> %d (%+d)."), WasStacks, NowStacks, NowStacks - WasStacks),
					WasRow, NowRow);
			}

			const int32 WasLevel = static_cast<int32>(MCPGasSnapNumber(WasRow, TEXT("level"), 1.0));
			const int32 NowLevel = static_cast<int32>(MCPGasSnapNumber(NowRow, TEXT("level"), 1.0));
			if (WasLevel != NowLevel)
			{
				MCPGasSnapAddChange(Changes, ByKind, TEXT("effect_level_changed"), EffectClass,
					FString::Printf(TEXT("Level %d -> %d."), WasLevel, NowLevel), WasRow, NowRow);
			}

			const bool bWasInhibited = MCPGasSnapBool(WasRow, TEXT("inhibited"));
			const bool bNowInhibited = MCPGasSnapBool(NowRow, TEXT("inhibited"));
			if (bWasInhibited != bNowInhibited)
			{
				MCPGasSnapAddChange(Changes, ByKind, TEXT("effect_inhibition_changed"), EffectClass,
					bNowInhibited
						? FString(TEXT("Now inhibited: it is still applied but its modifiers are switched off, "
									   "usually by an ongoing tag requirement that stopped being met."))
						: FString(TEXT("No longer inhibited: its modifiers apply again.")),
					WasRow, NowRow);
			}

			// Time is not a change. Reported, but out of the change list, so the
			// list only carries things that actually happened.
			TSharedPtr<FJsonObject> Timing = MakeShared<FJsonObject>();
			Timing->SetStringField(TEXT("effectClass"), EffectClass);
			Timing->SetStringField(TEXT("handle"), Key);
			Timing->SetNumberField(TEXT("timeRemainingBefore"), MCPGasSnapNumber(WasRow, TEXT("timeRemaining")));
			Timing->SetNumberField(TEXT("timeRemainingAfter"), MCPGasSnapNumber(NowRow, TEXT("timeRemaining")));
			StillActive.Add(MakeShared<FJsonValueObject>(Timing));
		}
		for (const FString& Key : FxBeforeOrder)
		{
			if (FxAfter.Contains(Key)) continue;
			const TSharedPtr<FJsonObject>& WasRow = FxBefore[Key];
			const double WasRemaining = MCPGasSnapNumber(WasRow, TEXT("timeRemaining"));
			MCPGasSnapAddChange(Changes, ByKind, TEXT("effect_removed"),
				MCPGasSnapString(WasRow, TEXT("effectClass")),
				WasRemaining > 0.0
					? FString::Printf(TEXT("Gone, with %.2fs still on the clock, so it was removed rather than expired."), WasRemaining)
					: FString(TEXT("Gone. Its duration had run out.")),
				WasRow, nullptr);
		}

		// ── Attributes ──────────────────────────────────────────────────────
		TMap<FString, TSharedPtr<FJsonObject>> AttrBefore, AttrAfter;
		TArray<FString> AttrBeforeOrder, AttrAfterOrder;
		MCPGasSnapIndex(Before, TEXT("attributes"), TEXT("key"), AttrBefore, AttrBeforeOrder);
		MCPGasSnapIndex(After, TEXT("attributes"), TEXT("key"), AttrAfter, AttrAfterOrder);

		int32 AttributesUnchanged = 0;
		for (const FString& Key : AttrAfterOrder)
		{
			const TSharedPtr<FJsonObject>& NowRow = AttrAfter[Key];
			TSharedPtr<FJsonObject>* WasFound = AttrBefore.Find(Key);
			if (!WasFound)
			{
				MCPGasSnapAddChange(Changes, ByKind, TEXT("attribute_added"), Key,
					FString::Printf(TEXT("Attribute set now registered on the ASC. base %.3f, current %.3f."),
						MCPGasSnapNumber(NowRow, TEXT("baseValue")),
						MCPGasSnapNumber(NowRow, TEXT("currentValue"))),
					nullptr, NowRow);
				continue;
			}
			const TSharedPtr<FJsonObject>& WasRow = *WasFound;
			const double WasBase = MCPGasSnapNumber(WasRow, TEXT("baseValue"));
			const double NowBase = MCPGasSnapNumber(NowRow, TEXT("baseValue"));
			const double WasCurrent = MCPGasSnapNumber(WasRow, TEXT("currentValue"));
			const double NowCurrent = MCPGasSnapNumber(NowRow, TEXT("currentValue"));

			const bool bBaseMoved = !MCPGasSnapSameNumber(WasBase, NowBase);
			const bool bCurrentMoved = !MCPGasSnapSameNumber(WasCurrent, NowCurrent);
			if (!bBaseMoved && !bCurrentMoved)
			{
				++AttributesUnchanged;
				continue;
			}

			// Naming WHICH of the two moved is the point. A base that moved is a
			// durable write; a current that moved on its own is a modifier from an
			// active effect; a current that moved while base did not is the case an
			// agent most often misreads as "my write did not take".
			FString Detail;
			if (bBaseMoved && bCurrentMoved)
			{
				Detail = FString::Printf(
					TEXT("base %.3f -> %.3f (%+.3f), current %.3f -> %.3f (%+.3f)."),
					WasBase, NowBase, NowBase - WasBase, WasCurrent, NowCurrent, NowCurrent - WasCurrent);
			}
			else if (bBaseMoved)
			{
				Detail = FString::Printf(
					TEXT("base %.3f -> %.3f (%+.3f); current is unchanged at %.3f, which means an active "
						 "modifier is holding it there."),
					WasBase, NowBase, NowBase - WasBase, NowCurrent);
			}
			else
			{
				Detail = FString::Printf(
					TEXT("current %.3f -> %.3f (%+.3f) with base unchanged at %.3f, so this came from a "
						 "duration-based effect's modifier rather than from a write to the base value."),
					WasCurrent, NowCurrent, NowCurrent - WasCurrent, NowBase);
			}

			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("kind"), TEXT("attribute_changed"));
			Row->SetStringField(TEXT("subject"), Key);
			Row->SetStringField(TEXT("detail"), Detail);
			Row->SetObjectField(TEXT("before"), WasRow);
			Row->SetObjectField(TEXT("after"), NowRow);
			Row->SetNumberField(TEXT("baseDelta"), NowBase - WasBase);
			Row->SetNumberField(TEXT("currentDelta"), NowCurrent - WasCurrent);
			Changes.Add(MakeShared<FJsonValueObject>(Row));
			ByKind.FindOrAdd(TEXT("attribute_changed")) += 1;
		}
		for (const FString& Key : AttrBeforeOrder)
		{
			if (AttrAfter.Contains(Key)) continue;
			MCPGasSnapAddChange(Changes, ByKind, TEXT("attribute_removed"), Key,
				TEXT("The attribute set holding it is no longer registered on the ASC."),
				AttrBefore[Key], nullptr);
		}

		// ── Owned tags, with counts ─────────────────────────────────────────
		TMap<FString, int32> TagsBefore, TagsAfter;
		TArray<FString> TagsBeforeOrder, TagsAfterOrder;
		MCPGasSnapTagCounts(Before, TEXT("ownedTags"), TagsBefore, TagsBeforeOrder);
		MCPGasSnapTagCounts(After, TEXT("ownedTags"), TagsAfter, TagsAfterOrder);

		for (const FString& Tag : TagsAfterOrder)
		{
			const int32 NowCount = TagsAfter[Tag];
			const int32* WasCount = TagsBefore.Find(Tag);
			if (!WasCount)
			{
				MCPGasSnapAddChange(Changes, ByKind, TEXT("tag_gained"), Tag,
					FString::Printf(TEXT("Now owned, count %d."), NowCount));
			}
			else if (*WasCount != NowCount)
			{
				// A count change with the tag still present is what makes an
				// apparently missing removal make sense.
				MCPGasSnapAddChange(Changes, ByKind, TEXT("tag_count_changed"), Tag,
					FString::Printf(
						TEXT("Count %d -> %d. The tag is still present: something else is still granting it."),
						*WasCount, NowCount));
			}
		}
		for (const FString& Tag : TagsBeforeOrder)
		{
			if (TagsAfter.Contains(Tag)) continue;
			MCPGasSnapAddChange(Changes, ByKind, TEXT("tag_lost"), Tag, TEXT("No longer owned."));
		}

		// ── Blocked ability tags ────────────────────────────────────────────
		TMap<FString, int32> BlockedBefore, BlockedAfter;
		TArray<FString> BlockedBeforeOrder, BlockedAfterOrder;
		MCPGasSnapTagCounts(Before, TEXT("blockedAbilityTags"), BlockedBefore, BlockedBeforeOrder);
		MCPGasSnapTagCounts(After, TEXT("blockedAbilityTags"), BlockedAfter, BlockedAfterOrder);
		for (const FString& Tag : BlockedAfterOrder)
		{
			if (BlockedBefore.Contains(Tag)) continue;
			MCPGasSnapAddChange(Changes, ByKind, TEXT("ability_block_added"), Tag,
				TEXT("Abilities carrying this tag are now blocked from activating."));
		}
		for (const FString& Tag : BlockedBeforeOrder)
		{
			if (BlockedAfter.Contains(Tag)) continue;
			MCPGasSnapAddChange(Changes, ByKind, TEXT("ability_block_removed"), Tag,
				TEXT("Abilities carrying this tag are no longer blocked."));
		}

		// ── Result ──────────────────────────────────────────────────────────
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("before"), BeforeSource);
		Result->SetStringField(TEXT("after"), AfterSource);
		Result->SetStringField(TEXT("actorLabel"), MCPGasSnapString(After, TEXT("actorLabel")));
		Result->SetStringField(TEXT("actorPath"), AfterActor);
		Result->SetBoolField(TEXT("changed"), Changes.Num() > 0);
		Result->SetNumberField(TEXT("changeCount"), Changes.Num());
		Result->SetArrayField(TEXT("changes"), Changes);
		Result->SetArrayField(TEXT("stillActiveEffects"), StillActive);
		Result->SetArrayField(TEXT("warnings"), Warnings);
		Result->SetNumberField(TEXT("timeElapsedSeconds"),
			MCPGasSnapNumber(After, TEXT("worldTimeSeconds")) - MCPGasSnapNumber(Before, TEXT("worldTimeSeconds")));

		TSharedPtr<FJsonObject> Kinds = MakeShared<FJsonObject>();
		for (const auto& KV : ByKind) Kinds->SetNumberField(KV.Key, KV.Value);
		Result->SetObjectField(TEXT("changesByKind"), Kinds);

		TSharedPtr<FJsonObject> Unchanged = MakeShared<FJsonObject>();
		Unchanged->SetNumberField(TEXT("abilities"), AbilitiesUnchanged);
		Unchanged->SetNumberField(TEXT("attributes"), AttributesUnchanged);
		Unchanged->SetNumberField(TEXT("effectsStillActive"), StillActive.Num());
		Result->SetObjectField(TEXT("unchanged"), Unchanged);

		// One readable line, because the common use is a human or an agent glancing
		// at the answer before deciding whether to read the detail.
		if (Changes.Num() == 0)
		{
			Result->SetStringField(TEXT("summary"), TEXT(
				"No change. Same granted abilities, same active effects, same attribute values, same tags."));
		}
		else
		{
			TArray<FString> Parts;
			for (const auto& KV : ByKind) Parts.Add(FString::Printf(TEXT("%d %s"), KV.Value, *KV.Key));
			Parts.Sort();
			Result->SetStringField(TEXT("summary"), FString::Printf(
				TEXT("%d change(s): %s."), Changes.Num(), *FString::Join(Parts, TEXT(", "))));
		}

		return Result;
	}
}

TSharedPtr<FJsonValue> FGasHandlers::CompareGasStates(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	TSharedPtr<FJsonValue> Error;
	FString BeforeSource;
	TSharedPtr<FJsonObject> Before =
		MCPGasSnapResolveSide(Params, TEXT("beforeId"), TEXT("beforeSnapshot"), BeforeSource, Error);
	if (!Before.IsValid())
	{
		if (Error.IsValid()) return Error;
		return MCPError(TEXT(
			"Missing the earlier reading. Pass 'beforeId' (a snapshot id from gas(capture_gas_state)) or "
			"'beforeSnapshot' (the snapshot object itself)."));
	}

	FString AfterSource;
	TSharedPtr<FJsonObject> After =
		MCPGasSnapResolveSide(Params, TEXT("afterId"), TEXT("afterSnapshot"), AfterSource, Error);
	if (!After.IsValid())
	{
		if (Error.IsValid()) return Error;
		// Deliberately NOT "capture the live state now". Capturing registers the
		// actor's attribute set subobjects on its ASC in a world that has not begun
		// play, which is a change to a live component, and this action is
		// classified as a read. A read that quietly mutated the editor would be
		// gated as safe to land in any session. The one-call ergonomic lives on
		// capture_gas_state instead, which is gated as the mutation it is.
		return MCPError(TEXT(
			"Missing the later reading. Pass 'afterId' (a snapshot id) or 'afterSnapshot' (the object). "
			"To capture the live state and diff it in one call use capture_gas_state with compareWith "
			"set to the earlier snapshot id: capturing touches the live ASC, so it belongs on the action "
			"that is gated as a mutation."));
	}

	TSharedPtr<FJsonObject> Diff = MCPGasSnapDiff(Before, After, BeforeSource, AfterSource);
	Diff->SetBoolField(TEXT("success"), true);
	return MCPResult(Diff);
}
