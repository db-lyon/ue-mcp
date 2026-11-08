	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("pie"));
	int32 PIEInstance = INDEX_NONE;
	double RawPIEInstance = 0.0;
	if (TryGetNumberParam(Params, TEXT("pieInstance"), RawPIEInstance))
	{
		PIEInstance = FMath::RoundToInt(RawPIEInstance);
	}

	if (!GEditor)
-----
	const FMCPRuntimeWorld Live = FMCPRuntimeWorld::Resolve(Params, TEXT("pie"));

	if (!GEditor)
=====
	UWorld* PIEWorld = ResolveWorldScope(WorldScope, PIEInstance);
	FMCPActorSelector PieSel;
-----
	if (auto Err = Live.Require(TEXT("PIE is not active. Start a PIE session first."))) return Err;
	UWorld* PIEWorld = Live.World;
	FMCPActorSelector PieSel;
=====
	const FString WorldHint = OptionalString(Params, TEXT("world"), TEXT("auto"));
	int32 PIEInstance = INDEX_NONE;
	double RawPIEInstance = 0.0;
	if (TryGetNumberParam(Params, TEXT("pieInstance"), RawPIEInstance))
	{
		PIEInstance = FMath::RoundToInt(RawPIEInstance);
	}

	if (!GEditor) return MCPError(TEXT("Editor not available"));
-----
	const FMCPRuntimeWorld Where = FMCPRuntimeWorld::Resolve(Params, TEXT("auto"));

	if (!GEditor) return MCPError(TEXT("Editor not available"));
=====
	UWorld* World = ResolveWorldScope(WorldHint, PIEInstance);
	if (!World) World = GetEditorWorld();
	if (!World) return MCPError(TEXT("No world available"));
-----
	if (auto Err = Where.Require(TEXT("No world available"))) return Err;
	UWorld* World = Where.World;
=====
	UWorld* World = GetPIEWorld();
	if (!World)
	{
		return MCPError(TEXT("No PIE/Game world active - start PIE first"));
	}

	AWorldSettings* WS = World->GetWorldSettings();
-----
	const FMCPRuntimeWorld Live = FMCPRuntimeWorld::ResolveLive(Params);
	if (auto Err = Live.Require(TEXT("No PIE/Game world active - start PIE first"))) return Err;
	UWorld* World = Live.World;

	AWorldSettings* WS = World->GetWorldSettings();
=====
	FWorldContext* PieCtx = GEditor->GetPIEWorldContext();
	UWorld* PieWorld = PieCtx ? PieCtx->World() : nullptr;
	if (!PieWorld)
	{
		return MCPError(TEXT("PIE not running - start PIE before resolving the player pawn"));
	}
-----
	const FMCPRuntimeWorld Live = FMCPRuntimeWorld::ResolveLive(Params);
	if (auto Err = Live.Require(TEXT("PIE not running - start PIE before resolving the player pawn"))) return Err;
	UWorld* PieWorld = Live.World;
=====
TSharedPtr<FJsonValue> FEditorHandlers::PieSetPlayerView(const TSharedPtr<FJsonObject>& Params)
{
	FWorldContext* PieCtx = GEditor ? GEditor->GetPIEWorldContext() : nullptr;
	UWorld* World = PieCtx ? PieCtx->World() : nullptr;
	if (!World) return MCPError(TEXT("PIE not running"));
-----
TSharedPtr<FJsonValue> FEditorHandlers::PieSetPlayerView(const TSharedPtr<FJsonObject>& Params)
{
	const FMCPRuntimeWorld Live = FMCPRuntimeWorld::ResolveLive(Params);
	if (auto Err = Live.Require(TEXT("PIE not running"))) return Err;
	UWorld* World = Live.World;
=====
TSharedPtr<FJsonValue> FEditorHandlers::StageGameInput(const TSharedPtr<FJsonObject>& Params)
{
	FWorldContext* PieCtx = GEditor ? GEditor->GetPIEWorldContext() : nullptr;
	UWorld* World = PieCtx ? PieCtx->World() : nullptr;
	if (!World) return MCPError(TEXT("PIE not running"));
-----
TSharedPtr<FJsonValue> FEditorHandlers::StageGameInput(const TSharedPtr<FJsonObject>& Params)
{
	const FMCPRuntimeWorld Live = FMCPRuntimeWorld::ResolveLive(Params);
	if (auto Err = Live.Require(TEXT("PIE not running"))) return Err;
	UWorld* World = Live.World;
