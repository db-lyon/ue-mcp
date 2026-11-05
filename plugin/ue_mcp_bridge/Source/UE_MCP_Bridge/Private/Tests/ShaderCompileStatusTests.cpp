#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/Editor/EditorHandlers.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FMCPShaderIdleCommand, FAutomationTestBase*, Test, double, Deadline);

bool FMCPShaderIdleCommand::Update()
{
	FMCPHandlerRegistry Registry;
	FEditorHandlers::RegisterHandlers(Registry);
	auto Request = MakeShared<FJsonObject>();
	Request->SetBoolField(TEXT("waitUntilIdle"), true);
	auto Result = Registry.ExecuteHandler(TEXT("get_shader_compile_status"), Request)->AsObject();
	if (!Result->GetBoolField(TEXT("idle")))
	{
		if (FPlatformTime::Seconds() < Deadline) return false;
		Test->AddError(TEXT("Shader compiler did not become idle within 60 seconds"));
		return true;
	}
	Test->TestTrue(TEXT("wait requested"), Result->GetBoolField(TEXT("waitUntilIdle")));
	Test->TestEqual(TEXT("idle state returned"), Result->GetStringField(TEXT("state")), FString(TEXT("idle")));
	Test->TestEqual(TEXT("idle outstanding jobs"), Result->GetIntegerField(TEXT("outstandingJobs")), 0);
	Test->TestEqual(TEXT("idle pending shader maps"), Result->GetIntegerField(TEXT("pendingShaderMaps")), 0);
	Test->TestFalse(TEXT("idle compiler"), Result->GetBoolField(TEXT("isCompiling")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPShaderCompileStatusTest,
	"UE.MCP.Editor.ShaderCompileStatus.FieldsAndIdle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPShaderCompileStatusTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FEditorHandlers::RegisterHandlers(Registry);
	auto Result = Registry.ExecuteHandler(TEXT("get_shader_compile_status"), MakeShared<FJsonObject>())->AsObject();
	TestTrue(TEXT("status succeeds"), Result->GetBoolField(TEXT("success")));
	TestTrue(TEXT("compiler state exists"), Result->HasTypedField<EJson::Boolean>(TEXT("isCompiling")));
	TestTrue(TEXT("manager availability exists"), Result->HasTypedField<EJson::Boolean>(TEXT("managerAvailable")));
	TestTrue(TEXT("outstanding count exists"), Result->HasTypedField<EJson::Number>(TEXT("outstandingJobs")));
	TestTrue(TEXT("material count exists"), Result->HasTypedField<EJson::Number>(TEXT("pendingMaterials")));
	TestTrue(TEXT("shader map count exists"), Result->HasTypedField<EJson::Number>(TEXT("pendingShaderMaps")));
	TestTrue(TEXT("outstanding count is nonnegative"), Result->GetIntegerField(TEXT("outstandingJobs")) >= 0);
	TestTrue(TEXT("shader map count is nonnegative"), Result->GetIntegerField(TEXT("pendingShaderMaps")) >= 0);
	ADD_LATENT_AUTOMATION_COMMAND(FMCPShaderIdleCommand(this, FPlatformTime::Seconds() + 60.0));
	return true;
}

#endif
