#pragma once

// Which port the bridge binds: the command line, the environment, the client's
// requested.json, `ue-mcp.bridge.port` from the layered config, or a port derived
// from the project path. The resolvers are FMCPBridgeServer statics.

#include "CoreMinimal.h"

/**
 * The port the bridge will try to bind, and where that number came from.
 *
 * The origin is carried alongside the number because the collision walk can
 * move it. A user who pinned a port needs the log to say the pin did not take
 * and what the bridge landed on, which is not something a bare int can say.
 */
struct FMCPBridgePortChoice
{
	/** The port to bind first. The walk in Run() starts here. */
	int32 Port = 0;

	/** Human-readable origin, for the one log line a user greps for. */
	FString Source = TEXT("unknown");

	/**
	 * True when a human asked for this exact number (command line, environment,
	 * or bridge.port in a config file). Losing a pinned port to a collision is
	 * a warning; losing a derived one is routine.
	 */
	bool bPinned = false;
};
