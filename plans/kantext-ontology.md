# Kantext ontology integration

## Thesis

Restructure ue-mcp's knowledge surface around context as a first-class data type, using kantext (https://kantext.dev) as the composition substrate. Not a search upgrade. A re-anchoring of how tool metadata, reflection, asset state, project intent, and agent feedback compose into a single queryable view.

## Why this, not tree-sitter / FTS5

- UE reflection is already path-shaped. It deserves an address space (`/UE/Classes/UCharacter/MovementComponent/MaxWalkSpeed`), not a grep index.
- Our 425 handlers have no queryable metadata. Classification, approval, required plugins, known workarounds - all implicit today. Kantext's Mediation Registry + Signals model is purpose-built for this.
- Engine-version deltas, user project subclasses, and repo-local policy overrides are naturally *layers*. Kantext's k-way composer is the right primitive.
- Git-sealed composition gives us session-level reproducibility of the context agents reasoned over.
- The "no lists / meaning-fused" discipline matches how UCLASS reflection already describes itself.

## Integration pattern: source / sink / composer

```
     UE Editor (live via C++ bridge)
                  │
                  ▼
     ┌─────────────────────────────┐
     │  ue-mcp Projectors          │   emits .kant
     └──────────┬──────────────────┘
                ▼
     ┌─────────────────────────────┐
     │  Layer stack on disk        │
     │  projected / repo / dist    │
     └──────────┬──────────────────┘
                ▼
     ┌─────────────────────────────┐
     │  kantext (sidecar)          │
     │  parse / compose / HQL      │
     └──────────┬──────────────────┘
                ▼
     ontology(selector) tool
     + handler arg resolvers
     + flowkit audit sink
```

### Three layer sources

| Layer | Source | Lifecycle | Priority |
|---|---|---|---|
| Projected | live UE state | ephemeral, regenerated on events | ground |
| Repo-local | hand-authored, git-tracked | stable, versioned | overrides |
| Distributed | shipped with ue-mcp | fixed per release | defaults |

Composition priority: repo-local > projected > distributed.

### Ownership split

| Concern | Owner |
|---|---|
| `.kant` parsing, k-way merge, HQL, blake3, git-sealing | kantext |
| UE kernel vocabulary, distributed `.kant` library | co-authored with Jason |
| Live reflection / asset registry / handler registry -> `.kant` | ue-mcp |
| Repo-local `.kant` conventions | ue-mcp |
| Handler arg resolution via ontology | ue-mcp |
| Flow audit fragment emission | ue-mcp |
| MCP tool surface | ue-mcp |

ue-mcp never parses or composes `.kant`. Emit only. Clean boundary.

## Projectors

First-class concept, peer to handlers. Each owns a path prefix under `/UE`:

| Projector | Path prefix | Refresh trigger |
|---|---|---|
| HandlerRegistry | `/UE/Mediation/Registry/Tools` | startup, handler add/remove |
| Class | `/UE/Classes` | hot reload, module load |
| Module | `/UE/Modules` | module registry change |
| Plugin | `/UE/Plugins` | startup |
| Asset | `/UE/Assets` | asset registry events |
| GameplayTag | `/UE/Project/GameplayTags` | tag manager change |
| AttributeSet | `/UE/Project/Attributes` | reflection over UAttributeSet subclasses |
| Flow | `/UE/Audit/Flows` | flow run completion |

Interface:

```ts
interface Projector<TInput> {
  readonly basePath: string;
  readonly triggerEvents: readonly ProjectorEvent[];
  project(input: TInput): KantFragment;
}
```

## Query surfaces

Two, not one.

1. **`ontology(selector)`** MCP tool - raw HQL over the composed view. Discovery, introspection.
2. **Handler argument resolution** - handlers resolve class/tag/asset refs through ontology before bridge dispatch. Eliminates silent-mismatch failures.

Mutation stays through handlers. HQL is for reasoning.

## Sealing

Not per-mutation. Event-driven snapshots to `.ue-mcp/ontology/`:
- engine version bump
- major asset batch
- flow completion
- handler version change

Agents can pin a snapshot and replay their reasoning view against a fixed commit.

## Flowkit fusion

Flows emit audit fragments at `/UE/Audit/Flows/{flowId}/{taskId}`: inputs, outputs, affected assets. Sealed to the post-flow commit. Agents querying the ontology see the record as HQL, not a separate logging system. This is where Jason's two projects (cumulusci lineage via flowkit + kantext) meet structurally.

## What we do NOT do

- Do not require users to hand-write `.kant` for the kernel. Projected.
- Do not replace the MCP tool surface with raw HQL. Mutation stays handler-driven.
- Do not try to express the entire editor as `.kant`. The ontology is what agents *reason over*. The bridge still executes.

## Open questions for Jason

1. Embedding: sidecar CLI (preferred for loose coupling) vs. napi-rs binding vs. reimplement a minimal TS composer.
2. Dual-layer sealing: kantext assumes static files; UE has a running editor. Projected layers are regenerated, repo-local layers are committed. Does the composer need hints about which layers are ephemeral?
3. `/UE` as a blessed top-level namespace in the kantext ecosystem, co-designed kernel vocabulary.

## First vertical slice (this branch)

- [x] Branch + plan
- [ ] Kernel `.kant` draft: `/UE/Mediation/Registry` vocabulary (Meaning + Space)
- [ ] `src/ontology/` scaffolding: types, projector interface, emitter
- [ ] HandlerRegistryProjector: emits all 425 actions as points with classification / approval / risk Signal placeholders
- [ ] `ontology` MCP tool: `project_all` and `list_layers` actions (query deferred until kantext engine wired)
- [ ] Unit test: projector produces parseable output and expected path set

Query (HQL pass-through) and sealing come in the next slice, after Phase 0 alignment with Jason.
