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

	UClass* ResolveExpressionClass(const FString& InType)
	{
		FString ClassName = InType;
		if (!ClassName.StartsWith(TEXT("MaterialExpression")) && !ClassName.StartsWith(TEXT("UMaterialExpression")))
		{
			ClassName = TEXT("UMaterialExpression") + ClassName;
		}
		else if (!ClassName.StartsWith(TEXT("U")))
		{
			ClassName = TEXT("U") + ClassName;
		}
		UClass* Result = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::ExactClass);
		if (!Result)
		{
			Result = FindFirstObject<UClass>(*InType, EFindFirstObjectOptions::ExactClass);
		}
		if (!Result || !Result->IsChildOf(UMaterialExpression::StaticClass())) return nullptr;
		return Result;
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
	if (!ExprClass) return MCPError(FString::Printf(TEXT("Unknown expression type: %s"), *ExpressionType));

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

	// No inverse. The surface has no delete-expression action for a
	// MaterialFunction graph: delete_material_expression loads its target
	// through LoadMaterialFromPath, which returns a UMaterial, and a
	// UMaterialFunction is not one - pointing a rollback at it would fail with
	// "Failed to load material". The node stays until such an action exists.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
		TEXT("No action removes an expression from a MaterialFunction. delete_material_expression only operates on UMaterial graphs, so it cannot be used here. The node this call added is '%s' at index %d - remove it in the Material Function editor if the change has to be undone."),
		*NewExpr->GetName(), Index));
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

	const bool bOk = UMaterialEditingLibrary::ConnectMaterialExpressions(From, SourceOutput, To, TargetInput);
	if (!bOk)
	{
		return MCPError(FString::Printf(TEXT("ConnectMaterialExpressions failed: '%s' -> '%s' (output='%s' input='%s')"),
			*From->GetName(), *To->GetName(), *SourceOutput, *TargetInput));
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
