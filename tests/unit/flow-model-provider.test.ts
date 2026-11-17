import { describe, expect, it } from "vitest";
import { NO_MODEL_MESSAGE, NO_MODEL_WAY_FORWARD, registerModelProvider, resolveModelProvider } from "../../src/flow/model-provider.js";
import { EXTENSION_API_MIN_VERSION, EXTENSION_API_VERSION, extensionApiSupported } from "../../src/contract/daemon.js";
import { PLAYTEST_BUILTIN_TASKS, PLAYTEST_DRAFT_OPTIONS, PLAYTEST_RETRIES, playtestFlow, SCRIPT_DRAFT_SCHEMA } from "../../src/flow/playtest/index.js";

const provider = (text: string) => ({ complete: async () => ({ text }) });

describe("the model behind flow agent steps", () => {
  it("prefers the newest live registration, and has none of its own", async () => {
    const savedKey = process.env.ANTHROPIC_API_KEY;
    process.env.ANTHROPIC_API_KEY = "sk-ant-x";
    try {
      expect(resolveModelProvider()).toBeNull();
    } finally {
      if (savedKey === undefined) delete process.env.ANTHROPIC_API_KEY;
      else process.env.ANTHROPIC_API_KEY = savedKey;
    }

    const offA = registerModelProvider(provider("a"));
    const offB = registerModelProvider(provider("b"));
    expect((await resolveModelProvider()!.complete({})).text).toBe("b");
    offB();
    expect((await resolveModelProvider()!.complete({})).text).toBe("a");
    offA();
    offA();
    expect(resolveModelProvider()).toBeNull();
  });

  it("refuses something that is not a provider", () => {
    expect(() => registerModelProvider({} as never)).toThrow(/complete/);
  });

  it("names the missing model in one sentence", () => {
    expect(NO_MODEL_MESSAGE).toBe("No model is configured for flow agent steps.");
  });

  it("says how to get one: the provider extension and the daemon it loads in", () => {
    expect(NO_MODEL_WAY_FORWARD).toContain("ue-mcp install anthropic");
    expect(NO_MODEL_WAY_FORWARD).toContain("daemon");
  });
});

describe("the extension API range", () => {
  it("is 3, and still loads an extension built for 1", () => {
    expect(EXTENSION_API_VERSION).toBe(3);
    expect(EXTENSION_API_MIN_VERSION).toBe(1);
    expect(extensionApiSupported(1, 1)).toBe(true);
    expect(extensionApiSupported(1, 2)).toBe(true);
    expect(extensionApiSupported(2, 2)).toBe(true);
    expect(extensionApiSupported(3, 3)).toBe(true);
    expect(extensionApiSupported(4, 4)).toBe(false);
    expect(extensionApiSupported(0, 0)).toBe(false);
  });
});

describe("the playtest flow definition", () => {
  const steps = (playtestFlow().playtest as { steps: Record<string, { task: string; when?: string; options?: Record<string, unknown> }> }).steps;

  it("gathers, drafts and checks up to three times, then saves", () => {
    expect(PLAYTEST_RETRIES).toBe(2);
    expect(Object.values(steps).map((s) => s.task)).toEqual([
      "playtest.gather",
      "playtest.draft", "playtest.check",
      "playtest.draft", "playtest.check",
      "playtest.draft", "playtest.check",
      "playtest.save",
    ]);
    expect(steps[4].when).toBe("${steps.3.retry}");
    expect(steps[6].when).toBe("${steps.5.retry}");
    expect(steps[7].options).toMatchObject({ previous: "${steps.5}", draft: "${steps.6.parsed}", final: true, attempt: 3 });
    expect(steps[8].options).toMatchObject({ checked: "${steps.7}" });
  });

  it("drafts on an agent step with no tools, a budget and the step schema", () => {
    const draft = PLAYTEST_BUILTIN_TASKS.find((t) => t.name === "playtest.draft");
    expect(draft?.definition?.group).toBe("playtest");
    expect(PLAYTEST_DRAFT_OPTIONS.tools).toEqual([]);
    expect(PLAYTEST_DRAFT_OPTIONS.tokenBudget).toBeGreaterThan(0);
    expect(PLAYTEST_DRAFT_OPTIONS.schema).toBe(SCRIPT_DRAFT_SCHEMA);
  });
});
