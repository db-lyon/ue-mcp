#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class APlayerController;
class SViewport;
class SWidget;
class SWindow;
class ULocalPlayer;
class UWorld;

/**
 * Slate input for one PIE player: which Slate user and viewport belong to it, where that user's focus is, and
 * key events sent through FSlateApplication as that user, so a UMG menu in UI-only input mode can be focused and
 * navigated on the client a caller names (#2018, #1928).
 *
 * The player is chosen the way script acts choose theirs (PIERemotePlayers::FindOwnLocalPlayer): a PIE instance
 * names one exact world, and no instance means the editor's own player (the listen-server host, else the lowest
 * client window). Never the first world context, which on a dedicated server is the server and has no viewport.
 *
 * Every PIE window in one process shares Slate user 0 (one keyboard), so focusing a widget on one client takes
 * focus away from the others. Key events follow that user's focus, which is why a key aimed at a client first
 * needs the focus inside that client's viewport.
 */
namespace MCPPIE::PIEClientUI
{
	struct FUIPlayer
	{
		ULocalPlayer* LocalPlayer = nullptr;
		APlayerController* PlayerController = nullptr;
		UWorld* World = nullptr;
		int32 PIEInstance = INDEX_NONE;
		int32 LocalPlayerIndex = 0;
		int32 SlateUserIndex = 0;
		TSharedPtr<SViewport> Viewport;
		TSharedPtr<SWindow> Window;
		/** "pie_instance N local player L in <world> (<net mode>)". */
		FString Description;
	};

	/** PIEInstance INDEX_NONE selects the editor's own player. False with an error naming the worlds there are. */
	bool Resolve(int32 PIEInstance, int32 LocalPlayerIndex, FUIPlayer& Out, FString& OutError);

	/** Whether Widget is the player's viewport or sits inside it. */
	bool IsInsideViewport(const FUIPlayer& Player, const TSharedPtr<SWidget>& Widget);

	/** The UMG widget in Player's world whose Slate widget is Widget or the nearest ancestor of it, else empty. */
	FString FindUmgName(const FUIPlayer& Player, const TSharedPtr<SWidget>& Widget);

	/**
	 * Where the player's Slate user has focus: {slate_user, has_focus, in_viewport, slate_type, widget}.
	 * in_viewport false means the focus is on another client's window or the editor's.
	 */
	TSharedPtr<FJsonObject> DescribeFocus(const FUIPlayer& Player);

	/** Puts the user's focus on the player's viewport when it is outside it. True when it moved. */
	bool FocusViewportIfOutside(const FUIPlayer& Player);

	enum class EKeyPhase { Press, Down, Up };

	/** "press", "down" or "up". False for anything else. */
	bool ParseKeyPhase(const FString& Text, EKeyPhase& Out);

	struct FKeyResult
	{
		bool bDownSent = false;
		bool bDownHandled = false;
		bool bUpSent = false;
		bool bUpHandled = false;
		int32 CharsSent = 0;
		bool bFocusMovedToViewport = false;
		TSharedPtr<FJsonObject> FocusBefore;
		TSharedPtr<FJsonObject> FocusAfter;
	};

	/**
	 * Sends KeyName (an FKey name: Right, Enter, Tab, Gamepad_DPad_Down...) through FSlateApplication as the
	 * player's Slate user. A key Slate does not handle reaches navigation, then the viewport and the player's input.
	 */
	bool SendKey(const FUIPlayer& Player, const FString& KeyName, EKeyPhase Phase, FKeyResult& Out, FString& OutError);

	/** Types Text into the focused widget, one character event per character. */
	bool SendText(const FUIPlayer& Player, const FString& Text, FKeyResult& Out, FString& OutError);

	/** The result fields both pie(send_key) and a step's key act report. */
	void WriteKeyResult(const FUIPlayer& Player, const FKeyResult& R, const TSharedPtr<FJsonObject>& Into);
}
