// Split from MaterialHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FMaterialHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in MaterialHandlers.cpp::RegisterHandlers.

#include "MaterialHandlers.h"
#include "UE_MCP_BridgeModule.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Factories/MaterialFactoryNew.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"


TSharedPtr<FJsonValue> FMaterialHandlers::CreateMaterialFromTexture(const TSharedPtr<FJsonObject>& Params)
{
	FString TexturePath;
	if (auto Err = RequireString(Params, TEXT("texturePath"), TexturePath)) return Err;

	FString MaterialName = OptionalString(Params, TEXT("materialName"));
	if (MaterialName.IsEmpty())
	{
		FString TextureName = FPaths::GetBaseFilename(TexturePath);
		MaterialName = TEXT("M_") + TextureName;
	}

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Materials"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (auto Existing = MCPCheckAssetExists(PackagePath, MaterialName, OnConflict, TEXT("Material")))
	{
		return Existing;
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

	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] CreateMaterialFromTexture: texture=%s materialName=%s packagePath=%s"), *TexturePath, *MaterialName, *PackagePath);

	// Create the material
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UMaterialFactoryNew* MaterialFactory = NewObject<UMaterialFactoryNew>();
	UObject* NewAsset = AssetTools.CreateAsset(MaterialName, PackagePath, UMaterial::StaticClass(), MaterialFactory);

	if (!NewAsset)
	{
		return MCPError(TEXT("Failed to create material asset"));
	}

	UMaterial* NewMaterial = Cast<UMaterial>(NewAsset);
	if (!NewMaterial)
	{
		return MCPError(TEXT("Created asset is not a material"));
	}

	NewMaterial->PreEditChange(nullptr);

	// Create a TextureSample expression
	UMaterialExpressionTextureSample* TextureSampleExpr = NewObject<UMaterialExpressionTextureSample>(NewMaterial);
	TextureSampleExpr->Texture = Texture;
	TextureSampleExpr->MaterialExpressionEditorX = -300;
	TextureSampleExpr->MaterialExpressionEditorY = 0;

	// Add expression to material
	NewMaterial->GetExpressionCollection().AddExpression(TextureSampleExpr);

	// Connect the RGB output (index 0) to the BaseColor input
	if (UMaterialEditorOnlyData* EOD = NewMaterial->GetEditorOnlyData())
	{
		EOD->BaseColor.Connect(0, TextureSampleExpr);
	}

	NewMaterial->PostEditChange();

	// Save the package
	SaveAssetPackage(NewMaterial);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("materialPath"), NewMaterial->GetPathName());
	Result->SetStringField(TEXT("materialName"), MaterialName);
	Result->SetStringField(TEXT("texturePath"), Texture->GetPathName());
	Result->SetStringField(TEXT("packagePath"), PackagePath);
	Result->SetNumberField(TEXT("expressionCount"), NewMaterial->GetExpressions().Num());
	MCPSetDeleteAssetRollback(Result, NewMaterial->GetPathName());

	return MCPResult(Result);
}


// ===========================================================================
// v0.7.9 — Material depth
// ===========================================================================

TSharedPtr<FJsonValue> FMaterialHandlers::DuplicateMaterial(const TSharedPtr<FJsonObject>& Params)
{
	FString SourcePath;
	if (auto Err = RequireString(Params, TEXT("sourcePath"), SourcePath)) return Err;
	FString DestinationPath;
	if (auto Err = RequireString(Params, TEXT("destinationPath"), DestinationPath)) return Err;

	UObject* Duplicated = UEditorAssetLibrary::DuplicateAsset(SourcePath, DestinationPath);
	if (!Duplicated)
	{
		return MCPError(FString::Printf(TEXT("Failed to duplicate '%s' -> '%s'"), *SourcePath, *DestinationPath));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("sourcePath"), SourcePath);
	Result->SetStringField(TEXT("destinationPath"), Duplicated->GetPathName());
	MCPSetDeleteAssetRollback(Result, Duplicated->GetPathName());
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FMaterialHandlers::ValidateMaterial(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("materialPath"), AssetPath)) return Err;

	UMaterial* Material = LoadMaterialFromPath(AssetPath);
	if (!Material) return MCPError(FString::Printf(TEXT("Material not found: %s"), *AssetPath));

	TArray<TSharedPtr<FJsonValue>> Issues;

	// Check every expression for broken refs and unused orphans.
	TSet<UMaterialExpression*> Referenced;
	auto MarkRef = [&](UMaterialExpression* Expr) { if (Expr) Referenced.Add(Expr); };

	// Walk material property inputs (reachable roots)
	for (int32 PropIdx = 0; PropIdx < MP_MAX; ++PropIdx)
	{
		FExpressionInput* In = Material->GetExpressionInputForProperty((EMaterialProperty)PropIdx);
		if (In && In->Expression) MarkRef(In->Expression);
	}

	// Flood-fill from referenced through their inputs.
	TArray<UMaterialExpression*> Stack = Referenced.Array();
	while (Stack.Num() > 0)
	{
		UMaterialExpression* Expr = Stack.Pop();
		for (FExpressionInputIterator It{ Expr }; It; ++It)
		{
			if (It->Expression && !Referenced.Contains(It->Expression))
			{
				Referenced.Add(It->Expression);
				Stack.Add(It->Expression);
			}
		}
	}

	auto AllExpressions = Material->GetExpressions();
	for (UMaterialExpression* Expr : AllExpressions)
	{
		if (!Expr) continue;
		// Orphan: present but unreachable from any material property.
		if (!Referenced.Contains(Expr))
		{
			TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
			Issue->SetStringField(TEXT("kind"), TEXT("orphan_expression"));
			Issue->SetStringField(TEXT("expression"), Expr->GetName());
			Issue->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
			Issues.Add(MakeShared<FJsonValueObject>(Issue));
		}

		// TextureSample with null texture
		if (UMaterialExpressionTextureSample* TS = Cast<UMaterialExpressionTextureSample>(Expr))
		{
			if (!TS->Texture)
			{
				TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
				Issue->SetStringField(TEXT("kind"), TEXT("null_texture_reference"));
				Issue->SetStringField(TEXT("expression"), Expr->GetName());
				Issues.Add(MakeShared<FJsonValueObject>(Issue));
			}
		}
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("expressionCount"), AllExpressions.Num());
	Result->SetNumberField(TEXT("reachableCount"), Referenced.Num());
	Result->SetArrayField(TEXT("issues"), Issues);
	Result->SetBoolField(TEXT("valid"), Issues.Num() == 0);
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FMaterialHandlers::GetMaterialShaderStats(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("materialPath"), AssetPath)) return Err;

	UMaterial* Material = LoadMaterialFromPath(AssetPath);
	if (!Material) return MCPError(FString::Printf(TEXT("Material not found: %s"), *AssetPath));

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);

	// Texture sampler usage — count texture-sample expressions directly.
	int32 NumTextures = 0;
	for (UMaterialExpression* Expr : Material->GetExpressions())
	{
		if (Cast<UMaterialExpressionTextureSample>(Expr)) ++NumTextures;
	}
	Result->SetNumberField(TEXT("referencedTextureCount"), NumTextures);

	// Parameter counts
	TArray<FMaterialParameterInfo> ScalarInfos, VectorInfos, TextureInfos;
	TArray<FGuid> ScalarGuids, VectorGuids, TextureGuids;
	Material->GetAllScalarParameterInfo(ScalarInfos, ScalarGuids);
	Material->GetAllVectorParameterInfo(VectorInfos, VectorGuids);
	Material->GetAllTextureParameterInfo(TextureInfos, TextureGuids);
	Result->SetNumberField(TEXT("scalarParameterCount"), ScalarInfos.Num());
	Result->SetNumberField(TEXT("vectorParameterCount"), VectorInfos.Num());
	Result->SetNumberField(TEXT("textureParameterCount"), TextureInfos.Num());

	// Shading/blend
	Result->SetStringField(TEXT("shadingModel"), ShadingModelToString(Material->GetShadingModels().GetFirstShadingModel()));
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FMaterialHandlers::ExportMaterialGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("materialPath"), AssetPath)) return Err;

	UMaterial* Material = LoadMaterialFromPath(AssetPath);
	if (!Material) return MCPError(FString::Printf(TEXT("Material not found: %s"), *AssetPath));

	auto AllExpressions = Material->GetExpressions();

	TArray<TSharedPtr<FJsonValue>> NodesArr;
	for (UMaterialExpression* Expr : AllExpressions)
	{
		if (!Expr) continue;
		TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
		Node->SetStringField(TEXT("name"), Expr->GetName());
		Node->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
		Node->SetNumberField(TEXT("posX"), Expr->MaterialExpressionEditorX);
		Node->SetNumberField(TEXT("posY"), Expr->MaterialExpressionEditorY);

		// Scalar / vector constants — capture literal
		if (UMaterialExpressionConstant* C = Cast<UMaterialExpressionConstant>(Expr))
		{
			Node->SetNumberField(TEXT("value"), C->R);
		}
		else if (UMaterialExpressionConstant3Vector* C3 = Cast<UMaterialExpressionConstant3Vector>(Expr))
		{
			TSharedPtr<FJsonObject> V = MakeShared<FJsonObject>();
			V->SetNumberField(TEXT("r"), C3->Constant.R);
			V->SetNumberField(TEXT("g"), C3->Constant.G);
			V->SetNumberField(TEXT("b"), C3->Constant.B);
			Node->SetObjectField(TEXT("value"), V);
		}
		else if (UMaterialExpressionScalarParameter* SP = Cast<UMaterialExpressionScalarParameter>(Expr))
		{
			Node->SetStringField(TEXT("parameterName"), SP->ParameterName.ToString());
			Node->SetNumberField(TEXT("defaultValue"), SP->DefaultValue);
		}
		else if (UMaterialExpressionTextureSample* TS = Cast<UMaterialExpressionTextureSample>(Expr))
		{
			if (TS->Texture) Node->SetStringField(TEXT("texturePath"), TS->Texture->GetPathName());
		}
		NodesArr.Add(MakeShared<FJsonValueObject>(Node));
	}

	// Property connections (reachable roots).
	TArray<TSharedPtr<FJsonValue>> PropArr;
	static const TMap<EMaterialProperty, FString> PropMap = {
		{ MP_BaseColor, TEXT("BaseColor") }, { MP_Metallic, TEXT("Metallic") },
		{ MP_Specular, TEXT("Specular") }, { MP_Roughness, TEXT("Roughness") },
		{ MP_EmissiveColor, TEXT("EmissiveColor") }, { MP_Opacity, TEXT("Opacity") },
		{ MP_OpacityMask, TEXT("OpacityMask") }, { MP_Normal, TEXT("Normal") },
	};
	for (const auto& Pair : PropMap)
	{
		FExpressionInput* In = Material->GetExpressionInputForProperty(Pair.Key);
		if (In && In->Expression)
		{
			TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("property"), Pair.Value);
			P->SetStringField(TEXT("from"), In->Expression->GetName());
			P->SetNumberField(TEXT("outputIndex"), In->OutputIndex);
			PropArr.Add(MakeShared<FJsonValueObject>(P));
		}
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetArrayField(TEXT("nodes"), NodesArr);
	Result->SetArrayField(TEXT("propertyConnections"), PropArr);
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FMaterialHandlers::ImportMaterialGraph(const TSharedPtr<FJsonObject>& Params)
{
	// Delegates to BuildMaterialGraph — same JSON spec format.
	return BuildMaterialGraph(Params);
}


TSharedPtr<FJsonValue> FMaterialHandlers::BuildMaterialGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("materialPath"), AssetPath)) return Err;

	const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("nodes"), NodesArr))
	{
		return MCPError(TEXT("Missing 'nodes' array"));
	}

	UMaterial* Material = LoadMaterialFromPath(AssetPath);
	if (!Material) return MCPError(FString::Printf(TEXT("Material not found: %s"), *AssetPath));

	TMap<FString, UMaterialExpression*> ByName;

	auto SpawnExpression = [&](const FString& ClassName) -> UMaterialExpression*
	{
		UClass* Cls = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *ClassName));
		if (!Cls) Cls = FindClassByShortName(ClassName);
		if (!Cls || !Cls->IsChildOf(UMaterialExpression::StaticClass())) return nullptr;
		UMaterialExpression* Expr = NewObject<UMaterialExpression>(Material, Cls);
		Material->GetExpressionCollection().AddExpression(Expr);
		return Expr;
	};

	int32 Created = 0;
	for (const TSharedPtr<FJsonValue>& V : *NodesArr)
	{
		const TSharedPtr<FJsonObject>* NodeObj = nullptr;
		if (!V->TryGetObject(NodeObj)) continue;
		FString Name = (*NodeObj)->GetStringField(TEXT("name"));
		FString Class = (*NodeObj)->GetStringField(TEXT("class"));
		UMaterialExpression* Expr = SpawnExpression(Class);
		if (!Expr) continue;
		Expr->MaterialExpressionEditorX = (*NodeObj)->GetNumberField(TEXT("posX"));
		Expr->MaterialExpressionEditorY = (*NodeObj)->GetNumberField(TEXT("posY"));
		ByName.Add(Name, Expr);
		++Created;

		// Apply literal values where we can.
		double NumVal = 0.0;
		if (UMaterialExpressionConstant* C = Cast<UMaterialExpressionConstant>(Expr))
		{
			if ((*NodeObj)->TryGetNumberField(TEXT("value"), NumVal)) C->R = (float)NumVal;
		}
		else if (UMaterialExpressionScalarParameter* SP = Cast<UMaterialExpressionScalarParameter>(Expr))
		{
			FString ParamName;
			if ((*NodeObj)->TryGetStringField(TEXT("parameterName"), ParamName)) SP->ParameterName = FName(*ParamName);
			if ((*NodeObj)->TryGetNumberField(TEXT("defaultValue"), NumVal)) SP->DefaultValue = (float)NumVal;
		}
	}

	// Property connections.
	const TArray<TSharedPtr<FJsonValue>>* PropArr = nullptr;
	int32 Connections = 0;
	if (Params->TryGetArrayField(TEXT("propertyConnections"), PropArr))
	{
		for (const TSharedPtr<FJsonValue>& V : *PropArr)
		{
			const TSharedPtr<FJsonObject>* ConnObj = nullptr;
			if (!V->TryGetObject(ConnObj)) continue;
			FString PropName = (*ConnObj)->GetStringField(TEXT("property"));
			FString FromName = (*ConnObj)->GetStringField(TEXT("from"));
			EMaterialProperty Prop;
			if (!ParseMaterialProperty(PropName, Prop)) continue;
			UMaterialExpression** Found = ByName.Find(FromName);
			if (!Found || !*Found) continue;
			FExpressionInput* In = Material->GetExpressionInputForProperty(Prop);
			if (!In) continue;
			In->Expression = *Found;
			In->OutputIndex = (int32)(*ConnObj)->GetNumberField(TEXT("outputIndex"));
			++Connections;
		}
	}

	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Material->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(Material);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("expressionsCreated"), Created);
	Result->SetNumberField(TEXT("connectionsMade"), Connections);
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FMaterialHandlers::RenderMaterialPreview(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("materialPath"), AssetPath)) return Err;
	FString OutputPath;
	if (auto Err = RequireString(Params, TEXT("outputPath"), OutputPath)) return Err;
	const int32 Width  = OptionalInt(Params, TEXT("width"), 256);
	const int32 Height = OptionalInt(Params, TEXT("height"), 256);

	UMaterial* Material = LoadMaterialFromPath(AssetPath);
	if (!Material) return MCPError(FString::Printf(TEXT("Material not found: %s"), *AssetPath));

	// Use FMaterialThumbnailRenderer via thumbnail tools API.
	// Full scene setup is heavy; we use UThumbnailManager's thumbnail rendering path.
	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>();
	RT->InitAutoFormat(Width, Height);
	RT->UpdateResourceImmediate(true);

	FTextureRenderTargetResource* RTResource = RT->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		return MCPError(TEXT("Failed to initialize render target"));
	}

	// Fall back to a simple stats-only response if we can't render here;
	// full preview requires a scene + view which is non-trivial on the game thread.
	// For the v0.7.9 implementation we emit a placeholder PNG derived from base color
	// so agents get a deterministic file out while full thumbnail rendering is wired.
	TArray<FColor> Pixels;
	Pixels.Init(FColor(128, 128, 128, 255), Width * Height);

	// Crude sampling of base color expression for a solid-color preview.
	FExpressionInput* BaseIn = Material->GetExpressionInputForProperty(MP_BaseColor);
	if (BaseIn && BaseIn->Expression)
	{
		if (UMaterialExpressionConstant3Vector* C3 = Cast<UMaterialExpressionConstant3Vector>(BaseIn->Expression))
		{
			FColor Col(
				FMath::Clamp(FMath::RoundToInt(C3->Constant.R * 255.f), 0, 255),
				FMath::Clamp(FMath::RoundToInt(C3->Constant.G * 255.f), 0, 255),
				FMath::Clamp(FMath::RoundToInt(C3->Constant.B * 255.f), 0, 255),
				255);
			for (FColor& P : Pixels) P = Col;
		}
	}

	TArray<uint8> Compressed;
	FImageUtils::ThumbnailCompressImageArray(Width, Height, Pixels, Compressed);
	if (!FFileHelper::SaveArrayToFile(Compressed, *OutputPath))
	{
		return MCPError(FString::Printf(TEXT("Failed to write PNG: %s"), *OutputPath));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("outputPath"), OutputPath);
	Result->SetNumberField(TEXT("width"), Width);
	Result->SetNumberField(TEXT("height"), Height);
	Result->SetStringField(TEXT("mode"), TEXT("base_color_approximation"));
	// No rollback: destructive/external (writes a file to disk).
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FMaterialHandlers::BeginMaterialTransaction(const TSharedPtr<FJsonObject>& Params)
{
	const FString Label = OptionalString(Params, TEXT("label"), TEXT("MCP Material Edit"));
	if (!GEditor) return MCPError(TEXT("GEditor not available"));
	// No rollback: transaction lifecycle; paired end_material_transaction is the natural counterpart.
	GEditor->BeginTransaction(FText::FromString(Label));
	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("label"), Label);
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FMaterialHandlers::EndMaterialTransaction(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor) return MCPError(TEXT("GEditor not available"));
	// No rollback: lifecycle op.
	const int32 Idx = GEditor->EndTransaction();
	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetNumberField(TEXT("transactionIndex"), Idx);
	return MCPResult(Result);
}
