// The response of the Foliage, Landscape, PCG, Reflection and Sequencer
// creators and of add_pcg_volume, recorded before the F4 and F6 port and held
// to the contract after it (plans/domain-design.md 0 and 8). The fixture is
// tests/golden/cpp/misc-create-responses.json; a run without it records it.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerUtils.h"
#include "Handlers/Foliage/FoliageHandlers.h"
#include "Handlers/Landscape/LandscapeHandlers.h"
#include "Handlers/PCG/PCGHandlers.h"
#include "Handlers/Reflection/ReflectionHandlers.h"
#include "Handlers/Sequencer/SequencerHandlers.h"
#include "Tests/MCPCharacterisationRun.h"
#include "Tests/MCPScopedTestMount.h"

class FMCPMiscCreateCharTestBase : public FAutomationTestBase
{
public:
	FMCPMiscCreateCharTestBase(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{
	}

	virtual TArray<FString> GetSuppressedLogCategories() override
	{
		TArray<FString> Categories = FAutomationTestBase::GetSuppressedLogCategories();
		Categories.Add(TEXT("LogPCG"));
		Categories.Add(TEXT("LogLandscape"));
		Categories.Add(TEXT("LogFoliage"));
		Categories.Add(TEXT("LogUObjectGlobals"));
		Categories.Add(TEXT("LogLinker"));
		Categories.Add(TEXT("LogUE_MCP"));
		return Categories;
	}
};

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FMCPMiscCreateCharacterisationTest, FMCPMiscCreateCharTestBase,
	"UE.MCP.Families.Characterisation.MiscCreate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPMiscCreateCharacterisationTest::RunTest(const FString& Parameters)
{
	using namespace UEMCPCharacterisation;

	FMCPScopedTestMount Mount(TEXT("/UEMCPMiscCreateChar/"), TEXT("UEMCPMiscCreateChar"));
	const FString Folder = Mount.RootPath.LeftChop(1);

	FScriptedRun Run;
	FFoliageHandlers::RegisterHandlers(Run.Registry);
	FLandscapeHandlers::RegisterHandlers(Run.Registry);
	FPCGHandlers::RegisterHandlers(Run.Registry);
	FReflectionHandlers::RegisterHandlers(Run.Registry);
	FSequencerHandlers::RegisterHandlers(Run.Registry);
	Run.Recording.Normaliser.Replace(Mount.RootPath, TEXT("/<root>/"));
	Run.Recording.Normaliser.Numbered({ TEXT("actorName"), TEXT("actorPath") });

	auto At = [&](const TCHAR* Name) { return FArgs().S(TEXT("name"), Name).S(TEXT("packagePath"), Folder); };
	auto Quad = [&](const TCHAR* Method, const FArgs& Args, const FArgs& Bad)
	{
		Run.Step(*(FString(Method) + TEXT("_01_created")), Method, Args);
		Run.Step(*(FString(Method) + TEXT("_02_existed")), Method, Args);
		FArgs Conflict;
		Conflict.Obj->Values = Args.Obj->Values;
		Run.Step(*(FString(Method) + TEXT("_03_conflict_error")), Method, Conflict.S(TEXT("onConflict"), TEXT("error")));
		Run.Step(*(FString(Method) + TEXT("_04_refused")), Method, Bad);
	};

	Quad(TEXT("create_foliage_type"), At(TEXT("FT_Char")).S(TEXT("meshPath"), TEXT("/Engine/BasicShapes/Cube")),
		At(TEXT("FT_Bad")).S(TEXT("meshPath"), Mount.RootPath + TEXT("Nope")));
	Quad(TEXT("create_landscape_layer_info"), At(TEXT("LI_Char")).S(TEXT("layerName"), TEXT("Grass")).N(TEXT("hardness"), 0.25),
		At(TEXT("LI_Bad")).S(TEXT("layerName"), TEXT("Grass")).S(TEXT("physMaterial"), Mount.RootPath + TEXT("Nope")));
	Quad(TEXT("create_pcg_graph"), At(TEXT("PCG_Char")), FArgs().S(TEXT("packagePath"), Folder));
	Quad(TEXT("create_enum"), At(TEXT("E_Char")).A(TEXT("entries"), Strings({ TEXT("One"), TEXT("Two") })), FArgs().S(TEXT("packagePath"), Folder));
	Quad(TEXT("create_level_sequence"), At(TEXT("LS_Char")), FArgs().S(TEXT("packagePath"), Folder));

	// A PCG volume in the editor world, removed again afterwards.
	const FString Label = TEXT("UEMCPCharPCGVolume");
	auto RemoveSpawned = [&Label]()
	{
		UWorld* World = GetEditorWorld();
		if (!World) return;
		TArray<AActor*> Spawned;
		MCPCollectActorsByToken(World, Label, EMCPActorMatch::Label, Spawned);
		for (AActor* Actor : Spawned) World->DestroyActor(Actor);
	};
	RemoveSpawned();
	Run.Step(TEXT("add_pcg_volume_01_created"), TEXT("add_pcg_volume"), FArgs().S(TEXT("label"), Label).S(TEXT("graphPath"), Mount.RootPath + TEXT("PCG_Char")).V(TEXT("location"), 5, 6, 7));
	Run.Step(TEXT("add_pcg_volume_02_existed"), TEXT("add_pcg_volume"), FArgs().S(TEXT("label"), Label));
	Run.Step(TEXT("add_pcg_volume_03_conflict_error"), TEXT("add_pcg_volume"), FArgs().S(TEXT("label"), Label).S(TEXT("onConflict"), TEXT("error")));
	RemoveSpawned();
	Run.Step(TEXT("add_pcg_volume_04_missing_graph"), TEXT("add_pcg_volume"), FArgs().S(TEXT("label"), Label).S(TEXT("graphPath"), Mount.RootPath + TEXT("Nope")));
	RemoveSpawned();

	return Run.Finish(*this, TEXT("misc-create-responses"));
}

#endif // WITH_DEV_AUTOMATION_TESTS
