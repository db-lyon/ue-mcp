/**
 * The editor launch shape: which window mode applies, what an `extraArgs`
 * string means, and how the editor's command line reads back.
 *
 * These are pure rules, so they are pinned here rather than through a launch:
 * spawning an editor to observe argv would put a window on somebody's screen.
 */
import { describe, expect, it } from "vitest";
import {
  buildEditorLaunchArgs,
  isOffscreenCommandLine,
  renderWindowsCommandLine,
  resolveWindowMode,
  splitEditorArgs,
} from "../../src/editor/launch-args.js";
import { UeMcpConfigSchema } from "../../src/surface/schemas.js";

describe("resolveWindowMode", () => {
  it("prefers the per-call value, then the project config, then normal", () => {
    expect(resolveWindowMode(undefined, undefined)).toBe("normal");
    expect(resolveWindowMode(undefined, "offscreen")).toBe("offscreen");
    expect(resolveWindowMode(undefined, "normal")).toBe("normal");
    expect(resolveWindowMode("offscreen", "normal")).toBe("offscreen");
    expect(resolveWindowMode("normal", "offscreen")).toBe("normal");
  });
});

describe("splitEditorArgs", () => {
  it("splits on whitespace", () => {
    expect(splitEditorArgs("-a b c")).toEqual(["-a", "b", "c"]);
  });

  it("keeps a quoted run as one argument, quotes removed", () => {
    expect(splitEditorArgs('-a "b c" d')).toEqual(["-a", "b c", "d"]);
  });

  it("continues an unquoted run through a quoted one", () => {
    expect(splitEditorArgs('-ini:X:[S]:K="v w"')).toEqual(["-ini:X:[S]:K=v w"]);
  });

  it("returns nothing for empty and whitespace-only input", () => {
    expect(splitEditorArgs("")).toEqual([]);
    expect(splitEditorArgs("   \t ")).toEqual([]);
  });

  it("collapses runs of whitespace", () => {
    expect(splitEditorArgs("  a   b  ")).toEqual(["a", "b"]);
  });

  it("keeps a quoted empty run as an empty argument", () => {
    expect(splitEditorArgs('-a ""')).toEqual(["-a", ""]);
  });

  it("refuses an unmatched quote, naming the input", () => {
    expect(() => splitEditorArgs('-a "b c')).toThrow(/unmatched/i);
    expect(() => splitEditorArgs('-a "b c')).toThrow(/-a "b c/);
  });
});

describe("buildEditorLaunchArgs", () => {
  it("is exactly the project path for a normal launch with no extras", () => {
    expect(buildEditorLaunchArgs("C:/P/P.uproject", { window: "normal" })).toEqual(["C:/P/P.uproject"]);
    expect(buildEditorLaunchArgs("C:/P/P.uproject", { window: "normal", configExtraArgs: "", extraArgs: "  " }))
      .toEqual(["C:/P/P.uproject"]);
  });

  it("puts the offscreen switch straight after the project", () => {
    expect(buildEditorLaunchArgs("C:/P/P.uproject", { window: "offscreen" }))
      .toEqual(["C:/P/P.uproject", "-RenderOffScreen"]);
  });

  it("splits a configured string and keeps a configured list item by item", () => {
    expect(buildEditorLaunchArgs("P", { window: "normal", configExtraArgs: '-x "y z"' }))
      .toEqual(["P", "-x", "y z"]);
    expect(buildEditorLaunchArgs("P", { window: "normal", configExtraArgs: ["-a b", "c"] }))
      .toEqual(["P", "-a b", "c"]);
  });

  it("puts configured extras before per-call extras, after the offscreen switch", () => {
    expect(buildEditorLaunchArgs("P", { window: "offscreen", configExtraArgs: ["-cfg"], extraArgs: "-call 1" }))
      .toEqual(["P", "-RenderOffScreen", "-cfg", "-call", "1"]);
  });
});

/**
 * On Windows Unreal reads the raw command line, and its parser skips quoted
 * text, so `"-ExecCmds=stat fps"` is invisible to it while `-ExecCmds="stat fps"`
 * works. Each piece therefore reaches the line exactly as it was typed.
 */
describe("the Windows command line", () => {
  it("keeps each piece's quotes when asked to", () => {
    expect(splitEditorArgs('-ExecCmds="stat fps" -x', { keepQuotes: true })).toEqual(['-ExecCmds="stat fps"', "-x"]);
    expect(splitEditorArgs('"a b"  c', { keepQuotes: true })).toEqual(['"a b"', "c"]);
  });

  it("still refuses an unmatched quote while keeping quotes", () => {
    expect(() => splitEditorArgs('-a "b c', { keepQuotes: true })).toThrow(/unmatched/i);
  });

  it("renders -ExecCmds=\"stat fps\" unchanged inside the line", () => {
    const args = buildEditorLaunchArgs("C:/P/P.uproject", { window: "offscreen", extraArgs: '-ExecCmds="stat fps"', keepQuotes: true });
    expect(renderWindowsCommandLine(args)).toBe('"C:/P/P.uproject" -RenderOffScreen -ExecCmds="stat fps"');
  });

  it("keeps the backslashes of a quoted path", () => {
    const args = buildEditorLaunchArgs("C:/P/P.uproject", { window: "normal", extraArgs: '-abslog="C:\\a b\\x.log"', keepQuotes: true });
    expect(renderWindowsCommandLine(args)).toBe('"C:/P/P.uproject" -abslog="C:\\a b\\x.log"');
  });

  it("quotes a project path that holds a space", () => {
    expect(renderWindowsCommandLine(["C:\\My Projects\\P.uproject"])).toBe('"C:\\My Projects\\P.uproject"');
  });

  it("passes a configured list item through as written, quotes included", () => {
    const args = buildEditorLaunchArgs("P", { window: "normal", configExtraArgs: ['-ExecCmds="stat fps"', "-log"], keepQuotes: true });
    expect(renderWindowsCommandLine(args)).toBe('"P" -ExecCmds="stat fps" -log');
  });

  it("still strips the quotes for the POSIX argv", () => {
    expect(buildEditorLaunchArgs("P", { window: "normal", extraArgs: '-ExecCmds="stat fps"' }))
      .toEqual(["P", "-ExecCmds=stat fps"]);
  });
});

describe("isOffscreenCommandLine", () => {
  it("recognises the switch as its own word, in any case", () => {
    expect(isOffscreenCommandLine("UnrealEditor.exe P.uproject -RenderOffScreen")).toBe(true);
    expect(isOffscreenCommandLine("-renderoffscreen")).toBe(true);
    expect(isOffscreenCommandLine("UnrealEditor.exe -RENDEROFFSCREEN P.uproject")).toBe(true);
  });

  it("takes the / switch prefix Unreal also accepts", () => {
    expect(isOffscreenCommandLine("UnrealEditor.exe P.uproject /RenderOffScreen")).toBe(true);
    expect(isOffscreenCommandLine("UnrealEditor.exe P.uproject /renderOFFscreen")).toBe(true);
  });

  it("does not count the text inside a quoted path", () => {
    expect(isOffscreenCommandLine('UnrealEditor.exe "C:\\a -RenderOffScreen b\\P.uproject"')).toBe(false);
    expect(isOffscreenCommandLine('UnrealEditor.exe "C:\\a -RenderOffScreen b\\P.uproject" -RenderOffScreen')).toBe(true);
    // Unreal skips quoted text when it looks for a switch.
    expect(isOffscreenCommandLine('UnrealEditor.exe P.uproject "-RenderOffScreen"')).toBe(false);
    // An unclosed quote runs to the end of the line, and is not an error here.
    expect(isOffscreenCommandLine('UnrealEditor.exe "P.uproject -RenderOffScreen')).toBe(false);
  });

  it("refuses a switch with a value or a longer name", () => {
    expect(isOffscreenCommandLine("UnrealEditor.exe P.uproject -RenderOffScreen=1")).toBe(false);
    expect(isOffscreenCommandLine("UnrealEditor.exe P.uproject -RenderOffScreenX")).toBe(false);
  });

  it("refuses near misses and absent command lines", () => {
    expect(isOffscreenCommandLine("UnrealEditor.exe -RenderOffScreenX P.uproject")).toBe(false);
    expect(isOffscreenCommandLine("UnrealEditor.exe --RenderOffScreen")).toBe(false);
    expect(isOffscreenCommandLine("UnrealEditor.exe P.uproject")).toBe(false);
    expect(isOffscreenCommandLine("")).toBe(false);
    expect(isOffscreenCommandLine(null)).toBe(false);
    expect(isOffscreenCommandLine(undefined)).toBe(false);
  });
});

describe("the editor config schema", () => {
  it("accepts both window modes and both extraArgs forms", () => {
    for (const window of ["normal", "offscreen"] as const) {
      expect(UeMcpConfigSchema.safeParse({ editor: { window } }).success, window).toBe(true);
    }
    expect(UeMcpConfigSchema.safeParse({ editor: { extraArgs: "-a b" } }).success).toBe(true);
    expect(UeMcpConfigSchema.safeParse({ editor: { extraArgs: ["-a b", "c"] } }).success).toBe(true);
  });

  it("refuses a mode it does not know", () => {
    const parsed = UeMcpConfigSchema.safeParse({ editor: { window: "hidden" } });
    expect(parsed.success).toBe(false);
  });

  it("refuses extraArgs of the wrong shape", () => {
    expect(UeMcpConfigSchema.safeParse({ editor: { extraArgs: 1 } }).success).toBe(false);
    expect(UeMcpConfigSchema.safeParse({ editor: { extraArgs: [1] } }).success).toBe(false);
  });
});
