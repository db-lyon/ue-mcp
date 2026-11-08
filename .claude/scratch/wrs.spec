
	static int32 ResolvePIEInstance(const UWorld* World)
	{
		if (!GEngine || !World) return INDEX_NONE;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() == World) return Context.PIEInstance;
		}
		return INDEX_NONE;
	}
}
-----
}
=====
	UWorld* World = ResolveWorldFromParams(Params, TEXT("pie"));
-----
	const FMCPRuntimeWorld Live = FMCPRuntimeWorld::ResolveLive(Params);
	UWorld* World = Live.World;
=====
	if (!World || (World->WorldType != EWorldType::PIE && World->WorldType != EWorldType::Game))
	{
		return MCPError(TEXT("No live PIE/Game world. Runtime widget inspection reads a running session only: start PIE, or pass a pieInstance that exists. 'world' accepts pie (the default), game or auto; any other value resolves to the editor world, which is never a valid target here."));
	}
-----
	if (auto Err = Live.Require(TEXT("No live PIE/Game world. Runtime widget inspection reads a running session only: start PIE, or pass a pieInstance that exists. 'world' accepts pie (the default), game or auto; any other value resolves to the editor world, which is never a valid target here."))) return Err;
=====
	const int32 PIEInstance = ResolvePIEInstance(World);
	if (PIEInstance != INDEX_NONE) Result->SetNumberField(TEXT("pieInstance"), PIEInstance);
-----
	if (Live.PIEInstance != INDEX_NONE) Result->SetNumberField(TEXT("pieInstance"), Live.PIEInstance);
