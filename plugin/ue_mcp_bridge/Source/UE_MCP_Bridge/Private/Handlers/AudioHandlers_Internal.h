// Helpers shared by the audio handler files. The module is a unity build, so a
// helper used by more than one AudioHandlers_*.cpp lives here, not as copies.
#pragma once

#include "CoreMinimal.h"
#include "HandlerUtils.h"

namespace MCPAudio
{
	/** Save every asset and record the outcome on Result. Every asset is
	 *  attempted; the first one that did not write fails the result. */
	inline bool SaveAndNote(const TSharedPtr<FJsonObject>& Result, std::initializer_list<UObject*> Assets)
	{
		FString FailedPath;
		FString FailedReason;
		for (UObject* Asset : Assets)
		{
			if (!Asset) continue;
			FString Reason;
			if (!SaveAssetPackageChecked(Asset, Reason) && FailedPath.IsEmpty())
			{
				FailedPath = Asset->GetPathName();
				FailedReason = Reason;
			}
		}
		const bool bSaved = FailedPath.IsEmpty();
		MCPNoteSaveOutcome(Result, FailedPath, bSaved, FailedReason);
		return bSaved;
	}
}
