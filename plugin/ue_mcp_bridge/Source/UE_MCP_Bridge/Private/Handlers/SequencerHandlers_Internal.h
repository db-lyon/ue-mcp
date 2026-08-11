#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Misc/FrameRate.h"
#include "MovieSceneTimeHelpers.h"

namespace UEMCP::SequencerInfo
{
	inline constexpr int32 MaxDetailedSections = 256;
	inline constexpr int32 MaxDetailedChannels = 1024;
	inline constexpr int32 MaxDetailedKeyTimes = 4096;
	inline constexpr int32 MaxKeyTimesPerChannel = 256;

	struct FDetailBudget
	{
		int32 Sections = MaxDetailedSections;
		int32 Channels = MaxDetailedChannels;
		int32 KeyTimes = MaxDetailedKeyTimes;
		bool bTruncated = false;
	};

	inline void SetFrameRateFields(FJsonObject& Json, const FFrameRate& Rate)
	{
		Json.SetNumberField(TEXT("numerator"), Rate.Numerator);
		Json.SetNumberField(TEXT("denominator"), Rate.Denominator);
	}

	inline bool IsUsableFrameRate(const FFrameRate& Rate)
	{
		return Rate.Numerator > 0 && Rate.Denominator > 0;
	}

	inline double TickToDisplayFrame(
		const FFrameNumber Tick,
		const FFrameRate& TickResolution,
		const FFrameRate& DisplayRate)
	{
		return FFrameRate::TransformTime(FFrameTime(Tick), TickResolution, DisplayRate).AsDecimal();
	}

	inline void SetTimingRangeFields(
		FJsonObject& Json,
		const TRange<FFrameNumber>& Range,
		const FFrameRate& TickResolution,
		const FFrameRate& DisplayRate)
	{
		const bool bHasStart = Range.HasLowerBound();
		const bool bHasEnd = Range.HasUpperBound();
		Json.SetBoolField(TEXT("hasStart"), bHasStart);
		Json.SetBoolField(TEXT("hasEnd"), bHasEnd);

		if (bHasStart)
		{
			const FFrameNumber StartTick = UE::MovieScene::DiscreteInclusiveLower(Range);
			Json.SetBoolField(TEXT("startInclusive"), true);
			Json.SetNumberField(TEXT("startTick"), StartTick.Value);
			Json.SetNumberField(TEXT("startDisplayFrame"), TickToDisplayFrame(StartTick, TickResolution, DisplayRate));
			Json.SetNumberField(TEXT("startSeconds"), TickResolution.AsSeconds(FFrameTime(StartTick)));
		}

		if (bHasEnd)
		{
			const FFrameNumber EndTick = UE::MovieScene::DiscreteExclusiveUpper(Range);
			Json.SetBoolField(TEXT("endExclusive"), true);
			Json.SetNumberField(TEXT("endTick"), EndTick.Value);
			Json.SetNumberField(TEXT("endDisplayFrame"), TickToDisplayFrame(EndTick, TickResolution, DisplayRate));
			Json.SetNumberField(TEXT("endSeconds"), TickResolution.AsSeconds(FFrameTime(EndTick)));
		}
	}

	inline TSharedPtr<FJsonObject> MakeKeyTimeObject(
		const FFrameNumber Tick,
		const FFrameRate& TickResolution,
		const FFrameRate& DisplayRate)
	{
		TSharedPtr<FJsonObject> KeyTime = MakeShared<FJsonObject>();
		KeyTime->SetNumberField(TEXT("tick"), Tick.Value);
		KeyTime->SetNumberField(TEXT("displayFrame"), TickToDisplayFrame(Tick, TickResolution, DisplayRate));
		KeyTime->SetNumberField(TEXT("seconds"), TickResolution.AsSeconds(FFrameTime(Tick)));
		return KeyTime;
	}
}
