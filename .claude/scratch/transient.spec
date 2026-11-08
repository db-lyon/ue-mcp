	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// The transiency contract, stated three ways so no single engine path can
	// quietly persist this actor.
	SpawnParameters.ObjectFlags |= RF_Transient;
#if WITH_EDITOR
	SpawnParameters.bTemporaryEditorActor = true;
	SpawnParameters.bHideFromSceneOutliner = bHideFromOutliner;
	SpawnParameters.bCreateActorPackage = false;
#endif
-----
	// The transiency contract, stated three ways so no single engine path can
	// quietly persist this actor.
	const FActorSpawnParameters SpawnParameters = MCPTransientSpawnParameters(bHideFromOutliner);
