import { buildProject } from "../../editor-build.js";
import { inspectInstall } from "../../install-check.js";
import { specBp } from "../specs/project.generated.js";
import type { ToolContext, ActionSpec } from "../../types.js";

/** Install, build and plugins: check the install, build, live coding, enable and disable plugins. */
export const installActions: Record<string, ActionSpec> = {
  check_install: {
    kind: "handler",
    effect: "read",
    description:
      "Answer whether this project can run the bridge at all, from disk, with no editor running and "
      + "nothing compiled. Reports the project kind (a project declaring no native modules of its own "
      + "is Blueprint-only, which is NOT a blocker: UnrealBuildTool writes temporary target and module "
      + "files under Intermediate/Source/ and compiles the plugin against them), the engine that will "
      + "be used and where it was resolved from, whether the plugin is deployed, enabled in the "
      + ".uproject, compiled and up to date with its source, and whether this machine has the C++ "
      + "toolchain Unreal needs. Every problem carries a stable code, what is wrong and the exact fix, "
      + "and nextSteps is those fixes in order. Read-only: it never deploys, enables or builds "
      + "anything. Params: projectPath? (default the loaded project), skipToolchain? (skip the "
      + "toolchain probe, which shells out to vswhere or the compiler)",
    handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
      const requested = (p.projectPath as string | undefined)?.trim();
      if (!requested) ctx.project.ensureLoaded();
      const uproject = requested || ctx.project.projectPath!;
      return inspectInstall(uproject, { skipToolchain: p.skipToolchain === true });
    },
  },
  build: {
    kind: "handler",
    effect: "mutate",
    // #958: this used to be dispatched over the editor bridge, so it failed
    // with ECONNREFUSED whenever the editor was down. That made it unusable
    // for its only real job: UnrealBuildTool refuses to link while an editor
    // holds the module DLLs, so a full rebuild has to happen with the editor
    // stopped. It runs UnrealBuildTool out of process instead, which needs no
    // editor at all.
    description:
      "Build the project's C++ out of process with UnrealBuildTool. Works with the editor STOPPED, which a full rebuild requires (UBT cannot link while an editor holds the module DLLs). Blocks until the build finishes and returns the compiler output. Params: configuration? (default Development), platform? (default the host platform), clean? (#958)",
    handler: async (ctx, p) => {
      ctx.project.ensureLoaded();
      const lines: string[] = [];
      const result = await buildProject(ctx.project.projectPath!, {
        onOutput: (text) => lines.push(text),
        configuration: p.configuration as string | undefined,
        platform: p.platform as string | undefined,
        clean: p.clean as boolean | undefined,
      });
      return { ...result, output: lines.join("") };
    },
  },
  generate_project_files: specBp("mutate", "Generate IDE project files (Visual Studio, Xcode, etc.).", "generate_project_files"),

  list_available_plugins: specBp("read", 
    "List every plugin installed in this engine or project, sorted by name, with its category, version, type, whether it is enabled in THIS editor session, whether it is enabled by default, and the .uproject's current reference to it under projectReference {present, enabled}. Those two disagree after enable_plugin until the editor restarts, which is the point of reporting both.",
    "list_available_plugins",
  ),
  enable_plugin: specBp("mutate", 
    "Enable a plugin in the .uproject. Plugin enablement is neither a UPROPERTY nor an INI key, it is a JSON array in the .uproject read once at startup, so set_config cannot reach it and without this a plugin-gated capability stays permanently unreachable through the bridge. Idempotent: a plugin already enabled, or enabled by default with no entry, reports existed and writes nothing. The change is a file change, so modules, classes, content and settings appear only after editor(restart_editor), which the result says.",
    "enable_plugin",
  ),
  disable_plugin: specBp("mutate", 
    "Disable a plugin in the .uproject. removeReference deletes the entry outright instead of writing an explicit disable, which is the difference between handing a default-on plugin back to its default and overriding it, and the two are not the same file. Idempotent against whichever of the two was asked for. Refuses to disable the bridge itself, since that would leave no way to undo it. Takes effect on the next editor start.",
    "disable_plugin",
  ),
  live_coding_compile: {
    ...specBp("mutate", "Trigger a Live Coding compile (Windows only). Hot-patches method bodies of existing UCLASSes without editor restart - the fast inner loop for UFUNCTION implementations. Does NOT reliably register brand-new UCLASSes; use build_project + editor restart for those.", "live_coding_compile"),
    timeoutMs: 300_000,
  },
  live_coding_status: specBp("read", 
    "Report Live Coding availability/state (available, started, enabledForSession, compiling). Helps choose between live_coding_compile and build_project.",
    "live_coding_status",
  ),
};
