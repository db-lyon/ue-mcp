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

| Category | Done | Remaining |
|---|---|---|
| Level | place_actor, spawn_light, spawn_volume, move_actor, set_actor_material, set_light_properties, set_component_property, set_volume_properties, set_world_settings, add_component_to_actor, delete_actor | — |
| Asset | duplicate_asset, rename_asset, move_asset, delete_asset, create_datatable, import_static_mesh, import_skeletal_mesh, import_animation, import_texture, set_mesh_material, set_texture_properties (partial), add_socket, remove_socket | recenter_pivot, reimport_* |
| Blueprint | create_blueprint, add_variable, add_component, create_function, rename_function, delete_function, delete_node, delete_variable, remove_component, create_blueprint_interface | set_variable_properties, set_node_property, add_node, connect_pins, set_class_default, set_variable_default, add_function_parameter |
| Material | create_material, create_material_instance, create_material_from_texture | add_material_expression, set_*, connect_expression, delete_expression |
| Animation | create_anim_blueprint, create_montage, create_blendspace, create_sequence | add_anim_notify, create_state_machine, add_state, add_transition, set_*, set_bone_keyframes |
| Audio | create_sound_cue, create_metasound_source, spawn_ambient_sound | — |
| Foliage | create_foliage_type | create_foliage_layer, paint_foliage, set_foliage_type_settings |
| Gameplay | create_smart_object_definition, create_input_action, create_input_mapping_context, create_blackboard, create_behavior_tree, create_eqs_query, create_state_tree, create_game_mode/state/player_controller/player_state/hud (via CreateBlueprintWithParent), spawn_nav_modifier_volume | set_collision_profile, set_physics_enabled, set_body_properties, create_ai_perception_config |
| GAS | create_gameplay_effect, create_gameplay_ability, create_attribute_set, create_gameplay_cue, create_gameplay_cue_notify | add_ability_tag, add_attribute, set_ability_tags, set_effect_modifier, add_ability_system_component |
| Niagara | create_niagara_system, create_niagara_emitter, create_niagara_system_from_emitter | spawn_niagara_at_location, set_niagara_parameter, add_emitter_to_system, set_emitter_property |
| PCG | create_pcg_graph, spawn_pcg_volume | add_pcg_node, connect_pcg_nodes, remove_pcg_node, set_pcg_node_settings |
| Sequencer | create_level_sequence | add_track, sequence_control |
| Spline | create_spline_actor | set_spline_points |
| Widget | create_widget_blueprint, create_editor_utility_widget, create_editor_utility_blueprint | set_widget_property, add_widget, remove_widget, move_widget |
| Landscape/Networking/Physics/Reflection | — | set_landscape_material, import_heightmap, sculpt_*, paint_*, networking setters, physics setters, create_gameplay_tag |

Every handler in the "Done" column is idempotent (checks for existing entity by natural key, returns `{ existed: true }` on replay) and emits a rollback record where a paired inverse exists. Handlers in "Remaining" are either pure modifies that need before-state capture, or pure deletes that need snapshot-before-delete to be reversible. They still work; they just don't yet participate in rollback.
