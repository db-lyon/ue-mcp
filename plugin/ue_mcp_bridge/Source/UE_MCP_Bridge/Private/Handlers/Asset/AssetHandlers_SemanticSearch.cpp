// asset(semantic_search), asset(semantic_search_status) and
// asset(semantic_search_index): Epic's Semantic Search plugin (#1829).
//
// 5.9 removed the toolset that wrapped this plugin, which leaves its C++ module
// interface (UE::SemanticSearch::ISemanticSearchModule) as the way in. The
// plugin is experimental and off by default, so it is never linked: its headers
// are compiled in when the engine ships them, and everything below is a virtual
// call on the module object or on the embedding provider it hands out, or a
// read of the settings object found by name. The bridge loads whether or not a
// project enables the plugin.
//
// A query is embedded by the configured provider (Epic's internal Loci service
// or an OpenAI-compatible endpoint) and then searched in the module's hybrid
// vector and keyword index. Neither works without a provider, so every action
// that needs one says so by name instead of returning an empty list that reads
// as "nothing matched".
//
// Members of FAssetHandlers live here as a translation-unit partition. Every
// file-local symbol is in MCPSemanticSearch, because the asset handlers share
// one unity blob.

#include "Handlers/Asset/AssetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerDeferred.h"

#include "Async/Async.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/EngineTypes.h"
#include "Hash/Blake3.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectGlobals.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#if WITH_UEMCP_SEMANTIC_SEARCH
#include "ISemanticSearchModule.h"
#endif

#if WITH_UEMCP_SEMANTIC_SEARCH
namespace MCPSemanticSearch
{
	static const TCHAR* const ModuleName = TEXT("SemanticSearch");

	static const TCHAR* const PluginOff =
		TEXT("Epic's Semantic Search plugin is not loaded. It ships with UE 5.8+ as an experimental plugin, off by default: "
		     "enable SemanticSearch in the project's plugins and restart the editor.");

	using namespace UE::SemanticSearch;

	static ISemanticSearchModule* Module()
	{
		// ISemanticSearchModule::Get() loads the module on demand, which an
		// action must not do to a plugin the project left off.
		if (!FModuleManager::Get().IsModuleLoaded(ModuleName)) return nullptr;
		return FModuleManager::GetModulePtr<ISemanticSearchModule>(ModuleName);
	}

	static USemanticSearchSettings* Settings()
	{
		// Found by name: GetDefault<USemanticSearchSettings>() would link StaticClass.
		UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/SemanticSearch.SemanticSearchSettings"));
		return Class ? static_cast<USemanticSearchSettings*>(Class->GetDefaultObject()) : nullptr;
	}

	/** The configured provider's name, whether it has what it needs to answer, and what is missing when not. */
	struct FProviderState
	{
		FString Name;
		bool bConfigured = false;
		FString Note;
	};

	static FProviderState ReadProvider(const USemanticSearchSettings* S)
	{
		FProviderState Out;
		if (!S)
		{
			Out.Note = TEXT("USemanticSearchSettings was not found");
			return Out;
		}
		if (S->Provider == ESemanticSearchEmbeddingProvider::OpenAI)
		{
			Out.Name = TEXT("OpenAI");
			// A local OpenAI-compatible server (LiteLLM, Ollama) needs no key; api.openai.com does.
			const bool bHostedOpenAI = S->EmbeddingBaseUrl.Contains(TEXT("api.openai.com"), ESearchCase::IgnoreCase);
			Out.bConfigured = !S->EmbeddingBaseUrl.IsEmpty() && (!bHostedOpenAI || !S->EmbeddingApiKey.IsEmpty());
			if (!Out.bConfigured)
			{
				Out.Note = S->EmbeddingBaseUrl.IsEmpty()
					? TEXT("Provider is OpenAI but EmbeddingBaseUrl is empty")
					: TEXT("Provider is OpenAI at api.openai.com but EmbeddingApiKey is empty. Set it (and CaptioningApiKey for indexing) "
					       "in Editor Preferences > Plugins > Semantic Search, or point EmbeddingBaseUrl at a local OpenAI-compatible server.");
			}
			return Out;
		}
#if UE_MCP_HAS_5_9_API
		if (S->Provider == ESemanticSearchEmbeddingProvider::Loci)
		{
			Out.Name = TEXT("Loci");
			// The provider reads its endpoints from this file and has nothing to call without it.
			const FString ConfigPath = FPaths::EngineDir() / TEXT("Restricted/NotForLicensees/Config/SemanticSearch.json");
			Out.bConfigured = FPaths::FileExists(ConfigPath);
			if (!Out.bConfigured)
			{
				Out.Note = FString::Printf(
					TEXT("Provider is Loci (Epic's internal service), whose endpoints come from %s, and that file is absent in this engine. "
					     "Switch Provider to OpenAI in Editor Preferences > Plugins > Semantic Search and set its embedding endpoint and key."),
					*FPaths::ConvertRelativePathToFull(ConfigPath));
			}
			return Out;
		}
#endif
		Out.Name = FString::Printf(TEXT("Unknown(%d)"), static_cast<int32>(S->Provider));
		Out.Note = TEXT("The configured provider is not one this bridge knows how to check");
		return Out;
	}

	/** The plugin's id for an asset: the first eight bytes of the BLAKE3 of its object path, sign bit cleared. */
	static int64 AssetIndexId(const FString& ObjectPath)
	{
		FBlake3 Hasher;
		Hasher.Update(*ObjectPath, ObjectPath.Len() * sizeof(TCHAR));
		const FBlake3Hash Hash = Hasher.Finalize();
		int64 Result;
		FMemory::Memcpy(&Result, Hash.GetBytes(), sizeof(int64));
		return Result & 0x7FFFFFFFFFFFFFFF;
	}

	/** Every asset under the folders the plugin indexes, deduplicated, as it enumerates them. */
	static TArray<FAssetData> AssetsInIndexedFolders(const USemanticSearchSettings* S)
	{
		TArray<FAssetData> Out;
		if (!S) return Out;
		IAssetRegistry& Registry = IAssetRegistry::GetChecked();
		TSet<FSoftObjectPath> Seen;
		for (const FDirectoryPath& Dir : S->IndexedPaths)
		{
			TArray<FAssetData> Found;
			Registry.GetAssetsByPath(FName(*Dir.Path), Found, /*bRecursive=*/true);
			for (FAssetData& Asset : Found)
			{
				bool bAlready = false;
				Seen.Add(Asset.GetSoftObjectPath(), &bAlready);
				if (!bAlready) Out.Add(MoveTemp(Asset));
			}
		}
		return Out;
	}

	static TArray<TSharedPtr<FJsonValue>> IndexedPathValues(const USemanticSearchSettings* S)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		if (S)
		{
			for (const FDirectoryPath& Dir : S->IndexedPaths) Out.Add(MakeShared<FJsonValueString>(Dir.Path));
		}
		return Out;
	}

	static TSharedPtr<FJsonObject> StatsObject(const FSemanticSearchIndexStats& Stats)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("indexType"), Stats.IndexType == ESemanticSearchIndexType::PQ ? TEXT("PQ") : TEXT("Flat"));
		Obj->SetNumberField(TEXT("vectorCount"), Stats.VectorCount);
		Obj->SetNumberField(TEXT("keywordCount"), Stats.BM25Count);
		Obj->SetNumberField(TEXT("dimension"), Stats.Dimension);
		Obj->SetBoolField(TEXT("trained"), Stats.bIsTrained);
		Obj->SetBoolField(TEXT("initialized"), Stats.bIsInitialized);
		Obj->SetNumberField(TEXT("supportedAssetCount"), Stats.SupportedAssetCount);
		Obj->SetNumberField(TEXT("failedCount"), Stats.FailedCount);
		Obj->SetNumberField(TEXT("permanentlyFailedCount"), Stats.PreProcessorFailedCount);
		Obj->SetBoolField(TEXT("indexing"), Stats.bIsIndexing);
		return Obj;
	}

	/** Null when a provider-backed action can run; otherwise the refusal naming what is missing. */
	static TSharedPtr<FJsonValue> RefuseUnlessReady(ISemanticSearchModule*& OutModule, USemanticSearchSettings*& OutSettings)
	{
		OutModule = Module();
		if (!OutModule) return MCPErrorWithCode(TEXT("PLUGIN_NOT_AVAILABLE"), PluginOff);
		OutSettings = Settings();
		const FProviderState Provider = ReadProvider(OutSettings);
		if (!Provider.bConfigured || !OutModule->GetEmbeddingProvider().IsValid())
		{
			TSharedPtr<FJsonObject> Err = MCPErrorObject(FString::Printf(
				TEXT("Semantic Search has no usable embedding provider: %s"),
				Provider.Note.IsEmpty() ? TEXT("the module has no provider registered") : *Provider.Note));
			Err->SetStringField(TEXT("errorCode"), TEXT("semantic_search_provider_not_configured"));
			Err->SetStringField(TEXT("provider"), Provider.Name);
			return MCPResult(Err);
		}
		if (!OutModule->IsInitialized())
		{
			return MCPErrorWithCode(TEXT("semantic_search_not_initialized"),
				TEXT("Semantic Search has not initialized its index (SemanticSearch.DelayInitialization is set and nothing called Initialize yet)"));
		}
		return nullptr;
	}
}
#endif

TSharedPtr<FJsonValue> FAssetHandlers::SemanticSearchStatus(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Res = MCPSuccess();
#if !WITH_UEMCP_SEMANTIC_SEARCH
	Res->SetBoolField(TEXT("available"), false);
	Res->SetStringField(TEXT("errorCode"), TEXT("unsupported_engine_version"));
	Res->SetStringField(TEXT("reason"), TEXT("Semantic Search requires Unreal Engine 5.8 or newer, where the plugin first ships"));
	return MCPResult(Res);
#else
	using namespace MCPSemanticSearch;
	ISemanticSearchModule* Mod = Module();
	USemanticSearchSettings* S = Settings();
	Res->SetBoolField(TEXT("pluginLoaded"), Mod != nullptr);
	if (!Mod)
	{
		Res->SetBoolField(TEXT("available"), false);
		Res->SetStringField(TEXT("errorCode"), TEXT("PLUGIN_NOT_AVAILABLE"));
		Res->SetStringField(TEXT("reason"), PluginOff);
		return MCPResult(Res);
	}
	const FProviderState Provider = ReadProvider(S);
	const TSharedPtr<IEmbeddingProvider> Embedder = Mod->GetEmbeddingProvider();
	const bool bInitialized = Mod->IsInitialized();
	Res->SetStringField(TEXT("provider"), Provider.Name);
	Res->SetBoolField(TEXT("providerConfigured"), Provider.bConfigured);
	Res->SetBoolField(TEXT("providerRegistered"), Embedder.IsValid());
	if (!Provider.Note.IsEmpty()) Res->SetStringField(TEXT("providerNote"), Provider.Note);
	if (Embedder.IsValid()) Res->SetNumberField(TEXT("embeddingDimension"), Embedder->GetEmbeddingDimension());
	Res->SetBoolField(TEXT("initialized"), bInitialized);
	Res->SetBoolField(TEXT("indexingInProgress"), Mod->IsIndexingInProgress());
	Res->SetArrayField(TEXT("indexedPaths"), IndexedPathValues(S));
	if (S) Res->SetBoolField(TEXT("autoIndexOnSave"), S->bAutoIndexOnSave);
	Res->SetObjectField(TEXT("stats"), StatsObject(Mod->GetIndexStats()));
	const bool bAvailable = Provider.bConfigured && Embedder.IsValid() && bInitialized;
	Res->SetBoolField(TEXT("available"), bAvailable);
	if (!bAvailable)
	{
		Res->SetStringField(TEXT("errorCode"), !Provider.bConfigured || !Embedder.IsValid()
			? TEXT("semantic_search_provider_not_configured") : TEXT("semantic_search_not_initialized"));
		Res->SetStringField(TEXT("reason"), !Provider.Note.IsEmpty() ? Provider.Note
			: !Embedder.IsValid() ? FString(TEXT("The module has no embedding provider registered"))
			: FString(TEXT("The index has not initialized")));
	}
	return MCPResult(Res);
#endif
}

TSharedPtr<FJsonValue> FAssetHandlers::SemanticSearchQuery(const TSharedPtr<FJsonObject>& Params)
{
	FString Query;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("query"), Query)) return Err;
	const int32 Limit = FMath::Clamp(OptionalInt(Params, TEXT("limit"), 20), 1, 200);
	const FString Directory = OptionalString(Params, TEXT("directory"), TEXT(""));
	const double MaxDistanceParam = OptionalNumber(Params, TEXT("maxDistance"), -1.0);
#if !WITH_UEMCP_SEMANTIC_SEARCH
	(void)Limit; (void)Directory; (void)MaxDistanceParam;
	return MCPUnsupportedEngineError(TEXT("asset(semantic_search)"), TEXT("5.8"), TEXT("Epic's Semantic Search plugin first ships in 5.8"));
#else
	using namespace MCPSemanticSearch;
	ISemanticSearchModule* Mod = nullptr;
	USemanticSearchSettings* S = nullptr;
	if (TSharedPtr<FJsonValue> Refusal = RefuseUnlessReady(Mod, S)) return Refusal;

	// Results come back as ids; the map turns them into assets, and a directory narrows the search to its assets.
	TSharedRef<TMap<int64, FAssetData>> ById = MakeShared<TMap<int64, FAssetData>>();
	TArray<int64> IdFilter;
	const FString DirPrefix = Directory.IsEmpty() ? FString() : (Directory.EndsWith(TEXT("/")) ? Directory : Directory + TEXT("/"));
	for (FAssetData& Asset : AssetsInIndexedFolders(S))
	{
		const int64 Id = AssetIndexId(Asset.GetObjectPathString());
		if (!DirPrefix.IsEmpty())
		{
			if (!(Asset.PackagePath.ToString() + TEXT("/")).StartsWith(DirPrefix)) continue;
			IdFilter.Add(Id);
		}
		ById->Add(Id, MoveTemp(Asset));
	}
	if (!DirPrefix.IsEmpty() && IdFilter.Num() == 0)
	{
		return MCPError(FString::Printf(TEXT("No assets under '%s' are in the folders Semantic Search indexes"), *Directory));
	}

	const float Cutoff = MaxDistanceParam > 0.0 ? static_cast<float>(MaxDistanceParam) : S->VectorDistanceCutoff;
	const FMCPDeferredCallPtr Deferred = MCPDeferRequest();
	if (!Deferred) return MCPError(TEXT("asset(semantic_search) answers asynchronously, and only a bridge request can wait for it"));
	Deferred->SetCancelNote(TEXT("The embedding request and the index search have no cancel hook; their answer is dropped."));

	const double Started = FPlatformTime::Seconds();
	TSharedPtr<IEmbeddingProvider> Embedder = Mod->GetEmbeddingProvider();
	Embedder->GenerateEmbeddingAsync(Query,
		[Deferred, Query, Limit, Cutoff, ById, IdFilter = MoveTemp(IdFilter), Started, Embedder](FEmbeddingResponse&& Response) mutable
		{
			// The provider may answer on an HTTP thread; the module is driven from the game thread.
			AsyncTask(ENamedThreads::GameThread, [Deferred, Query, Limit, Cutoff, ById, IdFilter = MoveTemp(IdFilter), Started,
				Response = MoveTemp(Response)]() mutable
			{
				if (Deferred->IsCancelled()) return;
				if (!Response.ErrorMessage.IsEmpty())
				{
					TSharedPtr<FJsonObject> Err = MCPErrorObject(FString::Printf(
						TEXT("The embedding provider could not embed the query: %s"), *Response.ErrorMessage));
					Err->SetStringField(TEXT("errorCode"), TEXT("semantic_search_embedding_failed"));
					Deferred->Complete(MCPResult(Err));
					return;
				}
				ISemanticSearchModule* Mod = Module();
				if (!Mod)
				{
					Deferred->Complete(MCPErrorWithCode(TEXT("PLUGIN_NOT_AVAILABLE"), PluginOff));
					return;
				}
				const int32 Dimension = Response.Embedding.Num();
				Mod->SearchAsync(Query, Response.Embedding, Limit, IdFilter, Cutoff,
					[Deferred, Query, ById, Started, Dimension, Cutoff](TArray<FHybridSearchResult>&& Results)
					{
						if (Deferred->IsCancelled()) return;
						TArray<TSharedPtr<FJsonValue>> Rows;
						for (const FHybridSearchResult& R : Results)
						{
							TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
							if (const FAssetData* Asset = ById->Find(R.ID))
							{
								Row->SetStringField(TEXT("objectPath"), Asset->GetObjectPathString());
								Row->SetStringField(TEXT("name"), Asset->AssetName.ToString());
								Row->SetStringField(TEXT("class"), Asset->AssetClassPath.GetAssetName().ToString());
							}
							else
							{
								// Indexed under a path no longer in the indexed folders.
								Row->SetStringField(TEXT("id"), LexToString(R.ID));
							}
							Row->SetNumberField(TEXT("score"), R.RRFScore);
							if (R.VectorDistance >= 0.0f) Row->SetNumberField(TEXT("vectorDistance"), R.VectorDistance);
							if (R.BM25Score >= 0.0f) Row->SetNumberField(TEXT("keywordScore"), R.BM25Score);
							Rows.Add(MakeShared<FJsonValueObject>(Row));
						}
						TSharedPtr<FJsonObject> Res = MCPSuccess();
						Res->SetStringField(TEXT("query"), Query);
						Res->SetNumberField(TEXT("count"), Rows.Num());
						Res->SetArrayField(TEXT("results"), Rows);
						Res->SetNumberField(TEXT("embeddingDimension"), Dimension);
						Res->SetNumberField(TEXT("maxDistance"), Cutoff);
						Res->SetNumberField(TEXT("elapsedMs"), (FPlatformTime::Seconds() - Started) * 1000.0);
						Deferred->Complete(MCPResult(Res));
					});
			});
		}, nullptr);
	return MCPDeferredMarker();
#endif
}

TSharedPtr<FJsonValue> FAssetHandlers::SemanticSearchIndex(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
	const bool bHasPaths = TryGetArrayParam(Params, TEXT("assetPaths"), Paths) && Paths && Paths->Num() > 0;
	const bool bForce = OptionalBool(Params, TEXT("force"), false);
#if !WITH_UEMCP_SEMANTIC_SEARCH
	(void)bHasPaths; (void)bForce;
	return MCPUnsupportedEngineError(TEXT("asset(semantic_search_index)"), TEXT("5.8"), TEXT("Epic's Semantic Search plugin first ships in 5.8"));
#else
	using namespace MCPSemanticSearch;
	ISemanticSearchModule* Mod = nullptr;
	USemanticSearchSettings* S = nullptr;
	if (TSharedPtr<FJsonValue> Refusal = RefuseUnlessReady(Mod, S)) return Refusal;

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	if (bHasPaths)
	{
		IAssetRegistry& Registry = IAssetRegistry::GetChecked();
		TArray<FAssetData> Assets;
		TArray<TSharedPtr<FJsonValue>> Missing;
		for (const TSharedPtr<FJsonValue>& V : *Paths)
		{
			FString Path;
			if (!V.IsValid() || !V->TryGetString(Path) || Path.IsEmpty()) continue;
			// Accept a package path as well as an object path.
			FString ObjectPath = Path;
			if (!ObjectPath.Contains(TEXT(".")))
			{
				ObjectPath = Path + TEXT(".") + FPackageName::GetShortName(Path);
			}
			const FAssetData Asset = Registry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
			if (Asset.IsValid()) Assets.Add(Asset);
			else Missing.Add(MakeShared<FJsonValueString>(Path));
		}
		if (Missing.Num() > 0)
		{
			TSharedPtr<FJsonObject> Err = MCPErrorObject(TEXT("Some assetPaths name no asset in the Asset Registry; nothing was queued"));
			Err->SetArrayField(TEXT("missing"), Missing);
			return MCPResult(Err);
		}
		Mod->IndexAssets(Assets);
		Res->SetStringField(TEXT("scope"), TEXT("assets"));
		Res->SetNumberField(TEXT("queuedAssetCount"), Assets.Num());
	}
	else
	{
		Mod->IndexAllAssets(bForce);
		Res->SetStringField(TEXT("scope"), TEXT("all"));
		Res->SetBoolField(TEXT("force"), bForce);
		Res->SetArrayField(TEXT("indexedPaths"), IndexedPathValues(S));
	}
	Res->SetBoolField(TEXT("started"), true);
	Res->SetBoolField(TEXT("indexingInProgress"), Mod->IsIndexingInProgress());
	Res->SetObjectField(TEXT("stats"), StatsObject(Mod->GetIndexStats()));
	MCPSetIdempotencyUnobservable(Res, TEXT("The module queues the work and reports no per-asset outcome; an asset whose embedding is already "
		"cached is read back from the derived-data cache rather than indexed twice, so a repeat is safe but whether it changed the index cannot be read here."));
	MCPSetNoRollback(Res, TEXT("Indexing adds embeddings to Semantic Search's own index and derived-data cache; there is no call that takes an asset back out of it. "
		"Running it again re-reads cached embeddings rather than duplicating them."));
	return MCPResult(Res);
#endif
}

void FAssetHandlers::RegisterSemanticSearchHandlers(FMCPHandlerRegistry& Registry)
{
	using EType = EMCPParamType;
	Registry.RegisterHandler(TEXT("semantic_search_status"), &SemanticSearchStatus, {});
	// The query is embedded by a remote provider before the index is searched.
	Registry.RegisterHandlerWithTimeout(TEXT("semantic_search"), &SemanticSearchQuery, 120.0f, {
		MCPParam::Required(TEXT("query"), EType::String, TEXT("Natural-language description of the assets to find")),
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Most results to return, 1 to 200 (default 20)")),
		MCPParam::Optional(TEXT("directory"), EType::String, TEXT("Content folder to search within, e.g. /Game/Props (default every indexed folder)")),
		MCPParam::Optional(TEXT("maxDistance"), EType::Number, TEXT("Largest vector distance a match may have (default the plugin's VectorDistanceCutoff)")),
	});
	Registry.RegisterHandler(TEXT("semantic_search_index"), &SemanticSearchIndex, {
		MCPParam::Optional(TEXT("assetPaths"), EType::Array, TEXT("Object or package paths to index (default every asset in the indexed folders)")).Items(EType::String),
		MCPParam::Optional(TEXT("force"), EType::Boolean, TEXT("With no assetPaths: clear the index and rebuild every embedding instead of reusing cached ones (default false)")),
	});
}
