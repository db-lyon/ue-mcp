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
import { readUeMcpConfig } from "../config/project.js";
import { EngineResolutionError, engineLookupFor, selectEngine } from "./engine-root.js";
import { invalidatePluginFreshness } from "./bridge-freshness.js";
import { clearPendingBuild, markPendingBuildFailed } from "./pending-build.js";
import { readEnv } from "../core/env.js";

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
  /** Outputs the link could not write because another process held them open. */
  lockedOutputs?: LockedOutput[];
  /** UnrealBuildTool's own verdict line, e.g. "Result: Failed (OtherCompilationError)". */
  resultLine?: string;
  /** The first compiler and linker errors, as `file(line,col): error CODE: text`. */
  errors?: string[];
}

/** How many compiler errors a failed build names in its message. */
const ERRORS_IN_MESSAGE = 5;

/**
 * What a failed build said about itself: UnrealBuildTool's verdict line and
 * the first compiler or linker errors, deduplicated in the order they came.
 * A failure reported as an exit code alone sent the caller to the log to find
 * a one-line C4458 (#1384).
 */
export function readBuildFailure(output: string): { resultLine: string | null; errors: string[] } {
  const lines = output.split(/\r?\n/).map((l) => l.trim()).filter(Boolean);
  const resultLine = [...lines].reverse().find((l) => /^Result:\s*\S/.test(l)) ?? null;
  const errors: string[] = [];
  for (const l of lines) {
    // A rules failure ("Could not find definition for module ...") stops UBT before any compile.
    const rules = /^CompilationResultException:\s*\S+\s+\S/.test(l);
    if (!rules && !/\b(fatal )?error\b/i.test(l)) continue;
    const located = /\(\d+(,\d+)?\)\s*:\s*(fatal )?error\b/i.test(l);
    const coded = /\berror (C|LNK|MSB)\d+/i.test(l) || /^error\s*:/i.test(l) || /: error:/.test(l);
    if ((rules || located || coded) && !errors.includes(l)) errors.push(l);
  }
  return { resultLine, errors };
}

/** The message a failed build returns: exit code, verdict, and the errors to look at first. */
export function describeBuildFailure(code: number | null, output: string): string {
  const { resultLine, errors } = readBuildFailure(output);
  const verdict = resultLine ? ` (${resultLine})` : "";
  if (errors.length === 0) return `Build failed with exit code ${code}${verdict}.`;
  const more = errors.length > ERRORS_IN_MESSAGE ? ` (${errors.length - ERRORS_IN_MESSAGE} more in output)` : "";
  return `Build failed with exit code ${code}${verdict}. First errors: ${errors.slice(0, ERRORS_IN_MESSAGE).join(" | ")}${more}`;
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

/** A file the build could not write because another process had it open. */
export interface LockedOutput {
  file: string;
  /** The process holding it, as the build tool named it, or null when it did not. */
  holder: string | null;
}

/**
 * Every output the link could not write because something held it open.
 *
 * UnrealBuildAccelerator names the holder on its own line ("ERROR opening file
 * X for write ... being used by another process. - <holder exe>"); the linker
 * only says LNK1104 and the file. Both are read, one entry per file, preferring
 * the line that names a holder.
 */
export function findLockedOutputs(output: string): LockedOutput[] {
  const byFile = new Map<string, LockedOutput>();
  const add = (file: string, holder: string | null): void => {
    const key = file.replace(/\\/g, "/").toLowerCase();
    const known = byFile.get(key);
    if (!known) byFile.set(key, { file, holder });
    else if (!known.holder && holder) known.holder = holder;
  };
  for (const line of output.split(/\r?\n/)) {
    const uba = /opening file\s+['"]?(.+?)['"]?\s+for write\b.*being used by another process\.?\s*-\s*(.+?)\)?\s*$/i.exec(line);
    if (uba) {
      add(uba[1].trim(), uba[2].trim().replace(/[).]+$/, "") || null);
      continue;
    }
    const lnk = /LNK1104:\s*cannot open file\s+'([^']+)'/i.exec(line);
    if (lnk) add(lnk[1].trim(), null);
  }
  return [...byFile.values()];
}

/** What to tell somebody whose link failed on a locked file, naming the holder. */
export function describeLockedOutputs(code: number | null, locked: LockedOutput[]): string {
  const detail = locked
    .map((l) => `${l.file} is held open by ${l.holder ?? "another process the build did not name"}`)
    .join("; ");
  // Split on both separators: the path is a Windows one whatever this runs on.
  const holders = locked.map((l) => (l.holder ?? "").split(/[\\/]/).pop()!.toLowerCase());
  let remedy: string;
  if (holders.some((h) => h.startsWith("crashreportclient"))) {
    remedy =
      "That is the crash reporter a crashed editor left behind. Run editor(action='stop_editor'), which ends a " +
      "crash reporter belonging to this project, then build again.";
  } else if (holders.some((h) => h.startsWith("unrealeditor"))) {
    remedy = "An editor still has the module loaded. Stop it with editor(action='stop_editor'), then build again.";
  } else if (holders.every((h) => h === "")) {
    remedy =
      "Run editor(action='stop_editor'), which stops this project's editor and ends a crash reporter a crashed " +
      "editor left holding its binaries, then build again. If it still fails, close whatever process has that file open.";
  } else {
    remedy = "Close that process, then build again.";
  }
  return `Build failed with exit code ${code}: the link could not write its output. ${detail}. ${remedy}`;
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
 * UnrealBuildTool's arguments for one editor-target build.
 *
 * -NoHotReloadFromIDE: without it, any running editor of the same engine with
 * Live Coding on makes UnrealBuildTool refuse the build, whatever project that
 * editor has open. The build is out of process and links its own project, so
 * it has no reason to wait on another editor's session.
 */
export function buildToolArgs(o: {
  target: string;
  platform: string;
  configuration: string;
  projectPath: string;
  clean?: boolean;
  windows: boolean;
}): string[] {
  // Only when it is below what UnrealBuildTool would have picked, so a machine
  // with headroom builds exactly as it did before and nobody has to know this
  // exists.
  const parallel = safeParallelActions();
  const parallelArgs = parallel < os.cpus().length ? [`-MaxParallelActions=${parallel}`] : [];
  // The quotes around the project path are shell syntax for the Windows `cmd /c`
  // line (#740); off Windows the path goes straight into argv.
  const project = o.windows ? `-Project="${o.projectPath}"` : `-Project=${o.projectPath}`;
  return [
    o.target, o.platform, o.configuration, project,
    "-WaitMutex", "-FromMsBuild", "-NoHotReloadFromIDE", ...parallelArgs, ...(o.clean ? ["-Clean"] : []),
  ];
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
  const windowsArgs = buildToolArgs({ target, platform, configuration, projectPath: resolvedPath, clean: opts.clean, windows: true });
  const posixArgs = buildToolArgs({ target, platform, configuration, projectPath: resolvedPath, clean: opts.clean, windows: false });

  return new Promise((resolve) => {
    let proc;
    if (IS_WINDOWS) {
      const quotedCommand = `"${buildTool}"`;
      const fullCommand = `cmd /c "${quotedCommand} ${windowsArgs.join(" ")}"`;
      proc = spawn(fullCommand, [], { windowsHide: true, shell: true, stdio: "pipe" });
    } else {
      proc = spawn(buildTool, posixArgs, { windowsHide: true, stdio: "pipe" });
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
      if (code === 0) {
        // The editor target includes the bridge, so a build owed since a source update is paid.
        clearPendingBuild(resolvedPath);
        resolve({ success: true, exitCode: 0, message: `Build succeeded (${target} ${platform} ${configuration})` });
        return;
      }
      const locked = ranOutOfMemory(transcript) ? [] : findLockedOutputs(transcript);
      const failure = readBuildFailure(transcript);
      const message = ranOutOfMemory(transcript)
        ? describeMemoryFailure(safeParallelActions())
        : locked.length > 0
          ? describeLockedOutputs(code, locked)
          : describeBuildFailure(code, transcript);
      markPendingBuildFailed(resolvedPath, message);
      resolve({
        success: false,
        exitCode: code,
        message,
        ...(locked.length > 0 ? { lockedOutputs: locked } : {}),
        ...(failure.resultLine ? { resultLine: failure.resultLine } : {}),
        ...(failure.errors.length > 0 ? { errors: failure.errors } : {}),
      });
    });

    proc.on("error", (err) => {
      resolve({ success: false, exitCode: null, message: `Build error: ${err.message}` });
    });
  });
}
