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
#include "UObject/Class.h"
#include "SceneTypes.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionTextureSample.h"
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
	// ── Texture set ──────────────────────────────────────────────────────────
	const TSharedPtr<FJsonObject>* TexturesObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("textures"), TexturesObj) || !TexturesObj || !(*TexturesObj).IsValid())
	{
		return MCPError(TEXT("Missing 'textures' object mapping a material property to a texture asset path, e.g. {\"baseColor\":\"/Game/T_Body_D\",\"normal\":\"/Game/T_Body_N\",\"orm\":\"/Game/T_Body_ORM\"}"));
	}
	if ((*TexturesObj)->Values.Num() == 0)
	{
		return MCPError(TEXT("'textures' is empty - name at least one property, e.g. baseColor"));
	}

	// ── Optional per-property sampler-type overrides ─────────────────────────
	const TSharedPtr<FJsonObject>* SamplerTypesObj = nullptr;
	Params->TryGetObjectField(TEXT("samplerTypes"), SamplerTypesObj);

	// ── Resolve every requested texture before touching the material, so a
	//    typo in the set never leaves a half-built graph behind. ─────────────
	TArray<FMaterialBuildSlot> Slots;
	TArray<FString> Warnings;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*TexturesObj)->Values)
	{
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

	// ── Resolve or create the material ───────────────────────────────────────
	FString MaterialPath = OptionalString(Params, TEXT("materialPath"));
	if (MaterialPath.IsEmpty()) MaterialPath = OptionalString(Params, TEXT("assetPath"));

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
		UMaterialExpressionTextureSample* Sample = nullptr;
		bool bReused = false;
		if (FExpressionInput* ExistingInput = GetMaterialPropertyInput(EditorOnlyData, Slot.Targets[0].Key))
		{
			UMaterialExpressionTextureSample* Existing = Cast<UMaterialExpressionTextureSample>(ExistingInput->Expression);
			if (Existing && Existing->Texture == Slot.Texture)
			{
				Sample = Existing;
				bReused = true;
			}
		}

		if (!Sample)
		{
			UMaterialExpression* Created = UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionTextureSample::StaticClass(), -450, NodeY);
			Sample = Cast<UMaterialExpressionTextureSample>(Created);
			if (!Sample)
			{
				return MCPError(FString::Printf(TEXT("Failed to create a TextureSample node for textures['%s']"), *Slot.Key));
			}
		}
		NodeY += 300;

		Sample->Texture = Slot.Texture;

		// ── Sampler type. This is the step that is silently fatal when it is
		//    wrong: a virtual texture read through a non-virtual sampler leaves
		//    the material input empty after the recompile and reports nothing.
		Sample->AutoSetSampleType();
		const EMaterialSamplerType AutoType = Sample->SamplerType;

		bool bVirtualTexture = Slot.Texture->VirtualTextureStreaming != 0
			|| Slot.Texture->IsCurrentlyVirtualTextured();
		int32 UdimBlocks = 1;
#if WITH_EDITORONLY_DATA
		if (Slot.Texture->Source.IsValid())
		{
			UdimBlocks = Slot.Texture->Source.GetNumBlocks();
		}
		// A UDIM set cannot be built without virtual texturing, so it samples
		// as virtual whether or not the flag has been round-tripped yet.
		if (Slot.Texture->RequiresVirtualTexturing())
		{
			bVirtualTexture = true;
		}
#endif
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
		SlotObj->SetArrayField(TEXT("targets"), ConnectedTargets);
		SamplerReport.Add(MakeShared<FJsonValueObject>(SlotObj));
	}

	// ── Recompile, then report what actually survived it ─────────────────────
#if UE_MCP_HAS_5_8_API
	const TArray<FString> CompileErrors = UMaterialEditingLibrary::RecompileMaterial(Material);
#else
	// RecompileMaterial returns void before 5.8, where it started handing back
	// the compile errors it collected. The recompile itself is identical, so
	// only the reporting differs: `compileErrors` below stays empty on 5.7 and
	// the response says the list could not be read rather than implying the
	// material compiled clean.
	UMaterialEditingLibrary::RecompileMaterial(Material);
	const TArray<FString> CompileErrors;
#endif
	const bool bSaved = SaveAssetPackage(Material);

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
	Result->SetObjectField(TEXT("connectedAfterRecompile"), Persisted);
	Result->SetNumberField(TEXT("droppedConnections"), DroppedCount);
	Result->SetBoolField(TEXT("saved"), bSaved);

	if (CompileErrors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Errors;
		for (const FString& Error : CompileErrors) Errors.Add(MakeShared<FJsonValueString>(Error));
		Result->SetArrayField(TEXT("compileErrors"), Errors);
	}
#if !UE_MCP_HAS_5_8_API
	// Said out loud, because an absent compileErrors array otherwise reads as
	// "the recompile produced no errors", and on this engine it means the call
	// that recompiles does not report them at all.
	Result->SetStringField(TEXT("compileErrorsNote"),
		TEXT("compileErrors is not reported on this engine: UMaterialEditingLibrary::RecompileMaterial "
		     "returns void before UE 5.8. The material was recompiled; read the editor log for its errors."));
#endif

	// ── Optional: put the finished material on a mesh's slots ────────────────
	FString MeshPath = OptionalString(Params, TEXT("assignToMesh"));
	if (MeshPath.IsEmpty()) MeshPath = OptionalString(Params, TEXT("meshPath"));
	if (!MeshPath.IsEmpty())
	{
		// Slots may be named or indexed; an empty list means every slot.
		TArray<FString> WantedNames;
		TArray<int32> WantedIndices;
		const TArray<TSharedPtr<FJsonValue>>* SlotSpec = nullptr;
		if (Params->TryGetArrayField(TEXT("meshSlots"), SlotSpec) && SlotSpec)
		{
			for (const TSharedPtr<FJsonValue>& Value : *SlotSpec)
			{
				if (!Value.IsValid()) continue;
				double AsNumber = 0.0;
				FString AsString;
				if (Value->TryGetNumber(AsNumber)) WantedIndices.Add((int32)AsNumber);
				else if (Value->TryGetString(AsString) && !AsString.IsEmpty()) WantedNames.Add(AsString);
			}
		}
		const bool bAllSlots = WantedNames.Num() == 0 && WantedIndices.Num() == 0;

		auto SlotWanted = [&](int32 Index, const FName& SlotName) -> bool
		{
			if (bAllSlots) return true;
			if (WantedIndices.Contains(Index)) return true;
			for (const FString& Name : WantedNames)
			{
				if (SlotName.ToString().Equals(Name, ESearchCase::IgnoreCase)) return true;
			}
			return false;
		};

		TArray<TSharedPtr<FJsonValue>> Assigned;

		if (UStaticMesh* StaticMesh = LoadAssetByPath<UStaticMesh>(MeshPath))
		{
			StaticMesh->Modify();
			const int32 SlotCount = StaticMesh->GetStaticMaterials().Num();
			for (int32 Index = 0; Index < SlotCount; ++Index)
			{
				const FName SlotName = StaticMesh->GetStaticMaterials()[Index].MaterialSlotName;
				if (!SlotWanted(Index, SlotName)) continue;
				StaticMesh->SetMaterial(Index, Material);

				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetNumberField(TEXT("slotIndex"), Index);
				Entry->SetStringField(TEXT("slotName"), SlotName.ToString());
				Assigned.Add(MakeShared<FJsonValueObject>(Entry));
			}
			StaticMesh->PostEditChange();
			Result->SetStringField(TEXT("meshPath"), StaticMesh->GetPathName());
			Result->SetStringField(TEXT("meshType"), TEXT("StaticMesh"));
			Result->SetBoolField(TEXT("meshSaved"), SaveAssetPackage(StaticMesh));
		}
		else if (USkeletalMesh* SkeletalMesh = LoadAssetByPath<USkeletalMesh>(MeshPath))
		{
			// Assigning through the slot array rather than a per-index setter:
			// the skeletal path is the one AssetHandlers found to stick.
			SkeletalMesh->Modify();
			TArray<FSkeletalMaterial> Materials = SkeletalMesh->GetMaterials();
			for (int32 Index = 0; Index < Materials.Num(); ++Index)
			{
				if (!SlotWanted(Index, Materials[Index].MaterialSlotName)) continue;
				Materials[Index].MaterialInterface = Material;

				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetNumberField(TEXT("slotIndex"), Index);
				Entry->SetStringField(TEXT("slotName"), Materials[Index].MaterialSlotName.ToString());
				Assigned.Add(MakeShared<FJsonValueObject>(Entry));
			}
			SkeletalMesh->SetMaterials(Materials);
			SkeletalMesh->PostEditChange();
			Result->SetStringField(TEXT("meshPath"), SkeletalMesh->GetPathName());
			Result->SetStringField(TEXT("meshType"), TEXT("SkeletalMesh"));
			Result->SetBoolField(TEXT("meshSaved"), SaveAssetPackage(SkeletalMesh));
		}
		else
		{
			Warnings.Add(FString::Printf(TEXT("assignToMesh '%s' is neither a StaticMesh nor a SkeletalMesh; the material was built but assigned to nothing."), *MeshPath));
		}

		Result->SetArrayField(TEXT("assignedSlots"), Assigned);
		if (Assigned.Num() == 0 && !bAllSlots)
		{
			Warnings.Add(TEXT("No mesh slot matched 'meshSlots'; nothing was assigned."));
		}
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

	return MCPResult(Result);
}
