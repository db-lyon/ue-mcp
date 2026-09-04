// Safe registration and parameter validation coverage for the live-only
// post-process AnimBP override. It never resolves a real actor or loads an
// asset, so it is safe to dispatch in any editor process.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/AnimationHandlers.h"
#include "Misc/AutomationTest.h"

namespace
{
TSharedPtr<FJsonObject> MakeLivePostProcessResponseObject(const TSharedPtr<FJsonValue>& Response)
{
	return (Response.IsValid() && Response->Type == EJson::Object) ? Response->AsObject() : nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLivePostProcessAnimBlueprintTest,
	"UE.MCP.Animation.LivePostProcessAnimBlueprint.RegistrationAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLivePostProcessAnimBlueprintTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FAnimationHandlers::RegisterHandlers(Registry);
	TestTrue(TEXT("set_live_post_process_anim_blueprint is registered"),
		Registry.HasHandler(TEXT("set_live_post_process_anim_blueprint")));

	// The selector is required before the handler can touch a world or component.
	{
		const TSharedPtr<FJsonObject> MissingActor = MakeLivePostProcessResponseObject(
			Registry.ExecuteHandler(TEXT("set_live_post_process_anim_blueprint"), MakeShared<FJsonObject>()));
		TestTrue(TEXT("missing actor returns an object"), MissingActor.IsValid());
		if (MissingActor.IsValid())
		{
			TestFalse(TEXT("missing actor is unsuccessful"), MissingActor->GetBoolField(TEXT("success")));
			TestTrue(TEXT("missing actor names actorLabel"),
				MissingActor->GetStringField(TEXT("error")).Contains(TEXT("actorLabel")));
		}
	}

	// A clear and a replacement mean opposite things, so reject the ambiguous
	// request before it can reinitialize a live component.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorLabel"), TEXT("UEMCP_NotResolved"));
		Request->SetStringField(TEXT("animBlueprintClassPath"), TEXT("/Game/UEMCP/ABP_Test.ABP_Test_C"));
		Request->SetBoolField(TEXT("clear"), true);
		const TSharedPtr<FJsonObject> Ambiguous = MakeLivePostProcessResponseObject(
			Registry.ExecuteHandler(TEXT("set_live_post_process_anim_blueprint"), Request));
		TestTrue(TEXT("ambiguous set/clear returns an object"), Ambiguous.IsValid());
		if (Ambiguous.IsValid())
		{
			TestFalse(TEXT("ambiguous set/clear is unsuccessful"), Ambiguous->GetBoolField(TEXT("success")));
			TestTrue(TEXT("ambiguous set/clear names clear"),
				Ambiguous->GetStringField(TEXT("error")).Contains(TEXT("clear")));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
