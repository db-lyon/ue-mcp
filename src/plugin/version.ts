/**
 * Minimal semver compare for plugin minServerVersion gating.
 * Accepts X, X.Y, X.Y.Z, with an optional pre-release suffix that compares
 * lexicographically against an absent suffix as "lower" per semver semantics.
 */

export interface ParsedVersion {
  major: number;
  minor: number;
  patch: number;
  pre?: string;
}

export function parseVersion(v: string): ParsedVersion | null {
  const m = /^(\d+)(?:\.(\d+))?(?:\.(\d+))?(?:-(.+))?$/.exec(v.trim());
  if (!m) return null;
  return {
    major: Number(m[1]),
    minor: m[2] ? Number(m[2]) : 0,
    patch: m[3] ? Number(m[3]) : 0,
    pre: m[4],
  };
}

/** -1 if a<b, 0 if a==b, 1 if a>b. */
export function compareVersions(a: string, b: string): number {
  const av = parseVersion(a);
  const bv = parseVersion(b);
  if (!av || !bv) {
    return a === b ? 0 : a < b ? -1 : 1;
  }
  if (av.major !== bv.major) return av.major < bv.major ? -1 : 1;
  if (av.minor !== bv.minor) return av.minor < bv.minor ? -1 : 1;
  if (av.patch !== bv.patch) return av.patch < bv.patch ? -1 : 1;
  if (av.pre === bv.pre) return 0;
  if (!av.pre) return 1;
  if (!bv.pre) return -1;
  return av.pre < bv.pre ? -1 : 1;
}

/** Returns true if `current` satisfies `>= required`. */
export function satisfiesMinimum(current: string, required: string): boolean {
  return compareVersions(current, required) >= 0;
}
