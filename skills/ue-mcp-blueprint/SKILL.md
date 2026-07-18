---
name: ue-mcp-blueprint
description: Use when authoring or modifying Unreal Blueprint assets through ue-mcp. Covers the read-then-write discipline, node/pin wiring, component (SCS) hierarchy, variables, interfaces, compilation, and the difference between pin defaults and linked inputs. Pulls in any time the user asks to create, edit, or inspect a Blueprint.
---

# ue-mcp blueprint authoring

The `blueprint` tool covers reading, authoring, and compiling Blueprints. The default workflow is **read → mutate → compile**, never fire-and-forget.

## Authoring a graph body: prefer the Epic DSL (#711)

When you are authoring the **contents of a graph** (event graph, function body, macro) - a set of nodes plus their wiring - do **not** default to node-by-node `add_node`/`connect_pins`. Reach for Epic's graph DSL first:

1. `blueprint(action="epic_get_graph_dsl_docs")` - read the S-expression grammar once.
2. `blueprint(action="epic_write_graph_dsl", ...)` - author + compile the whole graph body in one call.

The DSL authors and compiles an entire graph in a single round-trip, which is materially faster and more reliable than stitching individual K2Nodes together (one correct pass vs several failed iterations). Use it for graph bodies whenever it is available.

**Availability / fallback.** The `epic_*` actions come from Epic's ToolsetRegistry and require **UE 5.8+ with the Epic toolset plugins enabled**. Check with `epic(action="status")`. When they are unavailable (pre-5.8, or the registry is off), fall back to the native node path below.

**Keep using the native actions for** read/discovery, SCS components, CDO/class defaults, interfaces + event dispatchers, structured `compile`/`validate`, and anything with no Epic equivalent - the native path adds idempotency and rollback the raw tools lack. See the `ue-mcp-epic-routing` skill for the full epic-vs-native decision.

## Discovery before authoring

For any existing Blueprint:

- `blueprint(action="read", assetPath=...)` — structure (parent, components, graphs)
- `blueprint(action="read_graph_summary", assetPath=..., graphName=...)` — lightweight node+edge summary (~10KB) — use this before `read_graph` (which can be 100KB+)
- `blueprint(action="list_graphs", assetPath=...)` — every graph in the BP including event graphs, functions, macros, interface impls
- `blueprint(action="list_variables" | "list_functions" | "list_local_variables")` — the member surface
- `blueprint(action="get_execution_flow", assetPath=..., entryPoint=...)` — trace exec pins from an entry point
- `blueprint(action="get_dependencies", assetPath=..., reverse=false|true)` — classes/functions/assets this BP uses, or callers if `reverse: true`

## Mutation recipe

1. **Create the skeleton** — `blueprint(action="create", assetPath, parentClass?)`.
2. **Add variables + components** — `add_variable`, `add_component` (pass `parentComponent` for SCS hierarchy).
3. **Build graphs** — `add_node` each K2Node, `set_node_property` for pin defaults, `connect_pins` to wire exec + data.
4. **Compile** — `blueprint(action="compile", assetPath)`. Compilation errors come back in the result; fix them before proceeding.

## Node wiring fundamentals

- `add_node` takes `nodeClass` as a K2Node class short name (`K2Node_CallFunction`, `K2Node_VariableGet`, `K2Node_DynamicCast`, `K2Node_IfThenElse`, etc.) plus `nodeParams` for class-specific fields (`FunctionReference`, `VariableReference`, `TargetType`).
- Pin defaults vs linked values: `set_node_property` writes a literal default onto a pin; `connect_pins` wires a pin to another node's output. A literal default is ignored once a pin is linked.
- `read_node_property` reads either a pin default or a reflected node UPROPERTY — use this to verify the pin was actually set before compiling.
- Graphs with duplicate names (rare but possible after rename) can be disambiguated by passing `graphIndex` alongside `graphName`.

## Components (SCS)

- `add_component` creates a node in the Simple Construction Script. Pass `parentComponent` to put the new component under an existing parent — otherwise it becomes a top-level child of the scene root.
- `set_component_property` writes on the child BP's InheritableComponentHandler override template, not on the parent — the parent stays untouched. This matters when editing inherited components.
- `read_component_properties` dumps every UPROPERTY on the template, including array contents.
- `reparent_component` moves an SCS node to a new parent.

## CDO (class defaults)

- `set_class_default` writes a UPROPERTY on the Blueprint CDO (the class default object). For actor tick settings specifically, use `set_actor_tick_settings` — it handles `bCanEverTick`, `bStartWithTickEnabled`, `TickInterval` in one call.

## Interfaces + event dispatchers

- `create_interface` + `add_interface` — the implement-side.
- `add_event_dispatcher` — fires a multicast delegate from the BP.

## Verify before compile

- `blueprint(action="validate")` runs the compiler diagnostics without saving. Cheaper round-trip when iterating.
- `blueprint(action="read_graph_summary")` after mutations confirms the graph shape.
