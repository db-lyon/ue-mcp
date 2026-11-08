TSharedPtr<FJsonValue> FGameplayHandlers::SpawnNavModifierVolume(
// spawn_nav_modifier_volume is level(spawn_volume) of a NavModifierVolume with
// an areaClass. The default AreaClass (UNavArea_Null) cuts a navmesh hole; the
// brush the shared volume spawn builds is what makes it bound anything.
TSharedPtr<FJsonValue> FGameplayHandlers::SpawnNavModifierVolume(const TSharedPtr<FJsonObject>& Params)
{
	// areaClass is checked before anything spawns.
	const FString AreaClassPath = OptionalString(Params, TEXT("areaClass"));
	UClass* AreaClass = nullptr;
	if (!AreaClassPath.IsEmpty())
	{
		AreaClass = MCPResolveClassOfType(AreaClassPath, UNavArea::StaticClass());
		if (!AreaClass)
		{
			return MCPError(FString::Printf(
				TEXT("areaClass '%s' is not a UNavArea subclass (try NavArea_Null, NavArea_Obstacle, or a /Script/... path)."),
				*AreaClassPath));
		}
	}

	const TSharedPtr<FJsonValue> Response = UEMCP::SpawnVolume(Params, ANavModifierVolume::StaticClass(), FVector(100.0), TEXT("NavModifierVolume"),
		[AreaClass](AActor* Actor) -> TSharedPtr<FJsonValue>
		{
			if (AreaClass) CastChecked<ANavModifierVolume>(Actor)->SetAreaClass(AreaClass);
			return nullptr;
		});
	const TSharedPtr<FJsonObject> Obj = Response.IsValid() && Response->Type == EJson::Object ? Response->AsObject() : nullptr;
	FString Path;
	bool bCreated = false;
	if (Obj.IsValid() && Obj->TryGetBoolField(TEXT("created"), bCreated) && bCreated && Obj->TryGetStringField(TEXT("actorPath"), Path))
	{
		if (const ANavModifierVolume* Volume = FindObject<ANavModifierVolume>(nullptr, *Path))
		{
			Obj->SetStringField(TEXT("areaClass"), Volume->GetAreaClass() ? Volume->GetAreaClass()->GetName() : TEXT("(default)"));
		}
	}
	return Response;
}
