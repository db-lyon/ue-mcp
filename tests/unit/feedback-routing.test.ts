import { describe, it, expect, beforeEach } from "vitest";
import { routeFeedback, clearCoreSurfaceCache } from "../../src/feedback/feedback-routing.js";
import { parseGitHubRepo, parseRepoSlug, repoSlug, newIssueUrl } from "../../src/extensions/registry-catalog.js";
import type { RegistryPlugin } from "../../src/extensions/registry-catalog.js";
import type { PluginInfo } from "../../src/core/types.js";
import { ALL_TOOLS } from "../../src/tools.js";

/**
 * A trimmed copy of the live plugins.ue-mcp.com catalog. Injected, so these
 * tests never touch the network and never drift when the real registry gains
 * a listing.
 */
const CATALOG: RegistryPlugin[] = [
  {
    slug: "pie-studio",
    name: "PIE Studio",
    packageName: "pie-studio",
    repoUrl: "https://github.com/db-lyon/pie-studio",
    repoPrivate: false,
    tagline: "Record, replay, observe, and inject input in Play-In-Editor sessions.",
    category: "editor",
    tags: ["testing", "replay", "input-injection"],
    status: "published",
  },
  {
    slug: "perforce",
    name: "Perforce",
    packageName: "ue-mcp-perforce",
    repoUrl: "https://github.com/db-lyon/ue-mcp-perforce",
    repoPrivate: false,
    tagline: "A safe, reviewable p4 surface for agents on a shared Unreal depot.",
    category: "editor",
    tags: ["source-control", "perforce", "p4"],
    status: "published",
  },
  {
    slug: "meshy",
    name: "Meshy",
    packageName: "ue-mcp-meshy",
    repoUrl: "https://github.com/db-lyon/ue-mcp-meshy",
    repoPrivate: false,
    tagline: "Turn a prompt or image into a game-ready UE5 asset in one call.",
    category: "asset",
    tags: ["asset-generation", "import", "nanite"],
    status: "published",
  },
];

function installedPieStudio(overrides: Partial<PluginInfo> = {}): PluginInfo {
  return {
    name: "pie-studio",
    version: "1.0.0",
    actionPrefix: "pie",
    status: "active",
    degraded: [],
    injected: {},
    provided: { pie: ["replay", "record", "observe", "inject_input"] },
    knowledge: {},
    flows: [],
    tasks: [],
    pkgDir: "/does/not/exist",
    manifestPath: "/does/not/exist/ue-mcp.plugin.yml",
    ...overrides,
  };
}

describe("feedback routing", () => {
  beforeEach(() => {
    clearCoreSurfaceCache();
  });

  it("routes a report about a plugin-provided category to that plugin's repo", async () => {
    const d = await routeFeedback({
      title: "pie(replay) diverges from the recorded run after 200 frames",
      summary:
        "Replaying a recorded PIE session drifts from the capture: the pawn ends up several metres from where it was recorded, and the observation profile reports no divergence.",
      idealTool: "pie(action=replay)",
      catalog: CATALOG,
      tools: ALL_TOOLS,
      installed: [installedPieStudio()],
    });

    expect(d.target).toBe("plugin");
    expect(repoSlug(d.repo)).toBe("db-lyon/pie-studio");
    expect(d.candidate?.confidence).toBe("certain");
    expect(d.candidate?.reasons.join(" ")).toContain("provides the 'pie' category");
  });

  it("routes on registry keywords alone when the plugin is not installed", async () => {
    const d = await routeFeedback({
      title: "pie(inject_input) drops the first frame of a tape",
      summary:
        "Injecting a recorded input tape loses the very first frame, so a jump queued on frame 0 never fires during the replayed session.",
      catalog: CATALOG,
      tools: ALL_TOOLS,
      installed: [],
    });

    expect(d.target).toBe("plugin");
    expect(repoSlug(d.repo)).toBe("db-lyon/pie-studio");
    expect(d.candidate?.installed).toBe(false);
  });

  it("keeps a report on core when it names a built-in action, even if a plugin also matches", async () => {
    const d = await routeFeedback({
      title: "editor(play_in_editor) never returns on a cold editor start",
      summary:
        "Starting PIE through editor(play_in_editor) hangs until the asset registry scan finishes, and the call times out before that happens.",
      catalog: CATALOG,
      tools: ALL_TOOLS,
      installed: [installedPieStudio()],
    });

    expect(d.target).toBe("core");
    expect(repoSlug(d.repo)).toBe("db-lyon/ue-mcp");
    expect(d.coreAnchor).toContain("play_in_editor");
    // The match is still reported so the user can override it at the prompt.
    expect(d.suggestions.some((s) => s.slug === "pie-studio")).toBe(true);
    expect(d.note).toBeTruthy();
  });

  it("keeps an ordinary core report on core with no suggestions", async () => {
    const d = await routeFeedback({
      title: "blueprint(set_class_default) does not save the asset",
      summary:
        "Setting a class default marks the blueprint dirty but never saves it, so the change is lost unless a separate save call runs afterwards.",
      idealTool: "blueprint(action=set_class_default)",
      catalog: CATALOG,
      tools: ALL_TOOLS,
      installed: [installedPieStudio()],
    });

    expect(d.target).toBe("core");
    expect(d.candidate).toBeNull();
    expect(d.suggestions).toHaveLength(0);
  });

  it("does not route on a built-in category name that a plugin happens to be tagged with", async () => {
    // Meshy is tagged "import" and categorised "asset". An asset import bug is
    // a core bug.
    const d = await routeFeedback({
      title: "asset(import) fails silently for FBX files with embedded textures",
      summary:
        "Importing an FBX that carries embedded textures reports success but no texture assets appear on disk, and the material slots stay unassigned.",
      catalog: CATALOG,
      tools: ALL_TOOLS,
      installed: [],
    });

    expect(d.target).toBe("core");
  });

  it("does not re-file a report that only matches a descriptive tag", async () => {
    // Meshy is tagged "import". An import bug is not a Meshy bug.
    const d = await routeFeedback({
      title: "Import of an FBX loses its material slots",
      summary:
        "The import completes and the mesh appears, but every material slot comes back empty so the asset renders with the default grey material.",
      catalog: CATALOG,
      tools: ALL_TOOLS,
      installed: [],
    });

    expect(d.target).toBe("core");
    expect(d.suggestions[0]?.slug).toBe("meshy");
    expect(d.note).toContain("descriptive tags");
  });

  it("routes on the plugin's own name in the title even without call syntax", async () => {
    const d = await routeFeedback({
      title: "PIE record button does not arm the input recorder",
      summary:
        "Clicking record in the toolbar leaves the recorder disarmed, so starting play captures nothing at all and the frame count stays at zero.",
      catalog: CATALOG,
      tools: ALL_TOOLS,
      installed: [],
    });

    expect(d.target).toBe("plugin");
    expect(repoSlug(d.repo)).toBe("db-lyon/pie-studio");
  });

  it("routes on a call to a category core does not have, even from a tag term", async () => {
    // "p4" reaches the registry as a tag, not as Perforce's name. A call to
    // p4(...) is still structurally a Perforce call.
    const d = await routeFeedback({
      title: "p4(submit) leaves the changelist open after a successful submit",
      summary:
        "The submit reports success and the files land in the depot, but the changelist stays pending locally so the next submit picks up the same files again.",
      catalog: CATALOG,
      tools: ALL_TOOLS,
      installed: [],
    });

    expect(d.target).toBe("plugin");
    expect(repoSlug(d.repo)).toBe("db-lyon/ue-mcp-perforce");
  });

  it("does not read an ordinary parenthetical as a tool call", async () => {
    const d = await routeFeedback({
      title: "Level import (the FBX path) is ignored on Windows",
      summary:
        "Passing an import (the absolute path) does nothing at all when the level loads, and no warning is written to the log either.",
      catalog: CATALOG,
      tools: ALL_TOOLS,
      installed: [],
    });

    expect(d.target).toBe("core");
  });

  it("matches a two-token plugin name spelled out in prose", async () => {
    const d = await routeFeedback({
      title: "PIE Studio contact sheet is blank when capture_frame_every is 1",
      summary:
        "Every captured frame comes out fully black, so the generated contact sheet is unusable for reading what happened during the run.",
      catalog: CATALOG,
      tools: ALL_TOOLS,
      installed: [],
    });

    expect(d.target).toBe("plugin");
    expect(repoSlug(d.repo)).toBe("db-lyon/pie-studio");
  });

  it("honours an explicit repo that a registered plugin owns", async () => {
    const d = await routeFeedback({
      title: "blueprint(set_class_default) does not save the asset",
      summary: "Setting a class default marks the blueprint dirty but never saves it to disk.",
      explicitRepo: "db-lyon/ue-mcp-perforce",
      catalog: CATALOG,
      tools: ALL_TOOLS,
    });

    expect(d.target).toBe("plugin");
    expect(repoSlug(d.repo)).toBe("db-lyon/ue-mcp-perforce");
    expect(d.candidate?.confidence).toBe("certain");
  });

  it("refuses an explicit repo no registered plugin owns", async () => {
    const d = await routeFeedback({
      title: "pie(replay) diverges from the recorded run",
      summary: "Replaying a recorded PIE session drifts badly from the original capture.",
      explicitRepo: "someone-else/private-thing",
      catalog: CATALOG,
      tools: ALL_TOOLS,
      installed: [installedPieStudio()],
    });

    expect(d.target).toBe("core");
    expect(repoSlug(d.repo)).toBe("db-lyon/ue-mcp");
    expect(d.note).toContain("no registered ue-mcp plugin owns that repo");
  });

  it("falls back to core when the matched plugin has no public tracker", async () => {
    const d = await routeFeedback({
      title: "pie(replay) diverges from the recorded run after 200 frames",
      summary: "Replaying a recorded PIE session drifts from the capture by several metres.",
      idealTool: "pie(action=replay)",
      catalog: [{ ...CATALOG[0], repoUrl: undefined }],
      installed: [installedPieStudio()],
    });

    expect(d.target).toBe("core");
    expect(d.note).toContain("no public issue tracker");
    expect(d.suggestions[0]?.slug).toBe("pie-studio");
  });

  it("routes to a locally loaded plugin the registry has never seen", async () => {
    const d = await routeFeedback({
      title: "voxeltools(carve_sphere) leaves a hole in the collision mesh",
      summary:
        "Carving a sphere updates the render mesh but the collision geometry keeps the old surface, so the player walks on air over the hole.",
      idealTool: "voxeltools(action=carve_sphere)",
      catalog: [],
      installed: [
        installedPieStudio({
          name: "ue-mcp-voxeltools",
          actionPrefix: "voxeltools",
          provided: { voxeltools: ["carve_sphere"] },
          // Repo resolution reads package.json from pkgDir; there is none here,
          // so the candidate has no repo and the report stays on core.
        }),
      ],
    });

    expect(d.target).toBe("core");
    expect(d.suggestions[0]?.name).toBe("ue-mcp-voxeltools");
  });

  it("degrades to core when the registry is unreachable and nothing is installed", async () => {
    const d = await routeFeedback({
      title: "pie(replay) diverges from the recorded run after 200 frames",
      summary: "Replaying a recorded PIE session drifts from the capture by several metres.",
      catalog: [],
      installed: [],
    });

    expect(d.target).toBe("core");
    expect(d.catalogAvailable).toBe(false);
  });

  it("returns core for an empty report instead of throwing", async () => {
    const d = await routeFeedback({ title: "", summary: "", catalog: CATALOG });
    expect(d.target).toBe("core");
  });
});

describe("registry-catalog helpers", () => {
  it("parses github repo URLs in every shape the registry stores them", () => {
    expect(parseGitHubRepo("https://github.com/db-lyon/pie-studio")).toEqual({ owner: "db-lyon", repo: "pie-studio" });
    expect(parseGitHubRepo("git+https://github.com/db-lyon/pie-studio.git")).toEqual({ owner: "db-lyon", repo: "pie-studio" });
    expect(parseGitHubRepo("git@github.com:db-lyon/pie-studio.git")).toEqual({ owner: "db-lyon", repo: "pie-studio" });
    expect(parseGitHubRepo("https://gitlab.com/x/y")).toBeNull();
    expect(parseGitHubRepo(undefined)).toBeNull();
  });

  it("parses owner/name slugs and rejects anything else", () => {
    expect(parseRepoSlug("db-lyon/ue-mcp")).toEqual({ owner: "db-lyon", repo: "ue-mcp" });
    expect(parseRepoSlug("ue-mcp")).toBeNull();
    expect(parseRepoSlug("a/b/c")).toBeNull();
  });

  it("builds a prefilled new-issue URL and caps the body length", () => {
    const url = newIssueUrl({ owner: "db-lyon", repo: "pie-studio" }, "title here", "x".repeat(9000));
    expect(url.startsWith("https://github.com/db-lyon/pie-studio/issues/new?")).toBe(true);
    expect(url).toContain("title=title%20here");
    expect(url.length).toBeLessThan(6500);
  });
});
