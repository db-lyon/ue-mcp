import { describe, it, expect, beforeEach, afterEach } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { findLocalShadow, findBareNpxConfigs, parseServerInvocation, formatDoctor, type DoctorReport } from "../../src/doctor.js";

const stripAnsi = (s: string) => s.replace(/\x1b\[[0-9;]*m/g, "");

function baseReport(over: Partial<DoctorReport> = {}): DoctorReport {
  return {
    selfVersion: "1.0.78",
    registryLatest: "1.0.78",
    npmGlobal: { version: "1.0.78", dir: "/g/ue-mcp" },
    localShadow: null,
    effectiveNpx: "1.0.78",
    runningServers: [],
    targetProjectDir: "C:/proj/Vale",
    bridgePlugin: { version: "0.3.0", project: "C:/proj/Vale/Vale.uproject" },
    bareNpxConfigs: [],
    ...over,
  };
}

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

describe("formatDoctor verdict", () => {
  it("reports aligned when global, npx, and the target server are all latest", () => {
    const out = stripAnsi(formatDoctor(baseReport({
      runningServers: [{ pid: 1, version: "1.0.78", script: "x", project: "C:/proj/Vale", servesTarget: true }],
    })));
    expect(out).toContain("Everything aligned");
  });

  it("does NOT report aligned when npm global is behind latest", () => {
    // The running server is current, but the global is stale - a bare
    // `npx ue-mcp` would launch an old version next time. (#550 verdict fix)
    const out = stripAnsi(formatDoctor(baseReport({
      npmGlobal: { version: "1.0.77", dir: "/g/ue-mcp" },
      effectiveNpx: "1.0.77",
      runningServers: [{ pid: 1, version: "1.0.78", script: "x", project: "C:/proj/Vale", servesTarget: true }],
    })));
    expect(out).not.toContain("Everything aligned");
    expect(out).toContain("npm global is 1.0.77");
  });

  it("flags a stale target server as not aligned", () => {
    const out = stripAnsi(formatDoctor(baseReport({
      runningServers: [{ pid: 9, version: "1.0.60", script: "x", project: "C:/proj/Vale", servesTarget: true }],
    })));
    expect(out).not.toContain("Everything aligned");
    expect(out).toContain("pid 9");
  });

  it("does not let a server for another project produce a false aligned", () => {
    // Only an unrelated-project server is running; nothing serves the target.
    const out = stripAnsi(formatDoctor(baseReport({
      runningServers: [{ pid: 5, version: "1.0.78", script: "x", project: "C:/other/Proj", servesTarget: false }],
    })));
    expect(out).not.toContain("Everything aligned");
  });
});
