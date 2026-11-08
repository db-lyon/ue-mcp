TSharedPtr<FJsonValue> FLevelHandlers::SpawnLight(
TSharedPtr<FJsonValue> FLevelHandlers::SpawnLight(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, { TEXT("intensity"), TEXT("color"), TEXT("mobility"), TEXT("attenuationRadius") });
	FString LightType;
	if (auto Err = RequireString(Params, TEXT("lightType"), LightType)) return Err;
	FMCPSpawn Spawn(Params, { TEXT("Light") });

	// lightType is the class preset over place_actor.
	UClass* LightClass = nullptr;
	if (LightType.Equals(TEXT("point"), ESearchCase::IgnoreCase)) LightClass = APointLight::StaticClass();
	else if (LightType.Equals(TEXT("spot"), ESearchCase::IgnoreCase)) LightClass = ASpotLight::StaticClass();
	else if (LightType.Equals(TEXT("directional"), ESearchCase::IgnoreCase)) LightClass = ADirectionalLight::StaticClass();
	else if (LightType.Equals(TEXT("rect"), ESearchCase::IgnoreCase)) LightClass = ARectLight::StaticClass();
	else if (LightType.Equals(TEXT("sky"), ESearchCase::IgnoreCase) || LightType.Equals(TEXT("skylight"), ESearchCase::IgnoreCase)) LightClass = ASkyLight::StaticClass();
	else
	{
		return MCPError(FString::Printf(TEXT("Unknown light type: %s. Use point, spot, directional, rect, or sky."), *LightType));
	}

	if (auto Early = Spawn.Open()) return Early;
	AActor* NewLight = Spawn.Spawn(LightClass);
	if (!NewLight) return Spawn.Failed();

	const double Intensity = OptionalNumber(Params, TEXT("intensity"), 5000.0);
	// Colour in 0-255 per channel, the shape set_light_properties reads.
	const TSharedPtr<FJsonObject>* ColorObj = nullptr;
	const bool bHasColor = TryGetObjectParam(Params, TEXT("color"), ColorObj) && ColorObj && (*ColorObj).IsValid();
	const FLinearColor LightColor = bHasColor ? MCPLightColorFrom255(*ColorObj) : FLinearColor::White;

	// Mobility defaults to Movable so the light renders without a lighting
	// build (#310). A SkyLight ignores it.
	const FString MobilityStr = OptionalString(Params, TEXT("mobility"), TEXT("Movable"));
	EComponentMobility::Type Mobility = EComponentMobility::Movable;
	if (MobilityStr.Equals(TEXT("Static"), ESearchCase::IgnoreCase)) Mobility = EComponentMobility::Static;
	else if (MobilityStr.Equals(TEXT("Stationary"), ESearchCase::IgnoreCase)) Mobility = EComponentMobility::Stationary;

	if (ULightComponent* LightComponent = NewLight->FindComponentByClass<ULightComponent>())
	{
		LightComponent->SetMobility(Mobility);
		LightComponent->SetIntensity(Intensity);
		if (bHasColor) LightComponent->SetLightColor(LightColor);
		// #723: attenuationRadius lives on the local lights (point, spot, rect).
		double AttenuationRadius = 0.0;
		if (TryGetNumberParam(Params, TEXT("attenuationRadius"), AttenuationRadius) && AttenuationRadius > 0.0)
		{
			if (UPointLightComponent* PointComp = Cast<UPointLightComponent>(LightComponent))
			{
				PointComp->SetAttenuationRadius(static_cast<float>(AttenuationRadius));
			}
			else if (URectLightComponent* RectComp = Cast<URectLightComponent>(LightComponent))
			{
				RectComp->SetAttenuationRadius(static_cast<float>(AttenuationRadius));
			}
		}
		LightComponent->SetVisibility(true);
		LightComponent->MarkRenderStateDirty();
	}
	else if (USkyLightComponent* SkyComp = NewLight->FindComponentByClass<USkyLightComponent>())
	{
		// SkyLight has no ULightComponent: its own component, then a recapture.
		SkyComp->SetIntensity(Intensity);
		if (bHasColor) SkyComp->SetLightColor(LightColor);
		SkyComp->SetVisibility(true);
		SkyComp->RecaptureSky();
	}

	TSharedPtr<FJsonObject> Result = Spawn.Finish();
	Result->SetStringField(TEXT("lightType"), LightType);
	return MCPResult(Result);
}
