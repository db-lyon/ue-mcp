#pragma once

// Playhead control shared by scrub_sequence and render_sequence_frames.

#include "CoreMinimal.h"
#include "HandlerUtils.h"
#include "Editor.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "MovieSceneSequencePlayer.h"
#include "MovieSceneTimeUnit.h"
#include "Subsystems/AssetEditorSubsystem.h"
#if !UE_MCP_HAS_5_5_API
#include "ILevelSequenceEditorToolkit.h"
#include "ISequencer.h"
#endif

namespace UEMCP::SequencerPlayhead
{
	/** Pause, move the open sequence's playhead to a display-rate time, and
	 *  evaluate there now. The playhead move alone does not write
	 *  possessed-actor transforms, so a capture before the evaluation reads the
	 *  previous frame (#881). */
	inline void ScrubAndEvaluate(ULevelSequence* Current, const FFrameTime& TargetDisplay)
	{
		ULevelSequenceEditorBlueprintLibrary::Pause();
		const FMovieSceneSequencePlaybackParams ScrubTo(TargetDisplay, EUpdatePositionMethod::Scrub);
		ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(ScrubTo, EMovieSceneTimeUnit::DisplayRate);
#if UE_MCP_HAS_5_5_API
		ULevelSequenceEditorBlueprintLibrary::ForceUpdate();
#else
		// 5.4 has no ForceUpdate() wrapper; drive ISequencer::ForceEvaluate(),
		// which is what the wrapper calls. RefreshCurrentLevelSequence() defers
		// to a later tick and is not a substitute.
		if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr)
		{
			if (IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(Current, /*bFocusIfOpen*/ false))
			{
				ILevelSequenceEditorToolkit* Toolkit = static_cast<ILevelSequenceEditorToolkit*>(EditorInstance);
				if (const TSharedPtr<ISequencer> Sequencer = Toolkit->GetSequencer())
				{
					Sequencer->ForceEvaluate();
				}
			}
		}
#endif
	}
}
