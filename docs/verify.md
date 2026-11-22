---
description: "Commands that check ue-mcp's published numbers on your own machine: the advertised tool surface of any MCP server, the test suites, and an editor task benchmark scored by Unreal Python."
---

# Verify the claims

Every number ue-mcp publishes can be checked without trusting this site. This page gives the commands. The surface measurement and the task benchmark apply the same rules to any MCP server, so you can run them against ue-mcp and the alternatives side by side.

| Claim | How to check it | Needs Unreal |
|---|---|---|
| Tool, action and schema counts | [`ue-mcp surface`](#1-measure-any-servers-advertised-surface) | No |
| Comparison grades | [Per-cell sources in compare.md](#2-check-the-comparison-grades) | No |
| Test coverage | [Unit, integration and live suites](#3-reproduce-the-test-results) | Live suite only |
| Editor tasks work | [Task benchmark](#4-run-the-task-benchmark) | Yes, UE 5.7 or later |

## 1. Measure any server's advertised surface

`ue-mcp surface` starts an MCP server, sends `initialize` and `tools/list`, and prints what the server hands a client at startup:

```bash
npx ue-mcp surface -- <command that starts the server> [its arguments]
npx ue-mcp surface --url http://127.0.0.1:8000/mcp        # a streamable HTTP server
npx ue-mcp surface --json -- <command...>                  # machine-readable, with per-tool rows
```

`surface` ships in the first ue-mcp release after 1.4.16-beta.1. Until that release is on npm, run it from a clone: `npm ci`, then `npx tsx src/index.ts surface -- <command...>`.

Flags: `--env KEY=VALUE` (repeatable) sets a variable for the started server, `--timeout <seconds>` (default 180, which leaves room for a first `npx` or `uvx` download) and `--per-tool` adds a row per tool to the text output. It needs no Unreal install and starts no editor.

What it counts:

- **Tools**: the length of the `tools/list` answer, across every page.
- **Actions**: a tool whose input schema has a top-level `action`, `operation` or `command` parameter with enumerated values counts one action per value. The first of those three names that carries values is used, and values are read from `enum`, `const`, and the `enum` or `const` of each `anyOf` or `oneOf` branch. Every other tool counts as one action.
- **Input schemas**: the bytes of every tool's `inputSchema` as compact JSON, and an estimate of tokens at four characters per token. The true token count depends on the model's tokenizer.
- **tools/list**: the same for the whole answer, names and descriptions included, which is closer to what a client puts in the model's context.

The action rule only sees what the schema enumerates. A server that takes the action name as a free-form string, or puts its actions behind a search or describe tool, counts one per such tool. ue-mcp's default `micro` strategy is one gateway tool and counts as five actions by this rule; its `full` strategy enumerates every action. Read the action count together with the server's own documentation of what sits behind a gateway.

A server that runs inside the editor (an HTTP endpoint the editor plugin opens) answers `tools/list` only while the editor runs. Start the editor, then measure it with `--url`.

### Worked examples

Measured on 2026-11-21 on Windows 11 with Node 26, with no editor running. Each command can be rerun as written; a later version of a server can give different numbers.

| Server and version | Command | Tools | Actions | Input schemas | tools/list |
|---|---|---|---|---|---|
| ue-mcp at commit `cf0f3d2e` (1.4.16-beta.1), default `micro` | `npx ue-mcp surface -- npx -y ue-mcp` | 1 | 5 | 491 B, ~123 tokens | 745 B, ~187 tokens |
| ue-mcp at commit `cf0f3d2e` (1.4.16-beta.1), `full` | `npx ue-mcp surface --env UE_MCP_CONTEXT_STRATEGY=full -- npx -y ue-mcp` | 32 | 3,727 | 109,907 B, ~27,477 tokens | 375,144 B, ~93,786 tokens |
| @runreal/unreal-mcp 0.1.4 | `npx ue-mcp surface -- npx -y @runreal/unreal-mcp@0.1.4` | 20 | 20 | 5,305 B, ~1,327 tokens | 11,897 B, ~2,975 tokens <!-- lint-prose-allow: competitor --> |
| unreal-engine-mcp-server 0.6.0 (EngineWire stdio proxy) | `npx ue-mcp surface -- npx -y unreal-engine-mcp-server@0.6.0` | 1 | 4 | 140 B, ~35 tokens | 378 B, ~95 tokens |
| GenOrca/unreal-mcp at commit `f7986db` (reports UnrealMCP 1.9.3) | `npx ue-mcp surface -- uv --directory <clone>/mcp-server run --python 3.12 src/unreal_mcp/main.py` | 21 | 21 | 7,167 B, ~1,792 tokens | 14,683 B, ~3,671 tokens <!-- lint-prose-allow: competitor --> |

The ue-mcp rows were measured from the source at that commit. The last row uses the start command from that server's README, run in a clone checked out at that commit; `uv sync --python 3.12` beforehand installs its locked dependencies.

## 2. Check the comparison grades

[ue-mcp.com/compare.md](https://ue-mcp.com/compare.md) is the comparison matrix as Markdown. Every column lists the public documentation or repository it was graded from, and each cell that rests on a specific source or caveat names it below its table. A grade is `yes` only for a dedicated typed tool; reaching a feature through a server's generic Python tool is `partial`. To check a grade, open the column's source and look for the tool.

## 3. Reproduce the test results

From a fresh clone, with Node 20 or later and no Unreal install:

```bash
git clone https://github.com/db-lyon/ue-mcp.git
cd ue-mcp
npm ci
npm test
```

`npm test` runs the unit suite (`tests/unit`), the multi-editor suite and the integration suite (`tests/integration`), which spawns the real CLI, server and daemon in sandboxes with local fakes for the editor bridge and every network service.

Behavioural coverage is an inventory of which tests exercise each bridge action:

- `tests/coverage/behavioural-coverage.json` maps every registered bridge action to the live and C++ automation tests that dispatch it and assert on its effect. Its `summary` block gives the totals: `node -e "console.log(require('./tests/coverage/behavioural-coverage.json').summary)"`.
- `npx vitest run tests/unit/behavioural-coverage.test.ts` fails when that inventory is out of date or an action has no test and no recorded exemption. Exemptions and their reasons are in `tests/coverage/behavioural-exemptions.json`.
- The inventory shows that a test exists and what it asserts. The tests themselves need an editor; the live suite below runs them.

CI runs the type check, the unit and integration suites and the release gates on every pull request before it can merge: [CI workflow runs](https://github.com/db-lyon/ue-mcp/actions/workflows/ci.yml?query=event%3Apull_request). A push to `main` only publishes the package that pull request tested.

With Unreal Engine installed (the test project `tests/ue_mcp` targets 5.9; set `UE_MCP_TEST_ENGINE_ROOT` to build and run against another 5.x engine):

```bash
node scripts/deploy.mjs          # copy the bridge plugin into tests/ue_mcp
npm run build                    # compile the plugin and the test project
npm run test:live                # every file in tests/live, against editors it launches on copies of tests/ue_mcp
npm run test:automation          # the plugin's own UE.MCP.* tests, inside a running editor on tests/ue_mcp
```

`npm run test:live` launches its own offscreen editors and closes them when it finishes. Run it only on `tests/ue_mcp`, never on a project you care about: the tests create, change and delete assets.

## 4. Run the task benchmark

`benchmark/` holds 11 editor tasks covering the matrix's main areas. Each task file (`benchmark/tasks/*.md`) has an exact prompt to give an agent, and each check (`benchmark/checks/*.py`) reads the result through Unreal's own Python API and prints one `PASS` or `FAIL` line. The checks never call an MCP server, so they score every server the same way.

| Task | Area |
|---|---|
| 01 Blueprint function graph | Blueprint graph authoring |
| 02 Blueprint components | Blueprint components |
| 03 Material graph | Material graph authoring |
| 04 Material instance | Materials |
| 05 Level and actors | Levels, actors and components |
| 06 Sequencer animation | Animation (Sequencer) |
| 07 Landscape | Landscape |
| 08 PCG scatter | PCG |
| 09 Rollback of a failed multi-step edit | Safety (rollback) |
| 10 Delete a referenced asset | Safety (blocking dialogs) |
| 11 Play In Editor | PIE |

To score a server:

1. Create a project from the Blank template in UE 5.7 or later, with no starter content, or use `tests/ue_mcp`. Nothing may exist under `/Game/Bench`. Make sure the Python Editor Script Plugin is enabled.
2. Connect your agent to the server you are testing, with the project open in the editor.
3. Give the agent the prompt from each task file, in order, in one editor session. Tasks 04 to 09 and 11 build on earlier ones. Leave the PIE session from task 11 running.
4. Score the editor:

```bash
python benchmark/run_checks.py --engine "C:/Program Files/Epic Games/UE_5.7"
```

`run_checks.py` sends each check to the editor through the Python plugin's remote execution, which is off by default. Turn it on in Project Settings > Plugins > Python > Enable Remote Execution, or start the editor with `-ini:Engine:[/Script/PythonScriptPlugin.PythonScriptPluginSettings]:bRemoteExecution=True`. With more than one editor running, name the one to score with `--project <name or project folder>`. `--only 01,03` runs some checks, and `--json` prints the results as JSON.

Without remote execution, open the Output Log, set its console to Python, and paste each check file; or run `py "<path to the check>"` from the Cmd console. Each prints its verdict.

The checks see only the final editor state. Read the agent's transcript too: it shows whether the server did the work, or the agent did it another way (through a Python tool, for example).

### ue-mcp result

11 of 11 checks passed on 2026-11-21, on UE 5.9 with a hidden offscreen editor on `tests/ue_mcp`, at ue-mcp commit `cf0f3d2e` plus this benchmark. The tasks were done by direct calls to ue-mcp's own actions from a script, not by an agent following the prompts, so this run shows the actions produce the required state. It does not measure how well an agent picks them. Two tasks needed a second attempt:

| Task | Actions used | Notes |
|---|---|---|
| 01 | `blueprint(create, create_function, add_function_parameter, add_node, connect_pins_batch, compile)` | |
| 02 | `blueprint(create, add_component, set_component_property, compile)` | |
| 03 | `material(create, add_expression, connect_to_property, recompile)`, `asset(save)` | |
| 04 | `material(create_instance, set_parameter)`, `asset(save)` | |
| 05 | `level(create, place_actor, attach_actor, set_component_property, save)` | |
| 06 | `editor(create_sequence, add_sequence_section, set_sequence_keyframes)`, `asset(save)` | The first `set_sequence_keyframes` call passed keys as `frame`, which that action does not read; it wrote both keys at time 0 without an error. A second call with `seconds` was correct. |
| 07 | `landscape(create, sculpt)`, `level(save)` | |
| 08 | `pcg(create_graph, add_node, set_node_settings, set_static_mesh_spawner_meshes, connect_nodes, add_volume, execute)`, `level(save)` | `add_node` refused the short names `VolumeSampler` and `StaticMeshSpawner` and suggested the settings class names, which worked. |
| 09 | `level(place_actor)`, `flow(run)` with `rollback_on_failure: true` | The flow's third step failed, and its two inverses removed Bench_Rollback. |
| 10 | `material(create, create_instance)`, `asset(delete, force=true)`, `editor(list_dialogs)` | `asset(delete)` without `force` refused and listed the referencer. No dialog was left open. |
| 11 | `editor(play_in_editor)`, `level(move_actor, world=pie)` | |

## What this does not show

- The surface numbers are what a server advertises, not whether its actions work.
- The ue-mcp benchmark result above was produced by ue-mcp's maintainers. Run it yourself, with your agent, against the servers you are choosing between.
- The checks have been run on UE 5.9 only, not yet on 5.7 or 5.8.
- Eleven tasks sample each area; they do not cover every action in it.
