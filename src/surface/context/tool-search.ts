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

type SearchableTool = {
  name: string;
  actions?: Record<string, { description?: string; bridge?: string; effect?: string }>;
};

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
  ["nudge", "clockwise", "counterclockwise", "viewpoint"],
];

// Words too common to discriminate between actions, so they must not
// contribute to scoring (or to the adjacent-word pairs built from it).
const STOPWORDS = new Set([
  "a", "an", "as", "at", "be", "by", "do", "for", "from", "in", "into", "is", "it",
  "its", "of", "on", "or", "the", "to", "up", "via", "with", "and", "that", "this",
  "my", "me", "i", "we", "you", "want", "need", "make", "get", "set", "new", "some",
  "each", "every", "all", "any", "several",
]);

// A task summary says what the caller wants done to what. An action's effect
// answers the first half, so a read-only ask demotes the actions that mutate.
const READ_VERBS = new Set(["read", "get", "list", "inspect", "check", "dump", "show", "find", "query", "which", "what"]);
const MUTATE_VERBS = new Set([
  "set", "add", "create", "delete", "remove", "write", "modify", "change", "rename",
  "spawn", "import", "move", "apply", "assign", "update", "replace", "fix", "make",
]);
const MUTATE_PENALTY = 3;

// Descriptions are long free text, so their hits grow with length, not with
// relevance. Capped, a verbose action cannot outvote a name that says the word.
const TEXT_HIT_CAP = 3;
const PHRASE_HIT = 3;
// A query word naming the tool itself ("material parameter") outweighs incidental description text.
const TOOL_NAME_HIT = 2;
const PHRASE_HIT_CAP = 6;

/** Fold plurals together so "properties" meets "property". Both sides use it. */
function stem(w: string): string {
  return w.replace(/ies$/, "y").replace(/(ss|x)es$/, "$1").replace(/([^s])s$/, "$1");
}

/**
 * The meaningful words of a text, in order, stemmed. Split on anything that is
 * not a letter or digit, so punctuation ("class,") and underscores never glue
 * words together, and match on whole words, never substrings: "movement" is not
 * "move", "count" is not in "account", "order" is not in "reorder".
 */
function wordsOf(text: string): string[] {
  return text.toLowerCase().split(/[^a-z0-9]+/).filter((t) => t.length >= 3 && !STOPWORDS.has(t)).map(stem);
}

const pairsOf = (words: string[]): Set<string> =>
  new Set(words.slice(1).map((w, i) => `${words[i]} ${w}`));

const STEMMED_GROUPS = SYNONYM_GROUPS.map((g) => new Set(g.map(stem)));

/** The query words plus every member of a synonym group one of them belongs to. */
function expandTerms(words: string[]): Set<string> {
  const out = new Set<string>(words);
  for (const w of words) {
    for (const group of STEMMED_GROUPS) {
      if (group.has(w)) for (const g of group) out.add(g);
    }
  }
  return out;
}

// Actions that must never win a discovery ranking (they ARE the fallback / are
// advanced escape hatches), so they don't out-rank a real dedicated action.
const DEPRIORITIZE = new Set(["execute_python", "run_python_file", "execute_command"]);

// Wrapped Epic tools a single tool may place ahead of the other hits.
const EPIC_HITS_PER_TOOL = 2;

/** Rank the graph supplied by the caller, including that session's plugins. */
export function searchToolGraph(tools: SearchableTool[], query: string, limit = 20): ToolSearchHit[] {
  const q = (query ?? "").toLowerCase().trim();
  if (!q) return [];
  const queryWords = wordsOf(q);
  if (queryWords.length === 0) return [];
  const terms = expandTerms(queryWords);
  const queryPairs = pairsOf(queryWords);
  const queryPhrase = ` ${queryWords.join(" ")} `;
  const verbs = q.split(/[^a-z0-9]+/);
  const readOnlyAsk = verbs.some((v) => READ_VERBS.has(v)) && !verbs.some((v) => MUTATE_VERBS.has(v));

  // The live graph, not the pristine declaration: discovery has to see the
  // Epic and plugin actions the server actually advertises, and with one graph
  // per editor session those are no longer the same objects (#817).
  const hitsByHandler = new Map<string, ToolSearchHit & { epic: boolean }>();

  for (const tool of tools) {
    for (const [actionName, spec] of Object.entries(tool.actions ?? {})) {
      if (DEPRIORITIZE.has(actionName)) continue;
      const desc = spec?.description ?? "";
      const nameWords = wordsOf(actionName);
      const descWords = wordsOf(desc);
      const nameSet = new Set(nameWords);
      const toolSet = new Set(wordsOf(tool.name));
      const textSet = new Set(descWords);
      let nameHits = 0;
      let textHits = 0;
      for (const t of terms) {
        if (nameSet.has(t)) nameHits += 3; // action-name hit weighs most
        else if (toolSet.has(t)) nameHits += TOOL_NAME_HIT; // the tool is half the address ("material.list_parameters")
        else if (textSet.has(t)) textHits += 1;
      }
      // Two query words side by side in the name or description is a phrase
      // ("movement component"), far stronger than the two words apart.
      let phraseHits = 0;
      for (const p of [...pairsOf(nameWords), ...pairsOf(descWords)]) if (queryPairs.has(p)) phraseHits += PHRASE_HIT;
      let score = nameHits + Math.min(textHits, TEXT_HIT_CAP) + Math.min(phraseHits, PHRASE_HIT_CAP);
      if (` ${[...wordsOf(tool.name), ...nameWords, ...descWords].join(" ")} `.includes(queryPhrase)) score += 3; // whole-query phrase bonus
      if (readOnlyAsk && spec?.effect === "mutate") score -= MUTATE_PENALTY;
      if (score <= 0) continue;

      // Collapse aliases that route to the same bridge handler (e.g.
      // add_instances / add_hismc_instances) - keep the best-scoring name.
      // Generated Epic actions are different tools multiplexed through one
      // gateway, so their action names remain their search identities.
      const isEpicGatewayAction = actionName.startsWith("epic_") && spec?.bridge === "epic_call_tool";
      const key = `${tool.name}:${isEpicGatewayAction ? actionName : spec?.bridge ?? actionName}`;
      const existing = hitsByHandler.get(key);
      if (!existing || score > existing.score) {
        hitsByHandler.set(key, { tool: tool.name, action: actionName, description: desc, score, epic: isEpicGatewayAction });
      }
    }
  }

  // First-party actions win ties with wrapped Epic tools, and each tool ranks at
  // most EPIC_HITS_PER_TOOL wrappers in score order. Further wrappers follow
  // every other hit, so they stay findable without crowding the default page.
  const ranked = [...hitsByHandler.values()].sort(
    (a, b) => b.score - a.score || Number(a.epic) - Number(b.epic),
  );
  const primary: ToolSearchHit[] = [];
  const overflow: ToolSearchHit[] = [];
  const epicPerTool = new Map<string, number>();
  for (const { epic, ...hit } of ranked) {
    if (!epic) { primary.push(hit); continue; }
    const seen = epicPerTool.get(hit.tool) ?? 0;
    epicPerTool.set(hit.tool, seen + 1);
    (seen < EPIC_HITS_PER_TOOL ? primary : overflow).push(hit);
  }
  return [...primary, ...overflow].slice(0, limit);
}
