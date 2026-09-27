// create_gameplay_effect writes the requested durationPolicy onto the new
// effect's defaults, and refuses a policy the engine does not have.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Gas/GasHandlers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "GameplayEffect.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#include "Tests/MCPScopedTestMount.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPGasCreateEffectDurationTest,
	"UE.MCP.Gas.CreateEffect.WritesDurationPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPGasCreateEffectDurationTest::RunTest(const FString& Parameters)
{
	FMCPScopedTestMount Mount(
		TEXT("/UEMCPGasEffect_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"), TEXT("UEMCPGasEffect"));

	FMCPHandlerRegistry Registry;
	FGasHandlers::RegisterHandlers(Registry);

	auto Run = [&Registry](const TSharedPtr<FJsonObject>& Args)
	{
		const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(TEXT("create_gameplay_effect"), Args);
		return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : MakeShared<FJsonObject>();
	};

	{
		auto Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("name"), TEXT("GE_DurationProbe"));
		Args->SetStringField(TEXT("packagePath"), Mount.RootPath.LeftChop(1));
		Args->SetStringField(TEXT("durationPolicy"), TEXT("HasDuration"));
		bool bSuccess = false;
		if (!TestTrue(TEXT("effect created"), Run(Args)->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess)) return false;

		const FString ObjectPath = Mount.RootPath + TEXT("GE_DurationProbe.GE_DurationProbe");
		UBlueprint* Blueprint = FindObject<UBlueprint>(nullptr, *ObjectPath);
		if (!TestNotNull(TEXT("effect Blueprint in memory"), Blueprint)) return false;
		const UGameplayEffect* Defaults = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject<UGameplayEffect>() : nullptr;
		if (!TestNotNull(TEXT("effect defaults exist"), Defaults)) return false;
		TestTrue(TEXT("the requested durationPolicy was written"), Defaults->DurationPolicy == EGameplayEffectDurationType::HasDuration);
	}
	{
		auto Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("name"), TEXT("GE_BadPolicy"));
		Args->SetStringField(TEXT("packagePath"), Mount.RootPath.LeftChop(1));
		Args->SetStringField(TEXT("durationPolicy"), TEXT("Forever"));
		bool bSuccess = true;
		Run(Args)->TryGetBoolField(TEXT("success"), bSuccess);
		TestFalse(TEXT("an unknown durationPolicy is refused"), bSuccess);
		TestNull(TEXT("nothing is created for a refused policy"), FindPackage(nullptr, *(Mount.RootPath + TEXT("GE_BadPolicy"))));
	}
	return true;
}

#endif
