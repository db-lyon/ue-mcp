/**
 * The protected-engine deny list is parsed twice: by the build scripts (plain
 * node, no TS) and by the server. Both must split and reject entries the same
 * way, because a mangled entry silently protects nothing. The scripts also
 * canonicalize each entry, so entries are compared after path.resolve.
 */
import * as path from "node:path";
import { describe, expect, it } from "vitest";
import { protectedEngineRoots as scriptRoots } from "../../scripts/build-utils.js";
import { protectedEngineRoots as serverRoots } from "../../src/editor/engine-root.js";

const ACCEPTED = [
  "",
  "C:\\Program Files\\Epic Games\\UE_5.8",
  "D:\\protected;E:\\other",
  "D:\\protected:E:\\other",
  " C:\\spaced ; D:\\also ",
  "/opt/ue:/srv/engines/UE_5.7",
  ";;C:\\x;;",
];

const REJECTED = ["relative/path;C:\\ok", "C:\\ok:engines"];

const env = (value: string) => ({ UE_MCP_PROTECTED_ENGINE_ROOTS: value });

describe("protected engine deny list", () => {
  it.each(ACCEPTED)("script and server split %j the same way", (value) => {
    const server = serverRoots(env(value)).map((p) => path.resolve(p));
    expect(scriptRoots(env(value))).toEqual(server);
  });

  it.each(REJECTED)("script and server both reject %j", (value) => {
    expect(() => serverRoots(env(value))).toThrow(/not an absolute path/);
    expect(() => scriptRoots(env(value))).toThrow(/not an absolute path/);
  });
});
