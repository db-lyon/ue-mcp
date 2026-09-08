#include "FabHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "Modules/ModuleManager.h"
#include "UObject/UObjectHash.h"
#include "Misc/PackageName.h"
#include "Engine/Engine.h"

#ifndef WITH_FAB_PLUGIN
#define WITH_FAB_PLUGIN 0
#endif

#if WITH_FAB_PLUGIN
#include "Importers/GenericAssetImporter.h"
#include "Utilities/FabAssetsCache.h"
#endif

// ─── Helpers ──────────────────────────────────────────────────────────────

static const TCHAR* FabModuleName = TEXT("Fab");

// The Fab plugin is an engine editor plugin, enabled by default on 5.8 but not
// guaranteed to be present or loaded (disabled, or an older engine). Every
// action funnels through this so callers get a clear message instead of a
// crash or an "Unknown method".
static bool IsFabModuleLoaded()
{
	return FModuleManager::Get().IsModuleLoaded(FabModuleName);
}

// Run one of the Fab plugin's registered console commands (Fab.Login,
// Fab.Logout, Fab.ClearCache, Fab.TEDS.MyFolderIntegration, ...). These are
// FAutoConsoleCommands, so they route through GEngine->Exec without any
// compile-time dependency on the Fab module - the login/sync/clear paths work
// even when WITH_FAB_PLUGIN is off (as long as the module is loaded).
static bool RunFabConsoleCommand(const FString& Command)
{
	if (!GEngine) return false;
	return GEngine->Exec(nullptr, *Command);
}

// Best-effort login/window hint: whether a UFabBrowserApi (the JS<->native
// bridge object created with the Fab window) currently exists. Reached by
// reflection so we never link the plugin's Private headers. Not a definitive
// auth check - the real login state lives in EOS behind private symbols - but
// a useful "has the Fab window been opened this session" signal.
static bool HasFabBrowserApiInstance()
{
	UClass* ApiClass = FindObject<UClass>(nullptr, TEXT("/Script/Fab.FabBrowserApi"));
	if (!ApiClass) return false;
	TArray<UObject*> Instances;
	GetObjectsOfClass(ApiClass, Instances, /*bIncludeDerivedClasses=*/true, RF_ClassDefaultObject);
	return Instances.Num() > 0;
}

// ─── Actions ──────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FFabHandlers::Status(const TSharedPtr<FJsonObject>& Params)
{
	auto Res = MCPSuccess();
	const bool bLoaded = IsFabModuleLoaded();
	Res->SetBoolField(TEXT("pluginLoaded"), bLoaded);
	// Compile-time link state: whether cache/import actions are backed by the
	// native Fab API in this build, vs. console-command-only.
	Res->SetBoolField(TEXT("nativeApiLinked"), WITH_FAB_PLUGIN ? true : false);
	Res->SetBoolField(TEXT("fabWindowOpened"), HasFabBrowserApiInstance());

	if (!bLoaded)
	{
		Res->SetStringField(TEXT("note"), TEXT("Fab plugin module not loaded. It ships enabled by default on UE 5.8; enable it in the editor's Plugins panel if missing."));
		return MCPResult(Res);
	}

#if WITH_FAB_PLUGIN
	Res->SetStringField(TEXT("cacheLocation"), FFabAssetsCache::GetCacheLocation());
	Res->SetNumberField(TEXT("cacheSizeBytes"), (double)FFabAssetsCache::GetCacheSize());
	Res->SetStringField(TEXT("cacheSize"), FFabAssetsCache::GetCacheSizeString().ToString());
#endif
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FFabHandlers::Login(const TSharedPtr<FJsonObject>& Params)
{
	if (!IsFabModuleLoaded()) return MCPError(TEXT("Fab plugin not loaded"));
	// Fab.Login opens the EOS account-portal login flow. Asynchronous: this
	// returns once the flow is triggered, not once the user has authenticated.
	if (!RunFabConsoleCommand(TEXT("Fab.Login")))
		return MCPError(TEXT("Failed to invoke Fab.Login console command"));
	auto Res = MCPSuccess();
	Res->SetStringField(TEXT("note"), TEXT("Login flow triggered. Complete authentication in the account-portal window if prompted; call fab(status) afterward."));
	// No idempotency flag, and the reason is stated rather than left as an
	// absence: the Fab module publishes no authentication or library-sync state
	// this build can read, so nothing here can measure whether the call changed
	// anything. A fabricated unchanged=false would be a claim, not a reading.
	Res->SetBoolField(TEXT("idempotencyObservable"), false);
	Res->SetStringField(TEXT("idempotencyNote"),
		TEXT("Whether an account was already signed in, and whether this flow ends in one being signed in, are both decided outside this call. Nothing in the Fab module reports that state back, so this call cannot say whether it changed "
			 "anything. fab(status) is the closest reading available and does not cover it."));

	// No inverse. This opens the EOS account-portal flow and returns before
	// anything has authenticated, so it creates no state of its own to undo.
	// fab(logout) is not the inverse: it would clear whatever session the user
	// already had, including one this call had nothing to do with.
	Res->SetBoolField(TEXT("rollbackPossible"), false);
	Res->SetStringField(TEXT("rollbackNote"),
		TEXT("Triggering the login flow creates no state this call owns - the user authenticates, or does not, in the "
			 "account portal. fab(logout) would clear a session that may predate this call, so it is not offered as "
			 "an inverse."));
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FFabHandlers::Logout(const TSharedPtr<FJsonObject>& Params)
{
	if (!IsFabModuleLoaded()) return MCPError(TEXT("Fab plugin not loaded"));
	if (!RunFabConsoleCommand(TEXT("Fab.Logout")))
		return MCPError(TEXT("Failed to invoke Fab.Logout console command"));
	auto Res = MCPSuccess();
	Res->SetStringField(TEXT("note"), TEXT("Persistent Fab auth cleared."));
	// No idempotency flag, and the reason is stated rather than left as an
	// absence: the Fab module publishes no authentication or library-sync state
	// this build can read, so nothing here can measure whether the call changed
	// anything. A fabricated unchanged=false would be a claim, not a reading.
	Res->SetBoolField(TEXT("idempotencyObservable"), false);
	Res->SetStringField(TEXT("idempotencyNote"),
		TEXT("Whether a session was there to clear is not readable from here. Nothing in the Fab module reports that state back, so this call cannot say whether it changed "
			 "anything. fab(status) is the closest reading available and does not cover it."));

	// No inverse. Logging back in means the user authenticating in the EOS
	// account portal; fab(login) only opens that window and cannot restore the
	// credentials this call destroyed.
	Res->SetBoolField(TEXT("rollbackPossible"), false);
	Res->SetStringField(TEXT("rollbackNote"),
		TEXT("The stored credentials are gone. fab(login) opens the account-portal flow for a person to complete; no "
			 "call restores a cleared session, so this has no inverse."));
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FFabHandlers::SyncLibrary(const TSharedPtr<FJsonObject>& Params)
{
	if (!IsFabModuleLoaded()) return MCPError(TEXT("Fab plugin not loaded"));
	// Loads the user's owned Fab library ("My Folder") into TEDS so it surfaces
	// in the Content Browser. Optional batch size controls how many items are
	// pulled per sync request.
	const int32 BatchSize = OptionalInt(Params, TEXT("batchSize"), 0);
	const FString Command = BatchSize > 0
		? FString::Printf(TEXT("Fab.TEDS.MyFolderIntegration %d"), BatchSize)
		: FString(TEXT("Fab.TEDS.MyFolderIntegration"));
	if (!RunFabConsoleCommand(Command))
		return MCPError(TEXT("Failed to invoke Fab.TEDS.MyFolderIntegration console command"));
	auto Res = MCPSuccess();
	Res->SetStringField(TEXT("note"), TEXT("Library sync queued. Owned Fab items load into the Content Browser asynchronously; requires an active Fab login."));
	// No idempotency flag, and the reason is stated rather than left as an
	// absence: the Fab module publishes no authentication or library-sync state
	// this build can read, so nothing here can measure whether the call changed
	// anything. A fabricated unchanged=false would be a claim, not a reading.
	Res->SetBoolField(TEXT("idempotencyObservable"), false);
	Res->SetStringField(TEXT("idempotencyNote"),
		TEXT("The sync runs asynchronously and its results land in the Content Browser after this call has returned. Nothing in the Fab module reports that state back, so this call cannot say whether it changed "
			 "anything. fab(status) is the closest reading available and does not cover it."));

	// No inverse. This loads what the user already owns into the editor's TEDS
	// index so the Content Browser can show it. Nothing un-lists a library, and
	// the listing is rebuilt from the account on the next sync anyway.
	Res->SetBoolField(TEXT("rollbackPossible"), false);
	Res->SetStringField(TEXT("rollbackNote"),
		TEXT("Syncing populates an in-editor index of the library the account already owns. There is no call that "
			 "un-lists it, and nothing on disk or in the project changed."));
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FFabHandlers::ListCached(const TSharedPtr<FJsonObject>& Params)
{
	if (!IsFabModuleLoaded()) return MCPError(TEXT("Fab plugin not loaded"));
#if WITH_FAB_PLUGIN
	auto Res = MCPSuccess();
	const TArray<FString> Cached = FFabAssetsCache::GetCachedAssets();
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FString& Entry : Cached)
	{
		Arr.Add(MakeShared<FJsonValueString>(Entry));
	}
	Res->SetArrayField(TEXT("cachedAssets"), Arr);
	Res->SetNumberField(TEXT("count"), Cached.Num());
	Res->SetStringField(TEXT("cacheLocation"), FFabAssetsCache::GetCacheLocation());
	return MCPResult(Res);
#else
	return MCPError(TEXT("Fab native API not linked in this build (Fab plugin absent at build time). Cache listing unavailable."));
#endif
}

TSharedPtr<FJsonValue> FFabHandlers::CacheInfo(const TSharedPtr<FJsonObject>& Params)
{
	if (!IsFabModuleLoaded()) return MCPError(TEXT("Fab plugin not loaded"));
#if WITH_FAB_PLUGIN
	auto Res = MCPSuccess();
	Res->SetStringField(TEXT("cacheLocation"), FFabAssetsCache::GetCacheLocation());
	Res->SetNumberField(TEXT("cacheSizeBytes"), (double)FFabAssetsCache::GetCacheSize());
	Res->SetStringField(TEXT("cacheSize"), FFabAssetsCache::GetCacheSizeString().ToString());
	Res->SetNumberField(TEXT("count"), FFabAssetsCache::GetCachedAssets().Num());
	return MCPResult(Res);
#else
	return MCPError(TEXT("Fab native API not linked in this build (Fab plugin absent at build time). Cache info unavailable."));
#endif
}

TSharedPtr<FJsonValue> FFabHandlers::ClearCache(const TSharedPtr<FJsonObject>& Params)
{
	if (!IsFabModuleLoaded()) return MCPError(TEXT("Fab plugin not loaded"));

	// Read the cache size first so the result can say whether this call actually
	// deleted anything. Only available when the native API is linked; without it
	// the console command is still the right thing to run, there is just nothing
	// to measure it against.
	int64 PreviousBytes = -1;
#if WITH_FAB_PLUGIN
	PreviousBytes = (int64)FFabAssetsCache::GetCacheSize();
#endif

	// Routed through the console command so this works without WITH_FAB_PLUGIN.
	if (!RunFabConsoleCommand(TEXT("Fab.ClearCache")))
		return MCPError(TEXT("Failed to invoke Fab.ClearCache console command"));
	auto Res = MCPSuccess();
	Res->SetStringField(TEXT("note"), TEXT("Fab download cache cleared."));
	if (PreviousBytes >= 0)
	{
		Res->SetNumberField(TEXT("previousCacheSizeBytes"), (double)PreviousBytes);
		Res->SetBoolField(TEXT("unchanged"), PreviousBytes == 0);
	}
	// No inverse. The cached downloads are deleted from disk and nothing puts
	// them back: they return only by downloading them from Fab again, which is
	// a fresh transfer rather than a restoration.
	Res->SetBoolField(TEXT("rollbackPossible"), false);
	Res->SetStringField(TEXT("rollbackNote"),
		TEXT("The cached downloads are deleted from disk. No call restores them; they come back only by downloading "
			 "the assets from Fab again. Nothing in the project changed - the cache is a download staging area."));
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FFabHandlers::ImportFile(const TSharedPtr<FJsonObject>& Params)
{
	if (!IsFabModuleLoaded()) return MCPError(TEXT("Fab plugin not loaded"));
#if WITH_FAB_PLUGIN
	FString Source;
	if (auto Err = RequireStringAlt(Params, TEXT("source"), TEXT("sourceFile"), Source)) return Err;
	FString Destination;
	if (auto Err = RequireStringAlt(Params, TEXT("destination"), TEXT("destPath"), Destination)) return Err;

	if (!FPaths::FileExists(Source))
		return MCPError(FString::Printf(TEXT("Source file not found on disk: %s"), *Source));
	if (!Destination.StartsWith(TEXT("/")))
		return MCPError(FString::Printf(TEXT("Destination must be a content path like /Game/Fab/Imported (got '%s')"), *Destination));
	if (!FPackageName::IsValidLongPackageName(Destination, /*bIncludeReadOnlyRoots=*/true))
		return MCPError(FString::Printf(TEXT("Destination is not a valid content path: %s"), *Destination));

	// FFabGenericImporter::ImportAsset runs the Fab Interchange pipeline and
	// delivers the created objects via callback. Single source files (fbx,
	// textures) complete on the game thread synchronously, so the callback has
	// fired by the time ImportAsset returns; pack/quixel workflows can defer.
	// We capture whatever the callback produced and report accordingly rather
	// than blocking the game thread.
	TSharedRef<bool> bCompleted = MakeShared<bool>(false);
	TSharedRef<TArray<FString>> ImportedPaths = MakeShared<TArray<FString>>();

	FFabGenericImporter::ImportAsset(
		{ Source },
		Destination,
		[bCompleted, ImportedPaths](const TArray<UObject*>& Objects)
		{
			*bCompleted = true;
			for (const UObject* Obj : Objects)
			{
				if (Obj) ImportedPaths->Add(Obj->GetPathName());
			}
		});

	auto Res = MCPSuccess();
	Res->SetStringField(TEXT("source"), Source);
	Res->SetStringField(TEXT("destination"), Destination);
	if (*bCompleted)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& P : *ImportedPaths)
		{
			Arr.Add(MakeShared<FJsonValueString>(P));
		}
		Res->SetArrayField(TEXT("importedAssets"), Arr);
		Res->SetNumberField(TEXT("count"), ImportedPaths->Num());
		MCPSetCreated(Res);

		if (ImportedPaths->Num() > 0)
		{
			// The inverse deletes exactly the assets this import wrote, which is
			// only expressible because the callback handed back their paths.
			// force is true because the imported set references itself - a
			// material referencing its textures - and a reference check would
			// refuse to delete the half that is still pointed at.
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetArrayField(TEXT("assetPaths"), Arr);
			Payload->SetBoolField(TEXT("force"), true);
			MCPSetRollback(Res, TEXT("delete_asset_batch"), Payload);
			Res->SetBoolField(TEXT("rollbackLossy"), false);
		}
		else
		{
			Res->SetBoolField(TEXT("unchanged"), true);
			Res->SetBoolField(TEXT("rollbackPossible"), false);
			Res->SetStringField(TEXT("rollbackNote"),
				TEXT("The import produced no objects, so nothing was written and there is nothing to delete."));
		}
	}
	else
	{
		// Async workflow (pack extraction, quixel gltf, plugin install): the
		// import is running and will land in Destination shortly.
		Res->SetBoolField(TEXT("async"), true);
		Res->SetStringField(TEXT("note"), FString::Printf(TEXT("Import running asynchronously into %s; poll asset(list) on that path to confirm."), *Destination));
		// No inverse for this branch. The importer has not reported what it
		// created yet, and a record naming the destination FOLDER would delete
		// whatever else already lives there. Read the path afterwards and delete
		// deliberately instead.
		Res->SetBoolField(TEXT("rollbackPossible"), false);
		Res->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The import is still running, so the paths it writes are not known yet and no inverse can name them. "
				 "A record targeting '%s' would delete assets that were already there. List the destination after the "
				 "import lands and delete what it added with asset(delete_batch)."),
			*Destination));
	}
	return MCPResult(Res);
#else
	return MCPError(TEXT("Fab native API not linked in this build (Fab plugin absent at build time). Import unavailable."));
#endif
}

// ─── Registration ──────────────────────────────────────────────────────────

void FFabHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("fab_status"), &Status);
	Registry.RegisterHandler(TEXT("fab_login"), &Login);
	Registry.RegisterHandler(TEXT("fab_logout"), &Logout);
	Registry.RegisterHandler(TEXT("fab_sync_library"), &SyncLibrary);
	Registry.RegisterHandler(TEXT("fab_list_cached"), &ListCached);
	Registry.RegisterHandler(TEXT("fab_cache_info"), &CacheInfo);
	Registry.RegisterHandler(TEXT("fab_clear_cache"), &ClearCache);
	Registry.RegisterHandler(TEXT("fab_import_file"), &ImportFile);
}
