import crypto from "node:crypto";

// Deterministic per-project bridge port. Multiple checkouts of a project (or
// several unrelated projects) on one machine each need their own editor bridge
// port so their npm clients don't collide on a single fixed number. Deriving
// the port from a hash of the project root path gives every worktree a stable,
// launch-order-independent port that the Node client and the C++ bridge can
// both compute without any coordination step.
//
// The C++ bridge computes the identical value in FMCPBridgeServer::DeriveProjectPort
// (BridgeServer.cpp). If the two ever disagree (path normalization drift, etc.)
// the per-project port.json lockfile the bridge publishes remains the
// authoritative source of the actual bound port, so connectivity is never at
// the mercy of an exact hash match. Keep the two implementations in lockstep.

/** Legacy fixed port, retained as the ultimate fallback when no project root is known. */
export const DEFAULT_BRIDGE_PORT = 9877;

// IANA-registered dynamic/ephemeral range. 16384 ports of spread makes a
// same-machine collision between two projects unlikely; the bridge probes
// upward from the derived base to resolve the rare clash.
const EPHEMERAL_BASE = 49152;
const EPHEMERAL_SPAN = 65535 - EPHEMERAL_BASE + 1; // 16384

/**
 * Canonical form of a project root directory for hashing. Must match the C++
 * side byte-for-byte: forward slashes, no trailing slash, lowercased (the
 * filesystems we target are case-insensitive, and lowercasing also folds
 * Windows drive-letter casing like `C:` vs `c:`).
 */
export function normalizeProjectRoot(dir: string): string {
  return dir.replace(/\\/g, "/").replace(/\/+$/, "").toLowerCase();
}

/**
 * Derive the bridge base port from a project root directory. SHA-1 of the
 * normalized path, first 4 bytes folded into the ephemeral range. SHA-1 (not
 * SHA-256) because it is trivially available on both sides — Node's crypto and
 * UE's FSHA1 — and the hash is used only for port spreading, never security.
 */
export function deriveProjectPort(projectRootDir: string): number {
  const norm = normalizeProjectRoot(projectRootDir);
  const h = crypto.createHash("sha1").update(norm, "utf8").digest();
  const v = ((h[0] << 24) | (h[1] << 16) | (h[2] << 8) | h[3]) >>> 0;
  return EPHEMERAL_BASE + (v % EPHEMERAL_SPAN);
}
