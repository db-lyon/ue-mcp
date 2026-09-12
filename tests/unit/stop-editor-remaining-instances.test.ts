// #1072: a stop that closed one of two editors reported plain success.
//
// The stop is aimed by the port this project published, so it acts on one
// editor, and that is right. What it never said is that it aimed at one. With
// two editors of a project open, the call closed one and answered with nothing
// but "Editor quit itself via the bridge", and a caller who had no reason to
// suspect a second editor read that as "this project has no editor running".
//
// The survivor is discoverable: every bridge publishes its own address to
// instances/<pid>.json, which no other instance can delete. What is pinned
// here is which of those records count. A record can outlive a crash and a
// running editor may have published none, so a record and a live process must
// agree before anything is reported.
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, describe, expect, it } from "vitest";

import { findRemainingInstances } from "../../src/editor-control.js";

const made: string[] = [];

function projectWithInstances(
  records: Array<{ pid: number; port: number; state?: string }>,
): string {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-1072-"));
  made.push(dir);
  const instances = path.join(dir, "Saved", "UE_MCP_Bridge", "instances");
  fs.mkdirSync(instances, { recursive: true });
  for (const r of records) {
    fs.writeFileSync(
      path.join(instances, `${r.pid}.json`),
      JSON.stringify({ pid: r.pid, port: r.port, state: r.state ?? "listening", instanceId: `i${r.pid}` }),
    );
  }
  return dir;
}

const listing = (pids: number[]) => async () => pids.map((pid) => ({ pid }));

afterEach(() => {
  for (const dir of made.splice(0)) fs.rmSync(dir, { recursive: true, force: true });
});

describe("editors left running after a stop", () => {
  it("names an editor that is still up, with the port it published", async () => {
    const dir = projectWithInstances([{ pid: 111, port: 8321 }, { pid: 222, port: 8322 }]);

    const remaining = await findRemainingInstances(dir, "P.uproject", 111, listing([111, 222]));

    expect(remaining).toEqual([{ pid: 222, port: 8322 }]);
  });

  it("reports nothing for an ordinary single-editor stop", async () => {
    const dir = projectWithInstances([{ pid: 111, port: 8321 }]);

    expect(await findRemainingInstances(dir, "P.uproject", 111, listing([111]))).toEqual([]);
  });

  it("ignores a record whose process is gone", async () => {
    // What a crash leaves behind. Reporting it would tell the caller an editor
    // is running when none is, which is the same wrong answer in reverse.
    const dir = projectWithInstances([{ pid: 111, port: 8321 }, { pid: 999, port: 8399 }]);

    expect(await findRemainingInstances(dir, "P.uproject", 111, listing([111]))).toEqual([]);
  });

  it("ignores a record that names no reachable port", async () => {
    // A bind-failed record exists to explain a failure, not to be dialled.
    const dir = projectWithInstances([
      { pid: 111, port: 8321 },
      { pid: 222, port: 0, state: "bind-failed" },
    ]);

    expect(await findRemainingInstances(dir, "P.uproject", 111, listing([111, 222]))).toEqual([]);
  });

  it("never turns a successful stop into a failure when process listing throws", async () => {
    const dir = projectWithInstances([{ pid: 111, port: 8321 }, { pid: 222, port: 8322 }]);
    const throwing = async () => { throw new Error("no process API here"); };

    expect(await findRemainingInstances(dir, "P.uproject", 111, throwing)).toEqual([]);
  });

  it("reports nothing when there is no project directory to look in", async () => {
    expect(await findRemainingInstances(undefined, "P.uproject", 111, listing([111, 222]))).toEqual([]);
  });
});
