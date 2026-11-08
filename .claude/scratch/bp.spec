TSharedPtr<FJsonValue> FBlueprintHandlers::CreateBlueprint(
TSharedPtr<FJsonValue> FBlueprintHandlers::CreateBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	// The destination is assetPath ('path' is its alias) or a name plus packagePath pair.
	FMCPCreate Create(Params, { TEXT("Blueprint"), TEXT("/Game/Blueprints"), /*bOnConflict=*/true, /*bAssetPath=*/true });
	const FString ParentClassName = OptionalString(Params, TEXT("parentClass"), TEXT("Actor"));
	if (auto Err = Create.Read()) return Err;

	UClass* ParentClass = MCPResolveClass(ParentClassName);
	if (!ParentClass)
	{
		ParentClass = LoadObject<UClass>(nullptr, *ParentClassName);
	}
	if (!ParentClass)
	{
		return MCPError(FString::Printf(
			TEXT("Parent class not found: '%s'. Try the full path (e.g. '/Script/Engine.Actor') or the class name without prefix (e.g. 'Actor', 'Pawn', 'Character')."),
			*ParentClassName));
	}
	return MCPCreatePreset::BlueprintSubclass(Create, ParentClass);
}
