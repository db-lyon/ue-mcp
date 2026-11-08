TSharedPtr<FJsonValue> FLevelHandlers::PlaceActor(
TSharedPtr<FJsonValue> FLevelHandlers::PlaceActor(const TSharedPtr<FJsonObject>& Params)
{
	return PlaceActorOfClass(Params, /*bLabelCheck=*/true);
}
=====
TSharedPtr<FJsonValue> FLevelHandlers::SpawnActorUnchecked(
// internal_spawn_actor: place_actor without the label check. The demo flows
// spawn under labels a replay has already used, which place_actor skips.
TSharedPtr<FJsonValue> FLevelHandlers::SpawnActorUnchecked(const TSharedPtr<FJsonObject>& Params)
{
	return PlaceActorOfClass(Params, /*bLabelCheck=*/false);
}
=====
AActor* FLevelHandlers::SpawnForPlacement(
// Spawn actorClass through F6 with place_actor's mesh and material shorthands.
// The shorthand assets are loaded before anything spawns, and one that cannot
// apply to the class is a refusal, not a silent skip.
TSharedPtr<FJsonValue> FLevelHandlers::PlaceActorOfClass(const TSharedPtr<FJsonObject>& Params, bool bLabelCheck)
{
	FString ActorClass;
	if (auto Err = RequireString(Params, TEXT("actorClass"), ActorClass)) return Err;
	const FString StaticMeshPath = OptionalString(Params, TEXT("staticMesh"));
	const FString MaterialPath = OptionalString(Params, TEXT("material"));
	FMCPSpawn Spawn(Params, { TEXT("Actor"), bLabelCheck });

	UClass* Class = MCPResolveClassOfType(ActorClass, AActor::StaticClass());
	if (!Class) Class = LoadObject<UClass>(nullptr, *ActorClass);
	if (!Class || !Class->IsChildOf(AActor::StaticClass()))
	{
		return MCPError(FString::Printf(TEXT("Actor class not found: %s"), *ActorClass));
	}
	UStaticMesh* Mesh = nullptr;
	if (!StaticMeshPath.IsEmpty())
	{
		Mesh = LoadAssetByPath<UStaticMesh>(StaticMeshPath);
		if (!Mesh) return MCPAssetLoadError(StaticMeshPath, TEXT("StaticMesh"));
		if (!Class->IsChildOf(AStaticMeshActor::StaticClass()))
		{
			return MCPError(FString::Printf(TEXT("staticMesh applies to a StaticMeshActor, and %s is not one."), *Class->GetName()));
		}
	}
	UMaterialInterface* Material = nullptr;
	if (!MaterialPath.IsEmpty())
	{
		Material = LoadAssetByPath<UMaterialInterface>(MaterialPath);
		if (!Material) return MCPAssetLoadError(MaterialPath, TEXT("MaterialInterface"));
	}

	if (auto Early = Spawn.Open()) return Early;
	AActor* NewActor = Spawn.SpawnAt(Class, FTransform(Spawn.RequestedRotation(), Spawn.RequestedLocation()));
	if (!NewActor) return Spawn.Failed();

	// #1119: SpawnActor does not run the editor's volume factory, so a native
	// volume has no brush, bounds or collision. Keep a brush the class supplies;
	// give a missing or polygonless one the native 200 cm cube. The builder can
	// snap location and resets scale, so the transform is restored after it.
	AVolume* BuiltVolume = nullptr;
	if (AVolume* Volume = Cast<AVolume>(NewActor);
		Volume && (!Volume->Brush || !Volume->Brush->Polys || Volume->Brush->Polys->Element.Num() == 0))
	{
		const FTransform VolumeTransform = Volume->GetActorTransform();
		UEMCP::BuildVolumeAsCube(Spawn.World(), Volume, FVector(100.0));
		Volume->SetActorTransform(VolumeTransform);
		BuiltVolume = Volume;
	}

	// A bare Actor has no root, so its transform and attachments would have nothing to hold.
	if (!NewActor->GetRootComponent())
	{
		MCPWorldSpawn::AddInstanceComponent(NewActor, USceneComponent::StaticClass(), TEXT("DefaultSceneRoot"));
		NewActor->SetActorTransform(FTransform(Spawn.RequestedRotation(), Spawn.RequestedLocation()));
	}
	Spawn.ApplyScale();

	// Nav bounds are captured when registered, so re-register them against the final transform.
	if (AVolume* PlacedVolume = Cast<AVolume>(NewActor))
	{
		if (BuiltVolume) BuiltVolume->PostEditChange();
		UEMCP::NotifyVolumeBoundsChanged(PlacedVolume);
	}

	if (Mesh)
	{
		if (UStaticMeshComponent* MeshComp = CastChecked<AStaticMeshActor>(NewActor)->GetStaticMeshComponent())
		{
			MeshComp->SetStaticMesh(Mesh);
		}
	}
	if (Material)
	{
		UPrimitiveComponent* PrimComp = NewActor->FindComponentByClass<UPrimitiveComponent>();
		if (!PrimComp)
		{
			return Spawn.Abandon(MCPError(FString::Printf(TEXT("material was given, but a %s has no primitive component to take it."), *Class->GetName())));
		}
		PrimComp->SetMaterial(0, Material);
	}

	return MCPResult(Spawn.Finish());
}
