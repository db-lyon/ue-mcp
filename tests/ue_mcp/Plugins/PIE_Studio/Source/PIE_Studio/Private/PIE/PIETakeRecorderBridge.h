#pragma once

#include "CoreMinimal.h"

/**
 * Thin runtime bridge to the Take Recorder plugin. The plugin is reached
 * exclusively through UFunction reflection so this module never link-depends
 * on TakeRecorder; if the plugin is not enabled in the user's project every
 * call returns false / no-op cleanly.
 *
 * The integration drives the live Take Recorder panel rather than building
 * a UTakeRecorderSources from scratch. The user opens the panel, configures
 * sources (player pawn, actors of interest), and pie_record_arm with
 * take_record=true then calls Start/Stop on that already-configured panel
 * in lockstep with BeginPIE / EndPIE.
 */
namespace UEMCPPIE
{
	namespace TakeRecorderBridge
	{
		// True when the Take Recorder plugin is loaded.
		bool IsAvailable();

		// True when a Take Recorder panel is currently open. Required for
		// StartFromPanel to do anything useful.
		bool IsPanelOpen();

		// True when a take is currently in progress.
		bool IsRecording();

		// Drives UTakeRecorderPanel::StartRecording on the open panel.
		// Returns false (with reason in OutMessage) when the plugin is not
		// available, no panel is open, or the panel rejected the start.
		bool StartFromPanel(FString& OutMessage);

		// Drives UTakeRecorderPanel::StopRecording on the open panel. Returns
		// false when no take is in progress.
		bool StopFromPanel(FString& OutMessage);
	}
}
