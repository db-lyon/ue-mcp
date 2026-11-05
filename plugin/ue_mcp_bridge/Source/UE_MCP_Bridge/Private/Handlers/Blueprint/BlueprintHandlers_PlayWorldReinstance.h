#pragma once

#include "CoreMinimal.h"

namespace MCPBlueprintPlayWorld
{
	/** Binds the editor's compile events once so a pawn reinstanced during play
	 *  keeps its player state's OnDestroyed binding exactly once (#1600). */
	void RegisterPossessedPawnRebindGuard();
}
