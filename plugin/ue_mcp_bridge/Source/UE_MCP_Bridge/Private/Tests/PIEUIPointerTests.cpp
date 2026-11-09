// Cursor and UI pointer recording (#1822): viewport-relative positions, move
// coalescing, step building, and the ui_input step round-tripping through
// sequence.json.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/PIE/PIESequenceFormat.h"
#include "Handlers/PIE/PIEUIPointer.h"
#include "Dom/JsonObject.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEUIPointerViewportRelativeTest,
	"UE.MCP.PIE.UIPointer.ViewportRelative",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEUIPointerViewportRelativeTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIE;
	const FVector2D Origin(100.0, 50.0);
	const FVector2D Size(800.0, 600.0);

	FVector2D Rel;
	TestTrue(TEXT("centre is inside"), ScreenToViewportRelative(FVector2D(500.0, 350.0), Origin, Size, Rel));
	TestEqual(TEXT("centre x"), Rel.X, 0.5);
	TestEqual(TEXT("centre y"), Rel.Y, 0.5);

	TestTrue(TEXT("top-left corner is inside"), ScreenToViewportRelative(Origin, Origin, Size, Rel));
	TestTrue(TEXT("corner is 0,0"), Rel.X == 0.0 && Rel.Y == 0.0);

	TestFalse(TEXT("left of the viewport is outside"), ScreenToViewportRelative(FVector2D(99.0, 350.0), Origin, Size, Rel));
	TestTrue(TEXT("outside still reports a position"), Rel.X < 0.0);

	TestFalse(TEXT("zero-sized viewport"), ScreenToViewportRelative(FVector2D(1.0, 1.0), Origin, FVector2D::ZeroVector, Rel));

	// A recording made at one size replays at the same relative spot in another.
	const FVector2D OtherOrigin(0.0, 0.0);
	const FVector2D OtherSize(1920.0, 1080.0);
	ScreenToViewportRelative(FVector2D(300.0, 200.0), Origin, Size, Rel);
	const FVector2D Back = ViewportRelativeToScreen(Rel, OtherOrigin, OtherSize);
	TestEqual(TEXT("x scales with the viewport"), Back.X, 0.25 * 1920.0);
	TestEqual(TEXT("y scales with the viewport"), Back.Y, 0.25 * 1080.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEUIPointerCoalesceTest,
	"UE.MCP.PIE.UIPointer.MovesCoalescePerFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEUIPointerCoalesceTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIE;
	TArray<FUIPointerSample> Samples;
	auto Sample = [](EUIPointerEvent E, uint64 Frame, double X)
	{
		FUIPointerSample S;
		S.Event = E;
		S.FrameNumber = Frame;
		S.Position = FVector2D(X, 0.5);
		return S;
	};

	AppendUIPointerSample(Samples, Sample(EUIPointerEvent::Move, 10, 0.1));
	AppendUIPointerSample(Samples, Sample(EUIPointerEvent::Move, 10, 0.2));
	AppendUIPointerSample(Samples, Sample(EUIPointerEvent::Move, 10, 0.3));
	TestEqual(TEXT("three moves in one frame are one sample"), Samples.Num(), 1);
	TestEqual(TEXT("the latest position wins"), Samples[0].Position.X, 0.3);

	AppendUIPointerSample(Samples, Sample(EUIPointerEvent::Move, 11, 0.4));
	TestEqual(TEXT("a move in the next frame is kept"), Samples.Num(), 2);

	AppendUIPointerSample(Samples, Sample(EUIPointerEvent::Down, 11, 0.4));
	AppendUIPointerSample(Samples, Sample(EUIPointerEvent::Move, 11, 0.5));
	TestEqual(TEXT("a move after a press is not folded into the move before it"), Samples.Num(), 4);
	TestTrue(TEXT("press keeps its place"), Samples[2].Event == EUIPointerEvent::Down);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEUIPointerBuildStepsTest,
	"UE.MCP.PIE.UIPointer.BuildSteps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEUIPointerBuildStepsTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIE;
	TArray<FUIPointerSample> Samples;
	auto Add = [&Samples](EUIPointerEvent E, double T, const TCHAR* Button = TEXT(""), float Wheel = 0.f)
	{
		FUIPointerSample S;
		S.Event = E;
		S.RealTime = T;
		S.FrameNumber = static_cast<uint64>(T * 1000.0);
		S.Position = FVector2D(0.25, 0.75);
		S.Button = Button;
		S.WheelDelta = Wheel;
		S.Modifiers = UIModifier::Shift;
		Samples.Add(S);
	};
	Add(EUIPointerEvent::Move, 9.0);                          // before the first row: dropped
	Add(EUIPointerEvent::Move, 10.25);
	Add(EUIPointerEvent::Down, 10.5, TEXT("LeftMouseButton"));
	Add(EUIPointerEvent::Up, 10.6, TEXT("LeftMouseButton"));
	Add(EUIPointerEvent::Wheel, 11.0, TEXT(""), -1.f);

	TArray<FStep> Steps;
	BuildUIPointerSteps(Samples, 10.0, Steps);
	if (!TestEqual(TEXT("sample before the base is dropped"), Steps.Num(), 4)) return false;

	TestTrue(TEXT("type"), Steps[0].Type == EStepType::UIInput);
	TestEqual(TEXT("move delay"), Steps[0].DelayMs, 250);
	TestEqual(TEXT("move event"), Steps[0].UIEvent, FString(TEXT("mouse_move")));
	TestTrue(TEXT("move carries no button"), Steps[0].UIKey.IsEmpty());
	TestEqual(TEXT("x"), Steps[0].PointerX, 0.25);
	TestEqual(TEXT("y"), Steps[0].PointerY, 0.75);
	TestTrue(TEXT("modifiers"), Steps[0].UIModifiers == UIModifier::Shift);

	TestEqual(TEXT("press delay"), Steps[1].DelayMs, 500);
	TestEqual(TEXT("press button"), Steps[1].UIKey, FString(TEXT("LeftMouseButton")));
	TestEqual(TEXT("release event"), Steps[2].UIEvent, FString(TEXT("mouse_up")));
	TestEqual(TEXT("wheel delta"), Steps[3].WheelDelta, -1.0);
	TestTrue(TEXT("wheel carries no button"), Steps[3].UIKey.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEUIPointerSequenceRoundTripTest,
	"UE.MCP.PIE.UIPointer.SequenceRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEUIPointerSequenceRoundTripTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIE;
	FSequence In;
	In.SampleHz = 60;
	FStep Down;
	Down.Type = EStepType::UIInput;
	Down.DelayMs = 1200;
	Down.UIEvent = TEXT("mouse_down");
	Down.PointerX = 0.4213;
	Down.PointerY = 0.6012;
	Down.UIKey = TEXT("LeftMouseButton");
	Down.UIModifiers = UIModifier::Shift | UIModifier::Control;
	In.Steps.Add(Down);
	FStep Wheel;
	Wheel.Type = EStepType::UIInput;
	Wheel.DelayMs = 1300;
	Wheel.UIEvent = TEXT("mouse_wheel");
	Wheel.PointerX = 0.5;
	Wheel.PointerY = 0.5;
	Wheel.WheelDelta = 2.0;
	In.Steps.Add(Wheel);

	const TSharedRef<FJsonObject> Json = SequenceToJson(In);
	const TArray<TSharedPtr<FJsonValue>>& StepsJson = Json->GetArrayField(TEXT("steps"));
	TestEqual(TEXT("written type"), StepsJson[0]->AsObject()->GetStringField(TEXT("type")), FString(TEXT("ui_input")));
	TestFalse(TEXT("wheel step has no button field"), StepsJson[1]->AsObject()->HasField(TEXT("button")));
	TestFalse(TEXT("unmodified step has no modifiers field"), StepsJson[1]->AsObject()->HasField(TEXT("modifiers")));

	FSequence Out;
	FString Err;
	if (!TestTrue(TEXT("parses"), SequenceFromJson(Json, Out, Err))) { AddError(Err); return false; }
	if (!TestEqual(TEXT("both steps read back"), Out.Steps.Num(), 2)) return false;
	const FStep& D = Out.Steps[0];
	TestTrue(TEXT("type"), D.Type == EStepType::UIInput);
	TestEqual(TEXT("delay"), D.DelayMs, 1200);
	TestEqual(TEXT("event"), D.UIEvent, FString(TEXT("mouse_down")));
	TestEqual(TEXT("x"), D.PointerX, 0.4213);
	TestEqual(TEXT("y"), D.PointerY, 0.6012);
	TestEqual(TEXT("button"), D.UIKey, FString(TEXT("LeftMouseButton")));
	TestTrue(TEXT("modifiers"), D.UIModifiers == (UIModifier::Shift | UIModifier::Control));
	TestEqual(TEXT("wheel delta"), Out.Steps[1].WheelDelta, 2.0);

	// Inline steps go through the same parser, so a malformed one is refused
	// with a reason rather than silently skipped.
	auto ParseOne = [](const TSharedRef<FJsonObject>& StepObj, FString& OutErr)
	{
		TSharedRef<FJsonObject> Env = MakeShared<FJsonObject>();
		Env->SetNumberField(TEXT("version"), kFormatVersion);
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Add(MakeShared<FJsonValueObject>(StepObj));
		Env->SetArrayField(TEXT("steps"), Arr);
		FSequence S;
		return SequenceFromJson(Env, S, OutErr);
	};
	TSharedRef<FJsonObject> BadEvent = MakeShared<FJsonObject>();
	BadEvent->SetStringField(TEXT("type"), TEXT("ui_input"));
	BadEvent->SetStringField(TEXT("event"), TEXT("mouse_teleport"));
	TestFalse(TEXT("unknown event is refused"), ParseOne(BadEvent, Err));
	TestTrue(TEXT("error names the event"), Err.Contains(TEXT("mouse_teleport")));

	TSharedRef<FJsonObject> NoButton = MakeShared<FJsonObject>();
	NoButton->SetStringField(TEXT("type"), TEXT("ui_input"));
	NoButton->SetStringField(TEXT("event"), TEXT("mouse_up"));
	TestFalse(TEXT("release without a button is refused"), ParseOne(NoButton, Err));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEUIPointerModifiersTest,
	"UE.MCP.PIE.UIPointer.Modifiers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEUIPointerModifiersTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIE;
	const uint8 All = UIModifier::Shift | UIModifier::Control | UIModifier::Alt | UIModifier::Command;
	TestTrue(TEXT("round trip"), UIModifiersFromStrings(UIModifiersToStrings(All)) == All);
	TestEqual(TEXT("none"), UIModifiersToStrings(0).Num(), 0);
	TestTrue(TEXT("ctrl alias and case"), UIModifiersFromStrings({ TEXT("CTRL"), TEXT("Shift") })
		== (UIModifier::Control | UIModifier::Shift));
	TestTrue(TEXT("unknown names ignored"), UIModifiersFromStrings({ TEXT("hyper") }) == 0);
	return true;
}

#endif
