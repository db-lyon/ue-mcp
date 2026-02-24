# UE-MCP: Ontological Foundation Plan

## The Core Insight

The essay's argument applies directly here: we should not try to stuff the AI with a comprehensive dump of UE5's API surface. That's the volume/speed approach — it doesn't compose, it goes stale, and it drowns the model in information it can't structurally reason about.

Instead, we want **minimum sufficient structure**: a stable ontological substrate that lets the AI *reason about* UE concepts and *discover* specifics on demand. The ontology is the skeleton. The MCP tools put flesh on it for each specific task.

This means: **don't extract 40,000 UE types into Kantext. Build the conceptual architecture that lets the AI know what questions to ask and what tools to use to answer them.**

---

## Architecture: Three Layers

```
┌─────────────────────────────────────────────────┐
│  Layer 3: Project Context (stacked per-project) │
│  Modules, conventions, domain concepts, backlog │
├─────────────────────────────────────────────────┤
│  Layer 2: UE Structural Ontology (stable)       │
│  Type system, relationships, state machines,    │
│  workflows, cross-cutting concerns              │
├─────────────────────────────────────────────────┤
│  Layer 1: MCP Operational Ontology (stable)     │
│  Modes, tools, constraints, discovery patterns  │
└─────────────────────────────────────────────────┘
```

Layer 1 and 2 ship with ue-mcp. Layer 3 is composed by each project on top.

The critical design decision: **Layer 2 does NOT try to enumerate UE's API.** It encodes the *shape* of UE's type system — what kinds of things exist, how they relate, what invariants hold — and then delegates to MCP tools for instance-level discovery. The ontology tells the AI "Blueprints have variables, and variables have property flags that control visibility and replication." The AI then uses `list_blueprint_variables` to discover the actual variables in a specific Blueprint.

---

## Phase 1: Introspection Tools (MCP)

Before improving the ontology, give the MCP the ability to *discover* UE's own type system at runtime. This is the mechanical foundation everything else builds on.

### 1a. UE Python Reflection Harvester

Add a live-mode tool that asks the editor about its own type system:

```
reflect_class(className) → {
  parent, interfaces, properties[], functions[],
  specifiers, meta, module
}
```

This single tool replaces the need to hand-author thousands of class definitions. The AI reads the ontology to understand *what a UPROPERTY is and what its specifiers mean*, then calls `reflect_class` to discover *which UPROPERTYs exist on ACharacter*.

The Python side is straightforward — UE's `unreal` module exposes `unreal.find_class()`, field iteration via `dir()` + `get_editor_property`, and class hierarchy via `get_parent_class()`.

Additional reflection tools:

```
reflect_struct(structName)   → fields, size, parent
reflect_enum(enumName)       → values with display names
list_classes(parentFilter?)  → all classes, optionally filtered by parent
list_structs(moduleFilter?)  → all structs
list_gameplay_tags()         → full tag hierarchy from the running editor
```

This is the live-mode equivalent of reading C++ headers, but better — it captures *everything* the engine knows, including types from plugins, generated code, and Blueprint-defined types.

### 1b. C++ Header Parser (Offline)

For offline mode, add a lightweight C++ header scanner. Not a full C++ parser — just regex/AST extraction of UE reflection macros:

```
read_cpp_header(path) → UCLASS/USTRUCT/UENUM declarations,
                         UPROPERTY/UFUNCTION with specifiers,
                         #include dependencies
read_module(moduleName) → Build.cs deps, public headers
```

This gives the AI C++ awareness even without the editor running. The parser only needs to understand UE's macro conventions, not general C++.

### 1c. Config / INI Reader

```
read_config(configName)     → parsed sections and key-value pairs
search_config(key or value) → matches across all config files
```

Trivial to implement — INI parsing is simple. But the impact is large: the AI gains access to physics settings, input bindings, tag hierarchies, rendering config, and plugin settings.

**Phase 1 outcome:** The MCP can discover UE's type system dynamically in both modes. The ontology doesn't need to enumerate types — it needs to teach the AI how to use these discovery tools effectively.

---

## Phase 2: Ontology Restructure

With discovery tools in place, restructure the ontology around *reasoning patterns* rather than *type catalogs*.

### 2a. Rewrite UEConcepts.kant Around the Reflection Model

The current `UEConcepts.kant` lists asset types and property types. This is useful but static. Rewrite it to encode **how UE's type system works** so the AI can reason about any type it discovers:

```kantext
⛩️:
  Meaning@specifier:
    meaning: A UE reflection macro specifier that controls how a declaration is exposed
    purpose: Specifiers are the bridge between C++ and Blueprint. Understanding them is necessary to reason about what the AI can see and do.
    Category: /UE/Reflection

UEReflection:
  SpaceBond@PropertySpecifiers:
    meaning: Specifiers on UPROPERTY that control editor and Blueprint visibility.
    SpaceBond@EditAnywhere:
      meaning: Editable on CDO and per-instance. The most permissive edit specifier.
      🪝 Implies: /UEReflection/PropertySpecifiers/Visible
    SpaceBond@BlueprintReadWrite:
      meaning: Readable and writable from Blueprint graphs.
      constraint: Requires the owning class to be Blueprintable.
    SpaceBond@Replicated:
      meaning: Value is synchronized to clients in multiplayer.
      🪝 Requires: /UEReflection/Networking/ReplicationSetup
```

The key shift: instead of "here is a list of UE types," it's "here is how UE's type system works, so when you discover a type via `reflect_class`, you can interpret what its specifiers mean."

### 2b. Add Workflow Patterns

Encode multi-step task patterns as composable Kantext contexts. These are the causal chains from the essay — "if you want X, the steps are Y₁→Y₂→Y₃ and the tools are Z₁→Z₂→Z₃":

```kantext
Workflows:
  SpaceBond@UnderstandBlueprint:
    meaning: Full comprehension of a Blueprint's architecture and behavior.
    steps: 1. read_blueprint for structure. 2. If parent is C++, reflect_class on the parent to see inherited API. 3. read_blueprint_graph EventGraph for behavior. 4. If it implements interfaces, read each interface. 5. If it has components, understand their types.
    tools: read_blueprint, reflect_class, read_blueprint_graph, read_asset
    
  SpaceBond@AddFeatureToBlueprint:
    meaning: Add new behavior to an existing Blueprint.
    steps: 1. read_blueprint to understand current state. 2. add_blueprint_variable for any new state. 3. add_blueprint_node for logic. 4. connect_blueprint_pins to wire it. 5. compile_blueprint. 6. If errors, read errors and fix. 7. save_asset.
    constraint: Requires live mode. Always compile after structural changes.
    tools: read_blueprint, add_blueprint_variable, add_blueprint_node, connect_blueprint_pins, compile_blueprint, save_asset
```

These aren't documentation — they're structured context that composes with the rest of the ontology. When the AI stacks a workflow pattern, it has both the conceptual skeleton (what are Blueprints, what are variables) and the operational skeleton (what steps to take, what tools to call).

### 2c. Cross-Cutting Traits

Add traits that cut across the type taxonomy:

```kantext
⛩️:
  Meaning@trait:
    meaning: A capability or characteristic that applies across multiple asset types
    Category: /UE/Trait

Traits:
  SpaceBond@HasVisualGraph:
    meaning: This asset type has a node-based visual editor.
    applies_to: Blueprint, AnimBlueprint, Material, Niagara, BehaviorTree
    implication: The AI can use graph-reading tools to understand its logic.

  SpaceBond@Placeable:
    meaning: Instances of this type can be placed in levels.
    applies_to: Actor Blueprints, StaticMesh, SkeletalMesh (via actor)
    implication: The AI can reason about level composition.

  SpaceBond@Replicable:
    meaning: This type participates in network replication.
    applies_to: Actor subclasses, ActorComponent subclasses
    implication: Changes to this type may have multiplayer consequences.
```

### 2d. Discovery-Oriented Tool Linking

Restructure `McpSurface.kant` so tools are linked to the concepts they discover, not just described:

```kantext
ToolSurface:
  SpaceBond@read_blueprint:
    meaning: ...
    🪝 Discovers: /BlueprintAnatomy
        connection: discovers
        meaning: Returns instances of the concepts defined in BlueprintAnatomy.
    🪝 FollowUp: /Workflows/UnderstandBlueprint
        connection: part_of
```

This turns the tool catalog into a navigation structure. The AI doesn't just know "read_blueprint exists" — it knows "read_blueprint discovers BlueprintAnatomy concepts, and is part of the UnderstandBlueprint workflow."

---

## Phase 3: Dynamic Ontology Composition

This is where the essay's hybrid vision lands: a stable ontological substrate combined with dynamic, instance-level context from tools.

### 3a. Tool Results as Composable Kantext

When a tool returns data, that data should be composable into the ontological context. Not literally — we're not going to `compose()` every tool result into Kantext. But the *shape* of tool results should mirror the ontology structure so the AI can mentally slot results into the right conceptual location.

This means tool output schemas should be designed to match the ontology:
- `read_blueprint` returns `{ className, parentClass, variables, functions, graphs, components }` — these map 1:1 to `BlueprintAnatomy` concepts
- `reflect_class` returns `{ parent, properties, functions, specifiers }` — these map to `UEReflection` concepts

The ontology acts as a decoder ring for tool output.

### 3b. Project-Specific Layer Template

Ship a template `.kant` file that projects copy and fill in:

```kantext
# MyProject.kant — stack on top of UE-MCP's foundation

MyProject:
  SpaceBond@Identity:
    meaning: <describe your project>
    engine: Unreal Engine 5.x
    language: <C++, Blueprint, or hybrid>

  SpaceBond@Conventions:
    meaning: Naming conventions and path patterns.
    # Fill in your prefixes, directory structure, etc.

  SpaceBond@Constraints:
    meaning: Invariant rules for this project.
    # Fill in: C++ vs Blueprint policy, coding standards, etc.

  SpaceBond@Modules:
    meaning: Project module structure.
    # Declare your modules and their dependencies (like ValeModules.kant)
```

This is the composability argument from the essay: the UE ontology, the MCP surface, and the project context are separate layers that stack cleanly via Kantext's k-way merge.

### 3c. Grounding

Use Kantext's `seal_ground` / `verify_ground` to create verifiable snapshots of the ontology. When the UE ontology is stable, seal it. When a project stacks its context on top, that composition can be verified against the sealed ground. This gives you:
- Confidence that the ontological substrate hasn't drifted
- A way to detect when UE version changes invalidate parts of the ontology
- Audit trail for ontology evolution

---

## Phase 4: Close the Remaining MCP Gaps

With the ontological foundation solid and the discovery tools in place, build out the remaining MCP tools guided by the ontology's structure. Each new tool should:

1. Have a concept in the ontology that it discovers or operates on
2. Be linked to workflows that use it
3. Have its mode constraints (offline/live) and mutability declared

Prioritized by the ontology's own structure:
- Animation tools (the ontology says AnimBlueprints have state machines — now build the tool)
- Material tools (the ontology says Materials have node graphs — now build the tool)
- Level tools (the ontology says Levels contain placed actors — now build the tool)
- Dependency graph tools (the ontology says assets have hard/soft references — now build the tool that walks them)
- Bulk and diff tools (operational efficiency, driven by workflow patterns)

---

## Execution Order

```
Phase 1a  reflect_class + live reflection tools    ← highest leverage single feature
Phase 1c  config/INI reader                        ← trivial to build, high impact
Phase 2a  rewrite UEConcepts around reflection     ← ontology matches new capabilities
Phase 2b  workflow patterns                        ← AI knows what to do, not just what exists
Phase 1b  C++ header parser for offline            ← offline parity with live reflection
Phase 2c  cross-cutting traits                     ← structural refinement
Phase 2d  discovery-oriented tool linking          ← ontology becomes navigable
Phase 3a  tool output alignment                    ← ontology as decoder ring
Phase 3b  project template                         ← enable adoption
Phase 3c  grounding                                ← stability guarantees
Phase 4   remaining MCP tools                      ← fill gaps guided by ontology
```

The principle throughout: **build the ontology to teach reasoning, build the tools to enable discovery, and let the two reinforce each other.** The AI should never need to have 40,000 UE types in context. It needs to understand the *shape* of UE's type system and have tools to query specifics on demand. That's the ontological alternative the essay describes — minimum sufficient structure that enables correct reasoning.
