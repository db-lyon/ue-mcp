TSharedPtr<FJsonValue> FBlueprintHandlers::CreateBlueprintInterface(
TSharedPtr<FJsonValue> FBlueprintHandlers::CreateBlueprintInterface(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("Interface"), TEXT("/Game/Blueprints"), /*bOnConflict=*/true, /*bAssetPath=*/true });
	if (auto Err = Create.Read()) return Err;

	UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
	Factory->BlueprintType = BPTYPE_Interface;
	TSharedPtr<FJsonValue> Early;
	const TSharedPtr<FJsonObject> Result = MCPCreatePreset::BlueprintSubclass(
		Create, UInterface::StaticClass(), Early, [](UBlueprint*) {}, Factory);
	return Result.IsValid() ? MCPResult(Result) : Early;
}
