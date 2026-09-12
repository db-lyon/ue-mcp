// Coverage for the evaluated-pose reads - animation(sample_pose),
// animation(get_live_bone_transforms) and animation(measure_natural_speed) -
// that is safe to run anywhere.
//
// run_automation_tests dispatches every EditorContext/EngineFilter test in the
// process when it is called without a filter, against whatever project the
// bridge is attached to. All three are reads, and none of the cases below name
// a real asset or actor, so the suite holds in any project. Evaluating an actual
// clip is exercised by the smoke suite against the dedicated test project.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/AnimationHandlers.h"
#include "Misc/AutomationTest.h"

namespace
{
TSharedPtr<FJsonObject> MakePoseResponseObject(const TSharedPtr<FJsonValue>& Response)
{
	return (Response.IsValid() && Response->Type == EJson::Object) ? Response->AsObject() : nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAnimationPoseRegistrationTest,
	"UE.MCP.Animation.Pose.RegistrationAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationPoseRegistrationTest::RunTest(const FString& Parameters)
{
	// The engine's own asset loader logs an Error every time it is asked for a
	// path that names nothing, and the automation framework fails a test on any
	// unexpected Error. Asserting what a deliberate miss reports therefore
	// failed the test doing the asserting. Declared the way SequencerHandlerTests
	// declares it; count 0 means it must occur, so a test that stops
	// provoking it fails.
	AddExpectedError(TEXT("LoadAsset failed"), EAutomationExpectedErrorFlags::Contains, 0);

	FMCPHandlerRegistry Registry;
	FAnimationHandlers::RegisterHandlers(Registry);
	TestTrue(TEXT("sample_pose is registered"), Registry.HasHandler(TEXT("sample_pose")));
	TestTrue(TEXT("get_live_bone_transforms is registered"), Registry.HasHandler(TEXT("get_live_bone_transforms")));
	TestTrue(TEXT("measure_natural_speed is registered"), Registry.HasHandler(TEXT("measure_natural_speed")));

	// sample_pose without an asset names the parameter it wants.
	{
		const TSharedPtr<FJsonObject> Missing = MakePoseResponseObject(
			Registry.ExecuteHandler(TEXT("sample_pose"), MakeShared<FJsonObject>()));
		TestTrue(TEXT("sample_pose missing assetPath returns an object"), Missing.IsValid());
		if (Missing.IsValid())
		{
			TestFalse(TEXT("sample_pose missing assetPath is unsuccessful"), Missing->GetBoolField(TEXT("success")));
			TestTrue(TEXT("sample_pose missing assetPath names assetPath"),
				Missing->GetStringField(TEXT("error")).Contains(TEXT("assetPath")));
		}
	}

	// An unresolvable asset reports the path rather than dereferencing null.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("assetPath"), TEXT("/Game/UEMCP/DoesNotExist_SamplePoseTest"));
		const TSharedPtr<FJsonObject> NotFound = MakePoseResponseObject(
			Registry.ExecuteHandler(TEXT("sample_pose"), Request));
		TestTrue(TEXT("sample_pose unknown asset returns an object"), NotFound.IsValid());
		if (NotFound.IsValid())
		{
			TestFalse(TEXT("sample_pose unknown asset is unsuccessful"), NotFound->GetBoolField(TEXT("success")));
			TestTrue(TEXT("sample_pose unknown asset echoes the path"),
				NotFound->GetStringField(TEXT("error")).Contains(TEXT("DoesNotExist_SamplePoseTest")));
		}
	}

	// measure_natural_speed validates its foot set before it touches an asset,
	// because a speed measured without knowing which bones are feet is a number
	// with no meaning rather than an approximation.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("assetPath"), TEXT("/Game/UEMCP/DoesNotExist_NaturalSpeedTest"));
		const TSharedPtr<FJsonObject> Missing = MakePoseResponseObject(
			Registry.ExecuteHandler(TEXT("measure_natural_speed"), Request));
		TestTrue(TEXT("measure_natural_speed missing footBones returns an object"), Missing.IsValid());
		if (Missing.IsValid())
		{
			TestFalse(TEXT("measure_natural_speed missing footBones is unsuccessful"), Missing->GetBoolField(TEXT("success")));
			TestTrue(TEXT("measure_natural_speed missing footBones names footBones"),
				Missing->GetStringField(TEXT("error")).Contains(TEXT("footBones")));
		}
	}

	// get_live_bone_transforms needs an actor, and rejects a space it cannot
	// serve before it goes looking for one.
	{
		const TSharedPtr<FJsonObject> Missing = MakePoseResponseObject(
			Registry.ExecuteHandler(TEXT("get_live_bone_transforms"), MakeShared<FJsonObject>()));
		TestTrue(TEXT("get_live_bone_transforms missing actorLabel returns an object"), Missing.IsValid());
		if (Missing.IsValid())
		{
			TestFalse(TEXT("get_live_bone_transforms missing actorLabel is unsuccessful"), Missing->GetBoolField(TEXT("success")));
			TestTrue(TEXT("get_live_bone_transforms missing actorLabel names actorLabel"),
				Missing->GetStringField(TEXT("error")).Contains(TEXT("actorLabel")));
		}
	}
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorLabel"), TEXT("UEMCP_DoesNotExist_LiveBoneTest"));
		Request->SetStringField(TEXT("space"), TEXT("skeleton"));
		const TSharedPtr<FJsonObject> Rejected = MakePoseResponseObject(
			Registry.ExecuteHandler(TEXT("get_live_bone_transforms"), Request));
		TestTrue(TEXT("get_live_bone_transforms bad space returns an object"), Rejected.IsValid());
		if (Rejected.IsValid())
		{
			TestFalse(TEXT("get_live_bone_transforms bad space is unsuccessful"), Rejected->GetBoolField(TEXT("success")));
			const FString Error = Rejected->GetStringField(TEXT("error"));
			TestTrue(TEXT("get_live_bone_transforms bad space lists world"), Error.Contains(TEXT("world")));
			TestTrue(TEXT("get_live_bone_transforms bad space lists component"), Error.Contains(TEXT("component")));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
