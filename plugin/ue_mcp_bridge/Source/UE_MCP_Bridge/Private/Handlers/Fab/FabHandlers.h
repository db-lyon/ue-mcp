#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

// Fab asset importer. Fab is Epic's unified content marketplace; the engine
// ships an editor plugin (/Engine/Plugins/Fab) whose window is a web frontend
// (catalog browse, library, purchase, signed-URL resolution all live server
// side) sitting on top of a thin native download+import layer. This category
// drives the parts of that native layer that don't need the web frontend:
// login lifecycle, syncing the user's owned library into the Content Browser,
// inspecting/clearing the download cache, and importing owned/local source
// files into the project via the Fab import pipeline.
//
// Store catalog browsing and signed-URL resolution are deliberately out of
// scope for this v1: they require the authenticated Fab backend (no official
// consumer REST API exists), so they stay in the web window.
class FFabHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> Status(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> Login(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> Logout(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SyncLibrary(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListCached(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CacheInfo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ClearCache(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ImportFile(const TSharedPtr<FJsonObject>& Params);
};
