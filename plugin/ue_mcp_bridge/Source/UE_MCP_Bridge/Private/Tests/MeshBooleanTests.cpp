// Coverage for #916.
//
// asset(mesh_boolean) writes a StaticMesh, and everything it can refuse it
// refuses BEFORE it touches one. These assertions cover that front half: the
// argument validation, the destructive-form guard, and the protected-mount
// guardrail, all of which run before the Geometry Script plugin is even asked
// for. That is deliberate, and it is what makes them assertable on a machine
// where the plugin is disabled.
//
// The boolean itself needs two real meshes and the Geometry Script plugin, so
// it belongs to the live smoke pass rather than here. Nothing in this file
// creates, loads or writes an asset.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/AssetHandlers_MeshBoolean.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

namespace
{
	TSharedPtr<FJsonObject> BooleanResponseObject(const TSharedPtr<FJsonValue>& Response)
	{
		return (Response.IsValid() && Response->Type == EJson::Object)
			? Response->AsObject()
			: TSharedPtr<FJsonObject>();
	}

	bool BooleanResponseBool(const TSharedPtr<FJsonValue>& Response, const TCHAR* Field, bool bDefault = false)
	{
		const TSharedPtr<FJsonObject> Obj = BooleanResponseObject(Response);
		if (!Obj.IsValid()) return bDefault;
		bool bValue = bDefault;
		Obj->TryGetBoolField(Field, bValue);
		return bValue;
	}

	FString BooleanResponseString(const TSharedPtr<FJsonValue>& Response, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject> Obj = BooleanResponseObject(Response);
		if (!Obj.IsValid()) return FString();
		FString Value;
		Obj->TryGetStringField(Field, Value);
		return Value;
	}

	/** The shape every case here sends, with one field swapped per case. */
	TSharedPtr<FJsonObject> BaseBooleanParams()
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("operation"), TEXT("subtract"));
		Params->SetStringField(TEXT("targetPath"), TEXT("/Game/UEMCPNeverExists/SM_Target"));
		Params->SetStringField(TEXT("toolPath"), TEXT("/Game/UEMCPNeverExists/SM_Tool"));
		return Params;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPMeshBooleanRefusesBeforeWritingTest,
	"UE.MCP.Asset.MeshBoolean.RefusesBeforeItTouchesAnAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPMeshBooleanRefusesBeforeWritingTest::RunTest(const FString& Parameters)
{
	// The engine's own asset loader logs an Error every time it is asked for a
	// path that names nothing, and the automation framework fails a test on any
	// unexpected Error. Asserting what a deliberate miss reports therefore
	// failed the test doing the asserting. Declared the way SequencerHandlerTests
	// declares it; count 0 means it must occur, so a test that stops
	// provoking it fails.
	AddExpectedError(TEXT("LoadAsset failed"), EAutomationExpectedErrorFlags::Contains, 0);

	FMCPHandlerRegistry Registry;
	FAssetMeshBooleanHandlers::RegisterHandlers(Registry);
	if (!TestTrue(TEXT("mesh_boolean is registered"), Registry.HasHandler(TEXT("mesh_boolean")))) return false;

	// The boolean can run for minutes on a repair-sized mesh, so it registers a
	// timeout of its own. The client mirrors that number by hand, and this is
	// the assertion that the registration it mirrors actually happened.
	TestEqual(TEXT("mesh_boolean registers its own game-thread timeout"),
		Registry.GetHandlerTimeout(TEXT("mesh_boolean")), 300.0f);

	{
		TSharedPtr<FJsonObject> Params = BaseBooleanParams();
		Params->SetStringField(TEXT("operation"), TEXT("smoosh"));

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("mesh_boolean"), Params);
		TestFalse(TEXT("an unknown operation is refused"),
			BooleanResponseBool(Response, TEXT("success"), true));
		TestTrue(TEXT("an unknown operation lists the ones that exist"),
			BooleanResponseString(Response, TEXT("error")).Contains(TEXT("subtract")));
	}

	{
		// Two destinations, no way to honour both. Guessing here would pick
		// between writing a new asset and overwriting the caller's target.
		TSharedPtr<FJsonObject> Params = BaseBooleanParams();
		Params->SetBoolField(TEXT("inPlace"), true);
		Params->SetStringField(TEXT("outputPath"), TEXT("/Game/UEMCPNeverExists/SM_Result"));

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("mesh_boolean"), Params);
		TestFalse(TEXT("outputPath together with inPlace is refused"),
			BooleanResponseBool(Response, TEXT("success"), true));
		TestTrue(TEXT("the refusal says both were given"),
			BooleanResponseString(Response, TEXT("error")).Contains(TEXT("inPlace")));
	}

	{
		TSharedPtr<FJsonObject> Params = BaseBooleanParams();
		Params->SetStringField(TEXT("lodType"), TEXT("WhicheverIsFine"));

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("mesh_boolean"), Params);
		TestFalse(TEXT("an unknown lodType is refused"),
			BooleanResponseBool(Response, TEXT("success"), true));
		TestTrue(TEXT("the refusal lists the LOD selectors"),
			BooleanResponseString(Response, TEXT("error")).Contains(TEXT("MaxAvailable")));
	}

	{
		TSharedPtr<FJsonObject> Params = BaseBooleanParams();
		Params->SetStringField(TEXT("nanite"), TEXT("maybe"));

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("mesh_boolean"), Params);
		TestFalse(TEXT("an unknown nanite mode is refused"),
			BooleanResponseBool(Response, TEXT("success"), true));
		TestTrue(TEXT("the refusal lists the nanite modes"),
			BooleanResponseString(Response, TEXT("error")).Contains(TEXT("inherit")));
	}

	{
		TSharedPtr<FJsonObject> Params = BaseBooleanParams();
		Params->SetStringField(TEXT("onConflict"), TEXT("clobber"));

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("mesh_boolean"), Params);
		TestFalse(TEXT("an unknown onConflict is refused"),
			BooleanResponseBool(Response, TEXT("success"), true));
	}

	{
		// The guardrail runs before the Geometry Script plugin is asked for
		// anything, so engine content is refused whether or not the plugin is
		// installed on this machine.
		TSharedPtr<FJsonObject> Params = BaseBooleanParams();
		Params->SetStringField(TEXT("outputPath"), TEXT("/Engine/UEMCPNeverExists/SM_Result"));

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("mesh_boolean"), Params);
		TestFalse(TEXT("an output path on a protected mount is refused"),
			BooleanResponseBool(Response, TEXT("success"), true));
		TestTrue(TEXT("the refusal names the guardrail"),
			BooleanResponseString(Response, TEXT("error")).Contains(TEXT("protected mount")));
	}

	{
		// Everything valid, nothing on disk. Whether the Geometry Script plugin
		// is here or not, the answer is a named failure rather than a crash,
		// and it is one of exactly two reasons.
		const TSharedPtr<FJsonValue> Response =
			Registry.ExecuteHandler(TEXT("mesh_boolean"), BaseBooleanParams());
		TestFalse(TEXT("a missing target mesh is not a success"),
			BooleanResponseBool(Response, TEXT("success"), true));

		const FString Reason = BooleanResponseString(Response, TEXT("reason"));
		TestTrue(FString::Printf(
			TEXT("the failure is either the missing asset or the missing plugin, not something unnamed (got '%s')"),
			*Reason),
			Reason == TEXT("missing")
			|| Reason == TEXT("notIndexed")
			|| Reason == TEXT("geometry_scripting_unavailable"));
	}

	return true;
}

#endif
