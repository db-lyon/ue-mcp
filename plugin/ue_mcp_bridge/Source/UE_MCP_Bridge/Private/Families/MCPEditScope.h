#pragma once

// F1 AssetEdit (plans/domain-design.md 3.2). A handler opens a scope on the
// asset(s) it edits, does its one mutation inside a per-target transaction,
// and Finish() runs what the asset type declares: settle, compile, the
// compile-failure policy, the save set and the result dialect.
//
//   FMCPEditScope Edit(Params, TEXT("add_state_tree_task"), TEXT("assetPath"), Policy);
//   if (auto Err = Edit.Open<UStateTree>()) return Err;
//   FMCPEditTransaction Txn(Edit);
//   ...mutate... ; Txn.Commit();
//   return Edit.Finish(Result);

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Optional.h"
#include "ScopedTransaction.h"
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

/** How one action commits. It belongs to the action, not the asset type. */
struct FMCPCommitPolicy
{
	/** Run the type's settle op on each changed target, inside its transaction. */
	bool bSettle = false;
	bool bCompile = false;
	bool bSave = false;
	/** Compile and save even when no transaction committed a change. */
	bool bEvenIfUnchanged = false;
	EMCPCompileFailure OnCompileFailure = EMCPCompileFailure::SkipSave;

	static FMCPCommitPolicy Read() { return FMCPCommitPolicy(); }

	static FMCPCommitPolicy Edit(bool bInSettle)
	{
		FMCPCommitPolicy Policy;
		Policy.bSettle = bInSettle;
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
};

/**
 * What one asset type declares. Specialise per type with these members:
 *   static const TCHAR* TypeName();
 *   static TSharedPtr<FJsonValue> CheckOpen(T* Asset, const FString& Path);  // the default precondition
 *   static void Settle(T* Asset);
 *   static bool Compile(T* Asset, const TSharedPtr<FJsonObject>& Out);       // writes the type's compile keys
 *   static void SaveSet(T* Asset, TArray<UObject*>& Out);
 *   static constexpr EMCPResultDialect Dialect = ...;
 */
template <typename T>
struct TMCPEditTraits;

class FMCPEditTransaction;

class FMCPEditScope
{
public:
	FMCPEditScope(const TSharedPtr<FJsonObject>& InParams, const TCHAR* InAction, const TCHAR* InTargetParam, const FMCPCommitPolicy& InPolicy);
	~FMCPEditScope();

	FMCPEditScope(const FMCPEditScope&) = delete;
	FMCPEditScope& operator=(const FMCPEditScope&) = delete;

	/**
	 * Read the target param (one path, or an array of paths), load each as T,
	 * run Check on it, and refuse a package that cannot be written when this
	 * action saves (#932). Returns the error to hand back, or nullptr. Pass
	 * nullptr as Check to skip the type's default precondition.
	 */
	template <typename T>
	TSharedPtr<FJsonValue> Open(TSharedPtr<FJsonValue> (*Check)(T*, const FString&) = &TMCPEditTraits<T>::CheckOpen)
	{
		FOps Ops;
		Ops.TypeName = TMCPEditTraits<T>::TypeName();
		Ops.Load = [](const FString& Path) -> UObject* { return LoadAssetByPath<T>(Path); };
		if (Check)
		{
			Ops.Check = [Check](UObject* Asset, const FString& Path) { return Check(CastChecked<T>(Asset), Path); };
		}
		Ops.Settle = [](UObject* Asset) { TMCPEditTraits<T>::Settle(CastChecked<T>(Asset)); };
		Ops.Compile = [](UObject* Asset, const TSharedPtr<FJsonObject>& Out) { return TMCPEditTraits<T>::Compile(CastChecked<T>(Asset), Out); };
		Ops.SaveSet = [](UObject* Asset, TArray<UObject*>& Out) { TMCPEditTraits<T>::SaveSet(CastChecked<T>(Asset), Out); };
		Ops.Dialect = TMCPEditTraits<T>::Dialect;
		return OpenTargets(MoveTemp(Ops));
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

	/** Whether a transaction on target Index committed a change. */
	bool Changed(int32 Index = 0) const { return Targets.IsValidIndex(Index) && Targets[Index].bChanged; }

	/** After Finish: whether every compiled target compiled. True when nothing compiled. */
	bool Compiled() const { return bCompiled; }

	/** Close every transaction, then settle, compile and save as the policy says, and write the dialect onto Result. */
	TSharedPtr<FJsonValue> Finish(const TSharedPtr<FJsonObject>& Result);

private:
	friend class FMCPEditTransaction;

	struct FOps
	{
		const TCHAR* TypeName = TEXT("asset");
		TFunction<UObject*(const FString&)> Load;
		TFunction<TSharedPtr<FJsonValue>(UObject*, const FString&)> Check;
		TFunction<void(UObject*)> Settle;
		TFunction<bool(UObject*, const TSharedPtr<FJsonObject>&)> Compile;
		TFunction<void(UObject*, TArray<UObject*>&)> SaveSet;
		EMCPResultDialect Dialect = EMCPResultDialect::SavedSaveError;
	};

	struct FTarget
	{
		FString Path;
		UObject* Asset = nullptr;
		bool bChanged = false;
	};

	TSharedPtr<FJsonValue> OpenTargets(FOps&& InOps);
	/** Compile and save one target, writing the type's keys and the dialect onto Out. */
	bool CommitTarget(FTarget& Entry, const TSharedPtr<FJsonObject>& Out);
	void CloseTransactions();

	TSharedPtr<FJsonObject> Params;
	FString Action;
	FString TargetParam;
	FMCPCommitPolicy Policy;
	FOps Ops;
	TArray<FTarget> Targets;
	TArray<FMCPEditTransaction*> OpenTransactions;
	bool bCompiled = true;
	bool bFinished = false;
};

/**
 * One undo record for one target. It is cancelled unless Commit() marked a
 * change, so a refusal and a no-op leave the undo history untouched, and the
 * scope closes it before anything compiles or saves.
 */
class FMCPEditTransaction
{
public:
	explicit FMCPEditTransaction(FMCPEditScope& InScope, int32 InTargetIndex = 0);
	~FMCPEditTransaction();

	FMCPEditTransaction(const FMCPEditTransaction&) = delete;
	FMCPEditTransaction& operator=(const FMCPEditTransaction&) = delete;

	/** Keep the record and mark the target changed. */
	void Commit();
	/** Drop the record; the target stays unchanged. */
	void Cancel();

private:
	friend class FMCPEditScope;
	void Close();

	FMCPEditScope* Scope;
	int32 TargetIndex;
	TOptional<FScopedTransaction> Transaction;
	bool bCommitted = false;
};
