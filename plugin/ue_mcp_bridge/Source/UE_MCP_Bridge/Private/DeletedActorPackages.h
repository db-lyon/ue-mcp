#pragma once

#include "CoreMinimal.h"

/**
 * Keeps the external package of a deleted one-file-per-actor actor in memory until it is saved (#1545).
 *
 * Deleting such an actor leaves its package dirty and empty, and the save is what deletes the file. Nothing
 * else references that package once the actor is collected, so a garbage collection between the delete and
 * the save dropped it, and no save could find it: the file stayed on disk and the actor came back on reload.
 * Released once the package is clean (saved or deleted) or its world is torn down.
 */
namespace FMCPDeletedActorPackages
{
	/** Start tracking editor actor deletions. Called once from StartupModule. */
	void Install();

	/** Stop tracking and release every held package. Safe to call twice. */
	void Remove();

	/** Number of packages currently held, for tests. */
	int32 NumHeld();
}
