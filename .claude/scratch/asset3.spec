TSharedPtr<FJsonValue> FAssetHandlers::CreateUserDefinedEnum(
TSharedPtr<FJsonValue> FAssetHandlers::CreateUserDefinedEnum(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("UserDefinedEnum"), TEXT("/Game") });
	if (auto Err = Create.Read()) return Err;

	auto Describe = [](const TSharedPtr<FJsonObject>& Out, UUserDefinedEnum* Enum)
	{
		Out->SetNumberField(TEXT("count"), CountRealEnumerators(Enum));
		TArray<TSharedPtr<FJsonValue>> ValueList;
		for (int32 i = 0; i < Enum->NumEnums(); ++i)
		{
			if (!IsEnumMaxSentinel(Enum, i)) ValueList.Add(MakeShared<FJsonValueObject>(EnumeratorToJson(Enum, i)));
		}
		Out->SetArrayField(TEXT("values"), ValueList);
	};
	UUserDefinedEnum* UDE = nullptr;
	if (auto Early = Create.Construct<UUserDefinedEnum>(UDE, [](UPackage* Package, FName Name, EObjectFlags Flags)
		{
			return Cast<UUserDefinedEnum>(FEnumEditorUtils::CreateUserDefinedEnum(Package, Name, Flags));
		}))
	{
		return FMCPCreate::DescribeExisting<UUserDefinedEnum>(Early, Describe);
	}

	// CreateUserDefinedEnum starts empty (only the trailing _MAX). Populate it.
	TArray<FString> DisplayNames;
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (TryGetArrayParam(Params, TEXT("values"), Values) && Values)
	{
		for (const TSharedPtr<FJsonValue>& V : *Values)
		{
			FString S;
			if (V->TryGetString(S) && !S.IsEmpty()) DisplayNames.Add(S);
		}
	}
	if (DisplayNames.Num() == 0) DisplayNames.Add(TEXT("NewEnumerator0"));

	for (const FString& Display : DisplayNames)
	{
		FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(UDE);
		const int32 NewIndex = UDE->NumEnums() - 2; // before trailing _MAX
		if (NewIndex >= 0)
		{
			FEnumEditorUtils::SetEnumeratorDisplayName(UDE, NewIndex, FText::FromString(Display));
		}
	}

	TSharedPtr<FJsonObject> Res = Create.Finish();
	Describe(Res, UDE);
	return MCPResult(Res);
}
