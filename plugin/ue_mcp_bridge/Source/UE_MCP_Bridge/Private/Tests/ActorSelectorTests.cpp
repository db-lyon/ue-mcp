// Coverage for the shared actor selector (#983).
//
// The reported harm was a level holding several actors labelled
// BP_SnappyRoad2, a write aimed at the one the user had selected, and the edit
// landing on a road at the other end of the map with a success response. So
// the case that matters here is two actors sharing a label: the call must
// refuse, and it must name both candidates with their actorPath so the retry
// is a copy of one field.
//
// The two actors are transient verification actors (level(spawn_transient_actor)),
// which cannot be saved into the map and are destroyed before the test returns.
// run_automation_tests dispatches every EditorContext test against whatever
// project the bridge is attached to, so nothing here may persist anything.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Level/LevelHandlers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

namespace
{
	const TCHAR* MCPSelectorTestLabel = TEXT("UEMCP_AmbiguousSelectorTest");

	TSharedPtr<FJsonObject> MCPSelectorRun(
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

	/** Spawn one transient StaticMeshActor carrying the shared test label.
	 *  Returns its object path, or an empty string when there is no world to
	 *  spawn into (a context this test must skip rather than fail). */
	FString MCPSelectorSpawn(FMCPHandlerRegistry& Registry)
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorClass"), TEXT("StaticMeshActor"));
		Request->SetStringField(TEXT("label"), MCPSelectorTestLabel);
		Request->SetBoolField(TEXT("hideFromOutliner"), true);
		const TSharedPtr<FJsonObject> Response =
			MCPSelectorRun(Registry, TEXT("spawn_transient_actor"), Request);
		if (!Response.IsValid() || !Response->GetBoolField(TEXT("success")))
		{
			return FString();
		}
		const TSharedPtr<FJsonObject> Actor = Response->GetObjectField(TEXT("actor"));
		return Actor.IsValid() ? Actor->GetStringField(TEXT("actorPath")) : FString();
	}

	void MCPSelectorDestroy(FMCPHandlerRegistry& Registry, const FString& ActorPath)
	{
		if (ActorPath.IsEmpty()) return;
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorPath"), ActorPath);
		MCPSelectorRun(Registry, TEXT("destroy_transient_actor"), Request);
	}

	/** A boolean field that may be absent, without a JSON warning for the miss. */
	bool MCPSelectorFlag(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		bool bValue = false;
		return Object.IsValid() && Object->TryGetBoolField(Field, bValue) && bValue;
	}

	/** Every actorPath in an ambiguity refusal's candidates array. */
	TArray<FString> MCPSelectorCandidatePaths(const TSharedPtr<FJsonObject>& Response)
	{
		TArray<FString> Paths;
		if (!Response.IsValid()) return Paths;
		const TArray<TSharedPtr<FJsonValue>>* Candidates = nullptr;
		if (!Response->TryGetArrayField(TEXT("candidates"), Candidates) || !Candidates) return Paths;
		for (const TSharedPtr<FJsonValue>& Entry : *Candidates)
		{
			if (!Entry.IsValid() || Entry->Type != EJson::Object) continue;
			FString Path;
			if (Entry->AsObject()->TryGetStringField(TEXT("actorPath"), Path)) Paths.Add(Path);
		}
		return Paths;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPActorSelectorAmbiguityTest,
	"UE.MCP.Level.ActorSelector.DuplicateLabelIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPActorSelectorAmbiguityTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		// No editor world in this context, and this test may not open a map.
		return true;
	}

	const FString FirstPath = MCPSelectorSpawn(Registry);
	if (FirstPath.IsEmpty())
	{
		return true;
	}
	const FString SecondPath = MCPSelectorSpawn(Registry);
	if (SecondPath.IsEmpty())
	{
		MCPSelectorDestroy(Registry, FirstPath);
		return true;
	}

	TestNotEqual(TEXT("the two same-labelled actors have different object paths"), FirstPath, SecondPath);

	// The search itself sees both, which is the fact every refusal rests on.
	{
		TArray<AActor*> Matches;
		MCPCollectActorsByToken(World, MCPSelectorTestLabel, EMCPActorMatch::Label, Matches);
		TestEqual(TEXT("the label names both actors"), Matches.Num(), 2);

		// Sorted by object path, so two runs produce the same candidate order
		// and the error is reproducible rather than iterator-dependent.
		if (Matches.Num() == 2)
		{
			TestTrue(TEXT("matches come back in a stable path order"),
				Matches[0]->GetPathName().Compare(Matches[1]->GetPathName()) < 0);
		}
	}

	// The path selector resolves to exactly one, with no label involved.
	{
		AActor* ByPath = MCPFindActorByPath(World, FirstPath);
		TestTrue(TEXT("the path resolves"), ByPath != nullptr);
		if (ByPath) TestEqual(TEXT("and to the actor it names"), ByPath->GetPathName(), FirstPath);
	}

	// The reported case: a label matching two actors is refused, and the
	// refusal names both candidates with their actorPath.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorLabel"), MCPSelectorTestLabel);
		const TSharedPtr<FJsonObject> Response =
			MCPSelectorRun(Registry, TEXT("get_actor_details"), Request);
		TestTrue(TEXT("the ambiguous call answers"), Response.IsValid());
		if (Response.IsValid())
		{
			TestFalse(TEXT("an ambiguous label fails rather than picking one"),
				Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and says it was ambiguous"),
				MCPSelectorFlag(Response, TEXT("ambiguous")));
			TestEqual(TEXT("and reports how many actors matched"),
				static_cast<int32>(Response->GetNumberField(TEXT("matchCount"))), 2);
			TestEqual(TEXT("and names the selector that was ambiguous"),
				Response->GetStringField(TEXT("selector")), FString(TEXT("actorLabel")));
			TestTrue(TEXT("and the message points at actorPath for the retry"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("actorPath")));

			const TArray<FString> Paths = MCPSelectorCandidatePaths(Response);
			TestEqual(TEXT("both candidates are listed"), Paths.Num(), 2);
			TestTrue(TEXT("the first actor is named"), Paths.Contains(FirstPath));
			TestTrue(TEXT("the second actor is named"), Paths.Contains(SecondPath));
		}
	}

	// A write refuses on the same terms. This is the half that mattered: the
	// report was a silent edit, not a silent read.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorLabel"), MCPSelectorTestLabel);
		Request->SetStringField(TEXT("propertyName"), TEXT("bHidden"));
		Request->SetBoolField(TEXT("value"), true);
		const TSharedPtr<FJsonObject> Response =
			MCPSelectorRun(Registry, TEXT("set_actor_property"), Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("an ambiguous write is refused"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and is refused for ambiguity, not for anything else"),
				MCPSelectorFlag(Response, TEXT("ambiguous")));
			TestEqual(TEXT("naming both candidates"), MCPSelectorCandidatePaths(Response).Num(), 2);
		}
	}

	// delete_actor is idempotent on a miss, but "already deleted" would be a
	// lie while two namesakes are still standing.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorLabel"), MCPSelectorTestLabel);
		const TSharedPtr<FJsonObject> Response =
			MCPSelectorRun(Registry, TEXT("delete_actor"), Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("an ambiguous delete is refused"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and does not claim the actor was already deleted"),
				MCPSelectorFlag(Response, TEXT("ambiguous")));
		}
	}

	// The unambiguous selector gets through, and answers about the actor it
	// names rather than its namesake.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorPath"), SecondPath);
		const TSharedPtr<FJsonObject> Response =
			MCPSelectorRun(Registry, TEXT("get_actor_details"), Request);
		TestTrue(TEXT("actorPath alone resolves"),
			Response.IsValid() && Response->GetBoolField(TEXT("success")));
		if (Response.IsValid() && Response->GetBoolField(TEXT("success")))
		{
			TestEqual(TEXT("and answers about that actor"),
				Response->GetStringField(TEXT("path")), SecondPath);
			TestEqual(TEXT("and returns actorPath so the round trip closes"),
				Response->GetStringField(TEXT("actorPath")), SecondPath);
		}
	}

	// Both given: the path wins, because it is the one that cannot be wrong.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorLabel"), MCPSelectorTestLabel);
		Request->SetStringField(TEXT("actorPath"), FirstPath);
		const TSharedPtr<FJsonObject> Response =
			MCPSelectorRun(Registry, TEXT("get_actor_details"), Request);
		TestTrue(TEXT("a path alongside an ambiguous label still resolves"),
			Response.IsValid() && Response->GetBoolField(TEXT("success")));
		if (Response.IsValid() && Response->GetBoolField(TEXT("success")))
		{
			TestEqual(TEXT("and the path decides"), Response->GetStringField(TEXT("actorPath")), FirstPath);
		}
	}

	// A path that names nothing is an error about the path. Falling through to
	// the label would demote a precise miss to a fuzzy hit, which is the same
	// class of bug this issue is about.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorLabel"), MCPSelectorTestLabel);
		Request->SetStringField(TEXT("actorPath"), TEXT("/Game/NoSuchMap.NoSuchMap:PersistentLevel.NoSuchActor_0"));
		const TSharedPtr<FJsonObject> Response =
			MCPSelectorRun(Registry, TEXT("get_actor_details"), Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("an unresolvable actorPath fails"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and the message names the path, not the label"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("NoSuchActor_0")));
		}
	}

	MCPSelectorDestroy(Registry, FirstPath);
	MCPSelectorDestroy(Registry, SecondPath);

	// The cleanup really happened: a later test must not inherit two actors
	// sharing a label.
	{
		TArray<AActor*> Remaining;
		MCPCollectActorsByToken(World, MCPSelectorTestLabel, EMCPActorMatch::Label, Remaining);
		TestEqual(TEXT("both verification actors were destroyed"), Remaining.Num(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPActorSelectorContractTest,
	"UE.MCP.Level.ActorSelector.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPActorSelectorContractTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);

	// Neither selector is a parameter error that names both, so a caller who
	// only knows actorLabel learns that actorPath exists.
	{
		const TSharedPtr<FJsonValue> Response =
			Registry.ExecuteHandler(TEXT("get_actor_details"), MakeShared<FJsonObject>());
		if (Response.IsValid() && Response->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Object = Response->AsObject();
			TestFalse(TEXT("no selector fails"), Object->GetBoolField(TEXT("success")));
			const FString Error = Object->GetStringField(TEXT("error"));
			TestTrue(TEXT("and names actorLabel"), Error.Contains(TEXT("actorLabel")));
			TestTrue(TEXT("and names actorPath"), Error.Contains(TEXT("actorPath")));
		}
	}

	// The refusal is a shape a caller can branch on without parsing prose.
	{
		UWorld* World = GetEditorWorld();
		TArray<AActor*> Candidates;
		if (World)
		{
			for (TActorIterator<AActor> It(World); It && Candidates.Num() < 2; ++It)
			{
				if (IsValid(*It)) Candidates.Add(*It);
			}
		}
		if (Candidates.Num() == 2)
		{
			const TSharedPtr<FJsonValue> Error = MCPAmbiguousActorError(
				TEXT("SharedLabel"), TEXT("actorLabel"), TEXT("actorPath"), TEXT("editor label"), Candidates);
			TestTrue(TEXT("the refusal is an object"), Error.IsValid() && Error->Type == EJson::Object);
			if (Error.IsValid() && Error->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject> Object = Error->AsObject();
				TestFalse(TEXT("it is a failure"), Object->GetBoolField(TEXT("success")));
				TestTrue(TEXT("flagged ambiguous"), MCPSelectorFlag(Object, TEXT("ambiguous")));
				TestTrue(TEXT("and MCPIsAmbiguousActorError agrees"), MCPIsAmbiguousActorError(Error));
				TestEqual(TEXT("echoing the value that was ambiguous"),
					Object->GetStringField(TEXT("selectorValue")), FString(TEXT("SharedLabel")));
				TestEqual(TEXT("with a candidate row per actor"),
					MCPSelectorCandidatePaths(Object).Num(), 2);

				// Each row carries enough to tell two namesakes apart without a
				// follow-up call: class, folder and location, not just a path.
				const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
				if (Object->TryGetArrayField(TEXT("candidates"), Rows) && Rows && Rows->Num() > 0)
				{
					const TSharedPtr<FJsonObject> Row = (*Rows)[0]->AsObject();
					TestTrue(TEXT("a candidate row carries actorLabel"), Row->HasField(TEXT("actorLabel")));
					TestTrue(TEXT("a candidate row carries actorClass"), Row->HasField(TEXT("actorClass")));
					TestTrue(TEXT("a candidate row carries location"), Row->HasField(TEXT("location")));
				}
			}
		}
	}

	// A plain error is not mistaken for an ambiguity refusal.
	TestFalse(TEXT("a plain error is not ambiguous"), MCPIsAmbiguousActorError(MCPError(TEXT("nope"))));
	TestFalse(TEXT("an unset error is not ambiguous"), MCPIsAmbiguousActorError(TSharedPtr<FJsonValue>()));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
