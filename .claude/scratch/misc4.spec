TSharedPtr<FJsonValue> FLandscapeHandlers::CreateLandscapeLayerInfo(
TSharedPtr<FJsonValue> FLandscapeHandlers::CreateLandscapeLayerInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString LayerName;
	if (auto Err = RequireString(Params, TEXT("layerName"), LayerName)) return Err;
	const FString Name = OptionalString(Params, TEXT("name"), FString::Printf(TEXT("LI_%s"), *LayerName));
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Landscape/LayerInfos"));
	const FString PhysMaterialPath = OptionalString(Params, TEXT("physMaterial"));
	FMCPCreate Create(Params, { TEXT("LandscapeLayerInfoObject"), TEXT("/Game/Landscape/LayerInfos") });
	Create.Target(Name, PackagePath);

	// physMaterial is resolved before anything is created. PhysicsCore is not a
	// hard dependency of this module, so the class is reached through the
	// PhysMaterial property rather than a link-time include.
	UObject* PhysMat = nullptr;
	FObjectProperty* PhysProp = nullptr;
	if (!PhysMaterialPath.IsEmpty())
	{
		PhysMat = LoadAssetByPath<UObject>(PhysMaterialPath);
		if (!PhysMat) return MCPError(FString::Printf(TEXT("physMaterial not found: %s"), *PhysMaterialPath));
		PhysProp = CastField<FObjectProperty>(ULandscapeLayerInfoObject::StaticClass()->FindPropertyByName(TEXT("PhysMaterial")));
		if (!PhysProp || !PhysProp->PropertyClass || !PhysMat->IsA(PhysProp->PropertyClass))
		{
			return MCPError(FString::Printf(
				TEXT("'%s' is a %s, not a PhysicalMaterial."),
				*PhysMaterialPath, *PhysMat->GetClass()->GetName()));
		}
	}

	ULandscapeLayerInfoObject* LayerInfo = nullptr;
	if (auto Early = Create.MakeNewObject(LayerInfo)) return Early;

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	LayerInfo->LayerName = FName(*LayerName);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	if (PhysProp) PhysProp->SetObjectPropertyValue_InContainer(LayerInfo, PhysMat);

	double Hardness = 0.0;
	if (TryGetNumberParam(Params, TEXT("hardness"), Hardness))
	{
#if UE_MCP_HAS_5_5_API
		// Hardness is becoming private; the setter also handles Modify() and the
		// property-change notification the direct write skipped.
		LayerInfo->SetHardness(static_cast<float>(Hardness), /*bInModify=*/true, EPropertyChangeType::ValueSet);
#else
		// 5.4 has no setter: Hardness is a public member, so the Modify() and
		// the change notification are done here instead of by the engine.
		LayerInfo->Modify();
		LayerInfo->Hardness = static_cast<float>(Hardness);
		{
			FProperty* HardnessProperty = FindFProperty<FProperty>(
				ULandscapeLayerInfoObject::StaticClass(), TEXT("Hardness"));
			FPropertyChangedEvent ChangeEvent(HardnessProperty, EPropertyChangeType::ValueSet);
			LayerInfo->PostEditChangeProperty(ChangeEvent);
		}
#endif
	}

	TSharedPtr<FJsonObject> Result = Create.Finish();
	Result->SetStringField(TEXT("layerName"), LayerName);
	if (PhysMat) Result->SetStringField(TEXT("physMaterial"), PhysMat->GetPathName());
	return MCPResult(Result);
}
