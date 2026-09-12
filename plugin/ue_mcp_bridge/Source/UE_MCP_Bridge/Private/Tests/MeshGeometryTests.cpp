// Coverage for asset(get_mesh_geometry) and asset(measure_mesh_geometry) that
// is safe to run anywhere.
//
// run_automation_tests dispatches every EditorContext/EngineFilter test in the
// process when it is called without a filter, against whatever project the
// bridge is attached to. Both handlers here are reads, so nothing they do can
// mutate a project, but a test that named a real mesh would still only pass on
// the machine that had it. Everything below is registration and argument
// validation, which holds in any project; the geometry itself is exercised by
// the smoke suite against the dedicated test project.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/AssetHandlers_Geometry.h"
#include "Misc/AutomationTest.h"

namespace
{
TSharedPtr<FJsonObject> MakeGeometryResponseObject(const TSharedPtr<FJsonValue>& Response)
{
	return (Response.IsValid() && Response->Type == EJson::Object) ? Response->AsObject() : nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetMeshGeometryRegistrationTest,
	"UE.MCP.Asset.MeshGeometry.RegistrationAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetMeshGeometryRegistrationTest::RunTest(const FString& Parameters)
{
	// The engine's own asset loader logs an Error every time it is asked for a
	// path that names nothing, and the automation framework fails a test on any
	// unexpected Error. Asserting what a deliberate miss reports therefore
	// failed the test doing the asserting. Declared the way SequencerHandlerTests
	// declares it; a count of 0 means any number of occurrences.
	AddExpectedError(TEXT("LoadAsset failed"), EAutomationExpectedErrorFlags::Contains, 0);

	FMCPHandlerRegistry Registry;
	FAssetGeometryHandlers::RegisterHandlers(Registry);
	TestTrue(TEXT("get_mesh_geometry is registered"), Registry.HasHandler(TEXT("get_mesh_geometry")));
	TestTrue(TEXT("measure_mesh_geometry is registered"), Registry.HasHandler(TEXT("measure_mesh_geometry")));

	// Missing assetPath names the parameter it wants.
	{
		const TSharedPtr<FJsonObject> Missing = MakeGeometryResponseObject(
			Registry.ExecuteHandler(TEXT("get_mesh_geometry"), MakeShared<FJsonObject>()));
		TestTrue(TEXT("missing assetPath returns an object"), Missing.IsValid());
		if (Missing.IsValid())
		{
			TestFalse(TEXT("missing assetPath is unsuccessful"), Missing->GetBoolField(TEXT("success")));
			TestTrue(TEXT("missing assetPath names assetPath"),
				Missing->GetStringField(TEXT("error")).Contains(TEXT("assetPath")));
		}
	}
	{
		const TSharedPtr<FJsonObject> Missing = MakeGeometryResponseObject(
			Registry.ExecuteHandler(TEXT("measure_mesh_geometry"), MakeShared<FJsonObject>()));
		TestTrue(TEXT("measure missing assetPath returns an object"), Missing.IsValid());
		if (Missing.IsValid())
		{
			TestFalse(TEXT("measure missing assetPath is unsuccessful"), Missing->GetBoolField(TEXT("success")));
			TestTrue(TEXT("measure missing assetPath names assetPath"),
				Missing->GetStringField(TEXT("error")).Contains(TEXT("assetPath")));
		}
	}

	// An unknown include token is rejected before anything is loaded, and the
	// message carries the full allowed set so a caller does not have to guess.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("assetPath"), TEXT("/Game/UEMCP/DoesNotExist_MeshGeometryTest"));
		Request->SetArrayField(TEXT("include"), { MakeShared<FJsonValueString>(TEXT("tangents")) });
		const TSharedPtr<FJsonObject> Rejected = MakeGeometryResponseObject(
			Registry.ExecuteHandler(TEXT("get_mesh_geometry"), Request));
		TestTrue(TEXT("bad include returns an object"), Rejected.IsValid());
		if (Rejected.IsValid())
		{
			TestFalse(TEXT("bad include is unsuccessful"), Rejected->GetBoolField(TEXT("success")));
			const FString Error = Rejected->GetStringField(TEXT("error"));
			TestTrue(TEXT("bad include quotes the offending token"), Error.Contains(TEXT("tangents")));
			TestTrue(TEXT("bad include lists the allowed set"), Error.Contains(TEXT("positions")));
			TestTrue(TEXT("bad include lists triangles too"), Error.Contains(TEXT("triangles")));
		}
	}

	// An empty include is a request for nothing, which is a mistake rather than
	// an answer, so it is refused instead of returning empty section arrays.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("assetPath"), TEXT("/Game/UEMCP/DoesNotExist_MeshGeometryTest"));
		Request->SetArrayField(TEXT("include"), TArray<TSharedPtr<FJsonValue>>());
		const TSharedPtr<FJsonObject> Rejected = MakeGeometryResponseObject(
			Registry.ExecuteHandler(TEXT("get_mesh_geometry"), Request));
		TestTrue(TEXT("empty include returns an object"), Rejected.IsValid());
		if (Rejected.IsValid())
		{
			TestFalse(TEXT("empty include is unsuccessful"), Rejected->GetBoolField(TEXT("success")));
			TestTrue(TEXT("empty include says what to pass"),
				Rejected->GetStringField(TEXT("error")).Contains(TEXT("include")));
		}
	}

	// A path that resolves to nothing reports the path, not a null dereference.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("assetPath"), TEXT("/Game/UEMCP/DoesNotExist_MeshGeometryTest"));
		const TSharedPtr<FJsonObject> NotFound = MakeGeometryResponseObject(
			Registry.ExecuteHandler(TEXT("get_mesh_geometry"), Request));
		TestTrue(TEXT("unknown asset returns an object"), NotFound.IsValid());
		if (NotFound.IsValid())
		{
			TestFalse(TEXT("unknown asset is unsuccessful"), NotFound->GetBoolField(TEXT("success")));
			TestTrue(TEXT("unknown asset echoes the path"),
				NotFound->GetStringField(TEXT("error")).Contains(TEXT("DoesNotExist_MeshGeometryTest")));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
