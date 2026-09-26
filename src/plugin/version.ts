/**
 * Semver precedence, shared by the minServerVersion gate and the npm upgrade
 * check. Accepts X, X.Y or X.Y.Z with an optional prerelease; build metadata
 * is ignored, as semver requires.
 */

export interface ParsedVersion {
  major: number;
  minor: number;
  patch: number;
  pre?: string;
}

export function parseVersion(v: string): ParsedVersion | null {
  const m = /^(\d+)(?:\.(\d+))?(?:\.(\d+))?(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$/.exec(v.trim());
  if (!m) return null;
  return {
    major: Number(m[1]),
    minor: m[2] ? Number(m[2]) : 0,
    patch: m[3] ? Number(m[3]) : 0,
    pre: m[4],
  };
}

/**
 * Prerelease precedence per semver 11.4: dot-separated identifiers compared
 * left to right, numeric ones numerically and below alphanumeric ones, and a
 * shorter list lower when its identifiers all match. No prerelease ranks highest.
 */
export function comparePrerelease(a: string | undefined, b: string | undefined): number {
  if (a === b) return 0;
  if (a === undefined) return 1;
  if (b === undefined) return -1;
  const left = a.split(".");
  const right = b.split(".");
  for (let i = 0; i < Math.max(left.length, right.length); i++) {
    const l = left[i];
    const r = right[i];
    if (l === undefined) return -1;
    if (r === undefined) return 1;
    const lNum = /^\d+$/.test(l);
    const rNum = /^\d+$/.test(r);
    if (lNum && rNum) {
      const d = Number(l) - Number(r);
      if (d !== 0) return d < 0 ? -1 : 1;
      continue;
    }
    if (lNum !== rNum) return lNum ? -1 : 1;
    if (l !== r) return l < r ? -1 : 1;
  }
  return 0;
}

/** -1 if a<b, 0 if a==b, 1 if a>b. Unparseable input falls back to a string compare. */
export function compareVersions(a: string, b: string): number {
  const av = parseVersion(a);
  const bv = parseVersion(b);
  if (!av || !bv) {
    return a === b ? 0 : a < b ? -1 : 1;
  }
  if (av.major !== bv.major) return av.major < bv.major ? -1 : 1;
  if (av.minor !== bv.minor) return av.minor < bv.minor ? -1 : 1;
  if (av.patch !== bv.patch) return av.patch < bv.patch ? -1 : 1;
  return comparePrerelease(av.pre, bv.pre);
}

/** Returns true if `current` satisfies `>= required`. */
export function satisfiesMinimum(current: string, required: string): boolean {
  return compareVersions(current, required) >= 0;
}
