/**
 * Holds DLLs the way a running editor holds its loaded modules on Windows.
 *
 * A loaded module is a mapped image section, so Windows lets the file be
 * renamed but refuses to delete it or open it for writing. A Node file handle
 * cannot reproduce that (libuv opens with every share mode), so another
 * process maps each file with LoadLibraryEx and DONT_RESOLVE_DLL_REFERENCES:
 * the same image mapping the loader makes, without resolving imports, so a
 * real bridge DLL maps even though the engine DLLs it imports are absent.
 */
import { spawn, type ChildProcess } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";

export const SYSTEM_DLL = path.join(process.env.SystemRoot ?? process.env.SYSTEMROOT ?? "C:\\Windows", "System32", "version.dll");

/**
 * A valid PE image tagged with `tag` in its overlay, so a stub module can be
 * mapped like a real one and still be told apart by content. Off Windows,
 * where nothing maps it, the tag alone.
 */
export function stubModule(tag: string): Buffer {
  const label = Buffer.from(`\nue-mcp-stub:${tag}\n`);
  return process.platform === "win32" ? Buffer.concat([fs.readFileSync(SYSTEM_DLL), label]) : label;
}

/** The tag `stubModule` wrote, or null for any other content. */
export function stubTag(content: Buffer): string | null {
  const m = /\nue-mcp-stub:([^\n]*)\n$/.exec(content.subarray(Math.max(0, content.length - 512)).toString("latin1"));
  return m ? m[1] : null;
}

export class DllHolder {
  private child: ChildProcess | null = null;

  /** Map every file in `dlls` in one other process; resolves once all are mapped. */
  async hold(dlls: string[]): Promise<void> {
    const list = dlls.map((d) => `'${d.replace(/'/g, "''")}'`).join(",");
    const ps = [
      `Add-Type -Name K -Namespace W -MemberDefinition '[DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr LoadLibraryExW(string p, IntPtr f, uint flags);'`,
      `foreach ($d in @(${list})) { if ([W.K]::LoadLibraryExW($d, [IntPtr]::Zero, 1) -eq [IntPtr]::Zero) { Write-Output "failed $d"; exit 3 } }`,
      `Write-Output held`,
      `Start-Sleep -Seconds 300`,
    ].join("; ");
    this.child = spawn("powershell", ["-NoProfile", "-NonInteractive", "-Command", ps], { stdio: ["ignore", "pipe", "inherit"], windowsHide: true });
    await new Promise<void>((resolve, reject) => {
      let out = "";
      this.child!.stdout!.on("data", (d: Buffer) => {
        out += d.toString();
        if (out.includes("held")) resolve();
      });
      this.child!.on("exit", (code) => reject(new Error(`DLL holder exited ${code}: ${out.trim()}`)));
    });
  }

  /** Unmap everything by ending the holding process, as the editor exiting does. */
  async release(): Promise<void> {
    const child = this.child;
    this.child = null;
    if (!child || child.exitCode !== null) return;
    const exited = new Promise((r) => child.once("exit", r));
    child.kill();
    await exited;
  }
}
