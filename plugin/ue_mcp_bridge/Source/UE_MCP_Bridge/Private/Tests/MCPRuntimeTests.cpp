// F7 Runtime: the one world rule, the refusal that names a pieInstance, the
// rollback echo and the component lookup. No PIE runs here, so every live
// answer is the refusal; the runtime handlers are held to the same rule.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Components/PointLightComponent.h"
#include "Families/MCPRuntime.h"
#include "Handlers/Animation/AnimationHandlers.h"
#include "Handlers/Editor/EditorHandlers.h"
#include "Handlers/Gas/GasHandlers.h"
#include "Handlers/Widget/WidgetHandlers.h"
#include "Misc/AutomationTest.h"

namespace MCPRuntimeTests
{
	TSharedPtr<FJsonObject> Args(const TCHAR* Scope, int32 Instance = INDEX_NONE)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		if (Scope) Out->SetStringField(TEXT("world"), Scope);
		if (Instance != INDEX_NONE) Out->SetNumberField(TEXT("pieInstance"), Instance);
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPRuntimeWorldRuleTest, "UE.MCP.Families.Runtime.WorldRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPRuntimeWorldRuleTest::RunTest(const FString& Parameters)
{
	using namespace MCPRuntimeTests;
	if (MCPIsPlayInEditorActive())
	{
		AddWarning(TEXT("PIE is running; the no-PIE answers are not checked."));
		return true;
	}
	UWorld* Editor = GetEditorWorld();

	TestTrue(TEXT("editor scope takes the editor world"), FMCPRuntimeWorld::Resolve(Args(TEXT("editor")), TEXT("pie")).World == Editor);
	TestTrue(TEXT("auto without PIE takes the editor world"), FMCPRuntimeWorld::Resolve(Args(nullptr), TEXT("auto")).World == Editor);
	TestNull(TEXT("pie without PIE takes nothing"), FMCPRuntimeWorld::Resolve(Args(TEXT("pie")), TEXT("auto")).World);
	TestNull(TEXT("auto with an unknown pieInstance is never widened to the editor"), FMCPRuntimeWorld::Resolve(Args(TEXT("auto"), 97), TEXT("auto")).World);
	TestNull(TEXT("a live-only resolve refuses the editor world"), FMCPRuntimeWorld::ResolveLive(Args(TEXT("editor"))).World);

	const FMCPRuntimeWorld Missing = FMCPRuntimeWorld::Resolve(Args(TEXT("auto"), 97), TEXT("auto"));
	TestTrue(TEXT("the refusal names the pieInstance"), Missing.Missing(TEXT("default")).Contains(TEXT("pieInstance 97")));
	TestEqual(TEXT("without one, the handler's own words"), FMCPRuntimeWorld::Resolve(Args(TEXT("pie")), TEXT("pie")).Missing(TEXT("default")), FString(TEXT("default")));

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	FMCPRuntimeWorld::Resolve(Args(TEXT("editor")), TEXT("editor")).Echo(Payload);
	TestEqual(TEXT("the echo names the world"), Payload->GetStringField(TEXT("world")), FString(TEXT("editor")));
	TestFalse(TEXT("an editor world echoes no pieInstance"), Payload->HasField(TEXT("pieInstance")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPRuntimeFindComponentTest, "UE.MCP.Families.Runtime.FindComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPRuntimeFindComponentTest::RunTest(const FString& Parameters)
{
	UWorld* World = GetEditorWorld();
	if (!TestNotNull(TEXT("editor world"), World)) return false;
	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, MCPTransientSpawnParameters());
	if (!TestNotNull(TEXT("an actor"), Actor)) return false;
	UPointLightComponent* KeyLight = NewObject<UPointLightComponent>(Actor, TEXT("KeyLight"));
	KeyLight->RegisterComponent();
	Actor->AddInstanceComponent(KeyLight);
	UPointLightComponent* FillLight = NewObject<UPointLightComponent>(Actor, TEXT("FillLight"));
	FillLight->RegisterComponent();
	Actor->AddInstanceComponent(FillLight);

	TestTrue(TEXT("an exact name, any case"), MCPRuntime::FindComponent<UPointLightComponent>(Actor, TEXT("filllight")) == FillLight);
	TestNotNull(TEXT("a class name"), MCPRuntime::FindComponent<UPointLightComponent>(Actor, TEXT("PointLightComponent")));
	TestTrue(TEXT("a name prefix"), MCPRuntime::FindComponent<UPointLightComponent>(Actor, TEXT("Key")) == KeyLight);
	TestNotNull(TEXT("no name takes the first"), MCPRuntime::FindComponent<UPointLightComponent>(Actor, FString()));
	TestNull(TEXT("no match is null"), MCPRuntime::FindComponent<UPointLightComponent>(Actor, TEXT("Rim")));

	World->DestroyActor(Actor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPRuntimeHandlersPieInstanceTest, "UE.MCP.Families.Runtime.HandlersRefuseUnknownPieInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPRuntimeHandlersPieInstanceTest::RunTest(const FString& Parameters)
{
	if (MCPIsPlayInEditorActive())
	{
		AddWarning(TEXT("PIE is running; pieInstance 97 might exist."));
		return true;
	}
	FMCPHandlerRegistry Registry;
	FGasHandlers::RegisterHandlers(Registry);
	FEditorHandlers::RegisterHandlers(Registry);
	FAnimationHandlers::RegisterHandlers(Registry);
	FWidgetHandlers::RegisterHandlers(Registry);

	// Each would have answered from the editor world, or the first PIE world, before F7.
	for (const TCHAR* Method : { TEXT("get_attribute"), TEXT("get_runtime_values"),
		TEXT("set_pie_time_scale"), TEXT("get_pie_pawn"), TEXT("get_bone_transform"), TEXT("list_runtime_widgets"),
		TEXT("set_runtime_focus") })
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("world"), TEXT("auto"));
		Args->SetNumberField(TEXT("pieInstance"), 97);
		Args->SetStringField(TEXT("actorLabel"), TEXT("UEMCPNoSuchRuntimeActor"));
		Args->SetStringField(TEXT("attribute"), TEXT("Set.Health"));
		Args->SetStringField(TEXT("propertyName"), TEXT("bHidden"));
		Args->SetStringField(TEXT("boneName"), TEXT("root"));
		Args->SetStringField(TEXT("widgetName"), TEXT("WBP_None"));
		Args->SetNumberField(TEXT("factor"), 2);
		TArray<TSharedPtr<FJsonValue>> Paths;
		Paths.Add(MakeShared<FJsonValueString>(TEXT("bHidden")));
		Args->SetArrayField(TEXT("paths"), Paths);
		const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(Method, Args);
		const TSharedPtr<FJsonObject> Result = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("%s answered"), Method), Result.Get())) continue;
		bool bSuccess = true;
		Result->TryGetBoolField(TEXT("success"), bSuccess);
		TestFalse(FString::Printf(TEXT("%s refuses a pieInstance no world has"), Method), bSuccess);
		FString Error;
		Result->TryGetStringField(TEXT("error"), Error);
		TestTrue(FString::Printf(TEXT("%s names it (%s)"), Method, *Error), Error.Contains(TEXT("pieInstance")));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
