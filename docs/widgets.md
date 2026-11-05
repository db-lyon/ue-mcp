# Widgets

The `widget` category authors UMG assets and drives live widgets while
Play-In-Editor is running. These pages cover what needs more than a line in the
[Tool Reference](tool-reference.md).

## Start here

Every action in the category takes the same parameter names for the same
concepts. Read the [parameter contract](widget-parameters.md) first.

## Authoring versus runtime

- **Authoring** actions operate on a Widget Blueprint asset. During PIE, an
  asset that is not already in memory can fail to load, so stop play first.
- **Runtime** actions operate on live `UUserWidget` instances inside a running
  PIE or Game world. They never fall back to the editor world, so a call made
  before PIE starts returns an error.

## When an authoring action cannot find the asset

Every authoring action resolves its WidgetBlueprint from the path on every call. Nothing is cached between calls. When resolution fails, the error says which of four things happened:

| Error says | What it means | What to do |
|---|---|---|
| `No asset exists at '<path>'` | Nothing of that name is in the AssetRegistry and no package of that name is on disk. | Fix the path. `widget(action="list")` and `asset(action="search")` show what is really there. |
| `'<path>' exists but could not be resolved to a live WidgetBlueprint` | The asset is there and the object handle went stale. | Retry the call. If it keeps failing, `editor(action="reload_bridge")`. |
| `'<path>' is a <Class>, not a WidgetBlueprint` | The path names a real asset of another type. | Point at the WidgetBlueprint. |
| `WidgetBlueprint '<path>' resolved but has no WidgetTree` | The asset loaded and is broken. | Open it in the editor, or re-create it. |

While PIE is running, the first two errors add a note to stop play and retry.

## Where the runtime actions stop

The runtime actions are point-in-time: one read of the current state, or one
write plus the delegate broadcast a real interaction would fire. Use the
[`pie`](pie.md) tool instead to:

- sample a widget's state every frame over a window: `pie(observe_arm)` and
  `pie(observe_read)`;
- play a timed sequence of input values: `pie(inject_input_tape)`;
- inject an Enhanced Input action at the input layer rather than calling a
  widget's own setter: `pie(inject_input)`;
- record a session and replay it with drift comparison: `pie(record_arm)` and
  `pie(replay_arm)`.

`editor(stage_game_input)` sets the input mode for the running game; the
injection itself is `pie(inject_input*)`.

## Runtime pages

| Page | Use it when |
|---|---|
| [Runtime widget inspection](runtime-widget-inspection.md) | You need the state of every matching live instance, with stable identity and selected reflected properties. |
| [Runtime widget interaction](runtime-widget-interaction.md) | You need to drive a widget: toggle a checkbox, move a slider, type into a text box, pick a combo entry, or call a UFUNCTION. |
| [Runtime layout diagnostics](runtime-widget-layout-diagnostics.md) | A widget is in the wrong place or the wrong size and you need geometry, slot data, clipping and opacity to find out why. |
