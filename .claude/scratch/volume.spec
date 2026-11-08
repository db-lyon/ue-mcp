TSharedPtr<FJsonValue> FLevelHandlers::SpawnVolume(
TSharedPtr<FJsonValue> FLevelHandlers::SpawnVolume(const TSharedPtr<FJsonObject>& Params)
{
	FString VolumeType;
	if (auto Err = RequireString(Params, TEXT("volumeType"), VolumeType)) return Err;

	// volumeType is the class preset: a short alias, or any class name.
	struct FAlias { const TCHAR* Long; const TCHAR* Short; UClass* Class; };
	const FAlias Aliases[] = {
		{ TEXT("BlockingVolume"), TEXT("blocking"), ABlockingVolume::StaticClass() },
		{ TEXT("TriggerVolume"), TEXT("trigger"), ATriggerVolume::StaticClass() },
		{ TEXT("PostProcessVolume"), TEXT("postprocess"), APostProcessVolume::StaticClass() },
		{ TEXT("AudioVolume"), TEXT("audio"), AAudioVolume::StaticClass() },
		{ TEXT("LightmassImportanceVolume"), TEXT("lightmass"), ALightmassImportanceVolume::StaticClass() },
		{ TEXT("CullDistanceVolume"), TEXT("culldistance"), MCPResolveClass(TEXT("CullDistanceVolume")) },
		{ TEXT("NavMeshBoundsVolume"), TEXT("navmesh"), ANavMeshBoundsVolume::StaticClass() },
		{ TEXT("PainCausingVolume"), TEXT("pain"), APainCausingVolume::StaticClass() },
		{ TEXT("PCGVolume"), TEXT("pcg"), APCGVolume::StaticClass() },
	};
	UClass* VolumeClass = nullptr;
	for (const FAlias& Alias : Aliases)
	{
		if (VolumeType.Equals(Alias.Long, ESearchCase::IgnoreCase) || VolumeType.Equals(Alias.Short, ESearchCase::IgnoreCase))
		{
			VolumeClass = Alias.Class;
			break;
		}
	}
	if (!VolumeClass) VolumeClass = MCPResolveClass(VolumeType);
	if (!VolumeClass || !VolumeClass->IsChildOf(AActor::StaticClass()))
	{
		return MCPError(FString::Printf(TEXT("Volume class not found: %s"), *VolumeType));
	}

	const TSharedPtr<FJsonValue> Response = UEMCP::SpawnVolume(Params, VolumeClass, FVector(100.0), TEXT("Volume"),
		[](AActor*) -> TSharedPtr<FJsonValue> { return nullptr; });
	if (const TSharedPtr<FJsonObject> Obj = Response.IsValid() && Response->Type == EJson::Object ? Response->AsObject() : nullptr)
	{
		bool bCreated = false;
		if (Obj->TryGetBoolField(TEXT("created"), bCreated) && bCreated) Obj->SetStringField(TEXT("volumeType"), VolumeType);
	}
	return Response;
}
