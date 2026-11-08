TSharedPtr<FJsonValue> FGasHandlers::CreateGasBlueprint(
=====
TSharedPtr<FJsonValue> FGasHandlers::CreateGameplayEffect(
TSharedPtr<FJsonValue> FGasHandlers::CreateGameplayEffect(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("GameplayEffect"), TEXT("/Game/GAS/Effects") });
	if (auto Err = Create.Read()) return Err;

	const FString DurationPolicy = OptionalString(Params, TEXT("durationPolicy"), TEXT("Instant"));
	EGameplayEffectDurationType Policy = EGameplayEffectDurationType::Instant;
	if (DurationPolicy.Equals(TEXT("Instant"), ESearchCase::IgnoreCase)) Policy = EGameplayEffectDurationType::Instant;
	else if (DurationPolicy.Equals(TEXT("HasDuration"), ESearchCase::IgnoreCase)) Policy = EGameplayEffectDurationType::HasDuration;
	else if (DurationPolicy.Equals(TEXT("Infinite"), ESearchCase::IgnoreCase)) Policy = EGameplayEffectDurationType::Infinite;
	else
	{
		return MCPError(FString::Printf(
			TEXT("Unknown durationPolicy '%s'. Use Instant, HasDuration or Infinite."), *DurationPolicy));
	}

	TSharedPtr<FJsonValue> Early;
	const TSharedPtr<FJsonObject> Result = MCPCreatePreset::BlueprintSubclass(Create, UGameplayEffect::StaticClass(), Early,
		[Policy](UBlueprint* Blueprint)
		{
			UGameplayEffect* Defaults = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject<UGameplayEffect>() : nullptr;
			if (!Defaults) return;
			Defaults->Modify();
			Defaults->DurationPolicy = Policy;
		});
	if (!Result.IsValid()) return Early;
	Result->SetStringField(TEXT("durationPolicy"), DurationPolicy);
	return MCPResult(Result);
}
=====
TSharedPtr<FJsonValue> FGasHandlers::CreateGameplayAbility(
TSharedPtr<FJsonValue> FGasHandlers::CreateGameplayAbility(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("GameplayAbility"), TEXT("/Game/GAS/Abilities") });
	if (auto Err = Create.Read()) return Err;
	return MCPCreatePreset::BlueprintSubclass(Create, UGameplayAbility::StaticClass());
}
=====
TSharedPtr<FJsonValue> FGasHandlers::CreateAttributeSet(
TSharedPtr<FJsonValue> FGasHandlers::CreateAttributeSet(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("AttributeSet"), TEXT("/Game/GAS/Attributes") });
	if (auto Err = Create.Read()) return Err;
	return MCPCreatePreset::BlueprintSubclass(Create, UAttributeSet::StaticClass());
}
=====
TSharedPtr<FJsonValue> FGasHandlers::CreateGameplayCue(
TSharedPtr<FJsonValue> FGasHandlers::CreateGameplayCue(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("GameplayCue"), TEXT("/Game/GAS/Cues") });
	if (auto Err = Create.Read()) return Err;
	const FString CueType = OptionalString(Params, TEXT("cueType"), TEXT("Static"));
	UClass* Parent = CueType == TEXT("Actor")
		? AGameplayCueNotify_Actor::StaticClass()
		: UGameplayCueNotify_Static::StaticClass();

	TSharedPtr<FJsonValue> Early;
	const TSharedPtr<FJsonObject> Result = MCPCreatePreset::BlueprintSubclass(Create, Parent, Early, [](UBlueprint*) {});
	if (!Result.IsValid()) return Early;
	Result->SetStringField(TEXT("cueType"), CueType);
	return MCPResult(Result);
}
