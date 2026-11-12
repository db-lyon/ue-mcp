#pragma once

// Keystroke-speed search over assets, open-level actors and project files
// (#1486). Assets come from an in-memory index built once from the Asset
// Registry and kept current by its add, remove, rename and update events, so a
// query never copies or re-tokenizes the registry. Actors are read from the
// editor world per query, and Source/ and Config/ files from a short-lived cache.

#include "CoreMinimal.h"
#include "Delegates/IDelegateInstance.h"
#include "HAL/CriticalSection.h"
#include "Templates/Function.h"

struct FAssetData;

namespace MCPQuickSearch
{
	enum class EKind : uint8 { Asset, Actor, File };

	const TCHAR* KindName(EKind Kind);

	/** One searchable entry, tokenized once when it enters the index. */
	struct FDoc
	{
		EKind Kind = EKind::Asset;
		/** Asset: package name. Actor: object path. File: project-relative path. */
		FString Path;
		/** Asset: object path. Empty for the other kinds. */
		FString ObjectPath;
		FString Name;
		FString ClassName;
		FString Summary;
		/** Lowercase name, alphanumerics only, for prefix and subsequence matches. */
		FString CompactName;
		/** Lowercase name with separators kept, for the name prefix filter. */
		FString NameLower;
		TArray<FString> NameTokens;
		TArray<FString> ClassTokens;
		TArray<FString> PathTokens;
	};

	FDoc MakeDoc(EKind Kind, const FString& Path, const FString& Name, const FString& ClassName, const FString& Summary, const FString& ObjectPath = FString());

	/** A query split into its filters and the words that are scored. */
	struct FQuery
	{
		TArray<FString> Terms;
		/** Name prefix and terms joined, lowercase alphanumerics only; empty without terms. */
		FString CompactText;
		/** Lowercase name prefix such as "bp_", from a leading BP_, M_, DT_ word. */
		FString NamePrefix;
		bool bKindFilter = false;
		EKind Kind = EKind::Asset;

		bool IsEmpty() const { return Terms.Num() == 0 && NamePrefix.IsEmpty() && !bKindFilter; }
	};

	FQuery ParseQuery(const FString& Text);

	/** 0 when the entry does not match; higher is better. */
	int32 Score(const FQuery& Query, const FDoc& Doc);

	struct FHit
	{
		int32 Score = 0;
		const FDoc* Doc = nullptr;
	};

	/** Best first; equal scores order by shorter name, then kind, then path, so a page cursor has one fixed sequence. */
	void SortHits(TArray<FHit>& Hits);

	/**
	 * The asset half. Get() returns the one bound to the Asset Registry; a
	 * default-constructed index is unbound and filled by hand, which is how the
	 * latency test builds 50,000 entries without a 50,000-asset project.
	 */
	class FAssetIndex
	{
	public:
		FAssetIndex() = default;
		~FAssetIndex();
		FAssetIndex(const FAssetIndex&) = delete;
		FAssetIndex& operator=(const FAssetIndex&) = delete;

		static FAssetIndex& Get();
		/** Called from module shutdown, while the Asset Registry is still loaded. */
		static void Shutdown();

		/** Build from the registry and subscribe to its events. Returns the build time in ms, 0 when already bound. */
		double EnsureBound();
		bool IsBound() const { return bBound; }

		void Upsert(FDoc&& Doc);
		void Remove(const FString& ObjectPath);
		int32 Num() const;

		/** Score every entry under the index lock and append the matches to Out. */
		void Query(const FQuery& Query, TArray<FHit>& Out) const;

		/** Hits point into the index; copy what you need before the next mutation. */
		void WithLock(TFunctionRef<void()> Fn) const;

	private:
		void UpsertAsset(const FAssetData& Data);
		void Unbind();

		TArray<FDoc> Docs;
		TMap<FString, int32> IndexByObjectPath;
		mutable FCriticalSection Lock;
		bool bBound = false;
		FDelegateHandle AddedHandle;
		FDelegateHandle RemovedHandle;
		FDelegateHandle RenamedHandle;
		FDelegateHandle UpdatedHandle;
	};
}
