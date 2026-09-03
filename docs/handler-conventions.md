# Handler Conventions

How mutating C++ handlers participate in **idempotency** (safe replay) and **rollback** (failure recovery).

## Why

Flows mutate editor state. When a flow fails partway, the user wants two guarantees:

1. **Rerun is safe** - running the same flow again doesn't duplicate work or explode on "already exists" errors.
2. **Failure is recoverable** - the user can opt into automatic rollback that undoes completed mutations in reverse order.

Both are properties of each individual handler. The runner coordinates across handlers; each handler decides what the natural key is, how to detect existing state, and what the inverse operation looks like.

## The contract

Every mutating handler (create, modify, delete) follows this shape:

### Natural key

Each handler accepts a parameter identifying the entity it operates on. Examples:

| Entity | Natural key param |
|---|---|
| Actor | `actorPath`, or `actorLabel` (or `label` shorthand on creates) |
| Asset (material, texture, mesh, datatable…) | `assetPath` or `path` |
| Blueprint variable | `blueprintPath` + `variableName` |
| Blueprint function | `blueprintPath` + `functionName` |
| Component | parent + `componentName` |
| Material parameter | `materialPath` + `parameterName` |

Handlers without a natural key (e.g., `execute_command`, `shell`) **cannot** be idempotent or reversible - document them as such, do not emit rollback records.

#### Selecting an actor

Editor labels are **not unique**. A copy-pasted Blueprint gives every copy the
same label, so a lookup that answers with the first actor the level iterator
reached is a coin flip decided by streaming order. That is how a write aimed at
one road landed on a road at the other end of the map and reported success
(#983).

Every actor-targeting handler therefore resolves its target through
`MCPResolveActor` in `Public/HandlerUtils.h`, and never through a loop of its
own:

```cpp
FString ActorLabel;
if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

REQUIRE_EDITOR_WORLD(World);

TSharedPtr<FJsonValue> ActorErr;
AActor* Actor = MCPResolveActor(World, Params, ActorErr);
if (!Actor) return ActorErr;
ActorLabel = Actor->GetActorLabel();
```

The rules the resolver enforces, so no handler restates them:

- `actorPath` is the unambiguous selector and wins whenever it is given. A path
  that names nothing is an error about the path, not a quiet fall-through to
  the label.
- A label matching more than one actor is refused with a structured error:
  `ambiguous: true`, `matchCount`, and a `candidates[]` array carrying each
  actor's `actorPath`, label, class, folder and location. There is no override
  that picks one anyway.
- An action whose answer is genuinely plural (an ignore list, a selection, a
  batch) uses `MCPCollectActorsByToken` and acts on **every** match instead.
- An action naming its actor something else passes an `FMCPActorSelector`; the
  convention is that the path key is the label key with "Label" swapped for
  "Path" (`childLabel` / `childPath`).

Every handler that returns an actor also returns its `actorPath`, so the round
trip from a read into the next write is a copy of one field.

### `onConflict` - creates only

Create handlers accept an optional `onConflict` parameter controlling what happens when the natural key already resolves to an existing entity:

| Value | Behavior |
|---|---|
| `"skip"` (default) | Return the existing entity, set `existed: true`, no rollback |
| `"update"` | Reconcile the existing entity to the desired state (if applicable), set `updated: true` |
| `"error"` | Return an `MCPError` ("already exists") |

### Return shape

Creates and modifies populate one of:

```json
{ "success": true, "created": true,  "existed": false, /* entity fields */ }
{ "success": true, "created": false, "existed": true,  /* entity fields */ }
{ "success": true, "updated": true,                     /* entity fields */ }
```

Deletes return:

```json
{ "success": true, "deleted": true }               /* actually removed something */
{ "success": true, "alreadyDeleted": true }        /* nothing to do */
```

### Rollback record

On a successful **mutation that actually changed state**, the handler attaches a rollback record naming the inverse handler and the payload needed to call it:

```cpp
// In the handler, after a successful create:
TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
Payload->SetStringField(TEXT("actorLabel"), NewActor->GetActorLabel());
MCPSetRollback(Result, TEXT("delete_actor"), Payload);
```

The TS bridge lifts the `rollback` field onto `TaskResult.rollback`. When `rollback_on_failure: true` is set on a flow and a later step fails, flowkit invokes these records in reverse order.

**Key rules:**

- **Only emit a rollback record when the handler actually mutated state.** An `existed: true` result means nothing was changed, so there's nothing to undo - do NOT emit a record.
- **The inverse must be another registered handler.** Don't invent bespoke inverse handlers unless necessary; for creates, it's almost always the paired `delete_X`. For modifies, it's the same handler called with the previous value (self-inverse).
- **Modifies capture the previous value _before_ mutation.** The rollback payload restores exactly that value.
- **A record on a `success: false` body describes the part that landed, and only that part.** A handler that partly applied a mutation before giving up may attach an inverse for what it did write, and a few do. flowkit collects it like any other record and invokes it FIRST when `rollback_on_failure` is armed, so build the payload from what actually applied rather than from what was asked for. Without `rollback_on_failure` nothing replays it and it arrives on `steps[i].partialWriteRollback` and in the step's error text for a caller to run. Build the record the same way either way. See [docs/flows.md](flows.md).

### Idempotency, and why a re-run must not fail

A content mutation asked for twice REPORTS the second time rather than failing it. `success: true` with the marker that says nothing changed - `alreadyExists`, `alreadyRemoved`, `existed`, `unchanged` - and no rollback record, because nothing was mutated to undo. The caller asked for a state and the state holds.

**Lifecycle actions are the exception, and they answer differently.** `editor(start_editor)` on an editor that is already up, `editor(stop_editor)` with nothing running, and both halves of `editor(play_in_editor)` report `success: false` and carry `alreadyRunning` or `alreadyStopped` beside it. The call did not start, stop or play anything, and saying otherwise would be the handler reporting a success for work it did not do. The marker is what lets a caller tell that apart from a real failure, which is the job the marker exists for; the verdict stays honest.

This is not cosmetic for a content mutation. A `success: false` body fails the flow step that ran it and stops the run, so an asset create that treats "it already exists" as an error aborts every flow that makes sure of something before doing work, on the common path where it was already sure. `MCPError` is for a call that could not do what it was asked; being handed a state that already holds is not that.

Where the answer genuinely is a failure, as with the lifecycle actions above, the flow absorbs it rather than the handler hiding it: a step marks itself `ignore_failure: true` and the run continues with the failure still recorded. See [docs/flows.md](flows.md). That is the right layer, because only the flow author knows whether a particular step failing is expected.

## Helpers

`HandlerUtils.h` provides:

```cpp
MCPSuccess()                                  // { success: true }
MCPError(Message)                             // { success: false, error }
MCPResult(Obj)                                // wrap FJsonObject as FJsonValue

MCPSetCreated(Result)                         // { created: true,  existed: false }
MCPSetExisted(Result)                         // { created: false, existed: true  }
MCPSetUpdated(Result)                         // { updated: true }
MCPSetRollback(Result, InverseMethod, Payload)
MCPSetDeleteAssetRollback(Result, AssetPath)  // shorthand for delete_asset rollback
MCPSetNoRollback(Result, Reason)              // { rollbackPossible: false, rollbackNote }
                                              // The reason is required: see
                                              // "rollbackPossible" below.
MCPSetIdempotencyUnobservable(Result, Reason) // { idempotencyObservable: false, idempotencyNote }
                                              // The same statement, one
                                              // convention over: this call
                                              // cannot read whether it changed
                                              // anything, and here is why.

// Existence probes - return a ready-to-return Existed/Error JSON value
// on hit, an unset shared pointer on miss.
MCPCheckAssetExists(PackagePath, Name, OnConflict, FriendlyType?)
MCPCheckActorLabelExists(World, Label, OnConflict, FriendlyType?)

// Protected mount guardrail. True for /Engine/, /Memory/, /Temp/ and anything
// containing /Script/. Call it from every handler that deletes, moves, renames
// or writes an asset. Shared, never copied: see "File-local helpers and the
// unity build" in development.md.
MCPIsProtectedAssetPath(Path)

// Actor lookup
FindActorByLabel(World, Label)                // canonical label lookup
FindActorByLabelOrName(World, Token)          // PIE: label OR internal name
FindActorByLabelOrPath(World, Label, Path)    // get_actor_details: one of two
FindActorByLabelNameOrPath(World, Token)      // PIE invoke: any of three

// Blueprint CDO load + cast with structured error
LoadBlueprintCDO<TActor>(Path, OutError)

// Class name resolution - one shared path for every string-to-UClass lookup.
// Tolerates the C++ type prefix in both directions (UMyConfig <-> MyConfig),
// object paths, Module.Class shorthand, and Blueprint generated classes.
MCPResolveClass(Spec, bAllowLoad?)             // UClass* or nullptr
MCPResolveClassOfType(Spec, Base, bAllowLoad?) // constrained to a base class
FindClassByShortName(Name)                     // thin wrapper over MCPResolveClass
MCPClassNotFoundError(Spec, ParamName?)        // lists tried spellings + suggestions
MCPCheckClassUsable(Spec, Class, Base?, bConcrete?) // abstract / deprecated /
                                               // wrong_base reported separately

// Parameter extraction (Vec3 / Rotator / Color / Transform helpers)
RequireString, OptionalString, OptionalNumber, OptionalInt, OptionalBool
OptionalVec3, RequireVec3, OptionalRotator, RequireRotator, OptionalTransform
MCPVec3ToJsonObject, MCPRotatorToJsonObject, MCPLinearColorToJsonObject
```

`HandlerAssetCreate.h` adds:

```cpp
// Probe-then-create using AssetTools. Returns FMCPAssetCreate<T> with either
// an EarlyReturn JSON value (caller just returns it) or an Asset pointer.
// Two overloads: static class (TAsset::StaticClass()) or runtime UClass*.
MCPCreateAssetIdempotent<TAsset>(Name, PackagePath, OnConflict, Label, Factory)
MCPCreateAssetIdempotent<TAsset>(Name, PackagePath, OnConflict, Label, UClass*, Factory)

// Probe-then-create via raw NewObject<> on a fresh UPackage + AssetCreated.
// Used by AnimSequence / AnimComposite / LevelSequence / PoseSearchDatabase /
// NiagaraSystem-from-spec where AssetTools.CreateAsset isn't the right entry
// point (factory configuration must happen on the constructed object first).
MCPCreateAssetIdempotentNewObject<TAsset>(Name, PackagePath, OnConflict, Label)
```

## Patterns

### Spawn an actor with natural-key idempotency

```cpp
TSharedPtr<FJsonValue> FLevelHandlers::PlaceActor(const TSharedPtr<FJsonObject>& Params)
{
    FString Label = OptionalString(Params, TEXT("label"));
    const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

    REQUIRE_EDITOR_WORLD(World);

    // Idempotency: if an actor with this label exists, return Existed JSON.
    if (auto Existing = MCPCheckActorLabelExists(World, Label, OnConflict, TEXT("Actor")))
    {
        return Existing;
    }

    AActor* NewActor = /* spawn */;
    if (Label.IsEmpty()) Label = NewActor->GetActorLabel();

    auto Result = MCPSuccess();
    MCPSetCreated(Result);
    Result->SetStringField(TEXT("actorLabel"), Label);

    TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("actorLabel"), Label);
    MCPSetRollback(Result, TEXT("delete_actor"), Payload);

    return MCPResult(Result);
}
```

### Create an asset with natural-key idempotency

```cpp
TSharedPtr<FJsonValue> FMaterialHandlers::CreateMaterial(const TSharedPtr<FJsonObject>& Params)
{
    FString Name;
    if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
    const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Materials"));
    const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

    UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
    auto Created = MCPCreateAssetIdempotent<UMaterial>(Name, PackagePath, OnConflict, TEXT("Material"), Factory);
    if (Created.EarlyReturn) return Created.EarlyReturn;  // Existed or Error

    SaveAssetPackage(Created.Asset);
    const FString AssetPath = Created.Asset->GetPathName();

    auto Result = MCPSuccess();
    MCPSetCreated(Result);
    Result->SetStringField(TEXT("path"), AssetPath);
    Result->SetStringField(TEXT("name"), Name);
    Result->SetStringField(TEXT("packagePath"), PackagePath);
    MCPSetDeleteAssetRollback(Result, AssetPath);
    return MCPResult(Result);
}
```

### Modify with before-state capture

```cpp
TSharedPtr<FJsonValue> FLevelHandlers::SetActorMaterial(const TSharedPtr<FJsonObject>& Params)
{
    // Capture previous material BEFORE changing
    FString PreviousMaterialPath;
    if (UMaterialInterface* Prev = PrimComp->GetMaterial(SlotIndex))
    {
        PreviousMaterialPath = Prev->GetPathName();
    }

    PrimComp->SetMaterial(SlotIndex, NewMaterial);

    auto Result = MCPSuccess();
    MCPSetUpdated(Result);

    TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
    Payload->SetNumberField(TEXT("slotIndex"), SlotIndex);
    Payload->SetStringField(TEXT("materialPath"), PreviousMaterialPath);
    MCPSetRollback(Result, TEXT("set_actor_material"), Payload);

    return MCPResult(Result);
}
```

### Delete - reversible where the state can be captured, and explicit where it cannot

Delete handlers are idempotent: deleting a thing that is not there is a no-op, not an error.

Reversibility is a decision, not a default. A delete that captures enough of the entity beforehand to recreate it emits a rollback record like any other mutation, and many now do. A delete that cannot must **say so in the result**, because a caller reading a body with no `rollback` field cannot tell a considered decision from an oversight, and neither can the conventions audit.

```cpp
auto Result = MCPSuccess();
if (NotFound) {
    Result->SetBoolField(TEXT("alreadyDeleted"), true);
    return MCPResult(Result);
}

/* capture what recreating it would need, then delete */
Result->SetBoolField(TEXT("deleted"), true);

// Either the inverse:
MCPSetRollback(Result, TEXT("add_socket"), Payload);

// Or the reason there is not one, which is a call rather than a comment:
MCPSetNoRollback(Result,
    TEXT("The socket carried per-instance overrides on three meshes and this handler reads none of them, "
         "so add_socket would put back a socket that is not the one removed."));

return MCPResult(Result);
```

Saying nothing is the one option that is not available. "Delete is not reversible by default" used to be written here as a licence to emit neither, and it is now the exact shape the ratchet in `tests/unit/handler-conventions.test.ts` counts and pins.

### `rollbackPossible` - stating that there is no inverse

The counterpart to `MCPSetRollback`, and the field the conventions audit actually gates on. It is set through `MCPSetNoRollback(Result, Reason)`, which writes both halves together:

```json
{
  "success": true,
  "updated": true,
  "rollbackPossible": false,
  "rollbackNote": "A one-shot sound is an event, not a state. It is already audible by the time this returns and nothing changed on disk or in the level, so there is nothing to undo and no action that would undo it. Calling again plays it again."
}
```

Roughly 290 sites across the plugin carry it, which makes it the most common way a mutation in this codebase finishes.

Two rules:

- **The note is required.** `rollbackPossible: false` on its own tells a caller that recovery is off the table without telling it why, which is the half that decides what the caller does next. `MCPSetNoRollback` takes the reason as an argument so the pair cannot come apart.
- **Say what was changed and what call would have to exist.** "No inverse" is not a note. "The offset was baked into the mesh descriptions and committed, and the bridge has no action that translates mesh vertices, so nothing can add it back" is: it names the change, names the missing capability, and tells the next person what would close it.

A handler that emits both a rollback record and `rollbackPossible: false` is a contradiction, and the audit reports it as one.

### Batch handlers - preflight, then per-item results

A batch handler takes one bounded array and does N of something. It has two obligations the single-item version does not.

**Preflight the whole request before writing anything.** Resolve every class, validate every name and destination, reject duplicate targets, and apply every requested value to a transient copy of the target. A descriptor that cannot possibly work should reject the entire request while it is still free to do so. `asset(bulk_upsert_data_assets)` does this by duplicating the existing asset (or constructing a fresh instance) into the transient package and running the real property writes against that copy first, so a typo in a property path cannot leave half a batch written.

**Report per item once you start writing.** Past the preflight boundary, a failure must not abort the response. Each entry in `items[]` carries its own `status`, `success`, and `error`, and the aggregate response still carries the rollback record naming everything that did land. Returning a bare error from inside the apply loop throws away exactly the information the caller needs to recover.

Batch handlers should also accept `dryRun`, which runs the full preflight and reports what each item *would* do (`wouldCreate`, `wouldUpdate`, `wouldRemainUnchanged`, `wouldSkip`) without dirtying or saving a package, and should size their array bound explicitly rather than accepting an unbounded request.

## Handlers that cannot have an inverse

A handler with no natural key cannot be idempotent, and one whose effect is an event rather than a state cannot be undone. These are the standing cases:

- `shell` - arbitrary command execution
- `editor.execute_command` - arbitrary console commands
- `editor.take_screenshot` - produces an output artifact, and deleting a file that regenerates on demand is not an undo
- `editor.start_editor`, `editor.quit_editor` - process lifecycle
- `level.save` - writing packages to disk has no inverse call
- `asset.reimport` - the previous import is not recoverable from the asset it replaced

Being on this list is not an exemption from saying so. Each of these owes the caller a `rollbackPossible: false` and a note, and every one of them now emits it. There is no allowlist to be added to: the `NO_INVERSE` map that used to exempt four handlers is gone, because it stated their reasons inside a test file where no caller could read them, and all four now state the same reasons in the response body.

`level.load` used to be on this list and is not any more: it captures the level that was open and emits the `load_level` call that returns to it.

## Where the conversion stands

The numbers move every time anyone touches a handler, so the live answer is the audit rather than a table here:

```bash
node scripts/audit-handler-conventions.mjs        # counts, plus the offenders by name
node scripts/audit-handler-conventions.mjs --json # every handler, one row each
```

`tests/unit/handler-conventions.test.ts` pins those counts. Two of them are flat
rules at zero: a mutation that answers neither question fails the suite outright.
The rest are a ratchet, where a number that goes UP means a new handler skipped a
convention, and a number that goes DOWN means somebody fixed one and the fix is
not finished until the lower number is committed.

Worth knowing when reading any figure quoted from this audit: for most of its
life the gating test did not run at all. It imports the audit out of a
`scripts/*.mjs` file that began with a shebang, which Vite hands to the ESM
loader unstripped, so the suite failed to LOAD rather than to assert and
contributed no assertions and no failure. Numbers from that period were produced
by running the CLI by hand.

The shape of what is left, as of the last sweep:

| | |
|---|---|
| Registered handlers | ~1040 |
| Classified as mutations | ~685 |
| Emitting a rollback record | ~555 |
| Emitting `rollbackPossible: false` with a reason instead | ~127 |
| Emitting neither, and therefore actually outstanding | 0 |
| Giving no idempotency answer | 0 |

Every authoring category has been swept. Level, Asset, Blueprint, Material, Animation, Audio, Gameplay, GAS, Niagara, PCG, Sequencer, Spline, Widget, Landscape and Networking all emit an inverse from their creates and their modifies, including the ones an older version of this page listed as outstanding: `add_node`, `connect_pins`, `set_class_default`, `add_material_expression`, `delete_material_expression`, `create_state_machine`, `add_state`, `add_transition`, `set_bone_keyframes`, `add_attribute`, `set_ability_tags`, `set_niagara_parameter`, `add_emitter_to_system`, `add_pcg_node`, `set_pcg_node_settings`, `set_spline_points`, `add_widget`, `remove_widget`, `move_widget` and `set_sequence_keyframes`.

The last two rows are zero and are held there by flat assertions rather than by a
ratchet, because there is no debt left to ratchet down. `foliage(set_settings)`
was the clearest of the tail and is now the clearest of the fixes: it captures
each property's previous exported value before writing, emits itself as its own
inverse with those values, and reports `changedCount` against the exported form
so `1` and `1.000000` do not read as a change.

A caveat that has not changed: this audit is source-level. It proves a rollback
is EMITTED, never that it is correct. Correctness is what the live tier is for.

`mutationsWithoutRollback` stays a number rather than a rule and stays non-zero
on purpose. Not every change has an inverse, and a codebase where that count
reached zero would be one that had started emitting rollbacks that do not roll
anything back.
