#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/Asset/AssetHandlers.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#if __has_include("Engine/SkinnedAssetCommon.h")
#include "Engine/SkinnedAssetCommon.h"
#endif
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "StaticMeshResources.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeshMaterialReadTest,
	"UE.MCP.Asset.MeshMaterials.EffectiveOverridesAndReadOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMeshMaterialReadTest::RunTest(const FString& Parameters)
{
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/MCPMeshMaterials_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	TStrongObjectPtr<UMaterial> Base(NewObject<UMaterial>(Package, TEXT("Base"), RF_Transient));
	TStrongObjectPtr<UMaterialInstanceConstant> Override(NewObject<UMaterialInstanceConstant>(Package, TEXT("Override"), RF_Transient));
	TStrongObjectPtr<UMaterialInstanceConstant> Inherited(NewObject<UMaterialInstanceConstant>(Package, TEXT("Inherited"), RF_Transient));
	TStrongObjectPtr<UStaticMesh> Mesh(NewObject<UStaticMesh>(Package, TEXT("Mesh"), RF_Transient));
	Base->TwoSided = false;
	Base->BlendMode = BLEND_Opaque;
	Base->DitherOpacityMask = true;
	Override->Parent = Base.Get();
	Override->BasePropertyOverrides.bOverride_TwoSided = true;
	Override->BasePropertyOverrides.TwoSided = true;
	Override->BasePropertyOverrides.bOverride_BlendMode = true;
	Override->BasePropertyOverrides.BlendMode = BLEND_Masked;
	Override->BasePropertyOverrides.bOverride_ShadingModel = true;
	Override->BasePropertyOverrides.ShadingModel = MSM_Unlit;
	Inherited->Parent = Override.Get();
	Inherited->BasePropertyOverrides.bOverride_TwoSided = false;
	Inherited->BasePropertyOverrides.TwoSided = false;
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Inherited.Get(), TEXT("Outer"), TEXT("ImportedOuter")));
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Base.Get(), TEXT("BaseSlot"), TEXT("ImportedBase")));
	auto RenderData = MakeUnique<FStaticMeshRenderData>();
	RenderData->AllocateLODResources(2);
	RenderData->LODResources[0].Sections.AddDefaulted();
	RenderData->LODResources[1].Sections.AddDefaulted();
	Mesh->SetRenderData(MoveTemp(RenderData));
	Mesh->GetSectionInfoMap().Set(0, 0, FMeshSectionInfo(1));
	Mesh->GetSectionInfoMap().Set(1, 0, FMeshSectionInfo(0));
	Package->SetDirtyFlag(false);
	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);
	auto Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
	auto Result = Registry.ExecuteHandler(TEXT("read_mesh_materials"), Request)->AsObject();
	if (!TestTrue(TEXT("mesh read succeeds"), Result->GetBoolField(TEXT("success")))) return false;
	const auto& Slots = Result->GetArrayField(TEXT("slots"));
	if (!TestEqual(TEXT("all slots returned"), Slots.Num(), 2)) return false;
	const auto& Slot = Slots[0]->AsObject();
	TestTrue(TEXT("non-overriding child inherits two sided"), Slot->GetBoolField(TEXT("twoSided")));
	TestEqual(TEXT("blend override inherited"), Slot->GetStringField(TEXT("blendMode")), FString(TEXT("BLEND_Masked")));
	TestEqual(TEXT("shading override inherited"), Slot->GetStringField(TEXT("shadingModel")), FString(TEXT("MSM_Unlit")));
	TestTrue(TEXT("base dither opacity inherited"), Slot->GetBoolField(TEXT("ditherOpacityMask")));
	TestEqual(TEXT("slot index"), Slot->GetIntegerField(TEXT("index")), 0);
	TestEqual(TEXT("imported slot name"), Slot->GetStringField(TEXT("importedName")), FString(TEXT("ImportedOuter")));
	TestFalse(TEXT("base remains one sided"), Slots[1]->AsObject()->GetBoolField(TEXT("twoSided")));
	const auto& Chain = Slot->GetArrayField(TEXT("chain"));
	if (!TestEqual(TEXT("instance-to-base chain"), Chain.Num(), 3)) return false;
	TestEqual(TEXT("child first"), Chain[0]->AsObject()->GetStringField(TEXT("path")), Inherited->GetPathName());
	TestFalse(TEXT("child override disabled"), Chain[0]->AsObject()->GetBoolField(TEXT("overrideTwoSided")));
	TestTrue(TEXT("parent override enabled"), Chain[1]->AsObject()->GetBoolField(TEXT("overrideTwoSided")));
	TestEqual(TEXT("base class"), Chain[2]->AsObject()->GetStringField(TEXT("class")), FString(TEXT("Material")));
	TestTrue(TEXT("LOD field exists"), Result->HasTypedField<EJson::Array>(TEXT("lods")));
	const auto& LODs = Result->GetArrayField(TEXT("lods"));
	if (!TestEqual(TEXT("all LOD mappings"), LODs.Num(), 2)) return false;
	TestEqual(TEXT("LOD zero material remap"), LODs[0]->AsObject()->GetArrayField(TEXT("sections"))[0]->AsObject()->GetIntegerField(TEXT("slotIndex")), 1);
	Request->SetNumberField(TEXT("lodIndex"), 1);
	auto Filtered = Registry.ExecuteHandler(TEXT("read_mesh_materials"), Request)->AsObject();
	TestEqual(TEXT("LOD filter"), Filtered->GetArrayField(TEXT("lods")).Num(), 1);
	TestEqual(TEXT("filtered LOD identity"), Filtered->GetArrayField(TEXT("lods"))[0]->AsObject()->GetIntegerField(TEXT("lodIndex")), 1);
	TestFalse(TEXT("Nanite disabled on empty mesh"), Result->GetBoolField(TEXT("naniteEnabled")));
	Request->SetNumberField(TEXT("lodIndex"), -1);
	auto Invalid = Registry.ExecuteHandler(TEXT("read_mesh_materials"), Request)->AsObject();
	TestFalse(TEXT("negative LOD refused"), Invalid->GetBoolField(TEXT("success")));
	Request->RemoveField(TEXT("lodIndex"));
	TStrongObjectPtr<USkeletalMesh> Skeletal(NewObject<USkeletalMesh>(Package, TEXT("Skeletal"), RF_Transient));
	Skeletal->GetMaterials().Add(FSkeletalMaterial(Inherited.Get(), true, false, TEXT("Skin"), TEXT("ImportedSkin")));
	Request->SetStringField(TEXT("assetPath"), Skeletal->GetPathName());
	auto SkelResult = Registry.ExecuteHandler(TEXT("read_mesh_materials"), Request)->AsObject();
	if (!TestTrue(TEXT("skeletal read succeeds"), SkelResult->GetBoolField(TEXT("success")))) return false;
	TestTrue(TEXT("skeletal slot resolves the same overrides"), SkelResult->GetArrayField(TEXT("slots"))[0]->AsObject()->GetBoolField(TEXT("twoSided")));
	TestFalse(TEXT("read did not dirty package"), Package->IsDirty());
	return true;
}

#endif
