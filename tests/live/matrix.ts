/**
 * The 7.3 matrix, written down (#817).
 *
 * Plan item 7.3 enumerates the cases the multi-editor work has to be tested
 * against. A list in a plan file cannot say whether it is satisfied, so it is
 * transcribed here with, for each case, where the assertion actually lives:
 *
 *   - `live`       an assertion in this suite, against a real editor;
 *   - `engine-free` an assertion in the suite that needs no engine. Referenced
 *                  rather than duplicated: running the same check twice does
 *                  not make it truer, and a live copy of an engine-free test
 *                  is a test that only runs when somebody has an editor up;
 *   - `cpp`        owned by the plugin's own automation suite (plan item 0.9),
 *                  because it is about a socket option, a bind result or a
 *                  process lifetime that no client-side test can observe;
 *   - `pending`    the behaviour has not shipped yet. Named with the plan item
 *                  that ships it, so the row is a to-do rather than a silence.
 *
 * `coverage.test.ts` checks every reference against the file it names, so a
 * renamed or deleted test breaks this list instead of quietly emptying it.
 */

export type CoverageRef =
  | { kind: "live"; file: string; title: string }
  | { kind: "engine-free"; file: string; title: string }
  | { kind: "cpp"; reason: string }
  | { kind: "pending"; planItem: string; reason: string };

export interface MatrixCase {
  /** Stable handle, used in reports. */
  id: string;
  /** The case as plan item 7.3 states it. */
  text: string;
  coverage: CoverageRef[];
}

const LIVE_LEAK = "tests/live/dispatch-leak.test.ts";
const LIVE_ADDRESSING = "tests/live/addressing.test.ts";
const LIVE_SINGLE = "tests/live/single-editor.test.ts";
const LIVE_GOLDEN = "tests/live/golden-connected.test.ts";

/** The eleven cases plan item 7.3 enumerates. */
export const MATRIX_CASES: MatrixCase[] = [
  {
    id: "per-path-dispatch-and-leaks",
    text: "Per-path dispatch and leak assertions.",
    coverage: [
      { kind: "live", file: LIVE_LEAK, title: "MCP category tools: the editor receives the call and no routing key" },
      { kind: "live", file: LIVE_LEAK, title: "flows: neither the run's params nor a step's own options carry it through" },
      { kind: "live", file: LIVE_LEAK, title: "the micro gateway: a target nested inside args does not travel" },
      { kind: "live", file: LIVE_LEAK, title: "niagara(batch), which bypasses the task registry entirely" },
      { kind: "live", file: LIVE_LEAK, title: "the category tool's own handler, which is not the MCP path but is kept correct" },
      { kind: "live", file: LIVE_LEAK, title: "the HTTP flow surface resolves the target instead of forwarding it" },
      {
        kind: "engine-free",
        file: "tests/multi-editor/write-gating.test.ts",
        title: "never forwards the routing instruction into the bridge call",
      },
      {
        kind: "engine-free",
        file: "tests/multi-editor/two-bridge-harness.test.ts",
        title: "routes each session's calls to its own editor and to no other",
      },
    ],
  },
  {
    id: "identity-mismatch",
    text: "Identity mismatch refusing beyond one editor and warning at one.",
    coverage: [
      { kind: "live", file: LIVE_ADDRESSING, title: "refuses to connect a session whose port was pinned for another project" },
      { kind: "live", file: LIVE_ADDRESSING, title: "accepts the same pin for the project it was chosen for" },
      { kind: "live", file: LIVE_ADDRESSING, title: "reports the project the connected editor actually has open" },
      {
        kind: "engine-free",
        file: "tests/unit/bridge-retarget.test.ts",
        title: "accepts the pinned port again once the new project's editor publishes a lockfile",
      },
      {
        kind: "pending",
        planItem: "1.6",
        reason:
          "comparing the handshake's project root against the session's, which is what turns the refusal " +
          "from a port-provenance rule into an identity one, is not implemented; the bridge publishes the " +
          "project name and the client reads it, but connect() does not compare roots",
      },
    ],
  },
  {
    id: "legacy-bridge",
    text: "A legacy bridge connecting unchanged and within the normal timeout.",
    coverage: [
      { kind: "live", file: LIVE_SINGLE, title: "answers on a fresh connection inside the ordinary connect budget" },
      {
        kind: "engine-free",
        file: "tests/unit/bridge.test.ts",
        title: "names both versions when an older plugin does not know a method",
      },
      {
        kind: "engine-free",
        file: "tests/unit/bridge.test.ts",
        title: "settles at once when the socket dies before the handshake is answered",
      },
    ],
  },
  {
    id: "two-editors-one-project",
    text: "Two editors of one project detected and reported.",
    coverage: [
      { kind: "live", file: LIVE_ADDRESSING, title: "registers one session for the project however the path is spelled" },
      { kind: "live", file: LIVE_ADDRESSING, title: "keeps a per-process instance record next to the port lockfile" },
      {
        kind: "engine-free",
        file: "tests/unit/session.test.ts",
        title: "records sessions that collapse onto one port, because they cannot be told apart",
      },
      {
        kind: "engine-free",
        file: "tests/unit/session.test.ts",
        title: "folds the case of the path and of the extension, where the filesystem does",
      },
      {
        kind: "engine-free",
        file: "tests/unit/session.test.ts",
        title: "keeps projects that differ by more than spelling apart",
      },
      {
        kind: "cpp",
        reason:
          "two editors of one project binding two ports is a socket-exclusivity result (plan 0.2), and " +
          "these tests attach to an editor it did not start",
      },
    ],
  },
  {
    id: "crash-stale-bind-failure",
    text: "Crash, stale record, and bind-failure states.",
    coverage: [
      { kind: "live", file: LIVE_SINGLE, title: "publishes a port lockfile whose process is alive and answering" },
      { kind: "live", file: LIVE_SINGLE, title: "treats a lockfile whose process is gone as evidence of a crash, not an address" },
      { kind: "live", file: LIVE_SINGLE, title: "reads a bind-failure record only while the editor that wrote it is alive" },
      {
        kind: "engine-free",
        file: "tests/unit/editor-lifecycle-target.test.ts",
        title: "refuses a lockfile whose process is gone rather than trusting its port",
      },
      {
        kind: "engine-free",
        file: "tests/unit/bridge.test.ts",
        title: "reports a bridge that failed to bind while its editor is still running",
      },
    ],
  },
  {
    id: "no-project-default-bridge",
    text: "The no-project default bridge.",
    coverage: [
      { kind: "live", file: LIVE_ADDRESSING, title: "dispatches bridge actions with no project argument at all" },
      {
        kind: "engine-free",
        file: "tests/unit/session.test.ts",
        title: "keeps a project-less default session on the legacy fixed port",
      },
    ],
  },
  {
    id: "union-with-per-session-refusal",
    text: "Union advertised with per-session refusal.",
    coverage: [
      { kind: "live", file: LIVE_ADDRESSING, title: "advertises a category one project disabled, and refuses it for that project" },
      { kind: "live", file: LIVE_ADDRESSING, title: "keeps the same category working for the editor that did not disable it" },
      {
        kind: "engine-free",
        file: "tests/unit/session-surface.test.ts",
        title: "advertises the union and records which editor provides each action",
      },
      {
        kind: "engine-free",
        file: "tests/unit/session-surface.test.ts",
        title: "refuses a disabled category for the editor that disabled it, naming the config",
      },
    ],
  },
  {
    id: "gating-armed-and-inert",
    text: "Gating active beyond one editor and inert at one.",
    coverage: [
      { kind: "live", file: LIVE_ADDRESSING, title: "refuses an untargeted change and names both editors" },
      { kind: "live", file: LIVE_ADDRESSING, title: "refuses an untargeted lifecycle action, which is the one that closes a window" },
      { kind: "live", file: LIVE_ADDRESSING, title: "serves an untargeted read from the active session" },
      { kind: "live", file: LIVE_SINGLE, title: "gates nothing: an untargeted change runs, as it always did" },
      {
        kind: "engine-free",
        file: "tests/multi-editor/write-gating.test.ts",
        title: "refuses a mutation and reaches neither bridge",
      },
      { kind: "engine-free", file: "tests/unit/editor-gate.test.ts", title: "refuses an unclassifiable call rather than guessing it is a read" },
    ],
  },
  {
    id: "attribution",
    text: "Responses carry the serving editor beyond one editor, and nothing at one.",
    coverage: [
      { kind: "live", file: LIVE_ADDRESSING, title: "names the serving editor on a response" },
      { kind: "live", file: LIVE_SINGLE, title: "attributes nothing to an editor, because there is only one" },
      { kind: "engine-free", file: "tests/unit/editor-gate.test.ts", title: "is absent at one editor" },
    ],
  },
  {
    id: "both-golden-baselines",
    text: "Both golden baselines.",
    coverage: [
      // This case asked for the surface recorded in both states because it
      // used to differ between them. It no longer can: every action is
      // declared, so the two recordings are identical and their equality is
      // what the connected half asserts.
      { kind: "live", file: LIVE_GOLDEN, title: "advertises exactly what it advertises with no editor attached" },
      { kind: "live", file: LIVE_GOLDEN, title: "matches the committed baseline" },
      { kind: "live", file: LIVE_GOLDEN, title: "still matches the committed baseline" },
      { kind: "live", file: LIVE_GOLDEN, title: "records the same bytes from a catalog enumerated in a different order" },
      { kind: "engine-free", file: "tests/unit/golden-editor-down.test.ts", title: "matches the committed baseline" },
      { kind: "engine-free", file: "tests/unit/golden-editor-down.test.ts", title: "orders the enrichment-injected actions canonically, in the recording and in the file" },
    ],
  },
  {
    id: "single-editor-changes",
    text: "Every enumerated single-editor change, before and after.",
    coverage: [
      { kind: "live", file: LIVE_SINGLE, title: "the single-editor shape is the shape it always was" },
      { kind: "live", file: LIVE_GOLDEN, title: "advertises no targeting parameter, because this is one editor" },
    ],
  },
];

/**
 * The eighteen single-editor changes the plan enumerates, in its order. Each
 * one is a fix, so each one needs an assertion that it happened rather than a
 * baseline saying nothing moved.
 */
export const SINGLE_EDITOR_CHANGES: MatrixCase[] = [
  {
    id: "set-project-moves-both-halves",
    text: "set_project moves path resolution and the socket together (#818).",
    coverage: [
      { kind: "live", file: LIVE_SINGLE, title: "moves path resolution and the socket together, onto a live editor" },
      {
        kind: "engine-free",
        file: "tests/unit/project-switch.test.ts",
        title: "moves path resolution and the live connection to the new project together",
      },
    ],
  },
  {
    id: "set-project-attaches",
    text: "set_project attaches instead of deploying; bridgeSetup comes from attachSummary.",
    coverage: [{ kind: "live", file: LIVE_SINGLE, title: "leaves a project that already has the bridge byte-identical" }],
  },
  {
    id: "attach-no-dangling-entry",
    text: "attach() no longer writes a dangling .uproject entry for an absent bridge.",
    coverage: [
      { kind: "live", file: LIVE_SINGLE, title: "writes no dangling plugin entry into a project that has no bridge" },
      {
        kind: "engine-free",
        file: "tests/unit/deployer-attach.test.ts",
        title: "writes nothing into a project that has no bridge installed",
      },
    ],
  },
  {
    id: "staleness-against-package",
    text: "Staleness is measured against the packaged bridge, so silently stale projects now report it.",
    coverage: [{ kind: "live", file: LIVE_SINGLE, title: "decides staleness for a real deployment instead of always saying fresh" }],
  },
  {
    id: "start-editor-own-project",
    text: "start_editor refuses on its own project, not any editor (#819).",
    coverage: [
      { kind: "live", file: LIVE_SINGLE, title: "refuses to start an editor for a project that already has one" },
      {
        kind: "engine-free",
        file: "tests/unit/editor-lifecycle-target.test.ts",
        title: "startEditor asks for a project instead of scanning the machine",
      },
    ],
  },
  {
    id: "start-editor-mcpport",
    text: "start_editor passes -MCPPort= where the bridge supports it.",
    coverage: [
      {
        kind: "pending",
        planItem: "2.3",
        reason: "startEditor spawns the editor with the project path and nothing else; no -MCPPort argument is passed yet",
      },
      { kind: "live", file: LIVE_SINGLE, title: "hands the editor the port the client resolved" },
    ],
  },
  {
    id: "stop-editor-port-from-record",
    text: "stop_editor resolves this project's port from its record.",
    coverage: [
      { kind: "live", file: LIVE_SINGLE, title: "resolves this project's port from its own record, not from a guess" },
      {
        kind: "engine-free",
        file: "tests/unit/editor-target.test.ts",
        title: "never falls back to the legacy fixed port 9877",
      },
    ],
  },
  {
    id: "stop-editor-through-session-bridge",
    text: "stop_editor routes through the session bridge, so guards can veto.",
    coverage: [
      {
        kind: "pending",
        planItem: "2.3",
        reason:
          "stopEditor still opens its own socket to the resolved port rather than calling through the " +
          "session's guarded bridge, so a guard cannot veto a quit",
      },
      {
        kind: "engine-free",
        file: "tests/unit/guarded-bridge.test.ts",
        title: "a before denial propagates and the inner call never happens",
      },
    ],
  },
  {
    id: "restart-liveness-probe",
    text: "restart_editor stop-failure branch uses a liveness probe.",
    coverage: [
      {
        kind: "engine-free",
        file: "tests/unit/editor-lifecycle-target.test.ts",
        title: "restartEditor asks for a project instead of scanning the machine",
      },
      {
        kind: "cpp",
        reason: "the branch only runs when a stop fails, which means stopping the editor this suite attached to",
      },
    ],
  },
  {
    id: "time-wait-restart",
    text: "A restart inside TIME_WAIT binds and publishes P+1 instead of P.",
    coverage: [
      {
        kind: "cpp",
        reason:
          "a socket option and a bind result inside a process this suite does not own; the client cannot " +
          "observe either (plan item 0.9)",
      },
    ],
  },
  {
    id: "pinned-self-launch",
    text: "A pinned self-launched editor binds the pin once the bridge supports it.",
    coverage: [
      { kind: "live", file: LIVE_SINGLE, title: "hands the editor the port the client resolved" },
      {
        kind: "cpp",
        reason: "the bind itself happens at editor startup, which this suite does not perform",
      },
      {
        kind: "engine-free",
        file: "tests/unit/requested-port.test.ts",
        title: "publishes the pin where the bridge looks for it",
      },
    ],
  },
  {
    id: "owner-checked-port-json",
    text: "port.json writes and deletes become owner-checked.",
    coverage: [
      { kind: "live", file: LIVE_ADDRESSING, title: "stamps the port lockfile with the process that owns it" },
      {
        kind: "cpp",
        reason: "the delete side needs a second editor to quit while the first is live, which needs a process this suite does not own",
      },
    ],
  },
  {
    id: "abi-gate-from-running-bridge",
    text: "The ABI gate and get_status.bridgeApiVersion use the running bridge when connected.",
    coverage: [
      { kind: "live", file: LIVE_SINGLE, title: "reports the version compiled into the binary, not scraped from source" },
      {
        kind: "pending",
        planItem: "0.5",
        reason:
          "project(get_status) still reads readDeployedBridgeApiVersion, which scrapes deployed source and " +
          "reports the newest value while the loaded binary is arbitrarily old",
      },
    ],
  },
  {
    id: "reconnect-port-not-clobbered",
    text: "Reconnect no longer inherits a permanently clobbered port.",
    coverage: [{ kind: "live", file: LIVE_SINGLE, title: "takes the port from the project's published record, not from a stale field" }],
  },
  {
    id: "flow-context-complete",
    text: "Flows receive the full context: elicit, getFlows, getPlugins.",
    coverage: [
      { kind: "live", file: LIVE_SINGLE, title: "hands a flow the whole context, including flows and plugins" },
      {
        kind: "engine-free",
        file: "tests/unit/flow-run-result.test.ts",
        title: "hands the step the accessors the context had, not a rebuilt subset",
      },
    ],
  },
  {
    id: "default-config-categories",
    text: "dist/ue-mcp.default.yml covers 24 categories instead of 19.",
    coverage: [
      {
        kind: "pending",
        planItem: "1.3",
        reason:
          "scripts/generate-default-config.ts still carries its own hand-rolled list of 19 category tools " +
          "instead of reading the live tool graph",
      },
    ],
  },
  {
    id: "instructions-counts",
    text: "Instructions report the real action count and include fab.",
    coverage: [
      { kind: "live", file: LIVE_SINGLE, title: "reports the real action count and the fab category in its instructions" },
      { kind: "live", file: LIVE_GOLDEN, title: "matches the committed baseline" },
    ],
  },
  {
    id: "context-subcommand-parse",
    text: "ue-mcp context <arg> stops being misparsed as a server invocation.",
    coverage: [
      {
        kind: "engine-free",
        file: "tests/unit/doctor.test.ts",
        title: "rejects the context, login and logout subcommands",
      },
    ],
  },
];
