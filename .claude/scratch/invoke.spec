	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor")).ToLower();
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World)
	{
		return MCPError(WorldScope == TEXT("pie")
			? TEXT("PIE not running (or no such pieInstance) - cannot invoke against PIE world. See editor(list_pie_instances).")
			: TEXT("No editor world available"));
	}
-----
	const FMCPRuntimeWorld Where = FMCPRuntimeWorld::Resolve(Params, TEXT("editor"));
	if (auto Err = Where.Require(Where.Scope.Equals(TEXT("editor"), ESearchCase::IgnoreCase)
			? TEXT("No editor world available")
			: TEXT("PIE not running - cannot invoke against PIE world. See editor(list_pie_instances).")))
	{
		return Err;
	}
	UWorld* World = Where.World;
=====
	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor")).ToLower();
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World)
	{
		return MCPError(WorldScope == TEXT("pie") || WorldScope == TEXT("game")
			? TEXT("PIE not running (or no such pieInstance) - cannot invoke against a PIE world. See editor(list_pie_instances).")
			: TEXT("No editor world available"));
	}
-----
	const FMCPRuntimeWorld Where = FMCPRuntimeWorld::Resolve(Params, TEXT("editor"));
	if (auto Err = Where.Require(Where.Scope.Equals(TEXT("editor"), ESearchCase::IgnoreCase)
			? TEXT("No editor world available")
			: TEXT("PIE not running - cannot invoke against a PIE world. See editor(list_pie_instances).")))
	{
		return Err;
	}
	UWorld* World = Where.World;
