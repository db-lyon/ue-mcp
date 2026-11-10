// Chooser table authoring beyond rows (#1828): type discovery, the signature
// (result type, result class, parameters), property paths on columns, results
// and parameters, column and row restructuring, cells, the fallback, auto
// populate, and nested choosers.
//
// UE 5.9 ships the same surface as UChooserTableEditorLibrary in the
// ChooserEditor module. These handlers do not call it: they edit the engine's
// own chooser data structures (UChooserTable's ColumnsStructs, ResultsStructs,
// DisabledRows, FallbackResult and nested objects, and the column virtuals)
// the way that library does, so every engine from 5.4 to 5.9 takes one code
// path and the live tests on any engine cover all of them. Members that moved
// between engine versions are reached through HandlerChooserShared.h.
//
// Translation-unit partition of FChooserHandlers; registrations live in
// ChooserHandlers.cpp.

#include "ChooserHandlers.h"

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerClassResolve.h"
#include "HandlerJsonProperty.h"
#include "HandlerChooserShared.h"

#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

namespace
{
	TArray<TSharedPtr<FJsonValue>> MCPChooserAuthInts(const TArray<int32>& In)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (int32 V : In) Out.Add(MakeShared<FJsonValueNumber>(V));
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> MCPChooserAuthStrings(const TArray<FString>& In)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& V : In) Out.Add(MakeShared<FJsonValueString>(V));
		return Out;
	}

	/** Validate a `rows` array against a table: indices in range, returned de-duplicated and sorted. */
	bool MCPChooserAuthRows(const TArray<TSharedPtr<FJsonValue>>* In, const UChooserTable* Table, TArray<int32>& Out, FString& OutError)
	{
		Out.Reset();
		if (!In)
		{
			OutError = TEXT("rows must be an array of row indices");
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *In)
		{
			double Number = 0.0;
			if (!Value.IsValid() || !Value->TryGetNumber(Number))
			{
				OutError = TEXT("rows must be an array of row indices");
				return false;
			}
			const int32 Row = static_cast<int32>(Number);
			if (!Table->ResultsStructs.IsValidIndex(Row))
			{
				OutError = FString::Printf(TEXT("row index %d out of range (rowCount=%d)"), Row, Table->ResultsStructs.Num());
				return false;
			}
			Out.AddUnique(Row);
		}
		Out.Sort();
		return true;
	}

	/** Recompile and save a table after an edit and stamp the result. */
	TSharedPtr<FJsonValue> MCPChooserAuthFinish(UChooserTable* Table, const TSharedPtr<FJsonObject>& Res)
	{
		FString SaveError;
		const bool bSaved = MCPChooserFinalize(Table, SaveError);
		MCPSetUpdated(Res);
		Res->SetStringField(TEXT("path"), Table->GetPathName());
		MCPNoteSaveOutcome(Res, MCPChooserRoot(Table)->GetPathName(), bSaved, SaveError);
		return MCPResult(Res);
	}

	/** The object that holds the parameters a chooser evaluates with, following inherited signatures. */
	UObject* MCPChooserAuthResolvedSignature(UObject* Owner)
	{
		UObject* Current = Owner;
		for (int32 Depth = 0; Current && Depth < 8; ++Depth)
		{
			UObject* Next = MCPChooserInheritedSignature(Current);
			if (!Next || Next == Owner) break;
			Current = Next;
		}
		return Current;
	}

	/** Load a chooser table or a chooser signature asset, the two things that carry a signature. */
	UObject* MCPChooserAuthLoadSignature(const FString& Path, const FString& Nested, TSharedPtr<FJsonValue>& OutError)
	{
		if (!Nested.IsEmpty() || Path.Contains(TEXT(":")))
		{
			return MCPChooserResolve(Path, Nested, OutError);
		}
		UObject* Obj = MCPLoadAssetObject(Path);
		if (!Obj)
		{
			OutError = MCPAssetLoadError(Path, TEXT("ChooserTable or ChooserSignature"));
			return nullptr;
		}
		if (!MCPChooserContextData(Obj))
		{
			OutError = MCPError(FString::Printf(TEXT("%s is a %s, not a ChooserTable or ChooserSignature"), *Path, *Obj->GetClass()->GetName()));
			return nullptr;
		}
		return Obj;
	}

	/** A signature inherited from another asset is edited there, not here. */
	bool MCPChooserAuthLocalSignature(UObject* Owner, FString& OutError)
	{
		if (UObject* Inherited = MCPChooserInheritedSignature(Owner))
		{
			OutError = FString::Printf(TEXT("The signature is inherited from '%s'. Edit that asset instead, or clear the reference with chooser(set_signature, signatureAsset='')."),
				*Inherited->GetPathName());
			return false;
		}
		return true;
	}

	/** Persist a signature edit. A table recompiles itself and its nested choosers, whose bindings index its parameters. */
	TSharedPtr<FJsonValue> MCPChooserAuthFinishSignature(UObject* Owner, const TSharedPtr<FJsonObject>& Res)
	{
		FString SaveError;
		bool bSaved = false;
		if (UChooserTable* Table = Cast<UChooserTable>(Owner))
		{
			for (UObject* Obj : MCPChooserNestedObjects(Table))
			{
				if (UChooserTable* NestedTable = Cast<UChooserTable>(Obj)) NestedTable->Compile(true);
			}
			bSaved = MCPChooserFinalize(Table, SaveError);
		}
		else
		{
			Owner->PostEditChange();
			bSaved = SaveAssetPackageChecked(Owner, SaveError);
		}
		MCPSetUpdated(Res);
		Res->SetStringField(TEXT("path"), Owner->GetPathName());
		MCPNoteSaveOutcome(Res, Owner->GetPathName(), bSaved, SaveError);
		return MCPResult(Res);
	}

	/** A class or struct parameter's type (FContextObjectTypeClass::Class / FContextObjectTypeStruct::Struct). */
	const UStruct* MCPChooserAuthParameterType(const FInstancedStruct& Param, bool& bOutIsStruct)
	{
		bOutIsStruct = false;
		const UScriptStruct* SS = Param.GetScriptStruct();
		if (!SS || !Param.GetMemory()) return nullptr;
		if (FObjectPropertyBase* ClassProp = FindFProperty<FObjectPropertyBase>(SS, TEXT("Class")))
		{
			return Cast<UStruct>(ClassProp->GetObjectPropertyValue_InContainer(Param.GetMemory()));
		}
		if (FObjectPropertyBase* StructProp = FindFProperty<FObjectPropertyBase>(SS, TEXT("Struct")))
		{
			bOutIsStruct = true;
			return Cast<UStruct>(StructProp->GetObjectPropertyValue_InContainer(Param.GetMemory()));
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> MCPChooserAuthDescribeParameter(int32 Index, const FInstancedStruct& Param, UObject* Owner)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("index"), Index);
		const UScriptStruct* SS = Param.GetScriptStruct();
		Obj->SetStringField(TEXT("parameterType"), SS ? SS->GetPathName() : FString());
		bool bIsStruct = false;
		if (const UStruct* Type = MCPChooserAuthParameterType(Param, bIsStruct))
		{
			Obj->SetStringField(bIsStruct ? TEXT("struct") : TEXT("class"), Type->GetPathName());
		}
		if (SS && Param.GetMemory())
		{
			FString Text;
			SS->ExportText(Text, Param.GetMemory(), nullptr, Owner, PPF_None, nullptr);
			Obj->SetStringField(TEXT("value"), Text);
		}
		return Obj;
	}

	/** The struct a nested chooser result uses (FNestedChooser), null on an engine without one. */
	const UScriptStruct* MCPChooserAuthNestedResultStruct()
	{
		return FindObject<UScriptStruct>(nullptr, TEXT("/Script/Chooser.NestedChooser"));
	}

	/** The nested chooser a result points at, when it is an FNestedChooser. */
	UChooserTable* MCPChooserAuthNestedRef(const FInstancedStruct& Result, FObjectPropertyBase** OutProp = nullptr)
	{
		const UScriptStruct* NestedStruct = MCPChooserAuthNestedResultStruct();
		const UScriptStruct* SS = Result.GetScriptStruct();
		if (!NestedStruct || !SS || !SS->IsChildOf(NestedStruct) || !Result.GetMemory()) return nullptr;
		FObjectPropertyBase* Prop = FindFProperty<FObjectPropertyBase>(SS, TEXT("Chooser"));
		if (!Prop) return nullptr;
		if (OutProp) *OutProp = Prop;
		return Cast<UChooserTable>(Prop->GetObjectPropertyValue_InContainer(Result.GetMemory()));
	}

	void MCPChooserAuthDuplicateNestedResults(TArrayView<FInstancedStruct> Results, UChooserTable* NewOuter, TMap<UChooserTable*, UChooserTable*>& Done);

	/** Deep copy a nested chooser into NewOuter's asset, keeping its name when free,
	 *  as the editor's duplicate and paste do. Choosers nested inside it are copied too. */
	UChooserTable* MCPChooserAuthDuplicateNested(UChooserTable* Source, UChooserTable* NewOuter, TMap<UChooserTable*, UChooserTable*>& Done)
	{
		if (UChooserTable** Already = Done.Find(Source)) return *Already;
		UChooserTable* Root = MCPChooserRoot(NewOuter);
		const FName NewName = StaticFindObjectFast(nullptr, NewOuter, Source->GetFName())
			? MakeUniqueObjectName(NewOuter, UChooserTable::StaticClass(), Source->GetFName())
			: Source->GetFName();
		UChooserTable* Copy = NewObject<UChooserTable>(NewOuter, UChooserTable::StaticClass(), NewName, RF_Transactional);
		Done.Add(Source, Copy);
		MCPChooserSetRoot(Copy, Root);
		MCPChooserAddNestedObject(Root, Copy);
		Copy->ResultsStructs = Source->ResultsStructs;
#if UE_MCP_HAS_5_5_API
		Copy->DisabledRows = Source->DisabledRows;
#endif
		Copy->FallbackResult = Source->FallbackResult;
		MCPChooserAuthDuplicateNestedResults(Copy->ResultsStructs, Copy, Done);
		MCPChooserAuthDuplicateNestedResults(TArrayView<FInstancedStruct>(&Copy->FallbackResult, 1), Copy, Done);
		Copy->ColumnsStructs = Source->ColumnsStructs;
		Copy->Compile(true);
		return Copy;
	}

	void MCPChooserAuthDuplicateNestedResults(TArrayView<FInstancedStruct> Results, UChooserTable* NewOuter, TMap<UChooserTable*, UChooserTable*>& Done)
	{
		for (FInstancedStruct& Result : Results)
		{
			FObjectPropertyBase* Prop = nullptr;
			if (UChooserTable* Nested = MCPChooserAuthNestedRef(Result, &Prop))
			{
				Prop->SetObjectPropertyValue_InContainer(Result.GetMutableMemory(), MCPChooserAuthDuplicateNested(Nested, NewOuter, Done));
			}
		}
	}

	/** The column key the editor's paste matches columns by: type plus input name. */
	FString MCPChooserAuthColumnKey(FInstancedStruct& ColumnData)
	{
		const UScriptStruct* SS = ColumnData.GetScriptStruct();
		FString Key = SS ? SS->GetPathName() : FString();
		if (FChooserColumnBase* Column = ColumnData.GetMutablePtr<FChooserColumnBase>())
		{
			if (FChooserParameterBase* Input = Column->GetInputValue()) Key += TEXT("|") + Input->GetDebugName();
		}
		return Key;
	}

	/** A copy of a column with every cell back at its default, sized to NumRows. */
	FInstancedStruct MCPChooserAuthEmptyColumnCopy(const FInstancedStruct& Source, int32 NumRows)
	{
		FInstancedStruct Copy = Source;
		if (FChooserColumnBase* Column = Copy.GetMutablePtr<FChooserColumnBase>())
		{
			Column->SetNumRows(0);
			Column->SetNumRows(NumRows);
		}
		return Copy;
	}

	/** Resolve the property a get_property/set_property request names, rooted on a column, a row result,
	 *  the fallback result or a parameter. */
	bool MCPChooserAuthResolvePath(UObject* Root, const FString& Prefix, const FString& PropertyPath,
		FProperty*& OutProp, void*& OutAddr, UObject*& OutOwner, FString& OutError)
	{
		const FString Trimmed = PropertyPath.TrimStartAndEnd();
		const FString FullPath = Trimmed.IsEmpty() ? Prefix : Prefix + TEXT(".") + Trimmed;
		return MCPJsonProperty::ResolveDottedPath(Root, FullPath, OutProp, OutAddr, OutOwner, OutError);
	}

	/** Cell storage for one row (or MCPChooserFallbackIndex) of a column. */
	bool MCPChooserAuthCell(FInstancedStruct& ColumnData, int32 Row, FProperty*& OutProp, void*& OutAddr, FString& OutError)
	{
		if (Row == MCPChooserFallbackIndex)
		{
			FProperty* Prop = MCPChooserFallbackValueProperty(ColumnData);
			if (!Prop)
			{
				OutError = TEXT("this column is not an output column, so it has no fallback value");
				return false;
			}
			OutProp = Prop;
			OutAddr = Prop->ContainerPtrToValuePtr<void>(ColumnData.GetMutableMemory());
			return true;
		}
		FArrayProperty* Arr = nullptr; void* Data = nullptr;
		if (!MCPChooserRowValuesArray(ColumnData, Arr, Data, OutError)) return false;
		FScriptArrayHelper Helper(Arr, Arr->ContainerPtrToValuePtr<void>(Data));
		if (!Helper.IsValidIndex(Row))
		{
			OutError = FString::Printf(TEXT("row %d has no cell in this column (the column holds %d)"), Row, Helper.Num());
			return false;
		}
		OutProp = Arr->Inner;
		OutAddr = Helper.GetRawPtr(Row);
		return true;
	}

	FString MCPChooserAuthCellType(const FProperty* Prop)
	{
		if (const FStructProperty* SP = CastField<const FStructProperty>(Prop)) return SP->Struct ? SP->Struct->GetName() : FString();
		return Prop ? Prop->GetCPPType() : FString();
	}

	/** Find a nested object of a root table by name or path. */
	UObject* MCPChooserAuthFindNested(UChooserTable* Root, const FString& NameOrPath)
	{
		for (UObject* Obj : MCPChooserNestedObjects(Root))
		{
			if (Obj->GetName() == NameOrPath || Obj->GetPathName() == NameOrPath) return Obj;
		}
		return nullptr;
	}

	/** Validate a binding chain under the editor's rules: each step is a blueprint
	 *  visible property, stepping through struct and object properties, or a
	 *  blueprint callable function with no parameters that returns an object.
	 *  Fills the display name the editor writes. */
	bool MCPChooserAuthCheckChain(const UStruct* ContextType, const TArray<FName>& Chain, FString& OutDisplayName, FString& OutError)
	{
		const UStruct* Current = ContextType;
		OutDisplayName = ContextType ? ContextType->GetName() : FString();
#if WITH_EDITOR
		if (ContextType) OutDisplayName = ContextType->GetDisplayNameText().ToString();
#endif
		for (int32 i = 0; i < Chain.Num(); ++i)
		{
			if (!Current)
			{
				OutError = FString::Printf(TEXT("'%s' cannot be followed: the previous step is not a struct or object"), *Chain[i].ToString());
				return false;
			}
			const bool bLast = i == Chain.Num() - 1;
			if (FProperty* Prop = Current->FindPropertyByName(Chain[i]))
			{
				if (!Prop->HasAnyPropertyFlags(CPF_BlueprintVisible))
				{
					OutError = FString::Printf(TEXT("'%s' on %s is not blueprint visible, so a chooser cannot bind to it"), *Chain[i].ToString(), *Current->GetName());
					return false;
				}
#if WITH_EDITOR
				OutDisplayName = Prop->GetDisplayNameText().ToString();
#else
				OutDisplayName = Prop->GetName();
#endif
				if (FStructProperty* SP = CastField<FStructProperty>(Prop)) Current = SP->Struct;
				else if (FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(Prop)) Current = OP->PropertyClass;
				else if (!bLast)
				{
					OutError = FString::Printf(TEXT("'%s' is not a struct or object property, so the chain cannot continue past it"), *Chain[i].ToString());
					return false;
				}
				else Current = nullptr;
				continue;
			}
			const UClass* AsClass = Cast<UClass>(Current);
			UFunction* Function = AsClass ? AsClass->FindFunctionByName(Chain[i]) : nullptr;
			if (Function)
			{
				FObjectPropertyBase* ReturnProp = CastField<FObjectPropertyBase>(Function->GetReturnProperty());
				bool bHasParams = false;
				for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
				{
					if (!It->HasAnyPropertyFlags(CPF_ReturnParm)) bHasParams = true;
				}
				if (!Function->HasAnyFunctionFlags(FUNC_BlueprintCallable) || bHasParams || (!ReturnProp && !bLast))
				{
					OutError = FString::Printf(TEXT("'%s' on %s must be a blueprint callable function that takes no parameters and returns an object"), *Chain[i].ToString(), *Current->GetName());
					return false;
				}
				OutDisplayName = Function->GetName();
				Current = ReturnProp ? ReturnProp->PropertyClass : nullptr;
				continue;
			}
			OutError = FString::Printf(TEXT("%s has no property or function named '%s'"), *Current->GetName(), *Chain[i].ToString());
			return false;
		}
		return true;
	}
}

// ─── Types ────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FChooserHandlers::ListTypes(const TSharedPtr<FJsonObject>& Params)
{
	FString Kind;
	const TSharedPtr<FJsonValue> KindErr = RequireString(Params, TEXT("kind"), Kind);
	const FString TablePath = OptionalString(Params, TEXT("table"));
	const FString Nested = OptionalString(Params, TEXT("nested"));
	int32 ColumnIndex = INDEX_NONE;
	const bool bHasColumn = TryGetNumberParam(Params, TEXT("column"), ColumnIndex);
	if (KindErr) return KindErr;

	const UScriptStruct* Base = nullptr;
	if (Kind == TEXT("column")) Base = FChooserColumnBase::StaticStruct();
	else if (Kind == TEXT("result")) Base = FObjectChooserBase::StaticStruct();
	else if (Kind == TEXT("parameter")) Base = MCPChooserParameterBaseStruct();
	else if (Kind == TEXT("input"))
	{
		if (TablePath.IsEmpty() || !bHasColumn)
		{
			return MCPError(TEXT("kind 'input' lists what one column accepts, so it needs table and column"));
		}
		TSharedPtr<FJsonValue> ResolveError;
		UChooserTable* Table = MCPChooserResolve(TablePath, Nested, ResolveError);
		if (!Table) return ResolveError;
		FString ColumnError;
		FChooserColumnBase* Column = MCPChooserColumn(Table, ColumnIndex, ColumnError);
		if (!Column) return MCPError(ColumnError);
		Base = Column->GetInputBaseType();
		if (!Base) return MCPError(FString::Printf(TEXT("column %d has no input"), ColumnIndex));
	}
	else
	{
		return MCPError(FString::Printf(TEXT("unknown kind '%s': column, result, parameter or input"), *Kind));
	}
	if (!Base) return MCPError(FString::Printf(TEXT("the %s base type is not loaded on this engine"), *Kind));

	const TArray<FString> Types = MCPChooserAvailableTypes(Base);
	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("kind"), Kind);
	Res->SetStringField(TEXT("baseType"), Base->GetPathName());
	Res->SetNumberField(TEXT("count"), Types.Num());
	Res->SetArrayField(TEXT("types"), MCPChooserAuthStrings(Types));
	return MCPResult(Res);
}

// ─── Signature ────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FChooserHandlers::GetSignature(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	if (TableErr) return TableErr;

	TSharedPtr<FJsonValue> LoadError;
	UObject* Asset = MCPChooserAuthLoadSignature(TablePath, Nested, LoadError);
	if (!Asset) return LoadError;
	UObject* Owner = MCPChooserSignatureOwner(Asset);
	UObject* Resolved = MCPChooserAuthResolvedSignature(Owner);
	UObject* Inherited = MCPChooserInheritedSignature(Owner);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("path"), Asset->GetPathName());
	Res->SetStringField(TEXT("owner"), Owner->GetPathName());
	Res->SetStringField(TEXT("signatureAsset"), Inherited ? Inherited->GetPathName() : FString());
	Res->SetBoolField(TEXT("usingLocalSignature"), Resolved == Owner);
	Res->SetStringField(TEXT("resultType"), MCPChooserGetResultTypeName(Resolved));
	FObjectPropertyBase* ClassProp = MCPChooserResultClassProperty(Resolved);
	UObject* ResultClass = ClassProp ? ClassProp->GetObjectPropertyValue_InContainer(Resolved) : nullptr;
	Res->SetStringField(TEXT("resultClass"), ResultClass ? ResultClass->GetPathName() : FString());
	TArray<TSharedPtr<FJsonValue>> ParamsJson;
	if (TArray<FInstancedStruct>* ContextData = MCPChooserContextData(Resolved))
	{
		for (int32 i = 0; i < ContextData->Num(); ++i)
		{
			ParamsJson.Add(MakeShared<FJsonValueObject>(MCPChooserAuthDescribeParameter(i, (*ContextData)[i], Resolved)));
		}
	}
	Res->SetNumberField(TEXT("parameterCount"), ParamsJson.Num());
	Res->SetArrayField(TEXT("parameters"), ParamsJson);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FChooserHandlers::SetSignature(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	FString ResultType, ResultClassPath, SignaturePath;
	const bool bHasResultType = TryGetStringParam(Params, TEXT("resultType"), ResultType);
	const bool bHasResultClass = TryGetStringParam(Params, TEXT("resultClass"), ResultClassPath);
	const bool bHasSignature = TryGetStringParam(Params, TEXT("signatureAsset"), SignaturePath);
	if (TableErr) return TableErr;
	if (!bHasResultType && !bHasResultClass && !bHasSignature)
	{
		return MCPError(TEXT("Nothing to set: pass resultType, resultClass and/or signatureAsset"));
	}

	TSharedPtr<FJsonValue> LoadError;
	UObject* Asset = MCPChooserAuthLoadSignature(TablePath, Nested, LoadError);
	if (!Asset) return LoadError;
	UObject* Owner = MCPChooserSignatureOwner(Asset);

	// Validate everything before anything is written.
	FObjectPropertyBase* SignatureProp = FindFProperty<FObjectPropertyBase>(Owner->GetClass(), TEXT("Signature"));
	UObject* NewSignature = nullptr;
	if (bHasSignature)
	{
		if (!SignatureProp)
		{
			return MCPError(TEXT("Inheriting a signature from a ChooserSignature asset needs UE 5.9; this engine's choosers carry their own result type, result class and parameters only"));
		}
		if (!SignaturePath.IsEmpty())
		{
			NewSignature = MCPLoadAssetObject(SignaturePath);
			if (!NewSignature) return MCPAssetLoadError(SignaturePath, TEXT("ChooserSignature"));
			if (Cast<UChooserTable>(NewSignature) || !NewSignature->IsA(SignatureProp->PropertyClass))
			{
				return MCPError(FString::Printf(TEXT("%s is not a ChooserSignature asset; a ChooserTable cannot be a signature asset"), *SignaturePath));
			}
			UObject* Link = NewSignature;
			for (int32 Depth = 0; Link && Depth < 16; ++Depth, Link = MCPChooserInheritedSignature(Link))
			{
				if (Link == Owner) return MCPError(FString::Printf(TEXT("Referencing '%s' would create a circular signature reference"), *SignaturePath));
			}
		}
	}
	const bool bWillInherit = bHasSignature ? NewSignature != nullptr : MCPChooserInheritedSignature(Owner) != nullptr;
	if ((bHasResultType || bHasResultClass) && bWillInherit)
	{
		FString Err;
		MCPChooserAuthLocalSignature(Owner, Err);
		return MCPError(Err.IsEmpty() ? TEXT("The signature is inherited from the signatureAsset being set, so resultType and resultClass are read from it") : Err);
	}
	UClass* NewClass = nullptr;
	FObjectPropertyBase* ClassProp = MCPChooserResultClassProperty(Owner);
	if (bHasResultClass)
	{
		if (!ClassProp) return MCPError(TEXT("this asset has no result class"));
		if (!ResultClassPath.IsEmpty())
		{
			NewClass = MCPResolveClass(ResultClassPath);
			if (!NewClass) return MCPClassNotFoundError(ResultClassPath, TEXT("resultClass"));
		}
	}
	if (bHasResultType)
	{
		FProperty* Prop = nullptr; UEnum* Enum = nullptr;
		if (!MCPChooserResultTypeProperty(Owner, Prop, Enum)) return MCPError(TEXT("this asset has no result type"));
		if (Enum->GetValueByNameString(ResultType) == INDEX_NONE)
		{
			return MCPError(FString::Printf(TEXT("invalid result type '%s', must be ObjectResult, ClassResult or NoPrimaryResult"), *ResultType));
		}
	}

	// The prior values make the inverse.
	TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
	RbPayload->SetStringField(TEXT("table"), Owner->GetPathName());
	if (bHasResultType) RbPayload->SetStringField(TEXT("resultType"), MCPChooserGetResultTypeName(Owner));
	if (bHasResultClass)
	{
		UObject* Prior = ClassProp->GetObjectPropertyValue_InContainer(Owner);
		RbPayload->SetStringField(TEXT("resultClass"), Prior ? Prior->GetPathName() : FString());
	}
	if (bHasSignature)
	{
		UObject* Prior = MCPChooserInheritedSignature(Owner);
		RbPayload->SetStringField(TEXT("signatureAsset"), Prior ? Prior->GetPathName() : FString());
	}

	Owner->Modify();
	FString SetError;
	if (bHasResultType && !MCPChooserSetResultTypeName(Owner, ResultType, SetError)) return MCPError(SetError);
	if (bHasResultClass) ClassProp->SetObjectPropertyValue_InContainer(Owner, NewClass);
	if (bHasSignature)
	{
		SignatureProp->SetObjectPropertyValue_InContainer(Owner, NewSignature);
		// Notified as an edit of the Signature property, so the asset rebinds its
		// subscriptions and recompiles what depends on the signature.
		FPropertyChangedEvent Changed(SignatureProp);
		Owner->PostEditChangeProperty(Changed);
	}

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("owner"), Owner->GetPathName());
	UObject* ResolvedOwner = MCPChooserAuthResolvedSignature(Owner);
	Res->SetStringField(TEXT("resultType"), MCPChooserGetResultTypeName(ResolvedOwner));
	FObjectPropertyBase* ResolvedClassProp = MCPChooserResultClassProperty(ResolvedOwner);
	UObject* ResolvedClass = ResolvedClassProp ? ResolvedClassProp->GetObjectPropertyValue_InContainer(ResolvedOwner) : nullptr;
	Res->SetStringField(TEXT("resultClass"), ResolvedClass ? ResolvedClass->GetPathName() : FString());
	UObject* NowInherited = MCPChooserInheritedSignature(Owner);
	Res->SetStringField(TEXT("signatureAsset"), NowInherited ? NowInherited->GetPathName() : FString());
	MCPSetRollback(Res, TEXT("chooser_set_signature"), RbPayload);
	return MCPChooserAuthFinishSignature(Owner, Res);
}

TSharedPtr<FJsonValue> FChooserHandlers::AddParameter(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath, ParameterType;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	const TSharedPtr<FJsonValue> TypeErr = RequireString(Params, TEXT("parameterType"), ParameterType);
	const TSharedPtr<FJsonObject>* Properties = nullptr;
	TryGetObjectParam(Params, TEXT("properties"), Properties);
	if (TableErr) return TableErr;
	if (TypeErr) return TypeErr;

	TSharedPtr<FJsonValue> LoadError;
	UObject* Asset = MCPChooserAuthLoadSignature(TablePath, Nested, LoadError);
	if (!Asset) return LoadError;
	UObject* Owner = MCPChooserSignatureOwner(Asset);
	FString Err;
	if (!MCPChooserAuthLocalSignature(Owner, Err)) return MCPError(Err);
	TArray<FInstancedStruct>* ContextData = MCPChooserContextData(Owner);
	if (!ContextData) return MCPError(TEXT("this asset has no parameter list"));
	const UScriptStruct* Type = MCPChooserFindType(ParameterType, MCPChooserParameterBaseStruct(), Err);
	if (!Type) return MCPError(Err);

	Owner->Modify();
	const int32 NewIndex = ContextData->Num();
	ContextData->AddDefaulted_GetRef().InitializeAs(Type);

	// Each property is written through the same path resolver set_property uses;
	// a value that does not parse takes the new parameter back out.
	if (Properties && Properties->IsValid())
	{
		for (const auto& Pair : (*Properties)->Values)
		{
			const FString Text = MCPChooserJsonToText(Pair.Value);
			FProperty* Prop = nullptr; void* Addr = nullptr; UObject* PropOwner = nullptr; FString PathError;
			if (!MCPChooserAuthResolvePath(Owner, FString::Printf(TEXT("ContextData[%d]"), NewIndex), Pair.Key, Prop, Addr, PropOwner, PathError)
				|| !MCPChooserImportValidated(Prop, Addr, Text, PropOwner, PathError))
			{
				ContextData->RemoveAt(NewIndex);
				return MCPError(FString::Printf(TEXT("properties.%s: %s. The parameter was not added"), *Pair.Key, *PathError));
			}
		}
	}

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetNumberField(TEXT("index"), NewIndex);
	Res->SetObjectField(TEXT("parameter"), MCPChooserAuthDescribeParameter(NewIndex, (*ContextData)[NewIndex], Owner));
	TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
	RbPayload->SetStringField(TEXT("table"), Owner->GetPathName());
	RbPayload->SetNumberField(TEXT("index"), NewIndex);
	MCPSetRollback(Res, TEXT("chooser_remove_parameter"), RbPayload);
	return MCPChooserAuthFinishSignature(Owner, Res);
}

TSharedPtr<FJsonValue> FChooserHandlers::RemoveParameter(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	int32 Index = INDEX_NONE;
	const TSharedPtr<FJsonValue> IndexErr = RequireNumber(Params, TEXT("index"), Index);
	if (TableErr) return TableErr;
	if (IndexErr) return IndexErr;

	TSharedPtr<FJsonValue> LoadError;
	UObject* Asset = MCPChooserAuthLoadSignature(TablePath, Nested, LoadError);
	if (!Asset) return LoadError;
	UObject* Owner = MCPChooserSignatureOwner(Asset);
	FString Err;
	if (!MCPChooserAuthLocalSignature(Owner, Err)) return MCPError(Err);
	TArray<FInstancedStruct>* ContextData = MCPChooserContextData(Owner);
	if (!ContextData) return MCPError(TEXT("this asset has no parameter list"));
	if (!ContextData->IsValidIndex(Index))
	{
		return MCPError(FString::Printf(TEXT("parameter index %d out of range (parameterCount=%d)"), Index, ContextData->Num()));
	}

	// The inverse re-adds a parameter of the same type with its top-level fields.
	const FInstancedStruct& Doomed = (*ContextData)[Index];
	TSharedPtr<FJsonObject> Removed = MCPChooserAuthDescribeParameter(Index, Doomed, Owner);
	TSharedPtr<FJsonObject> RbProps = MakeShared<FJsonObject>();
	if (const UScriptStruct* SS = Doomed.GetScriptStruct())
	{
		for (TFieldIterator<FProperty> It(SS); It; ++It)
		{
			FString Text;
			It->ExportTextItem_Direct(Text, It->ContainerPtrToValuePtr<void>(Doomed.GetMemory()), nullptr, Owner, PPF_None);
			RbProps->SetStringField(It->GetName(), Text);
		}
	}

	Owner->Modify();
	ContextData->RemoveAt(Index);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetObjectField(TEXT("removed"), Removed);
	Res->SetNumberField(TEXT("parameterCount"), ContextData->Num());
	TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
	RbPayload->SetStringField(TEXT("table"), Owner->GetPathName());
	RbPayload->SetStringField(TEXT("parameterType"), Removed->GetStringField(TEXT("parameterType")));
	RbPayload->SetObjectField(TEXT("properties"), RbProps);
	MCPSetRollback(Res, TEXT("chooser_add_parameter"), RbPayload);
	Res->SetBoolField(TEXT("rollbackLossy"), Index != ContextData->Num());
	Res->SetStringField(TEXT("rollbackNote"), FString::Printf(
		TEXT("chooser(add_parameter) appends, so the restored parameter is index %d rather than %d. Column bindings name parameters by index, and the engine does not renumber them on removal, so bindings to parameters after %d now point one later than they did, before and after the rollback alike."),
		ContextData->Num(), Index, Index));
	return MCPChooserAuthFinishSignature(Owner, Res);
}

// ─── Property paths ───────────────────────────────────────────────────────

namespace
{
	/** Work out the root object and path prefix a property target names. */
	bool MCPChooserAuthPropertyRoot(const FString& TablePath, const FString& Nested, const FString& Target, int32 Index, bool bHasIndex, bool bForEdit,
		UObject*& OutRoot, UChooserTable*& OutTable, FString& OutPrefix, TSharedPtr<FJsonValue>& OutError)
	{
		OutTable = nullptr;
		if (Target == TEXT("parameter"))
		{
			UObject* Asset = MCPChooserAuthLoadSignature(TablePath, Nested, OutError);
			if (!Asset) return false;
			UObject* Owner = MCPChooserSignatureOwner(Asset);
			FString Err;
			if (bForEdit && !MCPChooserAuthLocalSignature(Owner, Err)) { OutError = MCPError(Err); return false; }
			OutRoot = bForEdit ? Owner : MCPChooserAuthResolvedSignature(Owner);
			TArray<FInstancedStruct>* ContextData = MCPChooserContextData(OutRoot);
			if (!ContextData) { OutError = MCPError(TEXT("this asset has no parameter list")); return false; }
			if (!bHasIndex || !ContextData->IsValidIndex(Index))
			{
				OutError = MCPError(FString::Printf(TEXT("target 'parameter' needs a valid index (parameterCount=%d)"), ContextData->Num()));
				return false;
			}
			OutPrefix = FString::Printf(TEXT("ContextData[%d]"), Index);
			return true;
		}
		UChooserTable* Table = MCPChooserResolve(TablePath, Nested, OutError);
		if (!Table) return false;
		OutTable = Table;
		OutRoot = Table;
		if (Target == TEXT("column"))
		{
			FString Err;
			if (!bHasIndex || !MCPChooserColumn(Table, Index, Err))
			{
				OutError = MCPError(bHasIndex ? Err : FString(TEXT("target 'column' needs index")));
				return false;
			}
			OutPrefix = FString::Printf(TEXT("ColumnsStructs[%d]"), Index);
			return true;
		}
		if (Target == TEXT("row"))
		{
			if (!bHasIndex || !Table->ResultsStructs.IsValidIndex(Index))
			{
				OutError = MCPError(FString::Printf(TEXT("target 'row' needs a valid index (rowCount=%d)"), Table->ResultsStructs.Num()));
				return false;
			}
			OutPrefix = FString::Printf(TEXT("ResultsStructs[%d]"), Index);
			return true;
		}
		if (Target == TEXT("fallback"))
		{
			if (!Table->FallbackResult.IsValid())
			{
				OutError = MCPError(TEXT("the chooser has no fallback result; add one with chooser(set_fallback)"));
				return false;
			}
			OutPrefix = TEXT("FallbackResult");
			return true;
		}
		OutError = MCPError(FString::Printf(TEXT("unknown target '%s': column, row, fallback or parameter"), *Target));
		return false;
	}
}

TSharedPtr<FJsonValue> FChooserHandlers::GetProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath, Target;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	const TSharedPtr<FJsonValue> TargetErr = RequireString(Params, TEXT("target"), Target);
	int32 Index = INDEX_NONE;
	const bool bHasIndex = TryGetNumberParam(Params, TEXT("index"), Index);
	const FString PropertyPath = OptionalString(Params, TEXT("propertyPath"));
	if (TableErr) return TableErr;
	if (TargetErr) return TargetErr;

	UObject* Root = nullptr; UChooserTable* Table = nullptr; FString Prefix; TSharedPtr<FJsonValue> RootError;
	if (!MCPChooserAuthPropertyRoot(TablePath, Nested, Target, Index, bHasIndex, false, Root, Table, Prefix, RootError)) return RootError;

	FProperty* Prop = nullptr; void* Addr = nullptr; UObject* PropOwner = nullptr; FString PathError;
	if (!MCPChooserAuthResolvePath(Root, Prefix, PropertyPath, Prop, Addr, PropOwner, PathError)) return MCPError(PathError);

	FString Value;
	Prop->ExportTextItem_Direct(Value, Addr, nullptr, PropOwner, PPF_None);
	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("target"), Target);
	if (Target != TEXT("fallback")) Res->SetNumberField(TEXT("index"), Index);
	Res->SetStringField(TEXT("propertyPath"), PropertyPath);
	Res->SetStringField(TEXT("value"), Value);
	Res->SetStringField(TEXT("cppType"), Prop->GetCPPType());
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FChooserHandlers::SetProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath, Target, Value;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	const TSharedPtr<FJsonValue> TargetErr = RequireString(Params, TEXT("target"), Target);
	int32 Index = INDEX_NONE;
	const bool bHasIndex = TryGetNumberParam(Params, TEXT("index"), Index);
	const FString PropertyPath = OptionalString(Params, TEXT("propertyPath"));
	const TSharedPtr<FJsonValue> RawValue = TryGetParam(Params, TEXT("value"));
	if (TableErr) return TableErr;
	if (TargetErr) return TargetErr;
	if (!RawValue.IsValid() || RawValue->Type == EJson::Null) return MCPError(TEXT("Missing required parameter 'value'"));
	Value = MCPChooserJsonToText(RawValue);

	UObject* Root = nullptr; UChooserTable* Table = nullptr; FString Prefix; TSharedPtr<FJsonValue> RootError;
	if (!MCPChooserAuthPropertyRoot(TablePath, Nested, Target, Index, bHasIndex, true, Root, Table, Prefix, RootError)) return RootError;

	FProperty* Prop = nullptr; void* Addr = nullptr; UObject* PropOwner = nullptr; FString PathError;
	if (!MCPChooserAuthResolvePath(Root, Prefix, PropertyPath, Prop, Addr, PropOwner, PathError)) return MCPError(PathError);

	FString Prior;
	Prop->ExportTextItem_Direct(Prior, Addr, nullptr, PropOwner, PPF_None);
	Root->Modify();
	FString ImportError;
	if (!MCPChooserImportValidated(Prop, Addr, Value, PropOwner, ImportError))
	{
		return MCPError(FString::Printf(TEXT("'%s' is not a valid %s for %s: %s. Nothing was changed"), *Value, *Prop->GetCPPType(), *Prefix, *ImportError));
	}
	FString Now;
	Prop->ExportTextItem_Direct(Now, Addr, nullptr, PropOwner, PPF_None);

	if (Table)
	{
		const FString Trimmed = PropertyPath.TrimStartAndEnd();
		if (Target == TEXT("column") && (Trimmed.IsEmpty() || Trimmed.StartsWith(TEXT("InputValue"))))
		{
			MCPChooserInputChanged(Table->ColumnsStructs[Index].GetMutablePtr<FChooserColumnBase>());
		}
		// The editor fills auto populating columns when a result changes.
		else if (Target == TEXT("row")) MCPChooserAutoPopulateRow(Table, Index);
		else if (Target == TEXT("fallback")) MCPChooserAutoPopulateRow(Table, MCPChooserFallbackIndex);
	}

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("target"), Target);
	Res->SetStringField(TEXT("propertyPath"), PropertyPath);
	Res->SetStringField(TEXT("previousValue"), Prior);
	Res->SetStringField(TEXT("value"), Now);
	TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
	RbPayload->SetStringField(TEXT("table"), Table ? Table->GetPathName() : Root->GetPathName());
	RbPayload->SetStringField(TEXT("target"), Target);
	if (bHasIndex) RbPayload->SetNumberField(TEXT("index"), Index);
	RbPayload->SetStringField(TEXT("propertyPath"), PropertyPath);
	RbPayload->SetStringField(TEXT("value"), Prior);
	MCPSetRollback(Res, TEXT("chooser_set_property"), RbPayload);
	if (Table) return MCPChooserAuthFinish(Table, Res);
	return MCPChooserAuthFinishSignature(Root, Res);
}

// ─── Columns ──────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FChooserHandlers::DeleteColumn(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	int32 ColumnIndex = INDEX_NONE;
	const TSharedPtr<FJsonValue> ColumnErr = RequireNumber(Params, TEXT("column"), ColumnIndex);
	if (TableErr) return TableErr;
	if (ColumnErr) return ColumnErr;

	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Table = MCPChooserResolve(TablePath, Nested, ResolveError);
	if (!Table) return ResolveError;
	if (!Table->ColumnsStructs.IsValidIndex(ColumnIndex))
	{
		return MCPError(FString::Printf(TEXT("column index %d out of range (columnCount=%d)"), ColumnIndex, Table->ColumnsStructs.Num()));
	}

	const UScriptStruct* SS = Table->ColumnsStructs[ColumnIndex].GetScriptStruct();
	const FString DeletedType = SS ? SS->GetName() : FString();
	const FString DeletedName = MCPChooserColumnName(Table->ColumnsStructs[ColumnIndex], ColumnIndex);
	Table->Modify();
	Table->ColumnsStructs.RemoveAt(ColumnIndex);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetNumberField(TEXT("deletedColumn"), ColumnIndex);
	Res->SetStringField(TEXT("deletedColumnType"), DeletedType);
	Res->SetStringField(TEXT("deletedColumnName"), DeletedName);
	Res->SetNumberField(TEXT("columnCount"), Table->ColumnsStructs.Num());
	MCPSetNoRollback(Res, FString::Printf(
		TEXT("Column %d (%s) took its cell for every row with it. chooser(add_column) adds an empty column of that type, so restoring it means re-adding it and setting each cell again, or reverting the saved asset in source control."),
		ColumnIndex, *DeletedType));
	return MCPChooserAuthFinish(Table, Res);
}

TSharedPtr<FJsonValue> FChooserHandlers::MoveColumn(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	int32 ColumnIndex = INDEX_NONE, NewIndex = INDEX_NONE;
	const TSharedPtr<FJsonValue> ColumnErr = RequireNumber(Params, TEXT("column"), ColumnIndex);
	const TSharedPtr<FJsonValue> NewIndexErr = RequireNumber(Params, TEXT("newIndex"), NewIndex);
	if (TableErr) return TableErr;
	if (ColumnErr) return ColumnErr;
	if (NewIndexErr) return NewIndexErr;

	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Table = MCPChooserResolve(TablePath, Nested, ResolveError);
	if (!Table) return ResolveError;
	FString Err;
	if (!MCPChooserColumn(Table, ColumnIndex, Err)) return MCPError(Err);
	if (MCPChooserIsRandomizeColumn(Table->ColumnsStructs[ColumnIndex]))
	{
		return MCPError(TEXT("A randomize column must be the last column, and cannot be moved"));
	}
	if (!Table->ColumnsStructs.IsValidIndex(NewIndex))
	{
		return MCPError(FString::Printf(TEXT("newIndex %d out of range (columnCount=%d)"), NewIndex, Table->ColumnsStructs.Num()));
	}
	// Nothing goes after a randomize column.
	const int32 FinalIndex = MCPChooserHasRandomizeColumn(Table) ? FMath::Min(NewIndex, Table->ColumnsStructs.Num() - 2) : NewIndex;

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	if (FinalIndex != ColumnIndex)
	{
		Table->Modify();
		FInstancedStruct Moving = MoveTemp(Table->ColumnsStructs[ColumnIndex]);
		Table->ColumnsStructs.RemoveAt(ColumnIndex);
		Table->ColumnsStructs.Insert(MoveTemp(Moving), FinalIndex);
	}
	Res->SetNumberField(TEXT("columnIndex"), FinalIndex);
	Res->SetNumberField(TEXT("previousIndex"), ColumnIndex);
	TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
	RbPayload->SetStringField(TEXT("table"), Table->GetPathName());
	RbPayload->SetNumberField(TEXT("column"), FinalIndex);
	RbPayload->SetNumberField(TEXT("newIndex"), ColumnIndex);
	MCPSetRollback(Res, TEXT("chooser_move_column"), RbPayload);
	return MCPChooserAuthFinish(Table, Res);
}

TSharedPtr<FJsonValue> FChooserHandlers::DuplicateColumn(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	int32 ColumnIndex = INDEX_NONE;
	const TSharedPtr<FJsonValue> ColumnErr = RequireNumber(Params, TEXT("column"), ColumnIndex);
	if (TableErr) return TableErr;
	if (ColumnErr) return ColumnErr;

	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Table = MCPChooserResolve(TablePath, Nested, ResolveError);
	if (!Table) return ResolveError;
	FString Err;
	if (!MCPChooserColumn(Table, ColumnIndex, Err)) return MCPError(Err);
	if (MCPChooserIsRandomizeColumn(Table->ColumnsStructs[ColumnIndex]))
	{
		return MCPError(TEXT("A chooser can only have one randomize column"));
	}

	Table->Modify();
	MCPChooserSyncRowCounts(Table);
	FInstancedStruct Copy = Table->ColumnsStructs[ColumnIndex];
	const int32 NewIndex = ColumnIndex + 1;
	Table->ColumnsStructs.Insert(MoveTemp(Copy), NewIndex);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetNumberField(TEXT("columnIndex"), NewIndex);
	Res->SetNumberField(TEXT("columnCount"), Table->ColumnsStructs.Num());
	TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
	RbPayload->SetStringField(TEXT("table"), Table->GetPathName());
	RbPayload->SetNumberField(TEXT("column"), NewIndex);
	MCPSetRollback(Res, TEXT("chooser_delete_column"), RbPayload);
	return MCPChooserAuthFinish(Table, Res);
}

TSharedPtr<FJsonValue> FChooserHandlers::CopyColumn(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	int32 ColumnIndex = INDEX_NONE;
	const TSharedPtr<FJsonValue> ColumnErr = RequireNumber(Params, TEXT("column"), ColumnIndex);
	const FString TargetPath = OptionalString(Params, TEXT("targetTable"));
	const FString TargetNested = OptionalString(Params, TEXT("targetNested"));
	const int32 InsertIndex = OptionalInt(Params, TEXT("insertIndex"), INDEX_NONE);
	if (TableErr) return TableErr;
	if (ColumnErr) return ColumnErr;

	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Source = MCPChooserResolve(TablePath, Nested, ResolveError);
	if (!Source) return ResolveError;
	UChooserTable* Target = (TargetPath.IsEmpty() && TargetNested.IsEmpty())
		? Source : MCPChooserResolve(TargetPath.IsEmpty() ? TablePath : TargetPath, TargetNested, ResolveError);
	if (!Target) return ResolveError;
	FString Err;
	if (!MCPChooserColumn(Source, ColumnIndex, Err)) return MCPError(Err);
	if (MCPChooserIsRandomizeColumn(Source->ColumnsStructs[ColumnIndex]) && MCPChooserHasRandomizeColumn(Target))
	{
		return MCPError(TEXT("The target chooser already has a randomize column"));
	}
	if (InsertIndex != INDEX_NONE && (InsertIndex < 0 || InsertIndex > Target->ColumnsStructs.Num()))
	{
		return MCPError(FString::Printf(TEXT("insertIndex %d out of range (target columnCount=%d)"), InsertIndex, Target->ColumnsStructs.Num()));
	}

	MCPChooserSyncRowCounts(Source);
	// A snapshot, so copying a column into its own table before itself reads the cells it had.
	FInstancedStruct Snapshot = Source->ColumnsStructs[ColumnIndex];
	const int32 SourceRows = Source->ResultsStructs.Num();
	Target->Modify();
	MCPChooserSyncRowCounts(Target);
	const int32 TargetRows = Target->ResultsStructs.Num();
	FInstancedStruct NewColumn = MCPChooserAuthEmptyColumnCopy(Snapshot, TargetRows);
	// Cells match rows by index.
	for (int32 Row = 0; Row < FMath::Min(SourceRows, TargetRows); ++Row)
	{
		MCPChooserColumnCopyRow(NewColumn, Snapshot, Row, Row);
	}
	MCPChooserColumnCopyFallback(NewColumn, Snapshot);
	const int32 NewIndex = MCPChooserColumnInsertPosition(Target, NewColumn, InsertIndex);
	Target->ColumnsStructs.Insert(MoveTemp(NewColumn), NewIndex);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("targetTable"), Target->GetPathName());
	Res->SetNumberField(TEXT("columnIndex"), NewIndex);
	Res->SetNumberField(TEXT("copiedCells"), FMath::Min(SourceRows, TargetRows));
	Res->SetStringField(TEXT("note"), TEXT("The copy's input binding keeps its parameter index, so the target needs compatible parameters"));
	TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
	RbPayload->SetStringField(TEXT("table"), Target->GetPathName());
	RbPayload->SetNumberField(TEXT("column"), NewIndex);
	MCPSetRollback(Res, TEXT("chooser_delete_column"), RbPayload);
	return MCPChooserAuthFinish(Target, Res);
}

TSharedPtr<FJsonValue> FChooserHandlers::SetColumn(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	int32 ColumnIndex = INDEX_NONE;
	const TSharedPtr<FJsonValue> ColumnErr = RequireNumber(Params, TEXT("column"), ColumnIndex);
	bool bDisabled = false;
	const bool bHasDisabled = TryGetBoolParam(Params, TEXT("disabled"), bDisabled);
	FString InputTypeName;
	const bool bHasInputType = TryGetStringParam(Params, TEXT("inputType"), InputTypeName) && !InputTypeName.IsEmpty();
	int32 ParameterIndex = INDEX_NONE;
	const bool bHasBinding = TryGetNumberParam(Params, TEXT("parameterIndex"), ParameterIndex);
	const TArray<TSharedPtr<FJsonValue>>* ChainJson = nullptr;
	const bool bHasChain = TryGetArrayParam(Params, TEXT("propertyChain"), ChainJson);
	if (TableErr) return TableErr;
	if (ColumnErr) return ColumnErr;
	if (!bHasDisabled && !bHasInputType && !bHasBinding)
	{
		return MCPError(bHasChain
			? TEXT("propertyChain needs parameterIndex, the parameter the chain starts from")
			: TEXT("Nothing to set: pass disabled, inputType and/or parameterIndex (+ propertyChain)"));
	}

	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Table = MCPChooserResolve(TablePath, Nested, ResolveError);
	if (!Table) return ResolveError;
	FString Err;
	if (!MCPChooserColumn(Table, ColumnIndex, Err)) return MCPError(Err);

	// Every change is made on a copy and committed only once all of it is valid.
	FInstancedStruct Scratch = Table->ColumnsStructs[ColumnIndex];
	FChooserColumnBase* ScratchColumn = Scratch.GetMutablePtr<FChooserColumnBase>();

	// The prior state, for the inverse.
	TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
	RbPayload->SetStringField(TEXT("table"), Table->GetPathName());
	RbPayload->SetNumberField(TEXT("column"), ColumnIndex);
	bool bLossy = false;
	if (bHasDisabled)
	{
		bool* DisabledPtr = MCPChooserColumnDisabledPtr(Scratch);
		if (!DisabledPtr) return MCPError(TEXT("this engine's chooser columns have no disabled flag"));
		RbPayload->SetBoolField(TEXT("disabled"), *DisabledPtr);
	}
	if (bHasInputType || bHasBinding)
	{
		if (FInstancedStruct* PriorInput = MCPChooserColumnInput(Scratch))
		{
			if (const UScriptStruct* PriorType = PriorInput->GetScriptStruct()) RbPayload->SetStringField(TEXT("inputType"), PriorType->GetPathName());
			const FStructProperty* PriorBindingProp = nullptr;
			if (void* PriorBinding = MCPChooserFindBinding(*PriorInput, &PriorBindingProp))
			{
				int32 PriorIndex = INDEX_NONE; TArray<FName> PriorChain; FString PriorName;
				MCPChooserReadBinding(PriorBindingProp, PriorBinding, PriorIndex, PriorChain, PriorName);
				if (PriorIndex != INDEX_NONE)
				{
					RbPayload->SetNumberField(TEXT("parameterIndex"), PriorIndex);
					TArray<FString> ChainStrings;
					for (const FName& Name : PriorChain) ChainStrings.Add(Name.ToString());
					RbPayload->SetArrayField(TEXT("propertyChain"), MCPChooserAuthStrings(ChainStrings));
				}
			}
			// A new input type starts from defaults, so fields beyond the binding are not carried back.
			bLossy = bHasInputType;
		}
	}

	if (bHasInputType)
	{
		const UScriptStruct* Base = ScratchColumn->GetInputBaseType();
		if (!Base) return MCPError(FString::Printf(TEXT("column %d has no input"), ColumnIndex));
		const UScriptStruct* Type = MCPChooserFindType(InputTypeName, Base, Err);
		if (!Type) return MCPError(Err);
		ScratchColumn->SetInputType(Type);
	}

	FString BindingDisplay;
	if (bHasBinding)
	{
		FInstancedStruct* Input = MCPChooserColumnInput(Scratch);
		const FStructProperty* BindingProp = nullptr;
		void* Binding = Input ? MCPChooserFindBinding(*Input, &BindingProp) : nullptr;
		if (!Binding)
		{
			return MCPError(FString::Printf(TEXT("column %d's input (%s) has no property binding. Change it to a property binding type first with inputType"),
				ColumnIndex, (Input && Input->GetScriptStruct()) ? *Input->GetScriptStruct()->GetName() : TEXT("None")));
		}
		TArray<FInstancedStruct>* ContextData = MCPChooserResolvedContextData(MCPChooserSignatureOwner(Table));
		if (!ContextData || !ContextData->IsValidIndex(ParameterIndex))
		{
			return MCPError(FString::Printf(TEXT("parameter index %d out of range (parameterCount=%d)"), ParameterIndex, ContextData ? ContextData->Num() : 0));
		}
		bool bIsStruct = false;
		const UStruct* ContextType = MCPChooserAuthParameterType((*ContextData)[ParameterIndex], bIsStruct);
		if (!ContextType)
		{
			return MCPError(FString::Printf(TEXT("parameter %d is not a class or struct parameter with a type assigned"), ParameterIndex));
		}
		TArray<FName> Chain;
		if (ChainJson)
		{
			for (const TSharedPtr<FJsonValue>& V : *ChainJson)
			{
				FString S;
				if (!V.IsValid() || !V->TryGetString(S) || S.IsEmpty()) return MCPError(TEXT("propertyChain must be an array of property names"));
				Chain.Add(FName(*S));
			}
		}
		if (!MCPChooserAuthCheckChain(ContextType, Chain, BindingDisplay, Err)) return MCPError(Err);
		MCPChooserWriteBinding(BindingProp, Binding, ParameterIndex, Chain, BindingDisplay);
	}
	if (bHasDisabled)
	{
		*MCPChooserColumnDisabledPtr(Scratch) = bDisabled;
	}

	Table->Modify();
	Table->ColumnsStructs[ColumnIndex] = MoveTemp(Scratch);
	if (bHasInputType || bHasBinding)
	{
		MCPChooserInputChanged(Table->ColumnsStructs[ColumnIndex].GetMutablePtr<FChooserColumnBase>());
	}

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetNumberField(TEXT("columnIndex"), ColumnIndex);
	if (bHasDisabled) Res->SetBoolField(TEXT("disabled"), bDisabled);
	if (FInstancedStruct* Input = MCPChooserColumnInput(Table->ColumnsStructs[ColumnIndex]))
	{
		if (const UScriptStruct* InputType = Input->GetScriptStruct()) Res->SetStringField(TEXT("inputType"), InputType->GetPathName());
	}
	if (bHasBinding) Res->SetStringField(TEXT("bindingDisplayName"), BindingDisplay);
	MCPSetRollback(Res, TEXT("chooser_set_column"), RbPayload);
	if (bLossy)
	{
		Res->SetBoolField(TEXT("rollbackLossy"), true);
		Res->SetStringField(TEXT("rollbackNote"), TEXT("The inverse restores the previous input type and its binding, but a new input type starts from defaults, so other fields of the previous input (an enum or a test value, say) are not carried back."));
	}
	return MCPChooserAuthFinish(Table, Res);
}

// ─── Rows ─────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FChooserHandlers::DeleteRows(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	const TArray<TSharedPtr<FJsonValue>>* RowsJson = nullptr;
	const TSharedPtr<FJsonValue> RowsErr = RequireArray(Params, TEXT("rows"), RowsJson);
	if (TableErr) return TableErr;
	if (RowsErr) return RowsErr;

	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Table = MCPChooserResolve(TablePath, Nested, ResolveError);
	if (!Table) return ResolveError;
	TArray<int32> Rows; FString Err;
	if (!MCPChooserAuthRows(RowsJson, Table, Rows, Err)) return MCPError(Err);

	Table->Modify();
	MCPChooserSyncRowCounts(Table);
	for (FInstancedStruct& ColumnData : Table->ColumnsStructs)
	{
		MCPChooserColumnDeleteRows(ColumnData, Rows);
	}
	for (int32 i = Rows.Num() - 1; i >= 0; --i)
	{
		Table->ResultsStructs.RemoveAt(Rows[i]);
		MCPChooserRemoveDisabledRow(Table, Rows[i]);
	}

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetArrayField(TEXT("deletedRows"), MCPChooserAuthInts(Rows));
	Res->SetNumberField(TEXT("rowCount"), Table->ResultsStructs.Num());
	MCPSetNoRollback(Res, FString::Printf(
		TEXT("%d row(s) were deleted together, and the inverse takes one call per row: chooser(add_row, insertIndex) puts back one row at a time. chooser(delete_row) deletes a single row and records that call as its inverse."),
		Rows.Num()));
	return MCPChooserAuthFinish(Table, Res);
}

TSharedPtr<FJsonValue> FChooserHandlers::MoveRows(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	const TArray<TSharedPtr<FJsonValue>>* RowsJson = nullptr;
	const TSharedPtr<FJsonValue> RowsErr = RequireArray(Params, TEXT("rows"), RowsJson);
	int32 TargetIndex = INDEX_NONE;
	const TSharedPtr<FJsonValue> TargetErr = RequireNumber(Params, TEXT("targetIndex"), TargetIndex);
	if (TableErr) return TableErr;
	if (RowsErr) return RowsErr;
	if (TargetErr) return TargetErr;

	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Table = MCPChooserResolve(TablePath, Nested, ResolveError);
	if (!Table) return ResolveError;
	TArray<int32> Rows; FString Err;
	if (!MCPChooserAuthRows(RowsJson, Table, Rows, Err)) return MCPError(Err);
	if (Rows.Num() == 0) return MCPError(TEXT("No rows to move"));
	const int32 NumRows = Table->ResultsStructs.Num();
	if (TargetIndex < 0 || TargetIndex > NumRows)
	{
		return MCPError(FString::Printf(TEXT("targetIndex %d out of range (rowCount=%d)"), TargetIndex, NumRows));
	}

	// The order after the move: the rows before the target that are not moving,
	// the moved rows in their current order, then the rest.
	TArray<int32> NewOrder;
	for (int32 i = 0; i < TargetIndex; ++i) if (!Rows.Contains(i)) NewOrder.Add(i);
	const int32 FirstNewIndex = NewOrder.Num();
	NewOrder.Append(Rows);
	for (int32 i = TargetIndex; i < NumRows; ++i) if (!Rows.Contains(i)) NewOrder.Add(i);

	Table->Modify();
	MCPChooserSyncRowCounts(Table);
	// Applied as single-row moves front to back, so every column moves its cells
	// through its own MoveRow exactly as the results and disabled flags move.
	TArray<int32> Current;
	for (int32 i = 0; i < NumRows; ++i) Current.Add(i);
	for (int32 k = 0; k < NumRows; ++k)
	{
		const int32 From = Current.IndexOfByKey(NewOrder[k]);
		if (From == k) continue;
		Current.RemoveAt(From);
		Current.Insert(NewOrder[k], k);
		FInstancedStruct Moving = MoveTemp(Table->ResultsStructs[From]);
		Table->ResultsStructs.RemoveAt(From);
		Table->ResultsStructs.Insert(MoveTemp(Moving), k);
#if UE_MCP_HAS_5_5_API
		if (Table->DisabledRows.IsValidIndex(From))
		{
			const bool bFlag = Table->DisabledRows[From];
			Table->DisabledRows.RemoveAt(From);
			Table->DisabledRows.Insert(bFlag, k);
		}
#endif
		for (FInstancedStruct& ColumnData : Table->ColumnsStructs)
		{
			// From > k always, so the insertion index needs no adjustment.
			MCPChooserColumnMoveRow(ColumnData, From, k);
		}
	}

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetNumberField(TEXT("firstRow"), FirstNewIndex);
	Res->SetNumberField(TEXT("movedCount"), Rows.Num());
	// A contiguous block moves back as one block: before the row that followed it.
	const bool bContiguous = Rows.Last() - Rows[0] + 1 == Rows.Num();
	if (bContiguous)
	{
		const int32 Follower = Rows.Last() + 1;
		const int32 BackTarget = Follower < NumRows ? NewOrder.IndexOfByKey(Follower) : NumRows;
		TArray<int32> MovedNow;
		for (int32 i = 0; i < Rows.Num(); ++i) MovedNow.Add(FirstNewIndex + i);
		TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
		RbPayload->SetStringField(TEXT("table"), Table->GetPathName());
		RbPayload->SetArrayField(TEXT("rows"), MCPChooserAuthInts(MovedNow));
		RbPayload->SetNumberField(TEXT("targetIndex"), BackTarget);
		MCPSetRollback(Res, TEXT("chooser_move_rows"), RbPayload);
	}
	else
	{
		MCPSetNoRollback(Res, TEXT("The moved rows were not adjacent, and they now sit together, so no single chooser(move_rows) call scatters them back. Move them back one row at a time."));
	}
	return MCPChooserAuthFinish(Table, Res);
}

TSharedPtr<FJsonValue> FChooserHandlers::DuplicateRows(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	const TArray<TSharedPtr<FJsonValue>>* RowsJson = nullptr;
	const TSharedPtr<FJsonValue> RowsErr = RequireArray(Params, TEXT("rows"), RowsJson);
	if (TableErr) return TableErr;
	if (RowsErr) return RowsErr;

	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Table = MCPChooserResolve(TablePath, Nested, ResolveError);
	if (!Table) return ResolveError;
	TArray<int32> Rows; FString Err;
	if (!MCPChooserAuthRows(RowsJson, Table, Rows, Err)) return MCPError(Err);
	if (Rows.Num() == 0) return MCPError(TEXT("No rows to duplicate"));

	Table->Modify();
	MCPChooserRoot(Table)->Modify();
	MCPChooserSyncRowCounts(Table);
	const int32 InsertIndex = Rows.Last() + 1;
	TMap<UChooserTable*, UChooserTable*> Duplicated;
	TArray<FInstancedStruct> NewResults;
	TArray<bool> NewDisabled;
	for (int32 Row : Rows)
	{
		NewResults.Add(Table->ResultsStructs[Row]);
		NewDisabled.Add(MCPChooserRowDisabled(Table, Row));
	}
	// A nested chooser is part of the row, so its copy gets its own.
	MCPChooserAuthDuplicateNestedResults(NewResults, Table, Duplicated);
	Table->ResultsStructs.Insert(MoveTemp(NewResults), InsertIndex);
#if UE_MCP_HAS_5_5_API
	Table->DisabledRows.Insert(MoveTemp(NewDisabled), InsertIndex);
#endif
	for (FInstancedStruct& ColumnData : Table->ColumnsStructs)
	{
		MCPChooserColumnInsertRows(ColumnData, InsertIndex, Rows.Num());
		// The source rows all sit before InsertIndex, so their indices hold.
		for (int32 i = 0; i < Rows.Num(); ++i)
		{
			MCPChooserColumnCopyRow(ColumnData, ColumnData, Rows[i], InsertIndex + i);
		}
	}
	MCPChooserSyncRowCounts(Table);

	TArray<int32> Copies;
	for (int32 i = 0; i < Rows.Num(); ++i) Copies.Add(InsertIndex + i);
	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetNumberField(TEXT("firstRow"), InsertIndex);
	Res->SetArrayField(TEXT("newRows"), MCPChooserAuthInts(Copies));
	Res->SetNumberField(TEXT("rowCount"), Table->ResultsStructs.Num());
	Res->SetNumberField(TEXT("nestedChoosersCopied"), Duplicated.Num());
	TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
	RbPayload->SetStringField(TEXT("table"), Table->GetPathName());
	RbPayload->SetArrayField(TEXT("rows"), MCPChooserAuthInts(Copies));
	MCPSetRollback(Res, TEXT("chooser_delete_rows"), RbPayload);
	if (Duplicated.Num() > 0)
	{
		Res->SetBoolField(TEXT("rollbackLossy"), true);
		Res->SetStringField(TEXT("rollbackNote"), TEXT("The inverse deletes the copied rows; the nested choosers copied with them stay in the asset, unreferenced, until chooser(delete_nested) removes them."));
	}
	return MCPChooserAuthFinish(Table, Res);
}

TSharedPtr<FJsonValue> FChooserHandlers::CopyRows(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	const TArray<TSharedPtr<FJsonValue>>* RowsJson = nullptr;
	const TSharedPtr<FJsonValue> RowsErr = RequireArray(Params, TEXT("rows"), RowsJson);
	const FString TargetPath = OptionalString(Params, TEXT("targetTable"));
	const FString TargetNested = OptionalString(Params, TEXT("targetNested"));
	const int32 RequestedIndex = OptionalInt(Params, TEXT("insertIndex"), INDEX_NONE);
	const bool bIncludeFallback = OptionalBool(Params, TEXT("includeFallback"), false);
	if (TableErr) return TableErr;
	if (RowsErr) return RowsErr;

	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Source = MCPChooserResolve(TablePath, Nested, ResolveError);
	if (!Source) return ResolveError;
	UChooserTable* Target = (TargetPath.IsEmpty() && TargetNested.IsEmpty())
		? Source : MCPChooserResolve(TargetPath.IsEmpty() ? TablePath : TargetPath, TargetNested, ResolveError);
	if (!Target) return ResolveError;
	TArray<int32> Rows; FString Err;
	if (!MCPChooserAuthRows(RowsJson, Source, Rows, Err)) return MCPError(Err);
	if (Rows.Num() == 0) return MCPError(TEXT("No rows to copy"));
	if (bIncludeFallback && !Source->FallbackResult.IsValid()) return MCPError(TEXT("The chooser has no fallback result to copy"));
	if (RequestedIndex != INDEX_NONE && (RequestedIndex < 0 || RequestedIndex > Target->ResultsStructs.Num()))
	{
		return MCPError(FString::Printf(TEXT("insertIndex %d out of range (target rowCount=%d)"), RequestedIndex, Target->ResultsStructs.Num()));
	}

	// Snapshot what is copied, as the editor's clipboard does, so a copy into the
	// same table reads the rows as they were.
	MCPChooserSyncRowCounts(Source);
	TArray<FInstancedStruct> SourceColumns = Source->ColumnsStructs;
	TArray<FInstancedStruct> SourceResults;
	TArray<bool> SourceDisabled;
	for (int32 Row : Rows)
	{
		SourceResults.Add(Source->ResultsStructs[Row]);
		SourceDisabled.Add(MCPChooserRowDisabled(Source, Row));
	}
	const FInstancedStruct SourceFallback = Source->FallbackResult;

	Target->Modify();
	MCPChooserRoot(Target)->Modify();
	MCPChooserSyncRowCounts(Target);
	const int32 InsertIndex = RequestedIndex == INDEX_NONE ? Target->ResultsStructs.Num() : RequestedIndex;

	// Columns match by type and input name; a source column with no match is
	// added to the target, empty, so its cells have somewhere to go.
	TArray<int32> TargetForSource;
	TArray<int32> AddedColumns;
	TSet<int32> Used;
	for (int32 s = 0; s < SourceColumns.Num(); ++s)
	{
		const FString Key = MCPChooserAuthColumnKey(SourceColumns[s]);
		int32 Match = INDEX_NONE;
		for (int32 t = 0; t < Target->ColumnsStructs.Num(); ++t)
		{
			if (!Used.Contains(t) && MCPChooserAuthColumnKey(Target->ColumnsStructs[t]) == Key) { Match = t; break; }
		}
		if (Match == INDEX_NONE)
		{
			if (MCPChooserIsRandomizeColumn(SourceColumns[s]) && MCPChooserHasRandomizeColumn(Target))
			{
				TargetForSource.Add(INDEX_NONE);
				continue;
			}
			FInstancedStruct NewColumn = MCPChooserAuthEmptyColumnCopy(SourceColumns[s], Target->ResultsStructs.Num());
			Match = MCPChooserColumnInsertPosition(Target, NewColumn, INDEX_NONE);
			Target->ColumnsStructs.Insert(MoveTemp(NewColumn), Match);
			// Indices at or after the insert moved up by one.
			for (int32& Mapped : TargetForSource) if (Mapped != INDEX_NONE && Mapped >= Match) ++Mapped;
			TSet<int32> Shifted;
			for (int32 U : Used) Shifted.Add(U >= Match ? U + 1 : U);
			Used = MoveTemp(Shifted);
			for (int32& Added : AddedColumns) if (Added >= Match) ++Added;
			AddedColumns.Add(Match);
		}
		Used.Add(Match);
		TargetForSource.Add(Match);
	}

	TMap<UChooserTable*, UChooserTable*> Duplicated;
	MCPChooserAuthDuplicateNestedResults(SourceResults, Target, Duplicated);
	Target->ResultsStructs.Insert(MoveTemp(SourceResults), InsertIndex);
#if UE_MCP_HAS_5_5_API
	Target->DisabledRows.Insert(MoveTemp(SourceDisabled), InsertIndex);
#endif
	for (FInstancedStruct& ColumnData : Target->ColumnsStructs)
	{
		MCPChooserColumnInsertRows(ColumnData, InsertIndex, Rows.Num());
	}
	for (int32 s = 0; s < SourceColumns.Num(); ++s)
	{
		const int32 t = TargetForSource[s];
		if (t == INDEX_NONE) continue;
		for (int32 i = 0; i < Rows.Num(); ++i)
		{
			MCPChooserColumnCopyRow(Target->ColumnsStructs[t], SourceColumns[s], Rows[i], InsertIndex + i);
		}
		if (bIncludeFallback) MCPChooserColumnCopyFallback(Target->ColumnsStructs[t], SourceColumns[s]);
	}
	if (bIncludeFallback)
	{
		Target->FallbackResult = SourceFallback;
		MCPChooserAuthDuplicateNestedResults(TArrayView<FInstancedStruct>(&Target->FallbackResult, 1), Target, Duplicated);
	}
	MCPChooserSyncRowCounts(Target);

	TArray<int32> Copies;
	for (int32 i = 0; i < Rows.Num(); ++i) Copies.Add(InsertIndex + i);
	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("targetTable"), Target->GetPathName());
	Res->SetNumberField(TEXT("firstRow"), InsertIndex);
	Res->SetArrayField(TEXT("newRows"), MCPChooserAuthInts(Copies));
	Res->SetArrayField(TEXT("addedColumns"), MCPChooserAuthInts(AddedColumns));
	Res->SetNumberField(TEXT("nestedChoosersCopied"), Duplicated.Num());
	TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
	RbPayload->SetStringField(TEXT("table"), Target->GetPathName());
	RbPayload->SetArrayField(TEXT("rows"), MCPChooserAuthInts(Copies));
	MCPSetRollback(Res, TEXT("chooser_delete_rows"), RbPayload);
	TArray<FString> Lost;
	if (AddedColumns.Num() > 0) Lost.Add(FString::Printf(TEXT("the %d column(s) added to hold the copied cells stay"), AddedColumns.Num()));
	if (bIncludeFallback) Lost.Add(TEXT("the replaced fallback result is not restored"));
	if (Duplicated.Num() > 0) Lost.Add(TEXT("the nested choosers copied with the rows stay in the asset, unreferenced"));
	if (Lost.Num() > 0)
	{
		Res->SetBoolField(TEXT("rollbackLossy"), true);
		Res->SetStringField(TEXT("rollbackNote"), TEXT("The inverse deletes the copied rows, but ") + FString::Join(Lost, TEXT("; ")) + TEXT("."));
	}
	return MCPChooserAuthFinish(Target, Res);
}

// ─── Cells and fallback ───────────────────────────────────────────────────

TSharedPtr<FJsonValue> FChooserHandlers::GetCell(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	int32 ColumnIndex = INDEX_NONE, Row = INDEX_NONE;
	const TSharedPtr<FJsonValue> ColumnErr = RequireNumber(Params, TEXT("column"), ColumnIndex);
	const bool bHasRow = TryGetNumberParam(Params, TEXT("row"), Row);
	const bool bFallback = OptionalBool(Params, TEXT("fallback"), false);
	if (TableErr) return TableErr;
	if (ColumnErr) return ColumnErr;
	if (bHasRow == bFallback) return MCPError(TEXT("Pass row, or fallback=true for the column's fallback cell"));

	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Table = MCPChooserResolve(TablePath, Nested, ResolveError);
	if (!Table) return ResolveError;
	FString Err;
	if (!MCPChooserColumn(Table, ColumnIndex, Err)) return MCPError(Err);
	if (!bFallback && !Table->ResultsStructs.IsValidIndex(Row))
	{
		return MCPError(FString::Printf(TEXT("row index %d out of range (rowCount=%d)"), Row, Table->ResultsStructs.Num()));
	}
	FProperty* Prop = nullptr; void* Addr = nullptr;
	if (!MCPChooserAuthCell(Table->ColumnsStructs[ColumnIndex], bFallback ? MCPChooserFallbackIndex : Row, Prop, Addr, Err))
	{
		return MCPError(FString::Printf(TEXT("column %d: %s"), ColumnIndex, *Err));
	}
	FString Value;
	Prop->ExportTextItem_Direct(Value, Addr, nullptr, Table, PPF_None);
	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetNumberField(TEXT("column"), ColumnIndex);
	if (bFallback) Res->SetBoolField(TEXT("fallback"), true);
	else Res->SetNumberField(TEXT("row"), Row);
	Res->SetStringField(TEXT("value"), Value);
	Res->SetStringField(TEXT("cellType"), MCPChooserAuthCellType(Prop));
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FChooserHandlers::SetCell(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	int32 ColumnIndex = INDEX_NONE, Row = INDEX_NONE;
	const TSharedPtr<FJsonValue> ColumnErr = RequireNumber(Params, TEXT("column"), ColumnIndex);
	const bool bHasRow = TryGetNumberParam(Params, TEXT("row"), Row);
	const bool bFallback = OptionalBool(Params, TEXT("fallback"), false);
	const TSharedPtr<FJsonValue> RawValue = TryGetParam(Params, TEXT("value"));
	if (TableErr) return TableErr;
	if (ColumnErr) return ColumnErr;
	if (!RawValue.IsValid() || RawValue->Type == EJson::Null) return MCPError(TEXT("Missing required parameter 'value'"));
	if (bHasRow == bFallback) return MCPError(TEXT("Pass row, or fallback=true for the column's fallback cell"));
	const FString Value = MCPChooserJsonToText(RawValue);

	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Table = MCPChooserResolve(TablePath, Nested, ResolveError);
	if (!Table) return ResolveError;
	FString Err;
	if (!MCPChooserColumn(Table, ColumnIndex, Err)) return MCPError(Err);
	if (!bFallback && !Table->ResultsStructs.IsValidIndex(Row))
	{
		return MCPError(FString::Printf(TEXT("row index %d out of range (rowCount=%d)"), Row, Table->ResultsStructs.Num()));
	}
	MCPChooserSyncRowCounts(Table);
	FProperty* Prop = nullptr; void* Addr = nullptr;
	if (!MCPChooserAuthCell(Table->ColumnsStructs[ColumnIndex], bFallback ? MCPChooserFallbackIndex : Row, Prop, Addr, Err))
	{
		return MCPError(FString::Printf(TEXT("column %d: %s"), ColumnIndex, *Err));
	}
	FString Prior;
	Prop->ExportTextItem_Direct(Prior, Addr, nullptr, Table, PPF_None);
	Table->Modify();
	if (!MCPChooserImportValidated(Prop, Addr, Value, Table, Err))
	{
		return MCPError(FString::Printf(TEXT("'%s' is not a valid %s cell: %s. Nothing was changed"), *Value, *MCPChooserAuthCellType(Prop), *Err));
	}
	FString Now;
	Prop->ExportTextItem_Direct(Now, Addr, nullptr, Table, PPF_None);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetNumberField(TEXT("column"), ColumnIndex);
	if (bFallback) Res->SetBoolField(TEXT("fallback"), true);
	else Res->SetNumberField(TEXT("row"), Row);
	Res->SetStringField(TEXT("previousValue"), Prior);
	Res->SetStringField(TEXT("value"), Now);
	TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
	RbPayload->SetStringField(TEXT("table"), Table->GetPathName());
	RbPayload->SetNumberField(TEXT("column"), ColumnIndex);
	if (bFallback) RbPayload->SetBoolField(TEXT("fallback"), true);
	else RbPayload->SetNumberField(TEXT("row"), Row);
	RbPayload->SetStringField(TEXT("value"), Prior);
	MCPSetRollback(Res, TEXT("chooser_set_cell"), RbPayload);
	return MCPChooserAuthFinish(Table, Res);
}

TSharedPtr<FJsonValue> FChooserHandlers::SetFallback(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	const FString ResultType = OptionalString(Params, TEXT("resultType"));
	const FString OutputPath = OptionalString(Params, TEXT("output"));
	const FString OutputType = OptionalString(Params, TEXT("outputType"), TEXT("asset"));
	const bool bRemove = OptionalBool(Params, TEXT("remove"), false);
	if (TableErr) return TableErr;
	const int32 Modes = (bRemove ? 1 : 0) + (ResultType.IsEmpty() ? 0 : 1) + (OutputPath.IsEmpty() ? 0 : 1);
	if (Modes != 1) return MCPError(TEXT("Pass exactly one of output (an asset, wrapped per outputType), resultType (a default result of that type) or remove=true"));

	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Table = MCPChooserResolve(TablePath, Nested, ResolveError);
	if (!Table) return ResolveError;

	FInstancedStruct NewResult;
	FString Err;
	if (bRemove)
	{
		if (!Table->FallbackResult.IsValid()) return MCPError(TEXT("The chooser has no fallback result"));
	}
	else if (!OutputPath.IsEmpty())
	{
		if (!MCPChooserBuildOutput(OutputPath, OutputType, NewResult, Err)) return MCPError(Err);
	}
	else if (!MCPChooserMakeResult(Table, ResultType, NewResult, Err))
	{
		return MCPError(Err);
	}

	// The prior fallback makes the inverse.
	TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
	RbPayload->SetStringField(TEXT("table"), Table->GetPathName());
	bool bLossy = false;
	if (!Table->FallbackResult.IsValid())
	{
		RbPayload->SetBoolField(TEXT("remove"), true);
	}
	else
	{
		TSharedPtr<FJsonObject> Prior = MCPChooserDescribeOutput(Table->FallbackResult);
		const FString PriorPath = Prior->HasField(TEXT("output")) ? Prior->GetStringField(TEXT("output")) : FString();
		const FString PriorType = Prior->GetStringField(TEXT("resultType"));
		const bool bAssetLike = PriorType == TEXT("AssetChooser") || PriorType == TEXT("SoftAssetChooser") || PriorType == TEXT("EvaluateChooser");
		if (!PriorPath.IsEmpty() && bAssetLike)
		{
			RbPayload->SetStringField(TEXT("output"), PriorPath);
			RbPayload->SetStringField(TEXT("outputType"), PriorType == TEXT("EvaluateChooser") ? TEXT("evaluate") : (PriorType == TEXT("SoftAssetChooser") ? TEXT("soft_asset") : TEXT("asset")));
		}
		else
		{
			RbPayload->SetStringField(TEXT("resultType"), Table->FallbackResult.GetScriptStruct()->GetPathName());
			bLossy = true;
		}
	}

	Table->Modify();
	if (bRemove)
	{
		// Output columns keep their fallback cells, as when the editor deletes the fallback row.
		Table->FallbackResult.Reset();
	}
	else
	{
		Table->FallbackResult = MoveTemp(NewResult);
		MCPChooserAutoPopulateRow(Table, MCPChooserFallbackIndex);
	}

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetBoolField(TEXT("hasFallback"), Table->FallbackResult.IsValid());
	if (Table->FallbackResult.IsValid()) Res->SetObjectField(TEXT("fallbackResult"), MCPChooserDescribeOutput(Table->FallbackResult));
	MCPSetRollback(Res, TEXT("chooser_set_fallback"), RbPayload);
	if (bLossy)
	{
		Res->SetBoolField(TEXT("rollbackLossy"), true);
		Res->SetStringField(TEXT("rollbackNote"), TEXT("The previous fallback was not a plain asset reference, so the inverse restores a default result of its type; its fields need chooser(set_property, target='fallback') again."));
	}
	return MCPChooserAuthFinish(Table, Res);
}

TSharedPtr<FJsonValue> FChooserHandlers::AutoPopulate(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	int32 ColumnIndex = INDEX_NONE;
	const bool bHasColumn = TryGetNumberParam(Params, TEXT("column"), ColumnIndex);
	const TArray<TSharedPtr<FJsonValue>>* RowsJson = nullptr;
	const bool bHasRows = TryGetArrayParam(Params, TEXT("rows"), RowsJson);
	const bool bIncludeFallback = OptionalBool(Params, TEXT("includeFallback"), false);
	if (TableErr) return TableErr;
	if (bHasColumn && bHasRows) return MCPError(TEXT("Pass column (one column, every row) or rows (those rows, every column), not both; neither fills everything"));
	if (!MCPChooserAutoPopulateSupported())
	{
		return MCPError(TEXT("Auto populate needs FChooserColumnBase::AutoPopulate, which this bridge reaches on UE 5.7 and newer"));
	}

	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Table = MCPChooserResolve(TablePath, Nested, ResolveError);
	if (!Table) return ResolveError;
	FString Err;
	TArray<int32> Rows;
	if (bHasColumn)
	{
		if (!MCPChooserColumn(Table, ColumnIndex, Err)) return MCPError(Err);
		if (!MCPChooserColumnAutoPopulates(Table->ColumnsStructs[ColumnIndex]))
		{
			return MCPError(FString::Printf(TEXT("column %d does not support auto populate"), ColumnIndex));
		}
	}
	if (bHasRows && !MCPChooserAuthRows(RowsJson, Table, Rows, Err)) return MCPError(Err);

	TArray<int32> Populating;
	for (int32 c = 0; c < Table->ColumnsStructs.Num(); ++c)
	{
		if ((!bHasColumn || c == ColumnIndex) && MCPChooserColumnAutoPopulates(Table->ColumnsStructs[c])) Populating.Add(c);
	}

	Table->Modify();
	if (bHasColumn)
	{
		MCPChooserAutoPopulateColumn(Table, Table->ColumnsStructs[ColumnIndex]);
	}
	else if (bHasRows)
	{
		for (int32 Row : Rows) MCPChooserAutoPopulateRow(Table, Row);
		if (bIncludeFallback) MCPChooserAutoPopulateRow(Table, MCPChooserFallbackIndex);
	}
	else
	{
		for (FInstancedStruct& ColumnData : Table->ColumnsStructs) MCPChooserAutoPopulateColumn(Table, ColumnData);
	}

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetArrayField(TEXT("columns"), MCPChooserAuthInts(Populating));
	if (bHasRows) Res->SetArrayField(TEXT("rows"), MCPChooserAuthInts(Rows));
	MCPSetNoRollback(Res, TEXT("Auto populate rewrites the cells of auto populating columns from each row's result, across many cells at once. Each previous value can be put back with chooser(set_cell); chooser(get_cell) reads them first."));
	return MCPChooserAuthFinish(Table, Res);
}

// ─── Nested choosers and objects ──────────────────────────────────────────

TSharedPtr<FJsonValue> FChooserHandlers::CreateNested(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	int32 Row = INDEX_NONE;
	const bool bHasRow = TryGetNumberParam(Params, TEXT("row"), Row);
	const FString RequestedName = OptionalString(Params, TEXT("name"));
	if (TableErr) return TableErr;

	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Table = MCPChooserResolve(TablePath, Nested, ResolveError);
	if (!Table) return ResolveError;
	const UScriptStruct* NestedStruct = MCPChooserAuthNestedResultStruct();
	FObjectPropertyBase* ChooserProp = NestedStruct ? FindFProperty<FObjectPropertyBase>(NestedStruct, TEXT("Chooser")) : nullptr;
	if (!ChooserProp) return MCPError(TEXT("This engine's Chooser plugin has no nested chooser result type"));
	if (bHasRow && !Table->ResultsStructs.IsValidIndex(Row))
	{
		return MCPError(FString::Printf(TEXT("row index %d out of range (rowCount=%d)"), Row, Table->ResultsStructs.Num()));
	}
	FName NewName = RequestedName.IsEmpty() ? FName(TEXT("NestedChooser")) : FName(*RequestedName);
	FText Reason;
	if (!FName::IsValidXName(NewName.ToString(), INVALID_OBJECTNAME_CHARACTERS, &Reason))
	{
		return MCPError(FString::Printf(TEXT("'%s' is not a valid name: %s"), *NewName.ToString(), *Reason.ToString()));
	}

	UChooserTable* Root = MCPChooserRoot(Table);
	const bool bReplacing = bHasRow;
	TSharedPtr<FJsonObject> Replaced = bReplacing ? MCPChooserDescribeOutput(Table->ResultsStructs[Row]) : nullptr;
	Table->Modify();
	Root->Modify();
	if (FindObject<UObject>(Table, *NewName.ToString()))
	{
		NewName = MakeUniqueObjectName(Table, UChooserTable::StaticClass(), NewName);
	}
	UChooserTable* NewChooser = NewObject<UChooserTable>(Table, UChooserTable::StaticClass(), NewName, RF_Transactional);
	MCPChooserSetRoot(NewChooser, Root);
	MCPChooserAddNestedObject(Root, NewChooser);

	FInstancedStruct NewResult;
	NewResult.InitializeAs(NestedStruct);
	ChooserProp->SetObjectPropertyValue_InContainer(NewResult.GetMutableMemory(), NewChooser);
	int32 RowIndex = Row;
	if (!bHasRow)
	{
		MCPChooserSyncRowCounts(Table);
		RowIndex = Table->ResultsStructs.Add(MoveTemp(NewResult));
		MCPChooserSyncRowCounts(Table);
	}
	else
	{
		Table->ResultsStructs[Row] = MoveTemp(NewResult);
	}
	NewChooser->Compile(true);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("nestedPath"), NewChooser->GetPathName());
	Res->SetStringField(TEXT("name"), NewChooser->GetName());
	Res->SetNumberField(TEXT("rowIndex"), RowIndex);
	Res->SetBoolField(TEXT("addedRow"), !bHasRow);
	if (Replaced.IsValid()) Res->SetObjectField(TEXT("replacedOutput"), Replaced);
	TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
	RbPayload->SetStringField(TEXT("table"), Root->GetPathName());
	RbPayload->SetStringField(TEXT("name"), NewChooser->GetPathName());
	MCPSetRollback(Res, TEXT("chooser_delete_nested"), RbPayload);
	Res->SetBoolField(TEXT("rollbackLossy"), true);
	Res->SetStringField(TEXT("rollbackNote"), bHasRow
		? FString::Printf(TEXT("The inverse deletes the nested chooser and clears row %d's reference to it; the result the row had before is not put back (it is in replacedOutput)."), RowIndex)
		: FString::Printf(TEXT("The inverse deletes the nested chooser and clears row %d's reference to it; the row itself stays. chooser(delete_row, index=%d) removes it."), RowIndex, RowIndex));
	return MCPChooserAuthFinish(Table, Res);
}

TSharedPtr<FJsonValue> FChooserHandlers::ListNested(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	if (auto Err = RequireString(Params, TEXT("table"), TablePath)) return Err;
	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Table = MCPChooserResolve(TablePath, FString(), ResolveError);
	if (!Table) return ResolveError;
	UChooserTable* Root = MCPChooserRoot(Table);

	TArray<TSharedPtr<FJsonValue>> Objects;
	int32 ChooserCount = 0;
	for (UObject* Obj : MCPChooserNestedObjects(Root))
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Obj->GetName());
		Entry->SetStringField(TEXT("path"), Obj->GetPathName());
		Entry->SetStringField(TEXT("class"), Obj->GetClass()->GetName());
		const bool bIsChooser = Obj->IsA<UChooserTable>();
		Entry->SetBoolField(TEXT("isChooser"), bIsChooser);
		Entry->SetStringField(TEXT("parent"), Obj->GetOuter() ? Obj->GetOuter()->GetPathName() : FString());
		if (UChooserTable* NestedTable = Cast<UChooserTable>(Obj))
		{
			++ChooserCount;
			Entry->SetNumberField(TEXT("rowCount"), NestedTable->ResultsStructs.Num());
			Entry->SetNumberField(TEXT("columnCount"), NestedTable->ColumnsStructs.Num());
		}
		Objects.Add(MakeShared<FJsonValueObject>(Entry));
	}
	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("rootChooser"), Root->GetPathName());
	Res->SetBoolField(TEXT("isNested"), Root != Table);
	Res->SetNumberField(TEXT("nestedChooserCount"), ChooserCount);
	Res->SetNumberField(TEXT("count"), Objects.Num());
	Res->SetArrayField(TEXT("nestedObjects"), Objects);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FChooserHandlers::DeleteNested(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath, Name;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const TSharedPtr<FJsonValue> NameErr = RequireString(Params, TEXT("name"), Name);
	if (TableErr) return TableErr;
	if (NameErr) return NameErr;

	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Table = MCPChooserResolve(TablePath, FString(), ResolveError);
	if (!Table) return ResolveError;
	UChooserTable* Root = MCPChooserRoot(Table);
	UObject* Doomed = MCPChooserAuthFindNested(Root, Name);
	if (!Doomed) return MCPError(FString::Printf(TEXT("'%s' is not a nested object of %s (chooser(list_nested) lists them)"), *Name, *Root->GetPathName()));
	const FString DoomedPath = Doomed->GetPathName();
	const FString DoomedClass = Doomed->GetClass()->GetName();

	Root->Modify();
#if UE_MCP_HAS_5_9_API
	// The engine's own delete clears every reference to it from rows in any table.
	if (UChooserTable* DoomedTable = Cast<UChooserTable>(Doomed)) Root->DeleteNestedChooser(DoomedTable);
	else Root->DeleteNestedObject(Doomed);
#else
	// Clear every row and fallback that points at it (or at a chooser nested
	// inside it), then take it and those out of the asset.
	TArray<UObject*> Removing = { Doomed };
	for (UObject* Obj : MCPChooserNestedObjects(Root))
	{
		if (Obj != Doomed && Obj->IsIn(Doomed)) Removing.Add(Obj);
	}
	TArray<UChooserTable*> Tables = { Root };
	for (UObject* Obj : MCPChooserNestedObjects(Root))
	{
		if (UChooserTable* NestedTable = Cast<UChooserTable>(Obj)) Tables.Add(NestedTable);
	}
	for (UChooserTable* Owner : Tables)
	{
		auto Clear = [&Removing, Owner](FInstancedStruct& Result)
		{
			FObjectPropertyBase* Prop = nullptr;
			if (UChooserTable* Ref = MCPChooserAuthNestedRef(Result, &Prop))
			{
				if (Removing.Contains(Ref))
				{
					Owner->Modify();
					Prop->SetObjectPropertyValue_InContainer(Result.GetMutableMemory(), nullptr);
				}
			}
		};
		for (FInstancedStruct& Result : Owner->ResultsStructs) Clear(Result);
		Clear(Owner->FallbackResult);
	}
	for (UObject* Obj : Removing)
	{
		MCPChooserRemoveNestedObject(Root, Obj);
		Obj->Modify();
		Obj->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
		Obj->MarkAsGarbage();
	}
#endif

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("deleted"), DoomedPath);
	Res->SetStringField(TEXT("deletedClass"), DoomedClass);
	Res->SetNumberField(TEXT("nestedObjectCount"), MCPChooserNestedObjects(Root).Num());
	MCPSetNoRollback(Res, FString::Printf(
		TEXT("'%s' was deleted with its rows and columns, and every reference to it was cleared. chooser(create_nested) makes an empty nested chooser; there is no action that recreates one with its contents, so a restore is a revert of the saved asset."),
		*DoomedPath));
	return MCPChooserAuthFinish(Root, Res);
}

TSharedPtr<FJsonValue> FChooserHandlers::RenameNested(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath, Name, NewName;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const TSharedPtr<FJsonValue> NameErr = RequireString(Params, TEXT("name"), Name);
	const TSharedPtr<FJsonValue> NewNameErr = RequireString(Params, TEXT("newName"), NewName);
	if (TableErr) return TableErr;
	if (NameErr) return NameErr;
	if (NewNameErr) return NewNameErr;

	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Table = MCPChooserResolve(TablePath, FString(), ResolveError);
	if (!Table) return ResolveError;
	UChooserTable* Root = MCPChooserRoot(Table);
	UObject* Target = MCPChooserAuthFindNested(Root, Name);
	if (!Target) return MCPError(FString::Printf(TEXT("'%s' is not a nested object of %s (chooser(list_nested) lists them)"), *Name, *Root->GetPathName()));
	const FString OldName = Target->GetName();

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	if (OldName != NewName)
	{
		FText Reason;
		if (!FName::IsValidXName(NewName, INVALID_OBJECTNAME_CHARACTERS, &Reason))
		{
			return MCPError(FString::Printf(TEXT("'%s' is not a valid name: %s"), *NewName, *Reason.ToString()));
		}
		for (UObject* Obj : MCPChooserNestedObjects(Root))
		{
			if (Obj->GetName() == NewName) return MCPError(FString::Printf(TEXT("A nested object named '%s' already exists"), *NewName));
		}
		if (!Target->Rename(*NewName, nullptr, REN_Test))
		{
			return MCPError(FString::Printf(TEXT("'%s' can't be renamed to '%s'"), *OldName, *NewName));
		}
		Target->Modify();
		Target->Rename(*NewName);
		Target->PostEditChange();
	}
	Res->SetStringField(TEXT("previousName"), OldName);
	Res->SetStringField(TEXT("name"), Target->GetName());
	Res->SetStringField(TEXT("nestedPath"), Target->GetPathName());
	TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
	RbPayload->SetStringField(TEXT("table"), Root->GetPathName());
	RbPayload->SetStringField(TEXT("name"), Target->GetName());
	RbPayload->SetStringField(TEXT("newName"), OldName);
	MCPSetRollback(Res, TEXT("chooser_rename_nested"), RbPayload);
	return MCPChooserAuthFinish(Root, Res);
}

TSharedPtr<FJsonValue> FChooserHandlers::RemoveDisabled(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	const TSharedPtr<FJsonValue> TableErr = RequireString(Params, TEXT("table"), TablePath);
	const FString Nested = OptionalString(Params, TEXT("nested"));
	if (TableErr) return TableErr;

	TSharedPtr<FJsonValue> ResolveError;
	UChooserTable* Table = MCPChooserResolve(TablePath, Nested, ResolveError);
	if (!Table) return ResolveError;

	Table->Modify();
	MCPChooserSyncRowCounts(Table);
	const int32 RowsBefore = Table->ResultsStructs.Num();
	const int32 ColumnsBefore = Table->ColumnsStructs.Num();
#if UE_MCP_HAS_5_7_API
	Table->RemoveDisabledData();
#else
	// Disabled rows (5.5 and newer carry the flag), then disabled columns and
	// columns whose type is not loaded.
	TArray<int32> DisabledRows;
	for (int32 Row = 0; Row < Table->ResultsStructs.Num(); ++Row)
	{
		if (MCPChooserRowDisabled(Table, Row)) DisabledRows.Add(Row);
	}
	for (FInstancedStruct& ColumnData : Table->ColumnsStructs) MCPChooserColumnDeleteRows(ColumnData, DisabledRows);
	for (int32 i = DisabledRows.Num() - 1; i >= 0; --i)
	{
		Table->ResultsStructs.RemoveAt(DisabledRows[i]);
		MCPChooserRemoveDisabledRow(Table, DisabledRows[i]);
	}
	for (int32 c = Table->ColumnsStructs.Num() - 1; c >= 0; --c)
	{
		const bool* Disabled = MCPChooserColumnDisabledPtr(Table->ColumnsStructs[c]);
		if (!Table->ColumnsStructs[c].IsValid() || (Disabled && *Disabled)) Table->ColumnsStructs.RemoveAt(c);
	}
#endif

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetNumberField(TEXT("removedRows"), RowsBefore - Table->ResultsStructs.Num());
	Res->SetNumberField(TEXT("removedColumns"), ColumnsBefore - Table->ColumnsStructs.Num());
	Res->SetNumberField(TEXT("rowCount"), Table->ResultsStructs.Num());
	Res->SetNumberField(TEXT("columnCount"), Table->ColumnsStructs.Num());
	MCPSetNoRollback(Res, TEXT("The disabled rows and columns were deleted with their cells. Nothing re-adds them with their contents, so a restore is a revert of the saved asset."));
	return MCPChooserAuthFinish(Table, Res);
}
