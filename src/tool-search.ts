/**
 * Shared tool-registry search used by project(search_tools) AND the
 * execute_python interceptor. Keyword search over every tool + action, with a
 * task-intent synonym layer so a caller who searches by INTENT ("screenshot the
 * game", "make a texture tile") finds the dedicated action even though the
 * action's own text is implementation-worded (capture_scene_png, MF_TextureBomb).
 * (#704)
 */

export interface ToolSearchHit {
  tool: string;
  action: string;
  description: string;
  score: number;
}

// Task-verb / noun synonyms. When a query contains the KEY (or any value), the
// whole group is added to the term set before scoring, so intent words bridge
// to the implementation words that appear in action names/descriptions.
const SYNONYM_GROUPS: string[][] = [
  ["screenshot", "capture", "png", "image", "picture", "frame", "render", "grab", "snap"],
  ["spawn", "place", "instantiate", "add", "put", "drop"],
  ["import", "bring", "ingest", "load"],
  ["delete", "remove", "destroy", "erase"],
  ["reorder", "move", "shift", "order", "arrange", "sort", "insert"],
  ["tile", "tiling", "tileable", "detile", "texturebomb"],
  ["retarget", "retargeting", "ik", "rig"],
  ["cloth", "clothing", "chaos", "maxdistance"],
  ["automation", "test", "runtest"],
  ["nanite", "mesh"],
  ["blendspace", "blend"],
  ["morph", "curve", "posedriver", "pose"],
  ["statetree", "brain", "behavior"],
  ["impulse", "force", "physics", "push"],
  ["material", "shader", "bsdf", "substrate"],
  ["widget", "umg", "hud", "button", "style", "font"],
  ["gas", "ability", "attribute", "gameplayeffect"],
  ["light", "lighting", "skylight", "fog", "volumetric"],
  ["audio", "sound", "wav", "soundwave", "music"],
  ["reference", "referencer", "dependency", "dependencies"],
  ["run", "invoke", "call", "fire", "trigger", "execute"],
  ["preview", "thumbnail", "lit"],
  ["config", "setting", "property", "param", "parameter"],
];

// Words too common/short to discriminate. "a"/"an"/"as" as substrings match
// almost every action name, so they must not contribute to scoring.
const STOPWORDS = new Set([
  "a", "an", "as", "at", "be", "by", "do", "for", "from", "in", "into", "is", "it",
  "its", "of", "on", "or", "the", "to", "up", "via", "with", "and", "that", "this",
  "my", "me", "i", "we", "you", "want", "need", "make", "get", "set", "new", "some",
]);

function meaningfulTerms(rawTerms: string[]): string[] {
  return rawTerms.filter((t) => t.length >= 3 && !STOPWORDS.has(t));
}

function expandTerms(rawTerms: string[]): Set<string> {
  const out = new Set<string>(rawTerms);
  for (const term of rawTerms) {
    for (const group of SYNONYM_GROUPS) {
      if (group.some((g) => term.includes(g) || g.includes(term))) {
        for (const g of group) out.add(g);
      }
    }
  }
  return out;
}

// Actions that must never win a discovery ranking (they ARE the fallback / are
// advanced escape hatches), so they don't out-rank a real dedicated action.
const DEPRIORITIZE = new Set(["execute_python", "run_python_file", "execute_command"]);

/**
 * Search every registered tool + action for a keyword/intent query.
 * The tool graph is imported lazily to avoid a load-time circular dependency
 * (tools.ts imports the project tool, which imports this).
 */
export async function searchTools(query: string, limit = 20): Promise<ToolSearchHit[]> {
  const q = (query ?? "").toLowerCase().trim();
  if (!q) return [];
  const rawTerms = meaningfulTerms(q.split(/\s+/).filter(Boolean));
  if (rawTerms.length === 0) return [];
  const terms = expandTerms(rawTerms);

  // The live graph, not the pristine declaration: discovery has to see the
  // Epic and plugin actions the server actually advertises, and with one graph
  // per editor session those are no longer the same objects (#817).
  const { getLiveToolGraph } = await import("./tools.js");
  const hitsByHandler = new Map<string, ToolSearchHit>();

  for (const tool of getLiveToolGraph() as Array<{ name: string; actions?: Record<string, { description?: string; bridge?: string }> }>) {
    for (const [actionName, spec] of Object.entries(tool.actions ?? {})) {
      if (DEPRIORITIZE.has(actionName)) continue;
      const desc = spec?.description ?? "";
      const nameL = actionName.toLowerCase();
      const hay = `${tool.name} ${nameL} ${desc}`.toLowerCase();
      let score = 0;
      for (const t of terms) {
        if (!t) continue;
        if (nameL.includes(t)) score += 3; // action-name hit weighs most
        else if (hay.includes(t)) score += 1;
      }
      if (hay.includes(q)) score += 3; // whole-query phrase bonus
      if (score <= 0) continue;

      // Collapse aliases that route to the same bridge handler (e.g.
      // add_instances / add_hismc_instances) - keep the best-scoring name.
      const key = `${tool.name}:${spec?.bridge ?? actionName}`;
      const existing = hitsByHandler.get(key);
      if (!existing || score > existing.score) {
        hitsByHandler.set(key, { tool: tool.name, action: actionName, description: desc, score });
      }
    }
  }

  return [...hitsByHandler.values()].sort((a, b) => b.score - a.score).slice(0, limit);
}
