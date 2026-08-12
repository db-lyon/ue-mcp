#if WITH_DEV_AUTOMATION_TESTS && (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8))

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Misc/AutomationTest.h"
#include "ReferenceSkeleton.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAnimationControlRigRawTimelineRateScaleTest,
	"UE.MCP.Animation.ControlRig.RawTimelineRateScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationControlRigRawTimelineRateScaleTest::RunTest(const FString& Parameters)
{
	const FFrameRate DisplayRate(25, 1);
	USkeleton* Skeleton = NewObject<USkeleton>(GetTransientPackage());
	{
		FReferenceSkeletonModifier Modifier(Skeleton);
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
	}
	UAnimSequence* SourceAnimation = NewObject<UAnimSequence>(GetTransientPackage());
	SourceAnimation->SetSkeleton(Skeleton);
	IAnimationDataController& Controller = SourceAnimation->GetController();
	Controller.InitializeModel();
	Controller.OpenBracket(FText::FromString(TEXT("Build raw-timeline test animation")), false);
	Controller.SetFrameRate(DisplayRate, false);
	Controller.SetNumberOfFrames(FFrameNumber(40), false);
	Controller.CloseBracket(false);
	SourceAnimation->RateScale = 3.06608796f;

	UMovieSceneSkeletalAnimationSection* Section =
		NewObject<UMovieSceneSkeletalAnimationSection>(GetTransientPackage());
	Section->Params.Animation = SourceAnimation;
	Section->Params.PlayRate = 1.0f / SourceAnimation->RateScale;
	Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(41)));

	double PreviousTime = -1.0;
	for (int32 Frame = 0; Frame <= 40; ++Frame)
	{
		const double ExpectedTime = static_cast<double>(Frame) / DisplayRate.AsDecimal();
		const double MappedTime = Section->MapTimeToAnimation(FFrameTime(Frame), DisplayRate);
		TestTrue(
			*FString::Printf(TEXT("frame %d maps to raw source time"), Frame),
			FMath::IsNearlyEqual(MappedTime, ExpectedTime, 0.0001));
		TestTrue(
			*FString::Printf(TEXT("frame %d does not loop backwards"), Frame),
			MappedTime >= PreviousTime);
		PreviousTime = MappedTime;
	}

	TestTrue(
		TEXT("the final frame reaches the source end once"),
		FMath::IsNearlyEqual(PreviousTime, SourceAnimation->GetPlayLength(), 0.0001));
	TestEqual(TEXT("the source RateScale is unchanged"), SourceAnimation->RateScale, 3.06608796f);
	return true;
}

#endif
