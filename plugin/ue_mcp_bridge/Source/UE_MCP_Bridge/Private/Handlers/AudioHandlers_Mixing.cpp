// Mixing, routing, and spatialization for the audio category.
//
// Submixes + submix effect chains, sound classes, sound mixes, concurrency,
// attenuation, and assigning any of those onto a sound. Plain-UObject assets are
// created via the shared idempotent helper and configured with the reflection
// property setter (HandlerJsonProperty), so arbitrary struct/array fields are
// authorable without a bespoke code path per field.

#include "AudioHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "HandlerJsonProperty.h"
#include "HandlerQuery.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"

#include "Sound/SoundSubmix.h"
#include "Sound/SoundSubmixSend.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundEffectSubmix.h"

namespace
{
	/** Set a (possibly dotted) property from a JSON value, best-effort. */
	bool SetProp(UObject* Obj, const FString& Path, const TSharedPtr<FJsonValue>& Val, FString& OutErr)
	{
		if (!Val.IsValid()) return true;
		return MCPJsonProperty::SetDottedPropertyFromJson(Obj, Path, Val, OutErr);
	}

	void SetNumberProp(UObject* Obj, const FString& Path, const TSharedPtr<FJsonObject>& Params, const FString& Key)
	{
		double N;
		if (Params->TryGetNumberField(Key, N))
		{
			FString E; MCPJsonProperty::SetDottedPropertyFromJson(Obj, Path, MakeShared<FJsonValueNumber>(N), E);
		}
	}

	void SetBoolProp(UObject* Obj, const FString& Path, const TSharedPtr<FJsonObject>& Params, const FString& Key)
	{
		bool B;
		if (Params->TryGetBoolField(Key, B))
		{
			FString E; MCPJsonProperty::SetDottedPropertyFromJson(Obj, Path, MakeShared<FJsonValueBoolean>(B), E);
		}
	}

	USoundBase* LoadSound(const FString& Path)
	{
		return Cast<USoundBase>(UEditorAssetLibrary::LoadAsset(Path));
	}
}

TSharedPtr<FJsonValue> FAudioHandlers::CreateSubmix(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Audio/Submixes"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	auto Created = MCPCreateAssetIdempotent<USoundSubmix>(Name, PackagePath, OnConflict, TEXT("SoundSubmix"), nullptr);
	if (Created.EarlyReturn) return Created.EarlyReturn;
	USoundSubmix* Submix = Created.Asset;

	// Static levels live on the modulation-destination structs in UE5; set .Value.
	SetNumberProp(Submix, TEXT("OutputVolumeModulation.Value"), Params, TEXT("outputVolume"));
	SetNumberProp(Submix, TEXT("WetLevelModulation.Value"), Params, TEXT("wetLevel"));
	SetNumberProp(Submix, TEXT("DryLevelModulation.Value"), Params, TEXT("dryLevel"));

	FString ParentPath;
	if (Params->TryGetStringField(TEXT("parentPath"), ParentPath) && !ParentPath.IsEmpty())
	{
		if (USoundSubmixBase* Parent = Cast<USoundSubmixBase>(UEditorAssetLibrary::LoadAsset(ParentPath)))
		{
			Submix->SetParentSubmix(Parent);
			UEditorAssetLibrary::SaveAsset(ParentPath);
		}
	}

	UEditorAssetLibrary::SaveAsset(Submix->GetPathName());

	auto Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("path"), Submix->GetPathName());
	Res->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Res, Submix->GetPathName());
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::SetSubmixParent(const TSharedPtr<FJsonObject>& Params)
{
	FString SubmixPath;
	if (auto Err = RequireString(Params, TEXT("submixPath"), SubmixPath)) return Err;

	USoundSubmix* Submix = Cast<USoundSubmix>(UEditorAssetLibrary::LoadAsset(SubmixPath));
	if (!Submix) return MCPError(FString::Printf(TEXT("Submix not found: %s"), *SubmixPath));

	const FString ParentPath = OptionalString(Params, TEXT("parentPath"));
	USoundSubmixBase* Parent = nullptr;
	if (!ParentPath.IsEmpty())
	{
		Parent = Cast<USoundSubmixBase>(UEditorAssetLibrary::LoadAsset(ParentPath));
		if (!Parent) return MCPError(FString::Printf(TEXT("Parent submix not found: %s"), *ParentPath));
	}

	// Read the old parent before the reparent: SetParentSubmix rewrites both
	// ends, so afterwards there is nothing left to read it from.
	const USoundSubmixBase* PreviousParent = Submix->ParentSubmix;
	const FString PreviousParentPath = PreviousParent ? PreviousParent->GetPathName() : FString();

	Submix->SetParentSubmix(Parent);
	UEditorAssetLibrary::SaveAsset(SubmixPath);
	if (Parent) UEditorAssetLibrary::SaveAsset(ParentPath);

	auto Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("submixPath"), SubmixPath);
	Res->SetStringField(TEXT("parentPath"), ParentPath);
	Res->SetStringField(TEXT("previousParentPath"), PreviousParentPath);

	// The same action reparents back. parentPath is optional here and an empty
	// one means "no parent", which is exactly what has to be replayed when the
	// submix was a root before this call.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("submixPath"), SubmixPath);
	Payload->SetStringField(TEXT("parentPath"), PreviousParentPath);
	MCPSetRollback(Res, TEXT("set_submix_parent"), Payload);
	Res->SetBoolField(TEXT("rollbackLossy"), false);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::AddSubmixEffect(const TSharedPtr<FJsonObject>& Params)
{
	FString SubmixPath, EffectType;
	if (auto Err = RequireString(Params, TEXT("submixPath"), SubmixPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("effectType"), EffectType)) return Err;

	USoundSubmix* Submix = Cast<USoundSubmix>(UEditorAssetLibrary::LoadAsset(SubmixPath));
	if (!Submix) return MCPError(FString::Printf(TEXT("Submix not found: %s"), *SubmixPath));

	// Map the friendly effect type to its preset class path.
	const FString T = EffectType.ToLower();
	FString ClassPath;
	if (T == TEXT("reverb"))        ClassPath = TEXT("/Script/AudioMixer.SubmixEffectReverbPreset");
	else if (T == TEXT("eq"))       ClassPath = TEXT("/Script/AudioMixer.SubmixEffectSubmixEQPreset");
	else if (T == TEXT("dynamics")) ClassPath = TEXT("/Script/AudioMixer.SubmixEffectDynamicsProcessorPreset");
	else if (T == TEXT("filter"))   ClassPath = TEXT("/Script/Synthesis.SubmixEffectFilterPreset");
	else if (T == TEXT("delay"))    ClassPath = TEXT("/Script/Synthesis.SubmixEffectDelayPreset");
	else return MCPError(TEXT("effectType must be one of: reverb, eq, dynamics, filter, delay."));

	UClass* PresetClass = FindObject<UClass>(nullptr, *ClassPath);
	if (!PresetClass) return MCPError(FString::Printf(TEXT("Effect preset class unavailable: %s (plugin not loaded?)"), *ClassPath));

	const FString Name = OptionalString(Params, TEXT("name"), FString::Printf(TEXT("%s_%s"), *Submix->GetName(), *EffectType));
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Audio/SubmixEffects"));

	auto Created = MCPCreateAssetIdempotent<USoundEffectSubmixPreset>(Name, PackagePath, TEXT("rename"), TEXT("SubmixEffectPreset"), PresetClass, nullptr);
	if (Created.EarlyReturn)
	{
		// MCPCheckAssetExists has no rename semantics: any onConflict other than
		// "error" answers "an asset of that name is already there" and returns.
		// So this path appended NOTHING to the submix's chain, and left as it
		// stood it came back success:true, existed:true and otherwise identical
		// to a call that worked. Say what actually happened.
		if (TSharedPtr<FJsonObject> Existing = Created.EarlyReturn->AsObject())
		{
			bool bSucceeded = false;
			if (Existing->TryGetBoolField(TEXT("success"), bSucceeded) && bSucceeded)
			{
				Existing->SetBoolField(TEXT("unchanged"), true);
				Existing->SetStringField(TEXT("submixPath"), SubmixPath);
				Existing->SetStringField(TEXT("effectType"), EffectType);
				Existing->SetNumberField(TEXT("chainLength"), Submix->SubmixEffectChain.Num());
				Existing->SetBoolField(TEXT("rollbackPossible"), false);
				Existing->SetStringField(TEXT("note"), FString::Printf(
					TEXT("A SubmixEffectPreset already exists at %s/%s, so nothing was created and nothing was appended to '%s'. ")
					TEXT("Pass a different 'name' to add another effect of this type, or rewrite the chain with audio(set_property) on ")
					TEXT("'SubmixEffectChain' to include the existing preset."), *PackagePath, *Name, *SubmixPath));
				Existing->SetStringField(TEXT("rollbackNote"),
					TEXT("This call changed nothing, so there is nothing to roll back."));
			}
		}
		return Created.EarlyReturn;
	}
	USoundEffectSubmixPreset* Preset = Created.Asset;

	// Apply effect settings, if given, onto the preset's Settings struct.
	const TSharedPtr<FJsonObject>* SettingsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("settings"), SettingsObj) && SettingsObj)
	{
		FString E;
		SetProp(Preset, TEXT("Settings"), MakeShared<FJsonValueObject>(*SettingsObj), E);
	}

	// The chain as it stands before the append, in UE export text. There is no
	// remove_submix_effect action: SubmixEffectChain is a UPROPERTY and
	// set_audio_property rewrites it whole, which is how a chain entry is taken
	// back out (see the note at the top of AudioHandlers_Depth.cpp).
	FString PreviousChainType;
	const TSharedPtr<FJsonValue> PreviousChain =
		MCPQuery::ReadDottedProperty(Submix, TEXT("SubmixEffectChain"), PreviousChainType);

	Submix->SubmixEffectChain.Add(Preset);
	UEditorAssetLibrary::SaveAsset(Preset->GetPathName());
	UEditorAssetLibrary::SaveAsset(SubmixPath);

	auto Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("submixPath"), SubmixPath);
	Res->SetStringField(TEXT("effectType"), EffectType);
	Res->SetStringField(TEXT("presetPath"), Preset->GetPathName());
	Res->SetNumberField(TEXT("chainLength"), Submix->SubmixEffectChain.Num());

	if (PreviousChain.IsValid())
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), SubmixPath);
		Payload->SetStringField(TEXT("propertyName"), TEXT("SubmixEffectChain"));
		Payload->SetField(TEXT("value"), PreviousChain);
		MCPSetRollback(Res, TEXT("set_audio_property"), Payload);
		Res->SetBoolField(TEXT("rollbackLossy"), true);
		Res->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The rollback restores the submix's effect chain to what it held before this call, which takes the entry back out. ")
			TEXT("It does NOT delete the preset asset at '%s'. That asset was created by this call - a preset of that name already ")
			TEXT("existing is the early return above, which appends nothing - so deleting it with asset(delete) after the rollback is ")
			TEXT("always safe and is what fully undoes this call."),
			*Preset->GetPathName()));
	}
	else
	{
		Res->SetBoolField(TEXT("rollbackPossible"), false);
		Res->SetStringField(TEXT("rollbackNote"),
			TEXT("The submix's effect chain could not be read back before the append, so there is no captured state to restore. ")
			TEXT("Rewrite the chain by hand with audio(set_property) on 'SubmixEffectChain'."));
	}
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::CreateSoundClass(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Audio/SoundClasses"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	auto Created = MCPCreateAssetIdempotent<USoundClass>(Name, PackagePath, OnConflict, TEXT("SoundClass"), nullptr);
	if (Created.EarlyReturn) return Created.EarlyReturn;
	USoundClass* SoundClass = Created.Asset;

	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("properties"), PropsObj) && PropsObj)
	{
		FString E;
		SetProp(SoundClass, TEXT("Properties"), MakeShared<FJsonValueObject>(*PropsObj), E);
	}

	FString ParentPath;
	if (Params->TryGetStringField(TEXT("parentPath"), ParentPath) && !ParentPath.IsEmpty())
	{
		if (USoundClass* Parent = Cast<USoundClass>(UEditorAssetLibrary::LoadAsset(ParentPath)))
		{
#if WITH_EDITOR
			SoundClass->SetParentClass(Parent);
#endif
			UEditorAssetLibrary::SaveAsset(ParentPath);
		}
	}

	UEditorAssetLibrary::SaveAsset(SoundClass->GetPathName());

	auto Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("path"), SoundClass->GetPathName());
	Res->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Res, SoundClass->GetPathName());
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::CreateSoundMix(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Audio/SoundMixes"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	auto Created = MCPCreateAssetIdempotent<USoundMix>(Name, PackagePath, OnConflict, TEXT("SoundMix"), nullptr);
	if (Created.EarlyReturn) return Created.EarlyReturn;
	USoundMix* Mix = Created.Asset;

	SetNumberProp(Mix, TEXT("FadeInTime"), Params, TEXT("fadeInTime"));
	SetNumberProp(Mix, TEXT("FadeOutTime"), Params, TEXT("fadeOutTime"));

	const TArray<TSharedPtr<FJsonValue>>* Adjusters = nullptr;
	int32 Added = 0;
	if (Params->TryGetArrayField(TEXT("adjusters"), Adjusters) && Adjusters)
	{
		for (const TSharedPtr<FJsonValue>& Entry : *Adjusters)
		{
			const TSharedPtr<FJsonObject> AObj = Entry->AsObject();
			if (!AObj.IsValid()) continue;
			FString ClassPath;
			if (!AObj->TryGetStringField(TEXT("soundClassPath"), ClassPath)) continue;
			USoundClass* SC = Cast<USoundClass>(UEditorAssetLibrary::LoadAsset(ClassPath));
			if (!SC) continue;

			FSoundClassAdjuster Adj;
			Adj.SoundClassObject = SC;
			double Num;
			Adj.VolumeAdjuster = AObj->TryGetNumberField(TEXT("volumeAdjuster"), Num) ? (float)Num : 1.0f;
			Adj.PitchAdjuster = AObj->TryGetNumberField(TEXT("pitchAdjuster"), Num) ? (float)Num : 1.0f;
			bool B;
			Adj.bApplyToChildren = AObj->TryGetBoolField(TEXT("applyToChildren"), B) ? B : false;
			Mix->SoundClassEffects.Add(Adj);
			Added++;
		}
	}

	UEditorAssetLibrary::SaveAsset(Mix->GetPathName());

	auto Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("path"), Mix->GetPathName());
	Res->SetStringField(TEXT("name"), Name);
	Res->SetNumberField(TEXT("adjusters"), Added);
	MCPSetDeleteAssetRollback(Res, Mix->GetPathName());
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::CreateConcurrency(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Audio/Concurrency"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	auto Created = MCPCreateAssetIdempotent<USoundConcurrency>(Name, PackagePath, OnConflict, TEXT("SoundConcurrency"), nullptr);
	if (Created.EarlyReturn) return Created.EarlyReturn;
	USoundConcurrency* Conc = Created.Asset;

	SetNumberProp(Conc, TEXT("Concurrency.MaxCount"), Params, TEXT("maxCount"));
	SetBoolProp(Conc, TEXT("Concurrency.bLimitToOwner"), Params, TEXT("limitToOwner"));
	SetNumberProp(Conc, TEXT("Concurrency.VolumeScale"), Params, TEXT("volumeScale"));
	FString Rule;
	if (Params->TryGetStringField(TEXT("resolutionRule"), Rule) && !Rule.IsEmpty())
	{
		FString E;
		SetProp(Conc, TEXT("Concurrency.ResolutionRule"), MakeShared<FJsonValueString>(Rule), E);
	}

	UEditorAssetLibrary::SaveAsset(Conc->GetPathName());

	auto Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("path"), Conc->GetPathName());
	Res->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Res, Conc->GetPathName());
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::CreateAttenuation(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Audio/Attenuation"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	auto Created = MCPCreateAssetIdempotent<USoundAttenuation>(Name, PackagePath, OnConflict, TEXT("SoundAttenuation"), nullptr);
	if (Created.EarlyReturn) return Created.EarlyReturn;
	USoundAttenuation* Atten = Created.Asset;

	// Full settings struct, if provided.
	const TSharedPtr<FJsonObject>* SettingsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("settings"), SettingsObj) && SettingsObj)
	{
		FString E;
		SetProp(Atten, TEXT("Attenuation"), MakeShared<FJsonValueObject>(*SettingsObj), E);
	}

	// Convenience shortcuts (override individual fields).
	SetNumberProp(Atten, TEXT("Attenuation.FalloffDistance"), Params, TEXT("falloffDistance"));
	SetBoolProp(Atten, TEXT("Attenuation.bSpatialize"), Params, TEXT("spatialize"));
	SetBoolProp(Atten, TEXT("Attenuation.bEnableOcclusion"), Params, TEXT("enableOcclusion"));
	if (Params->HasField(TEXT("falloffDistance")))
	{
		// A falloff was requested -> ensure volume attenuation is on.
		FString E;
		MCPJsonProperty::SetDottedPropertyFromJson(Atten, TEXT("Attenuation.bAttenuate"), MakeShared<FJsonValueBoolean>(true), E);
	}

	UEditorAssetLibrary::SaveAsset(Atten->GetPathName());

	auto Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("path"), Atten->GetPathName());
	Res->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Res, Atten->GetPathName());
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::SetSoundSubmix(const TSharedPtr<FJsonObject>& Params)
{
	FString SoundPath;
	if (auto Err = RequireStringAlt(Params, TEXT("soundPath"), TEXT("assetPath"), SoundPath)) return Err;
	USoundBase* Sound = LoadSound(SoundPath);
	if (!Sound) return MCPError(FString::Printf(TEXT("Sound not found: %s"), *SoundPath));

	const FString SubmixPath = OptionalString(Params, TEXT("submixPath"));
	USoundSubmixBase* Submix = SubmixPath.IsEmpty() ? nullptr : Cast<USoundSubmixBase>(UEditorAssetLibrary::LoadAsset(SubmixPath));
	if (!SubmixPath.IsEmpty() && !Submix) return MCPError(FString::Printf(TEXT("Submix not found: %s"), *SubmixPath));

	const USoundSubmixBase* PreviousSubmix = Sound->SoundSubmixObject;
	const FString PreviousSubmixPath = PreviousSubmix ? PreviousSubmix->GetPathName() : FString();

	Sound->SoundSubmixObject = Submix;
	UEditorAssetLibrary::SaveAsset(SoundPath);

	auto Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("soundPath"), SoundPath);
	Res->SetStringField(TEXT("submixPath"), SubmixPath);
	Res->SetStringField(TEXT("previousSubmixPath"), PreviousSubmixPath);

	// submixPath is optional on this action and empty clears the assignment, so
	// the same call restores both "it pointed somewhere" and "it pointed nowhere".
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("soundPath"), SoundPath);
	Payload->SetStringField(TEXT("submixPath"), PreviousSubmixPath);
	MCPSetRollback(Res, TEXT("set_sound_submix"), Payload);
	Res->SetBoolField(TEXT("rollbackLossy"), false);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::AddSoundSubmixSend(const TSharedPtr<FJsonObject>& Params)
{
	FString SoundPath, SubmixPath;
	if (auto Err = RequireStringAlt(Params, TEXT("soundPath"), TEXT("assetPath"), SoundPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("submixPath"), SubmixPath)) return Err;

	USoundBase* Sound = LoadSound(SoundPath);
	if (!Sound) return MCPError(FString::Printf(TEXT("Sound not found: %s"), *SoundPath));
	USoundSubmixBase* Submix = Cast<USoundSubmixBase>(UEditorAssetLibrary::LoadAsset(SubmixPath));
	if (!Submix) return MCPError(FString::Printf(TEXT("Submix not found: %s"), *SubmixPath));

	// The send array before the append, in UE export text. This action appends
	// unconditionally and so duplicates on replay; rewriting the array whole is
	// the only removal path, and SoundSubmixSends is a UPROPERTY that
	// set_audio_property writes.
	FString PreviousSendsType;
	const TSharedPtr<FJsonValue> PreviousSends =
		MCPQuery::ReadDottedProperty(Sound, TEXT("SoundSubmixSends"), PreviousSendsType);

	FSoundSubmixSendInfo Send;
	Send.SoundSubmix = Submix;
	Send.SendLevel = (float)OptionalNumber(Params, TEXT("sendLevel"), 1.0);
	Send.SendLevelControlMethod = ESendLevelControlMethod::Manual;
	Sound->SoundSubmixSends.Add(Send);
	UEditorAssetLibrary::SaveAsset(SoundPath);

	auto Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("soundPath"), SoundPath);
	Res->SetStringField(TEXT("submixPath"), SubmixPath);
	Res->SetNumberField(TEXT("sends"), Sound->SoundSubmixSends.Num());

	if (PreviousSends.IsValid())
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), SoundPath);
		Payload->SetStringField(TEXT("propertyName"), TEXT("SoundSubmixSends"));
		Payload->SetField(TEXT("value"), PreviousSends);
		MCPSetRollback(Res, TEXT("set_audio_property"), Payload);
		Res->SetBoolField(TEXT("rollbackLossy"), false);
		Res->SetStringField(TEXT("rollbackNote"),
			TEXT("The rollback rewrites SoundSubmixSends to the exact array this call found, which drops the send it appended. ")
			TEXT("The value travels as UE export text and is read back through the same property, so every field of every send survives."));
	}
	else
	{
		Res->SetBoolField(TEXT("rollbackPossible"), false);
		Res->SetStringField(TEXT("rollbackNote"),
			TEXT("The sound's send array could not be read back before the append, so there is no captured state to restore. ")
			TEXT("Rewrite it by hand with audio(set_property) on 'SoundSubmixSends'."));
	}
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::SetSoundClass(const TSharedPtr<FJsonObject>& Params)
{
	FString SoundPath, ClassPath;
	if (auto Err = RequireStringAlt(Params, TEXT("soundPath"), TEXT("assetPath"), SoundPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("soundClassPath"), ClassPath)) return Err;

	USoundBase* Sound = LoadSound(SoundPath);
	if (!Sound) return MCPError(FString::Printf(TEXT("Sound not found: %s"), *SoundPath));
	USoundClass* SC = Cast<USoundClass>(UEditorAssetLibrary::LoadAsset(ClassPath));
	if (!SC) return MCPError(FString::Printf(TEXT("SoundClass not found: %s"), *ClassPath));

	const USoundClass* PreviousClass = Sound->SoundClassObject;
	const FString PreviousClassPath = PreviousClass ? PreviousClass->GetPathName() : FString();

	Sound->SoundClassObject = SC;
	UEditorAssetLibrary::SaveAsset(SoundPath);

	auto Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("soundPath"), SoundPath);
	Res->SetStringField(TEXT("soundClassPath"), ClassPath);
	Res->SetStringField(TEXT("previousSoundClassPath"), PreviousClassPath);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	if (!PreviousClassPath.IsEmpty())
	{
		Payload->SetStringField(TEXT("soundPath"), SoundPath);
		Payload->SetStringField(TEXT("soundClassPath"), PreviousClassPath);
		MCPSetRollback(Res, TEXT("set_sound_class"), Payload);
	}
	else
	{
		// set_sound_class REQUIRES a non-empty soundClassPath, so it cannot put
		// the field back to unset. The property write can, and SoundClassObject
		// is a UPROPERTY object reference that a JSON null clears.
		Payload->SetStringField(TEXT("assetPath"), SoundPath);
		Payload->SetStringField(TEXT("propertyName"), TEXT("SoundClassObject"));
		Payload->SetField(TEXT("value"), MakeShared<FJsonValueNull>());
		MCPSetRollback(Res, TEXT("set_audio_property"), Payload);
	}
	Res->SetBoolField(TEXT("rollbackLossy"), false);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::SetSoundAttenuation(const TSharedPtr<FJsonObject>& Params)
{
	FString SoundPath;
	if (auto Err = RequireStringAlt(Params, TEXT("soundPath"), TEXT("assetPath"), SoundPath)) return Err;
	USoundBase* Sound = LoadSound(SoundPath);
	if (!Sound) return MCPError(FString::Printf(TEXT("Sound not found: %s"), *SoundPath));

	const FString AttenPath = OptionalString(Params, TEXT("attenuationPath"));
	USoundAttenuation* Atten = AttenPath.IsEmpty() ? nullptr : Cast<USoundAttenuation>(UEditorAssetLibrary::LoadAsset(AttenPath));
	if (!AttenPath.IsEmpty() && !Atten) return MCPError(FString::Printf(TEXT("Attenuation not found: %s"), *AttenPath));

	const USoundAttenuation* PreviousAtten = Sound->AttenuationSettings;
	const FString PreviousAttenPath = PreviousAtten ? PreviousAtten->GetPathName() : FString();

	Sound->AttenuationSettings = Atten;
	UEditorAssetLibrary::SaveAsset(SoundPath);

	auto Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("soundPath"), SoundPath);
	Res->SetStringField(TEXT("attenuationPath"), AttenPath);
	Res->SetStringField(TEXT("previousAttenuationPath"), PreviousAttenPath);

	// attenuationPath is optional and empty clears it, so one call restores
	// either previous state.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("soundPath"), SoundPath);
	Payload->SetStringField(TEXT("attenuationPath"), PreviousAttenPath);
	MCPSetRollback(Res, TEXT("set_sound_attenuation"), Payload);
	Res->SetBoolField(TEXT("rollbackLossy"), false);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::SetSoundConcurrency(const TSharedPtr<FJsonObject>& Params)
{
	FString SoundPath;
	if (auto Err = RequireStringAlt(Params, TEXT("soundPath"), TEXT("assetPath"), SoundPath)) return Err;
	USoundBase* Sound = LoadSound(SoundPath);
	if (!Sound) return MCPError(FString::Printf(TEXT("Sound not found: %s"), *SoundPath));

	const FString ConcPath = OptionalString(Params, TEXT("concurrencyPath"));

	// This action replaces the whole set with at most one entry, so the previous
	// set is what has to be captured, not just its first member. A set that held
	// more than one cannot be put back by one call.
	TArray<FString> PreviousConcurrencyPaths;
	for (const TObjectPtr<USoundConcurrency>& Existing : Sound->ConcurrencySet)
	{
		if (Existing) PreviousConcurrencyPaths.Add(Existing->GetPathName());
	}
	PreviousConcurrencyPaths.Sort();

	Sound->ConcurrencySet.Empty();
	if (!ConcPath.IsEmpty())
	{
		USoundConcurrency* Conc = Cast<USoundConcurrency>(UEditorAssetLibrary::LoadAsset(ConcPath));
		if (!Conc) return MCPError(FString::Printf(TEXT("Concurrency not found: %s"), *ConcPath));
		Sound->ConcurrencySet.Add(Conc);
	}
	UEditorAssetLibrary::SaveAsset(SoundPath);

	auto Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("soundPath"), SoundPath);
	Res->SetStringField(TEXT("concurrencyPath"), ConcPath);
	{
		TArray<TSharedPtr<FJsonValue>> PreviousArr;
		for (const FString& P : PreviousConcurrencyPaths) PreviousArr.Add(MakeShared<FJsonValueString>(P));
		Res->SetArrayField(TEXT("previousConcurrencyPaths"), PreviousArr);
	}

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("soundPath"), SoundPath);
	Payload->SetStringField(TEXT("concurrencyPath"),
		PreviousConcurrencyPaths.Num() > 0 ? PreviousConcurrencyPaths[0] : FString());
	MCPSetRollback(Res, TEXT("set_sound_concurrency"), Payload);
	Res->SetBoolField(TEXT("rollbackLossy"), PreviousConcurrencyPaths.Num() > 1);
	if (PreviousConcurrencyPaths.Num() > 1)
	{
		Res->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The sound held %d concurrency assets and set_sound_concurrency assigns at most one, so the rollback restores only '%s'. ")
			TEXT("previousConcurrencyPaths lists them all; rewrite the whole set with audio(set_property) on 'ConcurrencySet' to get them back."),
			PreviousConcurrencyPaths.Num(), *PreviousConcurrencyPaths[0]));
	}
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FAudioHandlers::SetAudioProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath, PropertyName;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset) return MCPError(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));

	// Read the property before writing it. A scalar comes back typed and an
	// array or struct comes back as UE export text, and the setter accepts both
	// (its ImportText fallback is what handles the export-text form), so the
	// captured value replays through this same action.
	FString PreviousType;
	const TSharedPtr<FJsonValue> PreviousValue =
		MCPQuery::ReadDottedProperty(Asset, PropertyName, PreviousType);

	FString E;
	if (!MCPJsonProperty::SetDottedPropertyFromJson(Asset, PropertyName, Params->TryGetField(TEXT("value")), E))
	{
		return MCPError(FString::Printf(TEXT("Failed to set '%s': %s"), *PropertyName, *E));
	}
	Asset->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetStringField(TEXT("propertyName"), PropertyName);

	if (PreviousValue.IsValid())
	{
		Res->SetField(TEXT("previousValue"), PreviousValue);
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("propertyName"), PropertyName);
		Payload->SetField(TEXT("value"), PreviousValue);
		MCPSetRollback(Res, TEXT("set_audio_property"), Payload);
		Res->SetBoolField(TEXT("rollbackLossy"), false);
	}
	else
	{
		// The setter resolves paths the reader does not (it follows more than
		// nested structs), so a write can land on a property whose previous
		// value was never captured. Say that rather than emit a rollback with
		// nothing behind it.
		Res->SetBoolField(TEXT("rollbackPossible"), false);
		Res->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The previous value of '%s' could not be read back before the write - the dotted path resolves through something other ")
			TEXT("than nested structs - so there is nothing captured to restore. Read the value you want with asset(read_properties) and ")
			TEXT("write it back with audio(set_property)."), *PropertyName));
	}
	return MCPResult(Res);
}
