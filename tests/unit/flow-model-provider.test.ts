import { describe, expect, it } from "vitest";
import { modelSource, NO_MODEL_MESSAGE, registerModelProvider, resolveModelProvider } from "../../src/flow/model-provider.js";
import { EXTENSION_API_MIN_VERSION, EXTENSION_API_VERSION, extensionApiSupported } from "../../src/daemon/extension-api.js";
import { PLAYTEST_DRAFT_OPTIONS, PLAYTEST_RETRIES, playtestFlow, playtestTasks, SCRIPT_DRAFT_SCHEMA } from "../../src/flow/playtest.js";

const provider = (text: string) => ({ complete: async () => ({ text }) });

describe("the model behind flow agent steps", () => {
  it("prefers the newest live registration, then the env key, then nothing", async () => {
    expect(resolveModelProvider({})).toBeNull();
    expect(modelSource({})).toBeNull();
    expect(modelSource({ ANTHROPIC_API_KEY: "sk-ant-x" })).toBe("env");
    expect(resolveModelProvider({ ANTHROPIC_API_KEY: "sk-ant-x" })).not.toBeNull();

    const offA = registerModelProvider(provider("a"));
    const offB = registerModelProvider(provider("b"));
    expect(modelSource({ ANTHROPIC_API_KEY: "sk-ant-x" })).toBe("extension");
    expect((await resolveModelProvider({})!.complete({})).text).toBe("b");
    offB();
    expect((await resolveModelProvider({})!.complete({})).text).toBe("a");
    offA();
    offA();
    expect(resolveModelProvider({})).toBeNull();
  });

  it("refuses something that is not a provider", () => {
    expect(() => registerModelProvider({} as never)).toThrow(/complete/);
  });

  it("names the missing model in one sentence", () => {
    expect(NO_MODEL_MESSAGE).toBe("No model is configured for flow agent steps.");
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
    const draft = playtestTasks()["playtest.draft"] as { class_path: string };
    expect(draft.class_path).toBe("playtest.draft");
    expect(PLAYTEST_DRAFT_OPTIONS.tools).toEqual([]);
    expect(PLAYTEST_DRAFT_OPTIONS.tokenBudget).toBeGreaterThan(0);
    expect(PLAYTEST_DRAFT_OPTIONS.schema).toBe(SCRIPT_DRAFT_SCHEMA);
  });
});
