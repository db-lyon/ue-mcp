#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Tells connected clients when a Play session starts, pauses, resumes or ends,
 * however it was started: the toolbar, a keyboard shortcut, a console command
 * or a bridge call. A client that shows whether the game is running needs this
 * without polling, because a session the user starts by hand never passes
 * through the bridge at all.
 *
 * Each event is a "notifications/pie" frame whose params are
 * { state: "started"|"ended"|"paused"|"resumed", simulate: bool, at: ISO-8601 }.
 */
namespace FMCPPieNotifyHooks
{
	/** Where a notification goes: the method and its params. */
	using FSink = TFunction<void(const FString& Method, const TSharedRef<FJsonObject>& Params)>;

	/** The frame's method name. */
	inline const TCHAR* Method() { return TEXT("notifications/pie"); }

	/** The params of one event, as Install's delegates build them. */
	TSharedRef<FJsonObject> MakeParams(const TCHAR* State, bool bSimulate);

	/** Subscribe to the editor's PIE delegates once. Game thread. */
	void Install(FSink Sink);

	/** Unsubscribe everything Install subscribed. Safe to call twice. */
	void Remove();
}
