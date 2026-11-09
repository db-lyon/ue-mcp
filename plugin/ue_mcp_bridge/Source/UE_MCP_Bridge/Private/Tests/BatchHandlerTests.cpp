// execute_batch: several handlers in one job, in order, with the stop and
// continue policies, and refused whole when one step cannot run. The probes
// below are registered on a registry of the test's own, so nothing here reaches
// a real handler or the bridge server's registry.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Batch/BatchHandlers.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Curves/CurveFloat.h"
#include "Misc/AutomationTest.h"
#include "UObject/GCObjectScopeGuard.h"
#include "UObject/Package.h"
#include "Misc/Guid.h"
#include "Tests/MCPScopedTestMount.h"

namespace MCPBatchHandlerTests
{
	// Two probes and the batch itself. Each probe records the tag it was called
	// with, so the order the batch ran them in can be read back, and the failing
	// one answers the way a real handler refuses: success false with an error.
	struct FProbeRegistry
	{
		FMCPHandlerRegistry Registry;
		TArray<FString> Order;

		FProbeRegistry()
		{
			Registry.RegisterHandler(TEXT("batch_probe_ok"), [this](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonValue>
			{
				const FString Tag = OptionalString(Params, TEXT("tag"));
				Order.Add(Tag);
				TSharedPtr<FJsonObject> Result = MCPSuccess();
				Result->SetStringField(TEXT("tag"), Tag);
				return MCPResult(Result);
			});
			Registry.RegisterHandler(TEXT("batch_probe_fail"), [this](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonValue>
			{
				const FString Tag = OptionalString(Params, TEXT("tag"));
				Order.Add(Tag);
				return MCPError(TEXT("probe refused ") + Tag);
			});
			FBatchHandlers::RegisterHandlers(Registry);
		}

		/** One step: {method, params: {tag}}. */
		static TSharedPtr<FJsonValue> Step(const TCHAR* Method, const TCHAR* Tag)
		{
			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("tag"), Tag);
			TSharedPtr<FJsonObject> Call = MakeShared<FJsonObject>();
			Call->SetStringField(TEXT("method"), Method);
			Call->SetObjectField(TEXT("params"), Params);
			return MakeShared<FJsonValueObject>(Call);
		}

		/** Dispatch a batch of Steps through the registry, by its registered name, and hand back the body. */
		TSharedPtr<FJsonObject> Run(const TArray<TSharedPtr<FJsonValue>>& Steps, const TCHAR* OnError = nullptr)
		{
			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetArrayField(TEXT("ops"), Steps);
			if (OnError)
			{
				Params->SetStringField(TEXT("onError"), OnError);
			}
			const TSharedPtr<FJsonValue> Answer = Registry.ExecuteHandler(TEXT("execute_batch"), Params);
			return Answer.IsValid() && Answer->Type == EJson::Object ? Answer->AsObject() : nullptr;
		}
	};

	/** results[i].success as the body reports it. */
	static bool StepSuccess(const TSharedPtr<FJsonObject>& Body, int32 Index)
	{
		const TArray<TSharedPtr<FJsonValue>>* Results = nullptr;
		if (!Body.IsValid() || !Body->TryGetArrayField(TEXT("results"), Results) || !Results || !Results->IsValidIndex(Index))
		{
			return false;
		}
		const TSharedPtr<FJsonObject> Entry = (*Results)[Index]->AsObject();
		bool bSuccess = false;
		return Entry.IsValid() && Entry->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
	}

	static int32 ResultCount(const TSharedPtr<FJsonObject>& Body)
	{
		const TArray<TSharedPtr<FJsonValue>>* Results = nullptr;
		return Body.IsValid() && Body->TryGetArrayField(TEXT("results"), Results) && Results ? Results->Num() : -1;
	}

	static double Number(const TSharedPtr<FJsonObject>& Body, const TCHAR* Key)
	{
		double Value = -1.0;
		if (Body.IsValid()) Body->TryGetNumberField(Key, Value);
		return Value;
	}

	static bool Success(const TSharedPtr<FJsonObject>& Body)
	{
		bool bSuccess = false;
		return Body.IsValid() && Body->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
	}

	static FString ErrorText(const TSharedPtr<FJsonObject>& Body)
	{
		FString Message;
		if (Body.IsValid()) Body->TryGetStringField(TEXT("error"), Message);
		return Message;
	}
}

using namespace MCPBatchHandlerTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBatchRunsInOrderTest,
	"UE.MCP.Bridge.Batch.RunsInOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBatchRunsInOrderTest::RunTest(const FString& Parameters)
{
	FProbeRegistry Probe;
	const TSharedPtr<FJsonObject> Body = Probe.Run({
		FProbeRegistry::Step(TEXT("batch_probe_ok"), TEXT("first")),
		FProbeRegistry::Step(TEXT("batch_probe_ok"), TEXT("second")),
		FProbeRegistry::Step(TEXT("batch_probe_ok"), TEXT("third")),
	});
	TestTrue(TEXT("a batch of succeeding steps succeeds"), Success(Body));
	TestEqual(TEXT("every step ran, in the order it was listed"), Probe.Order, TArray<FString>{ TEXT("first"), TEXT("second"), TEXT("third") });
	TestEqual(TEXT("completedCalls counts them"), Number(Body, TEXT("completedCalls")), 3.0);
	TestEqual(TEXT("requestedCalls echoes the list"), Number(Body, TEXT("requestedCalls")), 3.0);
	TestEqual(TEXT("one result per step"), ResultCount(Body), 3);
	TestTrue(TEXT("each result carries the step's own answer"), StepSuccess(Body, 0) && StepSuccess(Body, 1) && StepSuccess(Body, 2));
	TestFalse(TEXT("no failedIndex when nothing failed"), Body.IsValid() && Body->HasField(TEXT("failedIndex")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBatchStopsOnErrorTest,
	"UE.MCP.Bridge.Batch.StopsOnError",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBatchStopsOnErrorTest::RunTest(const FString& Parameters)
{
	FProbeRegistry Probe;
	const TSharedPtr<FJsonObject> Body = Probe.Run({
		FProbeRegistry::Step(TEXT("batch_probe_ok"), TEXT("first")),
		FProbeRegistry::Step(TEXT("batch_probe_fail"), TEXT("second")),
		FProbeRegistry::Step(TEXT("batch_probe_ok"), TEXT("third")),
	});
	TestFalse(TEXT("a batch with a failing step fails"), Success(Body));
	TestEqual(TEXT("the default policy stops at the failure, so the third step never ran"), Probe.Order, TArray<FString>{ TEXT("first"), TEXT("second") });
	TestEqual(TEXT("failedIndex names the step"), Number(Body, TEXT("failedIndex")), 1.0);
	TestEqual(TEXT("completedCalls counts the steps that answered, the failed one included"), Number(Body, TEXT("completedCalls")), 2.0);
	TestEqual(TEXT("results stop where the batch did"), ResultCount(Body), 2);
	TestTrue(TEXT("the first step's result stands: nothing is rolled back"), StepSuccess(Body, 0));
	TestFalse(TEXT("the failed step's result is the failure"), StepSuccess(Body, 1));
	bool bStoppedEarly = false;
	TestTrue(TEXT("stoppedEarly says steps were left unrun"), Body.IsValid() && Body->TryGetBoolField(TEXT("stoppedEarly"), bStoppedEarly) && bStoppedEarly);
	TestTrue(TEXT("the error names the step and its own message"), ErrorText(Body).Contains(TEXT("ops[1] (batch_probe_fail) failed: probe refused second")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBatchContinuesOnErrorTest,
	"UE.MCP.Bridge.Batch.ContinuesOnError",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBatchContinuesOnErrorTest::RunTest(const FString& Parameters)
{
	FProbeRegistry Probe;
	const TSharedPtr<FJsonObject> Body = Probe.Run({
		FProbeRegistry::Step(TEXT("batch_probe_fail"), TEXT("first")),
		FProbeRegistry::Step(TEXT("batch_probe_ok"), TEXT("second")),
		FProbeRegistry::Step(TEXT("batch_probe_fail"), TEXT("third")),
	}, TEXT("continue"));
	TestFalse(TEXT("a batch with failing steps fails even when it ran them all"), Success(Body));
	TestEqual(TEXT("continue runs every step"), Probe.Order, TArray<FString>{ TEXT("first"), TEXT("second"), TEXT("third") });
	TestEqual(TEXT("failedIndex is the FIRST failure"), Number(Body, TEXT("failedIndex")), 0.0);
	TestEqual(TEXT("failedCount counts them all"), Number(Body, TEXT("failedCount")), 2.0);
	TestEqual(TEXT("completedCalls counts every step"), Number(Body, TEXT("completedCalls")), 3.0);
	TestEqual(TEXT("one result per step"), ResultCount(Body), 3);
	TestTrue(TEXT("each step keeps its own verdict"), !StepSuccess(Body, 0) && StepSuccess(Body, 1) && !StepSuccess(Body, 2));
	bool bStoppedEarly = true;
	TestTrue(TEXT("nothing was left unrun"), Body.IsValid() && Body->TryGetBoolField(TEXT("stoppedEarly"), bStoppedEarly) && !bStoppedEarly);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBatchRefusalsTest,
	"UE.MCP.Bridge.Batch.Refusals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBatchRefusalsTest::RunTest(const FString& Parameters)
{
	// Every refusal happens before any step runs: the probe order stays empty.
	FProbeRegistry Probe;

	TSharedPtr<FJsonObject> Body = Probe.Run({});
	TestFalse(TEXT("an empty list is refused"), Success(Body));
	TestTrue(TEXT("and says which parameter"), ErrorText(Body).Contains(TEXT("'ops'")));

	Body = Probe.Run({
		FProbeRegistry::Step(TEXT("batch_probe_ok"), TEXT("first")),
		FProbeRegistry::Step(TEXT("no_such_handler"), TEXT("second")),
	});
	TestFalse(TEXT("a step naming an unregistered method refuses the batch"), Success(Body));
	TestTrue(TEXT("naming the step and the method"), ErrorText(Body).Contains(TEXT("ops[1]: Unknown method: no_such_handler")));

	Body = Probe.Run({
		FProbeRegistry::Step(TEXT("batch_probe_ok"), TEXT("first")),
		FProbeRegistry::Step(TEXT("execute_batch"), TEXT("second")),
	});
	TestFalse(TEXT("a nested batch is refused"), Success(Body));
	TestTrue(TEXT("with the reason"), ErrorText(Body).Contains(TEXT("cannot run inside a batch")));

	Body = Probe.Run({ FProbeRegistry::Step(TEXT("undo"), TEXT("first")) });
	TestFalse(TEXT("an undo-stack action is refused"), Success(Body));

	Body = Probe.Run({ FProbeRegistry::Step(TEXT("batch_probe_ok"), TEXT("first")) }, TEXT("retry"));
	TestFalse(TEXT("an onError the batch does not know is refused"), Success(Body));
	TestTrue(TEXT("naming the two it knows"), ErrorText(Body).Contains(TEXT("'stop' or 'continue'")));

	TestEqual(TEXT("no refused batch ran a single step"), Probe.Order.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBatchOneUndoEntryTest,
	"UE.MCP.Bridge.Batch.OneUndoEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBatchOneUndoEntryTest::RunTest(const FString& Parameters)
{
	if (!GEditor || !GEditor->Trans || GEditor->Trans->IsActive() || !GEditor->CanTransact())
	{
		AddInfo(TEXT("No transactor to record into here; the undo grouping is checked where there is one."));
		return true;
	}

	// A transactional asset in a package of its own, under a private mount. A
	// Modify() inside a step then records into whatever transaction is open at
	// the time. The transient package would not do: a transaction that holds
	// only transient objects is dropped from the undo buffer as ineffective. A
	// float curve is the smallest concrete asset class that transacts, and each
	// step adds a key to it, because a transaction whose records hold no actual
	// change is dropped from the undo buffer as ineffective as well.
	// Two steps each change it; one Undo entry must come out, not two.
	FMCPScopedTestMount Mount(TEXT("/UEMCPBatchUndo_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"), TEXT("UEMCPBatchUndo"));
	UPackage* Package = CreatePackage(*(Mount.RootPath + TEXT("BatchProbe")));
	UCurveFloat* Subject = NewObject<UCurveFloat>(Package, TEXT("BatchProbe"), RF_Transactional | RF_Standalone);
	FGCObjectScopeGuard SubjectGuard(Subject);

	FProbeRegistry Probe;
	Probe.Registry.RegisterHandler(TEXT("batch_probe_modify"), [Subject](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonValue>
	{
		Subject->Modify();
		Subject->FloatCurve.AddKey(static_cast<float>(Subject->FloatCurve.GetNumKeys()), 1.0f);
		return MCPResult(MCPSuccess());
	});

	const int32 QueueBefore = GEditor->Trans->GetQueueLength();
	const TSharedPtr<FJsonObject> Body = Probe.Run({
		FProbeRegistry::Step(TEXT("batch_probe_modify"), TEXT("first")),
		FProbeRegistry::Step(TEXT("batch_probe_modify"), TEXT("second")),
	});
	TestTrue(TEXT("the batch succeeds"), Success(Body));
	TestEqual(TEXT("two transactional steps make exactly one undo entry"), GEditor->Trans->GetQueueLength() - QueueBefore, 1);

	const TSharedPtr<FJsonObject>* Transaction = nullptr;
	bool bUndoEntry = false;
	TestTrue(TEXT("and the result says so"),
		Body.IsValid() && Body->TryGetObjectField(TEXT("transaction"), Transaction) && Transaction
		&& (*Transaction)->TryGetBoolField(TEXT("undoEntry"), bUndoEntry) && bUndoEntry);

	// Take the entry back off the stack and leave the probe package clean, so
	// the editor this ran in is left as it was found; the mount evicts the
	// package on the way out.
	GEditor->UndoTransaction();
	Package->SetDirtyFlag(false);
	return true;
}

#endif
