#include "MaterialHandlers.h"
#include "HandlerRegistry.h"
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

void FMaterialHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("list_expression_types"), &ListExpressionTypes);
	Registry.RegisterHandler(TEXT("create_material"), &CreateMaterial);
	Registry.RegisterHandler(TEXT("read_material"), &ReadMaterial);
	Registry.RegisterHandler(TEXT("set_material_shading_model"), &SetMaterialShadingModel);
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
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
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
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::CreateMaterial(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/Materials");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	UE_LOG(LogTemp, Log, TEXT("[UE-MCP] CreateMaterial: name=%s packagePath=%s"), *Name, *PackagePath);

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UMaterialFactoryNew* MaterialFactory = NewObject<UMaterialFactoryNew>();
	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, UMaterial::StaticClass(), MaterialFactory);

	if (!NewAsset)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create material asset"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UMaterial* NewMaterial = Cast<UMaterial>(NewAsset);
	if (!NewMaterial)
	{
		Result->SetStringField(TEXT("error"), TEXT("Created asset is not a material"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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

	FString AssetPath = NewMaterial->GetPathName();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("packagePath"), PackagePath);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::ReadMaterial(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UMaterial* Material = LoadMaterialFromPath(AssetPath);
	if (!Material)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load material at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	Result->SetStringField(TEXT("name"), Material->GetName());
	Result->SetStringField(TEXT("path"), Material->GetPathName());
	Result->SetStringField(TEXT("shadingModel"), ShadingModelToString(Material->GetShadingModels().GetFirstShadingModel()));
	Result->SetStringField(TEXT("blendMode"), StaticEnum<EBlendMode>()->GetNameStringByValue((int64)Material->BlendMode));
	Result->SetBoolField(TEXT("twoSided"), Material->IsTwoSided());

	// Expression count
	int32 ExpressionCount = Material->GetExpressions().Num();
	Result->SetNumberField(TEXT("expressionCount"), ExpressionCount);

	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::SetMaterialShadingModel(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString ShadingModelStr;
	if (!Params->TryGetStringField(TEXT("shadingModel"), ShadingModelStr) || ShadingModelStr.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'shadingModel' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UMaterial* Material = LoadMaterialFromPath(AssetPath);
	if (!Material)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load material at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	EMaterialShadingModel NewShadingModel = ParseShadingModel(ShadingModelStr);

	Material->PreEditChange(nullptr);
	Material->SetShadingModel(NewShadingModel);
	Material->PostEditChange();

	// Mark dirty and save
	Material->MarkPackageDirty();

	Result->SetStringField(TEXT("path"), Material->GetPathName());
	Result->SetStringField(TEXT("shadingModel"), ShadingModelToString(NewShadingModel));
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::SetMaterialBaseColor(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	const TSharedPtr<FJsonObject>* ColorObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("color"), ColorObj))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'color' parameter (object with r,g,b,a)"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	double R = 1.0, G = 1.0, B = 1.0, A = 1.0;
	(*ColorObj)->TryGetNumberField(TEXT("r"), R);
	(*ColorObj)->TryGetNumberField(TEXT("g"), G);
	(*ColorObj)->TryGetNumberField(TEXT("b"), B);
	(*ColorObj)->TryGetNumberField(TEXT("a"), A);

	UMaterial* Material = LoadMaterialFromPath(AssetPath);
	if (!Material)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load material at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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

	TSharedPtr<FJsonObject> ColorResult = MakeShared<FJsonObject>();
	ColorResult->SetNumberField(TEXT("r"), R);
	ColorResult->SetNumberField(TEXT("g"), G);
	ColorResult->SetNumberField(TEXT("b"), B);
	ColorResult->SetNumberField(TEXT("a"), A);
	Result->SetObjectField(TEXT("color"), ColorResult);
	Result->SetStringField(TEXT("path"), Material->GetPathName());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::AddMaterialExpression(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString MaterialPath;
	if (!Params->TryGetStringField(TEXT("materialPath"), MaterialPath) || MaterialPath.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'materialPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString ExpressionType;
	if (!Params->TryGetStringField(TEXT("expressionType"), ExpressionType) || ExpressionType.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'expressionType' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Ensure expression type has the U prefix for class lookup
	FString ClassName = ExpressionType;
	if (!ClassName.StartsWith(TEXT("U")))
	{
		ClassName = TEXT("U") + ClassName;
	}

	// Find the expression class
	UClass* ExpressionClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::ExactClass);
	if (!ExpressionClass)
	{
		// Try with /Script/Engine prefix
		FString FullPath = FString::Printf(TEXT("/Script/Engine.%s"), *ExpressionType);
		ExpressionClass = FindObject<UClass>(nullptr, *FullPath);
	}

	if (!ExpressionClass || !ExpressionClass->IsChildOf(UMaterialExpression::StaticClass()))
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown expression type: '%s'"), *ExpressionType));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	Material->PreEditChange(nullptr);

	UMaterialExpression* NewExpression = NewObject<UMaterialExpression>(Material, ExpressionClass);
	Material->GetExpressionCollection().AddExpression(NewExpression);

	Material->PostEditChange();
	Material->MarkPackageDirty();

	Result->SetStringField(TEXT("expressionType"), ExpressionType);
	Result->SetStringField(TEXT("expressionClass"), NewExpression->GetClass()->GetName());
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetNumberField(TEXT("expressionCount"), Material->GetExpressions().Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::ListMaterialExpressions(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString MaterialPath;
	if (!Params->TryGetStringField(TEXT("materialPath"), MaterialPath) || MaterialPath.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'materialPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	TArray<TSharedPtr<FJsonValue>> ExpressionsArray;
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (!Expression) continue;

		TSharedPtr<FJsonObject> ExprObj = MakeShared<FJsonObject>();
		ExprObj->SetStringField(TEXT("class"), Expression->GetClass()->GetName());
		ExprObj->SetStringField(TEXT("description"), Expression->GetDescription());
		ExprObj->SetNumberField(TEXT("positionX"), Expression->MaterialExpressionEditorX);
		ExprObj->SetNumberField(TEXT("positionY"), Expression->MaterialExpressionEditorY);

		ExpressionsArray.Add(MakeShared<FJsonValueObject>(ExprObj));
	}

	Result->SetArrayField(TEXT("expressions"), ExpressionsArray);
	Result->SetNumberField(TEXT("count"), ExpressionsArray.Num());
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::ListMaterialParameters(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UMaterial* Material = LoadMaterialFromPath(AssetPath);
	if (!Material)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load material at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

	Result->SetArrayField(TEXT("scalarParameters"), ScalarParams);
	Result->SetArrayField(TEXT("vectorParameters"), VectorParams);
	Result->SetArrayField(TEXT("textureParameters"), TextureParams);
	Result->SetStringField(TEXT("path"), Material->GetPathName());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::RecompileMaterial(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString MaterialPath;
	if (!Params->TryGetStringField(TEXT("materialPath"), MaterialPath) || MaterialPath.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'materialPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UE_LOG(LogTemp, Log, TEXT("[UE-MCP] Recompiling material: %s"), *MaterialPath);

	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Material->MarkPackageDirty();

	Result->SetStringField(TEXT("path"), Material->GetPathName());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::CreateMaterialInstance(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString ParentPath;
	if (!Params->TryGetStringField(TEXT("parentPath"), ParentPath) || ParentPath.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'parentPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/Materials");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	// Load the parent material
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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load parent material at '%s'"), *ParentPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UE_LOG(LogTemp, Log, TEXT("[UE-MCP] CreateMaterialInstance: name=%s parent=%s packagePath=%s"), *Name, *ParentPath, *PackagePath);

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
	Factory->InitialParent = ParentMaterial;

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, UMaterialInstanceConstant::StaticClass(), Factory);
	if (!NewAsset)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create material instance asset"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UMaterialInstanceConstant* MaterialInstance = Cast<UMaterialInstanceConstant>(NewAsset);
	if (!MaterialInstance)
	{
		Result->SetStringField(TEXT("error"), TEXT("Created asset is not a material instance"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

	Result->SetStringField(TEXT("path"), MaterialInstance->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("parentPath"), ParentMaterial->GetPathName());
	Result->SetStringField(TEXT("packagePath"), PackagePath);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::SetMaterialParameter(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString ParameterName;
	if (!Params->TryGetStringField(TEXT("parameterName"), ParameterName) || ParameterName.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'parameterName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString ParameterType;
	if (!Params->TryGetStringField(TEXT("parameterType"), ParameterType) || ParameterType.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'parameterType' parameter (scalar/vector/texture)"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UMaterialInstanceConstant* MaterialInstance = LoadMaterialInstanceFromPath(AssetPath);
	if (!MaterialInstance)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load material instance at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString TypeLower = ParameterType.ToLower();

	if (TypeLower == TEXT("scalar"))
	{
		double ScalarValue = 0.0;
		if (!Params->TryGetNumberField(TEXT("value"), ScalarValue))
		{
			Result->SetStringField(TEXT("error"), TEXT("Missing 'value' number field for scalar parameter"));
			Result->SetBoolField(TEXT("success"), false);
			return MakeShared<FJsonValueObject>(Result);
		}

		MaterialInstance->SetScalarParameterValueEditorOnly(FName(*ParameterName), static_cast<float>(ScalarValue));
		MaterialInstance->MarkPackageDirty();

		Result->SetStringField(TEXT("parameterName"), ParameterName);
		Result->SetStringField(TEXT("parameterType"), TEXT("scalar"));
		Result->SetNumberField(TEXT("value"), ScalarValue);
		Result->SetStringField(TEXT("path"), MaterialInstance->GetPathName());
		Result->SetBoolField(TEXT("success"), true);
	}
	else if (TypeLower == TEXT("vector"))
	{
		const TSharedPtr<FJsonObject>* ValueObj = nullptr;
		if (!Params->TryGetObjectField(TEXT("value"), ValueObj))
		{
			Result->SetStringField(TEXT("error"), TEXT("Missing 'value' object field (r,g,b,a) for vector parameter"));
			Result->SetBoolField(TEXT("success"), false);
			return MakeShared<FJsonValueObject>(Result);
		}

		double R = 0.0, G = 0.0, B = 0.0, A = 1.0;
		(*ValueObj)->TryGetNumberField(TEXT("r"), R);
		(*ValueObj)->TryGetNumberField(TEXT("g"), G);
		(*ValueObj)->TryGetNumberField(TEXT("b"), B);
		(*ValueObj)->TryGetNumberField(TEXT("a"), A);

		FLinearColor ColorValue(R, G, B, A);
		MaterialInstance->SetVectorParameterValueEditorOnly(FName(*ParameterName), ColorValue);
		MaterialInstance->MarkPackageDirty();

		TSharedPtr<FJsonObject> ValueResult = MakeShared<FJsonObject>();
		ValueResult->SetNumberField(TEXT("r"), R);
		ValueResult->SetNumberField(TEXT("g"), G);
		ValueResult->SetNumberField(TEXT("b"), B);
		ValueResult->SetNumberField(TEXT("a"), A);

		Result->SetStringField(TEXT("parameterName"), ParameterName);
		Result->SetStringField(TEXT("parameterType"), TEXT("vector"));
		Result->SetObjectField(TEXT("value"), ValueResult);
		Result->SetStringField(TEXT("path"), MaterialInstance->GetPathName());
		Result->SetBoolField(TEXT("success"), true);
	}
	else if (TypeLower == TEXT("texture"))
	{
		FString TexturePath;
		if (!Params->TryGetStringField(TEXT("value"), TexturePath) || TexturePath.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), TEXT("Missing 'value' string field (texture asset path) for texture parameter"));
			Result->SetBoolField(TEXT("success"), false);
			return MakeShared<FJsonValueObject>(Result);
		}

		UTexture* Texture = Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr, *TexturePath));
		if (!Texture)
		{
			Texture = Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr,
				*(TEXT("Texture2D'") + TexturePath + TEXT("'"))));
		}
		if (!Texture)
		{
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load texture at '%s'"), *TexturePath));
			Result->SetBoolField(TEXT("success"), false);
			return MakeShared<FJsonValueObject>(Result);
		}

		MaterialInstance->SetTextureParameterValueEditorOnly(FName(*ParameterName), Texture);
		MaterialInstance->MarkPackageDirty();

		Result->SetStringField(TEXT("parameterName"), ParameterName);
		Result->SetStringField(TEXT("parameterType"), TEXT("texture"));
		Result->SetStringField(TEXT("value"), Texture->GetPathName());
		Result->SetStringField(TEXT("path"), MaterialInstance->GetPathName());
		Result->SetBoolField(TEXT("success"), true);
	}
	else
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown parameterType '%s'. Use 'scalar', 'vector', or 'texture'."), *ParameterType));
		Result->SetBoolField(TEXT("success"), false);
	}

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::ConnectExpression(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString MaterialPath;
	if (!Params->TryGetStringField(TEXT("materialPath"), MaterialPath) || MaterialPath.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'materialPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	int32 SourceIndex = -1;
	if (!Params->TryGetNumberField(TEXT("sourceIndex"), SourceIndex))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'sourceIndex' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	int32 TargetIndex = -1;
	if (!Params->TryGetNumberField(TEXT("targetIndex"), TargetIndex))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'targetIndex' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	int32 SourceOutputIndex = 0;
	Params->TryGetNumberField(TEXT("sourceOutputIndex"), SourceOutputIndex);

	int32 TargetInputIndex = 0;
	Params->TryGetNumberField(TEXT("targetInputIndex"), TargetInputIndex);

	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressions();

	if (SourceIndex < 0 || SourceIndex >= Expressions.Num())
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Source expression index %d out of range (0-%d)"), SourceIndex, Expressions.Num() - 1));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	if (TargetIndex < 0 || TargetIndex >= Expressions.Num())
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Target expression index %d out of range (0-%d)"), TargetIndex, Expressions.Num() - 1));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UMaterialExpression* SourceExpression = Expressions[SourceIndex];
	UMaterialExpression* TargetExpression = Expressions[TargetIndex];

	if (!SourceExpression || !TargetExpression)
	{
		Result->SetStringField(TEXT("error"), TEXT("Source or target expression is null"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Get the target expression's inputs
	TArray<FExpressionInput*> TargetInputs = TargetExpression->GetInputs();
	if (TargetInputIndex < 0 || TargetInputIndex >= TargetInputs.Num())
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Target input index %d out of range (0-%d)"), TargetInputIndex, TargetInputs.Num() - 1));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	Material->PreEditChange(nullptr);

	FExpressionInput* TargetInput = TargetInputs[TargetInputIndex];
	TargetInput->Connect(SourceOutputIndex, SourceExpression);

	Material->PostEditChange();
	Material->MarkPackageDirty();

	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetNumberField(TEXT("sourceIndex"), SourceIndex);
	Result->SetStringField(TEXT("sourceClass"), SourceExpression->GetClass()->GetName());
	Result->SetNumberField(TEXT("targetIndex"), TargetIndex);
	Result->SetStringField(TEXT("targetClass"), TargetExpression->GetClass()->GetName());
	Result->SetNumberField(TEXT("sourceOutputIndex"), SourceOutputIndex);
	Result->SetNumberField(TEXT("targetInputIndex"), TargetInputIndex);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::ConnectMaterialProperty(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString MaterialPath;
	if (!Params->TryGetStringField(TEXT("materialPath"), MaterialPath) || MaterialPath.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'materialPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	int32 ExpressionIndex = -1;
	if (!Params->TryGetNumberField(TEXT("expressionIndex"), ExpressionIndex))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'expressionIndex' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PropertyName;
	if (!Params->TryGetStringField(TEXT("property"), PropertyName) || PropertyName.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'property' parameter (BaseColor, Normal, Metallic, Roughness, etc.)"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	int32 OutputIndex = 0;
	Params->TryGetNumberField(TEXT("outputIndex"), OutputIndex);

	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressions();

	if (ExpressionIndex < 0 || ExpressionIndex >= Expressions.Num())
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Expression index %d out of range (0-%d)"), ExpressionIndex, Expressions.Num() - 1));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UMaterialExpression* Expression = Expressions[ExpressionIndex];
	if (!Expression)
	{
		Result->SetStringField(TEXT("error"), TEXT("Expression at given index is null"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	EMaterialProperty MatProperty;
	if (!ParseMaterialProperty(PropertyName, MatProperty))
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown material property '%s'. Available: BaseColor, Metallic, Specular, Roughness, Anisotropy, EmissiveColor, Opacity, OpacityMask, Normal, Tangent, WorldPositionOffset, SubsurfaceColor, AmbientOcclusion, Refraction, PixelDepthOffset, ShadingModel"), *PropertyName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Material property '%s' is not supported for direct connection"), *PropertyName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	PropertyInput->Connect(OutputIndex, Expression);

	Material->PostEditChange();
	Material->MarkPackageDirty();

	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetNumberField(TEXT("expressionIndex"), ExpressionIndex);
	Result->SetStringField(TEXT("expressionClass"), Expression->GetClass()->GetName());
	Result->SetStringField(TEXT("property"), PropertyName);
	Result->SetNumberField(TEXT("outputIndex"), OutputIndex);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::DeleteExpression(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString MaterialPath;
	if (!Params->TryGetStringField(TEXT("materialPath"), MaterialPath) || MaterialPath.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'materialPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	int32 ExpressionIndex = -1;
	if (!Params->TryGetNumberField(TEXT("expressionIndex"), ExpressionIndex))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'expressionIndex' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressions();

	if (ExpressionIndex < 0 || ExpressionIndex >= Expressions.Num())
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Expression index %d out of range (0-%d)"), ExpressionIndex, Expressions.Num() - 1));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UMaterialExpression* Expression = Expressions[ExpressionIndex];
	if (!Expression)
	{
		Result->SetStringField(TEXT("error"), TEXT("Expression at given index is null"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString DeletedClass = Expression->GetClass()->GetName();

	Material->PreEditChange(nullptr);

	Material->GetExpressionCollection().RemoveExpression(Expression);

	Material->PostEditChange();
	Material->MarkPackageDirty();

	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetNumberField(TEXT("deletedIndex"), ExpressionIndex);
	Result->SetStringField(TEXT("deletedClass"), DeletedClass);
	Result->SetNumberField(TEXT("expressionCount"), Material->GetExpressions().Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::SetExpressionValue(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString MaterialPath;
	if (!Params->TryGetStringField(TEXT("materialPath"), MaterialPath) || MaterialPath.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'materialPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	int32 ExpressionIndex = -1;
	if (!Params->TryGetNumberField(TEXT("expressionIndex"), ExpressionIndex))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'expressionIndex' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressions();

	if (ExpressionIndex < 0 || ExpressionIndex >= Expressions.Num())
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Expression index %d out of range (0-%d)"), ExpressionIndex, Expressions.Num() - 1));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UMaterialExpression* Expression = Expressions[ExpressionIndex];
	if (!Expression)
	{
		Result->SetStringField(TEXT("error"), TEXT("Expression at given index is null"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	Material->PreEditChange(nullptr);

	FString ExpressionClass = Expression->GetClass()->GetName();
	bool bValueSet = false;

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
				Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load texture at '%s'"), *TexturePath));
				Result->SetBoolField(TEXT("success"), false);
				return MakeShared<FJsonValueObject>(Result);
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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Could not set value on expression of type '%s'. Provide appropriate value parameters for this expression type."), *ExpressionClass));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	Material->PostEditChange();
	Material->MarkPackageDirty();

	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetNumberField(TEXT("expressionIndex"), ExpressionIndex);
	Result->SetStringField(TEXT("expressionClass"), ExpressionClass);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::CreateMaterialFromTexture(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString TexturePath;
	if (!Params->TryGetStringField(TEXT("texturePath"), TexturePath) || TexturePath.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'texturePath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString MaterialName;
	if (!Params->TryGetStringField(TEXT("materialName"), MaterialName) || MaterialName.IsEmpty())
	{
		// Derive a material name from the texture name
		FString TextureName = FPaths::GetBaseFilename(TexturePath);
		MaterialName = TEXT("M_") + TextureName;
	}

	FString PackagePath = TEXT("/Game/Materials");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	// Load the texture
	UTexture* Texture = Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr, *TexturePath));
	if (!Texture)
	{
		Texture = Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr,
			*(TEXT("Texture2D'") + TexturePath + TEXT("'"))));
	}
	if (!Texture)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load texture at '%s'"), *TexturePath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UE_LOG(LogTemp, Log, TEXT("[UE-MCP] CreateMaterialFromTexture: texture=%s materialName=%s packagePath=%s"), *TexturePath, *MaterialName, *PackagePath);

	// Create the material
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UMaterialFactoryNew* MaterialFactory = NewObject<UMaterialFactoryNew>();
	UObject* NewAsset = AssetTools.CreateAsset(MaterialName, PackagePath, UMaterial::StaticClass(), MaterialFactory);

	if (!NewAsset)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create material asset"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UMaterial* NewMaterial = Cast<UMaterial>(NewAsset);
	if (!NewMaterial)
	{
		Result->SetStringField(TEXT("error"), TEXT("Created asset is not a material"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

	Result->SetStringField(TEXT("materialPath"), NewMaterial->GetPathName());
	Result->SetStringField(TEXT("materialName"), MaterialName);
	Result->SetStringField(TEXT("texturePath"), Texture->GetPathName());
	Result->SetStringField(TEXT("packagePath"), PackagePath);
	Result->SetNumberField(TEXT("expressionCount"), NewMaterial->GetExpressions().Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FMaterialHandlers::ReadMaterialInstance(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing or empty 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UMaterialInstanceConstant* MaterialInstance = LoadMaterialInstanceFromPath(AssetPath);
	if (!MaterialInstance)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load material instance at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}
