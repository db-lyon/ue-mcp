// GENERATED FILE - do not edit.
//
// Written by scripts/generate-epic-actions.mjs from tests/golden/epic-catalog/
// and src/tools/epic/effects.ts. To change what an action DOES, edit the
// effects file and regenerate; to pick up a new engine's toolsets, re-record
// the catalog and regenerate.
//
// These are ordinary actions. They carry a declared effect, real parameters and
// a Params: clause, they are in ALL_TOOLS, and they dispatch through the same
// task factory, guards and locks as every hand-written action in this package.
// Each also carries its input schema, which the compact signatures read (#1172).
import { z } from "zod";
import type { ActionSpec } from "../../core/types.js";
import { bp } from "../../surface/category-tool.js";

const S_epic_activate_widget = {"properties":{"widgetName":{"type":"string"},"localPlayerIndex":{"type":"integer"}},"required":["widgetName"]} as const;
const S_epic_add_uicomponent = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgetName":{"type":"string"},"componentClass":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","widgetName","componentClass"]} as const;
const S_epic_add_uicomponent__1 = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgetName":{"type":"string"},"componentClass":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","widgetName","componentClass"]} as const;
const S_epic_add_view_condition = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"sourceContext":{"type":"object","properties":{"refPath":{}}},"sourcePropertyPath":{"type":"string"},"destinationContext":{"type":"object","properties":{"refPath":{}}},"destinationPropertyPath":{"type":"string"},"operation":{"type":"string","enum":["Equal","NotEqual","MoreThan","MoreThanOrEqual","LessThan","LessThanOrEqual","BetweenInclusive","BetweenExclusive"]},"value":{"type":"number"},"maxValue":{"type":"number"}},"required":["widgetBlueprint","sourceContext","sourcePropertyPath","destinationContext","destinationPropertyPath","operation","value"]} as const;
const S_epic_add_view_model_property = {"properties":{"viewModel":{"type":"object","properties":{"refPath":{}}},"propertyName":{"type":"string"},"propertyType":{"type":"string"},"defaultValue":{"type":"string"}},"required":["viewModel","propertyName","propertyType","defaultValue"]} as const;
const S_epic_add_view_model_to_widget = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"viewModelClass":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","viewModelClass"]} as const;
const S_epic_add_widget = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgetClass":{"type":"object","properties":{"refPath":{}}},"widgetDisplayName":{"type":"string"},"parentWidget":{"type":"object","properties":{"refPath":{}}},"childIndex":{"type":"integer"}},"required":["widgetBlueprint","widgetClass","widgetDisplayName"]} as const;
const S_epic_add_widget_to_animation = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"animation":{"type":"object","properties":{"refPath":{}}},"objectToBind":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","animation","objectToBind"]} as const;
const S_epic_analog_input = {"properties":{"key":{"type":"string"},"value":{"type":"number"},"userIndex":{"type":"integer"}},"required":["key","value"]} as const;
const S_epic_bind_to_event_property = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"eventName":{"type":"string"},"propertyName":{"type":"string"},"propertyClass":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","eventName","propertyName","propertyClass"]} as const;
const S_epic_bind_to_event_property__1 = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"eventName":{"type":"string"},"propertyName":{"type":"string"},"propertyClass":{"type":"object","properties":{"refPath":{}}},"bShouldJumpToNode":{"type":"boolean"}},"required":["widgetBlueprint","eventName","propertyName","propertyClass"]} as const;
const S_epic_clear_conversion_function_argument_path = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"bindingId":{"type":"string"},"argumentName":{"type":"string"},"bSourceToDestination":{"type":"boolean"}},"required":["widgetBlueprint","bindingId","argumentName"]} as const;
const S_epic_clear_focus_log = {"properties":{}} as const;
const S_epic_clear_highlight = {"properties":{}} as const;
const S_epic_clear_widget_lifetime_log = {"properties":{}} as const;
const S_epic_click = {"properties":{"ref":{"type":"string"},"button":{"type":"string"},"doubleClick":{"type":"boolean"},"modifiers":{"type":"object","properties":{"bShift":{"type":"boolean"},"bCtrl":{"type":"boolean"},"bAlt":{"type":"boolean"},"bCmd":{"type":"boolean"}}},"params":{"type":"object","properties":{"button":{"type":"string"},"bDoubleClick":{"type":"boolean"},"modifiers":{"type":"object","properties":{"bShift":{"type":"boolean"},"bCtrl":{"type":"boolean"},"bAlt":{"type":"boolean"},"bCmd":{"type":"boolean"}}},"position":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}}}}},"required":["ref"]} as const;
const S_epic_compile_widget_blueprint = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint"]} as const;
const S_epic_compile_widget_blueprint__1 = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint"]} as const;
const S_epic_create_and_add_widget = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgetClass":{"type":"object","properties":{"refPath":{}}},"widgetDisplayName":{"type":"string"},"parentWidget":{"type":"object","properties":{"refPath":{}}},"childIndex":{"type":"integer"}},"required":["widgetBlueprint","widgetClass","widgetDisplayName"]} as const;
const S_epic_create_view_binding = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"sourceContext":{"type":"object","properties":{"refPath":{}}},"sourcePropertyPath":{"type":"string"},"destinationContext":{"type":"object","properties":{"refPath":{}}},"destinationPropertyPath":{"type":"string"},"conversionName":{"type":"string"}},"required":["widgetBlueprint","sourceContext","sourcePropertyPath","destinationContext","destinationPropertyPath","conversionName"]} as const;
const S_epic_create_view_event_binding = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"eventContext":{"type":"object","properties":{"refPath":{}}},"eventPropertyPath":{"type":"string"},"destinationContext":{"type":"object","properties":{"refPath":{}}},"destinationPropertyPath":{"type":"string"}},"required":["widgetBlueprint","eventContext","eventPropertyPath","destinationContext","destinationPropertyPath"]} as const;
const S_epic_create_view_model = {"properties":{"viewModelName":{"type":"string"},"packagePath":{"type":"string"},"parentClass":{"type":"object","properties":{"refPath":{}}}},"required":["viewModelName","packagePath","parentClass"]} as const;
const S_epic_create_widget_animation = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"animationName":{"type":"string"},"lengthSeconds":{"type":"number"}},"required":["widgetBlueprint","animationName"]} as const;
const S_epic_create_widget_blueprint = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"},"parentClass":{"type":"object","properties":{"refPath":{}}}},"required":["folderPath","assetName","parentClass"]} as const;
const S_epic_create_widget_blueprint__1 = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"},"parentClass":{"type":"object","properties":{"refPath":{}}}},"required":["folderPath","assetName","parentClass"]} as const;
const S_epic_deactivate_widget = {"properties":{"widgetName":{"type":"string"},"localPlayerIndex":{"type":"integer"}},"required":["widgetName"]} as const;
const S_epic_describe_widget = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"startWidget":{"type":"object","properties":{"refPath":{}}},"maxDepth":{"type":"integer"},"propertyNameFilter":{"type":"string"},"bSkipDefaultValues":{"type":"boolean"},"bIncludeFullDescription":{"type":"boolean"},"bIncludeComposedColors":{"type":"boolean"}},"required":["widgetBlueprint"]} as const;
const S_epic_drag = {"properties":{"startRef":{"type":"string"},"endRef":{"type":"string"},"modifiers":{"type":"object","properties":{"bShift":{"type":"boolean"},"bCtrl":{"type":"boolean"},"bAlt":{"type":"boolean"},"bCmd":{"type":"boolean"}}},"params":{"type":"object","properties":{"modifiers":{"type":"object","properties":{"bShift":{"type":"boolean"},"bCtrl":{"type":"boolean"},"bAlt":{"type":"boolean"},"bCmd":{"type":"boolean"}}},"button":{"type":"string"},"waypoints":{"type":"object","properties":{"points":{"type":"array","items":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}}}}},"startPosition":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}},"endPosition":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}},"moveByX":{"type":"number"},"moveByY":{"type":"number"}}}},"required":["startRef","endRef"]} as const;
const S_epic_fill_form = {"properties":{"fields":{"type":"array","items":{"type":"object","properties":{"ref":{"type":"string"},"value":{"type":"string"},"fieldType":{"type":"string"}}}}},"required":["fields"]} as const;
const S_epic_find_widget_animation = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"animationName":{"type":"string"}},"required":["widgetBlueprint","animationName"]} as const;
const S_epic_fixup_mvvmdata = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint"]} as const;
const S_epic_focus = {"properties":{"ref":{"type":"string"},"userIndex":{"type":"integer"}},"required":["ref"]} as const;
const S_epic_get_action_bindings = {"properties":{"localPlayerIndex":{"type":"integer"}}} as const;
const S_epic_get_activatable_tree = {"properties":{"localPlayerIndex":{"type":"integer"},"bIncludeFullDump":{"type":"boolean"}}} as const;
const S_epic_get_container_widget_navigation_doc = {"properties":{"widgetClass":{"type":"object","properties":{"refPath":{}}}},"required":["widgetClass"]} as const;
const S_epic_get_current_focus = {"properties":{"userIndex":{"type":"integer"}}} as const;
const S_epic_get_event_logging_status = {"properties":{}} as const;
const S_epic_get_event_timeline = {"properties":{"maxCount":{"type":"integer"},"eventTypeFilter":{"type":"string"},"localPlayerIndex":{"type":"integer"}}} as const;
const S_epic_get_focus_event_detail = {"properties":{"seq":{"type":"integer"}},"required":["seq"]} as const;
const S_epic_get_focus_events = {"properties":{"widgetNameFilter":{"type":"string"},"eventTypeFilter":{"type":"string"},"focusCauseFilter":{"type":"string"},"userIndex":{"type":"integer"},"maxCount":{"type":"integer"},"bIncludePath":{"type":"boolean"}},"required":["widgetNameFilter","eventTypeFilter"]} as const;
const S_epic_get_focus_log_status = {"properties":{}} as const;
const S_epic_get_focus_state = {"properties":{"localPlayerIndex":{"type":"integer"}}} as const;
const S_epic_get_focus_timeline = {"properties":{"maxCount":{"type":"integer"},"eventTypeFilter":{"type":"string"},"focusCauseFilter":{"type":"string"},"userIndex":{"type":"integer"}}} as const;
const S_epic_get_input_routing_state = {"properties":{"localPlayerIndex":{"type":"integer"}}} as const;
const S_epic_get_logged_events = {"properties":{"widgetNameFilter":{"type":"string"},"eventTypeFilter":{"type":"string"},"localPlayerIndex":{"type":"integer"},"maxCount":{"type":"integer"},"bIncludeCallstacks":{"type":"boolean"}}} as const;
const S_epic_get_named_slots = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint"]} as const;
const S_epic_get_named_slots__1 = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint"]} as const;
const S_epic_get_query_syntax = {"properties":{}} as const;
const S_epic_get_service_status = {"properties":{}} as const;
const S_epic_get_umginfo = {"properties":{"ref":{"type":"string"}},"required":["ref"]} as const;
const S_epic_get_view_model_connection_map = {"properties":{"viewModelClass":{"type":"object","properties":{"refPath":{}}},"scopeFilter":{"type":"string"}},"required":["viewModelClass"]} as const;
const S_epic_get_widget_animation_bindings = {"properties":{"animation":{"type":"object","properties":{"refPath":{}}}},"required":["animation"]} as const;
const S_epic_get_widget_class_info = {"properties":{"widgetClass":{"type":"object","properties":{"refPath":{}}}},"required":["widgetClass"]} as const;
const S_epic_get_widget_class_info__1 = {"properties":{"widgetClass":{"type":"object","properties":{"refPath":{}}}},"required":["widgetClass"]} as const;
const S_epic_get_widget_connection_map = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"scopeFilter":{"type":"string"}},"required":["widgetBlueprint"]} as const;
const S_epic_get_widget_description = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"startWidget":{"type":"object","properties":{"refPath":{}}},"maxDepth":{"type":"integer"}},"required":["widgetBlueprint"]} as const;
const S_epic_get_widget_lifetime_events = {"properties":{"nameFilter":{"type":"string"},"classFilter":{"type":"string"},"eventTypeFilter":{"type":"string"},"maxCount":{"type":"integer"},"playerFilter":{"type":"integer"}}} as const;
const S_epic_get_widget_lifetime_log_status = {"properties":{}} as const;
const S_epic_get_widget_preview_active = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint"]} as const;
const S_epic_get_widget_tree_depth = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"startWidget":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint"]} as const;
const S_epic_get_widget_tree_depth__widget_static_analysis_toolset = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"startWidget":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint"]} as const;
const S_epic_get_widget_view_config = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint"]} as const;
const S_epic_get_widgets = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint"]} as const;
const S_epic_get_widgets__1 = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint"]} as const;
const S_epic_highlight = {"properties":{"request":{"type":"object","properties":{"query":{"type":"string"}}}},"required":["request"]} as const;
const S_epic_hit_test = {"properties":{"ref":{"type":"string"},"params":{"type":"object","properties":{"position":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}},"grid":{"type":"integer"}}}},"required":["ref"]} as const;
const S_epic_hover = {"properties":{"ref":{"type":"string"},"params":{"type":"object","properties":{"position":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}}}}},"required":["ref"]} as const;
const S_epic_key_down = {"properties":{"key":{"type":"string"},"userIndex":{"type":"integer"}},"required":["key"]} as const;
const S_epic_key_up = {"properties":{"key":{"type":"string"},"userIndex":{"type":"integer"}},"required":["key"]} as const;
const S_epic_last_refusal = {"properties":{}} as const;
const S_epic_list_bindable_widget_properties = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widget":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","widget"]} as const;
const S_epic_list_binding_conversion_options = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"sourceContext":{"type":"object","properties":{"refPath":{}}},"sourcePropertyPath":{"type":"string"},"destinationContext":{"type":"object","properties":{"refPath":{}}},"destinationPropertyPath":{"type":"string"}},"required":["widgetBlueprint","sourceContext","sourcePropertyPath","destinationContext","destinationPropertyPath"]} as const;
const S_epic_list_class_bindable_fields = {"properties":{"targetClass":{"type":"object","properties":{"refPath":{}}},"accessorClass":{"type":"object","properties":{"refPath":{}}}},"required":["targetClass","accessorClass"]} as const;
const S_epic_list_conversion_functions = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint"]} as const;
const S_epic_list_observers = {"properties":{}} as const;
const S_epic_list_property_path_bindable_fields = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"context":{"type":"object","properties":{"refPath":{}}},"propertyPath":{"type":"string"}},"required":["widgetBlueprint","context","propertyPath"]} as const;
const S_epic_list_view_conditions = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint"]} as const;
const S_epic_list_view_models = {"properties":{"searchPath":{"type":"string"}},"required":["searchPath"]} as const;
const S_epic_list_widget_animations = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint"]} as const;
const S_epic_list_widget_blueprints = {"properties":{"folderPath":{"type":"string"}},"required":["folderPath"]} as const;
const S_epic_list_widget_blueprints__1 = {"properties":{"folderPath":{"type":"string"}},"required":["folderPath"]} as const;
const S_epic_list_widget_classes = {"properties":{"filter":{"type":"string"}},"required":["filter"]} as const;
const S_epic_list_widget_classes__1 = {"properties":{"filter":{"type":"string"}}} as const;
const S_epic_list_widget_view_bindings = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint"]} as const;
const S_epic_list_widget_view_event_bindings = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint"]} as const;
const S_epic_list_widget_view_events = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint"]} as const;
const S_epic_list_widget_view_models = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint"]} as const;
const S_epic_mouse_down = {"properties":{"ref":{"type":"string"},"button":{"type":"string"},"modifiers":{"type":"object","properties":{"bShift":{"type":"boolean"},"bCtrl":{"type":"boolean"},"bAlt":{"type":"boolean"},"bCmd":{"type":"boolean"}}}},"required":["ref"]} as const;
const S_epic_mouse_move = {"properties":{"x":{"type":"number"},"y":{"type":"number"},"modifiers":{"type":"object","properties":{"bShift":{"type":"boolean"},"bCtrl":{"type":"boolean"},"bAlt":{"type":"boolean"},"bCmd":{"type":"boolean"}}}},"required":["x","y"]} as const;
const S_epic_mouse_move_to = {"properties":{"ref":{"type":"string"},"offsetX":{"type":"number"},"offsetY":{"type":"number"},"modifiers":{"type":"object","properties":{"bShift":{"type":"boolean"},"bCtrl":{"type":"boolean"},"bAlt":{"type":"boolean"},"bCmd":{"type":"boolean"}}}},"required":["ref"]} as const;
const S_epic_mouse_up = {"properties":{"button":{"type":"string"},"modifiers":{"type":"object","properties":{"bShift":{"type":"boolean"},"bCtrl":{"type":"boolean"},"bAlt":{"type":"boolean"},"bCmd":{"type":"boolean"}}}}} as const;
const S_epic_move_uicomponent = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgetName":{"type":"string"},"componentClassToMove":{"type":"object","properties":{"refPath":{}}},"relativeToComponentClass":{"type":"object","properties":{"refPath":{}}},"bMoveAfter":{"type":"boolean"}},"required":["widgetBlueprint","widgetName","componentClassToMove","relativeToComponentClass","bMoveAfter"]} as const;
const S_epic_move_uicomponent__1 = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgetName":{"type":"string"},"componentClassToMove":{"type":"object","properties":{"refPath":{}}},"relativeToComponentClass":{"type":"object","properties":{"refPath":{}}},"bMoveAfter":{"type":"boolean"}},"required":["widgetBlueprint","widgetName","componentClassToMove","relativeToComponentClass","bMoveAfter"]} as const;
const S_epic_move_widget = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widget":{"type":"object","properties":{"refPath":{}}},"newParent":{"type":"object","properties":{"refPath":{}}},"childIndex":{"type":"integer"}},"required":["widgetBlueprint","widget","newParent"]} as const;
const S_epic_move_widget__1 = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widget":{"type":"object","properties":{"refPath":{}}},"newParent":{"type":"object","properties":{"refPath":{}}},"childIndex":{"type":"integer"}},"required":["widgetBlueprint","widget","newParent"]} as const;
const S_epic_observe = {"properties":{"ref":{"type":"string"},"maxDepth":{"type":"integer"}},"required":["ref"]} as const;
const S_epic_press_key = {"properties":{"key":{"type":"string"},"userIndex":{"type":"integer"},"repeatCount":{"type":"integer"}},"required":["key"]} as const;
const S_epic_query = {"properties":{"request":{"type":"object","properties":{"query":{"type":"string"},"limit":{"type":"integer"},"offset":{"type":"integer"}}}},"required":["request"]} as const;
const S_epic_read_preview_widget_property = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgetName":{"type":"string"},"propertyName":{"type":"string"}},"required":["widgetBlueprint","widgetName","propertyName"]} as const;
const S_epic_reconstruct_focus_path = {"properties":{"widgetName":{"type":"string"},"occurrenceFromEnd":{"type":"integer"}},"required":["widgetName"]} as const;
const S_epic_refresh_active_root_focus = {"properties":{"localPlayerIndex":{"type":"integer"}}} as const;
const S_epic_release_held_input = {"properties":{}} as const;
const S_epic_remove_input_type_lock = {"properties":{"reason":{"type":"string"},"localPlayerIndex":{"type":"integer"}}} as const;
const S_epic_remove_uicomponent = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgetName":{"type":"string"},"componentClass":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","widgetName","componentClass"]} as const;
const S_epic_remove_uicomponent__1 = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgetName":{"type":"string"},"componentClass":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","widgetName","componentClass"]} as const;
const S_epic_remove_view_condition = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"condition":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","condition"]} as const;
const S_epic_remove_widget = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widget":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","widget"]} as const;
const S_epic_remove_widget__1 = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widget":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","widget"]} as const;
const S_epic_remove_widget_animation = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"animation":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","animation"]} as const;
const S_epic_remove_widget_binding = {"properties":{"animation":{"type":"object","properties":{"refPath":{}}},"bindingGuid":{"type":"string"}},"required":["animation","bindingGuid"]} as const;
const S_epic_remove_widget_view_binding = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"bindingId":{"type":"string"}},"required":["widgetBlueprint","bindingId"]} as const;
const S_epic_rename_widget = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widget":{"type":"object","properties":{"refPath":{}}},"newDisplayName":{"type":"string"}},"required":["widgetBlueprint","widget","newDisplayName"]} as const;
const S_epic_rename_widget__1 = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widget":{"type":"object","properties":{"refPath":{}}},"newDisplayName":{"type":"string"}},"required":["widgetBlueprint","widget","newDisplayName"]} as const;
const S_epic_rename_widget_animation = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"animation":{"type":"object","properties":{"refPath":{}}},"newName":{"type":"string"}},"required":["widgetBlueprint","animation","newName"]} as const;
const S_epic_replace_widget_with_child = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgetToReplace":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","widgetToReplace"]} as const;
const S_epic_replace_widget_with_child__1 = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgetToReplace":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","widgetToReplace"]} as const;
const S_epic_replace_widget_with_named_slot = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgetToReplace":{"type":"object","properties":{"refPath":{}}},"namedSlot":{"type":"string"}},"required":["widgetBlueprint","widgetToReplace","namedSlot"]} as const;
const S_epic_replace_widget_with_named_slot__1 = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgetToReplace":{"type":"object","properties":{"refPath":{}}},"namedSlot":{"type":"string"},"bWarnAndAskUser":{"type":"boolean"}},"required":["widgetBlueprint","widgetToReplace","namedSlot"]} as const;
const S_epic_replace_widget_with_template = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgetToReplace":{"type":"object","properties":{"refPath":{}}},"templateClass":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","widgetToReplace","templateClass"]} as const;
const S_epic_replace_widget_with_template__1 = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgetToReplace":{"type":"object","properties":{"refPath":{}}},"templateClass":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","widgetToReplace","templateClass"]} as const;
const S_epic_screenshot = {"properties":{"ref":{"type":"string"}},"required":["ref"]} as const;
const S_epic_scroll = {"properties":{"ref":{"type":"string"},"params":{"type":"object","properties":{"ticks":{"type":"integer"},"gestureX":{"type":"number"},"gestureY":{"type":"number"},"modifiers":{"type":"object","properties":{"bShift":{"type":"boolean"},"bCtrl":{"type":"boolean"},"bAlt":{"type":"boolean"},"bCmd":{"type":"boolean"}}},"position":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}}}}},"required":["ref"]} as const;
const S_epic_select_option = {"properties":{"ref":{"type":"string"},"value":{"type":"string"}},"required":["ref","value"]} as const;
const S_epic_set_binding_mode = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"bindingId":{"type":"string"},"mode":{"type":"string","enum":["OneTimeToDestination","OneWayToDestination","TwoWay","OneWayToSource"]}},"required":["widgetBlueprint","bindingId","mode"]} as const;
const S_epic_set_conversion_function_argument_literal = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"bindingId":{"type":"string"},"argumentName":{"type":"string"},"value":{"type":"string"},"bSourceToDestination":{"type":"boolean"}},"required":["widgetBlueprint","bindingId","argumentName","value"]} as const;
const S_epic_set_event_binding_argument_literal = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"event":{"type":"object","properties":{"refPath":{}}},"argumentName":{"type":"string"},"value":{"type":"string"}},"required":["widgetBlueprint","event","argumentName","value"]} as const;
const S_epic_set_input_type_lock = {"properties":{"inputType":{"type":"string"},"reason":{"type":"string"},"localPlayerIndex":{"type":"integer"}},"required":["inputType"]} as const;
const S_epic_set_named_slot_content = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"hostWidget":{"type":"object","properties":{"refPath":{}}},"slotName":{"type":"string"},"widgetClass":{"type":"object","properties":{"refPath":{}}},"widgetName":{"type":"string"}},"required":["widgetBlueprint","hostWidget","slotName","widgetClass","widgetName"]} as const;
const S_epic_set_named_slot_content__1 = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"slotName":{"type":"string"},"widgetClass":{"type":"object","properties":{"refPath":{}}},"widgetName":{"type":"string"},"hostWidget":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","slotName","widgetClass","widgetName"]} as const;
const S_epic_set_tree_visualization = {"properties":{"bEnabled":{"type":"boolean"},"bIncludeInactive":{"type":"boolean"},"bShowLabels":{"type":"boolean"},"bLeafmostOnly":{"type":"boolean"},"bShowLegend":{"type":"boolean"}},"required":["bEnabled"]} as const;
const S_epic_set_view_condition_operation = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"condition":{"type":"object","properties":{"refPath":{}}},"operation":{"type":"string","enum":["Equal","NotEqual","MoreThan","MoreThanOrEqual","LessThan","LessThanOrEqual","BetweenInclusive","BetweenExclusive"]},"value":{"type":"number"},"maxValue":{"type":"number"}},"required":["widgetBlueprint","condition","operation","value"]} as const;
const S_epic_set_widget_animation_display_label = {"properties":{"animation":{"type":"object","properties":{"refPath":{}}},"displayLabel":{"type":"string"}},"required":["animation","displayLabel"]} as const;
const S_epic_set_widget_animation_length = {"properties":{"animation":{"type":"object","properties":{"refPath":{}}},"lengthSeconds":{"type":"number"}},"required":["animation","lengthSeconds"]} as const;
const S_epic_set_widget_preview_active = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"bActive":{"type":"boolean"}},"required":["widgetBlueprint","bActive"]} as const;
const S_epic_set_widget_view_binding_enabled = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"bindingId":{"type":"string"},"bEnabled":{"type":"boolean"},"bCompile":{"type":"boolean"}},"required":["widgetBlueprint","bindingId"]} as const;
const S_epic_set_widget_view_config = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"configuration":{"type":"object","properties":{"bInitializeSourcesOnConstruct":{"type":"boolean"},"bInitializeBindingsOnConstruct":{"type":"boolean"},"bInitializeEventsOnConstruct":{"type":"boolean"},"bCreateViewWithoutBindings":{"type":"boolean"}}}},"required":["widgetBlueprint","configuration"]} as const;
const S_epic_show_widget_lifetime_log_window = {"properties":{}} as const;
const S_epic_snapshot = {"properties":{"ref":{"type":"string"},"maxDepth":{"type":"integer"},"bIncludeSourceLocations":{"type":"boolean"}},"required":["ref"]} as const;
const S_epic_start_event_logging = {"properties":{"bCaptureCallstacks":{"type":"boolean"},"bufferSize":{"type":"integer"}}} as const;
const S_epic_start_focus_recording = {"properties":{"bCaptureCallstack":{"type":"boolean"},"bCaptureContext":{"type":"boolean"},"bufferSize":{"type":"integer"}}} as const;
const S_epic_start_service = {"properties":{}} as const;
const S_epic_start_widget_lifetime_logging = {"properties":{"bufferSize":{"type":"integer"},"bCaptureInvalidations":{"type":"boolean"}}} as const;
const S_epic_stop_event_logging = {"properties":{}} as const;
const S_epic_stop_focus_recording = {"properties":{}} as const;
const S_epic_stop_service = {"properties":{}} as const;
const S_epic_stop_widget_lifetime_logging = {"properties":{}} as const;
const S_epic_toggle_widget_as_variable = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widget":{"type":"object","properties":{"refPath":{}}},"bIsVariable":{"type":"boolean"}},"required":["widgetBlueprint","widget","bIsVariable"]} as const;
const S_epic_toggle_widget_as_variable__1 = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widget":{"type":"object","properties":{"refPath":{}}},"bIsVariable":{"type":"boolean"},"bMarkBlueprintModified":{"type":"boolean"}},"required":["widgetBlueprint","widget","bIsVariable"]} as const;
const S_epic_touch_end = {"properties":{"x":{"type":"number"},"y":{"type":"number"},"touchIndex":{"type":"integer"}},"required":["x","y"]} as const;
const S_epic_touch_move = {"properties":{"x":{"type":"number"},"y":{"type":"number"},"touchIndex":{"type":"integer"}},"required":["x","y"]} as const;
const S_epic_touch_start = {"properties":{"ref":{"type":"string"},"x":{"type":"number"},"y":{"type":"number"},"touchIndex":{"type":"integer"}},"required":["ref"]} as const;
const S_epic_trace_widget_property_overrides = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"propertyNameFilter":{"type":"string"},"widgetNameFilter":{"type":"string"}},"required":["widgetBlueprint"]} as const;
const S_epic_type = {"properties":{"ref":{"type":"string"},"text":{"type":"string"},"submit":{"type":"boolean"}},"required":["ref","text"]} as const;
const S_epic_unobserve = {"properties":{"identifier":{"type":"string"}},"required":["identifier"]} as const;
const S_epic_wait_for = {"properties":{"text":{"type":"string"},"textGone":{"type":"string"}}} as const;
const S_epic_wait_for_query = {"properties":{"request":{"type":"object","properties":{"comparison":{"type":"string","enum":["GreaterThan","GreaterOrEqual","LessThan","LessOrEqual","Equal","NotEqual"]},"threshold":{"type":"integer"},"timeoutSeconds":{"type":"number"},"query":{"type":"string"},"limit":{"type":"integer"},"offset":{"type":"integer"}}}},"required":["request"]} as const;
const S_epic_windows = {"properties":{"action":{"type":"string"},"index":{"type":"integer"},"width":{"type":"integer"},"height":{"type":"integer"},"ref":{"type":"string"}}} as const;
const S_epic_wrap_widgets = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgets":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"wrapperClass":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","widgets","wrapperClass"]} as const;
const S_epic_wrap_widgets__1 = {"properties":{"widgetBlueprint":{"type":"object","properties":{"refPath":{}}},"widgets":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"wrapperClass":{"type":"object","properties":{"refPath":{}}}},"required":["widgetBlueprint","widgets","wrapperClass"]} as const;

/** 139 wrapped engine tools routed to the `widget` category. */
export const actions: Record<string, ActionSpec> = {
  epic_activate_widget: {
    epicSchema: S_epic_activate_widget,
    epicTool: { toolset: "CommonUIToolset.CommonUIToolset", name: "CommonUIToolset.CommonUIToolset.ActivateWidget", engines: ["5.9"] },
    ...bp("mutate", "[Epic CommonUIToolset.CommonUIToolset] Activate a live activatable widget, as if the game had activated it. Available on UE 5.9; absent from UE 5.8. Params: widgetName, localPlayerIndex?", "epic_call_tool"),
  },
  epic_add_uicomponent: {
    epicSchema: S_epic_add_uicomponent,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.AddUIComponent", engines: ["5.8"], successors: [{ toolset: "UMGEditor.WidgetBlueprintEditorSubsystem", name: "UMGEditor.WidgetBlueprintEditorSubsystem.AddUIComponent", engines: ["5.9"], schema: S_epic_add_uicomponent__1 }] },
    ...bp("mutate", "[Epic UMGToolSet.UMGToolSet] Adds a UI component of the given class to the named widget. Params: widgetBlueprint, widgetName, componentClass. On UE 5.9 it calls UMGEditor.WidgetBlueprintEditorSubsystem.AddUIComponent (Params: widgetBlueprint, widgetName, componentClass).", "epic_call_tool"),
  },
  epic_add_view_condition: {
    epicSchema: S_epic_add_view_condition,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.AddViewCondition", engines: ["5.9"] },
    ...bp("mutate", "[Epic MVVMToolset.MVVMToolset] Adds a condition to the WidgetBlueprint's View: when the source value changes and compares true against Value (Value to MaxValue for a Between operation), the destination is called or set. The widget blueprint is then compiled; compile errors do not fail the call and are reported in the condition's CompileErrors. Not supported during PIE. Values are compared as 32-bit floats. A destination function is called with default parameter values, and a destination property is set to its type's default value, not to the source value. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, sourceContext, sourcePropertyPath, destinationContext, destinationPropertyPath, operation, value, maxValue?", "epic_call_tool"),
  },
  epic_add_view_model_property: {
    epicSchema: S_epic_add_view_model_property,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.AddViewModelProperty", engines: ["5.9"] },
    ...bp("mutate", "[Epic MVVMToolset.MVVMToolset] Add a property to an existing ViewModel. The ViewModel is compiled automatically when it is next added to a widget or used in a binding. Available on UE 5.9; absent from UE 5.8. Params: viewModel, propertyName, propertyType, defaultValue", "epic_call_tool"),
  },
  epic_add_view_model_to_widget: {
    epicSchema: S_epic_add_view_model_to_widget,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.AddViewModelToWidget", engines: ["5.9"] },
    ...bp("mutate", "[Epic MVVMToolset.MVVMToolset] Adds a ViewModel to the WidgetBlueprint Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, viewModelClass", "epic_call_tool"),
  },
  epic_add_widget: {
    epicSchema: S_epic_add_widget,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.AddWidget", engines: ["5.8"] },
    ...bp("mutate", "[Epic UMGToolSet.UMGToolSet] Adds a widget to the tree at the specified position. Returns full widget info including Slot pointer. When ParentWidget is null and no root exists, the new widget becomes the root of the tree. Use ObjectTools.list_properties on the returned Widget and Slot to get property names before calling set_properties. Available on UE 5.8; absent from UE 5.9. Params: widgetBlueprint, widgetClass, widgetDisplayName, parentWidget?, childIndex?", "epic_call_tool"),
  },
  epic_add_widget_to_animation: {
    epicSchema: S_epic_add_widget_to_animation,
    epicTool: { toolset: "WidgetAnimationToolset.WidgetAnimationToolset", name: "WidgetAnimationToolset.WidgetAnimationToolset.AddWidgetToAnimation", engines: ["5.9"] },
    ...bp("mutate", "[Epic WidgetAnimationToolset.WidgetAnimationToolset] Add a UWidget (or a UPanelSlot with non-null content) as a possessable binding on a widget animation. Returns a FMovieSceneBindingProxy usable with every SequencerScripting method that takes a binding (MovieSceneBindingExtensions.AddTrack, etc.). ObjectToBind must be either: - A UWidget inside the Widget Blueprint's tree (typical case: animate a child widget's properties). - A UPanelSlot whose Content is non-null (animate slot properties like padding/anchors). The Widget Blueprint must have a compiled GeneratedClass. The implementation compiles the blueprint if needed before binding. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, animation, objectToBind", "epic_call_tool"),
  },
  epic_analog_input: {
    epicSchema: S_epic_analog_input,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.AnalogInput", engines: ["5.9"] },
    ...bp("unknown", "[Epic SlateInspectorToolset.SlateInspectorToolset] Send an analog axis value along the target user's focus path; 1D axes only, send 0 afterwards to release. Available on UE 5.9; absent from UE 5.8. Params: key, value, userIndex?", "epic_call_tool"),
  },
  epic_bind_to_event_property: {
    epicSchema: S_epic_bind_to_event_property,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.BindToEventProperty", engines: ["5.8"], successors: [{ toolset: "UMGEditor.WidgetBlueprintEditorSubsystem", name: "UMGEditor.WidgetBlueprintEditorSubsystem.BindToEventProperty", engines: ["5.9"], schema: S_epic_bind_to_event_property__1 }] },
    ...bp("mutate", "[Epic UMGToolSet.UMGToolSet] Adds a Blueprint event handler graph node bound to a widget's multicast delegate event, Typical events: UButton::OnClicked / OnPressed / OnReleased / OnHovered / OnUnhovered, UCheckBox::OnCheckStateChanged, USlider::OnValueChanged. The matching delegate UPROPERTY must exist on PropertyClass (or a parent of it). Preconditions: - PropertyName must exist in the blueprint. - PropertyClass must be the widget's class (or a parent class) that declares the delegate. Params: widgetBlueprint, eventName, propertyName, propertyClass. On UE 5.9 it calls UMGEditor.WidgetBlueprintEditorSubsystem.BindToEventProperty (Params: widgetBlueprint, eventName, propertyName, propertyClass, bShouldJumpToNode?).", "epic_call_tool"),
  },
  epic_clear_conversion_function_argument_path: {
    epicSchema: S_epic_clear_conversion_function_argument_path,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.ClearConversionFunctionArgumentPath", engines: ["5.9"] },
    ...bp("mutate", "[Epic MVVMToolset.MVVMToolset] Removes the property path bound to an input pin of a binding's conversion function, resetting the pin to its default literal, then recompiles the WidgetBlueprint. The compiler does not warn if the cleared pin carried the binding's source value. If the recompile fails the edit is kept; use CompileWidgetBlueprint to read the errors. Not supported during PIE. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, bindingId, argumentName, bSourceToDestination?", "epic_call_tool"),
  },
  epic_clear_focus_log: {
    epicSchema: S_epic_clear_focus_log,
    epicTool: { toolset: "SlateDebugger.SlateDebuggerSubsystem", name: "SlateDebugger.SlateDebuggerSubsystem.ClearFocusLog", engines: ["5.9"] },
    ...bp("mutate", "[Epic SlateDebugger.SlateDebuggerSubsystem] Clears all recorded events. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_clear_highlight: {
    epicSchema: S_epic_clear_highlight,
    epicTool: { toolset: "WidgetFinder.WidgetFinderSubsystem", name: "WidgetFinder.WidgetFinderSubsystem.ClearHighlight", engines: ["5.9"] },
    ...bp("mutate", "[Epic WidgetFinder.WidgetFinderSubsystem] Remove any highlight overlay drawn by Highlight. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_clear_widget_lifetime_log: {
    epicSchema: S_epic_clear_widget_lifetime_log,
    epicTool: { toolset: "UMGEditor.UMGDebuggerSubsystem", name: "UMGEditor.UMGDebuggerSubsystem.ClearWidgetLifetimeLog", engines: ["5.9"] },
    ...bp("mutate", "[Epic UMGEditor.UMGDebuggerSubsystem] Discards all captured events. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_click: {
    epicSchema: S_epic_click,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.Click", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic SlateInspectorToolset.SlateInspectorToolset] Click a Slate widget identified by its ref. Returns false without clicking when the target point would not receive the event -- something is on top of it, the widget has no arranged size, or MouseDown still holds the button. The log and LastRefusal() name the reason. True means the event was delivered to the widget's branch, not that the widget handled it. Params: ref, button?, doubleClick?, modifiers?, params?", "epic_call_tool"),
  },
  epic_compile_widget_blueprint: {
    epicSchema: S_epic_compile_widget_blueprint,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.CompileWidgetBlueprint", engines: ["5.8"], successors: [{ toolset: "UMGEditor.WidgetBlueprintEditorSubsystem", name: "UMGEditor.WidgetBlueprintEditorSubsystem.CompileWidgetBlueprint", engines: ["5.9"], schema: S_epic_compile_widget_blueprint__1 }] },
    ...bp("mutate", "[Epic UMGToolSet.UMGToolSet] Compiles a widget blueprint. Returns false with error details if compilation fails. Errors include missing BindWidget bindings, type mismatches, and graph errors. Call after all widgets and properties are set. Save separately via AssetTools.save_asset. Params: widgetBlueprint. On UE 5.9 it calls UMGEditor.WidgetBlueprintEditorSubsystem.CompileWidgetBlueprint (Params: widgetBlueprint).", "epic_call_tool"),
  },
  epic_create_and_add_widget: {
    epicSchema: S_epic_create_and_add_widget,
    epicTool: { toolset: "UMGEditor.WidgetBlueprintEditorSubsystem", name: "UMGEditor.WidgetBlueprintEditorSubsystem.CreateAndAddWidget", engines: ["5.9"] },
    ...bp("mutate", "[Epic UMGEditor.WidgetBlueprintEditorSubsystem] Adds a widget to the tree at the specified position. When ParentWidget is null and no root exists, the new widget becomes the root of the tree. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, widgetClass, widgetDisplayName, parentWidget?, childIndex?", "epic_call_tool"),
  },
  epic_create_view_binding: {
    epicSchema: S_epic_create_view_binding,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.CreateViewBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic MVVMToolset.MVVMToolset] Creates a View Binding from the SourceProperty to the Destination property. If ConversionName is given, that conversion function is used, even if the property types already match (e.g. to format a FText). Otherwise, if there is a type mismatch, an existing conversion function will try to be used. Property paths resolve against each segment's declared type, so a field that only exists on a subclass of an intermediate object's declared type won't resolve. Bind to the intermediate object and cast inside a conversion function instead. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, sourceContext, sourcePropertyPath, destinationContext, destinationPropertyPath, conversionName", "epic_call_tool"),
  },
  epic_create_view_event_binding: {
    epicSchema: S_epic_create_view_event_binding,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.CreateViewEventBinding", engines: ["5.9"] },
    ...bp("unknown", "[Epic MVVMToolset.MVVMToolset] Creates a View Event Binding from the EventProperty to the Destination property. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, eventContext, eventPropertyPath, destinationContext, destinationPropertyPath", "epic_call_tool"),
  },
  epic_create_view_model: {
    epicSchema: S_epic_create_view_model,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.CreateViewModel", engines: ["5.9"] },
    ...bp("mutate", "[Epic MVVMToolset.MVVMToolset] Creates a new ViewModel asset Available on UE 5.9; absent from UE 5.8. Params: viewModelName, packagePath, parentClass", "epic_call_tool"),
  },
  epic_create_widget_animation: {
    epicSchema: S_epic_create_widget_animation,
    epicTool: { toolset: "WidgetAnimationToolset.WidgetAnimationToolset", name: "WidgetAnimationToolset.WidgetAnimationToolset.CreateWidgetAnimation", engines: ["5.9"] },
    ...bp("mutate", "[Epic WidgetAnimationToolset.WidgetAnimationToolset] Create a new UWidgetAnimation on a Widget Blueprint and return its summary. The empty FWidgetAnimationInfo return signals failure (Animation == nullptr). Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, animationName, lengthSeconds?", "epic_call_tool"),
  },
  epic_create_widget_blueprint: {
    epicSchema: S_epic_create_widget_blueprint,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.CreateWidgetBlueprint", engines: ["5.8"], successors: [{ toolset: "UMGEditor.WidgetBlueprintEditorSubsystem", name: "UMGEditor.WidgetBlueprintEditorSubsystem.CreateWidgetBlueprint", engines: ["5.9"], schema: S_epic_create_widget_blueprint__1 }] },
    ...bp("mutate", "[Epic UMGToolSet.UMGToolSet] Creates a new Widget Blueprint asset. Returns the blueprint or nullptr on failure. Params: folderPath, assetName, parentClass. On UE 5.9 it calls UMGEditor.WidgetBlueprintEditorSubsystem.CreateWidgetBlueprint (Params: folderPath, assetName, parentClass).", "epic_call_tool"),
  },
  epic_deactivate_widget: {
    epicSchema: S_epic_deactivate_widget,
    epicTool: { toolset: "CommonUIToolset.CommonUIToolset", name: "CommonUIToolset.CommonUIToolset.DeactivateWidget", engines: ["5.9"] },
    ...bp("mutate", "[Epic CommonUIToolset.CommonUIToolset] Deactivate a live activatable widget. Same addressing rules as ActivateWidget, but matches only currently-active widgets. Available on UE 5.9; absent from UE 5.8. Params: widgetName, localPlayerIndex?", "epic_call_tool"),
  },
  epic_describe_widget: {
    epicSchema: S_epic_describe_widget,
    epicTool: { toolset: "WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset", name: "WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset.DescribeWidget", engines: ["5.9"] },
    ...bp("read", "[Epic WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset] Full structured description of every widget in the tree (or the subtree rooted at StartWidget). Values are the designer-time state stored in the asset. Anything set at runtime (for example a Blueprint graph changing a slot through SlotAsCanvasSlot, or style state that is not a UPROPERTY) does not appear; use TraceWidgetPropertyOverrides for graph writes. ComposedColors reports the composed pre-display **linear** tint only -- never a display-space value, and never for URichTextBlock/CommonRichTextBlock (a fundamentally different per-inline-tag-run model, excluded from v1 coverage). It does not model the contrast setting, DisplayGamma, retainer boxes, Button's DisabledEffect, or widget animations. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, startWidget?, maxDepth?, propertyNameFilter?, bSkipDefaultValues?, bIncludeFullDescription?, bIncludeComposedColors?", "epic_call_tool"),
  },
  epic_drag: {
    epicSchema: S_epic_drag,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.Drag", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic SlateInspectorToolset.SlateInspectorToolset] Drag from one Slate widget to another (mouse down, stepped move, release). Both ends are hit-tested first; returns false without dragging if either is covered or unarranged. The pointer is stepped along the path, so drag detection fires partway and the target receives OnDragOver, including once at the drop point. Waypoints bend that path, for gestures a straight line would not produce. Start and end positions are normalized within each widget and default to the center. Use them to aim at edge-band drop zones, e.g. EndPosition.Y = 0.1 to drop against the target's top edge. Leave EndRef empty and pass MoveBy or Waypoints to drag StartRef by a distance instead of onto another widget, e.g. splitters, sliders, scrollbars and divider grips. EndPosition is then ignored, and a path that never leaves the press point is refused since the drag would not move. Params: startRef, endRef, modifiers?, params?", "epic_call_tool"),
  },
  epic_fill_form: {
    epicSchema: S_epic_fill_form,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.FillForm", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic SlateInspectorToolset.SlateInspectorToolset] Fill multiple Slate form fields at once. Params: fields", "epic_call_tool"),
  },
  epic_find_widget_animation: {
    epicSchema: S_epic_find_widget_animation,
    epicTool: { toolset: "WidgetAnimationToolset.WidgetAnimationToolset", name: "WidgetAnimationToolset.WidgetAnimationToolset.FindWidgetAnimation", engines: ["5.9"] },
    ...bp("read", "[Epic WidgetAnimationToolset.WidgetAnimationToolset] Find an animation by internal name. Returns empty info (Animation == nullptr) when not found. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, animationName", "epic_call_tool"),
  },
  epic_fixup_mvvmdata: {
    epicSchema: S_epic_fixup_mvvmdata,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.FixupMVVMData", engines: ["5.9"] },
    ...bp("unknown", "[Epic MVVMToolset.MVVMToolset] Attempts to fixup MVVM data. This function can be called if the MVVM Generated Blueprint graphs like conversion functions report compilation errors. It regenerates the graphs to ensure parity with view binding data. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint", "epic_call_tool"),
  },
  epic_focus: {
    epicSchema: S_epic_focus,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.Focus", engines: ["5.9"] },
    ...bp("mutate", "[Epic SlateInspectorToolset.SlateInspectorToolset] Set Slate keyboard/navigation focus to a widget, e.g. before gamepad navigation in PIE. Available on UE 5.9; absent from UE 5.8. Params: ref, userIndex?", "epic_call_tool"),
  },
  epic_get_action_bindings: {
    epicSchema: S_epic_get_action_bindings,
    epicTool: { toolset: "CommonUIToolset.CommonUIToolset", name: "CommonUIToolset.CommonUIToolset.GetActionBindings", engines: ["5.9"] },
    ...bp("read", "[Epic CommonUIToolset.CommonUIToolset] All UI action bindings currently active (reachable by input) for a player. Available on UE 5.9; absent from UE 5.8. Params: localPlayerIndex?", "epic_call_tool"),
  },
  epic_get_activatable_tree: {
    epicSchema: S_epic_get_activatable_tree,
    epicTool: { toolset: "CommonUIToolset.CommonUIToolset", name: "CommonUIToolset.CommonUIToolset.GetActivatableTree", engines: ["5.9"] },
    ...bp("read", "[Epic CommonUIToolset.CommonUIToolset] Snapshot of the CommonUI activatable tree: root nodes, action-domain roots with children and bindings, active root, leafmost active widget, and optionally the full-depth indented tree dump text. Available on UE 5.9; absent from UE 5.8. Params: localPlayerIndex?, bIncludeFullDump?", "epic_call_tool"),
  },
  epic_get_container_widget_navigation_doc: {
    epicSchema: S_epic_get_container_widget_navigation_doc,
    epicTool: { toolset: "UMGEditor.WidgetBlueprintEditorSubsystem", name: "UMGEditor.WidgetBlueprintEditorSubsystem.GetContainerWidgetNavigationDoc", engines: ["5.9"] },
    ...bp("read", "[Epic UMGEditor.WidgetBlueprintEditorSubsystem] Returns navigation/focus documentation for a container widget class - both layout panels (UScrollBox, UVerticalBox, ...) and list views (UListView, UTileView, ...). Reads from the class's Navigation* UCLASS meta and its ancestors', so it needs only a class, not a widget instance. Available on UE 5.9; absent from UE 5.8. Params: widgetClass", "epic_call_tool"),
  },
  epic_get_current_focus: {
    epicSchema: S_epic_get_current_focus,
    epicTool: { toolset: "SlateDebugger.SlateDebuggerSubsystem", name: "SlateDebugger.SlateDebuggerSubsystem.GetCurrentFocus", engines: ["5.9"] },
    ...bp("read", "[Epic SlateDebugger.SlateDebuggerSubsystem] What has focus RIGHT NOW for a user, queried live from Slate, with its full widget path. Available on UE 5.9; absent from UE 5.8. Params: userIndex?", "epic_call_tool"),
  },
  epic_get_event_logging_status: {
    epicSchema: S_epic_get_event_logging_status,
    epicTool: { toolset: "CommonUIToolset.CommonUIToolset", name: "CommonUIToolset.CommonUIToolset.GetEventLoggingStatus", engines: ["5.9"] },
    ...bp("read", "[Epic CommonUIToolset.CommonUIToolset] The current status of the logger: on/off, event count, dropped count, time span, subscribed players. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_event_timeline: {
    epicSchema: S_epic_get_event_timeline,
    epicTool: { toolset: "CommonUIToolset.CommonUIToolset", name: "CommonUIToolset.CommonUIToolset.GetEventTimeline", engines: ["5.9"] },
    ...bp("read", "[Epic CommonUIToolset.CommonUIToolset] Chronological digest of logged events, oldest first. Never carries callstacks; use GetLoggedEvents when those are wanted. Available on UE 5.9; absent from UE 5.8. Params: maxCount?, eventTypeFilter?, localPlayerIndex?", "epic_call_tool"),
  },
  epic_get_focus_event_detail: {
    epicSchema: S_epic_get_focus_event_detail,
    epicTool: { toolset: "SlateDebugger.SlateDebuggerSubsystem", name: "SlateDebugger.SlateDebuggerSubsystem.GetFocusEventDetail", engines: ["5.9"] },
    ...bp("read", "[Epic SlateDebugger.SlateDebuggerSubsystem] The full causal chain for ONE event, located precisely by its sequence number (from any GetFocusEvents/GetFocusTimeline row). FocusReceived -> full path + symbolicated callstack + context + linked input/nav; other event types -> that record bare. bFound=false if evicted. Available on UE 5.9; absent from UE 5.8. Params: seq", "epic_call_tool"),
  },
  epic_get_focus_events: {
    epicSchema: S_epic_get_focus_events,
    epicTool: { toolset: "SlateDebugger.SlateDebuggerSubsystem", name: "SlateDebugger.SlateDebuggerSubsystem.GetFocusEvents", engines: ["5.9"] },
    ...bp("read", "[Epic SlateDebugger.SlateDebuggerSubsystem] Recent events (newest first), filtered. Available on UE 5.9; absent from UE 5.8. Params: widgetNameFilter, eventTypeFilter, focusCauseFilter?, userIndex?, maxCount?, bIncludePath?", "epic_call_tool"),
  },
  epic_get_focus_log_status: {
    epicSchema: S_epic_get_focus_log_status,
    epicTool: { toolset: "SlateDebugger.SlateDebuggerSubsystem", name: "SlateDebugger.SlateDebuggerSubsystem.GetFocusLogStatus", engines: ["5.9"] },
    ...bp("read", "[Epic SlateDebugger.SlateDebuggerSubsystem] Recording status: on/off, record count, dropped count, time span. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_focus_state: {
    epicSchema: S_epic_get_focus_state,
    epicTool: { toolset: "CommonUIToolset.CommonUIToolset", name: "CommonUIToolset.CommonUIToolset.GetFocusState", engines: ["5.9"] },
    ...bp("read", "[Epic CommonUIToolset.CommonUIToolset] Where Slate focus actually is for a player and where CommonUI intends it to be: the focused Slate widget, its UMG and owning-activatable identity, the leafmost widget's desired focus target, and whether focus sits inside the active root and the leafmost hierarchy. Available on UE 5.9; absent from UE 5.8. Params: localPlayerIndex?", "epic_call_tool"),
  },
  epic_get_focus_timeline: {
    epicSchema: S_epic_get_focus_timeline,
    epicTool: { toolset: "SlateDebugger.SlateDebuggerSubsystem", name: "SlateDebugger.SlateDebuggerSubsystem.GetFocusTimeline", engines: ["5.9"] },
    ...bp("read", "[Epic SlateDebugger.SlateDebuggerSubsystem] Lightweight chronological digest of recorded events (oldest->newest). No paths/callstack. Available on UE 5.9; absent from UE 5.8. Params: maxCount?, eventTypeFilter?, focusCauseFilter?, userIndex?", "epic_call_tool"),
  },
  epic_get_input_routing_state: {
    epicSchema: S_epic_get_input_routing_state,
    epicTool: { toolset: "CommonUIToolset.CommonUIToolset", name: "CommonUIToolset.CommonUIToolset.GetInputRoutingState", engines: ["5.9"] },
    ...bp("read", "[Epic CommonUIToolset.CommonUIToolset] Everything governing whether input reaches game vs UI for a player: active input mode and config (mouse capture/lock, move/look ignore), the viewport and player controller flags that can block input regardless of CommonUI state, current input device type, input type locks, held keys, leafmost widget, and current Slate focus. Available on UE 5.9; absent from UE 5.8. Params: localPlayerIndex?", "epic_call_tool"),
  },
  epic_get_logged_events: {
    epicSchema: S_epic_get_logged_events,
    epicTool: { toolset: "CommonUIToolset.CommonUIToolset", name: "CommonUIToolset.CommonUIToolset.GetLoggedEvents", engines: ["5.9"] },
    ...bp("read", "[Epic CommonUIToolset.CommonUIToolset] Logged events, filtered. Available on UE 5.9; absent from UE 5.8. Params: widgetNameFilter?, eventTypeFilter?, localPlayerIndex?, maxCount?, bIncludeCallstacks?", "epic_call_tool"),
  },
  epic_get_named_slots: {
    epicSchema: S_epic_get_named_slots,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.GetNamedSlots", engines: ["5.8"], successors: [{ toolset: "UMGEditor.WidgetBlueprintEditorSubsystem", name: "UMGEditor.WidgetBlueprintEditorSubsystem.GetNamedSlots", engines: ["5.9"], schema: S_epic_get_named_slots__1 }] },
    ...bp("read", "[Epic UMGToolSet.UMGToolSet] Returns named slot bindings (separate from tree hierarchy). Params: widgetBlueprint. On UE 5.9 it calls UMGEditor.WidgetBlueprintEditorSubsystem.GetNamedSlots (Params: widgetBlueprint).", "epic_call_tool"),
  },
  epic_get_query_syntax: {
    epicSchema: S_epic_get_query_syntax,
    epicTool: { toolset: "WidgetFinder.WidgetFinderSubsystem", name: "WidgetFinder.WidgetFinderSubsystem.GetQuerySyntax", engines: ["5.9"] },
    ...bp("read", "[Epic WidgetFinder.WidgetFinderSubsystem] Retrieve the complete query grammar, so a Query can be built without guessing. Works with the service stopped. Call this first when unsure of the syntax. The response's Description carries the signature-token legend. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_service_status: {
    epicSchema: S_epic_get_service_status,
    epicTool: { toolset: "WidgetFinder.WidgetFinderSubsystem", name: "WidgetFinder.WidgetFinderSubsystem.GetServiceStatus", engines: ["5.9"] },
    ...bp("read", "[Epic WidgetFinder.WidgetFinderSubsystem] Read the service state without changing it, to check it is up or that a start stored something. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_umginfo: {
    epicSchema: S_epic_get_umginfo,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.GetUMGInfo", engines: ["5.9"] },
    ...bp("read", "[Epic SlateInspectorToolset.SlateInspectorToolset] Resolve a Slate widget ref back to the UMG UWidget that created it (for UMG and PIE UIs). Returns the UWidget name, its UClass, the owning WidgetBlueprint asset path, the enclosing UUserWidget chain, and a live Widget handle. \"resolvedVia\" is \"self\" when the widget itself carries UMG metadata, or \"parent\" when an ancestor did. On failure Error is set and Widget is null: either the widget is pure Slate with no UMG owner, or its authoring UWidget has since been destroyed. The remaining metadata fields are still filled in for the latter case. Available on UE 5.9; absent from UE 5.8. Params: ref", "epic_call_tool"),
  },
  epic_get_view_model_connection_map: {
    epicSchema: S_epic_get_view_model_connection_map,
    epicTool: { toolset: "WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset", name: "WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset.GetViewModelConnectionMap", engines: ["5.9"] },
    ...bp("read", "[Epic WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset] Reverse-searches every Widget Blueprint whose MVVM View references ViewModelClass, and returns each one's own connection map grouped per widget. Each widget's rows are narrowed to just the VMFieldBinding bindings to that exact ViewModelClass; call GetWidgetConnectionMap directly for a widget's whole map. Native (non-Blueprint) ViewModel classes never surface here, since they have no on-disk package identity for the reverse search to find. Available on UE 5.9; absent from UE 5.8. Params: viewModelClass, scopeFilter?", "epic_call_tool"),
  },
  epic_get_widget_animation_bindings: {
    epicSchema: S_epic_get_widget_animation_bindings,
    epicTool: { toolset: "WidgetAnimationToolset.WidgetAnimationToolset", name: "WidgetAnimationToolset.WidgetAnimationToolset.GetWidgetAnimationBindings", engines: ["5.9"] },
    ...bp("read", "[Epic WidgetAnimationToolset.WidgetAnimationToolset] Get all bindings on a widget animation as FMovieSceneBindingProxy entries. Available on UE 5.9; absent from UE 5.8. Params: animation", "epic_call_tool"),
  },
  epic_get_widget_class_info: {
    epicSchema: S_epic_get_widget_class_info,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.GetWidgetClassInfo", engines: ["5.8"], successors: [{ toolset: "UMGEditor.WidgetBlueprintEditorSubsystem", name: "UMGEditor.WidgetBlueprintEditorSubsystem.GetWidgetClassInfo", engines: ["5.9"], schema: S_epic_get_widget_class_info__1 }] },
    ...bp("read", "[Epic UMGToolSet.UMGToolSet] Returns the Category, Description and if it's a Panel for a single widget class. Same per-entry data as ListWidgetClasses, but lets callers query a class they already have without scanning every UClass. Returns an empty entry if WidgetClass is null. Can be used to get more information on the class from the Description and Category. Params: widgetClass. On UE 5.9 it calls UMGEditor.WidgetBlueprintEditorSubsystem.GetWidgetClassInfo (Params: widgetClass).", "epic_call_tool"),
  },
  epic_get_widget_connection_map: {
    epicSchema: S_epic_get_widget_connection_map,
    epicTool: { toolset: "WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset", name: "WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset.GetWidgetConnectionMap", engines: ["5.9"] },
    ...bp("read", "[Epic WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset] Maps every connection on WidgetBlueprint (MVVM field bindings, event bindings, delegates, and legacy property bindings) into one pre-sorted checklist, Unconnected/Misconfigured rows first. Misconfigured only applies to VMFieldBinding rows: Fixable if a conversion function can bridge the types, Unbridgeable if none is allowlisted. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, scopeFilter?", "epic_call_tool"),
  },
  epic_get_widget_description: {
    epicSchema: S_epic_get_widget_description,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.GetWidgetDescription", engines: ["5.8"] },
    ...bp("read", "[Epic UMGToolSet.UMGToolSet] Full property dump of every widget in the tree. Each line: [N] Type Name Prop:Value ... slot:(SlotProp:Value ...) N is the 0-based index into result.Widgets -- use result.Widgets[N] to get the widget ref without text parsing. Same indentation format as GetTaggedWidgetDescription; richer per-widget detail. Available on UE 5.8; absent from UE 5.9. Params: widgetBlueprint, startWidget?, maxDepth?", "epic_call_tool"),
  },
  epic_get_widget_lifetime_events: {
    epicSchema: S_epic_get_widget_lifetime_events,
    epicTool: { toolset: "UMGEditor.UMGDebuggerSubsystem", name: "UMGEditor.UMGDebuggerSubsystem.GetWidgetLifetimeEvents", engines: ["5.9"] },
    ...bp("read", "[Epic UMGEditor.UMGDebuggerSubsystem] Returns captured widget lifetime events, newest first. Available on UE 5.9; absent from UE 5.8. Params: nameFilter?, classFilter?, eventTypeFilter?, maxCount?, playerFilter?", "epic_call_tool"),
  },
  epic_get_widget_lifetime_log_status: {
    epicSchema: S_epic_get_widget_lifetime_log_status,
    epicTool: { toolset: "UMGEditor.UMGDebuggerSubsystem", name: "UMGEditor.UMGDebuggerSubsystem.GetWidgetLifetimeLogStatus", engines: ["5.9"] },
    ...bp("read", "[Epic UMGEditor.UMGDebuggerSubsystem] Returns whether recording is running plus how many events are held and were dropped. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_widget_preview_active: {
    epicSchema: S_epic_get_widget_preview_active,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.GetWidgetPreviewActive", engines: ["5.9"] },
    ...bp("read", "[Epic MVVMToolset.MVVMToolset] Reads the View Bindings design-time preview toggle state for a WidgetBlueprint. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint", "epic_call_tool"),
  },
  epic_get_widget_tree_depth: {
    epicSchema: S_epic_get_widget_tree_depth,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.GetWidgetTreeDepth", engines: ["5.8"] },
    ...bp("read", "[Epic UMGToolSet.UMGToolSet] Returns the maximum depth of the widget tree. Depth: root with no children = 0; root + children = 1; etc. Available on UE 5.8; absent from UE 5.9. Params: widgetBlueprint, startWidget?", "epic_call_tool"),
  },
  epic_get_widget_tree_depth__widget_static_analysis_toolset: {
    epicSchema: S_epic_get_widget_tree_depth__widget_static_analysis_toolset,
    epicTool: { toolset: "WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset", name: "WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset.GetWidgetTreeDepth", engines: ["5.9"] },
    ...bp("read", "[Epic WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset] Returns the maximum depth of the widget tree. Depth: root with no children = 0; root + children = 1; etc. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, startWidget?", "epic_call_tool"),
  },
  epic_get_widget_view_config: {
    epicSchema: S_epic_get_widget_view_config,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.GetWidgetViewConfig", engines: ["5.9"] },
    ...bp("read", "[Epic MVVMToolset.MVVMToolset] Returns the MVVM View configuration. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint", "epic_call_tool"),
  },
  epic_get_widgets: {
    epicSchema: S_epic_get_widgets,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.GetWidgets", engines: ["5.8"], successors: [{ toolset: "UMGEditor.WidgetBlueprintEditorSubsystem", name: "UMGEditor.WidgetBlueprintEditorSubsystem.GetWidgets", engines: ["5.9"], schema: S_epic_get_widgets__1 }] },
    ...bp("read", "[Epic UMGToolSet.UMGToolSet] Returns blueprint info and all widgets in depth-first order. Children within each parent are in their panel slot order - this is the hierarchy order shown in the designer. Info contains ParentClass (pass to CreateWidgetBlueprint) and RootWidgetClass. Use ObjectTools.list_properties on each returned Widget and Slot to get property names before calling set_properties. Params: widgetBlueprint. On UE 5.9 it calls UMGEditor.WidgetBlueprintEditorSubsystem.GetWidgets (Params: widgetBlueprint).", "epic_call_tool"),
  },
  epic_highlight: {
    epicSchema: S_epic_highlight,
    epicTool: { toolset: "WidgetFinder.WidgetFinderSubsystem", name: "WidgetFinder.WidgetFinderSubsystem.Highlight", engines: ["5.9"] },
    ...bp("mutate", "[Epic WidgetFinder.WidgetFinderSubsystem] Outline every match in the live UI with the Widget Reflector's debug border, to see what a query selects. Needs a running service. Replaces any current highlight and persists until ClearHighlight or an empty query. Detached matches count in TotalMatchCount but only draw once their tab or window is surfaced. Available on UE 5.9; absent from UE 5.8. Params: request", "epic_call_tool"),
  },
  epic_hit_test: {
    epicSchema: S_epic_hit_test,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.HitTest", engines: ["5.9"] },
    ...bp("read", "[Epic SlateInspectorToolset.SlateInspectorToolset] Report whether a widget is reachable by pointer input, without changing anything. Returns JSON {reached, samples, reachedSamples, x, y, reason}, using the same hit test Click, Hover, Drag and Scroll refuse on; `reason` names the obstruction. Grid > 1 samples a Grid x Grid lattice inset inside the widget instead of a single point. `reachedSamples` counts the samples that hit, and x/y locate the first sample that missed. Available on UE 5.9; absent from UE 5.8. Params: ref, params?", "epic_call_tool"),
  },
  epic_hover: {
    epicSchema: S_epic_hover,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.Hover", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic SlateInspectorToolset.SlateInspectorToolset] Hover over a Slate widget, triggering any hover state or tooltip. Returns false without hovering when the target point is covered or the widget is unarranged. Unlike the other action tools, Hover moves the OS cursor, since hover state only lasts while the pointer stays over the widget. On Mac, Click, Drag and Scroll move it too and leave it on the target, because Mac only routes pointer input to the window under the real cursor. A button held by MouseDown stays held. Params: ref, params?", "epic_call_tool"),
  },
  epic_key_down: {
    epicSchema: S_epic_key_down,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.KeyDown", engines: ["5.9"] },
    ...bp("unknown", "[Epic SlateInspectorToolset.SlateInspectorToolset] Press a key and leave it held; pair with KeyUp. Same key specs and gamepad routing as PressKey. A held modifier key (LeftShift, RightControl, LeftAlt, LeftCommand, ...) is down for every later key event of the same Slate user until released, by KeyUp or by a PressKey of the same key. It does not apply to pointer tools, which take their own Modifiers, nor to the platform modifier state FSlateApplication::GetModifierKeys reports. KeyUp or ReleaseHeldInput releases it, as does the hold watchdog after SlateInspectorToolset.HeldInputTimeoutSeconds without a pointer, touch, KeyDown or KeyUp call. Returns true once the press is sent, whether or not a widget handled it, as a held modifier is normally unhandled. False means nothing was sent because the key or user is invalid; LastRefusal() names the reason. Available on UE 5.9; absent from UE 5.8. Params: key, userIndex?", "epic_call_tool"),
  },
  epic_key_up: {
    epicSchema: S_epic_key_up,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.KeyUp", engines: ["5.9"] },
    ...bp("unknown", "[Epic SlateInspectorToolset.SlateInspectorToolset] Release a key held by an earlier KeyDown for the same Slate user. Same key specs and gamepad routing as PressKey. Returns true once the release is sent, whether or not a widget handled it, as most widgets act on the press. False means nothing was sent because the key or user is invalid; LastRefusal() names the reason. Available on UE 5.9; absent from UE 5.8. Params: key, userIndex?", "epic_call_tool"),
  },
  epic_last_refusal: {
    epicSchema: S_epic_last_refusal,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.LastRefusal", engines: ["5.9"] },
    ...bp("read", "[Epic SlateInspectorToolset.SlateInspectorToolset] Why the last action tool refused, as JSON {seq, tool, target, reason}; seq is 0 when nothing has refused this session. seq increments on each refusal, so sample it before an action and compare afterwards to tell a new refusal from one already read. Click, Hover, Scroll, Drag, Type, Focus, SelectOption, PressKey, KeyDown and KeyUp record refusals, and they record one every time they return false unless Slate is not running. For PressKey that includes a key that was sent but that nothing handled. Read-only tools such as Snapshot and HitTest never change it. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_list_bindable_widget_properties: {
    epicSchema: S_epic_list_bindable_widget_properties,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.ListBindableWidgetProperties", engines: ["5.9"] },
    ...bp("read", "[Epic MVVMToolset.MVVMToolset] Lists the bindable property paths available on a widget (binding source/destination candidates). Names aren't filtered by direction -- a read-only property can't be used as a destination and a write-only one can't be used as a source; CreateViewBinding reports that mismatch if it's tried. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, widget", "epic_call_tool"),
  },
  epic_list_binding_conversion_options: {
    epicSchema: S_epic_list_binding_conversion_options,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.ListBindingConversionOptions", engines: ["5.9"] },
    ...bp("read", "[Epic MVVMToolset.MVVMToolset] Reports whether SourcePropertyPath and DestinationPropertyPath bind directly, and/or which conversion functions bridge them. Parameters mirror CreateViewBinding's Source-/Destination-prefixed parameters. A function-backed leaf field on either side resolves as unbridgeable. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, sourceContext, sourcePropertyPath, destinationContext, destinationPropertyPath", "epic_call_tool"),
  },
  epic_list_class_bindable_fields: {
    epicSchema: S_epic_list_class_bindable_fields,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.ListClassBindableFields", engines: ["5.9"] },
    ...bp("read", "[Epic MVVMToolset.MVVMToolset] Lists every field on TargetClass valid as an MVVM binding source or destination. Available on UE 5.9; absent from UE 5.8. Params: targetClass, accessorClass", "epic_call_tool"),
  },
  epic_list_conversion_functions: {
    epicSchema: S_epic_list_conversion_functions,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.ListConversionFunctions", engines: ["5.9"] },
    ...bp("read", "[Epic MVVMToolset.MVVMToolset] List Available Conversion functions for a WidgetBlueprint that bind a Source Property to a Destination Property Includes Pure and Const functions on the WidgetBlueprint itself once it has been compiled. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint", "epic_call_tool"),
  },
  epic_list_observers: {
    epicSchema: S_epic_list_observers,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.ListObservers", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic SlateInspectorToolset.SlateInspectorToolset] List all active observers as a JSON array for debugging. Each entry includes the observer identifier, whether it is the root observer, the root widget ref (if any), max depth, and cached snapshot size. Params: none", "epic_call_tool"),
  },
  epic_list_property_path_bindable_fields: {
    epicSchema: S_epic_list_property_path_bindable_fields,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.ListPropertyPathBindableFields", engines: ["5.9"] },
    ...bp("read", "[Epic MVVMToolset.MVVMToolset] Resolves PropertyPath against Context and describes that single field. Unlike ListClassBindableFields, this doesn't gate on private/protected -- CreateViewBinding can bind those fields too, so we don't hide them. bIsPrivate/bIsProtected on the result say which case it is. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, context, propertyPath", "epic_call_tool"),
  },
  epic_list_view_conditions: {
    epicSchema: S_epic_list_view_conditions,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.ListViewConditions", engines: ["5.9"] },
    ...bp("read", "[Epic MVVMToolset.MVVMToolset] List all conditions on the WidgetBlueprint's View. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint", "epic_call_tool"),
  },
  epic_list_view_models: {
    epicSchema: S_epic_list_view_models,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.ListViewModels", engines: ["5.9"] },
    ...bp("read", "[Epic MVVMToolset.MVVMToolset] List all ViewModel under the search path Available on UE 5.9; absent from UE 5.8. Params: searchPath", "epic_call_tool"),
  },
  epic_list_widget_animations: {
    epicSchema: S_epic_list_widget_animations,
    epicTool: { toolset: "WidgetAnimationToolset.WidgetAnimationToolset", name: "WidgetAnimationToolset.WidgetAnimationToolset.ListWidgetAnimations", engines: ["5.9"] },
    ...bp("read", "[Epic WidgetAnimationToolset.WidgetAnimationToolset] List all animations on a Widget Blueprint. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint", "epic_call_tool"),
  },
  epic_list_widget_blueprints: {
    epicSchema: S_epic_list_widget_blueprints,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.ListWidgetBlueprints", engines: ["5.8"], successors: [{ toolset: "UMGEditor.WidgetBlueprintEditorSubsystem", name: "UMGEditor.WidgetBlueprintEditorSubsystem.ListWidgetBlueprints", engines: ["5.9"], schema: S_epic_list_widget_blueprints__1 }] },
    ...bp("read", "[Epic UMGToolSet.UMGToolSet] Lists widget blueprints in a content folder. Params: folderPath. On UE 5.9 it calls UMGEditor.WidgetBlueprintEditorSubsystem.ListWidgetBlueprints (Params: folderPath).", "epic_call_tool"),
  },
  epic_list_widget_classes: {
    epicSchema: S_epic_list_widget_classes,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.ListWidgetClasses", engines: ["5.8"], successors: [{ toolset: "UMGEditor.WidgetBlueprintEditorSubsystem", name: "UMGEditor.WidgetBlueprintEditorSubsystem.ListWidgetClasses", engines: ["5.9"], schema: S_epic_list_widget_classes__1 }] },
    ...bp("read", "[Epic UMGToolSet.UMGToolSet] Lists available widget classes, optionally filtered by name substring. Params: filter. On UE 5.9 it calls UMGEditor.WidgetBlueprintEditorSubsystem.ListWidgetClasses (Params: filter?).", "epic_call_tool"),
  },
  epic_list_widget_view_bindings: {
    epicSchema: S_epic_list_widget_view_bindings,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.ListWidgetViewBindings", engines: ["5.9"] },
    ...bp("read", "[Epic MVVMToolset.MVVMToolset] List all view bindings on the WidgetBlueprint (including possible conversion functions). Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint", "epic_call_tool"),
  },
  epic_list_widget_view_event_bindings: {
    epicSchema: S_epic_list_widget_view_event_bindings,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.ListWidgetViewEventBindings", engines: ["5.9"] },
    ...bp("read", "[Epic MVVMToolset.MVVMToolset] List all event bindings currently configured on the WidgetBlueprint's View, along with their context information. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint", "epic_call_tool"),
  },
  epic_list_widget_view_events: {
    epicSchema: S_epic_list_widget_view_events,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.ListWidgetViewEvents", engines: ["5.9"] },
    ...bp("unknown", "[Epic MVVMToolset.MVVMToolset] List Available Event Properties for a WidgetBlueprint (i.e. a Multicast Delegate) Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint", "epic_call_tool"),
  },
  epic_list_widget_view_models: {
    epicSchema: S_epic_list_widget_view_models,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.ListWidgetViewModels", engines: ["5.9"] },
    ...bp("read", "[Epic MVVMToolset.MVVMToolset] List all view models used by a widget Blueprint Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint", "epic_call_tool"),
  },
  epic_mouse_down: {
    epicSchema: S_epic_mouse_down,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.MouseDown", engines: ["5.9"] },
    ...bp("unknown", "[Epic SlateInspectorToolset.SlateInspectorToolset] Press a mouse button at a widget's center and leave it held; idempotent for a button this widget already holds. MouseUp or ReleaseHeldInput releases it, as does the hold watchdog after SlateInspectorToolset.HeldInputTimeoutSeconds without a pointer, touch, KeyDown or KeyUp call. Available on UE 5.9; absent from UE 5.8. Params: ref, button?, modifiers?", "epic_call_tool"),
  },
  epic_mouse_move: {
    epicSchema: S_epic_mouse_move,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.MouseMove", engines: ["5.9"] },
    ...bp("mutate", "[Epic SlateInspectorToolset.SlateInspectorToolset] Move the pointer to an absolute screen position; a button held by MouseDown stays held, so MouseDown/MouseMove/MouseUp drags. Available on UE 5.9; absent from UE 5.8. Params: x, y, modifiers?", "epic_call_tool"),
  },
  epic_mouse_move_to: {
    epicSchema: S_epic_mouse_move_to,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.MouseMoveTo", engines: ["5.9"] },
    ...bp("mutate", "[Epic SlateInspectorToolset.SlateInspectorToolset] Move the pointer to a widget's center plus an offset; a held button stays held. Use MouseMove for an absolute position. Available on UE 5.9; absent from UE 5.8. Params: ref, offsetX?, offsetY?, modifiers?", "epic_call_tool"),
  },
  epic_mouse_up: {
    epicSchema: S_epic_mouse_up,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.MouseUp", engines: ["5.9"] },
    ...bp("unknown", "[Epic SlateInspectorToolset.SlateInspectorToolset] Release a mouse button at the pointer, to its captor or whatever lies under it; safe without a prior MouseDown. Available on UE 5.9; absent from UE 5.8. Params: button?, modifiers?", "epic_call_tool"),
  },
  epic_move_uicomponent: {
    epicSchema: S_epic_move_uicomponent,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.MoveUIComponent", engines: ["5.8"], successors: [{ toolset: "UMGEditor.WidgetBlueprintEditorSubsystem", name: "UMGEditor.WidgetBlueprintEditorSubsystem.MoveUIComponent", engines: ["5.9"], schema: S_epic_move_uicomponent__1 }] },
    ...bp("mutate", "[Epic UMGToolSet.UMGToolSet] Moves a UI component before or after another component on the same widget. Params: widgetBlueprint, widgetName, componentClassToMove, relativeToComponentClass, bMoveAfter. On UE 5.9 it calls UMGEditor.WidgetBlueprintEditorSubsystem.MoveUIComponent (Params: widgetBlueprint, widgetName, componentClassToMove, relativeToComponentClass, bMoveAfter).", "epic_call_tool"),
  },
  epic_move_widget: {
    epicSchema: S_epic_move_widget,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.MoveWidget", engines: ["5.8"], successors: [{ toolset: "UMGEditor.WidgetBlueprintEditorSubsystem", name: "UMGEditor.WidgetBlueprintEditorSubsystem.MoveWidget", engines: ["5.9"], schema: S_epic_move_widget__1 }] },
    ...bp("mutate", "[Epic UMGToolSet.UMGToolSet] Moves a widget to a new parent panel at the specified position. Returns updated widget info with new Slot. Params: widgetBlueprint, widget, newParent, childIndex?. On UE 5.9 it calls UMGEditor.WidgetBlueprintEditorSubsystem.MoveWidget (Params: widgetBlueprint, widget, newParent, childIndex?).", "epic_call_tool"),
  },
  epic_observe: {
    epicSchema: S_epic_observe,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.Observe", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic SlateInspectorToolset.SlateInspectorToolset] Register an observer on a widget subtree so its refs are continuously kept up to date (~100ms tick). Call this on the window or panel you are about to work with. It ensures new widgets appearing in that subtree are assigned refs automatically. Unobserve when you are done. A shallow root observer (depth 0) already covers top-level windows. Params: ref, maxDepth?", "epic_call_tool"),
  },
  epic_press_key: {
    epicSchema: S_epic_press_key,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.PressKey", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic SlateInspectorToolset.SlateInspectorToolset] Press and release a key on the focused Slate widget; gamepad keys route as controller input for the target user. Modifier keys held by KeyDown are down for the press, as with \"Ctrl+\" prefixes. True means some handler consumed the press, an auto-repeat, its char event or the release, not that the UI changed as intended. Any focusable widget handles Tab, arrow and D-pad keys as a navigation request, so true for them does not mean focus moved; compare Snapshot or HitTest before and after. False means nothing handled the key, or that nothing was sent because the key, user or RepeatCount is invalid; LastRefusal() names the reason, including which widget had focus. In PIE, false can also mean the game viewport passed the key to the game and the game did not consume it. Only BackSpace, Tab, Enter and Escape send a character; use Type to enter text. Params: key, userIndex?, repeatCount?", "epic_call_tool"),
  },
  epic_query: {
    epicSchema: S_epic_query,
    epicTool: { toolset: "WidgetFinder.WidgetFinderSubsystem", name: "WidgetFinder.WidgetFinderSubsystem.Query", engines: ["5.9"] },
    ...bp("read", "[Epic WidgetFinder.WidgetFinderSubsystem] Locate live Slate widgets with a query, whose grammar GetQuerySyntax returns. Needs a running service. Check bSuccess and ErrorMessage first, since a syntax error also returns no matches. TotalMatchCount and bResultTruncated tell an under-constrained query from an exact one. Pass each result's `Ref` to the SlateInspectorToolset tools to act on the widget. Prefer Query and WaitForQuery over the SlateInspectorToolset Snapshot/Observe/Screenshot tools. Available on UE 5.9; absent from UE 5.8. Params: request", "epic_call_tool"),
  },
  epic_read_preview_widget_property: {
    epicSchema: S_epic_read_preview_widget_property,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.ReadPreviewWidgetProperty", engines: ["5.9"] },
    ...bp("read", "[Epic MVVMToolset.MVVMToolset] Exports a property value from the live design-time preview widget of an open WidgetBlueprint designer. Use after SetWidgetPreviewActive to assert what the binding preview wrote to the canvas. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, widgetName, propertyName", "epic_call_tool"),
  },
  epic_reconstruct_focus_path: {
    epicSchema: S_epic_reconstruct_focus_path,
    epicTool: { toolset: "SlateDebugger.SlateDebuggerSubsystem", name: "SlateDebugger.SlateDebuggerSubsystem.ReconstructFocusPath", engines: ["5.9"] },
    ...bp("read", "[Epic SlateDebugger.SlateDebuggerSubsystem] Reconstructs the causal chain (input -> navigation -> focus + context + callstack) that gave a widget focus. Available on UE 5.9; absent from UE 5.8. Params: widgetName, occurrenceFromEnd?", "epic_call_tool"),
  },
  epic_refresh_active_root_focus: {
    epicSchema: S_epic_refresh_active_root_focus,
    epicTool: { toolset: "CommonUIToolset.CommonUIToolset", name: "CommonUIToolset.CommonUIToolset.RefreshActiveRootFocus", engines: ["5.9"] },
    ...bp("mutate", "[Epic CommonUIToolset.CommonUIToolset] Ask the router to re-apply focus to the active root's desired focus target. Available on UE 5.9; absent from UE 5.8. Params: localPlayerIndex?", "epic_call_tool"),
  },
  epic_release_held_input: {
    epicSchema: S_epic_release_held_input,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.ReleaseHeldInput", engines: ["5.9"] },
    ...bp("unknown", "[Epic SlateInspectorToolset.SlateInspectorToolset] Release every button MouseDown still holds, lift every finger TouchStart still holds and release every key KeyDown still holds, returning how many; buttons the user holds are left alone. Also run by the hold watchdog after SlateInspectorToolset.HeldInputTimeoutSeconds (default 100) without a pointer, touch, KeyDown or KeyUp call, at PIE end and at shutdown. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_remove_input_type_lock: {
    epicSchema: S_epic_remove_input_type_lock,
    epicTool: { toolset: "CommonUIToolset.CommonUIToolset", name: "CommonUIToolset.CommonUIToolset.RemoveInputTypeLock", engines: ["5.9"] },
    ...bp("mutate", "[Epic CommonUIToolset.CommonUIToolset] Remove an input type lock previously added with SetInputTypeLock. Available on UE 5.9; absent from UE 5.8. Params: reason?, localPlayerIndex?", "epic_call_tool"),
  },
  epic_remove_uicomponent: {
    epicSchema: S_epic_remove_uicomponent,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.RemoveUIComponent", engines: ["5.8"], successors: [{ toolset: "UMGEditor.WidgetBlueprintEditorSubsystem", name: "UMGEditor.WidgetBlueprintEditorSubsystem.RemoveUIComponent", engines: ["5.9"], schema: S_epic_remove_uicomponent__1 }] },
    ...bp("mutate", "[Epic UMGToolSet.UMGToolSet] Removes a UI component of the given class from the named widget. Params: widgetBlueprint, widgetName, componentClass. On UE 5.9 it calls UMGEditor.WidgetBlueprintEditorSubsystem.RemoveUIComponent (Params: widgetBlueprint, widgetName, componentClass).", "epic_call_tool"),
  },
  epic_remove_view_condition: {
    epicSchema: S_epic_remove_view_condition,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.RemoveViewCondition", engines: ["5.9"] },
    ...bp("mutate", "[Epic MVVMToolset.MVVMToolset] Removes a condition from the WidgetBlueprint's View. Not supported during PIE. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, condition", "epic_call_tool"),
  },
  epic_remove_widget: {
    epicSchema: S_epic_remove_widget,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.RemoveWidget", engines: ["5.8"], successors: [{ toolset: "UMGEditor.WidgetBlueprintEditorSubsystem", name: "UMGEditor.WidgetBlueprintEditorSubsystem.RemoveWidget", engines: ["5.9"], schema: S_epic_remove_widget__1 }] },
    ...bp("mutate", "[Epic UMGToolSet.UMGToolSet] Removes a widget and its children from the tree. Params: widgetBlueprint, widget. On UE 5.9 it calls UMGEditor.WidgetBlueprintEditorSubsystem.RemoveWidget (Params: widgetBlueprint, widget).", "epic_call_tool"),
  },
  epic_remove_widget_animation: {
    epicSchema: S_epic_remove_widget_animation,
    epicTool: { toolset: "WidgetAnimationToolset.WidgetAnimationToolset", name: "WidgetAnimationToolset.WidgetAnimationToolset.RemoveWidgetAnimation", engines: ["5.9"] },
    ...bp("mutate", "[Epic WidgetAnimationToolset.WidgetAnimationToolset] Remove an animation from a Widget Blueprint. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, animation", "epic_call_tool"),
  },
  epic_remove_widget_binding: {
    epicSchema: S_epic_remove_widget_binding,
    epicTool: { toolset: "WidgetAnimationToolset.WidgetAnimationToolset", name: "WidgetAnimationToolset.WidgetAnimationToolset.RemoveWidgetBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic WidgetAnimationToolset.WidgetAnimationToolset] Remove a binding (by GUID) from a widget animation. Removes both the MovieScene possessable and the UMG binding entry. Available on UE 5.9; absent from UE 5.8. Params: animation, bindingGuid", "epic_call_tool"),
  },
  epic_remove_widget_view_binding: {
    epicSchema: S_epic_remove_widget_view_binding,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.RemoveWidgetViewBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic MVVMToolset.MVVMToolset] Removes View Binding on the WidgetBlueprint Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, bindingId", "epic_call_tool"),
  },
  epic_rename_widget: {
    epicSchema: S_epic_rename_widget,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.RenameWidget", engines: ["5.8"], successors: [{ toolset: "UMGEditor.WidgetBlueprintEditorSubsystem", name: "UMGEditor.WidgetBlueprintEditorSubsystem.RenameWidget", engines: ["5.9"], schema: S_epic_rename_widget__1 }] },
    ...bp("mutate", "[Epic UMGToolSet.UMGToolSet] Renames a widget. Returns updated widget info or empty on failure. Params: widgetBlueprint, widget, newDisplayName. On UE 5.9 it calls UMGEditor.WidgetBlueprintEditorSubsystem.RenameWidget (Params: widgetBlueprint, widget, newDisplayName).", "epic_call_tool"),
  },
  epic_rename_widget_animation: {
    epicSchema: S_epic_rename_widget_animation,
    epicTool: { toolset: "WidgetAnimationToolset.WidgetAnimationToolset", name: "WidgetAnimationToolset.WidgetAnimationToolset.RenameWidgetAnimation", engines: ["5.9"] },
    ...bp("mutate", "[Epic WidgetAnimationToolset.WidgetAnimationToolset] Rename an animation. NewName must not collide with an existing animation on the blueprint. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, animation, newName", "epic_call_tool"),
  },
  epic_replace_widget_with_child: {
    epicSchema: S_epic_replace_widget_with_child,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.ReplaceWidgetWithChild", engines: ["5.8"], successors: [{ toolset: "UMGEditor.WidgetBlueprintEditorSubsystem", name: "UMGEditor.WidgetBlueprintEditorSubsystem.ReplaceWidgetWithChild", engines: ["5.9"], schema: S_epic_replace_widget_with_child__1 }] },
    ...bp("mutate", "[Epic UMGToolSet.UMGToolSet] Replaces a panel widget with its first child, removing the panel from the tree. The widget to replace must be a UPanelWidget with only one child. Params: widgetBlueprint, widgetToReplace. On UE 5.9 it calls UMGEditor.WidgetBlueprintEditorSubsystem.ReplaceWidgetWithChild (Params: widgetBlueprint, widgetToReplace).", "epic_call_tool"),
  },
  epic_replace_widget_with_named_slot: {
    epicSchema: S_epic_replace_widget_with_named_slot,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.ReplaceWidgetWithNamedSlot", engines: ["5.8"], successors: [{ toolset: "UMGEditor.WidgetBlueprintEditorSubsystem", name: "UMGEditor.WidgetBlueprintEditorSubsystem.ReplaceWidgetWithNamedSlot", engines: ["5.9"], schema: S_epic_replace_widget_with_named_slot__1 }] },
    ...bp("mutate", "[Epic UMGToolSet.UMGToolSet] Replaces a host widget with the content of one of its named slots. The host must implement INamedSlotInterface (e.g., a UUserWidget exposing named slots). The slot's content widget is moved up to take the host's place in the tree. Params: widgetBlueprint, widgetToReplace, namedSlot. On UE 5.9 it calls UMGEditor.WidgetBlueprintEditorSubsystem.ReplaceWidgetWithNamedSlot (Params: widgetBlueprint, widgetToReplace, namedSlot, bWarnAndAskUser?).", "epic_call_tool"),
  },
  epic_replace_widget_with_template: {
    epicSchema: S_epic_replace_widget_with_template,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.ReplaceWidgetWithTemplate", engines: ["5.8"], successors: [{ toolset: "UMGEditor.WidgetBlueprintEditorSubsystem", name: "UMGEditor.WidgetBlueprintEditorSubsystem.ReplaceWidgetWithTemplate", engines: ["5.9"], schema: S_epic_replace_widget_with_template__1 }] },
    ...bp("mutate", "[Epic UMGToolSet.UMGToolSet] Replaces a widget instance in the blueprint's widget tree with a new instance created from a different template widget class. Preserves references for members that exist on both classes with a compatible type/signature: bindings, BP graph variable references, animation bindings, and delegate bindings. Members without a compatible counterpart on the new class are listed in the returned report; references to those members in the outer blueprint will become orphaned graph nodes / dangling bindings. Params: widgetBlueprint, widgetToReplace, templateClass. On UE 5.9 it calls UMGEditor.WidgetBlueprintEditorSubsystem.ReplaceWidgetWithTemplate (Params: widgetBlueprint, widgetToReplace, templateClass).", "epic_call_tool"),
  },
  epic_screenshot: {
    epicSchema: S_epic_screenshot,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.Screenshot", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic SlateInspectorToolset.SlateInspectorToolset] Screenshot a Slate widget or an editor window. Prefer this over SceneTools.take_screenshot for Editor UI; use SceneTools only for 3D viewport. Pass a window ref from Windows(\"list\") to capture a specific window; with no ref it captures the active window, falling back to the main editor window when nothing is active. A widget partly outside its window is cropped to the part inside it. Fails with an error naming the reason when there is nothing to capture: an unknown ref, a widget in no visible window, or one entirely outside it. Params: ref", "epic_call_tool"),
  },
  epic_scroll: {
    epicSchema: S_epic_scroll,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.Scroll", engines: ["5.9"] },
    ...bp("unknown", "[Epic SlateInspectorToolset.SlateInspectorToolset] Scroll the mouse wheel over a Slate widget. Hit-tested like Click; returns false without scrolling if the point is covered or unarranged. Available on UE 5.9; absent from UE 5.8. Params: ref, params?", "epic_call_tool"),
  },
  epic_select_option: {
    epicSchema: S_epic_select_option,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.SelectOption", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic SlateInspectorToolset.SlateInspectorToolset] Select an option in a Slate combobox by its text label. Opens the dropdown, finds the matching text, and clicks it. Params: ref, value", "epic_call_tool"),
  },
  epic_set_binding_mode: {
    epicSchema: S_epic_set_binding_mode,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.SetBindingMode", engines: ["5.9"] },
    ...bp("mutate", "[Epic MVVMToolset.MVVMToolset] Sets the binding mode (OneTimeToDestination/OneWayToDestination/TwoWay/OneWayToSource) for an existing view binding. Not supported during PIE. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, bindingId, mode", "epic_call_tool"),
  },
  epic_set_conversion_function_argument_literal: {
    epicSchema: S_epic_set_conversion_function_argument_literal,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.SetConversionFunctionArgumentLiteral", engines: ["5.9"] },
    ...bp("mutate", "[Epic MVVMToolset.MVVMToolset] Sets a literal value on an input pin of a binding's conversion function, then recompiles the WidgetBlueprint. A pin bound to a property path must be cleared first with ClearConversionFunctionArgumentPath. If the recompile fails the value is kept; use CompileWidgetBlueprint to read the errors. Not supported during PIE. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, bindingId, argumentName, value, bSourceToDestination?", "epic_call_tool"),
  },
  epic_set_event_binding_argument_literal: {
    epicSchema: S_epic_set_event_binding_argument_literal,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.SetEventBindingArgumentLiteral", engines: ["5.9"] },
    ...bp("mutate", "[Epic MVVMToolset.MVVMToolset] Sets a literal value on an input pin of an event binding, then recompiles the WidgetBlueprint if the edit changed it. A pin bound to a property path is refused. If the recompile fails the value is kept; use CompileWidgetBlueprint to read the errors. Not supported during PIE. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, event, argumentName, value", "epic_call_tool"),
  },
  epic_set_input_type_lock: {
    epicSchema: S_epic_set_input_type_lock,
    epicTool: { toolset: "CommonUIToolset.CommonUIToolset", name: "CommonUIToolset.CommonUIToolset.SetInputTypeLock", engines: ["5.9"] },
    ...bp("mutate", "[Epic CommonUIToolset.CommonUIToolset] Lock the effective input type for a player until the lock is removed. Available on UE 5.9; absent from UE 5.8. Params: inputType, reason?, localPlayerIndex?", "epic_call_tool"),
  },
  epic_set_named_slot_content: {
    epicSchema: S_epic_set_named_slot_content,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.SetNamedSlotContent", engines: ["5.8"], successors: [{ toolset: "UMGEditor.WidgetBlueprintEditorSubsystem", name: "UMGEditor.WidgetBlueprintEditorSubsystem.SetNamedSlotContent", engines: ["5.9"], schema: S_epic_set_named_slot_content__1 }] },
    ...bp("mutate", "[Epic UMGToolSet.UMGToolSet] Sets content for a named slot. Returns full widget info including Slot pointer. Params: widgetBlueprint, hostWidget, slotName, widgetClass, widgetName. On UE 5.9 it calls UMGEditor.WidgetBlueprintEditorSubsystem.SetNamedSlotContent (Params: widgetBlueprint, slotName, widgetClass, widgetName, hostWidget?).", "epic_call_tool"),
  },
  epic_set_tree_visualization: {
    epicSchema: S_epic_set_tree_visualization,
    epicTool: { toolset: "CommonUIToolset.CommonUIToolset", name: "CommonUIToolset.CommonUIToolset.SetTreeVisualization", engines: ["5.9"] },
    ...bp("mutate", "[Epic CommonUIToolset.CommonUIToolset] Toggle an in-viewport overlay that outlines activatable widgets, color-coded by activation/focus state: green bold with a black halo = leafmost active node, green dashed = activated in the active-root chain, yellow dashed = activated outside the active root, dim grey = deactivated (only with bIncludeInactive), cyan = the leafmost widget's desired focus target, orange = the widget actually holding Slate focus, white dashed = active root boundary. Outlines are stacked by Slate paint layer, so the widget CommonUI treats as topmost is drawn on top. Each activatable's label ends in [ActivationFocus] or [NoActivationFocus]; labels of widgets that opt out of activation focus are drawn in magenta. A legend with the active input mode and type is drawn top-left. The overlay is hit-test-invisible, so it does not perturb the input being debugged; it is PIE-only and auto-disables on PIE end. A multi-client session gets one overlay per client window, each showing only the players that window owns. Available on UE 5.9; absent from UE 5.8. Params: bEnabled, bIncludeInactive?, bShowLabels?, bLeafmostOnly?, bShowLegend?", "epic_call_tool"),
  },
  epic_set_view_condition_operation: {
    epicSchema: S_epic_set_view_condition_operation,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.SetViewConditionOperation", engines: ["5.9"] },
    ...bp("mutate", "[Epic MVVMToolset.MVVMToolset] Sets the comparison of an existing condition, then compiles the widget blueprint; compile errors do not fail the call and are reported in the condition's CompileErrors. Not supported during PIE. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, condition, operation, value, maxValue?", "epic_call_tool"),
  },
  epic_set_widget_animation_display_label: {
    epicSchema: S_epic_set_widget_animation_display_label,
    epicTool: { toolset: "WidgetAnimationToolset.WidgetAnimationToolset", name: "WidgetAnimationToolset.WidgetAnimationToolset.SetWidgetAnimationDisplayLabel", engines: ["5.9"] },
    ...bp("mutate", "[Epic WidgetAnimationToolset.WidgetAnimationToolset] Set the user-facing display label shown in the Animations panel. Available on UE 5.9; absent from UE 5.8. Params: animation, displayLabel", "epic_call_tool"),
  },
  epic_set_widget_animation_length: {
    epicSchema: S_epic_set_widget_animation_length,
    epicTool: { toolset: "WidgetAnimationToolset.WidgetAnimationToolset", name: "WidgetAnimationToolset.WidgetAnimationToolset.SetWidgetAnimationLength", engines: ["5.9"] },
    ...bp("mutate", "[Epic WidgetAnimationToolset.WidgetAnimationToolset] Set the animation playback length in seconds. Updates the MovieScene playback range to [0, LengthSeconds] in tick resolution. LengthSeconds must be > 0. Available on UE 5.9; absent from UE 5.8. Params: animation, lengthSeconds", "epic_call_tool"),
  },
  epic_set_widget_preview_active: {
    epicSchema: S_epic_set_widget_preview_active,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.SetWidgetPreviewActive", engines: ["5.9"] },
    ...bp("mutate", "[Epic MVVMToolset.MVVMToolset] Turns the View Bindings design-time preview on or off for a WidgetBlueprint, exactly like the eye toggle on the View Bindings panel toolbar. The WidgetBlueprint's designer must be open for the canvas to update; the state is stored either way. No-ops while the per-user Enable Design-Time View Model Preview editor preference is off. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, bActive", "epic_call_tool"),
  },
  epic_set_widget_view_binding_enabled: {
    epicSchema: S_epic_set_widget_view_binding_enabled,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.SetWidgetViewBindingEnabled", engines: ["5.9"] },
    ...bp("mutate", "[Epic MVVMToolset.MVVMToolset] Changes the enabled and compile flag for an existing widget view binding. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, bindingId, bEnabled?, bCompile?", "epic_call_tool"),
  },
  epic_set_widget_view_config: {
    epicSchema: S_epic_set_widget_view_config,
    epicTool: { toolset: "MVVMToolset.MVVMToolset", name: "MVVMToolset.MVVMToolset.SetWidgetViewConfig", engines: ["5.9"] },
    ...bp("mutate", "[Epic MVVMToolset.MVVMToolset] Writes the MVVM View configuration on the WidgetBlueprint's View. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, configuration", "epic_call_tool"),
  },
  epic_show_widget_lifetime_log_window: {
    epicSchema: S_epic_show_widget_lifetime_log_window,
    epicTool: { toolset: "UMGEditor.UMGDebuggerSubsystem", name: "UMGEditor.UMGDebuggerSubsystem.ShowWidgetLifetimeLogWindow", engines: ["5.9"] },
    ...bp("mutate", "[Epic UMGEditor.UMGDebuggerSubsystem] Opens or focuses the Widget Lifetime Log editor tab, which shows the same recording. Returns false without a Slate UI. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_snapshot: {
    epicSchema: S_epic_snapshot,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.Snapshot", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic SlateInspectorToolset.SlateInspectorToolset] Capture a Slate UI accessibility snapshot. Use this to read the current widget tree and discover refs for action tools (Click, Type, Hover, etc.). A shallow root observer (depth 0) covers top-level windows automatically. Before interacting with a specific window or panel, call Observe() on it to get deep coverage, then Snapshot that subtree to see its contents. Refs discovered by a previous Snapshot remain usable. You do NOT need to call Snapshot again before every action. Each [pos=X,Y size=W,H] is the widget's on-screen bounds in desktop pixels, whatever the DPI scale or rotation, the same space as the pointer and touch tools' screen positions and offsets. Params: ref, maxDepth?, bIncludeSourceLocations?", "epic_call_tool"),
  },
  epic_start_event_logging: {
    epicSchema: S_epic_start_event_logging,
    epicTool: { toolset: "CommonUIToolset.CommonUIToolset", name: "CommonUIToolset.CommonUIToolset.StartEventLogging", engines: ["5.9"] },
    ...bp("mutate", "[Epic CommonUIToolset.CommonUIToolset] Begin logging CommonUI events (widget activation/deactivation/rebuild, input mode/config changes, input method changes, container displayed-widget/transition changes) into an in-memory ring buffer. Restarting clears the buffer. Available on UE 5.9; absent from UE 5.8. Params: bCaptureCallstacks?, bufferSize?", "epic_call_tool"),
  },
  epic_start_focus_recording: {
    epicSchema: S_epic_start_focus_recording,
    epicTool: { toolset: "SlateDebugger.SlateDebuggerSubsystem", name: "SlateDebugger.SlateDebuggerSubsystem.StartFocusRecording", engines: ["5.9"] },
    ...bp("mutate", "[Epic SlateDebugger.SlateDebuggerSubsystem] Starts recording Slate focus and navigation events into an in-memory buffer, discarding events already captured. Available on UE 5.9; absent from UE 5.8. Params: bCaptureCallstack?, bCaptureContext?, bufferSize?", "epic_call_tool"),
  },
  epic_start_service: {
    epicSchema: S_epic_start_service,
    epicTool: { toolset: "WidgetFinder.WidgetFinderSubsystem", name: "WidgetFinder.WidgetFinderSubsystem.StartService", engines: ["5.9"] },
    ...bp("mutate", "[Epic WidgetFinder.WidgetFinderSubsystem] Begin mirroring the live Slate widget tree into the store, so Query/Highlight/WaitForQuery can work. The service is off by default. Call this once before locating widgets and leave it running for the session. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_start_widget_lifetime_logging: {
    epicSchema: S_epic_start_widget_lifetime_logging,
    epicTool: { toolset: "UMGEditor.UMGDebuggerSubsystem", name: "UMGEditor.UMGDebuggerSubsystem.StartWidgetLifetimeLogging", engines: ["5.9"] },
    ...bp("mutate", "[Epic UMGEditor.UMGDebuggerSubsystem] Starts recording Constructed, Destructed and Rebuilt events for every user widget, discarding events already captured. Query them with GetWidgetLifetimeEvents. Available on UE 5.9; absent from UE 5.8. Params: bufferSize?, bCaptureInvalidations?", "epic_call_tool"),
  },
  epic_stop_event_logging: {
    epicSchema: S_epic_stop_event_logging,
    epicTool: { toolset: "CommonUIToolset.CommonUIToolset", name: "CommonUIToolset.CommonUIToolset.StopEventLogging", engines: ["5.9"] },
    ...bp("mutate", "[Epic CommonUIToolset.CommonUIToolset] Stop logging; the buffer is retained for querying. Does nothing when not logging. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_stop_focus_recording: {
    epicSchema: S_epic_stop_focus_recording,
    epicTool: { toolset: "SlateDebugger.SlateDebuggerSubsystem", name: "SlateDebugger.SlateDebuggerSubsystem.StopFocusRecording", engines: ["5.9"] },
    ...bp("mutate", "[Epic SlateDebugger.SlateDebuggerSubsystem] Stops recording. The buffer is retained for querying. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_stop_service: {
    epicSchema: S_epic_stop_service,
    epicTool: { toolset: "WidgetFinder.WidgetFinderSubsystem", name: "WidgetFinder.WidgetFinderSubsystem.StopService", engines: ["5.9"] },
    ...bp("mutate", "[Epic WidgetFinder.WidgetFinderSubsystem] Stop mirroring and empty the store, ending the scanning cost. Pending WaitForQuery calls fail at once, and the query tools fail until StartService. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_stop_widget_lifetime_logging: {
    epicSchema: S_epic_stop_widget_lifetime_logging,
    epicTool: { toolset: "UMGEditor.UMGDebuggerSubsystem", name: "UMGEditor.UMGDebuggerSubsystem.StopWidgetLifetimeLogging", engines: ["5.9"] },
    ...bp("mutate", "[Epic UMGEditor.UMGDebuggerSubsystem] Stops recording. Captured events stay queryable until ClearWidgetLifetimeLog or the next start. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_toggle_widget_as_variable: {
    epicSchema: S_epic_toggle_widget_as_variable,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.ToggleWidgetAsVariable", engines: ["5.8"], successors: [{ toolset: "UMGEditor.WidgetBlueprintEditorSubsystem", name: "UMGEditor.WidgetBlueprintEditorSubsystem.ToggleWidgetAsVariable", engines: ["5.9"], schema: S_epic_toggle_widget_as_variable__1 }] },
    ...bp("mutate", "[Epic UMGToolSet.UMGToolSet] Sets the bIsVariable flag. Params: widgetBlueprint, widget, bIsVariable. On UE 5.9 it calls UMGEditor.WidgetBlueprintEditorSubsystem.ToggleWidgetAsVariable (Params: widgetBlueprint, widget, bIsVariable, bMarkBlueprintModified?).", "epic_call_tool"),
  },
  epic_touch_end: {
    epicSchema: S_epic_touch_end,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.TouchEnd", engines: ["5.9"] },
    ...bp("unknown", "[Epic SlateInspectorToolset.SlateInspectorToolset] Lift a finger held by an earlier TouchStart at an absolute screen position. Available on UE 5.9; absent from UE 5.8. Params: x, y, touchIndex?", "epic_call_tool"),
  },
  epic_touch_move: {
    epicSchema: S_epic_touch_move,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.TouchMove", engines: ["5.9"] },
    ...bp("unknown", "[Epic SlateInspectorToolset.SlateInspectorToolset] Move a finger held by an earlier TouchStart to an absolute screen position. Available on UE 5.9; absent from UE 5.8. Params: x, y, touchIndex?", "epic_call_tool"),
  },
  epic_touch_start: {
    epicSchema: S_epic_touch_start,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.TouchStart", engines: ["5.9"] },
    ...bp("unknown", "[Epic SlateInspectorToolset.SlateInspectorToolset] Put a finger down and leave it there; it shares the left mouse button with the cursor, so do not mix it with a held MouseDown. TouchEnd or ReleaseHeldInput lifts it, as does the hold watchdog after SlateInspectorToolset.HeldInputTimeoutSeconds without a pointer, touch, KeyDown or KeyUp call. Available on UE 5.9; absent from UE 5.8. Params: ref, x?, y?, touchIndex?", "epic_call_tool"),
  },
  epic_trace_widget_property_overrides: {
    epicSchema: S_epic_trace_widget_property_overrides,
    epicTool: { toolset: "WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset", name: "WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset.TraceWidgetPropertyOverrides", engines: ["5.9"] },
    ...bp("read", "[Epic WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset] Traces every statically-discoverable write to a widget property across the whole tree, merging Blueprint-graph writes with static origins (class defaults, archetype overrides, bindings). Results are grouped per (widget, property) pair. The highest-Order Applied entry is the Winner; earlier entries are Redundant, later ones Conflict. With no Applied entry, the highest-Order entry is an uncertain best guess and all are Conflict. Available on UE 5.9; absent from UE 5.8. Params: widgetBlueprint, propertyNameFilter?, widgetNameFilter?", "epic_call_tool"),
  },
  epic_type: {
    epicSchema: S_epic_type,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.Type", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic SlateInspectorToolset.SlateInspectorToolset] Type text into a Slate text input widget, focusing it first; use PressKey for keys that are not characters. Params: ref, text, submit?", "epic_call_tool"),
  },
  epic_unobserve: {
    epicSchema: S_epic_unobserve,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.Unobserve", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic SlateInspectorToolset.SlateInspectorToolset] Remove an observer by its identifier. Params: identifier", "epic_call_tool"),
  },
  epic_wait_for: {
    epicSchema: S_epic_wait_for,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.WaitFor", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic SlateInspectorToolset.SlateInspectorToolset] Check once whether text is present or absent in the Slate widget tree (substring match); poll to wait. Params: text?, textGone?", "epic_call_tool"),
  },
  epic_wait_for_query: {
    epicSchema: S_epic_wait_for_query,
    epicTool: { toolset: "WidgetFinder.WidgetFinderSubsystem", name: "WidgetFinder.WidgetFinderSubsystem.WaitForQuery", engines: ["5.9"] },
    ...bp("read", "[Epic WidgetFinder.WidgetFinderSubsystem] Wait, without polling, until a query's raw match count meets Comparison/Threshold, then return the matches. Prefer it over repeated Query calls and over the SlateInspectorToolset Snapshot/Observe/Screenshot tools. Completes once, with bConditionMet, or with bTimedOut after TimeoutSeconds, which still counts as success. A bad query, a stopped service, or stopping the service mid-wait fails the call. Available on UE 5.9; absent from UE 5.8. Params: request", "epic_call_tool"),
  },
  epic_windows: {
    epicSchema: S_epic_windows,
    epicTool: { toolset: "SlateInspectorToolset.SlateInspectorToolset", name: "SlateInspectorToolset.SlateInspectorToolset.Windows", engines: ["5.8","5.9"] },
    ...bp("unknown", "[Epic SlateInspectorToolset.SlateInspectorToolset] List, select, close or resize top-level Slate editor windows. \"list\" returns a JSON array of {index, ref, title, active, regular, x, y, width, height}. Pass the ref to Screenshot or Snapshot to target that window; the index is positional and changes as windows open and close, the ref does not. \"resize\" and \"maximize\" also work under -RenderOffScreen. Params: index?, width?, height?, ref?, input? (carries action, which cannot be sent at the top level)", "epic_call_tool"),
  },
  epic_wrap_widgets: {
    epicSchema: S_epic_wrap_widgets,
    epicTool: { toolset: "UMGToolSet.UMGToolSet", name: "UMGToolSet.UMGToolSet.WrapWidgets", engines: ["5.8"], successors: [{ toolset: "UMGEditor.WidgetBlueprintEditorSubsystem", name: "UMGEditor.WidgetBlueprintEditorSubsystem.WrapWidgets", engines: ["5.9"], schema: S_epic_wrap_widgets__1 }] },
    ...bp("mutate", "[Epic UMGToolSet.UMGToolSet] Wraps one or more widgets in a new panel widget of the specified class. Only the root-most widgets in the selection are wrapped - children of other selected widgets are skipped because their parent will be wrapped. Returns info for each newly created wrapper. Use ObjectTools.list_properties on each returned Widget and Slot to discover property names before calling set_properties (padding, alignment, anchors, etc. vary per panel class). Params: widgetBlueprint, widgets, wrapperClass. On UE 5.9 it calls UMGEditor.WidgetBlueprintEditorSubsystem.WrapWidgets (Params: widgetBlueprint, widgets, wrapperClass).", "epic_call_tool"),
  },
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  accessorClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Class the fields would be accessed from; gates private/protected visibility."),
  animation: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  animationName: z.string().optional().describe("Internal asset name. Must not collide with an existing animation."),
  argumentName: z.string().optional().describe("ArgumentName from ListWidgetViewBindings"),
  assetName: z.string().optional().describe("Name for the new blueprint asset."),
  bActive: z.boolean().optional().describe("True to enable the design-time preview, false to disable it."),
  bCaptureCallstack: z.boolean().optional().describe("Capture a C++ callstack on each FocusReceived (heavier)."),
  bCaptureCallstacks: z.boolean().optional().describe("Capture a C++ callstack on each Activated/Deactivated event; heavier."),
  bCaptureContext: z.boolean().optional().describe("Ask the registered focus-context providers (CommonUI, for one) to add context to each FocusReceived."),
  bCaptureInvalidations: z.boolean().optional().describe("Also record Invalidated events for each widget's root Slate widget. Off by default because a HUD invalidates every frame."),
  bCompile: z.boolean().optional().describe("If false then the binding is visible in the editor, but is not compiled and cannot be used at runtime."),
  bEnabled: z.boolean().optional().describe("Turn the overlay on or off. Enabling raises a script error when there is no PIE session or the session has no game viewport; disabling never raises."),
  bIncludeCallstacks: z.boolean().optional().describe("Symbolicate and include captured callstacks (slow; only for small result sets)."),
  bIncludeComposedColors: z.boolean().optional().describe("When true, populates each entry's ComposedColors; defaults to false."),
  bIncludeFullDescription: z.boolean().optional().describe("When true, populates Description (verbose per-property text); defaults to false."),
  bIncludeFullDump: z.boolean().optional().describe("Include the full-depth tree text (captured from the CommonUI.DumpActivatableTree console command); false = structured one-level data only."),
  bIncludeInactive: z.boolean().optional().describe("Also outline deactivated activatable widgets (dim grey)."),
  bIncludePath: z.boolean().optional().describe("false = omit the widget path (leaf + cause + context only) for cheap bulk browsing."),
  bIncludeSourceLocations: z.boolean().optional().describe("Include [src=File:Line] widget creation tags (default false); true bypasses snapshot caches."),
  bindingGuid: z.string().optional(),
  bindingId: z.string().optional().describe("The guid of the binding to change"),
  bIsVariable: z.boolean().optional().describe("True to expose as a blueprint variable, false to hide it."),
  bLeafmostOnly: z.boolean().optional().describe("Hide every activatable outline except the leafmost active node. Focus outlines are still drawn. Overrides bIncludeInactive."),
  bMarkBlueprintModified: z.boolean().optional().describe("True to mark the blueprint structurally modified after the change."),
  bMoveAfter: z.boolean().optional().describe("True to place after RelativeToComponentClass, false to place before."),
  bShouldJumpToNode: z.boolean().optional().describe("When true, the editor focuses the existing handler node if the event is already bound (this returns false without a script error) and the newly created node ot…"),
  bShowLabels: z.boolean().optional().describe("Draw widget names and state labels next to each outline."),
  bShowLegend: z.boolean().optional().describe("Draw the colour key and per-player input mode lines in the top-left corner."),
  bSkipDefaultValues: z.boolean().optional().describe("When true (default), a property whose value matches the class default is excluded."),
  bSourceToDestination: z.boolean().optional().describe("True for the source-to-destination conversion, false for destination-to-source"),
  bufferSize: z.number().optional().describe("Ring buffer capacity (oldest events drop when full). Must be at least 1; a smaller value raises a script error and leaves the event logger untouched."),
  button: z.string().optional().describe("\"left\", \"right\", or \"middle\"."),
  bWarnAndAskUser: z.boolean().optional().describe("When true, the user is prompted if the replaced host is referenced as a Blueprint variable. When false (default) the host is deleted silently."),
  childIndex: z.number().optional().describe("Position in parent's child list (0 = first child). -1 (default) appends to end."),
  classFilter: z.string().optional().describe("Case-insensitive substring of the widget class name. Empty matches all."),
  componentClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The UIComponent subclass to add."),
  componentClassToMove: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The UIComponent subclass to reorder."),
  condition: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Condition to remove"),
  configuration: z.record(z.string(), z.unknown()).optional().describe("New view configuration."),
  context: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Object that contains the property. Can be the following: - UWidget: property is from a widget in the widget blueprint's widgettree - UClass: property is from a…"),
  conversionName: z.string().optional().describe("Optional parameter, name of a conversion function to use. If none is provided, one will be inferred if the types don't match. See ListConversionFunctions. If t…"),
  defaultValue: z.string().optional().describe("If a default property is desired, string representation of the value"),
  destinationContext: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Object that contains the destination. Can be the following: - UWidget: destination is on a widget in the widget blueprint's widgettree - UClass: destination is…"),
  destinationPropertyPath: z.string().optional().describe("Function to call or property to set when the condition is true. Contains a dot '.' separated path to the field (path.SubField)"),
  displayLabel: z.string().optional(),
  doubleClick: z.boolean().optional().describe("True for double-click."),
  endRef: z.string().optional().describe("Slate widget ref for the drop target; empty means \"drag StartRef itself\"."),
  event: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Event binding to change, from ListWidgetViewEventBindings"),
  eventContext: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Event object that contains the source property. Can be the following: - UWidget: source property is from a widget in the widget blueprint's widgettree - UClass…"),
  eventName: z.string().optional().describe("Name of the multicast delegate UPROPERTY on PropertyClass (e.g. \"OnClicked\")."),
  eventPropertyPath: z.string().optional().describe("Event property to listen to. Contains a dot '.' separated path to the reflected property (path.SubField). Case-sensitive"),
  eventTypeFilter: z.string().optional().describe("Same values as GetLoggedEvents. Empty = all; anything else raises a script error listing the accepted values."),
  fields: z.array(z.unknown()).optional().describe("Array of {Ref, Value, FieldType} where FieldType is \"textbox\", \"checkbox\", or \"combobox\"."),
  filter: z.string().optional().describe("Substring to match against class names. Pass empty string to return all classes."),
  focusCauseFilter: z.string().optional().describe("One of Mouse/Navigation/SetDirectly/Cleared/OtherWidgetLostFocus/WindowActivate. Empty = all; excludes non-focus records when set."),
  folderPath: z.string().optional().describe("Content folder path, e.g. \"/Game/UI/Widgets\"."),
  height: z.number().optional().describe("New height in screen pixels, for \"resize\"."),
  hostWidget: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The widget that owns the named slot, or null to target the root WidgetTree."),
  identifier: z.string().optional().describe("The identifier returned by Observe()."),
  index: z.number().optional().describe("Window index for select/close/resize/maximize. Ignored when Ref is given."),
  input: z.record(z.string(), z.unknown()).optional().describe("The wrapped tool's arguments as an object. Use it for an argument this category cannot take at the top level because its dispatcher owns that name."),
  inputJson: z.string().optional().describe("The wrapped tool's arguments as a raw JSON string. Takes full control: nothing is merged in when it is set."),
  inputType: z.string().optional().describe("One of MouseAndKeyboard, Gamepad, Touch. A type the running platform does not support is rejected and raises a script error rather than silently not applying."),
  key: z.string().optional().describe("FKey name with optional \"Shift+\", \"Ctrl+\", \"Alt+\", \"Cmd+\" prefixes, e.g. \"Enter\", \"Ctrl+A\", \"Gamepad_DPad_Up\"."),
  lengthSeconds: z.number().optional().describe("Initial playback-range length in seconds. Defaults to 1.0s."),
  localPlayerIndex: z.number().optional().describe("Restrict matching to this player's widgets; -1 = any player."),
  maxCount: z.number().optional().describe("Maximum entries, taken from the most recent; 0 or less = unlimited."),
  maxDepth: z.number().optional().describe("Maximum depth to walk from the root (default 30); negative values count as 0."),
  maxValue: z.number().optional().describe("Upper bound of a Between operation"),
  mode: z.string().optional().describe("New binding mode to apply"),
  modifiers: z.record(z.string(), z.unknown()).optional().describe("Modifier keys held during the click."),
  namedSlot: z.string().optional().describe("The slot whose content replaces WidgetToReplace."),
  nameFilter: z.string().optional().describe("Case-insensitive substring of the widget name. Empty matches all."),
  newDisplayName: z.string().optional().describe("The new display name."),
  newName: z.string().optional(),
  newParent: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The destination panel widget."),
  objectToBind: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  occurrenceFromEnd: z.number().optional().describe("0 = most recent time it received focus, 1 = the time before, etc."),
  offsetX: z.number().optional().describe("Horizontal offset from the widget center in screen pixels; positive moves right (default 0)."),
  offsetY: z.number().optional().describe("Vertical offset from the widget center in screen pixels; positive moves down (default 0)."),
  operation: z.string().optional().describe("Comparison of the source value against Value"),
  packagePath: z.string().optional().describe("Target path in which the asset should reside"),
  params: z.record(z.string(), z.unknown()).optional().describe("Button, double-click, modifiers and position; omitted fields keep their defaults."),
  parentClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The parent UUserWidget class. Get this from GetWidgets Info.ParentClass on the source blueprint."),
  parentWidget: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The panel widget to add to. Pass null to add to root, or to make this widget the root if the tree is empty."),
  playerFilter: z.number().optional().describe("Local player index to keep. -1 keeps every player and widgets with no owning player."),
  propertyClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Class declaring the delegate, typically the widget's class (e.g. UButton::StaticClass())."),
  propertyName: z.string().optional().describe("Name of the blueprint variable owning the event."),
  propertyNameFilter: z.string().optional().describe("Substring match against property name; TEXT(\"\") matches every property."),
  propertyPath: z.string().optional().describe("Dot '.' separated path to the reflected property (path.SubField). Case-sensitive"),
  propertyType: z.string().optional().describe("Desired type of the new property. Corresponds with PinType"),
  reason: z.string().optional().describe("The lock key used when the lock was added. Raises a script error when the player holds no lock under that reason."),
  ref: z.string().optional().describe("Slate widget ref."),
  relativeToComponentClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The UIComponent subclass to move relative to."),
  repeatCount: z.number().optional().describe("Extra auto-repeat presses between the press and the release, 0..1000 (default 0)."),
  request: z.record(z.string(), z.unknown()).optional(),
  scopeFilter: z.string().optional().describe("Same substring-match semantics as GetWidgetConnectionMap's ScopeFilter."),
  searchPath: z.string().optional().describe("Mount point search path for ViewModel assets"),
  seq: z.number().optional().describe("The record's sequence number."),
  slotName: z.string().optional().describe("Name of the slot to fill (e.g., \"content\", \"header\")."),
  sourceContext: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Object that contains the source property. Can be the following: - UClass: source property is from a widget blueprint's ViewModel - Null: source property is fro…"),
  sourcePropertyPath: z.string().optional().describe("Property or getter to compare. Contains a dot '.' separated path to the field (path.SubField). Must be a bool, uint8 (not an enum), int32, int64, float or doub…"),
  startRef: z.string().optional().describe("Slate widget ref for the drag source."),
  startWidget: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("nullptr = full tree from root."),
  submit: z.boolean().optional().describe("Press Enter after typing (default false)."),
  targetClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("ViewModel (or any UClass) to enumerate bindable fields on."),
  templateClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The widget class to create the replacement from."),
  text: z.string().optional().describe("Text to type."),
  textGone: z.string().optional().describe("Text that must be absent (default empty = skip)."),
  touchIndex: z.number().optional().describe("Finger index 0..9 matching the TouchStart call (default 0)."),
  userIndex: z.number().optional().describe("Slate user to send to (default -1 = default controller or keyboard user); an explicit index must exist."),
  value: z.unknown().optional().describe("Exact option text to select."),
  viewModel: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("ViewModel asset to use."),
  viewModelClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("View model class to use."),
  viewModelName: z.string().optional().describe("Desired ViewModel asset name."),
  widget: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The widget to move."),
  widgetBlueprint: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The widget blueprint to modify."),
  widgetClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The widget class to instantiate."),
  widgetDisplayName: z.string().optional().describe("Display name for the new widget instance."),
  widgetName: z.string().optional().describe("Name of the widget instance to add the component to."),
  widgetNameFilter: z.string().optional().describe("Case-insensitive substring matched against widget name and class. Empty = all."),
  widgets: z.array(z.unknown()).optional().describe("The widgets to wrap. Must all be in WidgetBlueprint's tree."),
  widgetToReplace: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The panel widget to replace with its first child."),
  width: z.number().optional().describe("New width in screen pixels, for \"resize\"."),
  wrapperClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The panel widget class to wrap with (must be a UPanelWidget subclass)."),
  x: z.number().optional().describe("Horizontal screen position in pixels; positive is right."),
  y: z.number().optional().describe("Vertical screen position in pixels; positive is down."),
};
