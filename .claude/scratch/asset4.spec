TSharedPtr<FJsonValue> FAssetHandlers::CreateUserDefinedStruct(
TSharedPtr<FJsonValue> FAssetHandlers::CreateUserDefinedStruct(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("UserDefinedStruct"), TEXT("/Game") });
	if (auto Err = Create.Read()) return Err;

	auto Describe = [](const TSharedPtr<FJsonObject>& Out, UUserDefinedStruct* Struct)
	{
		TArray<TSharedPtr<FJsonValue>> FieldList;
		int32 Idx = 0;
		for (const FStructVariableDescription& Desc : FStructureEditorUtils::GetVarDesc(Struct))
		{
			FieldList.Add(MakeShared<FJsonValueObject>(StructFieldToJson(Desc, Idx++)));
		}
		Out->SetArrayField(TEXT("fields"), FieldList);
		Out->SetNumberField(TEXT("count"), FieldList.Num());
	};
	UUserDefinedStruct* Struct = nullptr;
	if (auto Early = Create.Construct<UUserDefinedStruct>(Struct, [](UPackage* Package, FName Name, EObjectFlags Flags)
		{
			return FStructureEditorUtils::CreateUserDefinedStruct(Package, Name, Flags);
		}))
	{
		return FMCPCreate::DescribeExisting<UUserDefinedStruct>(Early, Describe);
	}

	// CreateUserDefinedStruct seeds one default member. Capture its GUID so we can
	// strip it once the caller's own fields are in (a struct must never hit zero
	// members, so we add first and remove the seed last).
	TArray<FGuid> SeedGuids;
	for (const FStructVariableDescription& Desc : FStructureEditorUtils::GetVarDesc(Struct))
	{
		SeedGuids.Add(Desc.VarGuid);
	}

	const TArray<TSharedPtr<FJsonValue>>* Fields = nullptr;
	int32 AddedCount = 0;
	if (TryGetArrayParam(Params, TEXT("structFields"), Fields) && Fields)
	{
		for (const TSharedPtr<FJsonValue>& Entry : *Fields)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Entry->TryGetObject(Obj) || !Obj) continue;
			const FString FieldName = OptionalString(*Obj, TEXT("name"));
			const FString FieldType = OptionalString(*Obj, TEXT("type"), TEXT("bool"));
			if (FieldName.IsEmpty()) continue;

			// Snapshot GUIDs so we can identify the member AddVariable appends.
			TSet<FGuid> Before;
			for (const FStructVariableDescription& D : FStructureEditorUtils::GetVarDesc(Struct)) Before.Add(D.VarGuid);

			if (!FStructureEditorUtils::AddVariable(Struct, FBlueprintHandlers::MakePinType(FieldType))) continue;

			for (const FStructVariableDescription& D : FStructureEditorUtils::GetVarDesc(Struct))
			{
				if (!Before.Contains(D.VarGuid))
				{
					FStructureEditorUtils::RenameVariable(Struct, D.VarGuid, FieldName);
					++AddedCount;
					break;
				}
			}
		}
	}

	// Only drop the seed member if the caller supplied replacements; otherwise the
	// struct keeps the default so it never has zero members.
	if (AddedCount > 0)
	{
		for (const FGuid& Seed : SeedGuids)
		{
			FStructureEditorUtils::RemoveVariable(Struct, Seed);
		}
	}

	TSharedPtr<FJsonObject> Res = Create.Finish();
	Describe(Res, Struct);
	return MCPResult(Res);
}
