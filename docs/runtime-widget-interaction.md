# Runtime widget interaction

`widget(action="invoke_runtime_function")` drives a live UMG widget while Play-In-Editor is running. It covers two shapes:

- **A UFUNCTION on the UserWidget.** Pass `functionName` with no `childName`. The function must take no parameters.
- **An interaction on a child widget.** Pass `childName`. The handler writes the new state through the widget's own setter and broadcasts the delegate the real interaction fires, so Blueprint logic bound to that event runs.

Locate the widget with `widgetName` (exact instance name) or `className` (first instance whose class name contains it). `widget(action="list_runtime")` lists the live instances.

This is one interaction per call through the widget's own setter, not an input
event. For timed input sequences or Enhanced Input injection, use
`pie(inject_input*)` from the [PIE testing](pie.md) tool.

## Supported child widgets

| Widget class | `value` | Delegates broadcast |
| --- | --- | --- |
| `Button` | not used | `OnClicked` (default), or `OnPressed` / `OnReleased` / `OnHovered` / `OnUnhovered` via `functionName` |
| `CheckBox` | `true` / `false`, `0` / `1`, `"checked"`, `"unchecked"`, `"undetermined"`, `"toggle"`. Omitted means toggle | `OnCheckStateChanged` |
| `Slider` | number, clamped to the slider's min/max | `OnValueChanged` |
| `SpinBox` | number | `OnValueChanged` then `OnValueCommitted` |
| `EditableText`, `EditableTextBox`, `MultiLineEditableText`, `MultiLineEditableTextBox` | string | `OnTextChanged` then `OnTextCommitted` |
| `ComboBoxString` | option string, or the option index as a number | `OnSelectionChanged` |

Any other class returns an error naming the class and listing the supported ones.

## Selecting a single delegate

`functionName` alongside `childName` narrows the broadcast instead of naming a UFUNCTION on the parent. On a text field, `functionName: "OnTextChanged"` sets the text and fires only the change event, which is how you exercise live validation without the commit path. A name the child does not expose returns an error listing the delegates it does.

`commitMethod` picks the commit type for text fields and spin boxes: `OnEnter` (default), `OnUserMovedFocus`, `OnCleared`, `Default`.

## Examples

Toggle a checkbox and run its bound graph:

```json
{ "action": "invoke_runtime_function", "widgetName": "WBP_Settings_C_0", "childName": "CheckBox_0" }
```

Set a checkbox explicitly, move a slider, type a name, pick a combo entry:

```json
{ "action": "invoke_runtime_function", "className": "WBP_Settings", "childName": "InvertYCheck", "value": true }
{ "action": "invoke_runtime_function", "className": "WBP_Settings", "childName": "MasterVolume", "value": 0.35 }
{ "action": "invoke_runtime_function", "className": "WBP_Settings", "childName": "PlayerName", "value": "Ada" }
{ "action": "invoke_runtime_function", "className": "WBP_Settings", "childName": "QualityCombo", "value": "Epic" }
```

## Response

```json
{
  "success": true,
  "widget": "WBP_Settings_C_0",
  "child": "InvertYCheck",
  "childClass": "CheckBox",
  "interaction": "check",
  "previousValue": "unchecked",
  "value": "checked",
  "isChecked": true,
  "invoked": "OnCheckStateChanged",
  "delegates": ["OnCheckStateChanged"]
}
```

`delegates` lists every delegate the call broadcast and `invoked` holds the last of them. Text fields and spin boxes also return `commitMethod`. A slider returns `requestedValue`, the number sent before clamping. A combo box returns `selectedIndex`.

## Broadcast accounting

Each delegate fires exactly once per call. Some UMG setters broadcast on their own, so the handler only broadcasts by hand where the setter is silent:

- `UCheckBox::SetCheckedState`, `USpinBox::SetValue` and every `SetText` overload are silent. The handler broadcasts.
- `USlider::SetValue` broadcasts `OnValueChanged` only when the value moves. When it does not, the handler broadcasts.
- `UComboBoxString::SetSelectedOption` broadcasts `OnSelectionChanged` on a real change. When the option is already selected, the handler broadcasts with `ESelectInfo::Direct`.
