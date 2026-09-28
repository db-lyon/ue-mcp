// F4 Create: the destination rules, the idempotent create, Abandon and the
// one created shape every creator returns.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerUtils.h"
#include "Curves/CurveFloat.h"
#include "Families/MCPCreate.h"
#include "Misc/AutomationTest.h"
#include "Tests/MCPScopedTestMount.h"

namespace MCPCreateTests
{
	TSharedPtr<FJsonObject> Args(std::initializer_list<TPair<const TCHAR*, FString>> Pairs)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		for (const TPair<const TCHAR*, FString>& Pair : Pairs) Out->SetStringField(Pair.Key, Pair.Value);
		return Out;
	}

	TSharedPtr<FJsonObject> AsObject(const TSharedPtr<FJsonValue>& Value)
	{
		return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : MakeShared<FJsonObject>();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPCreateDestinationTest, "UE.MCP.Families.Create.Destination",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPCreateDestinationTest::RunTest(const FString& Parameters)
{
	using namespace MCPCreateTests;

	{
		FMCPCreate Create(Args({ { TEXT("name"), TEXT("A") } }), { TEXT("Thing"), TEXT("/Game/Things") });
		TestNull(TEXT("name alone reads"), Create.Read().Get());
		TestEqual(TEXT("default folder"), Create.PackagePath(), FString(TEXT("/Game/Things")));
	}
	{
		FMCPCreate Create(Args({}), { TEXT("Thing"), TEXT("/Game/Things") });
		TestNotNull(TEXT("a missing name is refused"), Create.Read().Get());
	}
	{
		FMCPCreate Create(Args({ { TEXT("assetPath"), TEXT("\\Game\\UI\\WBP_A.uasset") } }), { TEXT("Thing"), TEXT("/Game"), true, true });
		TestNull(TEXT("a repairable assetPath reads"), Create.Read().Get());
		TestEqual(TEXT("the suffix and backslashes are folded away"), Create.PackageName(), FString(TEXT("/Game/UI/WBP_A")));
	}
	{
		FMCPCreate Create(Args({ { TEXT("assetPath"), TEXT("C:/Temp/WBP_A") } }), { TEXT("Thing"), TEXT("/Game"), true, true });
		TestNotNull(TEXT("a filesystem path is refused"), Create.Read().Get());
	}
	{
		FMCPCreate Create(Args({ { TEXT("name"), TEXT("WBP_B") } }), { TEXT("Thing"), TEXT("/Game/UI"), true, true });
		TestNull(TEXT("a name without packagePath takes the default folder"), Create.Read().Get());
		TestEqual(TEXT("destination"), Create.PackageName(), FString(TEXT("/Game/UI/WBP_B")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPCreateLifecycleTest, "UE.MCP.Families.Create.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPCreateLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace MCPCreateTests;

	FMCPScopedTestMount Mount(TEXT("/UEMCPCreateFamily/"), TEXT("UEMCPCreateFamily"));
	const FString Folder = Mount.RootPath.LeftChop(1);

	// Created: the one shape.
	{
		FMCPCreate Create(Args({ { TEXT("name"), TEXT("C_One") }, { TEXT("packagePath"), Folder } }), { TEXT("CurveFloat") });
		TestNull(TEXT("reads"), Create.Read().Get());
		UCurveFloat* Curve = nullptr;
		TestNull(TEXT("creates"), Create.Make(Curve).Get());
		if (!TestNotNull(TEXT("the asset"), Curve)) return false;
		const TSharedPtr<FJsonObject> Result = Create.Finish();
		TestTrue(TEXT("created"), Result->GetBoolField(TEXT("created")));
		TestFalse(TEXT("not existed"), Result->GetBoolField(TEXT("existed")));
		TestTrue(TEXT("saved"), Result->GetBoolField(TEXT("saved")));
		TestEqual(TEXT("path is the object path"), Result->GetStringField(TEXT("path")), Curve->GetPathName());
		TestEqual(TEXT("name"), Result->GetStringField(TEXT("name")), FString(TEXT("C_One")));
		TestEqual(TEXT("packagePath"), Result->GetStringField(TEXT("packagePath")), Folder);
		TestEqual(TEXT("class"), Result->GetStringField(TEXT("class")), FString(TEXT("CurveFloat")));
		TestTrue(TEXT("a delete_asset rollback"), Result->GetObjectField(TEXT("rollback"))->GetStringField(TEXT("method")) == TEXT("delete_asset"));
	}

	// A rerun answers existed; onConflict=error refuses.
	{
		FMCPCreate Create(Args({ { TEXT("name"), TEXT("C_One") }, { TEXT("packagePath"), Folder } }), { TEXT("CurveFloat") });
		Create.Read();
		UCurveFloat* Curve = nullptr;
		const TSharedPtr<FJsonObject> Early = AsObject(Create.Make(Curve));
		TestNull(TEXT("no second asset"), Curve);
		TestTrue(TEXT("existed"), Early->GetBoolField(TEXT("existed")));
		TestTrue(TEXT("existed is a success"), Early->GetBoolField(TEXT("success")));
	}
	{
		FMCPCreate Create(Args({ { TEXT("name"), TEXT("C_One") }, { TEXT("packagePath"), Folder }, { TEXT("onConflict"), TEXT("error") } }), { TEXT("CurveFloat") });
		Create.Read();
		UCurveFloat* Curve = nullptr;
		TestFalse(TEXT("onConflict=error refuses"), AsObject(Create.Make(Curve))->GetBoolField(TEXT("success")));
	}

	// Abandon deletes what a refused configure left.
	{
		FMCPCreate Create(Args({ { TEXT("name"), TEXT("C_Abandoned") }, { TEXT("packagePath"), Folder } }), { TEXT("CurveFloat") });
		Create.Read();
		UCurveFloat* Curve = nullptr;
		Create.Make(Curve);
		const TSharedPtr<FJsonObject> Err = AsObject(Create.Abandon(MCPError(TEXT("refused"))));
		TestFalse(TEXT("the refusal comes back"), Err->GetBoolField(TEXT("success")));
		TestFalse(TEXT("the asset is gone"), UEditorAssetLibrary::DoesAssetExist(Mount.RootPath + TEXT("C_Abandoned")));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
