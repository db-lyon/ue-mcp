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
#include "UObject/UObjectIterator.h"
#include "Algo/Reverse.h"
#include "HandlerPropertyText.h"
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

// ─── Version-tolerant access to UChooserTable members ─────────────────────
//
// The members below moved or arrived between UE 5.4 and 5.9 (RootChooser
// replaced ParentTable, NestedObjects replaced NestedChoosers, UChooserSignature
// took over ResultType, OutputObjectType and ContextData). They are reached
// through the property system by name so one code path builds on every engine
// and a member an engine lacks is a runtime answer, not a compile break.

/** The table that owns a nested chooser's asset, or the table itself. */
inline UChooserTable* MCPChooserRoot(UChooserTable* Table)
{
	UChooserTable* Current = Table;
	for (int32 Depth = 0; Current && Depth < 32; ++Depth)
	{
		UChooserTable* Next = nullptr;
		for (const TCHAR* Field : { TEXT("RootChooser"), TEXT("ParentTable") })
		{
			if (FObjectPropertyBase* Prop = FindFProperty<FObjectPropertyBase>(UChooserTable::StaticClass(), Field))
			{
				Next = Cast<UChooserTable>(Prop->GetObjectPropertyValue_InContainer(Current));
				if (Next) break;
			}
		}
		if (!Next || Next == Current) return Current;
		Current = Next;
	}
	return Current;
}

/** The array property the root table lists its nested objects in. */
inline FArrayProperty* MCPChooserNestedArrayProperty()
{
	for (const TCHAR* Field : { TEXT("NestedObjects"), TEXT("NestedChoosers") })
	{
		FArrayProperty* Arr = FindFProperty<FArrayProperty>(UChooserTable::StaticClass(), Field);
		if (Arr && CastField<FObjectPropertyBase>(Arr->Inner)) return Arr;
	}
	return nullptr;
}

/** Every nested object (nested choosers and column-owned objects) in a root table's asset. */
inline TArray<UObject*> MCPChooserNestedObjects(UChooserTable* Root)
{
	TArray<UObject*> Out;
	FArrayProperty* Arr = Root ? MCPChooserNestedArrayProperty() : nullptr;
	if (!Arr) return Out;
	FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(Arr->Inner);
	FScriptArrayHelper Helper(Arr, Arr->ContainerPtrToValuePtr<void>(Root));
	for (int32 i = 0; i < Helper.Num(); ++i)
	{
		if (UObject* Obj = Inner->GetObjectPropertyValue(Helper.GetRawPtr(i))) Out.AddUnique(Obj);
	}
	return Out;
}

/** Register an object in the root table's nested-object list. */
inline bool MCPChooserAddNestedObject(UChooserTable* Root, UObject* Obj)
{
	FArrayProperty* Arr = (Root && Obj) ? MCPChooserNestedArrayProperty() : nullptr;
	if (!Arr) return false;
	FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(Arr->Inner);
	FScriptArrayHelper Helper(Arr, Arr->ContainerPtrToValuePtr<void>(Root));
	for (int32 i = 0; i < Helper.Num(); ++i)
	{
		if (Inner->GetObjectPropertyValue(Helper.GetRawPtr(i)) == Obj) return true;
	}
	const int32 NewIndex = Helper.AddValue();
	Inner->SetObjectPropertyValue(Helper.GetRawPtr(NewIndex), Obj);
	return true;
}

/** Drop an object from the root table's nested-object list. */
inline void MCPChooserRemoveNestedObject(UChooserTable* Root, UObject* Obj)
{
	FArrayProperty* Arr = (Root && Obj) ? MCPChooserNestedArrayProperty() : nullptr;
	if (!Arr) return;
	FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(Arr->Inner);
	FScriptArrayHelper Helper(Arr, Arr->ContainerPtrToValuePtr<void>(Root));
	for (int32 i = Helper.Num() - 1; i >= 0; --i)
	{
		if (Inner->GetObjectPropertyValue(Helper.GetRawPtr(i)) == Obj) Helper.RemoveValues(i, 1);
	}
}

/** Point a nested chooser at its root table, under whichever field this engine has. */
inline void MCPChooserSetRoot(UChooserTable* Nested, UChooserTable* Root)
{
	if (!Nested) return;
	if (FObjectPropertyBase* Prop = FindFProperty<FObjectPropertyBase>(UChooserTable::StaticClass(), TEXT("RootChooser")))
	{
		Prop->SetObjectPropertyValue_InContainer(Nested, Root);
	}
	else if (FObjectPropertyBase* Parent = FindFProperty<FObjectPropertyBase>(UChooserTable::StaticClass(), TEXT("ParentTable")))
	{
		Parent->SetObjectPropertyValue_InContainer(Nested, Root);
	}
}

/** Load the table a request names: `table` is the asset (or a nested chooser's
 *  object path) and `nested`, when given, picks a nested chooser in that asset
 *  by name or path. Returns null with OutError set on a miss. */
inline UChooserTable* MCPChooserResolve(const FString& TablePath, const FString& Nested, TSharedPtr<FJsonValue>& OutError)
{
	UChooserTable* Table = MCPChooserLoadTable(TablePath);
	if (!Table && TablePath.Contains(TEXT(":")))
	{
		Table = FindObject<UChooserTable>(nullptr, *TablePath);
		if (!Table)
		{
			// A nested chooser's path is "<asset>:<name>"; load the asset, then look it up.
			FString AssetPart, NestedPart;
			TablePath.Split(TEXT(":"), &AssetPart, &NestedPart, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			if (UChooserTable* Owner = MCPChooserLoadTable(AssetPart))
			{
				for (UObject* Obj : MCPChooserNestedObjects(MCPChooserRoot(Owner)))
				{
					if (UChooserTable* Candidate = Cast<UChooserTable>(Obj))
					{
						if (Candidate->GetPathName() == TablePath || Candidate->GetName() == NestedPart) { Table = Candidate; break; }
					}
				}
			}
		}
	}
	if (!Table)
	{
		OutError = MCPAssetLoadError(TablePath, TEXT("ChooserTable"));
		return nullptr;
	}
	if (Nested.IsEmpty()) return Table;

	TArray<FString> Names;
	for (UObject* Obj : MCPChooserNestedObjects(MCPChooserRoot(Table)))
	{
		if (UChooserTable* Candidate = Cast<UChooserTable>(Obj))
		{
			if (Candidate->GetName() == Nested || Candidate->GetPathName() == Nested) return Candidate;
			Names.Add(Candidate->GetName());
		}
	}
	OutError = MCPError(FString::Printf(TEXT("'%s' has no nested chooser named '%s'. Nested choosers: %s"),
		*Table->GetPathName(), *Nested, Names.Num() ? *FString::Join(Names, TEXT(", ")) : TEXT("(none)")));
	return nullptr;
}

/** The row index a column uses for its fallback cell (ChooserColumn_SpecialIndex_Fallback). */
inline constexpr int32 MCPChooserFallbackIndex = -2;

// ─── Columns ──────────────────────────────────────────────────────────────

/** A column's base pointer, or null with OutError set. */
inline FChooserColumnBase* MCPChooserColumn(UChooserTable* Table, int32 ColumnIndex, FString& OutError)
{
	if (!Table->ColumnsStructs.IsValidIndex(ColumnIndex))
	{
		OutError = FString::Printf(TEXT("column index %d out of range (columnCount=%d)"), ColumnIndex, Table->ColumnsStructs.Num());
		return nullptr;
	}
	FChooserColumnBase* Column = Table->ColumnsStructs[ColumnIndex].GetMutablePtr<FChooserColumnBase>();
	if (!Column)
	{
		OutError = FString::Printf(TEXT("column %d has no valid type (it may be defined in a plugin which is not loaded)"), ColumnIndex);
	}
	return Column;
}

/** Randomize columns must stay last, and a table holds at most one. */
inline bool MCPChooserIsRandomizeColumn(const FInstancedStruct& ColumnData)
{
#if UE_MCP_HAS_5_7_API
	const FChooserColumnBase* Column = ColumnData.GetPtr<FChooserColumnBase>();
	return Column && Column->IsRandomizeColumn();
#else
	const UScriptStruct* SS = ColumnData.GetScriptStruct();
	return SS && SS->GetName().Contains(TEXT("Randomize"));
#endif
}

inline bool MCPChooserHasRandomizeColumn(const UChooserTable* Table)
{
	return Table->ColumnsStructs.Num() > 0 && MCPChooserIsRandomizeColumn(Table->ColumnsStructs.Last());
}

/** The column's editor-only disabled flag (FChooserColumnBase::bDisabled), or null. */
inline bool* MCPChooserColumnDisabledPtr(FInstancedStruct& ColumnData)
{
	const UScriptStruct* SS = ColumnData.GetScriptStruct();
	FBoolProperty* Prop = SS ? FindFProperty<FBoolProperty>(SS, TEXT("bDisabled")) : nullptr;
	if (!Prop) return nullptr;
	return Prop->ContainerPtrToValuePtr<bool>(ColumnData.GetMutableMemory());
}

/** The FInstancedStruct holding a column's input parameter, or null. */
inline FInstancedStruct* MCPChooserColumnInput(FInstancedStruct& ColumnData)
{
#if UE_MCP_HAS_5_8_API
	if (FChooserColumnBase* Column = ColumnData.GetMutablePtr<FChooserColumnBase>())
	{
		if (FInstancedStruct* Input = Column->GetInputValuePtr()) return Input;
	}
#endif
	const UScriptStruct* SS = ColumnData.GetScriptStruct();
	FStructProperty* Prop = SS ? FindFProperty<FStructProperty>(SS, TEXT("InputValue")) : nullptr;
	if (!Prop || Prop->Struct != FInstancedStruct::StaticStruct()) return nullptr;
	return Prop->ContainerPtrToValuePtr<FInstancedStruct>(ColumnData.GetMutableMemory());
}

/** Tell a column its input changed (FChooserColumnBase::InputValueChanged, 5.8+). */
inline void MCPChooserInputChanged(FChooserColumnBase* Column)
{
#if UE_MCP_HAS_5_8_API
	if (Column) Column->InputValueChanged();
#endif
}

/** The property binding inside a column input struct (an FChooserPropertyBinding member), or null. */
inline void* MCPChooserFindBinding(FInstancedStruct& Input, const FStructProperty** OutProp = nullptr)
{
	const UScriptStruct* InputType = Input.GetScriptStruct();
	if (!InputType || !Input.GetMutableMemory()) return nullptr;
	const UScriptStruct* BindingBase = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Chooser.ChooserPropertyBinding"));
	for (TFieldIterator<FStructProperty> It(InputType); It; ++It)
	{
		const bool bIsBinding = BindingBase ? It->Struct->IsChildOf(BindingBase) : It->Struct->GetName().EndsWith(TEXT("PropertyBinding"));
		if (bIsBinding)
		{
			if (OutProp) *OutProp = *It;
			return It->ContainerPtrToValuePtr<void>(Input.GetMutableMemory());
		}
	}
	return nullptr;
}

/** Read a binding's parameter index and property chain. */
inline bool MCPChooserReadBinding(const FStructProperty* BindingProp, const void* Binding, int32& OutContextIndex, TArray<FName>& OutChain, FString& OutDisplayName)
{
	OutContextIndex = INDEX_NONE;
	OutChain.Reset();
	OutDisplayName.Reset();
	if (!BindingProp || !Binding) return false;
	const UStruct* BS = BindingProp->Struct;
	if (FIntProperty* IndexProp = FindFProperty<FIntProperty>(BS, TEXT("ContextIndex")))
	{
		OutContextIndex = IndexProp->GetPropertyValue_InContainer(Binding);
	}
	if (FArrayProperty* ChainProp = FindFProperty<FArrayProperty>(BS, TEXT("PropertyBindingChain")))
	{
		if (FNameProperty* NameInner = CastField<FNameProperty>(ChainProp->Inner))
		{
			FScriptArrayHelper Helper(ChainProp, ChainProp->ContainerPtrToValuePtr<void>(const_cast<void*>(Binding)));
			for (int32 i = 0; i < Helper.Num(); ++i) OutChain.Add(NameInner->GetPropertyValue(Helper.GetRawPtr(i)));
		}
	}
	if (FStrProperty* NameProp = FindFProperty<FStrProperty>(BS, TEXT("DisplayName")))
	{
		OutDisplayName = NameProp->GetPropertyValue_InContainer(Binding);
	}
	return true;
}

/** Write a binding the way the editor's binding menu does: the parameter index,
 *  the chain of property names below it, IsBoundToRoot when the chain is empty
 *  (bound to the parameter itself), and a display name. */
inline void MCPChooserWriteBinding(const FStructProperty* BindingProp, void* Binding, int32 ContextIndex, const TArray<FName>& Chain, const FString& DisplayName)
{
	const UStruct* BS = BindingProp->Struct;
	if (FIntProperty* IndexProp = FindFProperty<FIntProperty>(BS, TEXT("ContextIndex")))
	{
		IndexProp->SetPropertyValue_InContainer(Binding, ContextIndex);
	}
	if (FArrayProperty* ChainProp = FindFProperty<FArrayProperty>(BS, TEXT("PropertyBindingChain")))
	{
		if (FNameProperty* NameInner = CastField<FNameProperty>(ChainProp->Inner))
		{
			FScriptArrayHelper Helper(ChainProp, ChainProp->ContainerPtrToValuePtr<void>(Binding));
			Helper.EmptyValues();
			for (const FName& Name : Chain)
			{
				const int32 i = Helper.AddValue();
				NameInner->SetPropertyValue(Helper.GetRawPtr(i), Name);
			}
		}
	}
	if (FBoolProperty* RootProp = FindFProperty<FBoolProperty>(BS, TEXT("IsBoundToRoot")))
	{
		RootProp->SetPropertyValue_InContainer(Binding, Chain.Num() == 0);
	}
	if (FStrProperty* NameProp = FindFProperty<FStrProperty>(BS, TEXT("DisplayName")))
	{
		NameProp->SetPropertyValue_InContainer(Binding, DisplayName);
	}
}

// Per-row cell storage. The column virtuals are used where every engine this
// was checked against (5.7 to 5.9) declares them; below that the same edit is
// made on the column's row-values array through the property system, which is
// where the boilerplate columns keep their cells.

inline void MCPChooserColumnInsertRows(FInstancedStruct& ColumnData, int32 Index, int32 Count)
{
	FChooserColumnBase* Column = ColumnData.GetMutablePtr<FChooserColumnBase>();
	if (!Column || Count <= 0) return;
#if UE_MCP_HAS_5_7_API
	Column->InsertRows(Index, Count);
#else
	FArrayProperty* Arr = nullptr; void* Data = nullptr; FString Ignored;
	if (!MCPChooserRowValuesArray(ColumnData, Arr, Data, Ignored)) return;
	FScriptArrayHelper Helper(Arr, Arr->ContainerPtrToValuePtr<void>(Data));
	Index = FMath::Clamp(Index, 0, Helper.Num());
	Helper.InsertValues(Index, Count);
	if (FProperty* Default = ColumnData.GetScriptStruct()->FindPropertyByName(TEXT("DefaultRowValue")))
	{
		if (Default->SameType(Arr->Inner))
		{
			for (int32 i = 0; i < Count; ++i)
			{
				Arr->Inner->CopyCompleteValue(Helper.GetRawPtr(Index + i), Default->ContainerPtrToValuePtr<void>(Data));
			}
		}
	}
#endif
}

/** Move one row's cell so it lands before TargetIndex, an index from before the move. */
inline void MCPChooserColumnMoveRow(FInstancedStruct& ColumnData, int32 SourceIndex, int32 TargetIndex)
{
	FChooserColumnBase* Column = ColumnData.GetMutablePtr<FChooserColumnBase>();
	if (!Column) return;
#if UE_MCP_HAS_5_7_API
	Column->MoveRow(SourceIndex, TargetIndex);
#else
	FArrayProperty* Arr = nullptr; void* Data = nullptr; FString Ignored;
	if (!MCPChooserRowValuesArray(ColumnData, Arr, Data, Ignored)) return;
	FScriptArrayHelper Helper(Arr, Arr->ContainerPtrToValuePtr<void>(Data));
	if (!Helper.IsValidIndex(SourceIndex)) return;
	if (SourceIndex < TargetIndex) --TargetIndex;
	TargetIndex = FMath::Clamp(TargetIndex, 0, Helper.Num() - 1);
	while (SourceIndex < TargetIndex) { Helper.SwapValues(SourceIndex, SourceIndex + 1); ++SourceIndex; }
	while (SourceIndex > TargetIndex) { Helper.SwapValues(SourceIndex, SourceIndex - 1); --SourceIndex; }
#endif
}

/** Copy one cell from Source (a column of the same type) into Target. */
inline void MCPChooserColumnCopyRow(FInstancedStruct& Target, FInstancedStruct& Source, int32 SourceIndex, int32 TargetIndex)
{
	if (Target.GetScriptStruct() != Source.GetScriptStruct()) return;
	FChooserColumnBase* TargetColumn = Target.GetMutablePtr<FChooserColumnBase>();
	FChooserColumnBase* SourceColumn = Source.GetMutablePtr<FChooserColumnBase>();
	if (!TargetColumn || !SourceColumn) return;
#if UE_MCP_HAS_5_7_API
	TargetColumn->CopyRow(*SourceColumn, SourceIndex, TargetIndex);
#else
	FArrayProperty* Arr = nullptr; void* TargetData = nullptr; void* SourceData = nullptr; FString Ignored;
	if (!MCPChooserRowValuesArray(Target, Arr, TargetData, Ignored)) return;
	SourceData = Source.GetMutableMemory();
	FScriptArrayHelper TargetRows(Arr, Arr->ContainerPtrToValuePtr<void>(TargetData));
	FScriptArrayHelper SourceRows(Arr, Arr->ContainerPtrToValuePtr<void>(SourceData));
	if (!TargetRows.IsValidIndex(TargetIndex) || !SourceRows.IsValidIndex(SourceIndex)) return;
	Arr->Inner->CopyCompleteValue(TargetRows.GetRawPtr(TargetIndex), SourceRows.GetRawPtr(SourceIndex));
#endif
}

/** The property an output column keeps its fallback cell in, or null when it has none. */
inline FProperty* MCPChooserFallbackValueProperty(const FInstancedStruct& ColumnData)
{
	const UScriptStruct* SS = ColumnData.GetScriptStruct();
	if (!SS) return nullptr;
	for (const TCHAR* Field : { TEXT("FallbackValue"), TEXT("bFallbackValue") })
	{
		if (FProperty* Prop = SS->FindPropertyByName(Field)) return Prop;
	}
	return nullptr;
}

inline void MCPChooserColumnCopyFallback(FInstancedStruct& Target, FInstancedStruct& Source)
{
	if (Target.GetScriptStruct() != Source.GetScriptStruct()) return;
	FProperty* Prop = MCPChooserFallbackValueProperty(Target);
	if (!Prop) return;
	Prop->CopyCompleteValue(Prop->ContainerPtrToValuePtr<void>(Target.GetMutableMemory()), Prop->ContainerPtrToValuePtr<void>(Source.GetMutableMemory()));
}

/** Delete rows' cells from one column. Rows must be sorted ascending; they are removed from the end. */
inline void MCPChooserColumnDeleteRows(FInstancedStruct& ColumnData, const TArray<int32>& SortedRows)
{
	FChooserColumnBase* Column = ColumnData.GetMutablePtr<FChooserColumnBase>();
	if (!Column) return;
	// The column removes in the order given, so the highest index goes first
	// and every earlier index still names the row it named before.
#if UE_MCP_HAS_5_8_API
	TArray<int32> Descending = SortedRows;
	Algo::Reverse(Descending);
	Column->DeleteRows(TArrayView<int32>(Descending));
#else
	TArray<uint32> Descending;
	for (int32 i = SortedRows.Num() - 1; i >= 0; --i) Descending.Add(static_cast<uint32>(SortedRows[i]));
	Column->DeleteRows(Descending);
#endif
}

/** Size every column's cells and the disabled flags to the row count, as the editor does before an edit. */
inline void MCPChooserSyncRowCounts(UChooserTable* Table)
{
	MCPChooserGrowDisabledRows(Table);
#if UE_MCP_HAS_5_5_API
	if (Table->DisabledRows.Num() > Table->ResultsStructs.Num()) Table->DisabledRows.SetNum(Table->ResultsStructs.Num());
#endif
	for (FInstancedStruct& ColumnData : Table->ColumnsStructs)
	{
		if (FChooserColumnBase* Column = ColumnData.GetMutablePtr<FChooserColumnBase>())
		{
			Column->SetNumRows(Table->ResultsStructs.Num());
		}
	}
}

/** Insert a column where the editor would: before a trailing randomize column,
 *  unless it is itself the randomize column. InsertIndex INDEX_NONE means the
 *  editor's placement; otherwise it is clamped so it never lands after a randomize column. */
inline int32 MCPChooserColumnInsertPosition(const UChooserTable* Table, const FInstancedStruct& NewColumn, int32 InsertIndex)
{
	const int32 Num = Table->ColumnsStructs.Num();
	const bool bHasRandomize = MCPChooserHasRandomizeColumn(Table);
	if (MCPChooserIsRandomizeColumn(NewColumn)) return Num;
	const int32 Limit = bHasRandomize ? Num - 1 : Num;
	if (InsertIndex == INDEX_NONE) return Limit;
	return FMath::Clamp(InsertIndex, 0, Limit);
}

// ─── Auto populate (FChooserColumnBase::AutoPopulate, 5.7+) ───────────────

inline constexpr bool MCPChooserAutoPopulateSupported()
{
#if UE_MCP_HAS_5_7_API
	return true;
#else
	return false;
#endif
}

/** The object a row's result references (null for none). */
inline UObject* MCPChooserReferencedObject(const FInstancedStruct& Result)
{
#if UE_MCP_HAS_5_5_API && WITH_EDITOR
	if (const FObjectChooserBase* Base = Result.GetPtr<FObjectChooserBase>()) return Base->GetReferencedObject();
#endif
	return nullptr;
}

inline bool MCPChooserColumnAutoPopulates(const FInstancedStruct& ColumnData)
{
#if UE_MCP_HAS_5_7_API
	const FChooserColumnBase* Column = ColumnData.GetPtr<FChooserColumnBase>();
	return Column && Column->AutoPopulates();
#else
	return false;
#endif
}

/** Fill every auto populating column's cell for one row (or MCPChooserFallbackIndex) from the row's result. */
inline void MCPChooserAutoPopulateRow(UChooserTable* Table, int32 RowIndex)
{
#if UE_MCP_HAS_5_7_API
	MCPChooserSyncRowCounts(Table);
	if (RowIndex != MCPChooserFallbackIndex && !Table->ResultsStructs.IsValidIndex(RowIndex)) return;
	const FInstancedStruct& Result = RowIndex == MCPChooserFallbackIndex ? Table->FallbackResult : Table->ResultsStructs[RowIndex];
	if (!Result.GetPtr<FObjectChooserBase>()) return;
	UObject* Referenced = MCPChooserReferencedObject(Result);
	for (FInstancedStruct& ColumnData : Table->ColumnsStructs)
	{
		if (FChooserColumnBase* Column = ColumnData.GetMutablePtr<FChooserColumnBase>())
		{
			if (Column->AutoPopulates()) Column->AutoPopulate(RowIndex, Referenced);
		}
	}
#endif
}

/** Fill one auto populating column's cells for every row and the fallback. */
inline void MCPChooserAutoPopulateColumn(UChooserTable* Table, FInstancedStruct& ColumnData)
{
#if UE_MCP_HAS_5_7_API
	MCPChooserSyncRowCounts(Table);
	FChooserColumnBase* Column = ColumnData.GetMutablePtr<FChooserColumnBase>();
	if (!Column || !Column->AutoPopulates()) return;
	for (int32 Row = 0; Row < Table->ResultsStructs.Num(); ++Row)
	{
		Column->AutoPopulate(Row, MCPChooserReferencedObject(Table->ResultsStructs[Row]));
	}
	Column->AutoPopulate(MCPChooserFallbackIndex, MCPChooserReferencedObject(Table->FallbackResult));
#endif
}

// ─── Signature (result type, result class, parameters) ────────────────────

/** The object whose ResultType, OutputObjectType and ContextData a chooser uses:
 *  the root table for a nested chooser, the asset itself otherwise. */
inline UObject* MCPChooserSignatureOwner(UObject* Asset)
{
	if (UChooserTable* Table = Cast<UChooserTable>(Asset)) return MCPChooserRoot(Table);
	return Asset;
}

/** The signature asset Owner inherits from (UChooserSignature::Signature, 5.9), or null. */
inline UObject* MCPChooserInheritedSignature(UObject* Owner)
{
	FObjectPropertyBase* Prop = Owner ? FindFProperty<FObjectPropertyBase>(Owner->GetClass(), TEXT("Signature")) : nullptr;
	return Prop ? Prop->GetObjectPropertyValue_InContainer(Owner) : nullptr;
}

/** Owner's locally authored ContextData (the parameter list), or null when the class has none. */
inline TArray<FInstancedStruct>* MCPChooserContextData(UObject* Owner)
{
	FArrayProperty* Arr = Owner ? FindFProperty<FArrayProperty>(Owner->GetClass(), TEXT("ContextData")) : nullptr;
	FStructProperty* Inner = Arr ? CastField<FStructProperty>(Arr->Inner) : nullptr;
	if (!Inner || Inner->Struct != FInstancedStruct::StaticStruct()) return nullptr;
	return Arr->ContainerPtrToValuePtr<TArray<FInstancedStruct>>(Owner);
}

/** The parameters a chooser actually evaluates with: the inherited signature's when one is set. */
inline TArray<FInstancedStruct>* MCPChooserResolvedContextData(UObject* Owner)
{
	UObject* Current = Owner;
	for (int32 Depth = 0; Depth < 8; ++Depth)
	{
		UObject* Next = MCPChooserInheritedSignature(Current);
		if (!Next || Next == Owner) break;
		Current = Next;
	}
	return MCPChooserContextData(Current);
}

/** The enum-valued ResultType property and its enum. */
inline bool MCPChooserResultTypeProperty(UObject* Owner, FProperty*& OutProp, UEnum*& OutEnum)
{
	OutProp = Owner ? FindFProperty<FProperty>(Owner->GetClass(), TEXT("ResultType")) : nullptr;
	OutEnum = nullptr;
	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(OutProp)) OutEnum = EnumProp->GetEnum();
	else if (FByteProperty* ByteProp = CastField<FByteProperty>(OutProp)) OutEnum = ByteProp->Enum;
	return OutProp && OutEnum;
}

/** ResultType as its enum name: ObjectResult, ClassResult or NoPrimaryResult. */
inline FString MCPChooserGetResultTypeName(UObject* Owner)
{
	FProperty* Prop = nullptr; UEnum* Enum = nullptr;
	if (!MCPChooserResultTypeProperty(Owner, Prop, Enum)) return FString();
	const void* Addr = Prop->ContainerPtrToValuePtr<void>(Owner);
	int64 Value = 0;
	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop)) Value = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(Addr);
	else Value = CastField<FByteProperty>(Prop)->GetPropertyValue(Addr);
	return Enum->GetNameStringByValue(Value);
}

inline bool MCPChooserSetResultTypeName(UObject* Owner, const FString& Name, FString& OutError)
{
	FProperty* Prop = nullptr; UEnum* Enum = nullptr;
	if (!MCPChooserResultTypeProperty(Owner, Prop, Enum)) { OutError = TEXT("this asset has no ResultType"); return false; }
	const int64 Value = Enum->GetValueByNameString(Name);
	if (Value == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("invalid result type '%s', must be ObjectResult, ClassResult or NoPrimaryResult"), *Name);
		return false;
	}
	void* Addr = Prop->ContainerPtrToValuePtr<void>(Owner);
	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop)) EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(Addr, Value);
	else CastField<FByteProperty>(Prop)->SetPropertyValue(Addr, static_cast<uint8>(Value));
	return true;
}

inline FObjectPropertyBase* MCPChooserResultClassProperty(UObject* Owner)
{
	return Owner ? FindFProperty<FObjectPropertyBase>(Owner->GetClass(), TEXT("OutputObjectType")) : nullptr;
}

// ─── Types ────────────────────────────────────────────────────────────────

/** Base struct of a parameter (FContextObjectTypeBase), looked up by name. */
inline const UScriptStruct* MCPChooserParameterBaseStruct()
{
	return FindObject<UScriptStruct>(nullptr, TEXT("/Script/Chooser.ContextObjectTypeBase"));
}

/** Every loaded struct deriving from Base (Base itself and Hidden ones excluded), by path, sorted. */
inline TArray<FString> MCPChooserAvailableTypes(const UScriptStruct* Base)
{
	TArray<FString> Out;
	if (!Base) return Out;
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		if (*It == Base || !It->IsChildOf(Base)) continue;
#if WITH_EDITORONLY_DATA
		if (It->HasMetaData(TEXT("Hidden"))) continue;
#endif
		Out.Add(It->GetPathName());
	}
	Out.Sort();
	return Out;
}

/** Resolve a type name deriving from RequiredBase: a full path, a short name
 *  (with or without the C++ F prefix) or a display name. */
inline const UScriptStruct* MCPChooserFindType(const FString& InTypeName, const UScriptStruct* RequiredBase, FString& OutError)
{
	const FString TypeName = InTypeName.TrimStartAndEnd();
	if (TypeName.IsEmpty()) { OutError = TEXT("empty type name"); return nullptr; }
	if (!RequiredBase) { OutError = TEXT("the base type is not loaded"); return nullptr; }
	const UScriptStruct* Found = nullptr;
	if (TypeName.StartsWith(TEXT("/")))
	{
		Found = FindObject<UScriptStruct>(nullptr, *TypeName);
		if (!Found && !TypeName.StartsWith(TEXT("/Script/")))
		{
			Found = LoadObject<UScriptStruct>(nullptr, *TypeName, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
	}
	else
	{
		const FString Bare = (TypeName.Len() > 1 && TypeName[0] == TEXT('F') && FChar::IsUpper(TypeName[1])) ? TypeName.RightChop(1) : TypeName;
		TArray<const UScriptStruct*> ByName, ByDisplay;
		for (TObjectIterator<UScriptStruct> It; It; ++It)
		{
			if (*It == RequiredBase || !It->IsChildOf(RequiredBase)) continue;
			if (It->GetName().Equals(TypeName, ESearchCase::IgnoreCase) || It->GetName().Equals(Bare, ESearchCase::IgnoreCase)) ByName.Add(*It);
#if WITH_EDITOR
			else if (It->GetDisplayNameText().ToString().Equals(TypeName, ESearchCase::IgnoreCase)) ByDisplay.Add(*It);
#endif
		}
		const TArray<const UScriptStruct*>& Matches = ByName.Num() ? ByName : ByDisplay;
		if (Matches.Num() > 1)
		{
			TArray<FString> Paths;
			for (const UScriptStruct* M : Matches) Paths.Add(M->GetPathName());
			OutError = FString::Printf(TEXT("'%s' is ambiguous: %s. Pass the full path"), *TypeName, *FString::Join(Paths, TEXT(", ")));
			return nullptr;
		}
		Found = Matches.Num() ? Matches[0] : nullptr;
	}
	if (!Found)
	{
		OutError = FString::Printf(TEXT("no type named '%s' derives from %s (chooser(list_types) lists them)"), *TypeName, *RequiredBase->GetName());
		return nullptr;
	}
	if (Found == RequiredBase || !Found->IsChildOf(RequiredBase))
	{
		OutError = FString::Printf(TEXT("%s does not derive from %s"), *Found->GetPathName(), *RequiredBase->GetName());
		return nullptr;
	}
	return Found;
}

/** Build a default result of ResultType for a row or the fallback, under the
 *  editor's rules: empty is allowed only on a chooser with no primary result,
 *  and a type tagged ResultType=Class or Object must match the chooser. */
inline bool MCPChooserMakeResult(UChooserTable* Table, const FString& ResultType, FInstancedStruct& Out, FString& OutError)
{
	const FString ChooserResultType = MCPChooserGetResultTypeName(MCPChooserSignatureOwner(Table));
	if (ResultType.TrimStartAndEnd().IsEmpty())
	{
		if (ChooserResultType != TEXT("NoPrimaryResult"))
		{
			OutError = TEXT("a resultType is required (chooser(list_types, kind='result') lists them); it may be empty only on a chooser whose result type is NoPrimaryResult");
			return false;
		}
		// Every row needs a valid result, or it does not apply its outputs.
		Out.InitializeAs(FClassChooser::StaticStruct());
		if (FObjectPropertyBase* ClassProp = FindFProperty<FObjectPropertyBase>(FClassChooser::StaticStruct(), TEXT("Class")))
		{
			ClassProp->SetObjectPropertyValue_InContainer(Out.GetMutableMemory(), UClass::StaticClass());
		}
		return true;
	}
	const UScriptStruct* Type = MCPChooserFindType(ResultType, FObjectChooserBase::StaticStruct(), OutError);
	if (!Type) return false;
#if WITH_EDITORONLY_DATA
	if (Type->HasMetaData(TEXT("ResultType")))
	{
		const FString Meta = Type->GetMetaData(TEXT("ResultType"));
		if ((Meta == TEXT("Class") && ChooserResultType == TEXT("ObjectResult")) || (Meta == TEXT("Object") && ChooserResultType == TEXT("ClassResult")))
		{
			OutError = FString::Printf(TEXT("result type '%s' can only be used in choosers which return a %s"), *Type->GetName(), *Meta);
			return false;
		}
	}
#endif
	Out.InitializeAs(Type);
	return true;
}

// ─── Property paths ───────────────────────────────────────────────────────

/** Import Text into a property value only if all of it parses: the value is
 *  written to a scratch copy first, so a rejected write leaves nothing changed. */
inline bool MCPChooserImportValidated(FProperty* Prop, void* Addr, const FString& Text, UObject* Owner, FString& OutError)
{
	void* Scratch = FMemory::Malloc(Prop->GetSize(), Prop->GetMinAlignment());
	Prop->InitializeValue(Scratch);
	Prop->CopyCompleteValue(Scratch, Addr);
	const bool bOk = MCPPropertyText::ImportTextIntoProperty(Prop, Scratch, Text, Owner, OutError);
	if (bOk) Prop->CopyCompleteValue(Addr, Scratch);
	Prop->DestroyValue(Scratch);
	FMemory::Free(Scratch);
	return bOk;
}

// ─── Persist ──────────────────────────────────────────────────────────────

// Persist a chooser after structural edits: recompile cooked data, notify, save.
// A nested chooser is compiled with its root and saved through the root's
// package, which is the asset on disk. Returns whether the package reached
// disk, with the reason when it did not.
inline bool MCPChooserFinalize(UChooserTable* Table, FString& OutSaveError)
{
	UChooserTable* Root = MCPChooserRoot(Table);
	Table->Compile(true);
	Table->PostEditChange();
	if (Root && Root != Table)
	{
		Root->Compile(true);
		Root->PostEditChange();
	}
#if UE_MCP_HAS_5_9_API && WITH_EDITOR
	// An open Chooser Table editor redraws on these, as it does after its own edits.
	if (Root)
	{
		Root->ColumnsChanged.Broadcast();
		Root->RowsChanged.Broadcast();
	}
#endif
	return SaveAssetPackageChecked(Root ? Root : Table, OutSaveError);
}

