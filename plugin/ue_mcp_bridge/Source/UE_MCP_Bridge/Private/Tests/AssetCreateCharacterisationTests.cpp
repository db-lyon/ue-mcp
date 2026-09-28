// The response of the Asset category creators the F4 port covers, recorded
// before it and held to the contract after it (plans/domain-design.md 0 and
// 8). The fixture is tests/golden/cpp/asset-create-responses.json; a run
// without it records it.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerUtils.h"
#include "Handlers/Asset/AssetHandlers.h"
#include "Tests/MCPCharacterisationRun.h"
#include "Tests/MCPScopedTestMount.h"

class FMCPAssetCreateCharTestBase : public FAutomationTestBase
{
public:
	FMCPAssetCreateCharTestBase(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{
	}

	virtual TArray<FString> GetSuppressedLogCategories() override
	{
		TArray<FString> Categories = FAutomationTestBase::GetSuppressedLogCategories();
		Categories.Add(TEXT("LogDataTable"));
		Categories.Add(TEXT("LogInterchangeEngine"));
		Categories.Add(TEXT("LogUObjectGlobals"));
		Categories.Add(TEXT("LogLinker"));
		Categories.Add(TEXT("LogUE_MCP"));
		return Categories;
	}
};

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FMCPAssetCreateCharacterisationTest, FMCPAssetCreateCharTestBase,
	"UE.MCP.Asset.Characterisation.Create",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetCreateCharacterisationTest::RunTest(const FString& Parameters)
{
	using namespace UEMCPCharacterisation;

	FMCPScopedTestMount Mount(TEXT("/UEMCPAssetCreateChar/"), TEXT("UEMCPAssetCreateChar"));
	const FString Folder = Mount.RootPath.LeftChop(1);

	FScriptedRun Run;
	FAssetHandlers::RegisterHandlers(Run.Registry);
	Run.Recording.Normaliser.Replace(Mount.RootPath, TEXT("/<root>/"));

	auto At = [&](const TCHAR* Name) { return FArgs().S(TEXT("name"), Name).S(TEXT("packagePath"), Folder); };
	auto Quad = [&](const TCHAR* Method, const FArgs& Args, const FArgs& Bad)
	{
		Run.Step(*(FString(Method) + TEXT("_01_created")), Method, Args);
		Run.Step(*(FString(Method) + TEXT("_02_existed")), Method, Args);
		FArgs ConflictArgs;
		ConflictArgs.Obj->Values = Args.Obj->Values;
		ConflictArgs.S(TEXT("onConflict"), TEXT("error"));
		Run.Step(*(FString(Method) + TEXT("_03_conflict_error")), Method, ConflictArgs);
		Run.Step(*(FString(Method) + TEXT("_04_refused")), Method, Bad);
	};

	TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
	Props->SetBoolField(TEXT("bLabelAssetsInMyDirectory"), true);
	Quad(TEXT("create_data_asset"), At(TEXT("DA_Char")).S(TEXT("className"), TEXT("PrimaryAssetLabel")),
		At(TEXT("DA_Bad")).S(TEXT("className"), TEXT("Actor")));
	Run.Step(TEXT("create_data_asset_05_properties"), TEXT("create_data_asset"),
		At(TEXT("DA_Props")).S(TEXT("className"), TEXT("PrimaryAssetLabel")).O(TEXT("properties"), Props));
	Quad(TEXT("create_asset_by_class"), At(TEXT("C_Char")).S(TEXT("className"), TEXT("CurveFloat")),
		At(TEXT("C_Bad")).S(TEXT("className"), TEXT("NoSuchClassAnywhere")));
	Quad(TEXT("create_interchange_pipeline"), At(TEXT("IP_Char")),
		FArgs().S(TEXT("packagePath"), Folder));
	Quad(TEXT("create_datatable"), At(TEXT("DT_Char")).S(TEXT("rowStruct"), TEXT("UEMCPDataTableReferenceRow")),
		At(TEXT("DT_Bad")).S(TEXT("rowStruct"), TEXT("NoSuchRowStruct")));
	Quad(TEXT("create_curvetable"), At(TEXT("CT_Char")), FArgs().S(TEXT("packagePath"), Folder));
	Quad(TEXT("create_stringtable"), At(TEXT("ST_Char")).S(TEXT("namespace"), TEXT("Char")), FArgs().S(TEXT("packagePath"), Folder));
	Quad(TEXT("create_user_defined_enum"), At(TEXT("E_Char")).A(TEXT("values"), Strings({ TEXT("Red"), TEXT("Green") })),
		FArgs().S(TEXT("packagePath"), Folder));
	{
		TSharedPtr<FJsonObject> Health = MakeShared<FJsonObject>();
		Health->SetStringField(TEXT("name"), TEXT("Health"));
		Health->SetStringField(TEXT("type"), TEXT("float"));
		Quad(TEXT("create_user_defined_struct"), At(TEXT("S_Char")).A(TEXT("structFields"), Objects({ Health })),
			FArgs().S(TEXT("packagePath"), Folder));
	}

	return Run.Finish(*this, TEXT("asset-create-responses"));
}

#endif // WITH_DEV_AUTOMATION_TESTS
