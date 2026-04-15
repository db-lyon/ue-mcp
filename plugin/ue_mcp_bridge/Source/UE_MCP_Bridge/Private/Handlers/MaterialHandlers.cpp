#include "MaterialHandlers.h"
#include "UE_MCP_BridgeModule.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Factories/MaterialFactoryNew.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"

void FMaterialHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("list_expression_types"), &ListExpressionTypes);
	Registry.RegisterHandler(TEXT("create_material"), &CreateMaterial);
	Registry.RegisterHandler(TEXT("read_material"), &ReadMaterial);
	Registry.RegisterHandler(TEXT("set_material_shading_model"), &SetMaterialShadingModel);
	Registry.RegisterHandler(TEXT("set_material_blend_mode"), &SetMaterialBlendMode);
	Registry.RegisterHandler(TEXT("set_material_base_color"), &SetMaterialBaseColor);
	Registry.RegisterHandler(TEXT("add_material_expression"), &AddMaterialExpression);
	Registry.RegisterHandler(TEXT("list_material_expressions"), &ListMaterialExpressions);
	Registry.RegisterHandler(TEXT("list_material_parameters"), &ListMaterialParameters);
	Registry.RegisterHandler(TEXT("recompile_material"), &RecompileMaterial);
	Registry.RegisterHandler(TEXT("create_material_instance"), &CreateMaterialInstance);
	Registry.RegisterHandler(TEXT("set_material_parameter"), &SetMaterialParameter);
	Registry.RegisterHandler(TEXT("connect_expression"), &ConnectExpression);
	Registry.RegisterHandler(TEXT("connect_material_property"), &ConnectMaterialProperty);
	Registry.RegisterHandler(TEXT("delete_expression"), &DeleteExpression);
	Registry.RegisterHandler(TEXT("set_expression_value"), &SetExpressionValue);
	Registry.RegisterHandler(TEXT("create_material_from_texture"), &CreateMaterialFromTexture);
	Registry.RegisterHandler(TEXT("read_material_instance"), &ReadMaterialInstance);

	// TS-expected name aliases
	Registry.RegisterHandler(TEXT("connect_texture_to_material"), &ConnectTextureToMaterial);
	Registry.RegisterHandler(TEXT("connect_material_expressions"), &ConnectMaterialExpressions);
	Registry.RegisterHandler(TEXT("connect_to_material_property"), &ConnectToMaterialProperty);
	Registry.RegisterHandler(TEXT("delete_material_expression"), &DeleteMaterialExpression);
	Registry.RegisterHandler(TEXT("disconnect_material_property"), &DisconnectMaterialProperty);

	// v0.7.9 — depth
	Registry.RegisterHandler(TEXT("duplicate_material"), &DuplicateMaterial);
	Registry.RegisterHandler(TEXT("validate_material"), &ValidateMaterial);
	Registry.RegisterHandler(TEXT("get_material_shader_stats"), &GetMaterialShaderStats);
	Registry.RegisterHandler(TEXT("export_material_graph"), &ExportMaterialGraph);
	Registry.RegisterHandler(TEXT("import_material_graph"), &ImportMaterialGraph);
	Registry.RegisterHandler(TEXT("build_material_graph"), &BuildMaterialGraph);
	Registry.RegisterHandler(TEXT("render_material_preview"), &RenderMaterialPreview);
	Registry.RegisterHandler(TEXT("begin_material_transaction"), &BeginMaterialTransaction);
	Registry.RegisterHandler(TEXT("end_material_transaction"), &EndMaterialTransaction);
}

UMaterial* FMaterialHandlers::LoadMaterialFromPath(const FString& AssetPath)
{
	UObject* LoadedObject = StaticLoadObject(UMaterial::StaticClass(), nullptr, *AssetPath);
	if (!LoadedObject)
	{
		// Try with explicit class prefix
		LoadedObject = StaticLoadObject(UMaterial::StaticClass(), nullptr, *(TEXT("Material'") + AssetPath + TEXT("'")));
	}
	return Cast<UMaterial>(LoadedObject);
}

UMaterialInstanceConstant* FMaterialHandlers::LoadMaterialInstanceFromPath(const FString& AssetPath)
{
	UObject* LoadedObject = StaticLoadObject(UMaterialInstanceConstant::StaticClass(), nullptr, *AssetPath);
	if (!LoadedObject)
	{
		// Try with explicit class prefix
		LoadedObject = StaticLoadObject(UMaterialInstanceConstant::StaticClass(), nullptr,
			*(TEXT("MaterialInstanceConstant'") + AssetPath + TEXT("'")));
	}
	return Cast<UMaterialInstanceConstant>(LoadedObject);
}

EMaterialShadingModel FMaterialHandlers::ParseShadingModel(const FString& ShadingModelStr)
{
	FString Lower = ShadingModelStr.ToLower();
	if (Lower == TEXT("unlit"))                return MSM_Unlit;
	if (Lower == TEXT("defaultlit"))           return MSM_DefaultLit;
	if (Lower == TEXT("subsurface"))           return MSM_Subsurface;
	if (Lower == TEXT("subsurfaceprofile"))    return MSM_SubsurfaceProfile;
	if (Lower == TEXT("preintegratedskin"))    return MSM_PreintegratedSkin;
	if (Lower == TEXT("clearcoa") || Lower == TEXT("clearcoat")) return MSM_ClearCoat;
	if (Lower == TEXT("cloth"))                return MSM_Cloth;
	if (Lower == TEXT("eye"))                  return MSM_Eye;
	if (Lower == TEXT("twosidedfoliage"))      return MSM_TwoSidedFoliage;
	return MSM_DefaultLit;
}

FString FMaterialHandlers::ShadingModelToString(EMaterialShadingModel ShadingModel)
{
	switch (ShadingModel)
	{
	case MSM_Unlit:              return TEXT("Unlit");
	case MSM_DefaultLit:         return TEXT("DefaultLit");
	case MSM_Subsurface:         return TEXT("Subsurface");
	case MSM_SubsurfaceProfile:  return TEXT("SubsurfaceProfile");
	case MSM_PreintegratedSkin:  return TEXT("PreintegratedSkin");
	case MSM_ClearCoat:          return TEXT("ClearCoat");
	case MSM_Cloth:              return TEXT("Cloth");
	case MSM_Eye:                return TEXT("Eye");
	case MSM_TwoSidedFoliage:   return TEXT("TwoSidedFoliage");
	default:                     return TEXT("Unknown");
	}
}

bool FMaterialHandlers::ParseMaterialProperty(const FString& PropertyName, EMaterialProperty& OutProperty)
{
	FString Lower = PropertyName.ToLower();
	if (Lower == TEXT("basecolor"))              { OutProperty = MP_BaseColor; return true; }
	if (Lower == TEXT("metallic"))               { OutProperty = MP_Metallic; return true; }
	if (Lower == TEXT("specular"))               { OutProperty = MP_Specular; return true; }
	if (Lower == TEXT("roughness"))              { OutProperty = MP_Roughness; return true; }
	if (Lower == TEXT("anisotropy"))             { OutProperty = MP_Anisotropy; return true; }
	if (Lower == TEXT("emissivecolor"))          { OutProperty = MP_EmissiveColor; return true; }
	if (Lower == TEXT("emissive"))               { OutProperty = MP_EmissiveColor; return true; }
	if (Lower == TEXT("opacity"))                { OutProperty = MP_Opacity; return true; }
	if (Lower == TEXT("opacitymask"))            { OutProperty = MP_OpacityMask; return true; }
	if (Lower == TEXT("normal"))                 { OutProperty = MP_Normal; return true; }
	if (Lower == TEXT("tangent"))                { OutProperty = MP_Tangent; return true; }
	if (Lower == TEXT("worldpositionoffset"))    { OutProperty = MP_WorldPositionOffset; return true; }
	if (Lower == TEXT("subsurfacecolor"))        { OutProperty = MP_SubsurfaceColor; return true; }
	if (Lower == TEXT("ambientocclusion"))       { OutProperty = MP_AmbientOcclusion; return true; }
	if (Lower == TEXT("ao"))                     { OutProperty = MP_AmbientOcclusion; return true; }
	if (Lower == TEXT("refraction"))             { OutProperty = MP_Refraction; return true; }
	if (Lower == TEXT("pixeldepthoffset"))       { OutProperty = MP_PixelDepthOffset; return true; }
	if (Lower == TEXT("shadingmodel"))           { OutProperty = MP_ShadingModel; return true; }
	return false;
}

TSharedPtr<FJsonValue> FMaterialHandlers::ListExpressionTypes(const TSharedPtr<FJsonObject>& Params)
{
	auto Result = MCPSuccess();
	TArray<TSharedPtr<FJsonValue>> TypesArray;

	// Common material expression types
	TArray<FString> ExpressionTypes = {
		TEXT("MaterialExpressionConstant"),
		TEXT("MaterialExpressionConstant2Vector"),
		TEXT("MaterialExpressionConstant3Vector"),
		TEXT("MaterialExpressionConstant4Vector"),
		TEXT("MaterialExpressionTextureSample"),
		TEXT("MaterialExpressionTextureCoordinate"),
		TEXT("MaterialExpressionScalarParameter"),
		TEXT("MaterialExpressionVectorParameter"),
		TEXT("MaterialExpressionTextureObjectParameter"),
		TEXT("MaterialExpressionStaticSwitchParameter"),
		TEXT("MaterialExpressionAdd"),
		TEXT("MaterialExpressionMultiply"),
		TEXT("MaterialExpressionSubtract"),
		TEXT("MaterialExpressionDivide"),
		TEXT("MaterialExpressionLinearInterpolate"),
		TEXT("MaterialExpressionPower"),
		TEXT("MaterialExpressionClamp"),
		TEXT("MaterialExpressionAppendVector"),
		TEXT("MaterialExpressionComponentMask"),
		TEXT("MaterialExpressionDotProduct"),
		TEXT("MaterialExpressionCrossProduct"),
		TEXT("MaterialExpressionNormalize"),
		TEXT("MaterialExpressionOneMinus"),
		TEXT("MaterialExpressionAbs"),
		TEXT("MaterialExpressionTime"),
		TEXT("MaterialExpressionWorldPosition"),
		TEXT("MaterialExpressionVertexNormalWS"),
		TEXT("MaterialExpressionCameraPositionWS"),
		TEXT("MaterialExpressionFresnel"),
		TEXT("MaterialExpressionPanner"),
		TEXT("MaterialExpressionRotator"),
		TEXT("MaterialExpressionDesaturation"),
		TEXT("MaterialExpressionNoise"),
		TEXT("MaterialExpressionParticleColor"),
		TEXT("MaterialExpressionObjectPositionWS"),
		TEXT("MaterialExpressionActorPositionWS")
	};

	for (const FString& TypeName : ExpressionTypes)
	{
		TypesArray.Add(MakeShared<FJsonValueString>(TypeName));
	}

	Result->SetArrayField(TEXT("expressionTypes"), TypesArray);
	Result->SetNumberField(TEXT("count"), ExpressionTypes.Num());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::CreateMaterial(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Materials"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] CreateMaterial: name=%s packagePath=%s"), *Name, *PackagePath);

	// Idempotency: check if the material already exists at the target path.
	const FString ProbePath = PackagePath + TEXT("/") + Name + TEXT(".") + Name;
	if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, *ProbePath))
	{
		if (OnConflict == TEXT("error"))
		{
			return MCPError(FString::Printf(TEXT("Material '%s' already exists"), *ProbePath));
		}
		auto ExistingResult = MCPSuccess();
		MCPSetExisted(ExistingResult);
		ExistingResult->SetStringField(TEXT("path"), Existing->GetPathName());
		ExistingResult->SetStringField(TEXT("name"), Name);
		ExistingResult->SetStringField(TEXT("packagePath"), PackagePath);
		return MCPResult(ExistingResult);
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UMaterialFactoryNew* MaterialFactory = NewObject<UMaterialFactoryNew>();
	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, UMaterial::StaticClass(), MaterialFactory);

	if (!NewAsset)
	{
		return MCPError(TEXT("Failed to create material asset"));
	}

	UMaterial* NewMaterial = Cast<UMaterial>(NewAsset);
	if (!NewMaterial)
	{
		return MCPError(TEXT("Created asset is not a material"));
	}

	UPackage* Package = NewMaterial->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	const FString AssetPath = NewMaterial->GetPathName();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("packagePath"), PackagePath);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	MCPSetRollback(Result, TEXT("delete_asset"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::ReadMaterial(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UMaterial* Material = LoadMaterialFromPath(AssetPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *AssetPath));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("name"), Material->GetName());
	Result->SetStringField(TEXT("path"), Material->GetPathName());
	Result->SetStringField(TEXT("shadingModel"), ShadingModelToString(Material->GetShadingModels().GetFirstShadingModel()));
	Result->SetStringField(TEXT("blendMode"), StaticEnum<EBlendMode>()->GetNameStringByValue((int64)Material->BlendMode));
	Result->SetBoolField(TEXT("twoSided"), Material->IsTwoSided());

	// Expressions list with details
	TArray<TSharedPtr<FJsonValue>> ExpressionsArray;
	int32 Index = 0;
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (!Expression) { Index++; continue; }

		TSharedPtr<FJsonObject> ExprObj = MakeShared<FJsonObject>();
		ExprObj->SetNumberField(TEXT("index"), Index);
		ExprObj->SetStringField(TEXT("class"), Expression->GetClass()->GetName());
		ExprObj->SetStringField(TEXT("description"), Expression->GetDescription());
		ExprObj->SetNumberField(TEXT("positionX"), Expression->MaterialExpressionEditorX);
		ExprObj->SetNumberField(TEXT("positionY"), Expression->MaterialExpressionEditorY);

		// Extract parameter names for parameter expressions
		if (UMaterialExpressionScalarParameter* ScalarParam = Cast<UMaterialExpressionScalarParameter>(Expression))
		{
			ExprObj->SetStringField(TEXT("parameterName"), ScalarParam->ParameterName.ToString());
			ExprObj->SetNumberField(TEXT("defaultValue"), ScalarParam->DefaultValue);
		}
		else if (UMaterialExpressionVectorParameter* VectorParam = Cast<UMaterialExpressionVectorParameter>(Expression))
		{
			ExprObj->SetStringField(TEXT("parameterName"), VectorParam->ParameterName.ToString());
			TSharedPtr<FJsonObject> DefColor = MakeShared<FJsonObject>();
			DefColor->SetNumberField(TEXT("r"), VectorParam->DefaultValue.R);
			DefColor->SetNumberField(TEXT("g"), VectorParam->DefaultValue.G);
			DefColor->SetNumberField(TEXT("b"), VectorParam->DefaultValue.B);
			DefColor->SetNumberField(TEXT("a"), VectorParam->DefaultValue.A);
			ExprObj->SetObjectField(TEXT("defaultValue"), DefColor);
		}
		else if (UMaterialExpressionTextureSample* TexSample = Cast<UMaterialExpressionTextureSample>(Expression))
		{
			if (TexSample->Texture)
			{
				ExprObj->SetStringField(TEXT("texturePath"), TexSample->Texture->GetPathName());
			}
		}
		else if (UMaterialExpressionConstant* ConstExpr = Cast<UMaterialExpressionConstant>(Expression))
		{
			ExprObj->SetNumberField(TEXT("value"), ConstExpr->R);
		}
		else if (UMaterialExpressionConstant3Vector* Const3Expr = Cast<UMaterialExpressionConstant3Vector>(Expression))
		{
			TSharedPtr<FJsonObject> ConstColor = MakeShared<FJsonObject>();
			ConstColor->SetNumberField(TEXT("r"), Const3Expr->Constant.R);
			ConstColor->SetNumberField(TEXT("g"), Const3Expr->Constant.G);
			ConstColor->SetNumberField(TEXT("b"), Const3Expr->Constant.B);
			ConstColor->SetNumberField(TEXT("a"), Const3Expr->Constant.A);
			ExprObj->SetObjectField(TEXT("value"), ConstColor);
		}
		else if (UMaterialExpressionConstant4Vector* Const4Expr = Cast<UMaterialExpressionConstant4Vector>(Expression))
		{
			TSharedPtr<FJsonObject> ConstColor = MakeShared<FJsonObject>();
			ConstColor->SetNumberField(TEXT("r"), Const4Expr->Constant.R);
			ConstColor->SetNumberField(TEXT("g"), Const4Expr->Constant.G);
			ConstColor->SetNumberField(TEXT("b"), Const4Expr->Constant.B);
			ConstColor->SetNumberField(TEXT("a"), Const4Expr->Constant.A);
			ExprObj->SetObjectField(TEXT("value"), ConstColor);
		}

		// Expression-to-expression input connections
		TArray<TSharedPtr<FJsonValue>> InputsArray;
		for (int32 InputIdx = 0; ; InputIdx++)
		{
			FExpressionInput* Input = Expression->GetInput(InputIdx);
			if (!Input) break;

			TSharedPtr<FJsonObject> InputObj = MakeShared<FJsonObject>();
			InputObj->SetNumberField(TEXT("inputIndex"), InputIdx);
			InputObj->SetStringField(TEXT("inputName"), Expression->GetInputName(InputIdx).ToString());

			if (Input->Expression)
			{
				InputObj->SetStringField(TEXT("connectedExpressionClass"), Input->Expression->GetClass()->GetName());
				InputObj->SetStringField(TEXT("connectedExpressionDescription"), Input->Expression->GetDescription());
				InputObj->SetNumberField(TEXT("connectedOutputIndex"), Input->OutputIndex);

				// Find index of connected expression
				int32 ConnIdx = 0;
				for (UMaterialExpression* Expr : Material->GetExpressions())
				{
					if (Expr == Input->Expression)
					{
						InputObj->SetNumberField(TEXT("connectedExpressionIndex"), ConnIdx);
						break;
					}
					ConnIdx++;
				}
			}

			InputsArray.Add(MakeShared<FJsonValueObject>(InputObj));
		}
		if (InputsArray.Num() > 0)
		{
			ExprObj->SetArrayField(TEXT("inputs"), InputsArray);
		}

		ExpressionsArray.Add(MakeShared<FJsonValueObject>(ExprObj));
		Index++;
	}
	Result->SetArrayField(TEXT("expressions"), ExpressionsArray);
	Result->SetNumberField(TEXT("expressionCount"), ExpressionsArray.Num());

	// Material input connections (which expressions are wired to which material properties)
	UMaterialEditorOnlyData* EditorOnlyData = Material->GetEditorOnlyData();
	if (EditorOnlyData)
	{
		TSharedPtr<FJsonObject> ConnectionsObj = MakeShared<FJsonObject>();

		auto DescribeConnection = [&](const FExpressionInput& Input) -> TSharedPtr<FJsonValue>
		{
			if (Input.Expression)
			{
				TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
				ConnObj->SetStringField(TEXT("expressionClass"), Input.Expression->GetClass()->GetName());
				ConnObj->SetStringField(TEXT("expressionDescription"), Input.Expression->GetDescription());
				ConnObj->SetNumberField(TEXT("outputIndex"), Input.OutputIndex);

				// Find the expression index
				int32 ConnIdx = 0;
				for (UMaterialExpression* Expr : Material->GetExpressions())
				{
					if (Expr == Input.Expression)
					{
						ConnObj->SetNumberField(TEXT("expressionIndex"), ConnIdx);
						break;
					}
					ConnIdx++;
				}
				return MakeShared<FJsonValueObject>(ConnObj);
			}
			return MakeShared<FJsonValueNull>();
		};

		ConnectionsObj->SetField(TEXT("BaseColor"), DescribeConnection(EditorOnlyData->BaseColor));
		ConnectionsObj->SetField(TEXT("Metallic"), DescribeConnection(EditorOnlyData->Metallic));
		ConnectionsObj->SetField(TEXT("Specular"), DescribeConnection(EditorOnlyData->Specular));
		ConnectionsObj->SetField(TEXT("Roughness"), DescribeConnection(EditorOnlyData->Roughness));
		ConnectionsObj->SetField(TEXT("Anisotropy"), DescribeConnection(EditorOnlyData->Anisotropy));
		ConnectionsObj->SetField(TEXT("EmissiveColor"), DescribeConnection(EditorOnlyData->EmissiveColor));
		ConnectionsObj->SetField(TEXT("Opacity"), DescribeConnection(EditorOnlyData->Opacity));
		ConnectionsObj->SetField(TEXT("OpacityMask"), DescribeConnection(EditorOnlyData->OpacityMask));
		ConnectionsObj->SetField(TEXT("Normal"), DescribeConnection(EditorOnlyData->Normal));
		ConnectionsObj->SetField(TEXT("Tangent"), DescribeConnection(EditorOnlyData->Tangent));
		ConnectionsObj->SetField(TEXT("WorldPositionOffset"), DescribeConnection(EditorOnlyData->WorldPositionOffset));
		ConnectionsObj->SetField(TEXT("SubsurfaceColor"), DescribeConnection(EditorOnlyData->SubsurfaceColor));
		ConnectionsObj->SetField(TEXT("AmbientOcclusion"), DescribeConnection(EditorOnlyData->AmbientOcclusion));
		ConnectionsObj->SetField(TEXT("Refraction"), DescribeConnection(EditorOnlyData->Refraction));
		ConnectionsObj->SetField(TEXT("PixelDepthOffset"), DescribeConnection(EditorOnlyData->PixelDepthOffset));

		Result->SetObjectField(TEXT("connections"), ConnectionsObj);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::SetMaterialShadingModel(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString ShadingModelStr;
	if (auto Err = RequireString(Params, TEXT("shadingModel"), ShadingModelStr)) return Err;

	UMaterial* Material = LoadMaterialFromPath(AssetPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *AssetPath));
	}

	EMaterialShadingModel NewShadingModel = ParseShadingModel(ShadingModelStr);
	const EMaterialShadingModel PrevShadingModel = Material->GetShadingModels().GetFirstShadingModel();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), Material->GetPathName());
	Result->SetStringField(TEXT("shadingModel"), ShadingModelToString(NewShadingModel));

	if (PrevShadingModel == NewShadingModel)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		return MCPResult(Result);
	}

	Material->PreEditChange(nullptr);
	Material->SetShadingModel(NewShadingModel);
	Material->PostEditChange();
	Material->MarkPackageDirty();

	MCPSetUpdated(Result);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), Material->GetPathName());
	Payload->SetStringField(TEXT("shadingModel"), ShadingModelToString(PrevShadingModel));
	MCPSetRollback(Result, TEXT("set_material_shading_model"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::SetMaterialBlendMode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString BlendModeStr;
	if (auto Err = RequireString(Params, TEXT("blendMode"), BlendModeStr)) return Err;

	UMaterial* Material = LoadMaterialFromPath(AssetPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *AssetPath));
	}

	EBlendMode NewBlendMode = BLEND_Opaque;
	if (BlendModeStr.Equals(TEXT("Opaque"), ESearchCase::IgnoreCase)) NewBlendMode = BLEND_Opaque;
	else if (BlendModeStr.Equals(TEXT("Masked"), ESearchCase::IgnoreCase)) NewBlendMode = BLEND_Masked;
	else if (BlendModeStr.Equals(TEXT("Translucent"), ESearchCase::IgnoreCase)) NewBlendMode = BLEND_Translucent;
	else if (BlendModeStr.Equals(TEXT("Additive"), ESearchCase::IgnoreCase)) NewBlendMode = BLEND_Additive;
	else if (BlendModeStr.Equals(TEXT("Modulate"), ESearchCase::IgnoreCase)) NewBlendMode = BLEND_Modulate;
	else if (BlendModeStr.Equals(TEXT("AlphaComposite"), ESearchCase::IgnoreCase)) NewBlendMode = BLEND_AlphaComposite;
	else if (BlendModeStr.Equals(TEXT("AlphaHoldout"), ESearchCase::IgnoreCase)) NewBlendMode = BLEND_AlphaHoldout;
	else
	{
		return MCPError(FString::Printf(TEXT("Unknown blend mode: '%s'. Use Opaque, Masked, Translucent, Additive, Modulate, AlphaComposite, or AlphaHoldout"), *BlendModeStr));
	}

	const EBlendMode PrevBlendMode = Material->BlendMode;
	FString PrevBlendModeStr;
	switch (PrevBlendMode)
	{
	case BLEND_Opaque: PrevBlendModeStr = TEXT("Opaque"); break;
	case BLEND_Masked: PrevBlendModeStr = TEXT("Masked"); break;
	case BLEND_Translucent: PrevBlendModeStr = TEXT("Translucent"); break;
	case BLEND_Additive: PrevBlendModeStr = TEXT("Additive"); break;
	case BLEND_Modulate: PrevBlendModeStr = TEXT("Modulate"); break;
	case BLEND_AlphaComposite: PrevBlendModeStr = TEXT("AlphaComposite"); break;
	case BLEND_AlphaHoldout: PrevBlendModeStr = TEXT("AlphaHoldout"); break;
	default: PrevBlendModeStr = TEXT("Opaque"); break;
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), Material->GetPathName());
	Result->SetStringField(TEXT("blendMode"), BlendModeStr);

	if (PrevBlendMode == NewBlendMode)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		return MCPResult(Result);
	}

	Material->PreEditChange(nullptr);
	Material->BlendMode = NewBlendMode;
	Material->PostEditChange();
	Material->MarkPackageDirty();

	MCPSetUpdated(Result);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), Material->GetPathName());
	Payload->SetStringField(TEXT("blendMode"), PrevBlendModeStr);
	MCPSetRollback(Result, TEXT("set_material_blend_mode"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::SetMaterialBaseColor(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	const TSharedPtr<FJsonObject>* ColorObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("color"), ColorObj))
	{
		return MCPError(TEXT("Missing 'color' parameter (object with r,g,b,a)"));
	}

	double R = 1.0, G = 1.0, B = 1.0, A = 1.0;
	(*ColorObj)->TryGetNumberField(TEXT("r"), R);
	(*ColorObj)->TryGetNumberField(TEXT("g"), G);
	(*ColorObj)->TryGetNumberField(TEXT("b"), B);
	(*ColorObj)->TryGetNumberField(TEXT("a"), A);

	UMaterial* Material = LoadMaterialFromPath(AssetPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *AssetPath));
	}

	// No rollback: this adds a new Constant3Vector expression each call (not natural-key idempotent).
	// Caller should use set_material_parameter with a named scalar/vector parameter for true idempotency.
	Material->PreEditChange(nullptr);

	// Create a Constant3Vector expression for the base color
	UMaterialExpressionConstant3Vector* ColorExpression = NewObject<UMaterialExpressionConstant3Vector>(Material);
	ColorExpression->Constant = FLinearColor(R, G, B, A);

	// Add expression to material
	Material->GetExpressionCollection().AddExpression(ColorExpression);

	// Connect to base color input
	Material->GetEditorOnlyData()->BaseColor.Connect(0, ColorExpression);

	Material->PostEditChange();
	Material->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	TSharedPtr<FJsonObject> ColorResult = MakeShared<FJsonObject>();
	ColorResult->SetNumberField(TEXT("r"), R);
	ColorResult->SetNumberField(TEXT("g"), G);
	ColorResult->SetNumberField(TEXT("b"), B);
	ColorResult->SetNumberField(TEXT("a"), A);
	Result->SetObjectField(TEXT("color"), ColorResult);
	Result->SetStringField(TEXT("path"), Material->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::AddMaterialExpression(const TSharedPtr<FJsonObject>& Params)
{
	FString MaterialPath;
	if (auto Err = RequireStringAlt(Params, TEXT("materialPath"), TEXT("path"), MaterialPath)) return Err;
	if (MaterialPath.IsEmpty())
	{
		// Also try assetPath as a third key
		Params->TryGetStringField(TEXT("assetPath"), MaterialPath);
		if (MaterialPath.IsEmpty())
		{
			return MCPError(TEXT("Missing required parameter 'materialPath' (or 'path')"));
		}
	}

	FString ExpressionType;
	if (auto Err = RequireString(Params, TEXT("expressionType"), ExpressionType)) return Err;

	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
	}

	// Resolve short expression type names: "Multiply" -> "UMaterialExpressionMultiply"
	FString ClassName = ExpressionType;
	if (!ClassName.StartsWith(TEXT("MaterialExpression")) && !ClassName.StartsWith(TEXT("UMaterialExpression")))
	{
		ClassName = TEXT("UMaterialExpression") + ClassName;
	}
	else if (!ClassName.StartsWith(TEXT("U")))
	{
		ClassName = TEXT("U") + ClassName;
	}

	// Find the expression class
	UClass* ExpressionClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::ExactClass);
	if (!ExpressionClass)
	{
		// Try with /Script/Engine prefix
		FString FullPath = FString::Printf(TEXT("/Script/Engine.%s"), *ClassName.Mid(1)); // strip U prefix for path
		ExpressionClass = FindObject<UClass>(nullptr, *FullPath);
	}
	if (!ExpressionClass)
	{
		// Try original name as-is (user may have passed the full class name)
		ExpressionClass = FindFirstObject<UClass>(*ExpressionType, EFindFirstObjectOptions::ExactClass);
		if (!ExpressionClass)
		{
			FString WithU = TEXT("U") + ExpressionType;
			ExpressionClass = FindFirstObject<UClass>(*WithU, EFindFirstObjectOptions::ExactClass);
		}
	}

	if (!ExpressionClass || !ExpressionClass->IsChildOf(UMaterialExpression::StaticClass()))
	{
		return MCPError(FString::Printf(TEXT("Unknown expression type: '%s'"), *ExpressionType));
	}

	Material->PreEditChange(nullptr);

	UMaterialExpression* NewExpression = NewObject<UMaterialExpression>(Material, ExpressionClass);
	Material->GetExpressionCollection().AddExpression(NewExpression);

	// Apply optional properties
	FString ExpressionName;
	if (Params->TryGetStringField(TEXT("name"), ExpressionName) || Params->TryGetStringField(TEXT("expressionName"), ExpressionName))
	{
		NewExpression->Desc = ExpressionName;
	}

	// Set parameter name for parameter expressions
	FString ParameterName;
	if (Params->TryGetStringField(TEXT("parameterName"), ParameterName))
	{
		if (UMaterialExpressionScalarParameter* ScalarParam = Cast<UMaterialExpressionScalarParameter>(NewExpression))
		{
			ScalarParam->ParameterName = FName(*ParameterName);
		}
		else if (UMaterialExpressionVectorParameter* VectorParam = Cast<UMaterialExpressionVectorParameter>(NewExpression))
		{
			VectorParam->ParameterName = FName(*ParameterName);
		}
		else if (UMaterialExpressionTextureObjectParameter* TexParam = Cast<UMaterialExpressionTextureObjectParameter>(NewExpression))
		{
			TexParam->ParameterName = FName(*ParameterName);
		}
		// If name not set via Desc, use parameterName as the description too
		if (NewExpression->Desc.IsEmpty())
		{
			NewExpression->Desc = ParameterName;
		}
	}

	// Set position
	double PosX = 0, PosY = 0;
	if (Params->TryGetNumberField(TEXT("positionX"), PosX))
	{
		NewExpression->MaterialExpressionEditorX = static_cast<int32>(PosX);
	}
	if (Params->TryGetNumberField(TEXT("positionY"), PosY))
	{
		NewExpression->MaterialExpressionEditorY = static_cast<int32>(PosY);
	}

	Material->PostEditChange();

	// Save the package so subsequent list/connect calls see the expression
	UPackage* Package = Material->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	// Return the index as nodeId for use with connect_expressions and other operations
	int32 NodeIndex = Material->GetExpressions().Num() - 1;

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("expressionType"), ExpressionType);
	Result->SetStringField(TEXT("expressionClass"), NewExpression->GetClass()->GetName());
	Result->SetStringField(TEXT("nodeId"), FString::FromInt(NodeIndex));
	Result->SetStringField(TEXT("description"), NewExpression->GetDescription());
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetNumberField(TEXT("expressionCount"), Material->GetExpressions().Num());

	// Rollback: remove the expression by nodeId
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Payload->SetStringField(TEXT("nodeId"), FString::FromInt(NodeIndex));
	MCPSetRollback(Result, TEXT("delete_material_expression"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::ListMaterialExpressions(const TSharedPtr<FJsonObject>& Params)
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

	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
	}

	TArray<TSharedPtr<FJsonValue>> ExpressionsArray;
	auto Expressions = Material->GetExpressions();
	for (int32 i = 0; i < Expressions.Num(); i++)
	{
		UMaterialExpression* Expression = Expressions[i];
		if (!Expression) continue;

		TSharedPtr<FJsonObject> ExprObj = MakeShared<FJsonObject>();
		ExprObj->SetStringField(TEXT("nodeId"), FString::FromInt(i));
		ExprObj->SetStringField(TEXT("class"), Expression->GetClass()->GetName());
		ExprObj->SetStringField(TEXT("description"), Expression->GetDescription());
		ExprObj->SetStringField(TEXT("name"), Expression->Desc);
		ExprObj->SetNumberField(TEXT("positionX"), Expression->MaterialExpressionEditorX);
		ExprObj->SetNumberField(TEXT("positionY"), Expression->MaterialExpressionEditorY);

		// Include parameter name if applicable
		if (UMaterialExpressionScalarParameter* SP = Cast<UMaterialExpressionScalarParameter>(Expression))
		{
			ExprObj->SetStringField(TEXT("parameterName"), SP->ParameterName.ToString());
		}
		else if (UMaterialExpressionVectorParameter* VP = Cast<UMaterialExpressionVectorParameter>(Expression))
		{
			ExprObj->SetStringField(TEXT("parameterName"), VP->ParameterName.ToString());
		}

		ExpressionsArray.Add(MakeShared<FJsonValueObject>(ExprObj));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("expressions"), ExpressionsArray);
	Result->SetNumberField(TEXT("count"), ExpressionsArray.Num());
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::ListMaterialParameters(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UMaterial* Material = LoadMaterialFromPath(AssetPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *AssetPath));
	}

	TArray<TSharedPtr<FJsonValue>> ScalarParams;
	TArray<TSharedPtr<FJsonValue>> VectorParams;
	TArray<TSharedPtr<FJsonValue>> TextureParams;

	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (!Expression) continue;

		if (UMaterialExpressionScalarParameter* ScalarParam = Cast<UMaterialExpressionScalarParameter>(Expression))
		{
			TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
			ParamObj->SetStringField(TEXT("name"), ScalarParam->ParameterName.ToString());
			ParamObj->SetNumberField(TEXT("defaultValue"), ScalarParam->DefaultValue);
			ScalarParams.Add(MakeShared<FJsonValueObject>(ParamObj));
		}
		else if (UMaterialExpressionVectorParameter* VectorParam = Cast<UMaterialExpressionVectorParameter>(Expression))
		{
			TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
			ParamObj->SetStringField(TEXT("name"), VectorParam->ParameterName.ToString());

			TSharedPtr<FJsonObject> DefaultColor = MakeShared<FJsonObject>();
			DefaultColor->SetNumberField(TEXT("r"), VectorParam->DefaultValue.R);
			DefaultColor->SetNumberField(TEXT("g"), VectorParam->DefaultValue.G);
			DefaultColor->SetNumberField(TEXT("b"), VectorParam->DefaultValue.B);
			DefaultColor->SetNumberField(TEXT("a"), VectorParam->DefaultValue.A);
			ParamObj->SetObjectField(TEXT("defaultValue"), DefaultColor);

			VectorParams.Add(MakeShared<FJsonValueObject>(ParamObj));
		}
		else if (UMaterialExpressionTextureSample* TextureParam = Cast<UMaterialExpressionTextureSample>(Expression))
		{
			TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
			ParamObj->SetStringField(TEXT("class"), TEXT("TextureSample"));
			if (TextureParam->Texture)
			{
				ParamObj->SetStringField(TEXT("texture"), TextureParam->Texture->GetPathName());
			}
			TextureParams.Add(MakeShared<FJsonValueObject>(ParamObj));
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("scalarParameters"), ScalarParams);
	Result->SetArrayField(TEXT("vectorParameters"), VectorParams);
	Result->SetArrayField(TEXT("textureParameters"), TextureParams);
	Result->SetStringField(TEXT("path"), Material->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::RecompileMaterial(const TSharedPtr<FJsonObject>& Params)
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

	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
	}

	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Recompiling material: %s"), *MaterialPath);

	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Material->MarkPackageDirty();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), Material->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::CreateMaterialInstance(const TSharedPtr<FJsonObject>& Params)
{
	FString ParentPath;
	if (auto Err = RequireString(Params, TEXT("parentPath"), ParentPath)) return Err;

	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Materials"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (auto Existing = MCPCheckAssetExists(PackagePath, Name, OnConflict, TEXT("MaterialInstance")))
	{
		return Existing;
	}

	UMaterialInterface* ParentMaterial = Cast<UMaterialInterface>(
		StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *ParentPath));
	if (!ParentMaterial)
	{
		// Try with class prefix
		ParentMaterial = Cast<UMaterialInterface>(
			StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *(TEXT("Material'") + ParentPath + TEXT("'"))));
	}
	if (!ParentMaterial)
	{
		return MCPError(FString::Printf(TEXT("Failed to load parent material at '%s'"), *ParentPath));
	}

	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] CreateMaterialInstance: name=%s parent=%s packagePath=%s"), *Name, *ParentPath, *PackagePath);

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
	Factory->InitialParent = ParentMaterial;

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, UMaterialInstanceConstant::StaticClass(), Factory);
	if (!NewAsset)
	{
		return MCPError(TEXT("Failed to create material instance asset"));
	}

	UMaterialInstanceConstant* MaterialInstance = Cast<UMaterialInstanceConstant>(NewAsset);
	if (!MaterialInstance)
	{
		return MCPError(TEXT("Created asset is not a material instance"));
	}

	// Save the package
	UPackage* Package = MaterialInstance->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), MaterialInstance->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("parentPath"), ParentMaterial->GetPathName());
	Result->SetStringField(TEXT("packagePath"), PackagePath);
	MCPSetDeleteAssetRollback(Result, MaterialInstance->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::SetMaterialParameter(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString ParameterName;
	if (auto Err = RequireString(Params, TEXT("parameterName"), ParameterName)) return Err;

	// parameterType is optional -- auto-detect if not provided (#71, #72)
	FString ParameterType = OptionalString(Params, TEXT("parameterType"));

	UMaterialInstanceConstant* MaterialInstance = LoadMaterialInstanceFromPath(AssetPath);
	if (!MaterialInstance)
	{
		// Not a MaterialInstance -- might be a base Material with expression nodes (#71)
		// Redirect to set_expression_value logic
		UMaterial* BaseMaterial = LoadMaterialFromPath(AssetPath);
		if (BaseMaterial)
		{
			// Find the expression by parameter name
			UMaterialExpression* Expr = FindExpressionByName(BaseMaterial, ParameterName);
			if (Expr)
			{
				return MCPError(FString::Printf(
					TEXT("'%s' is a base Material, not a MaterialInstance. Use set_expression_value with expressionIndex to set values on expression nodes directly."),
					*AssetPath));
			}
			else
			{
				return MCPError(FString::Printf(
					TEXT("'%s' is a base Material, not a MaterialInstance. Cannot set parameters. Create a MaterialInstance first."),
					*AssetPath));
			}
		}
		else
		{
			return MCPError(FString::Printf(TEXT("Failed to load material or material instance at '%s'"), *AssetPath));
		}
	}

	// Auto-detect parameter type if not provided
	if (ParameterType.IsEmpty())
	{
		// Check which parameter collections contain this name
		FName ParamFName(*ParameterName);
		float ScalarVal;
		FLinearColor VectorVal;
		UTexture* TextureVal;
		if (MaterialInstance->GetScalarParameterValue(ParamFName, ScalarVal))
			ParameterType = TEXT("scalar");
		else if (MaterialInstance->GetVectorParameterValue(ParamFName, VectorVal))
			ParameterType = TEXT("vector");
		else if (MaterialInstance->GetTextureParameterValue(ParamFName, TextureVal))
			ParameterType = TEXT("texture");
		else
			ParameterType = TEXT("scalar"); // default fallback
	}

	FString TypeLower = ParameterType.ToLower();

	if (TypeLower == TEXT("scalar"))
	{
		double ScalarValue = 0.0;
		if (!Params->TryGetNumberField(TEXT("value"), ScalarValue))
		{
			return MCPError(TEXT("Missing 'value' number field for scalar parameter"));
		}

		float PrevScalar = 0.0f;
		const bool bHadPrev = MaterialInstance->GetScalarParameterValue(FName(*ParameterName), PrevScalar);

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("parameterName"), ParameterName);
		Result->SetStringField(TEXT("parameterType"), TEXT("scalar"));
		Result->SetNumberField(TEXT("value"), ScalarValue);
		Result->SetStringField(TEXT("path"), MaterialInstance->GetPathName());

		if (bHadPrev && FMath::IsNearlyEqual(PrevScalar, (float)ScalarValue))
		{
			MCPSetExisted(Result);
			Result->SetBoolField(TEXT("updated"), false);
			return MCPResult(Result);
		}

		MaterialInstance->SetScalarParameterValueEditorOnly(FName(*ParameterName), static_cast<float>(ScalarValue));
		MaterialInstance->MarkPackageDirty();

		MCPSetUpdated(Result);
		if (bHadPrev)
		{
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("path"), MaterialInstance->GetPathName());
			Payload->SetStringField(TEXT("parameterName"), ParameterName);
			Payload->SetStringField(TEXT("parameterType"), TEXT("scalar"));
			Payload->SetNumberField(TEXT("value"), PrevScalar);
			MCPSetRollback(Result, TEXT("set_material_parameter"), Payload);
		}

		return MCPResult(Result);
	}
	else if (TypeLower == TEXT("vector"))
	{
		const TSharedPtr<FJsonObject>* ValueObj = nullptr;
		if (!Params->TryGetObjectField(TEXT("value"), ValueObj))
		{
			return MCPError(TEXT("Missing 'value' object field (r,g,b,a) for vector parameter"));
		}

		double R = 0.0, G = 0.0, B = 0.0, A = 1.0;
		(*ValueObj)->TryGetNumberField(TEXT("r"), R);
		(*ValueObj)->TryGetNumberField(TEXT("g"), G);
		(*ValueObj)->TryGetNumberField(TEXT("b"), B);
		(*ValueObj)->TryGetNumberField(TEXT("a"), A);

		FLinearColor ColorValue(R, G, B, A);
		FLinearColor PrevColor;
		const bool bHadPrev = MaterialInstance->GetVectorParameterValue(FName(*ParameterName), PrevColor);

		TSharedPtr<FJsonObject> ValueResult = MakeShared<FJsonObject>();
		ValueResult->SetNumberField(TEXT("r"), R);
		ValueResult->SetNumberField(TEXT("g"), G);
		ValueResult->SetNumberField(TEXT("b"), B);
		ValueResult->SetNumberField(TEXT("a"), A);

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("parameterName"), ParameterName);
		Result->SetStringField(TEXT("parameterType"), TEXT("vector"));
		Result->SetObjectField(TEXT("value"), ValueResult);
		Result->SetStringField(TEXT("path"), MaterialInstance->GetPathName());

		if (bHadPrev && PrevColor.Equals(ColorValue))
		{
			MCPSetExisted(Result);
			Result->SetBoolField(TEXT("updated"), false);
			return MCPResult(Result);
		}

		MaterialInstance->SetVectorParameterValueEditorOnly(FName(*ParameterName), ColorValue);
		MaterialInstance->MarkPackageDirty();

		MCPSetUpdated(Result);
		if (bHadPrev)
		{
			TSharedPtr<FJsonObject> PrevValueObj = MakeShared<FJsonObject>();
			PrevValueObj->SetNumberField(TEXT("r"), PrevColor.R);
			PrevValueObj->SetNumberField(TEXT("g"), PrevColor.G);
			PrevValueObj->SetNumberField(TEXT("b"), PrevColor.B);
			PrevValueObj->SetNumberField(TEXT("a"), PrevColor.A);
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("path"), MaterialInstance->GetPathName());
			Payload->SetStringField(TEXT("parameterName"), ParameterName);
			Payload->SetStringField(TEXT("parameterType"), TEXT("vector"));
			Payload->SetObjectField(TEXT("value"), PrevValueObj);
			MCPSetRollback(Result, TEXT("set_material_parameter"), Payload);
		}

		return MCPResult(Result);
	}
	else if (TypeLower == TEXT("texture"))
	{
		FString TexturePath;
		if (!Params->TryGetStringField(TEXT("value"), TexturePath) || TexturePath.IsEmpty())
		{
			return MCPError(TEXT("Missing 'value' string field (texture asset path) for texture parameter"));
		}

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

		UTexture* PrevTexture = nullptr;
		const bool bHadPrev = MaterialInstance->GetTextureParameterValue(FName(*ParameterName), PrevTexture);

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("parameterName"), ParameterName);
		Result->SetStringField(TEXT("parameterType"), TEXT("texture"));
		Result->SetStringField(TEXT("value"), Texture->GetPathName());
		Result->SetStringField(TEXT("path"), MaterialInstance->GetPathName());

		if (bHadPrev && PrevTexture == Texture)
		{
			MCPSetExisted(Result);
			Result->SetBoolField(TEXT("updated"), false);
			return MCPResult(Result);
		}

		MaterialInstance->SetTextureParameterValueEditorOnly(FName(*ParameterName), Texture);
		MaterialInstance->MarkPackageDirty();

		MCPSetUpdated(Result);
		if (bHadPrev && PrevTexture)
		{
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("path"), MaterialInstance->GetPathName());
			Payload->SetStringField(TEXT("parameterName"), ParameterName);
			Payload->SetStringField(TEXT("parameterType"), TEXT("texture"));
			Payload->SetStringField(TEXT("value"), PrevTexture->GetPathName());
			MCPSetRollback(Result, TEXT("set_material_parameter"), Payload);
		}

		return MCPResult(Result);
	}
	else
	{
		return MCPError(FString::Printf(TEXT("Unknown parameterType '%s'. Use 'scalar', 'vector', or 'texture'."), *ParameterType));
	}
}

TSharedPtr<FJsonValue> FMaterialHandlers::ConnectExpression(const TSharedPtr<FJsonObject>& Params)
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

	int32 SourceIndex = -1;
	if (!Params->TryGetNumberField(TEXT("sourceIndex"), SourceIndex))
	{
		return MCPError(TEXT("Missing required parameter 'sourceIndex'"));
	}

	int32 TargetIndex = -1;
	if (!Params->TryGetNumberField(TEXT("targetIndex"), TargetIndex))
	{
		return MCPError(TEXT("Missing required parameter 'targetIndex'"));
	}

	int32 SourceOutputIndex = OptionalInt(Params, TEXT("sourceOutputIndex"), 0);
	int32 TargetInputIndex = OptionalInt(Params, TEXT("targetInputIndex"), 0);

	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
	}

	auto Expressions = Material->GetExpressions();

	if (SourceIndex < 0 || SourceIndex >= Expressions.Num())
	{
		return MCPError(FString::Printf(TEXT("Source expression index %d out of range (0-%d)"), SourceIndex, Expressions.Num() - 1));
	}

	if (TargetIndex < 0 || TargetIndex >= Expressions.Num())
	{
		return MCPError(FString::Printf(TEXT("Target expression index %d out of range (0-%d)"), TargetIndex, Expressions.Num() - 1));
	}

	UMaterialExpression* SourceExpression = Expressions[SourceIndex];
	UMaterialExpression* TargetExpression = Expressions[TargetIndex];

	if (!SourceExpression || !TargetExpression)
	{
		return MCPError(TEXT("Source or target expression is null"));
	}

	// Validate target input index by probing GetInput()
	FExpressionInput* TargetInput = TargetExpression->GetInput(TargetInputIndex);
	if (!TargetInput)
	{
		return MCPError(FString::Printf(TEXT("Target input index %d is out of range"), TargetInputIndex));
	}

	Material->PreEditChange(nullptr);
	TargetInput->Connect(SourceOutputIndex, SourceExpression);

	Material->PostEditChange();
	Material->MarkPackageDirty();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetNumberField(TEXT("sourceIndex"), SourceIndex);
	Result->SetStringField(TEXT("sourceClass"), SourceExpression->GetClass()->GetName());
	Result->SetNumberField(TEXT("targetIndex"), TargetIndex);
	Result->SetStringField(TEXT("targetClass"), TargetExpression->GetClass()->GetName());
	Result->SetNumberField(TEXT("sourceOutputIndex"), SourceOutputIndex);
	Result->SetNumberField(TEXT("targetInputIndex"), TargetInputIndex);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::ConnectMaterialProperty(const TSharedPtr<FJsonObject>& Params)
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

	int32 ExpressionIndex = -1;
	if (!Params->TryGetNumberField(TEXT("expressionIndex"), ExpressionIndex))
	{
		return MCPError(TEXT("Missing required parameter 'expressionIndex'"));
	}

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("property"), PropertyName)) return Err;

	int32 OutputIndex = OptionalInt(Params, TEXT("outputIndex"), 0);

	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
	}

	auto Expressions = Material->GetExpressions();

	if (ExpressionIndex < 0 || ExpressionIndex >= Expressions.Num())
	{
		return MCPError(FString::Printf(TEXT("Expression index %d out of range (0-%d)"), ExpressionIndex, Expressions.Num() - 1));
	}

	UMaterialExpression* Expression = Expressions[ExpressionIndex];
	if (!Expression)
	{
		return MCPError(TEXT("Expression at given index is null"));
	}

	EMaterialProperty MatProperty;
	if (!ParseMaterialProperty(PropertyName, MatProperty))
	{
		return MCPError(FString::Printf(TEXT("Unknown material property '%s'. Available: BaseColor, Metallic, Specular, Roughness, Anisotropy, EmissiveColor, Opacity, OpacityMask, Normal, Tangent, WorldPositionOffset, SubsurfaceColor, AmbientOcclusion, Refraction, PixelDepthOffset, ShadingModel"), *PropertyName));
	}

	Material->PreEditChange(nullptr);

	// Get the editor-only data to access the material property inputs
	UMaterialEditorOnlyData* EditorOnlyData = Material->GetEditorOnlyData();
	FExpressionInput* PropertyInput = nullptr;

	switch (MatProperty)
	{
	case MP_BaseColor:            PropertyInput = &EditorOnlyData->BaseColor; break;
	case MP_Metallic:             PropertyInput = &EditorOnlyData->Metallic; break;
	case MP_Specular:             PropertyInput = &EditorOnlyData->Specular; break;
	case MP_Roughness:            PropertyInput = &EditorOnlyData->Roughness; break;
	case MP_Anisotropy:           PropertyInput = &EditorOnlyData->Anisotropy; break;
	case MP_EmissiveColor:        PropertyInput = &EditorOnlyData->EmissiveColor; break;
	case MP_Opacity:              PropertyInput = &EditorOnlyData->Opacity; break;
	case MP_OpacityMask:          PropertyInput = &EditorOnlyData->OpacityMask; break;
	case MP_Normal:               PropertyInput = &EditorOnlyData->Normal; break;
	case MP_Tangent:              PropertyInput = &EditorOnlyData->Tangent; break;
	case MP_WorldPositionOffset:  PropertyInput = &EditorOnlyData->WorldPositionOffset; break;
	case MP_SubsurfaceColor:      PropertyInput = &EditorOnlyData->SubsurfaceColor; break;
	case MP_AmbientOcclusion:     PropertyInput = &EditorOnlyData->AmbientOcclusion; break;
	case MP_Refraction:           PropertyInput = &EditorOnlyData->Refraction; break;
	case MP_PixelDepthOffset:     PropertyInput = &EditorOnlyData->PixelDepthOffset; break;
	case MP_ShadingModel:         PropertyInput = &EditorOnlyData->ShadingModelFromMaterialExpression; break;
	default:
		return MCPError(FString::Printf(TEXT("Material property '%s' is not supported for direct connection"), *PropertyName));
	}

	PropertyInput->Connect(OutputIndex, Expression);

	Material->PostEditChange();
	Material->MarkPackageDirty();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetNumberField(TEXT("expressionIndex"), ExpressionIndex);
	Result->SetStringField(TEXT("expressionClass"), Expression->GetClass()->GetName());
	Result->SetStringField(TEXT("property"), PropertyName);
	Result->SetNumberField(TEXT("outputIndex"), OutputIndex);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::DeleteExpression(const TSharedPtr<FJsonObject>& Params)
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

	int32 ExpressionIndex = -1;
	if (!Params->TryGetNumberField(TEXT("expressionIndex"), ExpressionIndex))
	{
		return MCPError(TEXT("Missing required parameter 'expressionIndex'"));
	}

	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
	}

	auto Expressions = Material->GetExpressions();

	if (ExpressionIndex < 0 || ExpressionIndex >= Expressions.Num())
	{
		return MCPError(FString::Printf(TEXT("Expression index %d out of range (0-%d)"), ExpressionIndex, Expressions.Num() - 1));
	}

	UMaterialExpression* Expression = Expressions[ExpressionIndex];
	if (!Expression)
	{
		return MCPError(TEXT("Expression at given index is null"));
	}

	FString DeletedClass = Expression->GetClass()->GetName();

	Material->PreEditChange(nullptr);

	Material->GetExpressionCollection().RemoveExpression(Expression);

	Material->PostEditChange();
	Material->MarkPackageDirty();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetNumberField(TEXT("deletedIndex"), ExpressionIndex);
	Result->SetStringField(TEXT("deletedClass"), DeletedClass);
	Result->SetNumberField(TEXT("expressionCount"), Material->GetExpressions().Num());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::SetExpressionValue(const TSharedPtr<FJsonObject>& Params)
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

	int32 ExpressionIndex = -1;
	if (!Params->TryGetNumberField(TEXT("expressionIndex"), ExpressionIndex))
	{
		return MCPError(TEXT("Missing required parameter 'expressionIndex'"));
	}

	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
	}

	auto Expressions = Material->GetExpressions();

	if (ExpressionIndex < 0 || ExpressionIndex >= Expressions.Num())
	{
		return MCPError(FString::Printf(TEXT("Expression index %d out of range (0-%d)"), ExpressionIndex, Expressions.Num() - 1));
	}

	UMaterialExpression* Expression = Expressions[ExpressionIndex];
	if (!Expression)
	{
		return MCPError(TEXT("Expression at given index is null"));
	}

	Material->PreEditChange(nullptr);

	FString ExpressionClass = Expression->GetClass()->GetName();
	bool bValueSet = false;

	auto Result = MCPSuccess();

	// Handle UMaterialExpressionConstant - has a single float "R" value
	if (UMaterialExpressionConstant* ConstExpr = Cast<UMaterialExpressionConstant>(Expression))
	{
		double Value = 0.0;
		if (Params->TryGetNumberField(TEXT("value"), Value))
		{
			ConstExpr->R = static_cast<float>(Value);
			bValueSet = true;
			Result->SetNumberField(TEXT("value"), Value);
		}
	}
	// Handle UMaterialExpressionConstant2Vector
	else if (UMaterialExpressionConstant2Vector* Const2Expr = Cast<UMaterialExpressionConstant2Vector>(Expression))
	{
		double R = 0.0, G = 0.0;
		if (Params->TryGetNumberField(TEXT("r"), R)) { Const2Expr->R = static_cast<float>(R); bValueSet = true; }
		if (Params->TryGetNumberField(TEXT("g"), G)) { Const2Expr->G = static_cast<float>(G); bValueSet = true; }

		if (bValueSet)
		{
			Result->SetNumberField(TEXT("r"), Const2Expr->R);
			Result->SetNumberField(TEXT("g"), Const2Expr->G);
		}
	}
	// Handle UMaterialExpressionConstant3Vector - has FLinearColor Constant
	else if (UMaterialExpressionConstant3Vector* Const3Expr = Cast<UMaterialExpressionConstant3Vector>(Expression))
	{
		const TSharedPtr<FJsonObject>* ColorObj = nullptr;
		if (Params->TryGetObjectField(TEXT("value"), ColorObj))
		{
			double R = 0.0, G = 0.0, B = 0.0, A = 1.0;
			(*ColorObj)->TryGetNumberField(TEXT("r"), R);
			(*ColorObj)->TryGetNumberField(TEXT("g"), G);
			(*ColorObj)->TryGetNumberField(TEXT("b"), B);
			(*ColorObj)->TryGetNumberField(TEXT("a"), A);
			Const3Expr->Constant = FLinearColor(R, G, B, A);
			bValueSet = true;

			TSharedPtr<FJsonObject> ColorResult = MakeShared<FJsonObject>();
			ColorResult->SetNumberField(TEXT("r"), R);
			ColorResult->SetNumberField(TEXT("g"), G);
			ColorResult->SetNumberField(TEXT("b"), B);
			ColorResult->SetNumberField(TEXT("a"), A);
			Result->SetObjectField(TEXT("value"), ColorResult);
		}
		else
		{
			// Also support individual r, g, b, a fields directly
			double R = 0.0, G = 0.0, B = 0.0, A = 1.0;
			bool bHasR = Params->TryGetNumberField(TEXT("r"), R);
			bool bHasG = Params->TryGetNumberField(TEXT("g"), G);
			bool bHasB = Params->TryGetNumberField(TEXT("b"), B);
			Params->TryGetNumberField(TEXT("a"), A);
			if (bHasR || bHasG || bHasB)
			{
				Const3Expr->Constant = FLinearColor(R, G, B, A);
				bValueSet = true;
				Result->SetNumberField(TEXT("r"), R);
				Result->SetNumberField(TEXT("g"), G);
				Result->SetNumberField(TEXT("b"), B);
				Result->SetNumberField(TEXT("a"), A);
			}
		}
	}
	// Handle UMaterialExpressionConstant4Vector
	else if (UMaterialExpressionConstant4Vector* Const4Expr = Cast<UMaterialExpressionConstant4Vector>(Expression))
	{
		const TSharedPtr<FJsonObject>* ColorObj = nullptr;
		if (Params->TryGetObjectField(TEXT("value"), ColorObj))
		{
			double R = 0.0, G = 0.0, B = 0.0, A = 1.0;
			(*ColorObj)->TryGetNumberField(TEXT("r"), R);
			(*ColorObj)->TryGetNumberField(TEXT("g"), G);
			(*ColorObj)->TryGetNumberField(TEXT("b"), B);
			(*ColorObj)->TryGetNumberField(TEXT("a"), A);
			Const4Expr->Constant = FLinearColor(R, G, B, A);
			bValueSet = true;

			TSharedPtr<FJsonObject> ColorResult = MakeShared<FJsonObject>();
			ColorResult->SetNumberField(TEXT("r"), R);
			ColorResult->SetNumberField(TEXT("g"), G);
			ColorResult->SetNumberField(TEXT("b"), B);
			ColorResult->SetNumberField(TEXT("a"), A);
			Result->SetObjectField(TEXT("value"), ColorResult);
		}
		else
		{
			double R = 0.0, G = 0.0, B = 0.0, A = 1.0;
			bool bHasR = Params->TryGetNumberField(TEXT("r"), R);
			bool bHasG = Params->TryGetNumberField(TEXT("g"), G);
			bool bHasB = Params->TryGetNumberField(TEXT("b"), B);
			Params->TryGetNumberField(TEXT("a"), A);
			if (bHasR || bHasG || bHasB)
			{
				Const4Expr->Constant = FLinearColor(R, G, B, A);
				bValueSet = true;
				Result->SetNumberField(TEXT("r"), R);
				Result->SetNumberField(TEXT("g"), G);
				Result->SetNumberField(TEXT("b"), B);
				Result->SetNumberField(TEXT("a"), A);
			}
		}
	}
	// Handle UMaterialExpressionScalarParameter - has float DefaultValue
	else if (UMaterialExpressionScalarParameter* ScalarParamExpr = Cast<UMaterialExpressionScalarParameter>(Expression))
	{
		double Value = 0.0;
		if (Params->TryGetNumberField(TEXT("value"), Value))
		{
			ScalarParamExpr->DefaultValue = static_cast<float>(Value);
			bValueSet = true;
			Result->SetNumberField(TEXT("value"), Value);
		}

		FString ParamName;
		if (Params->TryGetStringField(TEXT("parameterName"), ParamName))
		{
			ScalarParamExpr->ParameterName = FName(*ParamName);
			bValueSet = true;
			Result->SetStringField(TEXT("parameterName"), ParamName);
		}
	}
	// Handle UMaterialExpressionVectorParameter - has FLinearColor DefaultValue
	else if (UMaterialExpressionVectorParameter* VectorParamExpr = Cast<UMaterialExpressionVectorParameter>(Expression))
	{
		const TSharedPtr<FJsonObject>* ValueObj = nullptr;
		if (Params->TryGetObjectField(TEXT("value"), ValueObj))
		{
			double R = 0.0, G = 0.0, B = 0.0, A = 1.0;
			(*ValueObj)->TryGetNumberField(TEXT("r"), R);
			(*ValueObj)->TryGetNumberField(TEXT("g"), G);
			(*ValueObj)->TryGetNumberField(TEXT("b"), B);
			(*ValueObj)->TryGetNumberField(TEXT("a"), A);
			VectorParamExpr->DefaultValue = FLinearColor(R, G, B, A);
			bValueSet = true;

			TSharedPtr<FJsonObject> ColorResult = MakeShared<FJsonObject>();
			ColorResult->SetNumberField(TEXT("r"), R);
			ColorResult->SetNumberField(TEXT("g"), G);
			ColorResult->SetNumberField(TEXT("b"), B);
			ColorResult->SetNumberField(TEXT("a"), A);
			Result->SetObjectField(TEXT("value"), ColorResult);
		}

		FString ParamName;
		if (Params->TryGetStringField(TEXT("parameterName"), ParamName))
		{
			VectorParamExpr->ParameterName = FName(*ParamName);
			bValueSet = true;
			Result->SetStringField(TEXT("parameterName"), ParamName);
		}
	}
	// Handle UMaterialExpressionTextureSample - has UTexture* Texture
	else if (UMaterialExpressionTextureSample* TexSampleExpr = Cast<UMaterialExpressionTextureSample>(Expression))
	{
		FString TexturePath;
		if (Params->TryGetStringField(TEXT("texturePath"), TexturePath))
		{
			UTexture* Texture = Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr, *TexturePath));
			if (!Texture)
			{
				Texture = Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr,
					*(TEXT("Texture2D'") + TexturePath + TEXT("'"))));
			}
			if (Texture)
			{
				TexSampleExpr->Texture = Texture;
				bValueSet = true;
				Result->SetStringField(TEXT("texturePath"), Texture->GetPathName());
			}
			else
			{
				Material->PostEditChange();
				return MCPError(FString::Printf(TEXT("Failed to load texture at '%s'"), *TexturePath));
			}
		}
	}
	// Handle UMaterialExpressionTextureCoordinate
	else if (UMaterialExpressionTextureCoordinate* TexCoordExpr = Cast<UMaterialExpressionTextureCoordinate>(Expression))
	{
		double UTiling = 1.0, VTiling = 1.0;
		if (Params->TryGetNumberField(TEXT("uTiling"), UTiling))
		{
			TexCoordExpr->UTiling = static_cast<float>(UTiling);
			bValueSet = true;
		}
		if (Params->TryGetNumberField(TEXT("vTiling"), VTiling))
		{
			TexCoordExpr->VTiling = static_cast<float>(VTiling);
			bValueSet = true;
		}

		int32 CoordinateIndex = 0;
		if (Params->TryGetNumberField(TEXT("coordinateIndex"), CoordinateIndex))
		{
			TexCoordExpr->CoordinateIndex = CoordinateIndex;
			bValueSet = true;
		}

		if (bValueSet)
		{
			Result->SetNumberField(TEXT("uTiling"), TexCoordExpr->UTiling);
			Result->SetNumberField(TEXT("vTiling"), TexCoordExpr->VTiling);
			Result->SetNumberField(TEXT("coordinateIndex"), TexCoordExpr->CoordinateIndex);
		}
	}

	if (!bValueSet)
	{
		Material->PostEditChange();
		return MCPError(FString::Printf(TEXT("Could not set value on expression of type '%s'. Provide appropriate value parameters for this expression type."), *ExpressionClass));
	}

	Material->PostEditChange();
	Material->MarkPackageDirty();

	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetNumberField(TEXT("expressionIndex"), ExpressionIndex);
	Result->SetStringField(TEXT("expressionClass"), ExpressionClass);

	return MCPResult(Result);
}

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
	NewMaterial->GetEditorOnlyData()->BaseColor.Connect(0, TextureSampleExpr);

	NewMaterial->PostEditChange();

	// Save the package
	UPackage* Package = NewMaterial->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

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

TSharedPtr<FJsonValue> FMaterialHandlers::ReadMaterialInstance(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UMaterialInstanceConstant* MaterialInstance = LoadMaterialInstanceFromPath(AssetPath);
	if (!MaterialInstance)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material instance at '%s'"), *AssetPath));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("name"), MaterialInstance->GetName());
	Result->SetStringField(TEXT("path"), MaterialInstance->GetPathName());
	Result->SetBoolField(TEXT("isMaterialInstance"), true);

	// Parent material
	UMaterialInterface* Parent = MaterialInstance->Parent;
	if (Parent)
	{
		Result->SetStringField(TEXT("parent"), Parent->GetPathName());
	}
	else
	{
		Result->SetStringField(TEXT("parent"), TEXT(""));
	}

	// Scalar parameter overrides
	TArray<TSharedPtr<FJsonValue>> ScalarOverrides;
	for (const FScalarParameterValue& ScalarParam : MaterialInstance->ScalarParameterValues)
	{
		TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
		ParamObj->SetStringField(TEXT("name"), ScalarParam.ParameterInfo.Name.ToString());
		ParamObj->SetNumberField(TEXT("value"), ScalarParam.ParameterValue);
		ScalarOverrides.Add(MakeShared<FJsonValueObject>(ParamObj));
	}

	// Vector parameter overrides
	TArray<TSharedPtr<FJsonValue>> VectorOverrides;
	for (const FVectorParameterValue& VectorParam : MaterialInstance->VectorParameterValues)
	{
		TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
		ParamObj->SetStringField(TEXT("name"), VectorParam.ParameterInfo.Name.ToString());

		TSharedPtr<FJsonObject> ValueObj = MakeShared<FJsonObject>();
		ValueObj->SetNumberField(TEXT("r"), VectorParam.ParameterValue.R);
		ValueObj->SetNumberField(TEXT("g"), VectorParam.ParameterValue.G);
		ValueObj->SetNumberField(TEXT("b"), VectorParam.ParameterValue.B);
		ValueObj->SetNumberField(TEXT("a"), VectorParam.ParameterValue.A);
		ParamObj->SetObjectField(TEXT("value"), ValueObj);

		VectorOverrides.Add(MakeShared<FJsonValueObject>(ParamObj));
	}

	// Texture parameter overrides
	TArray<TSharedPtr<FJsonValue>> TextureOverrides;
	for (const FTextureParameterValue& TextureParam : MaterialInstance->TextureParameterValues)
	{
		TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
		ParamObj->SetStringField(TEXT("name"), TextureParam.ParameterInfo.Name.ToString());
		if (TextureParam.ParameterValue)
		{
			ParamObj->SetStringField(TEXT("value"), TextureParam.ParameterValue->GetPathName());
		}
		else
		{
			ParamObj->SetStringField(TEXT("value"), TEXT(""));
		}
		TextureOverrides.Add(MakeShared<FJsonValueObject>(ParamObj));
	}

	Result->SetArrayField(TEXT("scalarOverrides"), ScalarOverrides);
	Result->SetArrayField(TEXT("vectorOverrides"), VectorOverrides);
	Result->SetArrayField(TEXT("textureOverrides"), TextureOverrides);
	Result->SetNumberField(TEXT("totalOverrides"), ScalarOverrides.Num() + VectorOverrides.Num() + TextureOverrides.Num());

	return MCPResult(Result);
}

UMaterialExpression* FMaterialHandlers::FindExpressionByName(UMaterial* Material, const FString& ExpressionName)
{
	if (!Material || ExpressionName.IsEmpty()) return nullptr;

	// Try matching by description first (most specific)
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (Expression && Expression->GetDescription() == ExpressionName)
		{
			return Expression;
		}
	}

	// Try matching by class name (with or without prefix)
	FString NameWithPrefix = ExpressionName;
	if (!NameWithPrefix.StartsWith(TEXT("MaterialExpression")))
	{
		NameWithPrefix = TEXT("MaterialExpression") + ExpressionName;
	}

	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (!Expression) continue;
		FString ClassName = Expression->GetClass()->GetName();
		if (ClassName == ExpressionName || ClassName == NameWithPrefix)
		{
			return Expression;
		}
	}

	// Try matching by parameter name for parameter expressions
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (!Expression) continue;
		if (UMaterialExpressionScalarParameter* ScalarParam = Cast<UMaterialExpressionScalarParameter>(Expression))
		{
			if (ScalarParam->ParameterName.ToString() == ExpressionName)
			{
				return Expression;
			}
		}
		else if (UMaterialExpressionVectorParameter* VectorParam = Cast<UMaterialExpressionVectorParameter>(Expression))
		{
			if (VectorParam->ParameterName.ToString() == ExpressionName)
			{
				return Expression;
			}
		}
	}

	// Try matching as an index string (e.g. "0", "1", "2")
	if (ExpressionName.IsNumeric())
	{
		int32 Idx = FCString::Atoi(*ExpressionName);
		auto Expressions = Material->GetExpressions();
		if (Idx >= 0 && Idx < Expressions.Num())
		{
			return Expressions[Idx];
		}
	}

	return nullptr;
}

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

	// Create a TextureSample expression
	UMaterialExpressionTextureSample* TextureSampleExpr = NewObject<UMaterialExpressionTextureSample>(Material);
	TextureSampleExpr->Texture = Texture;
	TextureSampleExpr->MaterialExpressionEditorX = -400;
	TextureSampleExpr->MaterialExpressionEditorY = 0;

	Material->GetExpressionCollection().AddExpression(TextureSampleExpr);

	// Connect RGB output (index 0) to the requested material property
	UMaterialEditorOnlyData* EditorOnlyData = Material->GetEditorOnlyData();
	FExpressionInput* PropertyInput = nullptr;

	switch (MatProperty)
	{
	case MP_BaseColor:            PropertyInput = &EditorOnlyData->BaseColor; break;
	case MP_Metallic:             PropertyInput = &EditorOnlyData->Metallic; break;
	case MP_Specular:             PropertyInput = &EditorOnlyData->Specular; break;
	case MP_Roughness:            PropertyInput = &EditorOnlyData->Roughness; break;
	case MP_Anisotropy:           PropertyInput = &EditorOnlyData->Anisotropy; break;
	case MP_EmissiveColor:        PropertyInput = &EditorOnlyData->EmissiveColor; break;
	case MP_Opacity:              PropertyInput = &EditorOnlyData->Opacity; break;
	case MP_OpacityMask:          PropertyInput = &EditorOnlyData->OpacityMask; break;
	case MP_Normal:               PropertyInput = &EditorOnlyData->Normal; break;
	case MP_Tangent:              PropertyInput = &EditorOnlyData->Tangent; break;
	case MP_WorldPositionOffset:  PropertyInput = &EditorOnlyData->WorldPositionOffset; break;
	case MP_SubsurfaceColor:      PropertyInput = &EditorOnlyData->SubsurfaceColor; break;
	case MP_AmbientOcclusion:     PropertyInput = &EditorOnlyData->AmbientOcclusion; break;
	case MP_Refraction:           PropertyInput = &EditorOnlyData->Refraction; break;
	case MP_PixelDepthOffset:     PropertyInput = &EditorOnlyData->PixelDepthOffset; break;
	case MP_ShadingModel:         PropertyInput = &EditorOnlyData->ShadingModelFromMaterialExpression; break;
	default: break;
	}

	if (PropertyInput)
	{
		PropertyInput->Connect(0, TextureSampleExpr);
	}

	Material->PostEditChange();
	Material->MarkPackageDirty();

	auto Result = MCPSuccess();
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

	// Resolve source output index
	int32 SourceOutputIndex = 0;
	if (!SourceOutputName.IsEmpty())
	{
		if (SourceOutputName.IsNumeric())
		{
			SourceOutputIndex = FCString::Atoi(*SourceOutputName);
		}
		else
		{
			// Try to find named output
			TArray<FExpressionOutput>& Outputs = SourceExpression->GetOutputs();
			for (int32 i = 0; i < Outputs.Num(); i++)
			{
				if (Outputs[i].OutputName.ToString().Equals(SourceOutputName, ESearchCase::IgnoreCase))
				{
					SourceOutputIndex = i;
					break;
				}
			}
		}
	}

	// Resolve target input index
	int32 TargetInputIndex = 0;
	if (!TargetInputName.IsEmpty())
	{
		if (TargetInputName.IsNumeric())
		{
			TargetInputIndex = FCString::Atoi(*TargetInputName);
		}
		else
		{
			// Try to find named input
			for (int32 i = 0; ; i++)
			{
				FExpressionInput* Input = TargetExpression->GetInput(i);
				if (!Input) break;
				FName InputName = TargetExpression->GetInputName(i);
				if (InputName.ToString().Equals(TargetInputName, ESearchCase::IgnoreCase))
				{
					TargetInputIndex = i;
					break;
				}
			}
		}
	}

	FExpressionInput* TargetInput = TargetExpression->GetInput(TargetInputIndex);
	if (!TargetInput)
	{
		return MCPError(FString::Printf(TEXT("Target input index %d is out of range"), TargetInputIndex));
	}

	Material->PreEditChange(nullptr);
	TargetInput->Connect(SourceOutputIndex, SourceExpression);
	Material->PostEditChange();
	Material->MarkPackageDirty();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetStringField(TEXT("sourceExpression"), SourceExpression->GetClass()->GetName());
	Result->SetStringField(TEXT("targetExpression"), TargetExpression->GetClass()->GetName());
	Result->SetNumberField(TEXT("sourceOutputIndex"), SourceOutputIndex);
	Result->SetNumberField(TEXT("targetInputIndex"), TargetInputIndex);

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
	FExpressionInput* PropertyInput = nullptr;

	switch (MatProperty)
	{
	case MP_BaseColor:            PropertyInput = &EditorOnlyData->BaseColor; break;
	case MP_Metallic:             PropertyInput = &EditorOnlyData->Metallic; break;
	case MP_Specular:             PropertyInput = &EditorOnlyData->Specular; break;
	case MP_Roughness:            PropertyInput = &EditorOnlyData->Roughness; break;
	case MP_Anisotropy:           PropertyInput = &EditorOnlyData->Anisotropy; break;
	case MP_EmissiveColor:        PropertyInput = &EditorOnlyData->EmissiveColor; break;
	case MP_Opacity:              PropertyInput = &EditorOnlyData->Opacity; break;
	case MP_OpacityMask:          PropertyInput = &EditorOnlyData->OpacityMask; break;
	case MP_Normal:               PropertyInput = &EditorOnlyData->Normal; break;
	case MP_Tangent:              PropertyInput = &EditorOnlyData->Tangent; break;
	case MP_WorldPositionOffset:  PropertyInput = &EditorOnlyData->WorldPositionOffset; break;
	case MP_SubsurfaceColor:      PropertyInput = &EditorOnlyData->SubsurfaceColor; break;
	case MP_AmbientOcclusion:     PropertyInput = &EditorOnlyData->AmbientOcclusion; break;
	case MP_Refraction:           PropertyInput = &EditorOnlyData->Refraction; break;
	case MP_PixelDepthOffset:     PropertyInput = &EditorOnlyData->PixelDepthOffset; break;
	case MP_ShadingModel:         PropertyInput = &EditorOnlyData->ShadingModelFromMaterialExpression; break;
	default:
		return MCPError(FString::Printf(TEXT("Material property '%s' is not supported for direct connection"), *PropertyName));
	}

	PropertyInput->Connect(OutputIndex, Expression);

	Material->PostEditChange();
	Material->MarkPackageDirty();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetStringField(TEXT("expressionName"), ExpressionName);
	Result->SetStringField(TEXT("expressionClass"), Expression->GetClass()->GetName());
	Result->SetStringField(TEXT("property"), PropertyName);
	Result->SetNumberField(TEXT("outputIndex"), OutputIndex);

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
		return MCPError(FString::Printf(TEXT("Expression '%s' not found"), *ExpressionName));
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

	return MCPResult(Result);
}

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
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetStringField(TEXT("property"), PropertyName);

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
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::BeginMaterialTransaction(const TSharedPtr<FJsonObject>& Params)
{
	const FString Label = OptionalString(Params, TEXT("label"), TEXT("MCP Material Edit"));
	if (!GEditor) return MCPError(TEXT("GEditor not available"));
	GEditor->BeginTransaction(FText::FromString(Label));
	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("label"), Label);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::EndMaterialTransaction(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor) return MCPError(TEXT("GEditor not available"));
	const int32 Idx = GEditor->EndTransaction();
	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetNumberField(TEXT("transactionIndex"), Idx);
	return MCPResult(Result);
}
