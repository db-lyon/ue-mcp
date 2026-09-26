#pragma once

// Which world a handler acts on: the editor world, a play-in-editor world by
// instance, and the refusal an editor-world action gives while play is running.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HandlerResult.h"
#include "HandlerParams.h"

/** Is a play-in-editor or simulate session running right now?
 *
 *  The editor's asset loader refuses every call in play mode and logs the
 *  reason where a bridge caller cannot see it (#1065). */
inline bool MCPIsPlayInEditorActive()
{
	return GEditor && (GEditor->PlayWorld != nullptr || GEditor->bIsSimulatingInEditor);
}

/** Appended to a load failure while PIE is running, empty otherwise. One
 *  wording, shared by every resolver. */
inline FString MCPPlayInEditorLoadNote()
{
	if (!MCPIsPlayInEditorActive()) return FString();
	return TEXT(" A play-in-editor session is running, which changes how assets load: ")
		TEXT("the editor's own asset loader refuses every call while in play mode. ")
		TEXT("Stop play with editor(action=\"stop_pie\") and retry before treating this as an asset problem.");
}

/** A precondition refusal for an editor-world action while PIE is running,
 *  nullptr otherwise. Without it the load fails as "not found" (#1098). */
inline TSharedPtr<FJsonValue> MCPRefuseDuringPlayInEditor(const TCHAR* ActionName)
{
	if (!MCPIsPlayInEditorActive()) return nullptr;
	return MCPError(FString::Printf(
		TEXT("%s cannot run while a play-in-editor session is running: the editor's asset loader refuses every call in play mode, ")
		TEXT("so the sequence would read as missing and captures would not follow the playhead. ")
		TEXT("Stop play with editor(action=\"stop_pie\") and retry."),
		ActionName));
}

/** Get the editor world, or nullptr if not available. */
inline UWorld* GetEditorWorld()
{
	if (!GEditor) return nullptr;
	return GEditor->GetEditorWorldContext().World();
}

/** Get the active PIE/Game world if one is running, or nullptr. */
inline UWorld* GetPIEWorld()
{
	if (!GEngine) return nullptr;
	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		if (Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game)
		{
			if (UWorld* W = Ctx.World()) return W;
		}
	}
	return nullptr;
}

/**
 * #778: get a specific PIE world by its instance id. GetPIEWorld() returns the
 * first PIE context it finds, which in a multi-instance session is the server
 * - so every runtime read resolved to the server and there was no way to
 * inspect a client at all. Pass INDEX_NONE for "first available".
 */
inline UWorld* GetPIEWorldByInstance(int32 PIEInstance)
{
	if (!GEngine) return nullptr;
	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		if (Ctx.WorldType != EWorldType::PIE && Ctx.WorldType != EWorldType::Game) continue;
		if (PIEInstance != INDEX_NONE && Ctx.PIEInstance != PIEInstance) continue;
		if (UWorld* W = Ctx.World()) return W;
	}
	return nullptr;
}

/** Net role of a PIE world, as a short string for reporting. */
inline FString DescribePIENetMode(UWorld* World)
{
	if (!World) return TEXT("none");
	switch (World->GetNetMode())
	{
		case NM_Standalone:      return TEXT("standalone");
		case NM_DedicatedServer: return TEXT("dedicatedServer");
		case NM_ListenServer:    return TEXT("listenServer");
		case NM_Client:          return TEXT("client");
		default:                 return TEXT("unknown");
	}
}

/** Resolve a world scope string ("editor"|"pie"|"game"|"auto") to a UWorld. "auto" prefers PIE if running. */
inline UWorld* ResolveWorldScope(const FString& Scope, int32 PIEInstance = INDEX_NONE)
{
	if (Scope.Equals(TEXT("pie"), ESearchCase::IgnoreCase) || Scope.Equals(TEXT("game"), ESearchCase::IgnoreCase))
	{
		return GetPIEWorldByInstance(PIEInstance);
	}
	if (Scope.Equals(TEXT("auto"), ESearchCase::IgnoreCase))
	{
		if (UWorld* W = GetPIEWorldByInstance(PIEInstance)) return W;
		return GetEditorWorld();
	}
	return GetEditorWorld();
}

/**
 * Resolve the world a request targets from its own params: `world`
 * (editor|pie|game|auto) plus an optional `pieInstance` selector. Keeping this
 * in one place means adding multi-instance support to an action is a one-line
 * change at the call site rather than a re-implementation.
 */
inline UWorld* ResolveWorldFromParams(const TSharedPtr<FJsonObject>& Params, const TCHAR* DefaultScope = TEXT("editor"))
{
	const FString Scope = OptionalString(Params, TEXT("world"), DefaultScope);
	int32 PIEInstance = INDEX_NONE;
	double Raw = 0.0;
	MCPNoteParamRead(Params, TEXT("pieInstance"));
	if (Params.IsValid() && Params->TryGetNumberField(TEXT("pieInstance"), Raw))
	{
		PIEInstance = FMath::RoundToInt(Raw);
	}
	return ResolveWorldScope(Scope, PIEInstance);
}

/** Get the editor world or return an error response. */
#define REQUIRE_EDITOR_WORLD(WorldVar) \
	UWorld* WorldVar = GetEditorWorld(); \
	if (!WorldVar) return MCPError(TEXT("Editor world not available"));
