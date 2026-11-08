TSharedPtr<FJsonValue> FAssetHandlers::CreateCurveTable(
TSharedPtr<FJsonValue> FAssetHandlers::CreateCurveTable(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("CurveTable"), TEXT("/Game/CurveTables") });
	if (auto Err = Create.Read()) return Err;

	auto Describe = [](const TSharedPtr<FJsonObject>& Out, UCurveTable* Table)
	{
		Out->SetStringField(TEXT("curveType"), CurveTableModeName(Table->GetCurveTableMode()));
		Out->SetNumberField(TEXT("rowCount"), Table->GetRowMap().Num());
	};
	UCurveTable* Table = nullptr;
	if (auto Early = Create.MakeNewObject(Table)) return FMCPCreate::DescribeExisting<UCurveTable>(Early, Describe);

	// A curve table's save also invalidates its cached curves, so it saves through its own change routine.
	TSharedPtr<FJsonObject> Result = Create.Finish(/*bSave=*/false);
	FString SaveReason;
	const bool bSaved = SaveCurveTableChange(Table, SaveReason);
	MCPNoteSaveOutcome(Result, Table->GetPathName(), bSaved, SaveReason);
	Describe(Result, Table);
	return MCPResult(Result);
}
=====
TSharedPtr<FJsonValue> FAssetHandlers::CreateDataTable(
TSharedPtr<FJsonValue> FAssetHandlers::CreateDataTable(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("DataTable"), TEXT("/Game/DataTables") });
	if (auto Err = Create.Read()) return Err;
	FString RowStruct;
	if (auto Err = RequireString(Params, TEXT("rowStruct"), RowStruct)) return Err;

	// #1088: FTableRowBase and /Script/Engine.FTableRowBase are the spellings
	// callers copy from headers; the UScriptStruct is named TableRowBase.
	FString ResolveError;
	UScriptStruct* ScriptStruct = MCPResolveScriptStruct(RowStruct, &ResolveError);
	if (!ScriptStruct)
	{
		return MCPError(ResolveError.IsEmpty()
			? FString::Printf(TEXT("Row struct not found: %s"), *RowStruct)
			: ResolveError);
	}

	auto Describe = [](const TSharedPtr<FJsonObject>& Out, UDataTable* Table)
	{
		Out->SetStringField(TEXT("rowStruct"), Table->RowStruct ? Table->RowStruct->GetName() : TEXT(""));
		Out->SetNumberField(TEXT("rowCount"), Table->GetRowMap().Num());
	};
	UDataTableFactory* Factory = NewObject<UDataTableFactory>();
	Factory->Struct = ScriptStruct;
	UDataTable* DataTable = nullptr;
	if (auto Early = Create.Make(DataTable, UDataTable::StaticClass(), Factory)) return FMCPCreate::DescribeExisting<UDataTable>(Early, Describe);

	TSharedPtr<FJsonObject> Result = Create.Finish();
	Describe(Result, DataTable);
	return MCPResult(Result);
}
=====
TSharedPtr<FJsonValue> FAssetHandlers::CreateStringTable(
TSharedPtr<FJsonValue> FAssetHandlers::CreateStringTable(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("StringTable"), TEXT("/Game/StringTables") });
	if (auto Err = Create.Read()) return Err;
	const FString TableNamespace = OptionalString(Params, TEXT("namespace"));

	// The readers' info fields, without their assetPath: a create reports path.
	auto Describe = [](const TSharedPtr<FJsonObject>& Out, UStringTable* Table)
	{
		SetStringTableInfoFields(Out, Table);
		Out->RemoveField(TEXT("assetPath"));
	};
	UStringTable* StringTable = nullptr;
	if (auto Early = Create.MakeNewObject(StringTable)) return FMCPCreate::DescribeExisting<UStringTable>(Early, Describe);

	if (!TableNamespace.IsEmpty())
	{
		StringTable->Modify(true);
#if UE_MCP_HAS_5_5_API
		StringTable->GetMutableStringTable()->SetNamespace(FTextKey(TableNamespace));
#else
		// 5.4's FStringTable::SetNamespace takes the string itself.
		StringTable->GetMutableStringTable()->SetNamespace(TableNamespace);
#endif
	}

	TSharedPtr<FJsonObject> Result = Create.Finish();
	Describe(Result, StringTable);
	return MCPResult(Result);
}
