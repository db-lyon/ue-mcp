// Replay input lock (lock_input): which physical events are blocked while a
// replay runs and which always get through.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/PIE/PIEReplayInputLock.h"

namespace
{
	MCPPIE::FReplayLockInputEvent KeyEvent(const FKey& Key, bool bViewportFocused, bool bInsideViewport = false)
	{
		MCPPIE::FReplayLockInputEvent E;
		E.Kind = MCPPIE::EReplayLockInput::Key;
		E.Key = Key;
		E.bViewportFocused = bViewportFocused;
		E.bInsideViewport = bInsideViewport;
		return E;
	}

	MCPPIE::FReplayLockInputEvent PointerEvent(MCPPIE::EReplayLockInput Kind, bool bInsideViewport, bool bInjected = false)
	{
		MCPPIE::FReplayLockInputEvent E;
		E.Kind = Kind;
		E.bInsideViewport = bInsideViewport;
		E.bInjected = bInjected;
		return E;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEReplayInputLockRuleTest,
	"UE.MCP.PIE.ReplayInputLock.Rule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEReplayInputLockRuleTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIE;

	// Keyboard follows focus.
	TestTrue(TEXT("key into the focused viewport is blocked"), ShouldReplayLockBlock(KeyEvent(EKeys::W, true)));
	TestFalse(TEXT("key typed elsewhere in the editor passes"), ShouldReplayLockBlock(KeyEvent(EKeys::W, false)));

	// Escape hatches.
	TestFalse(TEXT("Esc passes"), ShouldReplayLockBlock(KeyEvent(EKeys::Escape, true)));
	TestFalse(TEXT("F8 passes"), ShouldReplayLockBlock(KeyEvent(EKeys::F8, true)));
	FReplayLockInputEvent ShiftF1 = KeyEvent(EKeys::F1, true);
	ShiftF1.bShift = true;
	TestFalse(TEXT("Shift+F1 passes"), ShouldReplayLockBlock(ShiftF1));
	TestTrue(TEXT("F1 without Shift is blocked"), ShouldReplayLockBlock(KeyEvent(EKeys::F1, true)));

	// Releases always pass so nothing is left held.
	FReplayLockInputEvent Release = KeyEvent(EKeys::W, true);
	Release.bRelease = true;
	TestFalse(TEXT("key release passes"), ShouldReplayLockBlock(Release));

	// Gamepad is blocked regardless of focus.
	TestTrue(TEXT("gamepad button is blocked"), ShouldReplayLockBlock(KeyEvent(EKeys::Gamepad_FaceButton_Bottom, false)));
	FReplayLockInputEvent Stick;
	Stick.Kind = EReplayLockInput::Analog;
	Stick.Key = EKeys::Gamepad_LeftX;
	TestTrue(TEXT("gamepad stick is blocked"), ShouldReplayLockBlock(Stick));

	// Mouse follows the pointer, so the editor's own controls stay usable.
	TestTrue(TEXT("mouse look inside the viewport is blocked"), ShouldReplayLockBlock(PointerEvent(EReplayLockInput::MouseMove, true)));
	TestFalse(TEXT("mouse outside the viewport passes"), ShouldReplayLockBlock(PointerEvent(EReplayLockInput::MouseMove, false)));
	TestTrue(TEXT("wheel inside the viewport is blocked"), ShouldReplayLockBlock(PointerEvent(EReplayLockInput::MouseWheel, true)));
	TestTrue(TEXT("click inside the viewport is blocked"), ShouldReplayLockBlock(KeyEvent(EKeys::LeftMouseButton, true, true)));
	TestFalse(TEXT("click outside the viewport passes"), ShouldReplayLockBlock(KeyEvent(EKeys::LeftMouseButton, true, false)));

	// Replayed events are never blocked.
	TestFalse(TEXT("replayed move passes"), ShouldReplayLockBlock(PointerEvent(EReplayLockInput::MouseMove, true, true)));
	return true;
}

#endif
