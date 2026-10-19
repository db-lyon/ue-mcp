#pragma once

// The shutdown decision for the editor's background CPU throttle, in a header
// so the automation test reaches the same pure function the module calls.

#include "CoreMinimal.h"

/**
 * Whether the bridge writes the user's throttle preference back at shutdown.
 *
 * An editor launched with -RenderOffScreen has "Use Less CPU when in
 * Background" turned off in memory, and the bridge never saves it. Other
 * editor code saves the performance settings whole (the scalability panel,
 * the performance monitor, the settings editor), and that would write the
 * session's false into the per-user ini every project on this engine version
 * shares. The bridge then puts the user's true back.
 *
 * @param bTurnedOff This session turned the throttle off.
 * @param bUserValue The throttle value as read from config before it was turned off.
 * @param bConfigHoldsFalse The config now holds false for the throttle.
 * @return True only when the session turned it off, the user had it on, and
 *         the saved config now says off.
 */
inline bool MCPShouldRestoreThrottle(bool bTurnedOff, bool bUserValue, bool bConfigHoldsFalse)
{
	return bTurnedOff && bUserValue && bConfigHoldsFalse;
}
