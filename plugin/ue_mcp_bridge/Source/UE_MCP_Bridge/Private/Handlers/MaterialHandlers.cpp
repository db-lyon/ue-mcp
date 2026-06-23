#include "MaterialHandlers.h"
#include "UE_MCP_BridgeModule.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "StaticParameterSet.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionComponentMask.h"
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
	Registry.RegisterHandler(TEXT("set_material_domain"), &SetMaterialDomain);
	Registry.RegisterHandler(TEXT("set_material_base_color"), &SetMaterialBaseColor);
	Registry.RegisterHandler(TEXT("add_material_expression"), &AddMaterialExpression);
	Registry.RegisterHandler(TEXT("list_material_expressions"), &ListMaterialExpressions);
	Registry.RegisterHandler(TEXT("list_material_parameters"), &ListMaterialParameters);
	Registry.RegisterHandler(TEXT("recompile_material"), &RecompileMaterial);
	Registry.RegisterHandler(TEXT("create_material_instance"), &CreateMaterialInstance);
	Registry.RegisterHandler(TEXT("set_material_parameter"), &SetMaterialParameter);
	Registry.RegisterHandler(TEXT("read_material_instance"), &ReadMaterialInstance);
	Registry.RegisterHandler(TEXT("set_material_instance_parent"), &SetMaterialInstanceParent);
	Registry.RegisterHandler(TEXT("clear_material_instance_parameters"), &ClearMaterialInstanceParameters);
	Registry.RegisterHandler(TEXT("list_material_static_switches"), &ListMaterialStaticSwitches);
	Registry.RegisterHandler(TEXT("set_material_static_switch"), &SetMaterialStaticSwitch);
	Registry.RegisterHandler(TEXT("set_expression_value"), &SetExpressionValue);

	// Expression graph operations
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

	Registry.RegisterHandler(TEXT("create_material_simple"), &CreateMaterialSimple);
	Registry.RegisterHandler(TEXT("set_material_usage"), &SetMaterialUsage);

	// #463: MaterialFunction authoring.
	Registry.RegisterHandler(TEXT("create_material_function"), &CreateMaterialFunction);
	Registry.RegisterHandler(TEXT("add_material_function_expression"), &AddMaterialFunctionExpression);
	Registry.RegisterHandler(TEXT("add_expression_in_function"), &AddMaterialFunctionExpression);
	Registry.RegisterHandler(TEXT("connect_material_function_expressions"), &ConnectMaterialFunctionExpressions);
	Registry.RegisterHandler(TEXT("connect_expressions_in_function"), &ConnectMaterialFunctionExpressions);
	Registry.RegisterHandler(TEXT("list_material_function_expressions"), &ListMaterialFunctionExpressions);
	Registry.RegisterHandler(TEXT("list_expressions_in_function"), &ListMaterialFunctionExpressions);
}

UMaterial* FMaterialHandlers::LoadMaterialFromPath(const FString& AssetPath)
{
	return LoadAssetByPath<UMaterial>(AssetPath);
}

UMaterialInstanceConstant* FMaterialHandlers::LoadMaterialInstanceFromPath(const FString& AssetPath)
{
	return LoadAssetByPath<UMaterialInstanceConstant>(AssetPath);
}

namespace
{
	FString MaterialParameterAssociationToString(EMaterialParameterAssociation Association)
	{
		switch (Association)
		{
		case EMaterialParameterAssociation::LayerParameter: return TEXT("Layer");
		case EMaterialParameterAssociation::BlendParameter: return TEXT("Blend");
		case EMaterialParameterAssociation::GlobalParameter:
		default: return TEXT("Global");
		}
	}

	EMaterialParameterAssociation ParseMaterialParameterAssociation(const FString& Association)
	{
		const FString Lower = Association.ToLower();
		if (Lower == TEXT("layer") || Lower == TEXT("layerparameter")) return EMaterialParameterAssociation::LayerParameter;
		if (Lower == TEXT("blend") || Lower == TEXT("blendparameter")) return EMaterialParameterAssociation::BlendParameter;
		return EMaterialParameterAssociation::GlobalParameter;
	}

	FMaterialParameterInfo MakeMaterialParameterInfoFromParams(
		const TSharedPtr<FJsonObject>& Params,
		const FString& ParameterName)
	{
		const EMaterialParameterAssociation Association = ParseMaterialParameterAssociation(
			OptionalString(Params, TEXT("association"), TEXT("Global")));
		const int32 DefaultIndex = Association == EMaterialParameterAssociation::GlobalParameter ? INDEX_NONE : 0;
		const int32 Index = OptionalInt(Params, TEXT("parameterIndex"), DefaultIndex);
		return FMaterialParameterInfo(FName(*ParameterName), Association, Index);
	}

	TSharedPtr<FJsonObject> MaterialParameterInfoToJson(const FMaterialParameterInfo& Info)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Info.Name.ToString());
		Obj->SetStringField(TEXT("association"), MaterialParameterAssociationToString(Info.Association));
		Obj->SetNumberField(TEXT("index"), Info.Index);
		Obj->SetStringField(TEXT("fullName"), Info.ToString());
		return Obj;
	}

	TSharedPtr<FJsonObject> LinearColorToJson(const FLinearColor& Color)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("r"), Color.R);
		Obj->SetNumberField(TEXT("g"), Color.G);
		Obj->SetNumberField(TEXT("b"), Color.B);
		Obj->SetNumberField(TEXT("a"), Color.A);
		return Obj;
	}

	TSharedPtr<FJsonObject> MaterialInstanceOverrideCounts(UMaterialInstanceConstant* Instance)
	{
		TSharedPtr<FJsonObject> Counts = MakeShared<FJsonObject>();
		if (!Instance)
		{
			return Counts;
		}

		FStaticParameterSet StaticParameters;
		Instance->GetStaticParameterValues(StaticParameters);
		int32 StaticSwitchOverrideCount = 0;
		for (const FStaticSwitchParameter& Parameter : StaticParameters.StaticSwitchParameters)
		{
			if (Parameter.bOverride) ++StaticSwitchOverrideCount;
		}

		Counts->SetNumberField(TEXT("scalar"), Instance->ScalarParameterValues.Num());
		Counts->SetNumberField(TEXT("vector"), Instance->VectorParameterValues.Num());
		Counts->SetNumberField(TEXT("doubleVector"), Instance->DoubleVectorParameterValues.Num());
		Counts->SetNumberField(TEXT("texture"), Instance->TextureParameterValues.Num());
		Counts->SetNumberField(TEXT("runtimeVirtualTexture"), Instance->RuntimeVirtualTextureParameterValues.Num());
		Counts->SetNumberField(TEXT("sparseVolumeTexture"), Instance->SparseVolumeTextureParameterValues.Num());
		Counts->SetNumberField(TEXT("font"), Instance->FontParameterValues.Num());
		Counts->SetNumberField(TEXT("staticSwitch"), StaticSwitchOverrideCount);
		return Counts;
	}

	int32 CountTotalMaterialInstanceOverrides(UMaterialInstanceConstant* Instance)
	{
		if (!Instance)
		{
			return 0;
		}

		FStaticParameterSet StaticParameters;
		Instance->GetStaticParameterValues(StaticParameters);
		int32 StaticSwitchOverrideCount = 0;
		for (const FStaticSwitchParameter& Parameter : StaticParameters.StaticSwitchParameters)
		{
			if (Parameter.bOverride) ++StaticSwitchOverrideCount;
		}

		return Instance->ScalarParameterValues.Num()
			+ Instance->VectorParameterValues.Num()
			+ Instance->DoubleVectorParameterValues.Num()
			+ Instance->TextureParameterValues.Num()
			+ Instance->RuntimeVirtualTextureParameterValues.Num()
			+ Instance->SparseVolumeTextureParameterValues.Num()
			+ Instance->FontParameterValues.Num()
			+ StaticSwitchOverrideCount;
	}

	TArray<TSharedPtr<FJsonValue>> MaterialStaticSwitchesToJson(UMaterialInterface* Material)
	{
		TArray<TSharedPtr<FJsonValue>> Switches;
		if (!Material)
		{
			return Switches;
		}

		FStaticParameterSet StaticParameters;
		Material->GetStaticParameterValues(StaticParameters);
		for (const FStaticSwitchParameter& Parameter : StaticParameters.StaticSwitchParameters)
		{
			TSharedPtr<FJsonObject> Obj = MaterialParameterInfoToJson(Parameter.ParameterInfo);
			Obj->SetBoolField(TEXT("value"), Parameter.Value);
			Obj->SetBoolField(TEXT("override"), Parameter.bOverride);
			Obj->SetStringField(TEXT("expressionGuid"), Parameter.ExpressionGUID.ToString(EGuidFormats::DigitsWithHyphens));
			Switches.Add(MakeShared<FJsonValueObject>(Obj));
		}
		return Switches;
	}

	void SetMaterialInstanceSummaryFields(TSharedPtr<FJsonObject> Result, UMaterialInstanceConstant* Instance)
	{
		if (!Result.IsValid() || !Instance)
		{
			return;
		}

		Result->SetStringField(TEXT("name"), Instance->GetName());
		Result->SetStringField(TEXT("path"), Instance->GetPathName());
		Result->SetStringField(TEXT("parentPath"), Instance->Parent ? Instance->Parent->GetPathName() : FString());
		Result->SetObjectField(TEXT("overrideCounts"), MaterialInstanceOverrideCounts(Instance));
		Result->SetNumberField(TEXT("overrideCount"), CountTotalMaterialInstanceOverrides(Instance));
	}
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

	UMaterialFactoryNew* MaterialFactory = NewObject<UMaterialFactoryNew>();
	auto Created = MCPCreateAssetIdempotent<UMaterial>(Name, PackagePath, OnConflict, TEXT("Material"), MaterialFactory);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	SaveAssetPackage(Created.Asset);
	const FString AssetPath = Created.Asset->GetPathName();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("packagePath"), PackagePath);
	MCPSetDeleteAssetRollback(Result, AssetPath);
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

// #299/#356: native setter for UMaterial.MaterialDomain. Required to build
// PostProcess / UI / DeferredDecal / Volume / LightFunction materials without
// dropping out to execute_python -> MaterialEditingLibrary.
TSharedPtr<FJsonValue> FMaterialHandlers::SetMaterialDomain(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString DomainStr;
	if (auto Err = RequireStringAlt(Params, TEXT("materialDomain"), TEXT("domain"), DomainStr)) return Err;

	UMaterial* Material = LoadMaterialFromPath(AssetPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *AssetPath));
	}

	const FString N = DomainStr;
	EMaterialDomain NewDomain = MD_Surface;
	if      (N.Equals(TEXT("Surface"),           ESearchCase::IgnoreCase) || N.Equals(TEXT("MD_Surface"),           ESearchCase::IgnoreCase)) NewDomain = MD_Surface;
	else if (N.Equals(TEXT("DeferredDecal"),     ESearchCase::IgnoreCase) || N.Equals(TEXT("MD_DeferredDecal"),     ESearchCase::IgnoreCase)) NewDomain = MD_DeferredDecal;
	else if (N.Equals(TEXT("LightFunction"),     ESearchCase::IgnoreCase) || N.Equals(TEXT("MD_LightFunction"),     ESearchCase::IgnoreCase)) NewDomain = MD_LightFunction;
	else if (N.Equals(TEXT("Volume"),            ESearchCase::IgnoreCase) || N.Equals(TEXT("MD_Volume"),            ESearchCase::IgnoreCase)) NewDomain = MD_Volume;
	else if (N.Equals(TEXT("PostProcess"),       ESearchCase::IgnoreCase) || N.Equals(TEXT("MD_PostProcess"),       ESearchCase::IgnoreCase)) NewDomain = MD_PostProcess;
	else if (N.Equals(TEXT("UI"),                ESearchCase::IgnoreCase) || N.Equals(TEXT("MD_UI"),                ESearchCase::IgnoreCase)) NewDomain = MD_UI;
	else if (N.Equals(TEXT("RuntimeVirtualTexture"), ESearchCase::IgnoreCase) || N.Equals(TEXT("MD_RuntimeVirtualTexture"), ESearchCase::IgnoreCase)) NewDomain = MD_RuntimeVirtualTexture;
	else
	{
		return MCPError(FString::Printf(
			TEXT("Unknown material domain: '%s'. Use Surface, DeferredDecal, LightFunction, Volume, PostProcess, UI, or RuntimeVirtualTexture."),
			*DomainStr));
	}

	const EMaterialDomain PrevDomain = Material->MaterialDomain;

	auto DomainName = [](EMaterialDomain D) -> FString
	{
		switch (D)
		{
		case MD_Surface:                return TEXT("Surface");
		case MD_DeferredDecal:          return TEXT("DeferredDecal");
		case MD_LightFunction:          return TEXT("LightFunction");
		case MD_Volume:                 return TEXT("Volume");
		case MD_PostProcess:            return TEXT("PostProcess");
		case MD_UI:                     return TEXT("UI");
		case MD_RuntimeVirtualTexture:  return TEXT("RuntimeVirtualTexture");
		default:                        return TEXT("Surface");
		}
	};

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), Material->GetPathName());
	Result->SetStringField(TEXT("materialDomain"), DomainName(NewDomain));

	if (PrevDomain == NewDomain)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		return MCPResult(Result);
	}

	Material->PreEditChange(nullptr);
	Material->MaterialDomain = NewDomain;
	Material->PostEditChange();
	Material->MarkPackageDirty();

	MCPSetUpdated(Result);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), Material->GetPathName());
	Payload->SetStringField(TEXT("materialDomain"), DomainName(PrevDomain));
	MCPSetRollback(Result, TEXT("set_material_domain"), Payload);

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

	// Set parameter name for parameter expressions (#318 sub-item: previously
	// TextureSampleParameter2D was silently dropped because the cast targeted
	// TextureObjectParameter; route through the common UMaterialExpressionParameter
	// base class so every Parameter subclass is covered uniformly).
	FString ParameterName;
	if (Params->TryGetStringField(TEXT("parameterName"), ParameterName))
	{
		if (UMaterialExpressionParameter* AsParameter = Cast<UMaterialExpressionParameter>(NewExpression))
		{
			AsParameter->ParameterName = FName(*ParameterName);
		}
		else if (UMaterialExpressionTextureSampleParameter* AsTextureSampleParam = Cast<UMaterialExpressionTextureSampleParameter>(NewExpression))
		{
			AsTextureSampleParam->ParameterName = FName(*ParameterName);
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

	// #318: Group/SortPriority on parameter expressions, default value on
	// scalar/vector parameters, Constant on Constant3Vector, and channel
	// flags on ComponentMask. Without these the corresponding parameter
	// authoring workflows had to fall back to MaterialEditingLibrary.
	FString GroupName;
	if (Params->TryGetStringField(TEXT("group"), GroupName))
	{
		if (UMaterialExpressionParameter* AsParameter = Cast<UMaterialExpressionParameter>(NewExpression))
		{
			AsParameter->Group = FName(*GroupName);
		}
		else if (UMaterialExpressionTextureSampleParameter* AsTextureSampleParam = Cast<UMaterialExpressionTextureSampleParameter>(NewExpression))
		{
			AsTextureSampleParam->Group = FName(*GroupName);
		}
	}
	double SortPriority = 0.0;
	if (Params->TryGetNumberField(TEXT("sortPriority"), SortPriority))
	{
		if (UMaterialExpressionParameter* AsParameter = Cast<UMaterialExpressionParameter>(NewExpression))
		{
			AsParameter->SortPriority = static_cast<int32>(SortPriority);
		}
	}

	if (UMaterialExpressionScalarParameter* ScalarParam = Cast<UMaterialExpressionScalarParameter>(NewExpression))
	{
		double DefaultValue = 0.0;
		if (Params->TryGetNumberField(TEXT("defaultValue"), DefaultValue))
		{
			ScalarParam->DefaultValue = static_cast<float>(DefaultValue);
		}
	}
	else if (UMaterialExpressionVectorParameter* VectorParam = Cast<UMaterialExpressionVectorParameter>(NewExpression))
	{
		const TSharedPtr<FJsonObject>* DefaultColorObj = nullptr;
		if (Params->TryGetObjectField(TEXT("defaultValue"), DefaultColorObj) && DefaultColorObj && (*DefaultColorObj).IsValid())
		{
			double R = 0.0, G = 0.0, B = 0.0, A = 1.0;
			(*DefaultColorObj)->TryGetNumberField(TEXT("r"), R);
			(*DefaultColorObj)->TryGetNumberField(TEXT("g"), G);
			(*DefaultColorObj)->TryGetNumberField(TEXT("b"), B);
			(*DefaultColorObj)->TryGetNumberField(TEXT("a"), A);
			VectorParam->DefaultValue = FLinearColor((float)R, (float)G, (float)B, (float)A);
		}
	}

	// Constant3Vector: bare value assignment (the previous SetMaterialBaseColor
	// pattern needed a wrapper helper; expose direct authoring here).
	// #444: also accept {R,G,B,A} or {x,y,z,w} dict shapes.
	auto ReadColorAny = [](const TSharedPtr<FJsonObject>& Obj, FLinearColor& Out) -> bool
	{
		double R = 0, G = 0, B = 0, A = 1; bool bAny = false;
		auto Pick = [&](const TCHAR* L, const TCHAR* U, const TCHAR* Alt, double& Slot)
		{
			double V; if (Obj->TryGetNumberField(L, V) || Obj->TryGetNumberField(U, V) || Obj->TryGetNumberField(Alt, V)) { Slot = V; bAny = true; }
		};
		Pick(TEXT("r"), TEXT("R"), TEXT("x"), R);
		Pick(TEXT("g"), TEXT("G"), TEXT("y"), G);
		Pick(TEXT("b"), TEXT("B"), TEXT("z"), B);
		Pick(TEXT("a"), TEXT("A"), TEXT("w"), A);
		Out = FLinearColor((float)R, (float)G, (float)B, (float)A);
		return bAny;
	};
	if (UMaterialExpressionConstant3Vector* Const3 = Cast<UMaterialExpressionConstant3Vector>(NewExpression))
	{
		const TSharedPtr<FJsonObject>* ConstColor = nullptr;
		FLinearColor Col = Const3->Constant;
		if (Params->TryGetObjectField(TEXT("value"), ConstColor) && ConstColor && (*ConstColor).IsValid() && ReadColorAny(*ConstColor, Col))
		{
			Const3->Constant = Col;
		}
		else if (Params->TryGetObjectField(TEXT("defaultValue"), ConstColor) && ConstColor && (*ConstColor).IsValid() && ReadColorAny(*ConstColor, Col))
		{
			Const3->Constant = Col;
		}
	}
	if (UMaterialExpressionConstant4Vector* Const4 = Cast<UMaterialExpressionConstant4Vector>(NewExpression))
	{
		const TSharedPtr<FJsonObject>* ConstColor = nullptr;
		FLinearColor Col = Const4->Constant;
		if (Params->TryGetObjectField(TEXT("value"), ConstColor) && ConstColor && (*ConstColor).IsValid() && ReadColorAny(*ConstColor, Col))
		{
			Const4->Constant = Col;
		}
		else if (Params->TryGetObjectField(TEXT("defaultValue"), ConstColor) && ConstColor && (*ConstColor).IsValid() && ReadColorAny(*ConstColor, Col))
		{
			Const4->Constant = Col;
		}
	}
	if (UMaterialExpressionConstant* Const1 = Cast<UMaterialExpressionConstant>(NewExpression))
	{
		double Scalar = 0.0;
		if (Params->TryGetNumberField(TEXT("value"), Scalar))
		{
			Const1->R = static_cast<float>(Scalar);
		}
	}
	if (UMaterialExpressionConstant2Vector* Const2 = Cast<UMaterialExpressionConstant2Vector>(NewExpression))
	{
		const TSharedPtr<FJsonObject>* Vec2 = nullptr;
		if (Params->TryGetObjectField(TEXT("value"), Vec2) && Vec2 && (*Vec2).IsValid())
		{
			double X = 0.0, Y = 0.0;
			(*Vec2)->TryGetNumberField(TEXT("r"), X); (*Vec2)->TryGetNumberField(TEXT("x"), X);
			(*Vec2)->TryGetNumberField(TEXT("g"), Y); (*Vec2)->TryGetNumberField(TEXT("y"), Y);
			Const2->R = static_cast<float>(X);
			Const2->G = static_cast<float>(Y);
		}
	}

	// ComponentMask: channels object {r,g,b,a} → bool flags on the node.
	if (UMaterialExpressionComponentMask* Mask = Cast<UMaterialExpressionComponentMask>(NewExpression))
	{
		const TSharedPtr<FJsonObject>* Channels = nullptr;
		if (Params->TryGetObjectField(TEXT("channels"), Channels) && Channels && (*Channels).IsValid())
		{
			bool BR = false, BG = false, BB = false, BA = false;
			(*Channels)->TryGetBoolField(TEXT("r"), BR);
			(*Channels)->TryGetBoolField(TEXT("g"), BG);
			(*Channels)->TryGetBoolField(TEXT("b"), BB);
			(*Channels)->TryGetBoolField(TEXT("a"), BA);
			Mask->R = BR; Mask->G = BG; Mask->B = BB; Mask->A = BA;
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

	// #421 gap 8: cascade to MaterialInstances so existing instance instances
	// pick up shader changes without the caller re-saving each one manually.
	bool bRecompileChildren = false;
	Params->TryGetBoolField(TEXT("recompileChildren"), bRecompileChildren);
	if (bRecompileChildren)
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& Reg = ARM.Get();
		TArray<FAssetData> AllInstances;
		FARFilter Filter;
		Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("MaterialInstanceConstant")));
		Filter.bRecursivePaths = true;
		Filter.PackagePaths.Add(FName(TEXT("/Game")));
		Reg.GetAssets(Filter, AllInstances);

		TArray<TSharedPtr<FJsonValue>> RecompiledPaths;
		const FString ParentPath = Material->GetPathName();
		for (const FAssetData& Data : AllInstances)
		{
			UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(Data.GetAsset());
			if (!MIC) continue;
			UMaterialInterface* Walk = MIC->Parent;
			bool bDescends = false;
			while (Walk)
			{
				if (Walk->GetPathName() == ParentPath) { bDescends = true; break; }
				UMaterialInstance* ParentMI = Cast<UMaterialInstance>(Walk);
				Walk = ParentMI ? ParentMI->Parent : nullptr;
			}
			if (!bDescends) continue;
			MIC->PreEditChange(nullptr);
			MIC->PostEditChange();
			MIC->MarkPackageDirty();
			RecompiledPaths.Add(MakeShared<FJsonValueString>(MIC->GetPathName()));
		}
		Result->SetArrayField(TEXT("recompiledChildren"), RecompiledPaths);
		Result->SetNumberField(TEXT("childCount"), RecompiledPaths.Num());
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::CreateMaterialInstance(const TSharedPtr<FJsonObject>& Params)
{
	FString ParentPath;
	if (auto Err = RequireString(Params, TEXT("parentPath"), ParentPath)) return Err;

	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Materials"));

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

	UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
	Factory->InitialParent = ParentMaterial;

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	auto Created = MCPCreateAssetIdempotent<UMaterialInstanceConstant>(Name, PackagePath, OnConflict, TEXT("Material instance"), Factory);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	SaveAssetPackage(Created.Asset);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), Created.Asset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("parentPath"), ParentMaterial->GetPathName());
	Result->SetStringField(TEXT("packagePath"), PackagePath);
	MCPSetDeleteAssetRollback(Result, Created.Asset->GetPathName());
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

TSharedPtr<FJsonValue> FMaterialHandlers::ReadMaterialInstance(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UMaterialInstanceConstant* Instance = LoadMaterialInstanceFromPath(AssetPath);
	if (!Instance)
	{
		return MCPError(FString::Printf(TEXT("Failed to load MaterialInstanceConstant at '%s'"), *AssetPath));
	}

	TArray<TSharedPtr<FJsonValue>> ScalarOverrides;
	for (const FScalarParameterValue& Parameter : Instance->ScalarParameterValues)
	{
		TSharedPtr<FJsonObject> Obj = MaterialParameterInfoToJson(Parameter.ParameterInfo);
		Obj->SetNumberField(TEXT("value"), Parameter.ParameterValue);
		Obj->SetStringField(TEXT("expressionGuid"), Parameter.ExpressionGUID.ToString(EGuidFormats::DigitsWithHyphens));
		ScalarOverrides.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TArray<TSharedPtr<FJsonValue>> VectorOverrides;
	for (const FVectorParameterValue& Parameter : Instance->VectorParameterValues)
	{
		TSharedPtr<FJsonObject> Obj = MaterialParameterInfoToJson(Parameter.ParameterInfo);
		Obj->SetObjectField(TEXT("value"), LinearColorToJson(Parameter.ParameterValue));
		Obj->SetStringField(TEXT("expressionGuid"), Parameter.ExpressionGUID.ToString(EGuidFormats::DigitsWithHyphens));
		VectorOverrides.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TArray<TSharedPtr<FJsonValue>> TextureOverrides;
	for (const FTextureParameterValue& Parameter : Instance->TextureParameterValues)
	{
		TSharedPtr<FJsonObject> Obj = MaterialParameterInfoToJson(Parameter.ParameterInfo);
		Obj->SetStringField(TEXT("value"), Parameter.ParameterValue ? Parameter.ParameterValue->GetPathName() : FString());
		Obj->SetStringField(TEXT("expressionGuid"), Parameter.ExpressionGUID.ToString(EGuidFormats::DigitsWithHyphens));
		TextureOverrides.Add(MakeShared<FJsonValueObject>(Obj));
	}

	auto Result = MCPSuccess();
	SetMaterialInstanceSummaryFields(Result, Instance);
	Result->SetArrayField(TEXT("scalarOverrides"), ScalarOverrides);
	Result->SetArrayField(TEXT("vectorOverrides"), VectorOverrides);
	Result->SetArrayField(TEXT("textureOverrides"), TextureOverrides);
	Result->SetArrayField(TEXT("staticSwitches"), MaterialStaticSwitchesToJson(Instance));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::SetMaterialInstanceParent(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString NewParentPath;
	if (auto Err = RequireStringAlt(Params, TEXT("newParentPath"), TEXT("parentPath"), NewParentPath)) return Err;

	UMaterialInstanceConstant* Instance = LoadMaterialInstanceFromPath(AssetPath);
	if (!Instance)
	{
		return MCPError(FString::Printf(TEXT("Failed to load MaterialInstanceConstant at '%s'"), *AssetPath));
	}

	UMaterialInterface* NewParent = LoadAssetByPath<UMaterialInterface>(NewParentPath);
	if (!NewParent)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material parent at '%s'"), *NewParentPath));
	}
	if (NewParent == Instance)
	{
		return MCPError(TEXT("A MaterialInstance cannot be its own parent"));
	}

	const FString OldParentPath = Instance->Parent ? Instance->Parent->GetPathName() : FString();
	if (Instance->Parent == NewParent)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		SetMaterialInstanceSummaryFields(Noop, Instance);
		return MCPResult(Noop);
	}

	Instance->Modify(true);
	Instance->SetParentEditorOnly(NewParent, true);
	Instance->PostEditChange();
	SaveAssetPackage(Instance);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	SetMaterialInstanceSummaryFields(Result, Instance);
	Result->SetStringField(TEXT("oldParentPath"), OldParentPath);
	Result->SetStringField(TEXT("newParentPath"), NewParent->GetPathName());

	if (!OldParentPath.IsEmpty())
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), Instance->GetPathName());
		Payload->SetStringField(TEXT("newParentPath"), OldParentPath);
		MCPSetRollback(Result, TEXT("set_material_instance_parent"), Payload);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::ClearMaterialInstanceParameters(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UMaterialInstanceConstant* Instance = LoadMaterialInstanceFromPath(AssetPath);
	if (!Instance)
	{
		return MCPError(FString::Printf(TEXT("Failed to load MaterialInstanceConstant at '%s'"), *AssetPath));
	}

	const int32 BeforeCount = CountTotalMaterialInstanceOverrides(Instance);
	if (BeforeCount == 0)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		SetMaterialInstanceSummaryFields(Noop, Instance);
		Noop->SetNumberField(TEXT("clearedOverrideCount"), 0);
		return MCPResult(Noop);
	}

	Instance->Modify(true);
	Instance->ClearParameterValuesEditorOnly();
	Instance->PostEditChange();
	SaveAssetPackage(Instance);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	SetMaterialInstanceSummaryFields(Result, Instance);
	Result->SetNumberField(TEXT("clearedOverrideCount"), BeforeCount);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::ListMaterialStaticSwitches(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UMaterialInterface* Material = LoadAssetByPath<UMaterialInterface>(AssetPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material or material instance at '%s'"), *AssetPath));
	}

	TArray<TSharedPtr<FJsonValue>> Switches = MaterialStaticSwitchesToJson(Material);
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), Material->GetPathName());
	Result->SetArrayField(TEXT("staticSwitches"), Switches);
	Result->SetNumberField(TEXT("count"), Switches.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::SetMaterialStaticSwitch(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString ParameterName;
	if (auto Err = RequireString(Params, TEXT("parameterName"), ParameterName)) return Err;

	bool bValue = false;
	if (!Params->TryGetBoolField(TEXT("value"), bValue))
	{
		return MCPError(TEXT("Missing 'value' bool field for static switch parameter"));
	}

	UMaterialInstanceConstant* Instance = LoadMaterialInstanceFromPath(AssetPath);
	if (!Instance)
	{
		return MCPError(FString::Printf(TEXT("Failed to load MaterialInstanceConstant at '%s'"), *AssetPath));
	}

	const FMaterialParameterInfo ParameterInfo = MakeMaterialParameterInfoFromParams(Params, ParameterName);
	bool bPreviousValue = false;
	FGuid PreviousGuid;
	const bool bHadPrevious = Instance->GetStaticSwitchParameterValue(
		FHashedMaterialParameterInfo(ParameterInfo),
		bPreviousValue,
		PreviousGuid);
	if (bHadPrevious && bPreviousValue == bValue)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		SetMaterialInstanceSummaryFields(Noop, Instance);
		Noop->SetStringField(TEXT("parameterName"), ParameterName);
		Noop->SetBoolField(TEXT("value"), bValue);
		return MCPResult(Noop);
	}

	Instance->Modify(true);
	Instance->SetStaticSwitchParameterValueEditorOnly(ParameterInfo, bValue);
	Instance->PostEditChange();
	SaveAssetPackage(Instance);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	SetMaterialInstanceSummaryFields(Result, Instance);
	Result->SetStringField(TEXT("parameterName"), ParameterName);
	Result->SetObjectField(TEXT("parameterInfo"), MaterialParameterInfoToJson(ParameterInfo));
	Result->SetBoolField(TEXT("value"), bValue);
	Result->SetBoolField(TEXT("hadPreviousValue"), bHadPrevious);
	if (bHadPrevious)
	{
		Result->SetBoolField(TEXT("previousValue"), bPreviousValue);

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), Instance->GetPathName());
		Payload->SetStringField(TEXT("parameterName"), ParameterName);
		Payload->SetStringField(TEXT("association"), MaterialParameterAssociationToString(ParameterInfo.Association));
		Payload->SetNumberField(TEXT("parameterIndex"), ParameterInfo.Index);
		Payload->SetBoolField(TEXT("value"), bPreviousValue);
		MCPSetRollback(Result, TEXT("set_material_static_switch"), Payload);
	}

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
		// #444: accept {r,g,b,a} or {R,G,B,A} or {x,y,z,w} in value object,
		// or top-level r/g/b/a/x/y/z/w fields.
		auto ReadColor = [](const TSharedPtr<FJsonObject>& Obj, FLinearColor& Out) -> bool
		{
			double R = 0, G = 0, B = 0, A = 1; bool bAny = false;
			auto Pick = [&](const TCHAR* L, const TCHAR* U, const TCHAR* Alt, double& Slot)
			{
				double V; if (Obj->TryGetNumberField(L, V) || Obj->TryGetNumberField(U, V) || Obj->TryGetNumberField(Alt, V)) { Slot = V; bAny = true; }
			};
			Pick(TEXT("r"), TEXT("R"), TEXT("x"), R);
			Pick(TEXT("g"), TEXT("G"), TEXT("y"), G);
			Pick(TEXT("b"), TEXT("B"), TEXT("z"), B);
			Pick(TEXT("a"), TEXT("A"), TEXT("w"), A);
			Out = FLinearColor((float)R, (float)G, (float)B, (float)A);
			return bAny;
		};
		const TSharedPtr<FJsonObject>* ColorObj = nullptr;
		FLinearColor Color = Const3Expr->Constant;
		if (Params->TryGetObjectField(TEXT("value"), ColorObj) && *ColorObj && ReadColor(*ColorObj, Color))
		{
			Const3Expr->Constant = Color;
			bValueSet = true;
		}
		else if (ReadColor(Params, Color))
		{
			Const3Expr->Constant = Color;
			bValueSet = true;
		}
		if (bValueSet)
		{
			TSharedPtr<FJsonObject> ColorResult = MakeShared<FJsonObject>();
			ColorResult->SetNumberField(TEXT("r"), Const3Expr->Constant.R);
			ColorResult->SetNumberField(TEXT("g"), Const3Expr->Constant.G);
			ColorResult->SetNumberField(TEXT("b"), Const3Expr->Constant.B);
			ColorResult->SetNumberField(TEXT("a"), Const3Expr->Constant.A);
			Result->SetObjectField(TEXT("value"), ColorResult);
		}
	}
	// Handle UMaterialExpressionConstant4Vector
	else if (UMaterialExpressionConstant4Vector* Const4Expr = Cast<UMaterialExpressionConstant4Vector>(Expression))
	{
		auto ReadColor4 = [](const TSharedPtr<FJsonObject>& Obj, FLinearColor& Out) -> bool
		{
			double R = 0, G = 0, B = 0, A = 1; bool bAny = false;
			auto Pick = [&](const TCHAR* L, const TCHAR* U, const TCHAR* Alt, double& Slot)
			{
				double V; if (Obj->TryGetNumberField(L, V) || Obj->TryGetNumberField(U, V) || Obj->TryGetNumberField(Alt, V)) { Slot = V; bAny = true; }
			};
			Pick(TEXT("r"), TEXT("R"), TEXT("x"), R);
			Pick(TEXT("g"), TEXT("G"), TEXT("y"), G);
			Pick(TEXT("b"), TEXT("B"), TEXT("z"), B);
			Pick(TEXT("a"), TEXT("A"), TEXT("w"), A);
			Out = FLinearColor((float)R, (float)G, (float)B, (float)A);
			return bAny;
		};
		const TSharedPtr<FJsonObject>* ColorObj = nullptr;
		FLinearColor Color = Const4Expr->Constant;
		if (Params->TryGetObjectField(TEXT("value"), ColorObj) && *ColorObj && ReadColor4(*ColorObj, Color))
		{
			Const4Expr->Constant = Color;
			bValueSet = true;
		}
		else if (ReadColor4(Params, Color))
		{
			Const4Expr->Constant = Color;
			bValueSet = true;
		}
		if (bValueSet)
		{
			TSharedPtr<FJsonObject> ColorResult = MakeShared<FJsonObject>();
			ColorResult->SetNumberField(TEXT("r"), Const4Expr->Constant.R);
			ColorResult->SetNumberField(TEXT("g"), Const4Expr->Constant.G);
			ColorResult->SetNumberField(TEXT("b"), Const4Expr->Constant.B);
			ColorResult->SetNumberField(TEXT("a"), Const4Expr->Constant.A);
			Result->SetObjectField(TEXT("value"), ColorResult);
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

	// #307: fall back to the engine-assigned UObject name. The MaterialEditor
	// surfaces names like "MaterialExpressionConstant_0" and callers often
	// read those back via read_material_graph then pass them to delete; the
	// previous code only matched class names or descriptions so the lookup
	// failed and delete_expression cheerfully reported alreadyDeleted=true.
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (Expression && Expression->GetName() == ExpressionName)
		{
			return Expression;
		}
	}

	return nullptr;
}

// #225: parse a string usage flag into EMaterialUsage. Mirrors the
// MATUSAGE_* enum names but accepts shorter aliases too.
namespace
{
	static bool ParseMaterialUsage(const FString& In, EMaterialUsage& OutUsage)
	{
		const FString S = In.ToLower();
		auto Hit = [&](const TCHAR* Pat) { return S.Contains(Pat); };
		if (Hit(TEXT("instanced_static_meshes")) || Hit(TEXT("instancedstatic")) || Hit(TEXT("ism"))) { OutUsage = MATUSAGE_InstancedStaticMeshes; return true; }
		if (Hit(TEXT("skeletalmesh")) || Hit(TEXT("skeletal_mesh"))) { OutUsage = MATUSAGE_SkeletalMesh; return true; }
		if (Hit(TEXT("particle_sprites")) || Hit(TEXT("particlesprite"))) { OutUsage = MATUSAGE_ParticleSprites; return true; }
		if (Hit(TEXT("beam_trails")) || Hit(TEXT("beamtrails"))) { OutUsage = MATUSAGE_BeamTrails; return true; }
		if (Hit(TEXT("mesh_particles")) || Hit(TEXT("meshparticles"))) { OutUsage = MATUSAGE_MeshParticles; return true; }
		if (Hit(TEXT("static_lighting")) || Hit(TEXT("staticlighting"))) { OutUsage = MATUSAGE_StaticLighting; return true; }
		if (Hit(TEXT("morphtargets")) || Hit(TEXT("morph_targets"))) { OutUsage = MATUSAGE_MorphTargets; return true; }
		if (Hit(TEXT("splinemesh")) || Hit(TEXT("spline_mesh"))) { OutUsage = MATUSAGE_SplineMesh; return true; }
		if (Hit(TEXT("niagara_sprites")) || Hit(TEXT("niagarasprite"))) { OutUsage = MATUSAGE_NiagaraSprites; return true; }
		if (Hit(TEXT("niagara_ribbons")) || Hit(TEXT("niagararibbon"))) { OutUsage = MATUSAGE_NiagaraRibbons; return true; }
		if (Hit(TEXT("niagara_meshparticles")) || Hit(TEXT("niagaramesh"))) { OutUsage = MATUSAGE_NiagaraMeshParticles; return true; }
		if (Hit(TEXT("geometrycache")) || Hit(TEXT("geometry_cache"))) { OutUsage = MATUSAGE_GeometryCache; return true; }
		if (Hit(TEXT("nanite"))) { OutUsage = MATUSAGE_Nanite; return true; }
		if (Hit(TEXT("watersurface")) || Hit(TEXT("water_surface"))) { OutUsage = MATUSAGE_Water; return true; }
		if (Hit(TEXT("hairstrands")) || Hit(TEXT("hair_strands"))) { OutUsage = MATUSAGE_HairStrands; return true; }
		if (Hit(TEXT("lidarpointcloud")) || Hit(TEXT("lidar"))) { OutUsage = MATUSAGE_LidarPointCloud; return true; }
		if (Hit(TEXT("virtualheightfieldmesh")) || Hit(TEXT("vhfm"))) { OutUsage = MATUSAGE_VirtualHeightfieldMesh; return true; }
		if (Hit(TEXT("clothing"))) { OutUsage = MATUSAGE_Clothing; return true; }
		if (Hit(TEXT("geometrycollections")) || Hit(TEXT("geometry_collections"))) { OutUsage = MATUSAGE_GeometryCollections; return true; }
		return false;
	}
}

TSharedPtr<FJsonValue> FMaterialHandlers::SetMaterialUsage(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UMaterial* Material = LoadMaterialFromPath(AssetPath);
	if (!Material) return MCPError(FString::Printf(TEXT("Material not found: %s"), *AssetPath));

	TArray<FString> UsagesIn;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Params->TryGetArrayField(TEXT("usages"), Arr) && Arr)
	{
		for (const auto& V : *Arr)
		{
			FString S; if (V.IsValid() && V->TryGetString(S)) UsagesIn.Add(S);
		}
	}
	FString Single;
	if (Params->TryGetStringField(TEXT("usage"), Single)) UsagesIn.Add(Single);
	if (UsagesIn.Num() == 0) return MCPError(TEXT("Missing 'usage' or 'usages' array"));

	const bool bEnabled = OptionalBool(Params, TEXT("enabled"), true);

	TArray<FString> Applied, Unknown;
	for (const FString& U : UsagesIn)
	{
		EMaterialUsage Usage;
		if (!ParseMaterialUsage(U, Usage))
		{
			Unknown.Add(U);
			continue;
		}
		bool bNeedsRecompile = false;
		Material->SetMaterialUsage(bNeedsRecompile, Usage);
		Applied.Add(U);
	}

	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Material->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(Material, /*bOnlyIfIsDirty=*/false);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), Material->GetPathName());
	TArray<TSharedPtr<FJsonValue>> AppliedJ, UnknownJ;
	for (const FString& S : Applied)  AppliedJ.Add(MakeShared<FJsonValueString>(S));
	for (const FString& S : Unknown)  UnknownJ.Add(MakeShared<FJsonValueString>(S));
	Result->SetArrayField(TEXT("applied"), AppliedJ);
	if (Unknown.Num() > 0) Result->SetArrayField(TEXT("unknown"), UnknownJ);
	Result->SetBoolField(TEXT("enabled"), bEnabled);
	return MCPResult(Result);
}

// #225: single-call simple material authoring. Creates the asset, wires
// constant base color / metallic / specular / roughness / emissive, sets
// any requested usage flags, recompiles, and saves - replaces the
// 5+ round-trip create/add_expression/connect/recompile sequence that
// drove repeated 30s timeouts.
TSharedPtr<FJsonValue> FMaterialHandlers::CreateMaterialSimple(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Materials"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
	auto CreatedRes = MCPCreateAssetIdempotent<UMaterial>(Name, PackagePath, OnConflict, TEXT("Material"), Factory);
	if (CreatedRes.EarlyReturn) return CreatedRes.EarlyReturn;
	UMaterial* Material = CreatedRes.Asset;

	auto AddConstant3 = [Material](double R, double G, double B) -> UMaterialExpressionConstant3Vector*
	{
		UMaterialExpressionConstant3Vector* Expr = NewObject<UMaterialExpressionConstant3Vector>(Material);
		Expr->Constant = FLinearColor((float)R, (float)G, (float)B, 1.0f);
		Material->GetExpressionCollection().AddExpression(Expr);
		return Expr;
	};
	auto AddConstant = [Material](double V) -> UMaterialExpressionConstant*
	{
		UMaterialExpressionConstant* Expr = NewObject<UMaterialExpressionConstant>(Material);
		Expr->R = (float)V;
		Material->GetExpressionCollection().AddExpression(Expr);
		return Expr;
	};

	UMaterialEditorOnlyData* EOD = Material->GetEditorOnlyData();

	const TSharedPtr<FJsonObject>* ColorObj = nullptr;
	if (Params->TryGetObjectField(TEXT("baseColor"), ColorObj))
	{
		double R = 0.5, G = 0.5, B = 0.5;
		(*ColorObj)->TryGetNumberField(TEXT("r"), R);
		(*ColorObj)->TryGetNumberField(TEXT("g"), G);
		(*ColorObj)->TryGetNumberField(TEXT("b"), B);
		UMaterialExpressionConstant3Vector* C = AddConstant3(R, G, B);
		if (EOD) EOD->BaseColor.Connect(0, C);
	}
	double Roughness = -1, Metallic = -1, Specular = -1, Emissive = -1;
	if (Params->TryGetNumberField(TEXT("roughness"), Roughness))
	{
		UMaterialExpressionConstant* Expr = AddConstant(Roughness);
		if (EOD) EOD->Roughness.Connect(0, Expr);
	}
	if (Params->TryGetNumberField(TEXT("metallic"), Metallic))
	{
		UMaterialExpressionConstant* Expr = AddConstant(Metallic);
		if (EOD) EOD->Metallic.Connect(0, Expr);
	}
	if (Params->TryGetNumberField(TEXT("specular"), Specular))
	{
		UMaterialExpressionConstant* Expr = AddConstant(Specular);
		if (EOD) EOD->Specular.Connect(0, Expr);
	}
	if (Params->TryGetNumberField(TEXT("emissive"), Emissive))
	{
		UMaterialExpressionConstant3Vector* Expr = AddConstant3(Emissive, Emissive, Emissive);
		if (EOD) EOD->EmissiveColor.Connect(0, Expr);
	}

	// Usage flags
	const TArray<TSharedPtr<FJsonValue>>* UsagesArr = nullptr;
	if (Params->TryGetArrayField(TEXT("usages"), UsagesArr) && UsagesArr)
	{
		for (const auto& V : *UsagesArr)
		{
			FString S; if (V.IsValid() && V->TryGetString(S))
			{
				EMaterialUsage U;
				if (ParseMaterialUsage(S, U))
				{
					bool bNeeds = false;
					Material->SetMaterialUsage(bNeeds, U);
				}
			}
		}
	}

	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Material->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(Material, /*bOnlyIfIsDirty=*/false);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), Material->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), Material->GetPathName());
	MCPSetRollback(Result, TEXT("delete_asset"), Payload);
	return MCPResult(Result);
}
