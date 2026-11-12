#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

// Every bridge PIE launch goes through MCPPlaySession::Request (#1526), so a
// launch lands where the editor's own Play button would put it instead of in a
// new floating window.

enum class EMCPPlayMode : uint8
{
	/** The active level viewport. */
	Viewport,
	/** A new editor window. */
	NewWindow,
	/** A separate game process. There is no PIE world in the editor. */
	Standalone,
};

namespace MCPPlaySession
{
	/** viewport | new_window | standalone */
	const TCHAR* PlayModeName(EMCPPlayMode Mode);

	/** Accepts the names PlayModeName returns. */
	bool ParsePlayMode(const FString& Name, EMCPPlayMode& OutMode);

	/** The mode the editor's Play button runs: LevelEditorPlaySettings LastExecutedPlayModeType, folded onto the three above. */
	EMCPPlayMode EditorPlayMode();

	/** Writes LastExecutedPlayModeType, the same field the Play button's mode menu writes. Does not save config. */
	void SetEditorPlayMode(EMCPPlayMode Mode);

	struct FLaunch
	{
		/** The mode the editor settings asked for. */
		EMCPPlayMode Requested = EMCPPlayMode::Viewport;
		/** The mode the session was requested in. */
		EMCPPlayMode Used = EMCPPlayMode::Viewport;
		/** Why Used differs from Requested; empty when it does not. */
		FString Note;
	};

	/**
	 * Queues a play session in the editor's play mode. bNeedsEditorWorld is for callers that drive the PIE world
	 * afterwards: a standalone process has none, so they play in the viewport instead. Viewport mode falls back to a
	 * new window when no level viewport is visible, which is what the engine does for its own Play button.
	 */
	FLaunch Request(bool bNeedsEditorWorld);

	/** playMode, playModeRequested and, when set, playModeNote. */
	void WriteLaunch(const FLaunch& Launch, const TSharedPtr<FJsonObject>& Out);
}
