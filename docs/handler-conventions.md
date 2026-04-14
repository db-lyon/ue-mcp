# Handler Conventions

How mutating C++ handlers participate in **idempotency** (safe replay) and **rollback** (failure recovery).

## Why

Flows mutate editor state. When a flow fails partway, the user wants two guarantees:

1. **Rerun is safe** — running the same flow again doesn't duplicate work or explode on "already exists" errors.
2. **Failure is recoverable** — the user can opt into automatic rollback that undoes completed mutations in reverse order.

Both are properties of each individual handler. The runner coordinates across handlers; each handler decides what the natural key is, how to detect existing state, and what the inverse operation looks like.

## The contract

Every mutating handler (create, modify, delete) follows this shape:

### Natural key

Each handler accepts a parameter identifying the entity it operates on. Examples:

| Entity | Natural key param |
|---|---|
| Actor | `actorLabel` (or `label` shorthand on creates) |
| Asset (material, texture, mesh, datatable…) | `assetPath` or `path` |
| Blueprint variable | `blueprintPath` + `variableName` |
| Blueprint function | `blueprintPath` + `functionName` |
| Component | parent + `componentName` |
| Material parameter | `materialPath` + `parameterName` |

Handlers without a natural key (e.g., `execute_console`, `shell`) **cannot** be idempotent or reversible — document them as such, do not emit rollback records.

### `onConflict` — creates only

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

- **Only emit a rollback record when the handler actually mutated state.** An `existed: true` result means nothing was changed, so there's nothing to undo — do NOT emit a record.
- **The inverse must be another registered handler.** Don't invent bespoke inverse handlers unless necessary; for creates, it's almost always the paired `delete_X`. For modifies, it's the same handler called with the previous value (self-inverse).
- **Modifies capture the previous value _before_ mutation.** The rollback payload restores exactly that value.

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
```

## Patterns

### Create with natural key

```cpp
TSharedPtr<FJsonValue> FLevelHandlers::PlaceActor(const TSharedPtr<FJsonObject>& Params)
{
    FString Label = OptionalString(Params, TEXT("label"));
    const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

    REQUIRE_EDITOR_WORLD(World);

    // Idempotency: if an actor with this label exists, reuse it.
    if (!Label.IsEmpty())
    {
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (It->GetActorLabel() == Label)
            {
                if (OnConflict == TEXT("error"))
                    return MCPError(FString::Printf(TEXT("Actor '%s' already exists"), *Label));

                auto Result = MCPSuccess();
                MCPSetExisted(Result);
                Result->SetStringField(TEXT("actorLabel"), Label);
                // No rollback record — nothing was created.
                return MCPResult(Result);
            }
        }
    }

    // Create path
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

### Delete — document as non-reversible

Delete handlers are idempotent (deleting a non-existent thing is a no-op) but **not reversible** by default. Undoing a delete requires snapshotting the deleted entity beforehand, which is only worthwhile for high-value handlers.

```cpp
auto Result = MCPSuccess();
if (NotFound) {
    Result->SetBoolField(TEXT("alreadyDeleted"), true);
} else {
    /* delete */
    Result->SetBoolField(TEXT("deleted"), true);
    // No rollback record — delete is not reversible by default.
}
return MCPResult(Result);
```

## Non-convertible handlers

These handlers cannot meaningfully participate:

- `shell` — arbitrary command execution
- `editor.execute_console` — arbitrary console commands
- `editor.take_screenshot` — side-effect with no natural inverse
- `editor.start_editor`, `editor.quit_editor`, `level.save`, `level.load` — lifecycle operations

## Conversion progress

| Category | Creates | Modifies | Deletes | Status |
|---|---|---|---|---|
| Level | place_actor, spawn_light, spawn_volume, create_level, add_component | move_actor, set_actor_material, set_component_property, set_light_*, set_volume_properties, set_world_settings | delete_actor | In progress |
| Asset | create_datatable, create_material, import_* | rename, move, set_texture_settings, add_socket, remove_socket | delete, duplicate (create side) | Pending |
| Blueprint | create, add_variable, create_function, add_node, add_component, add_interface, add_event_dispatcher, create_interface | set_variable_properties, set_node_property, rename_function | delete_function, delete_node | Pending |
| Material | create, add_parameter | — | — | Pending |
| Niagara/PCG/GAS/Widget/Audio/Foliage/Landscape/Animation/Gameplay/Networking | various | various | various | Pending |
