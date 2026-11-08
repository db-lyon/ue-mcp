TSharedPtr<FJsonValue> FAudioHandlers::CreateSubmix(
TSharedPtr<FJsonValue> FAudioHandlers::CreateSubmix(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("SoundSubmix"), TEXT("/Game/Audio/Submixes") });
	if (auto Err = Create.Read()) return Err;

	// A parent that does not load is refused before anything is created.
	const FString ParentPath = OptionalString(Params, TEXT("parentPath"));
	USoundSubmixBase* Parent = nullptr;
	if (!ParentPath.IsEmpty())
	{
		Parent = LoadAssetByPath<USoundSubmixBase>(ParentPath);
		if (!Parent) return MCPAssetLoadError(ParentPath, TEXT("SoundSubmixBase"));
	}

	USoundSubmix* Submix = nullptr;
	if (auto Early = Create.Make(Submix)) return Early;

	// Static levels live on the modulation-destination structs in UE5; set .Value.
	SetNumberProp(Submix, TEXT("OutputVolumeModulation.Value"), Params, TEXT("outputVolume"));
	SetNumberProp(Submix, TEXT("WetLevelModulation.Value"), Params, TEXT("wetLevel"));
	SetNumberProp(Submix, TEXT("DryLevelModulation.Value"), Params, TEXT("dryLevel"));
	if (Parent) Submix->SetParentSubmix(Parent);

	UObject* const AlsoSave[] = { Parent };
	TSharedPtr<FJsonObject> Res = Create.Finish(/*bSave=*/true, AlsoSave);
	if (Parent) Res->SetStringField(TEXT("parentPath"), Parent->GetPathName());
	return MCPResult(Res);
}
=====
TSharedPtr<FJsonValue> FAudioHandlers::CreateSoundClass(
TSharedPtr<FJsonValue> FAudioHandlers::CreateSoundClass(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("SoundClass"), TEXT("/Game/Audio/SoundClasses") });
	if (auto Err = Create.Read()) return Err;

	// A parent that does not load is refused before anything is created.
	const FString ParentPath = OptionalString(Params, TEXT("parentPath"));
	USoundClass* Parent = nullptr;
	if (!ParentPath.IsEmpty())
	{
		Parent = LoadAssetByPath<USoundClass>(ParentPath);
		if (!Parent) return MCPAssetLoadError(ParentPath, TEXT("SoundClass"));
	}

	USoundClass* SoundClass = nullptr;
	if (auto Early = Create.Make(SoundClass)) return Early;

	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (TryGetObjectParam(Params, TEXT("properties"), PropsObj) && PropsObj)
	{
		FString E;
		SetProp(SoundClass, TEXT("Properties"), MakeShared<FJsonValueObject>(*PropsObj), E);
	}
#if WITH_EDITOR
	if (Parent) SoundClass->SetParentClass(Parent);
#endif

	UObject* const AlsoSave[] = { Parent };
	TSharedPtr<FJsonObject> Res = Create.Finish(/*bSave=*/true, AlsoSave);
	if (Parent) Res->SetStringField(TEXT("parentPath"), Parent->GetPathName());
	return MCPResult(Res);
}
=====
TSharedPtr<FJsonValue> FAudioHandlers::CreateSoundMix(
TSharedPtr<FJsonValue> FAudioHandlers::CreateSoundMix(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("SoundMix"), TEXT("/Game/Audio/SoundMixes") });
	if (auto Err = Create.Read()) return Err;

	USoundMix* Mix = nullptr;
	if (auto Early = Create.Make(Mix)) return Early;

	SetNumberProp(Mix, TEXT("FadeInTime"), Params, TEXT("fadeInTime"));
	SetNumberProp(Mix, TEXT("FadeOutTime"), Params, TEXT("fadeOutTime"));

	const TArray<TSharedPtr<FJsonValue>>* Adjusters = nullptr;
	int32 Added = 0;
	if (TryGetArrayParam(Params, TEXT("adjusters"), Adjusters) && Adjusters)
	{
		for (const TSharedPtr<FJsonValue>& Entry : *Adjusters)
		{
			const TSharedPtr<FJsonObject> AObj = Entry->AsObject();
			if (!AObj.IsValid()) continue;
			FString ClassPath;
			if (!AObj->TryGetStringField(TEXT("soundClassPath"), ClassPath)) continue;
			USoundClass* SC = LoadAssetByPath<USoundClass>(ClassPath);
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

	TSharedPtr<FJsonObject> Res = Create.Finish();
	Res->SetNumberField(TEXT("adjusters"), Added);
	return MCPResult(Res);
}
=====
TSharedPtr<FJsonValue> FAudioHandlers::CreateConcurrency(
TSharedPtr<FJsonValue> FAudioHandlers::CreateConcurrency(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("SoundConcurrency"), TEXT("/Game/Audio/Concurrency") });
	if (auto Err = Create.Read()) return Err;

	USoundConcurrency* Conc = nullptr;
	if (auto Early = Create.Make(Conc)) return Early;

	SetNumberProp(Conc, TEXT("Concurrency.MaxCount"), Params, TEXT("maxCount"));
	SetBoolProp(Conc, TEXT("Concurrency.bLimitToOwner"), Params, TEXT("limitToOwner"));
	SetNumberProp(Conc, TEXT("Concurrency.VolumeScale"), Params, TEXT("volumeScale"));
	FString Rule;
	if (TryGetStringParam(Params, TEXT("resolutionRule"), Rule) && !Rule.IsEmpty())
	{
		FString E;
		SetProp(Conc, TEXT("Concurrency.ResolutionRule"), MakeShared<FJsonValueString>(Rule), E);
	}
	return MCPResult(Create.Finish());
}
=====
TSharedPtr<FJsonValue> FAudioHandlers::CreateAttenuation(
TSharedPtr<FJsonValue> FAudioHandlers::CreateAttenuation(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("SoundAttenuation"), TEXT("/Game/Audio/Attenuation") });
	if (auto Err = Create.Read()) return Err;

	USoundAttenuation* Atten = nullptr;
	if (auto Early = Create.Make(Atten)) return Early;

	// Full settings struct, if provided.
	const TSharedPtr<FJsonObject>* SettingsObj = nullptr;
	if (TryGetObjectParam(Params, TEXT("settings"), SettingsObj) && SettingsObj)
	{
		FString E;
		SetProp(Atten, TEXT("Attenuation"), MakeShared<FJsonValueObject>(*SettingsObj), E);
	}

	// Convenience shortcuts (override individual fields).
	SetNumberProp(Atten, TEXT("Attenuation.FalloffDistance"), Params, TEXT("falloffDistance"));
	SetBoolProp(Atten, TEXT("Attenuation.bSpatialize"), Params, TEXT("spatialize"));
	SetBoolProp(Atten, TEXT("Attenuation.bEnableOcclusion"), Params, TEXT("enableOcclusion"));
	if (HasParam(Params, TEXT("falloffDistance")))
	{
		// A falloff was requested -> ensure volume attenuation is on.
		FString E;
		MCPJsonProperty::SetDottedPropertyFromJson(Atten, TEXT("Attenuation.bAttenuate"), MakeShared<FJsonValueBoolean>(true), E);
	}
	return MCPResult(Create.Finish());
}
