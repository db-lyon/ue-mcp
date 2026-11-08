	Registry.RegisterHandler(TEXT("spawn_ambient_sound"), &SpawnAmbientSound, {
		MCPParam::Required(TEXT("soundPath"), EType::String, TEXT("Sound asset the AmbientSound plays")).Alias(TEXT("assetPath")).Alias(TEXT("path")),
		MCPParam::Optional(TEXT("location"), EType::Vec3, TEXT("World location of the actor (default origin)")),
		MCPParam::Optional(TEXT("label"), EType::String, TEXT("Actor label")),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("skip | error | rename when an actor with that label exists (default skip)")),
		MCPParam::Optional(TEXT("volumeMultiplier"), EType::Number, TEXT("Volume multiplier on the audio component (default 1)")).Alias(TEXT("volume")),
	});
-----
	Registry.RegisterHandler(TEXT("spawn_ambient_sound"), &SpawnAmbientSound, FMCPSpawn::ParamsWith({
		MCPParam::Required(TEXT("soundPath"), EType::String, TEXT("Sound asset the AmbientSound plays")).Alias(TEXT("assetPath")).Alias(TEXT("path")),
		MCPParam::Optional(TEXT("volumeMultiplier"), EType::Number, TEXT("Volume multiplier on the audio component (default 1)")).Alias(TEXT("volume")),
	}, TEXT("ambient sound")));
