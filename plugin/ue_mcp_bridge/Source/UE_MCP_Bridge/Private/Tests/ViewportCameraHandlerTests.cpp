#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/EditorHandlers.h"
#include "HandlerRegistry.h"

#include "Editor.h"
#include "LevelEditorViewport.h"
#include "EditorViewportClient.h"
#include "Misc/AutomationTest.h"

namespace
{
TSharedPtr<FJsonValue> ViewportCall(FMCPHandlerRegistry& Registry, const TCHAR* Projection, double OrthoZoom = 0.0, bool bIncludeZoom = false)
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	if (Projection) Params->SetStringField(TEXT("projection"), Projection);
	if (bIncludeZoom) Params->SetNumberField(TEXT("orthoZoom"), OrthoZoom);
	return Registry.ExecuteHandler(TEXT("set_viewport_camera"), Params);
}

bool IsFailure(const TSharedPtr<FJsonValue>& Response)
{
	return Response.IsValid() && Response->Type == EJson::Object
		&& !Response->AsObject()->GetBoolField(TEXT("success"));
}
}

/** The camera setter must reject bad projection/zoom values before changing the
* viewport, and valid orthographic/perspective writes must be observable in the
* response. This is intentionally a focused editor test; no project content is
* opened or modified. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FViewportCameraProjectionTest,
	"UE.MCP.Editor.ViewportCamera.ProjectionAndZoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FViewportCameraProjectionTest::RunTest(const FString& Parameters)
{
	if (!GEditor || GEditor->GetLevelViewportClients().Num() == 0)
	{
		AddInfo(TEXT("Skipped: no level editor viewport client is available in this editor session."));
		return true;
	}

	FMCPHandlerRegistry Registry;
	FEditorHandlers::RegisterHandlers(Registry);
	FLevelEditorViewportClient* Client = GCurrentLevelEditingViewportClient
		? GCurrentLevelEditingViewportClient : GEditor->GetLevelViewportClients()[0];
	const ELevelViewportType OriginalType = Client->GetViewportType();
	const float OriginalZoom = Client->GetOrthoZoom();
	const FVector OriginalLocation = Client->GetViewLocation();
	const FRotator OriginalRotation = Client->GetViewRotation();

	const TSharedPtr<FJsonValue> BadProjection = ViewportCall(Registry, TEXT("not-a-view"));
	TestTrue(TEXT("invalid projection is rejected"), IsFailure(BadProjection));
	TestEqual(TEXT("invalid projection leaves viewport type unchanged"), static_cast<int32>(Client->GetViewportType()), static_cast<int32>(OriginalType));

	const TSharedPtr<FJsonValue> BadZoom = ViewportCall(Registry, TEXT("top"), 0.0, true);
	TestTrue(TEXT("zero orthographic zoom is rejected"), IsFailure(BadZoom));
	TestEqual(TEXT("invalid zoom leaves viewport type unchanged"), static_cast<int32>(Client->GetViewportType()), static_cast<int32>(OriginalType));
	TestEqual(TEXT("invalid zoom leaves orthographic zoom unchanged"), Client->GetOrthoZoom(), OriginalZoom);
	for (double Extreme : { 1e-300, 1e300, -1.0 })
	{
		TestTrue(TEXT("unrepresentable/out-of-range zoom is rejected"), IsFailure(ViewportCall(Registry, TEXT("top"), Extreme, true)));
		TestEqual(TEXT("bad zoom preserves projection"), static_cast<int32>(Client->GetViewportType()), static_cast<int32>(OriginalType));
		TestEqual(TEXT("bad zoom preserves zoom"), Client->GetOrthoZoom(), OriginalZoom);
	}

	const TSharedPtr<FJsonValue> Top = ViewportCall(Registry, TEXT("top"), 1000.0, true);
	TestFalse(TEXT("top orthographic projection succeeds"), IsFailure(Top));
	TestEqual(TEXT("top projection is applied"), static_cast<int32>(Client->GetViewportType()), static_cast<int32>(LVT_OrthoXY));
	TestTrue(TEXT("top zoom is applied"), FMath::IsNearlyEqual(Client->GetOrthoZoom(), 1000.0f));
	if (Top.IsValid() && Top->Type == EJson::Object)
	{
		TestTrue(TEXT("response includes current location"), Top->AsObject()->HasField(TEXT("location")));
		TestTrue(TEXT("response includes current rotation"), Top->AsObject()->HasField(TEXT("rotation")));
		TestEqual(TEXT("response includes top projection"), Top->AsObject()->GetStringField(TEXT("projection")), FString(TEXT("top")));
	}

	const TSharedPtr<FJsonValue> Perspective = ViewportCall(Registry, TEXT("perspective"));
	TestFalse(TEXT("perspective projection succeeds"), IsFailure(Perspective));
	TestEqual(TEXT("perspective projection is applied"), static_cast<int32>(Client->GetViewportType()), static_cast<int32>(LVT_Perspective));
	const FVector BeforeCrossLocation = Client->GetViewLocation();
	const FRotator BeforeCrossRotation = Client->GetViewRotation();
	auto CrossParams = MakeShared<FJsonObject>();
	auto CrossArgs = MakeShared<FJsonObject>();
	CrossArgs->SetStringField(TEXT("projection"), TEXT("top"));
	CrossArgs->SetNumberField(TEXT("orthoZoom"), 4000.0);
	auto CrossLocation = MakeShared<FJsonObject>();
	CrossLocation->SetNumberField(TEXT("x"), 12345.0); CrossLocation->SetNumberField(TEXT("y"), -6789.0); CrossLocation->SetNumberField(TEXT("z"), 10000.0);
	CrossArgs->SetObjectField(TEXT("location"), CrossLocation);
	CrossParams->SetObjectField(TEXT("args"), CrossArgs);
	const auto Cross = Registry.ExecuteHandler(TEXT("set_viewport_camera"), CrossParams);
	TestFalse(TEXT("combined nested projection and pose succeeds"), IsFailure(Cross));
	TestTrue(TEXT("pose applies to the requested projection cache"), Client->GetViewLocation().Equals(FVector(12345,-6789,10000),.01));
	if (Cross && Cross->Type == EJson::Object && Cross->AsObject()->HasField(TEXT("rollback")))
	{
		const auto Payload = Cross->AsObject()->GetObjectField(TEXT("rollback"))->GetObjectField(TEXT("payload"));
		TestFalse(TEXT("cross-mode rollback succeeds"), IsFailure(Registry.ExecuteHandler(TEXT("set_viewport_camera"),Payload)));
		TestEqual(TEXT("rollback restores perspective"), static_cast<int32>(Client->GetViewportType()), static_cast<int32>(LVT_Perspective));
		TestTrue(TEXT("rollback restores the previous active location"), Client->GetViewLocation().Equals(BeforeCrossLocation,.01));
		TestTrue(TEXT("rollback restores the previous active rotation"), Client->GetViewRotation().Equals(BeforeCrossRotation,.001));
	}

	// Restore only the viewport state this test touched.
	Client->SetViewportType(OriginalType);
	Client->SetOrthoZoom(OriginalZoom);
	Client->SetViewLocation(OriginalLocation);
	Client->SetViewRotation(OriginalRotation);
	Client->Invalidate();
	return true;
}

#endif
