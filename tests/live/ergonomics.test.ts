/**
 * The action-schema and dispatch-ergonomics work, driven through the shipped
 * server against a real editor.
 *
 * The unit tests hold these behaviours against constructed inputs. That leaves
 * the thing that actually breaks unproven: whether the SERVER wires them
 * together on a live call. Field selection runs on a result the editor
 * produced, path repair has to survive zod validation and the whole dispatch
 * chain before it reaches a bridge method, and describe_action reports the
 * graph a running server assembled rather than the pristine declaration.
 *
 * Every case here is a read. Nothing in this file mutates the project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { LiveServer, resultJson } from "./server.js";
import { closeLiveBridges, liveTarget } from "./harness.js";

const target = await liveTarget();

let server: LiveServer;

beforeAll(async () => {
  server = await LiveServer.start({ projects: [target.uproject] });
}, 240_000);

afterAll(async () => {
  await server?.close();
  closeLiveBridges();
});

interface AssetList {
  assets?: Array<Record<string, unknown>>;
  totalMatched?: number;
  offset?: number;
  hasMore?: boolean;
  pathsRepaired?: { repairs: Array<{ param: string; from: string; to: string }> };
  fieldsNotFound?: { paths: string[]; note: string };
}

describe("describe_action against a running server", () => {
  it("answers with the parameters and bridge method of a real action", async () => {
    const result = await server.call("project", {
      action: "describe_action",
      name: "asset.set_property",
    });
    expect(result.isError).toBe(false);

    const schema = resultJson<{
      tool: string;
      action: string;
      bridge: string;
      local: boolean;
      class: string;
      drift: string[];
      params: Array<{ name: string; required: boolean; type: string }>;
    }>(result);

    expect(schema.tool).toBe("asset");
    expect(schema.bridge).toBe("set_asset_property");
    expect(schema.local).toBe(false);
    expect(schema.class).toBe("mutate");
    expect(schema.drift).toEqual([]);

    const byName = new Map(schema.params.map((p) => [p.name, p]));
    expect(byName.get("assetPath")?.required).toBe(true);
    expect(byName.get("propertyName")?.required).toBe(true);
    expect(byName.get("save")?.type).toBe("boolean");
  });

  it("suggests the intended action for a typo instead of failing bare", async () => {
    const result = await server.call("project", {
      action: "describe_action",
      name: "asset.set_propery",
    });
    expect(result.isError).toBe(true);
    expect(result.text).toContain("asset.set_property");
  });

  it("describes a whole category in one call, with each action's class", async () => {
    const result = await server.call("project", {
      action: "describe_action",
      category: "landscape",
    });
    const body = resultJson<{
      tool: string;
      actionCount: number;
      actions: Array<{ action: string; class: string }>;
    }>(result);

    expect(body.tool).toBe("landscape");
    expect(body.actionCount).toBeGreaterThan(0);
    expect(body.actions).toHaveLength(body.actionCount);
    for (const action of body.actions) {
      expect(["read", "mutate", "unknown"], action.action).toContain(action.class);
    }
    // A harness building a gate list wants both kinds present, or the
    // classification is not telling it anything.
    expect(body.actions.some((a) => a.class === "read")).toBe(true);
    expect(body.actions.some((a) => a.class === "mutate")).toBe(true);
  });

  it("covers the Epic actions this editor enriched the surface with", async () => {
    // describe_action reads the live graph, not the pristine declaration, so
    // an action injected at startup has to be describable too.
    const listed = await server.listTools();
    const asset = listed.find((t) => t.name === "asset");
    // The enum sits under anyOf, because `action` is advertised as an enum and
    // parsed as a string. Reading only the top level would find nothing and
    // skip this case silently, which is how a test comes to prove nothing.
    const action = (asset?.inputSchema as {
      properties?: { action?: { enum?: string[]; anyOf?: Array<{ enum?: string[] }> } };
    })?.properties?.action;
    const actions = action?.enum ?? action?.anyOf?.find((b) => Array.isArray(b.enum))?.enum ?? [];
    expect(actions.length, "the action enum must survive in the published schema").toBeGreaterThan(0);

    const injected = actions.find((a) => a.startsWith("epic_"));
    if (!injected) return; // no enrichment on this editor; nothing to assert

    const result = await server.call("project", {
      action: "describe_action",
      name: `asset.${injected}`,
    });
    expect(result.isError).toBe(false);
    expect(resultJson<{ action: string }>(result).action).toBe(injected);
  });
});

describe("field selection on a live read", () => {
  it("returns the unfiltered shape first, so the filtered ones mean something", async () => {
    const full = resultJson<AssetList>(
      await server.call("asset", { action: "list", directory: "/Game", maxResults: 5 }),
    );
    expect(Array.isArray(full.assets)).toBe(true);
    expect(full.totalMatched).toBeGreaterThan(0);
  });

  it("keeps only the named field of every element", async () => {
    const full = resultJson<AssetList>(
      await server.call("asset", { action: "list", directory: "/Game", maxResults: 5 }),
    );
    const key = Object.keys(full.assets?.[0] ?? {})[0];
    expect(key).toBeTruthy();

    const narrowed = resultJson<AssetList>(
      await server.call("asset", {
        action: "list",
        directory: "/Game",
        maxResults: 5,
        select: [`assets.${key}`],
      }),
    );

    expect(Object.keys(narrowed)).toEqual(["assets"]);
    expect(narrowed.assets).toHaveLength(full.assets!.length);
    for (const entry of narrowed.assets ?? []) {
      expect(Object.keys(entry)).toEqual([key]);
    }
  });

  it("drops a named field and keeps the rest of the result", async () => {
    const trimmed = resultJson<AssetList>(
      await server.call("asset", {
        action: "list",
        directory: "/Game",
        maxResults: 5,
        omit: ["assets"],
      }),
    );
    expect(trimmed.assets).toBeUndefined();
    expect(trimmed.totalMatched).toBeGreaterThan(0);
  });

  it("returns the full result and says so when no path matched", async () => {
    const result = resultJson<AssetList>(
      await server.call("asset", {
        action: "list",
        directory: "/Game",
        maxResults: 5,
        select: ["definitely.not.a.field"],
      }),
    );
    expect(result.assets).toBeDefined();
    expect(result.fieldsNotFound?.paths).toEqual(["definitely.not.a.field"]);
  });

  it("never lets the routing parameters reach the editor", async () => {
    // The bridge answers "Unknown parameter" for nothing, so the proof that
    // select/omit were stripped is that the call succeeded at all AND the
    // result is the editor's, not an error.
    const result = await server.call("asset", {
      action: "list",
      directory: "/Game",
      maxResults: 3,
      select: ["totalMatched"],
      omit: ["nothing"],
      timeoutMs: 60_000,
    });
    expect(result.isError).toBe(false);
    expect(resultJson<AssetList>(result).totalMatched).toBeGreaterThan(0);
  });
});

describe("path repair on a live call", () => {
  it("resolves a directory written with backslashes and reports the repair", async () => {
    const clean = resultJson<AssetList>(
      await server.call("asset", { action: "list", directory: "/Game", maxResults: 5 }),
    );

    const result = await server.call("asset", {
      action: "list",
      directory: "\\Game",
      maxResults: 5,
    });
    expect(result.isError).toBe(false);

    const repaired = resultJson<AssetList>(result);
    // Same answer as the correctly spelled call: the repair happened before
    // the bridge saw it, rather than the editor coping with a mangled path.
    expect(repaired.totalMatched).toBe(clean.totalMatched);
    expect(repaired.pathsRepaired?.repairs).toEqual([
      { param: "directory", from: "\\Game", to: "/Game" },
    ]);
  });

  it("keeps the repair report visible through a narrow selection", async () => {
    const result = resultJson<AssetList>(
      await server.call("asset", {
        action: "list",
        directory: "\\Game",
        maxResults: 5,
        select: ["totalMatched"],
      }),
    );
    expect(result.totalMatched).toBeGreaterThan(0);
    expect(result.assets).toBeUndefined();
    expect(result.pathsRepaired).toBeDefined();
  });

  it("says nothing about repairs on a call that needed none", async () => {
    const result = resultJson<AssetList>(
      await server.call("asset", { action: "list", directory: "/Game", maxResults: 5 }),
    );
    expect(result.pathsRepaired).toBeUndefined();
    expect(result.fieldsNotFound).toBeUndefined();
  });
});

describe("unknown action errors", () => {
  it("names the closest spelling instead of listing every action", async () => {
    const result = await server.call("level", { action: "get_outlner" });
    expect(result.isError).toBe(true);
    expect(result.text).toContain("get_outliner");
    expect(result.text).toContain("describe_action");
  });
});
