/**
 * Updating an installed bridge (spec 5.4, 7.3): find the newest release on the
 * install's channel, then apply it whether or not the editor is running.
 *
 * A binary install is fetched and verified against the release manifest and
 * staged in the update work directory, outside Plugins/. With the editor down
 * it is swapped in at once. With it running on Windows, each existing file is
 * renamed aside (a loaded DLL can be renamed, not overwritten) and the new one
 * moved into place, so the next launch loads it; a pending marker names the
 * renamed files for cleanup. Elsewhere the staged tree waits until the editor
 * exits. Either way the result is checked (verifyBridgeTree) and put back when
 * it would not load. Source is never written over binaries.
 *
 * A source install gets the new source from the running package and is built
 * at once with the editor down. With it running the build is recorded as owed
 * (pending-build.ts), and the next start through ue-mcp builds first: Unreal
 * itself rebuilds only a missing DLL, never one older than its source (#1593).
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { ProjectContext } from "../config/project.js";
import { distTagForVersion, isNewer, resolveUpdateTarget } from "../core/version-check.js";
import { packageVersion } from "../core/package-root.js";
import {
  BRIDGE_PLUGIN_NAME,
  installKindOf,
  readInstallMarker,
  resolvePluginDir,
  unrealPlatform,
  type InstallKind,
} from "./install-marker.js";
import {
  DEFAULT_BINARIES_BASE,
  binaryArchiveName,
  binaryManifestName,
  resolveBinarySource,
  stageBinaryPlugin,
  swapIn,
  sweepUpdateWork,
  type BinaryManifest,
  type BinaryTarget,
} from "./bridge-binaries.js";
import { deploy, projectEngineVersion } from "./deployer.js";
import { detectMcpClients, liftsAgeGate, readUeMcpEntry, writeMcpConfig } from "../integrations/claude-code/mcp-client-config.js";
import { readEnv } from "../core/env.js";
import { ASIDE_FILE, UPDATER_FILES, treeFiles, verifyBridgeTree } from "./bridge-tree.js";
import { buildProject, type BuildResult } from "./editor-build.js";
import { checkPluginFreshness } from "./bridge-freshness.js";
import { clearPendingBuild, markPendingBuildFailed, readPendingBuild, recordPendingBuild } from "./pending-build.js";

/** Builds the project's editor target; buildProject unless a test supplies another. */
export type BridgeBuild = (uprojectPath: string) => Promise<BuildResult>;

/** Minimal fetch, injected so the release lookups can be tested without a network. */
export type ReleaseFetcher = (url: string) => Promise<{ ok: boolean; status: number; json(): Promise<unknown> }>;

/** The ue-mcp package document on the npm registry: UE_MCP_NPM_REGISTRY, else the public one. */
export function npmPackageUrl(): string {
  return `${(readEnv("npmRegistry") || "https://registry.npmjs.org").replace(/\/+$/, "")}/ue-mcp`;
}

export const PENDING_UPDATE_NAME = ".ue-mcp-update-pending.json";

export interface UpdateCheck {
  installed: {
    pluginDir: string | null;
    kind: InstallKind | null;
    /** The bridge's own version, from its marker or descriptor. */
    version: string | null;
    engine: string | null;
    platform: string;
  };
  /** The ue-mcp package this process runs. */
  server: string;
  /** npm dist-tag the install follows: latest for stable, else its prerelease channel. */
  channel: string;
  /** Newest release on that channel, or null when the registry could not be read. */
  latest: string | null;
  /** The version an update would move to, or null when there is nothing newer. */
  target: string | null;
  /** For a binary install: whether the target release ships binaries for this engine and platform. */
  binaries: { available: boolean; file: string } | null;
  /** Set when the check could not be completed. */
  error?: string;
}

function readDescriptorVersion(pluginDir: string): string | null {
  try {
    const v = JSON.parse(fs.readFileSync(path.join(pluginDir, `${BRIDGE_PLUGIN_NAME}.uplugin`), "utf-8")).VersionName;
    return typeof v === "string" ? v : null;
  } catch {
    return null;
  }
}

function binariesBase(): string {
  const given = readEnv("binaries");
  return given && /^https?:\/\//i.test(given) && !given.toLowerCase().endsWith(".zip") ? given : DEFAULT_BINARIES_BASE;
}

/** The newest version on `channel`, and on `latest` when that is newer, from the npm registry's dist-tags. */
async function newestOnChannel(channel: string, fetcher: ReleaseFetcher): Promise<string | null> {
  const res = await fetcher(npmPackageUrl());
  if (!res.ok) throw new Error(`npm registry answered ${res.status}`);
  const tags = ((await res.json()) as { "dist-tags"?: Record<string, string> })["dist-tags"] ?? {};
  const onChannel = tags[channel] ?? null;
  const stable = tags.latest ?? null;
  if (!onChannel) return stable;
  return stable && isNewer(stable, onChannel) ? stable : onChannel;
}

/**
 * Compare the installed bridge against the newest release for its channel. Never throws.
 * `channel` overrides the one the installed version implies; a prerelease channel then
 * also moves a stable install onto it, since the caller chose that channel.
 */
export async function checkForUpdate(
  uprojectPath: string,
  fetcher: ReleaseFetcher = fetch as unknown as ReleaseFetcher,
  channel?: string,
): Promise<UpdateCheck> {
  const projectDir = path.dirname(path.resolve(uprojectPath));
  const located = resolvePluginDir(projectDir);
  const marker = located ? readInstallMarker(located.dir) : null;
  const version = marker?.version ?? (located ? readDescriptorVersion(located.dir) : null);
  let engine = marker?.engine ?? null;
  if (!engine) {
    try {
      const project = new ProjectContext();
      project.setProject(uprojectPath);
      engine = projectEngineVersion(project);
    } catch {
      engine = null;
    }
  }
  const check: UpdateCheck = {
    installed: {
      pluginDir: located?.dir ?? null,
      kind: located ? installKindOf(located.dir) : null,
      version,
      engine,
      platform: unrealPlatform(),
    },
    server: packageVersion(),
    channel: channel || distTagForVersion(version ?? packageVersion()),
    latest: null,
    target: null,
    binaries: null,
  };
  try {
    check.latest = await newestOnChannel(check.channel, fetcher);
    const from = version ?? packageVersion();
    const optedIn = !!channel && channel !== "latest";
    check.target = !check.latest ? null : optedIn ? (isNewer(check.latest, from) ? check.latest : null) : resolveUpdateTarget(from, check.latest);
    if (check.target && check.installed.kind === "binary" && engine) {
      const t: BinaryTarget = { version: check.target, engine, platform: check.installed.platform };
      const manifestUrl = `${binariesBase().replace(/\/+$/, "")}/v${check.target}/${binaryManifestName(check.target)}`;
      const res = await fetcher(manifestUrl);
      const manifest = res.ok ? ((await res.json()) as BinaryManifest) : null;
      const file = binaryArchiveName(t);
      check.binaries = { available: !!manifest?.assets?.some((a) => a.file === file), file };
    }
  } catch (e) {
    check.error = e instanceof Error ? e.message : String(e);
  }
  return check;
}

/* ------------------------------------------------------------------ */
/*  Applying                                                           */
/* ------------------------------------------------------------------ */

export interface PendingUpdate {
  version: string;
  /** "staged": a tree waiting beside the plugin for the editor to exit. "swapped": in place, old files renamed aside. */
  state: "staged" | "swapped";
  staging?: string;
  /** Files renamed aside, to delete once nothing holds them. */
  oldFiles: string[];
  at: string;
}

export function pendingUpdatePath(pluginDir: string): string {
  return path.join(pluginDir, PENDING_UPDATE_NAME);
}

export function readPendingUpdate(pluginDir: string): PendingUpdate | null {
  try {
    return JSON.parse(fs.readFileSync(pendingUpdatePath(pluginDir), "utf-8")) as PendingUpdate;
  } catch {
    return null;
  }
}

function writePendingUpdate(pluginDir: string, p: PendingUpdate): void {
  fs.writeFileSync(pendingUpdatePath(pluginDir), JSON.stringify(p, null, 2));
}

export interface InPlaceOptions {
  /** Suffix for the aside names; defaults to the current time. */
  stamp?: string;
  /** Checks the merged tree, given the files renamed aside; any problem undoes the swap and throws. */
  verify?: (aside: string[]) => string[];
}

/**
 * Move every file of `staging` into `target` while the editor holds some of
 * them. An existing file is renamed to `<name>.old-<stamp>` first, which a
 * loaded DLL allows where an overwrite does not; the new file then takes its
 * name. A file the old release had and the new one does not is renamed aside
 * too, so the result is exactly the new release. Returns the renamed files. On
 * a failure midway, or a tree `verify` rejects, everything is put back.
 */
export function swapInPlace(staging: string, target: string, opts: InPlaceOptions = {}): string[] {
  const stamp = opts.stamp ?? String(Date.now());
  const renamed: Array<{ from: string; to: string }> = [];
  const placed: string[] = [];
  const asideOf = (dest: string): string => {
    const aside = `${dest}.old-${stamp}`;
    fs.renameSync(dest, aside);
    renamed.push({ from: dest, to: aside });
    return aside;
  };
  const incoming = treeFiles(staging);
  const shipped = new Set(incoming.map((f) => f.toLowerCase()));
  const dropped = treeFiles(target).filter(
    (f) => !ASIDE_FILE.test(f) && !UPDATER_FILES.has(f) && !shipped.has(f.toLowerCase()),
  );
  try {
    for (const rel of incoming) {
      const dest = path.join(target, rel);
      fs.mkdirSync(path.dirname(dest), { recursive: true });
      if (fs.existsSync(dest)) asideOf(dest);
      fs.renameSync(path.join(staging, rel), dest);
      placed.push(dest);
    }
    for (const rel of dropped) asideOf(path.join(target, rel));
    const problems = opts.verify?.(renamed.map((r) => r.to)) ?? [];
    if (problems.length) {
      throw new Error(`the new bridge did not install cleanly, so the previous one was put back: ${problems.join("; ")}`);
    }
  } catch (e) {
    const stuck: string[] = [];
    for (const f of placed) {
      try {
        fs.rmSync(f, { force: true });
      } catch {
        stuck.push(f);
      }
    }
    for (const r of renamed.reverse()) {
      try {
        fs.renameSync(r.to, r.from);
      } catch {
        stuck.push(r.from);
      }
    }
    fs.rmSync(staging, { recursive: true, force: true });
    const message = e instanceof Error ? e.message : String(e);
    if (stuck.length) throw new Error(`${message}. Restoring the previous bridge failed for ${stuck.join(", ")}; close the editor and run ue-mcp update again.`);
    throw e instanceof Error ? e : new Error(message);
  }
  fs.rmSync(staging, { recursive: true, force: true });
  return renamed.map((r) => r.to);
}

/** Remove the directories under `root` that deleting `files` left empty. */
function pruneEmptyDirs(root: string, files: string[]): void {
  const top = path.resolve(root);
  for (const f of files) {
    let dir = path.dirname(path.resolve(f));
    while (dir.length > top.length && dir.toLowerCase().startsWith(top.toLowerCase())) {
      try {
        if (fs.readdirSync(dir).length > 0) break;
        fs.rmdirSync(dir);
      } catch {
        break;
      }
      dir = path.dirname(dir);
    }
  }
}

/**
 * Finish whatever an earlier update left: delete renamed-aside files nothing
 * holds any more, swap in a staged tree once the editor is down, and clear the
 * spare trees earlier updates left (sweepUpdateWork). Returns true when the
 * pending marker is gone afterwards.
 */
export function settlePendingUpdate(pluginDir: string, editorRunning: boolean): boolean {
  const pending = readPendingUpdate(pluginDir);
  if (!pending) {
    sweepUpdateWork(pluginDir);
    return true;
  }
  if (pending.state === "staged") {
    if (editorRunning || !pending.staging || !fs.existsSync(pending.staging)) {
      sweepUpdateWork(pluginDir, pending.staging ? [pending.staging] : []);
      return false;
    }
    const marker = readInstallMarker(pending.staging);
    const files = treeFiles(pending.staging);
    swapIn(pending.staging, pluginDir, {
      verify: () => verifyBridgeTree(pluginDir, { version: pending.version, platform: marker?.platform ?? unrealPlatform(), files }),
    });
    sweepUpdateWork(pluginDir);
    return true; // swapIn replaced the whole tree, marker included
  }
  const left = pending.oldFiles.filter((f) => {
    try {
      fs.rmSync(f, { force: true });
      return fs.existsSync(f);
    } catch {
      return true; // still loaded
    }
  });
  pruneEmptyDirs(pluginDir, pending.oldFiles.filter((f) => !left.includes(f)));
  sweepUpdateWork(pluginDir);
  if (left.length === 0) {
    fs.rmSync(pendingUpdatePath(pluginDir), { force: true });
    return true;
  }
  writePendingUpdate(pluginDir, { ...pending, oldFiles: left });
  return false;
}

export type UpdateEvent = (type: "update.staged" | "update.building" | "update.applied" | "update.failed", data: Record<string, unknown>) => void;

export interface ApplyOptions {
  /** The version to install. Defaults to the running package's. */
  version?: string;
  editorRunning: boolean;
  /** Explicit binaries source: a directory, a .zip or a URL base (see resolveBinarySource). */
  binaries?: string | null;
  platform?: NodeJS.Platform;
  onEvent?: UpdateEvent;
  /** The post-swap check; verifyBridgeTree unless a test supplies another. */
  verifyTree?: typeof verifyBridgeTree;
  /** Builds a source install; buildProject unless a test supplies another. */
  build?: BridgeBuild;
}

export interface ApplyResult {
  ok: boolean;
  kind: InstallKind | null;
  version: string;
  /**
   * applied: in place now; staged: waits for the editor to exit; built: source deployed and built;
   * rebuild: source deployed with the editor running, built by the next start through ue-mcp.
   */
  state: "applied" | "staged" | "built" | "rebuild" | "unchanged";
  /** A source install's build, when one ran. */
  build?: Pick<BuildResult, "success" | "message" | "errors" | "resultLine">;
  /** Client configs whose pin moved to the new version. */
  repinned: string[];
  error?: string;
}

/**
 * Move every pinned client config of this project to `version`, keeping each
 * entry's command. An entry already on `version` is rewritten when its launch
 * would still be refused by npm's release-age gate (#1510).
 */
export function repinClientConfigs(uprojectPath: string, version: string): string[] {
  const projectDir = path.dirname(path.resolve(uprojectPath));
  const target = path.resolve(uprojectPath).replace(/\\/g, "/").toLowerCase();
  const moved: string[] = [];
  for (const client of detectMcpClients(projectDir)) {
    const entry = readUeMcpEntry(client);
    if (!entry.pinned || (entry.pinned === version && liftsAgeGate(entry.args))) continue;
    if (entry.project && path.resolve(entry.project).replace(/\\/g, "/").toLowerCase() !== target) continue;
    writeMcpConfig(client, uprojectPath, { pin: version, command: entry.command && entry.command !== "npx" ? entry.command : null });
    moved.push(client.configPath);
  }
  return moved;
}

/**
 * Why an older ue-mcp leaves a newer bridge alone: `npx ue-mcp` resolves to the
 * stable line, which must not move a beta bridge or its pinned clients back.
 */
export function newerBridgeMessage(bridge: string, server: string): string {
  return `the bridge is ${bridge}, newer than this ue-mcp ${server}, so it is left as is. ` +
    `Run \`npx ue-mcp@${distTagForVersion(bridge)} update\` to update it on its own channel.`;
}

/** The engine a project opens with, major.minor, when that can be told; else null. */
function projectEngine(uprojectPath: string): string | null {
  try {
    const project = new ProjectContext();
    project.setProject(uprojectPath);
    const engine = projectEngineVersion(project);
    return engine && /^\d+\.\d+$/.test(engine) ? engine : null;
  } catch {
    return null;
  }
}

/** One apply per project at a time: two at once would swap over each other's files. */
const applying = new Map<string, Promise<ApplyResult>>();

/**
 * Install `options.version` of the bridge into this project. Never throws;
 * failures come back in the result. A binary install is checked after the
 * swap (verifyBridgeTree) and put back as it was when the check fails, so a
 * success means the editor will load the new bridge without a rebuild.
 * Concurrent calls for one project run one after another.
 */
export function applyUpdate(uprojectPath: string, options: ApplyOptions): Promise<ApplyResult> {
  const key = path.dirname(path.resolve(uprojectPath)).toLowerCase();
  const before = applying.get(key) ?? Promise.resolve(null);
  const run = before.then(() => applyUpdateNow(uprojectPath, options), () => applyUpdateNow(uprojectPath, options));
  applying.set(key, run);
  void run.finally(() => {
    if (applying.get(key) === run) applying.delete(key);
  });
  return run;
}

async function applyUpdateNow(uprojectPath: string, options: ApplyOptions): Promise<ApplyResult> {
  const version = options.version ?? packageVersion();
  const projectDir = path.dirname(path.resolve(uprojectPath));
  const located = resolvePluginDir(projectDir);
  const kind = located ? installKindOf(located.dir) : null;
  const emit = options.onEvent ?? (() => {});
  const verify = options.verifyTree ?? verifyBridgeTree;
  const result: ApplyResult = { ok: false, kind, version, state: "unchanged", repinned: [] };
  try {
    if (!located || !kind) throw new Error("the bridge is not installed in this project; run ue-mcp init");
    const pluginDir = located.dir;
    const installedVersion = readInstallMarker(pluginDir)?.version ?? readDescriptorVersion(pluginDir);
    if (!options.version && kind === "binary" && installedVersion && isNewer(installedVersion, version)) {
      throw new Error(newerBridgeMessage(installedVersion, version));
    }
    settlePendingUpdate(pluginDir, options.editorRunning);

    if (kind === "source") {
      if (version !== packageVersion()) {
        throw new Error(`this process runs ue-mcp ${packageVersion()}; a source install updates to the version of the package that deploys it. Run ue-mcp update.`);
      }
      const project = new ProjectContext();
      project.setProject(uprojectPath);
      const deployed = deploy(project);
      if (deployed.error) throw new Error(deployed.error);
      // Unchanged source still builds when a build is owed or the binary is older than the source,
      // which is how an update that deployed without building is finished by running it again.
      const owed = deployed.cppPluginDeployed || readPendingBuild(uprojectPath) !== null || checkPluginFreshness(uprojectPath).stale;
      if (owed) {
        // Recorded first, so a build that fails or is interrupted is still owed at the next start.
        recordPendingBuild(uprojectPath, version, options.editorRunning ? "editor running" : "source updated");
        if (options.editorRunning) {
          result.state = "rebuild";
        } else {
          result.state = "rebuild";
          emit("update.building", { version });
          const built = await (options.build ?? buildProject)(uprojectPath);
          result.build = {
            success: built.success,
            message: built.message,
            ...(built.errors ? { errors: built.errors } : {}),
            ...(built.resultLine ? { resultLine: built.resultLine } : {}),
          };
          if (!built.success) {
            markPendingBuildFailed(uprojectPath, built.message);
            throw new Error(`the bridge ${version} source is deployed, but building it failed: ${built.message}`);
          }
          clearPendingBuild(uprojectPath);
          result.state = "built";
        }
      } else {
        result.state = "unchanged";
      }
    } else {
      const marker = readInstallMarker(pluginDir);
      const current = marker?.version ?? readDescriptorVersion(pluginDir);
      const pending = readPendingUpdate(pluginDir);
      const settled = !pending || (pending.state === "swapped" && pending.version === version);
      // Already at this version still reinstalls when the tree would not load, which is how a broken install is repaired.
      const loads = () => verify(pluginDir, {
        version, platform: marker?.platform ?? unrealPlatform(), asideFiles: pending?.oldFiles ?? [], checkMarker: !!marker,
      }).length === 0;
      if (current === version && settled && loads()) {
        result.state = "unchanged";
      } else {
        // The project's engine first: a project moved to a new engine needs binaries for that one, not the marker's.
        const engine = projectEngine(uprojectPath) ?? marker?.engine ?? null;
        if (!engine) throw new Error("cannot tell which engine the binaries are for");
        const t: BinaryTarget = { version, engine, platform: marker?.platform ?? unrealPlatform() };
        const staged = await stageBinaryPlugin(pluginDir, t, resolveBinarySource(t, options.binaries));
        const expect = (asideFiles: string[]) => verify(pluginDir, { version, platform: t.platform, files: staged.files, asideFiles });
        if (!options.editorRunning) {
          swapIn(staged.staging, pluginDir, { verify: () => expect([]) });
          result.state = "applied";
        } else if ((options.platform ?? process.platform) === "win32") {
          // Aside files an earlier swap left, still mapped by the running editor, stay tracked for cleanup.
          const earlier = (pending?.state === "swapped" ? pending.oldFiles : []).filter((f) => fs.existsSync(f));
          // Files the new release dropped, the retired Python bridge's among them (#1273), go aside with the rest.
          const oldFiles = swapInPlace(staged.staging, pluginDir, { verify: (aside) => expect([...earlier, ...aside]) });
          writePendingUpdate(pluginDir, { version, state: "swapped", oldFiles: [...earlier, ...oldFiles], at: new Date().toISOString() });
          result.state = "applied";
        } else {
          if (pending?.state === "staged" && pending.staging) fs.rmSync(pending.staging, { recursive: true, force: true });
          writePendingUpdate(pluginDir, { version, state: "staged", staging: staged.staging, oldFiles: [], at: new Date().toISOString() });
          result.state = "staged";
          emit("update.staged", { version, waitingFor: "editor exit" });
        }
      }
    }
    if (result.state === "applied" || result.state === "built" || result.state === "rebuild") {
      result.repinned = repinClientConfigs(uprojectPath, version);
      emit("update.applied", { version, kind, state: result.state, repinned: result.repinned });
    }
    result.ok = true;
  } catch (e) {
    result.error = e instanceof Error ? e.message : String(e);
    emit("update.failed", { version, error: result.error });
  }
  return result;
}

export interface LaunchPreparation {
  ok: boolean;
  /** Set when the bridge would not have loaded and was reinstalled first. */
  repaired?: string[];
  /** Set when a source bridge owed a build and was built first. */
  built?: { version: string; message: string };
  /** Set when the owed build failed before, so the launch goes ahead on the previous build. */
  buildFailed?: { version: string; message: string };
  /** Why the launch should not go ahead. */
  message?: string;
}

/**
 * Make the installed bridge one the editor will load, just before launching
 * it: finish a pending update (the editor is down), clear spare copies under
 * Plugins/, and for a binary install check the tree. A tree that would not
 * load (a module without its DLL, a manifest out of step, Source over
 * binaries) is reinstalled at its own version; when that fails too the launch
 * is refused with the reasons, instead of the editor offering a rebuild that
 * a binary install cannot do. A source install that owes a build since an
 * update (pending-build.ts) is built first, and a failed build refuses the
 * launch with the compiler's errors rather than opening the old DLL. Once a
 * build of that source has failed, a start opens the previous build and says
 * so instead of failing the same build again. Never throws.
 */
export async function prepareBridgeForLaunch(
  uprojectPath: string,
  options: Pick<ApplyOptions, "binaries" | "verifyTree" | "build"> = {},
): Promise<LaunchPreparation> {
  const projectDir = path.dirname(path.resolve(uprojectPath));
  const located = resolvePluginDir(projectDir);
  if (!located) return { ok: true };
  const pluginDir = located.dir;
  try {
    settlePendingUpdate(pluginDir, false);
  } catch (e) {
    return { ok: false, message: `finishing the pending bridge update failed: ${e instanceof Error ? e.message : String(e)}` };
  }
  if (installKindOf(pluginDir) !== "binary") {
    const owed = readPendingBuild(uprojectPath);
    if (!owed) return { ok: true };
    if (owed.failed) return { ok: true, buildFailed: { version: owed.version, message: owed.failed.message } };
    let built: BuildResult;
    try {
      built = await (options.build ?? buildProject)(uprojectPath);
    } catch (e) {
      built = { success: false, exitCode: null, message: e instanceof Error ? e.message : String(e) };
    }
    if (!built.success) {
      markPendingBuildFailed(uprojectPath, built.message);
      return {
        ok: false,
        message: `the bridge ${owed.version} source was deployed and not built yet, and building it failed: ${built.message} ` +
          "Fix the error and build again, or start the editor again to open it on the previous build.",
      };
    }
    clearPendingBuild(uprojectPath);
    return { ok: true, built: { version: owed.version, message: built.message } };
  }
  const verify = options.verifyTree ?? verifyBridgeTree;
  const marker = readInstallMarker(pluginDir);
  const version = marker?.version ?? readDescriptorVersion(pluginDir);
  const problems = verify(pluginDir, {
    version: version ?? "",
    platform: marker?.platform ?? unrealPlatform(),
    asideFiles: readPendingUpdate(pluginDir)?.oldFiles ?? [],
    checkMarker: !!marker,
  });
  if (problems.length === 0) return { ok: true };
  if (!version) return { ok: false, message: `the bridge in ${pluginDir} would not load (${problems.join("; ")}) and names no version to reinstall` };
  const repaired = await applyUpdate(uprojectPath, { version, editorRunning: false, binaries: options.binaries, verifyTree: options.verifyTree });
  if (!repaired.ok) {
    return {
      ok: false,
      message: `the bridge in ${pluginDir} would not load (${problems.join("; ")}), and reinstalling ${version} failed: ${repaired.error}. ` +
        `Run \`npx ue-mcp@${version} update --apply-only\` from the project directory.`,
    };
  }
  return { ok: true, repaired: problems };
}
