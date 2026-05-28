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
	Result->SetNumberField(TEXT("expressionIndex"), Index);
	Result->SetStringField(TEXT("nodeId"), FString::FromInt(Index));
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

	const bool bOk = UMaterialEditingLibrary::ConnectMaterialExpressions(From, SourceOutput, To, TargetInput);
	if (!bOk)
	{
		return MCPError(FString::Printf(TEXT("ConnectMaterialExpressions failed: '%s' -> '%s' (output='%s' input='%s')"),
			*From->GetName(), *To->GetName(), *SourceOutput, *TargetInput));
	}

	UMaterialEditingLibrary::UpdateMaterialFunction(MF, nullptr);
	UEditorAssetLibrary::SaveAsset(MF->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("functionPath"), MF->GetPathName());
	Result->SetStringField(TEXT("sourceExpression"), From->GetName());
	Result->SetStringField(TEXT("targetExpression"), To->GetName());
	Result->SetStringField(TEXT("sourceOutput"), SourceOutput);
	Result->SetStringField(TEXT("targetInput"), TargetInput);
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
