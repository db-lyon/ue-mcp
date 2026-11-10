import { specBp } from "../specs/project.generated.js";
import type { ActionSpec } from "../../core/types.js";

/**
 * The project's localization: its cultures, its localization targets, and a
 * gather and a compile for one target. String table entries and FText keys are
 * asset actions.
 */
export const localizationActions: Record<string, ActionSpec> = {
  list_localization_cultures: specBp("read", "Report the project's cultures: the editor's current culture, language and locale, the game's native culture and the cultures it has compiled localization for, CulturesToStage from the packaging settings, and every project localization target with its native culture and supported cultures.", "list_localization_cultures"),
  list_localization_targets: specBp("read", "List the project's localization targets (includeEngineTargets adds the engine's): name, guid, cultures with word counts, nativeCulture, what each gathers from (packages with include/exclude paths, source files with search directories, metadata), conflict status, and its files (gather and compile configs, manifest, an archive and a LocRes per culture, each with exists, bytes and modified). Every row is valid set_localization_target input.", "list_localization_targets"),
  set_localization_target: specBp("mutate", "Create or change a project localization target: cultures (replaces the list), nativeCulture, gatherFromPackages with includePaths/excludePaths (wildcards relative to the project root, e.g. Content/UI/*), gatherFromTextFiles with searchDirectories. A new target gathers nothing until told what to. Written to DefaultEditor.ini as Project Settings would. A call that changes nothing reports unchanged; the rollback removes a created target or restores the previous settings.", "set_localization_target"),
  remove_localization_target: specBp("mutate", "Remove a project localization target; deleteFiles also deletes its configs, manifest, archives and LocRes. The rollback recreates its settings (not deleted files). Removing one that is not there reports alreadyRemoved.", "remove_localization_target"),
  gather_localization_target: {
    ...specBp("mutate", "Gather text for one localization target: writes its gather config and runs the GatherText commandlet in a separate process, as the Localization Dashboard's Gather Text does, then answers with the exit code, the error lines, a log tail and the target's refreshed files and word counts. A second gather or compile while one runs is refused.", "gather_localization_target"),
    timeoutMs: 1_800_000,
  },
  compile_localization_target: {
    ...specBp("mutate", "Compile one localization target's archives into LocRes files (culture narrows it to one), through the GatherText commandlet as the Localization Dashboard's Compile Text does, then refreshes the editor's loaded localization. Answers with the exit code, error lines, a log tail and the target's files.", "compile_localization_target"),
    timeoutMs: 1_800_000,
  },
};
