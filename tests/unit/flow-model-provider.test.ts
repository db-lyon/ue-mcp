import { describe, expect, it } from "vitest";
import { modelSource, NO_MODEL_MESSAGE, registerModelProvider, resolveModelProvider } from "../../src/flow/model-provider.js";
import { EXTENSION_API_MIN_VERSION, EXTENSION_API_VERSION, extensionApiSupported } from "../../src/contract/daemon.js";

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
  it("is 4, and still loads an extension built for 1", () => {
    expect(EXTENSION_API_VERSION).toBe(4);
    expect(EXTENSION_API_MIN_VERSION).toBe(1);
    expect(extensionApiSupported(1, 1)).toBe(true);
    expect(extensionApiSupported(1, 2)).toBe(true);
    expect(extensionApiSupported(2, 2)).toBe(true);
    expect(extensionApiSupported(4, 4)).toBe(true);
    expect(extensionApiSupported(5, 5)).toBe(false);
    expect(extensionApiSupported(0, 0)).toBe(false);
  });
});
