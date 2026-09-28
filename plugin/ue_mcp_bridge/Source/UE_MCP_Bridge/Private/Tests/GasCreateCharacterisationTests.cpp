// The response of the four GAS Blueprint creators and of
// add_ability_system_component, recorded before the F4 and F6 port and held to
// the contract after it (plans/domain-design.md 0 and 8). The fixture is
// tests/golden/cpp/gas-create-responses.json; a run without it records it.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerUtils.h"
#include "Handlers/Blueprint/BlueprintHandlers.h"
#include "Handlers/Gas/GasHandlers.h"
#include "Tests/MCPCharacterisationRun.h"
#include "Tests/MCPScopedTestMount.h"

class FMCPGasCreateCharTestBase : public FAutomationTestBase
{
public:
	FMCPGasCreateCharTestBase(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{
	}

	virtual TArray<FString> GetSuppressedLogCategories() override
	{
		TArray<FString> Categories = FAutomationTestBase::GetSuppressedLogCategories();
		Categories.Add(TEXT("LogBlueprint"));
		Categories.Add(TEXT("LogK2Compiler"));
		Categories.Add(TEXT("LogUObjectGlobals"));
		Categories.Add(TEXT("LogLinker"));
		Categories.Add(TEXT("LogUE_MCP"));
		Categories.Add(TEXT("LogMCPBridge"));
		return Categories;
	}
};

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FMCPGasCreateCharacterisationTest, FMCPGasCreateCharTestBase,
	"UE.MCP.Gas.Characterisation.Create",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPGasCreateCharacterisationTest::RunTest(const FString& Parameters)
{
	using namespace UEMCPCharacterisation;

	FMCPScopedTestMount Mount(TEXT("/UEMCPGasCreateChar/"), TEXT("UEMCPGasCreateChar"));
	const FString Folder = Mount.RootPath.LeftChop(1);

	FScriptedRun Run;
	FGasHandlers::RegisterHandlers(Run.Registry);
	FBlueprintHandlers::RegisterHandlers(Run.Registry);
	Run.Recording.Normaliser.Replace(Mount.RootPath, TEXT("/<root>/"));

	auto At = [&](const TCHAR* Name) { return FArgs().S(TEXT("name"), Name).S(TEXT("packagePath"), Folder); };

	for (const TCHAR* Method : { TEXT("create_gameplay_effect"), TEXT("create_gameplay_ability"),
		TEXT("create_attribute_set"), TEXT("create_gameplay_cue") })
	{
		const FString Name = FString(TEXT("BP_")) + Method;
		Run.Step(*(FString(Method) + TEXT("_01_created")), Method, At(*Name));
		Run.Step(*(FString(Method) + TEXT("_02_existed")), Method, At(*Name));
		Run.Step(*(FString(Method) + TEXT("_03_conflict_error")), Method, At(*Name).S(TEXT("onConflict"), TEXT("error")));
		Run.Step(*(FString(Method) + TEXT("_04_missing_name")), Method, FArgs().S(TEXT("packagePath"), Folder));
	}
	Run.Step(TEXT("create_gameplay_effect_05_duration"), TEXT("create_gameplay_effect"), At(TEXT("GE_Infinite")).S(TEXT("durationPolicy"), TEXT("Infinite")));
	Run.Step(TEXT("create_gameplay_effect_06_bad_duration"), TEXT("create_gameplay_effect"), At(TEXT("GE_Bad")).S(TEXT("durationPolicy"), TEXT("Forever")));
	Run.Step(TEXT("create_gameplay_cue_05_actor"), TEXT("create_gameplay_cue"), At(TEXT("GC_Actor")).S(TEXT("cueType"), TEXT("Actor")));

	// The component adder, on a Character Blueprint made for it.
	const FString Owner = Mount.RootPath + TEXT("BP_AscOwner");
	Run.Call(TEXT("create_blueprint"), FArgs().S(TEXT("assetPath"), Owner).S(TEXT("parentClass"), TEXT("Character")));
	Run.Step(TEXT("add_ability_system_component_01_added"), TEXT("add_ability_system_component"), FArgs().S(TEXT("blueprintPath"), Owner));
	Run.Step(TEXT("add_ability_system_component_02_existed"), TEXT("add_ability_system_component"), FArgs().S(TEXT("blueprintPath"), Owner));
	Run.Step(TEXT("add_ability_system_component_03_named"), TEXT("add_ability_system_component"), FArgs().S(TEXT("blueprintPath"), Owner).S(TEXT("componentName"), TEXT("OtherAsc")));
	Run.Step(TEXT("add_ability_system_component_04_missing_bp"), TEXT("add_ability_system_component"), FArgs().S(TEXT("blueprintPath"), Mount.RootPath + TEXT("BP_Nope")));

	return Run.Finish(*this, TEXT("gas-create-responses"));
}

#endif // WITH_DEV_AUTOMATION_TESTS
