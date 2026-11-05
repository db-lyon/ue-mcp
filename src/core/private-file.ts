import { promises as fs } from "node:fs";
import * as path from "node:path";

/** Owner-only modes for credential files and the directory that holds them. */
export const PRIVATE_DIR_MODE = 0o700;
export const PRIVATE_FILE_MODE = 0o600;

/**
 * Write a credential file readable by its owner only, on every write.
 *
 * `mode` on writeFile applies only when the file is created, and mkdir's only
 * when the directory is, so a file or folder that already existed with wider
 * bits kept them. The directory is chmodded each time, and the file is
 * written to a fresh temp file, chmodded, then renamed over the old one, so
 * a reader never sees it half written or with the old bits.
 */
export async function writePrivateFile(file: string, data: string): Promise<void> {
  const dir = path.dirname(file);
  await fs.mkdir(dir, { recursive: true, mode: PRIVATE_DIR_MODE });
  await fs.chmod(dir, PRIVATE_DIR_MODE);
  const tmp = `${file}.${process.pid}.${Date.now()}.tmp`;
  try {
    await fs.writeFile(tmp, data, { mode: PRIVATE_FILE_MODE, flag: "wx" });
    await fs.chmod(tmp, PRIVATE_FILE_MODE);
    await fs.rename(tmp, file);
  } catch (e) {
    await fs.unlink(tmp).catch(() => {});
    throw e;
  }
}
