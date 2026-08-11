#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/SequencerHandlers.h"
#include "HandlerRegistry.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameFramework/Actor.h"
#include "LevelSequence.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneFadeTrack.h"
#include "UObject/Package.h"

namespace UEMCPSequencerTimingTests
{
	/**
	 * An Asset Registry visible LevelSequence that exists only for one test.
	 *
	 * UEditorAssetLibrary intentionally loads registered assets, so a plain
	 * object in GetTransientPackage cannot exercise the real handler route. This
	 * fixture creates a uniquely named in-memory package, never saves it, removes
	 * its registry entry at scope exit, and leaves every package it owns clean.
	 */
	class FTransientSequenceAsset
	{
	public:
		explicit FTransientSequenceAsset(const TCHAR* Prefix)
		{
			const FString UniqueName = FString::Printf(
				TEXT("%s_%s"),
				Prefix,
				*FGuid::NewGuid().ToString(EGuidFormats::Digits));
			const FString PackageName = FString::Printf(
				TEXT("/Game/__UE_MCP_Automation/%s"),
				*UniqueName);

			Package = CreatePackage(*PackageName);
			Sequence = NewObject<ULevelSequence>(
				Package,
				FName(*UniqueName),
				RF_Public | RF_Standalone);
			Sequence->Initialize();
			Sequence->AddToRoot();
			FAssetRegistryModule::AssetCreated(Sequence);
			ClearDirtyState();
		}

		~FTransientSequenceAsset()
		{
			if (Sequence)
			{
				FAssetRegistryModule::AssetDeleted(Sequence);
				Sequence->RemoveFromRoot();
				Sequence->ClearFlags(RF_Public | RF_Standalone);
				Sequence->MarkAsGarbage();
			}
			if (Package)
			{
				Package->SetDirtyFlag(false);
				Package->MarkAsGarbage();
			}
		}

		FTransientSequenceAsset(const FTransientSequenceAsset&) = delete;
		FTransientSequenceAsset& operator=(const FTransientSequenceAsset&) = delete;

		ULevelSequence* Get() const
		{
			return Sequence;
		}

		FString GetAssetPath() const
		{
			return Sequence ? Sequence->GetPathName() : FString();
		}

		void ClearDirtyState() const
		{
			if (Package)
			{
				Package->SetDirtyFlag(false);
			}
		}

		bool IsDirty() const
		{
			return Package && Package->IsDirty();
		}

	private:
		UPackage* Package = nullptr;
		ULevelSequence* Sequence = nullptr;
	};

	TSharedPtr<FJsonObject> ExecuteSequenceInfo(
		FMCPHandlerRegistry& Registry,
		const FString& AssetPath,
		const bool bIncludeSectionDetails)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), AssetPath);
		Params->SetBoolField(TEXT("includeSectionDetails"), bIncludeSectionDetails);

		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(
			TEXT("get_sequence_info"), Params);
		return Response.IsValid() && Response->Type == EJson::Object
			? Response->AsObject()
			: nullptr;
	}

	TSharedPtr<FJsonObject> GetObjectField(
		const TSharedPtr<FJsonObject>& Parent,
		const TCHAR* FieldName)
	{
		const TSharedPtr<FJsonObject>* Value = nullptr;
		return Parent.IsValid() && Parent->TryGetObjectField(FieldName, Value) && Value
			? *Value
			: nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* GetArrayField(
		const TSharedPtr<FJsonObject>& Parent,
		const TCHAR* FieldName)
	{
		const TArray<TSharedPtr<FJsonValue>>* Value = nullptr;
		return Parent.IsValid() && Parent->TryGetArrayField(FieldName, Value)
			? Value
			: nullptr;
	}

	TSharedPtr<FJsonObject> FindObjectByStringField(
		const TArray<TSharedPtr<FJsonValue>>* Values,
		const TCHAR* FieldName,
		const FString& Expected)
	{
		if (!Values)
		{
			return nullptr;
		}

		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			if (!Value.IsValid() || Value->Type != EJson::Object)
			{
				continue;
			}

			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			FString Actual;
			if (Object.IsValid() && Object->TryGetStringField(FieldName, Actual) && Actual == Expected)
			{
				return Object;
			}
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> GetArrayObjectAt(
		const TArray<TSharedPtr<FJsonValue>>* Values,
		const int32 Index)
	{
		if (!Values || !Values->IsValidIndex(Index))
		{
			return nullptr;
		}
		const TSharedPtr<FJsonValue>& Value = (*Values)[Index];
		return Value.IsValid() && Value->Type == EJson::Object
			? Value->AsObject()
			: nullptr;
	}

	UMovieSceneSection* AddSection(
		UMovieSceneTrack* Track,
		const TRange<FFrameNumber>& Range)
	{
		if (!Track)
		{
			return nullptr;
		}
		UMovieSceneSection* Section = Track->CreateNewSection();
		if (Section)
		{
			Section->SetRange(Range);
			Track->AddSection(*Section);
		}
		return Section;
	}

	bool ExpectNumber(
		FAutomationTestBase& Test,
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* FieldName,
		const double Expected,
		const double Tolerance = UE_KINDA_SMALL_NUMBER)
	{
		double Actual = 0.0;
		const bool bFound = Object.IsValid() && Object->TryGetNumberField(FieldName, Actual);
		Test.TestTrue(FString::Printf(TEXT("%s is present"), FieldName), bFound);
		return bFound && Test.TestEqual(FieldName, Actual, Expected, Tolerance);
	}

	bool ExpectBool(
		FAutomationTestBase& Test,
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* FieldName,
		const bool Expected)
	{
		bool bActual = false;
		const bool bFound = Object.IsValid() && Object->TryGetBoolField(FieldName, bActual);
		Test.TestTrue(FString::Printf(TEXT("%s is present"), FieldName), bFound);
		return bFound && Test.TestEqual(FieldName, bActual, Expected);
	}

	void ExpectClosedRange(
		FAutomationTestBase& Test,
		const TSharedPtr<FJsonObject>& Range,
		const int32 StartTick,
		const int32 EndTick,
		const double StartDisplayFrame,
		const double EndDisplayFrame,
		const double StartSeconds,
		const double EndSeconds)
	{
		ExpectBool(Test, Range, TEXT("hasStart"), true);
		ExpectBool(Test, Range, TEXT("hasEnd"), true);
		ExpectBool(Test, Range, TEXT("startInclusive"), true);
		ExpectBool(Test, Range, TEXT("endExclusive"), true);
		ExpectNumber(Test, Range, TEXT("startTick"), StartTick);
		ExpectNumber(Test, Range, TEXT("endTick"), EndTick);
		ExpectNumber(Test, Range, TEXT("startDisplayFrame"), StartDisplayFrame);
		ExpectNumber(Test, Range, TEXT("endDisplayFrame"), EndDisplayFrame);
		ExpectNumber(Test, Range, TEXT("startSeconds"), StartSeconds, 1.e-9);
		ExpectNumber(Test, Range, TEXT("endSeconds"), EndSeconds, 1.e-9);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSequencerInfoRegistrationAndValidationTest,
	"UE.MCP.Sequencer.GetSequenceInfo.RegistrationAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSequencerInfoRegistrationAndValidationTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FSequencerHandlers::RegisterHandlers(Registry);
	TestTrue(TEXT("get_sequence_info is registered"), Registry.HasHandler(TEXT("get_sequence_info")));

	const TSharedPtr<FJsonValue> MissingResponse = Registry.ExecuteHandler(
		TEXT("get_sequence_info"), MakeShared<FJsonObject>());
	TestTrue(TEXT("missing path returns an object"),
		MissingResponse.IsValid() && MissingResponse->Type == EJson::Object);
	if (MissingResponse.IsValid() && MissingResponse->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Missing = MissingResponse->AsObject();
		TestFalse(TEXT("missing path is unsuccessful"), Missing->GetBoolField(TEXT("success")));
		TestTrue(TEXT("missing path identifies the accepted field"),
			Missing->GetStringField(TEXT("error")).Contains(TEXT("assetPath")));
	}

	AddExpectedError(TEXT("LoadAsset failed"), EAutomationExpectedErrorFlags::Contains, 1);
	TSharedPtr<FJsonObject> InvalidParams = MakeShared<FJsonObject>();
	InvalidParams->SetStringField(
		TEXT("assetPath"),
		TEXT("/Game/__UE_MCP_Automation/DefinitelyMissing.DefinitelyMissing"));
	const TSharedPtr<FJsonValue> InvalidResponse = Registry.ExecuteHandler(
		TEXT("get_sequence_info"), InvalidParams);
	TestTrue(TEXT("invalid asset path returns an object"),
		InvalidResponse.IsValid() && InvalidResponse->Type == EJson::Object);
	if (InvalidResponse.IsValid() && InvalidResponse->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Invalid = InvalidResponse->AsObject();
		TestFalse(TEXT("invalid asset path is unsuccessful"), Invalid->GetBoolField(TEXT("success")));
		TestTrue(TEXT("invalid asset path is named in the error"),
			Invalid->GetStringField(TEXT("error")).Contains(TEXT("DefinitelyMissing")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSequencerInfoTimingDetailsTest,
	"UE.MCP.Sequencer.GetSequenceInfo.TimingDetails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSequencerInfoTimingDetailsTest::RunTest(const FString& Parameters)
{
	using namespace UEMCPSequencerTimingTests;

	FTransientSequenceAsset Fixture(TEXT("TimingDetails"));
	ULevelSequence* Sequence = Fixture.Get();
	TestNotNull(TEXT("transient LevelSequence exists"), Sequence);
	if (!Sequence)
	{
		return false;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	TestNotNull(TEXT("transient LevelSequence has a MovieScene"), MovieScene);
	if (!MovieScene)
	{
		return false;
	}

	MovieScene->SetDisplayRate(FFrameRate(24, 1));
	MovieScene->SetTickResolutionDirectly(FFrameRate(24000, 1));
	MovieScene->SetPlaybackRange(
		TRange<FFrameNumber>(
			TRangeBound<FFrameNumber>::Inclusive(FFrameNumber(12000)),
			TRangeBound<FFrameNumber>::Exclusive(FFrameNumber(60000))),
		false);

	UMovieSceneCameraCutTrack* CameraCutTrack = Cast<UMovieSceneCameraCutTrack>(
		MovieScene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
	TestNotNull(TEXT("camera cut root track was created"), CameraCutTrack);
	UMovieSceneSection* CameraCutSection = AddSection(
		CameraCutTrack,
		TRange<FFrameNumber>(FFrameNumber(24000), FFrameNumber(48000)));
	TestNotNull(TEXT("camera cut section was created"), CameraCutSection);

	UMovieSceneFadeTrack* FadeTrack = MovieScene->AddTrack<UMovieSceneFadeTrack>();
	TestNotNull(TEXT("ordinary root track was created"), FadeTrack);
	UMovieSceneSection* OpenSection = AddSection(
		FadeTrack,
		TRange<FFrameNumber>(
			TRangeBound<FFrameNumber>::Open(),
			TRangeBound<FFrameNumber>::Exclusive(FFrameNumber(36000))));
	TestNotNull(TEXT("open-bound root section was created"), OpenSection);

	const FGuid BindingGuid = MovieScene->AddPossessable(TEXT("TimingActor"), AActor::StaticClass());
	UMovieScene3DTransformTrack* TransformTrack =
		MovieScene->AddTrack<UMovieScene3DTransformTrack>(BindingGuid);
	TestNotNull(TEXT("binding transform track was created"), TransformTrack);
	UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(
		AddSection(
			TransformTrack,
			TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(72000))));
	TestNotNull(TEXT("transform section was created"), TransformSection);
	if (!CameraCutTrack || !CameraCutSection || !FadeTrack || !OpenSection ||
		!TransformTrack || !TransformSection)
	{
		return false;
	}

	FMovieSceneDoubleChannel* LocationX = TransformSection->GetChannelProxy()
		.GetChannelByName<FMovieSceneDoubleChannel>(TEXT("Location.X"))
		.Get();
	TestNotNull(TEXT("Location.X channel exists"), LocationX);
	if (!LocationX)
	{
		return false;
	}
	LocationX->AddLinearKey(FFrameNumber(60000), 30.0);
	LocationX->AddLinearKey(FFrameNumber(500), 10.0);
	LocationX->AddLinearKey(FFrameNumber(24000), 20.0);
	Fixture.ClearDirtyState();

	FMCPHandlerRegistry Registry;
	FSequencerHandlers::RegisterHandlers(Registry);

	// Details remain opt-in even though top-level timing rates are always useful.
	const TSharedPtr<FJsonObject> Compact = ExecuteSequenceInfo(
		Registry, Fixture.GetAssetPath(), false);
	TestTrue(TEXT("compact response succeeds"),
		Compact.IsValid() && Compact->GetBoolField(TEXT("success")));
	TestFalse(TEXT("compact inspection leaves its fixture package clean"), Fixture.IsDirty());
	if (Compact.IsValid())
	{
		TestFalse(TEXT("compact response omits detail limits"), Compact->HasField(TEXT("detailLimits")));
		TestFalse(TEXT("compact response omits aggregate truncation"), Compact->HasField(TEXT("detailsTruncated")));

		const TArray<TSharedPtr<FJsonValue>>* CompactRoots = GetArrayField(Compact, TEXT("masterTracks"));
		const TSharedPtr<FJsonObject> CompactCamera = FindObjectByStringField(
			CompactRoots, TEXT("class"), TEXT("MovieSceneCameraCutTrack"));
		TestTrue(TEXT("compact response still lists the separate camera cut root"), CompactCamera.IsValid());
		if (CompactCamera.IsValid())
		{
			TestFalse(TEXT("compact root track omits sections"), CompactCamera->HasField(TEXT("sections")));
			TestFalse(TEXT("compact root track omits section truncation"), CompactCamera->HasField(TEXT("sectionsTruncated")));
		}

		const TSharedPtr<FJsonObject> CompactBinding = FindObjectByStringField(
			GetArrayField(Compact, TEXT("bindings")), TEXT("name"), TEXT("TimingActor"));
		const TSharedPtr<FJsonObject> CompactTransform = FindObjectByStringField(
			GetArrayField(CompactBinding, TEXT("tracks")), TEXT("class"), TEXT("MovieScene3DTransformTrack"));
		TestTrue(TEXT("compact response still lists binding tracks"), CompactTransform.IsValid());
		if (CompactTransform.IsValid())
		{
			TestFalse(TEXT("compact binding track omits sections"), CompactTransform->HasField(TEXT("sections")));
		}
	}

	const TSharedPtr<FJsonObject> Result = ExecuteSequenceInfo(
		Registry, Fixture.GetAssetPath(), true);
	TestTrue(TEXT("detailed response succeeds"),
		Result.IsValid() && Result->GetBoolField(TEXT("success")));
	TestFalse(TEXT("detailed inspection leaves its fixture package clean"), Fixture.IsDirty());
	if (!Result.IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject> DisplayRate = GetObjectField(Result, TEXT("displayRate"));
	const TSharedPtr<FJsonObject> TickResolution = GetObjectField(Result, TEXT("tickResolution"));
	TestTrue(TEXT("display rate is present"), DisplayRate.IsValid());
	TestTrue(TEXT("tick resolution is present"), TickResolution.IsValid());
	ExpectNumber(*this, DisplayRate, TEXT("numerator"), 24);
	ExpectNumber(*this, DisplayRate, TEXT("denominator"), 1);
	ExpectNumber(*this, TickResolution, TEXT("numerator"), 24000);
	ExpectNumber(*this, TickResolution, TEXT("denominator"), 1);

	const TSharedPtr<FJsonObject> PlaybackRange = GetObjectField(Result, TEXT("playbackRange"));
	TestTrue(TEXT("playback range is present"), PlaybackRange.IsValid());
	ExpectClosedRange(*this, PlaybackRange, 12000, 60000, 12.0, 60.0, 0.5, 2.5);
	// These two legacy fields remain in tick-resolution units for compatibility.
	ExpectNumber(*this, PlaybackRange, TEXT("startFrame"), 12000);
	ExpectNumber(*this, PlaybackRange, TEXT("endFrame"), 60000);

	const TArray<TSharedPtr<FJsonValue>>* RootTracks = GetArrayField(Result, TEXT("masterTracks"));
	const TSharedPtr<FJsonObject> CameraTrackObject = FindObjectByStringField(
		RootTracks, TEXT("class"), TEXT("MovieSceneCameraCutTrack"));
	const TSharedPtr<FJsonObject> FadeTrackObject = FindObjectByStringField(
		RootTracks, TEXT("class"), TEXT("MovieSceneFadeTrack"));
	TestTrue(TEXT("camera cut track is included with root tracks"), CameraTrackObject.IsValid());
	TestTrue(TEXT("ordinary root track is included"), FadeTrackObject.IsValid());
	const TSharedPtr<FJsonObject> FirstRootTrack = GetArrayObjectAt(RootTracks, 0);
	FString FirstRootClass;
	TestTrue(TEXT("camera cut track has deterministic first root priority"),
		FirstRootTrack.IsValid() &&
		FirstRootTrack->TryGetStringField(TEXT("class"), FirstRootClass) &&
		FirstRootClass == TEXT("MovieSceneCameraCutTrack"));

	const TSharedPtr<FJsonObject> CameraSectionObject = GetArrayObjectAt(
		GetArrayField(CameraTrackObject, TEXT("sections")), 0);
	TestTrue(TEXT("camera cut section details are present"), CameraSectionObject.IsValid());
	ExpectClosedRange(*this, CameraSectionObject, 24000, 48000, 24.0, 48.0, 1.0, 2.0);

	const TSharedPtr<FJsonObject> OpenSectionObject = GetArrayObjectAt(
		GetArrayField(FadeTrackObject, TEXT("sections")), 0);
	TestTrue(TEXT("open-bound section details are present"), OpenSectionObject.IsValid());
	if (OpenSectionObject.IsValid())
	{
		ExpectBool(*this, OpenSectionObject, TEXT("hasStart"), false);
		ExpectBool(*this, OpenSectionObject, TEXT("hasEnd"), true);
		ExpectBool(*this, OpenSectionObject, TEXT("endExclusive"), true);
		ExpectNumber(*this, OpenSectionObject, TEXT("endTick"), 36000);
		ExpectNumber(*this, OpenSectionObject, TEXT("endDisplayFrame"), 36.0);
		ExpectNumber(*this, OpenSectionObject, TEXT("endSeconds"), 1.5);
		TestFalse(TEXT("open start omits startInclusive"), OpenSectionObject->HasField(TEXT("startInclusive")));
		TestFalse(TEXT("open start omits startTick"), OpenSectionObject->HasField(TEXT("startTick")));
		TestFalse(TEXT("open start omits startDisplayFrame"), OpenSectionObject->HasField(TEXT("startDisplayFrame")));
		TestFalse(TEXT("open start omits startSeconds"), OpenSectionObject->HasField(TEXT("startSeconds")));
	}

	const TSharedPtr<FJsonObject> BindingObject = FindObjectByStringField(
		GetArrayField(Result, TEXT("bindings")), TEXT("name"), TEXT("TimingActor"));
	const TSharedPtr<FJsonObject> TransformTrackObject = FindObjectByStringField(
		GetArrayField(BindingObject, TEXT("tracks")), TEXT("class"), TEXT("MovieScene3DTransformTrack"));
	const TSharedPtr<FJsonObject> TransformSectionObject = GetArrayObjectAt(
		GetArrayField(TransformTrackObject, TEXT("sections")), 0);
	TestTrue(TEXT("binding transform section details are present"), TransformSectionObject.IsValid());
	ExpectClosedRange(*this, TransformSectionObject, 0, 72000, 0.0, 72.0, 0.0, 3.0);

	const TSharedPtr<FJsonObject> LocationXObject = FindObjectByStringField(
		GetArrayField(TransformSectionObject, TEXT("channels")), TEXT("name"), TEXT("Location.X"));
	TestTrue(TEXT("Location.X channel details are present"), LocationXObject.IsValid());
	if (LocationXObject.IsValid())
	{
		ExpectNumber(*this, LocationXObject, TEXT("keyCount"), 3);
		ExpectBool(*this, LocationXObject, TEXT("keyTimesTruncated"), false);
		FString ChannelType;
		TestTrue(TEXT("channel type is named"),
			LocationXObject->TryGetStringField(TEXT("type"), ChannelType) && !ChannelType.IsEmpty());

		const TArray<TSharedPtr<FJsonValue>>* KeyTimes = GetArrayField(LocationXObject, TEXT("keyTimes"));
		TestEqual(TEXT("all transform key times are returned"), KeyTimes ? KeyTimes->Num() : -1, 3);
		const TSharedPtr<FJsonObject> FractionalKey = GetArrayObjectAt(KeyTimes, 0);
		const TSharedPtr<FJsonObject> OneSecondKey = GetArrayObjectAt(KeyTimes, 1);
		const TSharedPtr<FJsonObject> LateKey = GetArrayObjectAt(KeyTimes, 2);
		ExpectNumber(*this, FractionalKey, TEXT("tick"), 500);
		ExpectNumber(*this, FractionalKey, TEXT("displayFrame"), 0.5);
		ExpectNumber(*this, FractionalKey, TEXT("seconds"), 1.0 / 48.0, 1.e-9);
		ExpectNumber(*this, OneSecondKey, TEXT("tick"), 24000);
		ExpectNumber(*this, OneSecondKey, TEXT("displayFrame"), 24.0);
		ExpectNumber(*this, OneSecondKey, TEXT("seconds"), 1.0);
		ExpectNumber(*this, LateKey, TEXT("tick"), 60000);
		ExpectNumber(*this, LateKey, TEXT("displayFrame"), 60.0);
		ExpectNumber(*this, LateKey, TEXT("seconds"), 2.5);
	}

	ExpectBool(*this, Result, TEXT("detailsTruncated"), false);
	Fixture.ClearDirtyState();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSequencerInfoKeyTimeLimitTest,
	"UE.MCP.Sequencer.GetSequenceInfo.KeyTimeLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSequencerInfoKeyTimeLimitTest::RunTest(const FString& Parameters)
{
	using namespace UEMCPSequencerTimingTests;

	FTransientSequenceAsset Fixture(TEXT("KeyLimit"));
	UMovieScene* MovieScene = Fixture.Get()->GetMovieScene();
	MovieScene->SetDisplayRate(FFrameRate(24, 1));
	MovieScene->SetTickResolutionDirectly(FFrameRate(24000, 1));

	const FGuid BindingGuid = MovieScene->AddPossessable(TEXT("CappedActor"), AActor::StaticClass());
	UMovieScene3DTransformTrack* TransformTrack =
		MovieScene->AddTrack<UMovieScene3DTransformTrack>(BindingGuid);
	UMovieScene3DTransformSection* Section = Cast<UMovieScene3DTransformSection>(
		AddSection(
			TransformTrack,
			TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(1000))));
	TestNotNull(TEXT("key limit fixture has a transform section"), Section);
	if (!Section)
	{
		return false;
	}

	FMovieSceneDoubleChannel* LocationX = Section->GetChannelProxy()
		.GetChannelByName<FMovieSceneDoubleChannel>(TEXT("Location.X"))
		.Get();
	TestNotNull(TEXT("key limit fixture has Location.X"), LocationX);
	if (!LocationX)
	{
		return false;
	}

	// Insert in reverse order. Output must sort before applying the 256-key cap.
	for (int32 Tick = 259; Tick >= 0; --Tick)
	{
		LocationX->AddLinearKey(FFrameNumber(Tick), Tick * 1.0);
	}
	Fixture.ClearDirtyState();

	FMCPHandlerRegistry Registry;
	FSequencerHandlers::RegisterHandlers(Registry);
	const TSharedPtr<FJsonObject> Result = ExecuteSequenceInfo(
		Registry, Fixture.GetAssetPath(), true);
	TestTrue(TEXT("key limit response succeeds"),
		Result.IsValid() && Result->GetBoolField(TEXT("success")));
	TestFalse(TEXT("key limit inspection leaves its fixture package clean"), Fixture.IsDirty());
	if (!Result.IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Limits = GetObjectField(Result, TEXT("detailLimits"));
	ExpectNumber(*this, Limits, TEXT("maxSections"), 256);
	ExpectNumber(*this, Limits, TEXT("maxChannels"), 1024);
	ExpectNumber(*this, Limits, TEXT("maxKeyTimes"), 4096);
	ExpectNumber(*this, Limits, TEXT("maxKeyTimesPerChannel"), 256);

	const TSharedPtr<FJsonObject> Binding = FindObjectByStringField(
		GetArrayField(Result, TEXT("bindings")), TEXT("name"), TEXT("CappedActor"));
	const TSharedPtr<FJsonObject> Track = FindObjectByStringField(
		GetArrayField(Binding, TEXT("tracks")), TEXT("class"), TEXT("MovieScene3DTransformTrack"));
	const TSharedPtr<FJsonObject> SectionObject = GetArrayObjectAt(
		GetArrayField(Track, TEXT("sections")), 0);
	const TSharedPtr<FJsonObject> Channel = FindObjectByStringField(
		GetArrayField(SectionObject, TEXT("channels")), TEXT("name"), TEXT("Location.X"));
	TestTrue(TEXT("capped Location.X output is present"), Channel.IsValid());
	if (Channel.IsValid())
	{
		ExpectNumber(*this, Channel, TEXT("keyCount"), 260);
		ExpectBool(*this, Channel, TEXT("keyTimesTruncated"), true);
		const TArray<TSharedPtr<FJsonValue>>* KeyTimes = GetArrayField(Channel, TEXT("keyTimes"));
		TestEqual(TEXT("per-channel key cap is applied"), KeyTimes ? KeyTimes->Num() : -1, 256);
		const TSharedPtr<FJsonObject> First = GetArrayObjectAt(KeyTimes, 0);
		const TSharedPtr<FJsonObject> Last = GetArrayObjectAt(KeyTimes, 255);
		ExpectNumber(*this, First, TEXT("tick"), 0);
		ExpectNumber(*this, Last, TEXT("tick"), 255);
	}
	ExpectBool(*this, Result, TEXT("detailsTruncated"), true);

	Fixture.ClearDirtyState();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSequencerInfoSectionLimitTest,
	"UE.MCP.Sequencer.GetSequenceInfo.SectionLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSequencerInfoSectionLimitTest::RunTest(const FString& Parameters)
{
	using namespace UEMCPSequencerTimingTests;

	FTransientSequenceAsset Fixture(TEXT("SectionLimit"));
	UMovieScene* MovieScene = Fixture.Get()->GetMovieScene();
	MovieScene->SetDisplayRate(FFrameRate(24, 1));
	MovieScene->SetTickResolutionDirectly(FFrameRate(24000, 1));

	UMovieSceneFadeTrack* FadeTrack = MovieScene->AddTrack<UMovieSceneFadeTrack>();
	TestNotNull(TEXT("section limit fixture has a root track"), FadeTrack);
	if (!FadeTrack)
	{
		return false;
	}

	for (int32 Index = 0; Index < 257; ++Index)
	{
		const FFrameNumber Start(Index * 1000);
		const FFrameNumber End((Index + 1) * 1000);
		if (!AddSection(FadeTrack, TRange<FFrameNumber>(Start, End)))
		{
			AddError(FString::Printf(TEXT("failed to create section %d"), Index));
			return false;
		}
	}
	Fixture.ClearDirtyState();

	FMCPHandlerRegistry Registry;
	FSequencerHandlers::RegisterHandlers(Registry);
	const TSharedPtr<FJsonObject> Result = ExecuteSequenceInfo(
		Registry, Fixture.GetAssetPath(), true);
	TestTrue(TEXT("section limit response succeeds"),
		Result.IsValid() && Result->GetBoolField(TEXT("success")));
	TestFalse(TEXT("section limit inspection leaves its fixture package clean"), Fixture.IsDirty());
	if (!Result.IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Track = FindObjectByStringField(
		GetArrayField(Result, TEXT("masterTracks")), TEXT("class"), TEXT("MovieSceneFadeTrack"));
	TestTrue(TEXT("capped root track output is present"), Track.IsValid());
	if (Track.IsValid())
	{
		ExpectNumber(*this, Track, TEXT("sectionCount"), 257);
		ExpectBool(*this, Track, TEXT("sectionsTruncated"), true);
		const TArray<TSharedPtr<FJsonValue>>* Sections = GetArrayField(Track, TEXT("sections"));
		TestEqual(TEXT("global section cap is applied"), Sections ? Sections->Num() : -1, 256);

		const TSharedPtr<FJsonObject> First = GetArrayObjectAt(Sections, 0);
		const TSharedPtr<FJsonObject> Last = GetArrayObjectAt(Sections, 255);
		ExpectNumber(*this, First, TEXT("index"), 0);
		ExpectNumber(*this, First, TEXT("startTick"), 0);
		ExpectNumber(*this, Last, TEXT("index"), 255);
		ExpectNumber(*this, Last, TEXT("startTick"), 255000);
	}
	ExpectBool(*this, Result, TEXT("detailsTruncated"), true);

	Fixture.ClearDirtyState();
	return true;
}

#endif
