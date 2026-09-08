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
#include "Editor/Transactor.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "ThumbnailRendering/ThumbnailRenderer.h"
#include "CanvasTypes.h"
#include "RenderingThread.h"
#include "TextureResource.h"


// ===========================================================================
// v0.7.9 - Material depth
// ===========================================================================

TSharedPtr<FJsonValue> FMaterialHandlers::DuplicateMaterial(const TSharedPtr<FJsonObject>& Params)
{
	FString SourcePath;
	if (auto Err = RequireString(Params, TEXT("sourcePath"), SourcePath)) return Err;
	FString DestinationPath;
	if (auto Err = RequireString(Params, TEXT("destinationPath"), DestinationPath)) return Err;

	const FString DestinationPackagePath = FPackageName::ObjectPathToPackageName(DestinationPath);
	FString DestinationDirectory;
	FString DestinationAssetName;
	if (DestinationPackagePath.Split(TEXT("/"), &DestinationDirectory, &DestinationAssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd)
		&& !UEditorAssetLibrary::DoesDirectoryExist(DestinationDirectory))
	{
		UEditorAssetLibrary::MakeDirectory(DestinationDirectory);
	}

	UObject* Duplicated = UEditorAssetLibrary::DuplicateAsset(SourcePath, DestinationPackagePath);
	if (!Duplicated)
	{
		return MCPError(FString::Printf(TEXT("Failed to duplicate '%s' -> '%s'"), *SourcePath, *DestinationPackagePath));
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
#if UE_MCP_HAS_5_5_API
		for (FExpressionInputIterator It{ Expr }; It; ++It)
		{
			if (It->Expression && !Referenced.Contains(It->Expression))
			{
				Referenced.Add(It->Expression);
				Stack.Add(It->Expression);
			}
		}
#else
		// FExpressionInputIterator was added in 5.5; on 5.4 use the legacy GetInput(i) loop.
		for (int32 InputIdx = 0, InputCount = Expr->GetInputs().Num(); InputIdx < InputCount; ++InputIdx)
		{
			FExpressionInput* In = Expr->GetInput(InputIdx);
			if (In && In->Expression && !Referenced.Contains(In->Expression))
			{
				Referenced.Add(In->Expression);
				Stack.Add(In->Expression);
			}
		}
#endif
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

			// #318 gap 7: UV type sanity check. TextureSample.Coordinates expects
			// a 2-channel UV vector. Wiring a 3-channel world-position (or any
			// non-UV3D-sized source) into it compiles but samples garbage. The
			// silent failure mode is hours of "why does my texture look wrong"
			// debugging. Flag the obvious cases - WorldPosition / ObjectPosition
			// / ActorPosition / CameraPosition wired into Coordinates.
			if (TS->Coordinates.Expression)
			{
				UMaterialExpression* CoordSrc = TS->Coordinates.Expression;
				const FString SrcClass = CoordSrc->GetClass()->GetName();
				static const TArray<FString> ThreeDPositionSources = {
					TEXT("MaterialExpressionWorldPosition"),
					TEXT("MaterialExpressionObjectPositionWS"),
					TEXT("MaterialExpressionActorPositionWS"),
					TEXT("MaterialExpressionCameraPositionWS"),
				};
				if (ThreeDPositionSources.Contains(SrcClass))
				{
					TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
					Issue->SetStringField(TEXT("kind"), TEXT("uv_type_mismatch"));
					Issue->SetStringField(TEXT("expression"), Expr->GetName());
					Issue->SetStringField(TEXT("input"), TEXT("Coordinates"));
					Issue->SetStringField(TEXT("sourceClass"), SrcClass);
					Issue->SetStringField(TEXT("message"), FString::Printf(
						TEXT("TextureSample '%s' Coordinates wired from %s. The Coordinates pin expects a 2-channel UV. Use a TextureCoordinate node or extract the XY channels via ComponentMask."),
						*Expr->GetName(), *SrcClass));
					Issues.Add(MakeShared<FJsonValueObject>(Issue));
				}
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

	// Texture sampler usage - count texture-sample expressions directly.
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

		// Scalar / vector constants - capture literal
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
	// Delegates to BuildMaterialGraph - same JSON spec format, same effect.
	TSharedPtr<FJsonValue> Delegated = BuildMaterialGraph(Params);

	// Say so on THIS action's own result. import_material_graph is registered
	// separately, so a caller reading its response never sees build_material_graph
	// and would otherwise have to infer where the answer came from. The inverse
	// verdict is restated here rather than left implicit for the same reason:
	// this action has no inverse either, and it is not obvious that an action
	// named "import" is additive rather than a replace.
	const TSharedPtr<FJsonObject>* AsObject = nullptr;
	if (Delegated.IsValid() && Delegated->TryGetObject(AsObject) && AsObject && (*AsObject).IsValid())
	{
		bool bSucceeded = false;
		if ((*AsObject)->TryGetBoolField(TEXT("success"), bSucceeded) && bSucceeded)
		{
			// TryGet, not Get: FJsonObject::GetStringField on an absent key is a
			// hard failure, and this must not turn a healthy import into one.
			FString DelegatedNote;
			(*AsObject)->TryGetStringField(TEXT("rollbackNote"), DelegatedNote);
			(*AsObject)->SetStringField(TEXT("aliasOf"), TEXT("build_material_graph"));
			(*AsObject)->SetBoolField(TEXT("rollbackPossible"), false);
			(*AsObject)->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("import_material_graph is build_material_graph under another name, and it ADDS to the graph rather than replacing it: there is no replace mode that could be handed a snapshot of the previous graph, and no bulk delete for material expressions, so no single call undoes it. %s"),
				*DelegatedNote));
		}
	}
	return Delegated;
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
	TArray<TSharedPtr<FJsonValue>> CreatedExpressionNames;
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
		CreatedExpressionNames.Add(MakeShared<FJsonValueString>(Expr->GetName()));
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

	// Property connections. Each one overwrites whatever the property carried,
	// so record the displaced binding: it is the half of the change that no
	// single inverse call can put back.
	const TArray<TSharedPtr<FJsonValue>>* PropArr = nullptr;
	int32 Connections = 0;
	TArray<TSharedPtr<FJsonValue>> OverwrittenProperties;
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
			if (In->Expression)
			{
				TSharedPtr<FJsonObject> Displaced = MakeShared<FJsonObject>();
				Displaced->SetStringField(TEXT("property"), PropName);
				Displaced->SetStringField(TEXT("previousFrom"), In->Expression->GetName());
				Displaced->SetNumberField(TEXT("previousOutputIndex"), In->OutputIndex);
				OverwrittenProperties.Add(MakeShared<FJsonValueObject>(Displaced));
			}
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
	Result->SetArrayField(TEXT("createdExpressions"), CreatedExpressionNames);
	Result->SetArrayField(TEXT("overwrittenProperties"), OverwrittenProperties);

	// No inverse. This call creates an arbitrary number of expressions and
	// rewrites an arbitrary number of property inputs, and a rollback record
	// carries exactly one call. There is no bulk delete for material
	// expressions, and this action has no replace mode that could be handed a
	// snapshot of the previous graph (the PCG twin, import_pcg_graph, does have
	// one and is reversible for that reason). Inventing a single
	// delete_material_expression here would undo one node out of many and
	// leave the rest, which is worse than saying so.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
		TEXT("No single call undoes this: %d expression(s) were created and %d property connection(s) written. Undo it by hand with one delete_material_expression per name in createdExpressions, then reconnect anything listed in overwrittenProperties with connect_to_material_property. Wrapping the build in begin_transaction / end_transaction gives a one-step undo instead."),
		Created, Connections));
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

	// #668: render a REAL lit preview using the engine's own material thumbnail
	// renderer (a lit sphere with the material, the same image the Content
	// Browser shows) - which correctly lit-renders any shading model including
	// Substrate, instead of the flat base-color swatch.
	TArray<FColor> Pixels;
	FString Mode = TEXT("base_color_approximation");
	bool bLitRendered = false;

	FThumbnailRenderingInfo* RenderInfo = UThumbnailManager::Get().GetRenderingInfo(Material);
	if (RenderInfo && RenderInfo->Renderer)
	{
		FCanvas Canvas(RTResource, nullptr, FGameTime::GetTimeSinceAppStart(), GMaxRHIFeatureLevel);
		RenderInfo->Renderer->Draw(Material, 0, 0, Width, Height, RTResource, &Canvas, false);
		Canvas.Flush_GameThread();
		FlushRenderingCommands();
		FReadSurfaceDataFlags ReadFlags(RCM_UNorm, CubeFace_MAX);
		if (RTResource->ReadPixels(Pixels, ReadFlags) && Pixels.Num() == Width * Height)
		{
			bLitRendered = true;
			Mode = TEXT("lit_thumbnail");
		}
	}

	if (!bLitRendered)
	{
		// Fallback: flat base-color swatch if the thumbnail renderer is unavailable.
		Pixels.Init(FColor(128, 128, 128, 255), Width * Height);
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
	}

	// Whether the destination already held a file decides what this call did to
	// the disk: created one, or replaced one whose bytes are not recoverable.
	const bool bOverwroteExistingFile = FPaths::FileExists(OutputPath);

	TArray<uint8> Compressed;
	FImageUtils::ThumbnailCompressImageArray(Width, Height, Pixels, Compressed);

	// Whether the render actually changed anything on disk. A replayed call
	// against an unchanged material produces the same bytes, and saying so is
	// the difference between "wrote a file" and "the file already said this".
	bool bBytesIdentical = false;
	if (bOverwroteExistingFile)
	{
		TArray<uint8> Existing;
		if (FFileHelper::LoadFileToArray(Existing, *OutputPath))
		{
			bBytesIdentical = Existing == Compressed;
		}
	}

	if (!bBytesIdentical && !FFileHelper::SaveArrayToFile(Compressed, *OutputPath))
	{
		return MCPError(FString::Printf(TEXT("Failed to write PNG: %s"), *OutputPath));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("outputPath"), OutputPath);
	Result->SetNumberField(TEXT("width"), Width);
	Result->SetNumberField(TEXT("height"), Height);
	Result->SetBoolField(TEXT("litRendered"), bLitRendered);
	Result->SetStringField(TEXT("mode"), Mode);
	Result->SetBoolField(TEXT("overwroteExistingFile"), bOverwroteExistingFile && !bBytesIdentical);
	if (bBytesIdentical) Result->SetBoolField(TEXT("unchanged"), true);
	else MCPSetUpdated(Result);

	// No inverse. The effect is a file on disk outside the project, and the
	// bridge has no action that deletes an arbitrary path (delete_asset works
	// on content-browser assets). The material itself is untouched.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	if (bBytesIdentical)
	{
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The file at outputPath already held exactly these bytes, so nothing was written and there is nothing to undo."));
	}
	else
	{
		Result->SetStringField(TEXT("rollbackNote"), bOverwroteExistingFile
			? TEXT("This replaced a different file that existed at outputPath. Its previous bytes were not kept and no bridge action deletes or restores an arbitrary file path, so there is no inverse. No project asset was modified.")
			: TEXT("This wrote a new file at outputPath. No bridge action deletes an arbitrary file path, so there is no inverse. No project asset was modified."));
	}
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FMaterialHandlers::BeginMaterialTransaction(const TSharedPtr<FJsonObject>& Params)
{
	const FString Label = OptionalString(Params, TEXT("label"), TEXT("MCP Material Edit"));
	if (!GEditor) return MCPError(TEXT("GEditor not available"));
	// Refuse BEFORE opening anything. UEditorEngine::BeginTransaction
	// dereferences GEditor->Trans without checking it, so a check placed after
	// the call is only reachable past the crash it describes. The editor twin
	// (begin_editor_transaction) guards in the same place for the same reason.
	if (!GEditor->Trans)
	{
		return MCPError(TEXT("No transaction buffer: GEditor->Trans is null. The editor was started without an undo buffer (a commandlet or -notransactions session), so transactions are unavailable in this process."));
	}

	const bool bWasActive = GEditor->Trans->IsActive();
	const int32 Idx = GEditor->BeginTransaction(FText::FromString(Label));

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("label"), Label);
	Result->SetNumberField(TEXT("transactionIndex"), Idx);
	// A transaction opened inside another one only nests: the undo step is not
	// recorded until the outermost end. Saying so is what stops a caller reading
	// this as "a fresh undo step starts here".
	Result->SetBoolField(TEXT("nested"), bWasActive);

	// Rollback: abort the transaction rather than commit it. This is the same
	// inverse the editor twin (begin_editor_transaction) emits, and the reason
	// cancel exists at all - a flow that fails after this call unwinds to the
	// state before it instead of committing half an edit.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetNumberField(TEXT("index"), Idx);
	MCPSetRollback(Result, TEXT("cancel_editor_transaction"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), false);
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FMaterialHandlers::EndMaterialTransaction(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor) return MCPError(TEXT("GEditor not available"));

	// Refuse BEFORE ending anything: UEditorEngine::EndTransaction dereferences
	// GEditor->Trans unconditionally, so a check after the call is unreachable.
	UTransactor* Trans = GEditor->Trans;
	if (!Trans)
	{
		return MCPError(TEXT("No transaction buffer: GEditor->Trans is null. The editor was started without an undo buffer (a commandlet or -notransactions session), so transactions are unavailable in this process."));
	}

	if (!Trans->IsActive())
	{
		// Ending when nothing is open is the idempotent replay of a flow that
		// already ended. Reported rather than errored, matching the editor twin.
		TSharedPtr<FJsonObject> NoOp = MCPSuccess();
		NoOp->SetBoolField(TEXT("wasActive"), false);
		NoOp->SetBoolField(TEXT("committed"), false);
		NoOp->SetBoolField(TEXT("unchanged"), true);
		NoOp->SetBoolField(TEXT("rollbackPossible"), false);
		NoOp->SetStringField(TEXT("rollbackNote"),
			TEXT("No transaction was open, so nothing was committed and there is nothing to undo."));
		return MCPResult(NoOp);
	}

	const int32 Idx = GEditor->EndTransaction();
	const bool bStillNested = Trans->IsActive();

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetNumberField(TEXT("transactionIndex"), Idx);
	Result->SetBoolField(TEXT("wasActive"), true);
	Result->SetBoolField(TEXT("committed"), true);
	Result->SetBoolField(TEXT("stillNested"), bStillNested);

	if (!bStillNested)
	{
		// The inverse of committing is undoing the step just recorded. Only
		// once the nest is fully closed, because before that the step does not
		// exist in the buffer yet and an undo would reverse someone else's.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetNumberField(TEXT("steps"), 1);
		Payload->SetStringField(TEXT("direction"), TEXT("undo"));
		MCPSetRollback(Result, TEXT("undo_redo_steps"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), false);
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("This closed one nesting level and an outer transaction is still open, so no undo step has been recorded yet. Undoing here would reverse whatever step precedes the open transaction, which is not what this call did. Close the outermost level first."));
	}
	return MCPResult(Result);
}
