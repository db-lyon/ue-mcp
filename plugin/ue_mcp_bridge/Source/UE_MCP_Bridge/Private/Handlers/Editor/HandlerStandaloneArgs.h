#pragma once

// The command line editor(launch_standalone_game) gives the game process, in a
// header so the automation test reaches the same pure function the handler
// calls.

#include "CoreMinimal.h"
#include "Misc/Parse.h"

/**
 * Appends the offscreen switch and the caller's extra arguments to the
 * standalone game's base arguments.
 *
 * An editor launched with -RenderOffScreen puts no window on screen, and a
 * game it launches inherits that: the game gets -RenderOffScreen too, unless
 * ExtraArgs already holds the switch. "Holds" is FParse::Param's rule: a
 * whole token with a '-' or '/' prefix, any case, not inside quotes.
 *
 * @param BaseArgs The arguments the handler built (project, map, -game, window size, trace).
 * @param ExtraArgs The caller's extraArgs, appended as given. May be empty.
 * @param bEditorOffScreen The editor itself runs with -RenderOffScreen.
 * @return BaseArgs, then " -RenderOffScreen" when inherited, then " " and ExtraArgs when given.
 */
inline FString MCPStandaloneGameArgs(const FString& BaseArgs, const FString& ExtraArgs, bool bEditorOffScreen)
{
	FString Args = BaseArgs;
	if (bEditorOffScreen && !FParse::Param(*ExtraArgs, TEXT("RenderOffScreen")))
	{
		Args += TEXT(" -RenderOffScreen");
	}
	if (!ExtraArgs.IsEmpty())
	{
		Args += TEXT(" ") + ExtraArgs;
	}
	return Args;
}
