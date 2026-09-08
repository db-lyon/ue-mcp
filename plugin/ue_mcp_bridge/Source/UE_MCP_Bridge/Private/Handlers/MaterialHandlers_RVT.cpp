// Runtime Virtual Texture authoring: the asset, the volume that bounds it,
// the material nodes that read and write it, and the landscape assignment.
//
// A translation-unit partition of FMaterialHandlers, like MaterialHandlers_Graph.cpp.
// Registration stays in MaterialHandlers.cpp::RegisterHandlers.
//
// WHAT ALREADY EXISTED, and is deliberately not rebuilt here:
//
// * level(get_runtime_virtual_texture_summary) (LevelHandlers.cpp:2486) lists
//   RuntimeVirtualTextureVolume actors and the RVT asset each one is bound to.
//   That was the entire RVT surface. It is a read over the LEVEL; read_rvt
//   below is a read over the ASSET and its whole reference graph, so the two
//   answer different questions and both are worth keeping.
// * material(add_expression) (MaterialHandlers.cpp:1134) resolves ANY
//   UMaterialExpression subclass by name, so a RuntimeVirtualTextureSample or
//   RuntimeVirtualTextureOutput node can already be added generically, and
//   material(connect_expressions) (MaterialHandlers_Graph.cpp:253) already
//   resolves target input pins by name through GetInput/GetInputName, which
//   works on both of those classes. Neither add_rvt_sampler nor add_rvt_output
//   exists in order to create a node: each exists for the step the generic
//   path CANNOT do, named in that function's own comment.
// * material(set_domain) already accepts RuntimeVirtualTexture
//   (MaterialHandlers.cpp:961), and material(set_expression_value) already
//   writes an arbitrary UPROPERTY on an expression by reflection.
//
// WHAT IS NOT WRITTEN HERE, on purpose:
//
// EVERY tunable on URuntimeVirtualTexture is a UPROPERTY - TileCount,
// TileSize, TileBorderSize, bCompressTextures, bAdaptive, bContinuousUpdate,
// bSinglePhysicalSpace, bPrivateSpace, RemoveLowMips, LODGroup,
// CustomMaterialData - and so is every tunable on
// URuntimeVirtualTextureComponent - ExpandBounds, bSnapBoundsToLandscape,
// bHidePrimitives, StreamLowMips, ScalabilityGroup - and so are the landscape's
// VirtualTextureNumLods, VirtualTextureLodBias and VirtualTextureRenderPassType.
// Being declared `protected` in C++ makes no difference: FProperty does not
// care about access specifiers, so asset(set_property) and editor(set_property)
// already reach all of them and already call PostEditChangeProperty, which is
// what makes an RVT write take effect. There is therefore no typed setter for
// any of them, and every action below returns the objectPath to aim
// set_property at.
//
// create_rvt takes materialType even though it is one of those UPROPERTYs, for
// a reason that is not convenience: RuntimeVirtualTexture::IsMaterialTypeSupported
// is an engine call, project settings can disable individual material types to
// cut shader permutations, and a property write would happily store a type that
// then compiles to nothing. The check is the justification, not the assignment.

#include "MaterialHandlers.h"

#include "HandlerAssetCreate.h"
#include "HandlerJsonProperty.h"
#include "HandlerUtils.h"

#include "Components/PrimitiveComponent.h"
#include "Components/RuntimeVirtualTextureComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeProxy.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureOutput.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureSample.h"
#include "MaterialEditingLibrary.h"
#include "PixelFormat.h"
#include "UObject/UnrealType.h"
#include "VT/RuntimeVirtualTexture.h"
#include "VT/RuntimeVirtualTextureEnum.h"
#include "VT/RuntimeVirtualTextureVolume.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Every helper here is prefixed MCPRvt. The module is a unity build, so two
// .cpp files sharing a blob merge their anonymous namespaces and a helper
// named the same as one in another handler file is a redefinition (C2084).
// Nothing below is copied from another handler file for the same reason.
namespace
{
	/** The default folder for RVT assets. A real asset domain, not a tool
	 *  namespace: an RVT belongs beside the textures and materials it feeds. */
	const TCHAR* MCPRvtDefaultPackagePath = TEXT("/Game/Textures/RVT");

	UEnum* MCPRvtMaterialTypeEnum()
	{
		return StaticEnum<ERuntimeVirtualTextureMaterialType>();
	}

	/** The bare enumerator name. UEnum answers with either "BaseColor" or
	 *  "ERuntimeVirtualTextureMaterialType::BaseColor" depending on how the
	 *  enum was declared and which engine version is answering, and a caller
	 *  should never have to know which. Both are normalised to the short form
	 *  on the way out, and both are accepted on the way in. */
	FString MCPRvtShortEnumName(const FString& Raw)
	{
		int32 Split = INDEX_NONE;
		return Raw.FindLastChar(TEXT(':'), Split) ? Raw.RightChop(Split + 1) : Raw;
	}

	/** Every material-type name this engine defines, minus the Count sentinel.
	 *  Read off the UENUM rather than a hand-written table, so a value added or
	 *  removed by an engine version cannot drift out of the error messages. */
	TArray<FString> MCPRvtMaterialTypeNames()
	{
		TArray<FString> Names;
		if (UEnum* Enum = MCPRvtMaterialTypeEnum())
		{
			for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
			{
				const FString Name = MCPRvtShortEnumName(Enum->GetNameStringByIndex(Index));
				if (Name.IsEmpty() || Name == TEXT("Count")) continue;
				Names.Add(Name);
			}
		}
		return Names;
	}

	FString MCPRvtMaterialTypeName(ERuntimeVirtualTextureMaterialType Type)
	{
		UEnum* Enum = MCPRvtMaterialTypeEnum();
		return Enum
			? MCPRvtShortEnumName(Enum->GetNameStringByValue(static_cast<int64>(Type)))
			: FString::FromInt(static_cast<int32>(Type));
	}

	bool MCPRvtParseMaterialType(const FString& Spec, ERuntimeVirtualTextureMaterialType& Out)
	{
		UEnum* Enum = MCPRvtMaterialTypeEnum();
		if (!Enum) return false;
		const FString Wanted = MCPRvtShortEnumName(Spec);
		for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
		{
			const FString Name = MCPRvtShortEnumName(Enum->GetNameStringByIndex(Index));
			if (Name == TEXT("Count")) continue;
			if (Name.Equals(Wanted, ESearchCase::IgnoreCase))
			{
				Out = static_cast<ERuntimeVirtualTextureMaterialType>(Enum->GetValueByIndex(Index));
				return true;
			}
		}
		return false;
	}

	/** Load an RVT asset from any of the path spellings the repo accepts. */
	URuntimeVirtualTexture* MCPRvtLoad(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonValue>& OutError,
		const TCHAR* PrimaryKey = TEXT("rvtPath"))
	{
		OutError.Reset();
		FString Path = OptionalString(Params, PrimaryKey);
		if (Path.IsEmpty()) Path = OptionalString(Params, TEXT("assetPath"));
		Path.TrimStartAndEndInline();
		if (Path.IsEmpty())
		{
			OutError = MCPError(FString::Printf(
				TEXT("Missing required parameter '%s' (assetPath is accepted for it too): the RuntimeVirtualTexture asset path."),
				PrimaryKey));
			return nullptr;
		}
		UObject* Loaded = MCPLoadAssetObject(Path);
		if (!Loaded)
		{
			OutError = MCPAssetNotFoundError(Path, TEXT("RuntimeVirtualTexture"));
			return nullptr;
		}
		URuntimeVirtualTexture* Rvt = Cast<URuntimeVirtualTexture>(Loaded);
		if (!Rvt)
		{
			OutError = MCPAssetWrongTypeError(Path, Loaded, TEXT("RuntimeVirtualTexture"));
			return nullptr;
		}
		return Rvt;
	}

	/** The layer layout an RVT ends up with, which is the thing a sample node
	 *  has to agree with and the thing nothing else in the toolset reports. */
	TSharedPtr<FJsonObject> MCPRvtDescribeLayers(URuntimeVirtualTexture* Rvt)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		const int32 LayerCount = Rvt->GetLayerCount();
		Obj->SetNumberField(TEXT("layerCount"), LayerCount);
		TArray<TSharedPtr<FJsonValue>> Layers;
		for (int32 Index = 0; Index < LayerCount; ++Index)
		{
			TSharedPtr<FJsonObject> Layer = MakeShared<FJsonObject>();
			Layer->SetNumberField(TEXT("index"), Index);
			Layer->SetStringField(TEXT("pixelFormat"), GetPixelFormatString(Rvt->GetLayerFormat(Index)));
			Layer->SetBoolField(TEXT("srgb"), Rvt->IsLayerSRGB(Index));
			Layer->SetBoolField(TEXT("ycocg"), Rvt->IsLayerYCoCg(Index));
			Layers.Add(MakeShared<FJsonValueObject>(Layer));
		}
		Obj->SetArrayField(TEXT("layers"), Layers);
		return Obj;
	}

	/** Full read-back of one RVT asset. Everything here is derived from the
	 *  asset's settings, which is why it is worth a call: GetSize,
	 *  GetPageTableSize and the per-layer formats are computed, not stored, so
	 *  a property read cannot produce them. */
	void MCPRvtWriteAsset(TSharedPtr<FJsonObject> Result, URuntimeVirtualTexture* Rvt)
	{
		Result->SetStringField(TEXT("rvtPath"), Rvt->GetPathName());
		Result->SetStringField(TEXT("objectPath"), Rvt->GetPathName());
		Result->SetStringField(TEXT("name"), Rvt->GetName());
		Result->SetStringField(TEXT("materialType"), MCPRvtMaterialTypeName(Rvt->GetMaterialType()));
		Result->SetBoolField(TEXT("materialTypeSupported"),
			RuntimeVirtualTexture::IsMaterialTypeSupported(Rvt->GetMaterialType()));
		Result->SetNumberField(TEXT("tileCount"), Rvt->GetTileCount());
		Result->SetNumberField(TEXT("tileSize"), Rvt->GetTileSize());
		Result->SetNumberField(TEXT("tileBorderSize"), Rvt->GetTileBorderSize());
		Result->SetNumberField(TEXT("size"), Rvt->GetSize());
		Result->SetNumberField(TEXT("pageTableSize"), Rvt->GetPageTableSize());
		Result->SetBoolField(TEXT("compressTextures"), Rvt->GetCompressTextures());
		Result->SetBoolField(TEXT("lowQualityCompression"), Rvt->GetLQCompression());
		Result->SetBoolField(TEXT("clearTextures"), Rvt->GetClearTextures());
		Result->SetBoolField(TEXT("singlePhysicalSpace"), Rvt->GetSinglePhysicalSpace());
		Result->SetBoolField(TEXT("privateSpace"), Rvt->GetPrivateSpace());
		Result->SetBoolField(TEXT("adaptivePageTable"), Rvt->GetAdaptivePageTable());
		Result->SetBoolField(TEXT("continuousUpdate"), Rvt->GetContinuousUpdate());
		Result->SetNumberField(TEXT("removeLowMips"), Rvt->GetRemoveLowMips());
		Result->SetObjectField(TEXT("layerLayout"), MCPRvtDescribeLayers(Rvt));
		Result->SetStringField(TEXT("settingsNote"),
			TEXT("Every setting above is a UPROPERTY on the asset at objectPath, so change any of them with ")
			TEXT("asset(set_property) rather than looking for a typed setter here. size and pageTableSize are ")
			TEXT("DERIVED from tileCount and tileSize and cannot be written directly."));
	}

	/** The RVT component on a volume actor, or null. VirtualTextureComponent is
	 *  a public UPROPERTY on ARuntimeVirtualTextureVolume, so this is a direct
	 *  read rather than a reflected component search. */
	URuntimeVirtualTextureComponent* MCPRvtComponentOf(AActor* Actor)
	{
		ARuntimeVirtualTextureVolume* Volume = Cast<ARuntimeVirtualTextureVolume>(Actor);
		return Volume ? Volume->VirtualTextureComponent : nullptr;
	}

	/** Every RVT volume in the world bound to this asset. */
	TArray<ARuntimeVirtualTextureVolume*> MCPRvtFindVolumes(UWorld* World, URuntimeVirtualTexture* Rvt)
	{
		TArray<ARuntimeVirtualTextureVolume*> Found;
		if (!World) return Found;
		for (TActorIterator<ARuntimeVirtualTextureVolume> It(World); It; ++It)
		{
			URuntimeVirtualTextureComponent* Comp = MCPRvtComponentOf(*It);
			if (Comp && Comp->GetVirtualTexture() == Rvt) Found.Add(*It);
		}
		return Found;
	}

	/** Every primitive component in the world that writes into this RVT. This
	 *  is what a volume's bounds have to cover, and it covers landscapes too:
	 *  ULandscapeComponent overrides GetRuntimeVirtualTextures to answer from
	 *  its proxy's array (LandscapeComponent.h:806). */
	TArray<UPrimitiveComponent*> MCPRvtFindWriters(UWorld* World, URuntimeVirtualTexture* Rvt)
	{
		TArray<UPrimitiveComponent*> Writers;
		if (!World) return Writers;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor) continue;
			TArray<UPrimitiveComponent*> Components;
			Actor->GetComponents<UPrimitiveComponent>(Components);
			for (UPrimitiveComponent* Comp : Components)
			{
				if (!Comp) continue;
				if (Comp->GetRuntimeVirtualTextures().Contains(Rvt)) Writers.Add(Comp);
			}
		}
		return Writers;
	}

	/** Union the world-space bounds of a set of components, expressed in the
	 *  frame of InverseRotation, so a rotated volume is fitted in its own axes
	 *  rather than in world axes. World AABB corners are used rather than the
	 *  exact shape, which can over-cover a rotated mesh; an RVT volume has to
	 *  contain its writers, so over-covering is the safe direction. */
	bool MCPRvtUnionBoundsInFrame(
		const TArray<UPrimitiveComponent*>& Components,
		const FQuat& InverseRotation,
		FBox& OutBox)
	{
		OutBox.Init();
		bool bAny = false;
		for (UPrimitiveComponent* Comp : Components)
		{
			if (!Comp) continue;
			const FBox WorldBox = Comp->Bounds.GetBox();
			if (!WorldBox.IsValid) continue;
			FVector Corners[8];
			WorldBox.GetVertices(Corners);
			for (const FVector& Corner : Corners)
			{
				OutBox += InverseRotation.RotateVector(Corner);
				bAny = true;
			}
		}
		return bAny;
	}

	/** Solve for the transform that makes the component's own local box cover
	 *  TargetInFrame exactly, under the given rotation.
	 *
	 *  Deliberately calibrated rather than assumed: the local extent an RVT
	 *  component occupies is asked for through the public
	 *  USceneComponent::CalcBounds virtual instead of being hard-coded as the
	 *  unit cube, so this stays correct if a future engine version changes the
	 *  volume's local shape. */
	FTransform MCPRvtSolveVolumeTransform(
		URuntimeVirtualTextureComponent* Component,
		const FQuat& Rotation,
		const FBox& TargetInFrame)
	{
		USceneComponent* AsScene = Component;
		const FBox LocalBox = AsScene->CalcBounds(FTransform::Identity).GetBox();

		FVector LocalMin = LocalBox.IsValid ? LocalBox.Min : FVector::ZeroVector;
		FVector LocalSize = LocalBox.IsValid ? (LocalBox.Max - LocalBox.Min) : FVector::OneVector;

		const FVector TargetSize = TargetInFrame.GetSize();
		FVector Scale = FVector::OneVector;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			// A degenerate axis (a perfectly flat set of writers, or a local
			// box with no extent on that axis) would divide by zero; keep unit
			// scale there rather than producing an inside-out volume.
			Scale[Axis] = (FMath::Abs(LocalSize[Axis]) > UE_KINDA_SMALL_NUMBER && TargetSize[Axis] > UE_KINDA_SMALL_NUMBER)
				? TargetSize[Axis] / LocalSize[Axis]
				: 1.0;
		}

		const FVector TranslationInFrame = TargetInFrame.Min - (Scale * LocalMin);
		return FTransform(Rotation, Rotation.RotateVector(TranslationInFrame), Scale);
	}

	TSharedPtr<FJsonObject> MCPRvtVec(const FVector& V)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("x"), V.X);
		Obj->SetNumberField(TEXT("y"), V.Y);
		Obj->SetNumberField(TEXT("z"), V.Z);
		return Obj;
	}

	/** Report a volume actor and its component: transform, bound RVT, and the
	 *  component objectPath every remaining setting is written through. */
	void MCPRvtWriteVolume(TSharedPtr<FJsonObject> Result, ARuntimeVirtualTextureVolume* Volume)
	{
		Result->SetStringField(TEXT("actorPath"), Volume->GetPathName());
		Result->SetStringField(TEXT("actorLabel"), Volume->GetActorLabel());
		URuntimeVirtualTextureComponent* Comp = MCPRvtComponentOf(Volume);
		if (!Comp) return;
		Result->SetStringField(TEXT("componentPath"), Comp->GetPathName());
		Result->SetStringField(TEXT("componentObjectPath"), Comp->GetPathName());
		if (URuntimeVirtualTexture* Bound = Comp->GetVirtualTexture())
		{
			Result->SetStringField(TEXT("rvtPath"), Bound->GetPathName());
		}
		const FTransform Xform = Comp->GetComponentTransform();
		TSharedPtr<FJsonObject> XformObj = MakeShared<FJsonObject>();
		XformObj->SetObjectField(TEXT("location"), MCPRvtVec(Xform.GetLocation()));
		XformObj->SetObjectField(TEXT("scale"), MCPRvtVec(Xform.GetScale3D()));
		const FRotator Rot = Xform.Rotator();
		TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
		RotObj->SetNumberField(TEXT("pitch"), Rot.Pitch);
		RotObj->SetNumberField(TEXT("yaw"), Rot.Yaw);
		RotObj->SetNumberField(TEXT("roll"), Rot.Roll);
		XformObj->SetObjectField(TEXT("rotation"), RotObj);
		Result->SetObjectField(TEXT("transform"), XformObj);
#if WITH_EDITOR
		Result->SetBoolField(TEXT("snapBoundsToLandscape"), Comp->GetSnapBoundsToLandscape());
		Result->SetNumberField(TEXT("expandBounds"), Comp->GetExpandBounds());
		if (AActor* Align = Comp->GetBoundsAlignActor().Get())
		{
			Result->SetStringField(TEXT("boundsAlignActorPath"), Align->GetPathName());
			Result->SetStringField(TEXT("boundsAlignActorLabel"), Align->GetActorLabel());
		}
#endif
		Result->SetStringField(TEXT("componentSettingsNote"),
			TEXT("bSnapBoundsToLandscape, ExpandBounds, bHidePrimitives, StreamLowMips and the scalability fields are ")
			TEXT("UPROPERTYs on the component at componentObjectPath: write them with editor(set_property), then call ")
			TEXT("material(set_rvt_volume_bounds) again so the fit uses the new values."));
	}
}

// ---------------------------------------------------------------------------
// create_runtime_virtual_texture
//
// Earns a handler because creating a UObject asset is not a property write.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FMaterialHandlers::CreateRuntimeVirtualTexture(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	Name.TrimStartAndEndInline();
	if (Name.IsEmpty() || Name.Contains(TEXT("/")) || Name.Contains(TEXT(".")))
	{
		return MCPError(TEXT("name must be a non-empty Unreal asset name with no '/' or '.'; put the folder in packagePath."));
	}

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), MCPRvtDefaultPackagePath);
	PackagePath.TrimStartAndEndInline();
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath.LeftChopInline(1);
	if (!FPackageName::IsValidLongPackageName(PackagePath, true))
	{
		return MCPError(FString::Printf(TEXT("Invalid packagePath '%s'. It must be a long package path such as /Game/Textures/RVT."), *PackagePath));
	}
	if (MCPIsProtectedAssetPath(PackagePath))
	{
		return MCPProtectedPathError(PackagePath);
	}

	FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	OnConflict.ToLowerInline();
	if (OnConflict != TEXT("skip") && OnConflict != TEXT("error"))
	{
		return MCPError(TEXT("onConflict must be 'skip' (return the existing asset) or 'error'."));
	}

	// materialType is validated here rather than left to a property write
	// because a project can disable individual material types in project
	// settings to cut shader permutations, and an RVT whose type is disabled
	// compiles to nothing while looking perfectly configured.
	const FString TypeSpec = OptionalString(Params, TEXT("materialType"));
	ERuntimeVirtualTextureMaterialType MaterialType = ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular;
	bool bTypeRequested = false;
	if (!TypeSpec.IsEmpty())
	{
		if (!MCPRvtParseMaterialType(TypeSpec, MaterialType))
		{
			return MCPError(FString::Printf(
				TEXT("Unknown materialType '%s'. This engine defines: %s."),
				*TypeSpec, *FString::Join(MCPRvtMaterialTypeNames(), TEXT(", "))));
		}
		if (!RuntimeVirtualTexture::IsMaterialTypeSupported(MaterialType))
		{
			return MCPError(FString::Printf(
				TEXT("materialType '%s' is disabled for this project, so an RVT using it would compile to nothing. ")
				TEXT("Enable it under Project Settings > Engine > Rendering > Virtual Textures, or pick a supported type from: %s."),
				*MCPRvtMaterialTypeName(MaterialType), *FString::Join(MCPRvtMaterialTypeNames(), TEXT(", "))));
		}
		bTypeRequested = true;
	}

	// URuntimeVirtualTextureFactory lives in the VirtualTexturingEditor module,
	// which this plugin does not link, so the asset is constructed directly.
	// It needs no factory-side initialisation: every field has a default and
	// the render resource is created on registration, not on construction.
	auto Created = MCPCreateAssetIdempotentNewObject<URuntimeVirtualTexture>(
		Name, PackagePath, OnConflict, TEXT("RuntimeVirtualTexture"));
	if (Created.EarlyReturn) return Created.EarlyReturn;

	URuntimeVirtualTexture* Rvt = Created.Asset;

	if (bTypeRequested)
	{
		// Written through the shared reflected setter, the same path
		// asset(set_property) takes, rather than by reaching at a protected
		// member. One writer, one set of conversion rules.
		FProperty* TypeProp = Rvt->GetClass()->FindPropertyByName(TEXT("MaterialType"));
		if (!TypeProp)
		{
			return MCPError(TEXT("URuntimeVirtualTexture has no MaterialType property on this engine version, so the requested type could not be applied."));
		}
		FString SetErr;
		TSharedPtr<FJsonValue> TypeValue =
			MakeShared<FJsonValueString>(MCPRvtMaterialTypeName(MaterialType));
		void* ValuePtr = TypeProp->ContainerPtrToValuePtr<void>(Rvt);
		if (!MCPJsonProperty::SetJsonOnProperty(TypeProp, ValuePtr, TypeValue, SetErr))
		{
			return MCPError(FString::Printf(TEXT("Created the asset but could not set MaterialType: %s"), *SetErr));
		}
		// Called through UObject: URuntimeVirtualTexture re-declares the
		// override under `protected:`, so the derived name is inaccessible
		// while UObject's is public and the virtual dispatch is identical.
		// This is the call that releases and re-creates the render resource,
		// so skipping it would leave a live RVT laid out for the old type.
		FPropertyChangedEvent ChangeEvent(TypeProp);
		static_cast<UObject*>(Rvt)->PostEditChangeProperty(ChangeEvent);
	}

	FString SaveReason;
	const bool bSaved = SaveAssetPackageChecked(Rvt, SaveReason);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetCreated(Result);
	MCPSetDeleteAssetRollback(Result, Rvt->GetPathName());
	Result->SetStringField(TEXT("packagePath"), PackagePath);
	MCPRvtWriteAsset(Result, Rvt);
	MCPNoteSaveOutcome(Result, Rvt->GetPathName(), bSaved, SaveReason);
	Result->SetStringField(TEXT("nextSteps"),
		TEXT("1. material(add_rvt_volume) to place the volume that bounds it in the world. ")
		TEXT("2. material(add_rvt_output) on the material that WRITES the RVT, plus ")
		TEXT("material(assign_rvt_to_landscape) or a RuntimeVirtualTextures write on the writing primitive. ")
		TEXT("3. material(add_rvt_sampler) on the material that READS it. ")
		TEXT("Tile counts, compression and page-table flags are asset(set_property) at objectPath."));
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// read_runtime_virtual_texture
//
// The verification half. Reports the derived layout (size, page table size,
// per-layer pixel formats and colour spaces) that no property read produces,
// and the whole reference graph: which volumes bound it, which primitives and
// landscapes write into it, which materials sample it.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FMaterialHandlers::ReadRuntimeVirtualTexture(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonValue> Error;
	URuntimeVirtualTexture* Rvt = MCPRvtLoad(Params, Error);
	if (!Rvt) return Error;

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPRvtWriteAsset(Result, Rvt);

	UWorld* World = GetEditorWorld();
	Result->SetBoolField(TEXT("worldAvailable"), World != nullptr);

	TArray<TSharedPtr<FJsonValue>> VolumeRows;
	TArray<TSharedPtr<FJsonValue>> WriterRows;
	if (World)
	{
		for (ARuntimeVirtualTextureVolume* Volume : MCPRvtFindVolumes(World, Rvt))
		{
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			MCPRvtWriteVolume(Row, Volume);
			VolumeRows.Add(MakeShared<FJsonValueObject>(Row));
		}
		for (UPrimitiveComponent* Writer : MCPRvtFindWriters(World, Rvt))
		{
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("componentPath"), Writer->GetPathName());
			Row->SetStringField(TEXT("componentClass"), Writer->GetClass()->GetName());
			if (AActor* Owner = Writer->GetOwner())
			{
				Row->SetStringField(TEXT("actorPath"), Owner->GetPathName());
				Row->SetStringField(TEXT("actorLabel"), Owner->GetActorLabel());
				Row->SetBoolField(TEXT("isLandscape"), Owner->IsA<ALandscapeProxy>());
			}
			WriterRows.Add(MakeShared<FJsonValueObject>(Row));
		}
		MCPNoteLoadedOnlyEnumeration(World, Result);
	}
	Result->SetArrayField(TEXT("volumes"), VolumeRows);
	Result->SetNumberField(TEXT("volumeCount"), VolumeRows.Num());
	Result->SetArrayField(TEXT("writers"), WriterRows);
	Result->SetNumberField(TEXT("writerCount"), WriterRows.Num());

	// A sampler node whose MaterialType disagrees with the asset reads garbage
	// and compiles without complaint, which is the classic RVT failure, so the
	// mismatch is reported here rather than left to be discovered visually.
	TArray<TSharedPtr<FJsonValue>> SamplerRows;
	TArray<TSharedPtr<FJsonValue>> Problems;
	for (TObjectIterator<UMaterialExpressionRuntimeVirtualTextureSample> It; It; ++It)
	{
		UMaterialExpressionRuntimeVirtualTextureSample* Sample = *It;
		if (!Sample || Sample->VirtualTexture != Rvt) continue;
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("expressionPath"), Sample->GetPathName());
		Row->SetStringField(TEXT("expressionName"), Sample->GetDescription());
		Row->SetStringField(TEXT("materialType"), MCPRvtMaterialTypeName(Sample->MaterialType));
		if (UObject* Outer = Sample->GetOuter())
		{
			Row->SetStringField(TEXT("materialPath"), Outer->GetPathName());
		}
		const bool bTypeMatches = Sample->MaterialType == Rvt->GetMaterialType();
		const bool bPackingMatches = Sample->bSinglePhysicalSpace == Rvt->GetSinglePhysicalSpace();
		const bool bAdaptiveMatches = Sample->bAdaptive == Rvt->GetAdaptivePageTable();
		Row->SetBoolField(TEXT("materialTypeMatches"), bTypeMatches);
		Row->SetBoolField(TEXT("packedPageTableMatches"), bPackingMatches);
		Row->SetBoolField(TEXT("adaptivePageTableMatches"), bAdaptiveMatches);
		if (!bTypeMatches || !bPackingMatches || !bAdaptiveMatches)
		{
			Problems.Add(MakeShared<FJsonValueString>(FString::Printf(
				TEXT("Sample node '%s' in %s does not match the asset (materialType %s vs %s). It will decode the wrong ")
				TEXT("channels and still compile. Fix with material(add_rvt_sampler) on that material, which calls the ")
				TEXT("engine's own InitVirtualTextureDependentSettings."),
				*Sample->GetDescription(),
				Sample->GetOuter() ? *Sample->GetOuter()->GetPathName() : TEXT("<unknown material>"),
				*MCPRvtMaterialTypeName(Sample->MaterialType),
				*MCPRvtMaterialTypeName(Rvt->GetMaterialType()))));
		}
		SamplerRows.Add(MakeShared<FJsonValueObject>(Row));
	}
	Result->SetArrayField(TEXT("samplers"), SamplerRows);
	Result->SetNumberField(TEXT("samplerCount"), SamplerRows.Num());
	Result->SetStringField(TEXT("samplerScopeNote"),
		TEXT("Sampler nodes are found by walking LOADED material objects, so a material nobody has opened or referenced this ")
		TEXT("session will not appear here. asset(search) over Material assets plus material(read) is the exhaustive route."));

	if (VolumeRows.Num() == 0)
	{
		Problems.Add(MakeShared<FJsonValueString>(
			TEXT("No RuntimeVirtualTextureVolume in the loaded level is bound to this asset, so it has no world bounds and ")
			TEXT("renders nothing. Place one with material(add_rvt_volume).")));
	}
	if (WriterRows.Num() == 0)
	{
		Problems.Add(MakeShared<FJsonValueString>(
			TEXT("No primitive in the loaded level writes into this asset, so every page would render empty. Add it to a ")
			TEXT("landscape with material(assign_rvt_to_landscape), or to a mesh's RuntimeVirtualTextures array with ")
			TEXT("editor(set_property).")));
	}
	if (!RuntimeVirtualTexture::IsMaterialTypeSupported(Rvt->GetMaterialType()))
	{
		Problems.Add(MakeShared<FJsonValueString>(FString::Printf(
			TEXT("materialType '%s' is disabled for this project, so this RVT compiles to nothing. Enable it under ")
			TEXT("Project Settings > Engine > Rendering > Virtual Textures."),
			*MCPRvtMaterialTypeName(Rvt->GetMaterialType()))));
	}
	Result->SetArrayField(TEXT("problems"), Problems);
	Result->SetNumberField(TEXT("problemCount"), Problems.Num());
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// add_rvt_volume
//
// Earns a handler because it spawns an actor, binds the RVT through
// URuntimeVirtualTextureComponent::SetVirtualTexture (an engine call that
// re-registers the render state, which a raw property write does not), and
// then fits the volume to what actually writes into the texture.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FMaterialHandlers::AddRvtVolume(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	TSharedPtr<FJsonValue> Error;
	URuntimeVirtualTexture* Rvt = MCPRvtLoad(Params, Error);
	if (!Rvt) return Error;

	REQUIRE_EDITOR_WORLD(World);

	// Idempotency by binding, not by label: two volumes bound to the same RVT
	// is a real misconfiguration (the engine picks one and the other renders
	// nothing), so an existing binding is reported rather than duplicated.
	TArray<ARuntimeVirtualTextureVolume*> Existing = MCPRvtFindVolumes(World, Rvt);
	if (Existing.Num() > 0)
	{
		TSharedPtr<FJsonObject> Result = MCPSuccess();
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("spawned"), false);
		MCPRvtWriteVolume(Result, Existing[0]);
		Result->SetNumberField(TEXT("existingVolumeCount"), Existing.Num());
		Result->SetStringField(TEXT("note"), Existing.Num() > 1
			? TEXT("More than one volume in this level is bound to this RVT. Only one takes effect and the others render nothing; delete the extras with level(delete_actor).")
			: TEXT("A volume bound to this RVT already exists, so none was spawned. Refit it with material(set_rvt_volume_bounds)."));
		return MCPResult(Result);
	}

	FString Label = OptionalString(Params, TEXT("actorLabel"));
	Label.TrimStartAndEndInline();
	if (Label.IsEmpty()) Label = FString::Printf(TEXT("RVTVolume_%s"), *Rvt->GetName());

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transactional;
	ARuntimeVirtualTextureVolume* Volume =
		World->SpawnActor<ARuntimeVirtualTextureVolume>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!Volume)
	{
		return MCPError(TEXT("Failed to spawn a RuntimeVirtualTextureVolume into the editor world."));
	}
#if WITH_EDITOR
	Volume->SetActorLabel(Label);
#endif

	URuntimeVirtualTextureComponent* Comp = MCPRvtComponentOf(Volume);
	if (!Comp)
	{
		World->DestroyActor(Volume);
		return MCPError(TEXT("The spawned RuntimeVirtualTextureVolume has no VirtualTextureComponent, so it cannot be bound. This is an engine-side failure, not a parameter problem."));
	}
	Comp->SetVirtualTexture(Rvt);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetBoolField(TEXT("spawned"), true);

	// Fitting is the point of the action: an unbounded volume at the origin
	// with unit scale renders a 100cm cube of the world and nothing else.
	// Delegated to the same code set_rvt_volume_bounds runs, so a first fit
	// and a refit cannot disagree.
	TSharedPtr<FJsonObject> FitParams = MakeShared<FJsonObject>();
	FitParams->SetStringField(TEXT("rvtPath"), Rvt->GetPathName());
	FitParams->SetStringField(TEXT("actorPath"), Volume->GetPathName());
	if (Params->HasField(TEXT("boundsMode")))
	{
		FitParams->SetStringField(TEXT("boundsMode"), OptionalString(Params, TEXT("boundsMode")));
	}
	if (Params->HasField(TEXT("boundsAlignActor")))
	{
		FitParams->SetStringField(TEXT("boundsAlignActor"), OptionalString(Params, TEXT("boundsAlignActor")));
	}
	TSharedPtr<FJsonValue> FitResult = SetRvtVolumeBounds(FitParams);
	const TSharedPtr<FJsonObject>* FitObj = nullptr;
	if (FitResult.IsValid() && FitResult->TryGetObject(FitObj) && FitObj && (*FitObj).IsValid())
	{
		bool bFitOk = false;
		(*FitObj)->TryGetBoolField(TEXT("success"), bFitOk);
		Result->SetBoolField(TEXT("boundsFitted"), bFitOk);
		Result->SetObjectField(TEXT("boundsFit"), *FitObj);
		if (!bFitOk)
		{
			// The common case, and not an error for this action: an RVT
			// created a moment ago has no writers yet, so there is nothing to
			// fit to. The volume exists and is bound; it is sitting at the
			// origin at unit scale until something writes into the texture.
			Result->SetStringField(TEXT("boundsFitNote"),
				TEXT("The volume was created and bound, but the bounds fit found nothing to cover, so it is still at the ")
				TEXT("origin at unit scale and renders a 100cm cube of the world. This is expected when nothing writes into ")
				TEXT("the RVT yet: assign it with material(assign_rvt_to_landscape) or editor(set_property) on a mesh's ")
				TEXT("RuntimeVirtualTextures, then call material(set_rvt_volume_bounds). boundsFit carries the exact reason."));
		}
	}

	MCPRvtWriteVolume(Result, Volume);
	Result->SetStringField(TEXT("note"),
		TEXT("The volume was spawned into the currently loaded level and the level is now dirty; save it with ")
		TEXT("editor(save_current_level). Its transform is an ordinary actor transform, so level(move_actor) ")
		TEXT("can override the fit at any time."));

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorPath"), Volume->GetPathName());
	MCPSetRollback(Result, TEXT("delete_actor"), Payload);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_rvt_volume_bounds
//
// The "Set Bounds" button, which is not a property write at all: it derives a
// transform from what is in the world. The engine's own implementation lives
// in RuntimeVirtualTexture::SetBounds (VirtualTexturingEditor, a module this
// plugin does not link), so this computes the same thing from public API and
// says plainly which part of it does NOT reproduce.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FMaterialHandlers::SetRvtVolumeBounds(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	REQUIRE_EDITOR_WORLD(World);

	// The volume can be named directly, or found from the RVT it is bound to.
	ARuntimeVirtualTextureVolume* Volume = nullptr;
	TSharedPtr<FJsonValue> Error;
	FMCPActorSelector Selector;
	Selector.bRequired = false;
	Selector.Match = EMCPActorMatch::LabelOrName;
	if (AActor* Named = MCPResolveActor(World, Params, Error, Selector))
	{
		Volume = Cast<ARuntimeVirtualTextureVolume>(Named);
		if (!Volume)
		{
			return MCPError(FString::Printf(
				TEXT("Actor '%s' is a %s, not a RuntimeVirtualTextureVolume. Pass the volume actor, or omit actorLabel/actorPath and pass rvtPath to find it by binding."),
				*Named->GetActorLabel(), *Named->GetClass()->GetName()));
		}
	}
	else if (Error.IsValid())
	{
		return Error;
	}

	URuntimeVirtualTexture* Rvt = nullptr;
	if (!Volume)
	{
		Rvt = MCPRvtLoad(Params, Error);
		if (!Rvt) return Error;
		TArray<ARuntimeVirtualTextureVolume*> Found = MCPRvtFindVolumes(World, Rvt);
		if (Found.Num() == 0)
		{
			return MCPError(FString::Printf(
				TEXT("No RuntimeVirtualTextureVolume in the loaded level is bound to '%s'. Place one with material(add_rvt_volume) first."),
				*Rvt->GetPathName()));
		}
		if (Found.Num() > 1)
		{
			TArray<FString> Labels;
			for (ARuntimeVirtualTextureVolume* Candidate : Found) Labels.Add(Candidate->GetActorLabel());
			return MCPError(FString::Printf(
				TEXT("%d volumes are bound to '%s', so the target is ambiguous: [%s]. Name one with actorPath or actorLabel. ")
				TEXT("Only one of them takes effect at a time; the extras render nothing and should be deleted."),
				Found.Num(), *Rvt->GetPathName(), *FString::Join(Labels, TEXT(", "))));
		}
		Volume = Found[0];
	}

	URuntimeVirtualTextureComponent* Comp = MCPRvtComponentOf(Volume);
	if (!Comp)
	{
		return MCPError(TEXT("The volume has no VirtualTextureComponent, so there is nothing to fit."));
	}
	if (!Rvt) Rvt = Comp->GetVirtualTexture();
	if (!Rvt)
	{
		return MCPError(FString::Printf(
			TEXT("Volume '%s' has no RuntimeVirtualTexture bound, so there is no way to know which primitives it should cover. ")
			TEXT("Bind one with material(add_rvt_volume), or write VirtualTexture on %s with editor(set_property)."),
			*Volume->GetActorLabel(), *Comp->GetPathName()));
	}

	FString BoundsMode = OptionalString(Params, TEXT("boundsMode"), TEXT("writers"));
	BoundsMode.TrimStartAndEndInline();
	BoundsMode.ToLowerInline();
	// An explicitly empty string means "not specified", not "invalid": a caller
	// forwarding an absent value should get the default rather than a refusal.
	if (BoundsMode.IsEmpty()) BoundsMode = TEXT("writers");
	if (BoundsMode != TEXT("writers") && BoundsMode != TEXT("alignactor"))
	{
		return MCPError(TEXT("boundsMode must be 'writers' (cover every primitive that writes into this RVT) or 'alignActor' (match one actor's box and rotation, which is what a landscape wants)."));
	}

	// An explicit align actor is also the rotation frame: the whole reason to
	// align to a landscape is that the volume's axes have to be the terrain's
	// axes, or the RVT texels do not line up with terrain vertices.
	AActor* AlignActor = nullptr;
	const FString AlignSpec = OptionalString(Params, TEXT("boundsAlignActor"));
	if (!AlignSpec.IsEmpty())
	{
		AlignActor = MCPResolveActorToken(World, AlignSpec, Error, Selector);
		if (!AlignActor) return Error;
#if WITH_EDITOR
		Comp->SetBoundsAlignActor(AlignActor);
#endif
	}
#if WITH_EDITOR
	else
	{
		AlignActor = Comp->GetBoundsAlignActor().Get();
	}
#endif

	if (BoundsMode == TEXT("alignactor") && !AlignActor)
	{
		return MCPError(TEXT("boundsMode='alignActor' needs boundsAlignActor (an actor label or object path), and the component has none set. Pass one, or use boundsMode='writers'."));
	}

	const FQuat Rotation = AlignActor ? AlignActor->GetActorQuat() : FQuat::Identity;
	const FQuat InverseRotation = Rotation.Inverse();

	TArray<UPrimitiveComponent*> Sources;
	if (BoundsMode == TEXT("alignactor"))
	{
		AlignActor->GetComponents<UPrimitiveComponent>(Sources);
	}
	else
	{
		Sources = MCPRvtFindWriters(World, Rvt);
		// The align actor is always part of the bounds when one is set: that is
		// what makes "align to this landscape" mean "and cover it", even before
		// the landscape has been assigned the RVT.
		if (AlignActor)
		{
			TArray<UPrimitiveComponent*> AlignComponents;
			AlignActor->GetComponents<UPrimitiveComponent>(AlignComponents);
			for (UPrimitiveComponent* Extra : AlignComponents) Sources.AddUnique(Extra);
		}
	}

	FBox TargetInFrame(ForceInit);
	if (!MCPRvtUnionBoundsInFrame(Sources, InverseRotation, TargetInFrame) || !TargetInFrame.IsValid)
	{
		return MCPError(FString::Printf(
			TEXT("Nothing to fit to: no primitive with valid bounds %s. Assign the RVT to a landscape with ")
			TEXT("material(assign_rvt_to_landscape), or to a mesh's RuntimeVirtualTextures array with editor(set_property), ")
			TEXT("then call this again. boundsMode='alignActor' with boundsAlignActor set fits to one actor instead."),
			BoundsMode == TEXT("alignactor")
				? TEXT("on the align actor")
				: TEXT("in the loaded level writes into this RVT")));
	}

#if WITH_EDITOR
	const float Expand = Comp->GetExpandBounds();
	if (Expand > 0.0f) TargetInFrame = TargetInFrame.ExpandBy(Expand);
	const bool bSnapRequested = Comp->GetSnapBoundsToLandscape();
#else
	const bool bSnapRequested = false;
#endif

	const FTransform Previous = Comp->GetComponentTransform();
	const FTransform Fitted = MCPRvtSolveVolumeTransform(Comp, Rotation, TargetInFrame);

	const bool bChanged = !Previous.Equals(Fitted, 0.01);
	if (bChanged)
	{
		Volume->Modify();
		Comp->Modify();
		Volume->SetActorTransform(Fitted);
		Comp->MarkRenderStateDirty();
		Volume->MarkPackageDirty();
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	if (bChanged) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("unchanged"), true);
	Result->SetBoolField(TEXT("changed"), bChanged);
	Result->SetStringField(TEXT("boundsMode"), BoundsMode);
	Result->SetNumberField(TEXT("sourceComponentCount"), Sources.Num());
	Result->SetObjectField(TEXT("fittedBoxSize"), MCPRvtVec(TargetInFrame.GetSize()));
	MCPRvtWriteVolume(Result, Volume);

	TSharedPtr<FJsonObject> PrevObj = MakeShared<FJsonObject>();
	PrevObj->SetObjectField(TEXT("location"), MCPRvtVec(Previous.GetLocation()));
	PrevObj->SetObjectField(TEXT("scale"), MCPRvtVec(Previous.GetScale3D()));
	Result->SetObjectField(TEXT("previousTransform"), PrevObj);

	Result->SetBoolField(TEXT("texelSnapApplied"), false);
	if (bSnapRequested)
	{
		Result->SetStringField(TEXT("texelSnapNote"),
			TEXT("bSnapBoundsToLandscape is set on this component, and this fit does NOT perform the texel snap: that ")
			TEXT("alignment lives in RuntimeVirtualTexture::SetBounds in the VirtualTexturingEditor module, which the ")
			TEXT("bridge does not link. The volume covers the right region and is aligned to the align actor's rotation, ")
			TEXT("which is what matters for coverage; sub-texel vertex alignment needs the Set Bounds button in the ")
			TEXT("volume's details panel."));
	}
	Result->SetStringField(TEXT("method"),
		TEXT("The volume's own local extent is read back through USceneComponent::CalcBounds and solved against the union ")
		TEXT("of the source bounds, rather than assuming a unit cube, so the fit stays correct across engine versions."));

	// The inverse is the transform that was there before, applied as an
	// ordinary actor transform - lossless, unlike most rollbacks here.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorPath"), Volume->GetPathName());
	Payload->SetObjectField(TEXT("location"), MCPRvtVec(Previous.GetLocation()));
	Payload->SetObjectField(TEXT("scale"), MCPRvtVec(Previous.GetScale3D()));
	const FRotator PrevRot = Previous.Rotator();
	TSharedPtr<FJsonObject> PrevRotObj = MakeShared<FJsonObject>();
	PrevRotObj->SetNumberField(TEXT("pitch"), PrevRot.Pitch);
	PrevRotObj->SetNumberField(TEXT("yaw"), PrevRot.Yaw);
	PrevRotObj->SetNumberField(TEXT("roll"), PrevRot.Roll);
	Payload->SetObjectField(TEXT("rotation"), PrevRotObj);
	MCPSetRollback(Result, TEXT("move_actor"), Payload);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// add_rvt_sampler
//
// material(add_expression) can already create a RuntimeVirtualTextureSample
// node and material(set_expression_value) can already write its VirtualTexture
// property. What NEITHER can do is call
// UMaterialExpressionRuntimeVirtualTextureSample::InitVirtualTextureDependentSettings,
// which copies the asset's MaterialType, packed-page-table and adaptive flags
// onto the node and rebuilds its output pins. Setting VirtualTexture without
// it leaves the node decoding the wrong channels from the right texture, which
// compiles cleanly and renders wrong. That call is why this handler exists.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FMaterialHandlers::AddRvtSampler(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FString MaterialPath;
	if (auto Err = RequireStringAlt(Params, TEXT("materialPath"), TEXT("assetPath"), MaterialPath)) return Err;
	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load a Material at '%s'. A MaterialInstance cannot hold graph nodes; sample wiring belongs on the parent Material."), *MaterialPath));
	}

	TSharedPtr<FJsonValue> Error;
	URuntimeVirtualTexture* Rvt = MCPRvtLoad(Params, Error);
	if (!Rvt) return Error;

	FString ExpressionName = OptionalString(Params, TEXT("expressionName"));
	ExpressionName.TrimStartAndEndInline();
	if (ExpressionName.IsEmpty()) ExpressionName = FString::Printf(TEXT("RVTSample_%s"), *Rvt->GetName());

	const bool bConnectOutputs = OptionalBool(Params, TEXT("connectOutputs"), true);
	const bool bRecompile = OptionalBool(Params, TEXT("recompile"), true);

	// Idempotency by the node's own name, which is the key FindExpressionByName
	// resolves and therefore the key every other material action agrees on.
	UMaterialExpressionRuntimeVirtualTextureSample* Sample = nullptr;
	bool bCreated = false;
	if (UMaterialExpression* Found = FindExpressionByName(Material, ExpressionName))
	{
		Sample = Cast<UMaterialExpressionRuntimeVirtualTextureSample>(Found);
		if (!Sample)
		{
			return MCPError(FString::Printf(
				TEXT("Expression '%s' in %s is a %s, not a RuntimeVirtualTextureSample. Pass a different expressionName."),
				*ExpressionName, *Material->GetPathName(), *Found->GetClass()->GetName()));
		}
	}

	Material->PreEditChange(nullptr);
	Material->Modify();

	if (!Sample)
	{
		Sample = NewObject<UMaterialExpressionRuntimeVirtualTextureSample>(Material);
		Sample->Desc = ExpressionName;
		Sample->MaterialExpressionEditorX = OptionalInt(Params, TEXT("positionX"), -600);
		Sample->MaterialExpressionEditorY = OptionalInt(Params, TEXT("positionY"), 0);
		Material->GetExpressionCollection().AddExpression(Sample);
		bCreated = true;
	}

	const bool bWasBound = Sample->VirtualTexture == Rvt;
	const ERuntimeVirtualTextureMaterialType PreviousType = Sample->MaterialType;
	Sample->VirtualTexture = Rvt;
	// The engine's own synchronisation. Everything after this point can be read
	// back, which is how the caller knows it actually took.
	Sample->InitVirtualTextureDependentSettings();
	{
		// The node's own PostEditChangeProperty is what rebuilds the output
		// pins, and it branches on WHICH property changed, so it is handed the
		// real FProperty rather than a null one.
		FProperty* VirtualTextureProp =
			Sample->GetClass()->FindPropertyByName(TEXT("VirtualTexture"));
		FPropertyChangedEvent ChangeEvent(VirtualTextureProp);
		Sample->PostEditChangeProperty(ChangeEvent);
	}

	// Optional wiring into the material's own outputs. Only the pins that map
	// to a real material property are connected; WorldHeight, Mask, Mask4 and
	// Displacement have no counterpart and are reported instead of silently
	// dropped.
	TArray<TSharedPtr<FJsonValue>> OutputRows;
	TArray<TSharedPtr<FJsonValue>> Connected;
	TArray<TSharedPtr<FJsonValue>> Unconnected;
	UMaterialEditorOnlyData* EditorOnly = Material->GetEditorOnlyData();
	TArray<FExpressionOutput>& Outputs = Sample->GetOutputs();
	for (int32 Index = 0; Index < Outputs.Num(); ++Index)
	{
		const FString OutputName = Outputs[Index].OutputName.ToString();
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("index"), Index);
		Row->SetStringField(TEXT("name"), OutputName);
		OutputRows.Add(MakeShared<FJsonValueObject>(Row));

		EMaterialProperty MatProperty = MP_MAX;
		FExpressionInput* PropertyInput = nullptr;
		if (ParseMaterialProperty(OutputName, MatProperty))
		{
			PropertyInput = GetMaterialPropertyInput(EditorOnly, MatProperty);
		}
		if (!bConnectOutputs || !PropertyInput)
		{
			TSharedPtr<FJsonObject> Skip = MakeShared<FJsonObject>();
			Skip->SetStringField(TEXT("output"), OutputName);
			Skip->SetStringField(TEXT("reason"), !bConnectOutputs
				? TEXT("connectOutputs was false")
				: TEXT("this pin has no matching material property; wire it by hand with material(connect_expressions) or material(connect_to_property)"));
			Unconnected.Add(MakeShared<FJsonValueObject>(Skip));
			continue;
		}
		const bool bAlready = PropertyInput->Expression == Sample && PropertyInput->OutputIndex == Index;
		if (!bAlready) PropertyInput->Connect(Index, Sample);
		TSharedPtr<FJsonObject> Hit = MakeShared<FJsonObject>();
		Hit->SetStringField(TEXT("output"), OutputName);
		Hit->SetStringField(TEXT("property"), OutputName);
		Hit->SetBoolField(TEXT("changed"), !bAlready);
		Connected.Add(MakeShared<FJsonValueObject>(Hit));
	}

	Material->PostEditChange();
	Material->MarkPackageDirty();
	if (bRecompile) UMaterialEditingLibrary::RecompileMaterial(Material);
	FString SaveReason;
	const bool bSaved = SaveAssetPackageChecked(Material, SaveReason);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	if (bCreated) MCPSetCreated(Result); else MCPSetUpdated(Result);
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetStringField(TEXT("expressionName"), ExpressionName);
	Result->SetStringField(TEXT("expressionPath"), Sample->GetPathName());
	Result->SetStringField(TEXT("expressionObjectPath"), Sample->GetPathName());
	Result->SetStringField(TEXT("rvtPath"), Rvt->GetPathName());
	Result->SetBoolField(TEXT("wasAlreadyBound"), bWasBound);
	Result->SetStringField(TEXT("materialType"), MCPRvtMaterialTypeName(Sample->MaterialType));
	Result->SetStringField(TEXT("previousMaterialType"), MCPRvtMaterialTypeName(PreviousType));
	Result->SetBoolField(TEXT("materialTypeMatchesAsset"), Sample->MaterialType == Rvt->GetMaterialType());
	Result->SetBoolField(TEXT("packedPageTable"), Sample->bSinglePhysicalSpace);
	Result->SetBoolField(TEXT("adaptivePageTable"), Sample->bAdaptive);
	Result->SetArrayField(TEXT("outputs"), OutputRows);
	Result->SetArrayField(TEXT("connected"), Connected);
	Result->SetArrayField(TEXT("unconnected"), Unconnected);
	Result->SetBoolField(TEXT("recompiled"), bRecompile);
	MCPNoteSaveOutcome(Result, Material->GetPathName(), bSaved, SaveReason);
	Result->SetStringField(TEXT("syncNote"),
		TEXT("InitVirtualTextureDependentSettings copied the asset's material type and page-table flags onto the node and ")
		TEXT("rebuilt its output pins. Writing VirtualTexture with set_expression_value alone would leave those stale, which ")
		TEXT("compiles cleanly and decodes the wrong channels; that is the one thing this action does that the generic path "
		    "cannot."));
	Result->SetStringField(TEXT("otherSettingsNote"),
		TEXT("MipValueMode, TextureAddressMode, bEnableFeedback and WorldPositionOriginType are plain UPROPERTYs on the node ")
		TEXT("at expressionObjectPath: write them with editor(set_property), or with material(set_expression_value) and a ")
		TEXT("propertyName."));

	if (bCreated)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("materialPath"), Material->GetPathName());
		Payload->SetStringField(TEXT("expressionName"), ExpressionName);
		MCPSetRollback(Result, TEXT("delete_material_expression"), Payload);
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("This call updated an existing node rather than creating one, and the pin layout the previous material type ")
			TEXT("produced is gone. Deleting the node would remove a node the caller did not add, so no rollback is offered: ")
			TEXT("re-run with the previous rvtPath to get back to the old binding."));
	}
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// add_rvt_output
//
// The writer half. material(add_expression) can create the
// RuntimeVirtualTextureOutput node, but a material that writes an RVT normally
// has to feed the SAME sources into both its own property inputs and the RVT
// output's inputs, and doing that by hand is one connect call per channel with
// the source expression and output index looked up by eye. This mirrors the
// material's existing property connections in one call, which is a graph read
// plus a graph write and is not a property assignment at all.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FMaterialHandlers::AddRvtOutput(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FString MaterialPath;
	if (auto Err = RequireStringAlt(Params, TEXT("materialPath"), TEXT("assetPath"), MaterialPath)) return Err;
	UMaterial* Material = LoadMaterialFromPath(MaterialPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load a Material at '%s'. Only a Material holds a graph; a MaterialInstance cannot carry an RVT output node."), *MaterialPath));
	}

	FString ExpressionName = OptionalString(Params, TEXT("expressionName"));
	ExpressionName.TrimStartAndEndInline();
	if (ExpressionName.IsEmpty()) ExpressionName = TEXT("RVTOutput");

	const bool bMirror = OptionalBool(Params, TEXT("mirrorProperties"), true);
	const bool bRecompile = OptionalBool(Params, TEXT("recompile"), true);

	UMaterialExpressionRuntimeVirtualTextureOutput* Output = nullptr;
	bool bCreated = false;
	if (UMaterialExpression* Found = FindExpressionByName(Material, ExpressionName))
	{
		Output = Cast<UMaterialExpressionRuntimeVirtualTextureOutput>(Found);
		if (!Output)
		{
			return MCPError(FString::Printf(
				TEXT("Expression '%s' in %s is a %s, not a RuntimeVirtualTextureOutput. Pass a different expressionName."),
				*ExpressionName, *Material->GetPathName(), *Found->GetClass()->GetName()));
		}
	}

	Material->PreEditChange(nullptr);
	Material->Modify();

	if (!Output)
	{
		Output = NewObject<UMaterialExpressionRuntimeVirtualTextureOutput>(Material);
		Output->Desc = ExpressionName;
		Output->MaterialExpressionEditorX = OptionalInt(Params, TEXT("positionX"), 400);
		Output->MaterialExpressionEditorY = OptionalInt(Params, TEXT("positionY"), 400);
		Material->GetExpressionCollection().AddExpression(Output);
		bCreated = true;
	}

	// Mirror by INPUT NAME rather than by a hard-coded index list: the node's
	// inputs are read back through the same GetInput/GetInputName pair
	// connect_expressions uses, so a channel added or removed by an engine
	// version is picked up rather than mis-wired.
	TArray<TSharedPtr<FJsonValue>> Mirrored;
	TArray<TSharedPtr<FJsonValue>> Skipped;
	UMaterialEditorOnlyData* EditorOnly = Material->GetEditorOnlyData();
	for (int32 Index = 0; ; ++Index)
	{
		FExpressionInput* RvtInput = Output->GetInput(Index);
		if (!RvtInput) break;
		const FString InputName = Output->GetInputName(Index).ToString();

		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("index"), Index);
		Row->SetStringField(TEXT("input"), InputName);

		if (!bMirror)
		{
			Row->SetStringField(TEXT("reason"), TEXT("mirrorProperties was false"));
			Skipped.Add(MakeShared<FJsonValueObject>(Row));
			continue;
		}

		EMaterialProperty MatProperty = MP_MAX;
		FExpressionInput* PropertyInput = nullptr;
		if (ParseMaterialProperty(InputName, MatProperty))
		{
			PropertyInput = GetMaterialPropertyInput(EditorOnly, MatProperty);
		}
		if (!PropertyInput)
		{
			Row->SetStringField(TEXT("reason"),
				TEXT("no material property of this name to mirror (WorldHeight, Mask, Mask4 and Displacement are RVT-only); wire it with material(connect_expressions) targetExpression=this node, targetInput=this name"));
			Skipped.Add(MakeShared<FJsonValueObject>(Row));
			continue;
		}
		if (!PropertyInput->Expression)
		{
			Row->SetStringField(TEXT("reason"),
				TEXT("the material's own input of this name has nothing connected, so there is nothing to mirror"));
			Skipped.Add(MakeShared<FJsonValueObject>(Row));
			continue;
		}

		const bool bAlready = RvtInput->Expression == PropertyInput->Expression
			&& RvtInput->OutputIndex == PropertyInput->OutputIndex;
		if (!bAlready) RvtInput->Connect(PropertyInput->OutputIndex, PropertyInput->Expression);
		Row->SetStringField(TEXT("sourceExpression"), PropertyInput->Expression->GetDescription().IsEmpty()
			? PropertyInput->Expression->GetClass()->GetName()
			: PropertyInput->Expression->GetDescription());
		Row->SetNumberField(TEXT("sourceOutputIndex"), PropertyInput->OutputIndex);
		Row->SetBoolField(TEXT("changed"), !bAlready);
		Mirrored.Add(MakeShared<FJsonValueObject>(Row));
	}

	Material->PostEditChange();
	Material->MarkPackageDirty();
	if (bRecompile) UMaterialEditingLibrary::RecompileMaterial(Material);
	FString SaveReason;
	const bool bSaved = SaveAssetPackageChecked(Material, SaveReason);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	if (bCreated) MCPSetCreated(Result); else MCPSetUpdated(Result);
	Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
	Result->SetStringField(TEXT("expressionName"), ExpressionName);
	Result->SetStringField(TEXT("expressionPath"), Output->GetPathName());
	Result->SetStringField(TEXT("expressionObjectPath"), Output->GetPathName());
	Result->SetArrayField(TEXT("mirrored"), Mirrored);
	Result->SetNumberField(TEXT("mirroredCount"), Mirrored.Num());
	Result->SetArrayField(TEXT("skipped"), Skipped);
	Result->SetBoolField(TEXT("recompiled"), bRecompile);
	MCPNoteSaveOutcome(Result, Material->GetPathName(), bSaved, SaveReason);
	Result->SetStringField(TEXT("nextSteps"),
		TEXT("This material now WRITES into an RVT. It writes into whichever RVTs the primitive using it lists in its ")
		TEXT("RuntimeVirtualTextures array, not into a particular asset: set that with material(assign_rvt_to_landscape) for ")
		TEXT("a landscape, or editor(set_property) on a mesh component's RuntimeVirtualTextures. Then place the volume with ")
		TEXT("material(add_rvt_volume) and read the RVT back with material(read_rvt) to confirm the writer shows up."));

	if (bCreated)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("materialPath"), Material->GetPathName());
		Payload->SetStringField(TEXT("expressionName"), ExpressionName);
		MCPSetRollback(Result, TEXT("delete_material_expression"), Payload);
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("This call rewired an existing node's inputs. The connections it replaced were not recorded, so there is no ")
			TEXT("clean inverse; use editor(begin_transaction) around a run of graph edits when you need one."));
	}
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// assign_rvt_to_landscape
//
// ALandscapeProxy::RuntimeVirtualTextures IS a UPROPERTY, so a single-proxy
// write is set_property territory and this action would be redundant if that
// were the whole job. It is not, and each of these is why:
//
//  * A World Partition or streaming landscape is many ALandscapeStreamingProxy
//    actors sharing one ULandscapeInfo. Writing the array on the ALandscape
//    alone leaves every proxy rendering nothing into the RVT, and the failure
//    is invisible until pages come back empty.
//  * The change only takes effect after MarkComponentsRenderStateDirty on each
//    proxy.
//  * The assignment is only correct when the landscape's material actually has
//    an RVT output node, and when the RVT's material type is enabled for the
//    project. Both are checked and reported rather than discovered visually.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FMaterialHandlers::AssignRvtToLandscape(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	REQUIRE_EDITOR_WORLD(World);

	TSharedPtr<FJsonValue> Error;
	FMCPActorSelector Selector;
	Selector.Match = EMCPActorMatch::LabelOrName;
	AActor* Actor = MCPResolveActor(World, Params, Error, Selector);
	if (!Actor) return Error;

	ALandscapeProxy* Target = Cast<ALandscapeProxy>(Actor);
	if (!Target)
	{
		return MCPError(FString::Printf(
			TEXT("Actor '%s' is a %s, not a Landscape. landscape(get_landscape_info) lists the landscapes in this level."),
			*Actor->GetActorLabel(), *Actor->GetClass()->GetName()));
	}

	FString AssignMode = OptionalString(Params, TEXT("assignMode"), TEXT("set"));
	AssignMode.ToLowerInline();
	if (AssignMode != TEXT("set") && AssignMode != TEXT("add") && AssignMode != TEXT("remove"))
	{
		return MCPError(TEXT("assignMode must be 'set' (replace the whole list), 'add' (append the ones missing) or 'remove' (take these out)."));
	}

	TArray<FString> RvtPaths;
	const TArray<TSharedPtr<FJsonValue>>* PathArray = nullptr;
	if (Params->TryGetArrayField(TEXT("rvtPaths"), PathArray) && PathArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *PathArray)
		{
			FString One;
			if (Value.IsValid() && Value->TryGetString(One))
			{
				One.TrimStartAndEndInline();
				if (!One.IsEmpty()) RvtPaths.Add(One);
			}
		}
	}
	else
	{
		const FString Single = OptionalString(Params, TEXT("rvtPath"));
		if (!Single.IsEmpty()) RvtPaths.Add(Single);
	}

	if (RvtPaths.Num() == 0 && AssignMode != TEXT("set"))
	{
		return MCPError(TEXT("Pass rvtPaths (an array) or rvtPath (one). Only assignMode='set' accepts an empty list, and that clears the landscape's RVT assignment."));
	}

	TArray<URuntimeVirtualTexture*> Resolved;
	TArray<TSharedPtr<FJsonValue>> Problems;
	for (const FString& Path : RvtPaths)
	{
		UObject* Loaded = MCPLoadAssetObject(Path);
		if (!Loaded) return MCPAssetNotFoundError(Path, TEXT("RuntimeVirtualTexture"));
		URuntimeVirtualTexture* Rvt = Cast<URuntimeVirtualTexture>(Loaded);
		if (!Rvt) return MCPAssetWrongTypeError(Path, Loaded, TEXT("RuntimeVirtualTexture"));
		Resolved.AddUnique(Rvt);
		if (!RuntimeVirtualTexture::IsMaterialTypeSupported(Rvt->GetMaterialType()))
		{
			Problems.Add(MakeShared<FJsonValueString>(FString::Printf(
				TEXT("'%s' uses materialType %s, which is disabled for this project, so it will render nothing however it is assigned."),
				*Rvt->GetPathName(), *MCPRvtMaterialTypeName(Rvt->GetMaterialType()))));
		}
	}

	// Every proxy of the same landscape, found by shared ULandscapeInfo. A
	// streaming landscape is many actors and assigning only the one that was
	// named is the silent half-failure this action exists to prevent.
	ULandscapeInfo* Info = Target->GetLandscapeInfo();
	TArray<ALandscapeProxy*> Proxies;
	Proxies.Add(Target);
	if (Info)
	{
		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			ALandscapeProxy* Candidate = *It;
			if (!Candidate || Candidate == Target) continue;
			if (Candidate->GetLandscapeInfo() == Info) Proxies.AddUnique(Candidate);
		}
	}

	TArray<TSharedPtr<FJsonValue>> ProxyRows;
	TArray<TSharedPtr<FJsonValue>> RollbackRows;
	int32 ChangedProxies = 0;
	for (ALandscapeProxy* Proxy : Proxies)
	{
		const TArray<TObjectPtr<URuntimeVirtualTexture>> Before = Proxy->RuntimeVirtualTextures;

		TArray<TObjectPtr<URuntimeVirtualTexture>> After;
		if (AssignMode == TEXT("set"))
		{
			for (URuntimeVirtualTexture* Rvt : Resolved) After.AddUnique(Rvt);
		}
		else if (AssignMode == TEXT("add"))
		{
			After = Before;
			for (URuntimeVirtualTexture* Rvt : Resolved) After.AddUnique(Rvt);
		}
		else
		{
			After = Before;
			for (URuntimeVirtualTexture* Rvt : Resolved) After.Remove(Rvt);
		}

		const bool bChanged = After != Before;
		if (bChanged)
		{
			Proxy->Modify();
			Proxy->RuntimeVirtualTextures = After;
			// Both are needed: PostEditChange rebuilds the landscape's cached
			// material state, MarkComponentsRenderStateDirty is what makes the
			// scene proxy pick the new RVT list up this frame.
			Proxy->PostEditChange();
			Proxy->MarkComponentsRenderStateDirty();
			Proxy->MarkPackageDirty();
			ChangedProxies++;
		}

		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("actorPath"), Proxy->GetPathName());
		Row->SetStringField(TEXT("actorLabel"), Proxy->GetActorLabel());
		Row->SetStringField(TEXT("actorClass"), Proxy->GetClass()->GetName());
		Row->SetBoolField(TEXT("changed"), bChanged);
		Row->SetNumberField(TEXT("beforeCount"), Before.Num());
		Row->SetNumberField(TEXT("afterCount"), After.Num());
		TArray<TSharedPtr<FJsonValue>> AfterPaths;
		for (const TObjectPtr<URuntimeVirtualTexture>& Rvt : After)
		{
			if (Rvt) AfterPaths.Add(MakeShared<FJsonValueString>(Rvt->GetPathName()));
		}
		Row->SetArrayField(TEXT("runtimeVirtualTextures"), AfterPaths);
		ProxyRows.Add(MakeShared<FJsonValueObject>(Row));

		if (bChanged)
		{
			TArray<TSharedPtr<FJsonValue>> BeforePaths;
			for (const TObjectPtr<URuntimeVirtualTexture>& Rvt : Before)
			{
				if (Rvt) BeforePaths.Add(MakeShared<FJsonValueString>(Rvt->GetPathName()));
			}
			TSharedPtr<FJsonObject> RollbackRow = MakeShared<FJsonObject>();
			RollbackRow->SetStringField(TEXT("actorPath"), Proxy->GetPathName());
			RollbackRow->SetArrayField(TEXT("rvtPaths"), BeforePaths);
			RollbackRows.Add(MakeShared<FJsonValueObject>(RollbackRow));
		}
	}

	// The landscape material has to have an RVT output node or nothing is
	// written, whatever the assignment says. Checked here because it is the
	// other half of the same mistake.
	bool bMaterialWritesRvt = false;
	FString MaterialPathName;
	if (UMaterialInterface* LandscapeMaterial = Target->GetLandscapeMaterial())
	{
		MaterialPathName = LandscapeMaterial->GetPathName();
		if (UMaterial* BaseMaterial = LandscapeMaterial->GetMaterial())
		{
			for (UMaterialExpression* Expression : BaseMaterial->GetExpressions())
			{
				if (Cast<UMaterialExpressionRuntimeVirtualTextureOutput>(Expression))
				{
					bMaterialWritesRvt = true;
					break;
				}
			}
		}
	}
	if (!bMaterialWritesRvt && AssignMode != TEXT("remove"))
	{
		Problems.Add(MakeShared<FJsonValueString>(MaterialPathName.IsEmpty()
			? FString(TEXT("The landscape has no material, so it cannot write into an RVT whatever this assignment says."))
			: FString::Printf(
				TEXT("The landscape material '%s' has no RuntimeVirtualTextureOutput node, so nothing is written into the RVT. ")
				TEXT("Add one with material(add_rvt_output, materialPath=\"%s\")."),
				*MaterialPathName, *MaterialPathName)));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	if (ChangedProxies > 0) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("unchanged"), true);
	Result->SetStringField(TEXT("assignMode"), AssignMode);
	Result->SetStringField(TEXT("landscapePath"), Target->GetPathName());
	Result->SetStringField(TEXT("landscapeLabel"), Target->GetActorLabel());
	Result->SetStringField(TEXT("landscapeMaterialPath"), MaterialPathName);
	Result->SetBoolField(TEXT("landscapeMaterialWritesRvt"), bMaterialWritesRvt);
	Result->SetNumberField(TEXT("proxyCount"), Proxies.Num());
	Result->SetNumberField(TEXT("changedProxyCount"), ChangedProxies);
	Result->SetArrayField(TEXT("proxies"), ProxyRows);
	Result->SetArrayField(TEXT("problems"), Problems);
	Result->SetNumberField(TEXT("problemCount"), Problems.Num());
	Result->SetStringField(TEXT("note"),
		TEXT("Every proxy sharing this landscape's ULandscapeInfo was assigned, not just the actor that was named: a ")
		TEXT("streaming landscape is many actors and writing only one leaves the rest rendering nothing into the RVT. ")
		TEXT("VirtualTextureNumLods, VirtualTextureLodBias and VirtualTextureRenderPassType are plain UPROPERTYs on each ")
		TEXT("proxy: write them with editor(set_property). The levels holding these actors are now dirty; save with ")
		TEXT("editor(save_current_level)."));

	if (ChangedProxies > 0)
	{
		// Lossy in one specific way, and it is stated rather than implied: the
		// inverse restores the list on the proxies THIS call changed, and a
		// proxy that already matched is left alone by both directions.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), Target->GetPathName());
		Payload->SetStringField(TEXT("assignMode"), TEXT("set"));
		TArray<TSharedPtr<FJsonValue>> FirstBefore;
		if (RollbackRows.Num() > 0)
		{
			const TSharedPtr<FJsonObject>* FirstObj = nullptr;
			if (RollbackRows[0]->TryGetObject(FirstObj) && FirstObj && (*FirstObj).IsValid())
			{
				const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
				if ((*FirstObj)->TryGetArrayField(TEXT("rvtPaths"), Paths) && Paths) FirstBefore = *Paths;
			}
		}
		Payload->SetArrayField(TEXT("rvtPaths"), FirstBefore);
		MCPSetRollback(Result, TEXT("assign_rvt_to_landscape"), Payload);
		Result->SetArrayField(TEXT("previousPerProxy"), RollbackRows);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The rollback replays a 'set' with the list the NAMED landscape had before this call, which is the right ")
			TEXT("answer whenever every proxy shared one list. previousPerProxy carries the exact per-proxy state for the ")
			TEXT("case where they did not, so a caller that needs an exact restore can replay it proxy by proxy."));
	}
	return MCPResult(Result);
}
