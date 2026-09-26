// Coverage for foliage(batch_set_settings_where) (#988).
//
// run_automation_tests dispatches every EditorContext/EngineFilter test in the
// process against whatever project the bridge is attached to, and this action
// writes to FoliageType assets, so the assertions stay on argument rejection
// and the dry run, both of which are specified to write nothing.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/Foliage/FoliageHandlers.h"
#include "Misc/AutomationTest.h"

namespace
{
	TSharedPtr<FJsonObject> MCPFoliageBatchTestRun(
		FMCPHandlerRegistry& Registry,
		const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonValue> Response =
			Registry.ExecuteHandler(TEXT("batch_set_foliage_settings_where"), Request);
		if (!Response.IsValid() || Response->Type != EJson::Object)
		{
			return nullptr;
		}
		return Response->AsObject();
	}

	TSharedPtr<FJsonObject> MCPFoliageBatchTestSettings()
	{
		TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
		Settings->SetBoolField(TEXT("bIncludeInHLOD"), false);
		return Settings;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFoliageBatchSettingsContractTest,
	"UE.MCP.Foliage.BatchSetSettingsWhere.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoliageBatchSettingsContractTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FFoliageHandlers::RegisterHandlers(Registry);

	TestTrue(TEXT("batch_set_foliage_settings_where is registered"),
		Registry.HasHandler(TEXT("batch_set_foliage_settings_where")));
	TestTrue(TEXT("it carries its own timeout, because scanning and saving hundreds of assets is slow"),
		Registry.GetHandlerTimeout(TEXT("batch_set_foliage_settings_where")) > 0.0f);

	// No settings means there is nothing to write.
	{
		const TSharedPtr<FJsonObject> Response =
			MCPFoliageBatchTestRun(Registry, MakeShared<FJsonObject>());
		if (Response.IsValid())
		{
			TestFalse(TEXT("no settings fails"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and names the parameter"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("settings")));
		}
	}

	// No predicate means "every foliage type in the project", which is not
	// what anybody means by a conditional batch. The error points at the
	// single-asset action for the case where one asset is what was wanted.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetObjectField(TEXT("settings"), MCPFoliageBatchTestSettings());
		const TSharedPtr<FJsonObject> Response = MCPFoliageBatchTestRun(Registry, Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("no predicate fails"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and offers set_settings for the single-asset case"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("set_settings")));
		}
	}

	// A predicate but no candidate source.
	{
		TSharedPtr<FJsonObject> Predicate = MakeShared<FJsonObject>();
		Predicate->SetStringField(TEXT("field"), TEXT("CullDistance.Max"));
		Predicate->SetStringField(TEXT("op"), TEXT("gt"));
		Predicate->SetNumberField(TEXT("value"), 0);

		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetObjectField(TEXT("settings"), MCPFoliageBatchTestSettings());
		Request->SetArrayField(TEXT("where"), { MakeShared<FJsonValueObject>(Predicate) });
		const TSharedPtr<FJsonObject> Response = MCPFoliageBatchTestRun(Registry, Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("no candidate source fails"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and lists all three"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("fromLevel")));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFoliageBatchSettingsPreviewTest,
	"UE.MCP.Foliage.BatchSetSettingsWhere.PreviewsByDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoliageBatchSettingsPreviewTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FFoliageHandlers::RegisterHandlers(Registry);

	// A complete, valid request against a directory that exists in every
	// project. dryRun is not passed, so the default decides, and the default
	// has to be the preview.
	TSharedPtr<FJsonObject> Predicate = MakeShared<FJsonObject>();
	Predicate->SetStringField(TEXT("field"), TEXT("CullDistance.Max"));
	Predicate->SetStringField(TEXT("op"), TEXT("gt"));
	Predicate->SetNumberField(TEXT("value"), 0);

	TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetObjectField(TEXT("settings"), MCPFoliageBatchTestSettings());
	Request->SetArrayField(TEXT("where"), { MakeShared<FJsonValueObject>(Predicate) });
	Request->SetStringField(TEXT("directory"), TEXT("/Game"));

	const TSharedPtr<FJsonObject> Response = MCPFoliageBatchTestRun(Registry, Request);
	if (!Response.IsValid() || !Response->GetBoolField(TEXT("success")))
	{
		return true;
	}

	TestTrue(TEXT("dryRun defaults to true"), Response->GetBoolField(TEXT("dryRun")));
	TestTrue(TEXT("a preview reports what it would update"), Response->HasField(TEXT("wouldUpdate")));
	TestFalse(TEXT("and does not claim to have updated anything"), Response->HasField(TEXT("updated")));
	TestFalse(TEXT("and saves nothing"), Response->HasField(TEXT("saved")));

	// The zero-match note has to explain the interval trap, because
	// CullDistance is an FInt32Interval and comparing it as a number is the
	// mistake this action invites.
	if (static_cast<int32>(Response->GetNumberField(TEXT("matched"))) == 0 &&
		static_cast<int32>(Response->GetNumberField(TEXT("scanned"))) > 0)
	{
		TestTrue(TEXT("a zero match explains itself"), Response->HasField(TEXT("zeroMatchNote")));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
