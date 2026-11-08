TSharedPtr<FJsonValue> FAudioHandlers::CreateSoundCue(
TSharedPtr<FJsonValue> FAudioHandlers::CreateSoundCue(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("SoundCue"), TEXT("/Game/Audio/SoundCues") });
	if (auto Err = Create.Read()) return Err;
	USoundCue* Cue = nullptr;
	if (auto Early = Create.Make(Cue, USoundCue::StaticClass(), NewObject<USoundCueFactoryNew>())) return Early;
	return MCPResult(Create.Finish());
}
