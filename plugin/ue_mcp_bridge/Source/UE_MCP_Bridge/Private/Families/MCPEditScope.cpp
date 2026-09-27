#include "Families/MCPEditScope.h"

FMCPEditScope::FMCPEditScope(const TSharedPtr<FJsonObject>& InParams, const TCHAR* InAction, const TCHAR* InTargetParam, const FMCPCommitPolicy& InPolicy)
	: Params(InParams)
	, Action(InAction)
	, TargetParam(InTargetParam)
	, Policy(InPolicy)
{
}

FMCPEditScope::~FMCPEditScope()
{
	CloseTransactions();
}

TSharedPtr<FJsonValue> FMCPEditScope::OpenTargets(FOps&& InOps)
{
	Ops = MoveTemp(InOps);
	Targets.Reset();

	// One path, or an array of them when the action's spec takes several.
	TArray<FString> Paths;
	const TSharedPtr<FJsonValue> Raw = TryGetParam(Params, *TargetParam);
	if (Raw.IsValid() && Raw->Type == EJson::Array)
	{
		for (const TSharedPtr<FJsonValue>& Item : Raw->AsArray())
		{
			FString Path;
			if (!Item.IsValid() || !Item->TryGetString(Path) || Path.IsEmpty())
			{
				return MCPError(FString::Printf(TEXT("Every entry of '%s' must be an asset path."), *TargetParam));
			}
			Paths.AddUnique(Path);
		}
		if (Paths.Num() == 0)
		{
			return MCPError(FString::Printf(TEXT("Missing required parameter '%s'"), *TargetParam));
		}
	}
	else
	{
		FString Path;
		if (auto Err = RequireString(Params, *TargetParam, Path)) return Err;
		Paths.Add(Path);
	}

	for (const FString& Path : Paths)
	{
		UObject* Asset = Ops.Load ? Ops.Load(Path) : nullptr;
		if (!Asset) return MCPAssetLoadError(Path, Ops.TypeName);
		if (Ops.Check)
		{
			if (auto Err = Ops.Check(Asset, Path)) return Err;
		}
		// #932: an action that saves refuses an unwritable package before it changes anything.
		if (Policy.bSave)
		{
			if (auto Blocked = MCPAssetWriteBlockedError(Asset, Path, *FString::Printf(TEXT("%s '%s'"), *Action, *Path)))
			{
				return Blocked;
			}
		}
		FTarget& Entry = Targets.AddDefaulted_GetRef();
		Entry.Path = Path;
		Entry.Asset = Asset;
	}
	return nullptr;
}

void FMCPEditScope::CloseTransactions()
{
	const TArray<FMCPEditTransaction*> Open = OpenTransactions;
	for (FMCPEditTransaction* Transaction : Open)
	{
		Transaction->Close();
	}
	OpenTransactions.Reset();
}

TSharedPtr<FJsonValue> FMCPEditScope::Finish(const TSharedPtr<FJsonObject>& Result)
{
	bFinished = true;

	// Settle inside the still-open transactions, so undo takes the fixups too.
	if (Policy.bSettle && Ops.Settle)
	{
		for (const FTarget& Entry : Targets)
		{
			if (Entry.bChanged) Ops.Settle(Entry.Asset);
		}
	}
	CloseTransactions();

	if (!Policy.bCompile && !Policy.bSave) return MCPResult(Result);

	if (Targets.Num() == 1)
	{
		CommitTarget(Targets[0], Result);
		return MCPResult(Result);
	}

	TArray<TSharedPtr<FJsonValue>> Rows;
	int32 Failed = 0;
	for (FTarget& Entry : Targets)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("assetPath"), Entry.Path);
		if (!CommitTarget(Entry, Row)) ++Failed;
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}
	Result->SetArrayField(TEXT("targets"), Rows);
	if (Failed > 0)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%d of %d targets did not commit; each row in targets says why."), Failed, Targets.Num()));
	}
	return MCPResult(Result);
}

bool FMCPEditScope::CommitTarget(FTarget& Entry, const TSharedPtr<FJsonObject>& Out)
{
	// No-op suppression: an untouched target is neither compiled nor written.
	if (!Entry.bChanged && !Policy.bEvenIfUnchanged) return true;

	bool bCompileOk = true;
	if (Policy.bCompile && Ops.Compile)
	{
		bCompileOk = Ops.Compile(Entry.Asset, Out);
		bCompiled = bCompiled && bCompileOk;
	}
	if (!bCompileOk && Policy.OnCompileFailure == EMCPCompileFailure::Refuse)
	{
		Out->SetBoolField(TEXT("success"), false);
		Out->SetBoolField(TEXT("saved"), false);
		Out->SetStringField(TEXT("error"), FString::Printf(TEXT("'%s' did not compile, so it was not saved."), *Entry.Path));
		return false;
	}
	if (!Policy.bSave) return true;
	if (!bCompileOk && Policy.OnCompileFailure == EMCPCompileFailure::SkipSave)
	{
		Out->SetBoolField(TEXT("saved"), false);
		return true;
	}

	if (Ops.Dialect == EMCPResultDialect::Persisted)
	{
		return MCPFinishPackageWrite(Out, Entry.Asset, /*bSave=*/true, Entry.bChanged);
	}

	TArray<UObject*> SaveSet;
	if (Ops.SaveSet) Ops.SaveSet(Entry.Asset, SaveSet);
	bool bSaved = true;
	FString Reason;
	for (UObject* Package : SaveSet)
	{
		FString PackageReason;
		if (!SaveAssetPackageChecked(Package, PackageReason))
		{
			bSaved = false;
			if (Reason.IsEmpty()) Reason = PackageReason;
		}
	}
	MCPNoteSaveOutcome(Out, Entry.Asset->GetPathName(), bSaved, Reason);
	return bSaved;
}

FMCPEditTransaction::FMCPEditTransaction(FMCPEditScope& InScope, int32 InTargetIndex)
	: Scope(&InScope)
	, TargetIndex(InTargetIndex)
{
	UObject* Primary = InScope.Targets.IsValidIndex(TargetIndex) ? InScope.Targets[TargetIndex].Asset : nullptr;
	Transaction.Emplace(TEXT("UE_MCP"), FText::FromString(InScope.Action), Primary);
	InScope.OpenTransactions.Add(this);
}

FMCPEditTransaction::~FMCPEditTransaction()
{
	Close();
}

void FMCPEditTransaction::Commit()
{
	bCommitted = true;
	if (Scope && Scope->Targets.IsValidIndex(TargetIndex)) Scope->Targets[TargetIndex].bChanged = true;
}

void FMCPEditTransaction::Cancel()
{
	// Drops the undo record only. Callers restore memory on refusal (F3 and F5 do).
	if (bCommitted && Scope && Scope->Targets.IsValidIndex(TargetIndex)) Scope->Targets[TargetIndex].bChanged = false;
	bCommitted = false;
	Close();
}

void FMCPEditTransaction::Close()
{
	if (Transaction.IsSet())
	{
		if (!bCommitted) Transaction->Cancel();
		Transaction.Reset();
	}
	if (Scope)
	{
		Scope->OpenTransactions.Remove(this);
		Scope = nullptr;
	}
}
