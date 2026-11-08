TSharedPtr<FJsonValue> FReflectionHandlers::CreateEnum(
// create_enum is asset(create_user_defined_enum) under its reflection name:
// its entries are that action's values.
TSharedPtr<FJsonValue> FReflectionHandlers::CreateEnum(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Forwarded = MakeShared<FJsonObject>();
	FString Name;
	if (TryGetStringParam(Params, TEXT("name"), Name)) Forwarded->SetStringField(TEXT("name"), Name);
	Forwarded->SetStringField(TEXT("packagePath"), OptionalString(Params, TEXT("packagePath"), TEXT("/Game")));
	Forwarded->SetStringField(TEXT("onConflict"), OptionalString(Params, TEXT("onConflict"), TEXT("skip")));
	if (const TSharedPtr<FJsonValue> Entries = TryGetParam(Params, TEXT("entries")))
	{
		Forwarded->SetField(TEXT("values"), Entries);
	}
	return FAssetHandlers::CreateUserDefinedEnum(Forwarded);
}
