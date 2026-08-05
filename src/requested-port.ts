/**
 * The port channel that runs client to editor.
 *
 * Every other file under `Saved/UE_MCP_Bridge/` is written by the plugin and
 * read here. This one is the reverse, and it exists because the two sides
 * resolve the port pin from different amounts of information.
 *
 * The client merges four config layers and then the environment on top of them.
 * An editor launched from Explorer sees none of that: no `UE_MCP_PORT` in its
 * environment, and no way to reproduce the merge. So the users who had gone out
 * of their way to pin a port were exactly the users whose client and editor
 * ended up on different ones.
 *
 * The file carries an integer, never a policy. The client does the resolving
 * and publishes the answer; the bridge reads it and binds it
 * (FMCPBridgeStateFiles::ReadRequestedPort).
 *
 * It exists only while a pin exists. Removing the pin removes the file, so an
 * unpinned project has nothing here and the bridge resolves its port exactly as
 * it did before this channel was added.
 */

import * as fs from "node:fs";
import * as path from "node:path";
import { debug } from "./log.js";
import { normalizeProjectRoot } from "./port.js";

/** Where the client publishes the pin, for one project root. */
export function requestedPortPath(projectDir: string): string {
  return path.join(projectDir, "Saved", "UE_MCP_Bridge", "requested.json");
}

export interface RequestedPortRecord {
  port: number;
  /** Normalized, so the bridge can compare it without re-normalizing. */
  projectRoot: string;
  /** Which layer the pin came from, for a human reading the file. */
  source: string;
  writtenBy: number;
  writtenAt: string;
}

/**
 * Publish the pin for `projectDir`.
 *
 * Written temp-and-rename for the same reason the bridge writes its records
 * that way: the reader is a separate process that can arrive at any moment, and
 * a rename is the one step it cannot catch halfway through.
 *
 * Failure is not raised. A project directory that is read-only, or on a share
 * that refuses the rename, still has every port path that worked before this
 * one; losing the optimisation is not worth losing the connection.
 */
export function publishRequestedPort(projectDir: string, port: number, source: string): void {
  if (!Number.isInteger(port) || port < 1 || port > 65535) return;

  const file = requestedPortPath(projectDir);
  const record: RequestedPortRecord = {
    port,
    projectRoot: normalizeProjectRoot(projectDir),
    source,
    writtenBy: process.pid,
    writtenAt: new Date().toISOString(),
  };

  const temp = `${file}.${process.pid}.tmp`;
  try {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(temp, `${JSON.stringify(record, null, 2)}\n`, "utf8");
    fs.renameSync(temp, file);
    debug("bridge", `published requested port ${port} (${source}) to ${file}`);
  } catch (err) {
    try {
      fs.unlinkSync(temp);
    } catch {
      // The temp file never existed, which is the common case here.
    }
    debug("bridge", `could not publish requested port to ${file}: ${(err as Error).message}`);
  }
}

/**
 * Remove the pin for `projectDir`.
 *
 * Called whenever the resolved port is not a pin, so a user who deletes
 * `bridge.port` from their config does not leave the editor bound to it
 * forever. A missing file is the expected case and is not an error.
 */
export function clearRequestedPort(projectDir: string): void {
  const file = requestedPortPath(projectDir);
  try {
    fs.rmSync(file, { force: true });
  } catch (err) {
    debug("bridge", `could not remove ${file}: ${(err as Error).message}`);
  }
}

/**
 * Publish or clear in one step, from the port the client resolved and where it
 * came from.
 *
 * Only the three sources that are pins are published. `derived` is the hash of
 * the project root, which the bridge computes identically on its own, and
 * `lockfile` is the port an editor already bound, which is an observation
 * rather than a request. Publishing either would turn a value the bridge
 * derives into one it is told, for no gain and one more thing to go stale.
 */
export function syncRequestedPort(
  projectDir: string,
  port: number,
  source: "lockfile" | "config" | "derived" | "explicit" | "env" | "default",
): void {
  if (source === "explicit" || source === "env" || source === "config") {
    publishRequestedPort(projectDir, port, source);
    return;
  }
  clearRequestedPort(projectDir);
}
