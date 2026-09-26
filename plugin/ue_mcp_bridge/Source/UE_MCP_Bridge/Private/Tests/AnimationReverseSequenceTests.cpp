// Coverage for animation(reverse_sequence) (#1162): build a five-key sequence
// with a notify, a float curve and a sync marker, reverse it into a copy, then
// reverse the copy in place with a cycle offset. Everything lives on a private
// mount, so the attached project is never touched.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Animation/AnimationHandlers.h"

#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimData/CurveIdentifier.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimTypes.h"
#include "Animation/Skeleton.h"
#include "Curves/RichCurve.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "ReferenceSkeleton.h"
#include "UObject/Package.h"
#include "Tests/MCPScopedTestMount.h"

namespace UEMCPReverseSequenceTests
{
	const TCHAR* const ProbeRoot = TEXT("/UEMCPReverseSequenceTest/");
	const TCHAR* const ProbeBoneName = TEXT("root");
	const TCHAR* const ProbeCurveName = TEXT("ReverseProbeCurve");
	const TCHAR* const ProbeNotifyName = TEXT("ReverseProbeNotify");

	TSharedPtr<FJsonObject> AsObject(const TSharedPtr<FJsonValue>& Response)
	{
		return (Response.IsValid() && Response->Type == EJson::Object) ? Response->AsObject() : nullptr;
	}

	double NumberAt(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field)
	{
		double Value = -1.0;
		if (Obj.IsValid()) Obj->TryGetNumberField(Field, Value);
		return Value;
	}

	double BoneKeyX(const UAnimSequence* Sequence, int32 KeyIndex)
	{
		TArray<FTransform> Keys;
		Sequence->GetDataModel()->GetBoneTrackTransforms(FName(ProbeBoneName), Keys);
		return Keys.IsValidIndex(KeyIndex) ? Keys[KeyIndex].GetLocation().X : -1.0;
	}

	float NotifyTime(const UAnimSequence* Sequence)
	{
		for (const FAnimNotifyEvent& Notify : Sequence->Notifies)
		{
			if (Notify.NotifyName == FName(ProbeNotifyName)) return Notify.GetTime();
		}
		return -1.f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAnimationReverseSequenceTest,
	"UE.MCP.Animation.ReverseSequence.KeysNotifiesAndCurves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationReverseSequenceTest::RunTest(const FString& Parameters)
{
	using namespace UEMCPReverseSequenceTests;

	// Declared first so it is destroyed last, after the root scopes below.
	const FMCPScopedTestMount Mount{ FString(ProbeRoot), TEXT("UEMCPReverseSequenceTest") };

	const FString SkeletonPackageName = FString(ProbeRoot) + TEXT("SK_ReverseProbe");
	UPackage* SkeletonPackage = CreatePackage(*SkeletonPackageName);
	USkeleton* Skeleton = NewObject<USkeleton>(SkeletonPackage, TEXT("SK_ReverseProbe"), RF_Public | RF_Standalone);
	const FGCRootScope KeepSkeleton(Skeleton);
	{
		FReferenceSkeletonModifier Modifier(Skeleton);
		Modifier.Add(FMeshBoneInfo(FName(ProbeBoneName), ProbeBoneName, INDEX_NONE), FTransform::Identity);
	}

	const FString SourcePackageName = FString(ProbeRoot) + TEXT("A_ReverseProbe");
	UPackage* SourcePackage = CreatePackage(*SourcePackageName);
	UAnimSequence* Source = NewObject<UAnimSequence>(SourcePackage, TEXT("A_ReverseProbe"), RF_Public | RF_Standalone);
	const FGCRootScope KeepSource(Source);
	Source->SetSkeleton(Skeleton);

	// Ten frames a second, four frames long: keys at 0.0 .. 0.4 s with the
	// root at x = 0, 10, 20, 30, 40.
	const FAnimationCurveIdentifier CurveId(FName(ProbeCurveName), ERawCurveTrackTypes::RCT_Float);
	{
		IAnimationDataController& Controller = Source->GetController();
		Controller.InitializeModel();
		Controller.OpenBracket(FText::FromString(TEXT("Build reverse_sequence probe")), false);
		Controller.SetFrameRate(FFrameRate(10, 1), false);
		Controller.SetNumberOfFrames(FFrameNumber(4), false);
		Controller.AddBoneCurve(FName(ProbeBoneName), false);
		TArray<FVector> Locations;
		TArray<FQuat> Rotations;
		TArray<FVector> Scales;
		for (int32 Key = 0; Key < 5; ++Key)
		{
			Locations.Add(FVector(10.0 * Key, 0.0, 0.0));
			Rotations.Add(FQuat::Identity);
			Scales.Add(FVector::OneVector);
		}
		TestTrue(TEXT("probe bone keys written"), Controller.SetBoneTrackKeys(FName(ProbeBoneName), Locations, Rotations, Scales, false));
		Controller.AddCurve(CurveId, AACF_DefaultCurve, false);
		TArray<FRichCurveKey> CurveKeys;
		CurveKeys.Add(FRichCurveKey(0.f, 0.f));
		CurveKeys.Add(FRichCurveKey(0.4f, 1.f));
		TestTrue(TEXT("probe curve keys written"), Controller.SetCurveKeys(CurveId, CurveKeys, false));
		Controller.NotifyPopulated();
		Controller.CloseBracket(false);
	}

	FAnimNotifyEvent& Notify = Source->Notifies.AddDefaulted_GetRef();
	Notify.NotifyName = FName(ProbeNotifyName);
	Notify.Link(Source, 0.1f);

	FAnimSyncMarker Marker;
	Marker.MarkerName = TEXT("ReverseProbeMarker");
	Marker.Time = 0.1f;
	Source->AuthoredSyncMarkers.Add(Marker);
	Source->RefreshSyncMarkerDataFromAuthored();

	FMCPHandlerRegistry Registry;
	FAnimationHandlers::RegisterHandlers(Registry);
	if (!TestTrue(TEXT("reverse_sequence is registered"), Registry.HasHandler(TEXT("reverse_sequence"))))
	{
		return false;
	}

	// A reversed copy.
	const FString DestinationPackageName = FString(ProbeRoot) + TEXT("A_ReverseProbe_Reversed");
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("sourcePath"), SourcePackageName);
		Request->SetStringField(TEXT("destinationPath"), DestinationPackageName);
		const TSharedPtr<FJsonObject> Response = AsObject(Registry.ExecuteHandler(TEXT("reverse_sequence"), Request));
		if (!TestTrue(TEXT("reverse_sequence answered"), Response.IsValid())) return false;

		FString Error;
		Response->TryGetStringField(TEXT("error"), Error);
		if (!TestTrue(FString::Printf(TEXT("reverse_sequence succeeded (%s)"), *Error), Response->GetBoolField(TEXT("success"))))
		{
			return false;
		}
		TestEqual(TEXT("numFrames"), NumberAt(Response, TEXT("numFrames")), 4.0);
		const TSharedPtr<FJsonObject>* Reversed = nullptr;
		if (TestTrue(TEXT("reversed counts reported"), Response->TryGetObjectField(TEXT("reversed"), Reversed)))
		{
			TestEqual(TEXT("one bone track reversed"), NumberAt(*Reversed, TEXT("boneTracks")), 1.0);
			TestEqual(TEXT("one float curve reversed"), NumberAt(*Reversed, TEXT("floatCurves")), 1.0);
			TestEqual(TEXT("one notify reversed"), NumberAt(*Reversed, TEXT("notifies")), 1.0);
			TestEqual(TEXT("one sync marker reversed"), NumberAt(*Reversed, TEXT("syncMarkers")), 1.0);
		}
		TestTrue(TEXT("a delete_asset rollback is attached"), Response->HasField(TEXT("rollback")));
	}

	UAnimSequence* Copy = FindObject<UAnimSequence>(nullptr, *(DestinationPackageName + TEXT(".A_ReverseProbe_Reversed")));
	if (!TestNotNull(TEXT("the reversed copy exists"), Copy)) return false;

	TestEqual(TEXT("copy key 0 holds the source's last key"), BoneKeyX(Copy, 0), 40.0);
	TestEqual(TEXT("copy key 1 holds the source's key 3"), BoneKeyX(Copy, 1), 30.0);
	TestEqual(TEXT("copy key 4 holds the source's first key"), BoneKeyX(Copy, 4), 0.0);
	TestTrue(TEXT("the notify moved from 0.1 s to 0.3 s"), FMath::IsNearlyEqual(NotifyTime(Copy), 0.3f, 1.0e-3f));
	if (const FRichCurve* Curve = Copy->GetDataModel()->FindRichCurve(CurveId))
	{
		TestTrue(TEXT("the curve starts where the source ended"), FMath::IsNearlyEqual(Curve->Eval(0.f), 1.f, 1.0e-3f));
		TestTrue(TEXT("the curve ends where the source started"), FMath::IsNearlyEqual(Curve->Eval(0.4f), 0.f, 1.0e-3f));
	}
	else
	{
		AddError(TEXT("the reversed copy lost its float curve"));
	}
	TestTrue(TEXT("the sync marker moved to 0.3 s"),
		Copy->AuthoredSyncMarkers.Num() == 1 && FMath::IsNearlyEqual(Copy->AuthoredSyncMarkers[0].Time, 0.3f, 1.0e-3f));

	TestEqual(TEXT("the source's first key is untouched"), BoneKeyX(Source, 0), 0.0);
	TestTrue(TEXT("the source's notify is untouched"), FMath::IsNearlyEqual(NotifyTime(Source), 0.1f, 1.0e-3f));

	// Replaying onto the same destination is an idempotent skip, not a second reversal.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("sourcePath"), SourcePackageName);
		Request->SetStringField(TEXT("destinationPath"), DestinationPackageName);
		const TSharedPtr<FJsonObject> Response = AsObject(Registry.ExecuteHandler(TEXT("reverse_sequence"), Request));
		TestTrue(TEXT("the replay succeeds"), Response.IsValid() && Response->GetBoolField(TEXT("success")));
		TestEqual(TEXT("the replay leaves the copy alone"), BoneKeyX(Copy, 0), 40.0);
	}

	// In place, rotated one frame: keys 40,30,20,10,0 reverse to 0..40 and
	// start one frame in, so the loop reads 10,20,30,0 and closes on 10.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("sourcePath"), DestinationPackageName);
		Request->SetBoolField(TEXT("inPlace"), true);
		Request->SetNumberField(TEXT("cycleOffsetFrames"), 1);
		const TSharedPtr<FJsonObject> Response = AsObject(Registry.ExecuteHandler(TEXT("reverse_sequence"), Request));
		FString Error;
		if (Response.IsValid()) Response->TryGetStringField(TEXT("error"), Error);
		if (!TestTrue(FString::Printf(TEXT("the in-place reversal succeeded (%s)"), *Error),
			Response.IsValid() && Response->GetBoolField(TEXT("success"))))
		{
			return false;
		}
		TestEqual(TEXT("cycleOffsetFrames echoed"), NumberAt(Response, TEXT("cycleOffsetFrames")), 1.0);
		TestEqual(TEXT("rotated key 0"), BoneKeyX(Copy, 0), 10.0);
		TestEqual(TEXT("rotated key 3"), BoneKeyX(Copy, 3), 0.0);
		TestEqual(TEXT("the last key closes the loop"), BoneKeyX(Copy, 4), 10.0);
		TestTrue(TEXT("the notify at 0.3 s reverses to 0.1 s and rotates to 0"), FMath::IsNearlyEqual(NotifyTime(Copy), 0.f, 1.0e-3f));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
