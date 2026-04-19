# Ontology subsystem

## Thesis

Restructure ue-mcp's knowledge surface around context as a first-class data type, using the [cairn](../../cairn) context engine as the composition substrate. Not a search upgrade. A re-anchoring of how tool metadata, reflection, asset state, project intent, and agent feedback compose into a single queryable view.

## Why this shape

- UE reflection is already path-shaped. It deserves an address space (`/UE/Classes/UCharacter/MovementComponent/MaxWalkSpeed`), not a grep index.
- Our 425 handlers have no queryable metadata. Classification, approval, required plugins, known workarounds - all implicit today. A Mediation Registry point with Signals is purpose-built for this.
- Engine-version deltas, user project subclasses, and repo-local policy overrides are naturally *layers*. A k-way composer is the right primitive.
- Git-tracked repo-local layers give us session-level reproducibility of the context agents reasoned over.
- The no-lists / meaning-fused discipline matches how UCLASS reflection already describes itself.

## Architecture: source / sink / composer

```
     UE Editor (live via C++ bridge)
                  │
                  ▼
     ┌─────────────────────────────┐
     │  ue-mcp Projectors          │   emits .cairn
     └──────────┬──────────────────┘
                ▼
     ┌─────────────────────────────┐
     │  Layer stack on disk        │
     │  projected / repo / kernel  │
     └──────────┬──────────────────┘
                ▼
     ┌─────────────────────────────┐
     │  cairn                      │
     │  parse / compose / select   │
     └──────────┬──────────────────┘
                ▼
     ontology(selector) MCP tool
     + handler arg resolvers
     + flowkit audit sink
```

### Three layer sources

| Layer | Source | Lifecycle | Priority |
|---|---|---|---|
| Projected | live UE state | ephemeral, regenerated on events | 1 |
| Repo-local | hand-authored, git-tracked | stable, versioned | 2 (wins) |
| Kernel | shipped in `ontology/kernel/*.cairn` | fixed per release | 0 |

Composition priority: repo-local > projected > kernel.

### Ownership split

| Concern | Owner |
|---|---|
| `.cairn` parsing, k-way merge, path selectors, Registry base class | cairn |
| `/UE` kernel vocabulary, distributed `.cairn` library | ue-mcp |
| Live reflection / asset registry / handler registry -> `.cairn` | ue-mcp |
| Repo-local `.cairn` conventions | ue-mcp |
| Handler arg resolution via ontology | ue-mcp |
| Flow audit fragment emission | ue-mcp |
| MCP tool surface | ue-mcp |

ue-mcp never parses or composes `.cairn` directly. Emit only (via projectors). Clean boundary.

## Projectors

First-class concept, peer to handlers. Each owns a path prefix under `/UE`:

| Projector | Path prefix | Refresh trigger |
|---|---|---|
| HandlerRegistry | `/UE/Mediation/Registry/Tools` | startup, handler add/remove |
| Plugins | `/UE/Plugins/Catalog` | startup, manual |
| EngineSymbols | `/UE/Engine/Symbols` | manual (expensive) |
| ProjectConfig | `/UE/Project/Config` | startup, manual |
| Workarounds | `/UE/Audit/Workarounds` | manual, flow-completed |
| Invocations | `/UE/Audit/Invocations` | manual, flow-completed |
| Future: Classes, Assets, GameplayTags, AttributeSets, FlowAudit | various | bridge-driven |

Interface:

```ts
interface Projector<TInput> {
  readonly name: string;
  readonly basePath: string;
  readonly triggerEvents: readonly ProjectorEvent[];
  project(input: TInput): EmittedFragment;
}
```

## Query surfaces

Two, not one.

1. **`ontology(action="query", selector="...")`** MCP tool. Raw path selectors over the composed view. Discovery, introspection.
2. **Handler argument resolution.** Handlers route class/tag/asset refs through ontology resolution before bridge dispatch. Eliminates silent-mismatch failures.

Mutation stays through handlers. Selectors are for reasoning.

## Preflight and auto-directive

- `OntologyRegistry.checkRequires(tool, action)` consults `/UE/Plugins/Catalog` before dispatch. Missing/disabled deps abort with `REQUIRES_UNMET`.
- `OntologyRegistry.resolveApproval(tool, action)` reads the declared approval marker. Explicit-approval actions automatically prepend a mandatory directive to the MCP response.

## Flowkit fusion

flowkit tasks dispatch through the same MCP category-tool surface. The InvocationProjector captures every dispatch at `/UE/Audit/Invocations/Log/entry_NNNNNN`. Flow runs land as first-class audit entries alongside direct agent calls. Query session history with a selector rather than a bespoke logging API.

## What we do NOT do

- Do not require users to hand-write `.cairn` for the kernel. All projected.
- Do not replace the MCP tool surface with raw selectors. Mutation stays handler-driven.
- Do not try to express the entire editor as `.cairn`. The ontology is what agents *reason over*. The bridge executes.

## Status

Shipped on `feat/cairn-ontology`. 61/61 unit tests green. Six projectors live. Declared metadata on ActionSpec drives dispatch-layer enforcement. Kernel + projected + repo-local layers compose via cairn.

Future work (requires bridge-driven projectors):
- ClassProjector, AssetProjector, GameplayTagProjector, AttributeSetProjector
- Handler argument resolution pipeline proven on a real action
- Integration test of the full MCP dispatch with preflight + auto-directive
- Per-field provenance tracking (which layer contributed each field)
