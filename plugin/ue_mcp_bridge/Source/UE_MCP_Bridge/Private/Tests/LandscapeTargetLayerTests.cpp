// Coverage for the landscape region writes on a landscape with more than one
// edit layer.
//
// set_height_region, sculpt_region, apply_erosion and import_heightmap read
// the "before" heights from the MERGED landscape and wrote into the edit layer
// named by editLayer. Edit layers add up, so as soon as a second layer held
// content the reports were wrong (heightBefore, verticesChanged, the rollback
// record), and sculpt_region / apply_erosion wrote merged heights into one
// layer, counting every other layer twice. A one-layer landscape hid it,
// because there merged and layer are the same heights.
//
// The fixture builds a 1-component landscape in a disposable world, gives it a
// second edit layer, and puts content in both, so merged != either layer.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerEngineVersion.h"

#if UE_MCP_HAS_5_5_API

#include "HandlerRegistry.h"
#include "Handlers/Landscape/LandscapeHandlers.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Landscape.h"
#include "LandscapeDataAccess.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "LandscapeInfo.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

namespace
{
	constexpr int32 MCPTestMid = 32768;   // a neutral height in every layer
	constexpr int32 MCPTestBase = MCPTestMid + 1000;
	constexpr int32 MCPTestUpper = MCPTestMid + 500;
	const TCHAR* MCPTestLabel = TEXT("UEMCP_TargetLayerTest");
	const TCHAR* MCPTestUpperName = TEXT("UEMCP_Upper");

	struct FMCPTargetLayerFixture
	{
		FAutomationTestBase& Test;
		UWorld* OriginalWorld = nullptr;
		UWorld* World = nullptr;
		FMCPHandlerRegistry Registry;
		ALandscape* Landscape = nullptr;
		FString BaseName;
		FGuid BaseGuid;
		FGuid UpperGuid;
		int32 Size = 0; // vertices per side

		explicit FMCPTargetLayerFixture(FAutomationTestBase& InTest) : Test(InTest)
		{
			if (!GEditor) return;
			OriginalWorld = GEditor->GetEditorWorldContext().World();
			const UWorld::InitializationValues Initialization = UWorld::InitializationValues()
				.AllowAudioPlayback(false).RequiresHitProxies(false)
				.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false)
				.ShouldSimulatePhysics(false).EnableTraceCollision(false).SetTransactional(false)
				.CreateFXSystem(false).CreateWorldPartition(false);
			World = UWorld::CreateWorld(EWorldType::Editor, false,
				MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("UEMCP_TargetLayerTest")),
				GetTransientPackage(), true, ERHIFeatureLevel::Num, &Initialization);
			if (!World) return;
			GEditor->GetEditorWorldContext().SetCurrentWorld(World);
			FLandscapeHandlers::RegisterHandlers(Registry);

			auto Create = MakeShared<FJsonObject>();
			Create->SetNumberField(TEXT("componentCountX"), 1);
			Create->SetNumberField(TEXT("componentCountY"), 1);
			Create->SetNumberField(TEXT("subsectionSizeQuads"), 7);
			Create->SetNumberField(TEXT("numSubsections"), 1);
			Create->SetStringField(TEXT("label"), MCPTestLabel);
			if (!Succeeds(TEXT("create_landscape"), Create)) return;

			for (TActorIterator<ALandscape> It(World); It; ++It)
			{
				if (It->GetActorLabel() == MCPTestLabel) { Landscape = *It; break; }
			}
			if (!Test.TestNotNull(TEXT("the landscape is in the disposable world"), Landscape)) return;
			Size = 8;

			const ULandscapeEditLayerBase* Base = Landscape->GetEditLayerConst(0);
			if (!Test.TestNotNull(TEXT("the landscape has a base edit layer"), Base)) { Landscape = nullptr; return; }
			BaseName = Base->GetName().ToString();
			BaseGuid = Base->GetGuid();
			const int32 UpperIndex = Landscape->CreateLayer(FName(MCPTestUpperName));
			const ULandscapeEditLayerBase* Upper = UpperIndex == INDEX_NONE ? nullptr : Landscape->GetEditLayerConst(UpperIndex);
			if (!Test.TestNotNull(TEXT("a second edit layer can be created"), Upper)) { Landscape = nullptr; return; }
			UpperGuid = Upper->GetGuid();
		}

		~FMCPTargetLayerFixture()
		{
			if (!World) return;
			GEditor->GetEditorWorldContext().SetCurrentWorld(OriginalWorld);
			World->DestroyWorld(false);
		}

		bool Ready() const { return Landscape != nullptr; }

		TSharedPtr<FJsonObject> Run(const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
		{
			const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, Params);
			if (!Response.IsValid() || Response->Type != EJson::Object) return nullptr;
			return Response->AsObject();
		}

		bool Succeeds(const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
		{
			const TSharedPtr<FJsonObject> Response = Run(Method, Params);
			bool bSuccess = false;
			const bool bOk = Response.IsValid() && Response->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
			FString Error;
			if (!bOk && Response.IsValid()) Response->TryGetStringField(TEXT("error"), Error);
			return Test.TestTrue(FString::Printf(TEXT("%s succeeds%s%s"), Method, Error.IsEmpty() ? TEXT("") : TEXT(": "), *Error), bOk);
		}

		/** A request on the whole landscape, written into one edit layer. */
		TSharedPtr<FJsonObject> Request(const FString& EditLayer) const
		{
			auto Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("actorLabel"), MCPTestLabel);
			auto Region = MakeShared<FJsonObject>();
			Region->SetNumberField(TEXT("minX"), 0);
			Region->SetNumberField(TEXT("minY"), 0);
			Region->SetNumberField(TEXT("maxX"), Size - 1);
			Region->SetNumberField(TEXT("maxY"), Size - 1);
			Params->SetObjectField(TEXT("region"), Region);
			if (!EditLayer.IsEmpty()) Params->SetStringField(TEXT("editLayer"), EditLayer);
			return Params;
		}

		/** Every layer re-merged now, as the next editor tick would. */
		void Merge()
		{
			Landscape->ForceLayersFullUpdate();
			Landscape->ForceUpdateLayersContent();
		}

		/** Heights over the whole landscape: one layer's, or merged when Guid is invalid. */
		TArray<uint16> Heights(const FGuid& Guid)
		{
			TArray<uint16> Out;
			Out.SetNumZeroed(Size * Size);
			FLandscapeEditDataInterface EditData(Landscape->GetLandscapeInfo());
			if (Guid.IsValid()) EditData.SetEditLayer(Guid);
			int32 X1 = 0, Y1 = 0, X2 = Size - 1, Y2 = Size - 1;
			EditData.GetHeightData(X1, Y1, X2, Y2, Out.GetData(), 0);
			return Out;
		}

		uint16 At(const TArray<uint16>& Heights, int32 X, int32 Y) const { return Heights[Y * Size + X]; }

		/** World Z of a raw merged height, for the operators that take one. */
		double RawToWorldZ(int32 Raw) const
		{
			return Landscape->GetActorLocation().Z + (Raw - MCPTestMid) * LANDSCAPE_ZSCALE * Landscape->GetActorScale3D().Z;
		}

		/** Base at +1000, the second layer at +500, merged: the state every case starts from. */
		bool Seed()
		{
			auto BaseFill = Request(BaseName);
			BaseFill->SetNumberField(TEXT("rawHeight"), MCPTestBase);
			auto UpperFill = Request(MCPTestUpperName);
			UpperFill->SetNumberField(TEXT("rawHeight"), MCPTestUpper);
			if (!Succeeds(TEXT("set_landscape_height_region"), BaseFill)) return false;
			if (!Succeeds(TEXT("set_landscape_height_region"), UpperFill)) return false;
			Merge();
			// The model the fix rests on: layers add, each relative to the neutral height.
			const TArray<uint16> Merged = Heights(FGuid());
			return Test.TestEqual(TEXT("edit layers add: merged = base + (upper - neutral)"),
				(int32)At(Merged, 3, 3), MCPTestBase + (MCPTestUpper - MCPTestMid));
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLandscapeSetHeightRegionReportsTheTargetLayerTest,
	"UE.MCP.Landscape.TargetLayer.SetHeightRegionReportsTheTargetLayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLandscapeSetHeightRegionReportsTheTargetLayerTest::RunTest(const FString& Parameters)
{
	FMCPTargetLayerFixture F(*this);
	if (!F.Ready() || !F.Seed()) return false;

	// Writing the second layer the value it already holds changes nothing. Read
	// against the merged heights it looked like every vertex changed, and the
	// rollback record carried merged heights that a replay would write into the layer.
	auto Same = F.Request(MCPTestUpperName);
	Same->SetNumberField(TEXT("rawHeight"), MCPTestUpper);
	const TSharedPtr<FJsonObject> Response = F.Run(TEXT("set_landscape_height_region"), Same);
	if (!TestTrue(TEXT("set_landscape_height_region answers"), Response.IsValid())) return false;
	double Changed = -1;
	Response->TryGetNumberField(TEXT("verticesChanged"), Changed);
	TestEqual(TEXT("rewriting the layer's own value changes no vertex"), Changed, 0.0);
	bool bUnchanged = false;
	TestTrue(TEXT("and says unchanged"), Response->TryGetBoolField(TEXT("unchanged"), bUnchanged) && bUnchanged);
	const TSharedPtr<FJsonObject>* Before = nullptr;
	double MinRaw = -1;
	if (Response->TryGetObjectField(TEXT("heightBefore"), Before) && Before) (*Before)->TryGetNumberField(TEXT("minRaw"), MinRaw);
	TestEqual(TEXT("heightBefore is the layer's height, not the merged one"), MinRaw, (double)MCPTestUpper);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLandscapeSculptRegionWritesOnlyItsChangeTest,
	"UE.MCP.Landscape.TargetLayer.SculptRegionWritesOnlyItsChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLandscapeSculptRegionWritesOnlyItsChangeTest::RunTest(const FString& Parameters)
{
	FMCPTargetLayerFixture F(*this);
	if (!F.Ready() || !F.Seed()) return false;

	// Flatten the TERRAIN to 500 raw above where it stands, working in the
	// second layer. The base layer must not move, and the ground must end at
	// the target: writing the merged result into the second layer would instead
	// land it a whole base layer (1000 raw) too high.
	const int32 Target = MCPTestBase + (MCPTestUpper - MCPTestMid) + 500;
	auto Flatten = F.Request(MCPTestUpperName);
	Flatten->SetStringField(TEXT("operator"), TEXT("flatten"));
	Flatten->SetNumberField(TEXT("targetHeight"), F.RawToWorldZ(Target));
	Flatten->SetStringField(TEXT("shape"), TEXT("rect"));
	Flatten->SetNumberField(TEXT("falloff"), 0.0);
	if (!F.Succeeds(TEXT("sculpt_landscape_region"), Flatten)) return false;
	F.Merge();

	const TArray<uint16> Base = F.Heights(F.BaseGuid);
	const TArray<uint16> Upper = F.Heights(F.UpperGuid);
	const TArray<uint16> Merged = F.Heights(FGuid());
	TestEqual(TEXT("the base layer is untouched"), (int32)F.At(Base, 3, 3), MCPTestBase);
	TestTrue(FString::Printf(TEXT("the terrain ends at the target (%d), not a layer too high (got %d)"), Target, (int32)F.At(Merged, 3, 3)),
		FMath::Abs((int32)F.At(Merged, 3, 3) - Target) <= 1);
	TestTrue(FString::Printf(TEXT("the second layer took only the change: %d + 500 (got %d)"), MCPTestUpper, (int32)F.At(Upper, 3, 3)),
		FMath::Abs((int32)F.At(Upper, 3, 3) - (MCPTestUpper + 500)) <= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLandscapeErosionWritesOnlyItsChangeTest,
	"UE.MCP.Landscape.TargetLayer.ErosionWritesOnlyItsChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLandscapeErosionWritesOnlyItsChangeTest::RunTest(const FString& Parameters)
{
	FMCPTargetLayerFixture F(*this);
	if (!F.Ready() || !F.Seed()) return false;

	// A steep spike in the second layer for thermal erosion to slump. Erosion
	// moves ground, it does not make it, so the terrain's total height is kept
	// (bar rounding). Writing the merged result into the layer would instead
	// lift every vertex it touched by the base layer's 1000 raw.
	auto Spike = F.Request(MCPTestUpperName);
	auto Point = MakeShared<FJsonObject>();
	Point->SetNumberField(TEXT("minX"), 3);
	Point->SetNumberField(TEXT("minY"), 3);
	Point->SetNumberField(TEXT("maxX"), 3);
	Point->SetNumberField(TEXT("maxY"), 3);
	Spike->SetObjectField(TEXT("region"), Point);
	Spike->SetNumberField(TEXT("rawHeight"), MCPTestUpper + 2000);
	if (!F.Succeeds(TEXT("set_landscape_height_region"), Spike)) return false;
	F.Merge();

	auto Total = [](const TArray<uint16>& Heights) { int64 Sum = 0; for (uint16 H : Heights) Sum += H; return Sum; };
	const int64 Before = Total(F.Heights(FGuid()));
	auto Erode = F.Request(MCPTestUpperName);
	Erode->SetStringField(TEXT("erosionType"), TEXT("thermal"));
	Erode->SetNumberField(TEXT("iterations"), 5);
	if (!F.Succeeds(TEXT("apply_landscape_erosion"), Erode)) return false;
	F.Merge();

	const TArray<uint16> Base = F.Heights(F.BaseGuid);
	const TArray<uint16> Merged = F.Heights(FGuid());
	TestEqual(TEXT("the base layer is untouched"), (int32)F.At(Base, 3, 3), MCPTestBase);
	TestTrue(TEXT("the spike was eroded"), (int32)F.At(Merged, 3, 3) < MCPTestBase + (MCPTestUpper - MCPTestMid) + 2000);
	const int64 Drift = Total(Merged) - Before;
	TestTrue(FString::Printf(TEXT("erosion keeps the terrain's total height (drift %lld raw over %d vertices)"), Drift, Merged.Num()),
		FMath::Abs(Drift) <= 200);
	return true;
}

#endif // UE_MCP_HAS_5_5_API
#endif // WITH_DEV_AUTOMATION_TESTS
