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
	// Static lookup table built once on first call. The lowercased input is
	// looked up directly; aliases ("emissive" -> MP_EmissiveColor, "ao" ->
	// MP_AmbientOcclusion) are separate entries so the reverse direction is
	// unambiguous when we ever need it.
	static const TMap<FString, EMaterialProperty> Table = {
		{ TEXT("basecolor"),           MP_BaseColor           },
		{ TEXT("metallic"),            MP_Metallic            },
		{ TEXT("specular"),            MP_Specular            },
		{ TEXT("roughness"),           MP_Roughness           },
		{ TEXT("anisotropy"),          MP_Anisotropy          },
		{ TEXT("emissivecolor"),       MP_EmissiveColor       },
		{ TEXT("emissive"),            MP_EmissiveColor       },
		{ TEXT("opacity"),             MP_Opacity             },
		{ TEXT("opacitymask"),         MP_OpacityMask         },
		{ TEXT("normal"),              MP_Normal              },
		{ TEXT("tangent"),             MP_Tangent             },
		{ TEXT("worldpositionoffset"), MP_WorldPositionOffset },
		{ TEXT("subsurfacecolor"),     MP_SubsurfaceColor     },
		{ TEXT("ambientocclusion"),    MP_AmbientOcclusion    },
		{ TEXT("ao"),                  MP_AmbientOcclusion    },
		{ TEXT("refraction"),          MP_Refraction          },
		{ TEXT("pixeldepthoffset"),    MP_PixelDepthOffset    },
		{ TEXT("shadingmodel"),        MP_ShadingModel        },
	};
	if (const EMaterialProperty* Found = Table.Find(PropertyName.ToLower()))
	{
		OutProperty = *Found;
		return true;
	}
	return false;
}

FExpressionInput* FMaterialHandlers::GetMaterialPropertyInput(
	UMaterialEditorOnlyData* EditorOnlyData,
	EMaterialProperty MatProperty)
{
	if (!EditorOnlyData) return nullptr;
	switch (MatProperty)
	{
	case MP_BaseColor:            return &EditorOnlyData->BaseColor;
	case MP_Metallic:             return &EditorOnlyData->Metallic;
	case MP_Specular:             return &EditorOnlyData->Specular;
	case MP_Roughness:            return &EditorOnlyData->Roughness;
	case MP_Anisotropy:           return &EditorOnlyData->Anisotropy;
	case MP_EmissiveColor:        return &EditorOnlyData->EmissiveColor;
	case MP_Opacity:              return &EditorOnlyData->Opacity;
	case MP_OpacityMask:          return &EditorOnlyData->OpacityMask;
	case MP_Normal:               return &EditorOnlyData->Normal;
	case MP_Tangent:              return &EditorOnlyData->Tangent;
	case MP_WorldPositionOffset:  return &EditorOnlyData->WorldPositionOffset;
	case MP_SubsurfaceColor:      return &EditorOnlyData->SubsurfaceColor;
	case MP_AmbientOcclusion:     return &EditorOnlyData->AmbientOcclusion;
	case MP_Refraction:           return &EditorOnlyData->Refraction;
	case MP_PixelDepthOffset:     return &EditorOnlyData->PixelDepthOffset;
	case MP_ShadingModel:         return &EditorOnlyData->ShadingModelFromMaterialExpression;
	default:                      return nullptr;
	}
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

	SaveAssetPackage(NewMaterial);

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

	// Connect to base color input (guarded: GetEditorOnlyData can return null
	// on unsupported material domains, which would otherwise null-deref here)
	if (UMaterialEditorOnlyData* EOD = Material->GetEditorOnlyData())
	{
		EOD->BaseColor.Connect(0, ColorExpression);
	}

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
	SaveAssetPackage(Material);

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
	SaveAssetPackage(MaterialInstance);

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

	// Idempotency: check if input is already wired to the same source
	if (TargetInput->Expression == SourceExpression && TargetInput->OutputIndex == SourceOutputIndex)
	{
		auto Existed = MCPSuccess();
		MCPSetExisted(Existed);
		Existed->SetStringField(TEXT("materialPath"), Material->GetPathName());
		Existed->SetNumberField(TEXT("sourceIndex"), SourceIndex);
		Existed->SetNumberField(TEXT("targetIndex"), TargetIndex);
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
	Result->SetNumberField(TEXT("sourceIndex"), SourceIndex);
	Result->SetStringField(TEXT("sourceClass"), SourceExpression->GetClass()->GetName());
	Result->SetNumberField(TEXT("targetIndex"), TargetIndex);
	Result->SetStringField(TEXT("targetClass"), TargetExpression->GetClass()->GetName());
	Result->SetNumberField(TEXT("sourceOutputIndex"), SourceOutputIndex);
	Result->SetNumberField(TEXT("targetInputIndex"), TargetInputIndex);
	// No rollback: no paired disconnect_expression handler.

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
	if (!EditorOnlyData)
	{
		return MCPError(TEXT("Material has no editor-only data (is this material domain supported?)"));
	}
	FExpressionInput* PropertyInput = GetMaterialPropertyInput(EditorOnlyData, MatProperty);
	if (!PropertyInput)
	{
		return MCPError(FString::Printf(TEXT("Material property '%s' is not supported for direct connection"), *PropertyName));
	}

	// Idempotency: check if already connected to this expression with same output index
	if (PropertyInput->Expression == Expression && PropertyInput->OutputIndex == OutputIndex)
	{
		auto Existed = MCPSuccess();
		MCPSetExisted(Existed);
		Existed->SetStringField(TEXT("materialPath"), Material->GetPathName());
		Existed->SetNumberField(TEXT("expressionIndex"), ExpressionIndex);
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
	Result->SetNumberField(TEXT("expressionIndex"), ExpressionIndex);
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
	Result->SetBoolField(TEXT("deleted"), true);
	// No rollback: deletion is destructive (would need to snapshot expression + connections to reverse).

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

	// #185: Generic UPROPERTY fallback — set arbitrary properties on any expression node
	// by property name (e.g. Noise node Levels, Quality, NoiseFunction, etc.)
	if (!bValueSet)
	{
		FString PropertyName;
		if (Params->TryGetStringField(TEXT("propertyName"), PropertyName))
		{
			FProperty* Prop = Expression->GetClass()->FindPropertyByName(FName(*PropertyName));
			if (Prop)
			{
				void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Expression);

				// Determine the string value to import
				FString ValueStr;
				TSharedPtr<FJsonValue> ValueJsonRef = Params->TryGetField(TEXT("value"));
				if (ValueJsonRef.IsValid())
				{
					if (ValueJsonRef->Type == EJson::String)
					{
						ValueStr = ValueJsonRef->AsString();
					}
					else if (ValueJsonRef->Type == EJson::Boolean)
					{
						ValueStr = ValueJsonRef->AsBool() ? TEXT("True") : TEXT("False");
					}
					else if (ValueJsonRef->Type == EJson::Number)
					{
						ValueStr = FString::SanitizeFloat(ValueJsonRef->AsNumber());
					}
					else
					{
						ValueStr = FString::SanitizeFloat(0.0);
					}
				}
				else
				{
					// Try direct number/bool/string params as fallback
					double NumVal = 0.0;
					bool BoolVal = false;
					if (Params->TryGetNumberField(TEXT("value"), NumVal))
					{
						ValueStr = FString::SanitizeFloat(NumVal);
					}
					else if (Params->TryGetBoolField(TEXT("value"), BoolVal))
					{
						ValueStr = BoolVal ? TEXT("True") : TEXT("False");
					}
					else if (!Params->TryGetStringField(TEXT("value"), ValueStr))
					{
						Material->PostEditChange();
						return MCPError(FString::Printf(TEXT("Found property '%s' on expression '%s' but no 'value' parameter provided"), *PropertyName, *ExpressionClass));
					}
				}

				const TCHAR* ImportResult = Prop->ImportText_Direct(*ValueStr, ValuePtr, Expression, PPF_None);
				if (ImportResult)
				{
					bValueSet = true;
					Result->SetStringField(TEXT("propertyName"), PropertyName);
					Result->SetStringField(TEXT("importedValue"), ValueStr);
				}
				else
				{
					Material->PostEditChange();
					return MCPError(FString::Printf(TEXT("ImportText failed for property '%s' on expression '%s' with value '%s'"), *PropertyName, *ExpressionClass, *ValueStr));
				}
			}
			else
			{
				// List available properties on this expression for discoverability
				TArray<FString> PropNames;
				for (TFieldIterator<FProperty> PropIt(Expression->GetClass()); PropIt; ++PropIt)
				{
					if (PropIt->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
					{
						PropNames.Add(PropIt->GetName());
					}
				}
				Material->PostEditChange();
				return MCPError(FString::Printf(TEXT("Property '%s' not found on expression '%s'. Editable properties: [%s]"),
					*PropertyName, *ExpressionClass, *FString::Join(PropNames, TEXT(", "))));
			}
		}
	}

	if (!bValueSet)
	{
		Material->PostEditChange();
		return MCPError(FString::Printf(TEXT("Could not set value on expression of type '%s'. For known types provide standard value params; for arbitrary expressions pass 'propertyName' + 'value'."), *ExpressionClass));
	}

	Material->PostEditChange();
	Material->MarkPackageDirty();

	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetNumberField(TEXT("expressionIndex"), ExpressionIndex);
	Result->SetStringField(TEXT("expressionClass"), ExpressionClass);
	// No rollback: would require per-expression-type before-state capture across many expression variants.

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