/**
 * The lock-management actions themselves, which must never take a lock.
 *
 * They change the editor's lock registry, so they declare `mutate` and the
 * routing gate is right to treat them as one. Locking is a different question:
 * taking an asset lock around `asset(lock)` would have the locking module
 * acquire a lock in order to acquire a lock. The exclusion was invisible before
 * because "lock" and "unlock" simply were not in the verb list; it is stated
 * here rather than left to an omission nobody could see.
 *
 * A leaf module, so the batch action can refuse these as steps without
 * importing the locking module, which reads the tool graph the batch is part of.
 */
export const NEVER_LOCKED: ReadonlySet<string> = new Set(["asset.lock", "asset.unlock", "asset.unlock_all", "asset.list_locks"]);
