#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerSceneCaptureJob.h"
#include "ContentStreaming.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPWarmCaptureViewExpiryTest,
	"UE.MCP.Editor.WarmCapture.StreamingViewExpiry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPWarmCaptureViewExpiryTest::RunTest(const FString& Parameters)
{
	using namespace UEMCP::SceneCapture;
	auto& Streaming = IStreamingManager::Get();
	// Unique distant viewpoints avoid matching a real editor camera or each other.
	const FVector CaptureLocation(47123456, 58234567, 69345678);
	const FVector OtherLocation(-47123456, -58234567, -69345678);
	const auto HasView = [&Streaming](const FVector& Location)
	{
		for (int32 Index = 0; Index < Streaming.GetNumViews(); ++Index)
			if (Streaming.GetViewInformation(Index).ViewOrigin.Equals(Location, 0.5)) return true;
		return false;
	};
	Streaming.AddViewInformation(OtherLocation, 640.0f, 640.0f, 1.0f, false, 1.0f);
	SubmitWarmStreamingView(CaptureLocation, 1920, 60.0f, nullptr);
	TestTrue(TEXT("Submitted capture view reaches the current streaming update"), HasView(CaptureLocation));
	TestTrue(TEXT("Submitting capture preserves another non-override view"), HasView(OtherLocation));

	Streaming.UpdateResourceStreaming(WarmStreamingViewDurationSeconds, false);
	TestTrue(TEXT("View is consumed in the update that reaches its expiry boundary"), HasView(CaptureLocation));
	// No further AddViewInformation is needed to retire the capture contribution.
	Streaming.UpdateResourceStreaming(0.0f, false);
	TestFalse(TEXT("Next update no longer contains the expired capture view"), HasView(CaptureLocation));
	TestTrue(TEXT("Expiry preserves the other view"), HasView(OtherLocation));

	// Retire only the test's sentinel via its normal lifetime, not RemoveStreamingViews.
	Streaming.AddViewInformation(OtherLocation, 640.0f, 640.0f, 1.0f, false, WarmStreamingViewDurationSeconds);
	Streaming.UpdateResourceStreaming(WarmStreamingViewDurationSeconds, false);
	Streaming.UpdateResourceStreaming(0.0f, false);
	TestFalse(TEXT("Sentinel is cleaned up without a new viewport submission"), HasView(OtherLocation));
	return true;
}

#endif
