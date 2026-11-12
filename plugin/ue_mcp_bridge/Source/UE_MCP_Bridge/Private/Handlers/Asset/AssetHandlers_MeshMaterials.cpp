#include "AssetHandlers.h"
#include "HandlerUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#if __has_include("Engine/SkinnedAssetCommon.h")
#include "Engine/SkinnedAssetCommon.h"
#endif
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "SkeletalMeshTypes.h"
#include "StaticMeshResources.h"
#include "UObject/UnrealType.h"

namespace
{
	TSharedPtr<FJsonValue> MeshMaterialSlotJson(int32 Index, FName Name, FName ImportedName, UMaterialInterface* Interface)
	{
		auto Slot = MakeShared<FJsonObject>();
		Slot->SetNumberField(TEXT("index"), Index);
		Slot->SetStringField(TEXT("slotName"), Name.ToString());
		Slot->SetStringField(TEXT("importedName"), ImportedName.ToString());
		Slot->SetStringField(TEXT("materialPath"), Interface ? Interface->GetPathName() : TEXT(""));
		TArray<UMaterialInterface*> Lineage;
		TSet<UMaterialInterface*> Visited;
		for (auto* Current = Interface; Current && !Visited.Contains(Current);)
		{
			Visited.Add(Current);
			Lineage.Add(Current);
			auto* Instance = Cast<UMaterialInstance>(Current);
			Current = Instance ? Instance->Parent.Get() : nullptr;
		}
		bool TwoSided = false;
		bool DitherOpacityMask = false;
		EBlendMode BlendMode = BLEND_Opaque;
		EMaterialShadingModel ShadingModel = MSM_DefaultLit;
		TArray<TSharedPtr<FJsonValue>> Chain;
		// Resolve the authored values, including uncached instance overrides.
		for (int32 Level = Lineage.Num() - 1; Level >= 0; --Level)
		{
			auto* Current = Lineage[Level];
			auto* Instance = Cast<UMaterialInstance>(Current);
			if (auto* Base = Cast<UMaterial>(Current))
			{
				TwoSided = Base->TwoSided != 0;
				BlendMode = Base->BlendMode;
				ShadingModel = Base->GetShadingModels().GetFirstShadingModel();
				DitherOpacityMask = Base->DitherOpacityMask != 0;
			}
			if (Instance)
			{
				const auto& Overrides = Instance->BasePropertyOverrides;
				if (Overrides.bOverride_TwoSided) TwoSided = Overrides.TwoSided != 0;
				if (Overrides.bOverride_BlendMode) BlendMode = Overrides.BlendMode;
				if (Overrides.bOverride_ShadingModel) ShadingModel = Overrides.ShadingModel;
			}
			auto Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("path"), Current->GetPathName());
			Row->SetStringField(TEXT("class"), Current->GetClass()->GetName());
			Row->SetBoolField(TEXT("overrideTwoSided"), Instance && Instance->BasePropertyOverrides.bOverride_TwoSided);
			Row->SetBoolField(TEXT("twoSided"), TwoSided);
			Row->SetBoolField(TEXT("overrideBlendMode"), Instance && Instance->BasePropertyOverrides.bOverride_BlendMode);
			Row->SetStringField(TEXT("blendMode"), StaticEnum<EBlendMode>()->GetNameStringByValue(BlendMode));
			Chain.Insert(MakeShared<FJsonValueObject>(Row), 0);
		}
		Slot->SetArrayField(TEXT("chain"), Chain);
		if (Interface)
		{
			Slot->SetBoolField(TEXT("twoSided"), TwoSided);
			Slot->SetStringField(TEXT("blendMode"), StaticEnum<EBlendMode>()->GetNameStringByValue(BlendMode));
			Slot->SetStringField(TEXT("shadingModel"), StaticEnum<EMaterialShadingModel>()->GetNameStringByValue(ShadingModel));
			Slot->SetBoolField(TEXT("ditherOpacityMask"), DitherOpacityMask);
		}
		else
		{
			for (const TCHAR* Field : { TEXT("twoSided"), TEXT("blendMode"), TEXT("shadingModel"), TEXT("ditherOpacityMask") })
				Slot->SetField(Field, MakeShared<FJsonValueNull>());
		}
		return MakeShared<FJsonValueObject>(Slot);
	}
}

TSharedPtr<FJsonValue> FAssetHandlers::ReadMeshMaterials(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	const int32 RequestedLOD = OptionalInt(Params, TEXT("lodIndex"), INDEX_NONE);
	if (HasParam(Params, TEXT("lodIndex")) && RequestedLOD < 0) return MCPError(TEXT("lodIndex must be non-negative"));
	UObject* Asset = LoadAssetByPath<UObject>(AssetPath);
	auto* StaticMesh = Cast<UStaticMesh>(Asset);
	auto* SkeletalMesh = Cast<USkeletalMesh>(Asset);
	if (!StaticMesh && !SkeletalMesh) return MCPError(TEXT("assetPath must load a StaticMesh or SkeletalMesh"));
	const int32 LODCount = StaticMesh ? StaticMesh->GetNumLODs() : SkeletalMesh->GetLODNum();
	if (RequestedLOD != INDEX_NONE && RequestedLOD >= LODCount) return MCPError(TEXT("lodIndex is out of range"));
	TArray<TSharedPtr<FJsonValue>> Slots;
	if (StaticMesh)
	{
		for (const auto& Slot : StaticMesh->GetStaticMaterials())
			Slots.Add(MeshMaterialSlotJson(Slots.Num(), Slot.MaterialSlotName, Slot.ImportedMaterialSlotName, Slot.MaterialInterface));
	}
	else
	{
		for (const auto& Slot : SkeletalMesh->GetMaterials())
			Slots.Add(MeshMaterialSlotJson(Slots.Num(), Slot.MaterialSlotName, Slot.ImportedMaterialSlotName, Slot.MaterialInterface));
	}
	TArray<TSharedPtr<FJsonValue>> LODs;
	for (int32 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
	{
		if (RequestedLOD != INDEX_NONE && LODIndex != RequestedLOD) continue;
		auto LOD = MakeShared<FJsonObject>();
		LOD->SetNumberField(TEXT("lodIndex"), LODIndex);
		TArray<TSharedPtr<FJsonValue>> Sections;
		auto AddSection = [&](int32 SectionIndex, int32 SlotIndex)
		{
			auto Section = MakeShared<FJsonObject>();
			Section->SetNumberField(TEXT("sectionIndex"), SectionIndex);
			Section->SetNumberField(TEXT("slotIndex"), SlotIndex);
			Sections.Add(MakeShared<FJsonValueObject>(Section));
		};
		if (StaticMesh && StaticMesh->GetRenderData() && StaticMesh->GetRenderData()->LODResources.IsValidIndex(LODIndex))
		{
			const auto& RenderLOD = StaticMesh->GetRenderData()->LODResources[LODIndex];
			for (int32 SectionIndex = 0; SectionIndex < RenderLOD.Sections.Num(); ++SectionIndex)
			{
				const int32 SlotIndex = StaticMesh->GetSectionInfoMap().IsValidSection(LODIndex, SectionIndex)
					? StaticMesh->GetSectionInfoMap().Get(LODIndex, SectionIndex).MaterialIndex
					: RenderLOD.Sections[SectionIndex].MaterialIndex;
				AddSection(SectionIndex, SlotIndex);
			}
		}
		if (SkeletalMesh && SkeletalMesh->GetResourceForRendering() && SkeletalMesh->GetResourceForRendering()->LODRenderData.IsValidIndex(LODIndex))
		{
			const auto& RenderLOD = SkeletalMesh->GetResourceForRendering()->LODRenderData[LODIndex];
			const auto* LODInfo = SkeletalMesh->GetLODInfo(LODIndex);
			for (int32 SectionIndex = 0; SectionIndex < RenderLOD.RenderSections.Num(); ++SectionIndex)
			{
				int32 SlotIndex = RenderLOD.RenderSections[SectionIndex].MaterialIndex;
				if (LODInfo && LODInfo->LODMaterialMap.IsValidIndex(SectionIndex) && LODInfo->LODMaterialMap[SectionIndex] != INDEX_NONE)
					SlotIndex = LODInfo->LODMaterialMap[SectionIndex];
				AddSection(SectionIndex, SlotIndex);
			}
		}
		LOD->SetArrayField(TEXT("sections"), Sections);
		LODs.Add(MakeShared<FJsonValueObject>(LOD));
	}
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), Asset->GetPathName());
	Result->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
	Result->SetArrayField(TEXT("slots"), Slots);
	Result->SetArrayField(TEXT("lods"), LODs);
	bool NaniteEnabled = StaticMesh && MCPGetNaniteSettings(StaticMesh).bEnabled != 0;
#if UE_MCP_HAS_5_5_API
	if (SkeletalMesh) NaniteEnabled = SkeletalMesh->IsNaniteEnabled();
#endif
	Result->SetBoolField(TEXT("naniteEnabled"), NaniteEnabled);
	return MCPResult(Result);
}
