/**
 * An editor plugin an extension declares through its manifest's
 * `pluginChannel`. The channel lists one build per engine with its SHA-256:
 *
 *   { "version": "0.1.7", "files": { "5.7": { "url": "...zip", "sha256": "..." } } }
 *
 * The build for the project's engine is verified, unpacked and swapped into
 * the project's Plugins/ folder the way the bridge binaries are.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { downloadVerified, extractZip } from "../core/archive.js";
import { isNewer } from "../core/version-check.js";
import { ProjectContext } from "../config/project.js";
import { projectEngineVersion } from "./deployer.js";
import { findProjectPlugin, unrealPlatform } from "./install-marker.js";
import { stagingDir, swapIn } from "./bridge-binaries.js";

interface PluginChannel {
  version: string;
  files: Record<string, { url: string; sha256: string }>;
}

export interface InstalledEditorPlugin {
  /** The plugin's folder and descriptor name, e.g. UE_MCP_Studio. */
  name: string;
  /** The descriptor's FriendlyName, else its name. */
  title: string;
  version: string;
  dir: string;
  /** False when that version was already in place. */
  changed: boolean;
}

interface Descriptor {
  name: string;
  dir: string;
  version: string | null;
  title: string;
}

/** The plugin in `dir`: a .uplugin at its root or in its only folder. */
function descriptorIn(dir: string): Descriptor | null {
  const read = (d: string): Descriptor | null => {
    const file = fs.readdirSync(d).find((f) => f.toLowerCase().endsWith(".uplugin"));
    if (!file) return null;
    const json = JSON.parse(fs.readFileSync(path.join(d, file), "utf-8")) as { VersionName?: unknown; FriendlyName?: unknown };
    const name = file.slice(0, -".uplugin".length);
    return {
      name,
      dir: d,
      version: typeof json.VersionName === "string" ? json.VersionName : null,
      title: typeof json.FriendlyName === "string" && json.FriendlyName ? json.FriendlyName : name,
    };
  };
  const top = read(dir);
  if (top) return top;
  const folders = fs.readdirSync(dir, { withFileTypes: true }).filter((e) => e.isDirectory());
  return folders.length === 1 ? read(path.join(dir, folders[0].name)) : null;
}

async function readChannel(url: string): Promise<PluginChannel> {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`editor plugin channel download failed (${res.status} ${res.statusText}): ${url}`);
  const c = (await res.json()) as Partial<PluginChannel>;
  if (typeof c.version !== "string" || !c.files || typeof c.files !== "object") {
    throw new Error(`${url} is not an editor plugin channel (needs version and files)`);
  }
  return c as PluginChannel;
}

/**
 * Install the build `channelUrl` offers for the project's engine into the
 * project's Plugins/ folder. `min` refuses a channel older than the
 * extension needs.
 */
export async function installEditorPlugin(channelUrl: string, uproject: string, min?: string): Promise<InstalledEditorPlugin> {
  const project = new ProjectContext();
  project.setProject(uproject);
  const engine = projectEngineVersion(project);
  if (!engine || !/^\d+\.\d+$/.test(engine)) {
    throw new Error(`cannot pick an editor plugin build: the engine version of ${project.projectName} is unknown (${engine ?? "unset"})`);
  }

  const channel = await readChannel(channelUrl);
  if (min && isNewer(min, channel.version)) {
    throw new Error(`${channelUrl} offers ${channel.version}, older than the ${min} this needs`);
  }
  const build = channel.files[engine];
  if (!build?.url || !build.sha256) {
    const have = Object.keys(channel.files).sort().join(", ") || "none";
    throw new Error(`no editor plugin build for UE ${engine} (builds exist for ${have})`);
  }

  const work = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-plugin-"));
  try {
    const zip = path.join(work, "plugin.zip");
    await downloadVerified(new URL(build.url, channelUrl).href, zip, build.sha256);
    extractZip(zip, path.join(work, "x"));
    const found = descriptorIn(path.join(work, "x"));
    if (!found || !/^[A-Za-z0-9_]+$/.test(found.name)) throw new Error(`${build.url} holds no plugin descriptor`);
    const platform = unrealPlatform();
    if (!fs.existsSync(path.join(found.dir, "Binaries", platform))) {
      throw new Error(`${found.title} ${channel.version} has no build for ${platform}`);
    }

    const projectDir = project.projectDir!;
    const target = findProjectPlugin(projectDir, found.name) ?? path.join(projectDir, "Plugins", found.name);
    const current = fs.existsSync(target) ? descriptorIn(target) : null;
    const changed = current?.version !== channel.version;
    if (changed) {
      const staging = stagingDir(target);
      fs.cpSync(found.dir, staging, { recursive: true });
      swapIn(staging, target);
    }
    return { name: found.name, title: found.title, version: channel.version, dir: target, changed };
  } finally {
    fs.rmSync(work, { recursive: true, force: true });
  }
}
