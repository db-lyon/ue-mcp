// Safe registration and validation coverage for animation(create_skeleton).
// Creating a real skeleton requires a project-owned skeletal mesh, so the
// asset-producing path belongs to the dedicated live test project rather than an
// arbitrary editor automation run.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/AnimationHandlers.h"
#include "Misc/AutomationTest.h"

namespace
{
TSharedPtr<FJsonObject> MakeSkeletonCreateResponse(const TSharedPtr<FJsonValue>& Response)
{
	return (Response.IsValid() && Response->Type == EJson::Object) ? Response->AsObject() : nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAnimationSkeletonCreateRegistrationTest,
	"UE.MCP.Animation.Skeleton.CreateRegistrationAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationSkeletonCreateRegistrationTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FAnimationHandlers::RegisterHandlers(Registry);
	TestTrue(TEXT("create_skeleton is registered"), Registry.HasHandler(TEXT("create_skeleton")));

	TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("name"), TEXT("SK_UEMCP_CreateSkeleton"));
	const TSharedPtr<FJsonObject> MissingMesh = MakeSkeletonCreateResponse(
		Registry.ExecuteHandler(TEXT("create_skeleton"), Request));
	TestTrue(TEXT("create_skeleton missing mesh returns an object"), MissingMesh.IsValid());
	if (MissingMesh.IsValid())
	{
		TestFalse(TEXT("create_skeleton missing mesh is unsuccessful"), MissingMesh->GetBoolField(TEXT("success")));
		TestTrue(TEXT("create_skeleton missing mesh names skeletalMeshPath"),
			MissingMesh->GetStringField(TEXT("error")).Contains(TEXT("skeletalMeshPath")));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
