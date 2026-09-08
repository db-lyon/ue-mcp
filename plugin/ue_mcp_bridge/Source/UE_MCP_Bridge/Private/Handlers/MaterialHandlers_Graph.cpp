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

	// Connect RGB output (index 0) to the requested material property. Whatever
	// was wired into that property is overwritten here, so record it: deleting
	// the node this call added is the inverse of the ADD, not of the overwrite.
	UMaterialExpression* PreviousPropertyExpression = nullptr;
	int32 PreviousPropertyOutputIndex = 0;
	UMaterialEditorOnlyData* EditorOnlyData = Material->GetEditorOnlyData();
	if (FExpressionInput* PropertyInput = GetMaterialPropertyInput(EditorOnlyData, MatProperty))
	{
		PreviousPropertyExpression = PropertyInput->Expression;
		PreviousPropertyOutputIndex = PropertyInput->OutputIndex;
		PropertyInput->Connect(0, TextureSampleExpr);
	}

	Material->PostEditChange();
	Material->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetStringField(TEXT("texturePath"), Texture->GetPathName());
	Result->SetStringField(TEXT("property"), PropertyName);
	Result->SetStringField(TEXT("expressionName"), TextureSampleExpr->GetName());
	Result->SetNumberField(TEXT("expressionCount"), Material->GetExpressions().Num());

	// Rollback: delete the TextureSample this call created. delete_material_expression
	// also clears every input that referenced it, so the property input goes back
	// to unconnected - which is only the previous state when the property was
	// unconnected to begin with. Lossy whenever it was not.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Payload->SetStringField(TEXT("expressionName"), TextureSampleExpr->GetName());
	MCPSetRollback(Result, TEXT("delete_material_expression"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), PreviousPropertyExpression != nullptr);
	if (PreviousPropertyExpression)
	{
		Result->SetStringField(TEXT("previousPropertyExpression"), PreviousPropertyExpression->GetName());
		Result->SetNumberField(TEXT("previousPropertyOutputIndex"), PreviousPropertyOutputIndex);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("Deleting the TextureSample removes what this call added and leaves '%s' unconnected. It does NOT restore the connection this call overwrote: '%s' output %d was wired into '%s' before. Rewire it with connect_to_material_property expressionName='%s' outputName='%d'."),
			*PropertyName, *PreviousPropertyExpression->GetName(), PreviousPropertyOutputIndex, *PropertyName,
			*PreviousPropertyExpression->GetName(), PreviousPropertyOutputIndex));
	}

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

	// What this pin carried before the write is the only thing that can undo it.
	UMaterialExpression* PreviousExpression = TargetInput->Expression;
	const int32 PreviousOutputIndex = TargetInput->OutputIndex;

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

	if (PreviousExpression)
	{
		// Rollback: rewire the pin to what it carried before. Both pin keys are
		// passed as index strings, which the resolvers above read via
		// IsNumeric(), so a pin whose name this material does not spell the same
		// way still lands on the same slot. targetExpression replays the
		// caller's own string because that string already resolved once.
		Result->SetStringField(TEXT("previousSourceExpression"), PreviousExpression->GetName());
		Result->SetNumberField(TEXT("previousSourceOutputIndex"), PreviousOutputIndex);

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("materialPath"), Material->GetPathName());
		Payload->SetStringField(TEXT("sourceExpression"), PreviousExpression->GetName());
		Payload->SetStringField(TEXT("sourceOutput"), FString::FromInt(PreviousOutputIndex));
		Payload->SetStringField(TEXT("targetExpression"), TargetExpressionName);
		Payload->SetStringField(TEXT("targetInput"), FString::FromInt(TargetInputIndex));
		MCPSetRollback(Result, TEXT("connect_material_expressions"), Payload);
	}
	else
	{
		// No inverse: the pin was unconnected, and nothing in the surface clears
		// an expression input. disconnect_material_property only clears the
		// material's own property inputs, not a pin on an expression node.
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("Input %d on '%s' was unconnected before this call, and no action clears an expression input (disconnect_material_property only clears the material's own property inputs). Undoing this needs the target node deleted and rebuilt, or an undo step."),
			TargetInputIndex, *TargetExpressionName));
	}

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
	// Desc, not GetDescription(). Desc is the comment a level designer typed;
	// GetDescription() is the single-line caption the editor synthesises for
	// lists, and it falls back to a class-derived string when Desc is empty.
	// Feeding that back as `name` would give the replacement node a visible
	// comment the deleted node never had.
	const FString DeletedDesc = Expression->Desc;
	const int32 DeletedPosX = Expression->MaterialExpressionEditorX;
	const int32 DeletedPosY = Expression->MaterialExpressionEditorY;

	Material->PreEditChange(nullptr);

	// Disconnect all references from other expressions that point to this one.
	// Record each one: the wires are what a rollback cannot put back, so the
	// caller is at least told exactly which ones were cut.
	TArray<TSharedPtr<FJsonValue>> SeveredWires;
	for (UMaterialExpression* OtherExpr : Material->GetExpressions())
	{
		if (!OtherExpr || OtherExpr == Expression) continue;
		for (int32 i = 0; ; i++)
		{
			FExpressionInput* Input = OtherExpr->GetInput(i);
			if (!Input) break;
			if (Input->Expression == Expression)
			{
				TSharedPtr<FJsonObject> Wire = MakeShared<FJsonObject>();
				Wire->SetStringField(TEXT("targetExpression"), OtherExpr->GetName());
				Wire->SetNumberField(TEXT("targetInputIndex"), i);
				Wire->SetNumberField(TEXT("sourceOutputIndex"), Input->OutputIndex);
				SeveredWires.Add(MakeShared<FJsonValueObject>(Wire));
				Input->Expression = nullptr;
				Input->OutputIndex = 0;
			}
		}
	}

	// Disconnect any material property inputs that reference this expression
	TArray<TSharedPtr<FJsonValue>> SeveredProperties;
	UMaterialEditorOnlyData* EditorOnlyData = Material->GetEditorOnlyData();
	if (EditorOnlyData)
	{
		auto ClearIfMatch = [Expression, &SeveredProperties](FExpressionInput& Input, const TCHAR* PropertyLabel)
		{
			if (Input.Expression == Expression)
			{
				TSharedPtr<FJsonObject> Wire = MakeShared<FJsonObject>();
				Wire->SetStringField(TEXT("property"), PropertyLabel);
				Wire->SetNumberField(TEXT("sourceOutputIndex"), Input.OutputIndex);
				SeveredProperties.Add(MakeShared<FJsonValueObject>(Wire));
				Input.Expression = nullptr;
				Input.OutputIndex = 0;
			}
		};
		ClearIfMatch(EditorOnlyData->BaseColor, TEXT("BaseColor"));
		ClearIfMatch(EditorOnlyData->Metallic, TEXT("Metallic"));
		ClearIfMatch(EditorOnlyData->Specular, TEXT("Specular"));
		ClearIfMatch(EditorOnlyData->Roughness, TEXT("Roughness"));
		ClearIfMatch(EditorOnlyData->Anisotropy, TEXT("Anisotropy"));
		ClearIfMatch(EditorOnlyData->EmissiveColor, TEXT("EmissiveColor"));
		ClearIfMatch(EditorOnlyData->Opacity, TEXT("Opacity"));
		ClearIfMatch(EditorOnlyData->OpacityMask, TEXT("OpacityMask"));
		ClearIfMatch(EditorOnlyData->Normal, TEXT("Normal"));
		ClearIfMatch(EditorOnlyData->Tangent, TEXT("Tangent"));
		ClearIfMatch(EditorOnlyData->WorldPositionOffset, TEXT("WorldPositionOffset"));
		ClearIfMatch(EditorOnlyData->SubsurfaceColor, TEXT("SubsurfaceColor"));
		ClearIfMatch(EditorOnlyData->AmbientOcclusion, TEXT("AmbientOcclusion"));
		ClearIfMatch(EditorOnlyData->Refraction, TEXT("Refraction"));
		ClearIfMatch(EditorOnlyData->PixelDepthOffset, TEXT("PixelDepthOffset"));
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
	Result->SetArrayField(TEXT("severedExpressionInputs"), SeveredWires);
	Result->SetArrayField(TEXT("severedProperties"), SeveredProperties);

	// Rollback: put a node of the same class back at the same spot. That is the
	// honest half of the inverse. Its authored values (a texture reference, a
	// constant, a parameter's default) and every wire listed above are gone,
	// because nothing was snapshotted before the delete and one rollback record
	// carries one call. The class name is passed as-is: add_material_expression
	// prefixes a "U" onto a MaterialExpression* name, so it resolves the class
	// this node really had.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Payload->SetStringField(TEXT("expressionType"), DeletedClass);
	Payload->SetNumberField(TEXT("positionX"), DeletedPosX);
	Payload->SetNumberField(TEXT("positionY"), DeletedPosY);
	if (!DeletedDesc.IsEmpty())
	{
		Payload->SetStringField(TEXT("name"), DeletedDesc);
	}
	MCPSetRollback(Result, TEXT("add_material_expression"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
		TEXT("The rollback adds a fresh %s at the same position, with default values and no wiring. It does NOT restore this node's authored property values, and it does NOT restore the %d expression input(s) and %d material property input(s) listed in severedExpressionInputs / severedProperties - rewire those with connect_material_expressions and connect_to_material_property. The replacement also gets a new engine name, so anything that addressed this node by name has to be re-pointed."),
		*DeletedClass, SeveredWires.Num(), SeveredProperties.Num()));

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

	// Read the binding out before clearing it. Without this the handler could
	// only report that it cleared something, never what, and there was nothing
	// to hand a rollback.
	UMaterialExpression* PreviousExpression = nullptr;
	int32 PreviousOutputIndex = 0;
	auto ClearInput = [&PreviousExpression, &PreviousOutputIndex](FExpressionInput& Input)
	{
		PreviousExpression = Input.Expression;
		PreviousOutputIndex = Input.OutputIndex;
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
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetStringField(TEXT("property"), PropertyName);

	if (PreviousExpression)
	{
		MCPSetUpdated(Result);
		Result->SetStringField(TEXT("previousExpression"), PreviousExpression->GetName());
		Result->SetNumberField(TEXT("previousOutputIndex"), PreviousOutputIndex);

		// Rollback: reconnect exactly what was cleared. connect_to_material_property
		// reads outputName numerically when it parses as a number, so the output
		// index round-trips without needing the pin's display name.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("materialPath"), Material->GetPathName());
		Payload->SetStringField(TEXT("expressionName"), PreviousExpression->GetName());
		Payload->SetStringField(TEXT("property"), PropertyName);
		Payload->SetStringField(TEXT("outputName"), FString::FromInt(PreviousOutputIndex));
		MCPSetRollback(Result, TEXT("connect_to_material_property"), Payload);
	}
	else
	{
		// Nothing was wired into the property, so nothing changed and there is
		// nothing to undo. Replaying this call is a no-op by construction.
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("'%s' was already unconnected, so this call changed nothing and there is no inverse to run."), *PropertyName));
	}

	return MCPResult(Result);
}

