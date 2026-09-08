#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/SequencerHandlers.h"
#include "HandlerRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

/**
 * #881: scrub_sequence has to be reachable, and a request it cannot honour has
 * to be an error rather than a reported position it never moved to.
 *
 * The assertions here are the ones that hold whatever the editor happens to
 * have open. A call with no parameters is refused either because no sequence is
 * open or because neither seconds nor frame was given, and both are the same
 * statement: the action does not answer with a position it did not scrub to.
 * Anything that needs a sequence open in Sequencer belongs to the live tests,
 * because a test that opened one would be driving the editor hosting it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSequencerScrubRegistrationTest,
	"UE.MCP.Sequencer.Scrub.RegistrationAndPreflight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSequencerScrubRegistrationTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FSequencerHandlers::RegisterHandlers(Registry);

	TestTrue(TEXT("scrub_sequence is registered"), Registry.HasHandler(TEXT("scrub_sequence")));
	// The transport verbs are untouched: scrub is a separate action precisely so
	// play_sequence's closed play|pause|stop enum did not have to widen.
	TestTrue(TEXT("play_sequence is still registered"), Registry.HasHandler(TEXT("play_sequence")));

	const TSharedPtr<FJsonValue> Response =
		Registry.ExecuteHandler(TEXT("scrub_sequence"), MakeShared<FJsonObject>());
	TestTrue(TEXT("a parameterless call returns an object"), Response.IsValid() && Response->Type == EJson::Object);
	if (Response.IsValid() && Response->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Object = Response->AsObject();
		TestFalse(TEXT("a parameterless call is not a success"), Object->GetBoolField(TEXT("success")));
		const FString Error = Object->GetStringField(TEXT("error"));
		TestTrue(TEXT("the refusal says what was missing"),
			Error.Contains(TEXT("Sequence")) || Error.Contains(TEXT("seconds")));
	}

	return true;
}

#endif
