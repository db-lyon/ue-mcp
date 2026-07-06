#include "ChooserHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Chooser.h"
#include "IChooserColumn.h"
#include "IChooserParameterBase.h"
#include "IObjectChooser.h"
#include "ObjectChooser_Asset.h"
#include "ObjectChooser_Class.h"
#include "StructUtils/InstancedStruct.h"
#include "EditorAssetLibrary.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// ─── Helpers ──────────────────────────────────────────────────────────────

static UChooserTable* LoadChooserTable(const FString& Path)
{
	return LoadAssetByPath<UChooserTable>(Path);
}

// Number of rows in a chooser (implicit: one result struct per row).
static int32 GetChooserRowCount(const UChooserTable* Table)
{
#if WITH_EDITORONLY_DATA
	return Table->ResultsStructs.Num();
#else
	return Table->CookedResults.Num();
#endif
}

// A short, friendly identifier for a column: the bound property name if the
// input parameter has one, else the column struct's type name.
static FString GetColumnName(FInstancedStruct& ColStruct, int32 ColumnIndex)
{
	if (FChooserColumnBase* Col = ColStruct.GetMutablePtr<FChooserColumnBase>())
	{
		if (FChooserParameterBase* Input = Col->GetInputValue())
		{
			const FString DebugName = Input->GetDebugName();
			if (!DebugName.IsEmpty()) return DebugName;
		}
	}
	if (const UScriptStruct* SS = ColStruct.GetScriptStruct())
	{
		return FString::Printf(TEXT("%s_%d"), *SS->GetName(), ColumnIndex);
	}
	return FString::Printf(TEXT("column_%d"), ColumnIndex);
}

// Resolve the FArrayProperty backing a column's per-row cell values, plus the
// column's struct memory. Returns false (with error text) if unavailable.
static bool GetColumnRowValuesArray(FInstancedStruct& ColStruct, FArrayProperty*& OutArr, void*& OutColData, FString& OutError)
{
#if WITH_EDITOR
	FChooserColumnBase* Col = ColStruct.GetMutablePtr<FChooserColumnBase>();
	const UScriptStruct* SS = ColStruct.GetScriptStruct();
	if (!Col || !SS) { OutError = TEXT("column has no valid struct"); return false; }
	const FName RowValsName = Col->RowValuesPropertyName();
	if (RowValsName.IsNone()) { OutError = TEXT("column exposes no row-values array"); return false; }
	FArrayProperty* ArrProp = CastField<FArrayProperty>(SS->FindPropertyByName(RowValsName));
	if (!ArrProp) { OutError = FString::Printf(TEXT("row-values property '%s' not found on %s"), *RowValsName.ToString(), *SS->GetName()); return false; }
	OutArr = ArrProp;
	OutColData = ColStruct.GetMutableMemory();
	return true;
#else
	OutError = TEXT("chooser row authoring requires an editor build");
	return false;
#endif
}

// Export a column's cell at RowIndex to text (round-trippable via ImportText).
static bool GetColumnCellText(FInstancedStruct& ColStruct, int32 RowIndex, FString& OutText, FString& OutError)
{
	FArrayProperty* ArrProp = nullptr; void* ColData = nullptr;
	if (!GetColumnRowValuesArray(ColStruct, ArrProp, ColData, OutError)) return false;
	FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>(ColData));
	if (!Helper.IsValidIndex(RowIndex)) { OutError = TEXT("row index out of range for column"); return false; }
	OutText.Reset();
	ArrProp->Inner->ExportTextItem_Direct(OutText, Helper.GetRawPtr(RowIndex), nullptr, nullptr, PPF_None, nullptr);
	return true;
}

// Import text into a column's cell at RowIndex. Partial struct text (e.g.
// "(Value=2)") sets only the named fields and leaves the rest at their defaults.
static bool SetColumnCellText(FInstancedStruct& ColStruct, int32 RowIndex, const FString& InText, FString& OutError)
{
	FArrayProperty* ArrProp = nullptr; void* ColData = nullptr;
	if (!GetColumnRowValuesArray(ColStruct, ArrProp, ColData, OutError)) return false;
	FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>(ColData));
	if (!Helper.IsValidIndex(RowIndex)) { OutError = TEXT("row index out of range for column"); return false; }
	if (!ArrProp->Inner->ImportText_Direct(*InText, Helper.GetRawPtr(RowIndex), nullptr, PPF_None, nullptr))
	{
		OutError = FString::Printf(TEXT("could not parse cell value '%s'"), *InText);
		return false;
	}
	return true;
}

// Coerce an arbitrary JSON value to the text form ImportText expects.
static FString JsonValueToCellText(const TSharedPtr<FJsonValue>& Val)
{
	if (!Val.IsValid()) return FString();
	FString S;
	if (Val->TryGetString(S)) return S;
	bool B;
	if (Val->TryGetBool(B)) return B ? TEXT("true") : TEXT("false");
	double D;
	if (Val->TryGetNumber(D)) return FString::SanitizeFloat(D);
	return FString();
}

// Build a result (output) struct from an asset path. outputType selects the
// wrapper: "asset" (hard ref, default), "soft_asset", or "evaluate" (nested
// ChooserTable). Returns false with error text on failure.
static bool BuildOutputStruct(const FString& OutputPath, const FString& OutputType, FInstancedStruct& Out, FString& OutError)
{
	UObject* Obj = UEditorAssetLibrary::LoadAsset(OutputPath);
	if (!Obj) { OutError = FString::Printf(TEXT("output asset not found: %s"), *OutputPath); return false; }

	if (OutputType == TEXT("evaluate"))
	{
		UChooserTable* Nested = Cast<UChooserTable>(Obj);
		if (!Nested) { OutError = TEXT("evaluate output must be a ChooserTable asset"); return false; }
		Out.InitializeAs<FEvaluateChooser>();
		Out.GetMutablePtr<FEvaluateChooser>()->Chooser = Nested;
	}
	else if (OutputType == TEXT("soft_asset"))
	{
		Out.InitializeAs<FSoftAssetChooser>();
		Out.GetMutablePtr<FSoftAssetChooser>()->Asset = Obj;
	}
	else
	{
		Out.InitializeAs<FAssetChooser>();
		Out.GetMutablePtr<FAssetChooser>()->Asset = Obj;
	}
	return true;
}

// Describe the current output object of a row (type + referenced asset path).
static TSharedPtr<FJsonObject> DescribeOutput(const FInstancedStruct& Result)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	const UScriptStruct* SS = Result.GetScriptStruct();
	Obj->SetStringField(TEXT("resultType"), SS ? SS->GetName() : FString());
#if WITH_EDITOR
	if (const FObjectChooserBase* Base = Result.GetPtr<FObjectChooserBase>())
	{
		UObject* Ref = Base->GetReferencedObject();
		Obj->SetStringField(TEXT("output"), Ref ? Ref->GetPathName() : FString());
	}
#endif
	return Obj;
}

// Build a map ColumnIndex -> cell text from the caller's `cells` array and/or
// `inputs` object (keyed by column index-as-string or column name).
static TMap<int32, FString> CollectCellAssignments(const TSharedPtr<FJsonObject>& Params, UChooserTable* Table)
{
	TMap<int32, FString> Assignments;

	// cells: array aligned to column order.
	const TArray<TSharedPtr<FJsonValue>>* Cells = nullptr;
	if (Params->TryGetArrayField(TEXT("cells"), Cells) && Cells)
	{
		for (int32 i = 0; i < Cells->Num() && i < Table->ColumnsStructs.Num(); ++i)
		{
			const TSharedPtr<FJsonValue>& V = (*Cells)[i];
			if (V.IsValid() && V->Type != EJson::Null)
			{
				Assignments.Add(i, JsonValueToCellText(V));
			}
		}
	}

	// inputs: object keyed by column index-string or column name.
	const TSharedPtr<FJsonObject>* Inputs = nullptr;
	if (Params->TryGetObjectField(TEXT("inputs"), Inputs) && Inputs)
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
					if (GetColumnName(Table->ColumnsStructs[c], c) == KeyStr) { ColIndex = c; break; }
				}
			}
			if (Table->ColumnsStructs.IsValidIndex(ColIndex))
			{
				Assignments.Add(ColIndex, JsonValueToCellText(Pair.Value));
			}
		}
	}
	return Assignments;
}

// Persist a chooser after structural edits: recompile cooked data, notify, save.
static void FinalizeChooser(UChooserTable* Table)
{
	Table->Compile(true);
	Table->PostEditChange();
	UEditorAssetLibrary::SaveLoadedAsset(Table);
}

// ─── Handlers ─────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FChooserHandlers::Describe(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	if (auto Err = RequireStringAlt(Params, TEXT("table"), TEXT("assetPath"), TablePath)) return Err;
	UChooserTable* Table = LoadChooserTable(TablePath);
	if (!Table) return MCPError(FString::Printf(TEXT("ChooserTable not found: %s"), *TablePath));

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("path"), Table->GetPathName());
	Res->SetStringField(TEXT("name"), Table->GetName());
	Res->SetNumberField(TEXT("rowCount"), GetChooserRowCount(Table));

	TArray<TSharedPtr<FJsonValue>> Columns;
	for (int32 c = 0; c < Table->ColumnsStructs.Num(); ++c)
	{
		TSharedPtr<FJsonObject> Col = MakeShared<FJsonObject>();
		Col->SetNumberField(TEXT("index"), c);
		Col->SetStringField(TEXT("name"), GetColumnName(Table->ColumnsStructs[c], c));
		const UScriptStruct* SS = Table->ColumnsStructs[c].GetScriptStruct();
		Col->SetStringField(TEXT("columnType"), SS ? SS->GetName() : FString());
#if WITH_EDITOR
		// Expose the cell struct type so callers know the text format to author.
		FArrayProperty* ArrProp = nullptr; void* ColData = nullptr; FString Ignored;
		if (GetColumnRowValuesArray(Table->ColumnsStructs[c], ArrProp, ColData, Ignored))
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
#if WITH_EDITORONLY_DATA
	if (Table->FallbackResult.IsValid())
	{
		Res->SetObjectField(TEXT("fallbackResult"), DescribeOutput(Table->FallbackResult));
	}
#endif
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FChooserHandlers::ListRows(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	if (auto Err = RequireStringAlt(Params, TEXT("table"), TEXT("assetPath"), TablePath)) return Err;
	UChooserTable* Table = LoadChooserTable(TablePath);
	if (!Table) return MCPError(FString::Printf(TEXT("ChooserTable not found: %s"), *TablePath));

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("path"), Table->GetPathName());

	const int32 RowCount = GetChooserRowCount(Table);
	Res->SetNumberField(TEXT("rowCount"), RowCount);

	TArray<TSharedPtr<FJsonValue>> Rows;
#if WITH_EDITORONLY_DATA
	for (int32 r = 0; r < RowCount; ++r)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("index"), r);
		Row->SetBoolField(TEXT("disabled"), Table->DisabledRows.IsValidIndex(r) && Table->DisabledRows[r]);
		Row->SetObjectField(TEXT("output"), DescribeOutput(Table->ResultsStructs[r]));

		TArray<TSharedPtr<FJsonValue>> Cells;
		for (int32 c = 0; c < Table->ColumnsStructs.Num(); ++c)
		{
			TSharedPtr<FJsonObject> Cell = MakeShared<FJsonObject>();
			Cell->SetNumberField(TEXT("column"), c);
			Cell->SetStringField(TEXT("name"), GetColumnName(Table->ColumnsStructs[c], c));
			FString CellText, CellErr;
			if (GetColumnCellText(Table->ColumnsStructs[c], r, CellText, CellErr))
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
	if (auto Err = RequireStringAlt(Params, TEXT("table"), TEXT("assetPath"), TablePath)) return Err;
	UChooserTable* Table = LoadChooserTable(TablePath);
	if (!Table) return MCPError(FString::Printf(TEXT("ChooserTable not found: %s"), *TablePath));

	// Build the output struct (optional — a row can start with no output).
	FInstancedStruct OutputStruct;
	OutputStruct.InitializeAs<FAssetChooser>();
	const FString OutputPath = OptionalString(Params, TEXT("output"));
	if (!OutputPath.IsEmpty())
	{
		FString BuildErr;
		if (!BuildOutputStruct(OutputPath, OptionalString(Params, TEXT("outputType"), TEXT("asset")), OutputStruct, BuildErr))
		{
			return MCPError(BuildErr);
		}
	}

	Table->Modify();
	const int32 NewRow = Table->ResultsStructs.Num();
	Table->ResultsStructs.Add(OutputStruct);
	while (Table->DisabledRows.Num() < Table->ResultsStructs.Num()) Table->DisabledRows.Add(false);

	// Grow every column's per-row array to match the new row count.
	for (FInstancedStruct& ColStruct : Table->ColumnsStructs)
	{
		if (FChooserColumnBase* Col = ColStruct.GetMutablePtr<FChooserColumnBase>())
		{
			Col->SetNumRows(Table->ResultsStructs.Num());
		}
	}

	// Apply provided cell values.
	TMap<int32, FString> Assignments = CollectCellAssignments(Params, Table);
	TArray<FString> CellWarnings;
	for (const auto& Pair : Assignments)
	{
		FString CellErr;
		if (!SetColumnCellText(Table->ColumnsStructs[Pair.Key], NewRow, Pair.Value, CellErr))
		{
			CellWarnings.Add(FString::Printf(TEXT("column %d: %s"), Pair.Key, *CellErr));
		}
	}

	FinalizeChooser(Table);

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
	MCPSetRollback(Res, TEXT("delete_row"), RbPayload);
	return MCPResult(Res);
#else
	return MCPError(TEXT("chooser row authoring requires an editor build"));
#endif
}

TSharedPtr<FJsonValue> FChooserHandlers::SetRow(const TSharedPtr<FJsonObject>& Params)
{
#if WITH_EDITORONLY_DATA
	FString TablePath;
	if (auto Err = RequireStringAlt(Params, TEXT("table"), TEXT("assetPath"), TablePath)) return Err;
	UChooserTable* Table = LoadChooserTable(TablePath);
	if (!Table) return MCPError(FString::Printf(TEXT("ChooserTable not found: %s"), *TablePath));

	int32 RowIndex = INDEX_NONE;
	if (!Params->TryGetNumberField(TEXT("index"), RowIndex))
	{
		return MCPError(TEXT("Missing required parameter 'index'"));
	}
	if (!Table->ResultsStructs.IsValidIndex(RowIndex))
	{
		return MCPError(FString::Printf(TEXT("row index %d out of range (rowCount=%d)"), RowIndex, Table->ResultsStructs.Num()));
	}

	Table->Modify();

	// Optional: replace the output object.
	const FString OutputPath = OptionalString(Params, TEXT("output"));
	if (!OutputPath.IsEmpty())
	{
		FInstancedStruct OutputStruct;
		FString BuildErr;
		if (!BuildOutputStruct(OutputPath, OptionalString(Params, TEXT("outputType"), TEXT("asset")), OutputStruct, BuildErr))
		{
			return MCPError(BuildErr);
		}
		Table->ResultsStructs[RowIndex] = OutputStruct;
	}

	// Optional: toggle disabled.
	bool bDisabled;
	if (Params->TryGetBoolField(TEXT("disabled"), bDisabled))
	{
		while (Table->DisabledRows.Num() < Table->ResultsStructs.Num()) Table->DisabledRows.Add(false);
		Table->DisabledRows[RowIndex] = bDisabled;
	}

	// Optional: update cells.
	TMap<int32, FString> Assignments = CollectCellAssignments(Params, Table);
	TArray<FString> CellWarnings;
	for (const auto& Pair : Assignments)
	{
		FString CellErr;
		if (!SetColumnCellText(Table->ColumnsStructs[Pair.Key], RowIndex, Pair.Value, CellErr))
		{
			CellWarnings.Add(FString::Printf(TEXT("column %d: %s"), Pair.Key, *CellErr));
		}
	}

	FinalizeChooser(Table);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("path"), Table->GetPathName());
	Res->SetNumberField(TEXT("rowIndex"), RowIndex);
	Res->SetObjectField(TEXT("output"), DescribeOutput(Table->ResultsStructs[RowIndex]));
	if (CellWarnings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> W;
		for (const FString& Warn : CellWarnings) W.Add(MakeShared<FJsonValueString>(Warn));
		Res->SetArrayField(TEXT("cellWarnings"), W);
	}
	return MCPResult(Res);
#else
	return MCPError(TEXT("chooser row authoring requires an editor build"));
#endif
}

TSharedPtr<FJsonValue> FChooserHandlers::DeleteRow(const TSharedPtr<FJsonObject>& Params)
{
#if WITH_EDITOR
	FString TablePath;
	if (auto Err = RequireStringAlt(Params, TEXT("table"), TEXT("assetPath"), TablePath)) return Err;
	UChooserTable* Table = LoadChooserTable(TablePath);
	if (!Table) return MCPError(FString::Printf(TEXT("ChooserTable not found: %s"), *TablePath));

	int32 RowIndex = INDEX_NONE;
	if (!Params->TryGetNumberField(TEXT("index"), RowIndex))
	{
		return MCPError(TEXT("Missing required parameter 'index'"));
	}
	if (!Table->ResultsStructs.IsValidIndex(RowIndex))
	{
		return MCPError(FString::Printf(TEXT("row index %d out of range (rowCount=%d)"), RowIndex, Table->ResultsStructs.Num()));
	}

	Table->Modify();

	// Drop the per-row cell from every column, then the result + disabled flag.
	int32 RowToDelete = RowIndex;
	TArrayView<int32> RowView(&RowToDelete, 1);
	for (FInstancedStruct& ColStruct : Table->ColumnsStructs)
	{
		if (FChooserColumnBase* Col = ColStruct.GetMutablePtr<FChooserColumnBase>())
		{
			Col->DeleteRows(RowView);
		}
	}
	Table->ResultsStructs.RemoveAt(RowIndex);
	if (Table->DisabledRows.IsValidIndex(RowIndex)) Table->DisabledRows.RemoveAt(RowIndex);

	FinalizeChooser(Table);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("path"), Table->GetPathName());
	Res->SetNumberField(TEXT("deletedIndex"), RowIndex);
	Res->SetNumberField(TEXT("rowCount"), Table->ResultsStructs.Num());
	return MCPResult(Res);
#else
	return MCPError(TEXT("chooser row authoring requires an editor build"));
#endif
}

void FChooserHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("chooser_describe"), &Describe);
	Registry.RegisterHandler(TEXT("chooser_list_rows"), &ListRows);
	Registry.RegisterHandler(TEXT("chooser_add_row"), &AddRow);
	Registry.RegisterHandler(TEXT("chooser_set_row"), &SetRow);
	Registry.RegisterHandler(TEXT("chooser_delete_row"), &DeleteRow);
}
