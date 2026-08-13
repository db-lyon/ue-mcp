// Safe preflight coverage for the live PIE visibility batch.
//
// These Automation tests may run inside a user's editor, so they deliberately
// stop before resolving or mutating a live PIE world. Runtime state transitions
// belong in the dedicated bridge smoke project.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/EditorHandlers.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRuntimeVisibilityPreflightTest,
	"UE.MCP.Editor.RuntimeVisibility.RegistrationAndPreflight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeVisibilityPreflightTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FEditorHandlers::RegisterHandlers(Registry);
	TestTrue(TEXT("set handler is registered"), Registry.HasHandler(TEXT("set_runtime_visibility")));
	TestTrue(TEXT("restore handler is registered"), Registry.HasHandler(TEXT("restore_runtime_visibility")));
	const TArray<FString> HandlerNames = Registry.GetHandlerNames();
	TestTrue(TEXT("set handler is capability-discoverable"),
		HandlerNames.Contains(TEXT("set_runtime_visibility")));
	TestTrue(TEXT("restore handler is capability-discoverable"),
		HandlerNames.Contains(TEXT("restore_runtime_visibility")));

	auto ExpectFailure = [this, &Registry](
		const TCHAR* Description,
		const TCHAR* Method,
		const TSharedPtr<FJsonObject>& Request,
		const TCHAR* ExpectedText)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, Request);
		TestTrue(FString::Printf(TEXT("%s returns an object"), Description),
			Response.IsValid() && Response->Type == EJson::Object);
		if (Response.IsValid() && Response->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Object = Response->AsObject();
			TestFalse(FString::Printf(TEXT("%s is unsuccessful"), Description),
				Object->GetBoolField(TEXT("success")));
			TestTrue(FString::Printf(TEXT("%s explains the rejection"), Description),
				Object->GetStringField(TEXT("error")).Contains(ExpectedText));
		}
	};

	TSharedPtr<FJsonObject> MissingHidden = MakeShared<FJsonObject>();
	MissingHidden->SetArrayField(TEXT("actorLabels"),
		{ MakeShared<FJsonValueString>(TEXT("Example")) });
	ExpectFailure(TEXT("missing hidden"), TEXT("set_runtime_visibility"),
		MissingHidden, TEXT("hidden"));

	TSharedPtr<FJsonObject> EditorWorld = MakeShared<FJsonObject>();
	EditorWorld->SetBoolField(TEXT("hidden"), true);
	EditorWorld->SetStringField(TEXT("world"), TEXT("editor"));
	EditorWorld->SetArrayField(TEXT("actorLabels"),
		{ MakeShared<FJsonValueString>(TEXT("Example")) });
	ExpectFailure(TEXT("editor world"), TEXT("set_runtime_visibility"),
		EditorWorld, TEXT("PIE-only"));

	TSharedPtr<FJsonObject> GameWorld = MakeShared<FJsonObject>();
	GameWorld->SetBoolField(TEXT("hidden"), true);
	GameWorld->SetStringField(TEXT("world"), TEXT("game"));
	GameWorld->SetArrayField(TEXT("actorLabels"),
		{ MakeShared<FJsonValueString>(TEXT("Example")) });
	ExpectFailure(TEXT("game world"), TEXT("set_runtime_visibility"),
		GameWorld, TEXT("PIE-only"));

	TSharedPtr<FJsonObject> FractionalPIEInstance = MakeShared<FJsonObject>();
	FractionalPIEInstance->SetBoolField(TEXT("hidden"), true);
	FractionalPIEInstance->SetStringField(TEXT("actorClass"), TEXT("AActor"));
	FractionalPIEInstance->SetNumberField(TEXT("pieInstance"), 1.5);
	ExpectFailure(TEXT("fractional PIE instance"), TEXT("set_runtime_visibility"),
		FractionalPIEInstance, TEXT("integer"));

	TSharedPtr<FJsonObject> MissingSelector = MakeShared<FJsonObject>();
	MissingSelector->SetBoolField(TEXT("hidden"), true);
	ExpectFailure(TEXT("missing selector"), TEXT("set_runtime_visibility"),
		MissingSelector, TEXT("exactly one actor selector"));

	TSharedPtr<FJsonObject> ConflictingSelectors = MakeShared<FJsonObject>();
	ConflictingSelectors->SetBoolField(TEXT("hidden"), true);
	ConflictingSelectors->SetStringField(TEXT("actorClass"), TEXT("Actor"));
	ConflictingSelectors->SetArrayField(TEXT("actorLabels"),
		{ MakeShared<FJsonValueString>(TEXT("Example")) });
	ExpectFailure(TEXT("conflicting selectors"), TEXT("set_runtime_visibility"),
		ConflictingSelectors, TEXT("exactly one actor selector"));

	TSharedPtr<FJsonObject> OversizedLabels = MakeShared<FJsonObject>();
	OversizedLabels->SetBoolField(TEXT("hidden"), true);
	TArray<TSharedPtr<FJsonValue>> Labels;
	for (int32 Index = 0; Index < 65; ++Index)
	{
		Labels.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("Actor_%d"), Index)));
	}
	OversizedLabels->SetArrayField(TEXT("actorLabels"), Labels);
	ExpectFailure(TEXT("oversized labels"), TEXT("set_runtime_visibility"),
		OversizedLabels, TEXT("64"));

	TSharedPtr<FJsonObject> OversizedBound = MakeShared<FJsonObject>();
	OversizedBound->SetBoolField(TEXT("hidden"), true);
	OversizedBound->SetNumberField(TEXT("maxTargets"), 257);
	OversizedBound->SetStringField(TEXT("actorClass"), TEXT("Actor"));
	ExpectFailure(TEXT("oversized maxTargets"), TEXT("set_runtime_visibility"),
		OversizedBound, TEXT("256"));

	TSharedPtr<FJsonObject> NoEffects = MakeShared<FJsonObject>();
	NoEffects->SetBoolField(TEXT("hidden"), true);
	NoEffects->SetStringField(TEXT("actorClass"), TEXT("Actor"));
	NoEffects->SetBoolField(TEXT("affectActor"), false);
	NoEffects->SetBoolField(TEXT("affectComponents"), false);
	ExpectFailure(TEXT("disabled effects"), TEXT("set_runtime_visibility"),
		NoEffects, TEXT("At least one"));

	TSharedPtr<FJsonObject> UnknownActorClass = MakeShared<FJsonObject>();
	UnknownActorClass->SetBoolField(TEXT("hidden"), true);
	UnknownActorClass->SetStringField(
		TEXT("actorClass"), TEXT("DefinitelyMissingRuntimeVisibilityActor"));
	ExpectFailure(TEXT("unknown actor class"), TEXT("set_runtime_visibility"),
		UnknownActorClass, TEXT("Class not found"));

	TSharedPtr<FJsonObject> WrongComponentBase = MakeShared<FJsonObject>();
	WrongComponentBase->SetBoolField(TEXT("hidden"), true);
	WrongComponentBase->SetArrayField(TEXT("actorLabels"),
		{ MakeShared<FJsonValueString>(TEXT("Example")) });
	WrongComponentBase->SetArrayField(TEXT("componentClasses"),
		{ MakeShared<FJsonValueString>(TEXT("AActor")) });
	ExpectFailure(TEXT("wrong component base"), TEXT("set_runtime_visibility"),
		WrongComponentBase, TEXT("does not derive from SceneComponent"));

	ExpectFailure(TEXT("missing restore token"), TEXT("restore_runtime_visibility"),
		MakeShared<FJsonObject>(), TEXT("rollbackToken"));

	TSharedPtr<FJsonObject> UnknownRestoreToken = MakeShared<FJsonObject>();
	UnknownRestoreToken->SetStringField(TEXT("rollbackToken"), TEXT("not-a-live-token"));
	ExpectFailure(TEXT("unknown restore token"), TEXT("restore_runtime_visibility"),
		UnknownRestoreToken, TEXT("not found or has expired"));
	return true;
}

#endif
