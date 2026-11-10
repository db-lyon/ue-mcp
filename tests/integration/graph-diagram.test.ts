/**
 * The diagram actions through the real server: the fake editor answers the
 * graph readers with the fixture answers the unit tests use, and the client
 * gets Mermaid text, an SVG written to disk, and a PNG as MCP image content,
 * through the micro gateway and a full category tool.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { json, McpServer, Sandbox } from "./harness.js";

const FIXTURES = path.join(__dirname, "..", "unit", "fixtures", "diagrams");
const fixture = (name: string) => JSON.parse(fs.readFileSync(path.join(FIXTURES, `${name}.json`), "utf8"));

let sb: Sandbox;
let bridge: FakeBridge;

beforeAll(async () => {
  sb = new Sandbox();
  bridge = await FakeBridge.start({
    projectDir: sb.path("editor"),
    handlers: {
      read_pcg_graph: () => fixture("pcg"),
      read_state_tree: () => fixture("statetree"),
    },
  });
}, 60_000);

afterAll(async () => {
  await bridge?.stop();
  sb.cleanup();
});

describe("graph diagrams", () => {
  it("answers Mermaid by default and a PNG as image content through the gateway", async () => {
    covers("server.graph-diagram");
    const server = await McpServer.start(sb, { projects: [], port: bridge.port });
    try {
      const r = await server.call("tools", { action: "call", category: "pcg", method: "diagram", args: { assetPath: "/Game/PCG/PCG_Forest" } });
      expect(r.isError, r.text).toBe(false);
      const body = json<{ format: string; mermaid: string; nodeCount: number }>(r);
      expect(body.format).toBe("mermaid");
      expect(body.nodeCount).toBe(5);
      expect(body.mermaid).toContain('n_InputNode -.->|"Landscape → Surface"| n_SurfaceSampler_1');
      expect(r.media).toEqual([]);
      expect(bridge.calls.filter((c) => c.method === "read_pcg_graph").at(-1)!.params).toEqual({ assetPath: "/Game/PCG/PCG_Forest" });

      const out = sb.path("diagrams", "forest.png");
      const png = await server.call("tools", { action: "call", category: "pcg", method: "diagram", args: { assetPath: "/Game/PCG/PCG_Forest", format: "png", outputPath: out } });
      expect(png.isError, png.text).toBe(false);
      expect(png.media).toHaveLength(1);
      expect(png.media[0]).toMatchObject({ type: "image", mimeType: "image/png" });
      const bytes = Buffer.from(png.media[0].data, "base64");
      expect(bytes.subarray(1, 4).toString("ascii")).toBe("PNG");
      expect(fs.readFileSync(out).equals(bytes)).toBe(true);
      const pngBody = json<{ path: string; media: Array<Record<string, unknown>> }>(png);
      expect(pngBody.path).toBe(out);
      expect(pngBody.media[0]).not.toHaveProperty("data");
    } finally {
      await server.close();
    }
  });

  it("writes an SVG from the full category tool and draws a StateTree as a state diagram", async () => {
    covers("server.graph-diagram");
    const server = await McpServer.start(sb, { projects: [], port: bridge.port, strategy: "full" });
    try {
      const out = sb.path("diagrams", "guard.svg");
      const r = await server.call("statetree", { action: "diagram", assetPath: "/Game/AI/ST_Guard", format: "svg", outputPath: out });
      expect(r.isError, r.text).toBe(false);
      const body = json<{ format: string; diagramType: string; path: string }>(r);
      expect(body).toMatchObject({ format: "svg", diagramType: "stateDiagram-v2", path: out });
      const svg = fs.readFileSync(out, "utf8");
      expect(svg).toMatch(/^<svg xmlns="http:\/\/www.w3.org\/2000\/svg"/);
      expect(svg).toContain('data-node="00000004-0000-0000-0000-000000000000"');

      const bad = await server.call("statetree", { action: "diagram", assetPath: "/Game/AI/ST_Guard", focus: "Nowhere" });
      expect(bad.isError).toBe(true);
      expect(bad.text).toMatch(/focus 'Nowhere' matches no node/);
    } finally {
      await server.close();
    }
  });
});
