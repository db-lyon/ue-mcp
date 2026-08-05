#pragma once

#include "CoreMinimal.h"

/**
 * Wires the editor-only sensors into FMCPEngineStatus, which lives in the
 * UE_MCP_BridgeStatus module and has been publishing since PostConfigInit on
 * Core-only hooks.
 *
 * Split this way because the snapshot has to exist long before Slate, the
 * asset compiler or the shader compiler do. Once the bridge module loads, the
 * same snapshot gains:
 *
 *   - Slate's pre-tick, which keeps firing while a long operation pumps the UI;
 *   - the modal loop tick, which fires while a dialog owns the main loop (and
 *     is also where modal-safe handlers get their chance to run);
 *   - a description of the active modal dialog, from the Slate widget walk;
 *   - remaining shader jobs and asset compiles.
 */
namespace FMCPEngineStatusHooks
{
	/** Register the Slate hooks and the Slate/Engine-backed providers. */
	void Install();

	/** Unregister everything registered by Install. Safe to call twice. */
	void Remove();
}
