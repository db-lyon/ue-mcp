import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { normalizeProjectRoot } from "../../src/port.js";
import {
  clearRequestedPort,
  publishRequestedPort,
  requestedPortPath,
  syncRequestedPort,
} from "../../src/requested-port.js";

const temporaryRoots: string[] = [];

function makeProjectDir(): string {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-requested-"));
  temporaryRoots.push(root);
  return root;
}

function readRecord(projectDir: string): Record<string, unknown> | null {
  try {
    return JSON.parse(fs.readFileSync(requestedPortPath(projectDir), "utf8")) as Record<string, unknown>;
  } catch {
    return null;
  }
}

afterEach(() => {
  while (temporaryRoots.length > 0) {
    const root = temporaryRoots.pop()!;
    fs.rmSync(root, { recursive: true, force: true });
  }
});

describe("requested port channel (#817)", () => {
  it("publishes the pin where the bridge looks for it", () => {
    const projectDir = makeProjectDir();
    publishRequestedPort(projectDir, 51234, "config");

    expect(requestedPortPath(projectDir)).toBe(
      path.join(projectDir, "Saved", "UE_MCP_Bridge", "requested.json"),
    );

    const record = readRecord(projectDir);
    expect(record?.port).toBe(51234);
    expect(record?.source).toBe("config");
    expect(record?.writtenBy).toBe(process.pid);
  });

  it("normalizes the project root the way the bridge compares it", () => {
    const projectDir = makeProjectDir();
    publishRequestedPort(projectDir, 51234, "config");

    // The bridge refuses a record whose root is not this project's, so the two
    // sides have to spell the same directory the same way.
    expect(readRecord(projectDir)?.projectRoot).toBe(normalizeProjectRoot(projectDir));
  });

  it("refuses to publish a port outside the usable range", () => {
    const projectDir = makeProjectDir();
    publishRequestedPort(projectDir, 0, "config");
    publishRequestedPort(projectDir, 70000, "config");
    expect(readRecord(projectDir)).toBeNull();
  });

  it("leaves no file behind when the port is not a pin", () => {
    const projectDir = makeProjectDir();
    syncRequestedPort(projectDir, 51234, "derived");
    expect(readRecord(projectDir)).toBeNull();

    syncRequestedPort(projectDir, 51234, "lockfile");
    expect(readRecord(projectDir)).toBeNull();

    syncRequestedPort(projectDir, 51234, "default");
    expect(readRecord(projectDir)).toBeNull();
  });

  it("publishes each of the three pin sources", () => {
    for (const source of ["explicit", "env", "config"] as const) {
      const projectDir = makeProjectDir();
      syncRequestedPort(projectDir, 51234, source);
      expect(readRecord(projectDir)?.source).toBe(source);
    }
  });

  it("removes the file when the pin goes away", () => {
    const projectDir = makeProjectDir();
    syncRequestedPort(projectDir, 51234, "config");
    expect(readRecord(projectDir)?.port).toBe(51234);

    // A user who deletes bridge.port from their config must not leave the
    // editor bound to it for the rest of time.
    syncRequestedPort(projectDir, 49152, "derived");
    expect(fs.existsSync(requestedPortPath(projectDir))).toBe(false);
  });

  it("treats an already absent file as nothing to do", () => {
    const projectDir = makeProjectDir();
    expect(() => clearRequestedPort(projectDir)).not.toThrow();
  });

  it("leaves no temporary file behind after a publish", () => {
    const projectDir = makeProjectDir();
    publishRequestedPort(projectDir, 51234, "env");

    const stateDir = path.join(projectDir, "Saved", "UE_MCP_Bridge");
    expect(fs.readdirSync(stateDir)).toEqual(["requested.json"]);
  });
});
