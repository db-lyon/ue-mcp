import * as fs from "node:fs";
import * as path from "node:path";
import type { PluginInfo } from "./types.js";
import {
  CORE_REPO,
  fetchRegistryCatalog,
  parseGitHubRepo,
  parseRepoSlug,
  repoSlug,
  sameRepo,
  type GitHubRepo,
  type RegistryPlugin,
} from "./registry-catalog.js";
import { debug } from "./log.js";

/**
 * Decide which tracker a feedback report belongs in.
 *
 * ue-mcp is a core server plus a growing set of npm-distributed plugins, each
 * with its own repo. An agent that hits a wall in a plugin-owned surface (PIE
 * record/replay, Perforce, Meshy, Voxel) has no idea the surface is not core,
 * so every report lands on the core tracker and has to be re-filed by hand.
 *
 * This module reads the published registry (plugins.ue-mcp.com) plus the set
 * of plugins loaded into THIS project and works out whether a better home
 * exists. It never posts anything and never throws: the worst case is
 * "target: core", which is exactly what happened before it existed.
 *
 * Precedence, strongest first:
 *
 *   1. explicitRepo         - the caller named a repo; honored if the registry
 *                             knows it (or it is core). Unknown repos are
 *                             refused, so the agent cannot aim a submission at
 *                             an arbitrary GitHub project.
 *   2. installed ownership  - `idealTool` names a category or action that a
 *                             locally loaded plugin actually provides. This is
 *                             a fact, not a guess: confidence "certain".
 *   3. core anchor          - the text names a real built-in category or action
 *                             (`editor(play_in_editor)`). Core owns it; any
 *                             keyword hit is demoted to a suggestion.
 *   4. keyword scoring      - registry slug / name / tag terms weighed by where
 *                             they appear (call syntax > idealTool > title >
 *                             summary).
 */

export type RouteConfidence = "certain" | "likely" | "possible";

export interface RouteCandidate {
  slug: string;
  name: string;
  packageName?: string;
  /** Where issues for this plugin go. null when the registry has no repo. */
  repo: GitHubRepo | null;
  repoUrl?: string;
  repoPrivate: boolean;
  /** True when this plugin is loaded in the current project. */
  installed: boolean;
  score: number;
  confidence: RouteConfidence;
  reasons: string[];
}

export interface RoutingDecision {
  /** Where the report will actually be filed. */
  target: "core" | "plugin";
  repo: GitHubRepo;
  /** The winning plugin, when target is "plugin". */
  candidate: RouteCandidate | null;
  /** Plugins worth offering the user even though core is the default. */
  suggestions: RouteCandidate[];
  /** The built-in category/action the text named, if any. */
  coreAnchor: string | null;
  /** False when the registry was unreachable and no cache existed. */
  catalogAvailable: boolean;
  /** Why the decision landed where it did, in one line, when non-obvious. */
  note?: string;
}

export interface RoutingInput {
  title: string;
  summary: string;
  idealTool?: string;
  /** Plugins loaded into the current project (ctx.getPlugins()). */
  installed?: PluginInfo[];
  /** `owner/name` override from the caller. */
  explicitRepo?: string;
  /** Injected catalog. Tests pass this; production leaves it undefined. */
  catalog?: RegistryPlugin[];
  timeoutMs?: number;
}

/* ── scoring weights ───────────────────────────────────────────────── */

/**
 * Two classes of term, and the difference decides whether a report is routed
 * or merely flagged.
 *
 *   strong - the plugin's own identity: slug, name, package, and the tool
 *            categories it provides. "pie", "meshy", "perforce".
 *   weak   - descriptive tags the listing chose: "import", "replay",
 *            "testing". Real signal, but shared with ordinary Unreal
 *            vocabulary, so "importing a mesh loses its materials" must not
 *            become a Meshy issue on the strength of the word "import".
 *
 * A weak match can suggest a plugin at the approval prompt. Only a strong one
 * can make it the default destination.
 */
const W_STRONG_CALL = 12; // "pie(replay)" / "pie.replay" and pie is not built-in
const W_STRONG_IDEAL = 10;
const W_STRONG_TITLE = 6;
const W_STRONG_SUMMARY = 3;
const W_WEAK_CALL = 6;
const W_WEAK_IDEAL = 4;
const W_WEAK_TITLE = 2;
const W_WEAK_SUMMARY = 1;
const W_PHRASE_TITLE = 6; // full multi-word slug, e.g. "pie studio"
const W_PHRASE_SUMMARY = 4;
const W_INSTALLED = 3;    // the plugin is actually loaded in this project

const LIKELY_AT = 9;
// Low enough that a tag echoed in both the title and the summary still shows
// up as "this also matched" at the approval prompt, high enough that a single
// passing mention does not.
const POSSIBLE_AT = 3;

/**
 * Terms too generic to route on. Every ue-mcp plugin is a "plugin" with
 * "tools" for "unreal"; matching those would send half the tracker to
 * whichever row happened to be listed first.
 */
const GENERIC_TOKENS = new Set([
  "studio", "plugin", "plugins", "tool", "tools", "ue", "ue5", "unreal", "mcp",
  "engine", "bridge", "free", "other", "core", "server", "agent", "game", "games",
  "test", "tests", "testing", "support", "api", "helper", "helpers", "utils", "util",
  "generation", "generator", "control", "controls", "runtime", "data",
]);

/* ── the built-in surface ──────────────────────────────────────────── */

interface CoreSurface {
  categories: Set<string>;
  /** Action names worth matching on: underscored and long enough to be unique. */
  actions: Map<string, string>; // action -> category
}

let coreSurfaceCache: CoreSurface | null = null;

/**
 * The built-in category/action surface, read from the live tool registry.
 *
 * Imported lazily: tools.ts imports feedback.ts which imports this module, so
 * a static import would close a cycle at module-init time. By the time any
 * routing runs, tools.ts is long since evaluated and this resolves instantly.
 */
async function coreSurface(): Promise<CoreSurface> {
  if (coreSurfaceCache) return coreSurfaceCache;
  const categories = new Set<string>();
  const actions = new Map<string, string>();
  try {
    const { getLiveToolGraph } = await import("./tools.js");
    for (const tool of getLiveToolGraph()) {
      categories.add(tool.name.toLowerCase());
      for (const action of Object.keys(tool.actions)) {
        const a = action.toLowerCase();
        // Underscored names only. Bare verbs ("list", "add", "search") appear
        // in ordinary prose and would anchor everything to core.
        if (a.includes("_") && a.length >= 6) actions.set(a, tool.name);
      }
    }
  } catch (e) {
    debug("feedback-routing", "could not read the built-in tool surface", e);
  }
  coreSurfaceCache = { categories, actions };
  return coreSurfaceCache;
}

/** Test seam: forget the memoised built-in surface. */
export function clearCoreSurfaceCache(): void {
  coreSurfaceCache = null;
}

/* ── helpers ───────────────────────────────────────────────────────── */

function escapeRe(s: string): string {
  return s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

function wordRe(token: string): RegExp {
  return new RegExp(`(^|[^\\w])${escapeRe(token)}($|[^\\w])`, "i");
}

/**
 * `pie(replay)` or `pie.replay` - a tool call, not a mention.
 *
 * Both forms are deliberately strict. The dotted form needs a following
 * identifier, so a sentence ending "...the default grey material." stays
 * prose. The paren form allows no space, so "import (the FBX path)" stays
 * prose too. What survives is what an agent actually types when it names a
 * tool call.
 */
function callSyntaxRe(token: string): RegExp {
  return new RegExp(`(^|[^\\w])${escapeRe(token)}(\\(|\\.[A-Za-z_])`, "i");
}

/** `blueprint(action=foo)` / `blueprint.foo` -> "blueprint". */
export function parseToolCategory(idealTool: string | undefined): string | null {
  if (!idealTool) return null;
  const m = idealTool.trim().match(/^([A-Za-z_]\w*)\s*[(.]/);
  return m ? m[1].toLowerCase() : null;
}

/** `blueprint(action=set_x)` / `blueprint.set_x` -> "set_x". */
function parseToolAction(idealTool: string | undefined): string | null {
  if (!idealTool) return null;
  const paren = idealTool.match(/\(\s*(?:action\s*=\s*)?["']?([A-Za-z_]\w*)/);
  if (paren) return paren[1].toLowerCase();
  const dotted = idealTool.match(/^[A-Za-z_]\w*\.([A-Za-z_]\w*)/);
  return dotted ? dotted[1].toLowerCase() : null;
}

function repoUrlFromPkgDir(pkgDir: string): string | undefined {
  try {
    const raw = fs.readFileSync(path.join(pkgDir, "package.json"), "utf-8");
    const pkg = JSON.parse(raw) as { repository?: unknown; homepage?: string };
    const r = pkg.repository;
    const url = typeof r === "string" ? r : (r as { url?: string } | undefined)?.url;
    return url ?? pkg.homepage;
  } catch {
    return undefined;
  }
}

/** Registry slug for an installed plugin's npm package name. */
function slugForPackage(pkgName: string): string {
  return pkgName.replace(/^@[^/]+\//, "").replace(/^ue-mcp-/, "");
}

/* ── candidate construction ────────────────────────────────────────── */

interface WorkingCandidate extends RouteCandidate {
  /** Identity terms. A hit here can make this plugin the destination. */
  strong: Set<string>;
  /** Descriptive tags. A hit here can only raise a suggestion. */
  weak: Set<string>;
  /** True once a strong term matched somewhere that matters. */
  strongSignal: boolean;
  phrases: string[];
  /** Categories this plugin provides, when it is installed locally. */
  ownedCategories: Set<string>;
  /** Action names this plugin injects into built-in categories. */
  ownedActions: Set<string>;
  actionPrefix?: string;
}

function tokensFromRow(row: RegistryPlugin, core: CoreSurface): { strong: Set<string>; weak: Set<string> } {
  const collect = (values: (string | undefined)[]): string[] => {
    const raw: string[] = [];
    for (const s of values) {
      if (!s) continue;
      const lower = s.toLowerCase();
      raw.push(lower);
      for (const part of lower.split(/[^a-z0-9]+/)) raw.push(part);
    }
    return raw.filter((t) => {
      if (t.length < 2) return false;
      if (GENERIC_TOKENS.has(t)) return false;
      // A plugin tagged "asset" must never capture asset issues: built-in
      // category names belong to core, whatever a listing calls itself.
      if (core.categories.has(t)) return false;
      return true;
    });
  };

  const strong = new Set(collect([row.slug, row.name, row.packageName ? slugForPackage(row.packageName) : undefined]));
  const weak = new Set(collect(row.tags ?? []));
  for (const t of strong) weak.delete(t);
  return { strong, weak };
}

function phrasesFromRow(row: RegistryPlugin): string[] {
  const out: string[] = [];
  for (const s of [row.slug, row.name]) {
    if (!s) continue;
    const parts = s.toLowerCase().split(/[^a-z0-9]+/).filter(Boolean);
    if (parts.length >= 2) out.push(parts.map(escapeRe).join("[\\s_-]*"));
  }
  return out;
}

function buildCandidates(
  catalog: RegistryPlugin[],
  installed: PluginInfo[],
  core: CoreSurface,
): WorkingCandidate[] {
  const byKey = new Map<string, WorkingCandidate>();

  for (const row of catalog) {
    const repoUrl = row.repoUrl;
    const { strong, weak } = tokensFromRow(row, core);
    byKey.set(row.slug.toLowerCase(), {
      slug: row.slug,
      name: row.name || row.slug,
      packageName: row.packageName,
      repo: parseGitHubRepo(repoUrl),
      repoUrl,
      repoPrivate: row.repoPrivate === true,
      installed: false,
      score: 0,
      confidence: "possible",
      reasons: [],
      strong,
      weak,
      strongSignal: false,
      phrases: phrasesFromRow(row),
      ownedCategories: new Set(),
      ownedActions: new Set(),
    });
  }

  // Merge in what is actually loaded here. A locally loaded plugin that never
  // published to the registry still deserves its own tracker, so unknown
  // packages become candidates in their own right.
  for (const p of installed) {
    if (p.status !== "active") continue;
    const slug = slugForPackage(p.name);
    const key = [...byKey.keys()].find(
      (k) => k === slug.toLowerCase() || byKey.get(k)!.packageName === p.name,
    );
    let cand = key ? byKey.get(key)! : undefined;
    if (!cand) {
      const repoUrl = repoUrlFromPkgDir(p.pkgDir);
      const { strong, weak } = tokensFromRow({ slug, name: p.name, packageName: p.name }, core);
      cand = {
        slug,
        name: p.name,
        packageName: p.name,
        repo: parseGitHubRepo(repoUrl),
        repoUrl,
        repoPrivate: false,
        installed: true,
        score: 0,
        confidence: "possible",
        reasons: [],
        strong,
        weak,
        strongSignal: false,
        phrases: phrasesFromRow({ slug, name: p.name }),
        ownedCategories: new Set(),
        ownedActions: new Set(),
      };
      byKey.set(slug.toLowerCase(), cand);
    }
    cand.installed = true;
    if (!cand.repo) {
      const repoUrl = cand.repoUrl ?? repoUrlFromPkgDir(p.pkgDir);
      cand.repoUrl = repoUrl;
      cand.repo = parseGitHubRepo(repoUrl);
    }
    cand.actionPrefix = p.actionPrefix;
    for (const cat of Object.keys(p.provided)) {
      cand.ownedCategories.add(cat.toLowerCase());
      // A category this plugin serves is as much its identity as its name.
      if (!core.categories.has(cat.toLowerCase())) cand.strong.add(cat.toLowerCase());
    }
    for (const [cat, actions] of Object.entries(p.injected)) {
      for (const a of actions) cand.ownedActions.add(`${cat.toLowerCase()}.${a.toLowerCase()}`);
    }
    for (const [cat, actions] of Object.entries(p.provided)) {
      for (const a of actions) cand.ownedActions.add(`${cat.toLowerCase()}.${a.toLowerCase()}`);
    }
  }

  return [...byKey.values()];
}

/* ── the decision ──────────────────────────────────────────────────── */

function detectCoreAnchor(text: string, idealTool: string | undefined, core: CoreSurface): string | null {
  const idealCat = parseToolCategory(idealTool);
  if (idealCat && core.categories.has(idealCat)) return `${idealCat}(...)`;

  // Action names first: `editor(play_in_editor)` is a more useful thing to
  // report back than the bare category it lives in.
  for (const [action, cat] of core.actions) {
    if (wordRe(action).test(text)) return `${cat}(${action})`;
  }
  for (const cat of core.categories) {
    if (callSyntaxRe(cat).test(text)) return `${cat}(...)`;
  }
  return null;
}

function toPublic(c: WorkingCandidate): RouteCandidate {
  return {
    slug: c.slug,
    name: c.name,
    packageName: c.packageName,
    repo: c.repo,
    repoUrl: c.repoUrl,
    repoPrivate: c.repoPrivate,
    installed: c.installed,
    score: c.score,
    confidence: c.confidence,
    reasons: c.reasons,
  };
}

function coreDecision(
  extra: Partial<RoutingDecision> = {},
): RoutingDecision {
  return {
    target: "core",
    repo: CORE_REPO,
    candidate: null,
    suggestions: [],
    coreAnchor: null,
    catalogAvailable: true,
    ...extra,
  };
}

export async function routeFeedback(input: RoutingInput): Promise<RoutingDecision> {
  const core = await coreSurface();
  const catalog = input.catalog ?? (await fetchRegistryCatalog({ timeoutMs: input.timeoutMs }));
  const catalogAvailable = catalog.length > 0;
  const installed = input.installed ?? [];

  const candidates = buildCandidates(catalog, installed, core);

  /* 1. explicit override ------------------------------------------- */
  if (input.explicitRepo) {
    const wanted = parseRepoSlug(input.explicitRepo) ?? parseGitHubRepo(input.explicitRepo);
    if (!wanted) {
      return coreDecision({
        catalogAvailable,
        note: `Ignored repo="${input.explicitRepo}": not an owner/name reference. Filing against ue-mcp core.`,
      });
    }
    if (sameRepo(wanted, CORE_REPO)) {
      return coreDecision({ catalogAvailable, note: "Caller pinned the report to ue-mcp core." });
    }
    const match = candidates.find((c) => sameRepo(c.repo, wanted));
    if (!match) {
      // Deliberate refusal. Honoring an arbitrary repo would turn feedback
      // into "post an issue anywhere as the signed-in user".
      return coreDecision({
        catalogAvailable,
        note: `Ignored repo="${input.explicitRepo}": no registered ue-mcp plugin owns that repo. Filing against ue-mcp core.`,
      });
    }
    match.score = 100;
    match.confidence = "certain";
    match.reasons = [`caller pinned the report to ${repoSlug(wanted)}`];
    return {
      target: "plugin",
      repo: wanted,
      candidate: toPublic(match),
      suggestions: [],
      coreAnchor: null,
      catalogAvailable,
    };
  }

  const title = (input.title ?? "").trim();
  const summary = (input.summary ?? "").trim();
  const idealTool = (input.idealTool ?? "").trim();
  const text = `${title}\n${summary}\n${idealTool}`;
  if (!text.replace(/\s/g, "")) return coreDecision({ catalogAvailable });

  const idealCat = parseToolCategory(idealTool);
  const idealAction = parseToolAction(idealTool);

  /* 2. ownership - a fact, not a guess ------------------------------ */
  for (const c of candidates) {
    if (idealCat && c.ownedCategories.has(idealCat)) {
      c.score = 100;
      c.confidence = "certain";
      c.reasons.push(`${c.name} provides the '${idealCat}' category in this project`);
    } else if (idealCat && idealAction && c.ownedActions.has(`${idealCat}.${idealAction}`)) {
      c.score = 100;
      c.confidence = "certain";
      c.reasons.push(`${c.name} owns ${idealCat}(${idealAction}) in this project`);
    } else if (c.actionPrefix && idealAction && idealAction.startsWith(`${c.actionPrefix.toLowerCase()}_`)) {
      c.score = 100;
      c.confidence = "certain";
      c.reasons.push(`'${idealAction}' carries ${c.name}'s '${c.actionPrefix}_' action prefix`);
    }
  }

  /* 3. keyword scoring ---------------------------------------------- */
  for (const c of candidates) {
    if (c.score >= 100) continue;

    const scoreToken = (token: string, isStrong: boolean): void => {
      // A call aimed at a category core does not have is the loudest signal
      // short of outright ownership.
      if (!core.categories.has(token) && callSyntaxRe(token).test(text)) {
        // A call to a category ue-mcp core does not have is structural, not
        // vocabulary: `p4(submit)` is a Perforce call whether "p4" arrived as
        // the plugin's name or as one of its tags.
        c.score += isStrong ? W_STRONG_CALL : W_WEAK_CALL;
        c.strongSignal = true;
        c.reasons.push(`the report calls '${token}(...)', which ue-mcp core does not provide`);
        return;
      }
      let hit = false;
      if (idealTool && wordRe(token).test(idealTool)) {
        c.score += isStrong ? W_STRONG_IDEAL : W_WEAK_IDEAL;
        if (isStrong) c.strongSignal = true;
        c.reasons.push(`idealTool names '${token}'`);
        hit = true;
      }
      if (wordRe(token).test(title)) {
        c.score += isStrong ? W_STRONG_TITLE : W_WEAK_TITLE;
        if (isStrong) c.strongSignal = true;
        if (!hit) c.reasons.push(`title names '${token}'`);
        hit = true;
      }
      if (wordRe(token).test(summary)) {
        c.score += isStrong ? W_STRONG_SUMMARY : W_WEAK_SUMMARY;
        if (!hit) c.reasons.push(`summary names '${token}'`);
      }
    };

    for (const token of c.strong) scoreToken(token, true);
    for (const token of c.weak) scoreToken(token, false);

    for (const phrase of c.phrases) {
      const re = new RegExp(`(^|[^\\w])${phrase}($|[^\\w])`, "i");
      if (re.test(title)) {
        c.score += W_PHRASE_TITLE;
        c.strongSignal = true;
        c.reasons.push(`title names "${c.name}"`);
      } else if (re.test(summary)) {
        c.score += W_PHRASE_SUMMARY;
        c.strongSignal = true;
        c.reasons.push(`summary names "${c.name}"`);
      }
    }
    if (c.score > 0 && c.installed) {
      c.score += W_INSTALLED;
      c.reasons.push("installed in this project");
    }
    c.confidence = c.strongSignal || c.score >= LIKELY_AT ? "likely" : "possible";
  }

  const scored = candidates
    .filter((c) => c.score >= POSSIBLE_AT)
    .sort((a, b) => b.score - a.score);

  const coreAnchor = detectCoreAnchor(text, idealTool, core);

  if (scored.length === 0) {
    return coreDecision({ catalogAvailable, coreAnchor });
  }

  const winner = scored[0];
  const rest = scored.slice(1).map(toPublic);

  // A named built-in action outranks any keyword. "editor(play_in_editor)
  // never returns" mentions PIE, but core owns play_in_editor.
  if (coreAnchor && winner.confidence !== "certain") {
    return coreDecision({
      catalogAvailable,
      coreAnchor,
      suggestions: scored.map(toPublic),
      note: `The report names ${coreAnchor}, which ue-mcp core owns, so it stays on the core tracker even though ${winner.name} also matched.`,
    });
  }

  // Tags alone do not move a report. "Importing a mesh loses its materials"
  // matches Meshy's "import" tag and is still a core asset bug; the user sees
  // the match at the prompt and can send it to Meshy in one click.
  if (winner.confidence !== "certain" && !winner.strongSignal) {
    return coreDecision({
      catalogAvailable,
      coreAnchor,
      suggestions: scored.map(toPublic),
      note: `${winner.name} matched only on descriptive tags, which is too weak to re-file on, so this stays on the core tracker.`,
    });
  }

  // Registered but unreachable: no repo, or a private one. Say so instead of
  // silently pretending core is the right home.
  if (!winner.repo || winner.repoPrivate) {
    return coreDecision({
      catalogAvailable,
      coreAnchor,
      suggestions: scored.map(toPublic),
      note: `${winner.name} looks like the owner but has no public issue tracker in the registry, so this files against ue-mcp core.`,
    });
  }

  return {
    target: "plugin",
    repo: winner.repo,
    candidate: toPublic(winner),
    suggestions: rest,
    coreAnchor,
    catalogAvailable,
  };
}
