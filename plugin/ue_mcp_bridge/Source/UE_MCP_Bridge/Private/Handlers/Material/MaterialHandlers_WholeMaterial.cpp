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
			W->SetStringField(TEXT("from"), In->Expression->GetName());
			W->SetNumberField(TEXT("fromOutput"), In->OutputIndex);
			W->SetStringField(TEXT("to"), Expr->GetName());
			W->SetNumberField(TEXT("toInput"), InputIdx);
			WireArr.Add(MakeShared<FJsonValueObject>(W));
		}
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetArrayField(TEXT("nodes"), NodesArr);
	Result->SetArrayField(TEXT("connections"), WireArr);
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

	UMaterial* Material = LoadMaterialFromPath(AssetPath);
	if (!Material) return MCPError(FString::Printf(TEXT("Material not found: %s"), *AssetPath));

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
	auto Abort = [&](const FString& Message) -> TSharedPtr<FJsonValue>
	{
		for (UMaterialExpression* Expr : CreatedNodes)
		{
			UMaterialEditingLibrary::DeleteMaterialExpression(Material, Expr);
		}
		Material->PostEditChange();
		return MCPError(Message + TEXT(" Nothing was kept: the nodes this call created were removed again."));
	};

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
			for (const auto& Pair : (*Props)->Values)
			{
				FProperty* Prop = Expr->GetClass()->FindPropertyByName(FName(*Pair.Key));
				if (!Prop)
				{
					for (TFieldIterator<FProperty> It(Expr->GetClass()); It; ++It)
					{
						if (It->GetName().Equals(FString(Pair.Key), ESearchCase::IgnoreCase)) { Prop = *It; break; }
					}
				}
				if (!Prop) return Abort(FString::Printf(TEXT("Node '%s' (%s) has no property '%s'."), *Spec.Name, *Spec.Class->GetName(), *Pair.Key));
				FString SetError;
				if (!MCPJsonProperty::SetJsonOnProperty(Prop, Prop->ContainerPtrToValuePtr<void>(Expr), Pair.Value, SetError))
				{
					return Abort(FString::Printf(TEXT("Node '%s' property '%s': %s."), *Spec.Name, *Pair.Key, *SetError));
				}
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

	Material->PostEditChange();
	Material->MarkPackageDirty();
	FString SaveError;
	const bool bSaved = SaveAssetPackageChecked(Material, SaveError);

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
	MCPNoteSaveOutcome(Result, AssetPath, bSaved, SaveError);
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("expressionsCreated"), Specs.Num());
	Result->SetNumberField(TEXT("wiresMade"), Wires);
	Result->SetNumberField(TEXT("connectionsMade"), Connections);
	Result->SetArrayField(TEXT("createdExpressions"), CreatedExpressionNames);
	Result->SetArrayField(TEXT("nodes"), NodeMap);
	Result->SetArrayField(TEXT("overwrittenProperties"), OverwrittenProperties);

	// One rollback record carries one call, and this created many nodes.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
		TEXT("No single call undoes this: %d expression(s) were created, %d wire(s) and %d property connection(s) written. Undo it with one delete_material_expression per name in createdExpressions, then reconnect anything listed in overwrittenProperties with connect_to_material_property. Wrapping the build in begin_transaction / end_transaction gives a one-step undo instead."),
		Specs.Num(), Wires, Connections));
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FMaterialHandlers::RenderMaterialPreview(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
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
