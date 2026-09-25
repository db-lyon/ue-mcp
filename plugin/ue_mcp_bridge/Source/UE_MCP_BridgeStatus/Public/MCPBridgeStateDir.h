#pragma once

#include "CoreMinimal.h"
#include "Misc/Paths.h"

namespace UEMCP
{
	/**
	 * <Project>/Saved/UE_MCP_Bridge, where every record the server reads is
	 * published: port.json, bridge-error.json, requested.json, instances/ and
	 * status files. Anchored on ProjectDir, not ProjectSavedDir, because
	 * -UserDir moves the latter and the server always reads the project's own
	 * Saved folder (#1021).
	 */
	inline FString BridgeStateDir()
	{
		return FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved"), TEXT("UE_MCP_Bridge"));
	}
}
