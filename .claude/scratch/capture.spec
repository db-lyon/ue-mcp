			FActorSpawnParameters SpawnParams;
			SpawnParams.ObjectFlags |= RF_Transient;
#if WITH_EDITOR
			SpawnParams.bTemporaryEditorActor = true;
			SpawnParams.bHideFromSceneOutliner = true;
			SpawnParams.bCreateActorPackage = false;
#endif
			Actor
-----
			const FActorSpawnParameters SpawnParams = MCPTransientSpawnParameters();
			Actor
