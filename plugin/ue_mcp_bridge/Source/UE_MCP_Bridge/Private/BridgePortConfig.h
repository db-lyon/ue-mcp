#pragma once

// Which port the bridge binds: the command line, the environment, the client's
// requested.json, `ue-mcp.bridge.port` from the layered config, or a port derived
// from the project path. Policy only; the socket server binds what it is handed.

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

	/**
	 * Ports the collision walk steps over: those the project's Unreal Canvas
	 * Bridge claims when it is enabled. Resolved with the choice, on the game
	 * thread, so the server thread never asks the plugin manager.
	 */
	TArray<int32> AvoidPorts;
};

namespace MCPBridgePortPolicy
{
	/**
	 * The port to bind first: -MCPPort= > UE_MCP_PORT > the port the client
	 * published in requested.json > `ue-mcp.bridge.port` from the layered config
	 * > the derived port. The client walks the same order (#819). requested.json
	 * (#817) sits above the config read because it is the same answer computed
	 * from more inputs (four config layers plus environment, none of which an
	 * editor launched from Explorer can see). A pin on a Canvas port falls back
	 * to the derived port. Call on the game thread.
	 */
	FMCPBridgePortChoice Resolve();

	/** The per-project port the wire contract derives from a project root. */
	int32 DeriveProjectPort(const FString& ProjectRootDir);
}
