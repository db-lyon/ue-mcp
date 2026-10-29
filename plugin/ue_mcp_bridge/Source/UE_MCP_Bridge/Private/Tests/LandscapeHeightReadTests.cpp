// Height queries must preserve the dirty state of their landscape package.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerEngineVersion.h"
#include "Handlers/Landscape/LandscapeHandlers.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace LandscapeHeightReadTests
{
	struct FWorldScope
	{
		UWorld* OriginalWorld = nullptr;
		UWorld* World = nullptr;
		UPackage* Package = nullptr;

		FWorldScope()
		{
			if (!GEditor) return;
			OriginalWorld = GEditor->GetEditorWorldContext().World();
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/UEMCP_LandscapeHeightRead_%s"),
				*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
			// Do not set RF_Transient here: it suppresses MarkPackageDirty and
			// would let the old, dirtying reader pass this test too.
			const UWorld::InitializationValues Initialization = UWorld::InitializationValues()
				.AllowAudioPlayback(false).RequiresHitProxies(false)
				.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
				.SetTransactional(false).CreateFXSystem(false).CreateWorldPartition(false);
			World = UWorld::CreateWorld(EWorldType::Editor, false, FName(TEXT("LandscapeHeightRead")),
				Package, true, ERHIFeatureLevel::Num, &Initialization);
			if (World) GEditor->GetEditorWorldContext().SetCurrentWorld(World);
		}

		~FWorldScope()
		{
			if (World)
			{
				GEditor->GetEditorWorldContext().SetCurrentWorld(OriginalWorld);
				World->DestroyWorld(false);
			}
			if (Package)
			{
				// Only this disposable fixture is cleared; no package is saved.
				Package->SetDirtyFlag(false);
				Package->MarkAsGarbage();
			}
		}
	};

	TSharedPtr<FJsonObject> Call(
		FAutomationTestBase& Test, FMCPHandlerRegistry& Registry,
		const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
	{
		const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(Method, Params);
		if (!Test.TestTrue(FString::Printf(TEXT("%s returns an object"), Method),
			Value.IsValid() && Value->Type == EJson::Object)) return nullptr;
		const TSharedPtr<FJsonObject> Result = Value->AsObject();
		bool bSuccess = false;
		if (!Test.TestTrue(FString::Printf(TEXT("%s succeeds"), Method),
			Result->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess)) return nullptr;
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandscapeHeightReadPreservesDirtyStateTest,
	"UE.MCP.Landscape.HeightRead.PreservesPackageDirtyState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLandscapeHeightReadPreservesDirtyStateTest::RunTest(const FString& Parameters)
{
	using namespace LandscapeHeightReadTests;
	if (!TestNotNull(TEXT("editor exists"), GEditor)) return false;
	if (!TestTrue(TEXT("test runs outside PIE"),
		GEditor->PlayWorld == nullptr && !GEditor->bIsSimulatingInEditor)) return false;
	FWorldScope Scope;
	if (!TestNotNull(TEXT("disposable editor world exists"), Scope.World)) return false;

	FMCPHandlerRegistry Registry;
	FLandscapeHandlers::RegisterHandlers(Registry);
	auto Create = MakeShared<FJsonObject>();
	Create->SetNumberField(TEXT("componentCountX"), 1);
	Create->SetNumberField(TEXT("componentCountY"), 1);
	Create->SetNumberField(TEXT("subsectionSizeQuads"), 7);
	Create->SetNumberField(TEXT("numSubsections"), 1);
	Create->SetNumberField(TEXT("heightOffset"), 33000);
	const auto Created = Call(*this, Registry, TEXT("create_landscape"), Create);
	if (!Created.IsValid()) return false;
	const FString ActorPath = Created->GetStringField(TEXT("actorPath"));
	ALandscape* Landscape = FindObject<ALandscape>(nullptr, *ActorPath);
	if (!TestNotNull(TEXT("created landscape exists"), Landscape)) return false;
	if (!TestTrue(TEXT("landscape belongs to the disposable world"),
		Landscape->GetWorld() == Scope.World)) return false;
	if (!TestEqual(TEXT("fixture has one component"), Landscape->LandscapeComponents.Num(), 1)) return false;
	UTexture2D* Heightmap = Landscape->LandscapeComponents[0]->GetHeightmap();
	if (!TestNotNull(TEXT("fixture has a heightmap texture"), Heightmap)) return false;
	if (!TestTrue(TEXT("heightmap dirties the observed fixture package"),
		Heightmap->GetPackage() == Scope.Package)) return false;
	// Prove the fixture can expose the regression rather than inheriting a
	// transient flag that suppresses the engine's Modify(true) dirtying path.
	Scope.Package->SetDirtyFlag(false);
	Heightmap->Modify();
	if (!TestTrue(TEXT("default texture Modify dirties the fixture"), Scope.Package->IsDirty())) return false;

	auto Region = MakeShared<FJsonObject>();
	Region->SetNumberField(TEXT("minX"), 1);
	Region->SetNumberField(TEXT("minY"), 1);
	Region->SetNumberField(TEXT("maxX"), 3);
	Region->SetNumberField(TEXT("maxY"), 3);
	auto Read = MakeShared<FJsonObject>();
	Read->SetStringField(TEXT("actorPath"), ActorPath);
	Read->SetObjectField(TEXT("region"), Region);

	for (bool bUseEditLayer : { false, true })
	{
#if UE_MCP_HAS_5_5_API
		if (bUseEditLayer) Read->SetNumberField(TEXT("editLayerIndex"), 0);
#else
		if (bUseEditLayer) continue;
#endif
		for (bool bInitiallyDirty : { false, true })
		{
			Scope.Package->SetDirtyFlag(bInitiallyDirty);
			const auto Result = Call(*this, Registry, TEXT("get_landscape_height_region"), Read);
			if (!Result.IsValid()) return false;
			const FString Context = FString::Printf(TEXT("%s read of %s package"),
				bUseEditLayer ? TEXT("edit-layer") : TEXT("merged"),
				bInitiallyDirty ? TEXT("dirty") : TEXT("clean"));
			TestEqual(Context + TEXT(" preserves its dirty flag"), Scope.Package->IsDirty(), bInitiallyDirty);
			TestEqual(Context + TEXT(" reports the selected source"),
				Result->GetBoolField(TEXT("merged")), !bUseEditLayer);
			const auto& Heights = Result->GetArrayField(TEXT("heights"));
			TestEqual(Context + TEXT(" returns all nine vertices"), Heights.Num(), 9);
			for (const auto& Height : Heights)
			{
				TestEqual(Context + TEXT(" preserves the height data"), Height->AsNumber(), 33000.0);
			}
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
