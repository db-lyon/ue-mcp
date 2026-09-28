// The response of the three Widget asset creators, recorded before the F4 port
// and held to the contract after it (plans/domain-design.md 0 and 8): both
// ways of naming the destination, the rerun, onConflict=error and the parent
// refusals. The fixture is tests/golden/cpp/widget-create-responses.json; a
// run without it records it.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerUtils.h"
#include "Handlers/Widget/WidgetHandlers.h"
#include "Tests/MCPCharacterisationRun.h"
#include "Tests/MCPScopedTestMount.h"

class FMCPWidgetCreateCharTestBase : public FAutomationTestBase
{
public:
	FMCPWidgetCreateCharTestBase(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{
	}

	virtual TArray<FString> GetSuppressedLogCategories() override
	{
		TArray<FString> Categories = FAutomationTestBase::GetSuppressedLogCategories();
		Categories.Add(TEXT("LogBlueprint"));
		Categories.Add(TEXT("LogK2Compiler"));
		Categories.Add(TEXT("LogUMG"));
		Categories.Add(TEXT("LogUObjectGlobals"));
		Categories.Add(TEXT("LogLinker"));
		Categories.Add(TEXT("LogUE_MCP"));
		return Categories;
	}
};

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FMCPWidgetCreateCharacterisationTest, FMCPWidgetCreateCharTestBase,
	"UE.MCP.Widget.Characterisation.Create",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPWidgetCreateCharacterisationTest::RunTest(const FString& Parameters)
{
	using namespace UEMCPCharacterisation;

	FMCPScopedTestMount Mount(TEXT("/UEMCPWidgetCreateChar/"), TEXT("UEMCPWidgetCreateChar"));
	const FString Folder = Mount.RootPath.LeftChop(1);

	FScriptedRun Run;
	FWidgetHandlers::RegisterHandlers(Run.Registry);
	Run.Recording.Normaliser.Replace(Mount.RootPath, TEXT("/<root>/"));

	for (const TCHAR* Method : { TEXT("create_widget_blueprint"), TEXT("create_editor_utility_widget"), TEXT("create_editor_utility_blueprint") })
	{
		const FString ByPath = Mount.RootPath + TEXT("P_") + Method;
		const FString ByName = FString(TEXT("N_")) + Method;
		Run.Step(*(FString(Method) + TEXT("_01_by_path")), Method, FArgs().S(TEXT("assetPath"), ByPath));
		Run.Step(*(FString(Method) + TEXT("_02_by_path_existed")), Method, FArgs().S(TEXT("assetPath"), ByPath));
		Run.Step(*(FString(Method) + TEXT("_03_by_path_conflict_error")), Method, FArgs().S(TEXT("assetPath"), ByPath).S(TEXT("onConflict"), TEXT("error")));
		Run.Step(*(FString(Method) + TEXT("_04_by_name")), Method, FArgs().S(TEXT("name"), ByName).S(TEXT("packagePath"), Folder));
		Run.Step(*(FString(Method) + TEXT("_05_no_destination")), Method, FArgs());
		Run.Step(*(FString(Method) + TEXT("_06_bad_path")), Method, FArgs().S(TEXT("assetPath"), TEXT("NotAPath")));
	}
	Run.Step(TEXT("create_widget_blueprint_07_bad_parent"), TEXT("create_widget_blueprint"),
		FArgs().S(TEXT("assetPath"), Mount.RootPath + TEXT("WBP_BadParent")).S(TEXT("parentClass"), TEXT("NoSuchParentClassAnywhere")));
	Run.Step(TEXT("create_widget_blueprint_08_unrelated_parent"), TEXT("create_widget_blueprint"),
		FArgs().S(TEXT("assetPath"), Mount.RootPath + TEXT("WBP_Unrelated")).S(TEXT("parentClass"), TEXT("Actor")));

	return Run.Finish(*this, TEXT("widget-create-responses"));
}

#endif // WITH_DEV_AUTOMATION_TESTS
