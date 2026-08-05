/**
 * Release channel maths for the publish pipeline.
 *
 * The publish job has to answer three questions about the version in
 * package.json, and getting any of them wrong ships a prerelease at everyone
 * who runs `npx ue-mcp`:
 *
 *   1. Which npm dist-tag does this version belong under? A plain X.Y.Z owns
 *      `latest`; a prerelease owns the tag named by its first prerelease
 *      identifier (1.2.0-beta and 1.2.0-beta.2 both go to `beta`,
 *      1.3.0-rc.1 goes to `rc`).
 *   2. Is this a prerelease, so the GitHub release is marked as one instead of
 *      taking the repo's "Latest" badge?
 *   3. Is this exact version already on the registry? Asking `npm view ue-mcp
 *      version` only reports the `latest` dist-tag, which stops moving the
 *      moment a prerelease is published under its own tag, so that question
 *      has to be asked against the full version list.
 *
 * Lives in scripts/ rather than src/ because the publish job calls it before
 * `npx tsc` has produced dist/. Kept as pure functions plus a thin CLI so the
 * behaviour is unit tested rather than buried in YAML.
 *
 * No shebang: vitest re-imports this file and its loader rejects the shebang
 * line as "Invalid or unexpected token". Invoke as
 * `node scripts/release-version.mjs <version>`.
 *
 * CLI output is GITHUB_OUTPUT key=value form:
 *
 *   dist_tag=beta
 *   prerelease=true
 */
import { pathToFileURL } from "node:url";

/** Semver with optional prerelease and build metadata. */
export const SEMVER_RE =
  /^(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z][0-9A-Za-z.-]*))?(?:\+([0-9A-Za-z][0-9A-Za-z.-]*))?$/;

/**
 * npm rejects a dist-tag that parses as a semver range, so a tag has to start
 * with a letter. A numeric-only prerelease identifier (1.2.0-1) therefore
 * cannot name its own tag.
 */
export const SAFE_TAG_RE = /^[A-Za-z][0-9A-Za-z._-]*$/;

/** Where prereleases go when their identifier cannot be used as a tag. */
export const FALLBACK_TAG = "next";

/** The tag a plain X.Y.Z owns, and the one no prerelease may ever take. */
export const STABLE_TAG = "latest";

export class VersionError extends Error {}

/**
 * Splits a version string. Throws VersionError on anything that is not plain
 * semver, so a typo in package.json fails the job instead of publishing under
 * a surprise tag.
 */
export function parseVersion(version) {
  if (typeof version !== "string") {
    throw new VersionError(`Version must be a string (got ${typeof version}).`);
  }
  const m = SEMVER_RE.exec(version.trim());
  if (!m) {
    throw new VersionError(
      `'${version}' is not a valid semver version. Expected X.Y.Z with an optional ` +
        `-prerelease suffix, for example 1.1.44, 1.2.0-beta, or 1.3.0-rc.1.`
    );
  }
  return {
    major: Number(m[1]),
    minor: Number(m[2]),
    patch: Number(m[3]),
    prerelease: m[4] ?? null,
    build: m[5] ?? null,
  };
}

/** True when the version carries a prerelease identifier. */
export function isPrerelease(version) {
  return parseVersion(version).prerelease !== null;
}

/**
 * The npm dist-tag this version should publish under.
 *
 * A plain X.Y.Z keeps `latest`. A prerelease takes the first dot-separated
 * identifier of its prerelease suffix, falling back to `next` when that
 * identifier cannot legally be a tag, or when it would collide with the
 * stable tag (1.2.0-latest must not hijack `latest`).
 */
export function distTag(version) {
  const { prerelease } = parseVersion(version);
  if (prerelease === null) return STABLE_TAG;
  const first = prerelease.split(".")[0];
  if (!SAFE_TAG_RE.test(first)) return FALLBACK_TAG;
  if (first.toLowerCase() === STABLE_TAG) return FALLBACK_TAG;
  return first;
}

/**
 * Membership test against the output of `npm view <pkg> versions --json`.
 *
 * npm prints a bare JSON string when exactly one version exists and an array
 * otherwise, so both shapes are accepted. Throws VersionError when the payload
 * is empty or unparseable: an unreachable registry must fail the job loudly
 * rather than be read as "not published yet" and trigger a blind publish.
 */
export function isPublished(versionsJson, version) {
  const raw = typeof versionsJson === "string" ? versionsJson.trim() : "";
  if (raw === "") {
    throw new VersionError(
      "Empty response from `npm view ue-mcp versions --json`. The registry was " +
        "unreachable or the package name is wrong; refusing to guess whether this " +
        "version is published."
    );
  }
  let parsed;
  try {
    parsed = JSON.parse(raw);
  } catch {
    throw new VersionError(`Could not parse the npm versions payload as JSON: ${raw.slice(0, 200)}`);
  }
  const list = Array.isArray(parsed) ? parsed : [parsed];
  const target = version.trim();
  return list.some((v) => typeof v === "string" && v.trim() === target);
}

/** Every CLI output for one version, in GITHUB_OUTPUT key=value form. */
export function outputsFor(version) {
  return [`dist_tag=${distTag(version)}`, `prerelease=${isPrerelease(version)}`];
}

function main() {
  const args = process.argv.slice(2);
  let version = null;
  let versionsJson = null;
  let mode = "outputs";
  for (let i = 0; i < args.length; i++) {
    if (args[i] === "--version") version = args[++i];
    else if (args[i] === "--published-in") {
      mode = "published";
      versionsJson = args[++i];
    } else if (!args[i].startsWith("-")) version = args[i];
  }
  if (!version) {
    console.error("::error::Usage: node scripts/release-version.mjs <version> [--published-in <json>]");
    process.exit(2);
  }
  try {
    if (mode === "published") {
      console.log(`published=${isPublished(versionsJson, version)}`);
    } else {
      for (const line of outputsFor(version)) console.log(line);
    }
  } catch (e) {
    if (e instanceof VersionError) {
      console.error(`::error::${e.message}`);
      process.exit(1);
    }
    throw e;
  }
}

const invokedAsScript =
  process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href;
if (invokedAsScript) main();
