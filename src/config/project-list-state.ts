// @session independent: The machine's project list, one per user, not per session.
import { readState, updateState, type UserState } from "./state-store.js";

/** The machine's project list beyond Unreal's recent ones: .uproject paths the user added, and found ones they hid. */
export interface ProjectList {
  added: string[];
  hidden: string[];
}

function projectListOf(state: UserState): ProjectList {
  const list = state.projectList as { added?: unknown; hidden?: unknown } | undefined;
  const paths = (v: unknown): string[] => (Array.isArray(v) ? v.filter((p): p is string => typeof p === "string" && p.length > 0) : []);
  return { added: paths(list?.added), hidden: paths(list?.hidden) };
}

export function getProjectList(): ProjectList {
  return projectListOf(readState());
}

/** Change the project list under the state lock. */
export function updateProjectList(mutate: (list: ProjectList) => void): void {
  updateState((state) => {
    const list = projectListOf(state);
    mutate(list);
    state.projectList = list;
  });
}
