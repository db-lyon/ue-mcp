#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

// Fab asset importer. Fab is Epic's unified content marketplace; the Epic
// Games Launcher installs its editor plugin into Engine/Plugins/Fab as a
// separate "Fab UE Plugin" item, not as part of the engine (#1889). Its window is a web frontend
// (catalog browse, library, purchase, signed-URL resolution all live server
// side) sitting on top of a thin native download+import layer. This category
// drives the parts of that native layer that don't need the web frontend:
// login lifecycle, syncing the user's owned library into the Content Browser,
// inspecting/clearing the download cache, and importing owned/local source
// files into the project via the Fab import pipeline.
//
// Store catalog browsing and signed-URL resolution are out of scope: Fab
// publishes no search API, and its terms forbid automated access to fab.com.
// fab_open_search only hands the user fab.com's own search page (#1485).
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

	static FUrlLauncher UrlLauncherOverride;
};
