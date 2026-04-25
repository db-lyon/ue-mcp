import { describe, it, expect } from "vitest";
import {
  ITEM_RE,
  MAX_ITEMS,
  MAX_ITEM_LEN,
  MAX_JOINED_LEN,
  ValidationError,
  parseHeadlineArray,
  processBody,
  splitFrontmatter,
  validate,
} from "../../scripts/release-headline.mjs";

const wrap = (front: string, body = "## v1.2.3\n\nbody\n") => `---\n${front}\n---\n\n${body}`;

describe("release-headline parser", () => {
  it("rejects bodies without frontmatter", () => {
    expect(() => splitFrontmatter("## v1.2.3\nbody")).toThrowError(ValidationError);
  });

  it("parses block-form headline arrays", () => {
    const items = parseHeadlineArray("headline:\n  - First feature\n  - Second feature");
    expect(items).toEqual(["First feature", "Second feature"]);
  });

  it("parses inline-form headline arrays", () => {
    const items = parseHeadlineArray("headline: [First feature, Second feature]");
    expect(items).toEqual(["First feature", "Second feature"]);
  });

  it("strips wrapping quotes from items", () => {
    const items = parseHeadlineArray('headline:\n  - "Quoted item"\n  - \'Single quoted\'');
    expect(items).toEqual(["Quoted item", "Single quoted"]);
  });

  it("rejects frontmatter that does not lead with headline:", () => {
    expect(() => parseHeadlineArray("other: value")).toThrowError(/headline:/);
  });
});

describe("release-headline validation", () => {
  it("accepts every historical good headline", () => {
    const cases: string[][] = [
      ["10 new actions", "Flow over HTTP", "Blueprint reparent"],
      ["Headless PNG capture", "PCG unstick actions", "JSON values in component_property"],
      ["Relicensed to BUSL-1.1"],
      [
        "PCG persistence fix",
        "animation authoring (bake root motion, batch seq props)",
        "SKM material slots",
        "registry diagnose",
      ],
      ["agent_prompt and Anthropic SDK removed", "flows stay deterministic", "reasoning stays in the parent agent"],
      ["Runtime UMG inspection", "Image brush authoring", "IMC rebind/remove"],
      [
        "CDO property access",
        "actor bounds",
        "mesh collision/nav",
        "construction script preview",
        "property serializer overhaul",
        "IMC crash fix",
      ],
    ];
    for (const items of cases) {
      expect(() => validate(items)).not.toThrow();
    }
  });

  it("rejects sentence-shaped items (colon)", () => {
    expect(() => validate(["Update notifier: nudges users"])).toThrowError(
      /forbidden characters/,
    );
  });

  it("rejects items with embedded middle-dots", () => {
    expect(() => validate(["foo · bar"])).toThrowError(/forbidden characters/);
  });

  it("rejects question marks and exclamations", () => {
    expect(() => validate(["Did this ship?"])).toThrowError(/forbidden characters/);
    expect(() => validate(["Shipped!"])).toThrowError(/forbidden characters/);
  });

  it("rejects empty arrays", () => {
    expect(() => validate([])).toThrowError(/at least 1/);
  });

  it("rejects more than the max number of items", () => {
    const items = Array.from({ length: MAX_ITEMS + 1 }, (_, i) => `item ${i + 1}`);
    expect(() => validate(items)).toThrowError(/max is/);
  });

  it("rejects items shorter than the minimum length", () => {
    expect(() => validate(["ab"])).toThrowError(/3-60/);
  });

  it("rejects items longer than the maximum length", () => {
    expect(() => validate(["a".repeat(MAX_ITEM_LEN + 1)])).toThrowError(/3-60/);
  });

  it("rejects joined strings over the GitHub status cap", () => {
    const items = Array.from({ length: 6 }, () => "a".repeat(50));
    expect(() => validate(items)).toThrowError(new RegExp(String(MAX_JOINED_LEN)));
  });

  it("returns the joined string with the U+00B7 separator", () => {
    expect(validate(["one", "two", "three"])).toBe("one · two · three");
  });
});

describe("release-headline ITEM_RE", () => {
  it("allows commas, slashes, parens, hyphens, periods, plus, underscore", () => {
    expect(ITEM_RE.test("animation authoring (bake root motion, batch seq props)")).toBe(true);
    expect(ITEM_RE.test("IMC rebind/remove")).toBe(true);
    expect(ITEM_RE.test("BUSL-1.1")).toBe(true);
    expect(ITEM_RE.test("a+b")).toBe(true);
    expect(ITEM_RE.test("snake_case_token")).toBe(true);
  });

  it("rejects sentence punctuation and the joiner", () => {
    expect(ITEM_RE.test("foo: bar")).toBe(false);
    expect(ITEM_RE.test("foo; bar")).toBe(false);
    expect(ITEM_RE.test("foo? bar")).toBe(false);
    expect(ITEM_RE.test("foo! bar")).toBe(false);
    expect(ITEM_RE.test("foo · bar")).toBe(false);
  });
});

describe("release-headline processBody", () => {
  it("returns headline and stripped body for a valid release notes file", () => {
    const body = wrap("headline:\n  - First feature\n  - Second feature");
    const out = processBody(body);
    expect(out.headline).toBe("First feature · Second feature");
    expect(out.strippedBody.startsWith("## v1.2.3")).toBe(true);
  });

  it("rejects a body that fails validation, propagating the error", () => {
    const body = wrap("headline:\n  - First: feature");
    expect(() => processBody(body)).toThrowError(ValidationError);
  });
});
