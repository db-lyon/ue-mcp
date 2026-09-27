#pragma once

// F1 AssetEdit (plans/domain-design.md 3.2). A handler opens a scope on the
// asset(s) it edits, begins an edit on a target right before it mutates it,
// and Finish() runs what the asset type declares: settle, compile, the
// compile-failure policy, the save set and the result dialect.
//
//   FMCPEditScope Edit(Params, TEXT("add_state_tree_task"), TEXT("assetPath"), Policy);
//   if (auto Err = Edit.Open<UStateTree>()) return Err;
//   ...refusals and no-op returns...
//   if (auto Err = Edit.Begin()) return Err;   // #932 guard, then the undo record
//   ...mutate... ; Edit.Commit();
//   return Edit.Finish(Result);

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"
#include "HandlerUtils.h"

/** What Finish does when the type's compile op reports failure. */
enum class EMCPCompileFailure : uint8
{
	/** The call still succeeds, reports saved:false and writes nothing. */
	SkipSave,
	/** The package is written anyway. */
	SaveAnyway,
	/** The call fails and nothing is written. */
	Refuse,
};

/** How a result reports persistence. */
enum class EMCPResultDialect : uint8
{
	/** saved, and saveError plus success:false when the write failed. */
	SavedSaveError,
	/** saved, persisted, packageDirty, packageName, persistError (MCPFinishPackageWrite). */
	Persisted,
};

/** The type's post-edit fixups an action asks for, run inside its undo record. */
enum class EMCPSettle : uint8
{
	None,
	/** A value changed; the type's light notification. */
	Value,
	/** Structure changed; the type's full validation or rebuild. */
	Structure,
};

/** How one action commits. It belongs to the action, not the asset type. */
struct FMCPCommitPolicy
{
	EMCPSettle Settle = EMCPSettle::None;
	bool bCompile = false;
	bool bSave = false;
	/** Compile and save even when no edit committed a change. */
	bool bEvenIfUnchanged = false;
	EMCPCompileFailure OnCompileFailure = EMCPCompileFailure::SkipSave;

	static FMCPCommitPolicy Read() { return FMCPCommitPolicy(); }

	/** An in-memory edit: settle, never compile or save. */
	static FMCPCommitPolicy Edit(EMCPSettle InSettle)
	{
		FMCPCommitPolicy Policy;
		Policy.Settle = InSettle;
		return Policy;
	}

	static FMCPCommitPolicy CompileSave(EMCPCompileFailure OnFailure, bool bInEvenIfUnchanged)
	{
		FMCPCommitPolicy Policy;
		Policy.bCompile = true;
		Policy.bSave = true;
		Policy.OnCompileFailure = OnFailure;
		Policy.bEvenIfUnchanged = bInEvenIfUnchanged;
		return Policy;
	}

	/** An edit that settles, compiles and saves once it changed something. */
	static FMCPCommitPolicy EditCompileSave(EMCPSettle InSettle, EMCPCompileFailure OnFailure)
	{
		FMCPCommitPolicy Policy = CompileSave(OnFailure, /*bInEvenIfUnchanged=*/false);
		Policy.Settle = InSettle;
		return Policy;
	}

	/** An edit that settles and saves, with no compile step. */
	static FMCPCommitPolicy EditSave(EMCPSettle InSettle)
	{
		FMCPCommitPolicy Policy;
		Policy.Settle = InSettle;
		Policy.bSave = true;
		return Policy;
	}

	/** Commit even when no edit changed anything (compile and lifecycle actions). */
	FMCPCommitPolicy EvenIfUnchanged() const
	{
		FMCPCommitPolicy Policy = *this;
		Policy.bEvenIfUnchanged = true;
		return Policy;
	}

	/** The public save param: false keeps compile and settle but drops the write and its guard. */
	FMCPCommitPolicy SaveIf(bool bInSave) const
	{
		FMCPCommitPolicy Policy = *this;
		Policy.bSave = Policy.bSave && bInSave;
		return Policy;
	}
};

/**
 * What one asset type declares. Specialise per type with these members:
 *   static const TCHAR* TypeName();
 *   static T* Load(const FString& Path, TSharedPtr<FJsonValue>& OutError);   // the type's resolver and its not-found error
 *   static TSharedPtr<FJsonValue> CheckOpen(T* Asset, const FString& Path);  // the default precondition
 *   static void Settle(T* Asset, EMCPSettle Kind);
 *   static bool Compile(T* Asset, const TSharedPtr<FJsonObject>& Out);       // writes the type's compile keys
 *   static void SaveSet(T* Asset, TArray<UObject*>& Out);
 *   static constexpr EMCPResultDialect Dialect = ...;
 */
template <typename T>
struct TMCPEditTraits;

class FMCPEditScope
{
public:
	FMCPEditScope(const TSharedPtr<FJsonObject>& InParams, const TCHAR* InAction, const TCHAR* InTargetParam, const FMCPCommitPolicy& InPolicy);
	~FMCPEditScope();

	FMCPEditScope(const FMCPEditScope&) = delete;
	FMCPEditScope& operator=(const FMCPEditScope&) = delete;

	/**
	 * Read the target param (one path, or an array of paths), load each as T
	 * and run Check on it. Returns the error to hand back, or nullptr. Pass
	 * nullptr as Check to skip the type's default precondition. When the save
	 * is certain (bEvenIfUnchanged), the #932 guard runs here too.
	 */
	template <typename T>
	TSharedPtr<FJsonValue> Open(TSharedPtr<FJsonValue> (*Check)(T*, const FString&) = &TMCPEditTraits<T>::CheckOpen)
	{
		FOps Declared;
		Declared.TypeName = TMCPEditTraits<T>::TypeName();
		Declared.Load = [](const FString& Path, TSharedPtr<FJsonValue>& OutError) -> UObject* { return TMCPEditTraits<T>::Load(Path, OutError); };
		if (Check)
		{
			Declared.Check = [Check](UObject* Asset, const FString& Path) { return Check(CastChecked<T>(Asset), Path); };
		}
		Declared.Settle = [](UObject* Asset, EMCPSettle Kind) { TMCPEditTraits<T>::Settle(CastChecked<T>(Asset), Kind); };
		Declared.Compile = [](UObject* Asset, const TSharedPtr<FJsonObject>& Out) { return TMCPEditTraits<T>::Compile(CastChecked<T>(Asset), Out); };
		Declared.SaveSet = [](UObject* Asset, TArray<UObject*>& Out) { TMCPEditTraits<T>::SaveSet(CastChecked<T>(Asset), Out); };
		Declared.Dialect = TMCPEditTraits<T>::Dialect;
		return OpenTargets(MoveTemp(Declared));
	}

	int32 Num() const { return Targets.Num(); }
	const FString& Path(int32 Index = 0) const { return Targets[Index].Path; }

	template <typename T>
	T* Target(int32 Index = 0) const
	{
		return Targets.IsValidIndex(Index) ? Cast<T>(Targets[Index].Asset) : nullptr;
	}

	template <typename T>
	TArray<T*> All() const
	{
		TArray<T*> Out;
		for (const FTarget& Entry : Targets) Out.Add(Cast<T>(Entry.Asset));
		return Out;
	}

	/**
	 * Start editing target Index: refuse an unwritable package when this action
	 * saves (#932), then open the target's undo record. The record belongs to
	 * the scope, so it stays open until Finish settles inside it, whatever
	 * block the handler called this from. Calling it again is a no-op.
	 */
	TSharedPtr<FJsonValue> Begin(int32 Index = 0);

	/** Keep target Index's undo record and mark it changed. */
	void Commit(int32 Index = 0);

	/** Drop target Index's undo record; the target stays unchanged. Callers restore memory (F3 and F5 do). */
	void Cancel(int32 Index = 0);

	/** Whether an edit on target Index committed a change. */
	bool Changed(int32 Index = 0) const { return Targets.IsValidIndex(Index) && Targets[Index].bChanged; }

	/** After Finish: whether every compiled target compiled. True when nothing compiled. */
	bool Compiled() const { return bCompiled; }

	/** Settle inside the open records, close them, then compile and save as the policy says and write the dialect onto Result. */
	TSharedPtr<FJsonValue> Finish(const TSharedPtr<FJsonObject>& Result);

private:
	struct FOps
	{
		const TCHAR* TypeName = TEXT("asset");
		TFunction<UObject*(const FString&, TSharedPtr<FJsonValue>&)> Load;
		TFunction<TSharedPtr<FJsonValue>(UObject*, const FString&)> Check;
		TFunction<void(UObject*, EMCPSettle)> Settle;
		TFunction<bool(UObject*, const TSharedPtr<FJsonObject>&)> Compile;
		TFunction<void(UObject*, TArray<UObject*>&)> SaveSet;
		EMCPResultDialect Dialect = EMCPResultDialect::SavedSaveError;
	};

	struct FTarget
	{
		FString Path;
		UObject* Asset = nullptr;
		bool bChanged = false;
		bool bGuarded = false;
		TUniquePtr<FScopedTransaction> Transaction;
	};

	TSharedPtr<FJsonValue> OpenTargets(FOps&& InOps);
	TSharedPtr<FJsonValue> Guard(FTarget& Entry);
	/** Compile and save one target, writing the type's keys and the dialect onto Out. */
	bool CommitTarget(FTarget& Entry, const TSharedPtr<FJsonObject>& Out);
	void CloseTransactions();

	TSharedPtr<FJsonObject> Params;
	FString Action;
	FString TargetParam;
	FMCPCommitPolicy Policy;
	FOps Ops;
	TArray<FTarget> Targets;
	bool bCompiled = true;
};
