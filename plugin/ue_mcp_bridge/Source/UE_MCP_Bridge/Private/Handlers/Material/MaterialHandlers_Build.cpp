// Split from MaterialHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FMaterialHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in MaterialHandlers.cpp::RegisterHandlers.
//
// #946: building a PBR material out of an imported texture set was a sequence
// of add_expression / connect_to_property / recompile calls, and one step in
// that sequence fails silently. A virtual texture (which is what every UDIM
// set imports as) needs the VIRTUAL variant of its sampler type; assign one to
// a sampler left on SAMPLERTYPE_Color and the connection reports success, then
// the material property comes back empty after the recompile with no error
// anywhere. Selecting that sampler type from the texture, and reporting what
// survived the recompile, is the whole point of this action.

#include "MaterialHandlers.h"
#include "UE_MCP_BridgeModule.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerMaterialCompile.h"
#include "UObject/Class.h"
#include "SceneTypes.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/PackageName.h"
#include "Materials/MaterialInterface.h"
#include "MaterialEditingLibrary.h"
#include "Factories/MaterialFactoryNew.h"
#include "Engine/EngineTypes.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
// FSkeletalMaterial moved out of Engine/SkeletalMesh.h in later UE versions.
#if __has_include("Engine/SkinnedAssetCommon.h")
#include "Engine/SkinnedAssetCommon.h"
#endif
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/UObjectGlobals.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	/** The sampler types that only differ from a plain type by reading a virtual
	 *  texture. Kept as one table so promote and demote cannot disagree. */
	struct FMaterialBuildVirtualSamplerPair
	{
		EMaterialSamplerType Plain;
		EMaterialSamplerType Virtual;
	};

	const FMaterialBuildVirtualSamplerPair GMaterialBuildVirtualSamplerPairs[] =
	{
		{ SAMPLERTYPE_Color,           SAMPLERTYPE_VirtualColor           },
		{ SAMPLERTYPE_Grayscale,       SAMPLERTYPE_VirtualGrayscale       },
		{ SAMPLERTYPE_Alpha,           SAMPLERTYPE_VirtualAlpha           },
		{ SAMPLERTYPE_Normal,          SAMPLERTYPE_VirtualNormal          },
		{ SAMPLERTYPE_Masks,           SAMPLERTYPE_VirtualMasks           },
		{ SAMPLERTYPE_LinearColor,     SAMPLERTYPE_VirtualLinearColor     },
		{ SAMPLERTYPE_LinearGrayscale, SAMPLERTYPE_VirtualLinearGrayscale },
	};

	EMaterialSamplerType MaterialBuildPromoteSamplerToVirtual(EMaterialSamplerType In)
	{
		for (const FMaterialBuildVirtualSamplerPair& Pair : GMaterialBuildVirtualSamplerPairs)
		{
			if (Pair.Plain == In) return Pair.Virtual;
		}
		return In;
	}

	EMaterialSamplerType MaterialBuildDemoteSamplerFromVirtual(EMaterialSamplerType In)
	{
		for (const FMaterialBuildVirtualSamplerPair& Pair : GMaterialBuildVirtualSamplerPairs)
		{
			if (Pair.Virtual == In) return Pair.Plain;
		}
		return In;
	}

	FString MaterialBuildSamplerTypeName(EMaterialSamplerType Type)
	{
		if (UEnum* Enum = StaticEnum<EMaterialSamplerType>())
		{
			return Enum->GetNameStringByValue((int64)Type);
		}
		return FString::FromInt((int32)Type);
	}

	/** Accepts "SAMPLERTYPE_VirtualColor", "VirtualColor", "virtual color" and
	 *  "virtual_color" alike, so an override never has to be spelled the way the
	 *  header happens to spell it. */
	bool MaterialBuildParseSamplerType(const FString& Input, EMaterialSamplerType& OutType)
	{
		FString Normalised = Input;
		Normalised.RemoveFromStart(TEXT("SAMPLERTYPE_"), ESearchCase::IgnoreCase);
		Normalised.ReplaceInline(TEXT("_"), TEXT(""), ESearchCase::CaseSensitive);
		Normalised.ReplaceInline(TEXT(" "), TEXT(""), ESearchCase::CaseSensitive);
		Normalised = Normalised.ToLower();

		UEnum* Enum = StaticEnum<EMaterialSamplerType>();
		if (!Enum) return false;

		for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
		{
			const int64 Value = Enum->GetValueByIndex(Index);
			if (Value == (int64)SAMPLERTYPE_MAX) continue;
			FString Candidate = Enum->GetNameStringByIndex(Index);
			Candidate.RemoveFromStart(TEXT("SAMPLERTYPE_"), ESearchCase::IgnoreCase);
			if (Candidate.ToLower() == Normalised)
			{
				OutType = (EMaterialSamplerType)Value;
				return true;
			}
		}
		return false;
	}

	/** One entry of the requested texture set, after the key has been resolved
	 *  to the material property (or properties) it drives. */
	struct FMaterialBuildSlot
	{
		FString Key;
		FString TexturePath;
		UTexture* Texture = nullptr;
		/** Property + the sampler output name that feeds it. An ORM map fans one
		 *  sample out across three properties, so this is a list. */
		TArray<TPair<EMaterialProperty, FString>> Targets;
		TArray<FString> TargetNames;
	};

	/** Read a colour from {r,g,b,a}, [r,g,b,a] or "(R=..,G=..,B=..,A=..)". */
	bool MaterialBuildReadColor(const TSharedPtr<FJsonValue>& Value, FLinearColor& Out)
	{
		if (!Value.IsValid()) return false;
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (Value->TryGetObject(Obj) && Obj && (*Obj).IsValid())
		{
			double R = 0, G = 0, B = 0, A = 1;
			const bool bAny = (*Obj)->TryGetNumberField(TEXT("r"), R) | (*Obj)->TryGetNumberField(TEXT("g"), G) | (*Obj)->TryGetNumberField(TEXT("b"), B);
			(*Obj)->TryGetNumberField(TEXT("a"), A);
			Out = FLinearColor((float)R, (float)G, (float)B, (float)A);
			return bAny;
		}
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Value->TryGetArray(Arr) && Arr && Arr->Num() >= 3)
		{
			double C[4] = { 0, 0, 0, 1 };
			for (int32 i = 0; i < Arr->Num() && i < 4; ++i) (*Arr)[i]->TryGetNumber(C[i]);
			Out = FLinearColor((float)C[0], (float)C[1], (float)C[2], (float)C[3]);
			return true;
		}
		FString Text;
		return Value->TryGetString(Text) && Out.InitFromString(Text);
	}

	/** Mesh slots named or indexed in a JSON array; empty means every slot. */
	void MaterialBuildReadSlots(const TArray<TSharedPtr<FJsonValue>>* SlotSpec, TArray<FString>& OutNames, TArray<int32>& OutIndices)
	{
		if (!SlotSpec) return;
		for (const TSharedPtr<FJsonValue>& Value : *SlotSpec)
		{
			if (!Value.IsValid()) continue;
			double AsNumber = 0.0;
			FString AsString;
			if (Value->TryGetNumber(AsNumber)) OutIndices.Add((int32)AsNumber);
			else if (Value->TryGetString(AsString) && !AsString.IsEmpty()) OutNames.Add(AsString);
		}
	}

	/** Put Material on the named or indexed slots of a static or skeletal mesh and
	 *  save it. Returns nullptr when MeshPath names neither kind of mesh. */
	TSharedPtr<FJsonObject> MaterialBuildAssignToMesh(const FString& MeshPath, const TArray<FString>& WantedNames, const TArray<int32>& WantedIndices, UMaterialInterface* Material)
	{
		const bool bAllSlots = WantedNames.Num() == 0 && WantedIndices.Num() == 0;
		auto SlotWanted = [&](int32 Index, const FName& SlotName) -> bool
		{
			if (bAllSlots || WantedIndices.Contains(Index)) return true;
			for (const FString& Name : WantedNames)
			{
				if (SlotName.ToString().Equals(Name, ESearchCase::IgnoreCase)) return true;
			}
			return false;
		};

		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Assigned;
		auto Record = [&Assigned](int32 Index, const FName& SlotName)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("slotIndex"), Index);
			Entry->SetStringField(TEXT("slotName"), SlotName.ToString());
			Assigned.Add(MakeShared<FJsonValueObject>(Entry));
		};

		UObject* Mesh = nullptr;
		if (UStaticMesh* StaticMesh = LoadAssetByPath<UStaticMesh>(MeshPath))
		{
			StaticMesh->Modify();
			for (int32 Index = 0; Index < StaticMesh->GetStaticMaterials().Num(); ++Index)
			{
				const FName SlotName = StaticMesh->GetStaticMaterials()[Index].MaterialSlotName;
				if (!SlotWanted(Index, SlotName)) continue;
				StaticMesh->SetMaterial(Index, Material);
				Record(Index, SlotName);
			}
			StaticMesh->PostEditChange();
			Out->SetStringField(TEXT("meshType"), TEXT("StaticMesh"));
			Mesh = StaticMesh;
		}
		else if (USkeletalMesh* SkeletalMesh = LoadAssetByPath<USkeletalMesh>(MeshPath))
		{
			// Through the slot array rather than a per-index setter: the skeletal
			// path is the one AssetHandlers found to stick.
			SkeletalMesh->Modify();
			TArray<FSkeletalMaterial> Materials = SkeletalMesh->GetMaterials();
			for (int32 Index = 0; Index < Materials.Num(); ++Index)
			{
				if (!SlotWanted(Index, Materials[Index].MaterialSlotName)) continue;
				Materials[Index].MaterialInterface = Material;
				Record(Index, Materials[Index].MaterialSlotName);
			}
			SkeletalMesh->SetMaterials(Materials);
			SkeletalMesh->PostEditChange();
			Out->SetStringField(TEXT("meshType"), TEXT("SkeletalMesh"));
			Mesh = SkeletalMesh;
		}
		if (!Mesh) return nullptr;

		Out->SetStringField(TEXT("meshPath"), Mesh->GetPathName());
		Out->SetArrayField(TEXT("assignedSlots"), Assigned);
		FString SaveError;
		const bool bSaved = SaveAssetPackageChecked(Mesh, SaveError);
		Out->SetBoolField(TEXT("meshSaved"), bSaved);
		if (!bSaved) Out->SetStringField(TEXT("meshSaveError"), SaveError);
		return Out;
	}

	/** ORM / RMA style packed maps: one sample, three properties, one channel
	 *  each. Named explicitly because guessing from the texture alone is not
	 *  something a build step should do silently. */
	bool MaterialBuildResolvePackedKey(const FString& LowerKey, TArray<TPair<EMaterialProperty, FString>>& OutTargets, TArray<FString>& OutNames)
	{
		if (LowerKey == TEXT("orm") || LowerKey == TEXT("occlusionroughnessmetallic"))
		{
			OutTargets.Add({ MP_AmbientOcclusion, TEXT("R") });
			OutTargets.Add({ MP_Roughness,        TEXT("G") });
			OutTargets.Add({ MP_Metallic,         TEXT("B") });
			OutNames = { TEXT("AmbientOcclusion"), TEXT("Roughness"), TEXT("Metallic") };
			return true;
		}
		if (LowerKey == TEXT("rma") || LowerKey == TEXT("roughnessmetallicao"))
		{
			OutTargets.Add({ MP_Roughness,        TEXT("R") });
			OutTargets.Add({ MP_Metallic,         TEXT("G") });
			OutTargets.Add({ MP_AmbientOcclusion, TEXT("B") });
			OutNames = { TEXT("Roughness"), TEXT("Metallic"), TEXT("AmbientOcclusion") };
			return true;
		}
		return false;
	}
}

TSharedPtr<FJsonValue> FMaterialHandlers::BuildMaterial(const TSharedPtr<FJsonObject>& Params)
{
	// Read after the texture set is validated, and on the path each one selects.
	MCPReadParamsAhead(Params, { TEXT("samplerTypes"), TEXT("materialPath"), TEXT("name"), TEXT("packagePath"),
		TEXT("clearExisting"), TEXT("assignToMesh"), TEXT("meshSlots"), TEXT("textureParameters"),
		TEXT("parameters"), TEXT("nodes"), TEXT("connections"), TEXT("propertyConnections"), TEXT("instances") });

	// ── Texture set ──────────────────────────────────────────────────────────
	// textures, parameters and a node graph are each optional; one of them has to say what to build (#1229).
	const TSharedPtr<FJsonObject> NoTextures = MakeShared<FJsonObject>();
	const TSharedPtr<FJsonObject>* TexturesObj = nullptr;
	if (!TryGetObjectParam(Params, TEXT("textures"), TexturesObj) || !TexturesObj || !(*TexturesObj).IsValid())
	{
		TexturesObj = &NoTextures;
	}
	const TArray<TSharedPtr<FJsonValue>>* ParameterSpecs = nullptr;
	TryGetArrayParam(Params, TEXT("parameters"), ParameterSpecs);
	const TArray<TSharedPtr<FJsonValue>>* GraphNodes = nullptr;
	TryGetArrayParam(Params, TEXT("nodes"), GraphNodes);
	const TArray<TSharedPtr<FJsonValue>>* GraphConnections = nullptr;
	TryGetArrayParam(Params, TEXT("connections"), GraphConnections);
	const TArray<TSharedPtr<FJsonValue>>* GraphPropertyConnections = nullptr;
	TryGetArrayParam(Params, TEXT("propertyConnections"), GraphPropertyConnections);
	const TArray<TSharedPtr<FJsonValue>>* InstanceSpecs = nullptr;
	TryGetArrayParam(Params, TEXT("instances"), InstanceSpecs);
	const bool bHasGraph = (GraphNodes && GraphNodes->Num() > 0) || (GraphConnections && GraphConnections->Num() > 0)
		|| (GraphPropertyConnections && GraphPropertyConnections->Num() > 0);
	if ((*TexturesObj)->Values.Num() == 0 && (!ParameterSpecs || ParameterSpecs->Num() == 0) && !bHasGraph)
	{
		return MCPError(TEXT("Nothing to build: the texture set is empty and no parameters or nodes were given. Pass textures {baseColor: '/Game/T_Body_D', orm: '/Game/T_Body_ORM'}, parameters [{name, type, default}], or nodes/connections/propertyConnections"));
	}

	// textureParameters: true makes every sample a TextureSampleParameter2D named
	// after its key; an object {key: parameterName} names them individually.
	bool bTextureParameters = false;
	const TSharedPtr<FJsonObject>* TextureParameterNames = nullptr;
	if (!TryGetBoolParam(Params, TEXT("textureParameters"), bTextureParameters)
		&& TryGetObjectParam(Params, TEXT("textureParameters"), TextureParameterNames) && TextureParameterNames)
	{
		bTextureParameters = true;
	}

	// ── Optional per-property sampler-type overrides ─────────────────────────
	const TSharedPtr<FJsonObject>* SamplerTypesObj = nullptr;
	TryGetObjectParam(Params, TEXT("samplerTypes"), SamplerTypesObj);

	// ── Resolve every requested texture before touching the material, so a
	//    typo in the set never leaves a half-built graph behind. ─────────────
	TArray<FMaterialBuildSlot> Slots;
	TArray<FString> Warnings;
	for (const auto& JsonEntry : (*TexturesObj)->Values)
	{
		const TPair<FString, TSharedPtr<FJsonValue>> Entry(JsonEntry.Key, JsonEntry.Value);
		FMaterialBuildSlot Slot;
		Slot.Key = Entry.Key;
		if (!Entry.Value.IsValid() || !Entry.Value->TryGetString(Slot.TexturePath) || Slot.TexturePath.IsEmpty())
		{
			return MCPError(FString::Printf(TEXT("textures['%s'] must be a texture asset path string"), *Entry.Key));
		}

		const FString LowerKey = Slot.Key.ToLower();
		if (!MaterialBuildResolvePackedKey(LowerKey, Slot.Targets, Slot.TargetNames))
		{
			EMaterialProperty Property;
			if (!ParseMaterialProperty(Slot.Key, Property))
			{
				return MCPError(FString::Printf(
					TEXT("textures['%s'] is not a material property. Use baseColor, normal, roughness, metallic, specular, ambientOcclusion, emissive, opacity, opacityMask, or the packed keys orm / rma."),
					*Slot.Key));
			}
			// The whole sample (RGB) drives a single property.
			Slot.Targets.Add({ Property, FString() });
			Slot.TargetNames.Add(Slot.Key);
		}

		Slot.Texture = LoadAssetByPath<UTexture>(Slot.TexturePath);
		if (!Slot.Texture)
		{
			return MCPError(FString::Printf(TEXT("Failed to load texture at '%s' for textures['%s']"), *Slot.TexturePath, *Slot.Key));
		}
		Slots.Add(MoveTemp(Slot));
	}

	// ── Parameters and instances are checked before the material is touched ──
	struct FMaterialBuildParam { FString Name; FString Type; TSharedPtr<FJsonObject> Obj; UTexture* Texture = nullptr; };
	TArray<FMaterialBuildParam> BuildParams;
	if (ParameterSpecs)
	{
		for (int32 i = 0; i < ParameterSpecs->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!(*ParameterSpecs)[i].IsValid() || !(*ParameterSpecs)[i]->TryGetObject(Obj) || !Obj)
			{
				return MCPError(FString::Printf(TEXT("parameters[%d] must be an object {name, type, default?}"), i));
			}
			FMaterialBuildParam Param;
			Param.Obj = *Obj;
			(*Obj)->TryGetStringField(TEXT("name"), Param.Name);
			(*Obj)->TryGetStringField(TEXT("type"), Param.Type);
			Param.Type = Param.Type.ToLower();
			if (Param.Name.IsEmpty()) return MCPError(FString::Printf(TEXT("parameters[%d] needs a name"), i));
			if (Param.Type != TEXT("scalar") && Param.Type != TEXT("vector") && Param.Type != TEXT("texture"))
			{
				return MCPError(FString::Printf(TEXT("parameters[%d] '%s': type must be scalar, vector or texture"), i, *Param.Name));
			}
			FString TexturePath;
			if (Param.Type == TEXT("texture") && (*Obj)->TryGetStringField(TEXT("default"), TexturePath) && !TexturePath.IsEmpty())
			{
				Param.Texture = LoadAssetByPath<UTexture>(TexturePath);
				if (!Param.Texture) return MCPError(FString::Printf(TEXT("parameters[%d] '%s': texture not found at '%s'"), i, *Param.Name, *TexturePath));
			}
			BuildParams.Add(Param);
		}
	}
	if (InstanceSpecs)
	{
		for (int32 i = 0; i < InstanceSpecs->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			FString InstanceName;
			if (!(*InstanceSpecs)[i].IsValid() || !(*InstanceSpecs)[i]->TryGetObject(Obj) || !Obj
				|| !(*Obj)->TryGetStringField(TEXT("name"), InstanceName) || InstanceName.IsEmpty())
			{
				return MCPError(FString::Printf(TEXT("instances[%d] must be an object with a name"), i));
			}
			FString InstanceMesh;
			if ((*Obj)->TryGetStringField(TEXT("assignToMesh"), InstanceMesh) && !InstanceMesh.IsEmpty()
				&& !LoadAssetByPath<UStaticMesh>(InstanceMesh) && !LoadAssetByPath<USkeletalMesh>(InstanceMesh))
			{
				return MCPError(FString::Printf(TEXT("instances[%d] '%s': assignToMesh '%s' is neither a StaticMesh nor a SkeletalMesh"), i, *InstanceName, *InstanceMesh));
			}
		}
	}

	// ── Resolve or create the material ───────────────────────────────────────
	// 'assetPath' is the spec's alias, renamed to materialPath before this runs.
	const FString MaterialPath = OptionalString(Params, TEXT("materialPath"));

	UMaterial* Material = nullptr;
	bool bCreated = false;
	FString CreatedPath;

	if (!MaterialPath.IsEmpty())
	{
		Material = LoadMaterialFromPath(MaterialPath);
		if (!Material)
		{
			return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
		}
	}
	else
	{
		FString Name;
		if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
		const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Materials"));

		// Building into a material that is already there is the same request as
		// building a new one, so this reuses rather than refusing.
		Material = LoadAssetByPath<UMaterial>(PackagePath / Name);
		if (!Material)
		{
			UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
			UObject* NewAsset = AssetToolsModule.Get().CreateAsset(Name, PackagePath, UMaterial::StaticClass(), Factory);
			Material = Cast<UMaterial>(NewAsset);
			if (!Material)
			{
				return MCPError(FString::Printf(TEXT("Failed to create material '%s' in '%s'"), *Name, *PackagePath));
			}
			bCreated = true;
			CreatedPath = Material->GetPathName();
		}
	}

	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] BuildMaterial: %s (%d texture slots)"), *Material->GetPathName(), Slots.Num());

	const bool bClearExisting = OptionalBool(Params, TEXT("clearExisting"), false);
	if (bClearExisting)
	{
		UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);
	}

	UMaterialEditorOnlyData* EditorOnlyData = Material->GetEditorOnlyData();
	if (!EditorOnlyData)
	{
		return MCPError(TEXT("Material has no editor-only data (is this material domain supported?)"));
	}

	// ── Build one TextureSample per entry ────────────────────────────────────
	Material->Modify();

	TArray<TSharedPtr<FJsonValue>> SamplerReport;
	// Property plus the name the caller used for it, so the post-recompile
	// report speaks the caller's vocabulary rather than MP_ enum spellings.
	TArray<TPair<EMaterialProperty, FString>> RequestedProperties;
	int32 NodeY = -300;

	for (const FMaterialBuildSlot& Slot : Slots)
	{
		// Reuse a sample already wired to the first target property with the
		// same texture, so building twice does not stack duplicate nodes.
		UClass* SampleClass = bTextureParameters
			? UMaterialExpressionTextureSampleParameter2D::StaticClass()
			: UMaterialExpressionTextureSample::StaticClass();
		UMaterialExpressionTextureSample* Sample = nullptr;
		bool bReused = false;
		if (FExpressionInput* ExistingInput = GetMaterialPropertyInput(EditorOnlyData, Slot.Targets[0].Key))
		{
			UMaterialExpressionTextureSample* Existing = Cast<UMaterialExpressionTextureSample>(ExistingInput->Expression);
			if (Existing && Existing->Texture == Slot.Texture && Existing->GetClass() == SampleClass)
			{
				Sample = Existing;
				bReused = true;
			}
		}

		if (!Sample)
		{
			UMaterialExpression* Created = UMaterialEditingLibrary::CreateMaterialExpression(
				Material, SampleClass, -450, NodeY);
			Sample = Cast<UMaterialExpressionTextureSample>(Created);
			if (!Sample)
			{
				return MCPError(FString::Printf(TEXT("Failed to create a TextureSample node for textures['%s']"), *Slot.Key));
			}
		}
		NodeY += 300;

		Sample->Texture = Slot.Texture;
		// The key is the node's description, so connections can name it ("orm").
		if (Sample->Desc.IsEmpty()) Sample->Desc = Slot.Key;
		FString TextureParameterName;
		if (UMaterialExpressionTextureSampleParameter2D* AsParameter = Cast<UMaterialExpressionTextureSampleParameter2D>(Sample))
		{
			TextureParameterName = Slot.Key;
			if (TextureParameterNames) (*TextureParameterNames)->TryGetStringField(Slot.Key, TextureParameterName);
			AsParameter->ParameterName = FName(*TextureParameterName);
		}

		// ── Sampler type. This is the step that is silently fatal when it is
		//    wrong: a virtual texture read through a non-virtual sampler leaves
		//    the material input empty after the recompile and reports nothing.
		Sample->AutoSetSampleType();
		const EMaterialSamplerType AutoType = Sample->SamplerType;

		bool bVirtualTexture = Slot.Texture->VirtualTextureStreaming != 0
			|| Slot.Texture->IsCurrentlyVirtualTextured();
		int32 UdimBlocks = 1;
		if (Slot.Texture->Source.IsValid())
		{
			UdimBlocks = Slot.Texture->Source.GetNumBlocks();
		}
		// A UDIM set cannot be built without virtual texturing, so it samples
		// as virtual whether or not the flag has been round-tripped yet.
#if UE_MCP_HAS_5_5_API
		if (Slot.Texture->RequiresVirtualTexturing())
#else
		// 5.4 has no RequiresVirtualTexturing; a multi-block source is the
		// condition that forces it.
		if (UdimBlocks > 1)
#endif
		{
			bVirtualTexture = true;
		}
		if (UdimBlocks > 1)
		{
			bVirtualTexture = true;
		}

		// Reconcile the engine's guess with what the texture actually is. This
		// is the correction the manual workflow has to know to make by hand.
		EMaterialSamplerType ResolvedType = AutoType;
		if (bVirtualTexture)
		{
			ResolvedType = MaterialBuildPromoteSamplerToVirtual(ResolvedType);
		}
		else
		{
			ResolvedType = MaterialBuildDemoteSamplerFromVirtual(ResolvedType);
		}

		// An explicit override wins over everything above.
		FString OverrideName;
		bool bOverridden = false;
		if (SamplerTypesObj && (*SamplerTypesObj).IsValid()
			&& (*SamplerTypesObj)->TryGetStringField(Slot.Key, OverrideName) && !OverrideName.IsEmpty())
		{
			EMaterialSamplerType Parsed;
			if (!MaterialBuildParseSamplerType(OverrideName, Parsed))
			{
				return MCPError(FString::Printf(
					TEXT("samplerTypes['%s'] = '%s' is not an EMaterialSamplerType. Use e.g. Color, Normal, Masks, LinearColor, VirtualColor, VirtualNormal."),
					*Slot.Key, *OverrideName));
			}
			ResolvedType = Parsed;
			bOverridden = true;
		}

		Sample->SamplerType = ResolvedType;

		if (bVirtualTexture && !IsVirtualSamplerType(ResolvedType))
		{
			Warnings.Add(FString::Printf(
				TEXT("textures['%s'] is a virtual texture but samples as %s. The material input will be dropped on recompile; use a Virtual* sampler type."),
				*Slot.Key, *MaterialBuildSamplerTypeName(ResolvedType)));
		}
		if (!bVirtualTexture && IsVirtualSamplerType(ResolvedType))
		{
			Warnings.Add(FString::Printf(
				TEXT("textures['%s'] is not a virtual texture but samples as %s."),
				*Slot.Key, *MaterialBuildSamplerTypeName(ResolvedType)));
		}
		if (Slot.Targets.Num() > 1
			&& (ResolvedType == SAMPLERTYPE_Color || ResolvedType == SAMPLERTYPE_VirtualColor))
		{
			Warnings.Add(FString::Printf(
				TEXT("textures['%s'] is a packed map sampled as %s (sRGB). Its channels will be gamma decoded; import it as Masks or disable sRGB on the texture."),
				*Slot.Key, *MaterialBuildSamplerTypeName(ResolvedType)));
		}

		// ── Wire the sample to its property or properties ────────────────────
#if UE_MCP_HAS_5_8_API
		const TArray<FString> OutputNames = UMaterialEditingLibrary::GetMaterialExpressionOutputNames(Sample);
#else
		// GetMaterialExpressionOutputNames() arrived in 5.8, but it only reads
		// back the expression's own output pins, and UMaterialExpression has
		// exposed those since long before. Read them directly rather than lose
		// the by-name channel wiring and fall back to channel index on 5.7.
		TArray<FString> OutputNames;
		for (const FExpressionOutput& Output : Sample->GetOutputs())
		{
			OutputNames.Add(Output.OutputName.ToString());
		}
#endif
		TArray<TSharedPtr<FJsonValue>> ConnectedTargets;
		for (int32 TargetIndex = 0; TargetIndex < Slot.Targets.Num(); ++TargetIndex)
		{
			const EMaterialProperty Property = Slot.Targets[TargetIndex].Key;
			const FString& OutputName = Slot.Targets[TargetIndex].Value;
			const FString TargetName = Slot.TargetNames.IsValidIndex(TargetIndex) ? Slot.TargetNames[TargetIndex] : Slot.Key;

			bool bConnected = false;
			FString ResolvedBy = TEXT("name");
			if (OutputName.IsEmpty() || OutputNames.Contains(OutputName))
			{
				bConnected = UMaterialEditingLibrary::ConnectMaterialProperty(Sample, OutputName, Property);
			}
			else
			{
				// The channel pin names are not what this engine build calls
				// them. Fall back to the conventional TextureSample output order
				// rather than dropping the channel and leaving the caller to
				// discover a half-wired packed map later.
				static const TCHAR* ChannelOrder[] = { TEXT("RGB"), TEXT("R"), TEXT("G"), TEXT("B"), TEXT("A") };
				int32 ChannelIndex = INDEX_NONE;
				for (int32 Channel = 0; Channel < (int32)UE_ARRAY_COUNT(ChannelOrder); ++Channel)
				{
					if (OutputName.Equals(ChannelOrder[Channel], ESearchCase::IgnoreCase)) { ChannelIndex = Channel; break; }
				}
				FExpressionInput* PropertyInput = (ChannelIndex != INDEX_NONE)
					? GetMaterialPropertyInput(EditorOnlyData, Property)
					: nullptr;
				if (PropertyInput)
				{
					PropertyInput->Connect(ChannelIndex, Sample);
					bConnected = true;
					ResolvedBy = TEXT("index");
				}
				Warnings.Add(FString::Printf(
					TEXT("textures['%s'] has no '%s' output (available: %s); %s wired by channel index instead: %s."),
					*Slot.Key, *OutputName, *FString::Join(OutputNames, TEXT(", ")), *TargetName,
					bConnected ? TEXT("connected") : TEXT("left unconnected")));
			}
			bool bAlreadyTracked = false;
			for (const TPair<EMaterialProperty, FString>& Tracked : RequestedProperties)
			{
				if (Tracked.Key == Property) { bAlreadyTracked = true; break; }
			}
			if (!bAlreadyTracked)
			{
				RequestedProperties.Add({ Property, TargetName });
			}

			TSharedPtr<FJsonObject> TargetObj = MakeShared<FJsonObject>();
			TargetObj->SetStringField(TEXT("property"), TargetName);
			TargetObj->SetStringField(TEXT("output"), OutputName.IsEmpty() ? FString(TEXT("RGB")) : OutputName);
			TargetObj->SetStringField(TEXT("outputResolvedBy"), ResolvedBy);
			TargetObj->SetBoolField(TEXT("connected"), bConnected);
			ConnectedTargets.Add(MakeShared<FJsonValueObject>(TargetObj));
		}

		TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
		SlotObj->SetStringField(TEXT("key"), Slot.Key);
		SlotObj->SetStringField(TEXT("texturePath"), Slot.Texture->GetPathName());
		SlotObj->SetStringField(TEXT("samplerType"), MaterialBuildSamplerTypeName(ResolvedType));
		SlotObj->SetStringField(TEXT("autoSamplerType"), MaterialBuildSamplerTypeName(AutoType));
		SlotObj->SetBoolField(TEXT("samplerTypeOverridden"), bOverridden);
		SlotObj->SetBoolField(TEXT("virtualTexture"), bVirtualTexture);
		SlotObj->SetNumberField(TEXT("udimBlocks"), UdimBlocks);
		SlotObj->SetBoolField(TEXT("reusedExistingNode"), bReused);
		if (!TextureParameterName.IsEmpty()) SlotObj->SetStringField(TEXT("parameterName"), TextureParameterName);
		SlotObj->SetArrayField(TEXT("targets"), ConnectedTargets);
		SamplerReport.Add(MakeShared<FJsonValueObject>(SlotObj));
	}

	// ── Parameter nodes. An existing parameter of the same name and kind is
	//    reused, so building twice does not stack duplicates. ─────────────────
	TArray<TSharedPtr<FJsonValue>> ParameterReport;
	int32 ParamY = -300;
	for (const FMaterialBuildParam& Param : BuildParams)
	{
		UClass* ParamClass = Param.Type == TEXT("scalar") ? UMaterialExpressionScalarParameter::StaticClass()
			: Param.Type == TEXT("vector") ? UMaterialExpressionVectorParameter::StaticClass()
			: UMaterialExpressionTextureSampleParameter2D::StaticClass();
		UMaterialExpression* Node = nullptr;
		for (UMaterialExpression* Existing : Material->GetExpressions())
		{
			if (!Existing || Existing->GetClass() != ParamClass) continue;
			FName ExistingName;
			if (UMaterialExpressionParameter* AsParam = Cast<UMaterialExpressionParameter>(Existing)) ExistingName = AsParam->ParameterName;
			else if (UMaterialExpressionTextureSampleParameter* AsTex = Cast<UMaterialExpressionTextureSampleParameter>(Existing)) ExistingName = AsTex->ParameterName;
			if (ExistingName.ToString().Equals(Param.Name, ESearchCase::IgnoreCase)) { Node = Existing; break; }
		}
		const bool bParamReused = Node != nullptr;
		if (!Node)
		{
			Node = UMaterialEditingLibrary::CreateMaterialExpression(Material, ParamClass, -800, ParamY);
			ParamY += 200;
		}
		if (!Node) return MCPError(FString::Printf(TEXT("Failed to create parameter node '%s'"), *Param.Name));
		Node->Desc = Param.Name;
		FString Group;
		Param.Obj->TryGetStringField(TEXT("group"), Group);
		const TSharedPtr<FJsonValue> Default = Param.Obj->TryGetField(TEXT("default"));
		if (UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Node))
		{
			Scalar->ParameterName = FName(*Param.Name);
			double Value = 0.0;
			if (Default.IsValid() && Default->TryGetNumber(Value)) Scalar->DefaultValue = (float)Value;
			if (!Group.IsEmpty()) Scalar->Group = FName(*Group);
		}
		else if (UMaterialExpressionVectorParameter* Vector = Cast<UMaterialExpressionVectorParameter>(Node))
		{
			Vector->ParameterName = FName(*Param.Name);
			FLinearColor Color = FLinearColor::White;
			if (MaterialBuildReadColor(Default, Color)) Vector->DefaultValue = Color;
			if (!Group.IsEmpty()) Vector->Group = FName(*Group);
		}
		else if (UMaterialExpressionTextureSampleParameter2D* Texture = Cast<UMaterialExpressionTextureSampleParameter2D>(Node))
		{
			Texture->ParameterName = FName(*Param.Name);
			if (Param.Texture)
			{
				Texture->Texture = Param.Texture;
				Texture->AutoSetSampleType();
			}
			if (!Group.IsEmpty()) Texture->Group = FName(*Group);
		}
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("name"), Param.Name);
		Row->SetStringField(TEXT("type"), Param.Type);
		Row->SetStringField(TEXT("expressionName"), Node->GetName());
		Row->SetBoolField(TEXT("reusedExistingNode"), bParamReused);
		ParameterReport.Add(MakeShared<FJsonValueObject>(Row));
	}

	// ── Math nodes and wiring, through build_material_graph. Its connections
	//    name texture keys, parameter names and its own nodes alike. ─────────
	TSharedPtr<FJsonObject> GraphResult;
	if (bHasGraph)
	{
		TSharedPtr<FJsonObject> GraphParams = MakeShared<FJsonObject>();
		GraphParams->SetStringField(TEXT("assetPath"), Material->GetPathName());
		GraphParams->SetArrayField(TEXT("nodes"), GraphNodes ? *GraphNodes : TArray<TSharedPtr<FJsonValue>>());
		if (GraphConnections) GraphParams->SetArrayField(TEXT("connections"), *GraphConnections);
		if (GraphPropertyConnections) GraphParams->SetArrayField(TEXT("propertyConnections"), *GraphPropertyConnections);
		const TSharedPtr<FJsonValue> GraphValue = BuildMaterialGraph(GraphParams);
		GraphResult = GraphValue.IsValid() ? GraphValue->AsObject() : nullptr;
		bool bGraphOk = false;
		if (!GraphResult.IsValid() || !GraphResult->TryGetBoolField(TEXT("success"), bGraphOk) || !bGraphOk)
		{
			FString GraphError = TEXT("build_material_graph failed");
			if (GraphResult.IsValid()) GraphResult->TryGetStringField(TEXT("error"), GraphError);
			auto Failed = MCPErrorObject(FString::Printf(TEXT("The textures and parameters were built, then the node graph failed: %s"), *GraphError));
			Failed->SetStringField(TEXT("materialPath"), Material->GetPathName());
			Failed->SetArrayField(TEXT("parameters"), ParameterReport);
			if (bCreated) MCPSetDeleteAssetRollback(Failed, Material->GetPathName());
			return MakeShared<FJsonValueObject>(Failed);
		}
		// Property wires the graph made count as requested, so the recompile check covers them.
		if (GraphPropertyConnections)
		{
			for (const TSharedPtr<FJsonValue>& Wire : *GraphPropertyConnections)
			{
				const TSharedPtr<FJsonObject>* WireObj = nullptr;
				FString PropName;
				EMaterialProperty Property;
				if (Wire.IsValid() && Wire->TryGetObject(WireObj) && WireObj && (*WireObj)->TryGetStringField(TEXT("property"), PropName)
					&& ParseMaterialProperty(PropName, Property)
					&& !RequestedProperties.ContainsByPredicate([Property](const TPair<EMaterialProperty, FString>& P) { return P.Key == Property; }))
				{
					RequestedProperties.Add({ Property, PropName });
				}
			}
		}
	}

	// ── Recompile, then report what actually survived it ─────────────────────
	const MCPMaterialCompile::FOutcome Compile = MCPMaterialCompile::Recompile(Material);
	FString SaveError;
	const bool bSaved = SaveAssetPackageChecked(Material, SaveError);

	// Re-read the editor-only data: RecompileMaterial can rebuild it, so the
	// pointer taken before the recompile is not the one to inspect after it.
	EditorOnlyData = Material->GetEditorOnlyData();

	TSharedPtr<FJsonObject> Persisted = MakeShared<FJsonObject>();
	int32 DroppedCount = 0;
	for (const TPair<EMaterialProperty, FString>& Requested : RequestedProperties)
	{
		const EMaterialProperty Property = Requested.Key;
		const FString& PropertyName = Requested.Value;

		FExpressionInput* Input = EditorOnlyData ? GetMaterialPropertyInput(EditorOnlyData, Property) : nullptr;
		const bool bStillConnected = Input && Input->Expression != nullptr;
		Persisted->SetBoolField(PropertyName, bStillConnected);
		if (!bStillConnected)
		{
			++DroppedCount;
			Warnings.Add(FString::Printf(
				TEXT("%s lost its connection during recompile. This is what a mismatched sampler type looks like: check the samplerType reported for the texture feeding it."),
				*PropertyName));
		}
	}

	auto Result = MCPSuccess();
	if (bCreated) MCPSetCreated(Result); else MCPSetUpdated(Result);
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetBoolField(TEXT("createdMaterial"), bCreated);
	Result->SetArrayField(TEXT("samplers"), SamplerReport);
	if (ParameterReport.Num() > 0) Result->SetArrayField(TEXT("parameters"), ParameterReport);
	if (GraphResult.IsValid())
	{
		TSharedPtr<FJsonObject> GraphSummary = MakeShared<FJsonObject>();
		const TArray<TSharedPtr<FJsonValue>>* GraphNodeMap = nullptr;
		if (GraphResult->TryGetArrayField(TEXT("nodes"), GraphNodeMap)) GraphSummary->SetArrayField(TEXT("nodes"), *GraphNodeMap);
		GraphSummary->SetNumberField(TEXT("wiresMade"), GraphResult->GetNumberField(TEXT("wiresMade")));
		GraphSummary->SetNumberField(TEXT("connectionsMade"), GraphResult->GetNumberField(TEXT("connectionsMade")));
		Result->SetObjectField(TEXT("graph"), GraphSummary);
	}
	Result->SetObjectField(TEXT("connectedAfterRecompile"), Persisted);
	Result->SetNumberField(TEXT("droppedConnections"), DroppedCount);
	MCPNoteSaveOutcome(Result, Material->GetPathName(), bSaved, SaveError);

	MCPMaterialCompile::Report(Result, Material, Compile, MCPMaterialCompile::EOnError::Fail);

	// ── Optional: put the finished material on a mesh's slots ────────────────
	// 'meshPath' is the spec's alias, renamed to assignToMesh before this runs.
	const FString MeshPath = OptionalString(Params, TEXT("assignToMesh"));
	if (!MeshPath.IsEmpty())
	{
		TArray<FString> WantedNames;
		TArray<int32> WantedIndices;
		const TArray<TSharedPtr<FJsonValue>>* SlotSpec = nullptr;
		TryGetArrayParam(Params, TEXT("meshSlots"), SlotSpec);
		MaterialBuildReadSlots(SlotSpec, WantedNames, WantedIndices);

		if (TSharedPtr<FJsonObject> Assignment = MaterialBuildAssignToMesh(MeshPath, WantedNames, WantedIndices, Material))
		{
			for (const auto& Field : Assignment->Values) Result->SetField(Field.Key, Field.Value);
			// The mesh write is part of the call, so a mesh that stays unsaved fails it (#931).
			if (!Assignment->GetBoolField(TEXT("meshSaved")))
			{
				Result->SetBoolField(TEXT("success"), false);
				Result->SetStringField(TEXT("error"), FString::Printf(
					TEXT("The material was assigned in memory but '%s' was not written: %s"), *MeshPath, *Assignment->GetStringField(TEXT("meshSaveError"))));
			}
			if (Assignment->GetArrayField(TEXT("assignedSlots")).Num() == 0 && (WantedNames.Num() > 0 || WantedIndices.Num() > 0))
			{
				Warnings.Add(TEXT("No mesh slot matched 'meshSlots'; nothing was assigned."));
			}
		}
		else
		{
			Warnings.Add(FString::Printf(TEXT("assignToMesh '%s' is neither a StaticMesh nor a SkeletalMesh; the material was built but assigned to nothing."), *MeshPath));
		}
	}

	// ── Optional: one MaterialInstanceConstant per entry, with its overrides,
	//    put on the mesh slots it names (#1229). ──────────────────────────────
	TArray<FString> CreatedInstancePaths;
	if (InstanceSpecs && InstanceSpecs->Num() > 0)
	{
		TArray<FMaterialParameterInfo> ScalarInfos, VectorInfos, TextureInfos;
		TArray<FGuid> Guids;
		Material->GetAllScalarParameterInfo(ScalarInfos, Guids);
		Material->GetAllVectorParameterInfo(VectorInfos, Guids);
		Material->GetAllTextureParameterInfo(TextureInfos, Guids);
		auto Has = [](const TArray<FMaterialParameterInfo>& Infos, const FString& Name)
		{
			return Infos.ContainsByPredicate([&Name](const FMaterialParameterInfo& Info) { return Info.Name.ToString().Equals(Name, ESearchCase::IgnoreCase); });
		};
		const FString DefaultInstanceFolder = FPackageName::GetLongPackagePath(Material->GetOutermost()->GetName());

		TArray<TSharedPtr<FJsonValue>> InstanceReport;
		for (const TSharedPtr<FJsonValue>& InstanceValue : *InstanceSpecs)
		{
			const TSharedPtr<FJsonObject> Spec = InstanceValue->AsObject();
			const FString InstanceName = Spec->GetStringField(TEXT("name"));
			FString InstanceFolder = DefaultInstanceFolder;
			Spec->TryGetStringField(TEXT("packagePath"), InstanceFolder);

			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("name"), InstanceName);
			UMaterialInstanceConstant* Instance = LoadAssetByPath<UMaterialInstanceConstant>(InstanceFolder / InstanceName);
			const bool bInstanceCreated = Instance == nullptr;
			if (!Instance)
			{
				UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
				Factory->InitialParent = Material;
				FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
				Instance = Cast<UMaterialInstanceConstant>(AssetToolsModule.Get().CreateAsset(InstanceName, InstanceFolder, UMaterialInstanceConstant::StaticClass(), Factory));
			}
			if (!Instance)
			{
				Warnings.Add(FString::Printf(TEXT("instances '%s': could not create a MaterialInstanceConstant in '%s'."), *InstanceName, *InstanceFolder));
				continue;
			}
			if (bInstanceCreated) CreatedInstancePaths.Add(Instance->GetPathName());
			UMaterialEditingLibrary::SetMaterialInstanceParent(Instance, Material);

			TArray<TSharedPtr<FJsonValue>> Applied;
			const TSharedPtr<FJsonObject>* Overrides = nullptr;
			if (Spec->TryGetObjectField(TEXT("parameters"), Overrides) && Overrides)
			{
				for (const auto& Pair : (*Overrides)->Values)
				{
					const FName ParamName(*Pair.Key);
					FString Kind;
					bool bOk = false;
					double Number = 0.0;
					FLinearColor Color = FLinearColor::White;
					FString TexturePath;
					// applied is the instance read back after the write: the editing library's setters can
					// answer false for a value they did write, which reported a landed override as missed (#1229).
					if (Has(ScalarInfos, FString(Pair.Key)) && Pair.Value->TryGetNumber(Number))
					{
						Kind = TEXT("scalar");
						UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Instance, ParamName, (float)Number);
						float Got = 0.f;
						bOk = Instance->GetScalarParameterValue(FMaterialParameterInfo(ParamName), Got, /*bOveriddenOnly*/ true)
							&& FMath::IsNearlyEqual(Got, (float)Number, 1.e-4f);
					}
					else if (Has(VectorInfos, FString(Pair.Key)) && MaterialBuildReadColor(Pair.Value, Color))
					{
						Kind = TEXT("vector");
						UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(Instance, ParamName, Color);
						FLinearColor Got;
						bOk = Instance->GetVectorParameterValue(FMaterialParameterInfo(ParamName), Got, /*bOveriddenOnly*/ true)
							&& Got.Equals(Color, 1.e-4f);
					}
					else if (Has(TextureInfos, FString(Pair.Key)) && Pair.Value->TryGetString(TexturePath))
					{
						Kind = TEXT("texture");
						UTexture* Texture = LoadAssetByPath<UTexture>(TexturePath);
						if (Texture) UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(Instance, ParamName, Texture);
						UTexture* Got = nullptr;
						bOk = Texture && Instance->GetTextureParameterValue(FMaterialParameterInfo(ParamName), Got, /*bOveriddenOnly*/ true)
							&& Got == Texture;
					}
					TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("name"), Pair.Key);
					Entry->SetStringField(TEXT("type"), Kind.IsEmpty() ? FString(TEXT("unknown")) : Kind);
					Entry->SetBoolField(TEXT("applied"), bOk);
					Applied.Add(MakeShared<FJsonValueObject>(Entry));
					if (!bOk)
					{
						Warnings.Add(FString::Printf(TEXT("instances '%s': parameter '%s' was not applied. The material has no %s parameter of that name, or the value does not fit its type."),
							*InstanceName, *Pair.Key, Kind.IsEmpty() ? TEXT("scalar, vector or texture") : *Kind));
					}
				}
			}
			UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
			FString InstanceSaveError;
			const bool bInstanceSaved = SaveAssetPackageChecked(Instance, InstanceSaveError);

			Row->SetStringField(TEXT("path"), Instance->GetPathName());
			Row->SetBoolField(TEXT("created"), bInstanceCreated);
			Row->SetBoolField(TEXT("saved"), bInstanceSaved);
			if (!bInstanceSaved) Row->SetStringField(TEXT("saveError"), InstanceSaveError);
			Row->SetArrayField(TEXT("parameters"), Applied);

			FString InstanceMesh;
			if (Spec->TryGetStringField(TEXT("assignToMesh"), InstanceMesh) && !InstanceMesh.IsEmpty())
			{
				TArray<FString> SlotNames;
				TArray<int32> SlotIndices;
				FString SlotName;
				if (Spec->TryGetStringField(TEXT("slotName"), SlotName) && !SlotName.IsEmpty()) SlotNames.Add(SlotName);
				const TArray<TSharedPtr<FJsonValue>>* InstanceSlots = nullptr;
				Spec->TryGetArrayField(TEXT("meshSlots"), InstanceSlots);
				MaterialBuildReadSlots(InstanceSlots, SlotNames, SlotIndices);
				if (TSharedPtr<FJsonObject> Assignment = MaterialBuildAssignToMesh(InstanceMesh, SlotNames, SlotIndices, Instance))
				{
					Row->SetObjectField(TEXT("assignment"), Assignment);
					if (Assignment->GetArrayField(TEXT("assignedSlots")).Num() == 0)
					{
						Warnings.Add(FString::Printf(TEXT("instances '%s': no slot on '%s' matched; nothing was assigned."), *InstanceName, *InstanceMesh));
					}
					if (!Assignment->GetBoolField(TEXT("meshSaved")))
					{
						Result->SetBoolField(TEXT("success"), false);
						Result->SetStringField(TEXT("error"), FString::Printf(TEXT("instances '%s': '%s' was assigned in memory but not written: %s"),
							*InstanceName, *InstanceMesh, *Assignment->GetStringField(TEXT("meshSaveError"))));
					}
				}
			}
			InstanceReport.Add(MakeShared<FJsonValueObject>(Row));
		}
		Result->SetArrayField(TEXT("instances"), InstanceReport);
	}

	if (Warnings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> WarningValues;
		for (const FString& Warning : Warnings) WarningValues.Add(MakeShared<FJsonValueString>(Warning));
		Result->SetArrayField(TEXT("warnings"), WarningValues);
	}

	if (bCreated && !CreatedPath.IsEmpty())
	{
		MCPSetDeleteAssetRollback(Result, CreatedPath);
	}
	if (CreatedInstancePaths.Num() > 0)
	{
		// One record carries one call; the instances need their own delete_asset each.
		TArray<TSharedPtr<FJsonValue>> Paths;
		for (const FString& Path : CreatedInstancePaths) Paths.Add(MakeShared<FJsonValueString>(Path));
		Result->SetArrayField(TEXT("createdInstances"), Paths);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("%d material instance(s) were created; the rollback record does not remove them. Delete each path in createdInstances with asset(delete), or call asset(delete_batch) with that list."),
			CreatedInstancePaths.Num()));
	}

	return MCPResult(Result);
}
