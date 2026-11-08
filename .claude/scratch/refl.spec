TSharedPtr<FJsonValue> FReflectionHandlers::CreateEnum(
// create_enum is asset(create_user_defined_enum) under its reflection name:
// its entries are that action's values.
TSharedPtr<FJsonValue> FReflectionHandlers::CreateEnum(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, { TEXT("name"), TEXT("packagePath"), TEXT("onConflict"), TEXT("entries") });
	TSharedPtr<FJsonObject> Forwarded = MakeShared<FJsonObject>();
	Forwarded->Values = Params->Values;
	if (const TSharedPtr<FJsonValue> Entries = Params->TryGetField(TEXT("entries")))
	{
		Forwarded->RemoveField(TEXT("entries"));
		Forwarded->SetField(TEXT("values"), Entries);
	}
	return FAssetHandlers::CreateUserDefinedEnum(Forwarded);
}
