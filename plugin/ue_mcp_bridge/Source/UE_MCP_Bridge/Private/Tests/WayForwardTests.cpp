// A refusal names the way forward (#1952). The repeated refusals share their
// last sentence through HandlerWayForward.h; these tests dispatch real handlers
// on a registry of the test's own and check the answer carries the call that
// gets the caller unstuck, and that a bound at its ceiling is never answered
// with "raise it".
//
// Nothing here mutates anything: every case is refused before any work runs.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Asset/AssetHandlers.h"
#include "Handlers/Batch/BatchHandlers.h"
#include "Handlers/Level/LevelHandlers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

namespace MCPWayForwardTests
{
	static FString RefusalText(const TSharedPtr<FJsonValue>& Answer)
	{
		if (!Answer.IsValid() || Answer->Type != EJson::Object) return FString();
		FString Error;
		Answer->AsObject()->TryGetStringField(TEXT("error"), Error);
		return Error;
	}

	static bool AnsweredSuccess(const TSharedPtr<FJsonValue>& Answer)
	{
		bool bSuccess = false;
		return Answer.IsValid() && Answer->Type == EJson::Object
			&& Answer->AsObject()->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPWayForwardProtectedMountTest,
	"UE.MCP.WayForward.ProtectedMountNamesTheCopyRoute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPWayForwardProtectedMountTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("assetPath"), TEXT("/Engine/EngineMaterials/DefaultMaterial"));
	const TSharedPtr<FJsonValue> Answer = Registry.ExecuteHandler(TEXT("delete_asset"), Params);
	const FString Error = MCPWayForwardTests::RefusalText(Answer);

	TestFalse(TEXT("deleting engine content is refused"), MCPWayForwardTests::AnsweredSuccess(Answer));
	TestTrue(TEXT("the refusal says it cannot be overridden"), Error.Contains(TEXT("cannot be overridden")));
	TestTrue(TEXT("the refusal names asset(duplicate)"), Error.Contains(TEXT("asset(duplicate")));
	TestTrue(TEXT("the refusal points at /Game"), Error.Contains(TEXT("/Game")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPWayForwardWorldScopeTest,
	"UE.MCP.WayForward.MissingPlayWorldNamesHowToGetOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPWayForwardWorldScopeTest::RunTest(const FString& Parameters)
{
	if (MCPIsPlayInEditorActive())
	{
		AddInfo(TEXT("A PIE session is running, so world=pie resolves; nothing to refuse."));
		return true;
	}
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("world"), TEXT("pie"));
	TArray<TSharedPtr<FJsonValue>> Start = { MakeShared<FJsonValueNumber>(0.0), MakeShared<FJsonValueNumber>(0.0), MakeShared<FJsonValueNumber>(100.0) };
	TArray<TSharedPtr<FJsonValue>> End = { MakeShared<FJsonValueNumber>(0.0), MakeShared<FJsonValueNumber>(0.0), MakeShared<FJsonValueNumber>(-100.0) };
	Params->SetArrayField(TEXT("start"), Start);
	Params->SetArrayField(TEXT("end"), End);
	const TSharedPtr<FJsonValue> Answer = Registry.ExecuteHandler(TEXT("line_trace"), Params);
	const FString Error = MCPWayForwardTests::RefusalText(Answer);

	TestFalse(TEXT("a trace in a PIE world that does not exist is refused"), MCPWayForwardTests::AnsweredSuccess(Answer));
	TestTrue(TEXT("the refusal names the scope"), Error.Contains(TEXT("'pie'")));
	TestTrue(TEXT("the refusal names how to start PIE"), Error.Contains(TEXT("editor(play_in_editor, pieAction=\"start\")")));
	TestTrue(TEXT("the refusal offers world='editor'"), Error.Contains(TEXT("world='editor'")));
	TestTrue(TEXT("the refusal points at list_pie_instances"), Error.Contains(TEXT("editor(list_pie_instances)")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPWayForwardBatchSizeTest,
	"UE.MCP.WayForward.BatchCapSaysHowToSplit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPWayForwardBatchSizeTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FBatchHandlers::RegisterHandlers(Registry);

	TArray<TSharedPtr<FJsonValue>> Ops;
	for (int32 Index = 0; Index < 257; ++Index)
	{
		TSharedPtr<FJsonObject> Call = MakeShared<FJsonObject>();
		Call->SetStringField(TEXT("method"), TEXT("execute_batch_probe_never_runs"));
		Call->SetObjectField(TEXT("params"), MakeShared<FJsonObject>());
		Ops.Add(MakeShared<FJsonValueObject>(Call));
	}
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetArrayField(TEXT("ops"), Ops);
	const TSharedPtr<FJsonValue> Answer = Registry.ExecuteHandler(TEXT("execute_batch"), Params);
	const FString Error = MCPWayForwardTests::RefusalText(Answer);

	TestFalse(TEXT("a batch over the cap is refused"), MCPWayForwardTests::AnsweredSuccess(Answer));
	TestTrue(TEXT("the refusal says how to split it"), Error.Contains(TEXT("several execute_batch calls of 256 or fewer")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPWayForwardCeilingTest,
	"UE.MCP.WayForward.BoundAtItsCeilingIsNotRaised",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPWayForwardCeilingTest::RunTest(const FString& Parameters)
{
	const FString Split = TEXT("Split the work by bounds.");

	const FString Below = MCPRaiseOrSplitNote(TEXT("maxActors"), 256, 1024, Split);
	TestTrue(TEXT("below the ceiling, raising is offered"), Below.StartsWith(TEXT("Raise maxActors (up to 1024")));
	TestTrue(TEXT("below the ceiling, the split is offered too"), Below.Contains(TEXT("split the work by bounds.")));

	const FString AtCeiling = MCPRaiseOrSplitNote(TEXT("maxActors"), 1024, 1024, Split);
	TestFalse(TEXT("at the ceiling, raising is not offered"), AtCeiling.StartsWith(TEXT("Raise")));
	TestTrue(TEXT("at the ceiling, the ceiling is named"), AtCeiling.Contains(TEXT("capped at 1024")));
	TestTrue(TEXT("at the ceiling, the split is the way forward"), AtCeiling.EndsWith(Split));

	TestEqual(TEXT("a batch refusal gains the split sentence"),
		MCPWithSplitNote(TEXT("'items' exceeds the maximum of 32"), 32),
		FString(TEXT("'items' exceeds the maximum of 32. Split the work into several calls of 32 or fewer.")));
	return true;
}

#endif
