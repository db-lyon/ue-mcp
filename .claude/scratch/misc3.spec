TSharedPtr<FJsonValue> FSequencerHandlers::CreateLevelSequence(
TSharedPtr<FJsonValue> FSequencerHandlers::CreateLevelSequence(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("LevelSequence"), TEXT("/Game/Cinematics") });
	if (auto Err = Create.Read()) return Err;
	ULevelSequence* NewSequence = nullptr;
	if (auto Early = Create.MakeNewObject(NewSequence)) return Early;
	NewSequence->Initialize();
	return MCPResult(Create.Finish());
}
