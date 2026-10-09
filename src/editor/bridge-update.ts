/**
 * Updating an installed bridge (spec 5.4, 7.3): find the newest release on the
 * install's channel, then apply it whether or not the editor is running.
 *
 * A binary install is fetched and verified against the release manifest and
 * staged beside the plugin. With the editor down it is swapped in at once.
 * With it running on Windows, each existing file is renamed aside (a loaded DLL
 * can be renamed, not overwritten) and the new one moved into place, so the
 * next launch loads it; a pending marker names the renamed files for cleanup.
 * Elsewhere the staged tree waits until the editor exits. Source is never
 * written over binaries.
 *
 * A source install gets the new source from the running package; it builds on
 * the next restart.
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
  type BinaryManifest,
  type BinaryTarget,
} from "./bridge-binaries.js";
import { deploy, projectEngineVersion } from "./deployer.js";
import { detectMcpClients, readUeMcpEntry, writeMcpConfig } from "../integrations/claude-code/mcp-client-config.js";
import { readEnv } from "../core/env.js";

/** Minimal fetch, injected so the release lookups can be tested without a network. */
export type ReleaseFetcher = (url: string) => Promise<{ ok: boolean; status: number; json(): Promise<unknown> }>;

export const NPM_PACKAGE_URL = "https://registry.npmjs.org/ue-mcp";
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
  const res = await fetcher(NPM_PACKAGE_URL);
  if (!res.ok) throw new Error(`npm registry answered ${res.status}`);
  const tags = ((await res.json()) as { "dist-tags"?: Record<string, string> })["dist-tags"] ?? {};
  const onChannel = tags[channel] ?? null;
  const stable = tags.latest ?? null;
  if (!onChannel) return stable;
  return stable && isNewer(stable, onChannel) ? stable : onChannel;
}

/** Compare the installed bridge against the newest release for its channel. Never throws. */
export async function checkForUpdate(
  uprojectPath: string,
  fetcher: ReleaseFetcher = fetch as unknown as ReleaseFetcher,
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
    channel: distTagForVersion(version ?? packageVersion()),
    latest: null,
    target: null,
    binaries: null,
  };
  try {
    check.latest = await newestOnChannel(check.channel, fetcher);
    const from = version ?? packageVersion();
    check.target = check.latest ? resolveUpdateTarget(from, check.latest) : null;
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

function listFiles(dir: string, base = dir): string[] {
  const out: string[] = [];
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, e.name);
    if (e.isDirectory()) out.push(...listFiles(full, base));
    else out.push(path.relative(base, full));
  }
  return out;
}

/**
 * Move every file of `staging` into `target` while the editor holds some of
 * them. An existing file is renamed to `<name>.old-<stamp>` first, which a
 * loaded DLL allows where an overwrite does not; the new file then takes its
 * name. Returns the renamed files. On a failure midway, the files already moved
 * are put back.
 */
export function swapInPlace(staging: string, target: string, stamp: string = String(Date.now())): string[] {
  const renamed: Array<{ from: string; to: string }> = [];
  const placed: string[] = [];
  try {
    for (const rel of listFiles(staging)) {
      const dest = path.join(target, rel);
      fs.mkdirSync(path.dirname(dest), { recursive: true });
      if (fs.existsSync(dest)) {
        const aside = `${dest}.old-${stamp}`;
        fs.renameSync(dest, aside);
        renamed.push({ from: dest, to: aside });
      }
      fs.renameSync(path.join(staging, rel), dest);
      placed.push(dest);
    }
  } catch (e) {
    for (const f of placed) fs.rmSync(f, { force: true });
    for (const r of renamed.reverse()) {
      try {
        fs.renameSync(r.to, r.from);
      } catch {
        // Leave it renamed rather than lose it; the pending marker is not written, so nothing claims success.
      }
    }
    throw e;
  }
  fs.rmSync(staging, { recursive: true, force: true });
  return renamed.map((r) => r.to);
}

/**
 * Finish whatever an earlier update left: delete renamed-aside files nothing
 * holds any more, and swap in a staged tree once the editor is down. Returns
 * true when the pending marker is gone afterwards.
 */
export function settlePendingUpdate(pluginDir: string, editorRunning: boolean): boolean {
  const pending = readPendingUpdate(pluginDir);
  if (!pending) return true;
  if (pending.state === "staged") {
    if (editorRunning || !pending.staging || !fs.existsSync(pending.staging)) return false;
    swapIn(pending.staging, pluginDir);
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
  if (left.length === 0) {
    fs.rmSync(pendingUpdatePath(pluginDir), { force: true });
    return true;
  }
  writePendingUpdate(pluginDir, { ...pending, oldFiles: left });
  return false;
}

export type UpdateEvent = (type: "update.staged" | "update.applied" | "update.failed", data: Record<string, unknown>) => void;

export interface ApplyOptions {
  /** The version to install. Defaults to the running package's. */
  version?: string;
  editorRunning: boolean;
  /** Explicit binaries source: a directory, a .zip or a URL base (see resolveBinarySource). */
  binaries?: string | null;
  platform?: NodeJS.Platform;
  onEvent?: UpdateEvent;
}

export interface ApplyResult {
  ok: boolean;
  kind: InstallKind | null;
  version: string;
  /** applied: in place now; staged: waits for the editor to exit; rebuild: source deployed, builds on restart. */
  state: "applied" | "staged" | "rebuild" | "unchanged";
  /** Client configs whose pin moved to the new version. */
  repinned: string[];
  error?: string;
}

/** Move every pinned client config of this project to `version`, keeping each entry's command. */
export function repinClientConfigs(uprojectPath: string, version: string): string[] {
  const projectDir = path.dirname(path.resolve(uprojectPath));
  const target = path.resolve(uprojectPath).replace(/\\/g, "/").toLowerCase();
  const moved: string[] = [];
  for (const client of detectMcpClients(projectDir)) {
    const entry = readUeMcpEntry(client);
    if (!entry.pinned || entry.pinned === version) continue;
    if (entry.project && path.resolve(entry.project).replace(/\\/g, "/").toLowerCase() !== target) continue;
    writeMcpConfig(client, uprojectPath, { pin: version, command: entry.command && entry.command !== "npx" ? entry.command : null });
    moved.push(client.configPath);
  }
  return moved;
}

/** Install `options.version` of the bridge into this project. Never throws; failures come back in the result. */
export async function applyUpdate(uprojectPath: string, options: ApplyOptions): Promise<ApplyResult> {
  const version = options.version ?? packageVersion();
  const projectDir = path.dirname(path.resolve(uprojectPath));
  const located = resolvePluginDir(projectDir);
  const kind = located ? installKindOf(located.dir) : null;
  const emit = options.onEvent ?? (() => {});
  const result: ApplyResult = { ok: false, kind, version, state: "unchanged", repinned: [] };
  try {
    if (!located || !kind) throw new Error("the bridge is not installed in this project; run ue-mcp init");
    const pluginDir = located.dir;
    settlePendingUpdate(pluginDir, options.editorRunning);

    if (kind === "source") {
      if (version !== packageVersion()) {
        throw new Error(`this process runs ue-mcp ${packageVersion()}; a source install updates to the version of the package that deploys it. Run ue-mcp update.`);
      }
      const project = new ProjectContext();
      project.setProject(uprojectPath);
      const deployed = deploy(project);
      if (deployed.error) throw new Error(deployed.error);
      result.state = deployed.cppPluginDeployed ? "rebuild" : "unchanged";
    } else {
      const marker = readInstallMarker(pluginDir);
      const current = marker?.version ?? readDescriptorVersion(pluginDir);
      if (current === version && !readPendingUpdate(pluginDir)) {
        result.state = "unchanged";
      } else {
        let engine = marker?.engine ?? null;
        if (!engine) {
          const project = new ProjectContext();
          project.setProject(uprojectPath);
          engine = projectEngineVersion(project);
        }
        if (!engine) throw new Error("cannot tell which engine the binaries are for");
        const t: BinaryTarget = { version, engine, platform: marker?.platform ?? unrealPlatform() };
        const staged = await stageBinaryPlugin(pluginDir, t, resolveBinarySource(t, options.binaries));
        if (!options.editorRunning) {
          swapIn(staged.staging, pluginDir);
          result.state = "applied";
        } else if ((options.platform ?? process.platform) === "win32") {
          const oldFiles = swapInPlace(staged.staging, pluginDir);
          writePendingUpdate(pluginDir, { version, state: "swapped", oldFiles, at: new Date().toISOString() });
          result.state = "applied";
        } else {
          const waiting = `${pluginDir}.pending-update`;
          fs.rmSync(waiting, { recursive: true, force: true });
          fs.renameSync(staged.staging, waiting);
          writePendingUpdate(pluginDir, { version, state: "staged", staging: waiting, oldFiles: [], at: new Date().toISOString() });
          result.state = "staged";
          emit("update.staged", { version, waitingFor: "editor exit" });
        }
      }
    }
    if (result.state === "applied" || result.state === "rebuild") {
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
