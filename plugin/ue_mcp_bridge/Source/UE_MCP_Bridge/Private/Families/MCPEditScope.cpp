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
	CloseTransactions();
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
		TSharedPtr<FJsonValue> LoadError;
		UObject* Asset = Ops.Load ? Ops.Load(Path, LoadError) : nullptr;
		if (!Asset) return LoadError.IsValid() ? LoadError : MCPAssetLoadError(Path, Ops.TypeName);
		if (Ops.Check)
		{
			if (auto Err = Ops.Check(Asset, Path)) return Err;
		}
		FTarget& Entry = Targets.AddDefaulted_GetRef();
		Entry.Path = Path;
		Entry.Asset = Asset;
	}

	// A save that happens whatever the edit does is guarded before anything runs.
	if (Policy.bSave && Policy.bEvenIfUnchanged)
	{
		for (FTarget& Entry : Targets)
		{
			if (auto Blocked = Guard(Entry)) return Blocked;
		}
	}
	return nullptr;
}

TSharedPtr<FJsonValue> FMCPEditScope::Guard(FTarget& Entry)
{
	// #932: the guard belongs to the save, so an action that does not write never meets it.
	if (!Policy.bSave || Entry.bGuarded) return nullptr;
	if (auto Blocked = MCPAssetWriteBlockedError(Entry.Asset, Entry.Path, *FString::Printf(TEXT("%s '%s'"), *Action, *Entry.Path)))
	{
		return Blocked;
	}
	Entry.bGuarded = true;
	return nullptr;
}

TSharedPtr<FJsonValue> FMCPEditScope::Begin(int32 Index)
{
	if (!Targets.IsValidIndex(Index))
	{
		return MCPError(FString::Printf(TEXT("%s: there is no target %d to edit."), *Action, Index));
	}
	FTarget& Entry = Targets[Index];
	if (auto Blocked = Guard(Entry)) return Blocked;
	if (!Entry.Transaction.IsValid())
	{
		Entry.Transaction = MakeUnique<FScopedTransaction>(TEXT("UE_MCP"), FText::FromString(Action), Entry.Asset);
	}
	return nullptr;
}

void FMCPEditScope::Commit(int32 Index)
{
	if (!Targets.IsValidIndex(Index)) return;
	FTarget& Entry = Targets[Index];
	// A commit with no Begin still marks the change, so Finish commits it; there is just no undo record.
	Entry.bChanged = true;
}

void FMCPEditScope::Cancel(int32 Index)
{
	if (!Targets.IsValidIndex(Index)) return;
	FTarget& Entry = Targets[Index];
	Entry.bChanged = false;
	if (Entry.Transaction.IsValid())
	{
		Entry.Transaction->Cancel();
		Entry.Transaction.Reset();
	}
}

void FMCPEditScope::CloseTransactions()
{
	// Newest first, the way nested records unwind. An unchanged target's record is dropped.
	for (int32 i = Targets.Num() - 1; i >= 0; --i)
	{
		FTarget& Entry = Targets[i];
		if (!Entry.Transaction.IsValid()) continue;
		if (!Entry.bChanged) Entry.Transaction->Cancel();
		Entry.Transaction.Reset();
	}
}

TSharedPtr<FJsonValue> FMCPEditScope::Finish(const TSharedPtr<FJsonObject>& Result)
{
	// Settle inside the still-open records, so undo takes the fixups too.
	if (Policy.Settle != EMCPSettle::None && Ops.Settle)
	{
		for (const FTarget& Entry : Targets)
		{
			if (Entry.bChanged) Ops.Settle(Entry.Asset, Policy.Settle);
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
		// The type's compile op may already have said why, in its own words.
		if (!Out->HasField(TEXT("error")))
		{
			Out->SetStringField(TEXT("error"), FString::Printf(TEXT("'%s' did not compile, so it was not saved."), *Entry.Path));
		}
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
