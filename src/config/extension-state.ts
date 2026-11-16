// @session per-session: Keyed by absolute project root: the daemon extension versions each project loads.
import { ensureProjectEntry, projectEntry, readState, updateState } from "./state-store.js";

/** The daemon extension version each project loads, by extension name (#1619). */

function versionsOf(entry: Record<string, unknown> | undefined): Record<string, unknown> {
  const v = entry?.extensions;
  return v && typeof v === "object" && !Array.isArray(v) ? (v as Record<string, unknown>) : {};
}

/** The version of extension `name` this project records, or null. */
export function getProjectExtensionVersion(projectRoot: string, name: string): string | null {
  const v = versionsOf(projectEntry(readState(), projectRoot))[name];
  return typeof v === "string" && v ? v : null;
}

/** Record the version of extension `name` this project loads. Pass undefined to clear it. */
export function setProjectExtensionVersion(projectRoot: string, name: string, version: string | undefined): void {
  updateState((state) => {
    const entry = ensureProjectEntry(state, projectRoot);
    const extensions = { ...versionsOf(entry) };
    if (version === undefined) delete extensions[name];
    else extensions[name] = version;
    entry.extensions = extensions;
  });
}

/** Every version of extension `name` some project records. */
export function recordedExtensionVersions(name: string): Set<string> {
  const out = new Set<string>();
  for (const entry of Object.values(readState().projects ?? {})) {
    const v = versionsOf(entry)[name];
    if (typeof v === "string" && v) out.add(v);
  }
  return out;
}

/** Forget extension `name` in every project, when it is removed from the machine. */
export function clearExtensionRecords(name: string): void {
  if (recordedExtensionVersions(name).size === 0) return;
  updateState((state) => {
    for (const entry of Object.values(state.projects ?? {})) {
      const extensions = versionsOf(entry);
      if (name in extensions) delete extensions[name];
    }
  });
}
