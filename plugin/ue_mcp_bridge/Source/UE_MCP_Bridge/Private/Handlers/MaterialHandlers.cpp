#include "MaterialHandlers.h"
#include "UE_MCP_BridgeModule.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerPagination.h"
#include "HandlerAssetCreate.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
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
#include "Materials/MaterialExpressionCustom.h"
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
#include "MaterialEditingLibrary.h"
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
	Registry.RegisterHandler(TEXT("batch_set_material_instances"), &BatchSetInstances);
	Registry.RegisterHandler(TEXT("clear_material_instance_parameters"), &ClearMaterialInstanceParameters);
	Registry.RegisterHandler(TEXT("list_material_static_switches"), &ListMaterialStaticSwitches);
	Registry.RegisterHandler(TEXT("set_material_static_switch"), &SetMaterialStaticSwitch);
	Registry.RegisterHandler(TEXT("set_expression_value"), &SetExpressionValue);
	Registry.RegisterHandler(TEXT("set_custom_expression"), &SetCustomExpression);

	// Expression graph operations
	Registry.RegisterHandler(TEXT("connect_texture_to_material"), &ConnectTextureToMaterial);
	Registry.RegisterHandler(TEXT("connect_material_expressions"), &ConnectMaterialExpressions);
	Registry.RegisterHandler(TEXT("connect_to_material_property"), &ConnectToMaterialProperty);
	Registry.RegisterHandler(TEXT("delete_material_expression"), &DeleteMaterialExpression);
	Registry.RegisterHandler(TEXT("disconnect_material_property"), &DisconnectMaterialProperty);

	// v0.7.9 - depth
	Registry.RegisterHandler(TEXT("duplicate_material"), &DuplicateMaterial);
	Registry.RegisterHandler(TEXT("validate_material"), &ValidateMaterial);
	Registry.RegisterHandler(TEXT("get_material_shader_stats"), &GetMaterialShaderStats);
	Registry.RegisterHandler(TEXT("export_material_graph"), &ExportMaterialGraph);
	Registry.RegisterHandler(TEXT("import_material_graph"), &ImportMaterialGraph);
	Registry.RegisterHandler(TEXT("build_material_graph"), &BuildMaterialGraph);
	Registry.RegisterHandler(TEXT("render_material_preview"), &RenderMaterialPreview);
	Registry.RegisterHandler(TEXT("begin_material_transaction"), &BeginMaterialTransaction);
	Registry.RegisterHandler(TEXT("end_material_transaction"), &EndMaterialTransaction);

	// #946: texture-set build with automatic virtual/UDIM sampler selection.
	Registry.RegisterHandler(TEXT("build_material"), &BuildMaterial);

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

	// Runtime Virtual Textures, in MaterialHandlers_RVT.cpp.
	Registry.RegisterHandler(TEXT("create_runtime_virtual_texture"), &CreateRuntimeVirtualTexture);
	Registry.RegisterHandler(TEXT("read_runtime_virtual_texture"), &ReadRuntimeVirtualTexture);
	Registry.RegisterHandler(TEXT("add_rvt_volume"), &AddRvtVolume);
	Registry.RegisterHandler(TEXT("set_rvt_volume_bounds"), &SetRvtVolumeBounds);
	Registry.RegisterHandler(TEXT("add_rvt_sampler"), &AddRvtSampler);
	Registry.RegisterHandler(TEXT("add_rvt_output"), &AddRvtOutput);
	Registry.RegisterHandler(TEXT("assign_rvt_to_landscape"), &AssignRvtToLandscape);
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

	// A colour object, in any of the spellings a client might use: {r,g,b,a},
	// {R,G,B,A} or {x,y,z,w}. Missing components fall back to 0, with alpha 1.
	bool TryReadMaterialColorObject(const TSharedPtr<FJsonObject>& Obj, FLinearColor& OutColor)
	{
		if (!Obj.IsValid()) return false;
		double R = 0.0, G = 0.0, B = 0.0, A = 1.0;
		bool bAny = false;
		auto Pick = [&Obj, &bAny](const TCHAR* Lower, const TCHAR* Upper, const TCHAR* Alt, double& Slot)
		{
			double V = 0.0;
			if (Obj->TryGetNumberField(Lower, V) || Obj->TryGetNumberField(Upper, V) || Obj->TryGetNumberField(Alt, V))
			{
				Slot = V;
				bAny = true;
			}
		};
		Pick(TEXT("r"), TEXT("R"), TEXT("x"), R);
		Pick(TEXT("g"), TEXT("G"), TEXT("y"), G);
		Pick(TEXT("b"), TEXT("B"), TEXT("z"), B);
		Pick(TEXT("a"), TEXT("A"), TEXT("w"), A);
		if (!bAny) return false;
		OutColor = FLinearColor((float)R, (float)G, (float)B, (float)A);
		return true;
	}

	// #952: a colour arrives in whatever shape the calling client serialised it
	// in - an object {r,g,b,a}, an array [r,g,b,a], a re-encoded JSON string, or
	// UE struct text "(R=..,G=..,B=..,A=..)". Accept every one of them under a
	// single named field so no caller has to guess the wire format.
	bool TryParseMaterialColorField(
		const TSharedPtr<FJsonObject>& Params,
		const TCHAR* FieldName,
		FLinearColor& OutColor)
	{
		if (!Params.IsValid()) return false;

		const TSharedPtr<FJsonObject>* AsObject = nullptr;
		if (Params->TryGetObjectField(FieldName, AsObject) && AsObject)
		{
			return TryReadMaterialColorObject(*AsObject, OutColor);
		}

		const TArray<TSharedPtr<FJsonValue>>* AsArray = nullptr;
		if (Params->TryGetArrayField(FieldName, AsArray) && AsArray && AsArray->Num() > 0)
		{
			double Components[4] = { 0.0, 0.0, 0.0, 1.0 };
			for (int32 Index = 0; Index < AsArray->Num() && Index < 4; ++Index)
			{
				(*AsArray)[Index]->TryGetNumber(Components[Index]);
			}
			OutColor = FLinearColor((float)Components[0], (float)Components[1], (float)Components[2], (float)Components[3]);
			return true;
		}

		FString AsString;
		if (Params->TryGetStringField(FieldName, AsString) && !AsString.IsEmpty())
		{
			TSharedPtr<FJsonObject> Reparsed;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(AsString);
			if (FJsonSerializer::Deserialize(Reader, Reparsed) && TryReadMaterialColorObject(Reparsed, OutColor))
			{
				return true;
			}
			FLinearColor Parsed;
			if (Parsed.InitFromString(AsString))
			{
				OutColor = Parsed;
				return true;
			}
		}

		return false;
	}

	// #952: agents reach for `color` as readily as `value` when the parameter is
	// a colour, and rejecting the synonym is indistinguishable from the whole
	// action being unsupported. Try both, and report which one was honoured.
	bool TryParseMaterialColorParam(
		const TSharedPtr<FJsonObject>& Params,
		FLinearColor& OutColor,
		FString& OutSourceField,
		bool bAllowTopLevelComponents = false)
	{
		static const TCHAR* Candidates[] = { TEXT("value"), TEXT("color"), TEXT("colour") };
		for (const TCHAR* Candidate : Candidates)
		{
			if (TryParseMaterialColorField(Params, Candidate, OutColor))
			{
				OutSourceField = Candidate;
				return true;
			}
		}
		// Raw bridge callers reach the handler without the tool schema in front
		// of them and have historically passed the components at the top level.
		if (bAllowTopLevelComponents && TryReadMaterialColorObject(Params, OutColor))
		{
			OutSourceField = TEXT("(top-level components)");
			return true;
		}
		return false;
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

	// #952: `read` and `list_parameters` used to insist on a base UMaterial, so
	// the asset an agent actually edits - the instance - could be written to and
	// never read back, and verifying a write meant dropping to python. Report
	// what the instance resolves to now, what its parent would give it, and
	// which of the two the instance is actually overriding.
	void AddMaterialInterfaceParameters(TSharedPtr<FJsonObject> Result, UMaterialInterface* Material)
	{
		if (!Result.IsValid() || !Material)
		{
			return;
		}

		UMaterialInstance* Instance = Cast<UMaterialInstance>(Material);
		UMaterialInterface* Parent = nullptr;
		if (Instance)
		{
			Parent = Instance->Parent;
		}

		{
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Guids;
			Material->GetAllScalarParameterInfo(Infos, Guids);
			TArray<TSharedPtr<FJsonValue>> Entries;
			for (int32 Index = 0; Index < Infos.Num(); ++Index)
			{
				const FMaterialParameterInfo& Info = Infos[Index];
				TSharedPtr<FJsonObject> Obj = MaterialParameterInfoToJson(Info);

				float Value = 0.0f;
				if (Material->GetScalarParameterValue(Info, Value))
				{
					Obj->SetNumberField(TEXT("value"), Value);
				}
				float Default = 0.0f;
				if (Parent && Parent->GetScalarParameterValue(Info, Default))
				{
					Obj->SetNumberField(TEXT("defaultValue"), Default);
				}

				bool bOverridden = false;
				if (Instance)
				{
					for (const FScalarParameterValue& Override : Instance->ScalarParameterValues)
					{
						if (Override.ParameterInfo == Info) { bOverridden = true; break; }
					}
				}
				Obj->SetBoolField(TEXT("overridden"), bOverridden);
				if (Guids.IsValidIndex(Index))
				{
					Obj->SetStringField(TEXT("expressionGuid"), Guids[Index].ToString(EGuidFormats::DigitsWithHyphens));
				}
				Entries.Add(MakeShared<FJsonValueObject>(Obj));
			}
			Result->SetArrayField(TEXT("scalarParameters"), Entries);
		}

		{
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Guids;
			Material->GetAllVectorParameterInfo(Infos, Guids);
			TArray<TSharedPtr<FJsonValue>> Entries;
			for (int32 Index = 0; Index < Infos.Num(); ++Index)
			{
				const FMaterialParameterInfo& Info = Infos[Index];
				TSharedPtr<FJsonObject> Obj = MaterialParameterInfoToJson(Info);

				FLinearColor Value;
				if (Material->GetVectorParameterValue(Info, Value))
				{
					Obj->SetObjectField(TEXT("value"), LinearColorToJson(Value));
				}
				FLinearColor Default;
				if (Parent && Parent->GetVectorParameterValue(Info, Default))
				{
					Obj->SetObjectField(TEXT("defaultValue"), LinearColorToJson(Default));
				}

				bool bOverridden = false;
				if (Instance)
				{
					for (const FVectorParameterValue& Override : Instance->VectorParameterValues)
					{
						if (Override.ParameterInfo == Info) { bOverridden = true; break; }
					}
				}
				Obj->SetBoolField(TEXT("overridden"), bOverridden);
				if (Guids.IsValidIndex(Index))
				{
					Obj->SetStringField(TEXT("expressionGuid"), Guids[Index].ToString(EGuidFormats::DigitsWithHyphens));
				}
				Entries.Add(MakeShared<FJsonValueObject>(Obj));
			}
			Result->SetArrayField(TEXT("vectorParameters"), Entries);
		}

		{
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Guids;
			Material->GetAllTextureParameterInfo(Infos, Guids);
			TArray<TSharedPtr<FJsonValue>> Entries;
			for (int32 Index = 0; Index < Infos.Num(); ++Index)
			{
				const FMaterialParameterInfo& Info = Infos[Index];
				TSharedPtr<FJsonObject> Obj = MaterialParameterInfoToJson(Info);

				UTexture* Value = nullptr;
				if (Material->GetTextureParameterValue(Info, Value) && Value)
				{
					Obj->SetStringField(TEXT("value"), Value->GetPathName());
				}
				UTexture* Default = nullptr;
				if (Parent && Parent->GetTextureParameterValue(Info, Default) && Default)
				{
					Obj->SetStringField(TEXT("defaultValue"), Default->GetPathName());
				}

				bool bOverridden = false;
				if (Instance)
				{
					for (const FTextureParameterValue& Override : Instance->TextureParameterValues)
					{
						if (Override.ParameterInfo == Info) { bOverridden = true; break; }
					}
				}
				Obj->SetBoolField(TEXT("overridden"), bOverridden);
				if (Guids.IsValidIndex(Index))
				{
					Obj->SetStringField(TEXT("expressionGuid"), Guids[Index].ToString(EGuidFormats::DigitsWithHyphens));
				}
				Entries.Add(MakeShared<FJsonValueObject>(Obj));
			}
			Result->SetArrayField(TEXT("textureParameters"), Entries);
		}
	}

	// Identity + shading summary shared by `read` and `list_parameters` when the
	// asset is not a base UMaterial.
	void SetMaterialInterfaceIdentityFields(TSharedPtr<FJsonObject> Result, UMaterialInterface* Material)
	{
		if (!Result.IsValid() || !Material)
		{
			return;
		}

		Result->SetStringField(TEXT("name"), Material->GetName());
		Result->SetStringField(TEXT("path"), Material->GetPathName());
		Result->SetStringField(TEXT("assetType"), Material->GetClass()->GetName());
		if (UMaterialInstance* Instance = Cast<UMaterialInstance>(Material))
		{
			Result->SetStringField(TEXT("parentPath"), Instance->Parent ? Instance->Parent->GetPathName() : FString());
		}
		if (UMaterial* Base = Material->GetMaterial())
		{
			Result->SetStringField(TEXT("baseMaterialPath"), Base->GetPathName());
		}
		if (UMaterialInstanceConstant* Constant = Cast<UMaterialInstanceConstant>(Material))
		{
			Result->SetObjectField(TEXT("overrideCounts"), MaterialInstanceOverrideCounts(Constant));
			Result->SetNumberField(TEXT("overrideCount"), CountTotalMaterialInstanceOverrides(Constant));
		}
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
	// T3: paged.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params, TEXT("list_expression_types"), /*DefaultLimit*/ 100, /*MaxLimit*/ 1000, Page))
	{
		return Err;
	}

	auto Result = MCPSuccess();

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

	// Authored order, so this list is deliberately NOT sorted: it is a curated
	// starting point grouped by what the nodes do, and alphabetising it would
	// bury the constants under Abs.
	TArray<MCPPagination::FPageRow> Rows;
	Rows.Reserve(ExpressionTypes.Num());
	for (const FString& TypeName : ExpressionTypes)
	{
		// The class name is the page anchor: it is what add_material_node
		// accepts, and it is unique in this list.
		Rows.Add({ TypeName, MakeShared<FJsonValueString>(TypeName) });
	}

	MCPPagination::EmitPage(Page, Rows, TEXT("expressionTypes"), Result);

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
		// #952: a MaterialInstance is a material as far as a caller is concerned.
		// It has no expression graph of its own, so answer with what it does
		// have: its lineage, its shading setup, and every parameter it resolves,
		// with the ones it overrides marked.
		UMaterialInterface* Interface = LoadAssetByPath<UMaterialInterface>(AssetPath);
		if (!Interface)
		{
			return MCPError(FString::Printf(TEXT("Failed to load material or material instance at '%s'"), *AssetPath));
		}

		auto InstanceResult = MCPSuccess();
		SetMaterialInterfaceIdentityFields(InstanceResult, Interface);
		InstanceResult->SetStringField(TEXT("shadingModel"), ShadingModelToString(Interface->GetShadingModels().GetFirstShadingModel()));
		InstanceResult->SetStringField(TEXT("blendMode"), StaticEnum<EBlendMode>()->GetNameStringByValue((int64)Interface->GetBlendMode()));
		InstanceResult->SetBoolField(TEXT("twoSided"), Interface->IsTwoSided());
		AddMaterialInterfaceParameters(InstanceResult, Interface);
		InstanceResult->SetArrayField(TEXT("staticSwitches"), MaterialStaticSwitchesToJson(Interface));
		// The graph lives on the base material; say so rather than returning an
		// empty expression list that reads like a material with no nodes.
		InstanceResult->SetStringField(TEXT("expressionsNote"),
			TEXT("A MaterialInstance has no expression graph. Read baseMaterialPath for the graph."));
		return MCPResult(InstanceResult);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("name"), Material->GetName());
	Result->SetStringField(TEXT("path"), Material->GetPathName());
	Result->SetStringField(TEXT("assetType"), Material->GetClass()->GetName());
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

	// This adds a new Constant3Vector expression each call (not natural-key
	// idempotent). Caller should use set_material_parameter with a named
	// scalar/vector parameter for true idempotency.
	Material->PreEditChange(nullptr);

	// Create a Constant3Vector expression for the base color
	UMaterialExpressionConstant3Vector* ColorExpression = NewObject<UMaterialExpressionConstant3Vector>(Material);
	ColorExpression->Constant = FLinearColor(R, G, B, A);

	// Add expression to material
	Material->GetExpressionCollection().AddExpression(ColorExpression);

	// Connect to base color input (guarded: GetEditorOnlyData can return null
	// on unsupported material domains, which would otherwise null-deref here).
	// Whatever BaseColor carried is overwritten, so read it out first.
	UMaterialExpression* PreviousBaseColor = nullptr;
	int32 PreviousBaseColorOutputIndex = 0;
	if (UMaterialEditorOnlyData* EOD = Material->GetEditorOnlyData())
	{
		PreviousBaseColor = EOD->BaseColor.Expression;
		PreviousBaseColorOutputIndex = EOD->BaseColor.OutputIndex;
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
	Result->SetStringField(TEXT("expressionName"), ColorExpression->GetName());

	// Rollback: delete the Constant3Vector this call created. That handler also
	// clears every input referencing it, so BaseColor goes back to unconnected -
	// the previous state only when BaseColor was unconnected to begin with.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Payload->SetStringField(TEXT("expressionName"), ColorExpression->GetName());
	MCPSetRollback(Result, TEXT("delete_material_expression"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), PreviousBaseColor != nullptr);
	if (PreviousBaseColor)
	{
		Result->SetStringField(TEXT("previousBaseColorExpression"), PreviousBaseColor->GetName());
		Result->SetNumberField(TEXT("previousBaseColorOutputIndex"), PreviousBaseColorOutputIndex);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("Deleting the constant removes what this call added and leaves BaseColor unconnected. It does NOT restore the connection this call overwrote: '%s' output %d was wired into BaseColor before. Rewire it with connect_to_material_property expressionName='%s' property='BaseColor' outputName='%d'."),
			*PreviousBaseColor->GetName(), PreviousBaseColorOutputIndex,
			*PreviousBaseColor->GetName(), PreviousBaseColorOutputIndex));
	}

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

	// Rollback: remove the expression this call added. delete_material_expression
	// REQUIRES expressionName - a payload carrying only nodeId was rejected with
	// "Missing required parameter 'expressionName'", so every rollback this
	// handler emitted failed on replay. The engine-assigned name is what
	// FindExpressionByName resolves and is unique inside the material, and it
	// is the ONLY key the payload carries: delete_material_expression reads no
	// nodeId, and a key the target ignores reads like an address that works.
	// nodeId stays on the RESULT, where callers do read it back.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Payload->SetStringField(TEXT("expressionName"), NewExpression->GetName());
	MCPSetRollback(Result, TEXT("delete_material_expression"), Payload);
	Result->SetStringField(TEXT("expressionName"), NewExpression->GetName());
	Result->SetBoolField(TEXT("rollbackLossy"), false);

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

	// T3: paged. A production master material carries several hundred nodes.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_material_expressions|materialPath=%s"), *MaterialPath),
			/*DefaultLimit*/ 200, /*MaxLimit*/ 2000, Page))
	{
		return Err;
	}

	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
	}

	TArray<MCPPagination::FPageRow> Rows;
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

		// The expression's OBJECT PATH is the page anchor, not its nodeId:
		// nodeId is the index into GetExpressions(), which every insertion and
		// deletion renumbers, and an index is exactly what a cursor must not
		// resume on. nodeId is still reported, and is still the addressing
		// scheme the other material actions take, because it is computed here
		// over the whole enumeration rather than over the page.
		Rows.Add({ Expression->GetPathName(), MakeShared<FJsonValueObject>(ExprObj) });
	}
	// GetExpressions() is the material's own stored order, which nodeId indexes
	// into, so the rows are deliberately NOT sorted: reordering them would
	// leave the reported nodeIds out of step with the sequence they name.

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	MCPPagination::EmitPage(Page, Rows, TEXT("expressions"), Result);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::ListMaterialParameters(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UMaterial* Material = LoadMaterialFromPath(AssetPath);
	if (!Material)
	{
		// #952: on a MaterialInstance report the resolved value of every
		// parameter, the parent's value, and which ones this instance overrides,
		// so a write can be verified without an execute_python round trip.
		UMaterialInterface* Interface = LoadAssetByPath<UMaterialInterface>(AssetPath);
		if (!Interface)
		{
			return MCPError(FString::Printf(TEXT("Failed to load material or material instance at '%s'"), *AssetPath));
		}

		auto InstanceResult = MCPSuccess();
		SetMaterialInterfaceIdentityFields(InstanceResult, Interface);
		AddMaterialInterfaceParameters(InstanceResult, Interface);
		InstanceResult->SetArrayField(TEXT("staticSwitches"), MaterialStaticSwitchesToJson(Interface));
		return MCPResult(InstanceResult);
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
	Result->SetStringField(TEXT("assetType"), Material->GetClass()->GetName());

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

	// No inverse. A recompile rebuilds shader maps from the graph as it stands;
	// there is no action that rebuilds them from the graph as it stood, and the
	// shader maps this replaced are gone. Replay is safe rather than reversible:
	// running it again on an unchanged graph lands on the same shaders.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Compiling shaders has no inverse. Recompiling again does not restore the previous shader maps, it produces the current graph's shaders a second time. Nothing in the material's authored state changed, so there is nothing to undo."));
	Result->SetStringField(TEXT("idempotencyNote"),
		TEXT("Recompiling an unchanged graph reaches the same end state every time, so a replayed or retried call cannot double-apply. It always does the work rather than reporting a no-op, because whether the existing shader maps are current is not something this handler can read back."));

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

	// The association decides which parameter a name refers to once material
	// layers are in play, and it has to be identical on the write and on the
	// read-back or the verify step reads a different parameter than it wrote.
	const EMaterialParameterAssociation Association = ParseMaterialParameterAssociation(
		OptionalString(Params, TEXT("association"), TEXT("Global")));
	const FMaterialParameterInfo ParameterInfo(FName(*ParameterName), Association);
	const FString AssociationName = MaterialParameterAssociationToString(Association);

	// Auto-detect parameter type if not provided
	if (ParameterType.IsEmpty())
	{
		// Check which parameter collections contain this name
		float ScalarVal;
		FLinearColor VectorVal;
		UTexture* TextureVal;
		if (MaterialInstance->GetScalarParameterValue(ParameterInfo, ScalarVal))
			ParameterType = TEXT("scalar");
		else if (MaterialInstance->GetVectorParameterValue(ParameterInfo, VectorVal))
			ParameterType = TEXT("vector");
		else if (MaterialInstance->GetTextureParameterValue(ParameterInfo, TextureVal))
			ParameterType = TEXT("texture");
		else
		{
			// The parent does not declare the name, so nothing can be looked
			// up. Fall back on the shape of the payload: a colour is only ever
			// meant for a vector parameter, and guessing scalar there produced
			// the "missing value" rejection reported in #952.
			FLinearColor Probe;
			FString ProbeField;
			ParameterType = TryParseMaterialColorParam(Params, Probe, ProbeField)
				? TEXT("vector")
				: TEXT("scalar");
		}
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
		const bool bHadPrev = MaterialInstance->GetScalarParameterValue(ParameterInfo, PrevScalar);

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("parameterName"), ParameterName);
		Result->SetStringField(TEXT("parameterType"), TEXT("scalar"));
		Result->SetStringField(TEXT("association"), AssociationName);
		Result->SetNumberField(TEXT("value"), ScalarValue);
		Result->SetStringField(TEXT("path"), MaterialInstance->GetPathName());

		if (bHadPrev && FMath::IsNearlyEqual(PrevScalar, (float)ScalarValue))
		{
			MCPSetExisted(Result);
			Result->SetBoolField(TEXT("updated"), false);
			Result->SetNumberField(TEXT("readBack"), PrevScalar);
			return MCPResult(Result);
		}

		MaterialInstance->Modify(true);
		const bool bApplied = UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(
			MaterialInstance, FName(*ParameterName), static_cast<float>(ScalarValue), Association);
		if (!bApplied)
		{
			// The library declines a name the parent does not declare. Writing
			// the override anyway is what this handler has always done, and a
			// parameter added to the parent later then picks it up.
			MaterialInstance->SetScalarParameterValueEditorOnly(ParameterInfo, static_cast<float>(ScalarValue));
			UMaterialEditingLibrary::UpdateMaterialInstance(MaterialInstance);
		}
		Result->SetBoolField(TEXT("declaredByParent"), bApplied);

		// Persist, then read the value straight back off the instance so the
		// caller never has to take "success" on trust (#952).
		Result->SetBoolField(TEXT("saved"), SaveAssetPackage(MaterialInstance));
		float ReadBack = 0.0f;
		if (MaterialInstance->GetScalarParameterValue(ParameterInfo, ReadBack))
		{
			Result->SetNumberField(TEXT("readBack"), ReadBack);
		}

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
	else if (TypeLower == TEXT("vector") || TypeLower == TEXT("color") || TypeLower == TEXT("colour"))
	{
		// #670/#952: the payload may be an object {r,g,b,a}, an array [r,g,b,a],
		// a re-encoded JSON string or UE struct text, and it may arrive under
		// `value` or under `color`. Every one of those is a colour a caller
		// meant to set, so accept them all rather than making the caller guess.
		FLinearColor ColorValue = FLinearColor::White;
		FString SourceField;
		if (!TryParseMaterialColorParam(Params, ColorValue, SourceField))
		{
			return MCPError(TEXT("Missing/unparseable colour for vector parameter - pass it as 'value' or 'color', shaped {r,g,b,a}, [r,g,b,a], or '(R=..,G=..,B=..,A=..)'"));
		}

		FLinearColor PrevColor;
		const bool bHadPrev = MaterialInstance->GetVectorParameterValue(ParameterInfo, PrevColor);

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("parameterName"), ParameterName);
		Result->SetStringField(TEXT("parameterType"), TEXT("vector"));
		Result->SetStringField(TEXT("association"), AssociationName);
		Result->SetStringField(TEXT("valueField"), SourceField);
		Result->SetObjectField(TEXT("value"), LinearColorToJson(ColorValue));
		Result->SetStringField(TEXT("path"), MaterialInstance->GetPathName());

		if (bHadPrev && PrevColor.Equals(ColorValue))
		{
			MCPSetExisted(Result);
			Result->SetBoolField(TEXT("updated"), false);
			Result->SetObjectField(TEXT("readBack"), LinearColorToJson(PrevColor));
			return MCPResult(Result);
		}

		MaterialInstance->Modify(true);
		const bool bApplied = UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(
			MaterialInstance, FName(*ParameterName), ColorValue, Association);
		if (!bApplied)
		{
			MaterialInstance->SetVectorParameterValueEditorOnly(ParameterInfo, ColorValue);
			UMaterialEditingLibrary::UpdateMaterialInstance(MaterialInstance);
		}
		Result->SetBoolField(TEXT("declaredByParent"), bApplied);

		Result->SetBoolField(TEXT("saved"), SaveAssetPackage(MaterialInstance));
		FLinearColor ReadBack;
		if (MaterialInstance->GetVectorParameterValue(ParameterInfo, ReadBack))
		{
			Result->SetObjectField(TEXT("readBack"), LinearColorToJson(ReadBack));
		}

		MCPSetUpdated(Result);
		if (bHadPrev)
		{
			TSharedPtr<FJsonObject> PrevValueObj = LinearColorToJson(PrevColor);
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
			Params->TryGetStringField(TEXT("texturePath"), TexturePath);
		}
		if (TexturePath.IsEmpty())
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
		const bool bHadPrev = MaterialInstance->GetTextureParameterValue(ParameterInfo, PrevTexture);

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("parameterName"), ParameterName);
		Result->SetStringField(TEXT("parameterType"), TEXT("texture"));
		Result->SetStringField(TEXT("association"), AssociationName);
		Result->SetStringField(TEXT("value"), Texture->GetPathName());
		Result->SetStringField(TEXT("path"), MaterialInstance->GetPathName());

		if (bHadPrev && PrevTexture == Texture)
		{
			MCPSetExisted(Result);
			Result->SetBoolField(TEXT("updated"), false);
			Result->SetStringField(TEXT("readBack"), PrevTexture->GetPathName());
			return MCPResult(Result);
		}

		MaterialInstance->Modify(true);
		const bool bApplied = UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(
			MaterialInstance, FName(*ParameterName), Texture, Association);
		if (!bApplied)
		{
			MaterialInstance->SetTextureParameterValueEditorOnly(ParameterInfo, Texture);
			UMaterialEditingLibrary::UpdateMaterialInstance(MaterialInstance);
		}
		Result->SetBoolField(TEXT("declaredByParent"), bApplied);

		Result->SetBoolField(TEXT("saved"), SaveAssetPackage(MaterialInstance));
		UTexture* ReadBack = nullptr;
		if (MaterialInstance->GetTextureParameterValue(ParameterInfo, ReadBack) && ReadBack)
		{
			Result->SetStringField(TEXT("readBack"), ReadBack->GetPathName());
		}

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
		return MCPError(FString::Printf(TEXT("Unknown parameterType '%s'. Use 'scalar', 'vector' (alias 'color'), or 'texture'."), *ParameterType));
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

// #594 batch reparent + reassign parameters across many Material Instances in
// one call. Each entry: {assetPath, parentPath?, parameters?:[{name,type,value}]}
// where type is scalar | vector | texture. Loops the proven per-instance ops.
TSharedPtr<FJsonValue> FMaterialHandlers::BatchSetInstances(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* Instances = nullptr;
	if (!Params->TryGetArrayField(TEXT("instances"), Instances) || !Instances)
	{
		return MCPError(TEXT("Missing 'instances' array of {assetPath, parentPath?, parameters?}"));
	}

	TArray<TSharedPtr<FJsonValue>> Results;
	// The inverse call, built as the batch runs: one entry per instance carrying
	// the parent it had and the value each written parameter held. Anything the
	// instance did not override before is unrestorable (this action has no way
	// to say "remove the override"), which is what bLossy tracks.
	TArray<TSharedPtr<FJsonValue>> InverseInstances;
	bool bLossy = false;
	// A parent written onto an instance that had none is a real change with no
	// expressible inverse, and it reaches the same "nothing to write back"
	// branch as a batch that changed nothing. The two must not share a note.
	bool bParentSetFromNull = false;
	int32 Updated = 0, Failed = 0, Changed = 0;
	for (const TSharedPtr<FJsonValue>& Entry : *Instances)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Entry.IsValid() || !Entry->TryGetObject(Obj) || !Obj) continue;

		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		FString AssetPath = (*Obj)->GetStringField(TEXT("assetPath"));
		Row->SetStringField(TEXT("assetPath"), AssetPath);

		UMaterialInstanceConstant* MIC = LoadMaterialInstanceFromPath(AssetPath);
		if (!MIC)
		{
			Row->SetBoolField(TEXT("ok"), false);
			Row->SetStringField(TEXT("error"), TEXT("not a MaterialInstanceConstant or not found"));
			Results.Add(MakeShared<FJsonValueObject>(Row)); ++Failed; continue;
		}

		TSharedPtr<FJsonObject> InverseEntry = MakeShared<FJsonObject>();
		InverseEntry->SetStringField(TEXT("assetPath"), MIC->GetPathName());
		TArray<TSharedPtr<FJsonValue>> InverseParameters;
		bool bRowChanged = false;

		// An override is explicit only when it sits in the instance's own value
		// array; GetScalarParameterValue answers with the parent's value too,
		// and writing that back would turn an inherited value into an override.
		auto HasExplicitOverride = [MIC](const FName& Name, const FString& Type) -> bool
		{
			if (Type == TEXT("scalar"))
			{
				for (const FScalarParameterValue& P : MIC->ScalarParameterValues) { if (P.ParameterInfo.Name == Name) return true; }
			}
			else if (Type == TEXT("texture"))
			{
				for (const FTextureParameterValue& P : MIC->TextureParameterValues) { if (P.ParameterInfo.Name == Name) return true; }
			}
			else
			{
				for (const FVectorParameterValue& P : MIC->VectorParameterValues) { if (P.ParameterInfo.Name == Name) return true; }
			}
			return false;
		};

		MIC->Modify(true);
		FString ParentPath;
		if ((*Obj)->TryGetStringField(TEXT("parentPath"), ParentPath) && !ParentPath.IsEmpty())
		{
			if (UMaterialInterface* NewParent = LoadAssetByPath<UMaterialInterface>(ParentPath))
			{
				UMaterialInterface* PreviousParent = MIC->Parent;
				if (NewParent != MIC && NewParent != PreviousParent)
				{
					MIC->SetParentEditorOnly(NewParent, true);
					Row->SetStringField(TEXT("parent"), NewParent->GetPathName());
					bRowChanged = true;
					if (PreviousParent)
					{
						InverseEntry->SetStringField(TEXT("parentPath"), PreviousParent->GetPathName());
					}
					else
					{
						// Nothing to reparent back to, and this action cannot
						// clear a parent.
						bLossy = true;
						bParentSetFromNull = true;
						Row->SetBoolField(TEXT("previousParentWasNull"), true);
					}
				}
				else
				{
					Row->SetBoolField(TEXT("parentUnchanged"), true);
				}
			}
			else { Row->SetStringField(TEXT("parentError"), FString::Printf(TEXT("parent not found: %s"), *ParentPath)); }
		}

		int32 ParamsSet = 0;
		const TArray<TSharedPtr<FJsonValue>>* ParamArr = nullptr;
		if ((*Obj)->TryGetArrayField(TEXT("parameters"), ParamArr) && ParamArr)
		{
			for (const TSharedPtr<FJsonValue>& PV : *ParamArr)
			{
				const TSharedPtr<FJsonObject>* PObj = nullptr;
				if (!PV.IsValid() || !PV->TryGetObject(PObj) || !PObj) continue;
				const FString PName = (*PObj)->GetStringField(TEXT("name"));
				const FString PType = (*PObj)->GetStringField(TEXT("type")).ToLower();
				if (PName.IsEmpty()) continue;
				const FMaterialParameterInfo PInfo{ FName(*PName) };
				if (PType == TEXT("scalar"))
				{
					double V = 0; (*PObj)->TryGetNumberField(TEXT("value"), V);
					float PrevScalar = 0.0f;
					const bool bHadValue = MIC->GetScalarParameterValue(PInfo, PrevScalar);
					if (bHadValue && HasExplicitOverride(FName(*PName), TEXT("scalar")))
					{
						TSharedPtr<FJsonObject> Inv = MakeShared<FJsonObject>();
						Inv->SetStringField(TEXT("name"), PName);
						Inv->SetStringField(TEXT("type"), TEXT("scalar"));
						Inv->SetNumberField(TEXT("value"), PrevScalar);
						InverseParameters.Add(MakeShared<FJsonValueObject>(Inv));
					}
					else { bLossy = true; }
					if (!bHadValue || !FMath::IsNearlyEqual(PrevScalar, (float)V)) bRowChanged = true;
					MIC->SetScalarParameterValueEditorOnly(FName(*PName), (float)V); ++ParamsSet;
				}
				else if (PType == TEXT("vector") || PType == TEXT("color") || PType == TEXT("colour"))
				{
					// Same colour shapes the single-instance path accepts (#952),
					// so a batch is never fussier about the payload than one call.
					FLinearColor Color;
					FString SourceField;
					if (TryParseMaterialColorParam(*PObj, Color, SourceField))
					{
						FLinearColor PrevColor;
						const bool bHadValue = MIC->GetVectorParameterValue(PInfo, PrevColor);
						if (bHadValue && HasExplicitOverride(FName(*PName), TEXT("vector")))
						{
							TSharedPtr<FJsonObject> Inv = MakeShared<FJsonObject>();
							Inv->SetStringField(TEXT("name"), PName);
							Inv->SetStringField(TEXT("type"), TEXT("vector"));
							Inv->SetObjectField(TEXT("value"), LinearColorToJson(PrevColor));
							InverseParameters.Add(MakeShared<FJsonValueObject>(Inv));
						}
						else { bLossy = true; }
						if (!bHadValue || !PrevColor.Equals(Color)) bRowChanged = true;
						MIC->SetVectorParameterValueEditorOnly(FName(*PName), Color); ++ParamsSet;
					}
				}
				else if (PType == TEXT("texture"))
				{
					FString TexPath; (*PObj)->TryGetStringField(TEXT("value"), TexPath);
					if (UTexture* Tex = LoadAssetByPath<UTexture>(TexPath))
					{
						UTexture* PrevTexture = nullptr;
						const bool bHadValue = MIC->GetTextureParameterValue(PInfo, PrevTexture);
						if (bHadValue && PrevTexture && HasExplicitOverride(FName(*PName), TEXT("texture")))
						{
							TSharedPtr<FJsonObject> Inv = MakeShared<FJsonObject>();
							Inv->SetStringField(TEXT("name"), PName);
							Inv->SetStringField(TEXT("type"), TEXT("texture"));
							Inv->SetStringField(TEXT("value"), PrevTexture->GetPathName());
							InverseParameters.Add(MakeShared<FJsonValueObject>(Inv));
						}
						else { bLossy = true; }
						if (PrevTexture != Tex) bRowChanged = true;
						MIC->SetTextureParameterValueEditorOnly(FName(*PName), Tex); ++ParamsSet;
					}
				}
			}
		}

		MIC->PostEditChange();
		SaveAssetPackage(MIC);
		Row->SetBoolField(TEXT("ok"), true);
		Row->SetNumberField(TEXT("parametersSet"), ParamsSet);
		Row->SetBoolField(TEXT("changed"), bRowChanged);
		if (bRowChanged) ++Changed;
		if (InverseParameters.Num() > 0)
		{
			InverseEntry->SetArrayField(TEXT("parameters"), InverseParameters);
		}
		if (InverseParameters.Num() > 0 || InverseEntry->HasField(TEXT("parentPath")))
		{
			InverseInstances.Add(MakeShared<FJsonValueObject>(InverseEntry));
		}
		Results.Add(MakeShared<FJsonValueObject>(Row)); ++Updated;
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("results"), Results);
	Result->SetNumberField(TEXT("updated"), Updated);
	Result->SetNumberField(TEXT("failed"), Failed);
	Result->SetNumberField(TEXT("changed"), Changed);
	Result->SetNumberField(TEXT("total"), Instances->Num());
	if (Changed == 0) Result->SetBoolField(TEXT("unchanged"), true);

	if (InverseInstances.Num() > 0)
	{
		// Rollback: the same batch call, aimed back at the values that were read
		// out of each instance immediately before it was written.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("instances"), InverseInstances);
		MCPSetRollback(Result, TEXT("batch_set_material_instances"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), bLossy);
		if (bLossy)
		{
			FString Note = TEXT("The rollback restores every parameter that each instance already overrode explicitly, and every parent that was not null. Parameters an instance did NOT override before are left as overrides at the value this call wrote, because batch_set_material_instances can set an override but cannot remove one - use clear_instance_parameters if an instance has to go back to inheriting everything. Overrides on a Layer or Blend association are restored as Global.");
			if (bParentSetFromNull)
			{
				Note += TEXT(" At least one instance was given a parent where it had none, and that does not come back: this action requires a parentPath to set and cannot clear one. Rows carrying previousParentWasNull name them; clear a parent with editor(set_property) on the instance.");
			}
			Result->SetStringField(TEXT("rollbackNote"), Note);
		}
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), bParentSetFromNull
			? TEXT("Nothing in this batch can be written back. Every parameter written was new to its instance, and at least one instance was given a parent where it had none - batch_set_material_instances requires a parentPath to set and cannot clear one, so that change has no inverse either. Rows carrying previousParentWasNull name the instances affected. Clear a parent with editor(set_property) on the instance if this has to be undone.")
			: TEXT("Nothing in this batch had a previous value that could be written back: every parameter written was new to its instance and every parent was already the one asked for. batch_set_material_instances can add an override but cannot remove one, so there is no call that undoes this."));
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

	// Snapshot the overrides batch_set_material_instances can write back before
	// they are dropped. Everything else it cannot express is counted so the
	// note can name what will not come back rather than implying a clean undo.
	TArray<TSharedPtr<FJsonValue>> RestorableParameters;
	int32 NonGlobalAssociationCount = 0;
	for (const FScalarParameterValue& Param : Instance->ScalarParameterValues)
	{
		if (Param.ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter) ++NonGlobalAssociationCount;
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Param.ParameterInfo.Name.ToString());
		Entry->SetStringField(TEXT("type"), TEXT("scalar"));
		Entry->SetNumberField(TEXT("value"), Param.ParameterValue);
		RestorableParameters.Add(MakeShared<FJsonValueObject>(Entry));
	}
	for (const FVectorParameterValue& Param : Instance->VectorParameterValues)
	{
		if (Param.ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter) ++NonGlobalAssociationCount;
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Param.ParameterInfo.Name.ToString());
		Entry->SetStringField(TEXT("type"), TEXT("vector"));
		Entry->SetObjectField(TEXT("value"), LinearColorToJson(Param.ParameterValue));
		RestorableParameters.Add(MakeShared<FJsonValueObject>(Entry));
	}
	for (const FTextureParameterValue& Param : Instance->TextureParameterValues)
	{
		if (!Param.ParameterValue) continue;
		if (Param.ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter) ++NonGlobalAssociationCount;
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Param.ParameterInfo.Name.ToString());
		Entry->SetStringField(TEXT("type"), TEXT("texture"));
		Entry->SetStringField(TEXT("value"), Param.ParameterValue->GetPathName());
		RestorableParameters.Add(MakeShared<FJsonValueObject>(Entry));
	}
	const int32 UnrestorableCount = BeforeCount - RestorableParameters.Num();
	const FString InstancePath = Instance->GetPathName();

	Instance->Modify(true);
	Instance->ClearParameterValuesEditorOnly();
	Instance->PostEditChange();
	SaveAssetPackage(Instance);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	SetMaterialInstanceSummaryFields(Result, Instance);
	Result->SetNumberField(TEXT("clearedOverrideCount"), BeforeCount);
	Result->SetArrayField(TEXT("clearedRestorableParameters"), RestorableParameters);
	Result->SetNumberField(TEXT("clearedUnrestorableCount"), UnrestorableCount);

	// Rollback: batch_set_material_instances is the only action that writes many
	// overrides onto one instance in a single call, and its {name, type, value}
	// shape is exactly what was snapshotted above.
	TSharedPtr<FJsonObject> InstanceEntry = MakeShared<FJsonObject>();
	InstanceEntry->SetStringField(TEXT("assetPath"), InstancePath);
	InstanceEntry->SetArrayField(TEXT("parameters"), RestorableParameters);
	TArray<TSharedPtr<FJsonValue>> InstancesArray;
	InstancesArray.Add(MakeShared<FJsonValueObject>(InstanceEntry));
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetArrayField(TEXT("instances"), InstancesArray);
	MCPSetRollback(Result, TEXT("batch_set_material_instances"), Payload);

	const bool bLossy = UnrestorableCount > 0 || NonGlobalAssociationCount > 0;
	Result->SetBoolField(TEXT("rollbackLossy"), bLossy);
	if (bLossy)
	{
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The rollback restores %d scalar/vector/texture override(s). %d cleared override(s) have no shape in batch_set_material_instances (static switches, double-vector, runtime virtual texture, sparse volume texture and font overrides) and do not come back. %d of the restored overrides were on a Layer or Blend association, which the batch writer cannot express, so they return as Global overrides."),
			RestorableParameters.Num(), UnrestorableCount, NonGlobalAssociationCount));
	}
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
		// Name the call that produces the index rather than only the key that is
		// missing: this action has no name-based address, so a caller who does
		// not already hold an index has nowhere to go from "missing parameter".
		return MCPError(TEXT("Missing required parameter 'expressionIndex'. This action addresses nodes by POSITION in the material's expression list and has no name form; material(list_expressions) reports that list in the same order, and add_expression returns the new node's index as nodeId."));
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

	// The inverse of a value write is the value that was there. Each branch below
	// reads its own field before overwriting it and fills this payload in the
	// same shape this handler accepts, so the rollback replays through the very
	// branch that made the change. A branch that cannot express its previous
	// state (a TextureSample that had no texture) says so instead.
	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	bool bRollbackExpressible = false;
	FString RollbackBlockedReason;

	// Handle UMaterialExpressionConstant - has a single float "R" value
	if (UMaterialExpressionConstant* ConstExpr = Cast<UMaterialExpressionConstant>(Expression))
	{
		double Value = 0.0;
		if (Params->TryGetNumberField(TEXT("value"), Value))
		{
			RollbackPayload->SetNumberField(TEXT("value"), ConstExpr->R);
			bRollbackExpressible = true;
			ConstExpr->R = static_cast<float>(Value);
			bValueSet = true;
			Result->SetNumberField(TEXT("value"), Value);
		}
	}
	// Handle UMaterialExpressionConstant2Vector
	else if (UMaterialExpressionConstant2Vector* Const2Expr = Cast<UMaterialExpressionConstant2Vector>(Expression))
	{
		// #979: this branch used to read only top-level lowercase `r`/`g`, which
		// the tool schema does not declare, so a Constant2Vector had no reachable
		// write path at all while its 3- and 4-component siblings took a `value`
		// object. All three now share one reader, and `{x,y}` works as documented.
		FLinearColor Components;
		FString SourceField;
		if (TryParseMaterialColorParam(Params, Components, SourceField, /*bAllowTopLevelComponents*/ true))
		{
			TSharedPtr<FJsonObject> PreviousValue = MakeShared<FJsonObject>();
			PreviousValue->SetNumberField(TEXT("r"), Const2Expr->R);
			PreviousValue->SetNumberField(TEXT("g"), Const2Expr->G);
			RollbackPayload->SetObjectField(TEXT("value"), PreviousValue);
			bRollbackExpressible = true;

			Const2Expr->R = Components.R;
			Const2Expr->G = Components.G;
			bValueSet = true;

			TSharedPtr<FJsonObject> ValueResult = MakeShared<FJsonObject>();
			ValueResult->SetNumberField(TEXT("r"), Const2Expr->R);
			ValueResult->SetNumberField(TEXT("g"), Const2Expr->G);
			Result->SetObjectField(TEXT("value"), ValueResult);
			Result->SetNumberField(TEXT("r"), Const2Expr->R);
			Result->SetNumberField(TEXT("g"), Const2Expr->G);
		}
	}
	// Handle UMaterialExpressionConstant3Vector - has FLinearColor Constant
	else if (UMaterialExpressionConstant3Vector* Const3Expr = Cast<UMaterialExpressionConstant3Vector>(Expression))
	{
		// #444/#979: accept {r,g,b,a}, {R,G,B,A} or {x,y,z,w} under `value` or
		// `color`, an array, UE struct text, or the components at the top level.
		FLinearColor Color;
		FString SourceField;
		if (TryParseMaterialColorParam(Params, Color, SourceField, /*bAllowTopLevelComponents*/ true))
		{
			RollbackPayload->SetObjectField(TEXT("value"), LinearColorToJson(Const3Expr->Constant));
			bRollbackExpressible = true;
			Const3Expr->Constant = Color;
			bValueSet = true;
			Result->SetObjectField(TEXT("value"), LinearColorToJson(Const3Expr->Constant));
		}
	}
	// Handle UMaterialExpressionConstant4Vector
	else if (UMaterialExpressionConstant4Vector* Const4Expr = Cast<UMaterialExpressionConstant4Vector>(Expression))
	{
		FLinearColor Color;
		FString SourceField;
		if (TryParseMaterialColorParam(Params, Color, SourceField, /*bAllowTopLevelComponents*/ true))
		{
			RollbackPayload->SetObjectField(TEXT("value"), LinearColorToJson(Const4Expr->Constant));
			bRollbackExpressible = true;
			Const4Expr->Constant = Color;
			bValueSet = true;
			Result->SetObjectField(TEXT("value"), LinearColorToJson(Const4Expr->Constant));
		}
	}
	// Handle UMaterialExpressionScalarParameter - has float DefaultValue
	else if (UMaterialExpressionScalarParameter* ScalarParamExpr = Cast<UMaterialExpressionScalarParameter>(Expression))
	{
		double Value = 0.0;
		if (Params->TryGetNumberField(TEXT("value"), Value))
		{
			RollbackPayload->SetNumberField(TEXT("value"), ScalarParamExpr->DefaultValue);
			bRollbackExpressible = true;
			ScalarParamExpr->DefaultValue = static_cast<float>(Value);
			bValueSet = true;
			Result->SetNumberField(TEXT("value"), Value);
		}

		FString ParamName;
		if (Params->TryGetStringField(TEXT("parameterName"), ParamName))
		{
			RollbackPayload->SetStringField(TEXT("parameterName"), ScalarParamExpr->ParameterName.ToString());
			bRollbackExpressible = true;
			ScalarParamExpr->ParameterName = FName(*ParamName);
			bValueSet = true;
			Result->SetStringField(TEXT("parameterName"), ParamName);
		}
	}
	// Handle UMaterialExpressionVectorParameter - has FLinearColor DefaultValue
	else if (UMaterialExpressionVectorParameter* VectorParamExpr = Cast<UMaterialExpressionVectorParameter>(Expression))
	{
		FLinearColor Color;
		FString SourceField;
		if (TryParseMaterialColorParam(Params, Color, SourceField, /*bAllowTopLevelComponents*/ false))
		{
			RollbackPayload->SetObjectField(TEXT("value"), LinearColorToJson(VectorParamExpr->DefaultValue));
			bRollbackExpressible = true;
			VectorParamExpr->DefaultValue = Color;
			bValueSet = true;
			Result->SetObjectField(TEXT("value"), LinearColorToJson(VectorParamExpr->DefaultValue));
		}

		FString ParamName;
		if (Params->TryGetStringField(TEXT("parameterName"), ParamName))
		{
			RollbackPayload->SetStringField(TEXT("parameterName"), VectorParamExpr->ParameterName.ToString());
			bRollbackExpressible = true;
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
				if (TexSampleExpr->Texture)
				{
					RollbackPayload->SetStringField(TEXT("texturePath"), TexSampleExpr->Texture->GetPathName());
					bRollbackExpressible = true;
				}
				else
				{
					RollbackBlockedReason = TEXT("The TextureSample had no texture before this call, and set_expression_value cannot clear one: an empty texturePath is rejected as an unloadable asset.");
				}
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
			RollbackPayload->SetNumberField(TEXT("uTiling"), TexCoordExpr->UTiling);
			bRollbackExpressible = true;
			TexCoordExpr->UTiling = static_cast<float>(UTiling);
			bValueSet = true;
		}
		if (Params->TryGetNumberField(TEXT("vTiling"), VTiling))
		{
			RollbackPayload->SetNumberField(TEXT("vTiling"), TexCoordExpr->VTiling);
			bRollbackExpressible = true;
			TexCoordExpr->VTiling = static_cast<float>(VTiling);
			bValueSet = true;
		}

		int32 CoordinateIndex = 0;
		if (Params->TryGetNumberField(TEXT("coordinateIndex"), CoordinateIndex))
		{
			RollbackPayload->SetNumberField(TEXT("coordinateIndex"), TexCoordExpr->CoordinateIndex);
			bRollbackExpressible = true;
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

	// #185: Generic UPROPERTY fallback - set arbitrary properties on any expression node
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

				// Export before import: the same property, read through the same
				// reflection path, produces text this handler will import back.
				FString PreviousValueText;
				Prop->ExportText_Direct(PreviousValueText, ValuePtr, ValuePtr, Expression, PPF_None);

				const TCHAR* ImportResult = Prop->ImportText_Direct(*ValueStr, ValuePtr, Expression, PPF_None);
				if (ImportResult)
				{
					bValueSet = true;
					Result->SetStringField(TEXT("propertyName"), PropertyName);
					Result->SetStringField(TEXT("importedValue"), ValueStr);
					Result->SetStringField(TEXT("previousValue"), PreviousValueText);
					// An empty export is not a value that can be handed back:
					// ImportText on an empty string fails for most property
					// types, so the replay would error rather than restore.
					if (!PreviousValueText.IsEmpty())
					{
						RollbackPayload->SetStringField(TEXT("propertyName"), PropertyName);
						RollbackPayload->SetStringField(TEXT("value"), PreviousValueText);
						bRollbackExpressible = true;
					}
					else
					{
						RollbackBlockedReason = FString::Printf(
							TEXT("'%s' exported to an empty string before this call (an empty FString, a None FName or an empty container). Replaying set_expression_value with an empty value would fail its ImportText rather than restore anything, so no inverse is offered."),
							*PropertyName);
					}
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

	// #979: the value was written in memory and the package only marked dirty,
	// so a caller who then asked something else to save it could be told the
	// save failed while the write had in fact landed - two answers, neither of
	// them the whole truth. Persist here, and report whether that worked
	// alongside the value that was written either way.
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("saved"), SaveAssetPackage(Material));
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetNumberField(TEXT("expressionIndex"), ExpressionIndex);
	Result->SetStringField(TEXT("expressionClass"), ExpressionClass);

	if (bRollbackExpressible)
	{
		// Rollback: the same call with the values read off the node a moment ago.
		// Addressed by expressionIndex, the same key this call was given, so the
		// replay lands on the same node as long as the expression list is intact.
		RollbackPayload->SetStringField(TEXT("materialPath"), Material->GetPathName());
		RollbackPayload->SetNumberField(TEXT("expressionIndex"), ExpressionIndex);
		MCPSetRollback(Result, TEXT("set_expression_value"), RollbackPayload);
		// Not lossy: the value written back is the one read off this node a
		// moment ago. The caveat is a PRECONDITION rather than a loss, because
		// expressionIndex is a position in the material's expression list.
		Result->SetBoolField(TEXT("rollbackLossy"), false);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("Exact while the expression list is unchanged: the rollback writes the previous value back to index %d, which is the node this call was given. set_expression_value addresses nodes by position, so adding or deleting an expression on this material first shifts what that index names. Re-read list_expressions before rolling back out of order."),
			ExpressionIndex));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), RollbackBlockedReason.IsEmpty()
			? TEXT("The previous value of this field could not be put into a form set_expression_value accepts, so no inverse call is offered rather than one that would write something else.")
			: RollbackBlockedReason);
	}

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

	// UMaterial::SetMaterialUsage became a one-argument virtual in UE 5.8. The
	// only form before that takes a bNeedsRecompile out param, and the 5.8
	// build keeps it as a deprecated inline shim that forwards and ignores the
	// param. Calling the one-argument form on 5.7 is a hard compile error
	// (C2660), so pick the form the engine in hand actually declares.
	static bool ApplyMaterialUsage(UMaterial* Material, EMaterialUsage Usage)
	{
#if UE_MCP_HAS_5_8_API
		return Material->SetMaterialUsage(Usage);
#else
		bool bNeedsRecompile = false;
		return Material->SetMaterialUsage(bNeedsRecompile, Usage);
#endif
	}
}

// #617 read/write a MaterialExpressionCustom's HLSL Code, named inputs, and
// output type. Omit 'code' (and 'inputs') to read the current node state.
TSharedPtr<FJsonValue> FMaterialHandlers::SetCustomExpression(const TSharedPtr<FJsonObject>& Params)
{
	FString MaterialPath;
	if (auto Err = RequireStringAlt(Params, TEXT("materialPath"), TEXT("path"), MaterialPath)) return Err;
	if (MaterialPath.IsEmpty()) Params->TryGetStringField(TEXT("assetPath"), MaterialPath);
	if (MaterialPath.IsEmpty()) return MCPError(TEXT("Missing required parameter 'materialPath'"));

	int32 ExpressionIndex = -1;
	if (!Params->TryGetNumberField(TEXT("expressionIndex"), ExpressionIndex))
	{
		return MCPError(TEXT("Missing required parameter 'expressionIndex' (index from list_expressions)"));
	}

	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material) return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));

	auto Expressions = Material->GetExpressions();
	if (ExpressionIndex < 0 || ExpressionIndex >= Expressions.Num())
	{
		return MCPError(FString::Printf(TEXT("Expression index %d out of range (0-%d)"), ExpressionIndex, Expressions.Num() - 1));
	}

	UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expressions[ExpressionIndex]);
	if (!Custom)
	{
		return MCPError(FString::Printf(TEXT("Expression %d is %s, not a Custom node. Add one with add_expression expressionType=Custom."),
			ExpressionIndex, *Expressions[ExpressionIndex]->GetClass()->GetName()));
	}

	// Everything this call can write, read out first. It is what the rollback
	// replays, and it is also what decides whether anything changed at all -
	// the previous code reported "updated" for a write that set a field to the
	// value it already held.
	const FString PreviousCode = Custom->Code;
	const FString PreviousDescription = Custom->Description;
	const ECustomMaterialOutputType PreviousOutputType = Custom->OutputType;
	TArray<TSharedPtr<FJsonValue>> PreviousInputNames;
	bool bPreviousInputWasWired = false;
	for (const FCustomInput& CI : Custom->Inputs)
	{
		PreviousInputNames.Add(MakeShared<FJsonValueString>(CI.InputName.ToString()));
		if (CI.Input.Expression) bPreviousInputWasWired = true;
	}

	// An output type only round-trips through the rollback if it can be named
	// in the vocabulary the parser below accepts.
	auto OutputTypeToString = [](ECustomMaterialOutputType Type) -> FString
	{
		switch (Type)
		{
		case CMOT_Float1: return TEXT("float1");
		case CMOT_Float2: return TEXT("float2");
		case CMOT_Float3: return TEXT("float3");
		case CMOT_Float4: return TEXT("float4");
		case CMOT_MaterialAttributes: return TEXT("materialattributes");
		default: return FString();
		}
	};

	bool bChanged = false;
	bool bInputsRebuilt = false;

	FString Code;
	if (Params->TryGetStringField(TEXT("code"), Code) && Code != PreviousCode)
	{
		Material->PreEditChange(nullptr);
		Custom->Code = Code;
		bChanged = true;
	}

	FString Description;
	if (Params->TryGetStringField(TEXT("description"), Description) && Description != PreviousDescription)
	{
		Custom->Description = Description;
		bChanged = true;
	}

	// Output type: CMOT_Float1..4 / CMOT_MaterialAttributes (accept "float3" etc.)
	FString OutputTypeStr;
	if (Params->TryGetStringField(TEXT("outputType"), OutputTypeStr))
	{
		const FString L = OutputTypeStr.ToLower();
		ECustomMaterialOutputType Requested = PreviousOutputType;
		if (L == TEXT("float1") || L == TEXT("cmot_float1")) Requested = CMOT_Float1;
		else if (L == TEXT("float2") || L == TEXT("cmot_float2")) Requested = CMOT_Float2;
		else if (L == TEXT("float3") || L == TEXT("cmot_float3")) Requested = CMOT_Float3;
		else if (L == TEXT("float4") || L == TEXT("cmot_float4")) Requested = CMOT_Float4;
		else if (L.Contains(TEXT("materialattributes"))) Requested = CMOT_MaterialAttributes;
		if (Requested != PreviousOutputType)
		{
			Custom->OutputType = Requested;
			bChanged = true;
		}
	}

	// Inputs: array of input names (rebuilds the input pin list). Wire them
	// afterward with connect_expressions targetInput=<name>.
	const TArray<TSharedPtr<FJsonValue>>* InputsArr = nullptr;
	if (Params->TryGetArrayField(TEXT("inputs"), InputsArr) && InputsArr)
	{
		Custom->Inputs.Empty();
		for (const TSharedPtr<FJsonValue>& V : *InputsArr)
		{
			FString InName;
			if (V.IsValid() && V->TryGetString(InName) && !InName.IsEmpty())
			{
				FCustomInput CI;
				CI.InputName = FName(*InName);
				Custom->Inputs.Add(CI);
			}
		}
		bChanged = true;
		bInputsRebuilt = true;
	}

	if (bChanged)
	{
		Custom->PostEditChange();
		Material->PostEditChange();
		Material->MarkPackageDirty();
		SaveAssetPackage(Material);
	}

	auto Result = MCPSuccess();
	if (bChanged) MCPSetUpdated(Result);
	else Result->SetBoolField(TEXT("unchanged"), true);
	Result->SetStringField(TEXT("materialPath"), MaterialPath);
	Result->SetNumberField(TEXT("expressionIndex"), ExpressionIndex);
	Result->SetStringField(TEXT("code"), Custom->Code);
	Result->SetStringField(TEXT("description"), Custom->Description);
	Result->SetNumberField(TEXT("outputType"), (int32)Custom->OutputType);
	TArray<TSharedPtr<FJsonValue>> InNames;
	for (const FCustomInput& CI : Custom->Inputs)
	{
		InNames.Add(MakeShared<FJsonValueString>(CI.InputName.ToString()));
	}
	Result->SetArrayField(TEXT("inputs"), InNames);

	if (bChanged)
	{
		// Rollback: write the node back the way it was found. Every field this
		// handler can set is a plain value it read before overwriting.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("materialPath"), MaterialPath);
		Payload->SetNumberField(TEXT("expressionIndex"), ExpressionIndex);
		Payload->SetStringField(TEXT("code"), PreviousCode);
		Payload->SetStringField(TEXT("description"), PreviousDescription);
		const FString PreviousOutputTypeName = OutputTypeToString(PreviousOutputType);
		if (!PreviousOutputTypeName.IsEmpty())
		{
			Payload->SetStringField(TEXT("outputType"), PreviousOutputTypeName);
		}
		if (bInputsRebuilt)
		{
			Payload->SetArrayField(TEXT("inputs"), PreviousInputNames);
		}
		MCPSetRollback(Result, TEXT("set_custom_expression"), Payload);

		// Rebuilding the input list empties FCustomInput entries wholesale, and
		// each entry carries its own FExpressionInput. Restoring the names does
		// not restore what was plugged into them.
		const bool bLossy = bInputsRebuilt && bPreviousInputWasWired;
		Result->SetBoolField(TEXT("rollbackLossy"), bLossy);
		if (bLossy)
		{
			Result->SetStringField(TEXT("rollbackNote"),
				TEXT("The rollback restores code, description, output type and the input pin NAMES. It does not restore what was wired into those pins: rebuilding Inputs discarded each pin's connection. Rewire them with connect_material_expressions targetInput=<name>."));
		}
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Nothing changed: every field supplied already held the value asked for (or none was supplied, which is the read form of this action). There is nothing to undo."));
	}
	return MCPResult(Result);
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

	// No 'enabled' parameter. This action turns flags on and nothing else; the
	// schema used to declare one and this handler never gated on it, so the
	// surface advertised a disable that silently did nothing. Clearing a flag
	// goes through editor(set_property) on the reflected bUsedWith* property,
	// and the exact calls are returned below under clearCalls.

	// Every usage ParseMaterialUsage above can recognise is backed by a plain
	// bUsedWith* UPROPERTY on UMaterial, which is what makes editor(set_property)
	// able to clear one. This table covers all twenty of them, including Water,
	// HairStrands, LidarPointCloud, VirtualHeightfieldMesh and Nanite - each of
	// those does have a property (uint32 bitfields rather than uint8, which is
	// the only thing that sets them apart) and an earlier version of this table
	// omitted them, so the response told callers they could not be cleared when
	// they could.
	//
	// Only the twenty are listed: EMaterialUsage carries more (Voxels,
	// VolumetricCloud, MeshDeformer, Curves and others), but this handler cannot
	// reach them because the parser does not spell them, and naming a 5.8-only
	// enumerator here would stop the plugin compiling against 5.7.
	//
	// The name is resolved against the class rather than trusted, so a rename in
	// a future engine degrades to "no reflected property" instead of handing the
	// caller a set_property call that would fail.
	auto UsageClearProperty = [](EMaterialUsage Usage) -> FString
	{
		const TCHAR* Name = nullptr;
		switch (Usage)
		{
		case MATUSAGE_SkeletalMesh:           Name = TEXT("bUsedWithSkeletalMesh"); break;
		case MATUSAGE_ParticleSprites:        Name = TEXT("bUsedWithParticleSprites"); break;
		case MATUSAGE_BeamTrails:             Name = TEXT("bUsedWithBeamTrails"); break;
		case MATUSAGE_MeshParticles:          Name = TEXT("bUsedWithMeshParticles"); break;
		case MATUSAGE_StaticLighting:         Name = TEXT("bUsedWithStaticLighting"); break;
		case MATUSAGE_MorphTargets:           Name = TEXT("bUsedWithMorphTargets"); break;
		// Plural in the property, singular in the enumerator.
		case MATUSAGE_SplineMesh:             Name = TEXT("bUsedWithSplineMeshes"); break;
		case MATUSAGE_InstancedStaticMeshes:  Name = TEXT("bUsedWithInstancedStaticMeshes"); break;
		case MATUSAGE_GeometryCollections:    Name = TEXT("bUsedWithGeometryCollections"); break;
		case MATUSAGE_Clothing:               Name = TEXT("bUsedWithClothing"); break;
		case MATUSAGE_NiagaraSprites:         Name = TEXT("bUsedWithNiagaraSprites"); break;
		case MATUSAGE_NiagaraRibbons:         Name = TEXT("bUsedWithNiagaraRibbons"); break;
		case MATUSAGE_NiagaraMeshParticles:   Name = TEXT("bUsedWithNiagaraMeshParticles"); break;
		case MATUSAGE_GeometryCache:          Name = TEXT("bUsedWithGeometryCache"); break;
		case MATUSAGE_Water:                  Name = TEXT("bUsedWithWater"); break;
		case MATUSAGE_HairStrands:            Name = TEXT("bUsedWithHairStrands"); break;
		case MATUSAGE_LidarPointCloud:        Name = TEXT("bUsedWithLidarPointCloud"); break;
		case MATUSAGE_VirtualHeightfieldMesh: Name = TEXT("bUsedWithVirtualHeightfieldMesh"); break;
		case MATUSAGE_Nanite:                 Name = TEXT("bUsedWithNanite"); break;
		default: return FString();
		}
		return UMaterial::StaticClass()->FindPropertyByName(FName(Name)) ? FString(Name) : FString();
	};

	TArray<FString> Applied, Unknown, AlreadySet;
	TArray<TSharedPtr<FJsonValue>> ClearCalls;
	FString SingleClearProperty;
	for (const FString& U : UsagesIn)
	{
		EMaterialUsage Usage;
		if (!ParseMaterialUsage(U, Usage))
		{
			Unknown.Add(U);
			continue;
		}
		// Read the flag first so the response can say which usages this call
		// actually turned on and which were already on, rather than reporting
		// every requested flag as applied on every replay. The write itself is
		// NOT skipped when the bit is already set: SetMaterialUsage is an ensure
		// that also drives the shader-map recompile, so a material whose bit is
		// set but whose shaders were never compiled is still repaired by a
		// replay. Skipping it would have quietly removed that repair.
		const bool bWasSet = Material->GetUsageByFlag(Usage);
		// The bNeedsRecompile out param is gone in the virtual implementation;
		// the shim that kept it always ignored the value anyway.
		ApplyMaterialUsage(Material, Usage);
		if (bWasSet)
		{
			AlreadySet.Add(U);
			continue;
		}
		Applied.Add(U);

		const FString ClearProperty = UsageClearProperty(Usage);
		if (!ClearProperty.IsEmpty())
		{
			SingleClearProperty = ClearProperty;
			TSharedPtr<FJsonObject> Call = MakeShared<FJsonObject>();
			Call->SetStringField(TEXT("usage"), U);
			Call->SetStringField(TEXT("objectPath"), Material->GetPathName());
			Call->SetStringField(TEXT("propertyName"), ClearProperty);
			Call->SetBoolField(TEXT("value"), false);
			ClearCalls.Add(MakeShared<FJsonValueObject>(Call));
		}
	}

	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Material->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(Material, /*bOnlyIfIsDirty=*/false);

	auto Result = MCPSuccess();
	if (Applied.Num() > 0) MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), Material->GetPathName());
	TArray<TSharedPtr<FJsonValue>> AppliedJ, UnknownJ, AlreadyJ;
	for (const FString& S : Applied)     AppliedJ.Add(MakeShared<FJsonValueString>(S));
	for (const FString& S : Unknown)     UnknownJ.Add(MakeShared<FJsonValueString>(S));
	for (const FString& S : AlreadySet)  AlreadyJ.Add(MakeShared<FJsonValueString>(S));
	Result->SetArrayField(TEXT("applied"), AppliedJ);
	Result->SetArrayField(TEXT("alreadySet"), AlreadyJ);
	if (Unknown.Num() > 0) Result->SetArrayField(TEXT("unknown"), UnknownJ);
	Result->SetArrayField(TEXT("clearCalls"), ClearCalls);
	Result->SetStringField(TEXT("idempotencyNote"),
		TEXT("'applied' lists the flags this call turned on and 'alreadySet' those that were on already. Both are still pushed through SetMaterialUsage, which is an ensure rather than a write: it also drives the shader-map recompile, so a replay repairs a material whose bit is set but whose shaders were never built. That is why a call with an empty 'applied' still recompiles and saves."));

	// set_material_usage itself has no inverse: it only ever turns flags on, and
	// its 'enabled' parameter is echoed back without gating the write, so a
	// rollback naming this action with enabled=false would report success and
	// change nothing. The flags ARE reachable another way - most are backed by a
	// bUsedWith* UPROPERTY on UMaterial, which editor(set_property) can write -
	// so the exact calls are handed back in clearCalls, and a single-flag change
	// gets that call as its rollback outright.
	if (Applied.Num() == 0)
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), AlreadySet.Num() > 0
			? TEXT("No flag was turned on by this call: every usage it recognised was already set. The recompile and save still ran, but there is no flag change to undo.")
			: TEXT("No flag was turned on by this call: none of the usages given was recognised, and they are listed under 'unknown'. Nothing changed, so there is nothing to undo."));
	}
	else if (ClearCalls.Num() == 1 && Applied.Num() == 1)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("objectPath"), Material->GetPathName());
		Payload->SetStringField(TEXT("propertyName"), SingleClearProperty);
		Payload->SetBoolField(TEXT("value"), false);
		MCPSetRollback(Result, TEXT("set_property"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("set_material_usage cannot clear a flag, so the inverse goes through the reflected property instead: editor(set_property) writes %s=false on the material. That clears the flag but does NOT discard the shader permutations this call had compiled, which stay in the derived data until the next full recompile."),
			*SingleClearProperty));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("This call turned on %d flag(s) and a rollback record carries one call, so no single inverse is offered. Each one is listed in clearCalls with the exact editor(set_property) arguments that clear it; run them in any order. %d of them resolved to no reflected property on this engine build and are absent from clearCalls, so nothing in the surface clears those."),
			Applied.Num(), Applied.Num() - ClearCalls.Num()));
	}
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
					ApplyMaterialUsage(Material, U);
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
