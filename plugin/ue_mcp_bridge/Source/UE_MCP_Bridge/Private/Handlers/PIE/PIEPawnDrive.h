#pragma once

#include "CoreMinimal.h"

namespace MCPPIE
{
	/**
	 * Drives a PIE player's pawn over later frames for pie(drive_pawn): movement input in a direction or toward a
	 * point, or a navmesh move, sampling the pawn as it goes. The pawn is resolved again every frame, so a drive
	 * follows the player through a level change (#1565).
	 */
	namespace PIEPawnDrive
	{
		/** True while any drive is running; keeps the editor from throttling an unfocused window. */
		bool IsActive();

		/** Ends every drive and withdraws the ticker and the EndPIE binding. */
		void Shutdown();
	}
}
