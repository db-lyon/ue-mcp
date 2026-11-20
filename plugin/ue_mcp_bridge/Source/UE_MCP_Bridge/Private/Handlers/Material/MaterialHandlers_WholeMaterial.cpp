// Split from MaterialHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FMaterialHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in MaterialHandlers.cpp::RegisterHandlers.

#include "MaterialHandlers.h"
#include "UE_MCP_BridgeModule.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
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
#include "Materials/MaterialExpressionCustomOutput.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "Materials/MaterialExpressionCustom.h"
#include "MaterialShared.h"
#include "Misc/App.h"
#include "JsonSerializer.h"
#include "MaterialEditingLibrary.h"
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
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	UMaterial* Material = LoadMaterialFromPath(AssetPath);
	if (!Material) return MCPError(FString::Printf(TEXT("Material not found: %s"), *AssetPath));

	TArray<TSharedPtr<FJsonValue>> Issues;

	const TSet<UMaterialExpression*> Referenced = CollectReachableExpressions(Material);

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


TSet<UMaterialExpression*> FMaterialHandlers::CollectReachableExpressions(UMaterial* Material)
{
	TSet<UMaterialExpression*> Referenced;
	if (!Material) return Referenced;

	// Roots: every material property input, plus custom output nodes (RVT
	// output, vertex interpolators' outputs and the like) that feed the
	// compile without a property wire.
	TArray<UMaterialExpression*> Stack;
	for (int32 PropIdx = 0; PropIdx < MP_MAX; ++PropIdx)
	{
		FExpressionInput* In = Material->GetExpressionInputForProperty((EMaterialProperty)PropIdx);
		if (In && In->Expression && !Referenced.Contains(In->Expression))
		{
			Referenced.Add(In->Expression);
			Stack.Add(In->Expression);
		}
	}
	for (UMaterialExpression* Expr : Material->GetExpressions())
	{
		if (Expr && Expr->IsA<UMaterialExpressionCustomOutput>() && !Referenced.Contains(Expr))
		{
			Referenced.Add(Expr);
			Stack.Add(Expr);
		}
	}

	auto Visit = [&Referenced, &Stack](UMaterialExpression* Expr)
	{
		if (Expr && !Referenced.Contains(Expr))
		{
			Referenced.Add(Expr);
			Stack.Add(Expr);
		}
	};
	while (Stack.Num() > 0)
	{
		UMaterialExpression* Expr = Stack.Pop();
		// A named reroute usage reads its declaration by reference, not by wire.
		if (UMaterialExpressionNamedRerouteUsage* Usage = Cast<UMaterialExpressionNamedRerouteUsage>(Expr))
		{
			Visit(Usage->Declaration.Get());
		}
#if UE_MCP_HAS_5_5_API
		for (FExpressionInputIterator It{ Expr }; It; ++It)
		{
			Visit(It->Expression);
		}
#else
		for (int32 InputIdx = 0, InputCount = Expr->GetInputs().Num(); InputIdx < InputCount; ++InputIdx)
		{
			FExpressionInput* In = Expr->GetInput(InputIdx);
			if (In) Visit(In->Expression);
		}
#endif
	}
	return Referenced;
}


TSharedPtr<FJsonValue> FMaterialHandlers::DeleteOrphanExpressions(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);

	UMaterial* Material = LoadMaterialFromPath(AssetPath);
	if (!Material) return MCPError(FString::Printf(TEXT("Material not found: %s"), *AssetPath));

	const TSet<UMaterialExpression*> Reachable = CollectReachableExpressions(Material);
	TArray<UMaterialExpression*> Orphans;
	TArray<TSharedPtr<FJsonValue>> OrphanJson;
	for (UMaterialExpression* Expr : Material->GetExpressions())
	{
		if (!Expr || Reachable.Contains(Expr)) continue;
		Orphans.Add(Expr);
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("expression"), Expr->GetName());
		Row->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
		if (!Expr->Desc.IsEmpty()) Row->SetStringField(TEXT("description"), Expr->Desc);
		OrphanJson.Add(MakeShared<FJsonValueObject>(Row));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), Material->GetPathName());
	Result->SetArrayField(TEXT("orphans"), OrphanJson);
	Result->SetNumberField(TEXT("orphanCount"), Orphans.Num());
	Result->SetBoolField(TEXT("dryRun"), bDryRun);

	if (bDryRun || Orphans.Num() == 0)
	{
		Result->SetNumberField(TEXT("deletedCount"), 0);
		if (Orphans.Num() == 0) Result->SetBoolField(TEXT("unchanged"), true);
		return MCPResult(Result);
	}

	for (UMaterialExpression* Expr : Orphans)
	{
		UMaterialEditingLibrary::DeleteMaterialExpression(Material, Expr);
	}
	UMaterialEditingLibrary::RecompileMaterial(Material);
	Material->MarkPackageDirty();
	FString SaveError;
	const bool bSaved = SaveAssetPackageChecked(Material, SaveError);
	MCPNoteSaveOutcome(Result, Material->GetPathName(), bSaved, SaveError);
	MCPSetUpdated(Result);
	Result->SetNumberField(TEXT("deletedCount"), Orphans.Num());
	Result->SetNumberField(TEXT("expressionCount"), Material->GetExpressions().Num());
	MCPSetNoRollback(Result, TEXT("Deleted expressions are not recorded for replay; dryRun=true previews the list, and an editor undo step restores them."));
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FMaterialHandlers::GetMaterialShaderStats(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

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
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	UMaterial* Material = LoadMaterialFromPath(AssetPath);
	if (!Material) return MCPError(FString::Printf(TEXT("Material not found: %s"), *AssetPath));

	auto AllExpressions = Material->GetExpressions();

	// #1951: a node is named by its description when that is unique, which is
	// the name build_material_graph gave it, so an export of an imported graph
	// names its nodes the way the first export did. Otherwise by engine name.
	TMap<const UMaterialExpression*, FString> NodeNames;
	{
		TMap<FString, int32> DescCount;
		TSet<FString> EngineNames;
		for (UMaterialExpression* Expr : AllExpressions)
		{
			if (!Expr) continue;
			EngineNames.Add(Expr->GetName());
			if (!Expr->Desc.IsEmpty()) DescCount.FindOrAdd(Expr->Desc)++;
		}
		for (UMaterialExpression* Expr : AllExpressions)
		{
			if (!Expr) continue;
			const bool bUseDesc = !Expr->Desc.IsEmpty() && DescCount.FindRef(Expr->Desc) == 1
				&& (!EngineNames.Contains(Expr->Desc) || Expr->Desc == Expr->GetName());
			NodeNames.Add(Expr, bUseDesc ? Expr->Desc : Expr->GetName());
		}
	}
	auto NameOf = [&NodeNames](const UMaterialExpression* Expr) { return NodeNames.Contains(Expr) ? NodeNames[Expr] : Expr->GetName(); };

	// Written as first-class fields, or derived from them, so not repeated in properties.
	static const TSet<FName> SkippedProperties = {
		TEXT("Desc"), TEXT("MaterialExpressionEditorX"), TEXT("MaterialExpressionEditorY"),
	};
	static const TSet<FName> CustomFieldProperties = {
		TEXT("Code"), TEXT("Description"), TEXT("OutputType"), TEXT("Inputs"), TEXT("AdditionalOutputs"),
		TEXT("AdditionalDefines"), TEXT("IncludeFilePaths"),
	};

	TArray<TSharedPtr<FJsonValue>> NodesArr;
	for (UMaterialExpression* Expr : AllExpressions)
	{
		if (!Expr) continue;
		TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
		Node->SetStringField(TEXT("name"), NameOf(Expr));
		Node->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
		Node->SetNumberField(TEXT("posX"), Expr->MaterialExpressionEditorX);
		Node->SetNumberField(TEXT("posY"), Expr->MaterialExpressionEditorY);

		// Every edited property that differs from the class default, in the
		// shape the properties map of a node spec takes back.
		const UObject* ClassDefault = Expr->GetClass()->GetDefaultObject();
		TSharedPtr<FJsonObject> EditedProps = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Expr->GetClass()); It; ++It)
		{
			FProperty* Prop = *It;
			if (!Prop->HasAnyPropertyFlags(CPF_Edit) || Prop->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_Deprecated)) continue;
			if (SkippedProperties.Contains(Prop->GetFName()) || MaterialPropertyHoldsExpressionInput(Prop)) continue;
			if (Expr->IsA<UMaterialExpressionCustom>() && CustomFieldProperties.Contains(Prop->GetFName())) continue;
			if (Prop->Identical_InContainer(Expr, ClassDefault)) continue;
			if (Cast<UMaterialExpressionTextureSample>(Expr) && Prop->GetFName() == TEXT("Texture")) continue;
			const TSharedPtr<FJsonValue> Value = FMCPJsonSerializer::SerializeValue(Prop->ContainerPtrToValuePtr<void>(Expr), Prop);
			if (Value.IsValid()) EditedProps->SetField(Prop->GetName(), Value);
		}
		if (EditedProps->Values.Num() > 0) Node->SetObjectField(TEXT("properties"), EditedProps);

		if (const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expr))
		{
			Node->SetStringField(TEXT("code"), Custom->Code);
			if (!Custom->Description.IsEmpty()) Node->SetStringField(TEXT("description"), Custom->Description);
			Node->SetStringField(TEXT("outputType"), CustomOutputTypeName((int32)Custom->OutputType));
			TArray<TSharedPtr<FJsonValue>> Inputs, Outputs, Defines, Includes;
			for (const FCustomInput& In : Custom->Inputs) Inputs.Add(MakeShared<FJsonValueString>(In.InputName.ToString()));
			for (const FCustomOutput& Out : Custom->AdditionalOutputs)
			{
				TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("name"), Out.OutputName.ToString());
				Row->SetStringField(TEXT("type"), CustomOutputTypeName((int32)Out.OutputType));
				Outputs.Add(MakeShared<FJsonValueObject>(Row));
			}
			for (const FCustomDefine& Define : Custom->AdditionalDefines)
			{
				TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("name"), Define.DefineName);
				Row->SetStringField(TEXT("value"), Define.DefineValue);
				Defines.Add(MakeShared<FJsonValueObject>(Row));
			}
			for (const FString& Path : Custom->IncludeFilePaths) Includes.Add(MakeShared<FJsonValueString>(Path));
			Node->SetArrayField(TEXT("inputs"), Inputs);
			if (Outputs.Num() > 0) Node->SetArrayField(TEXT("additionalOutputs"), Outputs);
			if (Defines.Num() > 0) Node->SetArrayField(TEXT("additionalDefines"), Defines);
			if (Includes.Num() > 0) Node->SetArrayField(TEXT("includeFilePaths"), Includes);
		}

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

	// Property connections: every material output build_material_graph can wire.
	TArray<TSharedPtr<FJsonValue>> PropArr;
	for (const TPair<EMaterialProperty, FString>& Pair : WireableMaterialProperties())
	{
		FExpressionInput* In = Material->GetExpressionInputForProperty(Pair.Key);
		if (In && In->Expression)
		{
			TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("property"), Pair.Value);
			P->SetStringField(TEXT("from"), NameOf(In->Expression));
			P->SetNumberField(TEXT("outputIndex"), In->OutputIndex);
			PropArr.Add(MakeShared<FJsonValueObject>(P));
		}
	}

	// Material-level settings that differ from a new material's.
	TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
	const UMaterial* MaterialDefault = GetDefault<UMaterial>();
	for (FProperty* Prop : ExportedMaterialSettings())
	{
		if (Prop->Identical_InContainer(Material, MaterialDefault)) continue;
		const TSharedPtr<FJsonValue> Value = FMCPJsonSerializer::SerializeValue(Prop->ContainerPtrToValuePtr<void>(Material), Prop);
		if (Value.IsValid()) Settings->SetField(Prop->GetName(), Value);
	}

	// Node-to-node wires in build_material_graph's connections shape, so an export imports back.
	TArray<TSharedPtr<FJsonValue>> WireArr;
	for (UMaterialExpression* Expr : AllExpressions)
	{
		if (!Expr) continue;
		for (int32 InputIdx = 0; Expr->GetInput(InputIdx); ++InputIdx)
		{
			const FExpressionInput* In = Expr->GetInput(InputIdx);
			if (!In->Expression) continue;
			TSharedPtr<FJsonObject> W = MakeShared<FJsonObject>();
			W->SetStringField(TEXT("from"), NameOf(In->Expression));
			W->SetNumberField(TEXT("fromOutput"), In->OutputIndex);
			W->SetStringField(TEXT("to"), NameOf(Expr));
			W->SetNumberField(TEXT("toInput"), InputIdx);
			WireArr.Add(MakeShared<FJsonValueObject>(W));
		}
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetArrayField(TEXT("nodes"), NodesArr);
	Result->SetArrayField(TEXT("connections"), WireArr);
	Result->SetArrayField(TEXT("propertyConnections"), PropArr);
	Result->SetObjectField(TEXT("materialSettings"), Settings);
	return MCPResult(Result);
}


TArray<TPair<EMaterialProperty, FString>> FMaterialHandlers::WireableMaterialProperties()
{
	static const TCHAR* Names[] = {
		TEXT("BaseColor"), TEXT("Metallic"), TEXT("Specular"), TEXT("Roughness"), TEXT("Anisotropy"),
		TEXT("EmissiveColor"), TEXT("Opacity"), TEXT("OpacityMask"), TEXT("Normal"), TEXT("Tangent"),
		TEXT("WorldPositionOffset"), TEXT("SubsurfaceColor"), TEXT("CustomData0"), TEXT("CustomData1"),
		TEXT("AmbientOcclusion"), TEXT("Refraction"), TEXT("PixelDepthOffset"), TEXT("ShadingModel"),
		TEXT("FrontMaterial"), TEXT("MaterialAttributes"),
	};
	TArray<TPair<EMaterialProperty, FString>> Out;
	for (const TCHAR* Name : Names)
	{
		EMaterialProperty Prop;
		if (ParseMaterialProperty(Name, Prop)) Out.Add({ Prop, Name });
	}
	return Out;
}

const TArray<FProperty*>& FMaterialHandlers::ExportedMaterialSettings()
{
	static TArray<FProperty*> Props;
	if (Props.Num() == 0)
	{
		static const TCHAR* Names[] = {
			TEXT("MaterialDomain"), TEXT("BlendMode"), TEXT("ShadingModel"), TEXT("TwoSided"), TEXT("bIsThinSurface"),
			TEXT("BlendableLocation"), TEXT("BlendablePriority"), TEXT("bIsBlendable"), TEXT("OpacityMaskClipValue"),
			TEXT("bTangentSpaceNormal"), TEXT("bUseMaterialAttributes"), TEXT("bCastDynamicShadowAsMasked"),
			TEXT("DitheredLODTransition"), TEXT("bAllowNegativeEmissiveColor"), TEXT("TranslucencyLightingMode"),
			TEXT("bEnableResponsiveAA"), TEXT("bScreenSpaceReflections"), TEXT("bUseEmissiveForDynamicArea"),
		};
		for (const TCHAR* Name : Names)
		{
			if (FProperty* Prop = UMaterial::StaticClass()->FindPropertyByName(Name)) Props.Add(Prop);
		}
		// Usage flags: whichever bUsedWith* this engine defines.
		for (TFieldIterator<FProperty> It(UMaterial::StaticClass()); It; ++It)
		{
			if (It->GetName().StartsWith(TEXT("bUsedWith")) && CastField<FBoolProperty>(*It)) Props.AddUnique(*It);
		}
	}
	return Props;
}

bool FMaterialHandlers::MaterialPropertyHoldsExpressionInput(const FProperty* Prop)
{
	if (const FArrayProperty* Arr = CastField<FArrayProperty>(Prop)) return MaterialPropertyHoldsExpressionInput(Arr->Inner);
	const FStructProperty* StructProp = CastField<FStructProperty>(Prop);
	if (!StructProp || !StructProp->Struct) return false;
	for (const UStruct* S = StructProp->Struct; S; S = S->GetSuperStruct())
	{
		const FString Name = S->GetName();
		if (Name.EndsWith(TEXT("ExpressionInput")) || Name.EndsWith(TEXT("MaterialInput"))) return true;
	}
	for (TFieldIterator<FProperty> It(StructProp->Struct); It; ++It)
	{
		if (MaterialPropertyHoldsExpressionInput(*It)) return true;
	}
	return false;
}


bool FMaterialHandlers::CompileMaterialForErrors(UMaterial* Material, TArray<FString>& OutErrors)
{
	OutErrors.Reset();
	// A -nullrhi editor compiles no shaders, so an empty list there says nothing.
	if (!FApp::CanEverRender())
	{
		UMaterialEditingLibrary::RecompileMaterial(Material);
		return false;
	}
#if UE_MCP_HAS_5_8_API
	OutErrors = UMaterialEditingLibrary::RecompileMaterial(Material);
	return true;
#else
	// RecompileMaterial returns void before 5.8; the resource holds the errors
	// once its compile has finished.
	UMaterialEditingLibrary::RecompileMaterial(Material);
	if (FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform))
	{
		Resource->FinishCompilation();
		OutErrors = Resource->GetCompileErrors();
		return true;
	}
	return false;
#endif
}


// #1951: replace the whole graph from a spec, saving only on a clean compile.
// The spec is built into a transient copy first, so a spec that fails to
// build or compile leaves the material exactly as it was.
static TSharedPtr<FJsonValue> MaterialGraphReplaceFailure(const FString& Message, const TArray<FString>& Errors)
{
	TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
	Error->SetBoolField(TEXT("success"), false);
	Error->SetStringField(TEXT("error"), Message);
	TArray<TSharedPtr<FJsonValue>> ErrorValues;
	for (const FString& E : Errors) ErrorValues.Add(MakeShared<FJsonValueString>(E));
	Error->SetArrayField(TEXT("compileErrors"), ErrorValues);
	return MakeShared<FJsonValueObject>(Error);
}

TSharedPtr<FJsonValue> FMaterialHandlers::ReplaceMaterialGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UMaterial* Material = LoadMaterialFromPath(AssetPath);
	if (!Material) return MCPError(FString::Printf(TEXT("Material not found: %s"), *AssetPath));

	// The graph as it stands, which is what the rollback replaces it with.
	TSharedPtr<FJsonObject> ExportParams = MakeShared<FJsonObject>();
	ExportParams->SetStringField(TEXT("assetPath"), AssetPath);
	const TSharedPtr<FJsonValue> PreviousValue = ExportMaterialGraph(ExportParams);
	const TSharedPtr<FJsonObject> Previous = PreviousValue.IsValid() ? PreviousValue->AsObject() : nullptr;

	auto BuildOk = [](const TSharedPtr<FJsonValue>& Value, FString& OutError) -> bool
	{
		const TSharedPtr<FJsonObject> Obj = Value.IsValid() ? Value->AsObject() : nullptr;
		bool bOk = false;
		if (Obj.IsValid() && Obj->TryGetBoolField(TEXT("success"), bOk) && bOk) return true;
		if (Obj.IsValid()) Obj->TryGetStringField(TEXT("error"), OutError);
		return false;
	};

	UMaterial* Trial = DuplicateObject<UMaterial>(Material, GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UMaterial::StaticClass(), TEXT("MCPReplaceTrial")));
	if (!Trial) return MCPError(TEXT("Could not make a trial copy of the material. Nothing was changed."));
	UMaterialEditingLibrary::DeleteAllMaterialExpressions(Trial);
	FString BuildError;
	TArray<FString> TrialErrors;
	const bool bTrialBuilt = BuildOk(BuildMaterialGraphInto(Params, Trial, false), BuildError);
	const bool bErrorsKnown = bTrialBuilt && CompileMaterialForErrors(Trial, TrialErrors);
	Trial->ClearFlags(RF_Public | RF_Standalone);
	Trial->MarkAsGarbage();
	if (!bTrialBuilt)
	{
		return MCPError(FString::Printf(TEXT("%s The material was not changed."), *BuildError));
	}
	if (TrialErrors.Num() > 0)
	{
		return MaterialGraphReplaceFailure(FString::Printf(TEXT("The replacement graph does not compile (%d error(s), in compileErrors). The material was not changed."), TrialErrors.Num()), TrialErrors);
	}

	UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);
	const TSharedPtr<FJsonValue> Built = BuildMaterialGraphInto(Params, Material, false);
	if (!BuildOk(Built, BuildError))
	{
		return MCPError(FString::Printf(TEXT("The replacement built on a trial copy but not on the material: %s The material's expressions were deleted and it was not saved; reload it from disk to discard the change."), *BuildError));
	}
	TArray<FString> Errors;
	CompileMaterialForErrors(Material, Errors);
	if (Errors.Num() > 0)
	{
		return MaterialGraphReplaceFailure(TEXT("The replacement compiled on a trial copy but not on the material, so it was not saved; reload the material from disk to discard the change."), Errors);
	}
	FString SaveError;
	const bool bSaved = SaveAssetPackageChecked(Material, SaveError);

	TSharedPtr<FJsonObject> Result = Built->AsObject();
	Result->SetBoolField(TEXT("replaced"), true);
	Result->SetBoolField(TEXT("compileChecked"), bErrorsKnown);
	Result->SetArrayField(TEXT("compileErrors"), TArray<TSharedPtr<FJsonValue>>());
	if (!bErrorsKnown) Result->SetStringField(TEXT("compileErrorsNote"), TEXT("This editor compiled no shaders (a -nullrhi session, or an engine that does not report the errors), so the replacement was saved without the compile check. recompile_material in a rendering editor reports them."));
	MCPNoteSaveOutcome(Result, AssetPath, bSaved, SaveError);
	Result->SetStringField(TEXT("aliasOf"), TEXT("build_material_graph"));
	Result->RemoveField(TEXT("rollbackPossible"));
	Result->RemoveField(TEXT("rollbackNote"));
	if (Previous.IsValid())
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetBoolField(TEXT("replace"), true);
		for (const TCHAR* Key : { TEXT("nodes"), TEXT("connections"), TEXT("propertyConnections") })
		{
			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (Previous->TryGetArrayField(Key, Arr)) Payload->SetArrayField(Key, *Arr);
		}
		const TSharedPtr<FJsonObject>* PrevSettings = nullptr;
		if (Previous->TryGetObjectField(TEXT("materialSettings"), PrevSettings)) Payload->SetObjectField(TEXT("materialSettings"), *PrevSettings);
		MCPSetRollback(Result, TEXT("import_material_graph"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), TEXT("The rollback replaces the graph with export_material_graph's reading of it before this call: every node, edited property, wire, property connection and listed material setting. Material settings this call changed that export does not list are not restored."));
	}
	else
	{
		MCPSetNoRollback(Result, TEXT("The previous graph could not be exported before the replace, so there is nothing to restore it from."));
	}
	return MakeShared<FJsonValueObject>(Result);
}


TSharedPtr<FJsonValue> FMaterialHandlers::ImportMaterialGraph(const TSharedPtr<FJsonObject>& Params)
{
	if (OptionalBool(Params, TEXT("replace"), false))
	{
		return ReplaceMaterialGraph(Params);
	}
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
				TEXT("import_material_graph is build_material_graph under another name, and it ADDS to the graph rather than replacing it: there is no replace mode that could be handed a snapshot of the previous graph, so no single call undoes it. %s"),
				*DelegatedNote));
		}
	}
	return Delegated;
}


bool FMaterialHandlers::ApplyExpressionProperties(UMaterialExpression* Expression, const TSharedPtr<FJsonObject>& Properties, FString& OutError)
{
	if (!Expression || !Properties.IsValid()) return true;
	// #1946: each write is a details-panel edit (Modify, PreEditChange, write,
	// PostEditChangeProperty), so a node that derives state from a property
	// rebuilds it: a CollectionParameter looks its ParameterId up in the
	// collection, a Custom node rebuilds its outputs. The notifications run
	// after every property of the node is written, because a derivation can
	// read a sibling: the id lookup needs Collection and ParameterName both.
	TArray<FProperty*> Written;
	OutError.Reset();
	Expression->Modify();
	for (const auto& Pair : Properties->Values)
	{
		const FString Key(Pair.Key);
		FProperty* Prop = Expression->GetClass()->FindPropertyByName(FName(*Key));
		if (!Prop)
		{
			for (TFieldIterator<FProperty> It(Expression->GetClass()); It; ++It)
			{
				if (It->GetName().Equals(Key, ESearchCase::IgnoreCase)) { Prop = *It; break; }
			}
		}
		if (!Prop)
		{
			OutError = FString::Printf(TEXT("%s has no property '%s'"), *Expression->GetClass()->GetName(), *Key);
			break;
		}
		FString SetError;
		Expression->PreEditChange(Prop);
		Written.Add(Prop);
		if (!MCPJsonProperty::SetJsonOnProperty(Prop, Prop->ContainerPtrToValuePtr<void>(Expression), Pair.Value, SetError))
		{
			OutError = FString::Printf(TEXT("property '%s': %s"), *Key, *SetError);
			break;
		}
	}
	// Every PreEditChange is balanced, on failure too.
	for (FProperty* Prop : Written)
	{
		FPropertyChangedEvent Changed(Prop, EPropertyChangeType::ValueSet);
		Expression->PostEditChangeProperty(Changed);
	}
	return OutError.IsEmpty();
}


bool FMaterialHandlers::ParseCustomOutputType(const FString& Text, int32& OutTypeValue)
{
	ECustomMaterialOutputType OutType = CMOT_Float1;
	const FString L = Text.ToLower();
	if (L == TEXT("float1") || L == TEXT("cmot_float1")) OutType = CMOT_Float1;
	else if (L == TEXT("float2") || L == TEXT("cmot_float2")) OutType = CMOT_Float2;
	else if (L == TEXT("float3") || L == TEXT("cmot_float3")) OutType = CMOT_Float3;
	else if (L == TEXT("float4") || L == TEXT("cmot_float4")) OutType = CMOT_Float4;
	else if (L.Contains(TEXT("materialattributes"))) OutType = CMOT_MaterialAttributes;
	else return false;
	OutTypeValue = (int32)OutType;
	return true;
}

FString FMaterialHandlers::CustomOutputTypeName(int32 TypeValue)
{
	switch ((ECustomMaterialOutputType)TypeValue)
	{
	case CMOT_Float1: return TEXT("float1");
	case CMOT_Float2: return TEXT("float2");
	case CMOT_Float3: return TEXT("float3");
	case CMOT_Float4: return TEXT("float4");
	case CMOT_MaterialAttributes: return TEXT("materialattributes");
	default: return FString();
	}
}

const TArray<FString>& FMaterialHandlers::CustomNodeSpecKeys()
{
	static const TArray<FString> Keys = {
		TEXT("code"), TEXT("inputs"), TEXT("outputType"), TEXT("additionalOutputs"), TEXT("additionalDefines"), TEXT("includeFilePaths"),
	};
	return Keys;
}

bool FMaterialHandlers::ApplyCustomNodeSpec(UMaterialExpressionCustom* Custom, const TSharedPtr<FJsonObject>& Spec, FString& OutError)
{
	if (!Custom || !Spec.IsValid()) return true;
	// An entry is a bare name or an object carrying one under name (or the
	// engine's own field name).
	auto EntryName = [](const TSharedPtr<FJsonValue>& V, const TCHAR* EngineKey, FString& Out) -> TSharedPtr<FJsonObject>
	{
		Out.Reset();
		if (!V.IsValid()) return nullptr;
		if (V->TryGetString(Out)) return nullptr;
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (V->TryGetObject(Obj) && Obj && (*Obj).IsValid())
		{
			if (!(*Obj)->TryGetStringField(TEXT("name"), Out)) (*Obj)->TryGetStringField(EngineKey, Out);
			return *Obj;
		}
		return nullptr;
	};

	Custom->Modify();
	TArray<FName> Changed;
	FString Text;
	if (Spec->TryGetStringField(TEXT("code"), Text)) { Custom->Code = Text; Changed.Add(TEXT("Code")); }
	if (Spec->TryGetStringField(TEXT("description"), Text)) { Custom->Description = Text; Changed.Add(TEXT("Description")); }
	if (Spec->TryGetStringField(TEXT("outputType"), Text))
	{
		int32 Type = 0;
		if (!ParseCustomOutputType(Text, Type))
		{
			OutError = FString::Printf(TEXT("outputType '%s' is not float1, float2, float3, float4 or materialAttributes"), *Text);
			return false;
		}
		Custom->OutputType = (ECustomMaterialOutputType)Type;
		Changed.Add(TEXT("OutputType"));
	}
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Spec->TryGetArrayField(TEXT("inputs"), Arr))
	{
		TArray<FCustomInput> Inputs;
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			FString Name;
			EntryName((*Arr)[i], TEXT("InputName"), Name);
			if (Name.IsEmpty()) { OutError = FString::Printf(TEXT("inputs[%d] needs a name"), i); return false; }
			FCustomInput& In = Inputs.AddDefaulted_GetRef();
			In.InputName = FName(*Name);
		}
		Custom->Inputs = MoveTemp(Inputs);
		Changed.Add(TEXT("Inputs"));
	}
	if (Spec->TryGetArrayField(TEXT("additionalOutputs"), Arr))
	{
		TArray<FCustomOutput> Outputs;
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			FString Name;
			const TSharedPtr<FJsonObject> Obj = EntryName((*Arr)[i], TEXT("OutputName"), Name);
			if (Name.IsEmpty()) { OutError = FString::Printf(TEXT("additionalOutputs[%d] needs a name"), i); return false; }
			FCustomOutput& Out = Outputs.AddDefaulted_GetRef();
			Out.OutputName = FName(*Name);
			int32 Type = (int32)CMOT_Float1;
			FString TypeText;
			if (Obj.IsValid() && (Obj->TryGetStringField(TEXT("type"), TypeText) || Obj->TryGetStringField(TEXT("OutputType"), TypeText)))
			{
				if (!ParseCustomOutputType(TypeText, Type))
				{
					OutError = FString::Printf(TEXT("additionalOutputs[%d] type '%s' is not float1, float2, float3, float4 or materialAttributes"), i, *TypeText);
					return false;
				}
			}
			Out.OutputType = (ECustomMaterialOutputType)Type;
		}
		Custom->AdditionalOutputs = MoveTemp(Outputs);
		Changed.Add(TEXT("AdditionalOutputs"));
	}
	if (Spec->TryGetArrayField(TEXT("additionalDefines"), Arr))
	{
		TArray<FCustomDefine> Defines;
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			FString Name, Value;
			const TSharedPtr<FJsonObject> Obj = EntryName((*Arr)[i], TEXT("DefineName"), Name);
			if (Obj.IsValid())
			{
				if (!Obj->TryGetStringField(TEXT("value"), Value)) Obj->TryGetStringField(TEXT("DefineValue"), Value);
			}
			else if (Name.Split(TEXT("="), &Name, &Value)) {}
			if (Name.IsEmpty()) { OutError = FString::Printf(TEXT("additionalDefines[%d] needs a name ({name, value} or 'NAME=VALUE')"), i); return false; }
			FCustomDefine& Define = Defines.AddDefaulted_GetRef();
			Define.DefineName = Name;
			Define.DefineValue = Value;
		}
		Custom->AdditionalDefines = MoveTemp(Defines);
		Changed.Add(TEXT("AdditionalDefines"));
	}
	if (Spec->TryGetArrayField(TEXT("includeFilePaths"), Arr))
	{
		TArray<FString> Paths;
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			FString Path;
			if (!(*Arr)[i].IsValid() || !(*Arr)[i]->TryGetString(Path) || Path.IsEmpty())
			{
				OutError = FString::Printf(TEXT("includeFilePaths[%d] must be a virtual shader path such as /Project/Shaders/Grade.ush"), i);
				return false;
			}
			Paths.Add(Path);
		}
		Custom->IncludeFilePaths = MoveTemp(Paths);
		Changed.Add(TEXT("IncludeFilePaths"));
	}

	// The node's own notification strips spaces from input names, rebuilds its
	// outputs from AdditionalOutputs and reconstructs an open editor's pins.
	for (const FName& Name : Changed)
	{
		FProperty* Prop = UMaterialExpressionCustom::StaticClass()->FindPropertyByName(Name);
		FPropertyChangedEvent Event(Prop, EPropertyChangeType::ValueSet);
		Custom->PostEditChangeProperty(Event);
	}
	return true;
}


TSharedPtr<FJsonValue> FMaterialHandlers::BuildMaterialGraph(const TSharedPtr<FJsonObject>& Params)
{
	return BuildMaterialGraphInto(Params, nullptr, true);
}


TSharedPtr<FJsonValue> FMaterialHandlers::BuildMaterialGraphInto(const TSharedPtr<FJsonObject>& Params, UMaterial* Target, bool bSave)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
	if (!TryGetArrayParam(Params, TEXT("nodes"), NodesArr) || !NodesArr)
	{
		return MCPError(TEXT("Missing 'nodes' array"));
	}
	// Read before anything can fail (#1057).
	const TArray<TSharedPtr<FJsonValue>>* PropArr = nullptr;
	const bool bHasPropertyConnections = TryGetArrayParam(Params, TEXT("propertyConnections"), PropArr) && PropArr;
	const TArray<TSharedPtr<FJsonValue>>* ConnArr = nullptr;
	const bool bHasConnections = TryGetArrayParam(Params, TEXT("connections"), ConnArr) && ConnArr;
	const TArray<TSharedPtr<FJsonValue>>* RemoveArr = nullptr;
	const bool bHasRemovals = TryGetArrayParam(Params, TEXT("removeExpressions"), RemoveArr) && RemoveArr;
	const TSharedPtr<FJsonObject>* SettingsObj = nullptr;
	const bool bHasSettings = TryGetObjectParam(Params, TEXT("materialSettings"), SettingsObj) && SettingsObj && (*SettingsObj).IsValid();

	UMaterial* Material = Target ? Target : LoadMaterialFromPath(AssetPath);
	if (!Material) return MCPError(FString::Printf(TEXT("Material not found: %s"), *AssetPath));

	// #1951: material-level settings by property name, checked before anything changes.
	TArray<TPair<FProperty*, TSharedPtr<FJsonValue>>> Settings;
	if (bHasSettings)
	{
		for (const auto& Pair : (*SettingsObj)->Values)
		{
			FProperty* Prop = UMaterial::StaticClass()->FindPropertyByName(FName(*Pair.Key));
			if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit))
			{
				return MCPError(FString::Printf(TEXT("materialSettings: UMaterial has no editable property '%s' (export_material_graph lists the ones it writes). Nothing was changed."), *Pair.Key));
			}
			Settings.Add({ Prop, Pair.Value });
		}
	}

	// #1461: resolved before any node exists, so a new node's description cannot
	// shadow the expression being replaced. Deleted last, once every wire is in.
	TArray<UMaterialExpression*> ToRemove;
	if (bHasRemovals)
	{
		for (int32 i = 0; i < RemoveArr->Num(); ++i)
		{
			FString RemoveName;
			if (!(*RemoveArr)[i].IsValid() || !(*RemoveArr)[i]->TryGetString(RemoveName) || RemoveName.IsEmpty())
			{
				return MCPError(FString::Printf(TEXT("removeExpressions[%d] must be a non-empty expression name"), i));
			}
			UMaterialExpression* Existing = FindExpressionByName(Material, RemoveName);
			if (!Existing)
			{
				return MCPError(FString::Printf(TEXT("removeExpressions[%d]: no expression '%s' in %s (read_graph lists them). Nothing was changed."), i, *RemoveName, *AssetPath));
			}
			ToRemove.AddUnique(Existing);
		}
	}

	// Preflight every node and property name, so a typo fails before the graph changes.
	struct FNodeSpec { FString Name; UClass* Class; TSharedPtr<FJsonObject> Obj; };
	TArray<FNodeSpec> Specs;
	TSet<FString> SpecNames;
	for (int32 i = 0; i < NodesArr->Num(); ++i)
	{
		const TSharedPtr<FJsonObject>* NodeObj = nullptr;
		if (!(*NodesArr)[i].IsValid() || !(*NodesArr)[i]->TryGetObject(NodeObj) || !NodeObj)
		{
			return MCPError(FString::Printf(TEXT("nodes[%d] is not an object"), i));
		}
		FString Name, ClassName;
		(*NodeObj)->TryGetStringField(TEXT("name"), Name);
		(*NodeObj)->TryGetStringField(TEXT("class"), ClassName);
		if (Name.IsEmpty() || ClassName.IsEmpty())
		{
			return MCPError(FString::Printf(TEXT("nodes[%d] needs name and class"), i));
		}
		if (SpecNames.Contains(Name))
		{
			return MCPError(FString::Printf(TEXT("nodes[%d]: duplicate node name '%s'"), i, *Name));
		}
		UClass* Cls = ResolveExpressionClass(ClassName);
		if (!Cls)
		{
			return MCPError(FString::Printf(TEXT("nodes[%d] '%s': unknown expression class '%s' (list_expression_types lists them)"), i, *Name, *ClassName));
		}
		if (!Cls->IsChildOf(UMaterialExpressionCustom::StaticClass()))
		{
			for (const FString& Key : CustomNodeSpecKeys())
			{
				if ((*NodeObj)->HasField(Key))
				{
					return MCPError(FString::Printf(TEXT("nodes[%d] '%s': %s applies to a Custom node, and this one is %s"), i, *Name, *Key, *Cls->GetName()));
				}
			}
		}
		SpecNames.Add(Name);
		Specs.Add({ Name, Cls, *NodeObj });
	}
	if (bHasPropertyConnections)
	{
		for (int32 i = 0; i < PropArr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* ConnObj = nullptr;
			FString PropName;
			EMaterialProperty Prop;
			if (!(*PropArr)[i].IsValid() || !(*PropArr)[i]->TryGetObject(ConnObj) || !ConnObj
				|| !(*ConnObj)->TryGetStringField(TEXT("property"), PropName) || !ParseMaterialProperty(PropName, Prop))
			{
				return MCPError(FString::Printf(TEXT("propertyConnections[%d]: needs a known material property (BaseColor, Metallic, Roughness, Normal, EmissiveColor, Opacity, OpacityMask, AmbientOcclusion and the rest)"), i));
			}
		}
	}

	Material->PreEditChange(nullptr);
	TMap<FString, UMaterialExpression*> ByName;
	TArray<UMaterialExpression*> CreatedNodes;
	TArray<TPair<FProperty*, FString>> PreviousSettings;
	auto Abort = [&](const FString& Message) -> TSharedPtr<FJsonValue>
	{
		for (UMaterialExpression* Expr : CreatedNodes)
		{
			UMaterialEditingLibrary::DeleteMaterialExpression(Material, Expr);
		}
		for (const TPair<FProperty*, FString>& Previous : PreviousSettings)
		{
			Previous.Key->ImportText_InContainer(*Previous.Value, Material, Material, PPF_None);
		}
		Material->PostEditChange();
		return MCPError(Message + TEXT(" Nothing was kept: the nodes this call created were removed again."));
	};

	// Settings first, so a MaterialAttributes wire below finds bUseMaterialAttributes on.
	for (const TPair<FProperty*, TSharedPtr<FJsonValue>>& Setting : Settings)
	{
		FString PreviousText;
		Setting.Key->ExportTextItem_InContainer(PreviousText, Material, nullptr, Material, PPF_None);
		PreviousSettings.Add({ Setting.Key, PreviousText });
		FString SetError;
		if (!MCPJsonProperty::SetJsonOnProperty(Setting.Key, Setting.Key->ContainerPtrToValuePtr<void>(Material), Setting.Value, SetError))
		{
			return Abort(FString::Printf(TEXT("materialSettings.%s: %s."), *Setting.Key->GetName(), *SetError));
		}
	}

	for (const FNodeSpec& Spec : Specs)
	{
		UMaterialExpression* Expr = NewObject<UMaterialExpression>(Material, Spec.Class, NAME_None, RF_Transactional);
		Material->GetExpressionCollection().AddExpression(Expr);
		CreatedNodes.Add(Expr);
		double Pos = 0.0;
		if (Spec.Obj->TryGetNumberField(TEXT("posX"), Pos)) Expr->MaterialExpressionEditorX = (int32)Pos;
		if (Spec.Obj->TryGetNumberField(TEXT("posY"), Pos)) Expr->MaterialExpressionEditorY = (int32)Pos;
		// The node name becomes its description, so later calls can address it by that name.
		ApplyExpressionSpec(Expr, Spec.Obj);
		if (Expr->Desc.IsEmpty()) Expr->Desc = Spec.Name;

		// #1948: before this call's connections, so they can wire the inputs by name.
		if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expr))
		{
			FString CustomError;
			if (!ApplyCustomNodeSpec(Custom, Spec.Obj, CustomError))
			{
				return Abort(FString::Printf(TEXT("Node '%s': %s."), *Spec.Name, *CustomError));
			}
		}

		FString TexturePath;
		if (Spec.Obj->TryGetStringField(TEXT("texturePath"), TexturePath) && !TexturePath.IsEmpty())
		{
			FObjectPropertyBase* TexProp = CastField<FObjectPropertyBase>(Expr->GetClass()->FindPropertyByName(TEXT("Texture")));
			UTexture* Texture = LoadObject<UTexture>(nullptr, *TexturePath);
			if (!TexProp) return Abort(FString::Printf(TEXT("Node '%s' (%s) has no Texture property for texturePath."), *Spec.Name, *Spec.Class->GetName()));
			if (!Texture) return Abort(FString::Printf(TEXT("Node '%s': texture not found at '%s'."), *Spec.Name, *TexturePath));
			TexProp->SetObjectPropertyValue_InContainer(Expr, Texture);
			if (UMaterialExpressionTextureSample* Sample = Cast<UMaterialExpressionTextureSample>(Expr))
			{
				Sample->AutoSetSampleType();
			}
		}

		const TSharedPtr<FJsonObject>* Props = nullptr;
		if (Spec.Obj->TryGetObjectField(TEXT("properties"), Props) && Props)
		{
			FString PropError;
			if (!ApplyExpressionProperties(Expr, *Props, PropError))
			{
				return Abort(FString::Printf(TEXT("Node '%s': %s."), *Spec.Name, *PropError));
			}
		}
		ByName.Add(Spec.Name, Expr);
	}

	// A connection endpoint names a node in this spec, or an expression already in the material.
	auto FindNode = [&](const FString& Name) -> UMaterialExpression*
	{
		if (UMaterialExpression** Found = ByName.Find(Name)) return *Found;
		return Name.IsEmpty() ? nullptr : FindExpressionByName(Material, Name);
	};
	// A pin is a name or an index, sent as a string or a number.
	auto ReadPin = [](const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, FString& Out)
	{
		double Num = 0.0;
		if (Obj->TryGetStringField(Key, Out)) return;
		if (Obj->TryGetNumberField(Key, Num)) Out = FString::FromInt((int32)Num);
	};

	int32 Wires = 0;
	if (bHasConnections)
	{
		for (int32 i = 0; i < ConnArr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* ConnObj = nullptr;
			if (!(*ConnArr)[i].IsValid() || !(*ConnArr)[i]->TryGetObject(ConnObj) || !ConnObj) return Abort(FString::Printf(TEXT("connections[%d] is not an object."), i));
			FString From, To, FromOutput, ToInput;
			(*ConnObj)->TryGetStringField(TEXT("from"), From);
			(*ConnObj)->TryGetStringField(TEXT("to"), To);
			ReadPin(*ConnObj, TEXT("fromOutput"), FromOutput);
			ReadPin(*ConnObj, TEXT("toInput"), ToInput);
			UMaterialExpression* Source = FindNode(From);
			UMaterialExpression* Target = FindNode(To);
			if (!Source) return Abort(FString::Printf(TEXT("connections[%d]: no node '%s'."), i, *From));
			if (!Target) return Abort(FString::Printf(TEXT("connections[%d]: no node '%s'."), i, *To));
			if (ToRemove.Contains(Source) || ToRemove.Contains(Target))
			{
				return Abort(FString::Printf(TEXT("connections[%d]: '%s' is listed in removeExpressions, so a wire to it would be deleted with it."), i, ToRemove.Contains(Source) ? *From : *To));
			}
			int32 OutIndex = 0, InIndex = 0;
			FString PinError;
			if (!ResolveExpressionOutputIndex(Source, FromOutput, OutIndex, PinError)) return Abort(FString::Printf(TEXT("connections[%d]: %s."), i, *PinError));
			if (!ResolveExpressionInputIndex(Target, ToInput, InIndex, PinError)) return Abort(FString::Printf(TEXT("connections[%d]: %s."), i, *PinError));
			Target->GetInput(InIndex)->Connect(OutIndex, Source);
			++Wires;
		}
	}

	// Property connections overwrite whatever the property carried; the displaced binding is reported.
	int32 Connections = 0;
	TArray<TSharedPtr<FJsonValue>> OverwrittenProperties;
	if (bHasPropertyConnections)
	{
		for (int32 i = 0; i < PropArr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject> ConnObj = (*PropArr)[i]->AsObject();
			const FString PropName = ConnObj->GetStringField(TEXT("property"));
			FString FromName, Output;
			ConnObj->TryGetStringField(TEXT("from"), FromName);
			ReadPin(ConnObj, TEXT("output"), Output);
			if (Output.IsEmpty()) ReadPin(ConnObj, TEXT("outputIndex"), Output);
			EMaterialProperty Prop = MP_BaseColor;
			ParseMaterialProperty(PropName, Prop);
			UMaterialExpression* Source = FindNode(FromName);
			if (!Source) return Abort(FString::Printf(TEXT("propertyConnections[%d]: no node '%s'."), i, *FromName));
			if (ToRemove.Contains(Source))
			{
				return Abort(FString::Printf(TEXT("propertyConnections[%d]: '%s' is listed in removeExpressions."), i, *FromName));
			}
			int32 OutIndex = 0;
			FString PinError;
			if (!ResolveExpressionOutputIndex(Source, Output, OutIndex, PinError)) return Abort(FString::Printf(TEXT("propertyConnections[%d]: %s."), i, *PinError));
			FExpressionInput* In = Material->GetExpressionInputForProperty(Prop);
			if (!In) return Abort(FString::Printf(TEXT("propertyConnections[%d]: '%s' cannot be wired directly."), i, *PropName));
			if (In->Expression)
			{
				TSharedPtr<FJsonObject> Displaced = MakeShared<FJsonObject>();
				Displaced->SetStringField(TEXT("property"), PropName);
				Displaced->SetStringField(TEXT("previousFrom"), In->Expression->GetName());
				Displaced->SetNumberField(TEXT("previousOutputIndex"), In->OutputIndex);
				OverwrittenProperties.Add(MakeShared<FJsonValueObject>(Displaced));
			}
			In->Connect(OutIndex, Source);
			++Connections;
		}
	}

	// Deleting breaks every link to the expression, material outputs included.
	TArray<TSharedPtr<FJsonValue>> RemovedExpressionNames;
	for (UMaterialExpression* Expr : ToRemove)
	{
		RemovedExpressionNames.Add(MakeShared<FJsonValueString>(Expr->GetName()));
		UMaterialEditingLibrary::DeleteMaterialExpression(Material, Expr);
	}

	Material->PostEditChange();
	Material->MarkPackageDirty();
	FString SaveError;
	const bool bSaved = bSave && SaveAssetPackageChecked(Material, SaveError);

	TArray<TSharedPtr<FJsonValue>> CreatedExpressionNames;
	TArray<TSharedPtr<FJsonValue>> NodeMap;
	for (const FNodeSpec& Spec : Specs)
	{
		UMaterialExpression* Expr = ByName.FindRef(Spec.Name);
		CreatedExpressionNames.Add(MakeShared<FJsonValueString>(Expr->GetName()));
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("name"), Spec.Name);
		Row->SetStringField(TEXT("expressionName"), Expr->GetName());
		Row->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
		Row->SetNumberField(TEXT("expressionIndex"), Material->GetExpressions().IndexOfByKey(Expr));
		NodeMap.Add(MakeShared<FJsonValueObject>(Row));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	if (bSave) MCPNoteSaveOutcome(Result, AssetPath, bSaved, SaveError);
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	if (Settings.Num() > 0) Result->SetNumberField(TEXT("settingsApplied"), Settings.Num());
	Result->SetNumberField(TEXT("expressionsCreated"), Specs.Num());
	Result->SetNumberField(TEXT("wiresMade"), Wires);
	Result->SetNumberField(TEXT("connectionsMade"), Connections);
	Result->SetArrayField(TEXT("createdExpressions"), CreatedExpressionNames);
	Result->SetArrayField(TEXT("nodes"), NodeMap);
	Result->SetArrayField(TEXT("overwrittenProperties"), OverwrittenProperties);
	Result->SetNumberField(TEXT("expressionsRemoved"), RemovedExpressionNames.Num());
	Result->SetArrayField(TEXT("removedExpressions"), RemovedExpressionNames);

	// One rollback record carries one call, and this created many nodes.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
		TEXT("No single call undoes this: %d expression(s) were created, %d removed, %d wire(s) and %d property connection(s) written. Undo the additions with one delete_material_expression per name in createdExpressions, then reconnect anything listed in overwrittenProperties with connect_to_material_property; removed expressions are not restorable that way. Wrapping the build in begin_transaction / end_transaction gives a one-step undo instead."),
		Specs.Num(), RemovedExpressionNames.Num(), Wires, Connections));
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FMaterialHandlers::RenderMaterialPreview(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString OutputPath;
	if (auto Err = RequireString(Params, TEXT("outputPath"), OutputPath)) return Err;
	// Read before anything can fail (#1057).
	const int32 Width  = OptionalInt(Params, TEXT("width"), 256);
	const int32 Height = OptionalInt(Params, TEXT("height"), 256);
	{
		FString Resolved;
		if (auto Err = MCPRequireWritablePath(OutputPath, Resolved)) return Err;
		OutputPath = Resolved;
	}

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

	// PNG explicitly: ThumbnailCompressImageArray writes JPEG under the default
	// thumbnail settings, which left a .png file holding JPEG bytes.
	TArray64<uint8> Compressed;
	FImageUtils::PNGCompressImageArray(Width, Height, Pixels, Compressed);

	// Whether the render actually changed anything on disk. A replayed call
	// against an unchanged material produces the same bytes, and saying so is
	// the difference between "wrote a file" and "the file already said this".
	bool bBytesIdentical = false;
	if (bOverwroteExistingFile)
	{
		TArray64<uint8> Existing;
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
