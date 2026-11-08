#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

// Native Fab account lifecycle, owned library synchronization, resumable UE
// asset-pack downloads, cache inspection and Content import. The Epic Games
// Launcher installs the Fab editor plugin into Engine/Plugins/Fab as its own
// "Fab UE Plugin" item, not as part of the engine (#1889). Catalog search and
// purchases stay in the Fab website: Fab publishes no search API and its terms
// forbid automated access to fab.com, so open_search only opens that page (#1485).
class FFabHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

	/** Opens a URL, writing a failure to OutError. fab_open_search's launcher. */
	using FUrlLauncher = TFunction<void(const FString& Url, FString& OutError)>;

	/** Test seam: replaces FPlatformProcess::LaunchURL for fab_open_search. An unset function restores it. */
	static void SetUrlLauncherForTesting(FUrlLauncher Launcher);

private:
	static TSharedPtr<FJsonValue> Status(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> Login(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> Logout(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SyncLibrary(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListCached(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CacheInfo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ClearCache(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ImportFile(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> OpenSearch(const TSharedPtr<FJsonObject>& Params);

	static TSharedPtr<FJsonValue> ListOwned(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> Download(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DownloadStatus(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PauseDownload(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ResumeDownload(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CancelDownload(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> InstallPack(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveInstallation(const TSharedPtr<FJsonObject>& Params);

	static FUrlLauncher UrlLauncherOverride;
};
