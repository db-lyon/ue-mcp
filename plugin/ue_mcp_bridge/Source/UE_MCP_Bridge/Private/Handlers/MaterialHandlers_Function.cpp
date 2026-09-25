// #463: MaterialFunction creation + per-function expression authoring.
// The material tool surface previously only covered UMaterial asset graphs.
// MaterialFunction assets had no native create path or per-function
// expression APIs, forcing execute_python for what is a normal authoring
// workflow (color packs, reusable shading functions, math helpers).
//
// Split into its own TU so the existing MaterialHandlers.cpp doesn't grow.
// All functions are still members of FMaterialHandlers - registration
// happens in MaterialHandlers.cpp::RegisterHandlers.

#include "MaterialHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/MaterialFunctionFactoryNew.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "MaterialEditingLibrary.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "UObject/UObjectIterator.h"

namespace
{
	UMaterialFunction* LoadMaterialFunction(const FString& Path)
	{
		UMaterialFunction* MF = LoadObject<UMaterialFunction>(nullptr, *Path);
		if (!MF)
		{
			MF = Cast<UMaterialFunction>(UEditorAssetLibrary::LoadAsset(Path));
		}
		return MF;
	}
}

// material(action="create_function", name, packagePath?, description?, onConflict?)
TSharedPtr<FJsonValue> FMaterialHandlers::CreateMaterialFunction(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Materials/Functions"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	UMaterialFunctionFactoryNew* Factory = NewObject<UMaterialFunctionFactoryNew>();
	auto Created = MCPCreateAssetIdempotent<UMaterialFunction>(Name, PackagePath, OnConflict, TEXT("MaterialFunction"), Factory);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	UMaterialFunction* MF = Created.Asset;
	FString Description;
	if (Params->TryGetStringField(TEXT("description"), Description))
	{
		MF->Description = Description;
	}

	UEditorAssetLibrary::SaveAsset(MF->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), MF->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("packagePath"), PackagePath);
	MCPSetDeleteAssetRollback(Result, MF->GetPathName());
	return MCPResult(Result);
}

// material(action="add_expression_in_function", functionPath, expressionType,
//          positionX?, positionY?, inputName?, outputName?)
TSharedPtr<FJsonValue> FMaterialHandlers::AddMaterialFunctionExpression(const TSharedPtr<FJsonObject>& Params)
{
	FString FunctionPath;
	if (auto Err = RequireStringAlt(Params, TEXT("functionPath"), TEXT("materialFunctionPath"), FunctionPath)) return Err;

	FString ExpressionType;
	if (auto Err = RequireString(Params, TEXT("expressionType"), ExpressionType)) return Err;

	UMaterialFunction* MF = LoadMaterialFunction(FunctionPath);
	if (!MF) return MCPError(FString::Printf(TEXT("MaterialFunction not found: %s"), *FunctionPath));

	UClass* ExprClass = ResolveExpressionClass(ExpressionType);
	if (!ExprClass) return MCPError(FString::Printf(TEXT("Unknown expression type: '%s'"), *ExpressionType));

	int32 PosX = (int32)OptionalNumber(Params, TEXT("positionX"), 0.0);
	int32 PosY = (int32)OptionalNumber(Params, TEXT("positionY"), 0.0);

	UMaterialExpression* NewExpr = UMaterialEditingLibrary::CreateMaterialExpressionInFunction(MF, ExprClass, PosX, PosY);
	if (!NewExpr) return MCPError(TEXT("CreateMaterialExpressionInFunction returned null"));

	// Input/Output expressions: name them so callers can reference them by name.
	if (UMaterialExpressionFunctionInput* AsInput = Cast<UMaterialExpressionFunctionInput>(NewExpr))
	{
		FString InputName;
		if (Params->TryGetStringField(TEXT("inputName"), InputName) || Params->TryGetStringField(TEXT("name"), InputName))
		{
			AsInput->InputName = FName(*InputName);
		}
		FString InputTypeStr;
		if (Params->TryGetStringField(TEXT("inputType"), InputTypeStr))
		{
			static const TMap<FString, EFunctionInputType> Map = {
				{TEXT("Scalar"), FunctionInput_Scalar},
				{TEXT("Vector2"), FunctionInput_Vector2},
				{TEXT("Vector3"), FunctionInput_Vector3},
				{TEXT("Vector4"), FunctionInput_Vector4},
				{TEXT("Texture2D"), FunctionInput_Texture2D},
				{TEXT("TextureCube"), FunctionInput_TextureCube},
				{TEXT("StaticBool"), FunctionInput_StaticBool},
				{TEXT("MaterialAttributes"), FunctionInput_MaterialAttributes},
			};
			if (const EFunctionInputType* Found = Map.Find(InputTypeStr))
			{
				AsInput->InputType = *Found;
			}
		}
	}
	if (UMaterialExpressionFunctionOutput* AsOutput = Cast<UMaterialExpressionFunctionOutput>(NewExpr))
	{
		FString OutputName;
		if (Params->TryGetStringField(TEXT("outputName"), OutputName) || Params->TryGetStringField(TEXT("name"), OutputName))
		{
			AsOutput->OutputName = FName(*OutputName);
		}
	}

	UMaterialEditingLibrary::UpdateMaterialFunction(MF, nullptr);
	UEditorAssetLibrary::SaveAsset(MF->GetPathName());

	int32 Index = MF->GetExpressions().IndexOfByKey(NewExpr);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("functionPath"), MF->GetPathName());
	Result->SetStringField(TEXT("expressionClass"), NewExpr->GetClass()->GetName());
	Result->SetStringField(TEXT("expressionName"), NewExpr->GetName());
	Result->SetNumberField(TEXT("expressionIndex"), Index);
	Result->SetStringField(TEXT("nodeId"), FString::FromInt(Index));

	// Rollback: delete the node by its engine name, which is unique in the function.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("functionPath"), MF->GetPathName());
	Payload->SetStringField(TEXT("expressionName"), NewExpr->GetName());
	MCPSetRollback(Result, TEXT("delete_material_expression"), Payload);
	return MCPResult(Result);
}

// material(action="connect_expressions_in_function", functionPath,
//          sourceExpression (name or index), sourceOutput?,
//          targetExpression (name or index), targetInput?)
TSharedPtr<FJsonValue> FMaterialHandlers::ConnectMaterialFunctionExpressions(const TSharedPtr<FJsonObject>& Params)
{
	FString FunctionPath;
	if (auto Err = RequireStringAlt(Params, TEXT("functionPath"), TEXT("materialFunctionPath"), FunctionPath)) return Err;

	UMaterialFunction* MF = LoadMaterialFunction(FunctionPath);
	if (!MF) return MCPError(FString::Printf(TEXT("MaterialFunction not found: %s"), *FunctionPath));

	auto ResolveExpr = [&](const TCHAR* Key) -> UMaterialExpression*
	{
		// Numeric index?
		int32 Idx = -1;
		if (Params->TryGetNumberField(Key, Idx))
		{
			if (Idx >= 0 && Idx < MF->GetExpressions().Num()) return MF->GetExpressions()[Idx];
			return nullptr;
		}
		FString Str;
		if (Params->TryGetStringField(Key, Str))
		{
			// FunctionInput/Output exposes InputName/OutputName; everything else uses Desc.
			for (UMaterialExpression* Expr : MF->GetExpressions())
			{
				if (!Expr) continue;
				if (Expr->Desc == Str) return Expr;
				if (Expr->GetName() == Str) return Expr;
				if (UMaterialExpressionFunctionInput* In = Cast<UMaterialExpressionFunctionInput>(Expr))
				{
					if (In->InputName.ToString() == Str) return Expr;
				}
				if (UMaterialExpressionFunctionOutput* Out = Cast<UMaterialExpressionFunctionOutput>(Expr))
				{
					if (Out->OutputName.ToString() == Str) return Expr;
				}
			}
			// Numeric in string form
			int32 ParsedIdx = FCString::Atoi(*Str);
			if (ParsedIdx >= 0 && ParsedIdx < MF->GetExpressions().Num() && Str.IsNumeric())
			{
				return MF->GetExpressions()[ParsedIdx];
			}
		}
		return nullptr;
	};

	UMaterialExpression* From = ResolveExpr(TEXT("sourceExpression"));
	UMaterialExpression* To = ResolveExpr(TEXT("targetExpression"));
	if (!From) return MCPError(TEXT("sourceExpression not found in function"));
	if (!To) return MCPError(TEXT("targetExpression not found in function"));

	FString SourceOutput = OptionalString(Params, TEXT("sourceOutput"));
	FString TargetInput = OptionalString(Params, TEXT("targetInput"));

	// Snapshot every input on the target BEFORE the write, then diff after it.
	// The engine decides which pin a targetInput name lands on, and guessing at
	// that resolution here would risk recording the wrong pin's previous state
	// and handing back a rollback that rewires something this call never
	// touched. The diff reads the answer off the graph instead of predicting it.
	struct FPinSnapshot { UMaterialExpression* Expression; int32 OutputIndex; };
	TArray<FPinSnapshot> Before;
	for (int32 i = 0; ; ++i)
	{
		FExpressionInput* In = To->GetInput(i);
		if (!In) break;
		Before.Add(FPinSnapshot{ In->Expression, In->OutputIndex });
	}

	// The source's outputs by index. An empty name is the default output, which
	// is what an omitted sourceOutput selects (#1138).
	TArray<TSharedPtr<FJsonValue>> SourceOutputs;
	TArray<FString> SourceOutputLabels;
	for (const FExpressionOutput& Output : From->GetOutputs())
	{
		const FString OutputName = Output.OutputName.ToString();
		SourceOutputs.Add(MakeShared<FJsonValueString>(OutputName));
		SourceOutputLabels.Add(OutputName.IsEmpty() ? TEXT("(default)") : OutputName);
	}

	const bool bOk = UMaterialEditingLibrary::ConnectMaterialExpressions(From, SourceOutput, To, TargetInput);
	if (!bOk)
	{
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetBoolField(TEXT("success"), false);
		Err->SetStringField(TEXT("error"), FString::Printf(TEXT("ConnectMaterialExpressions failed: '%s' -> '%s' (output='%s' input='%s'). Source outputs: [%s]"),
			*From->GetName(), *To->GetName(), *SourceOutput, *TargetInput, *FString::Join(SourceOutputLabels, TEXT(", "))));
		Err->SetArrayField(TEXT("sourceOutputs"), SourceOutputs);
		return MCPResult(Err);
	}

	int32 ChangedPin = INDEX_NONE;
	int32 ChangedPinCount = 0;
	for (int32 i = 0; i < Before.Num(); ++i)
	{
		FExpressionInput* In = To->GetInput(i);
		if (!In) break;
		if (In->Expression != Before[i].Expression || In->OutputIndex != Before[i].OutputIndex)
		{
			if (ChangedPin == INDEX_NONE) ChangedPin = i;
			++ChangedPinCount;
		}
	}

	UMaterialEditingLibrary::UpdateMaterialFunction(MF, nullptr);
	UEditorAssetLibrary::SaveAsset(MF->GetPathName());

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("functionPath"), MF->GetPathName());
	Result->SetStringField(TEXT("sourceExpression"), From->GetName());
	Result->SetStringField(TEXT("targetExpression"), To->GetName());
	Result->SetStringField(TEXT("sourceOutput"), SourceOutput);
	Result->SetStringField(TEXT("targetInput"), TargetInput);
	Result->SetNumberField(TEXT("changedInputIndex"), ChangedPin);
	Result->SetArrayField(TEXT("sourceOutputs"), SourceOutputs);
	if (ChangedPin != INDEX_NONE)
	{
		if (FExpressionInput* Wired = To->GetInput(ChangedPin))
		{
			Result->SetNumberField(TEXT("connectedOutputIndex"), Wired->OutputIndex);
			if (SourceOutputs.IsValidIndex(Wired->OutputIndex))
			{
				Result->SetStringField(TEXT("connectedOutputName"), SourceOutputs[Wired->OutputIndex]->AsString());
			}
		}
	}

	if (ChangedPin == INDEX_NONE)
	{
		// The engine reported success and no pin moved, so the wire this call
		// asks for was already there.
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The connection already existed: no input on the target expression changed. There is nothing to undo."));
		return MCPResult(Result);
	}

	MCPSetUpdated(Result);
	UMaterialExpression* PreviousExpression = Before[ChangedPin].Expression;
	const int32 PreviousOutputIndex = Before[ChangedPin].OutputIndex;
	const FString ChangedPinName = To->GetInputName(ChangedPin).ToString();
	Result->SetStringField(TEXT("changedInputName"), ChangedPinName);

	// Both keys have to be expressible in the vocabulary this action reads:
	// the target pin by name (an empty name means "first input" to the engine,
	// so a pin past the first with no name cannot be addressed) and the source
	// pin by output name (same rule).
	FString PreviousOutputName;
	if (PreviousExpression)
	{
		TArray<FExpressionOutput>& Outputs = PreviousExpression->GetOutputs();
		if (Outputs.IsValidIndex(PreviousOutputIndex))
		{
			PreviousOutputName = Outputs[PreviousOutputIndex].OutputName.ToString();
		}
		Result->SetStringField(TEXT("previousSourceExpression"), PreviousExpression->GetName());
		Result->SetNumberField(TEXT("previousSourceOutputIndex"), PreviousOutputIndex);
	}

	const bool bTargetPinAddressable = !ChangedPinName.IsEmpty() || ChangedPin == 0;
	const bool bSourcePinAddressable = !PreviousOutputName.IsEmpty() || PreviousOutputIndex == 0;

	if (PreviousExpression && bTargetPinAddressable && bSourcePinAddressable)
	{
		// Rollback: rewire the pin back to what it carried. sourceExpression and
		// targetExpression are passed as engine names, which the resolver above
		// matches directly.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("functionPath"), MF->GetPathName());
		Payload->SetStringField(TEXT("sourceExpression"), PreviousExpression->GetName());
		Payload->SetStringField(TEXT("sourceOutput"), PreviousOutputName);
		Payload->SetStringField(TEXT("targetExpression"), To->GetName());
		Payload->SetStringField(TEXT("targetInput"), ChangedPinName);
		MCPSetRollback(Result, TEXT("connect_expressions_in_function"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), ChangedPinCount > 1);
		if (ChangedPinCount > 1)
		{
			Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("%d inputs on the target expression changed, and the rollback restores only '%s'. Compare the graph against the others before relying on it."),
				ChangedPinCount, *ChangedPinName));
		}
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), PreviousExpression
			? FString::Printf(TEXT("The displaced connection cannot be replayed: connect_expressions_in_function addresses pins by name, and pin '%s' (input %d) or output %d on '%s' has no name, so an empty key would resolve to the first pin instead. Rewire it by hand in the Material Function editor."),
				*ChangedPinName, ChangedPin, PreviousOutputIndex, *PreviousExpression->GetName())
			: FString::Printf(TEXT("Input '%s' was unconnected before this call, and no action disconnects an input inside a MaterialFunction. Undoing this needs the target node removed and rebuilt, or an undo step."),
				*ChangedPinName));
	}
	return MCPResult(Result);
}

// material(action="list_expressions_in_function", functionPath)
TSharedPtr<FJsonValue> FMaterialHandlers::ListMaterialFunctionExpressions(const TSharedPtr<FJsonObject>& Params)
{
	FString FunctionPath;
	if (auto Err = RequireStringAlt(Params, TEXT("functionPath"), TEXT("materialFunctionPath"), FunctionPath)) return Err;

	UMaterialFunction* MF = LoadMaterialFunction(FunctionPath);
	if (!MF) return MCPError(FString::Printf(TEXT("MaterialFunction not found: %s"), *FunctionPath));

	TArray<TSharedPtr<FJsonValue>> Arr;
	int32 Index = 0;
	for (UMaterialExpression* Expr : MF->GetExpressions())
	{
		if (!Expr) { ++Index; continue; }
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("index"), Index);
		Obj->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
		Obj->SetStringField(TEXT("name"), Expr->GetName());
		Obj->SetStringField(TEXT("description"), Expr->Desc);
		Obj->SetNumberField(TEXT("positionX"), Expr->MaterialExpressionEditorX);
		Obj->SetNumberField(TEXT("positionY"), Expr->MaterialExpressionEditorY);
		if (UMaterialExpressionFunctionInput* In = Cast<UMaterialExpressionFunctionInput>(Expr))
		{
			Obj->SetStringField(TEXT("inputName"), In->InputName.ToString());
		}
		if (UMaterialExpressionFunctionOutput* Out = Cast<UMaterialExpressionFunctionOutput>(Expr))
		{
			Obj->SetStringField(TEXT("outputName"), Out->OutputName.ToString());
		}
		Arr.Add(MakeShared<FJsonValueObject>(Obj));
		++Index;
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("functionPath"), MF->GetPathName());
	Result->SetArrayField(TEXT("expressions"), Arr);
	Result->SetNumberField(TEXT("count"), Arr.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::DeleteFunctionExpression(UMaterialFunction* Function, UMaterialExpression* Expression, const FString& ExpressionName)
{
	const FString DeletedClass = Expression->GetClass()->GetName();
	const FString DeletedDesc = Expression->Desc;
	const int32 DeletedPosX = Expression->MaterialExpressionEditorX;
	const int32 DeletedPosY = Expression->MaterialExpressionEditorY;
	FString InputName;
	FString OutputName;
	if (UMaterialExpressionFunctionInput* In = Cast<UMaterialExpressionFunctionInput>(Expression)) InputName = In->InputName.ToString();
	if (UMaterialExpressionFunctionOutput* Out = Cast<UMaterialExpressionFunctionOutput>(Expression)) OutputName = Out->OutputName.ToString();

	// The wires into other nodes are what the rollback cannot restore, so name them.
	TArray<TSharedPtr<FJsonValue>> SeveredWires;
	for (UMaterialExpression* Other : Function->GetExpressions())
	{
		if (!Other || Other == Expression) continue;
		for (int32 i = 0; ; ++i)
		{
			FExpressionInput* Input = Other->GetInput(i);
			if (!Input) break;
			if (Input->Expression != Expression) continue;
			TSharedPtr<FJsonObject> Wire = MakeShared<FJsonObject>();
			Wire->SetStringField(TEXT("targetExpression"), Other->GetName());
			Wire->SetNumberField(TEXT("targetInputIndex"), i);
			Wire->SetNumberField(TEXT("sourceOutputIndex"), Input->OutputIndex);
			SeveredWires.Add(MakeShared<FJsonValueObject>(Wire));
		}
	}

	Function->Modify();
	// Breaks every link to the node before removing it.
	UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(Function, Expression);
	UMaterialEditingLibrary::UpdateMaterialFunction(Function, nullptr);
	const bool bSaved = UEditorAssetLibrary::SaveAsset(Function->GetPathName(), /*bOnlyIfIsDirty=*/false);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("functionPath"), Function->GetPathName());
	Result->SetStringField(TEXT("deletedExpression"), ExpressionName);
	Result->SetStringField(TEXT("deletedClass"), DeletedClass);
	Result->SetNumberField(TEXT("expressionCount"), Function->GetExpressions().Num());
	Result->SetBoolField(TEXT("deleted"), true);
	Result->SetBoolField(TEXT("saved"), bSaved);
	Result->SetArrayField(TEXT("severedExpressionInputs"), SeveredWires);

	// Rollback: a fresh node of the same class at the same spot. Values and wires are not restored.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("functionPath"), Function->GetPathName());
	Payload->SetStringField(TEXT("expressionType"), DeletedClass);
	Payload->SetNumberField(TEXT("positionX"), DeletedPosX);
	Payload->SetNumberField(TEXT("positionY"), DeletedPosY);
	if (!InputName.IsEmpty()) Payload->SetStringField(TEXT("inputName"), InputName);
	if (!OutputName.IsEmpty()) Payload->SetStringField(TEXT("outputName"), OutputName);
	MCPSetRollback(Result, TEXT("add_expression_in_function"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
		TEXT("The rollback adds a fresh %s at the same position%s, with default values and no wiring. It does not restore this node's property values or the %d input(s) listed in severedExpressionInputs; rewire those with connect_function_expressions."),
		*DeletedClass, DeletedDesc.IsEmpty() ? TEXT("") : TEXT(" (its description is not restored either)"), SeveredWires.Num()));
	return MCPResult(Result);
}
