// Split from MaterialHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FMaterialHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in MaterialHandlers.cpp::RegisterHandlers.

#include "MaterialHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "JsonSerializer.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "MaterialEditingLibrary.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"


TSharedPtr<FJsonValue> FMaterialHandlers::ConnectTextureToMaterial(const TSharedPtr<FJsonObject>& Params)
{
	FString MaterialPath;
	if (auto Err = RequireStringAlt(Params, TEXT("materialPath"), TEXT("path"), MaterialPath)) return Err;
	if (MaterialPath.IsEmpty())
	{
		Params->TryGetStringField(TEXT("assetPath"), MaterialPath);
		if (MaterialPath.IsEmpty())
		{
			return MCPError(TEXT("Missing required parameter 'materialPath' (or 'path')"));
		}
	}

	FString TexturePath;
	if (auto Err = RequireString(Params, TEXT("texturePath"), TexturePath)) return Err;

	FString PropertyName = TEXT("BaseColor");
	if (!Params->TryGetStringField(TEXT("property"), PropertyName))
	{
		Params->TryGetStringField(TEXT("materialProperty"), PropertyName);
	}

	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
	}

	// Load the texture
	UTexture* Texture = Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr, *TexturePath));
	if (!Texture)
	{
		Texture = Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr,
			*(TEXT("Texture2D'") + TexturePath + TEXT("'"))));
	}
	if (!Texture)
	{
		return MCPError(FString::Printf(TEXT("Failed to load texture at '%s'"), *TexturePath));
	}

	EMaterialProperty MatProperty;
	if (!ParseMaterialProperty(PropertyName, MatProperty))
	{
		return MCPError(FString::Printf(TEXT("Unknown material property '%s'"), *PropertyName));
	}

	Material->PreEditChange(nullptr);

	// Create a TextureSample expression.
	// Note: connect_texture_to_material adds a new TextureSample node every call
	// (not natural-key idempotent). Use connect_material_expressions with named
	// source/target expressions if idempotency is required.
	UMaterialExpressionTextureSample* TextureSampleExpr = NewObject<UMaterialExpressionTextureSample>(Material);
	TextureSampleExpr->Texture = Texture;
	TextureSampleExpr->MaterialExpressionEditorX = -400;
	TextureSampleExpr->MaterialExpressionEditorY = 0;

	Material->GetExpressionCollection().AddExpression(TextureSampleExpr);

	// Connect RGB output (index 0) to the requested material property
	UMaterialEditorOnlyData* EditorOnlyData = Material->GetEditorOnlyData();
	if (FExpressionInput* PropertyInput = GetMaterialPropertyInput(EditorOnlyData, MatProperty))
	{
		PropertyInput->Connect(0, TextureSampleExpr);
	}

	Material->PostEditChange();
	Material->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetStringField(TEXT("texturePath"), Texture->GetPathName());
	Result->SetStringField(TEXT("property"), PropertyName);
	Result->SetNumberField(TEXT("expressionCount"), Material->GetExpressions().Num());

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FMaterialHandlers::ConnectMaterialExpressions(const TSharedPtr<FJsonObject>& Params)
{
	FString MaterialPath;
	if (auto Err = RequireStringAlt(Params, TEXT("materialPath"), TEXT("path"), MaterialPath)) return Err;
	if (MaterialPath.IsEmpty())
	{
		Params->TryGetStringField(TEXT("assetPath"), MaterialPath);
		if (MaterialPath.IsEmpty())
		{
			return MCPError(TEXT("Missing required parameter 'materialPath' (or 'path')"));
		}
	}

	FString SourceExpressionName;
	if (auto Err = RequireString(Params, TEXT("sourceExpression"), SourceExpressionName)) return Err;

	FString TargetExpressionName;
	if (auto Err = RequireString(Params, TEXT("targetExpression"), TargetExpressionName)) return Err;

	// Source/target output/input can be specified by name or index
	FString SourceOutputName = OptionalString(Params, TEXT("sourceOutput"));
	FString TargetInputName = OptionalString(Params, TEXT("targetInput"));

	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
	}

	UMaterialExpression* SourceExpression = FindExpressionByName(Material, SourceExpressionName);
	if (!SourceExpression)
	{
		return MCPError(FString::Printf(TEXT("Source expression '%s' not found"), *SourceExpressionName));
	}

	UMaterialExpression* TargetExpression = FindExpressionByName(Material, TargetExpressionName);
	if (!TargetExpression)
	{
		return MCPError(FString::Printf(TEXT("Target expression '%s' not found"), *TargetExpressionName));
	}

	// Resolve source output index. Track whether we matched a named pin so
	// unknown names fail loudly instead of silently aliasing to index 0
	// and overwriting whatever connection lives there (#318).
	int32 SourceOutputIndex = 0;
	bool bSourceOutputResolved = SourceOutputName.IsEmpty(); // empty == "use default 0"
	if (!SourceOutputName.IsEmpty())
	{
		if (SourceOutputName.IsNumeric())
		{
			SourceOutputIndex = FCString::Atoi(*SourceOutputName);
			bSourceOutputResolved = true;
		}
		else
		{
			TArray<FExpressionOutput>& Outputs = SourceExpression->GetOutputs();
			for (int32 i = 0; i < Outputs.Num(); i++)
			{
				if (Outputs[i].OutputName.ToString().Equals(SourceOutputName, ESearchCase::IgnoreCase))
				{
					SourceOutputIndex = i;
					bSourceOutputResolved = true;
					break;
				}
			}
			if (!bSourceOutputResolved)
			{
				TArray<FString> Names;
				for (const FExpressionOutput& O : Outputs) { Names.Add(O.OutputName.ToString()); }
				return MCPError(FString::Printf(
					TEXT("Source output '%s' not found on '%s'. Available: [%s]"),
					*SourceOutputName, *SourceExpressionName,
					*FString::Join(Names, TEXT(", "))));
			}
		}
	}

	// Resolve target input index. Same loud-fail rule (#318) - this is the
	// case that previously aliased unknown names to A/RGB and clobbered prior
	// wiring.
	int32 TargetInputIndex = 0;
	bool bTargetInputResolved = TargetInputName.IsEmpty();
	if (!TargetInputName.IsEmpty())
	{
		if (TargetInputName.IsNumeric())
		{
			TargetInputIndex = FCString::Atoi(*TargetInputName);
			bTargetInputResolved = true;
		}
		else
		{
			for (int32 i = 0; ; i++)
			{
				FExpressionInput* Input = TargetExpression->GetInput(i);
				if (!Input) break;
				FName InputName = TargetExpression->GetInputName(i);
				if (InputName.ToString().Equals(TargetInputName, ESearchCase::IgnoreCase))
				{
					TargetInputIndex = i;
					bTargetInputResolved = true;
					break;
				}
			}
			if (!bTargetInputResolved)
			{
				TArray<FString> Names;
				for (int32 i = 0; ; i++)
				{
					if (!TargetExpression->GetInput(i)) break;
					Names.Add(TargetExpression->GetInputName(i).ToString());
				}
				return MCPError(FString::Printf(
					TEXT("Target input '%s' not found on '%s'. Available: [%s]"),
					*TargetInputName, *TargetExpressionName,
					*FString::Join(Names, TEXT(", "))));
			}
		}
	}

	FExpressionInput* TargetInput = TargetExpression->GetInput(TargetInputIndex);
	if (!TargetInput)
	{
		return MCPError(FString::Printf(TEXT("Target input index %d is out of range"), TargetInputIndex));
	}

	// Idempotency: already wired?
	if (TargetInput->Expression == SourceExpression && TargetInput->OutputIndex == SourceOutputIndex)
	{
		auto Existed = MCPSuccess();
		MCPSetExisted(Existed);
		Existed->SetStringField(TEXT("materialPath"), Material->GetPathName());
		Existed->SetStringField(TEXT("sourceExpression"), SourceExpressionName);
		Existed->SetStringField(TEXT("targetExpression"), TargetExpressionName);
		Existed->SetNumberField(TEXT("sourceOutputIndex"), SourceOutputIndex);
		Existed->SetNumberField(TEXT("targetInputIndex"), TargetInputIndex);
		return MCPResult(Existed);
	}

	Material->PreEditChange(nullptr);
	TargetInput->Connect(SourceOutputIndex, SourceExpression);
	Material->PostEditChange();
	Material->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetStringField(TEXT("sourceExpression"), SourceExpression->GetClass()->GetName());
	Result->SetStringField(TEXT("targetExpression"), TargetExpression->GetClass()->GetName());
	Result->SetNumberField(TEXT("sourceOutputIndex"), SourceOutputIndex);
	Result->SetNumberField(TEXT("targetInputIndex"), TargetInputIndex);
	// No rollback: no paired disconnect handler by names.

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FMaterialHandlers::ConnectToMaterialProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString MaterialPath;
	if (auto Err = RequireStringAlt(Params, TEXT("materialPath"), TEXT("path"), MaterialPath)) return Err;
	if (MaterialPath.IsEmpty())
	{
		Params->TryGetStringField(TEXT("assetPath"), MaterialPath);
		if (MaterialPath.IsEmpty())
		{
			return MCPError(TEXT("Missing required parameter 'materialPath' (or 'path')"));
		}
	}

	FString ExpressionName;
	if (auto Err = RequireString(Params, TEXT("expressionName"), ExpressionName)) return Err;

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("property"), PropertyName)) return Err;

	FString OutputName = OptionalString(Params, TEXT("outputName"));

	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
	}

	UMaterialExpression* Expression = FindExpressionByName(Material, ExpressionName);
	if (!Expression)
	{
		return MCPError(FString::Printf(TEXT("Expression '%s' not found"), *ExpressionName));
	}

	// Resolve output index
	int32 OutputIndex = 0;
	if (!OutputName.IsEmpty())
	{
		if (OutputName.IsNumeric())
		{
			OutputIndex = FCString::Atoi(*OutputName);
		}
		else
		{
			TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
			for (int32 i = 0; i < Outputs.Num(); i++)
			{
				if (Outputs[i].OutputName.ToString().Equals(OutputName, ESearchCase::IgnoreCase))
				{
					OutputIndex = i;
					break;
				}
			}
		}
	}

	EMaterialProperty MatProperty;
	if (!ParseMaterialProperty(PropertyName, MatProperty))
	{
		return MCPError(FString::Printf(TEXT("Unknown material property '%s'"), *PropertyName));
	}

	Material->PreEditChange(nullptr);

	UMaterialEditorOnlyData* EditorOnlyData = Material->GetEditorOnlyData();
	if (!EditorOnlyData)
	{
		return MCPError(TEXT("Material has no editor-only data (is this material domain supported?)"));
	}
	FExpressionInput* PropertyInput = GetMaterialPropertyInput(EditorOnlyData, MatProperty);
	if (!PropertyInput)
	{
		return MCPError(FString::Printf(TEXT("Material property '%s' is not supported for direct connection"), *PropertyName));
	}

	// Idempotency
	if (PropertyInput->Expression == Expression && PropertyInput->OutputIndex == OutputIndex)
	{
		auto Existed = MCPSuccess();
		MCPSetExisted(Existed);
		Existed->SetStringField(TEXT("materialPath"), Material->GetPathName());
		Existed->SetStringField(TEXT("expressionName"), ExpressionName);
		Existed->SetStringField(TEXT("property"), PropertyName);
		Existed->SetNumberField(TEXT("outputIndex"), OutputIndex);
		return MCPResult(Existed);
	}

	PropertyInput->Connect(OutputIndex, Expression);

	Material->PostEditChange();
	Material->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetStringField(TEXT("expressionName"), ExpressionName);
	Result->SetStringField(TEXT("expressionClass"), Expression->GetClass()->GetName());
	Result->SetStringField(TEXT("property"), PropertyName);
	Result->SetNumberField(TEXT("outputIndex"), OutputIndex);

	// Rollback: disconnect_material_property
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Payload->SetStringField(TEXT("property"), PropertyName);
	MCPSetRollback(Result, TEXT("disconnect_material_property"), Payload);

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FMaterialHandlers::DeleteMaterialExpression(const TSharedPtr<FJsonObject>& Params)
{
	FString MaterialPath;
	if (auto Err = RequireStringAlt(Params, TEXT("materialPath"), TEXT("path"), MaterialPath)) return Err;
	if (MaterialPath.IsEmpty())
	{
		Params->TryGetStringField(TEXT("assetPath"), MaterialPath);
		if (MaterialPath.IsEmpty())
		{
			return MCPError(TEXT("Missing required parameter 'materialPath' (or 'path')"));
		}
	}

	FString ExpressionName;
	if (auto Err = RequireString(Params, TEXT("expressionName"), ExpressionName)) return Err;

	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
	}

	UMaterialExpression* Expression = FindExpressionByName(Material, ExpressionName);
	if (!Expression)
	{
		// Idempotent: already deleted
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("materialPath"), Material->GetPathName());
		Noop->SetStringField(TEXT("expressionName"), ExpressionName);
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Noop);
	}

	FString DeletedClass = Expression->GetClass()->GetName();

	Material->PreEditChange(nullptr);

	// Disconnect all references from other expressions that point to this one
	for (UMaterialExpression* OtherExpr : Material->GetExpressions())
	{
		if (!OtherExpr || OtherExpr == Expression) continue;
		for (int32 i = 0; ; i++)
		{
			FExpressionInput* Input = OtherExpr->GetInput(i);
			if (!Input) break;
			if (Input->Expression == Expression)
			{
				Input->Expression = nullptr;
				Input->OutputIndex = 0;
			}
		}
	}

	// Disconnect any material property inputs that reference this expression
	UMaterialEditorOnlyData* EditorOnlyData = Material->GetEditorOnlyData();
	if (EditorOnlyData)
	{
		auto ClearIfMatch = [Expression](FExpressionInput& Input)
		{
			if (Input.Expression == Expression)
			{
				Input.Expression = nullptr;
				Input.OutputIndex = 0;
			}
		};
		ClearIfMatch(EditorOnlyData->BaseColor);
		ClearIfMatch(EditorOnlyData->Metallic);
		ClearIfMatch(EditorOnlyData->Specular);
		ClearIfMatch(EditorOnlyData->Roughness);
		ClearIfMatch(EditorOnlyData->Anisotropy);
		ClearIfMatch(EditorOnlyData->EmissiveColor);
		ClearIfMatch(EditorOnlyData->Opacity);
		ClearIfMatch(EditorOnlyData->OpacityMask);
		ClearIfMatch(EditorOnlyData->Normal);
		ClearIfMatch(EditorOnlyData->Tangent);
		ClearIfMatch(EditorOnlyData->WorldPositionOffset);
		ClearIfMatch(EditorOnlyData->SubsurfaceColor);
		ClearIfMatch(EditorOnlyData->AmbientOcclusion);
		ClearIfMatch(EditorOnlyData->Refraction);
		ClearIfMatch(EditorOnlyData->PixelDepthOffset);
	}

	Material->GetExpressionCollection().RemoveExpression(Expression);
	Material->PostEditChange();
	Material->MarkPackageDirty();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetStringField(TEXT("deletedExpression"), ExpressionName);
	Result->SetStringField(TEXT("deletedClass"), DeletedClass);
	Result->SetNumberField(TEXT("expressionCount"), Material->GetExpressions().Num());
	Result->SetBoolField(TEXT("deleted"), true);
	// No rollback: would require snapshotting the expression and all its connections.

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// disconnect_material_property -- Clear a material property input (#43)
// Params: materialPath, property (BaseColor, Normal, Roughness, etc.)
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// disconnect_material_property -- Clear a material property input (#43)
// Params: materialPath, property (BaseColor, Normal, Roughness, etc.)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FMaterialHandlers::DisconnectMaterialProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString MaterialPath;
	if (auto Err = RequireStringAlt(Params, TEXT("materialPath"), TEXT("assetPath"), MaterialPath)) return Err;

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("property"), PropertyName)) return Err;

	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
	}

	UMaterialEditorOnlyData* EditorOnlyData = Material->GetEditorOnlyData();
	if (!EditorOnlyData)
	{
		return MCPError(TEXT("Material has no editor-only data"));
	}

	Material->PreEditChange(nullptr);

	auto ClearInput = [](FExpressionInput& Input)
	{
		Input.Expression = nullptr;
		Input.OutputIndex = 0;
	};

	FString LowerProp = PropertyName.ToLower();
	bool bFound = true;

	if (LowerProp == TEXT("basecolor")) ClearInput(EditorOnlyData->BaseColor);
	else if (LowerProp == TEXT("metallic")) ClearInput(EditorOnlyData->Metallic);
	else if (LowerProp == TEXT("specular")) ClearInput(EditorOnlyData->Specular);
	else if (LowerProp == TEXT("roughness")) ClearInput(EditorOnlyData->Roughness);
	else if (LowerProp == TEXT("anisotropy")) ClearInput(EditorOnlyData->Anisotropy);
	else if (LowerProp == TEXT("emissivecolor") || LowerProp == TEXT("emissive")) ClearInput(EditorOnlyData->EmissiveColor);
	else if (LowerProp == TEXT("opacity")) ClearInput(EditorOnlyData->Opacity);
	else if (LowerProp == TEXT("opacitymask")) ClearInput(EditorOnlyData->OpacityMask);
	else if (LowerProp == TEXT("normal")) ClearInput(EditorOnlyData->Normal);
	else if (LowerProp == TEXT("tangent")) ClearInput(EditorOnlyData->Tangent);
	else if (LowerProp == TEXT("worldpositionoffset")) ClearInput(EditorOnlyData->WorldPositionOffset);
	else if (LowerProp == TEXT("subsurfacecolor")) ClearInput(EditorOnlyData->SubsurfaceColor);
	else if (LowerProp == TEXT("ambientocclusion")) ClearInput(EditorOnlyData->AmbientOcclusion);
	else if (LowerProp == TEXT("refraction")) ClearInput(EditorOnlyData->Refraction);
	else if (LowerProp == TEXT("pixeldepthoffset")) ClearInput(EditorOnlyData->PixelDepthOffset);
	else bFound = false;

	if (!bFound)
	{
		return MCPError(FString::Printf(
			TEXT("Unknown property '%s'. Use: BaseColor, Metallic, Specular, Roughness, EmissiveColor, Opacity, OpacityMask, Normal, Tangent, WorldPositionOffset, SubsurfaceColor, AmbientOcclusion, Refraction, PixelDepthOffset"),
			*PropertyName));
	}

	Material->PostEditChange();
	Material->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetStringField(TEXT("property"), PropertyName);
	// No rollback: we don't capture the previous expression binding before clearing.

	return MCPResult(Result);
}

