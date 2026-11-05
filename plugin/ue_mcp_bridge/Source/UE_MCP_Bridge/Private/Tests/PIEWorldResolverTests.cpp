#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/PIE/PIEWorldResolver.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEWorldResolverTest,
	"UE.MCP.PIE.PIEWorldResolver.SelectsLocalPlayerWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEWorldResolverTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIE::PIEWorldResolver;

	TArray<FWorldCandidate> Candidates;
	FWorldCandidate DedicatedServer;
	DedicatedServer.PIEInstance = 0;
	DedicatedServer.bIsRuntimeWorld = true;
	DedicatedServer.LocalPlayerCount = 0;
	Candidates.Add(DedicatedServer);

	FWorldCandidate ActiveClient;
	ActiveClient.PIEInstance = 1;
	ActiveClient.bIsRuntimeWorld = true;
	ActiveClient.bIsActivePlayWorld = true;
	ActiveClient.LocalPlayerCount = 1;
	Candidates.Add(ActiveClient);

	TestEqual(TEXT("automatic selection prefers active client"),
		SelectCandidateIndex(TConstArrayView<FWorldCandidate>(Candidates), INDEX_NONE, 0), 1);
	TestEqual(TEXT("explicit dedicated server rejects missing local player"),
		SelectCandidateIndex(TConstArrayView<FWorldCandidate>(Candidates), 0, 0), INDEX_NONE);
	TestEqual(TEXT("explicit client selects exact instance"),
		SelectCandidateIndex(TConstArrayView<FWorldCandidate>(Candidates), 1, 0), 1);

	TArray<FWorldCandidate> InvalidCandidates;
	FWorldCandidate EditorWorld;
	EditorWorld.PIEInstance = 7;
	EditorWorld.bIsRuntimeWorld = false;
	EditorWorld.bIsActivePlayWorld = true;
	EditorWorld.LocalPlayerCount = 2;
	InvalidCandidates.Add(EditorWorld);

	FWorldCandidate RuntimeClient;
	RuntimeClient.PIEInstance = 8;
	RuntimeClient.bIsRuntimeWorld = true;
	RuntimeClient.LocalPlayerCount = 1;
	InvalidCandidates.Add(RuntimeClient);

	FWorldCandidate MultiClient;
	MultiClient.PIEInstance = 9;
	MultiClient.bIsRuntimeWorld = true;
	MultiClient.LocalPlayerCount = 2;
	InvalidCandidates.Add(MultiClient);

	TestEqual(TEXT("non-runtime candidate is skipped"),
		SelectCandidateIndex(TConstArrayView<FWorldCandidate>(InvalidCandidates), INDEX_NONE, 0), 1);
	TestEqual(TEXT("local player one selects candidate with two locals"),
		SelectCandidateIndex(TConstArrayView<FWorldCandidate>(InvalidCandidates), INDEX_NONE, 1), 2);
	TestEqual(TEXT("explicit non-runtime candidate rejects"),
		SelectCandidateIndex(TConstArrayView<FWorldCandidate>(InvalidCandidates), 7, 0), INDEX_NONE);
	TestEqual(TEXT("explicit missing instance rejects"),
		SelectCandidateIndex(TConstArrayView<FWorldCandidate>(InvalidCandidates), 99, 0), INDEX_NONE);

	TArray<FWorldCandidate> EmptyCandidates;
	TestEqual(TEXT("empty candidate list rejects"),
		SelectCandidateIndex(TConstArrayView<FWorldCandidate>(EmptyCandidates), INDEX_NONE, 0), INDEX_NONE);

	TArray<FWorldCandidate> FallbackCandidates;
	FWorldCandidate Ineligible;
	Ineligible.PIEInstance = 10;
	Ineligible.bIsRuntimeWorld = true;
	Ineligible.LocalPlayerCount = 0;
	FallbackCandidates.Add(Ineligible);

	FWorldCandidate FirstEligible;
	FirstEligible.PIEInstance = 11;
	FirstEligible.bIsRuntimeWorld = true;
	FirstEligible.LocalPlayerCount = 1;
	FallbackCandidates.Add(FirstEligible);

	FWorldCandidate LaterEligible;
	LaterEligible.PIEInstance = 12;
	LaterEligible.bIsRuntimeWorld = true;
	LaterEligible.LocalPlayerCount = 1;
	FallbackCandidates.Add(LaterEligible);

	TestEqual(TEXT("automatic selection falls back to first eligible"),
		SelectCandidateIndex(TConstArrayView<FWorldCandidate>(FallbackCandidates), INDEX_NONE, 0), 1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
