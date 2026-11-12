// The Sequencer play rate begin_control_rig_edit gives its source section. A
// header so the handler and its automation test compute the same value.
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimTypes.h"

namespace MCPControlRigStepSampling
{
	/** RawTimelinePlayRate cancels the clip's RateScale. A Step clip holds each
	 *  key until the next one, and a key's own frame can map to a time a hair
	 *  before it, which a Step clip answers with the previous key (#1642). Such a
	 *  clip runs slightly fast so every sample lands just past its key; the drift
	 *  stays under a quarter frame over the clip and the session range. */
	inline float SessionPlayRate(double RawTimelinePlayRate, EAnimInterpolationType Interpolation, int32 SourceKeys, int32 SessionFrames)
	{
		if (Interpolation != EAnimInterpolationType::Step)
		{
			return static_cast<float>(RawTimelinePlayRate);
		}
		const double Samples = FMath::Max3(1.0, static_cast<double>(SourceKeys), static_cast<double>(SessionFrames));
		return static_cast<float>(RawTimelinePlayRate * (1.0 + 0.25 / Samples));
	}
}
