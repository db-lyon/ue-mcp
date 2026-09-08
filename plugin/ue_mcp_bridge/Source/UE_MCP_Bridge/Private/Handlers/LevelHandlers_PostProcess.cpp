// Post-process settings, with their override flags (#950).
//
// FPostProcessSettings is two parallel halves: a value for every setting, and a
// `bOverride_<Name>` bit that decides whether the renderer reads that value at
// all. Writing AutoExposureMinBrightness through the generic property setter
// stores the number and leaves the bit off, so the details panel shows the
// value the caller asked for while the engine keeps using the default. Nothing
// errors, nothing looks wrong, and the setting simply does not apply. That trap
// costs hours every time someone falls into it.
//
// So the setter here owns both halves: for every key it writes, it also enables
// the matching override bit. That auto-enable IS the action; a setter that
// wrote the value and left the flag off would reproduce the bug it exists to
// close. The reader is the other half of the answer: it says which settings a
// volume is actually overriding, without dumping the whole struct as one
// ExportText blob for the caller to parse.
//
// The work is done against the struct's REFLECTED fields, so every setting in
// FPostProcessSettings is covered, on this engine version and the next one. No
// hardcoded exposure list.
//
// Translation-unit partition of FLevelHandlers - registration lives in
// LevelHandlers.cpp::RegisterHandlers.

#include "LevelHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Scene.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	/** The prefix every FPostProcessSettings override bit carries. */
	const TCHAR* const MCPPostProcessOverridePrefix = TEXT("bOverride_");

	/**
	 * One FPostProcessSettings living on an actor or on one of its components,
	 * with everything needed to read it, write it and name it back to the caller.
	 */
	struct FMCPPostProcessTarget
	{
		UObject* Owner = nullptr;
		FStructProperty* StructProp = nullptr;
		void* StructPtr = nullptr;
		FString OwnerKind;      // "actor" or "component"
		FString OwnerName;      // the component's name, or the actor's label
		FString PropertyName;   // "Settings", "PostProcessSettings", ...

		bool IsValid() const { return Owner != nullptr && StructProp != nullptr && StructPtr != nullptr; }
		FString Describe() const { return FString::Printf(TEXT("%s.%s"), *OwnerName, *PropertyName); }
	};

	/** Append every FPostProcessSettings struct property declared on one object. */
	void CollectPostProcessStructs(
		UObject* Object,
		const TCHAR* OwnerKind,
		const FString& OwnerName,
		TArray<FMCPPostProcessTarget>& OutTargets)
	{
		if (!Object) return;
		UScriptStruct* WantedStruct = FPostProcessSettings::StaticStruct();
		for (TFieldIterator<FStructProperty> It(Object->GetClass()); It; ++It)
		{
			FStructProperty* Prop = *It;
			if (!Prop || Prop->Struct != WantedStruct) continue;
			FMCPPostProcessTarget Target;
			Target.Owner = Object;
			Target.StructProp = Prop;
			Target.StructPtr = Prop->ContainerPtrToValuePtr<void>(Object);
			Target.OwnerKind = OwnerKind;
			Target.OwnerName = OwnerName;
			Target.PropertyName = Prop->GetName();
			OutTargets.Add(Target);
		}
	}

	/**
	 * Find the one FPostProcessSettings a request means. A PostProcessVolume has
	 * exactly one and needs no disambiguation; a camera actor carries one per
	 * camera component, so componentName / propertyName narrow it. Ambiguity is
	 * reported by listing the candidates rather than resolved by picking first,
	 * because picking first is how a write lands on the wrong camera.
	 */
	bool ResolvePostProcessTarget(
		AActor* Actor,
		const TSharedPtr<FJsonObject>& Params,
		FMCPPostProcessTarget& OutTarget,
		FString& OutError)
	{
		const FString ComponentName = OptionalString(Params, TEXT("componentName"));
		const FString PropertyName = OptionalString(Params, TEXT("propertyName"));

		TArray<FMCPPostProcessTarget> Targets;
		if (ComponentName.IsEmpty())
		{
			CollectPostProcessStructs(Actor, TEXT("actor"), Actor->GetActorLabel(), Targets);
		}

		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (!Component) continue;
			if (!ComponentName.IsEmpty() && !Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase)) continue;
			CollectPostProcessStructs(Component, TEXT("component"), Component->GetName(), Targets);
		}

		if (!PropertyName.IsEmpty())
		{
			Targets.RemoveAll([&PropertyName](const FMCPPostProcessTarget& T)
			{
				return !T.PropertyName.Equals(PropertyName, ESearchCase::IgnoreCase);
			});
		}

		if (Targets.Num() == 0)
		{
			OutError = FString::Printf(
				TEXT("'%s' (%s) has no FPostProcessSettings%s. PostProcessVolume carries one as 'Settings'; a camera carries one per camera component as 'PostProcessSettings'."),
				*Actor->GetActorLabel(), *Actor->GetClass()->GetName(),
				(ComponentName.IsEmpty() && PropertyName.IsEmpty())
					? TEXT("")
					: TEXT(" matching componentName/propertyName"));
			return false;
		}
		if (Targets.Num() > 1)
		{
			TArray<FString> Names;
			for (const FMCPPostProcessTarget& T : Targets) Names.Add(T.Describe());
			OutError = FString::Printf(
				TEXT("'%s' has %d post-process settings structs; pass componentName and/or propertyName to choose. Candidates: [%s]"),
				*Actor->GetActorLabel(), Targets.Num(), *FString::Join(Names, TEXT(", ")));
			return false;
		}

		OutTarget = Targets[0];
		return true;
	}

	/** Case-insensitive field lookup inside FPostProcessSettings. */
	FProperty* FindPostProcessField(const FString& Name)
	{
		UStruct* Struct = FPostProcessSettings::StaticStruct();
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			FProperty* Prop = *It;
			if (Prop && Prop->GetName().Equals(Name, ESearchCase::IgnoreCase)) return Prop;
		}
		return nullptr;
	}

	/** The `bOverride_<Name>` bit for a value field, or null when it has none. */
	FBoolProperty* FindPostProcessOverrideFlag(const FString& ValueFieldName)
	{
		FProperty* Flag = FindPostProcessField(FString(MCPPostProcessOverridePrefix) + ValueFieldName);
		return CastField<FBoolProperty>(Flag);
	}

	bool IsPostProcessOverrideFlag(const FProperty* Prop)
	{
		return Prop != nullptr && Prop->GetName().StartsWith(MCPPostProcessOverridePrefix);
	}

	// The override bits are one-bit bitfields packed together, so they are read
	// and written through FBoolProperty rather than as bytes.
	bool ReadPostProcessOverrideFlag(const FBoolProperty* Flag, const void* StructPtr)
	{
		if (!Flag || !StructPtr) return false;
		return Flag->GetPropertyValue_InContainer(StructPtr);
	}

	void WritePostProcessOverrideFlag(FBoolProperty* Flag, void* StructPtr, bool bValue)
	{
		if (!Flag || !StructPtr) return;
		Flag->SetPropertyValue_InContainer(StructPtr, bValue);
	}

	/** Nearest field names, for the "no such setting" error. */
	FString SuggestPostProcessFields(const FString& Wanted)
	{
		TArray<FString> Near;
		for (TFieldIterator<FProperty> It(FPostProcessSettings::StaticStruct()); It; ++It)
		{
			FProperty* Prop = *It;
			if (!Prop || IsPostProcessOverrideFlag(Prop)) continue;
			const FString Name = Prop->GetName();
			if (Name.Contains(Wanted, ESearchCase::IgnoreCase) || Wanted.Contains(Name, ESearchCase::IgnoreCase))
			{
				Near.Add(Name);
				if (Near.Num() >= 8) break;
			}
		}
		return Near.Num() > 0 ? FString::Join(Near, TEXT(", ")) : FString(TEXT("(no similar field names)"));
	}

	/** Resolve actorLabel or actorPath to an actor plus its post-process
	 *  struct, or fail. #983: a label naming several actors refuses here
	 *  rather than writing exposure onto one of them at random. */
	bool ResolveActorAndPostProcessTarget(
		UWorld* World,
		const TSharedPtr<FJsonObject>& Params,
		AActor*& OutActor,
		FMCPPostProcessTarget& OutTarget,
		TSharedPtr<FJsonValue>& OutError)
	{
		// Presence check only, so a call with no selector at all fails naming
		// both parameters before the world is touched. The value is discarded:
		// the resolver reads the parameters itself, and every message below
		// names the actor that actually answered.
		FString UnusedSelector;
		if (TSharedPtr<FJsonValue> Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), UnusedSelector))
		{
			OutError = Err;
			return false;
		}
		OutActor = MCPResolveActor(World, Params, OutError);
		if (!OutActor) return false;
		FString ResolveError;
		if (!ResolvePostProcessTarget(OutActor, Params, OutTarget, ResolveError))
		{
			OutError = MCPError(ResolveError);
			return false;
		}
		return true;
	}

	/**
	 * Did any applied write actually move a value?
	 *
	 * ApplyPostProcessSetting records previousValue before its write and
	 * newValue after it, so this is a direct comparison rather than a guess. A
	 * call that wrote the values already there changed nothing and must not
	 * report `updated`, and must not carry a rollback either.
	 */
	bool PostProcessAnyValueChanged(const TArray<TSharedPtr<FJsonValue>>& Applied)
	{
		for (const TSharedPtr<FJsonValue>& Value : Applied)
		{
			const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Entry.IsValid()) continue;
			bool bApplied = false;
			if (!Entry->TryGetBoolField(TEXT("applied"), bApplied) || !bApplied) continue;

			const TSharedPtr<FJsonValue> Previous = Entry->TryGetField(TEXT("previousValue"));
			const TSharedPtr<FJsonValue> Current = Entry->TryGetField(TEXT("newValue"));
			if (!Previous.IsValid() || !Current.IsValid()) return true;
			if (!FJsonValue::CompareEqual(*Previous, *Current)) return true;

			// Turning an override bit on is a change even when the value under
			// it did not move, because the renderer starts honouring it.
			bool bWasEnabled = false;
			bool bIsEnabled = false;
			if (Entry->TryGetBoolField(TEXT("overrideWasEnabled"), bWasEnabled) &&
				Entry->TryGetBoolField(TEXT("overrideEnabled"), bIsEnabled) &&
				bWasEnabled != bIsEnabled)
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * The set_post_process_settings call that puts back what a write changed.
	 *
	 * Every entry ApplyPostProcessSetting produced carries the value that was
	 * there and, where the field has one, whether its override bit was already
	 * on. Both go into the inverse: restoring the value while leaving an
	 * override bit this call switched on would leave the volume overriding a
	 * setting it did not override before. enableOverrides is off in the
	 * inverse so the bits are written only where they are named explicitly.
	 *
	 * Returns false when nothing was applied, which is when there is nothing
	 * to undo.
	 */
	bool BuildPostProcessInverse(
		AActor* Actor,
		const FMCPPostProcessTarget& Target,
		const TArray<TSharedPtr<FJsonValue>>& Applied,
		TSharedPtr<FJsonObject>& OutPayload)
	{
		// Pass one: every field this call WROTE, by resolved name. Each of those
		// carries its own previousValue, read before its own write, and that
		// reading is authoritative.
		//
		// This matters because `overrideWasEnabled` on a value field is sampled
		// AFTER that field's write, and SetPostProcessSettings iterates the
		// caller's settings object in map order. A caller passing both Bloom
		// and bOverride_Bloom in one call can have the bit written first, so
		// Bloom's entry would report the bit as "already enabled" when this
		// same call is what enabled it. Deriving the flag from that reading
		// would then overwrite the flag's own honest previousValue and leave a
		// volume no longer overriding something it used to override.
		TSet<FString> WrittenFieldNames;
		for (const TSharedPtr<FJsonValue>& Value : Applied)
		{
			const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Entry.IsValid()) continue;
			bool bApplied = false;
			if (!Entry->TryGetBoolField(TEXT("applied"), bApplied) || !bApplied) continue;
			FString ResolvedName;
			if (Entry->TryGetStringField(TEXT("resolvedName"), ResolvedName) && !ResolvedName.IsEmpty())
			{
				WrittenFieldNames.Add(ResolvedName);
			}
		}

		TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
		for (const TSharedPtr<FJsonValue>& Value : Applied)
		{
			const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Entry.IsValid()) continue;
			bool bApplied = false;
			if (!Entry->TryGetBoolField(TEXT("applied"), bApplied) || !bApplied) continue;

			FString ResolvedName;
			if (!Entry->TryGetStringField(TEXT("resolvedName"), ResolvedName) || ResolvedName.IsEmpty()) continue;
			const TSharedPtr<FJsonValue> Previous = Entry->TryGetField(TEXT("previousValue"));
			if (!Previous.IsValid()) continue;
			Settings->SetField(ResolvedName, Previous);

			FString OverrideFlag;
			bool bOverrideWasEnabled = false;
			if (Entry->TryGetStringField(TEXT("overrideFlag"), OverrideFlag) && !OverrideFlag.IsEmpty() &&
				Entry->TryGetBoolField(TEXT("overrideWasEnabled"), bOverrideWasEnabled))
			{
				// Only derive the flag when this call did not write it itself.
				// When it did, that entry's own previousValue is already in
				// Settings and is the reading taken before anything changed.
				if (!WrittenFieldNames.Contains(OverrideFlag))
				{
					Settings->SetBoolField(OverrideFlag, bOverrideWasEnabled);
				}
			}
		}
		if (Settings->Values.Num() == 0) return false;

		OutPayload = MakeShared<FJsonObject>();
		OutPayload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		OutPayload->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		// Re-resolve the same struct: componentName only when the settings live
		// on a component, because passing it makes the lookup component-only.
		if (Target.OwnerKind == TEXT("component"))
		{
			OutPayload->SetStringField(TEXT("componentName"), Target.OwnerName);
		}
		OutPayload->SetStringField(TEXT("propertyName"), Target.PropertyName);
		OutPayload->SetObjectField(TEXT("settings"), Settings);
		OutPayload->SetBoolField(TEXT("enableOverrides"), false);
		return true;
	}

	/** Describe the resolved struct on a response, so a caller sees what it hit. */
	void EmitPostProcessTargetFields(const TSharedPtr<FJsonObject>& Result, AActor* Actor, const FMCPPostProcessTarget& Target)
	{
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		Result->SetStringField(TEXT("actorClass"), Actor->GetClass()->GetName());
		Result->SetStringField(TEXT("settingsOwner"), Target.OwnerKind);
		Result->SetStringField(TEXT("settingsOwnerName"), Target.OwnerName);
		Result->SetStringField(TEXT("settingsProperty"), Target.PropertyName);
	}

	/**
	 * Write one setting and turn its override bit on. Returns the per-key report
	 * either way, so a partial refusal is legible rather than silent.
	 */
	TSharedPtr<FJsonObject> ApplyPostProcessSetting(
		const FMCPPostProcessTarget& Target,
		const FString& Key,
		const TSharedPtr<FJsonValue>& Value,
		bool bEnableOverride,
		bool& bOutOk)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Key);
		bOutOk = false;

		FProperty* Prop = FindPostProcessField(Key);
		if (!Prop)
		{
			Entry->SetBoolField(TEXT("applied"), false);
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("FPostProcessSettings has no field '%s'. Similar: %s"), *Key, *SuggestPostProcessFields(Key)));
			return Entry;
		}

		Entry->SetStringField(TEXT("resolvedName"), Prop->GetName());
		Entry->SetField(TEXT("previousValue"), MCPExportPropertyValue(Prop, Target.StructPtr));

		void* ValueAddr = Prop->ContainerPtrToValuePtr<void>(Target.StructPtr);
		FString SetError;
		if (!MCPJsonProperty::SetJsonOnProperty(Prop, ValueAddr, Value, SetError))
		{
			Entry->SetBoolField(TEXT("applied"), false);
			Entry->SetStringField(TEXT("error"), SetError);
			return Entry;
		}
		Entry->SetBoolField(TEXT("applied"), true);
		Entry->SetField(TEXT("newValue"), MCPExportPropertyValue(Prop, Target.StructPtr));
		bOutOk = true;

		// The whole point: the value the caller just wrote is invisible to the
		// renderer until this bit is on. A key that IS an override bit is its own
		// flag and needs nothing further.
		if (IsPostProcessOverrideFlag(Prop))
		{
			Entry->SetBoolField(TEXT("isOverrideFlag"), true);
			return Entry;
		}

		FBoolProperty* Flag = FindPostProcessOverrideFlag(Prop->GetName());
		if (!Flag)
		{
			// A handful of fields (the blendable array, for one) have no override
			// bit at all. Say so rather than implying one was enabled.
			Entry->SetBoolField(TEXT("hasOverrideFlag"), false);
			return Entry;
		}
		Entry->SetBoolField(TEXT("hasOverrideFlag"), true);
		Entry->SetStringField(TEXT("overrideFlag"), Flag->GetName());
		Entry->SetBoolField(TEXT("overrideWasEnabled"), ReadPostProcessOverrideFlag(Flag, Target.StructPtr));
		if (bEnableOverride)
		{
			WritePostProcessOverrideFlag(Flag, Target.StructPtr, true);
		}
		// Read back rather than echo: the response says what the struct holds now.
		Entry->SetBoolField(TEXT("overrideEnabled"), ReadPostProcessOverrideFlag(Flag, Target.StructPtr));
		return Entry;
	}
}


// level(set_post_process_settings): write settings AND enable their override
// bits, which is the difference between a value that applies and a value that
// sits in the details panel while the engine ignores it.
TSharedPtr<FJsonValue> FLevelHandlers::SetPostProcessSettings(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	AActor* Actor = nullptr;
	FMCPPostProcessTarget Target;
	TSharedPtr<FJsonValue> ResolveError;
	if (!ResolveActorAndPostProcessTarget(World, Params, Actor, Target, ResolveError)) return ResolveError;

	const TSharedPtr<FJsonObject>* SettingsObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("settings"), SettingsObj) || !SettingsObj || !SettingsObj->IsValid())
	{
		return MCPError(TEXT("Missing 'settings' object of settingName -> value, e.g. {\"AutoExposureMinBrightness\": 1.0, \"AutoExposureMaxBrightness\": 1.0}"));
	}

	// Resolve every key before writing any of them, so a typo fails the call
	// instead of half-applying it and leaving the volume in a state nobody
	// asked for.
	TArray<TPair<FString, TSharedPtr<FJsonValue>>> Requested;
	TArray<FString> Unknown;
	for (const auto& Pair : (*SettingsObj)->Values)
	{
		// FJsonObject::Values is keyed by a shared string on 5.8, so the key is
		// materialised rather than bound as an FString. Same idiom as the rest
		// of the module.
		const FString Key = FString(*Pair.Key);
		if (!FindPostProcessField(Key)) Unknown.Add(Key);
		Requested.Add(TPair<FString, TSharedPtr<FJsonValue>>(Key, Pair.Value));
	}
	if (Requested.Num() == 0)
	{
		return MCPError(TEXT("'settings' is empty; pass at least one settingName -> value pair"));
	}
	if (Unknown.Num() > 0)
	{
		return MCPError(FString::Printf(
			TEXT("FPostProcessSettings has no field named %s. Nothing was written. Use level(get_post_process_settings) to list the exact names."),
			*FString::Join(Unknown, TEXT(", "))));
	}

	const bool bEnableOverrides = OptionalBool(Params, TEXT("enableOverrides"), true);

	Target.Owner->Modify();
	TArray<TSharedPtr<FJsonValue>> Applied;
	int32 SuccessCount = 0;
	int32 FailureCount = 0;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Requested)
	{
		bool bOk = false;
		Applied.Add(MakeShared<FJsonValueObject>(
			ApplyPostProcessSetting(Target, Pair.Key, Pair.Value, bEnableOverrides, bOk)));
		bOk ? ++SuccessCount : ++FailureCount;
	}
	Target.Owner->PostEditChange();
	Actor->MarkPackageDirty();

	const bool bAnyChanged = PostProcessAnyValueChanged(Applied);

	auto Result = MCPSuccess();
	if (bAnyChanged) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);
	Result->SetBoolField(TEXT("unchanged"), !bAnyChanged);
	EmitPostProcessTargetFields(Result, Actor, Target);
	Result->SetBoolField(TEXT("enableOverrides"), bEnableOverrides);
	Result->SetNumberField(TEXT("appliedCount"), SuccessCount);
	Result->SetNumberField(TEXT("failedCount"), FailureCount);
	Result->SetArrayField(TEXT("settings"), Applied);
	if (FailureCount > 0) Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("note"),
		TEXT("Each written setting also had its bOverride_<Name> flag enabled, which is what makes the value take effect. The level is left dirty and is NOT saved."));

	// Gated on whether a write LANDED, not on bAnyChanged. That comparison is
	// export text against export text, so two different floats that print the
	// same read as equal; suppressing the inverse on it would leave a struct
	// that was written, PostEditChange-d and marked dirty with no way back.
	// The marker above is allowed to be approximate. The rollback is not.
	TSharedPtr<FJsonObject> Payload;
	if (SuccessCount > 0 && BuildPostProcessInverse(Actor, Target, Applied, Payload))
	{
		MCPSetRollback(Result, TEXT("set_post_process_settings"), Payload);
	}
	return MCPResult(Result);
}


// level(get_post_process_settings): what this volume is actually overriding.
// onlyOverridden answers "is exposure being overridden here" without dumping
// FPostProcessSettings as one ExportText blob for the caller to parse.
TSharedPtr<FJsonValue> FLevelHandlers::GetPostProcessSettings(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	AActor* Actor = nullptr;
	FMCPPostProcessTarget Target;
	TSharedPtr<FJsonValue> ResolveError;
	if (!ResolveActorAndPostProcessTarget(World, Params, Actor, Target, ResolveError)) return ResolveError;

	const bool bOnlyOverridden = OptionalBool(Params, TEXT("onlyOverridden"), false);
	const FString NameFilter = OptionalString(Params, TEXT("nameContains"));

	TArray<FString> WantedNames;
	const TArray<TSharedPtr<FJsonValue>>* NamesArr = nullptr;
	if (Params->TryGetArrayField(TEXT("names"), NamesArr) && NamesArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *NamesArr)
		{
			FString Name;
			if (V.IsValid() && V->TryGetString(Name) && !Name.IsEmpty()) WantedNames.Add(Name);
		}
	}

	TArray<TSharedPtr<FJsonValue>> Rows;
	int32 TotalFields = 0;
	int32 OverriddenCount = 0;
	for (TFieldIterator<FProperty> It(FPostProcessSettings::StaticStruct()); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop) continue;
		// The override bits are reported as the `overridden` flag on the value
		// they gate, not as settings of their own. Listing both would double the
		// payload and invite a caller to write the bit without the value.
		if (IsPostProcessOverrideFlag(Prop)) continue;

		const FString Name = Prop->GetName();
		++TotalFields;

		FBoolProperty* Flag = FindPostProcessOverrideFlag(Name);
		const bool bOverridden = Flag != nullptr && ReadPostProcessOverrideFlag(Flag, Target.StructPtr);
		if (bOverridden) ++OverriddenCount;

		if (bOnlyOverridden && !bOverridden) continue;
		if (!NameFilter.IsEmpty() && !Name.Contains(NameFilter, ESearchCase::IgnoreCase)) continue;
		if (WantedNames.Num() > 0)
		{
			const bool bWanted = WantedNames.ContainsByPredicate([&Name](const FString& Candidate)
			{
				return Candidate.Equals(Name, ESearchCase::IgnoreCase);
			});
			if (!bWanted) continue;
		}

		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("name"), Name);
		Row->SetStringField(TEXT("type"), Prop->GetCPPType());
		Row->SetField(TEXT("value"), MCPExportPropertyValue(Prop, Target.StructPtr));
		Row->SetBoolField(TEXT("hasOverrideFlag"), Flag != nullptr);
		if (Flag)
		{
			Row->SetStringField(TEXT("overrideFlag"), Flag->GetName());
		}
		Row->SetBoolField(TEXT("overridden"), bOverridden);
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}

	auto Result = MCPSuccess();
	EmitPostProcessTargetFields(Result, Actor, Target);
	Result->SetBoolField(TEXT("onlyOverridden"), bOnlyOverridden);
	Result->SetNumberField(TEXT("totalSettings"), TotalFields);
	Result->SetNumberField(TEXT("overriddenCount"), OverriddenCount);
	Result->SetNumberField(TEXT("returnedCount"), Rows.Num());
	Result->SetArrayField(TEXT("settings"), Rows);
	Result->SetStringField(TEXT("note"),
		TEXT("overridden is the bOverride_<Name> bit: a value with overridden=false is ignored by the renderer no matter what it reads."));
	return MCPResult(Result);
}


// level(set_fixed_exposure): the common case of #950, done correctly in one
// call. Eye adaptation is disabled when the min and max adaptation brightnesses
// are equal, and both are ignored until their override bits are on, so the
// two-value-plus-two-bit write is what "fixed exposure" actually means.
TSharedPtr<FJsonValue> FLevelHandlers::SetFixedExposure(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	AActor* Actor = nullptr;
	FMCPPostProcessTarget Target;
	TSharedPtr<FJsonValue> ResolveError;
	if (!ResolveActorAndPostProcessTarget(World, Params, Actor, Target, ResolveError)) return ResolveError;

	if (!Params->HasField(TEXT("exposure")) && !Params->HasField(TEXT("brightness")))
	{
		return MCPError(TEXT("Missing 'exposure' (the fixed adaptation brightness written to both AutoExposureMinBrightness and AutoExposureMaxBrightness)"));
	}
	const double Exposure = Params->HasField(TEXT("exposure"))
		? OptionalNumber(Params, TEXT("exposure"), 1.0)
		: OptionalNumber(Params, TEXT("brightness"), 1.0);

	TArray<TPair<FString, TSharedPtr<FJsonValue>>> Writes;
	Writes.Add(TPair<FString, TSharedPtr<FJsonValue>>(
		TEXT("AutoExposureMinBrightness"), MakeShared<FJsonValueNumber>(Exposure)));
	Writes.Add(TPair<FString, TSharedPtr<FJsonValue>>(
		TEXT("AutoExposureMaxBrightness"), MakeShared<FJsonValueNumber>(Exposure)));
	if (Params->HasField(TEXT("bias")))
	{
		Writes.Add(TPair<FString, TSharedPtr<FJsonValue>>(
			TEXT("AutoExposureBias"), MakeShared<FJsonValueNumber>(OptionalNumber(Params, TEXT("bias"), 0.0))));
	}

	Target.Owner->Modify();
	TArray<TSharedPtr<FJsonValue>> Applied;
	int32 FailureCount = 0;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Write : Writes)
	{
		bool bOk = false;
		Applied.Add(MakeShared<FJsonValueObject>(
			ApplyPostProcessSetting(Target, Write.Key, Write.Value, /*bEnableOverride*/ true, bOk)));
		if (!bOk) ++FailureCount;
	}
	Target.Owner->PostEditChange();
	Actor->MarkPackageDirty();

	const bool bAnyChanged = PostProcessAnyValueChanged(Applied);

	auto Result = MCPSuccess();
	if (bAnyChanged) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);
	Result->SetBoolField(TEXT("unchanged"), !bAnyChanged);
	EmitPostProcessTargetFields(Result, Actor, Target);
	Result->SetNumberField(TEXT("exposure"), Exposure);
	Result->SetNumberField(TEXT("failedCount"), FailureCount);
	Result->SetArrayField(TEXT("settings"), Applied);
	if (FailureCount > 0) Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("note"),
		TEXT("Min and max adaptation brightness are set to the same value, which is how the engine disables eye adaptation, and both override flags are enabled so the values apply. The level is left dirty and is NOT saved."));

	// This is a shorthand over the same struct writes, so its inverse is the
	// general setter restoring the exposure fields it touched. Gated on a write
	// having landed rather than on bAnyChanged, for the reason given in
	// SetPostProcessSettings: the change test is export text and cannot be
	// trusted to prove nothing moved.
	TSharedPtr<FJsonObject> Payload;
	if ((Applied.Num() - FailureCount) > 0 && BuildPostProcessInverse(Actor, Target, Applied, Payload))
	{
		MCPSetRollback(Result, TEXT("set_post_process_settings"), Payload);
	}
	return MCPResult(Result);
}
