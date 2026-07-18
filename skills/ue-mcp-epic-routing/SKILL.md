---
name: ue-mcp-epic-routing
description: Use when deciding between ue-mcp's native category actions and Epic's wrapped ToolsetRegistry tools (the `epic_*` actions, incl. the Blueprint graph DSL) for a task in Unreal. Pulls in when authoring Blueprint graph bodies, or any time both a native action and an epic_* action could do the job and you need to pick.
---

# Epic-vs-native tool routing (ue-mcp)

ue-mcp exposes two overlapping surfaces. Picking the right one per task avoids slow, failure-prone paths.

- **Native category actions** - `blueprint(...)`, `level(...)`, `material(...)`, etc. Hand-written, idempotent, with rollback and structured errors.
- **Epic `epic_*` actions** - Epic's own MCP toolsets, surfaced in-category when the ToolsetRegistry is available (UE 5.8+ with the Epic toolset plugins enabled). Discover them with `epic(action="status")` and `epic(action="list_toolsets")`.

This is **not** a blanket "always use epic" rule. Epic needs 5.8 + plugins, and much of ue-mcp has no epic equivalent or adds idempotency/rollback the raw tools lack.

## The one that matters most: Blueprint graph bodies -> the DSL

Authoring the **contents of a graph** (event graph, function body, macro) is the headline case.

- Prefer `blueprint(action="epic_write_graph_dsl", ...)`. Call `blueprint(action="epic_get_graph_dsl_docs")` first to get the S-expression grammar.
- It authors **and compiles the whole graph in one call** - materially faster and more reliable than node-by-node `add_node` + `connect_pins` (typically one correct pass instead of several failed iterations).

## Decision table

| Task | Route |
|------|-------|
| Author/replace a graph body (nodes + wiring) | `blueprint(epic_write_graph_dsl)` (docs first) |
| Read / inspect a graph, list graphs/variables/functions | native `blueprint(read*, list_*, get_execution_flow)` |
| SCS components, reparenting, component properties | native `blueprint(add_component, ...)` |
| CDO / class defaults, tick settings | native `blueprint(set_class_default, set_actor_tick_settings)` |
| Interfaces, event dispatchers | native `blueprint(create_interface, add_interface, add_event_dispatcher)` |
| Structured compile / validate with diagnostics | native `blueprint(compile, validate)` |
| A task with no native action but an `epic_*` one exists | the `epic_*` action |
| A task with no epic equivalent | the native action |

## Fallback

If `epic(action="status")` reports the registry unavailable (pre-5.8, plugins off, or `available=false`), route **everything** through the native path - the DSL actions will not exist. The native node-by-node path (`add_node` -> `set_node_property` -> `connect_pins` -> `compile`) always works; see the `ue-mcp-blueprint` skill.
