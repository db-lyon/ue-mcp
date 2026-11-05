// @live-editor headless
/**
 * blueprint(set_component_property) on a Character Blueprint while play is
 * running and the player possesses an instance of it (#1600). The compile
 * reinstances the possessed pawn; the new pawn must not carry the player
 * state's OnDestroyed binding twice.
 * Refs #1600
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/Fix1600";
const BP = `${DIR}/BP_Fix1600Char`;
const IA = `${DIR}/IA_Fix1600.IA_Fix1600`;
const LABEL = "Fix1600_Char";

type Json = Record<string, unknown>;
type LogLine = { message: string; sequence: number };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

let bridge: EditorBridge;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function pieRunning(): Promise<boolean> {
  return body(await callBridge(bridge, "pie_control", { pieAction: "status" })).isPlaying === true;
}

async function waitForPie(playing: boolean): Promise<boolean> {
  for (let i = 0; i < 60; i++) {
    if ((await pieRunning()) === playing) return true;
    await sleep(500);
  }
  return false;
}

async function stopPie(): Promise<void> {
  if (await pieRunning()) {
    await callBridge(bridge, "pie_control", { pieAction: "stop" });
    await waitForPie(false);
  }
}

async function logSince(query: string, since: number): Promise<string[]> {
  const r = await call("search_log", { query, limit: 4096 });
  return ((r.matches as LogLine[]) ?? []).filter((m) => m.sequence > since).map((m) => m.message.trim());
}

/** The sequence number of a line logged now, so later reads see only what follows. */
async function logMark(): Promise<number> {
  const marker = `fix1600_mark_${Date.now()}_${Math.random().toString(36).slice(2)}`;
  await call("execute_python", { code: `import unreal\nunreal.log("${marker}")` });
  const rows = ((await call("search_log", { query: marker })).matches as LogLine[]) ?? [];
  return rows.length ? rows[rows.length - 1].sequence : 0;
}

/**
 * Every dynamic delegate bound on the actor and its components, read from
 * `obj dump`, as "<object> <property>" -> its invocation list.
 */
async function delegateBindings(actorPath: string): Promise<Map<string, string[]>> {
  const since = await logMark();
  const code = [
    "import unreal",
    `a = unreal.find_object(None, "${actorPath}")`,
    "for o in [a] + list(a.get_components_by_class(unreal.ActorComponent)):",
    "    unreal.log('fix1600_dump ' + o.get_path_name())",
    "    unreal.SystemLibrary.execute_console_command(None, 'obj dump ' + o.get_path_name())",
  ].join("\n");
  await call("execute_python", { code });
  const r = await call("get_output_log", { maxLines: 4096, limit: 4096 });
  const lines = ((r.lines as LogLine[]) ?? []).filter((l) => l.sequence > since).map((l) => l.message.trim());
  const out = new Map<string, string[]>();
  let owner = "";
  for (const line of lines) {
    if (line.startsWith("fix1600_dump ")) {
      owner = line.slice("fix1600_dump ".length);
      continue;
    }
    const m = /^(\w+)=\((.+)\)$/.exec(line);
    if (!m || !owner) continue;
    const entries = m[2].split(",");
    if (entries.every((e) => /^\/[^()=" ]+\.\w+$/.test(e))) out.set(`${owner} ${m[1]}`, entries);
  }
  return out;
}

function duplicates(bindings: Map<string, string[]>): string[] {
  const found: string[] = [];
  for (const [key, entries] of bindings) {
    if (new Set(entries).size !== entries.length) found.push(`${key}=(${entries.join(",")})`);
  }
  return found;
}

beforeAll(async () => {
  bridge = await getBridge();
  await stopPie();
  await call("demo_go_home");
  await callBridge(bridge, "delete_actor", { actorLabel: LABEL });
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await call("create_blueprint", { assetPath: BP, parentClass: "Character" });
  await call("create_input_action", { name: "IA_Fix1600", packagePath: DIR });
  await call("add_node", { assetPath: BP, graphName: "EventGraph", nodeClass: "EnhancedInputAction", nodeParams: { inputAction: IA } });
  await call("add_node", {
    assetPath: BP, graphName: "EventGraph", nodeClass: "ComponentBoundEvent",
    nodeParams: { componentName: "CapsuleComponent", delegateName: "OnComponentHit" },
  });
  await call("compile_blueprint", { assetPath: BP });
  await call("place_actor", { actorClass: `${BP}.BP_Fix1600Char_C`, label: LABEL, location: { x: 0, y: -16000, z: 200 } });
  await call("set_actor_property", { actorLabel: LABEL, propertyName: "AutoPossessPlayer", value: "Player0" });
  await call("save_level", {});
}, 180_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await stopPie();
  } finally {
    await callBridge(bridge, "delete_actor", { actorLabel: LABEL });
    await callBridge(bridge, "save_level", {});
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("set_component_property during play-in-editor (#1600)", () => {
  it("reinstances the possessed pawn without binding any delegate twice, and the value reads back", async () => {
    await callBridge(bridge, "pie_control", { pieAction: "start" });
    expect(await waitForPie(true)).toBe(true);
    await sleep(1000);

    const pawn = await call("get_pie_pawn");
    expect(pawn.actorLabel).toBe(LABEL);
    const pawnPath = String(pawn.path);
    await call("pie_inject_input", { action_path: IA, value_x: 1 });
    await sleep(500);

    const before = await delegateBindings(pawnPath);
    const stateBinding = (b: Map<string, string[]>): string[] =>
      (b.get(`${pawnPath} OnDestroyed`) ?? []).filter((e) => e.endsWith(".OnPawnPrivateDestroyed"));
    expect(stateBinding(before)).toHaveLength(1);
    expect(duplicates(before)).toEqual([]);

    const since = await logMark();
    const set = await call("set_blueprint_component_property", {
      assetPath: BP, componentName: "CharacterMovement", propertyName: "JumpZVelocity", value: 777,
    });
    expect(set.updated).toBe(true);
    await sleep(500);

    expect(await logSince("Ensure condition failed", since)).toEqual([]);

    const after = await call("get_pie_pawn");
    expect(after.actorLabel).toBe(LABEL);
    const bindings = await delegateBindings(String(after.path));
    expect(duplicates(bindings)).toEqual([]);
    // The player state still hears about the pawn's destruction, once.
    expect(stateBinding(bindings)).toHaveLength(1);

    const read = await call("get_blueprint_component_property", {
      assetPath: BP, componentName: "CharacterMovement", propertyName: "JumpZVelocity",
    });
    expect(Number(read.value)).toBe(777);
  }, 120_000);
});
