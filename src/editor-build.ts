/**
 * Compiling a project's C++ with UnrealBuildTool, out of process.
 *
 * Separate from editor-control.ts, which owns the editor's lifecycle: a build
 * needs no editor, and the one that matters most needs the editor down.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { spawn } from "child_process";
import * as os from "node:os";
import { readUeMcpConfig } from "./project.js";
import { EngineResolutionError, engineLookupFor, selectEngine } from "./engine-root.js";
import { invalidatePluginFreshness } from "./plugin-freshness.js";
import { readEnv } from "./env.js";

const IS_WINDOWS = process.platform === "win32";

/**
 * A project's `editor:` config, read from its .uproject path (#817).
 *
 * `buildProject` is handed a path rather than a loaded ProjectContext, and the
 * CLI build has no context at all, so the config is read from the project root.
 */
function readProjectEditorConfig(projectPath: string): { path?: string; buildToolPath?: string } {
  try {
    return readUeMcpConfig(path.dirname(path.resolve(projectPath))).editor ?? {};
  } catch {
    return {};
  }
}

/** Read EngineAssociation from a .uproject, or null if unreadable. */
function readEngineAssociation(projectPath: string): string | null {
  try {
    const parsed = JSON.parse(fs.readFileSync(projectPath, "utf-8"));
    return typeof parsed?.EngineAssociation === "string" ? parsed.EngineAssociation : null;
  } catch {
    return null;
  }
}

export interface BuildResult {
  success: boolean;
  message: string;
  exitCode: number | null;
}

function getPlatformString(): string {
  if (IS_WINDOWS) return "Win64";
  if (process.platform === "darwin") return "Mac";
  return "Linux";
}

/**
 * How many compiles this machine can run at once without running out of room.
 *
 * UnrealBuildTool defaults to one process per physical core. Each one maps the
 * Unreal precompiled header, which costs several GB, so on a machine with less
 * memory than cores x PCH the compiler does not queue: it fails outright with
 * C3859 ("failed to create virtual memory for PCH") or C1076, and the build
 * dies after several minutes of work. The failure names a paging file, so it
 * reads as a misconfigured machine rather than a parallelism default that does
 * not fit the hardware.
 *
 * This is not a knob anybody should have to find. The number is computed from
 * the machine and passed to every build, and the cap is only applied when it
 * is below what UnrealBuildTool would have chosen, so a machine with headroom
 * builds exactly as it did before.
 *
 * `UE_MCP_MAX_PARALLEL_ACTIONS` overrides it, for a machine whose real ceiling
 * this estimate gets wrong in either direction.
 */
export function safeParallelActions(
  totalMemBytes = os.totalmem(),
  cores = os.cpus().length,
): number {
  const override = Number(readEnv("maxParallelActions"));
  if (Number.isInteger(override) && override > 0) return override;

  // Measured against Unreal 5.8 on Windows: a PCH compile peaks around 5 GB,
  // and the OS plus the build server want a few GB that the compiles cannot
  // have. Both are deliberately pessimistic, because being one process under
  // costs a little wall clock and being one process over costs the whole build.
  const GB = 1024 ** 3;
  const PER_COMPILE_GB = 5;
  const RESERVED_GB = 4;
  const usableGB = totalMemBytes / GB - RESERVED_GB;
  const fits = Math.floor(usableGB / PER_COMPILE_GB);
  return Math.max(1, Math.min(cores, fits));
}

/** Whether a build died for want of memory rather than for anything in the code. */
export function ranOutOfMemory(output: string): boolean {
  return (
    output.includes("C3859")
    || output.includes("C1076")
    || output.includes("paging file is too small")
    || output.includes("internal heap limit reached")
  );
}

/** What to tell somebody whose build ran out of room, in the terms they hit it in. */
export function describeMemoryFailure(parallel: number, totalMemBytes = os.totalmem()): string {
  const gb = Math.round(totalMemBytes / 1024 ** 3);
  return (
    `The build ran out of memory rather than hitting anything wrong in the code. This machine has `
    + `${gb} GB of RAM and the compile was already limited to ${parallel} parallel `
    + `${parallel === 1 ? "process" : "processes"}, each of which needs several GB for Unreal's `
    + "precompiled header. Lower it further with UE_MCP_MAX_PARALLEL_ACTIONS, or give Windows a "
    + "larger paging file, and run the build again."
  );
}

export interface BuildOptions {
  /**
   * Receives the compiler output. Omitted, the output is dropped: stdout is the
   * MCP channel in a server, so a CLI that wants it passes its own writer.
   */
  onOutput?: (line: string) => void;
  /** Development (default), DebugGame, Shipping, Test. */
  configuration?: string;
  /** Win64, Mac, Linux. Defaults to the host platform. */
  platform?: string;
  /** Pass -Clean, which makes UnrealBuildTool rebuild from scratch. */
  clean?: boolean;
}

/**
 * Compile a project's C++ out of process, with UnrealBuildTool.
 *
 * Out of process is the point: UnrealBuildTool refuses to link while an editor
 * holds the module DLLs, so a full rebuild is exactly the case where the editor
 * must be down, and an editor that is down cannot answer a bridge call (#958).
 */
export async function buildProject(
  projectPath: string,
  opts: BuildOptions = {},
): Promise<BuildResult> {
  const resolvedPath = path.resolve(projectPath);

  if (!fs.existsSync(resolvedPath)) {
    return { success: false, exitCode: null, message: `Project file not found: ${resolvedPath}` };
  }

  // The failure names every location probed. The old message named one env var
  // and nothing else, so a missing tool, a wrong root and an unsupported layout
  // all read identically (#974).
  let buildTool: string;
  try {
    const engine = selectEngine(
      engineLookupFor(
        resolvedPath,
        readEngineAssociation(resolvedPath),
        readProjectEditorConfig(resolvedPath),
      ),
      "buildTool",
    );
    if (!engine.buildTool) throw new EngineResolutionError("Resolved engine names no build tool.", []);
    buildTool = engine.buildTool;
  } catch (err) {
    return {
      success: false,
      exitCode: null,
      message: err instanceof EngineResolutionError ? err.message : String(err),
    };
  }

  const projectName = path.basename(resolvedPath, path.extname(resolvedPath));
  const target = `${projectName}Editor`;
  const platform = opts.platform?.trim() || getPlatformString();
  const configuration = opts.configuration?.trim() || "Development";

  // #740: the quotes around the project path are SHELL syntax, not part of the
  // value. On Windows the args are joined into a single `cmd /c` string, so
  // they are required. Off Windows the args go straight into argv with no shell
  // to strip them, so UnrealBuildTool received a path containing literal quote
  // characters and reported "Unable to find project file" for a file that was
  // plainly there - while the same command pasted into a terminal worked,
  // because the shell removed them first.
  const commonArgs = [target, platform, configuration];
  // Only when it is below what UnrealBuildTool would have picked, so a machine
  // with headroom builds exactly as it did before and nobody has to know this
  // exists.
  const parallel = safeParallelActions();
  const parallelArgs = parallel < os.cpus().length ? [`-MaxParallelActions=${parallel}`] : [];
  const tailArgs = ["-WaitMutex", "-FromMsBuild", ...parallelArgs, ...(opts.clean ? ["-Clean"] : [])];
  const windowsArgs = [...commonArgs, `-Project="${resolvedPath}"`, ...tailArgs];
  const posixArgs = [...commonArgs, `-Project=${resolvedPath}`, ...tailArgs];

  return new Promise((resolve) => {
    let proc;
    if (IS_WINDOWS) {
      const quotedCommand = `"${buildTool}"`;
      const fullCommand = `cmd /c "${quotedCommand} ${windowsArgs.join(" ")}"`;
      proc = spawn(fullCommand, [], { shell: true, stdio: "pipe" });
    } else {
      proc = spawn(buildTool, posixArgs, { stdio: "pipe" });
    }

    // Kept so a failure can be read afterwards. The compiler reports running
    // out of room as a paging-file problem, which sends people to their
    // virtual memory settings for what is really a parallelism default that
    // does not fit the machine.
    let transcript = "";
    const forward = (data: Buffer) => {
      const text = data.toString();
      transcript += text;
      opts.onOutput?.(text);
    };

    if (proc.stdout) proc.stdout.on("data", forward);
    if (proc.stderr) proc.stderr.on("data", forward);

    proc.on("close", (code) => {
      // A build is the only event that can turn a "stale plugin" verdict fresh
      // ahead of the cache TTL, so drop the cached answer here rather than
      // making the next get_status report a binary that no longer exists.
      // Only this project's: a build in one editor says nothing about another.
      invalidatePluginFreshness(resolvedPath);
      resolve(
        code === 0
          ? { success: true, exitCode: 0, message: `Build succeeded (${target} ${platform} ${configuration})` }
          : {
              success: false,
              exitCode: code,
              message: ranOutOfMemory(transcript)
                ? describeMemoryFailure(parallel)
                : `Build failed with exit code ${code}`,
            },
      );
    });

    proc.on("error", (err) => {
      resolve({ success: false, exitCode: null, message: `Build error: ${err.message}` });
    });
  });
}
