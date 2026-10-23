#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/Asset/AssetHandlers_Geometry.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/MCPScopedTestMount.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
TStrongObjectPtr<UStaticMesh> MakeInspectionFixture()
{
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/MCPMeshInspection_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	TStrongObjectPtr<UStaticMesh> Mesh(NewObject<UStaticMesh>(Package, TEXT("Mesh"), RF_Transient));
	Mesh->SetNumSourceModels(1);
	Mesh->GetSourceModel(0).BuildSettings.BuildScale3D = FVector(2, 3, 4);
	Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("DisplayedA"), TEXT("ImportedA")));
	Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("DisplayedB"), TEXT("ImportedB")));
	FMeshDescription* Description = Mesh->CreateMeshDescription(0);
	FStaticMeshAttributes Attributes(*Description);
	Attributes.Register();
	Attributes.GetVertexInstanceUVs().SetNumChannels(2);
	const FVertexID Removed = Description->CreateVertex();
	TArray<FVertexID> Vertices;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FVertexID V = Description->CreateVertex();
		Attributes.GetVertexPositions()[V] = FVector3f(Index, Index * 2, Index * 3);
		Vertices.Add(V);
	}
	Description->DeleteVertex(Removed); // Sparse IDs must survive pagination.
	TArray<FVertexInstanceID> Corners;
	for (int32 Index = 0; Index < 7; ++Index)
	{
		const FVertexInstanceID C = Description->CreateVertexInstance(Vertices[Index % 3]);
		Attributes.GetVertexInstanceNormals()[C] = Index < 3 ? FVector3f(0, 0, 1) : FVector3f(0, 1, 0);
		Attributes.GetVertexInstanceTangents()[C] = FVector3f(1, 0, 0);
		Attributes.GetVertexInstanceBinormalSigns()[C] = Index < 3 ? 1.0f : -1.0f;
		Attributes.GetVertexInstanceColors()[C] = FVector4f(0.25f, 0.5f, 0.75f, Index / 8.0f);
		Attributes.GetVertexInstanceUVs().Set(C, 0, FVector2f(Index / 8.0f, 0.25f));
		Attributes.GetVertexInstanceUVs().Set(C, 1, FVector2f(10 + Index, -2));
		Corners.Add(C);
	}
	const FPolygonGroupID A = Description->CreatePolygonGroup();
	const FPolygonGroupID B = Description->CreatePolygonGroup();
	// Swapped names deliberately prove that group IDs are not material indices.
	Attributes.GetPolygonGroupMaterialSlotNames()[A] = TEXT("ImportedB");
	Attributes.GetPolygonGroupMaterialSlotNames()[B] = TEXT("ImportedA");
	Description->CreateTriangle(A, MakeArrayView(Corners.GetData(), 3));
	TArray<FVertexInstanceID> Reversed = { Corners[5], Corners[4], Corners[3] };
	Description->CreateTriangle(B, Reversed);
	// Vertex 4 and corner 6 remain unreferenced, intentionally.

	// Independent CPU render data with a different position and only one triangle.
	// No StaticMesh::Build, RHI initialization, package save or project content.
	auto Render = MakeUnique<FStaticMeshRenderData>();
	Render->LODResources.Add(new FStaticMeshLODResources());
	FStaticMeshLODResources& LOD = Render->LODResources[0];
	LOD.VertexBuffers.PositionVertexBuffer.Init(3);
	LOD.VertexBuffers.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(true);
	LOD.VertexBuffers.StaticMeshVertexBuffer.Init(3, 2);
	LOD.VertexBuffers.ColorVertexBuffer.Init(3);
	for (uint32 Index = 0; Index < 3; ++Index)
	{
		LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Index) = FVector3f(100 + Index, 0, 0);
		LOD.VertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(Index, FVector3f(1, 0, 0), FVector3f(0, 1, 0), FVector3f(0, 0, 1));
		LOD.VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(Index, 0, FVector2f(0.5f, 0.25f));
		LOD.VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(Index, 1, FVector2f(3, 4));
		LOD.VertexBuffers.ColorVertexBuffer.VertexColor(Index) = FColor(64, 128, 192, 32);
	}
	LOD.IndexBuffer.SetIndices({ 2, 1, 0 }, EIndexBufferStride::Force32Bit);
	FStaticMeshSection Section;
	Section.MaterialIndex = 1; Section.FirstIndex = 0; Section.NumTriangles = 1;
	Section.MinVertexIndex = 0; Section.MaxVertexIndex = 2;
	LOD.Sections.Add(Section);
	Mesh->SetRenderData(MoveTemp(Render));
	Package->SetDirtyFlag(false);
	return Mesh;
}

TSharedPtr<FJsonObject> InspectionCall(FMCPHandlerRegistry& Registry, const TSharedPtr<FJsonObject>& Request)
{
	const auto Response = Registry.ExecuteHandler(TEXT("get_static_mesh_geometry"), Request);
	return Response.IsValid() && Response->Type == EJson::Object ? Response->AsObject() : nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStaticMeshInspectionSourceTest,
	"UE.MCP.Asset.StaticMeshInspection.SourceAttributesAndSnapshot", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStaticMeshInspectionSourceTest::RunTest(const FString& Parameters)
{
	auto Mesh = MakeInspectionFixture();
	FMCPHandlerRegistry Registry;
	FAssetGeometryHandlers::RegisterHandlers(Registry);
	auto Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
	Request->SetStringField(TEXT("geometrySource"), TEXT("sourceModel"));
	Request->SetNumberField(TEXT("pageSize"), 1);
	auto First = InspectionCall(Registry, Request);
	if (!First || !TestTrue(TEXT("source capture succeeds"), First->GetBoolField(TEXT("success")))) return false;
	TestEqual(TEXT("all source vertices including loose vertex"), First->GetIntegerField(TEXT("vertexCount")), 4);
	TestEqual(TEXT("all source corners including attribute-only corner"), First->GetIntegerField(TEXT("cornerCount")), 7);
	TestEqual(TEXT("all source triangles"), First->GetIntegerField(TEXT("triangleCount")), 2);
	TestEqual(TEXT("all UV channels"), First->GetIntegerField(TEXT("uvChannelCount")), 2);
	TestEqual(TEXT("page is bounded"), First->GetArrayField(TEXT("elements")).Num(), 1);
	TestEqual(TEXT("continuation offset"), First->GetIntegerField(TEXT("nextOffset")), 1);
	TestTrue(TEXT("fresh capture says current state checked"), First->GetBoolField(TEXT("currentAssetStateChecked")));
	const auto& Group = First->GetObjectField(TEXT("metadata"))->GetArrayField(TEXT("groups"))[0]->AsObject();
	TestEqual(TEXT("group zero resolves to material one"), Group->GetArrayField(TEXT("materialSlotMatches"))[0]->AsNumber(), 1.0);
	Request->SetStringField(TEXT("snapshotId"), First->GetStringField(TEXT("snapshotId")));
	Request->SetNumberField(TEXT("elementOffset"), 1);
	auto Last = InspectionCall(Registry, Request);
	if (!Last || !Last->GetBoolField(TEXT("success"))) return false;
	const auto& Triangle = Last->GetArrayField(TEXT("elements"))[0]->AsObject();
	TestEqual(TEXT("winding retained at first corner"), Triangle->GetArrayField(TEXT("cornerIds"))[0]->AsNumber(), 5.0);
	TestEqual(TEXT("winding retained at last corner"), Triangle->GetArrayField(TEXT("cornerIds"))[2]->AsNumber(), 3.0);
	TestFalse(TEXT("last page has no continuation"), Last->GetBoolField(TEXT("hasMore")));
	TestTrue(TEXT("terminal nextOffset is null"), Last->HasTypedField<EJson::Null>(TEXT("nextOffset")));
	TestFalse(TEXT("cached read does not claim current asset validation"), Last->GetBoolField(TEXT("currentAssetStateChecked")));
	Request->SetStringField(TEXT("elementType"), TEXT("corners"));
	Request->SetNumberField(TEXT("elementOffset"), 3);
	auto CornerPage = InspectionCall(Registry, Request);
	const auto& Corner = CornerPage->GetArrayField(TEXT("elements"))[0]->AsObject();
	TestEqual(TEXT("seam normal preserved"), Corner->GetObjectField(TEXT("normal"))->GetNumberField(TEXT("y")), 1.0);
	TestEqual(TEXT("binormal sign preserved"), Corner->GetNumberField(TEXT("binormalSign")), -1.0);
	TestEqual(TEXT("second UV channel preserved"), Corner->GetArrayField(TEXT("uvs"))[1]->AsObject()->GetNumberField(TEXT("u")), 13.0);
	TestEqual(TEXT("RGBA alpha preserved"), Corner->GetObjectField(TEXT("color"))->GetNumberField(TEXT("a")), 3.0 / 8.0);
	Request->SetNumberField(TEXT("elementOffset"), 6);
	auto LooseCorner = InspectionCall(Registry, Request);
	TestEqual(TEXT("unreferenced corner retrievable"), LooseCorner->GetArrayField(TEXT("elements"))[0]->AsObject()->GetIntegerField(TEXT("id")), 6);
	Request->SetStringField(TEXT("elementType"), TEXT("vertices"));
	Request->SetNumberField(TEXT("elementOffset"), 0);
	auto VertexPage = InspectionCall(Registry, Request);
	TestEqual(TEXT("sparse ID not compacted"), VertexPage->GetArrayField(TEXT("elements"))[0]->AsObject()->GetIntegerField(TEXT("id")), 1);
	TestEqual(TEXT("build scale not applied to source position"), VertexPage->GetArrayField(TEXT("elements"))[0]->AsObject()->GetObjectField(TEXT("position"))->GetNumberField(TEXT("x")), 0.0);
	TestFalse(TEXT("capture never dirties the package"), Mesh->GetOutermost()->IsDirty());

	FStaticMeshAttributes Changed(*Mesh->GetMeshDescription(0));
	Changed.GetVertexInstanceUVs().Set(FVertexInstanceID(3), 1, FVector2f(99, 100));
	Request->SetStringField(TEXT("elementType"), TEXT("corners"));
	Request->SetNumberField(TEXT("elementOffset"), 3);
	auto Frozen = InspectionCall(Registry, Request);
	TestEqual(TEXT("edit does not change cached attributes"), Frozen->GetArrayField(TEXT("elements"))[0]->AsObject()->GetArrayField(TEXT("uvs"))[1]->AsObject()->GetNumberField(TEXT("u")), 13.0);
	TestEqual(TEXT("cached revision remains identical"), Frozen->GetStringField(TEXT("contentRevision")), First->GetStringField(TEXT("contentRevision")));
	Request->RemoveField(TEXT("snapshotId")); Request->SetNumberField(TEXT("elementOffset"), 0);
	auto Fresh = InspectionCall(Registry, Request);
	TestNotEqual(TEXT("new capture fingerprints attribute-only edit"), Fresh->GetStringField(TEXT("contentRevision")), First->GetStringField(TEXT("contentRevision")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStaticMeshInspectionRenderTest,
	"UE.MCP.Asset.StaticMeshInspection.RenderAttributesAndErrors", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStaticMeshInspectionRenderTest::RunTest(const FString& Parameters)
{
	auto Mesh = MakeInspectionFixture();
	FMCPHandlerRegistry Registry;
	FAssetGeometryHandlers::RegisterHandlers(Registry);
	auto Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
	Request->SetStringField(TEXT("geometrySource"), TEXT("renderData"));
	Request->SetStringField(TEXT("elementType"), TEXT("corners"));
	auto First = InspectionCall(Registry, Request);
	if (!First || !TestTrue(TEXT("render capture succeeds"), First->GetBoolField(TEXT("success")))) return false;
	TestEqual(TEXT("render count differs from source"), First->GetIntegerField(TEXT("triangleCount")), 1);
	const auto& Corner = First->GetArrayField(TEXT("elements"))[0]->AsObject();
	TestEqual(TEXT("render UV channel 1"), Corner->GetArrayField(TEXT("uvs"))[1]->AsObject()->GetNumberField(TEXT("v")), 4.0);
	TestTrue(TEXT("raw color bytes normalized without sRGB conversion"), FMath::IsNearlyEqual(Corner->GetObjectField(TEXT("color"))->GetNumberField(TEXT("r")), 64.0 / 255.0, 0.000001));
	TestTrue(TEXT("render alpha retained"), FMath::IsNearlyEqual(Corner->GetObjectField(TEXT("color"))->GetNumberField(TEXT("a")), 32.0 / 255.0, 0.000001));
	Request->SetStringField(TEXT("snapshotId"), First->GetStringField(TEXT("snapshotId")));
	Request->SetStringField(TEXT("assetPath"), TEXT("/Game/AnotherMesh"));
	TestEqual(TEXT("asset identity cannot be switched"), InspectionCall(Registry, Request)->GetStringField(TEXT("reason")), FString(TEXT("snapshot_mismatch")));
	Request->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
	Request->SetStringField(TEXT("geometrySource"), TEXT("sourceModel"));
	TestEqual(TEXT("source cannot reuse render snapshot"), InspectionCall(Registry, Request)->GetStringField(TEXT("reason")), FString(TEXT("snapshot_mismatch")));
	Request->SetStringField(TEXT("geometrySource"), TEXT("renderData"));
	Request->SetStringField(TEXT("elementType"), TEXT("vertices"));
	auto Vertices = InspectionCall(Registry, Request);
	TestEqual(TEXT("render positions never substituted from source"), Vertices->GetArrayField(TEXT("elements"))[0]->AsObject()->GetObjectField(TEXT("position"))->GetNumberField(TEXT("x")), 100.0);
	Request->SetStringField(TEXT("elementType"), TEXT("triangles"));
	auto Triangles = InspectionCall(Registry, Request);
	TestEqual(TEXT("render winding preserved"), Triangles->GetArrayField(TEXT("elements"))[0]->AsObject()->GetArrayField(TEXT("cornerIds"))[0]->AsNumber(), 2.0);
	TestEqual(TEXT("render material index"), Triangles->GetObjectField(TEXT("metadata"))->GetArrayField(TEXT("groups"))[0]->AsObject()->GetIntegerField(TEXT("materialIndex")), 1);
	Request->SetNumberField(TEXT("elementOffset"), 2);
	TestEqual(TEXT("offset past table refused"), InspectionCall(Registry, Request)->GetStringField(TEXT("reason")), FString(TEXT("offset_out_of_range")));
	Request->SetNumberField(TEXT("elementOffset"), 1);
	Request->RemoveField(TEXT("snapshotId"));
	TestEqual(TEXT("no silent recapture for continuation"), InspectionCall(Registry, Request)->GetStringField(TEXT("reason")), FString(TEXT("snapshot_required")));
	Request->SetNumberField(TEXT("elementOffset"), 0);
	Request->SetStringField(TEXT("snapshotId"), TEXT("expired"));
	TestEqual(TEXT("unknown or expired ID refused"), InspectionCall(Registry, Request)->GetStringField(TEXT("reason")), FString(TEXT("snapshot_unavailable")));
	Request->SetNumberField(TEXT("snapshotId"), 0);
	TestEqual(TEXT("malformed ID never recaptures"), InspectionCall(Registry, Request)->GetStringField(TEXT("reason")), FString(TEXT("invalid_snapshot")));
	Request->RemoveField(TEXT("snapshotId"));
	for (double Size : { 0.0, 1.5, 513.0, 1e30 })
	{
		Request->SetNumberField(TEXT("pageSize"), Size);
		TestEqual(TEXT("invalid page size refused"), InspectionCall(Registry, Request)->GetStringField(TEXT("reason")), FString(TEXT("invalid_page")));
	}
	Request->RemoveField(TEXT("pageSize"));
	Request->SetNumberField(TEXT("lodIndex"), 1);
	TestEqual(TEXT("invalid render LOD refused"), InspectionCall(Registry, Request)->GetStringField(TEXT("reason")), FString(TEXT("lod_out_of_range")));
	Request->RemoveField(TEXT("lodIndex"));
	// Four more fresh captures evict the first regardless of any earlier test captures.
	for (int32 Index = 0; Index < 4; ++Index) InspectionCall(Registry, Request);
	Request->SetStringField(TEXT("snapshotId"), First->GetStringField(TEXT("snapshotId")));
	TestEqual(TEXT("eviction never silently recaptures"), InspectionCall(Registry, Request)->GetStringField(TEXT("reason")), FString(TEXT("snapshot_unavailable")));
	Request->RemoveField(TEXT("snapshotId"));
	Mesh->SetNumSourceModels(2);
	Request->SetStringField(TEXT("geometrySource"), TEXT("sourceModel"));
	Request->SetNumberField(TEXT("lodIndex"), 1);
	TestEqual(TEXT("missing source description never falls back to render"), InspectionCall(Registry, Request)->GetStringField(TEXT("reason")), FString(TEXT("source_unavailable")));
	Request->RemoveField(TEXT("lodIndex"));
	Request->SetStringField(TEXT("geometrySource"), TEXT("renderData"));
	Request->SetStringField(TEXT("elementType"), TEXT("corners"));
	Mesh->GetRenderData()->LODResources[0].VertexBuffers.ColorVertexBuffer.Init(0);
	auto NoColors = InspectionCall(Registry, Request);
	TestTrue(TEXT("missing render colors explicitly null"), NoColors->GetArrayField(TEXT("elements"))[0]->AsObject()->HasTypedField<EJson::Null>(TEXT("color")));
	TestFalse(TEXT("missing colors not claimed available"), NoColors->GetObjectField(TEXT("metadata"))->GetBoolField(TEXT("hasColorAttribute")));
	Mesh->GetRenderData()->LODResources[0].VertexBuffers.StaticMeshVertexBuffer.Init(0, 2);
	TestEqual(TEXT("missing required CPU buffer refused"), InspectionCall(Registry, Request)->GetStringField(TEXT("reason")), FString(TEXT("cpu_data_unavailable")));
	TestFalse(TEXT("render inspections never dirty the package"), Mesh->GetOutermost()->IsDirty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStaticMeshInspectionExportTest,
	"UE.MCP.Asset.StaticMeshInspection.CompleteSnapshotExport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStaticMeshInspectionExportTest::RunTest(const FString& Parameters)
{
	const FMCPScopedTestMount Mount{ TEXT("/UEMCPMeshExport/"), TEXT("UEMCPMeshExport") };
	auto Mesh = MakeInspectionFixture();
	FMCPHandlerRegistry Registry;
	FAssetGeometryHandlers::RegisterHandlers(Registry);
	auto Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
	Request->SetStringField(TEXT("geometrySource"), TEXT("sourceModel"));
	auto Capture = InspectionCall(Registry, Request);
	if (!Capture || !Capture->GetBoolField(TEXT("success"))) return false;
	const FString Output = FPaths::Combine(Mount.ContentPath, TEXT("source.json"));
	Request->SetStringField(TEXT("snapshotId"), Capture->GetStringField(TEXT("snapshotId")));
	Request->SetBoolField(TEXT("dumpToFile"), true);
	Request->SetStringField(TEXT("outputPath"), Output);
	FStaticMeshAttributes Changed(*Mesh->GetMeshDescription(0));
	Changed.GetVertexInstanceUVs().Set(FVertexInstanceID(3), 1, FVector2f(99, 100));
	auto Export = InspectionCall(Registry, Request);
	if (!Export || !TestTrue(TEXT("full export succeeds"), Export->GetBoolField(TEXT("success")))) return false;
	TestFalse(TEXT("receipt excludes geometry payload"), Export->HasField(TEXT("elements")) || Export->HasField(TEXT("vertices")) || Export->HasField(TEXT("corners")));
	TestEqual(TEXT("receipt keeps exact capture identity"), Export->GetStringField(TEXT("snapshotId")), Capture->GetStringField(TEXT("snapshotId")));
	TestEqual(TEXT("receipt keeps exact capture revision"), Export->GetStringField(TEXT("contentRevision")), Capture->GetStringField(TEXT("contentRevision")));
	TestFalse(TEXT("cached export does not claim fresh state"), Export->GetBoolField(TEXT("currentAssetStateChecked")));
	FString Text;
	if (!TestTrue(TEXT("file readable"), FFileHelper::LoadFileToString(Text, *Output))) return false;
	TSharedPtr<FJsonObject> Document;
	if (!TestTrue(TEXT("export is valid JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Document))) return false;
	TestTrue(TEXT("completion marker present"), Document->GetBoolField(TEXT("exportComplete")));
	TestEqual(TEXT("file identity exact"), Document->GetStringField(TEXT("snapshotId")), Capture->GetStringField(TEXT("snapshotId")));
	TestEqual(TEXT("file revision exact"), Document->GetStringField(TEXT("contentRevision")), Capture->GetStringField(TEXT("contentRevision")));
	TestEqual(TEXT("capture time preserved"), Document->GetStringField(TEXT("capturedUtc")), Capture->GetStringField(TEXT("capturedUtc")));
	TestEqual(TEXT("all loose and used vertices exported"), Document->GetArrayField(TEXT("vertices")).Num(), 4);
	TestEqual(TEXT("all loose and used corners exported"), Document->GetArrayField(TEXT("corners")).Num(), 7);
	TestEqual(TEXT("all triangles exported"), Document->GetArrayField(TEXT("triangles")).Num(), 2);
	const auto& Corner = Document->GetArrayField(TEXT("corners"))[3]->AsObject();
	TestEqual(TEXT("all UV channels exported"), Corner->GetArrayField(TEXT("uvs")).Num(), 2);
	TestEqual(TEXT("cached UV exported despite later edit"), Corner->GetArrayField(TEXT("uvs"))[1]->AsObject()->GetNumberField(TEXT("u")), 13.0);
	TestEqual(TEXT("normal exported"), Corner->GetObjectField(TEXT("normal"))->GetNumberField(TEXT("y")), 1.0);
	TestEqual(TEXT("tangent exported"), Corner->GetObjectField(TEXT("tangent"))->GetNumberField(TEXT("x")), 1.0);
	TestEqual(TEXT("binormal sign exported"), Corner->GetNumberField(TEXT("binormalSign")), -1.0);
	TestEqual(TEXT("alpha exported"), Corner->GetObjectField(TEXT("color"))->GetNumberField(TEXT("a")), 3.0 / 8.0);
	TestEqual(TEXT("oriented triangle exported"), Document->GetArrayField(TEXT("triangles"))[1]->AsObject()->GetArrayField(TEXT("cornerIds"))[0]->AsNumber(), 5.0);
	TestEqual(TEXT("metadata groups exported"), Document->GetObjectField(TEXT("metadata"))->GetArrayField(TEXT("groups")).Num(), 2);
	TestEqual(TEXT("metadata material slots exported"), Document->GetObjectField(TEXT("metadata"))->GetArrayField(TEXT("materials")).Num(), 2);
	TestTrue(TEXT("build settings exported"), Document->GetObjectField(TEXT("metadata"))->HasTypedField<EJson::Object>(TEXT("sourceBuildSettings")));
	TestEqual(TEXT("receipt byte count exact"), Export->GetNumberField(TEXT("bytes")), static_cast<double>(IFileManager::Get().FileSize(*Output)));
	auto Conflict = InspectionCall(Registry, Request);
	TestEqual(TEXT("existing output refused"), Conflict->GetStringField(TEXT("reason")), FString(TEXT("output_exists")));
	FString After;
	FFileHelper::LoadFileToString(After, *Output);
	TestEqual(TEXT("conflicting output unmodified"), After, Text);
	Request->SetNumberField(TEXT("pageSize"), 1);
	TestEqual(TEXT("export never silently accepts partial table selection"), InspectionCall(Registry, Request)->GetStringField(TEXT("reason")), FString(TEXT("invalid_export")));
	Request->RemoveField(TEXT("pageSize")); Request->RemoveField(TEXT("snapshotId"));
	Request->SetStringField(TEXT("outputPath"), FPaths::Combine(Mount.ContentPath, TEXT("fresh.json")));
	auto Fresh = InspectionCall(Registry, Request);
	TestTrue(TEXT("fresh full export reports current state checked"), Fresh->GetBoolField(TEXT("currentAssetStateChecked")));
	TestNotEqual(TEXT("fresh export picks up edited attribute"), Fresh->GetStringField(TEXT("contentRevision")), Capture->GetStringField(TEXT("contentRevision")));
	Request->SetStringField(TEXT("geometrySource"), TEXT("renderData"));
	Request->SetStringField(TEXT("outputPath"), FPaths::Combine(Mount.ContentPath, TEXT("render.json")));
	auto Render = InspectionCall(Registry, Request);
	TestEqual(TEXT("render export uses independent topology"), Render->GetIntegerField(TEXT("triangleCount")), 1);
	TestFalse(TEXT("export never dirties the mesh"), Mesh->GetOutermost()->IsDirty());
	return true;
}

#endif
