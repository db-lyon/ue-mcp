/**
 * The recorded handler specs still describe the running plugin (#1057).
 *
 * The spec'd actions' parameters are generated from tests/golden/handler-specs.json,
 * a recording of what the bridge publishes in get_bridge_capabilities.handlerSpecs.
 * A plugin whose specs moved without a re-record would have the server
 * advertising parameters its handlers no longer read, which is the defect the
 * specs exist to remove. This asks the editor that is running.
 *
 * To re-record on purpose:  npm run specs:record, then npm run specs:generate
 *
 * Also checks the registry resolves an alias before the handler runs, through
 * the socket rather than in process: `path` on read_blendspace must reach the
 * handler as `assetPath`, so the answer names the asset rather than refusing a
 * missing parameter.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { afterAll, describe, expect, it } from "vitest";
import { compareHandlerSpecs, type HandlerSpecs } from "../../src/surface/handler-spec.js";
import { closeLiveBridges, liveBridge, liveTarget } from "./harness.js";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const recorded = JSON.parse(fs.readFileSync(path.join(ROOT, "tests", "golden", "handler-specs.json"), "utf8")) as {
  handlers: HandlerSpecs;
};

const target = await liveTarget();

afterAll(() => closeLiveBridges());

describe("handler specs against the running plugin", () => {
  it("publishes them", () => {
    expect(target.capabilities.features ?? []).toContain("handler-specs");
    expect(target.capabilities.handlerSpecs, "the plugin published no handlerSpecs; rebuild it").toBeDefined();
  });

  it("matches the recording the surface was generated from", () => {
    const drift = compareHandlerSpecs(recorded.handlers, target.capabilities.handlerSpecs);
    expect(drift.checked).toBe(true);
    expect(drift.drifted, "re-record with npm run specs:record, then npm run specs:generate").toEqual([]);
  });

  it("resolves an alias to the declared name before the handler runs", async () => {
    const bridge = await liveBridge();
    const missing = "/Game/UEMCP/HandlerSpecLive/NoSuchBlendSpace";
    let message = "";
    try {
      const result = (await bridge.call("read_blendspace", { path: missing }, 15_000)) as { error?: string };
      message = String(result?.error ?? "");
    } catch (e) {
      message = e instanceof Error ? e.message : String(e);
    }
    expect(message).not.toMatch(/assetPath/i);
    expect(message).toContain("NoSuchBlendSpace");
  });
});
