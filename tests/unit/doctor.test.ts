import { describe, it, expect, beforeEach, afterEach } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { findLocalShadow, findBareNpxConfigs, parseServerInvocation } from "../../src/doctor.js";

let root: string;

beforeEach(() => {
  root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-doctor-"));
});
afterEach(() => {
  fs.rmSync(root, { recursive: true, force: true });
});

function writeShadow(dir: string, version: string): void {
  const pkgDir = path.join(dir, "node_modules", "ue-mcp");
  fs.mkdirSync(pkgDir, { recursive: true });
  fs.writeFileSync(path.join(pkgDir, "package.json"), JSON.stringify({ name: "ue-mcp", version }));
}

function writeMcpJson(dir: string, server: Record<string, unknown>): void {
  fs.writeFileSync(path.join(dir, ".mcp.json"), JSON.stringify({ mcpServers: { "ue-mcp": server } }));
}

describe("findLocalShadow", () => {
  it("returns null when no node_modules/ue-mcp exists", () => {
    expect(findLocalShadow(root)).toBeNull();
  });

  it("detects a project-local shadow and its version", () => {
    writeShadow(root, "1.0.64");
    const shadow = findLocalShadow(root);
    expect(shadow?.version).toBe("1.0.64");
    expect(shadow?.dir).toContain(path.join("node_modules", "ue-mcp"));
  });

  it("walks up parent directories like npx resolution", () => {
    writeShadow(root, "1.0.64");
    const nested = path.join(root, "a", "b");
    fs.mkdirSync(nested, { recursive: true });
    expect(findLocalShadow(nested)?.version).toBe("1.0.64");
  });
});

describe("findBareNpxConfigs", () => {
  it("flags bare `npx ue-mcp` (no pin, no -y)", () => {
    writeMcpJson(root, { command: "npx", args: ["ue-mcp", "my.uproject"] });
    expect(findBareNpxConfigs(root)).toEqual([path.join(root, ".mcp.json")]);
  });

  it("does not flag a version-pinned invocation", () => {
    writeMcpJson(root, { command: "npx", args: ["ue-mcp@1.0.76", "my.uproject"] });
    expect(findBareNpxConfigs(root)).toEqual([]);
  });

  it("does not flag a self-healing `npx -y ue-mcp@latest`", () => {
    writeMcpJson(root, { command: "npx", args: ["-y", "ue-mcp@latest", "my.uproject"] });
    expect(findBareNpxConfigs(root)).toEqual([]);
  });

  it("ignores non-ue-mcp servers", () => {
    writeMcpJson(root, { command: "npx", args: ["some-other-mcp"] });
    expect(findBareNpxConfigs(root)).toEqual([]);
  });

  it("ignores a malformed .mcp.json without throwing", () => {
    fs.writeFileSync(path.join(root, ".mcp.json"), "{ not json");
    expect(findBareNpxConfigs(root)).toEqual([]);
  });
});

describe("parseServerInvocation", () => {
  it("parses a server launched with a project directory", () => {
    const cmd = "node C:\\Users\\david\\Projects\\UE\\ue-mcp\\dist\\index.js C:\\Users\\david\\Projects\\UE\\ue-mcp\\tests\\ue_mcp";
    const r = parseServerInvocation(cmd);
    expect(r?.project).toContain("ue_mcp");
    expect(r?.script.toLowerCase()).toContain("ue-mcp/dist/index.js");
  });

  it("parses a quoted local-node_modules server with a .uproject arg", () => {
    const cmd = '"node" "C:\\Users\\david\\Projects\\UE\\Vale\\node_modules\\.bin\\..\\ue-mcp\\dist\\index.js" C:/Users/david/Projects/UE/Vale/Vale.uproject';
    const r = parseServerInvocation(cmd);
    expect(r).not.toBeNull();
    expect(r?.project).toBe("C:/Users/david/Projects/UE/Vale/Vale.uproject");
  });

  it("rejects a --help invocation (not a server)", () => {
    expect(parseServerInvocation("node C:/x/ue-mcp/dist/index.js --help")).toBeNull();
  });

  it("rejects a subcommand invocation (doctor/update)", () => {
    expect(parseServerInvocation("node /x/ue-mcp/dist/index.js doctor")).toBeNull();
    expect(parseServerInvocation("node /x/ue-mcp/dist/index.js update --build")).toBeNull();
  });

  it("returns null for a non-ue-mcp process", () => {
    expect(parseServerInvocation("node /some/other/server.js project")).toBeNull();
  });
});
