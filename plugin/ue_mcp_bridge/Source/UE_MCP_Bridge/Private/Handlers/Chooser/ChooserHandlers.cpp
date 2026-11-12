#include "ChooserHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "Chooser.h"
#include "IChooserColumn.h"
#include "IChooserParameterBase.h"
#include "IObjectChooser.h"
#include "ObjectChooser_Asset.h"
#include "ObjectChooser_Class.h"
#include "MCPEngineCompat.h"
#include "EditorAssetLibrary.h"
#include "UObject/UnrealType.h"
#if UE_MCP_HAS_5_5_API
#include "Misc/StringOutputDevice.h"
#else
// 5.4 still declares FStringOutputDevice alongside FString.
#include "Containers/UnrealString.h"
#endif
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#include "HandlerChooserShared.h"


// Build a map ColumnIndex -> cell text from the caller's `cells` array and/or
// `inputs` object (keyed by column index-as-string or column name). The caller
// reads both parameters before loading the table (#1057).
static TMap<int32, FString> CollectCellAssignments(const TArray<TSharedPtr<FJsonValue>>* Cells, const TSharedPtr<FJsonObject>* Inputs, UChooserTable* Table)
{
	TMap<int32, FString> Assignments;

	// cells: array aligned to column order.
	if (Cells)
	{
		for (int32 i = 0; i < Cells->Num() && i < Table->ColumnsStructs.Num(); ++i)
		{
			const TSharedPtr<FJsonValue>& V = (*Cells)[i];
			if (V.IsValid() && V->Type != EJson::Null)
			{
				Assignments.Add(i, MCPChooserJsonToText(V));
			}
		}
	}

	// inputs: object keyed by column index-string or column name.
	if (Inputs && Inputs->IsValid())
	{
		for (const auto& Pair : (*Inputs)->Values)
		{
			const FString KeyStr(*Pair.Key);
			int32 ColIndex = INDEX_NONE;
			if (KeyStr.IsNumeric())
			{
				ColIndex = FCString::Atoi(*KeyStr);
			}
			else
			{
				for (int32 c = 0; c < Table->ColumnsStructs.Num(); ++c)
				{
					if (MCPChooserColumnName(Table->ColumnsStructs[c], c) == KeyStr) { ColIndex = c; break; }
				}
			}
			if (Table->ColumnsStructs.IsValidIndex(ColIndex))
			{
				Assignments.Add(ColIndex, MCPChooserJsonToText(Pair.Value));
			}
		}
	}
	return Assignments;
}

// Best-effort: point a column's input parameter at a bound property (and, for
// enum columns, an enum) via reflection so the resulting column filters something.
static void ConfigureColumnInput(FChooserColumnBase* Col, const FString& InputStruct, const FString& BoundProperty, const FString& EnumPath)
{
#if WITH_EDITOR
	if (!InputStruct.IsEmpty())
	{
		if (UScriptStruct* InSS = MCPChooserResolveStruct(InputStruct))
		{
			Col->SetInputType(InSS);
		}
	}

	// The input parameter's struct type + memory. FChooserColumnBase::GetInputValuePtr()
	// (a FInstancedStruct accessor) is UE 5.8+; on 5.7 reach the same memory through
	// GetInputValue() (the parameter pointer) + GetInputType() (its struct).
	const UScriptStruct* PSS = nullptr;
	void* PData = nullptr;
#if UE_MCP_HAS_5_8_API
	FInstancedStruct* InputPtr = Col->GetInputValuePtr();
	if (!InputPtr || !InputPtr->IsValid()) return;
	PSS = InputPtr->GetScriptStruct();
	PData = InputPtr->GetMutableMemory();
#else
	FChooserParameterBase* Param = Col->GetInputValue();
	PSS = Col->GetInputType();
	if (!Param || !PSS) return;
	PData = Param;
#endif

	FStructProperty* BindingProp = CastField<FStructProperty>(PSS->FindPropertyByName(TEXT("Binding")));
	if (!BindingProp) return;
	void* BindingData = BindingProp->ContainerPtrToValuePtr<void>(PData);
	const UStruct* BSS = BindingProp->Struct;

	if (!BoundProperty.IsEmpty())
	{
		if (FArrayProperty* ChainProp = CastField<FArrayProperty>(BSS->FindPropertyByName(TEXT("PropertyBindingChain"))))
		{
			FScriptArrayHelper Helper(ChainProp, ChainProp->ContainerPtrToValuePtr<void>(BindingData));
			Helper.EmptyValues();
			Helper.AddValue();
			if (FNameProperty* NameInner = CastField<FNameProperty>(ChainProp->Inner))
			{
				NameInner->SetPropertyValue(Helper.GetRawPtr(0), FName(*BoundProperty));
			}
		}
		if (FBoolProperty* RootProp = CastField<FBoolProperty>(BSS->FindPropertyByName(TEXT("IsBoundToRoot"))))
		{
			RootProp->SetPropertyValue(RootProp->ContainerPtrToValuePtr<void>(BindingData), true);
		}
	}
	if (!EnumPath.IsEmpty())
	{
		if (UEnum* Enum = LoadObject<UEnum>(nullptr, *EnumPath))
		{
			if (FObjectPropertyBase* EnumProp = CastField<FObjectPropertyBase>(BSS->FindPropertyByName(TEXT("Enum"))))
			{
				EnumProp->SetObjectPropertyValue(EnumProp->ContainerPtrToValuePtr<void>(BindingData), Enum);
			}
		}
	}
#endif
}

// ─── Handlers ─────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FChooserHandlers::Create(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game"));

	auto Created = MCPCreateAssetIdempotentNewObject<UChooserTable>(Name, PackagePath, OptionalString(Params, TEXT("onConflict"), TEXT("skip")), TEXT("ChooserTable"));
	if (Created.EarlyReturn) return Created.EarlyReturn;
	UChooserTable* Table = Created.Asset;

	Table->Compile(true);
	FString SaveError;
	const bool bSaved = SaveAssetPackageChecked(Table, SaveError);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("path"), Table->GetPathName());
	Res->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Res, Table->GetPathName());
	MCPNoteSaveOutcome(Res, Table->GetPathName(), bSaved, SaveError);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FChooserHandlers::Describe(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	if (auto Err = RequireString(Params, TEXT("table"), TablePath)) return Err;
	const FString Nested = OptionalString(Params, TEXT("nested"));
	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Table = MCPChooserResolve(TablePath, Nested, ResolveError);
	if (!Table) return ResolveError;

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("path"), Table->GetPathName());
	Res->SetStringField(TEXT("name"), Table->GetName());
	Res->SetNumberField(TEXT("rowCount"), MCPChooserRowCount(Table));
	UChooserTable* Root = MCPChooserRoot(Table);
	Res->SetBoolField(TEXT("isNested"), Root != Table);
	Res->SetStringField(TEXT("rootChooser"), Root ? Root->GetPathName() : FString());
	Res->SetStringField(TEXT("resultType"), MCPChooserGetResultTypeName(MCPChooserSignatureOwner(Table)));

	TArray<TSharedPtr<FJsonValue>> Columns;
	for (int32 c = 0; c < Table->ColumnsStructs.Num(); ++c)
	{
		TSharedPtr<FJsonObject> Col = MakeShared<FJsonObject>();
		Col->SetNumberField(TEXT("index"), c);
		Col->SetStringField(TEXT("name"), MCPChooserColumnName(Table->ColumnsStructs[c], c));
		const UScriptStruct* SS = Table->ColumnsStructs[c].GetScriptStruct();
		Col->SetStringField(TEXT("columnType"), SS ? SS->GetName() : FString());
		if (const bool* Disabled = MCPChooserColumnDisabledPtr(Table->ColumnsStructs[c]))
		{
			Col->SetBoolField(TEXT("disabled"), *Disabled);
		}
		Col->SetBoolField(TEXT("randomize"), MCPChooserIsRandomizeColumn(Table->ColumnsStructs[c]));
		Col->SetBoolField(TEXT("autoPopulates"), MCPChooserColumnAutoPopulates(Table->ColumnsStructs[c]));
		Col->SetBoolField(TEXT("hasFallbackValue"), MCPChooserFallbackValueProperty(Table->ColumnsStructs[c]) != nullptr);
		if (FInstancedStruct* Input = MCPChooserColumnInput(Table->ColumnsStructs[c]))
		{
			const UScriptStruct* InputType = Input->GetScriptStruct();
			Col->SetStringField(TEXT("inputType"), InputType ? InputType->GetPathName() : FString());
			const FStructProperty* BindingProp = nullptr;
			if (void* Binding = MCPChooserFindBinding(*Input, &BindingProp))
			{
				int32 ContextIndex = INDEX_NONE; TArray<FName> Chain; FString DisplayName;
				MCPChooserReadBinding(BindingProp, Binding, ContextIndex, Chain, DisplayName);
				TSharedPtr<FJsonObject> BindingJson = MakeShared<FJsonObject>();
				BindingJson->SetNumberField(TEXT("parameterIndex"), ContextIndex);
				TArray<TSharedPtr<FJsonValue>> ChainJson;
				for (const FName& Name : Chain) ChainJson.Add(MakeShared<FJsonValueString>(Name.ToString()));
				BindingJson->SetArrayField(TEXT("propertyChain"), ChainJson);
				BindingJson->SetStringField(TEXT("displayName"), DisplayName);
				Col->SetObjectField(TEXT("binding"), BindingJson);
			}
		}
#if WITH_EDITOR
		// Expose the cell struct type so callers know the text format to author.
		FArrayProperty* ArrProp = nullptr; void* ColData = nullptr; FString Ignored;
		if (MCPChooserRowValuesArray(Table->ColumnsStructs[c], ArrProp, ColData, Ignored))
		{
			if (FStructProperty* ElemStruct = CastField<FStructProperty>(ArrProp->Inner))
			{
				Col->SetStringField(TEXT("cellType"), ElemStruct->Struct ? ElemStruct->Struct->GetName() : FString());
			}
			else if (ArrProp->Inner)
			{
				Col->SetStringField(TEXT("cellType"), ArrProp->Inner->GetCPPType());
			}
		}
#endif
		Columns.Add(MakeShared<FJsonValueObject>(Col));
	}
	Res->SetArrayField(TEXT("columns"), Columns);

	// Fallback result (used when no row matches).
	Res->SetBoolField(TEXT("hasFallback"), Table->FallbackResult.IsValid());
#if WITH_EDITORONLY_DATA
	if (Table->FallbackResult.IsValid())
	{
		Res->SetObjectField(TEXT("fallbackResult"), MCPChooserDescribeOutput(Table->FallbackResult));
	}
#endif
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FChooserHandlers::AddColumn(const TSharedPtr<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString TablePath;
	if (auto Err = RequireString(Params, TEXT("table"), TablePath)) return Err;
	FString ColumnType;
	if (auto Err = RequireString(Params, TEXT("columnType"), ColumnType)) return Err;
	// Every parameter is read before anything can fail (#1057).
	const FString InputStruct = OptionalString(Params, TEXT("inputStruct"));
	const FString BoundProperty = OptionalString(Params, TEXT("boundProperty"));
	const FString EnumPath = OptionalString(Params, TEXT("enumPath"));
	const FString Nested = OptionalString(Params, TEXT("nested"));
	const int32 RequestedIndex = OptionalInt(Params, TEXT("insertIndex"), INDEX_NONE);

	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Table = MCPChooserResolve(TablePath, Nested, ResolveError);
	if (!Table) return ResolveError;

	UScriptStruct* ColStruct = MCPChooserResolveStruct(ColumnType);
	if (!ColStruct)
	{
		return MCPError(FString::Printf(TEXT("Column struct not found: %s (try e.g. EnumColumn, BoolColumn, FloatRangeColumn, GameplayTagColumn, OutputObjectColumn)"), *ColumnType));
	}
	if (!ColStruct->IsChildOf(FChooserColumnBase::StaticStruct()))
	{
		return MCPError(FString::Printf(TEXT("%s is not a ChooserColumn"), *ColStruct->GetName()));
	}
	if (RequestedIndex != INDEX_NONE && (RequestedIndex < 0 || RequestedIndex > Table->ColumnsStructs.Num()))
	{
		return MCPError(FString::Printf(TEXT("insertIndex %d out of range (columnCount=%d)"), RequestedIndex, Table->ColumnsStructs.Num()));
	}

	FInstancedStruct NewColumn;
	NewColumn.InitializeAs(ColStruct);
	if (MCPChooserIsRandomizeColumn(NewColumn) && MCPChooserHasRandomizeColumn(Table))
	{
		return MCPError(TEXT("The chooser already has a randomize column, and a table holds at most one"));
	}

	Table->Modify();
	if (FChooserColumnBase* Col = NewColumn.GetMutablePtr<FChooserColumnBase>())
	{
		ConfigureColumnInput(Col, InputStruct, BoundProperty, EnumPath);
		// Size the new column's per-row cell array to the existing row count.
		Col->SetNumRows(MCPChooserRowCount(Table));
	}
	// Placed where the editor places a column: a randomize column goes last and
	// every other column goes before it, at insertIndex when one is given.
	const int32 NewIndex = MCPChooserColumnInsertPosition(Table, NewColumn, RequestedIndex);
	Table->ColumnsStructs.Insert(MoveTemp(NewColumn), NewIndex);

	FString SaveError;
	const bool bSaved = MCPChooserFinalize(Table, SaveError);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("path"), Table->GetPathName());
	Res->SetNumberField(TEXT("columnIndex"), NewIndex);
	Res->SetStringField(TEXT("columnType"), ColStruct->GetName());
	Res->SetStringField(TEXT("name"), MCPChooserColumnName(Table->ColumnsStructs[NewIndex], NewIndex));
	{
		TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
		RbPayload->SetStringField(TEXT("table"), Table->GetPathName());
		RbPayload->SetNumberField(TEXT("column"), NewIndex);
		MCPSetRollback(Res, TEXT("chooser_delete_column"), RbPayload);
		Res->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The inverse deletes column %d by INDEX, which names this column only while no column is inserted, moved or deleted before it."),
			NewIndex));
	}
	FArrayProperty* ArrProp = nullptr; void* ColData = nullptr; FString Ignored;
	if (MCPChooserRowValuesArray(Table->ColumnsStructs[NewIndex], ArrProp, ColData, Ignored))
	{
		if (FStructProperty* ElemStruct = CastField<FStructProperty>(ArrProp->Inner))
		{
			Res->SetStringField(TEXT("cellType"), ElemStruct->Struct ? ElemStruct->Struct->GetName() : FString());
		}
		else if (ArrProp->Inner)
		{
			Res->SetStringField(TEXT("cellType"), ArrProp->Inner->GetCPPType());
		}
	}
	MCPNoteSaveOutcome(Res, Table->GetPathName(), bSaved, SaveError);
	return MCPResult(Res);
#else
	return MCPError(TEXT("chooser column authoring requires an editor build"));
#endif
}

TSharedPtr<FJsonValue> FChooserHandlers::ListRows(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	if (auto Err = RequireString(Params, TEXT("table"), TablePath)) return Err;
	const FString Nested = OptionalString(Params, TEXT("nested"));
	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Table = MCPChooserResolve(TablePath, Nested, ResolveError);
	if (!Table) return ResolveError;

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("path"), Table->GetPathName());

	const int32 RowCount = MCPChooserRowCount(Table);
	Res->SetNumberField(TEXT("rowCount"), RowCount);

	TArray<TSharedPtr<FJsonValue>> Rows;
#if WITH_EDITORONLY_DATA
	for (int32 r = 0; r < RowCount; ++r)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("index"), r);
		Row->SetBoolField(TEXT("disabled"), MCPChooserRowDisabled(Table, r));
		Row->SetObjectField(TEXT("output"), MCPChooserDescribeOutput(Table->ResultsStructs[r]));

		TArray<TSharedPtr<FJsonValue>> Cells;
		for (int32 c = 0; c < Table->ColumnsStructs.Num(); ++c)
		{
			TSharedPtr<FJsonObject> Cell = MakeShared<FJsonObject>();
			Cell->SetNumberField(TEXT("column"), c);
			Cell->SetStringField(TEXT("name"), MCPChooserColumnName(Table->ColumnsStructs[c], c));
			FString CellText, CellErr;
			if (MCPChooserGetCellText(Table->ColumnsStructs[c], r, CellText, CellErr))
			{
				Cell->SetStringField(TEXT("value"), CellText);
			}
			Cells.Add(MakeShared<FJsonValueObject>(Cell));
		}
		Row->SetArrayField(TEXT("cells"), Cells);
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}
#endif
	Res->SetArrayField(TEXT("rows"), Rows);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FChooserHandlers::AddRow(const TSharedPtr<FJsonObject>& Params)
{
#if WITH_EDITORONLY_DATA
	FString TablePath;
	if (auto Err = RequireString(Params, TEXT("table"), TablePath)) return Err;
	// Every parameter is read before anything can fail (#1057).
	const FString OutputPath = OptionalString(Params, TEXT("output"));
	const FString OutputType = OptionalString(Params, TEXT("outputType"), TEXT("asset"));
	const TArray<TSharedPtr<FJsonValue>>* Cells = nullptr;
	TryGetArrayParam(Params, TEXT("cells"), Cells);
	const TSharedPtr<FJsonObject>* Inputs = nullptr;
	TryGetObjectParam(Params, TEXT("inputs"), Inputs);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	const FString ResultType = OptionalString(Params, TEXT("resultType"));
	const int32 RequestedIndex = OptionalInt(Params, TEXT("insertIndex"), INDEX_NONE);

	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Table = MCPChooserResolve(TablePath, Nested, ResolveError);
	if (!Table) return ResolveError;

	if (!OutputPath.IsEmpty() && !ResultType.IsEmpty())
	{
		return MCPError(TEXT("Pass output (an asset, wrapped per outputType) or resultType (a default result of that type, filled in with chooser(set_property, target='row')), not both"));
	}
	if (RequestedIndex != INDEX_NONE && (RequestedIndex < 0 || RequestedIndex > Table->ResultsStructs.Num()))
	{
		return MCPError(FString::Printf(TEXT("insertIndex %d out of range (rowCount=%d)"), RequestedIndex, Table->ResultsStructs.Num()));
	}

	// Build the output struct (optional - a row can start with no output).
	FInstancedStruct OutputStruct;
	OutputStruct.InitializeAs<FAssetChooser>();
	if (!OutputPath.IsEmpty())
	{
		FString BuildErr;
		if (!MCPChooserBuildOutput(OutputPath, OutputType, OutputStruct, BuildErr))
		{
			return MCPError(BuildErr);
		}
	}
	else if (!ResultType.IsEmpty())
	{
		FString BuildErr;
		if (!MCPChooserMakeResult(Table, ResultType, OutputStruct, BuildErr))
		{
			return MCPError(BuildErr);
		}
	}

	Table->Modify();
	// Cells and disabled flags are brought to the row count first, so the
	// insert below lands every column's new cell at the same index.
	MCPChooserSyncRowCounts(Table);
	const int32 NewRow = RequestedIndex == INDEX_NONE ? Table->ResultsStructs.Num() : RequestedIndex;
	Table->ResultsStructs.Insert(OutputStruct, NewRow);
#if UE_MCP_HAS_5_5_API
	Table->DisabledRows.Insert(false, NewRow);
#endif
	for (FInstancedStruct& ColStruct : Table->ColumnsStructs)
	{
		MCPChooserColumnInsertRows(ColStruct, NewRow, 1);
	}
	// A column whose cells are not in one row-values array is sized here instead.
	MCPChooserSyncRowCounts(Table);
	// The editor fills auto populating columns from a new row's result.
	MCPChooserAutoPopulateRow(Table, NewRow);

	// Apply provided cell values.
	TMap<int32, FString> Assignments = CollectCellAssignments(Cells, Inputs, Table);
	TArray<FString> CellWarnings;
	for (const auto& Pair : Assignments)
	{
		FString CellErr;
		if (!MCPChooserSetCellText(Table->ColumnsStructs[Pair.Key], NewRow, Pair.Value, Table, CellErr))
		{
			CellWarnings.Add(FString::Printf(TEXT("column %d: %s"), Pair.Key, *CellErr));
		}
	}

	FString SaveError;
	const bool bSaved = MCPChooserFinalize(Table, SaveError);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("path"), Table->GetPathName());
	Res->SetNumberField(TEXT("rowIndex"), NewRow);
	Res->SetNumberField(TEXT("rowCount"), Table->ResultsStructs.Num());
	if (CellWarnings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> W;
		for (const FString& Warn : CellWarnings) W.Add(MakeShared<FJsonValueString>(Warn));
		Res->SetArrayField(TEXT("cellWarnings"), W);
	}

	TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
	RbPayload->SetStringField(TEXT("table"), Table->GetPathName());
	RbPayload->SetNumberField(TEXT("index"), NewRow);
	// The rollback method is a BRIDGE method name, which is what the flow
	// engine dispatches: "delete_row" is not registered anywhere.
	MCPSetRollback(Res, TEXT("chooser_delete_row"), RbPayload);
	// chooser_delete_row addresses a row by position and the table has no row id,
	// so there is no identity to address it by instead.
	Res->SetStringField(TEXT("rollbackNote"), FString::Printf(
		TEXT("The inverse deletes row %d by INDEX, and that index names this row only while no row is inserted, moved or deleted before it. ")
		TEXT("Run in reverse order, as a flow rolls back, every later row edit has been undone first, so the index holds. A chooser row has no id to address it by instead."),
		NewRow));
	MCPNoteSaveOutcome(Res, Table->GetPathName(), bSaved, SaveError);
	return MCPResult(Res);
#else
	return MCPError(TEXT("chooser row authoring requires an editor build"));
#endif
}

TSharedPtr<FJsonValue> FChooserHandlers::SetRow(const TSharedPtr<FJsonObject>& Params)
{
#if WITH_EDITORONLY_DATA
	FString TablePath;
	if (auto Err = RequireString(Params, TEXT("table"), TablePath)) return Err;
	int32 RowIndex = INDEX_NONE;
	const TSharedPtr<FJsonValue> IndexErr = RequireNumber(Params, TEXT("index"), RowIndex);
	// Every parameter is read before anything can fail (#1057).
	const FString OutputPath = OptionalString(Params, TEXT("output"));
	const FString OutputType = OptionalString(Params, TEXT("outputType"), TEXT("asset"));
	bool bDisabled = false;
	const bool bHasDisabled = TryGetBoolParam(Params, TEXT("disabled"), bDisabled);
	const TArray<TSharedPtr<FJsonValue>>* Cells = nullptr;
	TryGetArrayParam(Params, TEXT("cells"), Cells);
	const TSharedPtr<FJsonObject>* Inputs = nullptr;
	TryGetObjectParam(Params, TEXT("inputs"), Inputs);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	const FString ResultType = OptionalString(Params, TEXT("resultType"));
	if (IndexErr) return IndexErr;

	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Table = MCPChooserResolve(TablePath, Nested, ResolveError);
	if (!Table) return ResolveError;

	if (!Table->ResultsStructs.IsValidIndex(RowIndex))
	{
		return MCPError(FString::Printf(TEXT("row index %d out of range (rowCount=%d)"), RowIndex, Table->ResultsStructs.Num()));
	}
	if (!OutputPath.IsEmpty() && !ResultType.IsEmpty())
	{
		return MCPError(TEXT("Pass output (an asset, wrapped per outputType) or resultType (a default result of that type), not both"));
	}
	// A new result type replaces the row's result with a default of that type,
	// keeping its cells, as the editor's result type picker does.
	FInstancedStruct NewTypedResult;
	if (!ResultType.IsEmpty())
	{
		FString BuildErr;
		if (!MCPChooserMakeResult(Table, ResultType, NewTypedResult, BuildErr)) return MCPError(BuildErr);
	}

	// Read the row as it stands, before any of it is overwritten: chooser_set_row
	// is its own inverse when it is handed the previous output, disabled flag
	// and cell text.
	TSharedPtr<FJsonObject> PriorOutput = MCPChooserDescribeOutput(Table->ResultsStructs[RowIndex]);
	const FString PriorOutputPath = PriorOutput->HasField(TEXT("output")) ? PriorOutput->GetStringField(TEXT("output")) : FString();
	const FString PriorResultType = PriorOutput->GetStringField(TEXT("resultType"));
	const FString PriorOutputType = PriorResultType == TEXT("EvaluateChooser") ? TEXT("evaluate")
		: (PriorResultType == TEXT("SoftAssetChooser") ? TEXT("soft_asset") : TEXT("asset"));
	const bool bPriorDisabled = MCPChooserRowDisabled(Table, RowIndex);
	TArray<TSharedPtr<FJsonValue>> PriorCells;
	int32 UnreadableCells = 0;
	for (int32 c = 0; c < Table->ColumnsStructs.Num(); ++c)
	{
		FString CellText, CellErr;
		if (MCPChooserGetCellText(Table->ColumnsStructs[c], RowIndex, CellText, CellErr))
		{
			PriorCells.Add(MakeShared<FJsonValueString>(CellText));
		}
		else
		{
			// A null entry is skipped on replay, which leaves that column alone.
			PriorCells.Add(MakeShared<FJsonValueNull>());
			++UnreadableCells;
		}
	}

	Table->Modify();

	// Optional: replace the output object.
	if (!OutputPath.IsEmpty())
	{
		FInstancedStruct OutputStruct;
		FString BuildErr;
		if (!MCPChooserBuildOutput(OutputPath, OutputType, OutputStruct, BuildErr))
		{
			return MCPError(BuildErr);
		}
		Table->ResultsStructs[RowIndex] = OutputStruct;
	}
	else if (NewTypedResult.IsValid())
	{
		Table->ResultsStructs[RowIndex] = MoveTemp(NewTypedResult);
	}
	if (!OutputPath.IsEmpty() || !ResultType.IsEmpty())
	{
		// The editor fills auto populating columns when a row's result changes.
		MCPChooserAutoPopulateRow(Table, RowIndex);
	}

	// Optional: toggle disabled.
	if (bHasDisabled)
	{
		if (!MCPChooserRowDisableSupported())
		{
			return MCPError(TEXT("This engine's UChooserTable has no per-row disabled state, so 'disabled' cannot be honoured. Remove the row instead, or omit 'disabled'."));
		}
		MCPChooserSetRowDisabled(Table, RowIndex, bDisabled);
	}

	// Optional: update cells.
	TMap<int32, FString> Assignments = CollectCellAssignments(Cells, Inputs, Table);
	TArray<FString> CellWarnings;
	for (const auto& Pair : Assignments)
	{
		FString CellErr;
		if (!MCPChooserSetCellText(Table->ColumnsStructs[Pair.Key], RowIndex, Pair.Value, Table, CellErr))
		{
			CellWarnings.Add(FString::Printf(TEXT("column %d: %s"), Pair.Key, *CellErr));
		}
	}

	FString SaveError;
	const bool bSaved = MCPChooserFinalize(Table, SaveError);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("path"), Table->GetPathName());
	Res->SetNumberField(TEXT("rowIndex"), RowIndex);
	Res->SetObjectField(TEXT("output"), MCPChooserDescribeOutput(Table->ResultsStructs[RowIndex]));
	Res->SetObjectField(TEXT("priorOutput"), PriorOutput);
	if (CellWarnings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> W;
		for (const FString& Warn : CellWarnings) W.Add(MakeShared<FJsonValueString>(Warn));
		Res->SetArrayField(TEXT("cellWarnings"), W);
	}

	{
		TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
		RbPayload->SetStringField(TEXT("table"), Table->GetPathName());
		RbPayload->SetNumberField(TEXT("index"), RowIndex);
		RbPayload->SetBoolField(TEXT("disabled"), bPriorDisabled);
		RbPayload->SetArrayField(TEXT("cells"), PriorCells);
		if (!PriorOutputPath.IsEmpty())
		{
			RbPayload->SetStringField(TEXT("output"), PriorOutputPath);
			RbPayload->SetStringField(TEXT("outputType"), PriorOutputType);
		}
		MCPSetRollback(Res, TEXT("chooser_set_row"), RbPayload);
	}
	// chooser_set_row skips an empty `output` and a null cell, so those parts of
	// the row stay as this call left them.
	if (PriorOutputPath.IsEmpty())
	{
		Res->SetBoolField(TEXT("rollbackLossy"), true);
		Res->SetStringField(TEXT("rollbackNote"),
			TEXT("The row had no resolvable output object before this call, and chooser(set_row) treats an empty output as 'leave it alone', so the inverse restores the disabled flag and the readable cells but not the empty output."));
	}
	else if (UnreadableCells > 0)
	{
		Res->SetBoolField(TEXT("rollbackLossy"), true);
		Res->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("%d column(s) would not export their cell for this row, so the inverse leaves those columns as this call left them; every other column, the output and the disabled flag are restored."),
			UnreadableCells));
	}
	MCPNoteSaveOutcome(Res, Table->GetPathName(), bSaved, SaveError);
	return MCPResult(Res);
#else
	return MCPError(TEXT("chooser row authoring requires an editor build"));
#endif
}

TSharedPtr<FJsonValue> FChooserHandlers::DeleteRow(const TSharedPtr<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString TablePath;
	if (auto Err = RequireString(Params, TEXT("table"), TablePath)) return Err;
	int32 RowIndex = INDEX_NONE;
	const TSharedPtr<FJsonValue> IndexErr = RequireNumber(Params, TEXT("index"), RowIndex);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	if (IndexErr) return IndexErr;

	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Table = MCPChooserResolve(TablePath, Nested, ResolveError);
	if (!Table) return ResolveError;

	if (!Table->ResultsStructs.IsValidIndex(RowIndex))
	{
		return MCPError(FString::Printf(TEXT("row index %d out of range (rowCount=%d)"), RowIndex, Table->ResultsStructs.Num()));
	}

	// Read the row before it is dropped, so the inverse can rebuild it.
	TSharedPtr<FJsonObject> DoomedOutput = MCPChooserDescribeOutput(Table->ResultsStructs[RowIndex]);
	const FString DoomedOutputPath = DoomedOutput->HasField(TEXT("output")) ? DoomedOutput->GetStringField(TEXT("output")) : FString();
	const FString DoomedResultType = DoomedOutput->GetStringField(TEXT("resultType"));
	const FString DoomedOutputType = DoomedResultType == TEXT("EvaluateChooser") ? TEXT("evaluate")
		: (DoomedResultType == TEXT("SoftAssetChooser") ? TEXT("soft_asset") : TEXT("asset"));
	const bool bDoomedDisabled = MCPChooserRowDisabled(Table, RowIndex);
	TArray<TSharedPtr<FJsonValue>> DoomedCells;
	int32 UnexportableCells = 0;
	for (int32 c = 0; c < Table->ColumnsStructs.Num(); ++c)
	{
		FString CellText, CellErr;
		if (MCPChooserGetCellText(Table->ColumnsStructs[c], RowIndex, CellText, CellErr))
		{
			DoomedCells.Add(MakeShared<FJsonValueString>(CellText));
		}
		else
		{
			// A null entry is skipped on replay, leaving that column at its default.
			DoomedCells.Add(MakeShared<FJsonValueNull>());
			++UnexportableCells;
		}
	}

	Table->Modify();

	// Drop the per-row cell from every column, then the result + disabled flag.
	for (FInstancedStruct& ColStruct : Table->ColumnsStructs)
	{
		MCPChooserColumnDeleteRows(ColStruct, { RowIndex });
	}
	Table->ResultsStructs.RemoveAt(RowIndex);
	MCPChooserRemoveDisabledRow(Table, RowIndex);

	FString SaveError;
	const bool bSaved = MCPChooserFinalize(Table, SaveError);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("path"), Table->GetPathName());
	Res->SetNumberField(TEXT("deletedIndex"), RowIndex);
	Res->SetNumberField(TEXT("rowCount"), Table->ResultsStructs.Num());
	Res->SetObjectField(TEXT("deletedOutput"), DoomedOutput);

	{
		TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
		RbPayload->SetStringField(TEXT("table"), Table->GetPathName());
		RbPayload->SetArrayField(TEXT("cells"), DoomedCells);
		RbPayload->SetNumberField(TEXT("insertIndex"), RowIndex);
		if (!DoomedOutputPath.IsEmpty())
		{
			RbPayload->SetStringField(TEXT("output"), DoomedOutputPath);
			RbPayload->SetStringField(TEXT("outputType"), DoomedOutputType);
		}
		MCPSetRollback(Res, TEXT("chooser_add_row"), RbPayload);
	}
	Res->SetBoolField(TEXT("rollbackLossy"), true);
	{
		FString DeleteNote = FString::Printf(
			TEXT("chooser(add_row) puts the row back at index %d, so it keeps its place in evaluation order. "),
			RowIndex);
		if (DoomedOutputPath.IsEmpty())
		{
			// An empty `output` is "leave it alone" to chooser_add_row, so it is
			// omitted from the payload rather than sent blank.
			DeleteNote += TEXT("The row had no resolvable output object, so the payload carries no output and the restored row starts with an empty one. ");
		}
		else
		{
			DeleteNote += TEXT("Its output is carried in the payload. ");
		}
		if (UnexportableCells > 0)
		{
			DeleteNote += FString::Printf(
				TEXT("%d column(s) would not export their cell for this row and are sent as null, which chooser(add_row) skips, so the restored row takes those columns' defaults. "),
				UnexportableCells);
		}
		if (bDoomedDisabled)
		{
			DeleteNote += FString::Printf(TEXT("It also comes back enabled: this row was disabled, so follow the rollback with chooser(set_row, index=%d, disabled=true)."), RowIndex);
		}
		Res->SetStringField(TEXT("rollbackNote"), DeleteNote.TrimStartAndEnd());
	}
	MCPNoteSaveOutcome(Res, Table->GetPathName(), bSaved, SaveError);
	return MCPResult(Res);
#else
	return MCPError(TEXT("chooser row authoring requires an editor build"));
#endif
}

void FChooserHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	// Reports parameters its handlers never read (#1057).
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("chooser"));

	// #1057: a spec'd handler declares its parameters here and nowhere else; the
	// TS surface is generated from a recording of them.
	using EType = EMCPParamType;
	const FMCPParamSpec TableParam = MCPParam::Required(TEXT("table"), EType::String, TEXT("ChooserTable asset path, e.g. /Game/Path/CT_Locomotion")).Alias(TEXT("assetPath"));
	const FMCPParamSpec OutputParam = MCPParam::Optional(TEXT("output"), EType::String, TEXT("Output asset path for the row (a PoseSearchDatabase, a nested ChooserTable, etc.)"));
	const FMCPParamSpec OutputTypeParam = MCPParam::Optional(TEXT("outputType"), EType::String, TEXT("Output wrapper: 'asset' (hard ref, default) | 'soft_asset' | 'evaluate' (nested ChooserTable reference)"));
	const FMCPParamSpec CellsParam = MCPParam::Optional(TEXT("cells"), EType::Array, TEXT("Per-column cell values aligned to column order; each is struct text like '(Value=2)', or a string, number or boolean. null leaves a column at its default"));
	const FMCPParamSpec InputsParam = MCPParam::Optional(TEXT("inputs"), EType::Object, TEXT("Cell values keyed by column index (as string) or column name; same value format as cells"));
	const FMCPParamSpec ChooserPathParam = MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("ChooserTable asset path")).Alias(TEXT("path"));

	const FMCPParamSpec NestedParam = MCPParam::Optional(TEXT("nested"), EType::String, TEXT("Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"));
	const FMCPParamSpec ColumnParam = MCPParam::Required(TEXT("column"), EType::Integer, TEXT("Column index, 0-based"));
	const FMCPParamSpec RowsParam = MCPParam::Required(TEXT("rows"), EType::Array, TEXT("Row indices, 0-based; duplicates are ignored")).Items(EType::Integer);
	const FMCPParamSpec ResultTypeParam = MCPParam::Optional(TEXT("resultType"), EType::String, TEXT("Result struct type for a default result (AssetChooser, SoftAssetChooser, EvaluateChooser, ClassChooser and so on; chooser(list_types, kind='result') lists them). Fill it in with chooser(set_property)"));
	const FMCPParamSpec TargetTableParam = MCPParam::Optional(TEXT("targetTable"), EType::String, TEXT("Destination ChooserTable asset path (default the source table)"));
	const FMCPParamSpec TargetNestedParam = MCPParam::Optional(TEXT("targetNested"), EType::String, TEXT("Nested chooser inside targetTable's asset to copy into"));
	const FMCPParamSpec SignatureTableParam = MCPParam::Required(TEXT("table"), EType::String, TEXT("ChooserTable or ChooserSignature asset path. A nested chooser uses its root table's signature")).Alias(TEXT("assetPath"));
	const FMCPParamSpec NestedObjectParam = MCPParam::Required(TEXT("name"), EType::String, TEXT("Nested object name or object path (chooser(list_nested) lists them)"));

	Registry.RegisterHandler(TEXT("chooser_create"), &Create, {
		MCPParam::Required(TEXT("name"), EType::String, TEXT("New ChooserTable asset name")),
		MCPParam::Optional(TEXT("packagePath"), EType::String, TEXT("Destination folder (default /Game)")),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("When the table exists: skip (default, report it) | error")),
	}, MCPSpec::ContractExempt(TEXT("Creates and saves a ChooserTable under the contract values; nothing it reads fails first")));
	Registry.RegisterHandler(TEXT("chooser_describe"), &Describe, {
		TableParam,
		NestedParam,
	});
	Registry.RegisterHandler(TEXT("chooser_add_column"), &AddColumn, {
		TableParam,
		MCPParam::Required(TEXT("columnType"), EType::String, TEXT("Chooser column struct short name (EnumColumn, BoolColumn, FloatRangeColumn, GameplayTagColumn, ObjectColumn, Output*Column and so on)")),
		MCPParam::Optional(TEXT("inputStruct"), EType::String, TEXT("Parameter struct to bind the column input (e.g. EnumContextProperty, BoolContextProperty)")),
		MCPParam::Optional(TEXT("boundProperty"), EType::String, TEXT("Context property name the column reads at evaluation time")),
		MCPParam::Optional(TEXT("enumPath"), EType::String, TEXT("Enum asset path for an EnumColumn")),
		NestedParam,
		MCPParam::Optional(TEXT("insertIndex"), EType::Integer, TEXT("Column position (default where the editor places a new column: last, but before a trailing randomize column)")),
	});
	Registry.RegisterHandler(TEXT("chooser_list_rows"), &ListRows, {
		TableParam,
		NestedParam,
	});
	Registry.RegisterHandler(TEXT("chooser_add_row"), &AddRow, {
		TableParam,
		OutputParam,
		OutputTypeParam,
		CellsParam,
		InputsParam,
		NestedParam,
		ResultTypeParam,
		MCPParam::Optional(TEXT("insertIndex"), EType::Integer, TEXT("Insert the row before this row index (default append)")),
	});
	Registry.RegisterHandler(TEXT("chooser_set_row"), &SetRow, {
		TableParam,
		MCPParam::Required(TEXT("index"), EType::Integer, TEXT("Row index, 0-based")),
		OutputParam,
		OutputTypeParam,
		MCPParam::Optional(TEXT("disabled"), EType::Boolean, TEXT("Enable or disable the row without deleting it")),
		CellsParam,
		InputsParam,
		NestedParam,
		ResultTypeParam,
	});
	Registry.RegisterHandler(TEXT("chooser_delete_row"), &DeleteRow, {
		TableParam,
		MCPParam::Required(TEXT("index"), EType::Integer, TEXT("Row index, 0-based")),
		NestedParam,
	});
	Registry.RegisterHandler(TEXT("chooser_list_object_references"), &ListObjectReferences, {
		ChooserPathParam,
		MCPParam::Optional(TEXT("classFilter"), EType::String, TEXT("Only references whose target class matches (substring)")),
		MCPParam::Optional(TEXT("pathFilter"), EType::String, TEXT("Substring filter on the referenced object path")),
	});
	Registry.RegisterHandler(TEXT("chooser_remap_object_references"), &RemapObjectReferences, {
		ChooserPathParam,
		MCPParam::Optional(TEXT("from"), EType::String, TEXT("Exact object path to replace (with to)")),
		MCPParam::Optional(TEXT("to"), EType::String, TEXT("Replacement object path (with from)")),
		MCPParam::Optional(TEXT("fromPrefix"), EType::String, TEXT("Path prefix to rewrite, e.g. /Game/Vendor/ (with toPrefix)")),
		MCPParam::Optional(TEXT("toPrefix"), EType::String, TEXT("Replacement prefix, e.g. /Game/MyProject/ (with fromPrefix)")),
		MCPParam::Optional(TEXT("dryRun"), EType::Boolean, TEXT("Preview without writing (default true)")),
		MCPParam::Optional(TEXT("allowMissing"), EType::Boolean, TEXT("Write a soft reference even when the target does not exist yet (default false)")),
	});

	// Type discovery and signature.
	Registry.RegisterHandler(TEXT("chooser_list_types"), &ListTypes, {
		MCPParam::Required(TEXT("kind"), EType::String, TEXT("Which types: column, result (row result), parameter, or input (the input types one column accepts; needs table and column)")).Enum({ TEXT("column"), TEXT("result"), TEXT("parameter"), TEXT("input") }),
		MCPParam::Optional(TEXT("table"), EType::String, TEXT("ChooserTable asset path, for kind 'input'")),
		NestedParam,
		MCPParam::Optional(TEXT("column"), EType::Integer, TEXT("Column index, for kind 'input'")),
	});
	Registry.RegisterHandler(TEXT("chooser_get_signature"), &GetSignature, {
		SignatureTableParam,
		NestedParam,
	});
	Registry.RegisterHandler(TEXT("chooser_set_signature"), &SetSignature, {
		SignatureTableParam,
		NestedParam,
		MCPParam::Optional(TEXT("resultType"), EType::String, TEXT("What the chooser returns")).Enum({ TEXT("ObjectResult"), TEXT("ClassResult"), TEXT("NoPrimaryResult") }),
		MCPParam::Optional(TEXT("resultClass"), EType::String, TEXT("Result Class: the class of object returned, or the parent class of the classes returned. Empty clears it")),
		MCPParam::Optional(TEXT("signatureAsset"), EType::String, TEXT("ChooserSignature asset to inherit the result type, result class and parameters from (UE 5.9). Empty clears the reference and restores the local values")),
	});
	Registry.RegisterHandler(TEXT("chooser_add_parameter"), &AddParameter, {
		SignatureTableParam,
		NestedParam,
		MCPParam::Required(TEXT("parameterType"), EType::String, TEXT("Parameter struct type, e.g. ContextObjectTypeClass or ContextObjectTypeStruct (chooser(list_types, kind='parameter') lists them)")),
		MCPParam::Optional(TEXT("properties"), EType::Object, TEXT("Property path to value text to set on the new parameter, e.g. {\"Class\": \"/Script/Engine.Character\", \"Direction\": \"ReadWrite\"}")),
	});
	Registry.RegisterHandler(TEXT("chooser_remove_parameter"), &RemoveParameter, {
		SignatureTableParam,
		NestedParam,
		MCPParam::Required(TEXT("index"), EType::Integer, TEXT("Parameter index, 0-based")),
	});

	// Property paths on columns, row results, the fallback result and parameters.
	const FMCPParamSpec PropertyTargetParam = MCPParam::Required(TEXT("target"), EType::String, TEXT("What the path is read from: a column, a row's result, the fallback result, or a parameter")).Enum({ TEXT("column"), TEXT("row"), TEXT("fallback"), TEXT("parameter") });
	const FMCPParamSpec PropertyIndexParam = MCPParam::Optional(TEXT("index"), EType::Integer, TEXT("Column, row or parameter index (not used for fallback)"));
	const FMCPParamSpec PropertyPathParam = MCPParam::Optional(TEXT("propertyPath"), EType::String, TEXT("'.' separated property names with [index] for array elements, stepping into instanced structs, e.g. 'InputValue.Binding.DisplayName', 'Asset', 'DefaultRowValue'. Empty addresses the whole struct"));
	Registry.RegisterHandler(TEXT("chooser_get_property"), &GetProperty, {
		TableParam,
		NestedParam,
		PropertyTargetParam,
		PropertyIndexParam,
		PropertyPathParam,
	});
	Registry.RegisterHandler(TEXT("chooser_set_property"), &SetProperty, {
		TableParam,
		NestedParam,
		PropertyTargetParam,
		PropertyIndexParam,
		PropertyPathParam,
		MCPParam::Required(TEXT("value"), EType::String, TEXT("Value in Unreal text format, the form copy and paste use in the details panel, e.g. '0.5', '/Game/Anims/Run.Run', '(Min=0,Max=100)'")),
	});

	// Columns.
	Registry.RegisterHandler(TEXT("chooser_delete_column"), &DeleteColumn, {
		TableParam,
		NestedParam,
		ColumnParam,
	});
	Registry.RegisterHandler(TEXT("chooser_move_column"), &MoveColumn, {
		TableParam,
		NestedParam,
		ColumnParam,
		MCPParam::Required(TEXT("newIndex"), EType::Integer, TEXT("Index the column ends up at. A randomize column stays last and cannot be moved")),
	});
	Registry.RegisterHandler(TEXT("chooser_duplicate_column"), &DuplicateColumn, {
		TableParam,
		NestedParam,
		ColumnParam,
	});
	Registry.RegisterHandler(TEXT("chooser_copy_column"), &CopyColumn, {
		TableParam,
		NestedParam,
		ColumnParam,
		TargetTableParam,
		TargetNestedParam,
		MCPParam::Optional(TEXT("insertIndex"), EType::Integer, TEXT("Insert the copy before this column of the target (default last, before a randomize column)")),
	});
	Registry.RegisterHandler(TEXT("chooser_set_column"), &SetColumn, {
		TableParam,
		NestedParam,
		ColumnParam,
		MCPParam::Optional(TEXT("disabled"), EType::Boolean, TEXT("Disable or enable the column")),
		MCPParam::Optional(TEXT("inputType"), EType::String, TEXT("Change the column's input type (chooser(list_types, kind='input') lists the ones it accepts)")),
		MCPParam::Optional(TEXT("parameterIndex"), EType::Integer, TEXT("Bind the input to this class or struct parameter (with propertyChain)")),
		MCPParam::Optional(TEXT("propertyChain"), EType::Array, TEXT("Property names to follow from the parameter, e.g. [\"Speed\"] or [\"MyStruct\", \"MyFloat\"]; empty binds the parameter itself")).Items(EType::String),
	});

	// Rows.
	Registry.RegisterHandler(TEXT("chooser_delete_rows"), &DeleteRows, {
		TableParam,
		NestedParam,
		RowsParam,
	});
	Registry.RegisterHandler(TEXT("chooser_move_rows"), &MoveRows, {
		TableParam,
		NestedParam,
		RowsParam,
		MCPParam::Required(TEXT("targetIndex"), EType::Integer, TEXT("Insert the rows, in their current order, before this row index as it is before the move; rowCount moves them to the end. To move row r to final index n, pass n+1 when n > r, else n")),
	});
	Registry.RegisterHandler(TEXT("chooser_duplicate_rows"), &DuplicateRows, {
		TableParam,
		NestedParam,
		RowsParam,
	});
	Registry.RegisterHandler(TEXT("chooser_copy_rows"), &CopyRows, {
		TableParam,
		NestedParam,
		RowsParam,
		TargetTableParam,
		TargetNestedParam,
		MCPParam::Optional(TEXT("insertIndex"), EType::Integer, TEXT("Insert the copies before this row of the target (default append)")),
		MCPParam::Optional(TEXT("includeFallback"), EType::Boolean, TEXT("Also copy the fallback result and fallback cells, replacing the target's (default false)")),
	});

	// Cells and the fallback.
	const FMCPParamSpec CellRowParam = MCPParam::Optional(TEXT("row"), EType::Integer, TEXT("Row index, 0-based (omit with fallback=true)"));
	const FMCPParamSpec CellFallbackParam = MCPParam::Optional(TEXT("fallback"), EType::Boolean, TEXT("Address the column's fallback cell instead of a row (output columns only)"));
	Registry.RegisterHandler(TEXT("chooser_get_cell"), &GetCell, {
		TableParam,
		NestedParam,
		ColumnParam,
		CellRowParam,
		CellFallbackParam,
	});
	Registry.RegisterHandler(TEXT("chooser_set_cell"), &SetCell, {
		TableParam,
		NestedParam,
		ColumnParam,
		CellRowParam,
		CellFallbackParam,
		MCPParam::Required(TEXT("value"), EType::String, TEXT("Cell value as struct text, e.g. '(Min=0,Max=100)', or a bare number or boolean for a scalar cell")),
	});
	Registry.RegisterHandler(TEXT("chooser_set_fallback"), &SetFallback, {
		TableParam,
		NestedParam,
		ResultTypeParam,
		OutputParam,
		OutputTypeParam,
		MCPParam::Optional(TEXT("remove"), EType::Boolean, TEXT("Remove the fallback result; output columns keep their fallback cells")),
	});
	Registry.RegisterHandler(TEXT("chooser_auto_populate"), &AutoPopulate, {
		TableParam,
		NestedParam,
		MCPParam::Optional(TEXT("column"), EType::Integer, TEXT("Fill only this column, for every row and the fallback")),
		MCPParam::Optional(TEXT("rows"), EType::Array, TEXT("Fill only these rows, in every auto populating column")).Items(EType::Integer),
		MCPParam::Optional(TEXT("includeFallback"), EType::Boolean, TEXT("With rows, also fill the fallback")),
	});

	// Nested choosers and objects.
	Registry.RegisterHandler(TEXT("chooser_create_nested"), &CreateNested, {
		TableParam,
		NestedParam,
		MCPParam::Optional(TEXT("row"), EType::Integer, TEXT("Row whose result becomes the new nested chooser (default: add a new row for it)")),
		MCPParam::Optional(TEXT("name"), EType::String, TEXT("Name for the nested chooser (default NestedChooser, made unique)")),
	});
	Registry.RegisterHandler(TEXT("chooser_list_nested"), &ListNested, {
		TableParam,
	});
	Registry.RegisterHandler(TEXT("chooser_delete_nested"), &DeleteNested, {
		TableParam,
		NestedObjectParam,
	});
	Registry.RegisterHandler(TEXT("chooser_rename_nested"), &RenameNested, {
		TableParam,
		NestedObjectParam,
		MCPParam::Required(TEXT("newName"), EType::String, TEXT("New object name, unique among the asset's nested objects")),
	});
	Registry.RegisterHandler(TEXT("chooser_remove_disabled"), &RemoveDisabled, {
		TableParam,
		NestedParam,
	});
	Registry.RegisterHandler(TEXT("chooser_restore_snapshot"), &RestoreSnapshot, {
		TableParam,
		MCPParam::Required(TEXT("snapshot"), EType::Object, TEXT("Snapshot of the asset's chooser tables, as the rollback of delete_column, delete_rows, move_rows, auto_populate, delete_nested and remove_disabled carries it")),
	});
}
