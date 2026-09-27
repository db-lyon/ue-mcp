// The Blueprint component adders must fail when no SCS node was made: a
// Blueprint that is not an Actor has no SimpleConstructionScript, and each
// adder used to dereference it or report created for a node it never added.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Gameplay/GameplayHandlers.h"
#include "Handlers/Gas/GasHandlers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#include "Tests/MCPScopedTestMount.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPBlueprintComponentAdderNoScsTest,
	"UE.MCP.Blueprint.ComponentAdders.RefuseWithoutScsNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBlueprintComponentAdderNoScsTest::RunTest(const FString& Parameters)
{
	FMCPScopedTestMount Mount(
		TEXT("/UEMCPComponentAdder_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"), TEXT("UEMCPComponentAdder"));
	const FString PackageName = Mount.RootPath + TEXT("BP_NotAnActor");
	UPackage* Package = CreatePackage(*PackageName);
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		UObject::StaticClass(), Package, FName(TEXT("BP_NotAnActor")), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("fixture Blueprint created"), Blueprint)) return false;
	if (!TestNull(TEXT("a UObject Blueprint has no SCS"), Blueprint->SimpleConstructionScript.Get())) return false;

	FMCPHandlerRegistry Registry;
	FGameplayHandlers::RegisterHandlers(Registry);
	FGasHandlers::RegisterHandlers(Registry);

	for (const TCHAR* Method : { TEXT("add_state_tree_component"), TEXT("add_smart_object_component"),
		TEXT("add_ability_system_component"), TEXT("add_perception_component") })
	{
		auto Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprintPath"), PackageName);
		const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(Method, Args);
		const TSharedPtr<FJsonObject> Result = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("%s answered"), Method), Result.Get())) continue;
		bool bSuccess = true;
		Result->TryGetBoolField(TEXT("success"), bSuccess);
		TestFalse(FString::Printf(TEXT("%s fails without an SCS node"), Method), bSuccess);
		TestFalse(FString::Printf(TEXT("%s does not report created"), Method), Result->HasField(TEXT("created")));
	}

	Package->SetDirtyFlag(false);
	return true;
}

#endif
