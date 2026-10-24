#include "BatchHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "MCPEngineStatus.h"
#include "UE_MCP_BridgeModule.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "ScopedTransaction.h"

namespace
{
	// One batch is one game-thread job, so this bounds how long a single call
	// holds the editor. A longer edit is two batches.
	constexpr int32 MaxBatchCalls = 256;

	// Methods a batch cannot carry. The undo-stack ones would split, abort or
	// unwind the transaction the batch itself is recording into, and a nested
	// batch is the same job twice.
	bool IsRefusedInBatch(const FString& Method)
	{
		static const TSet<FString> Refused = {
			TEXT("execute_batch"),
			TEXT("undo"),
			TEXT("redo"),
			TEXT("undo_redo_steps"),
			TEXT("begin_editor_transaction"),
			TEXT("end_editor_transaction"),
			TEXT("cancel_editor_transaction"),
		};
		return Refused.Contains(Method);
	}

	struct FBatchStep
	{
		FString Method;
		TSharedPtr<FJsonObject> Params;
	};

	// Clears the progress the status snapshot reports, however the batch ends.
	struct FBatchProgressScope
	{
		~FBatchProgressScope()
		{
			FMCPEngineStatus::Get().ClearBatchProgress();
		}
	};

	// A step's verdict, read the way the client reads one: only an explicit
	// success:false fails, and anything else that answered is an answer.
	bool StepSucceeded(const TSharedPtr<FJsonValue>& Result, FString& OutError)
	{
		const TSharedPtr<FJsonObject>* Body = nullptr;
		if (!Result.IsValid() || !Result->TryGetObject(Body) || !Body || !Body->IsValid())
		{
			return true;
		}
		bool bSuccess = true;
		if (!(*Body)->TryGetBoolField(TEXT("success"), bSuccess) || bSuccess)
		{
			return true;
		}
		if (!(*Body)->TryGetStringField(TEXT("error"), OutError) || OutError.IsEmpty())
		{
			OutError = TEXT("the handler answered success: false without saying why");
		}
		return false;
	}
}

void FBatchHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	// Reports parameters its handler never read (#1057).
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("editor"));
	Registry.RegisterHandler(TEXT("execute_batch"), &FBatchHandlers::ExecuteBatch);
}

TSharedPtr<FJsonValue> FBatchHandlers::ExecuteBatch(const TSharedPtr<FJsonObject>& Params)
{
	FMCPHandlerRegistry* Dispatching = FMCPHandlerRegistry::Executing();
	if (!Dispatching)
	{
		return MCPError(TEXT("execute_batch runs only when dispatched through a handler registry"));
	}
	FMCPHandlerRegistry& Registry = *Dispatching;

	const TArray<TSharedPtr<FJsonValue>>* Calls = nullptr;
	if (!TryGetArrayParam(Params, TEXT("ops"), Calls) || !Calls || Calls->IsEmpty())
	{
		return MCPError(TEXT("Missing required non-empty array parameter 'ops'"));
	}
	if (Calls->Num() > MaxBatchCalls)
	{
		return MCPError(FString::Printf(TEXT("'ops' holds %d entries and a batch carries at most %d"), Calls->Num(), MaxBatchCalls));
	}

	const FString OnError = OptionalString(Params, TEXT("onError"), TEXT("stop"));
	if (OnError != TEXT("stop") && OnError != TEXT("continue"))
	{
		return MCPError(FString::Printf(TEXT("onError must be 'stop' or 'continue', not '%s'"), *OnError));
	}
	const bool bStopOnError = OnError == TEXT("stop");

	FString Description = OptionalString(Params, TEXT("transactionDescription"));
	if (Description.IsEmpty())
	{
		Description = FString::Printf(TEXT("MCP batch (%d calls)"), Calls->Num());
	}

	// Every entry is checked before any runs: a batch that cannot run whole
	// does not run at all, which is the guarantee a caller assembling one
	// edit out of several steps is asking for.
	TArray<FBatchStep> Steps;
	Steps.Reserve(Calls->Num());
	for (int32 Index = 0; Index < Calls->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* Call = nullptr;
		if (!(*Calls)[Index].IsValid() || !(*Calls)[Index]->TryGetObject(Call) || !Call || !Call->IsValid())
		{
			return MCPError(FString::Printf(TEXT("ops[%d] must be an object {method, params}"), Index));
		}
		FBatchStep Step;
		if (!(*Call)->TryGetStringField(TEXT("method"), Step.Method) || Step.Method.IsEmpty())
		{
			return MCPError(FString::Printf(TEXT("ops[%d] requires a non-empty 'method'"), Index));
		}
		if (IsRefusedInBatch(Step.Method))
		{
			return MCPError(FString::Printf(
				TEXT("ops[%d]: '%s' cannot run inside a batch. A batch is one undo transaction and one job; call it on its own"),
				Index, *Step.Method));
		}
		if (!Registry.HasHandler(Step.Method))
		{
			return MCPError(FString::Printf(TEXT("ops[%d]: Unknown method: %s"), Index, *Step.Method));
		}
		const TSharedPtr<FJsonObject>* StepParams = nullptr;
		if ((*Call)->HasField(TEXT("params")))
		{
			if (!(*Call)->TryGetObjectField(TEXT("params"), StepParams) || !StepParams || !StepParams->IsValid())
			{
				return MCPError(FString::Printf(TEXT("ops[%d].params must be an object"), Index));
			}
			Step.Params = *StepParams;
		}
		else
		{
			// A handler is never handed an unset pointer: ProcessMessage
			// substitutes a fresh object on the socket path, and so does this.
			Step.Params = MakeShared<FJsonObject>();
		}
		Steps.Add(MoveTemp(Step));
	}

	UTransactor* Transactor = GEditor ? GEditor->Trans : nullptr;
	const bool bNestedInOpenTransaction = Transactor && Transactor->IsActive();
	const int32 UndoQueueBefore = Transactor ? Transactor->GetQueueLength() : 0;

	TArray<TSharedPtr<FJsonValue>> Results;
	Results.Reserve(Steps.Num());
	int32 CompletedCalls = 0;
	int32 FailedCount = 0;
	int32 FailedIndex = INDEX_NONE;
	FString FirstError;
	bool bStoppedEarly = false;

	{
		FBatchProgressScope ProgressScope;
		// One transaction around every step, so the caller gets one Undo entry
		// for the batch rather than one per step (or none). The scoped
		// transaction declines to open where the editor cannot transact (no
		// transactor, or inside play), and then simply records nothing.
		TUniquePtr<FScopedTransaction> Transaction;
		if (Transactor)
		{
			Transaction = MakeUnique<FScopedTransaction>(FText::FromString(Description));
		}

		for (int32 Index = 0; Index < Steps.Num(); ++Index)
		{
			const FBatchStep& Step = Steps[Index];
			FMCPEngineStatus::Get().NoteBatchProgress(Index, Steps.Num(), Step.Method);
			UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] execute_batch %d/%d: %s"), Index + 1, Steps.Num(), *Step.Method);

			TSharedPtr<FJsonValue> StepResult = Registry.ExecuteHandler(Step.Method, Step.Params);
			if (!StepResult.IsValid())
			{
				StepResult = MCPError(FString::Printf(TEXT("Internal error: handler '%s' returned no result"), *Step.Method));
			}
			++CompletedCalls;

			FString StepError;
			const bool bSucceeded = StepSucceeded(StepResult, StepError);

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("index"), Index);
			Entry->SetStringField(TEXT("method"), Step.Method);
			Entry->SetBoolField(TEXT("success"), bSucceeded);
			Entry->SetField(TEXT("result"), StepResult);
			Results.Add(MakeShared<FJsonValueObject>(Entry));

			if (bSucceeded)
			{
				continue;
			}
			++FailedCount;
			if (FailedIndex == INDEX_NONE)
			{
				FailedIndex = Index;
				FirstError = StepError;
			}
			if (bStopOnError)
			{
				bStoppedEarly = Index + 1 < Steps.Num();
				break;
			}
		}
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetBoolField(TEXT("success"), FailedCount == 0);
	Result->SetStringField(TEXT("onError"), OnError);
	Result->SetNumberField(TEXT("requestedCalls"), Steps.Num());
	Result->SetNumberField(TEXT("completedCalls"), CompletedCalls);
	Result->SetNumberField(TEXT("failedCount"), FailedCount);
	Result->SetBoolField(TEXT("stoppedEarly"), bStoppedEarly);
	if (FailedIndex != INDEX_NONE)
	{
		Result->SetNumberField(TEXT("failedIndex"), FailedIndex);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("ops[%d] (%s) failed: %s. Nothing is rolled back: the steps that succeeded stay applied"),
			FailedIndex, *Steps[FailedIndex].Method, *FirstError));
	}
	Result->SetArrayField(TEXT("results"), Results);

	TSharedPtr<FJsonObject> TransactionInfo = MakeShared<FJsonObject>();
	TransactionInfo->SetStringField(TEXT("description"), Description);
	// A new queue entry means the batch is one Undo step. Nested inside a
	// transaction a caller opened with begin_transaction, the batch's changes
	// belong to that outer entry instead, and only its end records anything.
	TransactionInfo->SetBoolField(TEXT("undoEntry"), Transactor && Transactor->GetQueueLength() > UndoQueueBefore);
	TransactionInfo->SetBoolField(TEXT("nested"), bNestedInOpenTransaction);
	Result->SetObjectField(TEXT("transaction"), TransactionInfo);

	// The whole job is what a caller undoes, and only through the editor's own
	// Undo: no single bridge call reverses an arbitrary sequence.
	MCPSetNoRollback(Result,
		TEXT("A batch has no inverse of its own. Its transactional changes are one Undo entry (editor(undo)), and each step that reports a rollback descriptor carries it under results[i].result.rollback."));
	return MCPResult(Result);
}
