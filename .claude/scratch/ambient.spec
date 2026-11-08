TSharedPtr<FJsonValue> FAudioHandlers::SpawnAmbientSound(
// spawn_ambient_sound is place_actor of an AmbientSound playing soundPath.
TSharedPtr<FJsonValue> FAudioHandlers::SpawnAmbientSound(const TSharedPtr<FJsonObject>& Params)
{
	// The sound is loaded before anything is spawned: an AmbientSound with no sound plays nothing (#1057).
	FString SoundPath;
	if (auto Err = RequireString(Params, TEXT("soundPath"), SoundPath)) return Err;
	double Volume = 1.0;
	const bool bHasVolume = TryGetNumberParam(Params, TEXT("volumeMultiplier"), Volume);
	FMCPSpawn Spawn(Params, { TEXT("AmbientSound") });

	USoundBase* Sound = LoadAssetByPath<USoundBase>(SoundPath);
	if (!Sound)
	{
		return MCPAssetLoadError(SoundPath, TEXT("SoundBase"));
	}

	if (auto Early = Spawn.Open()) return Early;
	AAmbientSound* AmbientSoundActor = Spawn.Spawn<AAmbientSound>(AAmbientSound::StaticClass());
	if (!AmbientSoundActor) return Spawn.Failed();
	if (UAudioComponent* AudioComp = AmbientSoundActor->GetAudioComponent())
	{
		AudioComp->SetSound(Sound);
		if (bHasVolume) AudioComp->VolumeMultiplier = static_cast<float>(Volume);
	}

	TSharedPtr<FJsonObject> Result = Spawn.Finish();
	Result->SetStringField(TEXT("soundPath"), Sound->GetPathName());
	return MCPResult(Result);
}
