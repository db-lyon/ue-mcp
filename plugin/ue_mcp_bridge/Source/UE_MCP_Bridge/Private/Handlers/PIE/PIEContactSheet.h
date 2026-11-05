#pragma once

#include "CoreMinimal.h"

namespace MCPPIE
{
	struct FContactSheetParams
	{
		int32 Columns = 0;      // 0 = auto (roughly square)
		int32 CellWidth = 320;  // each cell scaled to this width, aspect preserved
		int32 MaxCells = 25;    // if more frames, sample evenly down to this many
		int32 Quality = 85;     // JPEG quality
		bool bLabels = true;    // bake index/time labels into each cell
	};

	// Compose a grid montage of the given frame images into a single JPEG the
	// agent can view at a glance. Frames are sampled evenly if there are more than
	// MaxCells. Optional Labels (parallel to FramePaths) are drawn top-left of
	// each cell; when null, the frame's basename is used.
	//
	// Returns true on success. Runs on the game thread (loads image files).
	bool ComposeContactSheet(
		const TArray<FString>& FramePaths,
		const FString& OutputPath,
		const FContactSheetParams& Params = FContactSheetParams(),
		const TArray<FString>* Labels = nullptr);
}
