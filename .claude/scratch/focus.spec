	if (!GetPIEWorld())
	{
		return MCPError(
			TEXT("No PIE or Game world is running. Focus is only meaningful while the game is playing - ")
			TEXT("start PIE with editor(play_in_editor) first. The editor's own focus is not reported ")
			TEXT("because it is the editor UI, not the game's."));
	}
-----
	if (auto Err = FMCPRuntimeWorld::ResolveLive(Params).Require(
			TEXT("No PIE or Game world is running. Focus is only meaningful while the game is playing - ")
			TEXT("start PIE with editor(play_in_editor) first. The editor's own focus is not reported ")
			TEXT("because it is the editor UI, not the game's.")))
	{
		return Err;
	}
=====
	UWorld* World = GetPIEWorld();
	if (!World)
	{
		return MCPError(
			TEXT("No PIE or Game world is running. Focus can only be set on a live widget - start PIE with ")
			TEXT("editor(play_in_editor), then add the widget with widget(add_to_viewport)."));
	}
-----
	const FMCPRuntimeWorld Live = FMCPRuntimeWorld::ResolveLive(Params);
	if (auto Err = Live.Require(
			TEXT("No PIE or Game world is running. Focus can only be set on a live widget - start PIE with ")
			TEXT("editor(play_in_editor), then add the widget with widget(add_to_viewport).")))
	{
		return Err;
	}
	UWorld* World = Live.World;
