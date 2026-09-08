// GENERATED FILE - do not edit.
//
// Written by scripts/generate-epic-actions.mjs from tests/golden/epic-catalog.json
// and src/tools/epic/effects.ts. To change what an action DOES, edit the
// effects file and regenerate; to pick up a new engine's toolsets, re-record
// the catalog and regenerate.
//
// These are ordinary actions. They carry a declared effect, real parameters and
// a Params: clause, they are in ALL_TOOLS, and they dispatch through the same
// task factory, guards and locks as every hand-written action in this package.
import { z } from "zod";
import { bp, type ActionSpec } from "../../types.js";
import { epicToolCall } from "../../epic-input.js";

const S_epic_add_uicomponent = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgetName":{"type":"string"},"componentClass":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","widgetName","componentClass"]} as const;
const S_epic_add_widget = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgetClass":{"type":"object","properties":{"refPath":{}}},"widgetDisplayName":{"type":"string"},"parentWidget":{"type":"object","properties":{"refPath":{}}},"childIndex":{"type":"integer"}},"required":["widgetBlueprint","widgetClass","widgetDisplayName"]} as const;
const S_epic_bind_to_event_property = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"eventName":{"type":"string"},"propertyName":{"type":"string"},"propertyClass":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","eventName","propertyName","propertyClass"]} as const;
const S_epic_click = {"properties":{"ref":{"type":"string"},"button":{"type":"string"},"doubleClick":{"type":"boolean"},"modifiers":{"type":"object"}},"required":["ref"]} as const;
const S_epic_compile_widget_blueprint = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint"]} as const;
const S_epic_create_widget_blueprint = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"},"parentClass":{"type":"object","properties":{"refPath":{}}}},"required":["folderPath","assetName","parentClass"]} as const;
const S_epic_drag = {"properties":{"startRef":{"type":"string"},"endRef":{"type":"string"},"modifiers":{"type":"object"}},"required":["startRef","endRef"]} as const;
const S_epic_fill_form = {"properties":{"fields":{"type":"array"}},"required":["fields"]} as const;
const S_epic_get_named_slots = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint"]} as const;
const S_epic_get_widget_class_info = {"properties":{"widgetClass":{"type":"object","properties":{"refPath":{}}}},"required":["widgetClass"]} as const;
const S_epic_get_widget_description = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"startWidget":{"type":"object","properties":{"refPath":{}}},"maxDepth":{"type":"integer"}},"required":["widgetBlueprint"]} as const;
const S_epic_get_widget_tree_depth = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"startWidget":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint"]} as const;
const S_epic_get_widgets = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint"]} as const;
const S_epic_hover = {"properties":{"ref":{"type":"string"}},"required":["ref"]} as const;
const S_epic_list_observers = {"properties":{}} as const;
const S_epic_list_widget_blueprints = {"properties":{"folderPath":{"type":"string"}},"required":["folderPath"]} as const;
const S_epic_list_widget_classes = {"properties":{"filter":{"type":"string"}},"required":["filter"]} as const;
const S_epic_move_uicomponent = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgetName":{"type":"string"},"componentClassToMove":{"type":"object","properties":{"refPath":{}}},"relativeToComponentClass":{"type":"object","properties":{"refPath":{}}},"bMoveAfter":{"type":"boolean"}},"required":["widgetBlueprint","widgetName","componentClassToMove","relativeToComponentClass","bMoveAfter"]} as const;
const S_epic_move_widget = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widget":{"type":"object","properties":{"refPath":{}}},"newParent":{"type":"object","properties":{"refPath":{}}},"childIndex":{"type":"integer"}},"required":["widgetBlueprint","widget","newParent"]} as const;
const S_epic_observe = {"properties":{"ref":{"type":"string"},"maxDepth":{"type":"integer"}},"required":["ref"]} as const;
const S_epic_press_key = {"properties":{"key":{"type":"string"}},"required":["key"]} as const;
const S_epic_remove_uicomponent = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgetName":{"type":"string"},"componentClass":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","widgetName","componentClass"]} as const;
const S_epic_remove_widget = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widget":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","widget"]} as const;
const S_epic_rename_widget = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widget":{"type":"object","properties":{"refPath":{}}},"newDisplayName":{"type":"string"}},"required":["widgetBlueprint","widget","newDisplayName"]} as const;
const S_epic_replace_widget_with_child = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgetToReplace":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","widgetToReplace"]} as const;
const S_epic_replace_widget_with_named_slot = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgetToReplace":{"type":"object","properties":{"refPath":{}}},"namedSlot":{"type":"string"}},"required":["widgetBlueprint","widgetToReplace","namedSlot"]} as const;
const S_epic_replace_widget_with_template = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgetToReplace":{"type":"object","properties":{"refPath":{}}},"templateClass":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","widgetToReplace","templateClass"]} as const;
const S_epic_screenshot = {"properties":{"ref":{"type":"string"}},"required":["ref"]} as const;
const S_epic_select_option = {"properties":{"ref":{"type":"string"},"value":{"type":"string"}},"required":["ref","value"]} as const;
const S_epic_set_named_slot_content = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"hostWidget":{"type":"object","properties":{"refPath":{}}},"slotName":{"type":"string"},"widgetClass":{"type":"object","properties":{"refPath":{}}},"widgetName":{"type":"string"}},"required":["widgetBlueprint","hostWidget","slotName","widgetClass","widgetName"]} as const;
const S_epic_snapshot = {"properties":{"ref":{"type":"string"},"maxDepth":{"type":"integer"},"bIncludeSourceLocations":{"type":"boolean"}},"required":["ref"]} as const;
const S_epic_toggle_widget_as_variable = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widget":{"type":"object","properties":{"refPath":{}}},"bIsVariable":{"type":"boolean"}},"required":["widgetBlueprint","widget","bIsVariable"]} as const;
const S_epic_type = {"properties":{"ref":{"type":"string"},"text":{"type":"string"},"submit":{"type":"boolean"}},"required":["ref","text"]} as const;
const S_epic_unobserve = {"properties":{"identifier":{"type":"string"}},"required":["identifier"]} as const;
const S_epic_wait_for = {"properties":{"text":{"type":"string"},"textGone":{"type":"string"}},"required":["text","textGone"]} as const;
const S_epic_windows = {"properties":{"action":{"type":"string"},"index":{"type":"integer"}}} as const;
const S_epic_wrap_widgets = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgets":{"type":"array"},"wrapperClass":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","widgets","wrapperClass"]} as const;

/** 37 wrapped engine tools routed to the `widget` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_uicomponent: bp(
    "mutate",
    "[Epic UMGToolSet.UMGToolSet] Adds a UI component of the given class to the named widget. Params: widgetBlueprint, widgetName, componentClass",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.AddUIComponent", S_epic_add_uicomponent, p),
  ),
  epic_add_widget: bp(
    "mutate",
    "[Epic UMGToolSet.UMGToolSet] Adds a widget to the tree at the specified position. Returns full widget info including Slot pointer. When ParentWidget is null and no root exists, the new widget becomes the root of the tree. Use ObjectTools.list_properties on the returned Widget and Slot to get property names before calling set_properties. Params: widgetBlueprint, widgetClass, widgetDisplayName, parentWidget?, childIndex?",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.AddWidget", S_epic_add_widget, p),
  ),
  epic_bind_to_event_property: bp(
    "mutate",
    "[Epic UMGToolSet.UMGToolSet] Adds a Blueprint event handler graph node bound to a widget's multicast delegate event, Typical events: UButton::OnClicked / OnPressed / OnReleased / OnHovered / OnUnhovered, UCheckBox::OnCheckStateChanged, USlider::OnValueChanged. The matching delegate UPROPERTY must exist on PropertyClass (or a parent of it). Preconditions: - PropertyName must exist in the blueprint. - PropertyClass must be the widget's class (or a parent class) that declares the delegate. Params: widgetBlueprint, eventName, propertyName, propertyClass",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.BindToEventProperty", S_epic_bind_to_event_property, p),
  ),
  epic_click: bp(
    "mutate",
    "[Epic SlateInspectorToolset.SlateInspectorToolset] Click a Slate widget identified by its ref. Params: ref, button?, doubleClick?, modifiers?",
    "epic_call_tool",
    (p) => epicToolCall("SlateInspectorToolset.SlateInspectorToolset", "SlateInspectorToolset.SlateInspectorToolset.Click", S_epic_click, p),
  ),
  epic_compile_widget_blueprint: bp(
    "mutate",
    "[Epic UMGToolSet.UMGToolSet] Compiles a widget blueprint. Returns false with error details if compilation fails. Errors include missing BindWidget bindings, type mismatches, and graph errors. Call after all widgets and properties are set. Save separately via AssetTools.save_asset. Params: widgetBlueprint",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.CompileWidgetBlueprint", S_epic_compile_widget_blueprint, p),
  ),
  epic_create_widget_blueprint: bp(
    "mutate",
    "[Epic UMGToolSet.UMGToolSet] Creates a new Widget Blueprint asset. Returns the blueprint or nullptr on failure. Params: folderPath, assetName, parentClass",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.CreateWidgetBlueprint", S_epic_create_widget_blueprint, p),
  ),
  epic_drag: bp(
    "mutate",
    "[Epic SlateInspectorToolset.SlateInspectorToolset] Drag from one Slate widget to another (mouse down, move, release). Params: startRef, endRef, modifiers?",
    "epic_call_tool",
    (p) => epicToolCall("SlateInspectorToolset.SlateInspectorToolset", "SlateInspectorToolset.SlateInspectorToolset.Drag", S_epic_drag, p),
  ),
  epic_fill_form: bp(
    "mutate",
    "[Epic SlateInspectorToolset.SlateInspectorToolset] Fill multiple Slate form fields at once. Params: fields",
    "epic_call_tool",
    (p) => epicToolCall("SlateInspectorToolset.SlateInspectorToolset", "SlateInspectorToolset.SlateInspectorToolset.FillForm", S_epic_fill_form, p),
  ),
  epic_get_named_slots: bp(
    "read",
    "[Epic UMGToolSet.UMGToolSet] Returns named slot bindings (separate from tree hierarchy). Params: widgetBlueprint",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.GetNamedSlots", S_epic_get_named_slots, p),
  ),
  epic_get_widget_class_info: bp(
    "read",
    "[Epic UMGToolSet.UMGToolSet] Returns the Category, Description and if it's a Panel for a single widget class. Same per-entry data as ListWidgetClasses, but lets callers query a class they already have without scanning every UClass. Returns an empty entry if WidgetClass is null. Can be used to get more information on the class from the Description and Category. Params: widgetClass",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.GetWidgetClassInfo", S_epic_get_widget_class_info, p),
  ),
  epic_get_widget_description: bp(
    "read",
    "[Epic UMGToolSet.UMGToolSet] Full property dump of every widget in the tree. Each line: [N] Type Name Prop:Value ... slot:(SlotProp:Value ...) N is the 0-based index into result.Widgets -- use result.Widgets[N] to get the widget ref without text parsing. Same indentation format as GetTaggedWidgetDescription; richer per-widget detail. Params: widgetBlueprint, startWidget?, maxDepth?",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.GetWidgetDescription", S_epic_get_widget_description, p),
  ),
  epic_get_widget_tree_depth: bp(
    "read",
    "[Epic UMGToolSet.UMGToolSet] Returns the maximum depth of the widget tree. Depth: root with no children = 0; root + children = 1; etc. Params: widgetBlueprint, startWidget?",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.GetWidgetTreeDepth", S_epic_get_widget_tree_depth, p),
  ),
  epic_get_widgets: bp(
    "read",
    "[Epic UMGToolSet.UMGToolSet] Returns blueprint info and all widgets in depth-first order. Children within each parent are in their panel slot order - this is the hierarchy order shown in the designer. Info contains ParentClass (pass to CreateWidgetBlueprint) and RootWidgetClass. Use ObjectTools.list_properties on each returned Widget and Slot to get property names before calling set_properties. Params: widgetBlueprint",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.GetWidgets", S_epic_get_widgets, p),
  ),
  epic_hover: bp(
    "mutate",
    "[Epic SlateInspectorToolset.SlateInspectorToolset] Hover over a Slate widget, triggering any hover state or tooltip. Params: ref",
    "epic_call_tool",
    (p) => epicToolCall("SlateInspectorToolset.SlateInspectorToolset", "SlateInspectorToolset.SlateInspectorToolset.Hover", S_epic_hover, p),
  ),
  epic_list_observers: bp(
    "read",
    "[Epic SlateInspectorToolset.SlateInspectorToolset] List all active observers as a JSON array for debugging. Each entry includes the observer identifier, whether it is the root observer, the root widget ref (if any), max depth, and cached snapshot size. Params: none",
    "epic_call_tool",
    (p) => epicToolCall("SlateInspectorToolset.SlateInspectorToolset", "SlateInspectorToolset.SlateInspectorToolset.ListObservers", S_epic_list_observers, p),
  ),
  epic_list_widget_blueprints: bp(
    "read",
    "[Epic UMGToolSet.UMGToolSet] Lists widget blueprints in a content folder. Params: folderPath",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.ListWidgetBlueprints", S_epic_list_widget_blueprints, p),
  ),
  epic_list_widget_classes: bp(
    "read",
    "[Epic UMGToolSet.UMGToolSet] Lists available widget classes, optionally filtered by name substring. Params: filter",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.ListWidgetClasses", S_epic_list_widget_classes, p),
  ),
  epic_move_uicomponent: bp(
    "mutate",
    "[Epic UMGToolSet.UMGToolSet] Moves a UI component before or after another component on the same widget. Params: widgetBlueprint, widgetName, componentClassToMove, relativeToComponentClass, bMoveAfter",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.MoveUIComponent", S_epic_move_uicomponent, p),
  ),
  epic_move_widget: bp(
    "mutate",
    "[Epic UMGToolSet.UMGToolSet] Moves a widget to a new parent panel at the specified position. Returns updated widget info with new Slot. Params: widgetBlueprint, widget, newParent, childIndex?",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.MoveWidget", S_epic_move_widget, p),
  ),
  epic_observe: bp(
    "mutate",
    "[Epic SlateInspectorToolset.SlateInspectorToolset] Register an observer on a widget subtree so its refs are continuously kept up to date (~100ms tick). Call this on the window or panel you are about to work with. It ensures new widgets appearing in that subtree are assigned refs automatically. Unobserve when you are done. A shallow root observer (depth 0) already covers top-level windows. Params: ref, maxDepth?",
    "epic_call_tool",
    (p) => epicToolCall("SlateInspectorToolset.SlateInspectorToolset", "SlateInspectorToolset.SlateInspectorToolset.Observe", S_epic_observe, p),
  ),
  epic_press_key: bp(
    "mutate",
    "[Epic SlateInspectorToolset.SlateInspectorToolset] Press and release a keyboard key on the currently focused Slate widget. Supports modifier prefixes: \"Ctrl+C\", \"Shift+1\". Params: key",
    "epic_call_tool",
    (p) => epicToolCall("SlateInspectorToolset.SlateInspectorToolset", "SlateInspectorToolset.SlateInspectorToolset.PressKey", S_epic_press_key, p),
  ),
  epic_remove_uicomponent: bp(
    "mutate",
    "[Epic UMGToolSet.UMGToolSet] Removes a UI component of the given class from the named widget. Params: widgetBlueprint, widgetName, componentClass",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.RemoveUIComponent", S_epic_remove_uicomponent, p),
  ),
  epic_remove_widget: bp(
    "mutate",
    "[Epic UMGToolSet.UMGToolSet] Removes a widget and its children from the tree. Params: widgetBlueprint, widget",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.RemoveWidget", S_epic_remove_widget, p),
  ),
  epic_rename_widget: bp(
    "mutate",
    "[Epic UMGToolSet.UMGToolSet] Renames a widget. Returns updated widget info or empty on failure. Params: widgetBlueprint, widget, newDisplayName",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.RenameWidget", S_epic_rename_widget, p),
  ),
  epic_replace_widget_with_child: bp(
    "mutate",
    "[Epic UMGToolSet.UMGToolSet] Replaces a panel widget with its first child, removing the panel from the tree. The widget to replace must be a UPanelWidget with only one child. Params: widgetBlueprint, widgetToReplace",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.ReplaceWidgetWithChild", S_epic_replace_widget_with_child, p),
  ),
  epic_replace_widget_with_named_slot: bp(
    "mutate",
    "[Epic UMGToolSet.UMGToolSet] Replaces a host widget with the content of one of its named slots. The host must implement INamedSlotInterface (e.g., a UUserWidget exposing named slots). The slot's content widget is moved up to take the host's place in the tree. Params: widgetBlueprint, widgetToReplace, namedSlot",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.ReplaceWidgetWithNamedSlot", S_epic_replace_widget_with_named_slot, p),
  ),
  epic_replace_widget_with_template: bp(
    "mutate",
    "[Epic UMGToolSet.UMGToolSet] Replaces a widget instance in the blueprint's widget tree with a new instance created from a different template widget class. Preserves references for members that exist on both classes with a compatible type/signature: bindings, BP graph variable references, animation bindings, and delegate bindings. Members without a compatible counterpart on the new class are listed in the returned report; references to those members in the outer blueprint will become orphaned graph nodes / dangling bindings. Params: widgetBlueprint, widgetToReplace, templateClass",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.ReplaceWidgetWithTemplate", S_epic_replace_widget_with_template, p),
  ),
  epic_screenshot: bp(
    "mutate",
    "[Epic SlateInspectorToolset.SlateInspectorToolset] Screenshot a Slate widget or the active editor window. Prefer this over SceneTools.take_screenshot for Editor UI; use SceneTools only for 3D viewport. Params: ref",
    "epic_call_tool",
    (p) => epicToolCall("SlateInspectorToolset.SlateInspectorToolset", "SlateInspectorToolset.SlateInspectorToolset.Screenshot", S_epic_screenshot, p),
  ),
  epic_select_option: bp(
    "mutate",
    "[Epic SlateInspectorToolset.SlateInspectorToolset] Select an option in a Slate combobox by its text label. Opens the dropdown, finds the matching text, and clicks it. Params: ref, value",
    "epic_call_tool",
    (p) => epicToolCall("SlateInspectorToolset.SlateInspectorToolset", "SlateInspectorToolset.SlateInspectorToolset.SelectOption", S_epic_select_option, p),
  ),
  epic_set_named_slot_content: bp(
    "mutate",
    "[Epic UMGToolSet.UMGToolSet] Sets content for a named slot. Returns full widget info including Slot pointer. Params: widgetBlueprint, hostWidget, slotName, widgetClass, widgetName",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.SetNamedSlotContent", S_epic_set_named_slot_content, p),
  ),
  epic_snapshot: bp(
    "read",
    "[Epic SlateInspectorToolset.SlateInspectorToolset] Capture a Slate UI accessibility snapshot. Use this to read the current widget tree and discover refs for action tools (Click, Type, Hover, etc.). A shallow root observer (depth 0) covers top-level windows automatically. Before interacting with a specific window or panel, call Observe() on it to get deep coverage, then Snapshot that subtree to see its contents. Refs discovered by a previous Snapshot remain usable. You do NOT need to call Snapshot again before every action. Params: ref, maxDepth?, bIncludeSourceLocations?",
    "epic_call_tool",
    (p) => epicToolCall("SlateInspectorToolset.SlateInspectorToolset", "SlateInspectorToolset.SlateInspectorToolset.Snapshot", S_epic_snapshot, p),
  ),
  epic_toggle_widget_as_variable: bp(
    "mutate",
    "[Epic UMGToolSet.UMGToolSet] Sets the bIsVariable flag. Params: widgetBlueprint, widget, bIsVariable",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.ToggleWidgetAsVariable", S_epic_toggle_widget_as_variable, p),
  ),
  epic_type: bp(
    "mutate",
    "[Epic SlateInspectorToolset.SlateInspectorToolset] Type text into a Slate text input widget. Focuses the widget first, then sends one key event per character. Params: ref, text, submit?",
    "epic_call_tool",
    (p) => epicToolCall("SlateInspectorToolset.SlateInspectorToolset", "SlateInspectorToolset.SlateInspectorToolset.Type", S_epic_type, p),
  ),
  epic_unobserve: bp(
    "mutate",
    "[Epic SlateInspectorToolset.SlateInspectorToolset] Remove an observer by its identifier. Params: identifier",
    "epic_call_tool",
    (p) => epicToolCall("SlateInspectorToolset.SlateInspectorToolset", "SlateInspectorToolset.SlateInspectorToolset.Unobserve", S_epic_unobserve, p),
  ),
  epic_wait_for: bp(
    "read",
    "[Epic SlateInspectorToolset.SlateInspectorToolset] Check if text is present or absent in the Slate widget tree. Non-blocking: checks once and returns immediately. Poll to wait. Params: text, textGone",
    "epic_call_tool",
    (p) => epicToolCall("SlateInspectorToolset.SlateInspectorToolset", "SlateInspectorToolset.SlateInspectorToolset.WaitFor", S_epic_wait_for, p),
  ),
  epic_windows: bp(
    "unknown",
    "[Epic SlateInspectorToolset.SlateInspectorToolset] List, select, or close top-level Slate editor windows. Params: action?, index?",
    "epic_call_tool",
    (p) => epicToolCall("SlateInspectorToolset.SlateInspectorToolset", "SlateInspectorToolset.SlateInspectorToolset.Windows", S_epic_windows, p),
  ),
  epic_wrap_widgets: bp(
    "mutate",
    "[Epic UMGToolSet.UMGToolSet] Wraps one or more widgets in a new panel widget of the specified class. Only the root-most widgets in the selection are wrapped - children of other selected widgets are skipped because their parent will be wrapped. Returns info for each newly created wrapper. Use ObjectTools.list_properties on each returned Widget and Slot to discover property names before calling set_properties (padding, alignment, anchors, etc. vary per panel class). Params: widgetBlueprint, widgets, wrapperClass",
    "epic_call_tool",
    (p) => epicToolCall("UMGToolSet.UMGToolSet", "UMGToolSet.UMGToolSet.WrapWidgets", S_epic_wrap_widgets, p),
  ),
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  action: z.string().optional().describe("\"list\" returns JSON array, \"select\" brings to front, \"close\" destroys."),
  assetName: z.string().optional().describe("Name for the new blueprint asset."),
  bIncludeSourceLocations: z.boolean().optional().describe("Include [src=File:Line] tags showing where each widget was created in C++."),
  bIsVariable: z.boolean().optional().describe("True to expose as a blueprint variable, false to hide it."),
  bMoveAfter: z.boolean().optional().describe("True to place after RelativeToComponentClass, false to place before."),
  button: z.string().optional().describe("\"left\", \"right\", or \"middle\"."),
  childIndex: z.number().optional().describe("Position in parent's child list (0 = first child). -1 (default) appends to end."),
  componentClass: z.union([z.string(), z.record(z.unknown())]).optional().describe("The UIComponent subclass to add."),
  componentClassToMove: z.union([z.string(), z.record(z.unknown())]).optional().describe("The UIComponent subclass to reorder."),
  doubleClick: z.boolean().optional().describe("True for double-click."),
  endRef: z.string().optional().describe("Slate widget ref for the drop target."),
  eventName: z.string().optional().describe("Name of the multicast delegate UPROPERTY on PropertyClass (e.g. \"OnClicked\")."),
  fields: z.array(z.unknown()).optional().describe("Array of {Ref, Value, FieldType} where FieldType is \"textbox\", \"checkbox\", or \"combobox\"."),
  filter: z.string().optional().describe("Substring to match against class names. Pass empty string to return all classes."),
  folderPath: z.string().optional().describe("Content folder path, e.g. \"/Game/UI/Widgets\"."),
  hostWidget: z.union([z.string(), z.record(z.unknown())]).optional().describe("The widget that owns the named slot, or null to target the root WidgetTree."),
  identifier: z.string().optional().describe("The identifier returned by Observe()."),
  index: z.number().optional().describe("Window index for select/close."),
  key: z.string().optional().describe("Key name with optional modifiers, e.g. \"Enter\", \"Ctrl+A\", \"Shift+Tab\"."),
  maxDepth: z.number().optional().describe("Maximum depth to walk from the root."),
  modifiers: z.record(z.unknown()).optional().describe("Modifier keys held during the click."),
  namedSlot: z.string().optional().describe("The slot whose content replaces WidgetToReplace."),
  newDisplayName: z.string().optional().describe("The new display name."),
  newParent: z.union([z.string(), z.record(z.unknown())]).optional().describe("The destination panel widget."),
  parentClass: z.union([z.string(), z.record(z.unknown())]).optional().describe("The parent UUserWidget class. Get this from GetWidgets Info.ParentClass on the source blueprint."),
  parentWidget: z.union([z.string(), z.record(z.unknown())]).optional().describe("The panel widget to add to. Pass null to add to root, or to make this widget the root if the tree is empty."),
  propertyClass: z.union([z.string(), z.record(z.unknown())]).optional().describe("Class declaring the delegate, typically the widget's class (e.g. UButton::StaticClass())."),
  propertyName: z.string().optional().describe("Name of the blueprint variable owning the event."),
  ref: z.string().optional().describe("Slate widget ref."),
  relativeToComponentClass: z.union([z.string(), z.record(z.unknown())]).optional().describe("The UIComponent subclass to move relative to."),
  slotName: z.string().optional().describe("Name of the slot to fill (e.g., \"content\", \"header\")."),
  startRef: z.string().optional().describe("Slate widget ref for the drag source."),
  startWidget: z.union([z.string(), z.record(z.unknown())]).optional().describe("nullptr = full tree from root."),
  submit: z.boolean().optional().describe("Press Enter after typing."),
  templateClass: z.union([z.string(), z.record(z.unknown())]).optional().describe("The widget class to create the replacement from."),
  text: z.string().optional().describe("Text to type."),
  textGone: z.string().optional().describe("Text that must be absent (empty = skip)."),
  value: z.string().optional().describe("Exact option text to select."),
  widget: z.union([z.string(), z.record(z.unknown())]).optional().describe("The widget to move."),
  widgetBlueprint: z.union([z.string(), z.record(z.unknown())]).optional().describe("The widget blueprint to modify."),
  widgetClass: z.union([z.string(), z.record(z.unknown())]).optional().describe("The widget class to instantiate."),
  widgetDisplayName: z.string().optional().describe("Display name for the new widget instance."),
  widgetName: z.string().optional().describe("Name of the widget instance to add the component to."),
  widgets: z.array(z.unknown()).optional().describe("The widgets to wrap. Must all be in WidgetBlueprint's tree."),
  widgetToReplace: z.union([z.string(), z.record(z.unknown())]).optional().describe("The panel widget to replace with its first child."),
  wrapperClass: z.union([z.string(), z.record(z.unknown())]).optional().describe("The panel widget class to wrap with (must be a UPanelWidget subclass)."),
};
