#pragma once

// The Live Coding decisions of an editor launched with -MCPNoIde, in a header
// so the automation test reaches the same pure functions the bridge calls.
//
// The only public way to hide the Live Coding console is
// ILiveCodingModule::EnableForSession(false), which leaves Live Coding running
// but no longer enabled for the session. Every compile enables it again and
// shows the console, so the bridge hides it after each one. The two engine
// paths that only use Live Coding while it is enabled for the session (a new
// class from AddCodeToProject, and the bridge's hot_reload) are told apart
// here, so they keep compiling through Live Coding.

#include "CoreMinimal.h"

/**
 * Whether the bridge hides the Live Coding console on this tick.
 *
 * A compile that waits for completion starts and ends inside one game-thread
 * call, so no tick ever sees it compiling: the rule cannot be "a compile just
 * ended". It hides whenever Live Coding is enabled for the session and has
 * been idle for a whole tick, which also lets the module's own tick finish
 * loading the patch before the console goes away.
 *
 * @param bNoIde The editor was launched with -MCPNoIde.
 * @param bEnabledForSession Live Coding is enabled for the session now.
 * @param bWasCompiling Live Coding was compiling on the previous tick.
 * @param bIsCompiling Live Coding is compiling now.
 * @return True when the console should be hidden now.
 */
inline bool MCPShouldHideLiveCodingConsole(bool bNoIde, bool bEnabledForSession, bool bWasCompiling, bool bIsCompiling)
{
	return bNoIde && bEnabledForSession && !bWasCompiling && !bIsCompiling;
}

/**
 * Whether create_cpp_class enables Live Coding for the session before adding
 * the class. AddCodeToProject compiles a new class through Live Coding only
 * when it is enabled for the session, and falls back to the engine's classic
 * hot reload otherwise; after a hide it is started but not enabled.
 *
 * @param bNoIde The editor was launched with -MCPNoIde.
 * @param bHasStarted Live Coding has started in this session.
 * @param bEnabledForSession Live Coding is enabled for the session now.
 * @return True when Live Coding should be enabled for the session first.
 */
inline bool MCPShouldEnableLiveCodingBeforeNewClass(bool bNoIde, bool bHasStarted, bool bEnabledForSession)
{
	return bNoIde && bHasStarted && !bEnabledForSession;
}

/**
 * Whether hot_reload compiles through the Live Coding module rather than its
 * console-command fallback. Without -MCPNoIde this is exactly "enabled for the
 * session"; with it, a Live Coding the bridge has hidden counts as well.
 *
 * @param bNoIde The editor was launched with -MCPNoIde.
 * @param bHasStarted Live Coding has started in this session.
 * @param bEnabledForSession Live Coding is enabled for the session now.
 * @return True when hot_reload should use the Live Coding module.
 */
inline bool MCPHotReloadUsesLiveCodingModule(bool bNoIde, bool bHasStarted, bool bEnabledForSession)
{
	return bEnabledForSession || (bNoIde && bHasStarted);
}

/**
 * The enabledForSession that live_coding_status reports. With -MCPNoIde a
 * started Live Coding the bridge has hidden can still compile, since the next
 * compile enables it for the session again, so it reports as enabled. Without
 * the switch this is exactly the module's own answer.
 *
 * @param bNoIde The editor was launched with -MCPNoIde.
 * @param bHasStarted Live Coding has started in this session.
 * @param bEnabledForSession Live Coding is enabled for the session now.
 * @return The value live_coding_status reports as enabledForSession.
 */
inline bool MCPReportsLiveCodingEnabledForSession(bool bNoIde, bool bHasStarted, bool bEnabledForSession)
{
	return bEnabledForSession || (bNoIde && bHasStarted);
}
