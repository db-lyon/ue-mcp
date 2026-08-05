# Widget parameter contract

Every action in the `widget` category takes the same parameter names for the
same concepts. There is nothing to guess per action, and nothing to discover by
probing.

| Concept | Canonical parameter | Shape |
| --- | --- | --- |
| The Widget Blueprint or Editor Utility asset | `assetPath` | Unreal package path: `/Game/UI/WBP_Example` |
| A widget inside the tree | `widgetName` | The designer name: `HealthBar` |
| Its parent panel | `parentWidgetName` | The designer name of the panel |
| Arguments for an `epic_*` action | `input` | One JSON object of the wrapped tool's arguments |

```json
{
  "action": "get_details",
  "assetPath": "/Game/UI/WBP_HUD",
  "widgetName": "HealthBar"
}
```

## assetPath

`assetPath` is an Unreal package path. It starts at a mount point (`/Game`,
`/Engine`, a plugin mount) and ends with the asset name.

These spellings are accepted and normalized to the same path, so a value copied
out of the content browser or off disk still works:

| Sent | Used |
| --- | --- |
| `/Game/UI/WBP_Example.uasset` | `/Game/UI/WBP_Example` |
| `/Game/UI/WBP_Example.WBP_Example` | `/Game/UI/WBP_Example` |
| `/Game/UI/WBP_Example.WBP_Example_C` | `/Game/UI/WBP_Example` |
| `\Game\UI\WBP_Example` | `/Game/UI/WBP_Example` |

These are rejected, and the error names the field it came from:

- a filesystem path such as `C:/Projects/Game/Content/UI/WBP_Example.uasset`
- a relative path such as `WBP_Example`
- an empty value

## Creating assets

`create`, `create_utility_widget`, and `create_utility_blueprint` take
`assetPath` as the full destination:

```json
{ "action": "create", "assetPath": "/Game/UI/WBP_HUD", "parentClass": "/Script/UMG.UserWidget" }
```

`name` plus `packagePath` remains valid and means exactly the same thing. What
is refused is the contradictory combination, because only one of the two can be
honoured:

```json
{ "action": "create", "assetPath": "/Game/UI/WBP_HUD", "name": "HUD" }
```

> Conflicting parameters: assetPath '/Game/UI/WBP_HUD' names the asset 'WBP_HUD',
> but name is 'HUD'. assetPath already carries the asset name. Pass assetPath
> alone, or pass name together with packagePath.

`blueprint(create)` follows the same rule for the same reason.

## Accepted aliases

Older and engine-side spellings keep working. They are declared in the tool
schema, so a client cannot strip them, and they are folded into the canonical
name before the call is dispatched. Prefer the canonical name in new code.

| Alias | Canonical |
| --- | --- |
| `path` | `assetPath` |
| `widgetBlueprintPath` | `assetPath` |
| `widgetBlueprint` (string, or `{ "refPath": "..." }`) | `assetPath` |
| `widgetDisplayName` | `widgetName` |
| `parentWidget` | `parentWidgetName` |
| `name` + `packagePath` (create actions) | `assetPath` |

`path` is an input alias only. A `path` field in a *response* is whatever that
handler chose to report and carries no promise of being an asset path.

## epic_* actions

The `epic_*` actions in this category wrap Unreal's own UMG toolset. Those tools
take one nested arguments object, so the canonical envelope is `input`:

```json
{
  "action": "epic_get_widgets",
  "input": { "widgetBlueprint": { "refPath": "/Game/UI/WBP_HUD" } }
}
```

Two conveniences sit on top of that:

- A top-level parameter named by the wrapped tool's own schema is folded into
  `input`.
- A top-level `assetPath` fills the tool's asset reference when the tool takes
  exactly one, so the call above can also be written:

```json
{ "action": "epic_get_widgets", "assetPath": "/Game/UI/WBP_HUD" }
```

An explicit `input` always wins over a top-level value, and `inputJson` (a raw
JSON string) is passed through untouched.

A call that still cannot satisfy the tool's required arguments is refused before
it reaches the editor, with the arguments that are missing, the shape to send,
and any top-level parameters that are not arguments of that tool:

> UMGToolSet.UMGToolSet.AddWidget is missing required argument(s): widgetClass.
> Pass them in 'input', for example {"input": {"widgetClass": "..."}}.
