// Coverage for post-process settings and their override flags (#950).
//
// The bug this guards is silent by construction: a value written into
// FPostProcessSettings without its bOverride_<Name> bit reads back exactly as
// requested and is ignored by the renderer. So the assertion that matters is
// not "the value was written" but "the bit was turned on with it", and it is
// checked by reading the struct back through the reader rather than by trusting
// the writer's own report.
//
// The round trip runs against a transient verification actor, which cannot be
// saved into the map and is destroyed at the end.

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Scene.h"
#include "HandlerRegistry.h"
#include "Handlers/Level/LevelHandlers.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

namespace
{
	TSharedPtr<FJsonObject> MCPPostProcessTestRun(
		FMCPHandlerRegistry& Registry,
		const TCHAR* Method,
		const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, Request);
		if (!Response.IsValid() || Response->Type != EJson::Object)
		{
			return nullptr;
		}
		return Response->AsObject();
	}

	/** The row for one setting in a get_post_process_settings response. */
	TSharedPtr<FJsonObject> MCPPostProcessTestFindRow(
		const TSharedPtr<FJsonObject>& Response,
		const TCHAR* SettingName)
	{
		if (!Response.IsValid()) return nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Response->TryGetArrayField(TEXT("settings"), Rows) || !Rows) return nullptr;
		for (const TSharedPtr<FJsonValue>& Value : *Rows)
		{
			const TSharedPtr<FJsonObject> Row = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Row.IsValid()) continue;
			if (Row->GetStringField(TEXT("name")).Equals(SettingName, ESearchCase::IgnoreCase)) return Row;
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelPostProcessOverridePairingTest,
	"UE.MCP.Level.PostProcess.EveryExposureValueHasAnOverrideBit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelPostProcessOverridePairingTest::RunTest(const FString& Parameters)
{
	// The whole feature rests on one naming convention: the bit that gates the
	// value `Foo` is called `bOverride_Foo`. If the engine ever renames one of
	// these, working generically off the reflected fields would silently stop
	// enabling anything, so the convention is asserted rather than assumed.
	UScriptStruct* Struct = FPostProcessSettings::StaticStruct();
	TestNotNull(TEXT("FPostProcessSettings is reflected"), Struct);
	if (!Struct) return true;

	for (const TCHAR* ValueField : {
		TEXT("AutoExposureMinBrightness"),
		TEXT("AutoExposureMaxBrightness"),
		TEXT("AutoExposureBias"),
		TEXT("AutoExposureMethod"),
		TEXT("BloomIntensity") })
	{
		FProperty* Value = Struct->FindPropertyByName(FName(ValueField));
		TestNotNull(FString::Printf(TEXT("%s exists"), ValueField), Value);

		const FString FlagName = FString(TEXT("bOverride_")) + ValueField;
		FBoolProperty* Flag = CastField<FBoolProperty>(Struct->FindPropertyByName(FName(*FlagName)));
		TestNotNull(FString::Printf(TEXT("%s exists and is a bool"), *FlagName), Flag);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelPostProcessSettingsRoundTripTest,
	"UE.MCP.Level.PostProcess.SettingWritesTheOverrideBitToo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelPostProcessSettingsRoundTripTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);

	for (const TCHAR* Method : {
		TEXT("set_post_process_settings"),
		TEXT("get_post_process_settings"),
		TEXT("set_fixed_exposure") })
	{
		TestTrue(FString::Printf(TEXT("%s is registered"), Method), Registry.HasHandler(Method));
	}

	// A settings map is required: an empty write is a request that means
	// nothing, and answering it with success would be a lie.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorLabel"), TEXT("UEMCP_NoSuchPostProcessVolume"));
		const TSharedPtr<FJsonObject> Response =
			MCPPostProcessTestRun(Registry, TEXT("set_post_process_settings"), Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("a write with no settings fails"), Response->GetBoolField(TEXT("success")));
		}
	}

	// The round trip needs a subject. A transient verification actor is one
	// that cannot end up committed to source control.
	TSharedPtr<FJsonObject> SpawnRequest = MakeShared<FJsonObject>();
	SpawnRequest->SetStringField(TEXT("actorClass"), TEXT("PostProcessVolume"));
	SpawnRequest->SetStringField(TEXT("label"), TEXT("UEMCP_PostProcessOverrideTest"));
	const TSharedPtr<FJsonObject> SpawnResponse =
		MCPPostProcessTestRun(Registry, TEXT("spawn_transient_actor"), SpawnRequest);
	if (!SpawnResponse.IsValid() || !SpawnResponse->GetBoolField(TEXT("success")))
	{
		// No editor world in this context, and this test may not open a map.
		return true;
	}
	const TSharedPtr<FJsonObject> SpawnedActor = SpawnResponse->GetObjectField(TEXT("actor"));
	const FString ActorPath = SpawnedActor.IsValid() ? SpawnedActor->GetStringField(TEXT("actorPath")) : FString();
	const FString ActorLabel = TEXT("UEMCP_PostProcessOverrideTest");

	// An unknown key fails the WHOLE call rather than writing the keys around
	// it, so a typo cannot leave a volume half-configured.
	{
		TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
		Settings->SetNumberField(TEXT("AutoExposureMinBrightness"), 2.0);
		Settings->SetNumberField(TEXT("NotARealPostProcessSetting"), 1.0);
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorLabel"), ActorLabel);
		Request->SetObjectField(TEXT("settings"), Settings);
		const TSharedPtr<FJsonObject> Response =
			MCPPostProcessTestRun(Registry, TEXT("set_post_process_settings"), Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("an unknown setting name fails the call"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and says nothing was written"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("Nothing was written")));
		}
	}

	// The assertion the issue is about: writing the value also turns the bit on.
	{
		TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
		Settings->SetNumberField(TEXT("AutoExposureMinBrightness"), 1.5);
		Settings->SetNumberField(TEXT("AutoExposureMaxBrightness"), 1.5);
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorLabel"), ActorLabel);
		Request->SetObjectField(TEXT("settings"), Settings);
		const TSharedPtr<FJsonObject> Response =
			MCPPostProcessTestRun(Registry, TEXT("set_post_process_settings"), Request);
		TestTrue(TEXT("the write succeeds"),
			Response.IsValid() && Response->GetBoolField(TEXT("success")));
	}

	// Read it back through the reader rather than trusting the writer's report.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorLabel"), ActorLabel);
		Request->SetBoolField(TEXT("onlyOverridden"), true);
		const TSharedPtr<FJsonObject> Response =
			MCPPostProcessTestRun(Registry, TEXT("get_post_process_settings"), Request);
		TestTrue(TEXT("the read succeeds"),
			Response.IsValid() && Response->GetBoolField(TEXT("success")));

		const TSharedPtr<FJsonObject> MinRow =
			MCPPostProcessTestFindRow(Response, TEXT("AutoExposureMinBrightness"));
		TestNotNull(TEXT("min brightness is reported as overridden"), MinRow.Get());
		if (MinRow.IsValid())
		{
			TestTrue(TEXT("and its override bit really is on"), MinRow->GetBoolField(TEXT("overridden")));
		}

		const TSharedPtr<FJsonObject> MaxRow =
			MCPPostProcessTestFindRow(Response, TEXT("AutoExposureMaxBrightness"));
		TestNotNull(TEXT("max brightness is reported as overridden"), MaxRow.Get());
	}

	// enableOverrides=false is the explicit opt out, and it must NOT turn the
	// bit on, or the opt out would be a lie in the other direction.
	{
		TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
		Settings->SetNumberField(TEXT("BloomIntensity"), 0.25);
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorLabel"), ActorLabel);
		Request->SetObjectField(TEXT("settings"), Settings);
		Request->SetBoolField(TEXT("enableOverrides"), false);
		const TSharedPtr<FJsonObject> Response =
			MCPPostProcessTestRun(Registry, TEXT("set_post_process_settings"), Request);
		if (Response.IsValid() && Response->GetBoolField(TEXT("success")))
		{
			TSharedPtr<FJsonObject> ReadRequest = MakeShared<FJsonObject>();
			ReadRequest->SetStringField(TEXT("actorLabel"), ActorLabel);
			TArray<TSharedPtr<FJsonValue>> Names;
			Names.Add(MakeShared<FJsonValueString>(TEXT("BloomIntensity")));
			ReadRequest->SetArrayField(TEXT("names"), Names);
			const TSharedPtr<FJsonObject> ReadResponse =
				MCPPostProcessTestRun(Registry, TEXT("get_post_process_settings"), ReadRequest);
			const TSharedPtr<FJsonObject> Row =
				MCPPostProcessTestFindRow(ReadResponse, TEXT("BloomIntensity"));
			if (Row.IsValid())
			{
				TestFalse(TEXT("enableOverrides=false leaves the bit off"), Row->GetBoolField(TEXT("overridden")));
			}
		}
	}

	// set_fixed_exposure is the same mechanism with the two writes chosen for
	// you, so it has to leave both bits on as well.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorLabel"), ActorLabel);
		Request->SetNumberField(TEXT("exposure"), 3.0);
		const TSharedPtr<FJsonObject> Response =
			MCPPostProcessTestRun(Registry, TEXT("set_fixed_exposure"), Request);
		TestTrue(TEXT("set_fixed_exposure succeeds"),
			Response.IsValid() && Response->GetBoolField(TEXT("success")));

		TSharedPtr<FJsonObject> ReadRequest = MakeShared<FJsonObject>();
		ReadRequest->SetStringField(TEXT("actorLabel"), ActorLabel);
		ReadRequest->SetBoolField(TEXT("onlyOverridden"), true);
		const TSharedPtr<FJsonObject> ReadResponse =
			MCPPostProcessTestRun(Registry, TEXT("get_post_process_settings"), ReadRequest);
		const TSharedPtr<FJsonObject> MinRow =
			MCPPostProcessTestFindRow(ReadResponse, TEXT("AutoExposureMinBrightness"));
		const TSharedPtr<FJsonObject> MaxRow =
			MCPPostProcessTestFindRow(ReadResponse, TEXT("AutoExposureMaxBrightness"));
		TestNotNull(TEXT("fixed exposure overrode min brightness"), MinRow.Get());
		TestNotNull(TEXT("fixed exposure overrode max brightness"), MaxRow.Get());
	}

	// Clean up the subject, since a test about not leaving state behind that
	// left an actor behind would be absurd.
	if (!ActorPath.IsEmpty())
	{
		TSharedPtr<FJsonObject> DestroyRequest = MakeShared<FJsonObject>();
		DestroyRequest->SetStringField(TEXT("actorPath"), ActorPath);
		const TSharedPtr<FJsonObject> DestroyResponse =
			MCPPostProcessTestRun(Registry, TEXT("destroy_transient_actor"), DestroyRequest);
		TestTrue(TEXT("the verification volume is destroyed"),
			DestroyResponse.IsValid() && DestroyResponse->GetBoolField(TEXT("success")));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
