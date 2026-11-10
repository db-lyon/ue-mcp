#pragma once

// Chooser table helpers shared by the chooser handler files. They are inline
// in a header, not file-local statics, because the module is a unity build:
// two .cpp files with their own copy of a helper merge into one translation
// unit and the second copy is a redefinition. Every name carries the
// MCPChooser prefix so it cannot meet a helper from another category.

#include "CoreMinimal.h"
#include "HandlerUtils.h"
#include "Chooser.h"
#include "IChooserColumn.h"
#include "IChooserParameterBase.h"
#include "IObjectChooser.h"
#include "ObjectChooser_Asset.h"
#include "ObjectChooser_Class.h"
#include "MCPEngineCompat.h"
#include "UObject/UnrealType.h"
#if UE_MCP_HAS_5_5_API
#include "Misc/StringOutputDevice.h"
#else
// 5.4 still declares FStringOutputDevice alongside FString.
#include "Containers/UnrealString.h"
#endif
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

inline UChooserTable* MCPChooserLoadTable(const FString& Path)
{
	return LoadAssetByPath<UChooserTable>(Path);
}

// Number of rows in a chooser (implicit: one result struct per row).
inline int32 MCPChooserRowCount(const UChooserTable* Table)
{
#if WITH_EDITORONLY_DATA
	return Table->ResultsStructs.Num();
#else
	return Table->CookedResults.Num();
#endif
}

// A short, friendly identifier for a column: the bound property name if the
// input parameter has one, else the column struct's type name.
inline FString MCPChooserColumnName(FInstancedStruct& ColStruct, int32 ColumnIndex)
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
inline bool MCPChooserRowValuesArray(FInstancedStruct& ColStruct, FArrayProperty*& OutArr, void*& OutColData, FString& OutError)
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
inline bool MCPChooserGetCellText(FInstancedStruct& ColStruct, int32 RowIndex, FString& OutText, FString& OutError)
{
	FArrayProperty* ArrProp = nullptr; void* ColData = nullptr;
	if (!MCPChooserRowValuesArray(ColStruct, ArrProp, ColData, OutError)) return false;
	FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>(ColData));
	if (!Helper.IsValidIndex(RowIndex)) { OutError = TEXT("row index out of range for column"); return false; }
	OutText.Reset();
	ArrProp->Inner->ExportTextItem_Direct(OutText, Helper.GetRawPtr(RowIndex), nullptr, nullptr, PPF_None, nullptr);
	return true;
}

// Import text into a column's cell at RowIndex. Partial struct text (e.g.
// "(Value=2)") sets only the named fields and leaves the rest at their defaults.
// A capture output device is passed to ImportText_Direct: passing nullptr there
// crashes the editor when the importer logs a parse warning (it dereferences the
// device), so malformed input must fail cleanly instead of taking the process down.
inline bool MCPChooserSetCellText(FInstancedStruct& ColStruct, int32 RowIndex, const FString& InText, UObject* Owner, FString& OutError)
{
	FArrayProperty* ArrProp = nullptr; void* ColData = nullptr;
	if (!MCPChooserRowValuesArray(ColStruct, ArrProp, ColData, OutError)) return false;
	FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>(ColData));
	if (!Helper.IsValidIndex(RowIndex)) { OutError = TEXT("row index out of range for column"); return false; }
	FStringOutputDevice ImportErrors;
	const TCHAR* Parsed = ArrProp->Inner->ImportText_Direct(*InText, Helper.GetRawPtr(RowIndex), Owner, PPF_None, &ImportErrors);
	if (Parsed == nullptr || !ImportErrors.IsEmpty())
	{
		OutError = ImportErrors.IsEmpty()
			? FString::Printf(TEXT("could not parse cell value '%s' for this column's cell type"), *InText)
			: FString::Printf(TEXT("cell value '%s': %s"), *InText, *ImportErrors);
		return false;
	}
	return true;
}

// Coerce an arbitrary JSON value to the text form ImportText expects.
inline FString MCPChooserJsonToText(const TSharedPtr<FJsonValue>& Val)
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
inline bool MCPChooserBuildOutput(const FString& OutputPath, const FString& OutputType, FInstancedStruct& Out, FString& OutError)
{
	UObject* Obj = MCPLoadAssetObject(OutputPath);
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
#if UE_MCP_HAS_5_5_API
		Out.InitializeAs<FSoftAssetChooser>();
		Out.GetMutablePtr<FSoftAssetChooser>()->Asset = Obj;
#else
		// FSoftAssetChooser does not exist in 5.4; a soft result there would
		// have to be written as a hard one, which is a different asset.
		OutError = TEXT("soft_asset results require Unreal Engine 5.5 or newer; use outputType 'asset' on 5.4");
		return false;
#endif
	}
	else
	{
		Out.InitializeAs<FAssetChooser>();
		Out.GetMutablePtr<FAssetChooser>()->Asset = Obj;
	}
	return true;
}

// Describe the current output object of a row (type + referenced asset path).
inline TSharedPtr<FJsonObject> MCPChooserDescribeOutput(const FInstancedStruct& Result)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	const UScriptStruct* SS = Result.GetScriptStruct();
	Obj->SetStringField(TEXT("resultType"), SS ? SS->GetName() : FString());
#if WITH_EDITOR
	if (const FObjectChooserBase* Base = Result.GetPtr<FObjectChooserBase>())
	{
#if UE_MCP_HAS_5_5_API
		UObject* Ref = Base->GetReferencedObject();
#else
		// 5.4's FObjectChooserBase has no GetReferencedObject. Every result
		// struct it defines holds the reference in one object property, named
		// Asset on an asset result and Chooser on a nested one, so the same
		// answer comes out of the property system.
		UObject* Ref = nullptr;
		if (SS)
		{
			const TCHAR* const ReferenceFields[] = { TEXT("Asset"), TEXT("Chooser") };
			for (const TCHAR* FieldName : ReferenceFields)
			{
				if (FObjectPropertyBase* ObjectProp = FindFProperty<FObjectPropertyBase>(SS, FieldName))
				{
					Ref = ObjectProp->GetObjectPropertyValue(
						ObjectProp->ContainerPtrToValuePtr<void>(Result.GetMemory()));
					if (Ref) break;
				}
			}
		}
#endif
		Obj->SetStringField(TEXT("output"), Ref ? Ref->GetPathName() : FString());
	}
#endif

	return Obj;
}

// UChooserTable::DisabledRows is 5.5 and newer. On 5.4 a chooser row cannot be
// disabled at all, so a row always reads as enabled, the grow and remove calls
// have nothing to keep in step, and an attempt to disable one is refused rather
// than reported as done.
inline bool MCPChooserRowDisabled(const UChooserTable* Table, int32 RowIndex)
{
#if UE_MCP_HAS_5_5_API
	return Table && Table->DisabledRows.IsValidIndex(RowIndex) && Table->DisabledRows[RowIndex];
#else
	return false;
#endif
}

inline void MCPChooserGrowDisabledRows(UChooserTable* Table)
{
#if UE_MCP_HAS_5_5_API
	if (!Table) return;
	while (Table->DisabledRows.Num() < Table->ResultsStructs.Num()) Table->DisabledRows.Add(false);
#endif
}

inline void MCPChooserRemoveDisabledRow(UChooserTable* Table, int32 RowIndex)
{
#if UE_MCP_HAS_5_5_API
	if (Table && Table->DisabledRows.IsValidIndex(RowIndex)) Table->DisabledRows.RemoveAt(RowIndex);
#endif
}

/** Can a row be disabled on this engine at all? */
inline constexpr bool MCPChooserRowDisableSupported()
{
#if UE_MCP_HAS_5_5_API
	return true;
#else
	return false;
#endif
}

inline void MCPChooserSetRowDisabled(UChooserTable* Table, int32 RowIndex, bool bDisabled)
{
#if UE_MCP_HAS_5_5_API
	if (!Table || !Table->ResultsStructs.IsValidIndex(RowIndex)) return;
	MCPChooserGrowDisabledRows(Table);
	Table->DisabledRows[RowIndex] = bDisabled;
#endif
}

// Resolve a Chooser struct (column or parameter) by short name ("EnumColumn"),
// F-prefixed name ("FEnumColumn"), or full path ("/Script/Chooser.EnumColumn").
inline UScriptStruct* MCPChooserResolveStruct(const FString& TypeName)
{
	if (TypeName.StartsWith(TEXT("/Script/")))
	{
		return FindObject<UScriptStruct>(nullptr, *TypeName);
	}
	// The name as given first, then without a C++ F prefix: stripping a leading F
	// unconditionally turned FloatRangeColumn into loatRangeColumn.
	TArray<FString> Candidates = { TypeName };
	if (TypeName.Len() > 1 && TypeName[0] == TEXT('F') && FChar::IsUpper(TypeName[1])) Candidates.Add(TypeName.RightChop(1));
	for (const FString& Short : Candidates)
	{
		if (UScriptStruct* SS = FindObject<UScriptStruct>(nullptr, *FString::Printf(TEXT("/Script/Chooser.%s"), *Short))) return SS;
		if (UScriptStruct* SS = FindFirstObject<UScriptStruct>(*Short, EFindFirstObjectOptions::None)) return SS;
	}
	return nullptr;
}

// Persist a chooser after structural edits: recompile cooked data, notify, save.
// Returns whether the package reached disk, with the reason when it did not.
inline bool MCPChooserFinalize(UChooserTable* Table, FString& OutSaveError)
{
	Table->Compile(true);
	Table->PostEditChange();
	return SaveAssetPackageChecked(Table, OutSaveError);
}

