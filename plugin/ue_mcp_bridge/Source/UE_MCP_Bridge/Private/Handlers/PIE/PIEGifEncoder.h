#pragma once

#include "CoreMinimal.h"

namespace MCPPIE
{
	struct FGifEncodeParams
	{
		int32 DelayCs = 3; // hundredths of a second per frame
		int32 MaxWidth = 720; // scale down if wider
		int32 LoopCount = 0; // 0 = loop forever
	};

	bool EncodeAnimatedGif(
		const TArray<FString>& FramePaths,
		const FString& OutputPath,
		const FGifEncodeParams& Params = FGifEncodeParams());
}
