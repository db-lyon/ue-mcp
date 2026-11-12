// asset(quick_search): one ranked list over assets, actors in the open level,
// and files under Source/ and Config/, fast enough to run on every keystroke
// (#1486). See AssetQuickSearch.h for the index.
//
// Members of FAssetHandlers live here as a translation-unit partition, like
// AssetHandlers_Hygiene.cpp. Every file-local symbol is in MCPQuickSearch or
// prefixed MCPQs, because the asset handlers share one unity blob.

#include "Handlers/Asset/AssetHandlers.h"
#include "Handlers/Asset/AssetQuickSearch.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerPagination.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EngineUtils.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace MCPQuickSearch
{
	namespace
	{
		/** Alphanumeric runs, lowercased, each also split at camelCase boundaries. */
		void MCPQsTokenize(const FString& In, TArray<FString>& Out)
		{
			auto AddRun = [&Out](const FString& Run)
			{
				if (Run.IsEmpty()) return;
				Out.AddUnique(Run.ToLower());
				// Split at lower-to-upper and at the last capital of an acronym.
				int32 Start = 0;
				for (int32 i = 1; i < Run.Len(); ++i)
				{
					const TCHAR Prev = Run[i - 1];
					const TCHAR Cur = Run[i];
					const bool bLowerToUpper = (FChar::IsLower(Prev) || FChar::IsDigit(Prev)) && FChar::IsUpper(Cur);
					const bool bAcronymEnd = FChar::IsUpper(Prev) && FChar::IsUpper(Cur) && i + 1 < Run.Len() && FChar::IsLower(Run[i + 1]);
					if (bLowerToUpper || bAcronymEnd)
					{
						Out.AddUnique(Run.Mid(Start, i - Start).ToLower());
						Start = i;
					}
				}
				if (Start > 0) Out.AddUnique(Run.Mid(Start).ToLower());
			};

			FString Run;
			Run.Reserve(In.Len());
			for (const TCHAR C : In)
			{
				if (FChar::IsAlnum(C)) Run.AppendChar(C);
				else { AddRun(Run); Run.Reset(); }
			}
			AddRun(Run);
		}

		FString MCPQsCompact(const FString& In)
		{
			FString Out;
			Out.Reserve(In.Len());
			for (const TCHAR C : In)
			{
				if (FChar::IsAlnum(C)) Out.AppendChar(FChar::ToLower(C));
			}
			return Out;
		}

		/** Exact token 10, token prefix 5, substring 2. */
		int32 MCPQsFieldScore(const FString& Term, const TArray<FString>& Tokens)
		{
			int32 Best = 0;
			for (const FString& Token : Tokens)
			{
				if (Token.Len() == Term.Len() && Token.Equals(Term, ESearchCase::CaseSensitive)) return 10;
				if (Best < 5 && Token.StartsWith(Term, ESearchCase::CaseSensitive)) Best = 5;
				else if (Best < 2 && Token.Contains(Term, ESearchCase::CaseSensitive)) Best = 2;
			}
			return Best;
		}

		/** Every character of Term appears in Text in order: "bpchr" finds bp_character. */
		bool MCPQsIsSubsequence(const FString& Term, const FString& Text)
		{
			if (Term.Len() < 2 || Term.Len() > Text.Len()) return false;
			int32 j = 0;
			for (int32 i = 0; i < Text.Len() && j < Term.Len(); ++i)
			{
				if (Text[i] == Term[j]) ++j;
			}
			return j == Term.Len();
		}

		/** A leading BP_, M_, DT_, WBP_: a letter, up to four more alphanumerics, then an underscore. */
		int32 MCPQsNamePrefixLength(const FString& Word)
		{
			if (Word.Len() < 2 || !FChar::IsAlpha(Word[0])) return 0;
			for (int32 i = 1; i < Word.Len() && i <= 5; ++i)
			{
				if (Word[i] == TCHAR('_')) return i + 1;
				if (!FChar::IsAlnum(Word[i])) return 0;
			}
			return 0;
		}
	}

	const TCHAR* KindName(EKind Kind)
	{
		switch (Kind)
		{
		case EKind::Actor: return TEXT("actor");
		case EKind::File: return TEXT("file");
		default: return TEXT("asset");
		}
	}

	FDoc MakeDoc(EKind Kind, const FString& Path, const FString& Name, const FString& ClassName, const FString& Summary, const FString& ObjectPath)
	{
		FDoc Doc;
		Doc.Kind = Kind;
		Doc.Path = Path;
		Doc.ObjectPath = ObjectPath;
		Doc.Name = Name;
		Doc.ClassName = ClassName;
		Doc.Summary = Summary;
		Doc.CompactName = MCPQsCompact(Name);
		Doc.NameLower = Name.ToLower();
		MCPQsTokenize(Name, Doc.NameTokens);
		MCPQsTokenize(ClassName, Doc.ClassTokens);
		MCPQsTokenize(Path, Doc.PathTokens);
		return Doc;
	}

	FQuery ParseQuery(const FString& Text)
	{
		FQuery Query;
		TArray<FString> Words;
		Text.ParseIntoArrayWS(Words);
		for (FString Word : Words)
		{
			const int32 Colon = Word.Find(TEXT(":"));
			if (Colon > 0)
			{
				const FString Head = Word.Left(Colon).ToLower();
				bool bKnown = true;
				if (Head == TEXT("actor") || Head == TEXT("actors")) Query.Kind = EKind::Actor;
				else if (Head == TEXT("asset") || Head == TEXT("assets")) Query.Kind = EKind::Asset;
				else if (Head == TEXT("file") || Head == TEXT("files")) Query.Kind = EKind::File;
				else bKnown = false;
				if (bKnown)
				{
					Query.bKindFilter = true;
					Word = Word.Mid(Colon + 1);
				}
			}
			if (Query.NamePrefix.IsEmpty())
			{
				const int32 PrefixLen = MCPQsNamePrefixLength(Word);
				if (PrefixLen > 0)
				{
					Query.NamePrefix = Word.Left(PrefixLen).ToLower();
					Word = Word.Mid(PrefixLen);
				}
			}
			FString Run;
			for (const TCHAR C : Word)
			{
				if (FChar::IsAlnum(C)) Run.AppendChar(FChar::ToLower(C));
				else if (Run.Len()) { Query.Terms.Add(Run); Run.Reset(); }
			}
			if (Run.Len()) Query.Terms.Add(Run);
		}
		// The prefix counts toward the whole-name bonus: SM_DoorFrame is an exact hit for "SM_DoorFrame".
		if (Query.Terms.Num() > 0) Query.CompactText = MCPQsCompact(Query.NamePrefix) + FString::Join(Query.Terms, TEXT(""));
		return Query;
	}

	int32 Score(const FQuery& Query, const FDoc& Doc)
	{
		if (Query.bKindFilter && Doc.Kind != Query.Kind) return 0;
		if (!Query.NamePrefix.IsEmpty() && !Doc.NameLower.StartsWith(Query.NamePrefix, ESearchCase::CaseSensitive)) return 0;

		// Every word has to land somewhere, so each keystroke narrows the list.
		int32 Total = 1;
		for (const FString& Term : Query.Terms)
		{
			int32 Best = MCPQsFieldScore(Term, Doc.NameTokens) * 3;
			if (Best < 30) Best = FMath::Max(Best, MCPQsFieldScore(Term, Doc.ClassTokens) * 2);
			if (Best < 10) Best = FMath::Max(Best, MCPQsFieldScore(Term, Doc.PathTokens));
			if (Best == 0)
			{
				if (!MCPQsIsSubsequence(Term, Doc.CompactName)) return 0;
				Best = 3;
			}
			Total += Best;
		}
		if (!Query.CompactText.IsEmpty())
		{
			if (Doc.CompactName.Equals(Query.CompactText, ESearchCase::CaseSensitive)) Total += 60;
			else if (Doc.CompactName.StartsWith(Query.CompactText, ESearchCase::CaseSensitive)) Total += 25;
		}
		return Total;
	}

	void SortHits(TArray<FHit>& Hits)
	{
		Hits.Sort([](const FHit& A, const FHit& B)
		{
			if (A.Score != B.Score) return A.Score > B.Score;
			if (A.Doc->Name.Len() != B.Doc->Name.Len()) return A.Doc->Name.Len() < B.Doc->Name.Len();
			if (A.Doc->Kind != B.Doc->Kind) return A.Doc->Kind < B.Doc->Kind;
			const FString& PathA = A.Doc->ObjectPath.IsEmpty() ? A.Doc->Path : A.Doc->ObjectPath;
			const FString& PathB = B.Doc->ObjectPath.IsEmpty() ? B.Doc->Path : B.Doc->ObjectPath;
			return PathA < PathB;
		});
	}

	// ─── Asset index ─────────────────────────────────────────────────────────

	namespace
	{
		// A raw pointer, deleted in Shutdown: a static destructor would run after
		// the Asset Registry is gone and try to unbind from it.
		FAssetIndex* GMCPQsIndex = nullptr;
	}

	FAssetIndex& FAssetIndex::Get()
	{
		if (!GMCPQsIndex) GMCPQsIndex = new FAssetIndex();
		return *GMCPQsIndex;
	}

	void FAssetIndex::Shutdown()
	{
		delete GMCPQsIndex;
		GMCPQsIndex = nullptr;
	}

	FAssetIndex::~FAssetIndex()
	{
		Unbind();
	}

	void FAssetIndex::Unbind()
	{
		if (!bBound) return;
		bBound = false;
		if (!FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry"))) return;
		if (IAssetRegistry* Registry = IAssetRegistry::Get())
		{
			Registry->OnAssetAdded().Remove(AddedHandle);
			Registry->OnAssetRemoved().Remove(RemovedHandle);
			Registry->OnAssetRenamed().Remove(RenamedHandle);
			Registry->OnAssetUpdated().Remove(UpdatedHandle);
		}
	}

	double FAssetIndex::EnsureBound()
	{
		if (bBound) return 0.0;
		IAssetRegistry* Registry = IAssetRegistry::Get();
		if (!Registry) return 0.0;

		const double Start = FPlatformTime::Seconds();
		// Subscribe before the snapshot so nothing added in between is missed;
		// Upsert is keyed by object path, so seeing an asset twice is harmless.
		AddedHandle = Registry->OnAssetAdded().AddLambda([this](const FAssetData& Data) { UpsertAsset(Data); });
		UpdatedHandle = Registry->OnAssetUpdated().AddLambda([this](const FAssetData& Data) { UpsertAsset(Data); });
		RemovedHandle = Registry->OnAssetRemoved().AddLambda([this](const FAssetData& Data) { Remove(Data.GetObjectPathString()); });
		RenamedHandle = Registry->OnAssetRenamed().AddLambda([this](const FAssetData& Data, const FString& OldObjectPath)
		{
			Remove(OldObjectPath);
			UpsertAsset(Data);
		});
		bBound = true;

		TArray<FAssetData> All;
		Registry->GetAllAssets(All, /*bIncludeOnlyOnDiskAssets=*/true);
		{
			FScopeLock Guard(&Lock);
			Docs.Reserve(Docs.Num() + All.Num());
			IndexByObjectPath.Reserve(IndexByObjectPath.Num() + All.Num());
		}
		for (const FAssetData& Data : All) UpsertAsset(Data);
		return (FPlatformTime::Seconds() - Start) * 1000.0;
	}

	void FAssetIndex::UpsertAsset(const FAssetData& Data)
	{
		const FString Package = Data.PackageName.ToString();
		if (Package.StartsWith(TEXT("/Temp/")) || Package.StartsWith(TEXT("/Memory/"))) return;
		const FString ClassName = Data.AssetClassPath.GetAssetName().ToString();
		// A rename leaves a redirector stub behind; it is not something to open.
		if (ClassName == TEXT("ObjectRedirector"))
		{
			Remove(Data.GetObjectPathString());
			return;
		}
		const FString PackagePath = Data.PackagePath.ToString();
		Upsert(MakeDoc(EKind::Asset, Package, Data.AssetName.ToString(), ClassName,
			FString::Printf(TEXT("%s in %s"), *ClassName, *PackagePath), Data.GetObjectPathString()));
	}

	void FAssetIndex::Upsert(FDoc&& Doc)
	{
		FScopeLock Guard(&Lock);
		const FString Key = Doc.ObjectPath.IsEmpty() ? Doc.Path : Doc.ObjectPath;
		if (const int32* Existing = IndexByObjectPath.Find(Key))
		{
			Docs[*Existing] = MoveTemp(Doc);
			return;
		}
		IndexByObjectPath.Add(Key, Docs.Num());
		Docs.Add(MoveTemp(Doc));
	}

	void FAssetIndex::Remove(const FString& ObjectPath)
	{
		FScopeLock Guard(&Lock);
		int32 Index = INDEX_NONE;
		if (!IndexByObjectPath.RemoveAndCopyValue(ObjectPath, Index)) return;
		const int32 Last = Docs.Num() - 1;
		if (Index != Last)
		{
			Docs.Swap(Index, Last);
			const FDoc& Moved = Docs[Index];
			IndexByObjectPath.Add(Moved.ObjectPath.IsEmpty() ? Moved.Path : Moved.ObjectPath, Index);
		}
		Docs.RemoveAt(Last, 1, EAllowShrinking::No);
	}

	int32 FAssetIndex::Num() const
	{
		FScopeLock Guard(&Lock);
		return Docs.Num();
	}

	void FAssetIndex::Query(const FQuery& InQuery, TArray<FHit>& Out) const
	{
		FScopeLock Guard(&Lock);
		if (InQuery.bKindFilter && InQuery.Kind != EKind::Asset) return;
		for (const FDoc& Doc : Docs)
		{
			const int32 S = Score(InQuery, Doc);
			if (S > 0) Out.Add({ S, &Doc });
		}
	}

	void FAssetIndex::WithLock(TFunctionRef<void()> Fn) const
	{
		FScopeLock Guard(&Lock);
		Fn();
	}

	// ─── Actors and files ────────────────────────────────────────────────────

	namespace
	{
		void MCPQsCollectActors(TArray<FDoc>& Out)
		{
			UWorld* World = GetEditorWorld();
			if (!World) return;
			const FString MapName = FPackageName::GetShortName(World->GetOutermost()->GetName());
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (!IsValid(Actor) || !Actor->IsListedInSceneOutliner()) continue;
				FString Label = Actor->GetActorLabel(/*bCreateIfNone=*/false);
				if (Label.IsEmpty()) Label = Actor->GetName();
				const FString ClassName = Actor->GetClass()->GetName();
				const FName FolderName = Actor->GetFolderPath();
				const FString Folder = FolderName.IsNone() ? FString() : FolderName.ToString();
				const FString Summary = Folder.IsEmpty()
					? FString::Printf(TEXT("%s in level %s"), *ClassName, *MapName)
					: FString::Printf(TEXT("%s in level %s, folder %s"), *ClassName, *MapName, *Folder);
				Out.Add(MakeDoc(EKind::Actor, Actor->GetPathName(), Label, ClassName, Summary));
			}
		}

		FString MCPQsDescribeFile(const FString& FileName)
		{
			const FString Lower = FileName.ToLower();
			if (Lower.EndsWith(TEXT(".build.cs"))) return TEXT("Module build rules");
			if (Lower.EndsWith(TEXT(".target.cs"))) return TEXT("Build target rules");
			const FString Ext = FPaths::GetExtension(Lower);
			if (Ext == TEXT("h") || Ext == TEXT("hpp") || Ext == TEXT("inl")) return TEXT("C++ header");
			if (Ext == TEXT("cpp") || Ext == TEXT("cc") || Ext == TEXT("c")) return TEXT("C++ source");
			if (Ext == TEXT("cs")) return TEXT("C# source");
			if (Ext == TEXT("ini")) return TEXT("Config file");
			return Ext.IsEmpty() ? FString(TEXT("File")) : FString::Printf(TEXT("%s file"), *Ext.ToUpper());
		}

		/** Files under Source/ and Config/, re-walked when older than this. */
		constexpr double MCPQsFileCacheSeconds = 2.0;
		constexpr int32 MCPQsMaxFiles = 50000;

		struct FMCPQsFileCache
		{
			double BuiltAt = -1.0;
			bool bTruncated = false;
			TArray<FDoc> Docs;
		};

		FMCPQsFileCache& MCPQsFiles(bool bForceRefresh)
		{
			static FMCPQsFileCache Cache;
			const double Now = FPlatformTime::Seconds();
			if (!bForceRefresh && Cache.BuiltAt >= 0.0 && Now - Cache.BuiltAt < MCPQsFileCacheSeconds) return Cache;

			Cache.Docs.Reset();
			Cache.bTruncated = false;
			FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
			FPaths::NormalizeDirectoryName(ProjectDir);
			ProjectDir /= TEXT("");
			const TCHAR* const Roots[] = { TEXT("Source"), TEXT("Config") };
			for (const TCHAR* Root : Roots)
			{
				const FString Dir = ProjectDir / Root;
				if (!IFileManager::Get().DirectoryExists(*Dir)) continue;
				IFileManager::Get().IterateDirectoryStatRecursively(*Dir, [&](const TCHAR* FileName, const FFileStatData& Stat) -> bool
				{
					if (Stat.bIsDirectory) return true;
					if (Cache.Docs.Num() >= MCPQsMaxFiles) { Cache.bTruncated = true; return false; }
					FString Full(FileName);
					FPaths::NormalizeFilename(Full);
					FString Rel = Full;
					if (Rel.StartsWith(ProjectDir)) Rel.RightChopInline(ProjectDir.Len());
					const FString Name = FPaths::GetCleanFilename(Rel);
					const FString RelDir = FPaths::GetPath(Rel);
					const FString Size = Stat.FileSize < 1024
						? FString::Printf(TEXT("%lld bytes"), static_cast<long long>(Stat.FileSize))
						: FString::Printf(TEXT("%.1f KB"), Stat.FileSize / 1024.0);
					const FString Desc = MCPQsDescribeFile(Name);
					Cache.Docs.Add(MakeDoc(EKind::File, Rel, Name, FPaths::GetExtension(Name),
						FString::Printf(TEXT("%s in %s, %s"), *Desc, *RelDir, *Size)));
					return true;
				});
			}
			Cache.BuiltAt = FPlatformTime::Seconds();
			return Cache;
		}
	}
}

TSharedPtr<FJsonValue> FAssetHandlers::QuickSearch(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPQuickSearch;
	const double Start = FPlatformTime::Seconds();

	FString Text;
	if (auto Err = RequireString(Params, TEXT("query"), Text)) return Err;
	const bool bRefreshFiles = OptionalBool(Params, TEXT("refreshFiles"), false);

	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params, FString::Printf(TEXT("quick_search|query=%s"), *Text),
			/*DefaultLimit*/ 20, /*MaxLimit*/ 200, Page))
	{
		return Err;
	}

	const FQuery Query = ParseQuery(Text);
	if (Query.IsEmpty())
	{
		return MCPError(TEXT("query has nothing to search for. Pass words, a name prefix such as BP_ or DT_, or a kind filter: actor:, asset: or file:."));
	}

	const bool bWantAssets = !Query.bKindFilter || Query.Kind == EKind::Asset;
	const bool bWantActors = !Query.bKindFilter || Query.Kind == EKind::Actor;
	const bool bWantFiles = !Query.bKindFilter || Query.Kind == EKind::File;

	FAssetIndex& Index = FAssetIndex::Get();
	const double BuildMs = bWantAssets ? Index.EnsureBound() : 0.0;

	TArray<FDoc> ActorDocs;
	if (bWantActors) MCPQsCollectActors(ActorDocs);
	FMCPQsFileCache* Files = bWantFiles ? &MCPQsFiles(bRefreshFiles) : nullptr;

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Index.WithLock([&]()
	{
		TArray<FHit> Hits;
		if (bWantAssets) Index.Query(Query, Hits);
		for (const FDoc& Doc : ActorDocs)
		{
			const int32 S = Score(Query, Doc);
			if (S > 0) Hits.Add({ S, &Doc });
		}
		if (Files)
		{
			for (const FDoc& Doc : Files->Docs)
			{
				const int32 S = Score(Query, Doc);
				if (S > 0) Hits.Add({ S, &Doc });
			}
		}
		SortHits(Hits);

		int32 ByKind[3] = { 0, 0, 0 };
		TArray<MCPPagination::FPageRow> Rows;
		Rows.Reserve(Hits.Num());
		for (const FHit& Hit : Hits)
		{
			++ByKind[static_cast<int32>(Hit.Doc->Kind)];
			const FString& Id = Hit.Doc->ObjectPath.IsEmpty() ? Hit.Doc->Path : Hit.Doc->ObjectPath;
			Rows.Add({ FString(KindName(Hit.Doc->Kind)) + TEXT(":") + Id, nullptr });
		}
		MCPPagination::EmitPage(Page, Rows, TEXT("results"), Result);

		// Only the page is turned into JSON: a broad query at 50k assets would
		// otherwise spend its budget building rows nobody reads.
		const int32 PageStart = static_cast<int32>(Result->GetNumberField(TEXT("pageOffset")));
		const int32 PageCount = static_cast<int32>(Result->GetNumberField(TEXT("count")));
		TArray<TSharedPtr<FJsonValue>> PageRows;
		for (int32 i = PageStart; i < PageStart + PageCount && i < Hits.Num(); ++i)
		{
			const FDoc& Doc = *Hits[i].Doc;
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("kind"), KindName(Doc.Kind));
			R->SetStringField(TEXT("path"), Doc.Path);
			if (!Doc.ObjectPath.IsEmpty()) R->SetStringField(TEXT("objectPath"), Doc.ObjectPath);
			R->SetStringField(TEXT("name"), Doc.Name);
			R->SetStringField(TEXT("class"), Doc.ClassName);
			R->SetStringField(TEXT("summary"), Doc.Summary);
			R->SetNumberField(TEXT("score"), Hits[i].Score);
			PageRows.Add(MakeShared<FJsonValueObject>(R));
		}
		Result->SetArrayField(TEXT("results"), PageRows);
		Result->SetNumberField(TEXT("totalMatched"), Hits.Num());

		TSharedPtr<FJsonObject> Counts = MakeShared<FJsonObject>();
		Counts->SetNumberField(TEXT("asset"), ByKind[static_cast<int32>(EKind::Asset)]);
		Counts->SetNumberField(TEXT("actor"), ByKind[static_cast<int32>(EKind::Actor)]);
		Counts->SetNumberField(TEXT("file"), ByKind[static_cast<int32>(EKind::File)]);
		Result->SetObjectField(TEXT("matchedByKind"), Counts);
	});

	Result->SetStringField(TEXT("query"), Text);
	if (Query.bKindFilter) Result->SetStringField(TEXT("kindFilter"), KindName(Query.Kind));
	if (!Query.NamePrefix.IsEmpty()) Result->SetStringField(TEXT("namePrefix"), Query.NamePrefix);
	TArray<TSharedPtr<FJsonValue>> Terms;
	for (const FString& Term : Query.Terms) Terms.Add(MakeShared<FJsonValueString>(Term));
	Result->SetArrayField(TEXT("terms"), Terms);
	if (bWantAssets) Result->SetNumberField(TEXT("indexedAssets"), Index.Num());
	if (bWantActors) Result->SetNumberField(TEXT("scannedActors"), ActorDocs.Num());
	if (Files)
	{
		Result->SetNumberField(TEXT("indexedFiles"), Files->Docs.Num());
		if (Files->bTruncated) Result->SetBoolField(TEXT("filesTruncated"), true);
	}
	if (BuildMs > 0.0) Result->SetNumberField(TEXT("indexBuildMs"), BuildMs);
	Result->SetNumberField(TEXT("elapsedMs"), (FPlatformTime::Seconds() - Start) * 1000.0);
	return MCPResult(Result);
}
