import { describe, it, expect } from "vitest";
import { privacyScrub } from "../../src/privacy-scrub.js";

const ctx = {
  projectRoot: "C:/Users/david/Projects/UE/Vale",
  projectName: "Vale",
  username: "david",
  homeDir: "C:/Users/david",
};

describe("privacyScrub", () => {
  it("redacts the absolute project root in both slash and backslash forms", () => {
    const input =
      "open C:/Users/david/Projects/UE/Vale/Content/Hero.uasset and also C:\\Users\\david\\Projects\\UE\\Vale\\Plugins";
    const { text, hits } = privacyScrub(input, ctx);
    expect(text).not.toContain("C:/Users/david/Projects/UE/Vale");
    expect(text).not.toContain("C:\\Users\\david\\Projects\\UE\\Vale");
    expect(text).toContain("REDACTED_PROJECT_ROOT/Content/Hero.uasset");
    expect(text).toContain("REDACTED_PROJECT_ROOT\\Plugins");
    expect(hits.find((h) => h.rule === "project-root-path")?.count).toBe(2);
  });

  it("redacts the home directory when paths land outside the project", () => {
    const input = "wrote token to C:/Users/david/.ue-mcp/auth.json";
    const { text } = privacyScrub(input, ctx);
    expect(text).toContain("REDACTED_HOME/.ue-mcp/auth.json");
    expect(text).not.toContain("david");
  });

  it("redacts the project name as a whole word, case-insensitive", () => {
    const input =
      "Working on the Vale rendering pipeline; vale also has a custom AssetManager.";
    const { text, hits } = privacyScrub(input, ctx);
    expect(text).toContain("REDACTED_PROJECT rendering pipeline");
    expect(text).toContain("REDACTED_PROJECT also has");
    expect(hits.find((h) => h.rule === "project-name")?.count).toBe(2);
  });

  it("does NOT redact a substring that contains the project name", () => {
    // "Valedictory" must not become "REDACTED_PROJECTdictory".
    const input = "Valedictory speech mentions valley";
    const { text } = privacyScrub(input, ctx);
    expect(text).toContain("Valedictory");
    expect(text).toContain("valley");
  });

  it("redacts the OS username as a whole word, case-insensitive", () => {
    const input =
      "trace from David Bingham's stack: see file owned by david@host";
    const { text, hits } = privacyScrub(input, ctx);
    expect(text).toContain("REDACTED_USER Bingham");
    expect(text).toContain("REDACTED_USER@host");
    expect(hits.find((h) => h.rule === "username")?.count).toBe(2);
  });

  it("does NOT redact a substring that contains the username", () => {
    const input = "Davidson and Davidic are not the user.";
    const { text } = privacyScrub(input, ctx);
    expect(text).toContain("Davidson");
    expect(text).toContain("Davidic");
  });

  it("skips redaction when a name is shorter than MIN_WORD_LEN", () => {
    // Project named "AI" would otherwise nuke every "ai" in the body.
    const { text } = privacyScrub("the ai system is fine", {
      ...ctx,
      projectName: "AI",
    });
    expect(text).toContain("ai system is fine");
  });

  it("preserves relative path structure under the redacted project root", () => {
    const input =
      "stack: at C:/Users/david/Projects/UE/Vale/Plugins/Voxel/Source/Foo.cpp:42";
    const { text } = privacyScrub(input, ctx);
    expect(text).toContain("REDACTED_PROJECT_ROOT/Plugins/Voxel/Source/Foo.cpp:42");
  });

  it("composes: a body mentioning paths, project, and username gets all four rules", () => {
    const input = [
      "Title references Vale.",
      "Path: C:/Users/david/Projects/UE/Vale/Content/X.uasset",
      "Home cache at C:/Users/david/.cache/foo",
      "User david ran the command.",
    ].join("\n");
    const { text, hits } = privacyScrub(input, ctx);
    expect(text).not.toMatch(/david/i);
    expect(text).not.toContain("Vale");
    expect(text).not.toContain("C:/Users/david");
    const ruleNames = new Set(hits.map((h) => h.rule));
    expect(ruleNames.has("project-root-path")).toBe(true);
    expect(ruleNames.has("home-path")).toBe(true);
    expect(ruleNames.has("project-name")).toBe(true);
    expect(ruleNames.has("username")).toBe(true);
  });

  it("is a no-op when no context fields are supplied (defaults to host os, no matches)", () => {
    // Without an explicit ctx the function reads os.homedir/userInfo,
    // which on a clean test runner won't appear in this input.
    const input = "no identifiable strings here at all.";
    const { text, hits } = privacyScrub(input, {
      projectRoot: undefined,
      projectName: undefined,
      username: "__nobody_user_match__",
      homeDir: "/__nobody_home_match__",
    });
    expect(text).toBe(input);
    expect(hits).toEqual([]);
  });
});
