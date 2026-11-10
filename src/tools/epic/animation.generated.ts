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

const S_epic_add_actors = {"properties":{"actors":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["actors"]} as const;
const S_epic_add_actors__level_sequence_editor_subsystem = {"properties":{"actors":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["actors"]} as const;
const S_epic_add_actors_by_name = {"properties":{"actor_names":{"type":"array","items":{"type":"string"}}},"required":["actor_names"]} as const;
const S_epic_add_actors_by_name__level_sequence_editor_subsystem = {"properties":{"actorNames":{"type":"array","items":{"type":"string"}}},"required":["actorNames"]} as const;
const S_epic_add_actors_to_binding = {"properties":{"actors":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["actors","binding"]} as const;
const S_epic_add_actors_to_binding__level_sequence_editor_subsystem = {"properties":{"actors":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"objectBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["actors","objectBinding"]} as const;
const S_epic_add_anim_layer_from_selection = {"properties":{}} as const;
const S_epic_add_auto_bake = {"properties":{"inBindings":{"type":"array","items":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}}},"required":["inBindings"]} as const;
const S_epic_add_auto_bake_with_anim_sequences = {"properties":{"inBindings":{"type":"array","items":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"inAnimSequences":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["inBindings","inAnimSequences"]} as const;
const S_epic_add_backward_solve_graph = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["control_rig"]} as const;
const S_epic_add_binding_to_folder = {"properties":{"folder":{"type":"object","properties":{"refPath":{}}},"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["folder","binding"]} as const;
const S_epic_add_bone = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"parent":{"type":"object","properties":{"type":{"type":"string","enum":["None","Bone","Null","Space","Control","Curve","Reference","Connector","Socket","All"]},"name":{"type":"string"}}},"transform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}}},"required":["control_rig","name"]} as const;
const S_epic_add_bool_key = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelName":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"boolean"}},"required":["section","channelName","frame","value"]} as const;
const S_epic_add_channel_override = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelName":{"type":"string"},"overrideClass":{"type":"object","properties":{"refPath":{}}}},"required":["section","channelName","overrideClass"]} as const;
const S_epic_add_child_folder = {"properties":{"targetFolder":{"type":"object","properties":{"refPath":{}}},"folderToAdd":{"type":"object","properties":{"refPath":{}}}},"required":["targetFolder","folderToAdd"]} as const;
const S_epic_add_child_object_binding = {"properties":{"folder":{"type":"object","properties":{"refPath":{}}},"inObjectBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["folder","inObjectBinding"]} as const;
const S_epic_add_child_track = {"properties":{"folder":{"type":"object","properties":{"refPath":{}}},"inTrack":{"type":"object","properties":{"refPath":{}}}},"required":["folder","inTrack"]} as const;
const S_epic_add_comment = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"graphPath":{"type":"string"},"comment":{"type":"string"},"position":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}},"size":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}},"color":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}}},"required":["deformer","graphPath","comment","position","size","color"]} as const;
const S_epic_add_component_binding = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"bindingName":{"type":"string"},"componentSource":{"type":"string"}},"required":["deformer","bindingName","componentSource"]} as const;
const S_epic_add_constraint = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"inType":{"type":"string","enum":["Translation","Rotation","Scale","Parent","LookAt"]},"inChild":{"type":"object","properties":{"refPath":{}}},"inParent":{"type":"object","properties":{"refPath":{}}},"bMaintainOffset":{"type":"boolean"}},"required":["world","inType","inChild","inParent","bMaintainOffset"]} as const;
const S_epic_add_constraint_between = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"type":{"type":"string","enum":["Translation","Rotation","Scale","Parent","LookAt"]},"childObject":{"type":"object","properties":{"refPath":{}}},"childAttachment":{"type":"string"},"parentObject":{"type":"object","properties":{"refPath":{}}},"parentAttachment":{"type":"string"},"bMaintainOffset":{"type":"boolean"},"creationTime":{"type":"string","enum":["CurrentFrame","FromStart","Infinite"]}},"required":["world","type","childObject","childAttachment","parentObject","parentAttachment","bMaintainOffset"]} as const;
const S_epic_add_control = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"parent":{"type":"object","properties":{"type":{"type":"string","enum":["None","Bone","Null","Space","Control","Curve","Reference","Connector","Socket","All"]},"name":{"type":"string"}}},"settings":{"type":"object","properties":{"animationType":{"type":"string","enum":["AnimationControl","AnimationChannel","ProxyControl","VisualCue"]},"controlType":{"type":"string","enum":["Bool","Float","Integer","Vector2D","Position","Scale","Rotator","EulerTransform","ScaleFloat"]},"layeredCombineMode":{"type":"string","enum":["Default","Override"]},"displayName":{"type":"string"},"primaryAxis":{"type":"string","enum":["X","Y","Z"]},"bIsCurve":{"type":"boolean"},"limitEnabled":{"type":"array","items":{"type":"object","properties":{"bMinimum":{"type":"boolean"},"bMaximum":{"type":"boolean"}}}},"bDrawLimits":{"type":"boolean"},"minimumValue":{"type":"object","properties":{"floatStorage":{"type":"object","properties":{"float00":{"type":"number"},"float01":{"type":"number"},"float02":{"type":"number"},"float03":{"type":"number"},"float10":{"type":"number"},"float11":{"type":"number"},"float12":{"type":"number"},"float13":{"type":"number"},"float20":{"type":"number"},"float21":{"type":"number"},"float22":{"type":"number"},"float23":{"type":"number"},"float30":{"type":"number"},"float31":{"type":"number"},"float32":{"type":"number"},"float33":{"type":"number"},"float00_2":{"type":"number"},"float01_2":{"type":"number"},"float02_2":{"type":"number"},"float03_2":{"type":"number"},"float10_2":{"type":"number"},"float11_2":{"type":"number"},"float12_2":{"type":"number"},"float13_2":{"type":"number"},"float20_2":{"type":"number"},"float21_2":{"type":"number"},"float22_2":{"type":"number"},"float23_2":{"type":"number"},"float30_2":{"type":"number"},"float31_2":{"type":"number"},"float32_2":{"type":"number"},"float33_2":{"type":"number"},"bValid":{"type":"boolean"}}}}},"maximumValue":{"type":"object","properties":{"floatStorage":{"type":"object","properties":{"float00":{"type":"number"},"float01":{"type":"number"},"float02":{"type":"number"},"float03":{"type":"number"},"float10":{"type":"number"},"float11":{"type":"number"},"float12":{"type":"number"},"float13":{"type":"number"},"float20":{"type":"number"},"float21":{"type":"number"},"float22":{"type":"number"},"float23":{"type":"number"},"float30":{"type":"number"},"float31":{"type":"number"},"float32":{"type":"number"},"float33":{"type":"number"},"float00_2":{"type":"number"},"float01_2":{"type":"number"},"float02_2":{"type":"number"},"float03_2":{"type":"number"},"float10_2":{"type":"number"},"float11_2":{"type":"number"},"float12_2":{"type":"number"},"float13_2":{"type":"number"},"float20_2":{"type":"number"},"float21_2":{"type":"number"},"float22_2":{"type":"number"},"float23_2":{"type":"number"},"float30_2":{"type":"number"},"float31_2":{"type":"number"},"float32_2":{"type":"number"},"float33_2":{"type":"number"},"bValid":{"type":"boolean"}}}}},"bShapeVisible":{"type":"boolean"},"shapeVisibility":{"type":"string","enum":["UserDefined","BasedOnSelection"]},"shapeName":{"type":"string"},"shapeColor":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}},"bIsTransientControl":{"type":"boolean"},"controlEnum":{"type":"object","properties":{"refPath":{}}},"customization":{"type":"object","properties":{"availableSpaces":{"type":"array","items":{"type":"object","properties":{"key":{"type":"object"},"label":{"type":"string"}}}},"removedSpaces":{"type":"array","items":{"type":"object","properties":{"type":{"type":"string","enum":["None","Bone","Null","Space","Control","Curve","Reference","Connector","Socket","All"]},"name":{"type":"string"}}}}}},"drivenControls":{"type":"array","items":{"type":"object","properties":{"type":{"type":"string","enum":["None","Bone","Null","Space","Control","Curve","Reference","Connector","Socket","All"]},"name":{"type":"string"}}}},"bGroupWithParentControl":{"type":"boolean"},"bRestrictSpaceSwitching":{"type":"boolean"},"filteredChannels":{"type":"array","items":{"type":"string","enum":["TranslationX","TranslationY","TranslationZ","Pitch","Yaw","Roll","ScaleX","ScaleY","ScaleZ"]}},"preferredRotationOrder":{"type":"string","enum":["XYZ","XZY","YXZ","YZX","ZXY","ZYX"]},"bUsePreferredRotationOrder":{"type":"boolean"},"shapeTransform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}}}}},"required":["control_rig","name"]} as const;
const S_epic_add_control_rig_available_space = {"properties":{"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"space":{"type":"object","properties":{"type":{"type":"string","enum":["None","Bone","Null","Space","Control","Curve","Reference","Connector","Socket","All"]},"name":{"type":"string"}}}},"required":["controlRig","controlName","space"]} as const;
const S_epic_add_double_key = {"properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}},"inTime":{"type":"object","properties":{"value":{"type":"integer"}}},"newValue":{"type":"number"},"subFrame":{"type":"number"},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]},"inInterpolation":{"type":"string","enum":["Auto","User","Break","Linear","Constant","SmartAuto"]}},"required":["channel","inTime","newValue"]} as const;
const S_epic_add_element = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"element_type":{"type":"string"},"parent":{"type":"object","properties":{"type":{"type":"string","enum":["None","Bone","Null","Space","Control","Curve","Reference","Connector","Socket","All"]},"name":{"type":"string"}}},"transform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}}},"required":["control_rig","name","element_type"]} as const;
const S_epic_add_empty_binding = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["sequence"]} as const;
const S_epic_add_event_graph = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"event_type":{"type":"string","enum":["FORWARD_SOLVE","BACKWARD_SOLVE","INTERACTION"]},"name":{"type":"string"}},"required":["control_rig","event_type"]} as const;
const S_epic_add_event_key = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"eventSection":{"type":"object","properties":{"refPath":{}}},"boundObject":{"type":"object","properties":{"refPath":{}}},"functionName":{"type":"string"},"frame":{"type":"integer"},"bCallInEditor":{"type":"boolean"}},"required":["sequence","eventSection","boundObject","functionName","frame"]} as const;
const S_epic_add_event_node = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}},"event_type":{"type":"string","enum":["FORWARD_SOLVE","BACKWARD_SOLVE","INTERACTION"]},"position":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}}},"required":["control_rig","graph","event_type"]} as const;
const S_epic_add_event_repeater_section = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_add_event_repeater_section__movie_scene_event_track_extensions = {"properties":{"inTrack":{"type":"object","properties":{"refPath":{}}}},"required":["inTrack"]} as const;
const S_epic_add_event_trigger_section = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_add_event_trigger_section__movie_scene_event_track_extensions = {"properties":{"inTrack":{"type":"object","properties":{"refPath":{}}}},"required":["inTrack"]} as const;
const S_epic_add_float_key = {"properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}},"inTime":{"type":"object","properties":{"value":{"type":"integer"}}},"newValue":{"type":"number"},"subFrame":{"type":"number"},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]},"inInterpolation":{"type":"string","enum":["Auto","User","Break","Linear","Constant","SmartAuto"]}},"required":["channel","inTime","newValue"]} as const;
const S_epic_add_graph = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["control_rig","name"]} as const;
const S_epic_add_graph__deformer_graph_scripting_subsystem = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"graphType":{"type":"string"},"graphName":{"type":"string"}},"required":["deformer","graphType","graphName"]} as const;
const S_epic_add_graph_binding = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"graphPath":{"type":"string"},"direction":{"type":"string"},"bindingName":{"type":"string"},"dataType":{"type":"string"},"dataDomain":{"type":"string"}},"required":["deformer","graphPath","direction","bindingName","dataType","dataDomain"]} as const;
const S_epic_add_integer_key = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelName":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"integer"}},"required":["section","channelName","frame","value"]} as const;
const S_epic_add_interaction_graph = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["control_rig"]} as const;
const S_epic_add_kernel = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"graphPath":{"type":"string"},"kernelName":{"type":"string"},"executionDomain":{"type":"string"},"position":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}}},"required":["deformer","graphPath","kernelName","executionDomain","position"]} as const;
const S_epic_add_kernel_binding = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"direction":{"type":"string"},"bindingName":{"type":"string"},"dataType":{"type":"string"},"dataDomain":{"type":"string"},"group":{"type":"string"}},"required":["deformer","nodePath","direction","bindingName","dataType","dataDomain"]} as const;
const S_epic_add_kernel_input_group = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"groupName":{"type":"string"}},"required":["deformer","nodePath","groupName"]} as const;
const S_epic_add_key_bool = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channel_name":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"boolean"}},"required":["section","channel_name","frame","value"]} as const;
const S_epic_add_key_float = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channel_name":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"number"},"interpolation":{"type":"string"}},"required":["section","channel_name","frame","value","interpolation"]} as const;
const S_epic_add_key_integer = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channel_name":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"integer"}},"required":["section","channel_name","frame","value"]} as const;
const S_epic_add_key_string = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channel_name":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"string"}},"required":["section","channel_name","frame","value"]} as const;
const S_epic_add_layer_from_selection = {"properties":{}} as const;
const S_epic_add_link = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"outputPinPath":{"type":"string"},"inputPinPath":{"type":"string"}},"required":["deformer","outputPinPath","inputPinPath"]} as const;
const S_epic_add_link_with_new_pin = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"sourcePinPath":{"type":"string"},"targetNodePath":{"type":"string"}},"required":["deformer","sourcePinPath","targetNodePath"]} as const;
const S_epic_add_loop_binding = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"bindingName":{"type":"string"},"dataType":{"type":"string"},"dataDomain":{"type":"string"}},"required":["deformer","nodePath","bindingName","dataType","dataDomain"]} as const;
const S_epic_add_marked_frame = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"frame":{"type":"integer"}},"required":["sequence","frame"]} as const;
const S_epic_add_marked_frame_range_to_sequence = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"startFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"endFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]},"label":{"type":"string"}},"required":["sequence","startFrame","endFrame"]} as const;
const S_epic_add_mirrored_controls_to_selection = {"properties":{}} as const;
const S_epic_add_node = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"graphPath":{"type":"string"},"nodeType":{"type":"string"},"position":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}}},"required":["deformer","graphPath","nodeType","position"]} as const;
const S_epic_add_nodes_to_node_group = {"properties":{"group":{"type":"object","properties":{"refPath":{}}},"nodes":{"type":"array","items":{"type":"object","properties":{"sessionId":{"type":"string"},"rootSequencePath":{"type":"string"},"focusedSequencePath":{"type":"string"},"focusedTemplateId":{"type":"integer"},"nodePath":{"type":"string"},"nodeType":{"type":"string"},"label":{"type":"string"}}}}},"required":["group","nodes"]} as const;
const S_epic_add_null = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"parent":{"type":"object","properties":{"type":{"type":"string","enum":["None","Bone","Null","Space","Control","Curve","Reference","Connector","Socket","All"]},"name":{"type":"string"}}},"transform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}}},"required":["control_rig","name"]} as const;
const S_epic_add_resource = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"resourceName":{"type":"string"},"dataType":{"type":"string"},"dataDomain":{"type":"string"}},"required":["deformer","resourceName","dataType","dataDomain"]} as const;
const S_epic_add_root_folder = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["sequence","name"]} as const;
const S_epic_add_root_folder_to_sequence = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"newFolderName":{"type":"string"}},"required":["sequence","newFolderName"]} as const;
const S_epic_add_section = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_add_section__movie_scene_track_extensions = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_add_section_of_class = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"sectionClass":{"type":"object","properties":{"refPath":{}}}},"required":["track","sectionClass"]} as const;
const S_epic_add_selected_to_anim_layer = {"properties":{"index":{"type":"integer"}},"required":["index"]} as const;
const S_epic_add_selection_to_selection_set = {"properties":{"id":{"type":"string"}},"required":["id"]} as const;
const S_epic_add_socket = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"socket_name":{"type":"string"},"bone_name":{"type":"string"}},"required":["mesh","socket_name","bone_name"]} as const;
const S_epic_add_socket__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"socketName":{"type":"string"},"boneName":{"type":"string"}},"required":["mesh","socketName","boneName"]} as const;
const S_epic_add_spawnable_from_class = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"actor_class_path":{"type":"string"}},"required":["sequence","actor_class_path"]} as const;
const S_epic_add_spawnable_from_class__level_sequence_editor_subsystem = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"classToSpawn":{"type":"object","properties":{"refPath":{}}}},"required":["sequence","classToSpawn"]} as const;
const S_epic_add_spawnable_from_instance = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"obj":{"type":"object","properties":{"refPath":{}}}},"required":["sequence","obj"]} as const;
const S_epic_add_spawnable_from_instance__level_sequence_editor_subsystem = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"objectToSpawn":{"type":"object","properties":{"refPath":{}}}},"required":["sequence","objectToSpawn"]} as const;
const S_epic_add_string_key = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelName":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"string"}},"required":["section","channelName","frame","value"]} as const;
const S_epic_add_track = {"properties":{"inBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"trackType":{"type":"object","properties":{"refPath":{}}}},"required":["inBinding","trackType"]} as const;
const S_epic_add_track__movie_scene_sequence_extensions = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"trackType":{"type":"object","properties":{"refPath":{}}}},"required":["sequence","trackType"]} as const;
const S_epic_add_track_to_binding = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"track_type":{"type":"object","properties":{"refPath":{}}}},"required":["binding","track_type"]} as const;
const S_epic_add_track_to_folder = {"properties":{"folder":{"type":"object","properties":{"refPath":{}}},"track":{"type":"object","properties":{"refPath":{}}}},"required":["folder","track"]} as const;
const S_epic_add_track_to_sequence = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"track_type":{"type":"object","properties":{"refPath":{}}}},"required":["sequence","track_type"]} as const;
const S_epic_add_variable = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"type_path":{"type":"string"},"is_public":{"type":"boolean"},"is_read_only":{"type":"boolean"},"default_value":{"type":"string"}},"required":["control_rig","name","type_path"]} as const;
const S_epic_add_variable__deformer_graph_scripting_subsystem = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"},"dataType":{"type":"string"},"defaultValue":{"type":"string"}},"required":["deformer","variableName","dataType","defaultValue"]} as const;
const S_epic_add_variable_node = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}},"variable_name":{"type":"string"},"is_getter":{"type":"boolean"},"position":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}}},"required":["control_rig","graph","variable_name"]} as const;
const S_epic_adjustment_blend_anim_layer = {"properties":{"index":{"type":"integer"}},"required":["index"]} as const;
const S_epic_are_marked_frames_shown_globally = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_are_motion_trails_visible = {"properties":{}} as const;
const S_epic_assign_physics_asset = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"physics_asset":{"type":"object","properties":{"refPath":{}}}},"required":["mesh","physics_asset"]} as const;
const S_epic_assign_physics_asset__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"physicsAsset":{"type":"object","properties":{"refPath":{}}}},"required":["mesh","physicsAsset"]} as const;
const S_epic_bake_channel_keys = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channel_name":{"type":"string"},"start_frame":{"type":"integer"},"end_frame":{"type":"integer"}},"required":["section","channel_name","start_frame","end_frame"]} as const;
const S_epic_bake_constraint = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"constraint":{"type":"object","properties":{"refPath":{}}},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["world","constraint","frames"]} as const;
const S_epic_bake_constraints_with_settings = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"constraints":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"settings":{"type":"object","properties":{"startFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"endFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"bakingKeySettings":{"type":"string","enum":["KeysOnly","AllFrames"]},"frameIncrement":{"type":"integer"},"bReduceKeys":{"type":"boolean"},"tolerance":{"type":"number"},"bTimeWarp":{"type":"boolean"}}}},"required":["world","constraints","settings"]} as const;
const S_epic_bake_constraints_with_settings_async = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"constraints":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"settings":{"type":"object","properties":{"startFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"endFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"bakingKeySettings":{"type":"string","enum":["KeysOnly","AllFrames"]},"frameIncrement":{"type":"integer"},"bReduceKeys":{"type":"boolean"},"tolerance":{"type":"number"},"bTimeWarp":{"type":"boolean"}}}},"required":["world","constraints","settings"]} as const;
const S_epic_bake_control_rig_space = {"properties":{"inSequence":{"type":"object","properties":{"refPath":{}}},"inControlRig":{"type":"object","properties":{"refPath":{}}},"inControlNames":{"type":"array","items":{"type":"string"}},"inSettings":{"type":"object","properties":{"targetSpace":{"type":"object","properties":{"type":{"type":"string","enum":["None","Bone","Null","Space","Control","Curve","Reference","Connector","Socket","All"]},"name":{"type":"string"}}},"settings":{"type":"object","properties":{"startFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"endFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"bakingKeySettings":{"type":"string","enum":["KeysOnly","AllFrames"]},"frameIncrement":{"type":"integer"},"bReduceKeys":{"type":"boolean"},"tolerance":{"type":"number"},"bTimeWarp":{"type":"boolean"}}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["inSequence","inControlRig","inControlNames","inSettings"]} as const;
const S_epic_bake_control_rig_space_async = {"properties":{"inSequence":{"type":"object","properties":{"refPath":{}}},"inControlRig":{"type":"object","properties":{"refPath":{}}},"inControlNames":{"type":"array","items":{"type":"string"}},"inSettings":{"type":"object","properties":{"targetSpace":{"type":"object","properties":{"type":{"type":"string","enum":["None","Bone","Null","Space","Control","Curve","Reference","Connector","Socket","All"]},"name":{"type":"string"}}},"settings":{"type":"object","properties":{"startFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"endFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"bakingKeySettings":{"type":"string","enum":["KeysOnly","AllFrames"]},"frameIncrement":{"type":"integer"},"bReduceKeys":{"type":"boolean"},"tolerance":{"type":"number"},"bTimeWarp":{"type":"boolean"}}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["inSequence","inControlRig","inControlNames","inSettings"]} as const;
const S_epic_bake_keys = {"properties":{"bakeInterval":{"type":"object","properties":{"value":{"type":"integer"}}},"movieSceneSection":{"type":"object","properties":{"refPath":{}}}},"required":["bakeInterval","movieSceneSection"]} as const;
const S_epic_bake_space = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_names":{"type":"array","items":{"type":"string"}},"start_frame":{"type":"integer"},"end_frame":{"type":"integer"},"reduce_keys":{"type":"boolean"},"tolerance":{"type":"number"}},"required":["sequence","control_rig_asset_path","control_names","start_frame","end_frame"]} as const;
const S_epic_bake_time_warp = {"properties":{"inSection":{"type":"object","properties":{"refPath":{}}}},"required":["inSection"]} as const;
const S_epic_bake_to_control_rig = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"control_rig_asset_path":{"type":"string"},"reduce_keys":{"type":"boolean"},"tolerance":{"type":"number"},"reset_controls":{"type":"boolean"}},"required":["sequence","binding","control_rig_asset_path"]} as const;
const S_epic_bake_to_control_rig_from_reference = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"levelSequence":{"type":"object","properties":{"refPath":{}}},"inRef":{"type":"object","properties":{"blueprintRigClass":{"type":"object","properties":{"refPath":{}}},"controlRigAsset":{"type":"object","properties":{"refPath":{}}}}},"exportOptions":{"type":"object","properties":{"refPath":{}}},"bReduceKeys":{"type":"boolean"},"tolerance":{"type":"number"},"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"bResetControls":{"type":"boolean"}},"required":["world","levelSequence","inRef","exportOptions","bReduceKeys","tolerance","binding"]} as const;
const S_epic_bake_to_control_rig_from_reference_async = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"levelSequence":{"type":"object","properties":{"refPath":{}}},"inRef":{"type":"object","properties":{"blueprintRigClass":{"type":"object","properties":{"refPath":{}}},"controlRigAsset":{"type":"object","properties":{"refPath":{}}}}},"exportOptions":{"type":"object","properties":{"refPath":{}}},"bReduceKeys":{"type":"boolean"},"tolerance":{"type":"number"},"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"bResetControls":{"type":"boolean"}},"required":["world","levelSequence","inRef","exportOptions","bReduceKeys","tolerance","binding"]} as const;
const S_epic_bake_transform = {"properties":{"bindings":{"type":"array","items":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}}},"required":["bindings"]} as const;
const S_epic_bake_transform_with_settings = {"properties":{"objectBindings":{"type":"array","items":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"inSettings":{"type":"object","properties":{"startFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"endFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"bakingKeySettings":{"type":"string","enum":["KeysOnly","AllFrames"]},"frameIncrement":{"type":"integer"},"bReduceKeys":{"type":"boolean"},"tolerance":{"type":"number"},"bTimeWarp":{"type":"boolean"}}},"params":{"type":"object","properties":{"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}}}},"required":["objectBindings","inSettings"]} as const;
const S_epic_batch_get_control_transforms = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlNames":{"type":"array","items":{"type":"string"}},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"space":{"type":"string","enum":["Local","Global"]},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlNames","frames","space"]} as const;
const S_epic_batch_get_control_transforms_async = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlNames":{"type":"array","items":{"type":"string"}},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"space":{"type":"string","enum":["Local","Global"]},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlNames","frames","space"]} as const;
const S_epic_batch_set_control_transforms = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controls":{"type":"array","items":{"type":"object","properties":{"controlName":{"type":"string"},"transforms":{"type":"array","items":{"type":"object","properties":{"translation":{"type":"object"},"rotation":{"type":"object"},"scale3D":{"type":"object"}}}}}}},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"space":{"type":"string","enum":["Local","Global"]},"optionalSectionToSet":{"type":"object","properties":{"refPath":{}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controls","frames","space"]} as const;
const S_epic_blend_control_rig_pose_asset = {"properties":{"poseAsset":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"blendValue":{"type":"number"},"bKey":{"type":"boolean"},"bMirror":{"type":"boolean"},"bAdditive":{"type":"boolean"}},"required":["poseAsset","controlRig","blendValue"]} as const;
const S_epic_blend_values_on_selected = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"operation":{"type":"string"},"blend_value":{"type":"number"}},"required":["sequence","operation","blend_value"]} as const;
const S_epic_blend_values_on_selected__control_rig_sequencer_editor_library = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"blendOperation":{"type":"string","enum":["Tween","BlendToNeighbor","PushPull","BlendRelative","BlendToEase","SmoothRough","ScaleFromNeighborLeft","ScaleFromNeighborRight","EasyEase","BlendToTrajectory","TimeOffset","BlendToNoise","BlendToWave","BlendToBuffer","BlendToMirror"]},"blendValue":{"type":"number"}},"required":["levelSequence","blendOperation","blendValue"]} as const;
const S_epic_change_actor_template_class = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"actor_class":{"type":"string"}},"required":["binding","actor_class"]} as const;
const S_epic_change_actor_template_class__1 = {"properties":{"objectBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"actorClass":{"type":"object","properties":{"refPath":{}}}},"required":["objectBinding","actorClass"]} as const;
const S_epic_change_variable_type = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"new_type":{"type":"string"}},"required":["control_rig","name","new_type"]} as const;
const S_epic_clear_control_rig_rotation_order_override = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"}},"required":["levelSequence","controlRig","controlName"]} as const;
const S_epic_clear_marked_frames_shown_globally = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_clear_motion_trail_offset = {"properties":{"trailId":{"type":"string"}},"required":["trailId"]} as const;
const S_epic_clear_motion_trail_space = {"properties":{"trailId":{"type":"string"}},"required":["trailId"]} as const;
const S_epic_clear_section_condition = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_clear_section_condition__1 = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_clear_selection = {"properties":{}} as const;
const S_epic_clear_selection__control_rig_sequencer_editor_library = {"properties":{}} as const;
const S_epic_clear_selection_range = {"properties":{}} as const;
const S_epic_clear_skeletal_animation_match = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_clear_track_condition = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_clear_track_condition__1 = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_clear_track_row_condition = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"row_index":{"type":"integer"}},"required":["track","row_index"]} as const;
const S_epic_clear_track_row_condition__1 = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"rowIndex":{"type":"integer"}},"required":["track","rowIndex"]} as const;
const S_epic_close_cinematic_viewport = {"properties":{}} as const;
const S_epic_close_curve_editor = {"properties":{}} as const;
const S_epic_close_curve_editor__level_sequence_editor_subsystem = {"properties":{}} as const;
const S_epic_close_level_sequence_async = {"properties":{}} as const;
const S_epic_close_pose_library_tab = {"properties":{}} as const;
const S_epic_close_sequence = {"properties":{}} as const;
const S_epic_collapse_all_curve_editor_nodes = {"properties":{}} as const;
const S_epic_collapse_anim_layers = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"reduce_keys":{"type":"boolean"},"tolerance":{"type":"number"}},"required":["sequence","control_rig_asset_path"]} as const;
const S_epic_collapse_control_rig_anim_layers = {"properties":{"inSequence":{"type":"object","properties":{"refPath":{}}},"inTrack":{"type":"object","properties":{"refPath":{}}},"bKeyReduce":{"type":"boolean"},"tolerance":{"type":"number"}},"required":["inSequence","inTrack"]} as const;
const S_epic_collapse_control_rig_anim_layers_with_settings = {"properties":{"inSequence":{"type":"object","properties":{"refPath":{}}},"inTrack":{"type":"object","properties":{"refPath":{}}},"inSettings":{"type":"object","properties":{"startFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"endFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"bakingKeySettings":{"type":"string","enum":["KeysOnly","AllFrames"]},"frameIncrement":{"type":"integer"},"bReduceKeys":{"type":"boolean"},"tolerance":{"type":"number"},"bTimeWarp":{"type":"boolean"}}}},"required":["inSequence","inTrack","inSettings"]} as const;
const S_epic_collapse_control_rig_anim_layers_with_settings_async = {"properties":{"inSequence":{"type":"object","properties":{"refPath":{}}},"inTrack":{"type":"object","properties":{"refPath":{}}},"inSettings":{"type":"object","properties":{"startFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"endFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"bakingKeySettings":{"type":"string","enum":["KeysOnly","AllFrames"]},"frameIncrement":{"type":"integer"},"bReduceKeys":{"type":"boolean"},"tolerance":{"type":"number"},"bTimeWarp":{"type":"boolean"}}}},"required":["inSequence","inTrack","inSettings"]} as const;
const S_epic_compensate = {"properties":{"inConstraint":{"type":"object","properties":{"refPath":{}}},"inTime":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["inConstraint","inTime"]} as const;
const S_epic_compensate_all = {"properties":{"inConstraint":{"type":"object","properties":{"refPath":{}}}},"required":["inConstraint"]} as const;
const S_epic_compile = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}}},"required":["deformer"]} as const;
const S_epic_connect_pins = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}},"source_pin":{"type":"object","properties":{"refPath":{}}},"target_pin":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig","graph","source_pin","target_pin"]} as const;
const S_epic_control_rig_copy_vector_parameter_curves_to_transform = {"properties":{"inTrack":{"type":"object","properties":{"refPath":{}}},"inControlName":{"type":"string"},"inType":{"type":"string","enum":["Bool","Float","Integer","Vector2D","Position","Scale","Rotator","EulerTransform","ScaleFloat"]}},"required":["inTrack","inControlName","inType"]} as const;
const S_epic_convert_sub_graph_to_function = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"functionName":{"type":"string"},"category":{"type":"string"},"bIsPublic":{"type":"boolean"}},"required":["deformer","nodePath","functionName","category","bIsPublic"]} as const;
const S_epic_convert_to_custom_binding = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"binding_type_class":{"type":"string"}},"required":["binding","binding_type_class"]} as const;
const S_epic_convert_to_custom_binding__1 = {"properties":{"objectBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"bindingType":{"type":"object","properties":{"refPath":{}}}},"required":["objectBinding","bindingType"]} as const;
const S_epic_convert_to_possessable = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["binding"]} as const;
const S_epic_convert_to_possessable__1 = {"properties":{"objectBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["objectBinding"]} as const;
const S_epic_convert_to_spawnable = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["binding"]} as const;
const S_epic_convert_to_spawnable__1 = {"properties":{"objectBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["objectBinding"]} as const;
const S_epic_copy_bindings = {"properties":{"bindings":{"type":"array","items":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}}},"required":["bindings"]} as const;
const S_epic_copy_bindings_to_text = {"properties":{"bindings":{"type":"array","items":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}}},"required":["bindings"]} as const;
const S_epic_copy_folders = {"properties":{"folders":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["folders"]} as const;
const S_epic_copy_folders_to_text = {"properties":{"folders":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["folders"]} as const;
const S_epic_copy_sections = {"properties":{"sections":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["sections"]} as const;
const S_epic_copy_sections_to_text = {"properties":{"sections":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["sections"]} as const;
const S_epic_copy_tracks = {"properties":{"tracks":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["tracks"]} as const;
const S_epic_copy_tracks_to_text = {"properties":{"tracks":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["tracks"]} as const;
const S_epic_create = {"properties":{"path":{"type":"string"}},"required":["path"]} as const;
const S_epic_create_anim_layers_from_selected_tracks = {"properties":{"bAddToExistingLayers":{"type":"boolean"}}} as const;
const S_epic_create_camera = {"properties":{"spawnable":{"type":"boolean"}}} as const;
const S_epic_create_camera_binding = {"properties":{"bSpawnable":{"type":"boolean"}},"required":["bSpawnable"]} as const;
const S_epic_create_condition_endpoint = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"conditionHost":{"type":"object","properties":{"refPath":{}}},"functionName":{"type":"string"},"rowIndex":{"type":"integer"}},"required":["sequence","conditionHost","functionName"]} as const;
const S_epic_create_control_rig_pose_asset = {"properties":{"controlRig":{"type":"object","properties":{"refPath":{}}},"packagePath":{"type":"string"},"assetName":{"type":"string"},"bUseAllControls":{"type":"boolean"}},"required":["controlRig","packagePath","assetName"]} as const;
const S_epic_create_deformer = {"properties":{"assetPath":{"type":"string"}},"required":["assetPath"]} as const;
const S_epic_create_dynamic_binding_resolver = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"functionName":{"type":"string"}},"required":["sequence","binding","functionName"]} as const;
const S_epic_create_level_sequence = {"properties":{"package_path":{"type":"string"},"asset_name":{"type":"string"}},"required":["package_path","asset_name"]} as const;
const S_epic_create_level_sequence__level_sequence_editor_blueprint_library = {"properties":{"packagePath":{"type":"string"},"assetName":{"type":"string"}},"required":["packagePath","assetName"]} as const;
const S_epic_create_mirrored_selection_set = {"properties":{"id":{"type":"string"}},"required":["id"]} as const;
const S_epic_create_node = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}},"node_type":{"type":"string"},"position":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}},"node_name":{"type":"string"}},"required":["control_rig","graph","node_type"]} as const;
const S_epic_create_node_group = {"properties":{"name":{"type":"string"},"nodes":{"type":"array","items":{"type":"object","properties":{"sessionId":{"type":"string"},"rootSequencePath":{"type":"string"},"focusedSequencePath":{"type":"string"},"focusedTemplateId":{"type":"integer"},"nodePath":{"type":"string"},"nodeType":{"type":"string"},"label":{"type":"string"}}}}},"required":["name","nodes"]} as const;
const S_epic_create_selection_set_from_selection = {"properties":{}} as const;
const S_epic_curve_editor_empty_selection = {"properties":{}} as const;
const S_epic_curve_editor_select_keys = {"properties":{"channel":{"type":"object","properties":{"channelName":{"type":"string"},"section":{"type":"object","properties":{"refPath":{}}}}},"indices":{"type":"array","items":{"type":"integer"}}},"required":["channel","indices"]} as const;
const S_epic_delete_all_marked_frames = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_delete_all_selection_sets = {"properties":{}} as const;
const S_epic_delete_anim_layer = {"properties":{"index":{"type":"integer"}},"required":["index"]} as const;
const S_epic_delete_anim_layer__control_rig_sequencer_editor_library = {"properties":{"index":{"type":"integer"}},"required":["index"]} as const;
const S_epic_delete_constraint_key = {"properties":{"constraint":{"type":"object","properties":{"refPath":{}}},"constraintSection":{"type":"object","properties":{"refPath":{}}},"inTime":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["constraint","constraintSection","inTime"]} as const;
const S_epic_delete_control_rig_control_keys = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlNames":{"type":"array","items":{"type":"string"}}},"required":["levelSequence","controlRig","controlNames"]} as const;
const S_epic_delete_control_rig_space = {"properties":{"inSequence":{"type":"object","properties":{"refPath":{}}},"inControlRig":{"type":"object","properties":{"refPath":{}}},"inControlName":{"type":"string"},"inTime":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["inSequence","inControlRig","inControlName","inTime"]} as const;
const S_epic_delete_linked_anim_sequence = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["binding"]} as const;
const S_epic_delete_linked_anim_track = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["binding"]} as const;
const S_epic_delete_marked_frame = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"index":{"type":"integer"}},"required":["sequence","index"]} as const;
const S_epic_delete_marked_frame__movie_scene_sequence_extensions = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"deleteIndex":{"type":"integer"}},"required":["sequence","deleteIndex"]} as const;
const S_epic_delete_marked_frames = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_delete_node = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}},"node":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig","graph","node"]} as const;
const S_epic_delete_node_group = {"properties":{"group":{"type":"object","properties":{"refPath":{}}}},"required":["group"]} as const;
const S_epic_delete_selection_set = {"properties":{"id":{"type":"string"}},"required":["id"]} as const;
const S_epic_delete_space = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_delete_time_warp = {"properties":{"inSection":{"type":"object","properties":{"refPath":{}}}},"required":["inSection"]} as const;
const S_epic_disconnect_pins = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}},"source_pin":{"type":"object","properties":{"refPath":{}}},"target_pin":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig","graph","source_pin","target_pin"]} as const;
const S_epic_duplicate_anim_layer = {"properties":{"index":{"type":"integer"}},"required":["index"]} as const;
const S_epic_duplicate_anim_layer__control_rig_sequencer_editor_library = {"properties":{"index":{"type":"integer"}},"required":["index"]} as const;
const S_epic_empty_curve_editor_selection = {"properties":{}} as const;
const S_epic_empty_selection = {"properties":{}} as const;
const S_epic_empty_selection__level_sequence_editor_blueprint_library = {"properties":{}} as const;
const S_epic_evaluate_channel_keys = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelName":{"type":"string"},"startFrame":{"type":"integer"},"endFrame":{"type":"integer"}},"required":["section","channelName","startFrame","endFrame"]} as const;
const S_epic_expand_all_curve_editor_nodes = {"properties":{}} as const;
const S_epic_expand_all_outliner_nodes = {"properties":{"bExpanded":{"type":"boolean"}}} as const;
const S_epic_export_anim_sequence = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"sequence":{"type":"object","properties":{"refPath":{}}},"anim_sequence":{"type":"object","properties":{"refPath":{}}},"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"create_link":{"type":"boolean"}},"required":["world","sequence","anim_sequence","binding"]} as const;
const S_epic_export_anim_sequence__sequencer_tools_function_library = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"sequence":{"type":"object","properties":{"refPath":{}}},"animSequence":{"type":"object","properties":{"refPath":{}}},"exportOptions":{"type":"object","properties":{"refPath":{}}},"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"bCreateLink":{"type":"boolean"}},"required":["world","sequence","animSequence","exportOptions","binding","bCreateLink"]} as const;
const S_epic_export_anim_sequence_from_sequencer = {"properties":{"animSequence":{"type":"object","properties":{"refPath":{}}},"exportOption":{"type":"object","properties":{"refPath":{}}},"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"bCreateLink":{"type":"boolean"}},"required":["animSequence","exportOption","binding","bCreateLink"]} as const;
const S_epic_export_anim_sequence_from_sequencer_async = {"properties":{"animSequence":{"type":"object","properties":{"refPath":{}}},"exportOption":{"type":"object","properties":{"refPath":{}}},"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"bCreateLink":{"type":"boolean"}},"required":["animSequence","exportOption","binding","bCreateLink"]} as const;
const S_epic_export_fbx = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"sequence":{"type":"object","properties":{"refPath":{}}},"bindings":{"type":"array","items":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"fbx_file_path":{"type":"string"},"override_options":{"type":"object","properties":{"refPath":{}}}},"required":["world","sequence","bindings","fbx_file_path"]} as const;
const S_epic_export_fbx_from_rig = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"export_file_path":{"type":"string"},"ascii":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","export_file_path"]} as const;
const S_epic_export_fbxfrom_control_rig_section = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"section":{"type":"object","properties":{"refPath":{}}},"exportFBXControlRigSettings":{"type":"object","properties":{"refPath":{}}}},"required":["sequence","section","exportFBXControlRigSettings"]} as const;
const S_epic_export_fbxfrom_control_rig_section_async = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"section":{"type":"object","properties":{"refPath":{}}},"exportFBXControlRigSettings":{"type":"object","properties":{"refPath":{}}}},"required":["sequence","section","exportFBXControlRigSettings"]} as const;
const S_epic_export_level_sequence_edl = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"saveFilename":{"type":"string"},"handleFrames":{"type":"integer"},"movieExtension":{"type":"string"}},"required":["sequence","saveFilename"]} as const;
const S_epic_export_level_sequence_fbx = {"properties":{"inParams":{"type":"object","properties":{"world":{"type":"object","properties":{"refPath":{}}},"sequence":{"type":"object","properties":{"refPath":{}}},"rootSequence":{"type":"object","properties":{"refPath":{}}},"bindings":{"type":"array","items":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"tracks":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"overrideOptions":{"type":"object","properties":{"refPath":{}}},"fBXFileName":{"type":"string"}}}},"required":["inParams"]} as const;
const S_epic_export_level_sequence_fcpxml = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"saveFilename":{"type":"string"},"filenameFormat":{"type":"string"},"resX":{"type":"integer"},"resY":{"type":"integer"},"handleFrames":{"type":"integer"},"movieExtension":{"type":"string"}},"required":["sequence","saveFilename"]} as const;
const S_epic_export_selection_sets_to_json = {"properties":{}} as const;
const S_epic_extract_anim_layer = {"properties":{"index":{"type":"integer"}},"required":["index"]} as const;
const S_epic_find_binding_by_name = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["sequence","name"]} as const;
const S_epic_find_binding_by_name__movie_scene_sequence_extensions = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["sequence","name"]} as const;
const S_epic_find_binding_by_tag = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"tag_name":{"type":"string"}},"required":["sequence","tag_name"]} as const;
const S_epic_find_binding_by_tag__movie_scene_sequence_extensions = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"tagName":{"type":"string"}},"required":["sequence","tagName"]} as const;
const S_epic_find_bindings_by_tag = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"tag_name":{"type":"string"}},"required":["sequence","tag_name"]} as const;
const S_epic_find_bindings_by_tag__movie_scene_sequence_extensions = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"tagName":{"type":"string"}},"required":["sequence","tagName"]} as const;
const S_epic_find_control_rig_tracks = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"inRef":{"type":"object","properties":{"blueprintRigClass":{"type":"object","properties":{"refPath":{}}},"controlRigAsset":{"type":"object","properties":{"refPath":{}}}}}},"required":["levelSequence","inRef"]} as const;
const S_epic_find_marked_frame_by_label = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"label":{"type":"string"}},"required":["sequence","label"]} as const;
const S_epic_find_marked_frame_by_label__movie_scene_sequence_extensions = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"inLabel":{"type":"string"}},"required":["sequence","inLabel"]} as const;
const S_epic_find_or_create_control_rig_component_track = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"levelSequence":{"type":"object","properties":{"refPath":{}}},"inBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["world","levelSequence","inBinding"]} as const;
const S_epic_find_or_create_control_rig_track_from_reference = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"levelSequence":{"type":"object","properties":{"refPath":{}}},"inRef":{"type":"object","properties":{"blueprintRigClass":{"type":"object","properties":{"refPath":{}}},"controlRigAsset":{"type":"object","properties":{"refPath":{}}}}},"inBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"bIsLayeredControlRig":{"type":"boolean"}},"required":["world","levelSequence","inRef","inBinding"]} as const;
const S_epic_find_or_create_track = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"control_rig_asset_path":{"type":"string"},"is_layered":{"type":"boolean"}},"required":["sequence","binding","control_rig_asset_path"]} as const;
const S_epic_find_tracks_by_type = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"track_type":{"type":"object","properties":{"refPath":{}}}},"required":["binding","track_type"]} as const;
const S_epic_find_tracks_by_type__movie_scene_binding_extensions = {"properties":{"inBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"trackType":{"type":"object","properties":{"refPath":{}}}},"required":["inBinding","trackType"]} as const;
const S_epic_fix_actor_references = {"properties":{}} as const;
const S_epic_fix_actor_references__level_sequence_editor_subsystem = {"properties":{}} as const;
const S_epic_focus_level_sequence_async = {"properties":{"subSection":{"type":"object","properties":{"refPath":{}}}},"required":["subSection"]} as const;
const S_epic_focus_parent_sequence = {"properties":{}} as const;
const S_epic_focus_parent_sequence_async = {"properties":{}} as const;
const S_epic_focus_sub_sequence = {"properties":{"sub_section":{"type":"object","properties":{"refPath":{}}}},"required":["sub_section"]} as const;
const S_epic_force_evaluate = {"properties":{}} as const;
const S_epic_force_update_async = {"properties":{}} as const;
const S_epic_frame_all_curves = {"properties":{}} as const;
const S_epic_frame_curve_editor_selection = {"properties":{}} as const;
const S_epic_frame_selection = {"properties":{}} as const;
const S_epic_frame_selection__control_rig_sequencer_editor_library = {"properties":{}} as const;
const S_epic_frame_time_posing_range_on_selected_keys = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}}},"required":["levelSequence"]} as const;
const S_epic_get_active_cinematic_viewport = {"properties":{}} as const;
const S_epic_get_actor_transform_at_frame = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"actor_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","actor_name","frame"]} as const;
const S_epic_get_actor_world_transform = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"actor":{"type":"object","properties":{"refPath":{}}},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","actor","frame"]} as const;
const S_epic_get_actor_world_transforms = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"actor":{"type":"object","properties":{"refPath":{}}},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","actor","frames"]} as const;
const S_epic_get_actor_world_transforms_async = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"actor":{"type":"object","properties":{"refPath":{}}},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","actor","frames"]} as const;
const S_epic_get_all_binding_tags = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_all_binding_tags__movie_scene_binding_tag_extensions = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_all_bones = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig"]} as const;
const S_epic_get_all_controls = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig"]} as const;
const S_epic_get_all_nulls = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig"]} as const;
const S_epic_get_anim_layer_index = {"properties":{"animLayer":{"type":"object","properties":{"refPath":{}}}},"required":["animLayer"]} as const;
const S_epic_get_anim_layer_properties = {"properties":{"index":{"type":"integer"}},"required":["index"]} as const;
const S_epic_get_anim_layers = {"properties":{}} as const;
const S_epic_get_anim_layers__control_rig_sequencer_editor_library = {"properties":{"bAddIfDoesNotExist":{"type":"boolean"}}} as const;
const S_epic_get_anim_mode_gizmo_scale = {"properties":{}} as const;
const S_epic_get_anim_mode_hide_manips = {"properties":{}} as const;
const S_epic_get_anim_mode_hierarchy = {"properties":{}} as const;
const S_epic_get_anim_mode_local_spaces = {"properties":{}} as const;
const S_epic_get_anim_mode_nulls = {"properties":{}} as const;
const S_epic_get_anim_mode_only_rig_sel = {"properties":{}} as const;
const S_epic_get_anim_sequence_link_from_level_sequence = {"properties":{"inLevelSequence":{"type":"object","properties":{"refPath":{}}}},"required":["inLevelSequence"]} as const;
const S_epic_get_animation_mode_settings = {"properties":{}} as const;
const S_epic_get_backward_solve_graph = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig"]} as const;
const S_epic_get_binding_id = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["sequence","binding"]} as const;
const S_epic_get_binding_id__movie_scene_sequence_extensions = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"inBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["sequence","inBinding"]} as const;
const S_epic_get_binding_name = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["binding"]} as const;
const S_epic_get_binding_tags = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["binding"]} as const;
const S_epic_get_binding_tags__movie_scene_binding_tag_extensions = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["binding"]} as const;
const S_epic_get_bindings = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_bindings__movie_scene_sequence_extensions = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_blend_type = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_blender_system = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_get_bone_children = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"bone_name":{"type":"string"}},"required":["mesh","bone_name"]} as const;
const S_epic_get_bone_children__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"}},"required":["mesh","boneName"]} as const;
const S_epic_get_bone_names = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_bone_names__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_bone_parent = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"bone_name":{"type":"string"}},"required":["mesh","bone_name"]} as const;
const S_epic_get_bone_parent__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"boneName":{"type":"string"}},"required":["mesh","boneName"]} as const;
const S_epic_get_bool = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_get_bound_actor_class = {"properties":{"templateSequence":{"type":"object","properties":{"refPath":{}}}},"required":["templateSequence"]} as const;
const S_epic_get_bound_objects = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["binding"]} as const;
const S_epic_get_bound_objects_for_binding = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["binding"]} as const;
const S_epic_get_bounds = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_bounds__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_camera_cut_auto_arrange_sections = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_get_camera_cut_can_blend = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_get_channel_keys = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelName":{"type":"string"}},"required":["section","channelName"]} as const;
const S_epic_get_channel_keys_by_index = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelName":{"type":"string"},"indices":{"type":"array","items":{"type":"integer"}}},"required":["section","channelName","indices"]} as const;
const S_epic_get_channel_names = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_channel_names__movie_scene_section_extensions = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_channel_override_class = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelName":{"type":"string"}},"required":["section","channelName"]} as const;
const S_epic_get_channel_override_classes = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelName":{"type":"string"}},"required":["section","channelName"]} as const;
const S_epic_get_child_folders = {"properties":{"folder":{"type":"object","properties":{"refPath":{}}}},"required":["folder"]} as const;
const S_epic_get_child_object_bindings = {"properties":{"folder":{"type":"object","properties":{"refPath":{}}}},"required":["folder"]} as const;
const S_epic_get_child_possessables = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["binding"]} as const;
const S_epic_get_child_possessables__movie_scene_binding_extensions = {"properties":{"inBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["inBinding"]} as const;
const S_epic_get_child_tracks = {"properties":{"folder":{"type":"object","properties":{"refPath":{}}}},"required":["folder"]} as const;
const S_epic_get_children = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"item":{"type":"object","properties":{"type":{"type":"string","enum":["None","Bone","Null","Space","Control","Curve","Reference","Connector","Socket","All"]},"name":{"type":"string"}}},"recursive":{"type":"boolean"}},"required":["control_rig","item"]} as const;
const S_epic_get_clock_source = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_clock_source__movie_scene_sequence_extensions = {"properties":{"inSequence":{"type":"object","properties":{"refPath":{}}}},"required":["inSequence"]} as const;
const S_epic_get_completion_mode = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_connected_pins = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"pin":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig","pin"]} as const;
const S_epic_get_constraint_keys = {"properties":{"inConstraint":{"type":"object","properties":{"refPath":{}}},"constraintSection":{"type":"object","properties":{"refPath":{}}},"outBools":{"type":"array","items":{"type":"boolean"}},"outFrames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["inConstraint","constraintSection","outBools","outFrames"]} as const;
const S_epic_get_constraints_for_handle = {"properties":{"inWorld":{"type":"object","properties":{"refPath":{}}},"inChild":{"type":"object","properties":{"refPath":{}}}},"required":["inWorld","inChild"]} as const;
const S_epic_get_constraints_for_object = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"object":{"type":"object","properties":{"refPath":{}}},"attachment":{"type":"string"}},"required":["world","object","attachment"]} as const;
const S_epic_get_control_rig_available_spaces = {"properties":{"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"}},"required":["controlRig","controlName"]} as const;
const S_epic_get_control_rig_controls = {"properties":{"controlRig":{"type":"object","properties":{"refPath":{}}}},"required":["controlRig"]} as const;
const S_epic_get_control_rig_controls_visible = {"properties":{"controlRig":{"type":"object","properties":{"refPath":{}}},"moduleName":{"type":"string"}},"required":["controlRig"]} as const;
const S_epic_get_control_rig_overrides = {"properties":{"controlRig":{"type":"object","properties":{"refPath":{}}}},"required":["controlRig"]} as const;
const S_epic_get_control_rig_pose_asset_controls = {"properties":{"poseAsset":{"type":"object","properties":{"refPath":{}}}},"required":["poseAsset"]} as const;
const S_epic_get_control_rig_priority_order = {"properties":{"inTrack":{"type":"object","properties":{"refPath":{}}}},"required":["inTrack"]} as const;
const S_epic_get_control_rig_rotation_order = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"}},"required":["levelSequence","controlRig","controlName"]} as const;
const S_epic_get_control_rig_section_transform_mask = {"properties":{"inSection":{"type":"object","properties":{"refPath":{}}}},"required":["inSection"]} as const;
const S_epic_get_control_rig_space_keys = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName"]} as const;
const S_epic_get_control_rig_world_transform = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frame"]} as const;
const S_epic_get_control_rig_world_transforms = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames"]} as const;
const S_epic_get_control_rig_world_transforms_async = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames"]} as const;
const S_epic_get_control_rigs = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_control_rigs__control_rig_sequencer_editor_library = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}}},"required":["levelSequence"]} as const;
const S_epic_get_control_values = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlNames":{"type":"array","items":{"type":"string"}},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlNames","frames"]} as const;
const S_epic_get_control_values_async = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlNames":{"type":"array","items":{"type":"string"}},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlNames","frames"]} as const;
const S_epic_get_controls_info = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"}},"required":["sequence","control_rig_asset_path"]} as const;
const S_epic_get_controls_mask = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"control_name":{"type":"string"}},"required":["section","control_name"]} as const;
const S_epic_get_controls_mask__control_rig_sequencer_editor_library = {"properties":{"inSection":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"}},"required":["inSection","controlName"]} as const;
const S_epic_get_current_level_sequence = {"properties":{}} as const;
const S_epic_get_current_sequence = {"properties":{}} as const;
const S_epic_get_curve_editor_channels_with_selected_keys = {"properties":{}} as const;
const S_epic_get_curve_editor_pinned_channels = {"properties":{}} as const;
const S_epic_get_curve_editor_selected_keys = {"properties":{"channel":{"type":"object","properties":{"channelName":{"type":"string"},"section":{"type":"object","properties":{"refPath":{}}}}}},"required":["channel"]} as const;
const S_epic_get_curve_editor_selected_keys__level_sequence_editor_subsystem = {"properties":{"channel":{"type":"object","properties":{"channelName":{"type":"string"},"section":{"type":"object","properties":{"refPath":{}}}}}},"required":["channel"]} as const;
const S_epic_get_curve_editor_view_mode = {"properties":{}} as const;
const S_epic_get_custom_binding_objects = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["binding"]} as const;
const S_epic_get_custom_binding_objects__1 = {"properties":{"objectBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["objectBinding"]} as const;
const S_epic_get_custom_binding_type = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["binding"]} as const;
const S_epic_get_custom_binding_type__1 = {"properties":{"objectBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["objectBinding"]} as const;
const S_epic_get_custom_bindings_of_type = {"properties":{"binding_type_class":{"type":"string"}},"required":["binding_type_class"]} as const;
const S_epic_get_custom_bindings_of_type__1 = {"properties":{"customBindingType":{"type":"object","properties":{"refPath":{}}}},"required":["customBindingType"]} as const;
const S_epic_get_custom_clock_source = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_custom_time_warp_class = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_deactivated_nodes = {"properties":{}} as const;
const S_epic_get_deactivated_nodes__1 = {"properties":{}} as const;
const S_epic_get_default_parent_key = {"properties":{}} as const;
const S_epic_get_default_value = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channel_name":{"type":"string"}},"required":["section","channel_name"]} as const;
const S_epic_get_deformer_info = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}}},"required":["deformer"]} as const;
const S_epic_get_directly_selected_folders = {"properties":{}} as const;
const S_epic_get_display_name = {"properties":{"inBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["inBinding"]} as const;
const S_epic_get_display_name__movie_scene_track_extensions = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_get_display_rate = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_display_rate__movie_scene_sequence_extensions = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_double_default = {"properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}}},"required":["channel"]} as const;
const S_epic_get_double_key_value = {"properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}},"key":{"type":"object","properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}},"keyHandleId":{"type":"integer"}}}},"required":["channel","key"]} as const;
const S_epic_get_ease_in_duration = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_ease_out_duration = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_elements = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"element_type":{"type":"string","enum":["None","Bone","Null","Space","Control","Curve","Reference","Connector","Socket","All"]}},"required":["control_rig"]} as const;
const S_epic_get_emulated_network_mask = {"properties":{}} as const;
const S_epic_get_enabled_film_overlay_frames = {"properties":{}} as const;
const S_epic_get_end_frame = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_enum_track_enum = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_get_euler_transform = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_get_evaluation_type = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_evaluation_type__movie_scene_sequence_extensions = {"properties":{"inSequence":{"type":"object","properties":{"refPath":{}}}},"required":["inSequence"]} as const;
const S_epic_get_event_graph = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"event_type":{"type":"string","enum":["FORWARD_SOLVE","BACKWARD_SOLVE","INTERACTION"]}},"required":["control_rig","event_type"]} as const;
const S_epic_get_film_overlay = {"properties":{"viewport":{"type":"object","properties":{"slotName":{"type":"string"}}}},"required":["viewport"]} as const;
const S_epic_get_film_overlay_frame_names = {"properties":{}} as const;
const S_epic_get_film_overlay_frame_tint = {"properties":{"frameName":{"type":"string"}},"required":["frameName"]} as const;
const S_epic_get_film_overlay_names = {"properties":{}} as const;
const S_epic_get_film_overlay_tint = {"properties":{"viewport":{"type":"object","properties":{"slotName":{"type":"string"}}}},"required":["viewport"]} as const;
const S_epic_get_fkcontrol_rig_active_controls = {"properties":{"controlRig":{"type":"object","properties":{"refPath":{}}}},"required":["controlRig"]} as const;
const S_epic_get_fkcontrol_rig_apply_mode = {"properties":{"inControlRig":{"type":"object","properties":{"refPath":{}}}},"required":["inControlRig"]} as const;
const S_epic_get_float = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_get_float_default = {"properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}}},"required":["channel"]} as const;
const S_epic_get_float_key_value = {"properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}},"key":{"type":"object","properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}},"keyHandleId":{"type":"integer"}}}},"required":["channel","key"]} as const;
const S_epic_get_focused_level_sequence = {"properties":{}} as const;
const S_epic_get_focused_sequence = {"properties":{}} as const;
const S_epic_get_folder_color = {"properties":{"folder":{"type":"object","properties":{"refPath":{}}}},"required":["folder"]} as const;
const S_epic_get_folder_contents = {"properties":{"folder":{"type":"object","properties":{"refPath":{}}}},"required":["folder"]} as const;
const S_epic_get_folder_name = {"properties":{"folder":{"type":"object","properties":{"refPath":{}}}},"required":["folder"]} as const;
const S_epic_get_forward_solve_graph = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig"]} as const;
const S_epic_get_global_position = {"properties":{"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}}} as const;
const S_epic_get_global_transform = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"item":{"type":"object","properties":{"type":{"type":"string","enum":["None","Bone","Null","Space","Control","Curve","Reference","Connector","Socket","All"]},"name":{"type":"string"}}},"initial":{"type":"boolean"}},"required":["control_rig","item"]} as const;
const S_epic_get_graph = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"graph_name":{"type":"string"}},"required":["control_rig","graph_name"]} as const;
const S_epic_get_graph_info = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"graphPath":{"type":"string"}},"required":["deformer","graphPath"]} as const;
const S_epic_get_hidden_nodes = {"properties":{}} as const;
const S_epic_get_int = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_get_interaction_graph = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig"]} as const;
const S_epic_get_isolated_nodes = {"properties":{}} as const;
const S_epic_get_kernel_info = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"}},"required":["deformer","nodePath"]} as const;
const S_epic_get_key_time = {"properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}},"key":{"type":"object","properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}},"keyHandleId":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["channel","key"]} as const;
const S_epic_get_keys = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channel_name":{"type":"string"}},"required":["section","channel_name"]} as const;
const S_epic_get_keys__movie_scene_section_extensions = {"properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}}},"required":["channel"]} as const;
const S_epic_get_keys_by_index = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channel_name":{"type":"string"},"indices":{"type":"array","items":{"type":"integer"}}},"required":["section","channel_name","indices"]} as const;
const S_epic_get_letterbox_aspect_ratio = {"properties":{}} as const;
const S_epic_get_level_sequence_link_from_anim_sequence = {"properties":{"inAnimSequence":{"type":"object","properties":{"refPath":{}}}},"required":["inAnimSequence"]} as const;
const S_epic_get_linked_anim_sequences = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_linked_anim_sequences__control_rig_sequencer_editor_library = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["binding"]} as const;
const S_epic_get_linked_anim_track = {"properties":{"inBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["inBinding"]} as const;
const S_epic_get_linked_level_sequence = {"properties":{"anim_sequence":{"type":"object","properties":{"refPath":{}}}},"required":["anim_sequence"]} as const;
const S_epic_get_local_control_rig_bool = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frame"]} as const;
const S_epic_get_local_control_rig_bools = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames"]} as const;
const S_epic_get_local_control_rig_euler_transform = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frame"]} as const;
const S_epic_get_local_control_rig_euler_transforms = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames"]} as const;
const S_epic_get_local_control_rig_float = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frame"]} as const;
const S_epic_get_local_control_rig_floats = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames"]} as const;
const S_epic_get_local_control_rig_int = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frame"]} as const;
const S_epic_get_local_control_rig_ints = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames"]} as const;
const S_epic_get_local_control_rig_position = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frame"]} as const;
const S_epic_get_local_control_rig_positions = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames"]} as const;
const S_epic_get_local_control_rig_rotator = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frame"]} as const;
const S_epic_get_local_control_rig_rotators = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames"]} as const;
const S_epic_get_local_control_rig_scale = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frame"]} as const;
const S_epic_get_local_control_rig_scales = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames"]} as const;
const S_epic_get_local_control_rig_transform = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frame"]} as const;
const S_epic_get_local_control_rig_transform_no_scale = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frame"]} as const;
const S_epic_get_local_control_rig_transform_no_scales = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames"]} as const;
const S_epic_get_local_control_rig_transforms = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames"]} as const;
const S_epic_get_local_control_rig_vector2_d = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frame"]} as const;
const S_epic_get_local_control_rig_vector2_ds = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames"]} as const;
const S_epic_get_local_transform = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"item":{"type":"object","properties":{"type":{"type":"string","enum":["None","Bone","Null","Space","Control","Curve","Reference","Connector","Socket","All"]},"name":{"type":"string"}}},"initial":{"type":"boolean"}},"required":["control_rig","item"]} as const;
const S_epic_get_locked_nodes = {"properties":{}} as const;
const S_epic_get_locked_nodes__1 = {"properties":{}} as const;
const S_epic_get_lod_count = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_lod_count__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_loop_info = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"}},"required":["deformer","nodePath"]} as const;
const S_epic_get_loop_mode = {"properties":{}} as const;
const S_epic_get_loop_mode__level_sequence_editor_blueprint_library = {"properties":{}} as const;
const S_epic_get_marked_frames = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_marked_frames_from_sequence = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["sequence"]} as const;
const S_epic_get_material = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"slot_name":{"type":"string"}},"required":["mesh","slot_name"]} as const;
const S_epic_get_material__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"slotName":{"type":"string"}},"required":["mesh","slotName"]} as const;
const S_epic_get_material_slots = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_material_slots__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_morph_target_names = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_morph_target_names__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_motion_trail_settings = {"properties":{}} as const;
const S_epic_get_muted_nodes = {"properties":{}} as const;
const S_epic_get_muted_nodes__1 = {"properties":{}} as const;
const S_epic_get_node_group_name = {"properties":{"group":{"type":"object","properties":{"refPath":{}}}},"required":["group"]} as const;
const S_epic_get_node_group_nodes = {"properties":{"group":{"type":"object","properties":{"refPath":{}}}},"required":["group"]} as const;
const S_epic_get_node_group_paths = {"properties":{"group":{"type":"object","properties":{"refPath":{}}}},"required":["group"]} as const;
const S_epic_get_node_groups = {"properties":{}} as const;
const S_epic_get_node_info = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"}},"required":["deformer","nodePath"]} as const;
const S_epic_get_node_label = {"properties":{"node":{"type":"object","properties":{"type":{"type":"string"}}}},"required":["node"]} as const;
const S_epic_get_node_label__1 = {"properties":{"node":{"type":"object","properties":{"sessionId":{"type":"string"},"rootSequencePath":{"type":"string"},"focusedSequencePath":{"type":"string"},"focusedTemplateId":{"type":"integer"},"nodePath":{"type":"string"},"nodeType":{"type":"string"},"label":{"type":"string"}}}},"required":["node"]} as const;
const S_epic_get_node_position = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"node":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig","node"]} as const;
const S_epic_get_num_channels_used = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_get_num_channels_used__movie_scene_float_vector_track_extensions = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_get_numeric_channels = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_open_cinematic_viewports = {"properties":{}} as const;
const S_epic_get_or_create_director_blueprint = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_or_create_linked_anim_track = {"properties":{"inBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["inBinding"]} as const;
const S_epic_get_origin_override_mask = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_outliner_children = {"properties":{"node":{"type":"object","properties":{"type":{"type":"string"}}},"type_filter":{"type":"string"}},"required":["node"]} as const;
const S_epic_get_outliner_children__1 = {"properties":{"node":{"type":"object","properties":{"sessionId":{"type":"string"},"rootSequencePath":{"type":"string"},"focusedSequencePath":{"type":"string"},"focusedTemplateId":{"type":"integer"},"nodePath":{"type":"string"},"nodeType":{"type":"string"},"label":{"type":"string"}}},"typeFilter":{"type":"string"}},"required":["node"]} as const;
const S_epic_get_outliner_nodes = {"properties":{}} as const;
const S_epic_get_outliner_paths = {"properties":{}} as const;
const S_epic_get_outliner_selection = {"properties":{}} as const;
const S_epic_get_outliner_selection__1 = {"properties":{}} as const;
const S_epic_get_outliner_tree = {"properties":{}} as const;
const S_epic_get_outliner_tree__1 = {"properties":{}} as const;
const S_epic_get_overridden_channel_names = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_parent = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"item":{"type":"object","properties":{"type":{"type":"string","enum":["None","Bone","Null","Space","Control","Curve","Reference","Connector","Socket","All"]},"name":{"type":"string"}}}},"required":["control_rig","item"]} as const;
const S_epic_get_perlin_noise_params = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelName":{"type":"string"}},"required":["section","channelName"]} as const;
const S_epic_get_physics_asset = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_physics_asset__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_pin_value = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"pin":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig","pin"]} as const;
const S_epic_get_pinned_motion_trails = {"properties":{}} as const;
const S_epic_get_pinned_nodes = {"properties":{}} as const;
const S_epic_get_pinned_nodes__1 = {"properties":{}} as const;
const S_epic_get_playback_context = {"properties":{}} as const;
const S_epic_get_playback_context_options = {"properties":{}} as const;
const S_epic_get_playback_end = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_playback_range = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_playback_speed = {"properties":{}} as const;
const S_epic_get_playback_speed__level_sequence_editor_blueprint_library = {"properties":{}} as const;
const S_epic_get_playback_start = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_playhead_frame = {"properties":{}} as const;
const S_epic_get_portable_binding_id = {"properties":{"rootSequence":{"type":"object","properties":{"refPath":{}}},"destinationSequence":{"type":"object","properties":{"refPath":{}}},"inBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["rootSequence","destinationSequence","inBinding"]} as const;
const S_epic_get_position = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_get_post_roll_frames = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_pre_roll_frames = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_preview_language = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_priority_order = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"}},"required":["sequence","control_rig_asset_path"]} as const;
const S_epic_get_root_folders = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_root_folders_in_sequence = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_rotator = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_get_safe_frame_size = {"properties":{"frameName":{"type":"string"}},"required":["frameName"]} as const;
const S_epic_get_scale = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_get_section_blend_type = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_section_completion_mode = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_section_condition = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_section_condition__1 = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_section_count = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"lod_index":{"type":"integer"}},"required":["mesh"]} as const;
const S_epic_get_section_count__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"lodIndex":{"type":"integer"}},"required":["mesh"]} as const;
const S_epic_get_section_ease_in = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_section_ease_out = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_section_group = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"section":{"type":"object","properties":{"refPath":{}}}},"required":["sequence","section"]} as const;
const S_epic_get_section_groups = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_section_play_rate = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_section_post_roll_frames = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_section_pre_roll_frames = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_section_properties = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_section_range = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_section_row_index = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_section_to_key = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_get_section_to_key__movie_scene_track_extensions = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_get_sections = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_get_sections__movie_scene_track_extensions = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_get_sections_for_nodes = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"type":{"type":"string"}}}}},"required":["nodes"]} as const;
const S_epic_get_sections_for_nodes__1 = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"sessionId":{"type":"string"},"rootSequencePath":{"type":"string"},"focusedSequencePath":{"type":"string"},"focusedTemplateId":{"type":"integer"},"nodePath":{"type":"string"},"nodeType":{"type":"string"},"label":{"type":"string"}}}}},"required":["nodes"]} as const;
const S_epic_get_selected_bindings = {"properties":{}} as const;
const S_epic_get_selected_bindings__level_sequence_editor_blueprint_library = {"properties":{}} as const;
const S_epic_get_selected_channels = {"properties":{}} as const;
const S_epic_get_selected_channels__level_sequence_editor_blueprint_library = {"properties":{}} as const;
const S_epic_get_selected_control_rig_controls = {"properties":{"controlRig":{"type":"object","properties":{"refPath":{}}}},"required":["controlRig"]} as const;
const S_epic_get_selected_controls = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"}},"required":["sequence","control_rig_asset_path"]} as const;
const S_epic_get_selected_folders = {"properties":{}} as const;
const S_epic_get_selected_folders__level_sequence_editor_blueprint_library = {"properties":{}} as const;
const S_epic_get_selected_key_channels = {"properties":{}} as const;
const S_epic_get_selected_sections = {"properties":{}} as const;
const S_epic_get_selected_sections__level_sequence_editor_blueprint_library = {"properties":{}} as const;
const S_epic_get_selected_tracks = {"properties":{}} as const;
const S_epic_get_selected_tracks__level_sequence_editor_blueprint_library = {"properties":{}} as const;
const S_epic_get_selection_range = {"properties":{}} as const;
const S_epic_get_selection_range_end = {"properties":{}} as const;
const S_epic_get_selection_range_start = {"properties":{}} as const;
const S_epic_get_selection_set_actors = {"properties":{}} as const;
const S_epic_get_selection_set_infos = {"properties":{}} as const;
const S_epic_get_selection_sets = {"properties":{"bAddIfDoesNotExist":{"type":"boolean"}}} as const;
const S_epic_get_sequence_flags = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_sequence_lock_state = {"properties":{}} as const;
const S_epic_get_show_curve = {"properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}}},"required":["channel"]} as const;
const S_epic_get_skeletal_animation_match_settings = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_skeletal_mesh_component_world_transform = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"skeletalMeshComponent":{"type":"object","properties":{"refPath":{}}},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]},"referenceName":{"type":"string"}},"required":["levelSequence","skeletalMeshComponent","frame"]} as const;
const S_epic_get_skeletal_mesh_component_world_transforms = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"skeletalMeshComponent":{"type":"object","properties":{"refPath":{}}},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]},"referenceName":{"type":"string"}},"required":["levelSequence","skeletalMeshComponent","frames"]} as const;
const S_epic_get_skeletal_mesh_component_world_transforms_async = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"skeletalMeshComponent":{"type":"object","properties":{"refPath":{}}},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]},"referenceName":{"type":"string"}},"required":["levelSequence","skeletalMeshComponent","frames"]} as const;
const S_epic_get_skeleton = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_skeleton__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_socket_bone = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"socket_name":{"type":"string"}},"required":["mesh","socket_name"]} as const;
const S_epic_get_socket_bone__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"socketName":{"type":"string"}},"required":["mesh","socketName"]} as const;
const S_epic_get_socket_names = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_socket_names__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}}},"required":["mesh"]} as const;
const S_epic_get_socket_transform = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"socket_name":{"type":"string"}},"required":["mesh","socket_name"]} as const;
const S_epic_get_socket_transform__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"socketName":{"type":"string"}},"required":["mesh","socketName"]} as const;
const S_epic_get_soloed_nodes = {"properties":{}} as const;
const S_epic_get_soloed_nodes__1 = {"properties":{}} as const;
const S_epic_get_sorting_order = {"properties":{"folder":{"type":"object","properties":{"refPath":{}}}},"required":["folder"]} as const;
const S_epic_get_space_channel = {"properties":{"inSequence":{"type":"object","properties":{"refPath":{}}},"inControlRig":{"type":"object","properties":{"refPath":{}}},"inControlName":{"type":"string"},"bCreateIfNeeded":{"type":"boolean"}},"required":["inSequence","inControlRig","inControlName"]} as const;
const S_epic_get_start_frame = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_sub_sequence_hierarchy = {"properties":{}} as const;
const S_epic_get_sub_sequence_hierarchy__level_sequence_editor_blueprint_library = {"properties":{}} as const;
const S_epic_get_supported_blender_systems = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_get_thumbnail_reference_offset = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_tick_resolution = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_tick_resolution__movie_scene_sequence_extensions = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_time_posing_range = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence"]} as const;
const S_epic_get_time_posing_settings = {"properties":{}} as const;
const S_epic_get_time_posing_weight = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","frame"]} as const;
const S_epic_get_time_warp_classes = {"properties":{}} as const;
const S_epic_get_time_warp_type = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_track_condition = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_get_track_condition__1 = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_get_track_display_name = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_get_track_filter_names = {"properties":{}} as const;
const S_epic_get_track_filter_names__level_sequence_editor_blueprint_library = {"properties":{}} as const;
const S_epic_get_track_filter_text = {"properties":{}} as const;
const S_epic_get_track_row_condition = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"row_index":{"type":"integer"}},"required":["track","row_index"]} as const;
const S_epic_get_track_row_condition__1 = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"rowIndex":{"type":"integer"}},"required":["track","rowIndex"]} as const;
const S_epic_get_tracks = {"properties":{"inBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["inBinding"]} as const;
const S_epic_get_tracks__movie_scene_sequence_extensions = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_tracks_on_binding = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["binding"]} as const;
const S_epic_get_tracks_on_sequence = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_transform = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_get_transform_mask = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_get_validation_rules = {"properties":{}} as const;
const S_epic_get_variable = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["control_rig","name"]} as const;
const S_epic_get_vector2d = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_get_vertex_count = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"lod_index":{"type":"integer"}},"required":["mesh"]} as const;
const S_epic_get_vertex_count__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"lodIndex":{"type":"integer"}},"required":["mesh"]} as const;
const S_epic_get_view_range = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_view_range_end = {"properties":{"inSequence":{"type":"object","properties":{"refPath":{}}}},"required":["inSequence"]} as const;
const S_epic_get_view_range_start = {"properties":{"inSequence":{"type":"object","properties":{"refPath":{}}}},"required":["inSequence"]} as const;
const S_epic_get_visible_control_rigs = {"properties":{}} as const;
const S_epic_get_work_range = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_get_work_range_end = {"properties":{"inSequence":{"type":"object","properties":{"refPath":{}}}},"required":["inSequence"]} as const;
const S_epic_get_work_range_start = {"properties":{"inSequence":{"type":"object","properties":{"refPath":{}}}},"required":["inSequence"]} as const;
const S_epic_get_world_space_reference_key = {"properties":{}} as const;
const S_epic_get_world_transform = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_group_sections = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"sections":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["sequence","sections"]} as const;
const S_epic_has_control_rig_pose_in_clipboard = {"properties":{}} as const;
const S_epic_has_default = {"properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}}},"required":["channel"]} as const;
const S_epic_has_end_frame = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_has_section_end_frame = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_has_section_start_frame = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_has_selection_range = {"properties":{}} as const;
const S_epic_has_start_frame = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_hide_all_controls = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_hide_all_controls__control_rig_sequencer_editor_library = {"properties":{"inSection":{"type":"object","properties":{"refPath":{}}}},"required":["inSection"]} as const;
const S_epic_import_bones_from_asset = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"skeletal_mesh":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig","skeletal_mesh"]} as const;
const S_epic_import_fbx = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"sequence":{"type":"object","properties":{"refPath":{}}},"bindings":{"type":"array","items":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"import_settings":{"type":"object","properties":{"refPath":{}}},"fbx_file_path":{"type":"string"}},"required":["world","sequence","bindings","import_settings","fbx_file_path"]} as const;
const S_epic_import_fbx_to_rig = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"import_file_path":{"type":"string"},"selected_controls":{"type":"array","items":{"type":"string"}}},"required":["sequence","control_rig_asset_path","import_file_path","selected_controls"]} as const;
const S_epic_import_fbxto_control_rig_track = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"inSequence":{"type":"object","properties":{"refPath":{}}},"inTrack":{"type":"object","properties":{"refPath":{}}},"inSection":{"type":"object","properties":{"refPath":{}}},"selectedControlRigNames":{"type":"array","items":{"type":"string"}},"importFBXControlRigSettings":{"type":"object","properties":{"refPath":{}}},"importFilename":{"type":"string"}},"required":["world","inSequence","inTrack","inSection","selectedControlRigNames","importFBXControlRigSettings","importFilename"]} as const;
const S_epic_import_fbxto_control_rig_track_async = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"inSequence":{"type":"object","properties":{"refPath":{}}},"inTrack":{"type":"object","properties":{"refPath":{}}},"inSection":{"type":"object","properties":{"refPath":{}}},"selectedControlRigNames":{"type":"array","items":{"type":"string"}},"importFBXControlRigSettings":{"type":"object","properties":{"refPath":{}}},"importFilename":{"type":"string"}},"required":["world","inSequence","inTrack","inSection","selectedControlRigNames","importFBXControlRigSettings","importFilename"]} as const;
const S_epic_import_file = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"},"source_file":{"type":"string"},"skeleton":{"type":"object","properties":{"refPath":{}}},"import_materials":{"type":"boolean"},"import_textures":{"type":"boolean"},"import_animations":{"type":"boolean"},"create_physics_asset":{"type":"boolean"}},"required":["folder_path","asset_name","source_file"]} as const;
const S_epic_import_file__1 = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"},"sourceFile":{"type":"string"},"skeleton":{"type":"object","properties":{"refPath":{}}},"bImportMaterials":{"type":"boolean"},"bImportTextures":{"type":"boolean"},"bImportAnimations":{"type":"boolean"},"bCreatePhysicsAsset":{"type":"boolean"},"bReplaceExisting":{"type":"boolean"},"bSave":{"type":"boolean"}},"required":["folderPath","assetName","sourceFile"]} as const;
const S_epic_import_level_sequence_edl = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"importFilename":{"type":"string"}},"required":["sequence","importFilename"]} as const;
const S_epic_import_level_sequence_fbx = {"properties":{"inWorld":{"type":"object","properties":{"refPath":{}}},"inSequence":{"type":"object","properties":{"refPath":{}}},"inBindings":{"type":"array","items":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"inImportFBXSettings":{"type":"object","properties":{"refPath":{}}},"inImportFilename":{"type":"string"},"actorContext":{"type":"object","properties":{"refPath":{}}}},"required":["inWorld","inSequence","inBindings","inImportFBXSettings","inImportFilename"]} as const;
const S_epic_import_level_sequence_fcpxml = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"importFilename":{"type":"string"}},"required":["sequence","importFilename"]} as const;
const S_epic_import_selection_sets_from_json = {"properties":{"json":{"type":"string"}},"required":["json"]} as const;
const S_epic_invert_input_pose = {"properties":{"bSelectionOnly":{"type":"boolean"},"bIncludeChannels":{"type":"boolean"}},"required":["bSelectionOnly","bIncludeChannels"]} as const;
const S_epic_is_animation_mode_tool_open = {"properties":{"tool":{"type":"string","enum":["PoseLibrary","AnimLayers","SelectionSets","Tween","Constraints","Snapper","MotionTrails","Details","Outliner"]}},"required":["tool"]} as const;
const S_epic_is_camera_cut_locked = {"properties":{}} as const;
const S_epic_is_camera_cut_locked_to_viewport = {"properties":{}} as const;
const S_epic_is_cinematic_viewport_open = {"properties":{}} as const;
const S_epic_is_curve_editor_open = {"properties":{}} as const;
const S_epic_is_curve_editor_open__level_sequence_editor_subsystem = {"properties":{}} as const;
const S_epic_is_curve_pinned = {"properties":{"channel":{"type":"object","properties":{"channelName":{"type":"string"},"section":{"type":"object","properties":{"refPath":{}}}}}},"required":["channel"]} as const;
const S_epic_is_curve_shown = {"properties":{"channel":{"type":"object","properties":{"channelName":{"type":"string"},"section":{"type":"object","properties":{"refPath":{}}}}}},"required":["channel"]} as const;
const S_epic_is_curve_shown__level_sequence_editor_subsystem = {"properties":{"channel":{"type":"object","properties":{"channelName":{"type":"string"},"section":{"type":"object","properties":{"refPath":{}}}}}},"required":["channel"]} as const;
const S_epic_is_deactivated = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_is_deactivated__movie_scene_track_extensions = {"properties":{"track":{"type":"object","properties":{"refPath":{}}}},"required":["track"]} as const;
const S_epic_is_edit_pivot_tool_active = {"properties":{}} as const;
const S_epic_is_film_overlay_frame_enabled = {"properties":{"frameName":{"type":"string"}},"required":["frameName"]} as const;
const S_epic_is_fk_control_rig = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"}},"required":["sequence","control_rig_asset_path"]} as const;
const S_epic_is_fkcontrol_rig = {"properties":{"inControlRig":{"type":"object","properties":{"refPath":{}}}},"required":["inControlRig"]} as const;
const S_epic_is_layered_control_rig = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"}},"required":["sequence","control_rig_asset_path"]} as const;
const S_epic_is_layered_control_rig__control_rig_sequencer_editor_library = {"properties":{"inControlRig":{"type":"object","properties":{"refPath":{}}}},"required":["inControlRig"]} as const;
const S_epic_is_level_sequence_locked = {"properties":{}} as const;
const S_epic_is_node_expanded = {"properties":{"node":{"type":"object","properties":{"type":{"type":"string"}}}},"required":["node"]} as const;
const S_epic_is_node_expanded__1 = {"properties":{"node":{"type":"object","properties":{"sessionId":{"type":"string"},"rootSequencePath":{"type":"string"},"focusedSequencePath":{"type":"string"},"focusedTemplateId":{"type":"integer"},"nodePath":{"type":"string"},"nodeType":{"type":"string"},"label":{"type":"string"}}}},"required":["node"]} as const;
const S_epic_is_node_expanded_by_path = {"properties":{"path":{"type":"string"}},"required":["path"]} as const;
const S_epic_is_node_group_filter_enabled = {"properties":{"group":{"type":"object","properties":{"refPath":{}}}},"required":["group"]} as const;
const S_epic_is_node_hidden = {"properties":{"node":{"type":"object","properties":{"sessionId":{"type":"string"},"rootSequencePath":{"type":"string"},"focusedSequencePath":{"type":"string"},"focusedTemplateId":{"type":"integer"},"nodePath":{"type":"string"},"nodeType":{"type":"string"},"label":{"type":"string"}}}},"required":["node"]} as const;
const S_epic_is_node_isolated = {"properties":{"node":{"type":"object","properties":{"sessionId":{"type":"string"},"rootSequencePath":{"type":"string"},"focusedSequencePath":{"type":"string"},"focusedTemplateId":{"type":"integer"},"nodePath":{"type":"string"},"nodeType":{"type":"string"},"label":{"type":"string"}}}},"required":["node"]} as const;
const S_epic_is_playback_range_locked = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_is_playback_range_locked__movie_scene_sequence_extensions = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_is_playing = {"properties":{}} as const;
const S_epic_is_playing__level_sequence_editor_blueprint_library = {"properties":{}} as const;
const S_epic_is_row_deactivated = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"rowIndex":{"type":"integer"}},"required":["track","rowIndex"]} as const;
const S_epic_is_sequence_locked = {"properties":{}} as const;
const S_epic_is_time_posing_active = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}}},"required":["levelSequence"]} as const;
const S_epic_is_track_filter_active = {"properties":{"name":{"type":"string"}},"required":["name"]} as const;
const S_epic_is_track_filter_active__level_sequence_editor_blueprint_library = {"properties":{"trackFilterName":{"type":"string"}},"required":["trackFilterName"]} as const;
const S_epic_is_valid = {"properties":{"inBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["inBinding"]} as const;
const S_epic_isolate_linked_anim_track = {"properties":{"inBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"bIsolate":{"type":"boolean"}},"required":["inBinding","bIsolate"]} as const;
const S_epic_isolate_linked_anim_tracks_for_all_bindings = {"properties":{"bIsolate":{"type":"boolean"}},"required":["bIsolate"]} as const;
const S_epic_key_control_rig_section_weight = {"properties":{"inSection":{"type":"object","properties":{"refPath":{}}},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"weight":{"type":"number"},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["inSection","frame","weight"]} as const;
const S_epic_key_control_rig_section_zero_value = {"properties":{"inSection":{"type":"object","properties":{"refPath":{}}},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"bSelectedControlsOnly":{"type":"boolean"},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["inSection","frame"]} as const;
const S_epic_key_controls = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"control_names":{"type":"array","items":{"type":"string"}}},"required":["section","control_names"]} as const;
const S_epic_key_controls__control_rig_sequencer_editor_library = {"properties":{"inSection":{"type":"object","properties":{"refPath":{}}},"controlNames":{"type":"array","items":{"type":"string"}}},"required":["inSection","controlNames"]} as const;
const S_epic_key_controls_at_frames = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"control_names":{"type":"array","items":{"type":"string"}},"frames":{"type":"array","items":{"type":"integer"}}},"required":["section","control_names","frames"]} as const;
const S_epic_key_controls_at_frames__control_rig_sequencer_editor_library = {"properties":{"inSection":{"type":"object","properties":{"refPath":{}}},"controlNames":{"type":"array","items":{"type":"string"}},"atTimes":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["inSection","controlNames","atTimes"]} as const;
const S_epic_key_selection_set = {"properties":{"id":{"type":"string"}},"required":["id"]} as const;
const S_epic_link_anim_sequence = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"anim_sequence":{"type":"object","properties":{"refPath":{}}},"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["sequence","anim_sequence","binding"]} as const;
const S_epic_link_anim_sequence__sequencer_tools_function_library = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"animSequence":{"type":"object","properties":{"refPath":{}}},"exportOptions":{"type":"object","properties":{"refPath":{}}},"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["sequence","animSequence","exportOptions","binding"]} as const;
const S_epic_list_component_sources = {"properties":{}} as const;
const S_epic_list_data_types = {"properties":{}} as const;
const S_epic_list_graphs = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig"]} as const;
const S_epic_list_node_types = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"filter":{"type":"string"}},"required":["deformer","filter"]} as const;
const S_epic_list_nodes = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig","graph"]} as const;
const S_epic_list_pins = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"node":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig","node"]} as const;
const S_epic_list_variables = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}}},"required":["control_rig"]} as const;
const S_epic_load_anim_into_rig = {"properties":{"cr_section":{"type":"object","properties":{"refPath":{}}},"anim_sequence_path":{"type":"string"},"start_frame":{"type":"integer"},"reset_controls":{"type":"boolean"},"key_reduce":{"type":"boolean"},"tolerance":{"type":"number"}},"required":["cr_section","anim_sequence_path"]} as const;
const S_epic_load_anim_sequence_into_control_rig_section_with_range = {"properties":{"movieSceneSection":{"type":"object","properties":{"refPath":{}}},"animSequence":{"type":"object","properties":{"refPath":{}}},"skelMeshComp":{"type":"object","properties":{"refPath":{}}},"inStartFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"bUseCustomAnimRange":{"type":"boolean"},"animStartRange":{"type":"object","properties":{"value":{"type":"integer"}}},"animEndRange":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]},"bKeyReduce":{"type":"boolean"},"tolerance":{"type":"number"},"interpolation":{"type":"string","enum":["Auto","User","Break","Linear","Constant","SmartAuto"]},"bResetControls":{"type":"boolean"},"bOntoSelectedControls":{"type":"boolean"}},"required":["movieSceneSection","animSequence","skelMeshComp","inStartFrame","bUseCustomAnimRange","animStartRange","animEndRange"]} as const;
const S_epic_load_anim_sequence_into_control_rig_section_with_range_async = {"properties":{"movieSceneSection":{"type":"object","properties":{"refPath":{}}},"animSequence":{"type":"object","properties":{"refPath":{}}},"skelMeshComp":{"type":"object","properties":{"refPath":{}}},"inStartFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"bUseCustomAnimRange":{"type":"boolean"},"animStartRange":{"type":"object","properties":{"value":{"type":"integer"}}},"animEndRange":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]},"bKeyReduce":{"type":"boolean"},"tolerance":{"type":"number"},"interpolation":{"type":"string","enum":["Auto","User","Break","Linear","Constant","SmartAuto"]},"bResetControls":{"type":"boolean"},"bOntoSelectedControls":{"type":"boolean"}},"required":["movieSceneSection","animSequence","skelMeshComp","inStartFrame","bUseCustomAnimRange","animStartRange","animEndRange"]} as const;
const S_epic_make_control_rig_reference = {"properties":{"assetOrClassPath":{"type":"string"}},"required":["assetOrClassPath"]} as const;
const S_epic_match_skeletal_animation_section = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"settings":{"type":"object","properties":{"boneName":{"type":"string"},"bMatchWithPrevious":{"type":"boolean"},"bMatchTranslation":{"type":"boolean"},"bMatchIncludeZHeight":{"type":"boolean"},"bMatchRotationYaw":{"type":"boolean"},"bMatchRotationPitch":{"type":"boolean"},"bMatchRotationRoll":{"type":"boolean"}}},"frame":{"type":"integer"}},"required":["section","settings","frame"]} as const;
const S_epic_merge_anim_layers = {"properties":{"indices":{"type":"array","items":{"type":"integer"}}},"required":["indices"]} as const;
const S_epic_merge_anim_layers_with_settings = {"properties":{"indices":{"type":"array","items":{"type":"integer"}},"mergeAnimLayerSettings":{"type":"object","properties":{"bakingKeySettings":{"type":"string","enum":["KeysOnly","AllFrames"]},"frameIncrement":{"type":"integer"},"bReduceKeys":{"type":"boolean"},"tolerancePercentage":{"type":"number"}}}},"required":["indices","mergeAnimLayerSettings"]} as const;
const S_epic_merge_anim_layers_with_settings_async = {"properties":{"indices":{"type":"array","items":{"type":"integer"}},"mergeAnimLayerSettings":{"type":"object","properties":{"bakingKeySettings":{"type":"string","enum":["KeysOnly","AllFrames"]},"frameIncrement":{"type":"integer"},"bReduceKeys":{"type":"boolean"},"tolerancePercentage":{"type":"number"}}}},"required":["indices","mergeAnimLayerSettings"]} as const;
const S_epic_mirror_selected_controls = {"properties":{}} as const;
const S_epic_mirror_selected_controls__control_rig_sequencer_editor_library = {"properties":{}} as const;
const S_epic_mirror_unselected_controls = {"properties":{}} as const;
const S_epic_move_constraint_key = {"properties":{"constraint":{"type":"object","properties":{"refPath":{}}},"constraintSection":{"type":"object","properties":{"refPath":{}}},"inTime":{"type":"object","properties":{"value":{"type":"integer"}}},"inNewTime":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["constraint","constraintSection","inTime","inNewTime"]} as const;
const S_epic_move_control_rig_space = {"properties":{"inSequence":{"type":"object","properties":{"refPath":{}}},"inControlRig":{"type":"object","properties":{"refPath":{}}},"inControlName":{"type":"string"},"inTime":{"type":"object","properties":{"value":{"type":"integer"}}},"inNewTime":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["inSequence","inControlRig","inControlName","inTime","inNewTime"]} as const;
const S_epic_move_space = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"old_frame":{"type":"integer"},"new_frame":{"type":"integer"}},"required":["sequence","control_rig_asset_path","control_name","old_frame","new_frame"]} as const;
const S_epic_open_cinematic_viewport = {"properties":{}} as const;
const S_epic_open_curve_editor = {"properties":{}} as const;
const S_epic_open_curve_editor__level_sequence_editor_subsystem = {"properties":{}} as const;
const S_epic_open_level_sequence_async = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_open_pose_library_tab = {"properties":{}} as const;
const S_epic_open_sequence = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}}},"required":["sequence"]} as const;
const S_epic_paste_bindings = {"properties":{"paste_token":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}},"parent_folder":{"type":"object","properties":{"refPath":{}}}},"required":["paste_token","sequence"]} as const;
const S_epic_paste_bindings_from_text = {"properties":{"textToImport":{"type":"string"},"pasteBindingsParams":{"type":"object","properties":{"bindings":{"type":"array","items":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"parentFolder":{"type":"object","properties":{"refPath":{}}},"folders":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"bDuplicateExistingActors":{"type":"boolean"},"pastedActors":{"type":"object"}}}},"required":["textToImport","pasteBindingsParams"]} as const;
const S_epic_paste_control_rig_pose_asset = {"properties":{"poseAsset":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"bKey":{"type":"boolean"},"bMirror":{"type":"boolean"},"bAdditive":{"type":"boolean"}},"required":["poseAsset","controlRig"]} as const;
const S_epic_paste_control_rig_pose_from_clipboard = {"properties":{"bMirror":{"type":"boolean"}}} as const;
const S_epic_paste_folders = {"properties":{"paste_token":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}},"parent_folder":{"type":"object","properties":{"refPath":{}}}},"required":["paste_token","sequence"]} as const;
const S_epic_paste_folders_from_text = {"properties":{"textToImport":{"type":"string"},"pasteFoldersParams":{"type":"object","properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"parentFolder":{"type":"object","properties":{"refPath":{}}}}}},"required":["textToImport","pasteFoldersParams"]} as const;
const S_epic_paste_sections = {"properties":{"paste_token":{"type":"string"},"target_tracks":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"paste_frame":{"type":"integer"}},"required":["paste_token","target_tracks"]} as const;
const S_epic_paste_sections_from_text = {"properties":{"textToImport":{"type":"string"},"pasteSectionsParams":{"type":"object","properties":{"tracks":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"trackRowIndices":{"type":"array","items":{"type":"integer"}},"decorationTargets":{"type":"array","items":{"type":"object","properties":{"container":{"type":"object","properties":{"refPath":{}}},"preferredDecoration":{"type":"object","properties":{"refPath":{}}}}}},"time":{"type":"object","properties":{"frameNumber":{"type":"object","properties":{"value":{"type":"integer"}}},"subFrame":{"type":"number"}}}}}},"required":["textToImport","pasteSectionsParams"]} as const;
const S_epic_paste_tracks = {"properties":{"paste_token":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}},"target_bindings":{"type":"array","items":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"parent_folder":{"type":"object","properties":{"refPath":{}}}},"required":["paste_token","sequence","target_bindings"]} as const;
const S_epic_paste_tracks_from_text = {"properties":{"textToImport":{"type":"string"},"pasteTracksParams":{"type":"object","properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"bindings":{"type":"array","items":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"parentFolder":{"type":"object","properties":{"refPath":{}}},"folders":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}}}},"required":["textToImport","pasteTracksParams"]} as const;
const S_epic_pause = {"properties":{}} as const;
const S_epic_pause_async = {"properties":{}} as const;
const S_epic_pin_actor_motion_trail = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"actor":{"type":"object","properties":{"refPath":{}}}},"required":["levelSequence","actor"]} as const;
const S_epic_pin_control_rig_motion_trail = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"}},"required":["levelSequence","controlRig","controlName"]} as const;
const S_epic_pin_curve = {"properties":{"channel":{"type":"object","properties":{"channelName":{"type":"string"},"section":{"type":"object","properties":{"refPath":{}}}}},"bPin":{"type":"boolean"}},"required":["channel","bPin"]} as const;
const S_epic_pin_selected_motion_trails = {"properties":{}} as const;
const S_epic_pin_socket_motion_trail = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"component":{"type":"object","properties":{"refPath":{}}},"socketName":{"type":"string"}},"required":["levelSequence","component","socketName"]} as const;
const S_epic_play = {"properties":{}} as const;
const S_epic_play_async = {"properties":{}} as const;
const S_epic_play_to = {"properties":{"frame":{"type":"integer"}},"required":["frame"]} as const;
const S_epic_play_to_frame_async = {"properties":{"frame":{"type":"integer"},"timeoutSeconds":{"type":"number"}},"required":["frame"]} as const;
const S_epic_rebake_linked_anim_sequences = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["binding"]} as const;
const S_epic_rebake_linked_anim_sequences_async = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["binding"]} as const;
const S_epic_rebind_component = {"properties":{"component_bindings":{"type":"array","items":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"component_name":{"type":"string"}},"required":["component_bindings","component_name"]} as const;
const S_epic_rebind_component__level_sequence_editor_subsystem = {"properties":{"componentBindings":{"type":"array","items":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"componentName":{"type":"string"}},"required":["componentBindings","componentName"]} as const;
const S_epic_recreate_control_rig_with_new_settings = {"properties":{"inTrack":{"type":"object","properties":{"refPath":{}}},"inSettingsToOverride":{"type":"object"}},"required":["inTrack","inSettingsToOverride"]} as const;
const S_epic_refresh_current_level_sequence_async = {"properties":{}} as const;
const S_epic_refresh_sequence = {"properties":{}} as const;
const S_epic_remove = {"properties":{"inBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["inBinding"]} as const;
const S_epic_remove_actors_from_binding = {"properties":{"actors":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["actors","binding"]} as const;
const S_epic_remove_actors_from_binding__level_sequence_editor_subsystem = {"properties":{"actors":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"objectBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["actors","objectBinding"]} as const;
const S_epic_remove_all_bindings = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["binding"]} as const;
const S_epic_remove_all_bindings__level_sequence_editor_subsystem = {"properties":{"objectBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["objectBinding"]} as const;
const S_epic_remove_all_links = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"pinPath":{"type":"string"}},"required":["deformer","pinPath"]} as const;
const S_epic_remove_binding = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["binding"]} as const;
const S_epic_remove_binding_tag = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"tag_name":{"type":"string"}},"required":["sequence","tag_name"]} as const;
const S_epic_remove_binding_tag__movie_scene_binding_tag_extensions = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"tagName":{"type":"string"}},"required":["sequence","tagName"]} as const;
const S_epic_remove_channel_key_at_frame = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelName":{"type":"string"},"frame":{"type":"integer"}},"required":["section","channelName","frame"]} as const;
const S_epic_remove_channel_override = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelName":{"type":"string"}},"required":["section","channelName"]} as const;
const S_epic_remove_child_folder = {"properties":{"targetFolder":{"type":"object","properties":{"refPath":{}}},"folderToRemove":{"type":"object","properties":{"refPath":{}}}},"required":["targetFolder","folderToRemove"]} as const;
const S_epic_remove_child_object_binding = {"properties":{"folder":{"type":"object","properties":{"refPath":{}}},"inObjectBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["folder","inObjectBinding"]} as const;
const S_epic_remove_child_track = {"properties":{"folder":{"type":"object","properties":{"refPath":{}}},"inTrack":{"type":"object","properties":{"refPath":{}}}},"required":["folder","inTrack"]} as const;
const S_epic_remove_component_binding = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"bindingName":{"type":"string"}},"required":["deformer","bindingName"]} as const;
const S_epic_remove_constraint = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"constraint":{"type":"object","properties":{"refPath":{}}},"bCompensate":{"type":"boolean"}},"required":["world","constraint"]} as const;
const S_epic_remove_control_rig_available_space = {"properties":{"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"space":{"type":"object","properties":{"type":{"type":"string","enum":["None","Bone","Null","Space","Control","Curve","Reference","Connector","Socket","All"]},"name":{"type":"string"}}}},"required":["controlRig","controlName","space"]} as const;
const S_epic_remove_control_rig_override = {"properties":{"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"propertyPath":{"type":"string"}},"required":["controlRig","controlName","propertyPath"]} as const;
const S_epic_remove_default = {"properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}}},"required":["channel"]} as const;
const S_epic_remove_graph = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"graphPath":{"type":"string"}},"required":["deformer","graphPath"]} as const;
const S_epic_remove_graph_binding = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"graphPath":{"type":"string"},"bindingName":{"type":"string"}},"required":["deformer","graphPath","bindingName"]} as const;
const S_epic_remove_invalid_bindings = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["binding"]} as const;
const S_epic_remove_invalid_bindings__level_sequence_editor_subsystem = {"properties":{"objectBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["objectBinding"]} as const;
const S_epic_remove_kernel_binding = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"bindingName":{"type":"string"}},"required":["deformer","nodePath","bindingName"]} as const;
const S_epic_remove_kernel_input_group = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"groupName":{"type":"string"}},"required":["deformer","nodePath","groupName"]} as const;
const S_epic_remove_key = {"properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}},"key":{"type":"object","properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}},"keyHandleId":{"type":"integer"}}}},"required":["channel","key"]} as const;
const S_epic_remove_key_at_frame = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channel_name":{"type":"string"},"frame":{"type":"integer"}},"required":["section","channel_name","frame"]} as const;
const S_epic_remove_link = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"outputPinPath":{"type":"string"},"inputPinPath":{"type":"string"}},"required":["deformer","outputPinPath","inputPinPath"]} as const;
const S_epic_remove_loop_binding = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"bindingName":{"type":"string"}},"required":["deformer","nodePath","bindingName"]} as const;
const S_epic_remove_node_paths_from_node_group = {"properties":{"group":{"type":"object","properties":{"refPath":{}}},"paths":{"type":"array","items":{"type":"string"}}},"required":["group","paths"]} as const;
const S_epic_remove_nodes = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"nodePaths":{"type":"array","items":{"type":"string"}}},"required":["deformer","nodePaths"]} as const;
const S_epic_remove_nodes_from_node_group = {"properties":{"group":{"type":"object","properties":{"refPath":{}}},"nodes":{"type":"array","items":{"type":"object","properties":{"sessionId":{"type":"string"},"rootSequencePath":{"type":"string"},"focusedSequencePath":{"type":"string"},"focusedTemplateId":{"type":"integer"},"nodePath":{"type":"string"},"nodeType":{"type":"string"},"label":{"type":"string"}}}}},"required":["group","nodes"]} as const;
const S_epic_remove_resource = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"resourceName":{"type":"string"}},"required":["deformer","resourceName"]} as const;
const S_epic_remove_root_folder = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"folder":{"type":"object","properties":{"refPath":{}}}},"required":["sequence","folder"]} as const;
const S_epic_remove_root_folder_from_sequence = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"folder":{"type":"object","properties":{"refPath":{}}}},"required":["sequence","folder"]} as const;
const S_epic_remove_section = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"section":{"type":"object","properties":{"refPath":{}}}},"required":["track","section"]} as const;
const S_epic_remove_section__movie_scene_track_extensions = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"section":{"type":"object","properties":{"refPath":{}}}},"required":["track","section"]} as const;
const S_epic_remove_selected_from_anim_layer = {"properties":{"index":{"type":"integer"}},"required":["index"]} as const;
const S_epic_remove_selection_from_selection_set = {"properties":{"id":{"type":"string"}},"required":["id"]} as const;
const S_epic_remove_socket = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"socket_name":{"type":"string"}},"required":["mesh","socket_name"]} as const;
const S_epic_remove_socket__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"socketName":{"type":"string"}},"required":["mesh","socketName"]} as const;
const S_epic_remove_track = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"track":{"type":"object","properties":{"refPath":{}}}},"required":["binding","track"]} as const;
const S_epic_remove_track__movie_scene_binding_extensions = {"properties":{"inBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"trackToRemove":{"type":"object","properties":{"refPath":{}}}},"required":["inBinding","trackToRemove"]} as const;
const S_epic_remove_track__movie_scene_sequence_extensions = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"track":{"type":"object","properties":{"refPath":{}}}},"required":["sequence","track"]} as const;
const S_epic_remove_track_from_sequence = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"track":{"type":"object","properties":{"refPath":{}}}},"required":["sequence","track"]} as const;
const S_epic_remove_variable = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["control_rig","name"]} as const;
const S_epic_remove_variable__deformer_graph_scripting_subsystem = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"}},"required":["deformer","variableName"]} as const;
const S_epic_rename_binding = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"pinPath":{"type":"string"},"newName":{"type":"string"}},"required":["deformer","pinPath","newName"]} as const;
const S_epic_rename_component_binding = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"bindingName":{"type":"string"},"newName":{"type":"string"}},"required":["deformer","bindingName","newName"]} as const;
const S_epic_rename_control_rig_control_channels = {"properties":{"inSequence":{"type":"object","properties":{"refPath":{}}},"inControlRig":{"type":"object","properties":{"refPath":{}}},"inOldControlNames":{"type":"array","items":{"type":"string"}},"inNewControlNames":{"type":"array","items":{"type":"string"}}},"required":["inSequence","inControlRig","inOldControlNames","inNewControlNames"]} as const;
const S_epic_rename_control_rig_pose_asset_control = {"properties":{"poseAsset":{"type":"object","properties":{"refPath":{}}},"currentName":{"type":"string"},"newName":{"type":"string"}},"required":["poseAsset","currentName","newName"]} as const;
const S_epic_rename_graph = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"graphPath":{"type":"string"},"newName":{"type":"string"}},"required":["deformer","graphPath","newName"]} as const;
const S_epic_rename_kernel_input_group = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"groupName":{"type":"string"},"newName":{"type":"string"}},"required":["deformer","nodePath","groupName","newName"]} as const;
const S_epic_rename_resource = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"resourceName":{"type":"string"},"newName":{"type":"string"}},"required":["deformer","resourceName","newName"]} as const;
const S_epic_rename_socket = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"old_name":{"type":"string"},"new_name":{"type":"string"}},"required":["mesh","old_name","new_name"]} as const;
const S_epic_rename_socket__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"oldName":{"type":"string"},"newName":{"type":"string"}},"required":["mesh","oldName","newName"]} as const;
const S_epic_rename_variable = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"},"newName":{"type":"string"}},"required":["deformer","variableName","newName"]} as const;
const S_epic_reorder_anim_layers = {"properties":{"old_index":{"type":"integer"},"new_index":{"type":"integer"}},"required":["old_index","new_index"]} as const;
const S_epic_reorder_anim_layers__control_rig_sequencer_editor_library = {"properties":{"oldIndex":{"type":"integer"},"newIndex":{"type":"integer"}},"required":["oldIndex","newIndex"]} as const;
const S_epic_reorder_nodes = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"sessionId":{"type":"string"},"rootSequencePath":{"type":"string"},"focusedSequencePath":{"type":"string"},"focusedTemplateId":{"type":"integer"},"nodePath":{"type":"string"},"nodeType":{"type":"string"},"label":{"type":"string"}}}}},"required":["nodes"]} as const;
const S_epic_replace_binding_with_actors = {"properties":{"actors":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["actors","binding"]} as const;
const S_epic_replace_binding_with_actors__level_sequence_editor_subsystem = {"properties":{"actors":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"objectBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["actors","objectBinding"]} as const;
const S_epic_replace_control_rig = {"properties":{"inControlRigTrack":{"type":"object","properties":{"refPath":{}}},"newControlRig":{"type":"object","properties":{"refPath":{}}}},"required":["inControlRigTrack","newControlRig"]} as const;
const S_epic_replace_control_rig_from_reference = {"properties":{"inTrack":{"type":"object","properties":{"refPath":{}}},"inRef":{"type":"object","properties":{"blueprintRigClass":{"type":"object","properties":{"refPath":{}}},"controlRigAsset":{"type":"object","properties":{"refPath":{}}}}}},"required":["inTrack","inRef"]} as const;
const S_epic_reset_control_rig_controls_to_default = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlNames":{"type":"array","items":{"type":"string"}},"bSetKey":{"type":"boolean"}},"required":["levelSequence","controlRig","controlNames"]} as const;
const S_epic_reset_playback_context = {"properties":{}} as const;
const S_epic_restore_pre_animated_state_async = {"properties":{}} as const;
const S_epic_save_control_rig_pose_to_clipboard = {"properties":{"bWholePose":{"type":"boolean"}}} as const;
const S_epic_save_default_spawnable_state = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["binding"]} as const;
const S_epic_save_default_spawnable_state__1 = {"properties":{"objectBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}},"required":["objectBinding"]} as const;
const S_epic_select_all_control_rig_controls = {"properties":{"bAlreadySelected":{"type":"boolean"}}} as const;
const S_epic_select_anim_layer_objects = {"properties":{"index":{"type":"integer"},"bClearSelection":{"type":"boolean"}},"required":["index"]} as const;
const S_epic_select_anim_layer_weight_channel = {"properties":{"index":{"type":"integer"},"bClearSelection":{"type":"boolean"}},"required":["index"]} as const;
const S_epic_select_bindings = {"properties":{"bindings":{"type":"array","items":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}}},"required":["bindings"]} as const;
const S_epic_select_bindings__level_sequence_editor_blueprint_library = {"properties":{"objectBindings":{"type":"array","items":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}}}},"required":["objectBindings"]} as const;
const S_epic_select_channels = {"properties":{"channels":{"type":"array","items":{"type":"object","properties":{"channelName":{"type":"string"},"section":{"type":"object","properties":{"refPath":{}}}}}}},"required":["channels"]} as const;
const S_epic_select_channels__level_sequence_editor_blueprint_library = {"properties":{"channels":{"type":"array","items":{"type":"object","properties":{"channelName":{"type":"string"},"section":{"type":"object","properties":{"refPath":{}}}}}}},"required":["channels"]} as const;
const S_epic_select_control = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"selected":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_name"]} as const;
const S_epic_select_control_rig_controls = {"properties":{"controlRig":{"type":"object","properties":{"refPath":{}}},"controlNames":{"type":"array","items":{"type":"string"}},"bSelect":{"type":"boolean"},"bClearSelection":{"type":"boolean"}},"required":["controlRig","controlNames"]} as const;
const S_epic_select_control_rig_pose_asset_controls = {"properties":{"poseAsset":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"bMirror":{"type":"boolean"},"bClearSelection":{"type":"boolean"}},"required":["poseAsset","controlRig"]} as const;
const S_epic_select_control_rig_pose_clipboard_controls = {"properties":{"bMirror":{"type":"boolean"}}} as const;
const S_epic_select_curve_editor_keys = {"properties":{"channel":{"type":"object","properties":{"channelName":{"type":"string"},"section":{"type":"object","properties":{"refPath":{}}}}},"indices":{"type":"array","items":{"type":"integer"}}},"required":["channel","indices"]} as const;
const S_epic_select_folders = {"properties":{"folders":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["folders"]} as const;
const S_epic_select_folders__level_sequence_editor_blueprint_library = {"properties":{"folders":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["folders"]} as const;
const S_epic_select_mirrored_controls = {"properties":{}} as const;
const S_epic_select_mirrored_controls__control_rig_sequencer_editor_library = {"properties":{}} as const;
const S_epic_select_sections = {"properties":{"sections":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["sections"]} as const;
const S_epic_select_sections__level_sequence_editor_blueprint_library = {"properties":{"sections":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["sections"]} as const;
const S_epic_select_selection_set = {"properties":{"id":{"type":"string"},"bMirror":{"type":"boolean"},"bAdd":{"type":"boolean"},"bToggle":{"type":"boolean"},"bSelect":{"type":"boolean"}},"required":["id"]} as const;
const S_epic_select_tracks = {"properties":{"tracks":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["tracks"]} as const;
const S_epic_select_tracks__level_sequence_editor_blueprint_library = {"properties":{"tracks":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}}},"required":["tracks"]} as const;
const S_epic_set_actor_world_transform = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"actor":{"type":"object","properties":{"refPath":{}}},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"worldTransform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","actor","frame","worldTransform"]} as const;
const S_epic_set_actor_world_transforms = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"actor":{"type":"object","properties":{"refPath":{}}},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"worldTransforms":{"type":"array","items":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","actor","frames","worldTransforms"]} as const;
const S_epic_set_actor_world_transforms_async = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"actor":{"type":"object","properties":{"refPath":{}}},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"worldTransforms":{"type":"array","items":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","actor","frames","worldTransforms"]} as const;
const S_epic_set_anim_layer_key = {"properties":{"index":{"type":"integer"}},"required":["index"]} as const;
const S_epic_set_anim_layer_passthrough_key = {"properties":{"index":{"type":"integer"}},"required":["index"]} as const;
const S_epic_set_anim_layer_properties = {"properties":{"index":{"type":"integer"},"properties":{"type":"object","properties":{"name":{"type":"string"},"bActive":{"type":"boolean"},"bLock":{"type":"boolean"},"weight":{"type":"number"},"type":{"type":"string","enum":["Base","Additive","Override"]},"bKeyed":{"type":"boolean"}}}},"required":["index","properties"]} as const;
const S_epic_set_anim_mode_gizmo_scale = {"properties":{"scale":{"type":"number"}},"required":["scale"]} as const;
const S_epic_set_anim_mode_hide_manips = {"properties":{"hide":{"type":"boolean"}},"required":["hide"]} as const;
const S_epic_set_anim_mode_hierarchy = {"properties":{"enabled":{"type":"boolean"}},"required":["enabled"]} as const;
const S_epic_set_anim_mode_local_spaces = {"properties":{"enabled":{"type":"boolean"}},"required":["enabled"]} as const;
const S_epic_set_anim_mode_nulls = {"properties":{"enabled":{"type":"boolean"}},"required":["enabled"]} as const;
const S_epic_set_anim_mode_only_rig_sel = {"properties":{"only_rig":{"type":"boolean"}},"required":["only_rig"]} as const;
const S_epic_set_animation_mode_settings = {"properties":{"settings":{"type":"object","properties":{"transformGizmoSize":{"type":"number"},"bDisplayHierarchy":{"type":"boolean"},"bDisplayNulls":{"type":"boolean"},"bHideControlShapes":{"type":"boolean"},"bOnlySelectRigControls":{"type":"boolean"},"bLocalTransformsInEachLocalSpace":{"type":"boolean"},"bDisplaySockets":{"type":"boolean"},"bShowAllProxyControls":{"type":"boolean"},"bShowControlsAsOverlay":{"type":"boolean"},"bDisplayAxesOnSelection":{"type":"boolean"},"axisScale":{"type":"number"},"axisThickness":{"type":"number"},"bCoordSystemPerWidgetMode":{"type":"boolean"},"bMirrorControlsOnKeys":{"type":"boolean"},"bMirrorControlsAtCurrentTime":{"type":"boolean"},"bMirrorControlsCopyTangents":{"type":"boolean"},"bMirrorControlsFlipTangents":{"type":"boolean"},"bShowConstraints":{"type":"boolean"},"bShowInactiveConstraints":{"type":"boolean"},"bAutoKeyOnRelease":{"type":"boolean"},"tweenNoiseAmplitude":{"type":"number"},"tweenNoiseFrequency":{"type":"number"},"tweenWaveAmplitude":{"type":"number"},"tweenWavePeriodSeconds":{"type":"number"},"tweenWavePhaseOffset":{"type":"number"}}}},"required":["settings"]} as const;
const S_epic_set_animation_mode_tool_open = {"properties":{"tool":{"type":"string","enum":["PoseLibrary","AnimLayers","SelectionSets","Tween","Constraints","Snapper","MotionTrails","Details","Outliner"]},"bOpen":{"type":"boolean"}},"required":["tool","bOpen"]} as const;
const S_epic_set_binding_data_domain = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"pinPath":{"type":"string"},"dataDomain":{"type":"string"}},"required":["deformer","pinPath","dataDomain"]} as const;
const S_epic_set_binding_data_type = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"pinPath":{"type":"string"},"dataType":{"type":"string"}},"required":["deformer","pinPath","dataType"]} as const;
const S_epic_set_binding_name = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"name":{"type":"string"}},"required":["binding","name"]} as const;
const S_epic_set_blend_type = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"inBlendType":{"type":"string","enum":["Absolute","Additive","Relative","AdditiveFromBase","Override"]}},"required":["section","inBlendType"]} as const;
const S_epic_set_blender_system = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"blenderSystem":{"type":"object","properties":{"refPath":{}}}},"required":["track","blenderSystem"]} as const;
const S_epic_set_bool = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"boolean"},"set_key":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_name","frame","value"]} as const;
const S_epic_set_bound_actor_class = {"properties":{"templateSequence":{"type":"object","properties":{"refPath":{}}},"actorClass":{"type":"object","properties":{"refPath":{}}}},"required":["templateSequence","actorClass"]} as const;
const S_epic_set_byte_track_enum = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"enum_class_path":{"type":"string"}},"required":["track","enum_class_path"]} as const;
const S_epic_set_byte_track_enum__movie_scene_property_track_extensions = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"inEnum":{"type":"object","properties":{"refPath":{}}}},"required":["track","inEnum"]} as const;
const S_epic_set_camera_cut_auto_arrange_sections = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"bAutoArrangeSections":{"type":"boolean"}},"required":["track","bAutoArrangeSections"]} as const;
const S_epic_set_camera_cut_binding = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"camera_binding_id":{"type":"string"}},"required":["section","camera_binding_id"]} as const;
const S_epic_set_camera_cut_binding__movie_scene_section_extensions = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"cameraBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"rootSequence":{"type":"object","properties":{"refPath":{}}}},"required":["section","cameraBinding"]} as const;
const S_epic_set_camera_cut_can_blend = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"bCanBlend":{"type":"boolean"}},"required":["track","bCanBlend"]} as const;
const S_epic_set_camera_lock = {"properties":{"lock":{"type":"boolean"}},"required":["lock"]} as const;
const S_epic_set_clock_source = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"clock_source":{"type":"string"}},"required":["sequence","clock_source"]} as const;
const S_epic_set_clock_source__movie_scene_sequence_extensions = {"properties":{"inSequence":{"type":"object","properties":{"refPath":{}}},"inClockSource":{"type":"string","enum":["Tick","Platform","Audio","RelativeTimecode","Timecode","PlayEveryFrame","Custom"]}},"required":["inSequence","inClockSource"]} as const;
const S_epic_set_comment = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"comment":{"type":"string"}},"required":["deformer","nodePath","comment"]} as const;
const S_epic_set_completion_mode = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"inCompletionMode":{"type":"string","enum":["KeepState","RestoreState","ProjectDefault"]}},"required":["section","inCompletionMode"]} as const;
const S_epic_set_component_binding_source = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"bindingName":{"type":"string"},"componentSource":{"type":"string"}},"required":["deformer","bindingName","componentSource"]} as const;
const S_epic_set_constraint_active_key = {"properties":{"inConstraint":{"type":"object","properties":{"refPath":{}}},"bActive":{"type":"boolean"},"inFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["inConstraint","bActive","inFrame"]} as const;
const S_epic_set_control_rig_apply_mode = {"properties":{"inControlRig":{"type":"object","properties":{"refPath":{}}},"inApplyMode":{"type":"string","enum":["Replace","Additive","Direct"]}},"required":["inControlRig","inApplyMode"]} as const;
const S_epic_set_control_rig_available_spaces_order = {"properties":{"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"spaces":{"type":"array","items":{"type":"object","properties":{"type":{"type":"string","enum":["None","Bone","Null","Space","Control","Curve","Reference","Connector","Socket","All"]},"name":{"type":"string"}}}}},"required":["controlRig","controlName","spaces"]} as const;
const S_epic_set_control_rig_controls_visible = {"properties":{"controlRig":{"type":"object","properties":{"refPath":{}}},"bVisible":{"type":"boolean"},"moduleName":{"type":"string"}},"required":["controlRig","bVisible"]} as const;
const S_epic_set_control_rig_layered_mode = {"properties":{"inTrack":{"type":"object","properties":{"refPath":{}}},"bSetIsLayered":{"type":"boolean"}},"required":["inTrack","bSetIsLayered"]} as const;
const S_epic_set_control_rig_override = {"properties":{"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"propertyPath":{"type":"string"},"value":{"type":"string"}},"required":["controlRig","controlName","propertyPath"]} as const;
const S_epic_set_control_rig_priority_order = {"properties":{"inTrack":{"type":"object","properties":{"refPath":{}}},"priorityOrder":{"type":"integer"}},"required":["inTrack","priorityOrder"]} as const;
const S_epic_set_control_rig_rotation_order_override = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"rotationOrder":{"type":"string","enum":["XYZ","XZY","YXZ","YZX","ZXY","ZYX"]}},"required":["levelSequence","controlRig","controlName","rotationOrder"]} as const;
const S_epic_set_control_rig_section_transform_mask = {"properties":{"inSection":{"type":"object","properties":{"refPath":{}}},"mask":{"type":"object","properties":{"bTranslationX":{"type":"boolean"},"bTranslationY":{"type":"boolean"},"bTranslationZ":{"type":"boolean"},"bRotationX":{"type":"boolean"},"bRotationY":{"type":"boolean"},"bRotationZ":{"type":"boolean"},"bScaleX":{"type":"boolean"},"bScaleY":{"type":"boolean"},"bScaleZ":{"type":"boolean"},"bWeight":{"type":"boolean"}}}},"required":["inSection","mask"]} as const;
const S_epic_set_control_rig_space = {"properties":{"inSequence":{"type":"object","properties":{"refPath":{}}},"inControlRig":{"type":"object","properties":{"refPath":{}}},"inControlName":{"type":"string"},"inSpaceKey":{"type":"object","properties":{"type":{"type":"string","enum":["None","Bone","Null","Space","Control","Curve","Reference","Connector","Socket","All"]},"name":{"type":"string"}}},"inTime":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["inSequence","inControlRig","inControlName","inSpaceKey","inTime"]} as const;
const S_epic_set_control_rig_world_transform = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"worldTransform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]},"bSetKey":{"type":"boolean"}},"required":["levelSequence","controlRig","controlName","frame","worldTransform"]} as const;
const S_epic_set_control_rig_world_transforms = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"worldTransforms":{"type":"array","items":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames","worldTransforms"]} as const;
const S_epic_set_controls_mask = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"control_names":{"type":"array","items":{"type":"string"}},"visible":{"type":"boolean"}},"required":["section","control_names","visible"]} as const;
const S_epic_set_controls_mask__control_rig_sequencer_editor_library = {"properties":{"inSection":{"type":"object","properties":{"refPath":{}}},"controlNames":{"type":"array","items":{"type":"string"}},"bVisible":{"type":"boolean"}},"required":["inSection","controlNames","bVisible"]} as const;
const S_epic_set_curve_editor_view_mode = {"properties":{"viewMode":{"type":"string","enum":["Absolute","Stacked","Normalized"]}},"required":["viewMode"]} as const;
const S_epic_set_custom_clock_source = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"clockSource":{"type":"object","properties":{"refPath":{}}}},"required":["sequence","clockSource"]} as const;
const S_epic_set_custom_time_warp = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"timeWarpClass":{"type":"object","properties":{"refPath":{}}}},"required":["section","timeWarpClass"]} as const;
const S_epic_set_data_interface_setting = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"settingName":{"type":"string"},"value":{"type":"string"}},"required":["deformer","nodePath","settingName","value"]} as const;
const S_epic_set_default_value = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channel_name":{"type":"string"},"value":{"type":"number"}},"required":["section","channel_name","value"]} as const;
const S_epic_set_display_name = {"properties":{"inBinding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"inDisplayName":{"type":"string"}},"required":["inBinding","inDisplayName"]} as const;
const S_epic_set_display_name__movie_scene_track_extensions = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"inName":{"type":"string"}},"required":["track","inName"]} as const;
const S_epic_set_display_rate = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"numerator":{"type":"integer"},"denominator":{"type":"integer"}},"required":["sequence","numerator"]} as const;
const S_epic_set_display_rate__movie_scene_sequence_extensions = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"displayRate":{"type":"object","properties":{"numerator":{"type":"integer"},"denominator":{"type":"integer"}}}},"required":["sequence","displayRate"]} as const;
const S_epic_set_double_default = {"properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}},"inDefaultValue":{"type":"number"}},"required":["channel","inDefaultValue"]} as const;
const S_epic_set_double_key_value = {"properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}},"key":{"type":"object","properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}},"keyHandleId":{"type":"integer"}}},"newValue":{"type":"number"}},"required":["channel","key","newValue"]} as const;
const S_epic_set_ease_in_duration = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"inDuration":{"type":"integer"}},"required":["section","inDuration"]} as const;
const S_epic_set_ease_out_duration = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"inDuration":{"type":"integer"}},"required":["section","inDuration"]} as const;
const S_epic_set_edit_pivot_tool_active = {"properties":{"bActive":{"type":"boolean"}},"required":["bActive"]} as const;
const S_epic_set_emulated_network_mask = {"properties":{"networkMask":{"type":"string","enum":["Server","Client","All"]}},"required":["networkMask"]} as const;
const S_epic_set_emulated_network_mask_async = {"properties":{"networkMask":{"type":"string","enum":["Server","Client","All"]}},"required":["networkMask"]} as const;
const S_epic_set_end_frame_bounded = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"bIsBounded":{"type":"boolean"}},"required":["section","bIsBounded"]} as const;
const S_epic_set_enum_track_enum = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"inEnum":{"type":"object","properties":{"refPath":{}}}},"required":["track","inEnum"]} as const;
const S_epic_set_euler_transform = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"location_x":{"type":"number"},"location_y":{"type":"number"},"location_z":{"type":"number"},"rotation_pitch":{"type":"number"},"rotation_yaw":{"type":"number"},"rotation_roll":{"type":"number"},"scale_x":{"type":"number"},"scale_y":{"type":"number"},"scale_z":{"type":"number"},"set_key":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_set_evaluation_type = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"eval_type":{"type":"string"}},"required":["sequence","eval_type"]} as const;
const S_epic_set_evaluation_type__movie_scene_sequence_extensions = {"properties":{"inSequence":{"type":"object","properties":{"refPath":{}}},"inEvaluationType":{"type":"string","enum":["FrameLocked","WithSubFrames"]}},"required":["inSequence","inEvaluationType"]} as const;
const S_epic_set_film_overlay = {"properties":{"viewport":{"type":"object","properties":{"slotName":{"type":"string"}}},"overlayName":{"type":"string"}},"required":["viewport","overlayName"]} as const;
const S_epic_set_film_overlay_frame_enabled = {"properties":{"frameName":{"type":"string"},"bEnabled":{"type":"boolean"}},"required":["frameName","bEnabled"]} as const;
const S_epic_set_film_overlay_frame_tint = {"properties":{"frameName":{"type":"string"},"tint":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}}},"required":["frameName","tint"]} as const;
const S_epic_set_film_overlay_tint = {"properties":{"viewport":{"type":"object","properties":{"slotName":{"type":"string"}}},"tint":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}}},"required":["viewport","tint"]} as const;
const S_epic_set_fkcontrol_rig_controls_active = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlNames":{"type":"array","items":{"type":"string"}},"bActive":{"type":"boolean"}},"required":["levelSequence","controlRig","controlNames","bActive"]} as const;
const S_epic_set_float = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"number"},"set_key":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_name","frame","value"]} as const;
const S_epic_set_float_default = {"properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}},"inDefaultValue":{"type":"number"}},"required":["channel","inDefaultValue"]} as const;
const S_epic_set_float_key_value = {"properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}},"key":{"type":"object","properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}},"keyHandleId":{"type":"integer"}}},"newValue":{"type":"number"}},"required":["channel","key","newValue"]} as const;
const S_epic_set_folder_color = {"properties":{"folder":{"type":"object","properties":{"refPath":{}}},"inFolderColor":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}}},"required":["folder","inFolderColor"]} as const;
const S_epic_set_folder_name = {"properties":{"folder":{"type":"object","properties":{"refPath":{}}},"inFolderName":{"type":"string"}},"required":["folder","inFolderName"]} as const;
const S_epic_set_function_settings = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"graphPath":{"type":"string"},"category":{"type":"string"},"bIsPublic":{"type":"boolean"}},"required":["deformer","graphPath","category","bIsPublic"]} as const;
const S_epic_set_global_transform = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"item":{"type":"object","properties":{"type":{"type":"string","enum":["None","Bone","Null","Space","Control","Curve","Reference","Connector","Socket","All"]},"name":{"type":"string"}}},"transform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"initial":{"type":"boolean"}},"required":["control_rig","item","transform"]} as const;
const S_epic_set_int = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"integer"},"set_key":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_name","frame","value"]} as const;
const S_epic_set_kernel_additional_sources = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"sourcePaths":{"type":"array","items":{"type":"string"}}},"required":["deformer","nodePath","sourcePaths"]} as const;
const S_epic_set_kernel_binding_options = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"bindingName":{"type":"string"},"bSupportAtomic":{"type":"boolean"},"bSupportRead":{"type":"boolean"}},"required":["deformer","nodePath","bindingName","bSupportAtomic","bSupportRead"]} as const;
const S_epic_set_kernel_execution_domain = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"executionDomain":{"type":"string"}},"required":["deformer","nodePath","executionDomain"]} as const;
const S_epic_set_kernel_group_size = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"groupSize":{"type":"object","properties":{"x":{"type":"integer"},"y":{"type":"integer"},"z":{"type":"integer"}}}},"required":["deformer","nodePath","groupSize"]} as const;
const S_epic_set_kernel_name = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"kernelName":{"type":"string"}},"required":["deformer","nodePath","kernelName"]} as const;
const S_epic_set_kernel_source = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"source":{"type":"string"}},"required":["deformer","nodePath","source"]} as const;
const S_epic_set_key_time = {"properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}},"key":{"type":"object","properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}},"keyHandleId":{"type":"integer"}}},"newFrameNumber":{"type":"object","properties":{"value":{"type":"integer"}}},"subFrame":{"type":"number"},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["channel","key","newFrameNumber"]} as const;
const S_epic_set_layered_mode = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"is_layered":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","is_layered"]} as const;
const S_epic_set_letterbox_aspect_ratio = {"properties":{"width":{"type":"number"},"height":{"type":"number"}},"required":["width","height"]} as const;
const S_epic_set_linked_anim_sequence_settings = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"animSequence":{"type":"object","properties":{"refPath":{}}},"settings":{"type":"object","properties":{"skelTrackGuid":{"type":"string"},"pathToAnimSequence":{"type":"object","properties":{"refPath":{}}},"bAutoBake":{"type":"boolean"},"bExportTransforms":{"type":"boolean"},"bExportMorphTargets":{"type":"boolean"},"bExportAttributeCurves":{"type":"boolean"},"bExportMaterialCurves":{"type":"boolean"},"bSkipCurvesWithZeroValue":{"type":"boolean"},"bSetRetargetSourceAsset":{"type":"boolean"},"interpolation":{"type":"string","enum":["Linear","Step"]},"curveInterpolation":{"type":"string","enum":["RCIM_Linear","RCIM_Constant","RCIM_Cubic"]},"bRecordInWorldSpace":{"type":"boolean"},"bEvaluateAllSkeletalMeshComponents":{"type":"boolean"},"includeAnimationNames":{"type":"array","items":{"type":"string"}},"excludeAnimationNames":{"type":"array","items":{"type":"string"}},"bRemoveExcludedCurves":{"type":"boolean"},"warmUpFrames":{"type":"object","properties":{"value":{"type":"integer"}}},"delayBeforeStart":{"type":"object","properties":{"value":{"type":"integer"}}},"bUseCustomTimeRange":{"type":"boolean"},"customStartFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"customEndFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"customDisplayRate":{"type":"object","properties":{"numerator":{"type":"integer"},"denominator":{"type":"integer"}}},"bUseCustomFrameRate":{"type":"boolean"},"customFrameRate":{"type":"object","properties":{"numerator":{"type":"integer"},"denominator":{"type":"integer"}}}}}},"required":["binding","animSequence","settings"]} as const;
const S_epic_set_local_control_rig_bool = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"value":{"type":"boolean"},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]},"bSetKey":{"type":"boolean"}},"required":["levelSequence","controlRig","controlName","frame","value"]} as const;
const S_epic_set_local_control_rig_bools = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"values":{"type":"array","items":{"type":"boolean"}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames","values"]} as const;
const S_epic_set_local_control_rig_euler_transform = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"value":{"type":"object","properties":{"location":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]},"bSetKey":{"type":"boolean"}},"required":["levelSequence","controlRig","controlName","frame","value"]} as const;
const S_epic_set_local_control_rig_euler_transforms = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"values":{"type":"array","items":{"type":"object","properties":{"location":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames","values"]} as const;
const S_epic_set_local_control_rig_float = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"value":{"type":"number"},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]},"bSetKey":{"type":"boolean"}},"required":["levelSequence","controlRig","controlName","frame","value"]} as const;
const S_epic_set_local_control_rig_floats = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"values":{"type":"array","items":{"type":"number"}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames","values"]} as const;
const S_epic_set_local_control_rig_int = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"value":{"type":"integer"},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]},"bSetKey":{"type":"boolean"}},"required":["levelSequence","controlRig","controlName","frame","value"]} as const;
const S_epic_set_local_control_rig_ints = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"values":{"type":"array","items":{"type":"integer"}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames","values"]} as const;
const S_epic_set_local_control_rig_position = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"value":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]},"bSetKey":{"type":"boolean"}},"required":["levelSequence","controlRig","controlName","frame","value"]} as const;
const S_epic_set_local_control_rig_positions = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"values":{"type":"array","items":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames","values"]} as const;
const S_epic_set_local_control_rig_rotator = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"value":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]},"bSetKey":{"type":"boolean"}},"required":["levelSequence","controlRig","controlName","frame","value"]} as const;
const S_epic_set_local_control_rig_rotators = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"values":{"type":"array","items":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames","values"]} as const;
const S_epic_set_local_control_rig_scale = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"value":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]},"bSetKey":{"type":"boolean"}},"required":["levelSequence","controlRig","controlName","frame","value"]} as const;
const S_epic_set_local_control_rig_scales = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"values":{"type":"array","items":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames","values"]} as const;
const S_epic_set_local_control_rig_transform = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"value":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]},"bSetKey":{"type":"boolean"}},"required":["levelSequence","controlRig","controlName","frame","value"]} as const;
const S_epic_set_local_control_rig_transform_no_scale = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"value":{"type":"object","properties":{"location":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"},"w":{"type":"number"}}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]},"bSetKey":{"type":"boolean"}},"required":["levelSequence","controlRig","controlName","frame","value"]} as const;
const S_epic_set_local_control_rig_transform_no_scales = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"values":{"type":"array","items":{"type":"object","properties":{"location":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"},"w":{"type":"number"}}}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames","values"]} as const;
const S_epic_set_local_control_rig_transforms = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"values":{"type":"array","items":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames","values"]} as const;
const S_epic_set_local_control_rig_vector2_d = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"value":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]},"bSetKey":{"type":"boolean"}},"required":["levelSequence","controlRig","controlName","frame","value"]} as const;
const S_epic_set_local_control_rig_vector2_ds = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"values":{"type":"array","items":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frames","values"]} as const;
const S_epic_set_local_transform = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"item":{"type":"object","properties":{"type":{"type":"string","enum":["None","Bone","Null","Space","Control","Curve","Reference","Connector","Socket","All"]},"name":{"type":"string"}}},"transform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"initial":{"type":"boolean"}},"required":["control_rig","item","transform"]} as const;
const S_epic_set_lock_camera_cut_to_viewport_async = {"properties":{"bLock":{"type":"boolean"}},"required":["bLock"]} as const;
const S_epic_set_lock_level_sequence = {"properties":{"bLock":{"type":"boolean"}},"required":["bLock"]} as const;
const S_epic_set_loop_count = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"count":{"type":"integer"}},"required":["deformer","nodePath","count"]} as const;
const S_epic_set_loop_mode = {"properties":{"loop":{"type":"boolean"}},"required":["loop"]} as const;
const S_epic_set_loop_mode__level_sequence_editor_blueprint_library = {"properties":{"newLoopMode":{"type":"string","enum":["SLM_NoLoop","SLM_Loop","SLM_LoopSelectionRange"]}},"required":["newLoopMode"]} as const;
const S_epic_set_marked_frames_shown_globally = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"bShow":{"type":"boolean"}},"required":["sequence","bShow"]} as const;
const S_epic_set_material = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"slot_name":{"type":"string"},"material":{"type":"object","properties":{"refPath":{}}}},"required":["mesh","slot_name","material"]} as const;
const S_epic_set_material__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"slotName":{"type":"string"},"material":{"type":"object","properties":{"refPath":{}}}},"required":["mesh","slotName","material"]} as const;
const S_epic_set_motion_trail_offset = {"properties":{"trailId":{"type":"string"},"offset":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}},"required":["trailId","offset"]} as const;
const S_epic_set_motion_trail_settings = {"properties":{"inSettings":{"type":"object","properties":{"bShowSelectedTrails":{"type":"boolean"},"trailStyle":{"type":"string","enum":["Default","Dashed","Time","HeatMap"]},"motionTrailRange":{"type":"string","enum":["PlaybackRange","SelectionRange","SpecifiedRange"]},"framesBefore":{"type":"integer"},"framesAfter":{"type":"integer"},"trailThickness":{"type":"number"},"bShowKeys":{"type":"boolean"},"keySize":{"type":"number"},"bShowMarks":{"type":"boolean"},"markSize":{"type":"number"},"currentFrameMarkSize":{"type":"number"},"currentFrameMarkWidth":{"type":"number"},"maxNumberPinned":{"type":"integer"},"defaultColor":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}},"timePreColor":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}},"timePostColor":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}},"dashPreColor":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}},"dashPostColor":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}},"keyColor":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}},"selectedKeyColor":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}},"markColor":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}},"currentFrameMarkColor":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}}}}},"required":["inSettings"]} as const;
const S_epic_set_motion_trail_space = {"properties":{"trailId":{"type":"string"},"spaceActor":{"type":"object","properties":{"refPath":{}}},"componentOrSocketName":{"type":"string"}},"required":["trailId","spaceActor","componentOrSocketName"]} as const;
const S_epic_set_motion_trails_visible = {"properties":{"bVisible":{"type":"boolean"}},"required":["bVisible"]} as const;
const S_epic_set_node_deactivated = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"type":{"type":"string"}}}},"deactivated":{"type":"boolean"}},"required":["nodes","deactivated"]} as const;
const S_epic_set_node_deactivated__1 = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"sessionId":{"type":"string"},"rootSequencePath":{"type":"string"},"focusedSequencePath":{"type":"string"},"focusedTemplateId":{"type":"integer"},"nodePath":{"type":"string"},"nodeType":{"type":"string"},"label":{"type":"string"}}}},"bDeactivated":{"type":"boolean"}},"required":["nodes","bDeactivated"]} as const;
const S_epic_set_node_expanded = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"type":{"type":"string"}}}},"expanded":{"type":"boolean"}},"required":["nodes","expanded"]} as const;
const S_epic_set_node_expanded__1 = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"sessionId":{"type":"string"},"rootSequencePath":{"type":"string"},"focusedSequencePath":{"type":"string"},"focusedTemplateId":{"type":"integer"},"nodePath":{"type":"string"},"nodeType":{"type":"string"},"label":{"type":"string"}}}},"bExpanded":{"type":"boolean"}},"required":["nodes","bExpanded"]} as const;
const S_epic_set_node_group_filter_enabled = {"properties":{"group":{"type":"object","properties":{"refPath":{}}},"bEnabled":{"type":"boolean"}},"required":["group","bEnabled"]} as const;
const S_epic_set_node_group_name = {"properties":{"group":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["group","name"]} as const;
const S_epic_set_node_hidden = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"sessionId":{"type":"string"},"rootSequencePath":{"type":"string"},"focusedSequencePath":{"type":"string"},"focusedTemplateId":{"type":"integer"},"nodePath":{"type":"string"},"nodeType":{"type":"string"},"label":{"type":"string"}}}},"bHidden":{"type":"boolean"}},"required":["nodes","bHidden"]} as const;
const S_epic_set_node_isolated = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"sessionId":{"type":"string"},"rootSequencePath":{"type":"string"},"focusedSequencePath":{"type":"string"},"focusedTemplateId":{"type":"integer"},"nodePath":{"type":"string"},"nodeType":{"type":"string"},"label":{"type":"string"}}}},"bIsolated":{"type":"boolean"}},"required":["nodes","bIsolated"]} as const;
const S_epic_set_node_locked = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"type":{"type":"string"}}}},"locked":{"type":"boolean"}},"required":["nodes","locked"]} as const;
const S_epic_set_node_locked__1 = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"sessionId":{"type":"string"},"rootSequencePath":{"type":"string"},"focusedSequencePath":{"type":"string"},"focusedTemplateId":{"type":"integer"},"nodePath":{"type":"string"},"nodeType":{"type":"string"},"label":{"type":"string"}}}},"bLocked":{"type":"boolean"}},"required":["nodes","bLocked"]} as const;
const S_epic_set_node_muted = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"type":{"type":"string"}}}},"muted":{"type":"boolean"}},"required":["nodes","muted"]} as const;
const S_epic_set_node_muted__1 = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"sessionId":{"type":"string"},"rootSequencePath":{"type":"string"},"focusedSequencePath":{"type":"string"},"focusedTemplateId":{"type":"integer"},"nodePath":{"type":"string"},"nodeType":{"type":"string"},"label":{"type":"string"}}}},"bMuted":{"type":"boolean"}},"required":["nodes","bMuted"]} as const;
const S_epic_set_node_pinned = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"type":{"type":"string"}}}},"pinned":{"type":"boolean"}},"required":["nodes","pinned"]} as const;
const S_epic_set_node_pinned__1 = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"sessionId":{"type":"string"},"rootSequencePath":{"type":"string"},"focusedSequencePath":{"type":"string"},"focusedTemplateId":{"type":"integer"},"nodePath":{"type":"string"},"nodeType":{"type":"string"},"label":{"type":"string"}}}},"bPinned":{"type":"boolean"}},"required":["nodes","bPinned"]} as const;
const S_epic_set_node_position = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}},"node":{"type":"object","properties":{"refPath":{}}},"position":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}}},"required":["control_rig","graph","node","position"]} as const;
const S_epic_set_node_position__deformer_graph_scripting_subsystem = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"nodePath":{"type":"string"},"position":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"}}}},"required":["deformer","nodePath","position"]} as const;
const S_epic_set_node_solo = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"type":{"type":"string"}}}},"soloed":{"type":"boolean"}},"required":["nodes","soloed"]} as const;
const S_epic_set_node_solo__1 = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"sessionId":{"type":"string"},"rootSequencePath":{"type":"string"},"focusedSequencePath":{"type":"string"},"focusedTemplateId":{"type":"integer"},"nodePath":{"type":"string"},"nodeType":{"type":"string"},"label":{"type":"string"}}}},"bSoloed":{"type":"boolean"}},"required":["nodes","bSoloed"]} as const;
const S_epic_set_nodes_expanded_by_path = {"properties":{"paths":{"type":"array","items":{"type":"string"}},"bExpanded":{"type":"boolean"}},"required":["paths","bExpanded"]} as const;
const S_epic_set_num_channels_used = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"inNumChannelsUsed":{"type":"integer"}},"required":["track","inNumChannelsUsed"]} as const;
const S_epic_set_num_channels_used__movie_scene_float_vector_track_extensions = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"inNumChannelsUsed":{"type":"integer"}},"required":["track","inNumChannelsUsed"]} as const;
const S_epic_set_origin_override_mask = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channels":{"type":"object","properties":{"bTranslationX":{"type":"boolean"},"bTranslationY":{"type":"boolean"},"bTranslationZ":{"type":"boolean"},"bRotationX":{"type":"boolean"},"bRotationY":{"type":"boolean"},"bRotationZ":{"type":"boolean"},"bScaleX":{"type":"boolean"},"bScaleY":{"type":"boolean"},"bScaleZ":{"type":"boolean"},"bWeight":{"type":"boolean"}}}},"required":["section","channels"]} as const;
const S_epic_set_outliner_selection = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"type":{"type":"string"}}}}},"required":["nodes"]} as const;
const S_epic_set_outliner_selection__1 = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"sessionId":{"type":"string"},"rootSequencePath":{"type":"string"},"focusedSequencePath":{"type":"string"},"focusedTemplateId":{"type":"integer"},"nodePath":{"type":"string"},"nodeType":{"type":"string"},"label":{"type":"string"}}}}},"required":["nodes"]} as const;
const S_epic_set_perlin_noise_params = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelName":{"type":"string"},"params":{"type":"object","properties":{"frequency":{"type":"number"},"amplitude":{"type":"number"},"offset":{"type":"number"}}}},"required":["section","channelName","params"]} as const;
const S_epic_set_pin_value = {"properties":{"control_rig":{"type":"object","properties":{"refPath":{}}},"graph":{"type":"object","properties":{"refPath":{}}},"pin":{"type":"object","properties":{"refPath":{}}},"value":{"type":"string"}},"required":["control_rig","graph","pin","value"]} as const;
const S_epic_set_pin_value__deformer_graph_scripting_subsystem = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"pinPath":{"type":"string"},"value":{"type":"string"}},"required":["deformer","pinPath","value"]} as const;
const S_epic_set_playback_context = {"properties":{"world":{"type":"object","properties":{"refPath":{}}},"client":{"type":"object","properties":{"refPath":{}}}},"required":["world"]} as const;
const S_epic_set_playback_end = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"endFrame":{"type":"integer"}},"required":["sequence","endFrame"]} as const;
const S_epic_set_playback_range = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"start_frame":{"type":"integer"},"end_frame":{"type":"integer"}},"required":["sequence","start_frame","end_frame"]} as const;
const S_epic_set_playback_range__movie_scene_sequence_extensions = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"startFrame":{"type":"integer"},"endFrame":{"type":"integer"}},"required":["sequence","startFrame","endFrame"]} as const;
const S_epic_set_playback_range_locked = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"locked":{"type":"boolean"}},"required":["sequence","locked"]} as const;
const S_epic_set_playback_range_locked__movie_scene_sequence_extensions = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"bInLocked":{"type":"boolean"}},"required":["sequence","bInLocked"]} as const;
const S_epic_set_playback_speed = {"properties":{"speed":{"type":"number"}},"required":["speed"]} as const;
const S_epic_set_playback_speed__level_sequence_editor_blueprint_library = {"properties":{"newPlaybackSpeed":{"type":"number"}},"required":["newPlaybackSpeed"]} as const;
const S_epic_set_playback_start = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"startFrame":{"type":"integer"}},"required":["sequence","startFrame"]} as const;
const S_epic_set_playhead_frame = {"properties":{"frame":{"type":"integer"}},"required":["frame"]} as const;
const S_epic_set_playhead_frame_async = {"properties":{"frame":{"type":"integer"}},"required":["frame"]} as const;
const S_epic_set_position = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"},"set_key":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_set_post_roll_frames = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"inPostRollFrames":{"type":"integer"}},"required":["section","inPostRollFrames"]} as const;
const S_epic_set_pre_roll_frames = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"inPreRollFrames":{"type":"integer"}},"required":["section","inPreRollFrames"]} as const;
const S_epic_set_preview_language = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"culture":{"type":"string"}},"required":["sequence","culture"]} as const;
const S_epic_set_preview_mesh = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"previewMesh":{"type":"object","properties":{"refPath":{}}}},"required":["deformer","previewMesh"]} as const;
const S_epic_set_priority_order = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"order":{"type":"integer"}},"required":["sequence","control_rig_asset_path","order"]} as const;
const S_epic_set_property_name_and_path = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"display_name":{"type":"string"},"property_path":{"type":"string"}},"required":["track","display_name","property_path"]} as const;
const S_epic_set_property_name_and_path__movie_scene_property_track_extensions = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"inPropertyName":{"type":"string"},"inPropertyPath":{"type":"string"}},"required":["track","inPropertyName","inPropertyPath"]} as const;
const S_epic_set_range = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"startFrame":{"type":"integer"},"endFrame":{"type":"integer"}},"required":["section","startFrame","endFrame"]} as const;
const S_epic_set_resource_component_binding = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"resourceName":{"type":"string"},"bindingName":{"type":"string"}},"required":["deformer","resourceName","bindingName"]} as const;
const S_epic_set_resource_data_domain = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"resourceName":{"type":"string"},"dataDomain":{"type":"string"}},"required":["deformer","resourceName","dataDomain"]} as const;
const S_epic_set_resource_data_type = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"resourceName":{"type":"string"},"dataType":{"type":"string"}},"required":["deformer","resourceName","dataType"]} as const;
const S_epic_set_rotator = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"},"set_key":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_set_safe_frame_size = {"properties":{"frameName":{"type":"string"},"sizePercentage":{"type":"integer"}},"required":["frameName","sizePercentage"]} as const;
const S_epic_set_scale = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"},"set_key":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_set_section_animation = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"anim_sequence_path":{"type":"string"}},"required":["section","anim_sequence_path"]} as const;
const S_epic_set_section_blend_type = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"blend_type":{"type":"string"}},"required":["section","blend_type"]} as const;
const S_epic_set_section_completion_mode = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"completion_mode":{"type":"string"}},"required":["section","completion_mode"]} as const;
const S_epic_set_section_condition = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"condition_class":{"type":"string"}},"required":["section","condition_class"]} as const;
const S_epic_set_section_condition__1 = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"conditionClass":{"type":"object","properties":{"refPath":{}}}},"required":["section","conditionClass"]} as const;
const S_epic_set_section_ease_in = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"duration":{"type":"integer"}},"required":["section","duration"]} as const;
const S_epic_set_section_ease_out = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"duration":{"type":"integer"}},"required":["section","duration"]} as const;
const S_epic_set_section_end_bounded = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"bounded":{"type":"boolean"}},"required":["section","bounded"]} as const;
const S_epic_set_section_play_rate = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"playRate":{"type":"number"}},"required":["section","playRate"]} as const;
const S_epic_set_section_post_roll_frames = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"frames":{"type":"integer"}},"required":["section","frames"]} as const;
const S_epic_set_section_pre_roll_frames = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"frames":{"type":"integer"}},"required":["section","frames"]} as const;
const S_epic_set_section_range = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"start_frame":{"type":"integer"},"end_frame":{"type":"integer"}},"required":["section","start_frame","end_frame"]} as const;
const S_epic_set_section_row_index = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"rowIndex":{"type":"integer"}},"required":["section","rowIndex"]} as const;
const S_epic_set_section_start_bounded = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"bounded":{"type":"boolean"}},"required":["section","bounded"]} as const;
const S_epic_set_selection_range = {"properties":{"start_frame":{"type":"integer"},"end_frame":{"type":"integer"}},"required":["start_frame","end_frame"]} as const;
const S_epic_set_selection_range_end = {"properties":{"newFrame":{"type":"integer"}},"required":["newFrame"]} as const;
const S_epic_set_selection_range_start = {"properties":{"newFrame":{"type":"integer"}},"required":["newFrame"]} as const;
const S_epic_set_selection_set_actor_active = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}},"bActive":{"type":"boolean"}},"required":["actor","bActive"]} as const;
const S_epic_set_selection_set_properties = {"properties":{"id":{"type":"string"},"properties":{"type":"object","properties":{"name":{"type":"string"},"color":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}},"row":{"type":"integer"}}}},"required":["id","properties"]} as const;
const S_epic_set_selection_set_visibility = {"properties":{"id":{"type":"string"},"visibility":{"type":"string","enum":["Show","Hide","Isolate","ShowAll"]},"bMirror":{"type":"boolean"}},"required":["id","visibility"]} as const;
const S_epic_set_selection_sets_use_viewport_selection = {"properties":{"bUseViewportSelection":{"type":"boolean"}},"required":["bUseViewportSelection"]} as const;
const S_epic_set_sequence_flag = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"flag":{"type":"string","enum":["Volatile","BlockingEvaluation","DynamicWeighting","LoopCuts"]},"bEnabled":{"type":"boolean"}},"required":["sequence","flag","bEnabled"]} as const;
const S_epic_set_sequence_locked = {"properties":{"lock":{"type":"boolean"}},"required":["lock"]} as const;
const S_epic_set_show_curve = {"properties":{"channel":{"type":"object","properties":{"section":{"type":"object","properties":{"refPath":{}}},"channelType":{"type":"string","enum":["Float","Double"]},"channelIndex":{"type":"integer"},"channelName":{"type":"string"}}},"bInShowCurve":{"type":"boolean"}},"required":["channel","bInShowCurve"]} as const;
const S_epic_set_skeletal_animation = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"animationAssetPath":{"type":"string"}},"required":["section","animationAssetPath"]} as const;
const S_epic_set_socket_transform = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"socket_name":{"type":"string"},"transform":{"type":"object","properties":{"location":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}}},"required":["mesh","socket_name","transform"]} as const;
const S_epic_set_socket_transform__1 = {"properties":{"mesh":{"type":"object","properties":{"refPath":{}}},"socketName":{"type":"string"},"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}},"required":["mesh","socketName"]} as const;
const S_epic_set_sorting_order = {"properties":{"folder":{"type":"object","properties":{"refPath":{}}},"sortingOrder":{"type":"integer"}},"required":["folder","sortingOrder"]} as const;
const S_epic_set_space = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"space_type":{"type":"string"},"frame":{"type":"integer"},"space_target":{"type":"string"}},"required":["sequence","control_rig_asset_path","control_name","space_type","frame","space_target"]} as const;
const S_epic_set_start_frame_bounded = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"bIsBounded":{"type":"boolean"}},"required":["section","bIsBounded"]} as const;
const S_epic_set_thumbnail_reference_offset = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"offsetSeconds":{"type":"number"}},"required":["section","offsetSeconds"]} as const;
const S_epic_set_tick_resolution = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"numerator":{"type":"integer"},"denominator":{"type":"integer"}},"required":["sequence","numerator"]} as const;
const S_epic_set_tick_resolution__movie_scene_sequence_extensions = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"tickResolution":{"type":"object","properties":{"numerator":{"type":"integer"},"denominator":{"type":"integer"}}}},"required":["sequence","tickResolution"]} as const;
const S_epic_set_time_posing_active = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"bActive":{"type":"boolean"}},"required":["levelSequence","bActive"]} as const;
const S_epic_set_time_posing_range = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"inRange":{"type":"object","properties":{"farLeft":{"type":"object","properties":{"value":{"type":"integer"}}},"middleLeft":{"type":"object","properties":{"value":{"type":"integer"}}},"middleRight":{"type":"object","properties":{"value":{"type":"integer"}}},"farRight":{"type":"object","properties":{"value":{"type":"integer"}}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","inRange"]} as const;
const S_epic_set_time_posing_settings = {"properties":{"inSettings":{"type":"object","properties":{"centerWidth":{"type":"number"},"falloffWidth":{"type":"number"},"falloff":{"type":"string","enum":["Linear","EaseIn","EaseOut","EaseInEaseOut"]},"linkMode":{"type":"string","enum":["CurrentFrame","AroundRange","Fixed"]},"bSetKeyOnBoundaries":{"type":"boolean"},"centerColor":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}},"falloffColor":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}}}}},"required":["inSettings"]} as const;
const S_epic_set_track_condition = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"condition_class":{"type":"string"}},"required":["track","condition_class"]} as const;
const S_epic_set_track_condition__1 = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"conditionClass":{"type":"object","properties":{"refPath":{}}}},"required":["track","conditionClass"]} as const;
const S_epic_set_track_display_name = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["track","name"]} as const;
const S_epic_set_track_filter_active = {"properties":{"name":{"type":"string"},"active":{"type":"boolean"}},"required":["name","active"]} as const;
const S_epic_set_track_filter_active__level_sequence_editor_blueprint_library = {"properties":{"trackFilterName":{"type":"string"},"bActive":{"type":"boolean"}},"required":["trackFilterName","bActive"]} as const;
const S_epic_set_track_filter_text = {"properties":{"filterText":{"type":"string"}},"required":["filterText"]} as const;
const S_epic_set_track_row_condition = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"row_index":{"type":"integer"},"condition_class":{"type":"string"}},"required":["track","row_index","condition_class"]} as const;
const S_epic_set_track_row_condition__1 = {"properties":{"track":{"type":"object","properties":{"refPath":{}}},"rowIndex":{"type":"integer"},"conditionClass":{"type":"object","properties":{"refPath":{}}}},"required":["track","rowIndex","conditionClass"]} as const;
const S_epic_set_transform = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"location_x":{"type":"number"},"location_y":{"type":"number"},"location_z":{"type":"number"},"rotation_pitch":{"type":"number"},"rotation_yaw":{"type":"number"},"rotation_roll":{"type":"number"},"set_key":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_set_transform_mask = {"properties":{"section":{"type":"object","properties":{"refPath":{}}},"channels":{"type":"object","properties":{"bTranslationX":{"type":"boolean"},"bTranslationY":{"type":"boolean"},"bTranslationZ":{"type":"boolean"},"bRotationX":{"type":"boolean"},"bRotationY":{"type":"boolean"},"bRotationZ":{"type":"boolean"},"bScaleX":{"type":"boolean"},"bScaleY":{"type":"boolean"},"bScaleZ":{"type":"boolean"},"bWeight":{"type":"boolean"}}}},"required":["section","channels"]} as const;
const S_epic_set_variable_data_type = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"},"dataType":{"type":"string"}},"required":["deformer","variableName","dataType"]} as const;
const S_epic_set_variable_default_value = {"properties":{"deformer":{"type":"object","properties":{"refPath":{}}},"variableName":{"type":"string"},"defaultValue":{"type":"string"}},"required":["deformer","variableName","defaultValue"]} as const;
const S_epic_set_vector2d = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"x":{"type":"number"},"y":{"type":"number"},"set_key":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_set_view_range = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"start_seconds":{"type":"number"},"end_seconds":{"type":"number"}},"required":["sequence","start_seconds","end_seconds"]} as const;
const S_epic_set_view_range__movie_scene_sequence_extensions = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"startTimeInSeconds":{"type":"number"},"endTimeInSeconds":{"type":"number"}},"required":["sequence","startTimeInSeconds","endTimeInSeconds"]} as const;
const S_epic_set_work_range = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"start_seconds":{"type":"number"},"end_seconds":{"type":"number"}},"required":["sequence","start_seconds","end_seconds"]} as const;
const S_epic_set_work_range__movie_scene_sequence_extensions = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"startTimeInSeconds":{"type":"number"},"endTimeInSeconds":{"type":"number"}},"required":["sequence","startTimeInSeconds","endTimeInSeconds"]} as const;
const S_epic_set_world_transform = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"location_x":{"type":"number"},"location_y":{"type":"number"},"location_z":{"type":"number"},"rotation_pitch":{"type":"number"},"rotation_yaw":{"type":"number"},"rotation_roll":{"type":"number"},"set_key":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_name","frame"]} as const;
const S_epic_show_all_controls = {"properties":{"section":{"type":"object","properties":{"refPath":{}}}},"required":["section"]} as const;
const S_epic_show_all_controls__control_rig_sequencer_editor_library = {"properties":{"inSection":{"type":"object","properties":{"refPath":{}}}},"required":["inSection"]} as const;
const S_epic_show_all_nodes = {"properties":{}} as const;
const S_epic_show_all_selection_set_actor_controls = {"properties":{"actor":{"type":"object","properties":{"refPath":{}}}},"required":["actor"]} as const;
const S_epic_show_curve = {"properties":{"channel":{"type":"object","properties":{"channelName":{"type":"string"},"section":{"type":"object","properties":{"refPath":{}}}}},"show":{"type":"boolean"}},"required":["channel","show"]} as const;
const S_epic_show_curve__level_sequence_editor_subsystem = {"properties":{"channel":{"type":"object","properties":{"channelName":{"type":"string"},"section":{"type":"object","properties":{"refPath":{}}}}},"bShowCurve":{"type":"boolean"}},"required":["channel","bShowCurve"]} as const;
const S_epic_smart_reduce_section = {"properties":{"movieSceneSection":{"type":"object","properties":{"refPath":{}}},"reduceParams":{"type":"object","properties":{"tolerancePercentage":{"type":"number"},"sampleRate":{"type":"object","properties":{"numerator":{"type":"integer"},"denominator":{"type":"integer"}}},"bDeleteStaticChannels":{"type":"boolean"},"bOnlyReduceStaticChannels":{"type":"boolean"}}}},"required":["movieSceneSection","reduceParams"]} as const;
const S_epic_snap_control_rig = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"control_names":{"type":"array","items":{"type":"string"}},"target_actor_name":{"type":"string"},"start_frame":{"type":"integer"},"end_frame":{"type":"integer"},"keep_offset":{"type":"boolean"},"snap_position":{"type":"boolean"},"snap_rotation":{"type":"boolean"},"snap_scale":{"type":"boolean"}},"required":["sequence","control_rig_asset_path","control_names","target_actor_name","start_frame","end_frame"]} as const;
const S_epic_snap_control_rig__control_rig_sequencer_editor_library = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"startFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"endFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"childrenToSnap":{"type":"object","properties":{"actors":{"type":"array","items":{"type":"object","properties":{"actor":{"type":"object","properties":{"refPath":{}}},"component":{"type":"object","properties":{"refPath":{}}},"socketName":{"type":"string"}}}},"controlRigs":{"type":"array","items":{"type":"object","properties":{"controlRig":{"type":"object","properties":{"refPath":{}}},"controlNames":{"type":"array","items":{"type":"string"}}}}}}},"parentToSnap":{"type":"object","properties":{"actors":{"type":"array","items":{"type":"object","properties":{"actor":{"type":"object","properties":{"refPath":{}}},"component":{"type":"object","properties":{"refPath":{}}},"socketName":{"type":"string"}}}},"controlRigs":{"type":"array","items":{"type":"object","properties":{"controlRig":{"type":"object","properties":{"refPath":{}}},"controlNames":{"type":"array","items":{"type":"string"}}}}}}},"snapSettings":{"type":"object","properties":{"refPath":{}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","startFrame","endFrame","childrenToSnap","parentToSnap","snapSettings"]} as const;
const S_epic_snap_control_rig_async = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"startFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"endFrame":{"type":"object","properties":{"value":{"type":"integer"}}},"childrenToSnap":{"type":"object","properties":{"actors":{"type":"array","items":{"type":"object","properties":{"actor":{"type":"object","properties":{"refPath":{}}},"component":{"type":"object","properties":{"refPath":{}}},"socketName":{"type":"string"}}}},"controlRigs":{"type":"array","items":{"type":"object","properties":{"controlRig":{"type":"object","properties":{"refPath":{}}},"controlNames":{"type":"array","items":{"type":"string"}}}}}}},"parentToSnap":{"type":"object","properties":{"actors":{"type":"array","items":{"type":"object","properties":{"actor":{"type":"object","properties":{"refPath":{}}},"component":{"type":"object","properties":{"refPath":{}}},"socketName":{"type":"string"}}}},"controlRigs":{"type":"array","items":{"type":"object","properties":{"controlRig":{"type":"object","properties":{"refPath":{}}},"controlNames":{"type":"array","items":{"type":"string"}}}}}}},"snapSettings":{"type":"object","properties":{"refPath":{}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","startFrame","endFrame","childrenToSnap","parentToSnap","snapSettings"]} as const;
const S_epic_sort_all_curve_editor_nodes = {"properties":{}} as const;
const S_epic_sort_all_nodes = {"properties":{}} as const;
const S_epic_sort_nodes_by_start_time = {"properties":{"nodes":{"type":"array","items":{"type":"object","properties":{"sessionId":{"type":"string"},"rootSequencePath":{"type":"string"},"focusedSequencePath":{"type":"string"},"focusedTemplateId":{"type":"integer"},"nodePath":{"type":"string"},"nodeType":{"type":"string"},"label":{"type":"string"}}}},"bDescending":{"type":"boolean"}},"required":["nodes"]} as const;
const S_epic_space_compensate = {"properties":{"inControlRig":{"type":"object","properties":{"refPath":{}}},"inTime":{"type":"object","properties":{"value":{"type":"integer"}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["inControlRig","inTime"]} as const;
const S_epic_space_compensate_all = {"properties":{"inControlRig":{"type":"object","properties":{"refPath":{}}}},"required":["inControlRig"]} as const;
const S_epic_tag_binding = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"tag_name":{"type":"string"}},"required":["binding","tag_name"]} as const;
const S_epic_tag_binding__movie_scene_binding_tag_extensions = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"tagName":{"type":"string"}},"required":["binding","tagName"]} as const;
const S_epic_time_pose_actor_world_transform = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"actor":{"type":"object","properties":{"refPath":{}}},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"worldTransform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","actor","frame","worldTransform"]} as const;
const S_epic_time_pose_control_rig_world_transform = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlName":{"type":"string"},"frame":{"type":"object","properties":{"value":{"type":"integer"}}},"worldTransform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlName","frame","worldTransform"]} as const;
const S_epic_transform_actors_about_pivot = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"actors":{"type":"array","items":{"type":"object","properties":{"refPath":{}}}},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"delta":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"pivotTransform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"bKeepOrientation":{"type":"boolean"},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","actors","frames","delta","pivotTransform"]} as const;
const S_epic_transform_controls_about_pivot = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlNames":{"type":"array","items":{"type":"string"}},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"delta":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"pivotObject":{"type":"object","properties":{"actors":{"type":"array","items":{"type":"object","properties":{"actor":{"type":"object","properties":{"refPath":{}}},"component":{"type":"object","properties":{"refPath":{}}},"socketName":{"type":"string"}}}},"controlRigs":{"type":"array","items":{"type":"object","properties":{"controlRig":{"type":"object","properties":{"refPath":{}}},"controlNames":{"type":"array","items":{"type":"string"}}}}}}},"pivotTransform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlNames","frames","delta","pivotObject","pivotTransform"]} as const;
const S_epic_transform_controls_about_pivot_async = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"controlNames":{"type":"array","items":{"type":"string"}},"frames":{"type":"array","items":{"type":"object","properties":{"value":{"type":"integer"}}}},"delta":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"pivotObject":{"type":"object","properties":{"actors":{"type":"array","items":{"type":"object","properties":{"actor":{"type":"object","properties":{"refPath":{}}},"component":{"type":"object","properties":{"refPath":{}}},"socketName":{"type":"string"}}}},"controlRigs":{"type":"array","items":{"type":"object","properties":{"controlRig":{"type":"object","properties":{"refPath":{}}},"controlNames":{"type":"array","items":{"type":"string"}}}}}}},"pivotTransform":{"type":"object","properties":{"translation":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}},"rotation":{"type":"object","properties":{"pitch":{"type":"number"},"yaw":{"type":"number"},"roll":{"type":"number"}}},"scale3D":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}}},"timeUnit":{"type":"string","enum":["DisplayRate","TickResolution"]}},"required":["levelSequence","controlRig","controlNames","frames","delta","pivotObject","pivotTransform"]} as const;
const S_epic_tween_control_rig = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"control_rig_asset_path":{"type":"string"},"tween_value":{"type":"number"}},"required":["sequence","control_rig_asset_path","tween_value"]} as const;
const S_epic_tween_control_rig__control_rig_sequencer_editor_library = {"properties":{"levelSequence":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"tweenValue":{"type":"number"}},"required":["levelSequence","controlRig","tweenValue"]} as const;
const S_epic_ungroup_section = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"section":{"type":"object","properties":{"refPath":{}}}},"required":["sequence","section"]} as const;
const S_epic_unlink_anim_sequence = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"animSequence":{"type":"object","properties":{"refPath":{}}}},"required":["binding","animSequence"]} as const;
const S_epic_unpin_all_motion_trails = {"properties":{}} as const;
const S_epic_unpin_motion_trail = {"properties":{"trailId":{"type":"string"}},"required":["trailId"]} as const;
const S_epic_unpin_selected_motion_trails = {"properties":{}} as const;
const S_epic_untag_binding = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"tag_name":{"type":"string"}},"required":["binding","tag_name"]} as const;
const S_epic_untag_binding__movie_scene_binding_tag_extensions = {"properties":{"binding":{"type":"object","properties":{"bindingId":{"type":"string"},"sequence":{"type":"object","properties":{"refPath":{}}}}},"tagName":{"type":"string"}},"required":["binding","tagName"]} as const;
const S_epic_update_control_rig_pose_asset = {"properties":{"poseAsset":{"type":"object","properties":{"refPath":{}}},"controlRig":{"type":"object","properties":{"refPath":{}}},"bUseAllControls":{"type":"boolean"}},"required":["poseAsset","controlRig"]} as const;
const S_epic_validate_sequence = {"properties":{"sequence":{"type":"object","properties":{"refPath":{}}},"ruleNames":{"type":"array","items":{"type":"string"}}},"required":["sequence","ruleNames"]} as const;
const S_epic_wait_for_evaluation_async = {"properties":{"timeoutSeconds":{"type":"number"}}} as const;
const S_epic_zero_transforms = {"properties":{"selection_only":{"type":"boolean"},"include_channels":{"type":"boolean"}}} as const;
const S_epic_zero_transforms__control_rig_sequencer_editor_library = {"properties":{"bSelectionOnly":{"type":"boolean"},"bIncludeChannels":{"type":"boolean"}},"required":["bSelectionOnly","bIncludeChannels"]} as const;

/** 1004 wrapped engine tools routed to the `animation` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_actors: {
    epicSchema: S_epic_add_actors,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.add_actors", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Add actors from the level to the currently open sequence. Available on UE 5.8; absent from UE 5.9. Params: actors", "epic_call_tool"),
  },
  epic_add_actors__level_sequence_editor_subsystem: {
    epicSchema: S_epic_add_actors__level_sequence_editor_subsystem,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.AddActors", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Add existing actors to Sequencer. Tracks will be automatically added based on default track settings. Available on UE 5.9; absent from UE 5.8. Params: actors", "epic_call_tool"),
  },
  epic_add_actors_by_name: {
    epicSchema: S_epic_add_actors_by_name,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.add_actors_by_name", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Add actors to the sequence by their names in the level. Finds actors by label in the current level and adds them to the currently open sequence as possessable bindings. Available on UE 5.8; absent from UE 5.9. Params: actor_names", "epic_call_tool"),
  },
  epic_add_actors_by_name__level_sequence_editor_subsystem: {
    epicSchema: S_epic_add_actors_by_name__level_sequence_editor_subsystem,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.AddActorsByName", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Add all actors in the Sequencer playback world whose labels match the supplied names. Raises an error when no actors match. Available on UE 5.9; absent from UE 5.8. Params: actorNames", "epic_call_tool"),
  },
  epic_add_actors_to_binding: {
    epicSchema: S_epic_add_actors_to_binding,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.add_actors_to_binding", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Add actors to an existing binding. Available on UE 5.8; absent from UE 5.9. Params: actors, binding", "epic_call_tool"),
  },
  epic_add_actors_to_binding__level_sequence_editor_subsystem: {
    epicSchema: S_epic_add_actors_to_binding__level_sequence_editor_subsystem,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.AddActorsToBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Assigns the given actors to the binding Available on UE 5.9; absent from UE 5.8. Params: actors, objectBinding", "epic_call_tool"),
  },
  epic_add_anim_layer_from_selection: {
    epicSchema: S_epic_add_anim_layer_from_selection,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.AddAnimLayerFromSelection", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Add an anim layer containing the controls and objects selected in Sequencer, adding the Base layer first if the sequence has no layers. With nothing selected it adds an empty layer named \"Empty Layer <N>\". Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_add_auto_bake: {
    epicSchema: S_epic_add_auto_bake,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.AddAutoBake", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set up auto-bake recorders for the linked anim sequences on the given bindings. Equivalent to the \"Add AutoBake\" context-menu entry in the Sequencer UI. Available on UE 5.9; absent from UE 5.8. Params: inBindings", "epic_call_tool"),
  },
  epic_add_auto_bake_with_anim_sequences: {
    epicSchema: S_epic_add_auto_bake_with_anim_sequences,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.AddAutoBakeWithAnimSequences", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Same as AddAutoBake, but links pre-created anim sequences instead of prompting for new ones. No asset creation or export options dialogs are shown. Everything is validated first: if anything is invalid an error is raised and nothing is changed. That includes a binding that already has a linked anim sequence, a null, incompatible, repeated or already linked anim sequence, or an anim sequence the editor's asset reference filter refuses to link (as in UEFN). The anim sequences are only linked, not baked here: like AddAutoBake the level sequence is dirtied and the auto-bake runs when the linked track is toggled to, overwriting each anim sequence. The current export options are recorded on the link. To undo this for a binding, call DeleteLinkedAnimTrack, which removes the recorder, the link and the linked track. Available on UE 5.9; absent from UE 5.8. Params: inBindings, inAnimSequences", "epic_call_tool"),
  },
  epic_add_backward_solve_graph: {
    epicSchema: S_epic_add_backward_solve_graph,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.add_backward_solve_graph", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Create a backward solve graph with InverseExecution event. Params: control_rig, name?", "epic_call_tool"),
  },
  epic_add_binding_to_folder: {
    epicSchema: S_epic_add_binding_to_folder,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.add_binding_to_folder", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Add a binding into a folder for organization. Available on UE 5.8; absent from UE 5.9. Params: folder, binding", "epic_call_tool"),
  },
  epic_add_bone: {
    epicSchema: S_epic_add_bone,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.add_bone", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Add a bone to the Control Rig hierarchy. Params: control_rig, name, parent?, transform?", "epic_call_tool"),
  },
  epic_add_bool_key: {
    epicSchema: S_epic_add_bool_key,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.AddBoolKey", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Add a bool key to the unique matching bool channel, including an unnamed one. Replaces a key already at Frame. Available on UE 5.9; absent from UE 5.8. Params: section, channelName, frame, value", "epic_call_tool"),
  },
  epic_add_channel_override: {
    epicSchema: S_epic_add_channel_override,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.AddChannelOverride", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Override the named channel with a new override of OverrideClass, replacing any existing override. The channel's keys are kept. Available on UE 5.9; absent from UE 5.8. Params: section, channelName, overrideClass", "epic_call_tool"),
  },
  epic_add_child_folder: {
    epicSchema: S_epic_add_child_folder,
    epicTool: { toolset: "SequencerScripting.MovieSceneFolderExtensions", name: "SequencerScripting.MovieSceneFolderExtensions.AddChildFolder", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneFolderExtensions] Add a child folder to the target folder. The child must belong to the target folder's movie scene. Available on UE 5.9; absent from UE 5.8. Params: targetFolder, folderToAdd", "epic_call_tool"),
  },
  epic_add_child_object_binding: {
    epicSchema: S_epic_add_child_object_binding,
    epicTool: { toolset: "SequencerScripting.MovieSceneFolderExtensions", name: "SequencerScripting.MovieSceneFolderExtensions.AddChildObjectBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneFolderExtensions] Add a guid for an object binding to this folder Available on UE 5.9; absent from UE 5.8. Params: folder, inObjectBinding", "epic_call_tool"),
  },
  epic_add_child_track: {
    epicSchema: S_epic_add_child_track,
    epicTool: { toolset: "SequencerScripting.MovieSceneFolderExtensions", name: "SequencerScripting.MovieSceneFolderExtensions.AddChildTrack", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneFolderExtensions] Add a track to this folder. The track must belong to the folder's movie scene. Available on UE 5.9; absent from UE 5.8. Params: folder, inTrack", "epic_call_tool"),
  },
  epic_add_comment: {
    epicSchema: S_epic_add_comment,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.AddComment", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] AddComment Available on UE 5.9; absent from UE 5.8. Params: deformer, graphPath, comment, position, size, color", "epic_call_tool"),
  },
  epic_add_component_binding: {
    epicSchema: S_epic_add_component_binding,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.AddComponentBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Adds a binding to another component of the actor than the one the deformer is applied to. Link a \"ComponentBinding/<Name>\" node to data interface nodes to read from it. Available on UE 5.9; absent from UE 5.8. Params: deformer, bindingName, componentSource", "epic_call_tool"),
  },
  epic_add_constraint: {
    epicSchema: S_epic_add_constraint,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.AddConstraint", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Add a constraint. When a level sequence is open in Sequencer the constraint is keyed active at the current time, otherwise it is added as a static constraint. Raises a script error if a handle is null or invalid, the child is the parent, or the parent is constrained to the child, directly or through a chain of constraints. Available on UE 5.9; absent from UE 5.8. Params: world, inType, inChild, inParent, bMaintainOffset", "epic_call_tool"),
  },
  epic_add_constraint_between: {
    epicSchema: S_epic_add_constraint_between,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.AddConstraintBetween", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Add a constraint between two objects, creating their transformable handles; the constraint owns the handles. Each object is a Control Rig with a control name as its attachment, a scene component with an optional socket, or an actor (its root component, optional socket). When a level sequence is open in Sequencer the constraint is keyed active at CreationTime, as the Constraints panel's creation options do; otherwise it is added as a static constraint. Raises a script error if an object is null or not transformable, a control or socket does not exist, a control's type cannot be constrained (Bool, Float, ScaleFloat, Integer), the child is the parent, the parent is constrained to the child, directly or through a chain of constraints, or the constraint cannot be added. Available on UE 5.9; absent from UE 5.8. Params: world, type, childObject, childAttachment, parentObject, parentAttachment, bMaintainOffset, creationTime?", "epic_call_tool"),
  },
  epic_add_control: {
    epicSchema: S_epic_add_control,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.add_control", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Add a control to the hierarchy. Params: control_rig, name, parent?, settings?", "epic_call_tool"),
  },
  epic_add_control_rig_available_space: {
    epicSchema: S_epic_add_control_rig_available_space,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.AddControlRigAvailableSpace", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Add a space to the end of a control's space picker list, as the level editor's Add Space does. Raises a script error if the Control Rig is null, the control or space does not exist in the rig, or the space is world or the control's default parent. Available on UE 5.9; absent from UE 5.8. Params: controlRig, controlName, space", "epic_call_tool"),
  },
  epic_add_double_key: {
    epicSchema: S_epic_add_double_key,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.AddDoubleKey", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Add a double key and return its identity. InTime is in display-rate frames unless TimeUnit is TickResolution. SubFrame is clamped to [0, 1) and ignored for tick-resolution frames. Available on UE 5.9; absent from UE 5.8. Params: channel, inTime, newValue, subFrame?, timeUnit?, inInterpolation?", "epic_call_tool"),
  },
  epic_add_element: {
    epicSchema: S_epic_add_element,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.add_element", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Add a bone or null element to the Control Rig hierarchy. Params: control_rig, name, element_type, parent?, transform?", "epic_call_tool"),
  },
  epic_add_empty_binding: {
    epicSchema: S_epic_add_empty_binding,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.AddEmptyBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Add a binding that resolves to nothing, to be assigned an object later, as Sequencer's New Empty Binding does. An empty Name gives \"EmptyBinding\"; either way a number is appended if a binding already has the name. The binding holds one empty locator, except in sequences without a binding reference table (such as Day Sequences), where it is a plain possessable with no locator. Available on UE 5.9; absent from UE 5.8. Params: sequence, name?", "epic_call_tool"),
  },
  epic_add_event_graph: {
    epicSchema: S_epic_add_event_graph,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.add_event_graph", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Create a new graph with the specified event type. Params: control_rig, event_type, name?", "epic_call_tool"),
  },
  epic_add_event_key: {
    epicSchema: S_epic_add_event_key,
    epicTool: { toolset: "AnimationAssistantToolset.SequencerDirectorToolset", name: "AnimationAssistantToolset.SequencerDirectorToolset.AddEventKey", engines: ["5.9"] },
    ...bp("mutate", "[Epic AnimationAssistantToolset.SequencerDirectorToolset] Author a complete event key end-to-end on an Event track section. Equivalent of the three-step manual flow: 1. SequencerToolsFunctionLibrary::CreateQuickBinding to create an endpoint on the Director Blueprint that calls FunctionName on BoundObject (or its class). 2. SequencerToolsFunctionLibrary::CreateEvent to wrap the endpoint in a FMovieSceneEvent bound to the event section. 3. Find the event channel on the section and add a key at Frame carrying the FMovieSceneEvent value. Available on UE 5.9; absent from UE 5.8. Params: sequence, eventSection, boundObject, functionName, frame, bCallInEditor?", "epic_call_tool"),
  },
  epic_add_event_node: {
    epicSchema: S_epic_add_event_node,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.add_event_node", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Add an event node to the graph. Params: control_rig, graph, event_type, position?", "epic_call_tool"),
  },
  epic_add_event_repeater_section: {
    epicSchema: S_epic_add_event_repeater_section,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.add_event_repeater_section", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Add an event repeater section to an event track. Available on UE 5.8; absent from UE 5.9. Params: track", "epic_call_tool"),
  },
  epic_add_event_repeater_section__movie_scene_event_track_extensions: {
    epicSchema: S_epic_add_event_repeater_section__movie_scene_event_track_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneEventTrackExtensions", name: "SequencerScripting.MovieSceneEventTrackExtensions.AddEventRepeaterSection", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneEventTrackExtensions] Create a new event repeater section for the given track Available on UE 5.9; absent from UE 5.8. Params: inTrack", "epic_call_tool"),
  },
  epic_add_event_trigger_section: {
    epicSchema: S_epic_add_event_trigger_section,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.add_event_trigger_section", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Add an event trigger section to an event track. Available on UE 5.8; absent from UE 5.9. Params: track", "epic_call_tool"),
  },
  epic_add_event_trigger_section__movie_scene_event_track_extensions: {
    epicSchema: S_epic_add_event_trigger_section__movie_scene_event_track_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneEventTrackExtensions", name: "SequencerScripting.MovieSceneEventTrackExtensions.AddEventTriggerSection", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneEventTrackExtensions] Create a new event trigger section for the given track Available on UE 5.9; absent from UE 5.8. Params: inTrack", "epic_call_tool"),
  },
  epic_add_float_key: {
    epicSchema: S_epic_add_float_key,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.AddFloatKey", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Add a float key and return its identity. InTime is in display-rate frames unless TimeUnit is TickResolution. SubFrame is clamped to [0, 1) and ignored for tick-resolution frames. Available on UE 5.9; absent from UE 5.8. Params: channel, inTime, newValue, subFrame?, timeUnit?, inInterpolation?", "epic_call_tool"),
  },
  epic_add_graph: {
    epicSchema: S_epic_add_graph,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.add_graph", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Create a new empty graph in the Control Rig. Params: control_rig, name", "epic_call_tool"),
  },
  epic_add_graph__deformer_graph_scripting_subsystem: {
    epicSchema: S_epic_add_graph__deformer_graph_scripting_subsystem,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.AddGraph", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] AddGraph Available on UE 5.9; absent from UE 5.8. Params: deformer, graphType, graphName", "epic_call_tool"),
  },
  epic_add_graph_binding: {
    epicSchema: S_epic_add_graph_binding,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.AddGraphBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Adds a binding to the signature of a sub-graph: a pin on its entry node (input) or return node (output), and on the node that runs it. Available on UE 5.9; absent from UE 5.8. Params: deformer, graphPath, direction, bindingName, dataType, dataDomain", "epic_call_tool"),
  },
  epic_add_integer_key: {
    epicSchema: S_epic_add_integer_key,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.AddIntegerKey", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Add an integer key to the unique matching integer channel, including an unnamed one. Replaces a key already at Frame. Available on UE 5.9; absent from UE 5.8. Params: section, channelName, frame, value", "epic_call_tool"),
  },
  epic_add_interaction_graph: {
    epicSchema: S_epic_add_interaction_graph,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.add_interaction_graph", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Create an interaction graph with InteractionExecution event. Params: control_rig, name?", "epic_call_tool"),
  },
  epic_add_kernel: {
    epicSchema: S_epic_add_kernel,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.AddKernel", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Adds a custom compute kernel with no bindings and an empty source. Available on UE 5.9; absent from UE 5.8. Params: deformer, graphPath, kernelName, executionDomain, position", "epic_call_tool"),
  },
  epic_add_kernel_binding: {
    epicSchema: S_epic_add_kernel_binding,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.AddKernelBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Adds a binding and its pin. An input named \"Position\" gives the source a ReadPosition function, an output a WritePosition function; an input in group \"Source\" a Source::ReadPosition function. Available on UE 5.9; absent from UE 5.8. Params: deformer, nodePath, direction, bindingName, dataType, dataDomain, group?", "epic_call_tool"),
  },
  epic_add_kernel_input_group: {
    epicSchema: S_epic_add_kernel_input_group,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.AddKernelInputGroup", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Adds a secondary input group: inputs read from another component binding than the kernel's primary group, declared in the HLSL namespace GroupName. Available on UE 5.9; absent from UE 5.8. Params: deformer, nodePath, groupName", "epic_call_tool"),
  },
  epic_add_key_bool: {
    epicSchema: S_epic_add_key_bool,
    epicTool: { toolset: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools", name: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.add_key_bool", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Add a bool key to a channel on a section. Available on UE 5.8; absent from UE 5.9. Params: section, channel_name, frame, value", "epic_call_tool"),
  },
  epic_add_key_float: {
    epicSchema: S_epic_add_key_float,
    epicTool: { toolset: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools", name: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.add_key_float", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Add a float key to a channel on a section. Available on UE 5.8; absent from UE 5.9. Params: section, channel_name, frame, value, interpolation", "epic_call_tool"),
  },
  epic_add_key_integer: {
    epicSchema: S_epic_add_key_integer,
    epicTool: { toolset: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools", name: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.add_key_integer", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Add an integer key to a channel on a section. Available on UE 5.8; absent from UE 5.9. Params: section, channel_name, frame, value", "epic_call_tool"),
  },
  epic_add_key_string: {
    epicSchema: S_epic_add_key_string,
    epicTool: { toolset: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools", name: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.add_key_string", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Add a string key to a channel on a section. Available on UE 5.8; absent from UE 5.9. Params: section, channel_name, frame, value", "epic_call_tool"),
  },
  epic_add_layer_from_selection: {
    epicSchema: S_epic_add_layer_from_selection,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.add_layer_from_selection", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Add an animation layer from the currently selected objects in Sequencer. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_add_link: {
    epicSchema: S_epic_add_link,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.AddLink", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Links an output pin to an input pin, replacing the input pin's existing link if any. Available on UE 5.9; absent from UE 5.8. Params: deformer, outputPinPath, inputPinPath", "epic_call_tool"),
  },
  epic_add_link_with_new_pin: {
    epicSchema: S_epic_add_link_with_new_pin,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.AddLinkWithNewPin", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Adds a pin to a node that supports it (a kernel adds a binding) with the name, type and data domain of SourcePinPath, and links the two. The new pin is an input if the source is an output, and the reverse. Available on UE 5.9; absent from UE 5.8. Params: deformer, sourcePinPath, targetNodePath", "epic_call_tool"),
  },
  epic_add_loop_binding: {
    epicSchema: S_epic_add_loop_binding,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.AddLoopBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Adds a value carried through the loop: an input and an output pin on both loop nodes. Available on UE 5.9; absent from UE 5.8. Params: deformer, nodePath, bindingName, dataType, dataDomain", "epic_call_tool"),
  },
  epic_add_marked_frame: {
    epicSchema: S_epic_add_marked_frame,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.add_marked_frame", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Add a marked frame (bookmark) to the sequence. Available on UE 5.8; absent from UE 5.9. Params: sequence, frame", "epic_call_tool"),
  },
  epic_add_marked_frame_range_to_sequence: {
    epicSchema: S_epic_add_marked_frame_range_to_sequence,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.AddMarkedFrameRangeToSequence", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Adds a mark or mark range at the given start and end frames with default settings (StartFrame == EndFrame is a single-frame mark, and EndFrame > StartFrame is a mark range) and returns its index in the sorted array of marks and mark ranges. A unique label will be generated when the given label is empty. Available on UE 5.9; absent from UE 5.8. Params: sequence, startFrame, endFrame, timeUnit?, label?", "epic_call_tool"),
  },
  epic_add_mirrored_controls_to_selection: {
    epicSchema: S_epic_add_mirrored_controls_to_selection,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.AddMirroredControlsToSelection", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Add the mirrored counterparts of the selected controls to the selection. Requires Animation Mode to be active. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_add_node: {
    epicSchema: S_epic_add_node,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.AddNode", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Adds a node. Data interface nodes read from the primary component binding unless a component binding node is linked to them. Use AddKernel for custom compute kernels and AddComment for comments. Available on UE 5.9; absent from UE 5.8. Params: deformer, graphPath, nodeType, position", "epic_call_tool"),
  },
  epic_add_nodes_to_node_group: {
    epicSchema: S_epic_add_nodes_to_node_group,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.AddNodesToNodeGroup", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Add folders, object bindings, tracks or track rows to a Sequencer Group of the focused sequence, as the Group Manager offers. A node already in the group stays in it once. Available on UE 5.9; absent from UE 5.8. Params: group, nodes", "epic_call_tool"),
  },
  epic_add_null: {
    epicSchema: S_epic_add_null,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.add_null", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Add a null (locator) to the Control Rig hierarchy. Params: control_rig, name, parent?, transform?", "epic_call_tool"),
  },
  epic_add_resource: {
    epicSchema: S_epic_add_resource,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.AddResource", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Adds a resource: a GPU buffer kernels write and read, kept between frames, accessed with \"Resource/<Name>\", \"ResourceGet/<Name>\" and \"ResourceSet/<Name>\" nodes. Available on UE 5.9; absent from UE 5.8. Params: deformer, resourceName, dataType, dataDomain", "epic_call_tool"),
  },
  epic_add_root_folder: {
    epicSchema: S_epic_add_root_folder,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.add_root_folder", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Create a new root-level folder in the sequence. Available on UE 5.8; absent from UE 5.9. Params: sequence, name", "epic_call_tool"),
  },
  epic_add_root_folder_to_sequence: {
    epicSchema: S_epic_add_root_folder_to_sequence,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.AddRootFolderToSequence", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Add a root folder to the given sequence Available on UE 5.9; absent from UE 5.8. Params: sequence, newFolderName", "epic_call_tool"),
  },
  epic_add_section: {
    epicSchema: S_epic_add_section,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.add_section", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Add a new section to a track. Available on UE 5.8; absent from UE 5.9. Params: track", "epic_call_tool"),
  },
  epic_add_section__movie_scene_track_extensions: {
    epicSchema: S_epic_add_section__movie_scene_track_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneTrackExtensions", name: "SequencerScripting.MovieSceneTrackExtensions.AddSection", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneTrackExtensions] Add a new section to this track Available on UE 5.9; absent from UE 5.8. Params: track", "epic_call_tool"),
  },
  epic_add_section_of_class: {
    epicSchema: S_epic_add_section_of_class,
    epicTool: { toolset: "SequencerScripting.MovieSceneTrackExtensions", name: "SequencerScripting.MovieSceneTrackExtensions.AddSectionOfClass", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneTrackExtensions] Add a new section of SectionClass to this track and return it, as the track's add-section menu does for tracks that offer more than one section type. SectionClass must be a section type the track supports, which on material tracks includes the legacy parameter section. A Blueprint section type must be visible in this editor and allowed by its asset reference filter. Like AddSection, the new section keeps its default range. Available on UE 5.9; absent from UE 5.8. Params: track, sectionClass", "epic_call_tool"),
  },
  epic_add_selected_to_anim_layer: {
    epicSchema: S_epic_add_selected_to_anim_layer,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.AddSelectedToAnimLayer", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Add the controls and objects selected in Sequencer to an anim layer, as the panel's Add Selected does. Raises a script error if Index is out of range or nothing is selected. Available on UE 5.9; absent from UE 5.8. Params: index", "epic_call_tool"),
  },
  epic_add_selection_to_selection_set: {
    epicSchema: S_epic_add_selection_to_selection_set,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.AddSelectionToSelectionSet", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Add the controls and actors selected in Sequencer to a selection set. Raises a script error if no level sequence is open in Sequencer, the set does not exist, or nothing is selected. Available on UE 5.9; absent from UE 5.8. Params: id", "epic_call_tool"),
  },
  epic_add_socket: {
    epicSchema: S_epic_add_socket,
    epicTool: { toolset: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", name: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.add_socket", engines: ["5.8"], successors: [{ toolset: "EditorToolset.SkeletalMeshToolset", name: "EditorToolset.SkeletalMeshToolset.AddSocket", engines: ["5.9"], schema: S_epic_add_socket__1, rename: {"socket_name":"socketName","bone_name":"boneName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Adds a named socket to a skeletal mesh attached to a bone. Sockets are named attachment points used to attach weapons, accessories, or effects at a consistent position relative to a bone. Params: mesh, socket_name, bone_name. On UE 5.9 it calls EditorToolset.SkeletalMeshToolset.AddSocket (Params: mesh, socketName, boneName).", "epic_call_tool"),
  },
  epic_add_spawnable_from_class: {
    epicSchema: S_epic_add_spawnable_from_class,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.add_spawnable_from_class", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Create a spawnable binding from an actor class. Available on UE 5.8; absent from UE 5.9. Params: sequence, actor_class_path", "epic_call_tool"),
  },
  epic_add_spawnable_from_class__level_sequence_editor_subsystem: {
    epicSchema: S_epic_add_spawnable_from_class__level_sequence_editor_subsystem,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.AddSpawnableFromClass", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Add a new binding to this sequence that will spawn the specified class. Available on UE 5.9; absent from UE 5.8. Params: sequence, classToSpawn", "epic_call_tool"),
  },
  epic_add_spawnable_from_instance: {
    epicSchema: S_epic_add_spawnable_from_instance,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.add_spawnable_from_instance", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Create a spawnable binding from an existing object instance. Available on UE 5.8; absent from UE 5.9. Params: sequence, obj", "epic_call_tool"),
  },
  epic_add_spawnable_from_instance__level_sequence_editor_subsystem: {
    epicSchema: S_epic_add_spawnable_from_instance__level_sequence_editor_subsystem,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.AddSpawnableFromInstance", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Add a new binding to this sequence that will spawn the specified object. Available on UE 5.9; absent from UE 5.8. Params: sequence, objectToSpawn", "epic_call_tool"),
  },
  epic_add_string_key: {
    epicSchema: S_epic_add_string_key,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.AddStringKey", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Add a string key to the unique matching string channel, including an unnamed one. Replaces a key already at Frame. Available on UE 5.9; absent from UE 5.8. Params: section, channelName, frame, value", "epic_call_tool"),
  },
  epic_add_track: {
    epicSchema: S_epic_add_track,
    epicTool: { toolset: "SequencerScripting.MovieSceneBindingExtensions", name: "SequencerScripting.MovieSceneBindingExtensions.AddTrack", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneBindingExtensions] Add a new track to the specified binding Available on UE 5.9; absent from UE 5.8. Params: inBinding, trackType", "epic_call_tool"),
  },
  epic_add_track__movie_scene_sequence_extensions: {
    epicSchema: S_epic_add_track__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.AddTrack", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Add a sequence-level track of the specified concrete type. Camera-cut tracks occupy the sequence's dedicated camera-cut slot. Available on UE 5.9; absent from UE 5.8. Params: sequence, trackType", "epic_call_tool"),
  },
  epic_add_track_to_binding: {
    epicSchema: S_epic_add_track_to_binding,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.add_track_to_binding", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Add a track of the given type to a binding. Available on UE 5.8; absent from UE 5.9. Params: binding, track_type", "epic_call_tool"),
  },
  epic_add_track_to_folder: {
    epicSchema: S_epic_add_track_to_folder,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.add_track_to_folder", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Add a track into a folder for organization. Available on UE 5.8; absent from UE 5.9. Params: folder, track", "epic_call_tool"),
  },
  epic_add_track_to_sequence: {
    epicSchema: S_epic_add_track_to_sequence,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.add_track_to_sequence", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Add a sequence-level (master) track. Available on UE 5.8; absent from UE 5.9. Params: sequence, track_type", "epic_call_tool"),
  },
  epic_add_variable: {
    epicSchema: S_epic_add_variable,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.add_variable", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Add a member variable to the Control Rig. Params: control_rig, name, type_path, is_public?, is_read_only?, default_value?", "epic_call_tool"),
  },
  epic_add_variable__deformer_graph_scripting_subsystem: {
    epicSchema: S_epic_add_variable__deformer_graph_scripting_subsystem,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.AddVariable", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Adds a variable, set per deformer instance at runtime and read with \"Variable/<Name>\" nodes. Available on UE 5.9; absent from UE 5.8. Params: deformer, variableName, dataType, defaultValue", "epic_call_tool"),
  },
  epic_add_variable_node: {
    epicSchema: S_epic_add_variable_node,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.add_variable_node", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Create a variable getter or setter node. The variable must already exist (created via add_variable first). Params: control_rig, graph, variable_name, is_getter?, position?", "epic_call_tool"),
  },
  epic_adjustment_blend_anim_layer: {
    epicSchema: S_epic_adjustment_blend_anim_layer,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.AdjustmentBlendAnimLayer", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Adjustment blend an anim layer into the Base layer, as the Animation Layers panel's Adjustment Blend does. Available on UE 5.9; absent from UE 5.8. Params: index", "epic_call_tool"),
  },
  epic_are_marked_frames_shown_globally: {
    epicSchema: S_epic_are_marked_frames_shown_globally,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.AreMarkedFramesShownGlobally", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Whether the sequence's marks and mark ranges are also drawn in Sequencer while another sequence in the hierarchy is focused Available on UE 5.9; absent from UE 5.8. Params: sequence", "epic_call_tool"),
  },
  epic_are_motion_trails_visible: {
    epicSchema: S_epic_are_motion_trails_visible,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.AreMotionTrailsVisible", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] AreMotionTrailsVisible Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_assign_physics_asset: {
    epicSchema: S_epic_assign_physics_asset,
    epicTool: { toolset: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", name: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.assign_physics_asset", engines: ["5.8"], successors: [{ toolset: "EditorToolset.SkeletalMeshToolset", name: "EditorToolset.SkeletalMeshToolset.AssignPhysicsAsset", engines: ["5.9"], schema: S_epic_assign_physics_asset__1, rename: {"physics_asset":"physicsAsset"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Assigns a physics asset to a skeletal mesh. The physics asset must be compatible with the mesh's skeleton. Use this to swap physics assets or assign one to a mesh that has none. Params: mesh, physics_asset. On UE 5.9 it calls EditorToolset.SkeletalMeshToolset.AssignPhysicsAsset (Params: mesh, physicsAsset).", "epic_call_tool"),
  },
  epic_bake_channel_keys: {
    epicSchema: S_epic_bake_channel_keys,
    epicTool: { toolset: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools", name: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.bake_channel_keys", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Bake a channel's values over a frame range. Evaluates the channel curve at every frame in the range and returns the computed values. Useful for extracting animation data or verifying interpolation results. Available on UE 5.8; absent from UE 5.9. Params: section, channel_name, start_frame, end_frame", "epic_call_tool"),
  },
  epic_bake_constraint: {
    epicSchema: S_epic_bake_constraint,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.BakeConstraint", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Bake the constraint to keys based on the passed in frames. This will use the open sequencer to bake. See ConstraintsScriptingLibrary to get the list of available constraints Available on UE 5.9; absent from UE 5.8. Params: world, constraint, frames, timeUnit?", "epic_call_tool"),
  },
  epic_bake_constraints_with_settings: {
    epicSchema: S_epic_bake_constraints_with_settings,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.BakeConstraintsWithSettings", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Bake transform constraints to keys over the frame range in Settings, using the level sequence open in Sequencer. After baking, each constraint is keyed inactive over the baked range. Available on UE 5.9; absent from UE 5.8. Params: world, constraints, settings", "epic_call_tool"),
  },
  epic_bake_constraints_with_settings_async: {
    epicSchema: S_epic_bake_constraints_with_settings_async,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.BakeConstraintsWithSettingsAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] BakeConstraintsWithSettings as an async operation. See BakeConstraintsWithSettings for the parameters. Available on UE 5.9; absent from UE 5.8. Params: world, constraints, settings", "epic_call_tool"),
  },
  epic_bake_control_rig_space: {
    epicSchema: S_epic_bake_control_rig_space,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.BakeControlRigSpace", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Bake specified Control Rig Controls to a specified Space based upon the current settings. Every control needs a space channel. Available on UE 5.9; absent from UE 5.8. Params: inSequence, inControlRig, inControlNames, inSettings, timeUnit?", "epic_call_tool"),
  },
  epic_bake_control_rig_space_async: {
    epicSchema: S_epic_bake_control_rig_space_async,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.BakeControlRigSpaceAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] BakeControlRigSpace as an async operation. See BakeControlRigSpace for the parameters. Available on UE 5.9; absent from UE 5.8. Params: inSequence, inControlRig, inControlNames, inSettings, timeUnit?", "epic_call_tool"),
  },
  epic_bake_keys: {
    epicSchema: S_epic_bake_keys,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.BakeKeys", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Bake (resample) all keys on the specified control rig section at a fixed display-frame interval. Available on UE 5.9; absent from UE 5.8. Params: bakeInterval, movieSceneSection", "epic_call_tool"),
  },
  epic_bake_space: {
    epicSchema: S_epic_bake_space,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.bake_space", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Bake Control Rig controls' space over a frame range. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_names, start_frame, end_frame, reduce_keys?, tolerance?", "epic_call_tool"),
  },
  epic_bake_time_warp: {
    epicSchema: S_epic_bake_time_warp,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.BakeTimeWarp", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Bake a section's time warp into its keys and remove the time warp Available on UE 5.9; absent from UE 5.8. Params: inSection", "epic_call_tool"),
  },
  epic_bake_to_control_rig: {
    epicSchema: S_epic_bake_to_control_rig,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.bake_to_control_rig", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Bake existing animation on a binding into a Control Rig track. Available on UE 5.8; absent from UE 5.9. Params: sequence, binding, control_rig_asset_path, reduce_keys?, tolerance?, reset_controls?", "epic_call_tool"),
  },
  epic_bake_to_control_rig_from_reference: {
    epicSchema: S_epic_bake_to_control_rig_from_reference,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.BakeToControlRigFromReference", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Bake the current animation in the binding to a Control Rig track, replacing any existing Control Rig tracks on the binding and disabling its skeletal animation track. Handles both the class-based and runtime-asset-based branches of FControlRigAssetStrongReference. The rig needs a backwards solve event unless it is an FK rig. For the bake dialog's Smart Reduce or Bake reduce types, pass bReduceKeys false, then call SmartReduceSection or BakeKeys on the new section (FindControlRigTracks finds its track). Raises, changing nothing, when the editor's asset reference filter refuses a reference from the level sequence to the rig (as in UEFN). Available on UE 5.9; absent from UE 5.8. Params: world, levelSequence, inRef, exportOptions, bReduceKeys, tolerance, binding, bResetControls?", "epic_call_tool"),
  },
  epic_bake_to_control_rig_from_reference_async: {
    epicSchema: S_epic_bake_to_control_rig_from_reference_async,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.BakeToControlRigFromReferenceAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] BakeToControlRigFromReference as an async operation. See BakeToControlRigFromReference for the parameters. Available on UE 5.9; absent from UE 5.8. Params: world, levelSequence, inRef, exportOptions, bReduceKeys, tolerance, binding, bResetControls?", "epic_call_tool"),
  },
  epic_bake_transform: {
    epicSchema: S_epic_bake_transform,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.bake_transform", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Bake transforms for the given bindings at every frame. Available on UE 5.8; absent from UE 5.9. Params: bindings", "epic_call_tool"),
  },
  epic_bake_transform_with_settings: {
    epicSchema: S_epic_bake_transform_with_settings,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.BakeTransformWithSettings", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Bake transform Available on UE 5.9; absent from UE 5.8. Params: objectBindings, inSettings, params?", "epic_call_tool"),
  },
  epic_batch_get_control_transforms: {
    epicSchema: S_epic_batch_get_control_transforms,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.BatchGetControlTransforms", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get an array per frame of specified control rig transforms for the specified control names. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlNames, frames, space, timeUnit?", "epic_call_tool"),
  },
  epic_batch_get_control_transforms_async: {
    epicSchema: S_epic_batch_get_control_transforms_async,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.BatchGetControlTransformsAsync", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] BatchGetControlTransforms read after the next editor world tick. See BatchGetControlTransforms for the parameters. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlNames, frames, space, timeUnit?", "epic_call_tool"),
  },
  epic_batch_set_control_transforms: {
    epicSchema: S_epic_batch_set_control_transforms,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.BatchSetControlTransforms", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set from Control transforms for different controls in the specified space at different frames Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controls, frames, space, optionalSectionToSet?, timeUnit?", "epic_call_tool"),
  },
  epic_blend_control_rig_pose_asset: {
    epicSchema: S_epic_blend_control_rig_pose_asset,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.BlendControlRigPoseAsset", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Blend a Control Rig's selected controls from their current values toward a pose asset, as dragging the Pose Library's blend slider does. Raises a script error if the pose asset or Control Rig is null, or no control is selected. Available on UE 5.9; absent from UE 5.8. Params: poseAsset, controlRig, blendValue, bKey?, bMirror?, bAdditive?", "epic_call_tool"),
  },
  epic_blend_values_on_selected: {
    epicSchema: S_epic_blend_values_on_selected,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.blend_values_on_selected", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Perform a blend operation on selected keys or controls. Available on UE 5.8; absent from UE 5.9. Params: sequence, operation, blend_value", "epic_call_tool"),
  },
  epic_blend_values_on_selected__control_rig_sequencer_editor_library: {
    epicSchema: S_epic_blend_values_on_selected__control_rig_sequencer_editor_library,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.BlendValuesOnSelected", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Perform the specified blend operation on the selected keys in the curve editor, or on the selected Control Rig controls at the current time. TimeOffset, BlendToNoise and BlendToWave need keys selected in the curve editor; they do not fall back to selected Control Rig controls. BlendToBuffer needs buffered curves and blends the key selection, or every curve drawn in the curve editor if no key is selected. BlendToMirror blends the curve editor key selection, falling back to a Sequencer key selection and then to the selected Control Rig controls at the current time, toward their mirrored pose. Requires Animation Mode to be active. Raises a script error when there is nothing to blend. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, blendOperation, blendValue", "epic_call_tool"),
  },
  epic_change_actor_template_class: {
    epicSchema: S_epic_change_actor_template_class,
    epicTool: { toolset: "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools", name: "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.change_actor_template_class", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.ChangeActorTemplateClass", engines: ["5.9"], schema: S_epic_change_actor_template_class__1, rename: {"actor_class":"actorClass"} }] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools] Set the actor class for a spawnable or replaceable template. Params: binding, actor_class. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.ChangeActorTemplateClass (Params: objectBinding, actorClass).", "epic_call_tool"),
  },
  epic_change_variable_type: {
    epicSchema: S_epic_change_variable_type,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.change_variable_type", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Change the type of an existing variable. Params: control_rig, name, new_type", "epic_call_tool"),
  },
  epic_clear_control_rig_rotation_order_override: {
    epicSchema: S_epic_clear_control_rig_rotation_order_override,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.ClearControlRigRotationOrderOverride", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Clear a control's rotation order override, returning it to the authored order; its keys are re-baked so the rotations stay the same. Raises a script error for the cases SetControlRigRotationOrderOverride raises for. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName", "epic_call_tool"),
  },
  epic_clear_marked_frames_shown_globally: {
    epicSchema: S_epic_clear_marked_frames_shown_globally,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.ClearMarkedFramesShownGlobally", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Hide the marks and mark ranges of this sequence and of every sub-sequence beneath it, as Sequencer's Clear Global Marks and Mark Ranges does. Raises and changes nothing when any sequence in the hierarchy is hidden, or is read-only and still shows its marks. Available on UE 5.9; absent from UE 5.8. Params: sequence", "epic_call_tool"),
  },
  epic_clear_motion_trail_offset: {
    epicSchema: S_epic_clear_motion_trail_offset,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.ClearMotionTrailOffset", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Remove a pinned trail's offset. Raises when the trail is not pinned. Available on UE 5.9; absent from UE 5.8. Params: trailId", "epic_call_tool"),
  },
  epic_clear_motion_trail_space: {
    epicSchema: S_epic_clear_motion_trail_space,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.ClearMotionTrailSpace", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Draw a pinned trail in world space again. Raises when the trail is not pinned. Available on UE 5.9; absent from UE 5.8. Params: trailId", "epic_call_tool"),
  },
  epic_clear_section_condition: {
    epicSchema: S_epic_clear_section_condition,
    epicTool: { toolset: "animation_toolset.toolsets.conditions.SequencerConditionTools", name: "animation_toolset.toolsets.conditions.SequencerConditionTools.clear_section_condition", engines: ["5.8"], successors: [{ toolset: "SequencerScripting.MovieSceneConditionExtensions", name: "SequencerScripting.MovieSceneConditionExtensions.ClearSectionCondition", engines: ["5.9"], schema: S_epic_clear_section_condition__1 }] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.conditions.SequencerConditionTools] Remove the condition from a section. Params: section. On UE 5.9 it calls SequencerScripting.MovieSceneConditionExtensions.ClearSectionCondition (Params: section).", "epic_call_tool"),
  },
  epic_clear_selection: {
    epicSchema: S_epic_clear_selection,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.clear_selection", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Clear the current Control Rig control selection. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_clear_selection__control_rig_sequencer_editor_library: {
    epicSchema: S_epic_clear_selection__control_rig_sequencer_editor_library,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.ClearSelection", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Clear the Control Rig control selection. Requires Animation Mode to be active. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_clear_selection_range: {
    epicSchema: S_epic_clear_selection_range,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.ClearSelectionRange", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Clear the focused sequence's selection range as an undoable step, as Clear Selection Range in the time slider does, including on a read-only sequence. Raises when no Sequencer is open. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_clear_skeletal_animation_match: {
    epicSchema: S_epic_clear_skeletal_animation_match,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.ClearSkeletalAnimationMatch", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Remove the section's match offsets and reset its match bone. Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_clear_track_condition: {
    epicSchema: S_epic_clear_track_condition,
    epicTool: { toolset: "animation_toolset.toolsets.conditions.SequencerConditionTools", name: "animation_toolset.toolsets.conditions.SequencerConditionTools.clear_track_condition", engines: ["5.8"], successors: [{ toolset: "SequencerScripting.MovieSceneConditionExtensions", name: "SequencerScripting.MovieSceneConditionExtensions.ClearTrackCondition", engines: ["5.9"], schema: S_epic_clear_track_condition__1 }] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.conditions.SequencerConditionTools] Remove the condition from a track. Params: track. On UE 5.9 it calls SequencerScripting.MovieSceneConditionExtensions.ClearTrackCondition (Params: track).", "epic_call_tool"),
  },
  epic_clear_track_row_condition: {
    epicSchema: S_epic_clear_track_row_condition,
    epicTool: { toolset: "animation_toolset.toolsets.conditions.SequencerConditionTools", name: "animation_toolset.toolsets.conditions.SequencerConditionTools.clear_track_row_condition", engines: ["5.8"], successors: [{ toolset: "SequencerScripting.MovieSceneConditionExtensions", name: "SequencerScripting.MovieSceneConditionExtensions.ClearTrackRowCondition", engines: ["5.9"], schema: S_epic_clear_track_row_condition__1, rename: {"row_index":"rowIndex"} }] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.conditions.SequencerConditionTools] Remove the condition from a specific track row. Params: track, row_index. On UE 5.9 it calls SequencerScripting.MovieSceneConditionExtensions.ClearTrackRowCondition (Params: track, rowIndex).", "epic_call_tool"),
  },
  epic_close_cinematic_viewport: {
    epicSchema: S_epic_close_cinematic_viewport,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.CloseCinematicViewport", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Switch every Cinematic Viewport back to the type its slot had before OpenCinematicViewport switched it, or to the default level viewport type, which persists in the editor layout like the menu's choice Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_close_curve_editor: {
    epicSchema: S_epic_close_curve_editor,
    epicTool: { toolset: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools", name: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.close_curve_editor", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Close the Sequencer Curve Editor panel. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_close_curve_editor__level_sequence_editor_subsystem: {
    epicSchema: S_epic_close_curve_editor__level_sequence_editor_subsystem,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.CloseCurveEditor", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Close the Curve Editor for the active Sequencer. Requires an active Sequencer. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_close_level_sequence_async: {
    epicSchema: S_epic_close_level_sequence_async,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.CloseLevelSequenceAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Close the current sequence and wait for the editor world to tick. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_close_pose_library_tab: {
    epicSchema: S_epic_close_pose_library_tab,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.ClosePoseLibraryTab", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Closes the Control Rig Pose Library panel. Requires Animation Mode to be active. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_close_sequence: {
    epicSchema: S_epic_close_sequence,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.close_sequence", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Close the currently open level sequence editor. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_collapse_all_curve_editor_nodes: {
    epicSchema: S_epic_collapse_all_curve_editor_nodes,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.CollapseAllCurveEditorNodes", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Collapse every item in the active Sequencer's open Curve Editor tree. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_collapse_anim_layers: {
    epicSchema: S_epic_collapse_anim_layers,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.collapse_anim_layers", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Collapse all sections and layers on a Control Rig track into one section. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, reduce_keys?, tolerance?", "epic_call_tool"),
  },
  epic_collapse_control_rig_anim_layers: {
    epicSchema: S_epic_collapse_control_rig_anim_layers,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.CollapseControlRigAnimLayers", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Collapse and bake all sections and layers on a control rig track to just one section, over the focused sequence's playback range. Available on UE 5.9; absent from UE 5.8. Params: inSequence, inTrack, bKeyReduce?, tolerance?", "epic_call_tool"),
  },
  epic_collapse_control_rig_anim_layers_with_settings: {
    epicSchema: S_epic_collapse_control_rig_anim_layers_with_settings,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.CollapseControlRigAnimLayersWithSettings", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Collapse and bake all sections and layers on a control rig track to just one section using passed in settings. Available on UE 5.9; absent from UE 5.8. Params: inSequence, inTrack, inSettings", "epic_call_tool"),
  },
  epic_collapse_control_rig_anim_layers_with_settings_async: {
    epicSchema: S_epic_collapse_control_rig_anim_layers_with_settings_async,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.CollapseControlRigAnimLayersWithSettingsAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] CollapseControlRigAnimLayersWithSettings as an async operation. See CollapseControlRigAnimLayersWithSettings for the parameters. Available on UE 5.9; absent from UE 5.8. Params: inSequence, inTrack, inSettings", "epic_call_tool"),
  },
  epic_compensate: {
    epicSchema: S_epic_compensate,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.Compensate", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Compensate the constrained animation at the specified time so the child keeps its world transform across the constraint switch. Available on UE 5.9; absent from UE 5.8. Params: inConstraint, inTime, timeUnit?", "epic_call_tool"),
  },
  epic_compensate_all: {
    epicSchema: S_epic_compensate_all,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.CompensateAll", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Compensate the constrained animation at every key of the constraint's active channel. Available on UE 5.9; absent from UE 5.8. Params: inConstraint", "epic_call_tool"),
  },
  epic_compile: {
    epicSchema: S_epic_compile,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.Compile", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Compiles the graphs and their shaders. Not undoable. Completes once shader compilation has finished. Raises if a previous Compile of the deformer has not completed yet. Available on UE 5.9; absent from UE 5.8. Params: deformer", "epic_call_tool"),
  },
  epic_connect_pins: {
    epicSchema: S_epic_connect_pins,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.connect_pins", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Connect two pins together. Params: control_rig, graph, source_pin, target_pin", "epic_call_tool"),
  },
  epic_control_rig_copy_vector_parameter_curves_to_transform: {
    epicSchema: S_epic_control_rig_copy_vector_parameter_curves_to_transform,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.ControlRigCopyVectorParameterCurvesToTransform", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Copy the internal parameter curves from vector control rig curves to transform curves, this is needed if a vector type is changed to transform type You would run this script first, resave, and then on next reload the curves should now show up on correct curves in the transform control Available on UE 5.9; absent from UE 5.8. Params: inTrack, inControlName, inType", "epic_call_tool"),
  },
  epic_convert_sub_graph_to_function: {
    epicSchema: S_epic_convert_sub_graph_to_function,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.ConvertSubGraphToFunction", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Turns a sub-graph into a function graph of the deformer, and the sub-graph node into a node calling it. Function graphs are read-only: build a function as a sub-graph (\"SubGraph/<Name>\" node), then convert it. Available on UE 5.9; absent from UE 5.8. Params: deformer, nodePath, functionName, category, bIsPublic", "epic_call_tool"),
  },
  epic_convert_to_custom_binding: {
    epicSchema: S_epic_convert_to_custom_binding,
    epicTool: { toolset: "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools", name: "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.convert_to_custom_binding", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.ConvertToCustomBinding", engines: ["5.9"], schema: S_epic_convert_to_custom_binding__1 }] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools] Convert a binding to a custom binding type. Params: binding, binding_type_class. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.ConvertToCustomBinding (Params: objectBinding, bindingType).", "epic_call_tool"),
  },
  epic_convert_to_possessable: {
    epicSchema: S_epic_convert_to_possessable,
    epicTool: { toolset: "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools", name: "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.convert_to_possessable", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.ConvertToPossessable", engines: ["5.9"], schema: S_epic_convert_to_possessable__1 }] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools] Convert a spawnable binding to a possessable. Params: binding. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.ConvertToPossessable (Params: objectBinding).", "epic_call_tool"),
  },
  epic_convert_to_spawnable: {
    epicSchema: S_epic_convert_to_spawnable,
    epicTool: { toolset: "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools", name: "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.convert_to_spawnable", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.ConvertToSpawnable", engines: ["5.9"], schema: S_epic_convert_to_spawnable__1 }] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools] Convert a possessable binding to a spawnable. Params: binding. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.ConvertToSpawnable (Params: objectBinding).", "epic_call_tool"),
  },
  epic_copy_bindings: {
    epicSchema: S_epic_copy_bindings,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.copy_bindings", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Copy one or more bindings to the Sequencer clipboard. Returns a paste token that can be passed to paste_bindings, or an empty string to consume from the clipboard. The token also lands in the editor clipboard for interactive use. Available on UE 5.8; absent from UE 5.9. Params: bindings", "epic_call_tool"),
  },
  epic_copy_bindings_to_text: {
    epicSchema: S_epic_copy_bindings_to_text,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.CopyBindingsToText", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Copy bindings of the focused sequence to the clipboard and return the text accepted by PasteBindingsFromText. Raises when no Sequencer is open or a binding belongs elsewhere. Available on UE 5.9; absent from UE 5.8. Params: bindings", "epic_call_tool"),
  },
  epic_copy_folders: {
    epicSchema: S_epic_copy_folders,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.copy_folders", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Copy one or more folders to the Sequencer clipboard. Available on UE 5.8; absent from UE 5.9. Params: folders", "epic_call_tool"),
  },
  epic_copy_folders_to_text: {
    epicSchema: S_epic_copy_folders_to_text,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.CopyFoldersToText", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Copy folders of the focused sequence to the clipboard and return all three text payloads. Raises when no Sequencer is open or a folder belongs elsewhere. Available on UE 5.9; absent from UE 5.8. Params: folders", "epic_call_tool"),
  },
  epic_copy_sections: {
    epicSchema: S_epic_copy_sections,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.copy_sections", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Copy one or more sections to the Sequencer clipboard. Available on UE 5.8; absent from UE 5.9. Params: sections", "epic_call_tool"),
  },
  epic_copy_sections_to_text: {
    epicSchema: S_epic_copy_sections_to_text,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.CopySectionsToText", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Copy sections to the clipboard and return the text accepted by PasteSectionsFromText. Available on UE 5.9; absent from UE 5.8. Params: sections", "epic_call_tool"),
  },
  epic_copy_tracks: {
    epicSchema: S_epic_copy_tracks,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.copy_tracks", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Copy one or more tracks to the Sequencer clipboard. Available on UE 5.8; absent from UE 5.9. Params: tracks", "epic_call_tool"),
  },
  epic_copy_tracks_to_text: {
    epicSchema: S_epic_copy_tracks_to_text,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.CopyTracksToText", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Copy tracks to the clipboard and return the text accepted by PasteTracksFromText. Available on UE 5.9; absent from UE 5.8. Params: tracks", "epic_call_tool"),
  },
  epic_create: {
    epicSchema: S_epic_create,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.create", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Creates a new Control Rig at the given location. Params: path", "epic_call_tool"),
  },
  epic_create_anim_layers_from_selected_tracks: {
    epicSchema: S_epic_create_anim_layers_from_selected_tracks,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.CreateAnimLayersFromSelectedTracks", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Create anim layers from the rows of the tracks selected in Sequencer, same-named rows share a layer Available on UE 5.9; absent from UE 5.8. Params: bAddToExistingLayers?", "epic_call_tool"),
  },
  epic_create_camera: {
    epicSchema: S_epic_create_camera,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.create_camera", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Create a new cine camera actor in the sequence. Available on UE 5.8; absent from UE 5.9. Params: spawnable?", "epic_call_tool"),
  },
  epic_create_camera_binding: {
    epicSchema: S_epic_create_camera_binding,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.CreateCameraBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Create a cine camera in the active Sequencer and return its binding. A spawnable camera is owned by the sequence. Otherwise the camera is a CineCameraActor placed in the level and bound as a plain possessable, so it renders in Movie Render Queue and PIE without a binding override. Available on UE 5.9; absent from UE 5.8. Params: bSpawnable", "epic_call_tool"),
  },
  epic_create_condition_endpoint: {
    epicSchema: S_epic_create_condition_endpoint,
    epicTool: { toolset: "AnimationAssistantToolset.SequencerDirectorToolset", name: "AnimationAssistantToolset.SequencerDirectorToolset.CreateConditionEndpoint", engines: ["5.9"] },
    ...bp("mutate", "[Epic AnimationAssistantToolset.SequencerDirectorToolset] Create a Director Blueprint Condition function endpoint on the sequence's Director Blueprint and bind it to a director-blueprint condition on the given host (a section, a whole track, or a single track row), so the host only evaluates at runtime when the function returns true. If the host has no condition, a Director Blueprint Condition is created on it; if it already has one, that condition is reused. A host carrying a different condition type (e.g. a platform or group condition) is rejected - clear it first via MovieSceneConditionExtensions.ClearSectionCondition or ClearTrackCondition. Generates a function graph named after FunctionName whose entry node takes an FMovieSceneConditionContext and returns a bool. The body is left empty by design - author it via BlueprintToolset, then call BlueprintToolset.CompileBlueprint on the Director BP so the condition's CompiledFunctionName is populated; without that the condition is skipped at runtime. The returned UK2Node_FunctionEntry is the new function's entry node. Scope, CheckFrequency, bEditorForceTrue and payload variables are EditAnywhere on the condition; set them via set_editor_property on the condition UObject, obtained from the native accessor (e.g. section.get_section_condition()). Retrieve the condition object before editing its properties; a class-path string is not an object reference. Available on UE 5.9; absent from UE 5.8. Params: sequence, conditionHost, functionName, rowIndex?", "epic_call_tool"),
  },
  epic_create_control_rig_pose_asset: {
    epicSchema: S_epic_create_control_rig_pose_asset,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.CreateControlRigPoseAsset", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Create a pose asset from a Control Rig's controls, as the Pose Library's Create Pose does; a unique name is used if the asset exists. Raises a script error if the Control Rig is null, the path is not a valid long package path, the folder is not visible or not writable in this editor (as in UEFN), or bUseAllControls is false and no control is selected. Available on UE 5.9; absent from UE 5.8. Params: controlRig, packagePath, assetName, bUseAllControls?", "epic_call_tool"),
  },
  epic_create_deformer: {
    epicSchema: S_epic_create_deformer,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.CreateDeformer", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Creates a deformer asset with an empty UpdateGraph and the primary component binding. Available on UE 5.9; absent from UE 5.8. Params: assetPath", "epic_call_tool"),
  },
  epic_create_dynamic_binding_resolver: {
    epicSchema: S_epic_create_dynamic_binding_resolver,
    epicTool: { toolset: "AnimationAssistantToolset.SequencerDirectorToolset", name: "AnimationAssistantToolset.SequencerDirectorToolset.CreateDynamicBindingResolver", engines: ["5.9"] },
    ...bp("mutate", "[Epic AnimationAssistantToolset.SequencerDirectorToolset] Create a dynamic-binding resolver function endpoint on the sequence's Director Blueprint and wire it to the binding's FMovieSceneDynamicBinding so the binding is resolved at runtime by the generated function. Generates a function graph named after FunctionName whose entry node returns a FMovieSceneDynamicBindingResolveResult. The function body is left empty by design - callers author it via BlueprintToolset (e.g. drop a \"GetPlayerPawn\" node, connect its return to the entry's Object pin via a MakeStruct FMovieSceneDynamicBindingResolveResult). The returned UK2Node_FunctionEntry is the entry-point node of the new function graph; pass it by name lookup to BlueprintToolset calls that take node references. After authoring the body, call BlueprintToolset.CompileBlueprint on the Director BP to populate FMovieSceneDynamicBinding's CompiledFunctionName. Otherwise the resolver will be silently skipped at runtime. Available on UE 5.9; absent from UE 5.8. Params: sequence, binding, functionName", "epic_call_tool"),
  },
  epic_create_level_sequence: {
    epicSchema: S_epic_create_level_sequence,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.create_level_sequence", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Create a new Level Sequence asset. If an asset already exists at the given path, it will be deleted first to avoid triggering a modal overwrite dialog. Available on UE 5.8; absent from UE 5.9. Params: package_path, asset_name", "epic_call_tool"),
  },
  epic_create_level_sequence__level_sequence_editor_blueprint_library: {
    epicSchema: S_epic_create_level_sequence__level_sequence_editor_blueprint_library,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.CreateLevelSequence", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Create a new Level Sequence asset without allowing an existing asset to be overwritten. Available on UE 5.9; absent from UE 5.8. Params: packagePath, assetName", "epic_call_tool"),
  },
  epic_create_mirrored_selection_set: {
    epicSchema: S_epic_create_mirrored_selection_set,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.CreateMirroredSelectionSet", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Create a selection set with the mirrored control names of an existing set, as the panel's Create Mirror does. Raises a script error if no level sequence is open in Sequencer or the set does not exist. Available on UE 5.9; absent from UE 5.8. Params: id", "epic_call_tool"),
  },
  epic_create_node: {
    epicSchema: S_epic_create_node,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.create_node", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Create a new RigUnit node in the graph. Params: control_rig, graph, node_type, position?, node_name?", "epic_call_tool"),
  },
  epic_create_node_group: {
    epicSchema: S_epic_create_node_group,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.CreateNodeGroup", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Create a Sequencer Group in the focused sequence holding Nodes, which may be empty. Nodes must be folders, object bindings, tracks or track rows, as the Group Manager offers. Name is stored without surrounding whitespace and must not be blank. Available on UE 5.9; absent from UE 5.8. Params: name, nodes", "epic_call_tool"),
  },
  epic_create_selection_set_from_selection: {
    epicSchema: S_epic_create_selection_set_from_selection,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.CreateSelectionSetFromSelection", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Create a selection set from the controls and actors selected in Sequencer, as the Selection Sets panel's Add Set does. Raises a script error if no level sequence is open in Sequencer or nothing is selected. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_curve_editor_empty_selection: {
    epicSchema: S_epic_curve_editor_empty_selection,
    epicTool: { toolset: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools", name: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.curve_editor_empty_selection", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Clear all key selection in the Curve Editor. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_curve_editor_select_keys: {
    epicSchema: S_epic_curve_editor_select_keys,
    epicTool: { toolset: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools", name: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.curve_editor_select_keys", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Select keys by index in the Curve Editor. Available on UE 5.8; absent from UE 5.9. Params: channel, indices", "epic_call_tool"),
  },
  epic_delete_all_marked_frames: {
    epicSchema: S_epic_delete_all_marked_frames,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.delete_all_marked_frames", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Delete all marked frames from the sequence. Available on UE 5.8; absent from UE 5.9. Params: sequence", "epic_call_tool"),
  },
  epic_delete_all_selection_sets: {
    epicSchema: S_epic_delete_all_selection_sets,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.DeleteAllSelectionSets", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Delete every selection set of the focused level sequence, showing their controls first. Raises a script error if no level sequence is open in Sequencer. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_delete_anim_layer: {
    epicSchema: S_epic_delete_anim_layer,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.delete_anim_layer", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Delete an animation layer at the specified index. Available on UE 5.8; absent from UE 5.9. Params: index", "epic_call_tool"),
  },
  epic_delete_anim_layer__control_rig_sequencer_editor_library: {
    epicSchema: S_epic_delete_anim_layer__control_rig_sequencer_editor_library,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.DeleteAnimLayer", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Delete anim layer at specified index in the level sequence open in Sequencer. Deleting the last layer above the Base layer also removes the Base layer and the sequence's anim layer data. Available on UE 5.9; absent from UE 5.8. Params: index", "epic_call_tool"),
  },
  epic_delete_constraint_key: {
    epicSchema: S_epic_delete_constraint_key,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.DeleteConstraintKey", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Delete the Key for the Constraint at the specified time, as deleting it in Sequencer does: the child's compensation key one tick before is removed and the keys after it are re-expressed to keep their world transform. Available on UE 5.9; absent from UE 5.8. Params: constraint, constraintSection, inTime, timeUnit?", "epic_call_tool"),
  },
  epic_delete_control_rig_control_keys: {
    epicSchema: S_epic_delete_control_rig_control_keys,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.DeleteControlRigControlKeys", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Remove every key on controls, as Anim Details' Remove All Keys does: in the section keyed for each control, each of its channels loses its keys and keeps the first key's value as its default. Raises a script error if the sequence is not the one focused in Sequencer, the Control Rig has no track in it, or a control does not exist. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlNames", "epic_call_tool"),
  },
  epic_delete_control_rig_space: {
    epicSchema: S_epic_delete_control_rig_space,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.DeleteControlRigSpace", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Delete the Control Rig Space Key for the Control at the specified time. This will delete any attached Control Rig keys at this time and will perform any needed compensation to the new space. Available on UE 5.9; absent from UE 5.8. Params: inSequence, inControlRig, inControlName, inTime, timeUnit?", "epic_call_tool"),
  },
  epic_delete_linked_anim_sequence: {
    epicSchema: S_epic_delete_linked_anim_sequence,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.DeleteLinkedAnimSequence", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Remove the links between a binding and every AnimSequence linked to it, on both assets. Raises a script error if the binding has no level sequence or no linked AnimSequence. Available on UE 5.9; absent from UE 5.8. Params: binding", "epic_call_tool"),
  },
  epic_delete_linked_anim_track: {
    epicSchema: S_epic_delete_linked_anim_track,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.DeleteLinkedAnimTrack", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Remove the linked animation track and its linked animation sequence for the given binding. Available on UE 5.9; absent from UE 5.8. Params: binding", "epic_call_tool"),
  },
  epic_delete_marked_frame: {
    epicSchema: S_epic_delete_marked_frame,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.delete_marked_frame", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Delete a marked frame by index. Available on UE 5.8; absent from UE 5.9. Params: sequence, index", "epic_call_tool"),
  },
  epic_delete_marked_frame__movie_scene_sequence_extensions: {
    epicSchema: S_epic_delete_marked_frame__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.DeleteMarkedFrame", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Delete the user mark or mark range by index @DeleteIndex The index to the user mark or mark range to delete Available on UE 5.9; absent from UE 5.8. Params: sequence, deleteIndex", "epic_call_tool"),
  },
  epic_delete_marked_frames: {
    epicSchema: S_epic_delete_marked_frames,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.DeleteMarkedFrames", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Delete all user marks and mark ranges Available on UE 5.9; absent from UE 5.8. Params: sequence", "epic_call_tool"),
  },
  epic_delete_node: {
    epicSchema: S_epic_delete_node,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.delete_node", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Delete a node from the graph. Params: control_rig, graph, node", "epic_call_tool"),
  },
  epic_delete_node_group: {
    epicSchema: S_epic_delete_node_group,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.DeleteNodeGroup", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Delete a Sequencer Group of the focused sequence Available on UE 5.9; absent from UE 5.8. Params: group", "epic_call_tool"),
  },
  epic_delete_selection_set: {
    epicSchema: S_epic_delete_selection_set,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.DeleteSelectionSet", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Delete a selection set, showing its controls first. Raises a script error if no level sequence is open in Sequencer or the set does not exist. Available on UE 5.9; absent from UE 5.8. Params: id", "epic_call_tool"),
  },
  epic_delete_space: {
    epicSchema: S_epic_delete_space,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.delete_space", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Delete a space-switch key at a specific frame. Performs compensation to the new space automatically. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, frame", "epic_call_tool"),
  },
  epic_delete_time_warp: {
    epicSchema: S_epic_delete_time_warp,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.DeleteTimeWarp", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Remove the time warp from a section Available on UE 5.9; absent from UE 5.8. Params: inSection", "epic_call_tool"),
  },
  epic_disconnect_pins: {
    epicSchema: S_epic_disconnect_pins,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.disconnect_pins", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Disconnect two pins. Params: control_rig, graph, source_pin, target_pin", "epic_call_tool"),
  },
  epic_duplicate_anim_layer: {
    epicSchema: S_epic_duplicate_anim_layer,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.duplicate_anim_layer", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Duplicate an animation layer at the specified index. Available on UE 5.8; absent from UE 5.9. Params: index", "epic_call_tool"),
  },
  epic_duplicate_anim_layer__control_rig_sequencer_editor_library: {
    epicSchema: S_epic_duplicate_anim_layer__control_rig_sequencer_editor_library,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.DuplicateAnimLayer", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Duplicate anim layer at specified index in the level sequence open in Sequencer Available on UE 5.9; absent from UE 5.8. Params: index", "epic_call_tool"),
  },
  epic_empty_curve_editor_selection: {
    epicSchema: S_epic_empty_curve_editor_selection,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.EmptyCurveEditorSelection", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Clear the active Curve Editor's selected keys. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_empty_selection: {
    epicSchema: S_epic_empty_selection,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.empty_selection", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Clear all selection in the Sequencer editor. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_empty_selection__level_sequence_editor_blueprint_library: {
    epicSchema: S_epic_empty_selection__level_sequence_editor_blueprint_library,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.EmptySelection", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Empties the current selection. Does nothing when no Sequencer is open. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_evaluate_channel_keys: {
    epicSchema: S_epic_evaluate_channel_keys,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.EvaluateChannelKeys", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Sample once per owning-sequence display frame in [StartFrame, EndFrame). Supports float, double, bool (0/1), integer and particle enum values. Raises when the range holds more than 100000 frames, so sample large ranges in chunks. No open editor is required. Available on UE 5.9; absent from UE 5.8. Params: section, channelName, startFrame, endFrame", "epic_call_tool"),
  },
  epic_expand_all_curve_editor_nodes: {
    epicSchema: S_epic_expand_all_curve_editor_nodes,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.ExpandAllCurveEditorNodes", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Expand every item in the active Sequencer's open Curve Editor tree. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_expand_all_outliner_nodes: {
    epicSchema: S_epic_expand_all_outliner_nodes,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.ExpandAllOutlinerNodes", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Expand or collapse every row that has a path, including rows whose path is ambiguous, returning the number updated. Available on UE 5.9; absent from UE 5.8. Params: bExpanded?", "epic_call_tool"),
  },
  epic_export_anim_sequence: {
    epicSchema: S_epic_export_anim_sequence,
    epicTool: { toolset: "animation_toolset.toolsets.import_export.SequencerImportExportTools", name: "animation_toolset.toolsets.import_export.SequencerImportExportTools.export_anim_sequence", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.import_export.SequencerImportExportTools] Export animation from a sequence binding to an AnimSequence asset. Available on UE 5.8; absent from UE 5.9. Params: world, sequence, anim_sequence, binding, create_link?", "epic_call_tool"),
  },
  epic_export_anim_sequence__sequencer_tools_function_library: {
    epicSchema: S_epic_export_anim_sequence__sequencer_tools_function_library,
    epicTool: { toolset: "SequencerScriptingEditor.SequencerToolsFunctionLibrary", name: "SequencerScriptingEditor.SequencerToolsFunctionLibrary.ExportAnimSequence", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScriptingEditor.SequencerToolsFunctionLibrary] Export Passed in Binding as an Anim Sequence. Note use this when you want to export without an open Level Sequence in Sequencer, and know that all spawnables that may be dynamically created and destroyed will be valid. If the spawnable won't be fully created and you have an open Sequencer use UControlRigSequencerEditorLibrary::ExportAnimSequenceFromSequencer instead. The animation asset must be writable, as must ExportOptions when provided. The binding must belong to Sequence. The exported range is the sequence's playback range, which must be non-empty and bounded, unless ExportOptions enables a custom time range; that range must then have a positive display rate and an end frame at or after its start. Creating a link also requires a writable sequence. Available on UE 5.9; absent from UE 5.8. Params: world, sequence, animSequence, exportOptions, binding, bCreateLink", "epic_call_tool"),
  },
  epic_export_anim_sequence_from_sequencer: {
    epicSchema: S_epic_export_anim_sequence_from_sequencer,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.ExportAnimSequenceFromSequencer", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Export the binding's animation to an Anim Sequence, using the level sequence focused in Sequencer. Use this instead of USequencerToolsFunctionLibrary::ExportAnimSequence when you may be exporting an anim sequence from a Spawnable that can't be created dynamically in a closed system. With bCreateLink, raises, changing nothing, when the level sequence is read-only or the editor's asset reference filter refuses a link between the level sequence and the AnimSequence (as in UEFN). Available on UE 5.9; absent from UE 5.8. Params: animSequence, exportOption, binding, bCreateLink", "epic_call_tool"),
  },
  epic_export_anim_sequence_from_sequencer_async: {
    epicSchema: S_epic_export_anim_sequence_from_sequencer_async,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.ExportAnimSequenceFromSequencerAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] ExportAnimSequenceFromSequencer as an async operation. See ExportAnimSequenceFromSequencer for the parameters. Available on UE 5.9; absent from UE 5.8. Params: animSequence, exportOption, binding, bCreateLink", "epic_call_tool"),
  },
  epic_export_fbx: {
    epicSchema: S_epic_export_fbx,
    epicTool: { toolset: "animation_toolset.toolsets.import_export.SequencerImportExportTools", name: "animation_toolset.toolsets.import_export.SequencerImportExportTools.export_fbx", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.import_export.SequencerImportExportTools] Export a level sequence to FBX. Available on UE 5.8; absent from UE 5.9. Params: world, sequence, bindings, fbx_file_path, override_options?", "epic_call_tool"),
  },
  epic_export_fbx_from_rig: {
    epicSchema: S_epic_export_fbx_from_rig,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.export_fbx_from_rig", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Export an FBX file from a Control Rig section. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, export_file_path, ascii?", "epic_call_tool"),
  },
  epic_export_fbxfrom_control_rig_section: {
    epicSchema: S_epic_export_fbxfrom_control_rig_section,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.ExportFBXFromControlRigSection", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Export the given Control Rig section's animation to an FBX file. Hidden from restricted (UEFN) AI hosts. Available on UE 5.9; absent from UE 5.8. Params: sequence, section, exportFBXControlRigSettings", "epic_call_tool"),
  },
  epic_export_fbxfrom_control_rig_section_async: {
    epicSchema: S_epic_export_fbxfrom_control_rig_section_async,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.ExportFBXFromControlRigSectionAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] ExportFBXFromControlRigSection as an async operation. See ExportFBXFromControlRigSection for the parameters. Available on UE 5.9; absent from UE 5.8. Params: sequence, section, exportFBXControlRigSettings", "epic_call_tool"),
  },
  epic_export_level_sequence_edl: {
    epicSchema: S_epic_export_level_sequence_edl,
    epicTool: { toolset: "SequencerScriptingEditor.SequencerToolsFunctionLibrary", name: "SequencerScriptingEditor.SequencerToolsFunctionLibrary.ExportLevelSequenceEDL", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScriptingEditor.SequencerToolsFunctionLibrary] Export a level sequence's cinematic shot track to an Edit Decision List (EDL) file. Audio tracks are not exported. Also writes sibling .rv and .bat files at the same base path, replacing SaveFilename's own extension. Fails if the sequence has no cinematic shot track or no active, fully-ranged shot sections. Available on UE 5.9; absent from UE 5.8. Params: sequence, saveFilename, handleFrames?, movieExtension?", "epic_call_tool"),
  },
  epic_export_level_sequence_fbx: {
    epicSchema: S_epic_export_level_sequence_fbx,
    epicTool: { toolset: "SequencerScriptingEditor.SequencerToolsFunctionLibrary", name: "SequencerScriptingEditor.SequencerToolsFunctionLibrary.ExportLevelSequenceFBX", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScriptingEditor.SequencerToolsFunctionLibrary] Export bindings and tracks to an FBX file. Available on UE 5.9; absent from UE 5.8. Params: inParams", "epic_call_tool"),
  },
  epic_export_level_sequence_fcpxml: {
    epicSchema: S_epic_export_level_sequence_fcpxml,
    epicTool: { toolset: "SequencerScriptingEditor.SequencerToolsFunctionLibrary", name: "SequencerScriptingEditor.SequencerToolsFunctionLibrary.ExportLevelSequenceFCPXML", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScriptingEditor.SequencerToolsFunctionLibrary] Export a level sequence's cinematic shot and audio tracks to a Final Cut Pro 7 XML file. Available on UE 5.9; absent from UE 5.8. Params: sequence, saveFilename, filenameFormat?, resX?, resY?, handleFrames?, movieExtension?", "epic_call_tool"),
  },
  epic_export_selection_sets_to_json: {
    epicSchema: S_epic_export_selection_sets_to_json,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.ExportSelectionSetsToJson", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Write every selection set of the focused level sequence to a JSON string, as Export Sets does. Raises a script error if no level sequence is open in Sequencer. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_extract_anim_layer: {
    epicSchema: S_epic_extract_anim_layer,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.ExtractAnimLayer", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Extract the controls/objects currently selected in the editor out of the anim layer at the specified index into a new layer. For a normal layer the selected items (and their keys) are removed from the source layer. For the Base Layer (index 0) the items are duplicated into a new Override layer and only their keys are removed from the Base Layer. Available on UE 5.9; absent from UE 5.8. Params: index", "epic_call_tool"),
  },
  epic_find_binding_by_name: {
    epicSchema: S_epic_find_binding_by_name,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.find_binding_by_name", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Find a binding by its display name. Available on UE 5.8; absent from UE 5.9. Params: sequence, name", "epic_call_tool"),
  },
  epic_find_binding_by_name__movie_scene_sequence_extensions: {
    epicSchema: S_epic_find_binding_by_name__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.FindBindingByName", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Find a binding by its exact internal possessable or spawnable name, which can differ from the binding's display name. Returns an invalid proxy on a miss; an empty name is an error. Available on UE 5.9; absent from UE 5.8. Params: sequence, name", "epic_call_tool"),
  },
  epic_find_binding_by_tag: {
    epicSchema: S_epic_find_binding_by_tag,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.find_binding_by_tag", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Find the first binding with the given tag in the sequence. Tags are authored via tag_binding() in this toolset, or via RMB -> Expose on a binding in the Sequencer editor. Available on UE 5.8; absent from UE 5.9. Params: sequence, tag_name", "epic_call_tool"),
  },
  epic_find_binding_by_tag__movie_scene_sequence_extensions: {
    epicSchema: S_epic_find_binding_by_tag__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.FindBindingByTag", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Find the first binding ID stored under a tag, or an invalid ID when absent. Available on UE 5.9; absent from UE 5.8. Params: sequence, tagName", "epic_call_tool"),
  },
  epic_find_bindings_by_tag: {
    epicSchema: S_epic_find_bindings_by_tag,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.find_bindings_by_tag", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Find all bindings with the given tag in the sequence. Available on UE 5.8; absent from UE 5.9. Params: sequence, tag_name", "epic_call_tool"),
  },
  epic_find_bindings_by_tag__movie_scene_sequence_extensions: {
    epicSchema: S_epic_find_bindings_by_tag__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.FindBindingsByTag", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Copy all binding IDs stored under a tag. Available on UE 5.9; absent from UE 5.8. Params: sequence, tagName", "epic_call_tool"),
  },
  epic_find_control_rig_tracks: {
    epicSchema: S_epic_find_control_rig_tracks,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.FindControlRigTracks", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Find the Control Rig tracks in a level sequence whose Control Rig comes from the given reference. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, inRef", "epic_call_tool"),
  },
  epic_find_marked_frame_by_label: {
    epicSchema: S_epic_find_marked_frame_by_label,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.find_marked_frame_by_label", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Find a marked frame by label. Available on UE 5.8; absent from UE 5.9. Params: sequence, label", "epic_call_tool"),
  },
  epic_find_marked_frame_by_label__movie_scene_sequence_extensions: {
    epicSchema: S_epic_find_marked_frame_by_label__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.FindMarkedFrameByLabel", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Find the user mark or mark range by label @InLabel The label to the user mark or mark range to find Available on UE 5.9; absent from UE 5.8. Params: sequence, inLabel", "epic_call_tool"),
  },
  epic_find_or_create_control_rig_component_track: {
    epicSchema: S_epic_find_or_create_control_rig_component_track,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.FindOrCreateControlRigComponentTrack", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Find or create a Control Rig track for each Control Rig component on the bound actor Raises, adding no track, when the editor's asset reference filter refuses a rig that needs a new track (as in UEFN). Available on UE 5.9; absent from UE 5.8. Params: world, levelSequence, inBinding", "epic_call_tool"),
  },
  epic_find_or_create_control_rig_track_from_reference: {
    epicSchema: S_epic_find_or_create_control_rig_track_from_reference,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.FindOrCreateControlRigTrackFromReference", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Find or create a Control Rig Track from a Control Rig asset reference. Handles both the class-based and runtime-asset-based branches of FControlRigAssetStrongReference. Track lookup matches by exact class for the class branch, and by URigVMHost::GeneratedBy identity for the asset branch. Creating a track activates Animation Mode in the level editor. Raises instead of creating a track when the editor's asset reference filter refuses a reference from the level sequence to the rig (as in UEFN). Available on UE 5.9; absent from UE 5.8. Params: world, levelSequence, inRef, inBinding, bIsLayeredControlRig?", "epic_call_tool"),
  },
  epic_find_or_create_track: {
    epicSchema: S_epic_find_or_create_track,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.find_or_create_track", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Add a Control Rig track to a binding using a Control Rig asset. This is the standard way to add a Control Rig to Sequencer. Uses ControlRigSequencerLibrary.find_or_create_control_rig_track. Available on UE 5.8; absent from UE 5.9. Params: sequence, binding, control_rig_asset_path, is_layered?", "epic_call_tool"),
  },
  epic_find_tracks_by_type: {
    epicSchema: S_epic_find_tracks_by_type,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.find_tracks_by_type", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Find all tracks of a specific type on a binding. Available on UE 5.8; absent from UE 5.9. Params: binding, track_type", "epic_call_tool"),
  },
  epic_find_tracks_by_type__movie_scene_binding_extensions: {
    epicSchema: S_epic_find_tracks_by_type__movie_scene_binding_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneBindingExtensions", name: "SequencerScripting.MovieSceneBindingExtensions.FindTracksByType", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneBindingExtensions] Find all tracks within a given binding of the specified type Available on UE 5.9; absent from UE 5.8. Params: inBinding, trackType", "epic_call_tool"),
  },
  epic_fix_actor_references: {
    epicSchema: S_epic_fix_actor_references,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.fix_actor_references", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Attempt to auto-fix broken actor references in the current sequence. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_fix_actor_references__level_sequence_editor_subsystem: {
    epicSchema: S_epic_fix_actor_references__level_sequence_editor_subsystem,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.FixActorReferences", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Attempts to automatically fix up broken actor references in the current scene Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_focus_level_sequence_async: {
    epicSchema: S_epic_focus_level_sequence_async,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.FocusLevelSequenceAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Focus a sub-sequence and wait for its editor world evaluation. Fails for sub-sequences hidden from scripting in this editor. Available on UE 5.9; absent from UE 5.8. Params: subSection", "epic_call_tool"),
  },
  epic_focus_parent_sequence: {
    epicSchema: S_epic_focus_parent_sequence,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.focus_parent_sequence", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Navigate up one level in the sub-sequence hierarchy. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_focus_parent_sequence_async: {
    epicSchema: S_epic_focus_parent_sequence_async,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.FocusParentSequenceAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Focus the parent sequence and wait for its editor world evaluation. Fails when the parent is hidden from scripting in this editor. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_focus_sub_sequence: {
    epicSchema: S_epic_focus_sub_sequence,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.focus_sub_sequence", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Navigate into a sub-sequence via its sub-section. Use get_sections() on a sub-track to find the sub-section, then pass it here to focus the sub-sequence it references. Available on UE 5.8; absent from UE 5.9. Params: sub_section", "epic_call_tool"),
  },
  epic_force_evaluate: {
    epicSchema: S_epic_force_evaluate,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.force_evaluate", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Force the Sequencer to evaluate and update the viewport. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_force_update_async: {
    epicSchema: S_epic_force_update_async,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.ForceUpdateAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Evaluate the current sequence and wait for bound skeletal meshes to refresh. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_frame_all_curves: {
    epicSchema: S_epic_frame_all_curves,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.FrameAllCurves", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Frame the active Sequencer's open Curve Editor on its time bounds and the values of every curve shown. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_frame_curve_editor_selection: {
    epicSchema: S_epic_frame_curve_editor_selection,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.FrameCurveEditorSelection", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Frame the keys selected in the active Sequencer's open Curve Editor. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_frame_selection: {
    epicSchema: S_epic_frame_selection,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.frame_selection", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Frame the viewport to the current Control Rig control selection. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_frame_selection__control_rig_sequencer_editor_library: {
    epicSchema: S_epic_frame_selection__control_rig_sequencer_editor_library,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.FrameSelection", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Frame the viewport on the selected controls. Requires Animation Mode to be active. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_frame_time_posing_range_on_selected_keys: {
    epicSchema: S_epic_frame_time_posing_range_on_selected_keys,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.FrameTimePosingRangeOnSelectedKeys", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Fit the full-weight band of the Time Posing window to the selected keys, keeping the falloff widths, one undoable step. Uses the Curve Editor's key selection while its panel is open, else Sequencer's. Raises when Time Posing is off, LinkMode is AroundRange, or no keys are selected. Available on UE 5.9; absent from UE 5.8. Params: levelSequence", "epic_call_tool"),
  },
  epic_get_active_cinematic_viewport: {
    epicSchema: S_epic_get_active_cinematic_viewport,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetActiveCinematicViewport", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] The active Cinematic Viewport, or the first one in the level editor's viewport order, by tab then slot, when the active viewport is not one. Raises when none is open. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_actor_transform_at_frame: {
    epicSchema: S_epic_get_actor_transform_at_frame,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_actor_transform_at_frame", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get an actor's world transform at a specific frame. Finds the actor by name in the current editor world. Available on UE 5.8; absent from UE 5.9. Params: sequence, actor_name, frame", "epic_call_tool"),
  },
  epic_get_actor_world_transform: {
    epicSchema: S_epic_get_actor_world_transform,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetActorWorldTransform", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get Actors World Transform at a specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, actor, frame, timeUnit?", "epic_call_tool"),
  },
  epic_get_actor_world_transforms: {
    epicSchema: S_epic_get_actor_world_transforms,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetActorWorldTransforms", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get Actors World Transforms at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, actor, frames, timeUnit?", "epic_call_tool"),
  },
  epic_get_actor_world_transforms_async: {
    epicSchema: S_epic_get_actor_world_transforms_async,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetActorWorldTransformsAsync", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] GetActorWorldTransforms read after the next editor world tick. See GetActorWorldTransforms for the parameters. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, actor, frames, timeUnit?", "epic_call_tool"),
  },
  epic_get_all_binding_tags: {
    epicSchema: S_epic_get_all_binding_tags,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_all_binding_tags", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get every tag name registered in the sequence. Uses the MovieSceneBindingTagExtensions C++ library. Available on UE 5.8; absent from UE 5.9. Params: sequence", "epic_call_tool"),
  },
  epic_get_all_binding_tags__movie_scene_binding_tag_extensions: {
    epicSchema: S_epic_get_all_binding_tags__movie_scene_binding_tag_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneBindingTagExtensions", name: "SequencerScripting.MovieSceneBindingTagExtensions.GetAllBindingTags", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneBindingTagExtensions] Get every tag name registered on the sequence's MovieScene. Available on UE 5.9; absent from UE 5.8. Params: sequence", "epic_call_tool"),
  },
  epic_get_all_bones: {
    epicSchema: S_epic_get_all_bones,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.get_all_bones", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get all bones in the Control Rig hierarchy. Params: control_rig", "epic_call_tool"),
  },
  epic_get_all_controls: {
    epicSchema: S_epic_get_all_controls,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.get_all_controls", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get all controls in the Control Rig hierarchy. Params: control_rig", "epic_call_tool"),
  },
  epic_get_all_nulls: {
    epicSchema: S_epic_get_all_nulls,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.get_all_nulls", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get all nulls (locators) in the Control Rig hierarchy. Params: control_rig", "epic_call_tool"),
  },
  epic_get_anim_layer_index: {
    epicSchema: S_epic_get_anim_layer_index,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetAnimLayerIndex", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get the index of an anim layer in the level sequence open in Sequencer Available on UE 5.9; absent from UE 5.8. Params: animLayer", "epic_call_tool"),
  },
  epic_get_anim_layer_properties: {
    epicSchema: S_epic_get_anim_layer_properties,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetAnimLayerProperties", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get an anim layer's name, mute, lock, weight, type and keyed state, as the Animation Layers panel shows them. Raises a script error if no level sequence is open in Sequencer, it has no anim layers, or Index is out of range. Available on UE 5.9; absent from UE 5.8. Params: index", "epic_call_tool"),
  },
  epic_get_anim_layers: {
    epicSchema: S_epic_get_anim_layers,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_anim_layers", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get all animation layers from the active Sequencer. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_anim_layers__control_rig_sequencer_editor_library: {
    epicSchema: S_epic_get_anim_layers__control_rig_sequencer_editor_library,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetAnimLayers", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get the anim layers of the level sequence open in Sequencer. With the default bAddIfDoesNotExist this adds anim layer data (a Base layer) to the sequence when it has none, so pass false for a read-only query. Available on UE 5.9; absent from UE 5.8. Params: bAddIfDoesNotExist?", "epic_call_tool"),
  },
  epic_get_anim_mode_gizmo_scale: {
    epicSchema: S_epic_get_anim_mode_gizmo_scale,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_anim_mode_gizmo_scale", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get the editor's transform gizmo size. Reads UTransformGizmoEditorSettings::TransformGizmoSize. The CR-specific gizmo scale was removed in UE 5.8 in favor of this editor-wide setting, so this affects every transform gizmo (level, sequencer, CR, etc.). Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_anim_mode_hide_manips: {
    epicSchema: S_epic_get_anim_mode_hide_manips,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_anim_mode_hide_manips", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get whether Animation Mode hides all manipulators. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_anim_mode_hierarchy: {
    epicSchema: S_epic_get_anim_mode_hierarchy,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_anim_mode_hierarchy", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get whether the Animation Mode draws hierarchy lines/dots. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_anim_mode_local_spaces: {
    epicSchema: S_epic_get_anim_mode_local_spaces,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_anim_mode_local_spaces", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get whether multi-select transforms act in each control's own space. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_anim_mode_nulls: {
    epicSchema: S_epic_get_anim_mode_nulls,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_anim_mode_nulls", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get whether the Animation Mode draws nulls. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_anim_mode_only_rig_sel: {
    epicSchema: S_epic_get_anim_mode_only_rig_sel,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_anim_mode_only_rig_sel", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get whether Animation Mode restricts viewport selection to rig controls. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_anim_sequence_link_from_level_sequence: {
    epicSchema: S_epic_get_anim_sequence_link_from_level_sequence,
    epicTool: { toolset: "SequencerScriptingEditor.SequencerToolsFunctionLibrary", name: "SequencerScriptingEditor.SequencerToolsFunctionLibrary.GetAnimSequenceLinkFromLevelSequence", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScriptingEditor.SequencerToolsFunctionLibrary] Get the links to the anim sequences if they exist on this level sequence Available on UE 5.9; absent from UE 5.8. Params: inLevelSequence", "epic_call_tool"),
  },
  epic_get_animation_mode_settings: {
    epicSchema: S_epic_get_animation_mode_settings,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetAnimationModeSettings", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get the Animation Mode viewport display preferences. These are editor-wide user preferences and never change sequence data. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_backward_solve_graph: {
    epicSchema: S_epic_get_backward_solve_graph,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.get_backward_solve_graph", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get the backward solve graph. The backward solve graph contains the InverseExecution event and runs during IK operations. Params: control_rig", "epic_call_tool"),
  },
  epic_get_binding_id: {
    epicSchema: S_epic_get_binding_id,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_binding_id", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the binding ID for a binding proxy. The binding ID can be used with get_bound_objects to resolve what actor or component the binding references at runtime. Available on UE 5.8; absent from UE 5.9. Params: sequence, binding", "epic_call_tool"),
  },
  epic_get_binding_id__movie_scene_sequence_extensions: {
    epicSchema: S_epic_get_binding_id__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.GetBindingID", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Get the binding ID for a binding within a sequence. Available on UE 5.9; absent from UE 5.8. Params: sequence, inBinding", "epic_call_tool"),
  },
  epic_get_binding_name: {
    epicSchema: S_epic_get_binding_name,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_binding_name", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the display name of a binding. Available on UE 5.8; absent from UE 5.9. Params: binding", "epic_call_tool"),
  },
  epic_get_binding_tags: {
    epicSchema: S_epic_get_binding_tags,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_binding_tags", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the tags currently attached to a specific binding. Available on UE 5.8; absent from UE 5.9. Params: binding", "epic_call_tool"),
  },
  epic_get_binding_tags__movie_scene_binding_tag_extensions: {
    epicSchema: S_epic_get_binding_tags__movie_scene_binding_tag_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneBindingTagExtensions", name: "SequencerScripting.MovieSceneBindingTagExtensions.GetBindingTags", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneBindingTagExtensions] Get every tag currently attached to this specific binding. Available on UE 5.9; absent from UE 5.8. Params: binding", "epic_call_tool"),
  },
  epic_get_bindings: {
    epicSchema: S_epic_get_bindings,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_bindings", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get all bindings in the sequence. Available on UE 5.8; absent from UE 5.9. Params: sequence", "epic_call_tool"),
  },
  epic_get_bindings__movie_scene_sequence_extensions: {
    epicSchema: S_epic_get_bindings__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.GetBindings", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Get all the bindings in this sequence Available on UE 5.9; absent from UE 5.8. Params: sequence", "epic_call_tool"),
  },
  epic_get_blend_type: {
    epicSchema: S_epic_get_blend_type,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetBlendType", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Return the section's optional blend type, including whether it is set. Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_get_blender_system: {
    epicSchema: S_epic_get_blender_system,
    epicTool: { toolset: "SequencerScripting.MovieSceneTrackExtensions", name: "SequencerScripting.MovieSceneTrackExtensions.GetBlenderSystem", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneTrackExtensions] Get the system that blends the track's overlapping sections. Raises for a track without a choice of blender. Available on UE 5.9; absent from UE 5.8. Params: track", "epic_call_tool"),
  },
  epic_get_bone_children: {
    epicSchema: S_epic_get_bone_children,
    epicTool: { toolset: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", name: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_bone_children", engines: ["5.8"], successors: [{ toolset: "EditorToolset.SkeletalMeshToolset", name: "EditorToolset.SkeletalMeshToolset.GetBoneChildren", engines: ["5.9"], schema: S_epic_get_bone_children__1, rename: {"bone_name":"boneName"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the direct children of a bone. Params: mesh, bone_name. On UE 5.9 it calls EditorToolset.SkeletalMeshToolset.GetBoneChildren (Params: mesh, boneName).", "epic_call_tool"),
  },
  epic_get_bone_names: {
    epicSchema: S_epic_get_bone_names,
    epicTool: { toolset: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", name: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_bone_names", engines: ["5.8"], successors: [{ toolset: "EditorToolset.SkeletalMeshToolset", name: "EditorToolset.SkeletalMeshToolset.GetBoneNames", engines: ["5.9"], schema: S_epic_get_bone_names__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the names of all bones in a skeletal mesh in hierarchy order. Bone names are used to target specific bones for socket attachment, physics constraints, and animation retargeting. Params: mesh. On UE 5.9 it calls EditorToolset.SkeletalMeshToolset.GetBoneNames (Params: mesh).", "epic_call_tool"),
  },
  epic_get_bone_parent: {
    epicSchema: S_epic_get_bone_parent,
    epicTool: { toolset: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", name: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_bone_parent", engines: ["5.8"], successors: [{ toolset: "EditorToolset.SkeletalMeshToolset", name: "EditorToolset.SkeletalMeshToolset.GetBoneParent", engines: ["5.9"], schema: S_epic_get_bone_parent__1, rename: {"bone_name":"boneName"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the name of a bone's parent, or an empty string for the root bone. Params: mesh, bone_name. On UE 5.9 it calls EditorToolset.SkeletalMeshToolset.GetBoneParent (Params: mesh, boneName).", "epic_call_tool"),
  },
  epic_get_bool: {
    epicSchema: S_epic_get_bool,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_bool", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get a bool control value at a specific frame. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, frame", "epic_call_tool"),
  },
  epic_get_bound_actor_class: {
    epicSchema: S_epic_get_bound_actor_class,
    epicTool: { toolset: "TemplateSequenceEditor.TemplateSequenceEditorSubsystem", name: "TemplateSequenceEditor.TemplateSequenceEditorSubsystem.GetBoundActorClass", engines: ["5.9"] },
    ...bp("read", "[Epic TemplateSequenceEditor.TemplateSequenceEditorSubsystem] The actor class the sequence binds to, or None when the sequence has no root binding. Available on UE 5.9; absent from UE 5.8. Params: templateSequence", "epic_call_tool"),
  },
  epic_get_bound_objects: {
    epicSchema: S_epic_get_bound_objects,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_bound_objects", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the objects currently resolved by a binding. Available on UE 5.8; absent from UE 5.9. Params: binding", "epic_call_tool"),
  },
  epic_get_bound_objects_for_binding: {
    epicSchema: S_epic_get_bound_objects_for_binding,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetBoundObjectsForBinding", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Resolve a binding in the active editor's focused sequence context. Cross-sequence bindings require unique instances in the root hierarchy. Available on UE 5.9; absent from UE 5.8. Params: binding", "epic_call_tool"),
  },
  epic_get_bounds: {
    epicSchema: S_epic_get_bounds,
    epicTool: { toolset: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", name: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_bounds", engines: ["5.8"], successors: [{ toolset: "EditorToolset.SkeletalMeshToolset", name: "EditorToolset.SkeletalMeshToolset.GetBounds", engines: ["5.9"], schema: S_epic_get_bounds__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the local-space bounding volume of a skeletal mesh. The bounds represent the reference pose and do not account for animation. Params: mesh. On UE 5.9 it calls EditorToolset.SkeletalMeshToolset.GetBounds (Params: mesh).", "epic_call_tool"),
  },
  epic_get_camera_cut_auto_arrange_sections: {
    epicSchema: S_epic_get_camera_cut_auto_arrange_sections,
    epicTool: { toolset: "SequencerScripting.MovieSceneTrackExtensions", name: "SequencerScripting.MovieSceneTrackExtensions.GetCameraCutAutoArrangeSections", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneTrackExtensions] Whether camera cut sections auto-arrange to fill gaps. Stays true on a track with more than one row, where auto-arrange is suspended. Available on UE 5.9; absent from UE 5.8. Params: track", "epic_call_tool"),
  },
  epic_get_camera_cut_can_blend: {
    epicSchema: S_epic_get_camera_cut_can_blend,
    epicTool: { toolset: "SequencerScripting.MovieSceneTrackExtensions", name: "SequencerScripting.MovieSceneTrackExtensions.GetCameraCutCanBlend", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneTrackExtensions] Whether overlapping camera cut sections blend between their cameras. Available on UE 5.9; absent from UE 5.8. Params: track", "epic_call_tool"),
  },
  epic_get_channel_keys: {
    epicSchema: S_epic_get_channel_keys,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetChannelKeys", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Snapshot keys as JSON with display frame, subframe, value, index and numeric interpolation. Binding IDs use exported text; events report their type, without Director Blueprint references; object references to assets the editor hides are reported as \"<inaccessible asset>\". Available on UE 5.9; absent from UE 5.8. Params: section, channelName", "epic_call_tool"),
  },
  epic_get_channel_keys_by_index: {
    epicSchema: S_epic_get_channel_keys_by_index,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetChannelKeysByIndex", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Snapshot requested key indices in order. Missing indices have null frame and value. Available on UE 5.9; absent from UE 5.8. Params: section, channelName, indices", "epic_call_tool"),
  },
  epic_get_channel_names: {
    epicSchema: S_epic_get_channel_names,
    epicTool: { toolset: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools", name: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_channel_names", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Get the names of all channels on a section. For example, a 3D Transform section has channels named 'Location.X', 'Location.Y', 'Location.Z', 'Rotation.X', etc. Available on UE 5.8; absent from UE 5.9. Params: section", "epic_call_tool"),
  },
  epic_get_channel_names__movie_scene_section_extensions: {
    epicSchema: S_epic_get_channel_names__movie_scene_section_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetChannelNames", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Get all scripting channel names, including NAME_None for unnamed channels. Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_get_channel_override_class: {
    epicSchema: S_epic_get_channel_override_class,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetChannelOverrideClass", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Get the class of the named channel's override, or None when the channel is not overridden. Available on UE 5.9; absent from UE 5.8. Params: section, channelName", "epic_call_tool"),
  },
  epic_get_channel_override_classes: {
    epicSchema: S_epic_get_channel_override_classes,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetChannelOverrideClasses", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] List the override classes that can replace the named channel, such as the float or double Perlin noise class. A float section's only channel is unnamed (None). Available on UE 5.9; absent from UE 5.8. Params: section, channelName", "epic_call_tool"),
  },
  epic_get_child_folders: {
    epicSchema: S_epic_get_child_folders,
    epicTool: { toolset: "SequencerScripting.MovieSceneFolderExtensions", name: "SequencerScripting.MovieSceneFolderExtensions.GetChildFolders", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneFolderExtensions] Get the child folders of a given folder Available on UE 5.9; absent from UE 5.8. Params: folder", "epic_call_tool"),
  },
  epic_get_child_object_bindings: {
    epicSchema: S_epic_get_child_object_bindings,
    epicTool: { toolset: "SequencerScripting.MovieSceneFolderExtensions", name: "SequencerScripting.MovieSceneFolderExtensions.GetChildObjectBindings", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneFolderExtensions] Get the object bindings contained by this folder Available on UE 5.9; absent from UE 5.8. Params: folder", "epic_call_tool"),
  },
  epic_get_child_possessables: {
    epicSchema: S_epic_get_child_possessables,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_child_possessables", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get component bindings under an actor binding. Actor bindings can own child possessable bindings for their components (e.g. SkeletalMeshComponent, CameraComponent). Use this to find the component binding when you need to add tracks to a specific component rather than the actor. Available on UE 5.8; absent from UE 5.9. Params: binding", "epic_call_tool"),
  },
  epic_get_child_possessables__movie_scene_binding_extensions: {
    epicSchema: S_epic_get_child_possessables__movie_scene_binding_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneBindingExtensions", name: "SequencerScripting.MovieSceneBindingExtensions.GetChildPossessables", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneBindingExtensions] Get all the children of this binding. Actor bindings can own child possessable bindings for their components (e.g. SkeletalMeshComponent, CameraComponent); use this to find the component binding when tracks belong on a specific component rather than the actor. Available on UE 5.9; absent from UE 5.8. Params: inBinding", "epic_call_tool"),
  },
  epic_get_child_tracks: {
    epicSchema: S_epic_get_child_tracks,
    epicTool: { toolset: "SequencerScripting.MovieSceneFolderExtensions", name: "SequencerScripting.MovieSceneFolderExtensions.GetChildTracks", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneFolderExtensions] Get the tracks contained by this folder Available on UE 5.9; absent from UE 5.8. Params: folder", "epic_call_tool"),
  },
  epic_get_children: {
    epicSchema: S_epic_get_children,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.get_children", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get children of a hierarchy element. Params: control_rig, item, recursive?", "epic_call_tool"),
  },
  epic_get_clock_source: {
    epicSchema: S_epic_get_clock_source,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_clock_source", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the clock source for the sequence. Available on UE 5.8; absent from UE 5.9. Params: sequence", "epic_call_tool"),
  },
  epic_get_clock_source__movie_scene_sequence_extensions: {
    epicSchema: S_epic_get_clock_source__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.GetClockSource", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Get the clock source for this sequence Available on UE 5.9; absent from UE 5.8. Params: inSequence", "epic_call_tool"),
  },
  epic_get_completion_mode: {
    epicSchema: S_epic_get_completion_mode,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetCompletionMode", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Return how the section's animated state is handled after evaluation. Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_get_connected_pins: {
    epicSchema: S_epic_get_connected_pins,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.get_connected_pins", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get all pins connected to this pin. Params: control_rig, pin", "epic_call_tool"),
  },
  epic_get_constraint_keys: {
    epicSchema: S_epic_get_constraint_keys,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetConstraintKeys", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get the constraint keys for the specified constraint Available on UE 5.9; absent from UE 5.8. Params: inConstraint, constraintSection, outBools, outFrames, timeUnit?", "epic_call_tool"),
  },
  epic_get_constraints_for_handle: {
    epicSchema: S_epic_get_constraints_for_handle,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetConstraintsForHandle", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get all constraints for this object, which is described by a transformable handle Available on UE 5.9; absent from UE 5.8. Params: inWorld, inChild", "epic_call_tool"),
  },
  epic_get_constraints_for_object: {
    epicSchema: S_epic_get_constraints_for_object,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetConstraintsForObject", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get the constraints whose child is an object, identified the same way as in AddConstraintBetween. Raises a script error if the world or object is null or the object is not transformable. Available on UE 5.9; absent from UE 5.8. Params: world, object, attachment", "epic_call_tool"),
  },
  epic_get_control_rig_available_spaces: {
    epicSchema: S_epic_get_control_rig_available_spaces,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigAvailableSpaces", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get the spaces the level editor's space picker lists for a control after its default parent and world, in picker order: the spaces added to this Control Rig instance, then the rig asset's available spaces that were not removed. Raises a script error if the Control Rig is null or the control does not exist. Available on UE 5.9; absent from UE 5.8. Params: controlRig, controlName", "epic_call_tool"),
  },
  epic_get_control_rig_controls: {
    epicSchema: S_epic_get_control_rig_controls,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigControls", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get the controls of a Control Rig in hierarchy order, with each control's value type. Available on UE 5.9; absent from UE 5.8. Params: controlRig", "epic_call_tool"),
  },
  epic_get_control_rig_controls_visible: {
    epicSchema: S_epic_get_control_rig_controls_visible,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigControlsVisible", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Whether a Control Rig's control shapes are visible in the viewport; with ModuleName, a module of a modular rig. Raises a script error if the Control Rig is null, or ModuleName is set and the rig is not modular or has no such module. Available on UE 5.9; absent from UE 5.8. Params: controlRig, moduleName?", "epic_call_tool"),
  },
  epic_get_control_rig_overrides: {
    epicSchema: S_epic_get_control_rig_overrides,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigOverrides", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get the control setting overrides on a Control Rig instance, from every override asset linked to it. Raises a script error if the Control Rig is null. Available on UE 5.9; absent from UE 5.8. Params: controlRig", "epic_call_tool"),
  },
  epic_get_control_rig_pose_asset_controls: {
    epicSchema: S_epic_get_control_rig_pose_asset_controls,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigPoseAssetControls", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get the names of the controls stored in a pose asset. Raises a script error if the pose asset is null. Available on UE 5.9; absent from UE 5.8. Params: poseAsset", "epic_call_tool"),
  },
  epic_get_control_rig_priority_order: {
    epicSchema: S_epic_get_control_rig_priority_order,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigPriorityOrder", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get the Control Rig track's priority order; tracks with a higher priority evaluate first. Available on UE 5.9; absent from UE 5.8. Params: inTrack", "epic_call_tool"),
  },
  epic_get_control_rig_rotation_order: {
    epicSchema: S_epic_get_control_rig_rotation_order,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigRotationOrder", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get a control's rotation order in its Control Rig track: its override, if any, and the order the rig asset authors. Raises a script error if the sequence is not the one focused in Sequencer, the Control Rig has no track in it, or the control does not exist or has no rotation. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName", "epic_call_tool"),
  },
  epic_get_control_rig_section_transform_mask: {
    epicSchema: S_epic_get_control_rig_section_transform_mask,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigSectionTransformMask", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get which transform and weight channels a Control Rig section animates (its Active Channels). Available on UE 5.9; absent from UE 5.8. Params: inSection", "epic_call_tool"),
  },
  epic_get_control_rig_space_keys: {
    epicSchema: S_epic_get_control_rig_space_keys,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigSpaceKeys", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get the control's space keys in time order, each with the space as SetControlRigSpace takes it. Raises a script error if the sequence is not the one focused in Sequencer or the control does not exist. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, timeUnit?", "epic_call_tool"),
  },
  epic_get_control_rig_world_transform: {
    epicSchema: S_epic_get_control_rig_world_transform,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigWorldTransform", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get ControlRig Control's World Transform at a specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, timeUnit?", "epic_call_tool"),
  },
  epic_get_control_rig_world_transforms: {
    epicSchema: S_epic_get_control_rig_world_transforms,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigWorldTransforms", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get ControlRig Control's World Transforms at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, timeUnit?", "epic_call_tool"),
  },
  epic_get_control_rig_world_transforms_async: {
    epicSchema: S_epic_get_control_rig_world_transforms_async,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigWorldTransformsAsync", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] GetControlRigWorldTransforms read after the next editor world tick. See GetControlRigWorldTransforms for the parameters. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, timeUnit?", "epic_call_tool"),
  },
  epic_get_control_rigs: {
    epicSchema: S_epic_get_control_rigs,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_control_rigs", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get all Control Rigs currently in the sequence. Returns proxy objects with track and rig references. Available on UE 5.8; absent from UE 5.9. Params: sequence", "epic_call_tool"),
  },
  epic_get_control_rigs__control_rig_sequencer_editor_library: {
    epicSchema: S_epic_get_control_rigs__control_rig_sequencer_editor_library,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlRigs", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get all of the control rigs and their bindings in the level sequence Available on UE 5.9; absent from UE 5.8. Params: levelSequence", "epic_call_tool"),
  },
  epic_get_control_values: {
    epicSchema: S_epic_get_control_values,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlValues", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get an array per frame of specified control rig values for the specified control names. Use URigHierarchy::Get*FromControlValue functions to convert these FRigControlValue to Bool,Rotator,EulerTransform, etc.. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlNames, frames, timeUnit?", "epic_call_tool"),
  },
  epic_get_control_values_async: {
    epicSchema: S_epic_get_control_values_async,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlValuesAsync", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] GetControlValues read after the next editor world tick. See GetControlValues for the parameters. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlNames, frames, timeUnit?", "epic_call_tool"),
  },
  epic_get_controls_info: {
    epicSchema: S_epic_get_controls_info,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_controls_info", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get all controls on a Control Rig with their names and types. Returns a list of controls with name and type so the caller can find controls of a specific type (e.g. find a Float control to use with set_float). Possible types: Bool, Float, Integer, Vector2D, Position, Rotator, Scale, Transform, TransformNoScale, EulerTransform. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path", "epic_call_tool"),
  },
  epic_get_controls_mask: {
    epicSchema: S_epic_get_controls_mask,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_controls_mask", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Check if a control is visible (unmasked) on a section. Available on UE 5.8; absent from UE 5.9. Params: section, control_name", "epic_call_tool"),
  },
  epic_get_controls_mask__control_rig_sequencer_editor_library: {
    epicSchema: S_epic_get_controls_mask__control_rig_sequencer_editor_library,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetControlsMask", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Whether the section animates the given control (true) or has it masked out (false). Available on UE 5.9; absent from UE 5.8. Params: inSection, controlName", "epic_call_tool"),
  },
  epic_get_current_level_sequence: {
    epicSchema: S_epic_get_current_level_sequence,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetCurrentLevelSequence", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Get the currently opened root level sequence asset Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_current_sequence: {
    epicSchema: S_epic_get_current_sequence,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_current_sequence", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the root level sequence currently open in the Sequencer editor. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_curve_editor_channels_with_selected_keys: {
    epicSchema: S_epic_get_curve_editor_channels_with_selected_keys,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetCurveEditorChannelsWithSelectedKeys", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Return channel proxies containing selected Curve Editor keys. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_curve_editor_pinned_channels: {
    epicSchema: S_epic_get_curve_editor_pinned_channels,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetCurveEditorPinnedChannels", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Return the channels whose curves are pinned in the active Sequencer's Curve Editor. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_curve_editor_selected_keys: {
    epicSchema: S_epic_get_curve_editor_selected_keys,
    epicTool: { toolset: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools", name: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_curve_editor_selected_keys", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Get selected key indices for a channel in the Curve Editor. Available on UE 5.8; absent from UE 5.9. Params: channel", "epic_call_tool"),
  },
  epic_get_curve_editor_selected_keys__level_sequence_editor_subsystem: {
    epicSchema: S_epic_get_curve_editor_selected_keys__level_sequence_editor_subsystem,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetCurveEditorSelectedKeys", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Return selected key indices for a channel proxy in the active Curve Editor. Available on UE 5.9; absent from UE 5.8. Params: channel", "epic_call_tool"),
  },
  epic_get_curve_editor_view_mode: {
    epicSchema: S_epic_get_curve_editor_view_mode,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetCurveEditorViewMode", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Report how the active Sequencer's Curve Editor lays out its curves. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_custom_binding_objects: {
    epicSchema: S_epic_get_custom_binding_objects,
    epicTool: { toolset: "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools", name: "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.get_custom_binding_objects", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetCustomBindingObjects", engines: ["5.9"], schema: S_epic_get_custom_binding_objects__1 }] },
    ...bp("read", "[Epic animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools] Get the custom binding instances for a binding. Params: binding. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.GetCustomBindingObjects (Params: objectBinding).", "epic_call_tool"),
  },
  epic_get_custom_binding_type: {
    epicSchema: S_epic_get_custom_binding_type,
    epicTool: { toolset: "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools", name: "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.get_custom_binding_type", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetCustomBindingType", engines: ["5.9"], schema: S_epic_get_custom_binding_type__1 }] },
    ...bp("read", "[Epic animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools] Get the custom binding class for a binding. Returns the class path of the custom binding type, or an empty string for standard possessable bindings. Params: binding. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.GetCustomBindingType (Params: objectBinding).", "epic_call_tool"),
  },
  epic_get_custom_bindings_of_type: {
    epicSchema: S_epic_get_custom_bindings_of_type,
    epicTool: { toolset: "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools", name: "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.get_custom_bindings_of_type", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetCustomBindingsOfType", engines: ["5.9"], schema: S_epic_get_custom_bindings_of_type__1 }] },
    ...bp("read", "[Epic animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools] Find all bindings of a given custom type in the current sequence. Params: binding_type_class. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.GetCustomBindingsOfType (Params: customBindingType).", "epic_call_tool"),
  },
  epic_get_custom_clock_source: {
    epicSchema: S_epic_get_custom_clock_source,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.GetCustomClockSource", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Get the actor or asset driving the sequence's custom clock, or None when the sequence does not use one or it is not loaded. Raises if the source is hidden in this editor. Available on UE 5.9; absent from UE 5.8. Params: sequence", "epic_call_tool"),
  },
  epic_get_custom_time_warp_class: {
    epicSchema: S_epic_get_custom_time_warp_class,
    epicTool: { toolset: "SequencerScripting.MovieSceneTimeWarpExtensions", name: "SequencerScripting.MovieSceneTimeWarpExtensions.GetCustomTimeWarpClass", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneTimeWarpExtensions] Get the class of a section's custom time warp, or None when the section does not use one. Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_get_deactivated_nodes: {
    epicSchema: S_epic_get_deactivated_nodes,
    epicTool: { toolset: "animation_toolset.toolsets.outliner.SequencerOutlinerTools", name: "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_deactivated_nodes", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetDeactivatedNodes", engines: ["5.9"], schema: S_epic_get_deactivated_nodes__1 }] },
    ...bp("read", "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Get the currently deactivated outliner nodes. Params: none. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.GetDeactivatedNodes (Params: none).", "epic_call_tool"),
  },
  epic_get_default_parent_key: {
    epicSchema: S_epic_get_default_parent_key,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetDefaultParentKey", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get the key that names a control's default parent, for use as a space in SetControlRigSpace. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_default_value: {
    epicSchema: S_epic_get_default_value,
    epicTool: { toolset: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools", name: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_default_value", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Get the default value of a float channel. Available on UE 5.8; absent from UE 5.9. Params: section, channel_name", "epic_call_tool"),
  },
  epic_get_deformer_info: {
    epicSchema: S_epic_get_deformer_info,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.GetDeformerInfo", engines: ["5.9"] },
    ...bp("read", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Returns the compile status, graphs, variables, resources and component bindings of a deformer. Available on UE 5.9; absent from UE 5.8. Params: deformer", "epic_call_tool"),
  },
  epic_get_directly_selected_folders: {
    epicSchema: S_epic_get_directly_selected_folders,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetDirectlySelectedFolders", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Gets only folders explicitly selected in the outliner. Returns an empty array when no Sequencer is open. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_display_name: {
    epicSchema: S_epic_get_display_name,
    epicTool: { toolset: "SequencerScripting.MovieSceneBindingExtensions", name: "SequencerScripting.MovieSceneBindingExtensions.GetDisplayName", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneBindingExtensions] Get this binding's name Available on UE 5.9; absent from UE 5.8. Params: inBinding", "epic_call_tool"),
  },
  epic_get_display_name__movie_scene_track_extensions: {
    epicSchema: S_epic_get_display_name__movie_scene_track_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneTrackExtensions", name: "SequencerScripting.MovieSceneTrackExtensions.GetDisplayName", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneTrackExtensions] Get this track's display name Available on UE 5.9; absent from UE 5.8. Params: track", "epic_call_tool"),
  },
  epic_get_display_rate: {
    epicSchema: S_epic_get_display_rate,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_display_rate", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the display frame rate of a sequence. Available on UE 5.8; absent from UE 5.9. Params: sequence", "epic_call_tool"),
  },
  epic_get_display_rate__movie_scene_sequence_extensions: {
    epicSchema: S_epic_get_display_rate__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.GetDisplayRate", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Gets this sequence's display rate Available on UE 5.9; absent from UE 5.8. Params: sequence", "epic_call_tool"),
  },
  epic_get_double_default: {
    epicSchema: S_epic_get_double_default,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetDoubleDefault", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Get the double default; use HasDefault to distinguish an absent default. Available on UE 5.9; absent from UE 5.8. Params: channel", "epic_call_tool"),
  },
  epic_get_double_key_value: {
    epicSchema: S_epic_get_double_key_value,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetDoubleKeyValue", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Read a live double key at double precision. Available on UE 5.9; absent from UE 5.8. Params: channel, key", "epic_call_tool"),
  },
  epic_get_ease_in_duration: {
    epicSchema: S_epic_get_ease_in_duration,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionEasingExtensions", name: "SequencerScripting.MovieSceneSectionEasingExtensions.GetEaseInDuration", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionEasingExtensions] Get the effective ease-in duration in tick-resolution frames (not display-rate frames), considering manual override Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_get_ease_out_duration: {
    epicSchema: S_epic_get_ease_out_duration,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionEasingExtensions", name: "SequencerScripting.MovieSceneSectionEasingExtensions.GetEaseOutDuration", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionEasingExtensions] Get the effective ease-out duration in tick-resolution frames (not display-rate frames), considering manual override Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_get_elements: {
    epicSchema: S_epic_get_elements,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.get_elements", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get hierarchy elements of the specified type. Params: control_rig, element_type?", "epic_call_tool"),
  },
  epic_get_emulated_network_mask: {
    epicSchema: S_epic_get_emulated_network_mask,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetEmulatedNetworkMask", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] The network mode the active Sequencer's root sequence emulates during playback. Session state; not saved with the sequence. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_enabled_film_overlay_frames: {
    epicSchema: S_epic_get_enabled_film_overlay_frames,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetEnabledFilmOverlayFrames", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] The frame overlays currently enabled Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_end_frame: {
    epicSchema: S_epic_get_end_frame,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetEndFrame", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Get the exclusive end in display-rate frames, rounded down. Raises a script error and returns -1 when unbounded. Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_get_enum_track_enum: {
    epicSchema: S_epic_get_enum_track_enum,
    epicTool: { toolset: "SequencerScripting.MovieScenePropertyTrackExtensions", name: "SequencerScripting.MovieScenePropertyTrackExtensions.GetEnumTrackEnum", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieScenePropertyTrackExtensions] Get the enum whose values an enum track animates, or None when it has not been set. Raises if the enum is hidden in this editor. Available on UE 5.9; absent from UE 5.8. Params: track", "epic_call_tool"),
  },
  epic_get_euler_transform: {
    epicSchema: S_epic_get_euler_transform,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_euler_transform", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get an EulerTransform control value at a specific frame. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, frame", "epic_call_tool"),
  },
  epic_get_evaluation_type: {
    epicSchema: S_epic_get_evaluation_type,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_evaluation_type", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the evaluation type of a sequence. Available on UE 5.8; absent from UE 5.9. Params: sequence", "epic_call_tool"),
  },
  epic_get_evaluation_type__movie_scene_sequence_extensions: {
    epicSchema: S_epic_get_evaluation_type__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.GetEvaluationType", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Get the evaluation type for this sequence Available on UE 5.9; absent from UE 5.8. Params: inSequence", "epic_call_tool"),
  },
  epic_get_event_graph: {
    epicSchema: S_epic_get_event_graph,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.get_event_graph", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get a graph containing the specified event type. Params: control_rig, event_type", "epic_call_tool"),
  },
  epic_get_film_overlay: {
    epicSchema: S_epic_get_film_overlay,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetFilmOverlay", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] The composition overlay of a Cinematic Viewport, or None when it shows none. Raises when the viewport is not open. Available on UE 5.9; absent from UE 5.8. Params: viewport", "epic_call_tool"),
  },
  epic_get_film_overlay_frame_names: {
    epicSchema: S_epic_get_film_overlay_frame_names,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetFilmOverlayFrameNames", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Names of the frame overlays, such as TitleSafe and LetterBox, any number of which every Cinematic Viewport shows at once Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_film_overlay_frame_tint: {
    epicSchema: S_epic_get_film_overlay_frame_tint,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetFilmOverlayFrameTint", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] The color a frame overlay draws with. Raises when the name is unknown. Available on UE 5.9; absent from UE 5.8. Params: frameName", "epic_call_tool"),
  },
  epic_get_film_overlay_names: {
    epicSchema: S_epic_get_film_overlay_names,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetFilmOverlayNames", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Names of the composition overlays a Cinematic Viewport shows one of at a time, such as Grid3x3 Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_film_overlay_tint: {
    epicSchema: S_epic_get_film_overlay_tint,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetFilmOverlayTint", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] The tint a Cinematic Viewport applies to the composition overlay it shows. Raises when the viewport is not open. Available on UE 5.9; absent from UE 5.8. Params: viewport", "epic_call_tool"),
  },
  epic_get_fkcontrol_rig_active_controls: {
    epicSchema: S_epic_get_fkcontrol_rig_active_controls,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetFKControlRigActiveControls", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get the controls an FK Control Rig animates, as its Select Bones To Animate dialog shows them checked. Raises a script error if the Control Rig is null or not an FK Control Rig. Available on UE 5.9; absent from UE 5.8. Params: controlRig", "epic_call_tool"),
  },
  epic_get_fkcontrol_rig_apply_mode: {
    epicSchema: S_epic_get_fkcontrol_rig_apply_mode,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetFKControlRigApplyMode", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get FKControlRig Apply Mode. The rig must be an FK Control Rig. Available on UE 5.9; absent from UE 5.8. Params: inControlRig", "epic_call_tool"),
  },
  epic_get_float: {
    epicSchema: S_epic_get_float,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_float", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get a float control value at a specific frame. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, frame", "epic_call_tool"),
  },
  epic_get_float_default: {
    epicSchema: S_epic_get_float_default,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetFloatDefault", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Get the float default; use HasDefault to distinguish an absent default. Available on UE 5.9; absent from UE 5.8. Params: channel", "epic_call_tool"),
  },
  epic_get_float_key_value: {
    epicSchema: S_epic_get_float_key_value,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetFloatKeyValue", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Read a live float key's value. Available on UE 5.9; absent from UE 5.8. Params: channel, key", "epic_call_tool"),
  },
  epic_get_focused_level_sequence: {
    epicSchema: S_epic_get_focused_level_sequence,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetFocusedLevelSequence", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Get the focused level sequence, which may be a subsequence of the opened root sequence. Returns nullptr when no Sequencer is open or the focused sequence is not a level sequence. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_focused_sequence: {
    epicSchema: S_epic_get_focused_sequence,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_focused_sequence", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the currently focused level sequence in the hierarchy. When navigated into a sub-sequence, this returns the sub-sequence rather than the root sequence. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_folder_color: {
    epicSchema: S_epic_get_folder_color,
    epicTool: { toolset: "SequencerScripting.MovieSceneFolderExtensions", name: "SequencerScripting.MovieSceneFolderExtensions.GetFolderColor", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneFolderExtensions] Get the display color of the given folder Available on UE 5.9; absent from UE 5.8. Params: folder", "epic_call_tool"),
  },
  epic_get_folder_contents: {
    epicSchema: S_epic_get_folder_contents,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_folder_contents", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the tracks and bindings inside a folder. Available on UE 5.8; absent from UE 5.9. Params: folder", "epic_call_tool"),
  },
  epic_get_folder_name: {
    epicSchema: S_epic_get_folder_name,
    epicTool: { toolset: "SequencerScripting.MovieSceneFolderExtensions", name: "SequencerScripting.MovieSceneFolderExtensions.GetFolderName", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneFolderExtensions] Get the given folder's display name Available on UE 5.9; absent from UE 5.8. Params: folder", "epic_call_tool"),
  },
  epic_get_forward_solve_graph: {
    epicSchema: S_epic_get_forward_solve_graph,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.get_forward_solve_graph", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get the forward solve graph (main execution graph). This graph contains the BeginExecution event and runs during normal animation evaluation. Params: control_rig", "epic_call_tool"),
  },
  epic_get_global_position: {
    epicSchema: S_epic_get_global_position,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetGlobalPosition", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Get the current global playhead position Available on UE 5.9; absent from UE 5.8. Params: timeUnit?", "epic_call_tool"),
  },
  epic_get_global_transform: {
    epicSchema: S_epic_get_global_transform,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.get_global_transform", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get global transform of a hierarchy element. Params: control_rig, item, initial?", "epic_call_tool"),
  },
  epic_get_graph: {
    epicSchema: S_epic_get_graph,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.get_graph", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get a specific graph from the Control Rig by name. Params: control_rig, graph_name", "epic_call_tool"),
  },
  epic_get_graph_info: {
    epicSchema: S_epic_get_graph_info,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.GetGraphInfo", engines: ["5.9"] },
    ...bp("read", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Graphs Available on UE 5.9; absent from UE 5.8. Params: deformer, graphPath", "epic_call_tool"),
  },
  epic_get_hidden_nodes: {
    epicSchema: S_epic_get_hidden_nodes,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetHiddenNodes", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Nodes hidden through SetNodeHidden, without the descendants hidden with them Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_int: {
    epicSchema: S_epic_get_int,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_int", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get an integer control value at a specific frame. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, frame", "epic_call_tool"),
  },
  epic_get_interaction_graph: {
    epicSchema: S_epic_get_interaction_graph,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.get_interaction_graph", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get the interaction graph. The interaction graph contains the InteractionExecution event and runs during user interaction with controls. Params: control_rig", "epic_call_tool"),
  },
  epic_get_isolated_nodes: {
    epicSchema: S_epic_get_isolated_nodes,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetIsolatedNodes", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Nodes isolated through SetNodeIsolated, without the descendants shown with them Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_kernel_info: {
    epicSchema: S_epic_get_kernel_info,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.GetKernelInfo", engines: ["5.9"] },
    ...bp("read", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Kernels Available on UE 5.9; absent from UE 5.8. Params: deformer, nodePath", "epic_call_tool"),
  },
  epic_get_key_time: {
    epicSchema: S_epic_get_key_time,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetKeyTime", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Return a live key's time in display-rate frames by default, or in tick-resolution frames. Available on UE 5.9; absent from UE 5.8. Params: channel, key, timeUnit?", "epic_call_tool"),
  },
  epic_get_keys: {
    epicSchema: S_epic_get_keys,
    epicTool: { toolset: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools", name: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_keys", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Get all keys on a channel, returned as a JSON array. Each key entry includes its frame number and value. Available on UE 5.8; absent from UE 5.9. Params: section, channel_name", "epic_call_tool"),
  },
  epic_get_keys__movie_scene_section_extensions: {
    epicSchema: S_epic_get_keys__movie_scene_section_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetKeys", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Return all keys, including keys outside the section range. Available on UE 5.9; absent from UE 5.8. Params: channel", "epic_call_tool"),
  },
  epic_get_keys_by_index: {
    epicSchema: S_epic_get_keys_by_index,
    epicTool: { toolset: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools", name: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_keys_by_index", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Get specific keys on a channel by their indices, returned as JSON. Useful for resolving the keys behind a Curve Editor selection, which provides indices rather than key objects. Available on UE 5.8; absent from UE 5.9. Params: section, channel_name, indices", "epic_call_tool"),
  },
  epic_get_letterbox_aspect_ratio: {
    epicSchema: S_epic_get_letterbox_aspect_ratio,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetLetterboxAspectRatio", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] The width and height terms of the ratio the LetterBox frame overlay masks the viewport to, as X and Y, as the Film Overlay menu shows them Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_level_sequence_link_from_anim_sequence: {
    epicSchema: S_epic_get_level_sequence_link_from_anim_sequence,
    epicTool: { toolset: "SequencerScriptingEditor.SequencerToolsFunctionLibrary", name: "SequencerScriptingEditor.SequencerToolsFunctionLibrary.GetLevelSequenceLinkFromAnimSequence", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScriptingEditor.SequencerToolsFunctionLibrary] Get the link to the level sequence if it exists on this anim sequence Available on UE 5.9; absent from UE 5.8. Params: inAnimSequence", "epic_call_tool"),
  },
  epic_get_linked_anim_sequences: {
    epicSchema: S_epic_get_linked_anim_sequences,
    epicTool: { toolset: "animation_toolset.toolsets.import_export.SequencerImportExportTools", name: "animation_toolset.toolsets.import_export.SequencerImportExportTools.get_linked_anim_sequences", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.import_export.SequencerImportExportTools] Get content paths of all AnimSequences linked to a LevelSequence. Linked AnimSequences auto-update when the LevelSequence changes. They are created via export_anim_sequence with create_link=True. Available on UE 5.8; absent from UE 5.9. Params: sequence", "epic_call_tool"),
  },
  epic_get_linked_anim_sequences__control_rig_sequencer_editor_library: {
    epicSchema: S_epic_get_linked_anim_sequences__control_rig_sequencer_editor_library,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLinkedAnimSequences", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get the Anim Sequences linked to a binding, with each link's export settings, as Create Linked Animation Sequence and Edit in Sequencer's link settings store them on the level sequence. Raises a script error if the binding has no level sequence. Available on UE 5.9; absent from UE 5.8. Params: binding", "epic_call_tool"),
  },
  epic_get_linked_anim_track: {
    epicSchema: S_epic_get_linked_anim_track,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLinkedAnimTrack", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get the linked animation track for the given binding (read-only lookup). Available on UE 5.9; absent from UE 5.8. Params: inBinding", "epic_call_tool"),
  },
  epic_get_linked_level_sequence: {
    epicSchema: S_epic_get_linked_level_sequence,
    epicTool: { toolset: "animation_toolset.toolsets.import_export.SequencerImportExportTools", name: "animation_toolset.toolsets.import_export.SequencerImportExportTools.get_linked_level_sequence", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.import_export.SequencerImportExportTools] Get the content path of the LevelSequence linked to an AnimSequence. Available on UE 5.8; absent from UE 5.9. Params: anim_sequence", "epic_call_tool"),
  },
  epic_get_local_control_rig_bool: {
    epicSchema: S_epic_get_local_control_rig_bool,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigBool", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get ControlRig Control's bool value at a specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, timeUnit?", "epic_call_tool"),
  },
  epic_get_local_control_rig_bools: {
    epicSchema: S_epic_get_local_control_rig_bools,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigBools", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get ControlRig Control's bool values at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, timeUnit?", "epic_call_tool"),
  },
  epic_get_local_control_rig_euler_transform: {
    epicSchema: S_epic_get_local_control_rig_euler_transform,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigEulerTransform", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get ControlRig Control's EulerTransform value at a specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, timeUnit?", "epic_call_tool"),
  },
  epic_get_local_control_rig_euler_transforms: {
    epicSchema: S_epic_get_local_control_rig_euler_transforms,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigEulerTransforms", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get ControlRig Control's EulerTransform values at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, timeUnit?", "epic_call_tool"),
  },
  epic_get_local_control_rig_float: {
    epicSchema: S_epic_get_local_control_rig_float,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigFloat", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get ControlRig Control's float value at a specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, timeUnit?", "epic_call_tool"),
  },
  epic_get_local_control_rig_floats: {
    epicSchema: S_epic_get_local_control_rig_floats,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigFloats", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get ControlRig Control's float values at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, timeUnit?", "epic_call_tool"),
  },
  epic_get_local_control_rig_int: {
    epicSchema: S_epic_get_local_control_rig_int,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigInt", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get ControlRig Control's integer value at a specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, timeUnit?", "epic_call_tool"),
  },
  epic_get_local_control_rig_ints: {
    epicSchema: S_epic_get_local_control_rig_ints,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigInts", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get ControlRig Control's integer values at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, timeUnit?", "epic_call_tool"),
  },
  epic_get_local_control_rig_position: {
    epicSchema: S_epic_get_local_control_rig_position,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigPosition", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get ControlRig Control's Position value at a specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, timeUnit?", "epic_call_tool"),
  },
  epic_get_local_control_rig_positions: {
    epicSchema: S_epic_get_local_control_rig_positions,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigPositions", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get ControlRig Control's Position values at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, timeUnit?", "epic_call_tool"),
  },
  epic_get_local_control_rig_rotator: {
    epicSchema: S_epic_get_local_control_rig_rotator,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigRotator", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get ControlRig Control's Rotator value at a specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, timeUnit?", "epic_call_tool"),
  },
  epic_get_local_control_rig_rotators: {
    epicSchema: S_epic_get_local_control_rig_rotators,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigRotators", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get ControlRig Control's Rotator values at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, timeUnit?", "epic_call_tool"),
  },
  epic_get_local_control_rig_scale: {
    epicSchema: S_epic_get_local_control_rig_scale,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigScale", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get ControlRig Control's Scale value at a specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, timeUnit?", "epic_call_tool"),
  },
  epic_get_local_control_rig_scales: {
    epicSchema: S_epic_get_local_control_rig_scales,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigScales", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get ControlRig Control's Scale values at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, timeUnit?", "epic_call_tool"),
  },
  epic_get_local_control_rig_transform: {
    epicSchema: S_epic_get_local_control_rig_transform,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigTransform", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get ControlRig Control's Transform value at a specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, timeUnit?", "epic_call_tool"),
  },
  epic_get_local_control_rig_transform_no_scale: {
    epicSchema: S_epic_get_local_control_rig_transform_no_scale,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigTransformNoScale", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get ControlRig Control's TransformNoScale value at a specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, timeUnit?", "epic_call_tool"),
  },
  epic_get_local_control_rig_transform_no_scales: {
    epicSchema: S_epic_get_local_control_rig_transform_no_scales,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigTransformNoScales", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get ControlRig Control's TransformNoScale values at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, timeUnit?", "epic_call_tool"),
  },
  epic_get_local_control_rig_transforms: {
    epicSchema: S_epic_get_local_control_rig_transforms,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigTransforms", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get ControlRig Control's Transform values at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, timeUnit?", "epic_call_tool"),
  },
  epic_get_local_control_rig_vector2_d: {
    epicSchema: S_epic_get_local_control_rig_vector2_d,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigVector2D", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get ControlRig Control's Vector2D value at a specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, timeUnit?", "epic_call_tool"),
  },
  epic_get_local_control_rig_vector2_ds: {
    epicSchema: S_epic_get_local_control_rig_vector2_ds,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetLocalControlRigVector2Ds", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get ControlRig Control's Vector2D values at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, timeUnit?", "epic_call_tool"),
  },
  epic_get_local_transform: {
    epicSchema: S_epic_get_local_transform,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.get_local_transform", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get local transform of a hierarchy element. Params: control_rig, item, initial?", "epic_call_tool"),
  },
  epic_get_locked_nodes: {
    epicSchema: S_epic_get_locked_nodes,
    epicTool: { toolset: "animation_toolset.toolsets.outliner.SequencerOutlinerTools", name: "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_locked_nodes", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetLockedNodes", engines: ["5.9"], schema: S_epic_get_locked_nodes__1 }] },
    ...bp("read", "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Get the currently locked outliner nodes. Params: none. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.GetLockedNodes (Params: none).", "epic_call_tool"),
  },
  epic_get_lod_count: {
    epicSchema: S_epic_get_lod_count,
    epicTool: { toolset: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", name: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_lod_count", engines: ["5.8"], successors: [{ toolset: "EditorToolset.SkeletalMeshToolset", name: "EditorToolset.SkeletalMeshToolset.GetLodCount", engines: ["5.9"], schema: S_epic_get_lod_count__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the number of LODs in a skeletal mesh asset. Params: mesh. On UE 5.9 it calls EditorToolset.SkeletalMeshToolset.GetLodCount (Params: mesh).", "epic_call_tool"),
  },
  epic_get_loop_info: {
    epicSchema: S_epic_get_loop_info,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.GetLoopInfo", engines: ["5.9"] },
    ...bp("read", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] GetLoopInfo Available on UE 5.9; absent from UE 5.8. Params: deformer, nodePath", "epic_call_tool"),
  },
  epic_get_loop_mode: {
    epicSchema: S_epic_get_loop_mode,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_loop_mode", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the current loop playback mode. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_loop_mode__level_sequence_editor_blueprint_library: {
    epicSchema: S_epic_get_loop_mode__level_sequence_editor_blueprint_library,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetLoopMode", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Get loop mode (note this is a per user preference) Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_marked_frames: {
    epicSchema: S_epic_get_marked_frames,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_marked_frames", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get all marked frames (bookmarks) in the sequence. Available on UE 5.8; absent from UE 5.9. Params: sequence", "epic_call_tool"),
  },
  epic_get_marked_frames_from_sequence: {
    epicSchema: S_epic_get_marked_frames_from_sequence,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.GetMarkedFramesFromSequence", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Get the marks and mark ranges for this sequence Available on UE 5.9; absent from UE 5.8. Params: sequence, timeUnit?", "epic_call_tool"),
  },
  epic_get_material: {
    epicSchema: S_epic_get_material,
    epicTool: { toolset: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", name: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_material", engines: ["5.8"], successors: [{ toolset: "EditorToolset.SkeletalMeshToolset", name: "EditorToolset.SkeletalMeshToolset.GetMaterial", engines: ["5.9"], schema: S_epic_get_material__1, rename: {"slot_name":"slotName"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the material assigned to a named slot on a skeletal mesh. Params: mesh, slot_name. On UE 5.9 it calls EditorToolset.SkeletalMeshToolset.GetMaterial (Params: mesh, slotName).", "epic_call_tool"),
  },
  epic_get_material_slots: {
    epicSchema: S_epic_get_material_slots,
    epicTool: { toolset: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", name: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_material_slots", engines: ["5.8"], successors: [{ toolset: "EditorToolset.SkeletalMeshToolset", name: "EditorToolset.SkeletalMeshToolset.GetMaterialSlots", engines: ["5.9"], schema: S_epic_get_material_slots__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the names of all material slots in a skeletal mesh. Material slot names are used when assigning materials to specific parts of the mesh. Use these names with get_material and set_material. Params: mesh. On UE 5.9 it calls EditorToolset.SkeletalMeshToolset.GetMaterialSlots (Params: mesh).", "epic_call_tool"),
  },
  epic_get_morph_target_names: {
    epicSchema: S_epic_get_morph_target_names,
    epicTool: { toolset: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", name: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_morph_target_names", engines: ["5.8"], successors: [{ toolset: "EditorToolset.SkeletalMeshToolset", name: "EditorToolset.SkeletalMeshToolset.GetMorphTargetNames", engines: ["5.9"], schema: S_epic_get_morph_target_names__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the names of all morph targets on a skeletal mesh. Morph targets (blend shapes) are per-vertex offsets used to deform the mesh, commonly used for facial expressions and cloth simulation. Params: mesh. On UE 5.9 it calls EditorToolset.SkeletalMeshToolset.GetMorphTargetNames (Params: mesh).", "epic_call_tool"),
  },
  epic_get_motion_trail_settings: {
    epicSchema: S_epic_get_motion_trail_settings,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.GetMotionTrailSettings", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] GetMotionTrailSettings Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_muted_nodes: {
    epicSchema: S_epic_get_muted_nodes,
    epicTool: { toolset: "animation_toolset.toolsets.outliner.SequencerOutlinerTools", name: "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_muted_nodes", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetMutedNodes", engines: ["5.9"], schema: S_epic_get_muted_nodes__1 }] },
    ...bp("read", "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Get the currently muted outliner nodes. Params: none. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.GetMutedNodes (Params: none).", "epic_call_tool"),
  },
  epic_get_node_group_name: {
    epicSchema: S_epic_get_node_group_name,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetNodeGroupName", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Get Node Group Name Available on UE 5.9; absent from UE 5.8. Params: group", "epic_call_tool"),
  },
  epic_get_node_group_nodes: {
    epicSchema: S_epic_get_node_group_nodes,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetNodeGroupNodes", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] The members of a Sequencer Group that the outliner currently shows with a unique path, in tree order Available on UE 5.9; absent from UE 5.8. Params: group", "epic_call_tool"),
  },
  epic_get_node_group_paths: {
    epicSchema: S_epic_get_node_group_paths,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetNodeGroupPaths", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Every node path stored in a Sequencer Group, in stored order, including paths of nodes the outliner no longer shows Available on UE 5.9; absent from UE 5.8. Params: group", "epic_call_tool"),
  },
  epic_get_node_groups: {
    epicSchema: S_epic_get_node_groups,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetNodeGroups", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] The Sequencer Groups of the focused sequence, as the Group Manager lists them Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_node_info: {
    epicSchema: S_epic_get_node_info,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.GetNodeInfo", engines: ["5.9"] },
    ...bp("read", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Nodes Available on UE 5.9; absent from UE 5.8. Params: deformer, nodePath", "epic_call_tool"),
  },
  epic_get_node_label: {
    epicSchema: S_epic_get_node_label,
    epicTool: { toolset: "animation_toolset.toolsets.outliner.SequencerOutlinerTools", name: "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_node_label", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetNodeLabel", engines: ["5.9"], schema: S_epic_get_node_label__1 }] },
    ...bp("read", "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Get the display label of an outliner node. Params: node. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.GetNodeLabel (Params: node).", "epic_call_tool"),
  },
  epic_get_node_position: {
    epicSchema: S_epic_get_node_position,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.get_node_position", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get the position of a node in the graph editor. Params: control_rig, node", "epic_call_tool"),
  },
  epic_get_num_channels_used: {
    epicSchema: S_epic_get_num_channels_used,
    epicTool: { toolset: "SequencerScripting.MovieSceneDoubleVectorTrackExtensions", name: "SequencerScripting.MovieSceneDoubleVectorTrackExtensions.GetNumChannelsUsed", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneDoubleVectorTrackExtensions] Get the number of channels used for this track Available on UE 5.9; absent from UE 5.8. Params: track", "epic_call_tool"),
  },
  epic_get_num_channels_used__movie_scene_float_vector_track_extensions: {
    epicSchema: S_epic_get_num_channels_used__movie_scene_float_vector_track_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneFloatVectorTrackExtensions", name: "SequencerScripting.MovieSceneFloatVectorTrackExtensions.GetNumChannelsUsed", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneFloatVectorTrackExtensions] Get the number of channels used for this track Available on UE 5.9; absent from UE 5.8. Params: track", "epic_call_tool"),
  },
  epic_get_numeric_channels: {
    epicSchema: S_epic_get_numeric_channels,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetNumericChannels", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Get the section's float channels followed by its double channels. Raises and returns an empty array when any channel name is ambiguous or has no live entry. Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_get_open_cinematic_viewports: {
    epicSchema: S_epic_get_open_cinematic_viewports,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetOpenCinematicViewports", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] The Cinematic Viewports the level editor shows, in its viewport order, by tab then slot Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_or_create_director_blueprint: {
    epicSchema: S_epic_get_or_create_director_blueprint,
    epicTool: { toolset: "AnimationAssistantToolset.SequencerDirectorToolset", name: "AnimationAssistantToolset.SequencerDirectorToolset.GetOrCreateDirectorBlueprint", engines: ["5.9"] },
    ...bp("mutate", "[Epic AnimationAssistantToolset.SequencerDirectorToolset] Return the Director Blueprint owned by the sequence, creating it if one does not yet exist. Returns nullptr if the sequence type does not support a Director Blueprint (e.g. a UTemplateSequence variant that has no director-BP class registered). Once you have the UBlueprint handle, drive BlueprintToolset against it for low-level authoring - AddFunctionGraph, AddFunctionParam, CreateNode, ConnectPins, CompileBlueprint - or BlueprintDslToolset.Write for a textual graph description. Available on UE 5.9; absent from UE 5.8. Params: sequence", "epic_call_tool"),
  },
  epic_get_or_create_linked_anim_track: {
    epicSchema: S_epic_get_or_create_linked_anim_track,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetOrCreateLinkedAnimTrack", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Find or create the linked animation track for the given binding. Same as Configure AutoBake in the UI. Creates the necessary track structure, linked anim sequence, and section if none exist. Available on UE 5.9; absent from UE 5.8. Params: inBinding", "epic_call_tool"),
  },
  epic_get_origin_override_mask: {
    epicSchema: S_epic_get_origin_override_mask,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetOriginOverrideMask", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Get the transform channels a shot or subsequence section's origin override animates. Scale and weight are always false. Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_get_outliner_children: {
    epicSchema: S_epic_get_outliner_children,
    epicTool: { toolset: "animation_toolset.toolsets.outliner.SequencerOutlinerTools", name: "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_outliner_children", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetOutlinerChildren", engines: ["5.9"], schema: S_epic_get_outliner_children__1, rename: {"type_filter":"typeFilter"} }] },
    ...bp("read", "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Get child nodes of an outliner node. Params: node, type_filter?. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.GetOutlinerChildren (Params: node, typeFilter?).", "epic_call_tool"),
  },
  epic_get_outliner_nodes: {
    epicSchema: S_epic_get_outliner_nodes,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetOutlinerNodes", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] List addressable nodes in tree order, including their current labels and types. Nodes that share a path with another node are omitted. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_outliner_paths: {
    epicSchema: S_epic_get_outliner_paths,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetOutlinerPaths", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] List the unique node paths in tree order; paths shared by several nodes are omitted. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_outliner_selection: {
    epicSchema: S_epic_get_outliner_selection,
    epicTool: { toolset: "animation_toolset.toolsets.outliner.SequencerOutlinerTools", name: "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_outliner_selection", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetOutlinerSelection", engines: ["5.9"], schema: S_epic_get_outliner_selection__1 }] },
    ...bp("read", "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Get the currently selected nodes in the outliner. Params: none. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.GetOutlinerSelection (Params: none).", "epic_call_tool"),
  },
  epic_get_outliner_tree: {
    epicSchema: S_epic_get_outliner_tree,
    epicTool: { toolset: "animation_toolset.toolsets.outliner.SequencerOutlinerTools", name: "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_outliner_tree", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetOutlinerTree", engines: ["5.9"], schema: S_epic_get_outliner_tree__1 }] },
    ...bp("read", "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Get a full snapshot of the Sequencer outliner tree. Builds a recursive tree structure from the outliner root nodes. Useful for testing and UI verification. Params: none. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.GetOutlinerTree (Params: none).", "epic_call_tool"),
  },
  epic_get_overridden_channel_names: {
    epicSchema: S_epic_get_overridden_channel_names,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetOverriddenChannelNames", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] List the names of the section's channels that are replaced by a procedural override, such as Perlin noise. Overridden channels are not returned by GetChannelNames. Only float and 3D transform sections support overrides; editor only. Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_get_parent: {
    epicSchema: S_epic_get_parent,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.get_parent", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get the parent of a hierarchy element. Params: control_rig, item", "epic_call_tool"),
  },
  epic_get_perlin_noise_params: {
    epicSchema: S_epic_get_perlin_noise_params,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetPerlinNoiseParams", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Get the noise parameters of a channel overridden with Perlin noise. Frequency is in peaks per second and Offset in seconds. Available on UE 5.9; absent from UE 5.8. Params: section, channelName", "epic_call_tool"),
  },
  epic_get_physics_asset: {
    epicSchema: S_epic_get_physics_asset,
    epicTool: { toolset: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", name: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_physics_asset", engines: ["5.8"], successors: [{ toolset: "EditorToolset.SkeletalMeshToolset", name: "EditorToolset.SkeletalMeshToolset.GetPhysicsAsset", engines: ["5.9"], schema: S_epic_get_physics_asset__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the physics asset assigned to a skeletal mesh. The physics asset defines the collision bodies and constraints used for ragdoll simulation and per-bone physics. Params: mesh. On UE 5.9 it calls EditorToolset.SkeletalMeshToolset.GetPhysicsAsset (Params: mesh).", "epic_call_tool"),
  },
  epic_get_pin_value: {
    epicSchema: S_epic_get_pin_value,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.get_pin_value", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get the default value of a pin. Params: control_rig, pin", "epic_call_tool"),
  },
  epic_get_pinned_motion_trails: {
    epicSchema: S_epic_get_pinned_motion_trails,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.GetPinnedMotionTrails", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] GetPinnedMotionTrails Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_pinned_nodes: {
    epicSchema: S_epic_get_pinned_nodes,
    epicTool: { toolset: "animation_toolset.toolsets.outliner.SequencerOutlinerTools", name: "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_pinned_nodes", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetPinnedNodes", engines: ["5.9"], schema: S_epic_get_pinned_nodes__1 }] },
    ...bp("read", "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Get the currently pinned outliner nodes. Params: none. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.GetPinnedNodes (Params: none).", "epic_call_tool"),
  },
  epic_get_playback_context: {
    epicSchema: S_epic_get_playback_context,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetPlaybackContext", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Get the world and actor the open Level Sequence editor currently evaluates in. Raises when no Level Sequence editor is open. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_playback_context_options: {
    epicSchema: S_epic_get_playback_context_options,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetPlaybackContextOptions", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] List the worlds and actors the open Level Sequence editor can evaluate in, as its world picker does. Raises when no Level Sequence editor is open. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_playback_end: {
    epicSchema: S_epic_get_playback_end,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.GetPlaybackEnd", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Get playback end of this sequence in display rate resolution Available on UE 5.9; absent from UE 5.8. Params: sequence", "epic_call_tool"),
  },
  epic_get_playback_range: {
    epicSchema: S_epic_get_playback_range,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_playback_range", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the playback start and end frames of a sequence. Available on UE 5.8; absent from UE 5.9. Params: sequence", "epic_call_tool"),
  },
  epic_get_playback_speed: {
    epicSchema: S_epic_get_playback_speed,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_playback_speed", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the current playback speed multiplier. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_playback_speed__level_sequence_editor_blueprint_library: {
    epicSchema: S_epic_get_playback_speed__level_sequence_editor_blueprint_library,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetPlaybackSpeed", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Get playback speed of the current level sequence Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_playback_start: {
    epicSchema: S_epic_get_playback_start,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.GetPlaybackStart", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Get playback start of this sequence in display rate resolution Available on UE 5.9; absent from UE 5.8. Params: sequence", "epic_call_tool"),
  },
  epic_get_playhead_frame: {
    epicSchema: S_epic_get_playhead_frame,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_playhead_frame", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the current playhead position in display rate frames. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_portable_binding_id: {
    epicSchema: S_epic_get_portable_binding_id,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.GetPortableBindingID", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Get a portable binding ID for a binding that resides in a different sequence to the one where this binding will be resolved. Available on UE 5.9; absent from UE 5.8. Params: rootSequence, destinationSequence, inBinding", "epic_call_tool"),
  },
  epic_get_position: {
    epicSchema: S_epic_get_position,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_position", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get a position control value at a specific frame. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, frame", "epic_call_tool"),
  },
  epic_get_post_roll_frames: {
    epicSchema: S_epic_get_post_roll_frames,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetPostRollFrames", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Return the section's post-roll in tick-resolution frames. Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_get_pre_roll_frames: {
    epicSchema: S_epic_get_pre_roll_frames,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetPreRollFrames", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Return the section's pre-roll in tick-resolution frames. Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_get_preview_language: {
    epicSchema: S_epic_get_preview_language,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetPreviewLanguage", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] The culture this sequence previews localized assets in, or empty when its sections preview their unlocalized assets. Available on UE 5.9; absent from UE 5.8. Params: sequence", "epic_call_tool"),
  },
  epic_get_priority_order: {
    epicSchema: S_epic_get_priority_order,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_priority_order", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get the evaluation priority order of a Control Rig track. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path", "epic_call_tool"),
  },
  epic_get_root_folders: {
    epicSchema: S_epic_get_root_folders,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_root_folders", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get all root-level folders in the sequence. Available on UE 5.8; absent from UE 5.9. Params: sequence", "epic_call_tool"),
  },
  epic_get_root_folders_in_sequence: {
    epicSchema: S_epic_get_root_folders_in_sequence,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.GetRootFoldersInSequence", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Get the root folders in the provided sequence Available on UE 5.9; absent from UE 5.8. Params: sequence", "epic_call_tool"),
  },
  epic_get_rotator: {
    epicSchema: S_epic_get_rotator,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_rotator", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get a rotator control value at a specific frame. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, frame", "epic_call_tool"),
  },
  epic_get_safe_frame_size: {
    epicSchema: S_epic_get_safe_frame_size,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetSafeFrameSize", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] The inset of a safe frame overlay as a percentage of the viewport. Raises when the name is not a safe frame. Available on UE 5.9; absent from UE 5.8. Params: frameName", "epic_call_tool"),
  },
  epic_get_scale: {
    epicSchema: S_epic_get_scale,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_scale", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get a scale control value at a specific frame. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, frame", "epic_call_tool"),
  },
  epic_get_section_blend_type: {
    epicSchema: S_epic_get_section_blend_type,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_section_blend_type", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the blend type of a section. Available on UE 5.8; absent from UE 5.9. Params: section", "epic_call_tool"),
  },
  epic_get_section_completion_mode: {
    epicSchema: S_epic_get_section_completion_mode,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_section_completion_mode", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the completion mode of a section. Available on UE 5.8; absent from UE 5.9. Params: section", "epic_call_tool"),
  },
  epic_get_section_condition: {
    epicSchema: S_epic_get_section_condition,
    epicTool: { toolset: "animation_toolset.toolsets.conditions.SequencerConditionTools", name: "animation_toolset.toolsets.conditions.SequencerConditionTools.get_section_condition", engines: ["5.8"], successors: [{ toolset: "SequencerScripting.MovieSceneConditionExtensions", name: "SequencerScripting.MovieSceneConditionExtensions.GetSectionCondition", engines: ["5.9"], schema: S_epic_get_section_condition__1 }] },
    ...bp("read", "[Epic animation_toolset.toolsets.conditions.SequencerConditionTools] Get the condition on a section. Returns the class path of the condition, or an empty string if no condition is set. Params: section. On UE 5.9 it calls SequencerScripting.MovieSceneConditionExtensions.GetSectionCondition (Params: section).", "epic_call_tool"),
  },
  epic_get_section_count: {
    epicSchema: S_epic_get_section_count,
    epicTool: { toolset: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", name: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_section_count", engines: ["5.8"], successors: [{ toolset: "EditorToolset.SkeletalMeshToolset", name: "EditorToolset.SkeletalMeshToolset.GetSectionCount", engines: ["5.9"], schema: S_epic_get_section_count__1, rename: {"lod_index":"lodIndex"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the number of sections in a specific LOD of a skeletal mesh. Sections correspond to individual material slots rendered by a single draw call. A mesh may have more sections than material slots if multiple sections share the same material. Params: mesh, lod_index?. On UE 5.9 it calls EditorToolset.SkeletalMeshToolset.GetSectionCount (Params: mesh, lodIndex?).", "epic_call_tool"),
  },
  epic_get_section_ease_in: {
    epicSchema: S_epic_get_section_ease_in,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_section_ease_in", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the effective ease-in duration of a section in frames. Available on UE 5.8; absent from UE 5.9. Params: section", "epic_call_tool"),
  },
  epic_get_section_ease_out: {
    epicSchema: S_epic_get_section_ease_out,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_section_ease_out", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the effective ease-out duration of a section in frames. Available on UE 5.8; absent from UE 5.9. Params: section", "epic_call_tool"),
  },
  epic_get_section_group: {
    epicSchema: S_epic_get_section_group,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.GetSectionGroup", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Return every section grouped with Section, including Section itself, or an empty array when it is not grouped. A grouped section that has been removed from its track is left out. Available on UE 5.9; absent from UE 5.8. Params: sequence, section", "epic_call_tool"),
  },
  epic_get_section_groups: {
    epicSchema: S_epic_get_section_groups,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.GetSectionGroups", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] List one section from each section group, the first that a track of the sequence holds. Pass it to GetSectionGroup for the whole group. Available on UE 5.9; absent from UE 5.8. Params: sequence", "epic_call_tool"),
  },
  epic_get_section_play_rate: {
    epicSchema: S_epic_get_section_play_rate,
    epicTool: { toolset: "SequencerScripting.MovieSceneTimeWarpExtensions", name: "SequencerScripting.MovieSceneTimeWarpExtensions.GetSectionPlayRate", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneTimeWarpExtensions] Get a section's fixed play rate. Returns 0 for a section held on a fixed time. Raises when the section uses another kind of time warp. Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_get_section_post_roll_frames: {
    epicSchema: S_epic_get_section_post_roll_frames,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_section_post_roll_frames", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the number of post-roll frames configured on a section. Available on UE 5.8; absent from UE 5.9. Params: section", "epic_call_tool"),
  },
  epic_get_section_pre_roll_frames: {
    epicSchema: S_epic_get_section_pre_roll_frames,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_section_pre_roll_frames", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the number of pre-roll frames configured on a section. Available on UE 5.8; absent from UE 5.9. Params: section", "epic_call_tool"),
  },
  epic_get_section_properties: {
    epicSchema: S_epic_get_section_properties,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_section_properties", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get all common properties of a section in a single call. Useful for testing and verification. Returns range, easing, blend type, and completion mode. Available on UE 5.8; absent from UE 5.9. Params: section", "epic_call_tool"),
  },
  epic_get_section_range: {
    epicSchema: S_epic_get_section_range,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_section_range", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the frame range of a section. Available on UE 5.8; absent from UE 5.9. Params: section", "epic_call_tool"),
  },
  epic_get_section_row_index: {
    epicSchema: S_epic_get_section_row_index,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetSectionRowIndex", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Get the zero-based track row. Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_get_section_to_key: {
    epicSchema: S_epic_get_section_to_key,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_section_to_key", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the active section that receives new keys on a track. When keying properties, Sequencer writes to a specific section. This returns that section, which is usually the first section or the one the user has designated. Available on UE 5.8; absent from UE 5.9. Params: track", "epic_call_tool"),
  },
  epic_get_section_to_key__movie_scene_track_extensions: {
    epicSchema: S_epic_get_section_to_key__movie_scene_track_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneTrackExtensions", name: "SequencerScripting.MovieSceneTrackExtensions.GetSectionToKey", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneTrackExtensions] Get the section to key for this track Available on UE 5.9; absent from UE 5.8. Params: track", "epic_call_tool"),
  },
  epic_get_sections: {
    epicSchema: S_epic_get_sections,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_sections", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get all sections on a track. Available on UE 5.8; absent from UE 5.9. Params: track", "epic_call_tool"),
  },
  epic_get_sections__movie_scene_track_extensions: {
    epicSchema: S_epic_get_sections__movie_scene_track_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneTrackExtensions", name: "SequencerScripting.MovieSceneTrackExtensions.GetSections", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneTrackExtensions] Access all this track's sections Available on UE 5.9; absent from UE 5.8. Params: track", "epic_call_tool"),
  },
  epic_get_sections_for_nodes: {
    epicSchema: S_epic_get_sections_for_nodes,
    epicTool: { toolset: "animation_toolset.toolsets.outliner.SequencerOutlinerTools", name: "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_sections_for_nodes", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetSectionsForNodes", engines: ["5.9"], schema: S_epic_get_sections_for_nodes__1 }] },
    ...bp("read", "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Get sections associated with the given outliner nodes. Params: nodes. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.GetSectionsForNodes (Params: nodes).", "epic_call_tool"),
  },
  epic_get_selected_bindings: {
    epicSchema: S_epic_get_selected_bindings,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_selected_bindings", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the currently selected bindings in the Sequencer editor. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_selected_bindings__level_sequence_editor_blueprint_library: {
    epicSchema: S_epic_get_selected_bindings__level_sequence_editor_blueprint_library,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetSelectedBindings", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Gets the currently selected object bindings for the focused sequence. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_selected_channels: {
    epicSchema: S_epic_get_selected_channels,
    epicTool: { toolset: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools", name: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_selected_channels", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Get the currently selected channels in the Sequencer editor. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_selected_channels__level_sequence_editor_blueprint_library: {
    epicSchema: S_epic_get_selected_channels__level_sequence_editor_blueprint_library,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetSelectedChannels", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Gets the currently selected channels. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_selected_control_rig_controls: {
    epicSchema: S_epic_get_selected_control_rig_controls,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetSelectedControlRigControls", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get the names of a Control Rig's selected controls. Available on UE 5.9; absent from UE 5.8. Params: controlRig", "epic_call_tool"),
  },
  epic_get_selected_controls: {
    epicSchema: S_epic_get_selected_controls,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_selected_controls", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get the currently selected controls on a Control Rig. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path", "epic_call_tool"),
  },
  epic_get_selected_folders: {
    epicSchema: S_epic_get_selected_folders,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_selected_folders", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the currently selected folders in the Sequencer editor. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_selected_folders__level_sequence_editor_blueprint_library: {
    epicSchema: S_epic_get_selected_folders__level_sequence_editor_blueprint_library,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetSelectedFolders", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Gets selected folders, including parent folders of selected tracks or bindings. Returns an empty array when no Sequencer is open. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_selected_key_channels: {
    epicSchema: S_epic_get_selected_key_channels,
    epicTool: { toolset: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools", name: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_selected_key_channels", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Get channels that have selected keys in the Curve Editor. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_selected_sections: {
    epicSchema: S_epic_get_selected_sections,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_selected_sections", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the currently selected sections in the Sequencer editor. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_selected_sections__level_sequence_editor_blueprint_library: {
    epicSchema: S_epic_get_selected_sections__level_sequence_editor_blueprint_library,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetSelectedSections", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Gets the currently selected sections, or an empty array when no Sequencer is open. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_selected_tracks: {
    epicSchema: S_epic_get_selected_tracks,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_selected_tracks", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the currently selected tracks in the Sequencer editor. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_selected_tracks__level_sequence_editor_blueprint_library: {
    epicSchema: S_epic_get_selected_tracks__level_sequence_editor_blueprint_library,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetSelectedTracks", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Gets the currently selected tracks, or an empty array when no Sequencer is open. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_selection_range: {
    epicSchema: S_epic_get_selection_range,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_selection_range", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the selection range (green bar) start and end frames. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_selection_range_end: {
    epicSchema: S_epic_get_selection_range_end,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetSelectionRangeEnd", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Get the selection range end frame. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_selection_range_start: {
    epicSchema: S_epic_get_selection_range_start,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetSelectionRangeStart", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Get the selection range start frame. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_selection_set_actors: {
    epicSchema: S_epic_get_selection_set_actors,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetSelectionSetActors", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get the actors selection sets act on in the focused level sequence. Raises a script error if no level sequence is open in Sequencer. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_selection_set_infos: {
    epicSchema: S_epic_get_selection_set_infos,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetSelectionSetInfos", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get the selection sets of the level sequence focused in Sequencer, sorted by row. Raises a script error if no level sequence is open in Sequencer. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_selection_sets: {
    epicSchema: S_epic_get_selection_sets,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetSelectionSets", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get the selection sets of the level sequence open in Sequencer. With the default bAddIfDoesNotExist this adds selection set data to the sequence when it has none, so pass false for a read-only query. Available on UE 5.9; absent from UE 5.8. Params: bAddIfDoesNotExist?", "epic_call_tool"),
  },
  epic_get_sequence_flags: {
    epicSchema: S_epic_get_sequence_flags,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.GetSequenceFlags", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] List the evaluation flags set on the sequence, one entry per flag. Available on UE 5.9; absent from UE 5.8. Params: sequence", "epic_call_tool"),
  },
  epic_get_sequence_lock_state: {
    epicSchema: S_epic_get_sequence_lock_state,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_sequence_lock_state", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Check whether the current level sequence is locked. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_show_curve: {
    epicSchema: S_epic_get_show_curve,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetShowCurve", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Whether the channel's curve is drawn in the Sequencer track area, the Show Curve entry under Display in the section menu Available on UE 5.9; absent from UE 5.8. Params: channel", "epic_call_tool"),
  },
  epic_get_skeletal_animation_match_settings: {
    epicSchema: S_epic_get_skeletal_animation_match_settings,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetSkeletalAnimationMatchSettings", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Get the settings the section was last matched with. BoneName is None when the section is not matched. Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_get_skeletal_mesh_component_world_transform: {
    epicSchema: S_epic_get_skeletal_mesh_component_world_transform,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetSkeletalMeshComponentWorldTransform", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get SkeletalMeshComponent World Transform at a specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, skeletalMeshComponent, frame, timeUnit?, referenceName?", "epic_call_tool"),
  },
  epic_get_skeletal_mesh_component_world_transforms: {
    epicSchema: S_epic_get_skeletal_mesh_component_world_transforms,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetSkeletalMeshComponentWorldTransforms", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get SkeletalMeshComponents World Transforms at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, skeletalMeshComponent, frames, timeUnit?, referenceName?", "epic_call_tool"),
  },
  epic_get_skeletal_mesh_component_world_transforms_async: {
    epicSchema: S_epic_get_skeletal_mesh_component_world_transforms_async,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetSkeletalMeshComponentWorldTransformsAsync", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] GetSkeletalMeshComponentWorldTransforms read after the next editor world tick. See GetSkeletalMeshComponentWorldTransforms for the parameters. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, skeletalMeshComponent, frames, timeUnit?, referenceName?", "epic_call_tool"),
  },
  epic_get_skeleton: {
    epicSchema: S_epic_get_skeleton,
    epicTool: { toolset: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", name: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_skeleton", engines: ["5.8"], successors: [{ toolset: "EditorToolset.SkeletalMeshToolset", name: "EditorToolset.SkeletalMeshToolset.GetSkeleton", engines: ["5.9"], schema: S_epic_get_skeleton__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the skeleton asset associated with a skeletal mesh. The skeleton defines the bone hierarchy shared across all meshes and animations that use it. Params: mesh. On UE 5.9 it calls EditorToolset.SkeletalMeshToolset.GetSkeleton (Params: mesh).", "epic_call_tool"),
  },
  epic_get_socket_bone: {
    epicSchema: S_epic_get_socket_bone,
    epicTool: { toolset: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", name: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_socket_bone", engines: ["5.8"], successors: [{ toolset: "EditorToolset.SkeletalMeshToolset", name: "EditorToolset.SkeletalMeshToolset.GetSocketBone", engines: ["5.9"], schema: S_epic_get_socket_bone__1, rename: {"socket_name":"socketName"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the name of the bone that a socket is attached to. Params: mesh, socket_name. On UE 5.9 it calls EditorToolset.SkeletalMeshToolset.GetSocketBone (Params: mesh, socketName).", "epic_call_tool"),
  },
  epic_get_socket_names: {
    epicSchema: S_epic_get_socket_names,
    epicTool: { toolset: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", name: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_socket_names", engines: ["5.8"], successors: [{ toolset: "EditorToolset.SkeletalMeshToolset", name: "EditorToolset.SkeletalMeshToolset.GetSocketNames", engines: ["5.9"], schema: S_epic_get_socket_names__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the names of all sockets on a skeletal mesh. Sockets are named attachment points parented to bones. They are used to attach weapons, accessories, or effects at a consistent location. Params: mesh. On UE 5.9 it calls EditorToolset.SkeletalMeshToolset.GetSocketNames (Params: mesh).", "epic_call_tool"),
  },
  epic_get_socket_transform: {
    epicSchema: S_epic_get_socket_transform,
    epicTool: { toolset: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", name: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_socket_transform", engines: ["5.8"], successors: [{ toolset: "EditorToolset.SkeletalMeshToolset", name: "EditorToolset.SkeletalMeshToolset.GetSocketTransform", engines: ["5.9"], schema: S_epic_get_socket_transform__1, rename: {"socket_name":"socketName"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the local transform of a socket relative to its parent bone. Params: mesh, socket_name. On UE 5.9 it calls EditorToolset.SkeletalMeshToolset.GetSocketTransform (Params: mesh, socketName).", "epic_call_tool"),
  },
  epic_get_soloed_nodes: {
    epicSchema: S_epic_get_soloed_nodes,
    epicTool: { toolset: "animation_toolset.toolsets.outliner.SequencerOutlinerTools", name: "animation_toolset.toolsets.outliner.SequencerOutlinerTools.get_soloed_nodes", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.GetSoloedNodes", engines: ["5.9"], schema: S_epic_get_soloed_nodes__1 }] },
    ...bp("read", "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Get the currently soloed outliner nodes. Params: none. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.GetSoloedNodes (Params: none).", "epic_call_tool"),
  },
  epic_get_sorting_order: {
    epicSchema: S_epic_get_sorting_order,
    epicTool: { toolset: "SequencerScripting.MovieSceneFolderExtensions", name: "SequencerScripting.MovieSceneFolderExtensions.GetSortingOrder", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneFolderExtensions] Get the sorting order of the given folder. A negative value means the folder has no custom order and compares by name with sibling folders. Available on UE 5.9; absent from UE 5.8. Params: folder", "epic_call_tool"),
  },
  epic_get_space_channel: {
    epicSchema: S_epic_get_space_channel,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetSpaceChannel", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get the space channel for the specified control rig and control name. Spaces always live on the first section. By default this adds a space channel to the section if the control has none. Keys added through the returned channel are not compensated; use SetControlRigSpace for compensated space switches. Available on UE 5.9; absent from UE 5.8. Params: inSequence, inControlRig, inControlName, bCreateIfNeeded?", "epic_call_tool"),
  },
  epic_get_start_frame: {
    epicSchema: S_epic_get_start_frame,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetStartFrame", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Get the inclusive start in display-rate frames, rounded down. Raises a script error and returns -1 when unbounded. Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_get_sub_sequence_hierarchy: {
    epicSchema: S_epic_get_sub_sequence_hierarchy,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_sub_sequence_hierarchy", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the current sub-sequence hierarchy path. Returns a list of sub-sections from the root down to the currently focused sub-sequence. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_sub_sequence_hierarchy__level_sequence_editor_blueprint_library: {
    epicSchema: S_epic_get_sub_sequence_hierarchy__level_sequence_editor_blueprint_library,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetSubSequenceHierarchy", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Get the current sub section hierarchy from the current sequence to the section associated with the focused sequence. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_supported_blender_systems: {
    epicSchema: S_epic_get_supported_blender_systems,
    epicTool: { toolset: "SequencerScripting.MovieSceneTrackExtensions", name: "SequencerScripting.MovieSceneTrackExtensions.GetSupportedBlenderSystems", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneTrackExtensions] List the blender systems the track can use. Raises for a track without a choice of blender. Available on UE 5.9; absent from UE 5.8. Params: track", "epic_call_tool"),
  },
  epic_get_thumbnail_reference_offset: {
    epicSchema: S_epic_get_thumbnail_reference_offset,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetThumbnailReferenceOffset", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Get the time of a camera cut or cinematic shot section's thumbnail, in seconds from the section start. Editor only. Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_get_tick_resolution: {
    epicSchema: S_epic_get_tick_resolution,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_tick_resolution", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the internal tick resolution of a sequence. Available on UE 5.8; absent from UE 5.9. Params: sequence", "epic_call_tool"),
  },
  epic_get_tick_resolution__movie_scene_sequence_extensions: {
    epicSchema: S_epic_get_tick_resolution__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.GetTickResolution", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Gets this sequence's tick resolution Available on UE 5.9; absent from UE 5.8. Params: sequence", "epic_call_tool"),
  },
  epic_get_time_posing_range: {
    epicSchema: S_epic_get_time_posing_range,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.GetTimePosingRange", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Get the Time Posing window. Raises when Time Posing is off for the sequence. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, timeUnit?", "epic_call_tool"),
  },
  epic_get_time_posing_settings: {
    epicSchema: S_epic_get_time_posing_settings,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.GetTimePosingSettings", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] GetTimePosingSettings Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_time_posing_weight: {
    epicSchema: S_epic_get_time_posing_weight,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.GetTimePosingWeight", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Get the share of a Time Posing offset applied at a frame: 1 in the full-weight band, the falloff curve between a far anchor and its middle, 0 at or outside the far anchors. Raises when Time Posing is off for the sequence. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, frame, timeUnit?", "epic_call_tool"),
  },
  epic_get_time_warp_classes: {
    epicSchema: S_epic_get_time_warp_classes,
    epicTool: { toolset: "SequencerScripting.MovieSceneTimeWarpExtensions", name: "SequencerScripting.MovieSceneTimeWarpExtensions.GetTimeWarpClasses", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneTimeWarpExtensions] List the custom time warp classes that SetCustomTimeWarp accepts, such as the play rate curve and the time warp curve. In the editor this includes Blueprint time warps that are not loaded yet, and loads them. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_time_warp_type: {
    epicSchema: S_epic_get_time_warp_type,
    epicTool: { toolset: "SequencerScripting.MovieSceneTimeWarpExtensions", name: "SequencerScripting.MovieSceneTimeWarpExtensions.GetTimeWarpType", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneTimeWarpExtensions] Get the kind of time warp a section uses. Raises for a section without a time warp. Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_get_track_condition: {
    epicSchema: S_epic_get_track_condition,
    epicTool: { toolset: "animation_toolset.toolsets.conditions.SequencerConditionTools", name: "animation_toolset.toolsets.conditions.SequencerConditionTools.get_track_condition", engines: ["5.8"], successors: [{ toolset: "SequencerScripting.MovieSceneConditionExtensions", name: "SequencerScripting.MovieSceneConditionExtensions.GetTrackCondition", engines: ["5.9"], schema: S_epic_get_track_condition__1 }] },
    ...bp("read", "[Epic animation_toolset.toolsets.conditions.SequencerConditionTools] Get the track-level condition. Params: track. On UE 5.9 it calls SequencerScripting.MovieSceneConditionExtensions.GetTrackCondition (Params: track).", "epic_call_tool"),
  },
  epic_get_track_display_name: {
    epicSchema: S_epic_get_track_display_name,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_track_display_name", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the display name of a track. Available on UE 5.8; absent from UE 5.9. Params: track", "epic_call_tool"),
  },
  epic_get_track_filter_names: {
    epicSchema: S_epic_get_track_filter_names,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_track_filter_names", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get all available track filter names. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_get_track_filter_names__level_sequence_editor_blueprint_library: {
    epicSchema: S_epic_get_track_filter_names__level_sequence_editor_blueprint_library,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetTrackFilterNames", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Gets available localized filter display names, or an empty array when no Sequencer is open. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_track_filter_text: {
    epicSchema: S_epic_get_track_filter_text,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.GetTrackFilterText", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Gets the filter bar's search text. Raises an error when no Sequencer is open. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_track_row_condition: {
    epicSchema: S_epic_get_track_row_condition,
    epicTool: { toolset: "animation_toolset.toolsets.conditions.SequencerConditionTools", name: "animation_toolset.toolsets.conditions.SequencerConditionTools.get_track_row_condition", engines: ["5.8"], successors: [{ toolset: "SequencerScripting.MovieSceneConditionExtensions", name: "SequencerScripting.MovieSceneConditionExtensions.GetTrackRowCondition", engines: ["5.9"], schema: S_epic_get_track_row_condition__1, rename: {"row_index":"rowIndex"} }] },
    ...bp("read", "[Epic animation_toolset.toolsets.conditions.SequencerConditionTools] Get the condition on a specific track row. Params: track, row_index. On UE 5.9 it calls SequencerScripting.MovieSceneConditionExtensions.GetTrackRowCondition (Params: track, rowIndex).", "epic_call_tool"),
  },
  epic_get_tracks: {
    epicSchema: S_epic_get_tracks,
    epicTool: { toolset: "SequencerScripting.MovieSceneBindingExtensions", name: "SequencerScripting.MovieSceneBindingExtensions.GetTracks", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneBindingExtensions] Get all the tracks stored within this binding Available on UE 5.9; absent from UE 5.8. Params: inBinding", "epic_call_tool"),
  },
  epic_get_tracks__movie_scene_sequence_extensions: {
    epicSchema: S_epic_get_tracks__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.GetTracks", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Get the sequence's root tracks, including its camera-cut track. Tracks attached to object bindings are not included. Available on UE 5.9; absent from UE 5.8. Params: sequence", "epic_call_tool"),
  },
  epic_get_tracks_on_binding: {
    epicSchema: S_epic_get_tracks_on_binding,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_tracks_on_binding", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get all tracks on a binding. Available on UE 5.8; absent from UE 5.9. Params: binding", "epic_call_tool"),
  },
  epic_get_tracks_on_sequence: {
    epicSchema: S_epic_get_tracks_on_sequence,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_tracks_on_sequence", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get all sequence-level (master) tracks. Available on UE 5.8; absent from UE 5.9. Params: sequence", "epic_call_tool"),
  },
  epic_get_transform: {
    epicSchema: S_epic_get_transform,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_transform", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get the transform value of a Control Rig control at a frame. Automatically detects whether the control is a Transform or EulerTransform type and uses the appropriate API. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, frame", "epic_call_tool"),
  },
  epic_get_transform_mask: {
    epicSchema: S_epic_get_transform_mask,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.GetTransformMask", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Get the channels a 3D transform section animates. Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_get_validation_rules: {
    epicSchema: S_epic_get_validation_rules,
    epicTool: { toolset: "SequenceValidator.SequenceValidatorSubsystem", name: "SequenceValidator.SequenceValidatorSubsystem.GetValidationRules", engines: ["5.9"] },
    ...bp("read", "[Epic SequenceValidator.SequenceValidatorSubsystem] List the validation rules registered with the Sequence Validator. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_variable: {
    epicSchema: S_epic_get_variable,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.get_variable", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Get a specific variable by name. Params: control_rig, name", "epic_call_tool"),
  },
  epic_get_vector2d: {
    epicSchema: S_epic_get_vector2d,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_vector2d", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get a Vector2D control value at a specific frame. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, frame", "epic_call_tool"),
  },
  epic_get_vertex_count: {
    epicSchema: S_epic_get_vertex_count,
    epicTool: { toolset: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", name: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.get_vertex_count", engines: ["5.8"], successors: [{ toolset: "EditorToolset.SkeletalMeshToolset", name: "EditorToolset.SkeletalMeshToolset.GetVertexCount", engines: ["5.9"], schema: S_epic_get_vertex_count__1, rename: {"lod_index":"lodIndex"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Returns the number of vertices in a specific LOD of a skeletal mesh. Params: mesh, lod_index?. On UE 5.9 it calls EditorToolset.SkeletalMeshToolset.GetVertexCount (Params: mesh, lodIndex?).", "epic_call_tool"),
  },
  epic_get_view_range: {
    epicSchema: S_epic_get_view_range,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_view_range", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the visible time range in the Sequencer timeline. Available on UE 5.8; absent from UE 5.9. Params: sequence", "epic_call_tool"),
  },
  epic_get_view_range_end: {
    epicSchema: S_epic_get_view_range_end,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.GetViewRangeEnd", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Get the sequence view range end in seconds Available on UE 5.9; absent from UE 5.8. Params: inSequence", "epic_call_tool"),
  },
  epic_get_view_range_start: {
    epicSchema: S_epic_get_view_range_start,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.GetViewRangeStart", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Get the sequence view range start in seconds Available on UE 5.9; absent from UE 5.8. Params: inSequence", "epic_call_tool"),
  },
  epic_get_visible_control_rigs: {
    epicSchema: S_epic_get_visible_control_rigs,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetVisibleControlRigs", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get the Control Rigs shown by Animation Mode in the level editor. Requires Animation Mode to be active. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_work_range: {
    epicSchema: S_epic_get_work_range,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.get_work_range", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Get the work range of the sequence. Available on UE 5.8; absent from UE 5.9. Params: sequence", "epic_call_tool"),
  },
  epic_get_work_range_end: {
    epicSchema: S_epic_get_work_range_end,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.GetWorkRangeEnd", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Get the sequence work range end in seconds Available on UE 5.9; absent from UE 5.8. Params: inSequence", "epic_call_tool"),
  },
  epic_get_work_range_start: {
    epicSchema: S_epic_get_work_range_start,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.GetWorkRangeStart", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Get the sequence work range start in seconds Available on UE 5.9; absent from UE 5.8. Params: inSequence", "epic_call_tool"),
  },
  epic_get_world_space_reference_key: {
    epicSchema: S_epic_get_world_space_reference_key,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.GetWorldSpaceReferenceKey", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Get the key that names world space, for use as a space in SetControlRigSpace. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_get_world_transform: {
    epicSchema: S_epic_get_world_transform,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_world_transform", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Get a control's world-space transform at a specific frame. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, frame", "epic_call_tool"),
  },
  epic_group_sections: {
    epicSchema: S_epic_group_sections,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.GroupSections", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Group sections so that moving one moves them all, merging any groups they already belong to. Requires at least two distinct sections in tracks of this sequence, each with a start or an end frame. Available on UE 5.9; absent from UE 5.8. Params: sequence, sections", "epic_call_tool"),
  },
  epic_has_control_rig_pose_in_clipboard: {
    epicSchema: S_epic_has_control_rig_pose_in_clipboard,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.HasControlRigPoseInClipboard", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Whether Animation Mode's pose clipboard holds a saved pose. Raises a script error if Animation Mode is not active. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_has_default: {
    epicSchema: S_epic_has_default,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.HasDefault", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Whether this channel has a default value. Available on UE 5.9; absent from UE 5.8. Params: channel", "epic_call_tool"),
  },
  epic_has_end_frame: {
    epicSchema: S_epic_has_end_frame,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.HasEndFrame", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Has end frame Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_has_section_end_frame: {
    epicSchema: S_epic_has_section_end_frame,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.has_section_end_frame", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Check if a section has a bounded end frame (vs infinite). Available on UE 5.8; absent from UE 5.9. Params: section", "epic_call_tool"),
  },
  epic_has_section_start_frame: {
    epicSchema: S_epic_has_section_start_frame,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.has_section_start_frame", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Check if a section has a bounded start frame (vs infinite). Available on UE 5.8; absent from UE 5.9. Params: section", "epic_call_tool"),
  },
  epic_has_selection_range: {
    epicSchema: S_epic_has_selection_range,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.HasSelectionRange", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Whether the focused sequence has a selection range. When it does not, the selection range start and end are both 0. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_has_start_frame: {
    epicSchema: S_epic_has_start_frame,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.HasStartFrame", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Has start frame Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_hide_all_controls: {
    epicSchema: S_epic_hide_all_controls,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.hide_all_controls", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Hide all controls on a Control Rig section (mask everything). Available on UE 5.8; absent from UE 5.9. Params: section", "epic_call_tool"),
  },
  epic_hide_all_controls__control_rig_sequencer_editor_library: {
    epicSchema: S_epic_hide_all_controls__control_rig_sequencer_editor_library,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.HideAllControls", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Mask out every control on the section so none of them are animated. Available on UE 5.9; absent from UE 5.8. Params: inSection", "epic_call_tool"),
  },
  epic_import_bones_from_asset: {
    epicSchema: S_epic_import_bones_from_asset,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.import_bones_from_asset", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Import bones from the given skeletal mesh to the Control Rig hierarchy. Params: control_rig, skeletal_mesh", "epic_call_tool"),
  },
  epic_import_fbx: {
    epicSchema: S_epic_import_fbx,
    epicTool: { toolset: "animation_toolset.toolsets.import_export.SequencerImportExportTools", name: "animation_toolset.toolsets.import_export.SequencerImportExportTools.import_fbx", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.import_export.SequencerImportExportTools] Import FBX data into a level sequence. Available on UE 5.8; absent from UE 5.9. Params: world, sequence, bindings, import_settings, fbx_file_path", "epic_call_tool"),
  },
  epic_import_fbx_to_rig: {
    epicSchema: S_epic_import_fbx_to_rig,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.import_fbx_to_rig", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Import an FBX file onto a Control Rig track. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, import_file_path, selected_controls", "epic_call_tool"),
  },
  epic_import_fbxto_control_rig_track: {
    epicSchema: S_epic_import_fbxto_control_rig_track,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.ImportFBXToControlRigTrack", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Import FBX animation onto a control rig with the specified track and section. Available on UE 5.9; absent from UE 5.8. Params: world, inSequence, inTrack, inSection, selectedControlRigNames, importFBXControlRigSettings, importFilename", "epic_call_tool"),
  },
  epic_import_fbxto_control_rig_track_async: {
    epicSchema: S_epic_import_fbxto_control_rig_track_async,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.ImportFBXToControlRigTrackAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] ImportFBXToControlRigTrack as an async operation. See ImportFBXToControlRigTrack for the parameters. Available on UE 5.9; absent from UE 5.8. Params: world, inSequence, inTrack, inSection, selectedControlRigNames, importFBXControlRigSettings, importFilename", "epic_call_tool"),
  },
  epic_import_file: {
    epicSchema: S_epic_import_file,
    epicTool: { toolset: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", name: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.import_file", engines: ["5.8"], successors: [{ toolset: "EditorToolset.SkeletalMeshToolset", name: "EditorToolset.SkeletalMeshToolset.ImportFile", engines: ["5.9"], schema: S_epic_import_file__1, rename: {"folder_path":"folderPath","asset_name":"assetName","source_file":"sourceFile","import_materials":"bImportMaterials","import_textures":"bImportTextures","import_animations":"bImportAnimations","create_physics_asset":"bCreatePhysicsAsset"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Imports a mesh file from disk as a SkeletalMesh asset. The source file must contain a skeleton hierarchy and skinned mesh data. Params: folder_path, asset_name, source_file, skeleton?, import_materials?, import_textures?, import_animations?, create_physics_asset?. On UE 5.9 it calls EditorToolset.SkeletalMeshToolset.ImportFile (Params: folderPath, assetName, sourceFile, skeleton?, bImportMaterials?, bImportTextures?, bImportAnimations?, bCreatePhysicsAsset?, bReplaceExisting?, bSave?).", "epic_call_tool"),
  },
  epic_import_level_sequence_edl: {
    epicSchema: S_epic_import_level_sequence_edl,
    epicTool: { toolset: "SequencerScriptingEditor.SequencerToolsFunctionLibrary", name: "SequencerScriptingEditor.SequencerToolsFunctionLibrary.ImportLevelSequenceEDL", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScriptingEditor.SequencerToolsFunctionLibrary] Import an Edit Decision List (EDL) file onto a level sequence's cinematic shot track, as one undoable transaction. The sequence's movie scene must be writable. Fails, leaving the shot track unchanged, if the file has no recognized shot events and the track was not already empty. Available on UE 5.9; absent from UE 5.8. Params: sequence, importFilename", "epic_call_tool"),
  },
  epic_import_level_sequence_fbx: {
    epicSchema: S_epic_import_level_sequence_fbx,
    epicTool: { toolset: "SequencerScriptingEditor.SequencerToolsFunctionLibrary", name: "SequencerScriptingEditor.SequencerToolsFunctionLibrary.ImportLevelSequenceFBX", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScriptingEditor.SequencerToolsFunctionLibrary] Import animation and cameras from an FBX file onto bindings of a level sequence, as one undoable transaction. The sequence and World must be writable and the sequence's movie scene must not be read-only. Every binding must belong to InSequence; child bindings of the passed bindings are imported too. Available on UE 5.9; absent from UE 5.8. Params: inWorld, inSequence, inBindings, inImportFBXSettings, inImportFilename, actorContext?", "epic_call_tool"),
  },
  epic_import_level_sequence_fcpxml: {
    epicSchema: S_epic_import_level_sequence_fcpxml,
    epicTool: { toolset: "SequencerScriptingEditor.SequencerToolsFunctionLibrary", name: "SequencerScriptingEditor.SequencerToolsFunctionLibrary.ImportLevelSequenceFCPXML", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScriptingEditor.SequencerToolsFunctionLibrary] Import a Final Cut Pro 7 XML file onto a level sequence's cinematic shot and audio tracks, as one undoable transaction. The sequence's movie scene must be writable. Available on UE 5.9; absent from UE 5.8. Params: sequence, importFilename", "epic_call_tool"),
  },
  epic_import_selection_sets_from_json: {
    epicSchema: S_epic_import_selection_sets_from_json,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.ImportSelectionSetsFromJson", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Add the selection sets in a JSON string written by ExportSelectionSetsToJson, as Import Sets does. Raises a script error if no level sequence is open in Sequencer or the string is not selection sets JSON. Available on UE 5.9; absent from UE 5.8. Params: json", "epic_call_tool"),
  },
  epic_invert_input_pose: {
    epicSchema: S_epic_invert_input_pose,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.InvertInputPose", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Invert Input Pose, this will set layered controls rigs to default pose, for non-layered same as ZeroTransforms. Requires Animation Mode to be active. Available on UE 5.9; absent from UE 5.8. Params: bSelectionOnly, bIncludeChannels", "epic_call_tool"),
  },
  epic_is_animation_mode_tool_open: {
    epicSchema: S_epic_is_animation_mode_tool_open,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.IsAnimationModeToolOpen", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Whether one of Animation Mode's tools is open. Raises a script error if Animation Mode is not active. Available on UE 5.9; absent from UE 5.8. Params: tool", "epic_call_tool"),
  },
  epic_is_camera_cut_locked: {
    epicSchema: S_epic_is_camera_cut_locked,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.is_camera_cut_locked", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Check if the camera cut is locked to the viewport. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_is_camera_cut_locked_to_viewport: {
    epicSchema: S_epic_is_camera_cut_locked_to_viewport,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.IsCameraCutLockedToViewport", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Check whether the lock for the viewport to the camera cuts is enabled. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_is_cinematic_viewport_open: {
    epicSchema: S_epic_is_cinematic_viewport_open,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.IsCinematicViewportOpen", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Whether a level editor viewport is currently a Cinematic Viewport Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_is_curve_editor_open: {
    epicSchema: S_epic_is_curve_editor_open,
    epicTool: { toolset: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools", name: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.is_curve_editor_open", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Check whether the Curve Editor panel is currently open. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_is_curve_editor_open__level_sequence_editor_subsystem: {
    epicSchema: S_epic_is_curve_editor_open__level_sequence_editor_subsystem,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.IsCurveEditorOpen", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Report whether the active Sequencer's Curve Editor is open. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_is_curve_pinned: {
    epicSchema: S_epic_is_curve_pinned,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.IsCurvePinned", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Report whether a channel's curve is pinned in the active Sequencer's Curve Editor. Available on UE 5.9; absent from UE 5.8. Params: channel", "epic_call_tool"),
  },
  epic_is_curve_shown: {
    epicSchema: S_epic_is_curve_shown,
    epicTool: { toolset: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools", name: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.is_curve_shown", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Check if a curve is visible in the Curve Editor. Available on UE 5.8; absent from UE 5.9. Params: channel", "epic_call_tool"),
  },
  epic_is_curve_shown__level_sequence_editor_subsystem: {
    epicSchema: S_epic_is_curve_shown__level_sequence_editor_subsystem,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.IsCurveShown", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Report whether a channel's curve is displayed in the active Curve Editor. Available on UE 5.9; absent from UE 5.8. Params: channel", "epic_call_tool"),
  },
  epic_is_deactivated: {
    epicSchema: S_epic_is_deactivated,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.IsDeactivated", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSectionExtensions] Get whether this section is skipped during evaluation because it is inactive, or because its track row or track has been deactivated. Local mute and solo are not included. Available on UE 5.9; absent from UE 5.8. Params: section", "epic_call_tool"),
  },
  epic_is_deactivated__movie_scene_track_extensions: {
    epicSchema: S_epic_is_deactivated__movie_scene_track_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneTrackExtensions", name: "SequencerScripting.MovieSceneTrackExtensions.IsDeactivated", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneTrackExtensions] Get whether this track has been deactivated, which is the state saved with the asset. Local mute and solo are not included. Available on UE 5.9; absent from UE 5.8. Params: track", "epic_call_tool"),
  },
  epic_is_edit_pivot_tool_active: {
    epicSchema: S_epic_is_edit_pivot_tool_active,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.IsEditPivotToolActive", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] IsEditPivotToolActive Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_is_film_overlay_frame_enabled: {
    epicSchema: S_epic_is_film_overlay_frame_enabled,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.IsFilmOverlayFrameEnabled", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Whether a frame overlay is enabled. Raises when the name is unknown. Available on UE 5.9; absent from UE 5.8. Params: frameName", "epic_call_tool"),
  },
  epic_is_fk_control_rig: {
    epicSchema: S_epic_is_fk_control_rig,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.is_fk_control_rig", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Check if a Control Rig is an FK Control Rig. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path", "epic_call_tool"),
  },
  epic_is_fkcontrol_rig: {
    epicSchema: S_epic_is_fkcontrol_rig,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.IsFKControlRig", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Whether or not the control rig is an FK Control Rig. Available on UE 5.9; absent from UE 5.8. Params: inControlRig", "epic_call_tool"),
  },
  epic_is_layered_control_rig: {
    epicSchema: S_epic_is_layered_control_rig,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.is_layered_control_rig", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Check if a Control Rig in the sequence is in layered mode. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path", "epic_call_tool"),
  },
  epic_is_layered_control_rig__control_rig_sequencer_editor_library: {
    epicSchema: S_epic_is_layered_control_rig__control_rig_sequencer_editor_library,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.IsLayeredControlRig", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Whether or not the control rig is an Layered Control Rig. Available on UE 5.9; absent from UE 5.8. Params: inControlRig", "epic_call_tool"),
  },
  epic_is_level_sequence_locked: {
    epicSchema: S_epic_is_level_sequence_locked,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.IsLevelSequenceLocked", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Check whether the current level sequence and its descendants are locked for editing. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_is_node_expanded: {
    epicSchema: S_epic_is_node_expanded,
    epicTool: { toolset: "animation_toolset.toolsets.outliner.SequencerOutlinerTools", name: "animation_toolset.toolsets.outliner.SequencerOutlinerTools.is_node_expanded", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.IsNodeExpanded", engines: ["5.9"], schema: S_epic_is_node_expanded__1 }] },
    ...bp("read", "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Check whether an outliner node is expanded. Params: node. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.IsNodeExpanded (Params: node).", "epic_call_tool"),
  },
  epic_is_node_expanded_by_path: {
    epicSchema: S_epic_is_node_expanded_by_path,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.IsNodeExpandedByPath", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Read expansion state; missing or ambiguous paths raise an error. Available on UE 5.9; absent from UE 5.8. Params: path", "epic_call_tool"),
  },
  epic_is_node_group_filter_enabled: {
    epicSchema: S_epic_is_node_group_filter_enabled,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.IsNodeGroupFilterEnabled", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Whether the outliner is filtered to the members of a Sequencer Group Available on UE 5.9; absent from UE 5.8. Params: group", "epic_call_tool"),
  },
  epic_is_node_hidden: {
    epicSchema: S_epic_is_node_hidden,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.IsNodeHidden", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Whether the node is hidden, through SetNodeHidden on it or on an ancestor Available on UE 5.9; absent from UE 5.8. Params: node", "epic_call_tool"),
  },
  epic_is_node_isolated: {
    epicSchema: S_epic_is_node_isolated,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.IsNodeIsolated", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Whether the node is isolated, through SetNodeIsolated on it or on an ancestor Available on UE 5.9; absent from UE 5.8. Params: node", "epic_call_tool"),
  },
  epic_is_playback_range_locked: {
    epicSchema: S_epic_is_playback_range_locked,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.is_playback_range_locked", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Check if the playback range is locked. Available on UE 5.8; absent from UE 5.9. Params: sequence", "epic_call_tool"),
  },
  epic_is_playback_range_locked__movie_scene_sequence_extensions: {
    epicSchema: S_epic_is_playback_range_locked__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.IsPlaybackRangeLocked", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Is playback ranged locked Available on UE 5.9; absent from UE 5.8. Params: sequence", "epic_call_tool"),
  },
  epic_is_playing: {
    epicSchema: S_epic_is_playing,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.is_playing", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Check whether the sequence is currently playing. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_is_playing__level_sequence_editor_blueprint_library: {
    epicSchema: S_epic_is_playing__level_sequence_editor_blueprint_library,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.IsPlaying", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Check whether the sequence is actively playing. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_is_row_deactivated: {
    epicSchema: S_epic_is_row_deactivated,
    epicTool: { toolset: "SequencerScripting.MovieSceneTrackExtensions", name: "SequencerScripting.MovieSceneTrackExtensions.IsRowDeactivated", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneTrackExtensions] Get whether a row of this track has been deactivated, which is the state saved with the asset. Local mute and solo are not included. Available on UE 5.9; absent from UE 5.8. Params: track, rowIndex", "epic_call_tool"),
  },
  epic_is_sequence_locked: {
    epicSchema: S_epic_is_sequence_locked,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.is_sequence_locked", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Check if the current sequence and its descendants are locked. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_is_time_posing_active: {
    epicSchema: S_epic_is_time_posing_active,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.IsTimePosingActive", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] IsTimePosingActive Available on UE 5.9; absent from UE 5.8. Params: levelSequence", "epic_call_tool"),
  },
  epic_is_track_filter_active: {
    epicSchema: S_epic_is_track_filter_active,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.is_track_filter_active", engines: ["5.8"] },
    ...bp("read", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Check whether a track filter is currently active. Available on UE 5.8; absent from UE 5.9. Params: name", "epic_call_tool"),
  },
  epic_is_track_filter_active__level_sequence_editor_blueprint_library: {
    epicSchema: S_epic_is_track_filter_active__level_sequence_editor_blueprint_library,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.IsTrackFilterActive", engines: ["5.9"] },
    ...bp("read", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Checks active state by the filter's localized display name. Unknown names and a closed Sequencer return false. Available on UE 5.9; absent from UE 5.8. Params: trackFilterName", "epic_call_tool"),
  },
  epic_is_valid: {
    epicSchema: S_epic_is_valid,
    epicTool: { toolset: "SequencerScripting.MovieSceneBindingExtensions", name: "SequencerScripting.MovieSceneBindingExtensions.IsValid", engines: ["5.9"] },
    ...bp("read", "[Epic SequencerScripting.MovieSceneBindingExtensions] Check whether the specified binding is valid Available on UE 5.9; absent from UE 5.8. Params: inBinding", "epic_call_tool"),
  },
  epic_isolate_linked_anim_track: {
    epicSchema: S_epic_isolate_linked_anim_track,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.IsolateLinkedAnimTrack", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Isolate or un-isolate the linked animation track for the given binding. When isolating: enables the linked anim track and disables other anim/control rig tracks. When un-isolating: restores the previous evaluation state. Available on UE 5.9; absent from UE 5.8. Params: inBinding, bIsolate", "epic_call_tool"),
  },
  epic_isolate_linked_anim_tracks_for_all_bindings: {
    epicSchema: S_epic_isolate_linked_anim_tracks_for_all_bindings,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.IsolateLinkedAnimTracksForAllBindings", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Isolate or un-isolate the linked animation tracks for every binding in the focused Sequencer movie scene. Available on UE 5.9; absent from UE 5.8. Params: bIsolate", "epic_call_tool"),
  },
  epic_key_control_rig_section_weight: {
    epicSchema: S_epic_key_control_rig_section_weight,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.KeyControlRigSectionWeight", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Key a Control Rig section's Weight channel at a frame, enabling the Weight channel if the section's mask leaves it out. Available on UE 5.9; absent from UE 5.8. Params: inSection, frame, weight, timeUnit?", "epic_call_tool"),
  },
  epic_key_control_rig_section_zero_value: {
    epicSchema: S_epic_key_control_rig_section_zero_value,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.KeyControlRigSectionZeroValue", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Key every control on a Control Rig section, or only the selected controls, to its zero value at a frame. Available on UE 5.9; absent from UE 5.8. Params: inSection, frame, bSelectedControlsOnly?, timeUnit?", "epic_call_tool"),
  },
  epic_key_controls: {
    epicSchema: S_epic_key_controls,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.key_controls", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Key the specified controls on the section at the current Sequencer time. Available on UE 5.8; absent from UE 5.9. Params: section, control_names", "epic_call_tool"),
  },
  epic_key_controls__control_rig_sequencer_editor_library: {
    epicSchema: S_epic_key_controls__control_rig_sequencer_editor_library,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.KeyControls", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Key the controls on the section at the current Sequencer time, using the values their channels evaluate to at that time Available on UE 5.9; absent from UE 5.8. Params: inSection, controlNames", "epic_call_tool"),
  },
  epic_key_controls_at_frames: {
    epicSchema: S_epic_key_controls_at_frames,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.key_controls_at_frames", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Key the specified controls at specific frame numbers. Available on UE 5.8; absent from UE 5.9. Params: section, control_names, frames", "epic_call_tool"),
  },
  epic_key_controls_at_frames__control_rig_sequencer_editor_library: {
    epicSchema: S_epic_key_controls_at_frames__control_rig_sequencer_editor_library,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.KeyControlsAtFrames", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Key the controls on the section at each of the specified times, using the values the section evaluates to at those times Available on UE 5.9; absent from UE 5.8. Params: inSection, controlNames, atTimes, timeUnit?", "epic_call_tool"),
  },
  epic_key_selection_set: {
    epicSchema: S_epic_key_selection_set,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.KeySelectionSet", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Key a selection set's controls and actors on the actors sets act on, at the current Sequencer time. Raises a script error if no level sequence is open in Sequencer, the set does not exist, or Animation Mode is not active. Available on UE 5.9; absent from UE 5.8. Params: id", "epic_call_tool"),
  },
  epic_link_anim_sequence: {
    epicSchema: S_epic_link_anim_sequence,
    epicTool: { toolset: "animation_toolset.toolsets.import_export.SequencerImportExportTools", name: "animation_toolset.toolsets.import_export.SequencerImportExportTools.link_anim_sequence", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.import_export.SequencerImportExportTools] Link an AnimSequence asset to a level sequence binding. When the sequence is modified, the linked AnimSequence can be automatically updated. Available on UE 5.8; absent from UE 5.9. Params: sequence, anim_sequence, binding", "epic_call_tool"),
  },
  epic_link_anim_sequence__sequencer_tools_function_library: {
    epicSchema: S_epic_link_anim_sequence__sequencer_tools_function_library,
    epicTool: { toolset: "SequencerScriptingEditor.SequencerToolsFunctionLibrary", name: "SequencerScriptingEditor.SequencerToolsFunctionLibrary.LinkAnimSequence", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScriptingEditor.SequencerToolsFunctionLibrary] Links a LevelSequence's SkeletalMesh binding to an existing anim sequence. Both assets must be writable, and the binding must belong to the sequence. Available on UE 5.9; absent from UE 5.8. Params: sequence, animSequence, exportOptions, binding", "epic_call_tool"),
  },
  epic_list_component_sources: {
    epicSchema: S_epic_list_component_sources,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.ListComponentSources", engines: ["5.9"] },
    ...bp("read", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] List Component Sources Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_list_data_types: {
    epicSchema: S_epic_list_data_types,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.ListDataTypes", engines: ["5.9"] },
    ...bp("read", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] List Data Types Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_list_graphs: {
    epicSchema: S_epic_list_graphs,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.list_graphs", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] List all graphs in the Control Rig. Params: control_rig", "epic_call_tool"),
  },
  epic_list_node_types: {
    epicSchema: S_epic_list_node_types,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.ListNodeTypes", engines: ["5.9"] },
    ...bp("read", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Lists the node types that can be added to the deformer's graphs, including nodes reading its own variables, resources, component bindings and functions. Available on UE 5.9; absent from UE 5.8. Params: deformer, filter", "epic_call_tool"),
  },
  epic_list_nodes: {
    epicSchema: S_epic_list_nodes,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.list_nodes", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] List all nodes in a graph. Params: control_rig, graph", "epic_call_tool"),
  },
  epic_list_pins: {
    epicSchema: S_epic_list_pins,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.list_pins", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] List all pins on a node. Params: control_rig, node", "epic_call_tool"),
  },
  epic_list_variables: {
    epicSchema: S_epic_list_variables,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.list_variables", engines: ["5.8","5.9"] },
    ...bp("read", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] List all member variables in the Control Rig. Params: control_rig", "epic_call_tool"),
  },
  epic_load_anim_into_rig: {
    epicSchema: S_epic_load_anim_into_rig,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.load_anim_into_rig", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Load an animation sequence into a Control Rig section. Finds the skeletal mesh component from the binding associated with the section's track. Available on UE 5.8; absent from UE 5.9. Params: cr_section, anim_sequence_path, start_frame?, reset_controls?, key_reduce?, tolerance?", "epic_call_tool"),
  },
  epic_load_anim_sequence_into_control_rig_section_with_range: {
    epicSchema: S_epic_load_anim_sequence_into_control_rig_section_with_range,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.LoadAnimSequenceIntoControlRigSectionWithRange", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Load anim sequence into this control rig section. For the load dialog's Smart Reduce or Bake reduce types, pass bKeyReduce false, then call SmartReduceSection or BakeKeys on the section. Available on UE 5.9; absent from UE 5.8. Params: movieSceneSection, animSequence, skelMeshComp, inStartFrame, bUseCustomAnimRange, animStartRange, animEndRange, timeUnit?, bKeyReduce?, tolerance?, interpolation?, bResetControls?, bOntoSelectedControls?", "epic_call_tool"),
  },
  epic_load_anim_sequence_into_control_rig_section_with_range_async: {
    epicSchema: S_epic_load_anim_sequence_into_control_rig_section_with_range_async,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.LoadAnimSequenceIntoControlRigSectionWithRangeAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] LoadAnimSequenceIntoControlRigSectionWithRange as an async operation. See LoadAnimSequenceIntoControlRigSectionWithRange for the parameters. Available on UE 5.9; absent from UE 5.8. Params: movieSceneSection, animSequence, skelMeshComp, inStartFrame, bUseCustomAnimRange, animStartRange, animEndRange, timeUnit?, bKeyReduce?, tolerance?, interpolation?, bResetControls?, bOntoSelectedControls?", "epic_call_tool"),
  },
  epic_make_control_rig_reference: {
    epicSchema: S_epic_make_control_rig_reference,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.MakeControlRigReference", engines: ["5.9"] },
    ...bp("read", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Make a Control Rig reference from an asset or class path, for use with FindOrCreateControlRigTrackFromReference, BakeToControlRigFromReference, and FindControlRigTracks. Available on UE 5.9; absent from UE 5.8. Params: assetOrClassPath", "epic_call_tool"),
  },
  epic_match_skeletal_animation_section: {
    epicSchema: S_epic_match_skeletal_animation_section,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.MatchSkeletalAnimationSection", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Offset the section's root motion so Settings.BoneName lines up with the previous or next section on the track at Frame. Frame is in display-rate frames; to match with the previous section, use a frame where both sections overlap or the section's start frame. Replaces any earlier match. Both sections must use animation sequences. Available on UE 5.9; absent from UE 5.8. Params: section, settings, frame", "epic_call_tool"),
  },
  epic_merge_anim_layers: {
    epicSchema: S_epic_merge_anim_layers,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.merge_anim_layers", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Merge specified animation layers into one. Merges onto the layer with the lowest index. Available on UE 5.8; absent from UE 5.9. Params: indices", "epic_call_tool"),
  },
  epic_merge_anim_layers_with_settings: {
    epicSchema: S_epic_merge_anim_layers_with_settings,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.MergeAnimLayersWithSettings", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Merge specified anim layers into one layer. Will merge onto the anim layer with the lowest index Available on UE 5.9; absent from UE 5.8. Params: indices, mergeAnimLayerSettings", "epic_call_tool"),
  },
  epic_merge_anim_layers_with_settings_async: {
    epicSchema: S_epic_merge_anim_layers_with_settings_async,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.MergeAnimLayersWithSettingsAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] MergeAnimLayersWithSettings as an async operation. See MergeAnimLayersWithSettings for the parameters. Available on UE 5.9; absent from UE 5.8. Params: indices, mergeAnimLayerSettings", "epic_call_tool"),
  },
  epic_mirror_selected_controls: {
    epicSchema: S_epic_mirror_selected_controls,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.mirror_selected_controls", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Apply a mirrored pose to the currently selected controls. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_mirror_selected_controls__control_rig_sequencer_editor_library: {
    epicSchema: S_epic_mirror_selected_controls__control_rig_sequencer_editor_library,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.MirrorSelectedControls", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set the selected controls to the mirror of their counterparts' pose. Requires Animation Mode to be active. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_mirror_unselected_controls: {
    epicSchema: S_epic_mirror_unselected_controls,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.MirrorUnselectedControls", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set the unselected counterparts to the mirror of the selected controls' pose. Requires Animation Mode to be active. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_move_constraint_key: {
    epicSchema: S_epic_move_constraint_key,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.MoveConstraintKey", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Move the constraint active key in the current open Sequencer, as dragging it does: the child's compensation key one tick before moves with it, and keys the switch passes are re-expressed to keep their world transform. A LookAt constraint keys no compensation, so its child's keys are left as they are. Available on UE 5.9; absent from UE 5.8. Params: constraint, constraintSection, inTime, inNewTime, timeUnit?", "epic_call_tool"),
  },
  epic_move_control_rig_space: {
    epicSchema: S_epic_move_control_rig_space,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.MoveControlRigSpace", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Move the Control Rig Space Key for the Control at the specified time to the new time. This will also move any Control Rig keys at this space switch boundary. Available on UE 5.9; absent from UE 5.8. Params: inSequence, inControlRig, inControlName, inTime, inNewTime, timeUnit?", "epic_call_tool"),
  },
  epic_move_space: {
    epicSchema: S_epic_move_space,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.move_space", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Move a space-switch key from one frame to another. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, old_frame, new_frame", "epic_call_tool"),
  },
  epic_open_cinematic_viewport: {
    epicSchema: S_epic_open_cinematic_viewport,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.OpenCinematicViewport", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Switch the active level editor viewport to the Cinematic Viewport type unless one is already open, which persists in the editor layout like the menu's choice. Raises when the active viewport cannot change type. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_open_curve_editor: {
    epicSchema: S_epic_open_curve_editor,
    epicTool: { toolset: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools", name: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.open_curve_editor", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Open the Sequencer Curve Editor panel. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_open_curve_editor__level_sequence_editor_subsystem: {
    epicSchema: S_epic_open_curve_editor__level_sequence_editor_subsystem,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.OpenCurveEditor", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Open the Curve Editor for the active Sequencer. Requires an active Sequencer. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_open_level_sequence_async: {
    epicSchema: S_epic_open_level_sequence_async,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.OpenLevelSequenceAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Open a sequence and wait for its first editor world evaluation. Fails for sequences hidden from scripting in this editor. Available on UE 5.9; absent from UE 5.8. Params: sequence", "epic_call_tool"),
  },
  epic_open_pose_library_tab: {
    epicSchema: S_epic_open_pose_library_tab,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.OpenPoseLibraryTab", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Opens the Control Rig Pose Library panel. Requires Animation Mode to be active. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_open_sequence: {
    epicSchema: S_epic_open_sequence,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.open_sequence", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Open a level sequence asset in the Sequencer editor. Available on UE 5.8; absent from UE 5.9. Params: sequence", "epic_call_tool"),
  },
  epic_paste_bindings: {
    epicSchema: S_epic_paste_bindings,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.paste_bindings", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Paste bindings from the clipboard (or a token returned by copy_bindings). Available on UE 5.8; absent from UE 5.9. Params: paste_token, sequence, parent_folder?", "epic_call_tool"),
  },
  epic_paste_bindings_from_text: {
    epicSchema: S_epic_paste_bindings_from_text,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.PasteBindingsFromText", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Paste bindings and return the new objects, raising a script error if nothing was pasted. Empty text uses the clipboard. Available on UE 5.9; absent from UE 5.8. Params: textToImport, pasteBindingsParams", "epic_call_tool"),
  },
  epic_paste_control_rig_pose_asset: {
    epicSchema: S_epic_paste_control_rig_pose_asset,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.PasteControlRigPoseAsset", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Paste a pose asset onto a Control Rig's selected controls, as the Pose Library's Paste Pose and Paste Mirror Pose do. Raises a script error if the pose asset or Control Rig is null, or no control is selected. Available on UE 5.9; absent from UE 5.8. Params: poseAsset, controlRig, bKey?, bMirror?, bAdditive?", "epic_call_tool"),
  },
  epic_paste_control_rig_pose_from_clipboard: {
    epicSchema: S_epic_paste_control_rig_pose_from_clipboard,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.PasteControlRigPoseFromClipboard", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Paste the pose in Animation Mode's pose clipboard onto the selected controls, as Paste Pose and Paste Mirror Pose do. Raises a script error if Animation Mode is not active or the clipboard is empty. Available on UE 5.9; absent from UE 5.8. Params: bMirror?", "epic_call_tool"),
  },
  epic_paste_folders: {
    epicSchema: S_epic_paste_folders,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.paste_folders", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Paste folders from the clipboard into the sequence. Available on UE 5.8; absent from UE 5.9. Params: paste_token, sequence, parent_folder?", "epic_call_tool"),
  },
  epic_paste_folders_from_text: {
    epicSchema: S_epic_paste_folders_from_text,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.PasteFoldersFromText", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Paste folders and return the new objects, raising a script error if nothing was pasted. Empty text uses the clipboard. Available on UE 5.9; absent from UE 5.8. Params: textToImport, pasteFoldersParams", "epic_call_tool"),
  },
  epic_paste_sections: {
    epicSchema: S_epic_paste_sections,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.paste_sections", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Paste sections from the clipboard onto the given tracks. Available on UE 5.8; absent from UE 5.9. Params: paste_token, target_tracks, paste_frame?", "epic_call_tool"),
  },
  epic_paste_sections_from_text: {
    epicSchema: S_epic_paste_sections_from_text,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.PasteSectionsFromText", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Paste sections and return the new objects, raising a script error if nothing was pasted. Empty text uses the clipboard. Available on UE 5.9; absent from UE 5.8. Params: textToImport, pasteSectionsParams", "epic_call_tool"),
  },
  epic_paste_tracks: {
    epicSchema: S_epic_paste_tracks,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.paste_tracks", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Paste tracks from the clipboard onto the given bindings. Available on UE 5.8; absent from UE 5.9. Params: paste_token, sequence, target_bindings, parent_folder?", "epic_call_tool"),
  },
  epic_paste_tracks_from_text: {
    epicSchema: S_epic_paste_tracks_from_text,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.PasteTracksFromText", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Paste tracks and return the new objects, raising a script error if nothing was pasted. Empty text uses the clipboard. Available on UE 5.9; absent from UE 5.8. Params: textToImport, pasteTracksParams", "epic_call_tool"),
  },
  epic_pause: {
    epicSchema: S_epic_pause,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.pause", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Pause playback of the current sequence. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_pause_async: {
    epicSchema: S_epic_pause_async,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.PauseAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Pause playback and wait for the stopped pose to evaluate. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_pin_actor_motion_trail: {
    epicSchema: S_epic_pin_actor_motion_trail,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.PinActorMotionTrail", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Pin an actor's motion trail without changing the actor or control selection; when the actor's trail is not drawn yet, selected trail keys are deselected. The actor needs a Transform track in the sequence. Raises when trails are hidden, the actor is not bound with a Transform track, or the trail could not be pinned, as when MaxNumberPinned trails are already pinned. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, actor", "epic_call_tool"),
  },
  epic_pin_control_rig_motion_trail: {
    epicSchema: S_epic_pin_control_rig_motion_trail,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.PinControlRigMotionTrail", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Pin a Control Rig control's motion trail without selecting the control. Only Transform, TransformNoScale and EulerTransform controls have trails. Raises when trails are hidden, the rig has no track in the sequence, the control is missing or has no trail, or the trail could not be pinned, as when MaxNumberPinned trails are already pinned. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName", "epic_call_tool"),
  },
  epic_pin_curve: {
    epicSchema: S_epic_pin_curve,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.PinCurve", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Pin or unpin a channel's curve in the active Sequencer's open Curve Editor tree, as the pin button on the channel's row does (for every section on that track row). Available on UE 5.9; absent from UE 5.8. Params: channel, bPin", "epic_call_tool"),
  },
  epic_pin_selected_motion_trails: {
    epicSchema: S_epic_pin_selected_motion_trails,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.PinSelectedMotionTrails", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Pin every trail currently shown for the selection, as Shift-clicking Motion Paths does. Raises when trails are hidden. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_pin_socket_motion_trail: {
    epicSchema: S_epic_pin_socket_motion_trail,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.PinSocketMotionTrail", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Pin the motion trail of a socket or bone, as Motion Paths > Pin Socket does. The component or its actor needs a Control Rig, skeletal animation or Transform track in the sequence. Socket trails have no keys. Pinning the same socket again replaces its trail. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, component, socketName", "epic_call_tool"),
  },
  epic_play: {
    epicSchema: S_epic_play,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.play", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Start playback of the current sequence. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_play_async: {
    epicSchema: S_epic_play_async,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.PlayAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Start playback and wait for the editor world to evaluate. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_play_to: {
    epicSchema: S_epic_play_to,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.play_to", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Play from the current position to a specific frame, then stop. Available on UE 5.8; absent from UE 5.9. Params: frame", "epic_call_tool"),
  },
  epic_play_to_frame_async: {
    epicSchema: S_epic_play_to_frame_async,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.PlayToFrameAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Play to a frame of the focused sequence, in its local display-rate time, and wait for the final pose. Fails if playback stops at another frame. Pauses playback and fails if the focused sequence changes, the tool call is canceled, or playback is still running after TimeoutSeconds (for example in loop mode). Available on UE 5.9; absent from UE 5.8. Params: frame, timeoutSeconds?", "epic_call_tool"),
  },
  epic_rebake_linked_anim_sequences: {
    epicSchema: S_epic_rebake_linked_anim_sequences,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.RebakeLinkedAnimSequences", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Export the binding's animation again into every Anim Sequence linked to it, each with its link's settings, as saving the level sequence does. The Anim Sequences are not saved. Raises a script error if the binding does not belong to the level sequence focused in Sequencer. Available on UE 5.9; absent from UE 5.8. Params: binding", "epic_call_tool"),
  },
  epic_rebake_linked_anim_sequences_async: {
    epicSchema: S_epic_rebake_linked_anim_sequences_async,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.RebakeLinkedAnimSequencesAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] RebakeLinkedAnimSequences as an async operation; the result holds the number of Anim Sequences re-baked. See RebakeLinkedAnimSequences for the parameters. Available on UE 5.9; absent from UE 5.8. Params: binding", "epic_call_tool"),
  },
  epic_rebind_component: {
    epicSchema: S_epic_rebind_component,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.rebind_component", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Rebind component bindings to a named component. Available on UE 5.8; absent from UE 5.9. Params: component_bindings, component_name", "epic_call_tool"),
  },
  epic_rebind_component__level_sequence_editor_subsystem: {
    epicSchema: S_epic_rebind_component__level_sequence_editor_subsystem,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.RebindComponent", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Rebind the component binding to the requested component Available on UE 5.9; absent from UE 5.8. Params: componentBindings, componentName", "epic_call_tool"),
  },
  epic_recreate_control_rig_with_new_settings: {
    epicSchema: S_epic_recreate_control_rig_with_new_settings,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.RecreateControlRigWithNewSettings", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Recreate a control rig track with new settings Available on UE 5.9; absent from UE 5.8. Params: inTrack, inSettingsToOverride", "epic_call_tool"),
  },
  epic_refresh_current_level_sequence_async: {
    epicSchema: S_epic_refresh_current_level_sequence_async,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.RefreshCurrentLevelSequenceAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Refresh the current sequence and wait for the editor world to tick. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_refresh_sequence: {
    epicSchema: S_epic_refresh_sequence,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.refresh_sequence", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Force refresh the Sequencer editor UI on the next tick. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_remove: {
    epicSchema: S_epic_remove,
    epicTool: { toolset: "SequencerScripting.MovieSceneBindingExtensions", name: "SequencerScripting.MovieSceneBindingExtensions.Remove", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneBindingExtensions] Remove the specified binding Available on UE 5.9; absent from UE 5.8. Params: inBinding", "epic_call_tool"),
  },
  epic_remove_actors_from_binding: {
    epicSchema: S_epic_remove_actors_from_binding,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.remove_actors_from_binding", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Remove specific actors from a binding. Available on UE 5.8; absent from UE 5.9. Params: actors, binding", "epic_call_tool"),
  },
  epic_remove_actors_from_binding__level_sequence_editor_subsystem: {
    epicSchema: S_epic_remove_actors_from_binding__level_sequence_editor_subsystem,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.RemoveActorsFromBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Removes the given actors from the binding. Raises an error when the list is empty or any actor is not bound to it. Available on UE 5.9; absent from UE 5.8. Params: actors, objectBinding", "epic_call_tool"),
  },
  epic_remove_all_bindings: {
    epicSchema: S_epic_remove_all_bindings,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.remove_all_bindings", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Remove all bound actors from a binding. Available on UE 5.8; absent from UE 5.9. Params: binding", "epic_call_tool"),
  },
  epic_remove_all_bindings__level_sequence_editor_subsystem: {
    epicSchema: S_epic_remove_all_bindings__level_sequence_editor_subsystem,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.RemoveAllBindings", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Remove all bound actors from this track Available on UE 5.9; absent from UE 5.8. Params: objectBinding", "epic_call_tool"),
  },
  epic_remove_all_links: {
    epicSchema: S_epic_remove_all_links,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.RemoveAllLinks", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] RemoveAllLinks Available on UE 5.9; absent from UE 5.8. Params: deformer, pinPath", "epic_call_tool"),
  },
  epic_remove_binding: {
    epicSchema: S_epic_remove_binding,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.remove_binding", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Remove a binding from the sequence. Available on UE 5.8; absent from UE 5.9. Params: binding", "epic_call_tool"),
  },
  epic_remove_binding_tag: {
    epicSchema: S_epic_remove_binding_tag,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.remove_binding_tag", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Remove a tag from the sequence entirely. Clears the tag from every binding that had it and unregisters the tag name from the sequence. Available on UE 5.8; absent from UE 5.9. Params: sequence, tag_name", "epic_call_tool"),
  },
  epic_remove_binding_tag__movie_scene_binding_tag_extensions: {
    epicSchema: S_epic_remove_binding_tag__movie_scene_binding_tag_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneBindingTagExtensions", name: "SequencerScripting.MovieSceneBindingTagExtensions.RemoveBindingTag", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneBindingTagExtensions] Remove a tag entirely from the sequence. Clears the tag from every binding that had it, and removes the registration. Available on UE 5.9; absent from UE 5.8. Params: sequence, tagName", "epic_call_tool"),
  },
  epic_remove_channel_key_at_frame: {
    epicSchema: S_epic_remove_channel_key_at_frame,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.RemoveChannelKeyAtFrame", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Remove the first key in a display-rate frame, including its subframes. Returns false if no key matches. Available on UE 5.9; absent from UE 5.8. Params: section, channelName, frame", "epic_call_tool"),
  },
  epic_remove_channel_override: {
    epicSchema: S_epic_remove_channel_override,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.RemoveChannelOverride", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Remove the named channel's override so its keys animate it again. Raises when the channel is not overridden. Available on UE 5.9; absent from UE 5.8. Params: section, channelName", "epic_call_tool"),
  },
  epic_remove_child_folder: {
    epicSchema: S_epic_remove_child_folder,
    epicTool: { toolset: "SequencerScripting.MovieSceneFolderExtensions", name: "SequencerScripting.MovieSceneFolderExtensions.RemoveChildFolder", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneFolderExtensions] Remove a child folder from the given folder Available on UE 5.9; absent from UE 5.8. Params: targetFolder, folderToRemove", "epic_call_tool"),
  },
  epic_remove_child_object_binding: {
    epicSchema: S_epic_remove_child_object_binding,
    epicTool: { toolset: "SequencerScripting.MovieSceneFolderExtensions", name: "SequencerScripting.MovieSceneFolderExtensions.RemoveChildObjectBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneFolderExtensions] Remove an object binding from the given folder Available on UE 5.9; absent from UE 5.8. Params: folder, inObjectBinding", "epic_call_tool"),
  },
  epic_remove_child_track: {
    epicSchema: S_epic_remove_child_track,
    epicTool: { toolset: "SequencerScripting.MovieSceneFolderExtensions", name: "SequencerScripting.MovieSceneFolderExtensions.RemoveChildTrack", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneFolderExtensions] Remove a track from the given folder Available on UE 5.9; absent from UE 5.8. Params: folder, inTrack", "epic_call_tool"),
  },
  epic_remove_component_binding: {
    epicSchema: S_epic_remove_component_binding,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.RemoveComponentBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Removes a binding and every node reading it. The primary binding cannot be removed. Available on UE 5.9; absent from UE 5.8. Params: deformer, bindingName", "epic_call_tool"),
  },
  epic_remove_constraint: {
    epicSchema: S_epic_remove_constraint,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.RemoveConstraint", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Remove a constraint, as deleting it in the Constraints panel does. With bCompensate the child keeps its world transform where the constraint was active. Raises a script error if the world or constraint is null, or the constraint is not in that world. Available on UE 5.9; absent from UE 5.8. Params: world, constraint, bCompensate?", "epic_call_tool"),
  },
  epic_remove_control_rig_available_space: {
    epicSchema: S_epic_remove_control_rig_available_space,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.RemoveControlRigAvailableSpace", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Remove a space from a control's space picker list, as the level editor's Delete does; a space the rig asset lists stays removed for this instance. Raises a script error if the Control Rig is null, the control does not exist, or the space is world or the default parent. Available on UE 5.9; absent from UE 5.8. Params: controlRig, controlName, space", "epic_call_tool"),
  },
  epic_remove_control_rig_override: {
    epicSchema: S_epic_remove_control_rig_override,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.RemoveControlRigOverride", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Remove a control setting override from the Control Rig's override assets, as Anim Details' Clear Override does, and reconstruct the rig so the setting returns to the rig's value. Raises a script error, removing nothing, if the Control Rig is null or an override asset holding the override is read-only in this editor (as in UEFN). Available on UE 5.9; absent from UE 5.8. Params: controlRig, controlName, propertyPath", "epic_call_tool"),
  },
  epic_remove_default: {
    epicSchema: S_epic_remove_default,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.RemoveDefault", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Clear the channel's default value. Available on UE 5.9; absent from UE 5.8. Params: channel", "epic_call_tool"),
  },
  epic_remove_graph: {
    epicSchema: S_epic_remove_graph,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.RemoveGraph", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Removes a graph and its nodes. The update graph cannot be removed. Available on UE 5.9; absent from UE 5.8. Params: deformer, graphPath", "epic_call_tool"),
  },
  epic_remove_graph_binding: {
    epicSchema: S_epic_remove_graph_binding,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.RemoveGraphBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Removes a binding from the signature of a sub-graph, with its pins and links. Available on UE 5.9; absent from UE 5.8. Params: deformer, graphPath, bindingName", "epic_call_tool"),
  },
  epic_remove_invalid_bindings: {
    epicSchema: S_epic_remove_invalid_bindings,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.remove_invalid_bindings", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Remove missing or broken actor references from a binding. Available on UE 5.8; absent from UE 5.9. Params: binding", "epic_call_tool"),
  },
  epic_remove_invalid_bindings__level_sequence_editor_subsystem: {
    epicSchema: S_epic_remove_invalid_bindings__level_sequence_editor_subsystem,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.RemoveInvalidBindings", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Remove missing objects bound to this track Available on UE 5.9; absent from UE 5.8. Params: objectBinding", "epic_call_tool"),
  },
  epic_remove_kernel_binding: {
    epicSchema: S_epic_remove_kernel_binding,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.RemoveKernelBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Removes a binding, with its pin and links. Available on UE 5.9; absent from UE 5.8. Params: deformer, nodePath, bindingName", "epic_call_tool"),
  },
  epic_remove_kernel_input_group: {
    epicSchema: S_epic_remove_kernel_input_group,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.RemoveKernelInputGroup", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Removes a secondary input group, with its bindings, pins and links. Available on UE 5.9; absent from UE 5.8. Params: deformer, nodePath, groupName", "epic_call_tool"),
  },
  epic_remove_key: {
    epicSchema: S_epic_remove_key,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.RemoveKey", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Remove a key only if its complete channel identity and live key handle match. Available on UE 5.9; absent from UE 5.8. Params: channel, key", "epic_call_tool"),
  },
  epic_remove_key_at_frame: {
    epicSchema: S_epic_remove_key_at_frame,
    epicTool: { toolset: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools", name: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.remove_key_at_frame", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Remove a key at a specific frame from a channel. Available on UE 5.8; absent from UE 5.9. Params: section, channel_name, frame", "epic_call_tool"),
  },
  epic_remove_link: {
    epicSchema: S_epic_remove_link,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.RemoveLink", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Remove Link Available on UE 5.9; absent from UE 5.8. Params: deformer, outputPinPath, inputPinPath", "epic_call_tool"),
  },
  epic_remove_loop_binding: {
    epicSchema: S_epic_remove_loop_binding,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.RemoveLoopBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Remove Loop Binding Available on UE 5.9; absent from UE 5.8. Params: deformer, nodePath, bindingName", "epic_call_tool"),
  },
  epic_remove_node_paths_from_node_group: {
    epicSchema: S_epic_remove_node_paths_from_node_group,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.RemoveNodePathsFromNodeGroup", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Remove stored node paths from a Sequencer Group of the focused sequence without resolving them, so paths of deleted nodes can be cleared. A path that is not in the group is left alone. Available on UE 5.9; absent from UE 5.8. Params: group, paths", "epic_call_tool"),
  },
  epic_remove_nodes: {
    epicSchema: S_epic_remove_nodes,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.RemoveNodes", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Removes nodes and their links, as one undo step. Nothing is removed if any path is invalid. Available on UE 5.9; absent from UE 5.8. Params: deformer, nodePaths", "epic_call_tool"),
  },
  epic_remove_nodes_from_node_group: {
    epicSchema: S_epic_remove_nodes_from_node_group,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.RemoveNodesFromNodeGroup", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Remove nodes from a Sequencer Group of the focused sequence. A node that is not in the group is left alone. Available on UE 5.9; absent from UE 5.8. Params: group, nodes", "epic_call_tool"),
  },
  epic_remove_resource: {
    epicSchema: S_epic_remove_resource,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.RemoveResource", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Removes a resource and every node accessing it. Available on UE 5.9; absent from UE 5.8. Params: deformer, resourceName", "epic_call_tool"),
  },
  epic_remove_root_folder: {
    epicSchema: S_epic_remove_root_folder,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.remove_root_folder", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Remove a root-level folder from the sequence. Available on UE 5.8; absent from UE 5.9. Params: sequence, folder", "epic_call_tool"),
  },
  epic_remove_root_folder_from_sequence: {
    epicSchema: S_epic_remove_root_folder_from_sequence,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.RemoveRootFolderFromSequence", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Remove a root folder from the given sequence. Will throw an exception if the specified folder is not valid or not a root folder. Available on UE 5.9; absent from UE 5.8. Params: sequence, folder", "epic_call_tool"),
  },
  epic_remove_section: {
    epicSchema: S_epic_remove_section,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.remove_section", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Remove a section from a track. Available on UE 5.8; absent from UE 5.9. Params: track, section", "epic_call_tool"),
  },
  epic_remove_section__movie_scene_track_extensions: {
    epicSchema: S_epic_remove_section__movie_scene_track_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneTrackExtensions", name: "SequencerScripting.MovieSceneTrackExtensions.RemoveSection", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneTrackExtensions] Remove the specified section Available on UE 5.9; absent from UE 5.8. Params: track, section", "epic_call_tool"),
  },
  epic_remove_selected_from_anim_layer: {
    epicSchema: S_epic_remove_selected_from_anim_layer,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.RemoveSelectedFromAnimLayer", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Remove the controls and objects selected in Sequencer from an anim layer, as the panel's Remove Selected does. A layer left empty is deleted. Raises a script error if Index is out of range or is the Base layer, or nothing is selected. Available on UE 5.9; absent from UE 5.8. Params: index", "epic_call_tool"),
  },
  epic_remove_selection_from_selection_set: {
    epicSchema: S_epic_remove_selection_from_selection_set,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.RemoveSelectionFromSelectionSet", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Remove the controls and actors selected in Sequencer from a selection set. Raises a script error if no level sequence is open in Sequencer, the set does not exist, or nothing is selected. Available on UE 5.9; absent from UE 5.8. Params: id", "epic_call_tool"),
  },
  epic_remove_socket: {
    epicSchema: S_epic_remove_socket,
    epicTool: { toolset: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", name: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.remove_socket", engines: ["5.8"], successors: [{ toolset: "EditorToolset.SkeletalMeshToolset", name: "EditorToolset.SkeletalMeshToolset.RemoveSocket", engines: ["5.9"], schema: S_epic_remove_socket__1, rename: {"socket_name":"socketName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Removes a named socket from a skeletal mesh. Params: mesh, socket_name. On UE 5.9 it calls EditorToolset.SkeletalMeshToolset.RemoveSocket (Params: mesh, socketName).", "epic_call_tool"),
  },
  epic_remove_track: {
    epicSchema: S_epic_remove_track,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.remove_track", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Remove a track from a binding. Available on UE 5.8; absent from UE 5.9. Params: binding, track", "epic_call_tool"),
  },
  epic_remove_track__movie_scene_binding_extensions: {
    epicSchema: S_epic_remove_track__movie_scene_binding_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneBindingExtensions", name: "SequencerScripting.MovieSceneBindingExtensions.RemoveTrack", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneBindingExtensions] Remove the specified track from this binding Available on UE 5.9; absent from UE 5.8. Params: inBinding, trackToRemove", "epic_call_tool"),
  },
  epic_remove_track__movie_scene_sequence_extensions: {
    epicSchema: S_epic_remove_track__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.RemoveTrack", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Remove a root or binding track owned by this sequence. Returns false for a track this sequence does not contain, including a stale camera-cut track. Available on UE 5.9; absent from UE 5.8. Params: sequence, track", "epic_call_tool"),
  },
  epic_remove_track_from_sequence: {
    epicSchema: S_epic_remove_track_from_sequence,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.remove_track_from_sequence", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Remove a sequence-level (master) track. Available on UE 5.8; absent from UE 5.9. Params: sequence, track", "epic_call_tool"),
  },
  epic_remove_variable: {
    epicSchema: S_epic_remove_variable,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.remove_variable", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Remove a member variable from the Control Rig. Params: control_rig, name", "epic_call_tool"),
  },
  epic_remove_variable__deformer_graph_scripting_subsystem: {
    epicSchema: S_epic_remove_variable__deformer_graph_scripting_subsystem,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.RemoveVariable", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Removes a variable and every node reading it. Available on UE 5.9; absent from UE 5.8. Params: deformer, variableName", "epic_call_tool"),
  },
  epic_rename_binding: {
    epicSchema: S_epic_rename_binding,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.RenameBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Renames a binding and its pins. Links are kept. Available on UE 5.9; absent from UE 5.8. Params: deformer, pinPath, newName", "epic_call_tool"),
  },
  epic_rename_component_binding: {
    epicSchema: S_epic_rename_component_binding,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.RenameComponentBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] The primary binding keeps its name. Available on UE 5.9; absent from UE 5.8. Params: deformer, bindingName, newName", "epic_call_tool"),
  },
  epic_rename_control_rig_control_channels: {
    epicSchema: S_epic_rename_control_rig_control_channels,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.RenameControlRigControlChannels", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Rename the Control Rig Channels in Sequencer to the specified new control names, which should be present on the Control Rig Available on UE 5.9; absent from UE 5.8. Params: inSequence, inControlRig, inOldControlNames, inNewControlNames", "epic_call_tool"),
  },
  epic_rename_control_rig_pose_asset_control: {
    epicSchema: S_epic_rename_control_rig_pose_asset_control,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.RenameControlRigPoseAssetControl", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Rename a control stored in a pose asset, for pasting onto a rig with other control names, as the Pose Library's Rename Controls does. Raises a script error if the pose asset is null, CurrentName is not in the pose, or NewName is empty or already in it. Available on UE 5.9; absent from UE 5.8. Params: poseAsset, currentName, newName", "epic_call_tool"),
  },
  epic_rename_graph: {
    epicSchema: S_epic_rename_graph,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.RenameGraph", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Renames a trigger graph, function graph or sub-graph. The setup and update graphs keep their names. Available on UE 5.9; absent from UE 5.8. Params: deformer, graphPath, newName", "epic_call_tool"),
  },
  epic_rename_kernel_input_group: {
    epicSchema: S_epic_rename_kernel_input_group,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.RenameKernelInputGroup", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Renames a secondary input group, which is also the HLSL namespace of its bindings. Links are kept. Available on UE 5.9; absent from UE 5.8. Params: deformer, nodePath, groupName, newName", "epic_call_tool"),
  },
  epic_rename_resource: {
    epicSchema: S_epic_rename_resource,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.RenameResource", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] RenameResource Available on UE 5.9; absent from UE 5.8. Params: deformer, resourceName, newName", "epic_call_tool"),
  },
  epic_rename_socket: {
    epicSchema: S_epic_rename_socket,
    epicTool: { toolset: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", name: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.rename_socket", engines: ["5.8"], successors: [{ toolset: "EditorToolset.SkeletalMeshToolset", name: "EditorToolset.SkeletalMeshToolset.RenameSocket", engines: ["5.9"], schema: S_epic_rename_socket__1, rename: {"old_name":"oldName","new_name":"newName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Renames a socket on a skeletal mesh. Params: mesh, old_name, new_name. On UE 5.9 it calls EditorToolset.SkeletalMeshToolset.RenameSocket (Params: mesh, oldName, newName).", "epic_call_tool"),
  },
  epic_rename_variable: {
    epicSchema: S_epic_rename_variable,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.RenameVariable", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] RenameVariable Available on UE 5.9; absent from UE 5.8. Params: deformer, variableName, newName", "epic_call_tool"),
  },
  epic_reorder_anim_layers: {
    epicSchema: S_epic_reorder_anim_layers,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.reorder_anim_layers", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Move an animation layer from one index to another. Cannot move the base layer (index 0). Available on UE 5.8; absent from UE 5.9. Params: old_index, new_index", "epic_call_tool"),
  },
  epic_reorder_anim_layers__control_rig_sequencer_editor_library: {
    epicSchema: S_epic_reorder_anim_layers__control_rig_sequencer_editor_library,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.ReorderAnimLayers", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Move Anim Layer to new index, this will also change evaluation order Available on UE 5.9; absent from UE 5.8. Params: oldIndex, newIndex", "epic_call_tool"),
  },
  epic_reorder_nodes: {
    epicSchema: S_epic_reorder_nodes,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.ReorderNodes", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Place nodes among their siblings in the order given, as the outliner's Sort by Selection Order does, storing the result in their sorting orders. A node in Nodes with a descendant also in Nodes keeps its place. Raises for a node that cannot be sorted, such as a channel row, and for a node under a binding, whose children keep their type and label order. Available on UE 5.9; absent from UE 5.8. Params: nodes", "epic_call_tool"),
  },
  epic_replace_binding_with_actors: {
    epicSchema: S_epic_replace_binding_with_actors,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.replace_binding_with_actors", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Replace all bound actors on a binding with new ones. Available on UE 5.8; absent from UE 5.9. Params: actors, binding", "epic_call_tool"),
  },
  epic_replace_binding_with_actors__level_sequence_editor_subsystem: {
    epicSchema: S_epic_replace_binding_with_actors__level_sequence_editor_subsystem,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.ReplaceBindingWithActors", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Replaces the binding with the given actors Available on UE 5.9; absent from UE 5.8. Params: actors, objectBinding", "epic_call_tool"),
  },
  epic_replace_control_rig: {
    epicSchema: S_epic_replace_control_rig,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.ReplaceControlRig", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Replace the Control Rig on the track with a new Control Rig. Note it will now be owned by the track. When the track's sequence is focused in Sequencer, a rig with no bound object is bound to the track's skeletal mesh and initialized, and the new rig replaces the old one in Sequencer and Animation Mode, so setting its values keys it. The new rig is constructed with the track's settings and rotation order overrides, and the controls selected on the old rig are selected on it. One undoable step. Raises a script error if the track is not a writable Control Rig track, the rig is already on it or on another track, one is layered and the other is not, or the editor's asset reference filter refuses a reference from the track's sequence to the rig's asset (as in UEFN). ReplaceControlRigFromReference creates the new rig from a Control Rig class or asset. Available on UE 5.9; absent from UE 5.8. Params: inControlRigTrack, newControlRig", "epic_call_tool"),
  },
  epic_replace_control_rig_from_reference: {
    epicSchema: S_epic_replace_control_rig_from_reference,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.ReplaceControlRigFromReference", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Replace the Control Rig on a Control Rig track with a new rig created from a Control Rig class or asset, as FindOrCreateControlRigTrackFromReference creates one. Keys on controls the new rig also has are kept, a layered track stays layered, an FK rig keeps the old FK rig's apply mode, and the track is renamed after the new rig when it comes from a different class or asset. The new rig is constructed with the track's settings and rotation order overrides, and the controls selected on the old rig are selected on it. When the track's sequence is focused in Sequencer, the new rig is bound to the track's skeletal mesh and replaces the old one in Sequencer and Animation Mode, so it can be read and keyed at once. One undoable step. Raises a script error if the track is not a writable Control Rig track, the reference is not a Control Rig class or asset, the track is layered and the new rig is not an FK rig and has no backwards solve event, or the editor's asset reference filter refuses a reference from the track's sequence to the rig (as in UEFN). Available on UE 5.9; absent from UE 5.8. Params: inTrack, inRef", "epic_call_tool"),
  },
  epic_reset_control_rig_controls_to_default: {
    epicSchema: S_epic_reset_control_rig_controls_to_default,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.ResetControlRigControlsToDefault", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set controls to their initial values at the current Sequencer time, as Anim Details' Reset To Default does; this covers every control type, where ZeroTransforms covers transforms. Raises a script error if the sequence is not the one focused in Sequencer or a control does not exist. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlNames, bSetKey?", "epic_call_tool"),
  },
  epic_reset_playback_context: {
    epicSchema: S_epic_reset_playback_context,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.ResetPlaybackContext", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Let the open Level Sequence editor choose its world automatically again, from the auto-bind to PIE and Simulate settings. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_restore_pre_animated_state_async: {
    epicSchema: S_epic_restore_pre_animated_state_async,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.RestorePreAnimatedStateAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Restore captured pre-animated state and wait for the editor world to tick. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_save_control_rig_pose_to_clipboard: {
    epicSchema: S_epic_save_control_rig_pose_to_clipboard,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SaveControlRigPoseToClipboard", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Save the pose of the selected controls to Animation Mode's pose clipboard, as Save Pose does; with bWholePose, every control of the rig with a selected control, or else of the selected bound actor's rig, as Save Whole Pose does. Raises a script error if Animation Mode is not active, or without bWholePose if no control is selected. Available on UE 5.9; absent from UE 5.8. Params: bWholePose?", "epic_call_tool"),
  },
  epic_save_default_spawnable_state: {
    epicSchema: S_epic_save_default_spawnable_state,
    epicTool: { toolset: "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools", name: "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools.save_default_spawnable_state", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SaveDefaultSpawnableState", engines: ["5.9"], schema: S_epic_save_default_spawnable_state__1 }] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools] Save the current state of a spawnable as its default. Params: binding. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.SaveDefaultSpawnableState (Params: objectBinding).", "epic_call_tool"),
  },
  epic_select_all_control_rig_controls: {
    epicSchema: S_epic_select_all_control_rig_controls,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SelectAllControlRigControls", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Select every control on the Control Rigs in Animation Mode. Raises a script error if Animation Mode is not active or has no Control Rigs, or if bAlreadySelected is true and no Control Rig in it has a selected control. Available on UE 5.9; absent from UE 5.8. Params: bAlreadySelected?", "epic_call_tool"),
  },
  epic_select_anim_layer_objects: {
    epicSchema: S_epic_select_anim_layer_objects,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SelectAnimLayerObjects", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Select the controls and objects in an anim layer, as the panel's Select Objects does. Raises a script error if Index is out of range or the layer has no controls or objects. Available on UE 5.9; absent from UE 5.8. Params: index, bClearSelection?", "epic_call_tool"),
  },
  epic_select_anim_layer_weight_channel: {
    epicSchema: S_epic_select_anim_layer_weight_channel,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SelectAnimLayerWeightChannel", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Select an anim layer's weight channels in Sequencer, as the panel's weight button does. Raises a script error if Index is out of range. Available on UE 5.9; absent from UE 5.8. Params: index, bClearSelection?", "epic_call_tool"),
  },
  epic_select_bindings: {
    epicSchema: S_epic_select_bindings,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.select_bindings", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the binding selection in the Sequencer editor. Available on UE 5.8; absent from UE 5.9. Params: bindings", "epic_call_tool"),
  },
  epic_select_bindings__level_sequence_editor_blueprint_library: {
    epicSchema: S_epic_select_bindings__level_sequence_editor_blueprint_library,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SelectBindings", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Adds bindings of the focused sequence to the selection without clearing it. Raises an error when no Sequencer is open, or without selecting anything when any binding is invalid or foreign. Available on UE 5.9; absent from UE 5.8. Params: objectBindings", "epic_call_tool"),
  },
  epic_select_channels: {
    epicSchema: S_epic_select_channels,
    epicTool: { toolset: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools", name: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.select_channels", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Set the channel selection in the Sequencer editor. Available on UE 5.8; absent from UE 5.9. Params: channels", "epic_call_tool"),
  },
  epic_select_channels__level_sequence_editor_blueprint_library: {
    epicSchema: S_epic_select_channels__level_sequence_editor_blueprint_library,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SelectChannels", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Adds channels to the selection without clearing it. A channel name matches a channel of the section or the label of the outliner node that contains it. Raises an error when no Sequencer is open, or without selecting anything when any section is foreign or any name matches nothing. Available on UE 5.9; absent from UE 5.8. Params: channels", "epic_call_tool"),
  },
  epic_select_control: {
    epicSchema: S_epic_select_control,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.select_control", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Select or deselect a control on a Control Rig. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, selected?", "epic_call_tool"),
  },
  epic_select_control_rig_controls: {
    epicSchema: S_epic_select_control_rig_controls,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SelectControlRigControls", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Select or deselect controls on a Control Rig. Selection drives the selection-based functions such as TweenControlRig, BlendValuesOnSelected, MirrorSelectedControls, ZeroTransforms, and AddAnimLayerFromSelection. Available on UE 5.9; absent from UE 5.8. Params: controlRig, controlNames, bSelect?, bClearSelection?", "epic_call_tool"),
  },
  epic_select_control_rig_pose_asset_controls: {
    epicSchema: S_epic_select_control_rig_pose_asset_controls,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SelectControlRigPoseAssetControls", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Select the controls stored in a pose asset, as the Pose Library's Select Controls does. Raises a script error if the pose asset or Control Rig is null, or none of the pose's controls exist on the rig. Available on UE 5.9; absent from UE 5.8. Params: poseAsset, controlRig, bMirror?, bClearSelection?", "epic_call_tool"),
  },
  epic_select_control_rig_pose_clipboard_controls: {
    epicSchema: S_epic_select_control_rig_pose_clipboard_controls,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SelectControlRigPoseClipboardControls", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Select the controls in Animation Mode's pose clipboard, as Select Pose and Select Mirror Pose do. Raises a script error if Animation Mode is not active or the clipboard is empty. Available on UE 5.9; absent from UE 5.8. Params: bMirror?", "epic_call_tool"),
  },
  epic_select_curve_editor_keys: {
    epicSchema: S_epic_select_curve_editor_keys,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SelectCurveEditorKeys", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Add the specified key indices to the active Curve Editor selection. Available on UE 5.9; absent from UE 5.8. Params: channel, indices", "epic_call_tool"),
  },
  epic_select_folders: {
    epicSchema: S_epic_select_folders,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.select_folders", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the folder selection in the Sequencer editor. Available on UE 5.8; absent from UE 5.9. Params: folders", "epic_call_tool"),
  },
  epic_select_folders__level_sequence_editor_blueprint_library: {
    epicSchema: S_epic_select_folders__level_sequence_editor_blueprint_library,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SelectFolders", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Adds folders from the focused sequence to the selection without clearing it. Raises an error when no Sequencer is open, or without selecting anything when any folder is invalid or foreign. Available on UE 5.9; absent from UE 5.8. Params: folders", "epic_call_tool"),
  },
  epic_select_mirrored_controls: {
    epicSchema: S_epic_select_mirrored_controls,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.select_mirrored_controls", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Select the mirrored counterparts of the currently selected controls. Replaces the current selection with mirrored controls. Available on UE 5.8; absent from UE 5.9. Params: none", "epic_call_tool"),
  },
  epic_select_mirrored_controls__control_rig_sequencer_editor_library: {
    epicSchema: S_epic_select_mirrored_controls__control_rig_sequencer_editor_library,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SelectMirroredControls", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Replace the selection with the mirrored counterparts of the selected controls. Requires Animation Mode to be active. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_select_sections: {
    epicSchema: S_epic_select_sections,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.select_sections", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the section selection in the Sequencer editor. Available on UE 5.8; absent from UE 5.9. Params: sections", "epic_call_tool"),
  },
  epic_select_sections__level_sequence_editor_blueprint_library: {
    epicSchema: S_epic_select_sections__level_sequence_editor_blueprint_library,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SelectSections", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Adds sections from the focused sequence to the selection without clearing it. Raises an error when no Sequencer is open, or without selecting anything when any section is invalid or foreign. Available on UE 5.9; absent from UE 5.8. Params: sections", "epic_call_tool"),
  },
  epic_select_selection_set: {
    epicSchema: S_epic_select_selection_set,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SelectSelectionSet", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Select or deselect a selection set's controls and actors on the actors sets act on, as clicking the set does. Raises a script error if no level sequence is open in Sequencer or the set does not exist. Available on UE 5.9; absent from UE 5.8. Params: id, bMirror?, bAdd?, bToggle?, bSelect?", "epic_call_tool"),
  },
  epic_select_tracks: {
    epicSchema: S_epic_select_tracks,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.select_tracks", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the track selection in the Sequencer editor. Available on UE 5.8; absent from UE 5.9. Params: tracks", "epic_call_tool"),
  },
  epic_select_tracks__level_sequence_editor_blueprint_library: {
    epicSchema: S_epic_select_tracks__level_sequence_editor_blueprint_library,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SelectTracks", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Adds tracks from the focused sequence to the selection without clearing it. Raises an error when no Sequencer is open, or without selecting anything when any track is invalid or foreign. Available on UE 5.9; absent from UE 5.8. Params: tracks", "epic_call_tool"),
  },
  epic_set_actor_world_transform: {
    epicSchema: S_epic_set_actor_world_transform,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetActorWorldTransform", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Key an actor's world transform at a frame in the level sequence focused in Sequencer. The key is expressed in the actor's attach parent and, where the actor is the child of an active constraint, in that constraint's space, so the actor reaches the transform. Raises a script error for the cases SetActorWorldTransforms raises for. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, actor, frame, worldTransform, timeUnit?", "epic_call_tool"),
  },
  epic_set_actor_world_transforms: {
    epicSchema: S_epic_set_actor_world_transforms,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetActorWorldTransforms", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Key an actor's world transforms at frames in the level sequence focused in Sequencer, adding a Transform track and section if needed. Each key is expressed in the actor's attach parent and, where the actor is the child of an active constraint, in that constraint's space, so the actor reaches the transform. Location, rotation and scale are keyed. Raises a script error if the sequence is not the one focused in Sequencer, the actor is null or not bound in it, or the counts differ. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, actor, frames, worldTransforms, timeUnit?", "epic_call_tool"),
  },
  epic_set_actor_world_transforms_async: {
    epicSchema: S_epic_set_actor_world_transforms_async,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetActorWorldTransformsAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] SetActorWorldTransforms as an async operation. See SetActorWorldTransforms for the parameters. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, actor, frames, worldTransforms, timeUnit?", "epic_call_tool"),
  },
  epic_set_anim_layer_key: {
    epicSchema: S_epic_set_anim_layer_key,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetAnimLayerKey", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Key an anim layer's current values at the current Sequencer time, on the layer's selected controls and objects or, if none are selected, on all of them. Available on UE 5.9; absent from UE 5.8. Params: index", "epic_call_tool"),
  },
  epic_set_anim_layer_passthrough_key: {
    epicSchema: S_epic_set_anim_layer_passthrough_key,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetAnimLayerPassthroughKey", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set a passthrough key on an anim layer at the current Sequencer time, on the layer's selected controls and objects or, if none are selected, on all of them. A passthrough key makes the layer contribute nothing at that time: additive layers key zero and override layers key the value of the layers below. On the Base layer this keys the inverted input pose instead. Available on UE 5.9; absent from UE 5.8. Params: index", "epic_call_tool"),
  },
  epic_set_anim_layer_properties: {
    epicSchema: S_epic_set_anim_layer_properties,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetAnimLayerProperties", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set an anim layer's name, mute, lock, weight, type and keyed state; only the properties that differ from the layer's are applied, in one undo step. Raises a script error, changing nothing, if no level sequence is open in Sequencer, Index is out of range, the name is empty, the type is not Base for the Base layer or is Base for another layer, or bKeyed is false on the keyed layer. Available on UE 5.9; absent from UE 5.8. Params: index, properties", "epic_call_tool"),
  },
  epic_set_anim_mode_gizmo_scale: {
    epicSchema: S_epic_set_anim_mode_gizmo_scale,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_anim_mode_gizmo_scale", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set the editor's transform gizmo size. Writes UTransformGizmoEditorSettings::TransformGizmoSize. The CR-specific gizmo scale was removed in UE 5.8 in favor of this editor-wide setting. Available on UE 5.8; absent from UE 5.9. Params: scale", "epic_call_tool"),
  },
  epic_set_anim_mode_hide_manips: {
    epicSchema: S_epic_set_anim_mode_hide_manips,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_anim_mode_hide_manips", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Toggle whether Animation Mode hides all manipulators. Available on UE 5.8; absent from UE 5.9. Params: hide", "epic_call_tool"),
  },
  epic_set_anim_mode_hierarchy: {
    epicSchema: S_epic_set_anim_mode_hierarchy,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_anim_mode_hierarchy", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Toggle the Animation Mode hierarchy lines/dots display. Available on UE 5.8; absent from UE 5.9. Params: enabled", "epic_call_tool"),
  },
  epic_set_anim_mode_local_spaces: {
    epicSchema: S_epic_set_anim_mode_local_spaces,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_anim_mode_local_spaces", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Toggle multi-select transforms acting in each control's own space. When True, transforming multiple selected controls respects each control's own local space. When False, all use a shared reference. Available on UE 5.8; absent from UE 5.9. Params: enabled", "epic_call_tool"),
  },
  epic_set_anim_mode_nulls: {
    epicSchema: S_epic_set_anim_mode_nulls,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_anim_mode_nulls", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Toggle the Animation Mode nulls display. Available on UE 5.8; absent from UE 5.9. Params: enabled", "epic_call_tool"),
  },
  epic_set_anim_mode_only_rig_sel: {
    epicSchema: S_epic_set_anim_mode_only_rig_sel,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_anim_mode_only_rig_sel", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Toggle Animation Mode restricting viewport selection to rig controls. Available on UE 5.8; absent from UE 5.9. Params: only_rig", "epic_call_tool"),
  },
  epic_set_animation_mode_settings: {
    epicSchema: S_epic_set_animation_mode_settings,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetAnimationModeSettings", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set every Animation Mode preference and save them to the user's config. Open viewports update immediately. To change some preferences only, read them with GetAnimationModeSettings, edit those fields, and pass the result back. Raises a script error, changing nothing, if TransformGizmoSize is outside -10 to 150, AxisScale or AxisThickness is negative, or a tween setting is not finite. Available on UE 5.9; absent from UE 5.8. Params: settings", "epic_call_tool"),
  },
  epic_set_animation_mode_tool_open: {
    epicSchema: S_epic_set_animation_mode_tool_open,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetAnimationModeToolOpen", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Open or close one of Animation Mode's tools, as its toolbar button or command does. Raises a script error if Animation Mode is not active. Available on UE 5.9; absent from UE 5.8. Params: tool, bOpen", "epic_call_tool"),
  },
  epic_set_binding_data_domain: {
    epicSchema: S_epic_set_binding_data_domain,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.SetBindingDataDomain", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Sets the data domain of a binding and its pins, which removes the links the new data domain is incompatible with. Available on UE 5.9; absent from UE 5.8. Params: deformer, pinPath, dataDomain", "epic_call_tool"),
  },
  epic_set_binding_data_type: {
    epicSchema: S_epic_set_binding_data_type,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.SetBindingDataType", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Sets the data type of a binding and its pins, which removes their links. Available on UE 5.9; absent from UE 5.8. Params: deformer, pinPath, dataType", "epic_call_tool"),
  },
  epic_set_binding_name: {
    epicSchema: S_epic_set_binding_name,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_binding_name", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the display name of a binding. Available on UE 5.8; absent from UE 5.9. Params: binding, name", "epic_call_tool"),
  },
  epic_set_blend_type: {
    epicSchema: S_epic_set_blend_type,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.SetBlendType", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Set a blend type supported by the section's track. Available on UE 5.9; absent from UE 5.8. Params: section, inBlendType", "epic_call_tool"),
  },
  epic_set_blender_system: {
    epicSchema: S_epic_set_blender_system,
    epicTool: { toolset: "SequencerScripting.MovieSceneTrackExtensions", name: "SequencerScripting.MovieSceneTrackExtensions.SetBlenderSystem", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneTrackExtensions] Set the system that blends the track's overlapping sections. Must be one of GetSupportedBlenderSystems. Available on UE 5.9; absent from UE 5.8. Params: track, blenderSystem", "epic_call_tool"),
  },
  epic_set_bool: {
    epicSchema: S_epic_set_bool,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_bool", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set a bool control value at a specific frame. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, frame, value, set_key?", "epic_call_tool"),
  },
  epic_set_bound_actor_class: {
    epicSchema: S_epic_set_bound_actor_class,
    epicTool: { toolset: "TemplateSequenceEditor.TemplateSequenceEditorSubsystem", name: "TemplateSequenceEditor.TemplateSequenceEditorSubsystem.SetBoundActorClass", engines: ["5.9"] },
    ...bp("mutate", "[Epic TemplateSequenceEditor.TemplateSequenceEditorSubsystem] Rebind the sequence to ActorClass by replacing its root spawnable, moving the tracks and component bindings under the old root to the new one. Component bindings resolve by component name, so those ActorClass lacks no longer bind. Camera animation sequences only accept ACameraActor and its subclasses, and a sequence with more than one spawnable is rejected since its root is ambiguous. Available on UE 5.9; absent from UE 5.8. Params: templateSequence, actorClass", "epic_call_tool"),
  },
  epic_set_byte_track_enum: {
    epicSchema: S_epic_set_byte_track_enum,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_byte_track_enum", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Configure a byte track to use a specific enum type. Byte tracks can animate enum properties. Call this after adding the track and before setting property_name_and_path. Available on UE 5.8; absent from UE 5.9. Params: track, enum_class_path", "epic_call_tool"),
  },
  epic_set_byte_track_enum__movie_scene_property_track_extensions: {
    epicSchema: S_epic_set_byte_track_enum__movie_scene_property_track_extensions,
    epicTool: { toolset: "SequencerScripting.MovieScenePropertyTrackExtensions", name: "SequencerScripting.MovieScenePropertyTrackExtensions.SetByteTrackEnum", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieScenePropertyTrackExtensions] Set this byte track's enum Available on UE 5.9; absent from UE 5.8. Params: track, inEnum", "epic_call_tool"),
  },
  epic_set_camera_cut_auto_arrange_sections: {
    epicSchema: S_epic_set_camera_cut_auto_arrange_sections,
    epicTool: { toolset: "SequencerScripting.MovieSceneTrackExtensions", name: "SequencerScripting.MovieSceneTrackExtensions.SetCameraCutAutoArrangeSections", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneTrackExtensions] Set whether camera cut sections auto-arrange. Enabling it on a single-row track closes gaps between sections, as the editor does. Available on UE 5.9; absent from UE 5.8. Params: track, bAutoArrangeSections", "epic_call_tool"),
  },
  epic_set_camera_cut_binding: {
    epicSchema: S_epic_set_camera_cut_binding,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_camera_cut_binding", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set which camera a camera cut section uses. Available on UE 5.8; absent from UE 5.9. Params: section, camera_binding_id", "epic_call_tool"),
  },
  epic_set_camera_cut_binding__movie_scene_section_extensions: {
    epicSchema: S_epic_set_camera_cut_binding__movie_scene_section_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.SetCameraCutBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Assign a camera binding. Supply RootSequence for a binding in another sequence; instances must be unique in that hierarchy. Available on UE 5.9; absent from UE 5.8. Params: section, cameraBinding, rootSequence?", "epic_call_tool"),
  },
  epic_set_camera_cut_can_blend: {
    epicSchema: S_epic_set_camera_cut_can_blend,
    epicTool: { toolset: "SequencerScripting.MovieSceneTrackExtensions", name: "SequencerScripting.MovieSceneTrackExtensions.SetCameraCutCanBlend", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneTrackExtensions] Set whether overlapping camera cut sections blend. Disabling it removes section overlaps and easing, as the editor does. Available on UE 5.9; absent from UE 5.8. Params: track, bCanBlend", "epic_call_tool"),
  },
  epic_set_camera_lock: {
    epicSchema: S_epic_set_camera_lock,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_camera_lock", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Lock or unlock the camera cut to the viewport. Available on UE 5.8; absent from UE 5.9. Params: lock", "epic_call_tool"),
  },
  epic_set_clock_source: {
    epicSchema: S_epic_set_clock_source,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_clock_source", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the clock source for the sequence. Available on UE 5.8; absent from UE 5.9. Params: sequence, clock_source", "epic_call_tool"),
  },
  epic_set_clock_source__movie_scene_sequence_extensions: {
    epicSchema: S_epic_set_clock_source__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.SetClockSource", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Set the clock source for this sequence Available on UE 5.9; absent from UE 5.8. Params: inSequence, inClockSource", "epic_call_tool"),
  },
  epic_set_comment: {
    epicSchema: S_epic_set_comment,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.SetComment", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Set Comment Available on UE 5.9; absent from UE 5.8. Params: deformer, nodePath, comment", "epic_call_tool"),
  },
  epic_set_completion_mode: {
    epicSchema: S_epic_set_completion_mode,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.SetCompletionMode", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Set how the section's animated state is handled after evaluation. Available on UE 5.9; absent from UE 5.8. Params: section, inCompletionMode", "epic_call_tool"),
  },
  epic_set_component_binding_source: {
    epicSchema: S_epic_set_component_binding_source,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.SetComponentBindingSource", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Changes the kind of component a binding reads from. Available on UE 5.9; absent from UE 5.8. Params: deformer, bindingName, componentSource", "epic_call_tool"),
  },
  epic_set_constraint_active_key: {
    epicSchema: S_epic_set_constraint_active_key,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetConstraintActiveKey", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set the constraint active key in the current open Sequencer Available on UE 5.9; absent from UE 5.8. Params: inConstraint, bActive, inFrame, timeUnit?", "epic_call_tool"),
  },
  epic_set_control_rig_apply_mode: {
    epicSchema: S_epic_set_control_rig_apply_mode,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlRigApplyMode", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set the FK Control Rig to apply mode Available on UE 5.9; absent from UE 5.8. Params: inControlRig, inApplyMode", "epic_call_tool"),
  },
  epic_set_control_rig_available_spaces_order: {
    epicSchema: S_epic_set_control_rig_available_spaces_order,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlRigAvailableSpacesOrder", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Reorder a control's space picker list, as moving spaces up and down in the level editor does. Raises a script error if the Control Rig is null, the control does not exist, or Spaces is not the listed spaces in some order. Available on UE 5.9; absent from UE 5.8. Params: controlRig, controlName, spaces", "epic_call_tool"),
  },
  epic_set_control_rig_controls_visible: {
    epicSchema: S_epic_set_control_rig_controls_visible,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlRigControlsVisible", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Show or hide a Control Rig's control shapes in the viewport, as Toggle Controls Visibility does for the selected rigs and Toggle Module Controls Visibility for modules; hiding all shapes at once is SetAnimationModeSettings' bHideControlShapes. Raises a script error for the cases GetControlRigControlsVisible raises for. Available on UE 5.9; absent from UE 5.8. Params: controlRig, bVisible, moduleName?", "epic_call_tool"),
  },
  epic_set_control_rig_layered_mode: {
    epicSchema: S_epic_set_control_rig_layered_mode,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlRigLayeredMode", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Convert the control rig track into absolute or layered rig. This is destructive: non-FK rigs reset their control values and every section clears its animation and is recreated, and the track is renamed to the rig's class name. Converting to layered requires a backwards solve event unless the rig is an FK rig. Available on UE 5.9; absent from UE 5.8. Params: inTrack, bSetIsLayered", "epic_call_tool"),
  },
  epic_set_control_rig_override: {
    epicSchema: S_epic_set_control_rig_override,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlRigOverride", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Override a control setting on a Control Rig instance, as Anim Details' override view does: with an empty Value the setting's current value is captured (Add Override), otherwise the setting takes Value (Edit Override Value). The override goes in the rig's last linked override asset, creating one in the developer folder when it has none, and the rig is reconstructed so it applies. PropertyPath is a control setting path such as TopologyControlSettings->DisplayName, TopologyControlSettings->bShapeVisible, ShapeName, ShapeColor or ShapeTransform; Anim Details' names DisplayName, Shape->bVisible, Shape->Name, Shape->Color and Shape->Transform are accepted too. Raises a script error if overrides are disabled (ControlRig.Overrides), the Control Rig is null, the control does not exist, the path is not a setting of the control, Value cannot be read as that setting, the override asset to edit is read-only, or an override asset must be created and the rig has no asset or the developer folder is not visible or not writable in this editor (as in UEFN). Available on UE 5.9; absent from UE 5.8. Params: controlRig, controlName, propertyPath, value?", "epic_call_tool"),
  },
  epic_set_control_rig_priority_order: {
    epicSchema: S_epic_set_control_rig_priority_order,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlRigPriorityOrder", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set the Control Rig track's priority order; tracks with a higher priority evaluate first. Raises a script error if PriorityOrder is negative. Available on UE 5.9; absent from UE 5.8. Params: inTrack, priorityOrder", "epic_call_tool"),
  },
  epic_set_control_rig_rotation_order_override: {
    epicSchema: S_epic_set_control_rig_rotation_order_override,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlRigRotationOrderOverride", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Override a control's rotation order, as the Anim Details rotation order dropdown does: its keys are re-baked into the new order so the rotations stay the same, and the rig evaluates in that order. Layered (additive) Control Rigs do not support overrides. Raises a script error for the cases GetControlRigRotationOrder raises for, or when the Control Rig is layered. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, rotationOrder", "epic_call_tool"),
  },
  epic_set_control_rig_section_transform_mask: {
    epicSchema: S_epic_set_control_rig_section_transform_mask,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlRigSectionTransformMask", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set which transform and weight channels a Control Rig section animates (its Active Channels). Disabled channels stop animating and are not keyed; their keys are kept. Available on UE 5.9; absent from UE 5.8. Params: inSection, mask", "epic_call_tool"),
  },
  epic_set_control_rig_space: {
    epicSchema: S_epic_set_control_rig_space,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlRigSpace", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Key the Control Rig Space for the Control at the specified time, compensating so the control keeps its world transform. If the control has no space channel and the space is its current parent, nothing is keyed. Available on UE 5.9; absent from UE 5.8. Params: inSequence, inControlRig, inControlName, inSpaceKey, inTime, timeUnit?", "epic_call_tool"),
  },
  epic_set_control_rig_world_transform: {
    epicSchema: S_epic_set_control_rig_world_transform,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlRigWorldTransform", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set ControlRig Control's World Transform at a specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, worldTransform, timeUnit?, bSetKey?", "epic_call_tool"),
  },
  epic_set_control_rig_world_transforms: {
    epicSchema: S_epic_set_control_rig_world_transforms,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlRigWorldTransforms", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set ControlRig Control's World Transforms at a specific times. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, worldTransforms, timeUnit?", "epic_call_tool"),
  },
  epic_set_controls_mask: {
    epicSchema: S_epic_set_controls_mask,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_controls_mask", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set the visibility mask for the specified controls on a section. Available on UE 5.8; absent from UE 5.9. Params: section, control_names, visible", "epic_call_tool"),
  },
  epic_set_controls_mask__control_rig_sequencer_editor_library: {
    epicSchema: S_epic_set_controls_mask__control_rig_sequencer_editor_library,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetControlsMask", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set whether the section animates the given controls (bVisible true) or masks them out (false). Available on UE 5.9; absent from UE 5.8. Params: inSection, controlNames, bVisible", "epic_call_tool"),
  },
  epic_set_curve_editor_view_mode: {
    epicSchema: S_epic_set_curve_editor_view_mode,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetCurveEditorViewMode", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Set how the active Sequencer's Curve Editor lays out its curves. Available on UE 5.9; absent from UE 5.8. Params: viewMode", "epic_call_tool"),
  },
  epic_set_custom_clock_source: {
    epicSchema: S_epic_set_custom_clock_source,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.SetCustomClockSource", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Drive the sequence's playback from ClockSource, as the Custom clock source picker does: an asset or actor that implements MovieSceneCustomClockSource, or an actor with a component that does. Sets the clock source to Custom. Raises if the source is hidden in this editor or may not be referenced from this sequence. Available on UE 5.9; absent from UE 5.8. Params: sequence, clockSource", "epic_call_tool"),
  },
  epic_set_custom_time_warp: {
    epicSchema: S_epic_set_custom_time_warp,
    epicTool: { toolset: "SequencerScripting.MovieSceneTimeWarpExtensions", name: "SequencerScripting.MovieSceneTimeWarpExtensions.SetCustomTimeWarp", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneTimeWarpExtensions] Replace a section's time warp with a new custom time warp of TimeWarpClass, initialized the way the editor does. Its curve then appears among the section's channels (\"PlayRate\" for a play rate curve, \"TimeWarp\" for a time warp curve). PlayRate values are play rate multipliers. TimeWarp values are warped times in tick resolution frames, keyed against unwarped time measured from the section start, so the initial keys map 0 to 0 and the section length to itself. Available on UE 5.9; absent from UE 5.8. Params: section, timeWarpClass", "epic_call_tool"),
  },
  epic_set_data_interface_setting: {
    epicSchema: S_epic_set_data_interface_setting,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.SetDataInterfaceSetting", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Sets a setting of a data interface node, as listed in FDeformerGraphNodeInfo::Settings. The node's pins are recreated if the setting defines them, which removes the links of pins that no longer exist. Available on UE 5.9; absent from UE 5.8. Params: deformer, nodePath, settingName, value", "epic_call_tool"),
  },
  epic_set_default_value: {
    epicSchema: S_epic_set_default_value,
    epicTool: { toolset: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools", name: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.set_default_value", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Set the default value of a channel. Available on UE 5.8; absent from UE 5.9. Params: section, channel_name, value", "epic_call_tool"),
  },
  epic_set_display_name: {
    epicSchema: S_epic_set_display_name,
    epicTool: { toolset: "SequencerScripting.MovieSceneBindingExtensions", name: "SequencerScripting.MovieSceneBindingExtensions.SetDisplayName", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneBindingExtensions] Set this binding's name Available on UE 5.9; absent from UE 5.8. Params: inBinding, inDisplayName", "epic_call_tool"),
  },
  epic_set_display_name__movie_scene_track_extensions: {
    epicSchema: S_epic_set_display_name__movie_scene_track_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneTrackExtensions", name: "SequencerScripting.MovieSceneTrackExtensions.SetDisplayName", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneTrackExtensions] Set this track's display name Available on UE 5.9; absent from UE 5.8. Params: track, inName", "epic_call_tool"),
  },
  epic_set_display_rate: {
    epicSchema: S_epic_set_display_rate,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_display_rate", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the display frame rate of a sequence. Available on UE 5.8; absent from UE 5.9. Params: sequence, numerator, denominator?", "epic_call_tool"),
  },
  epic_set_display_rate__movie_scene_sequence_extensions: {
    epicSchema: S_epic_set_display_rate__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.SetDisplayRate", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Sets this sequence's display rate Available on UE 5.9; absent from UE 5.8. Params: sequence, displayRate", "epic_call_tool"),
  },
  epic_set_double_default: {
    epicSchema: S_epic_set_double_default,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.SetDoubleDefault", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Set the double channel default value. Available on UE 5.9; absent from UE 5.8. Params: channel, inDefaultValue", "epic_call_tool"),
  },
  epic_set_double_key_value: {
    epicSchema: S_epic_set_double_key_value,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.SetDoubleKeyValue", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Set a live double key's value at double precision. Available on UE 5.9; absent from UE 5.8. Params: channel, key, newValue", "epic_call_tool"),
  },
  epic_set_ease_in_duration: {
    epicSchema: S_epic_set_ease_in_duration,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionEasingExtensions", name: "SequencerScripting.MovieSceneSectionEasingExtensions.SetEaseInDuration", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionEasingExtensions] Set manual ease-in duration in tick-resolution frames (not display-rate frames), enabling the manual override Available on UE 5.9; absent from UE 5.8. Params: section, inDuration", "epic_call_tool"),
  },
  epic_set_ease_out_duration: {
    epicSchema: S_epic_set_ease_out_duration,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionEasingExtensions", name: "SequencerScripting.MovieSceneSectionEasingExtensions.SetEaseOutDuration", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionEasingExtensions] Set manual ease-out duration in tick-resolution frames (not display-rate frames), enabling the manual override Available on UE 5.9; absent from UE 5.8. Params: section, inDuration", "epic_call_tool"),
  },
  epic_set_edit_pivot_tool_active: {
    epicSchema: S_epic_set_edit_pivot_tool_active,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.SetEditPivotToolActive", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Turn the Edit Pivot tool on or off in the level editor, as the Animation Mode \"Temporary Pivot\" button does. It works on the selected actors and Control Rig controls, so select them first; it turns itself off when the selection changes. It shares the level editor's tool slot with motion trails: turning it on stops motion trails, and SetMotionTrailsVisible(true) turns it off. Raises when no level sequence is open in Sequencer, or the tool does not start. Available on UE 5.9; absent from UE 5.8. Params: bActive", "epic_call_tool"),
  },
  epic_set_emulated_network_mask: {
    epicSchema: S_epic_set_emulated_network_mask,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetEmulatedNetworkMask", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Set the network mode the active Sequencer's root sequence emulates during playback and recompile to match. Raises when no Sequencer is open or the mask is None. Available on UE 5.9; absent from UE 5.8. Params: networkMask", "epic_call_tool"),
  },
  epic_set_emulated_network_mask_async: {
    epicSchema: S_epic_set_emulated_network_mask_async,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetEmulatedNetworkMaskAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Set the network mode the active Sequencer's root sequence emulates during playback and wait for evaluation. Available on UE 5.9; absent from UE 5.8. Params: networkMask", "epic_call_tool"),
  },
  epic_set_end_frame_bounded: {
    epicSchema: S_epic_set_end_frame_bounded,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.SetEndFrameBounded", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Set end frame bounded Available on UE 5.9; absent from UE 5.8. Params: section, bIsBounded", "epic_call_tool"),
  },
  epic_set_enum_track_enum: {
    epicSchema: S_epic_set_enum_track_enum,
    epicTool: { toolset: "SequencerScripting.MovieScenePropertyTrackExtensions", name: "SequencerScripting.MovieScenePropertyTrackExtensions.SetEnumTrackEnum", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieScenePropertyTrackExtensions] Set the enum whose values an enum track animates, including on its existing sections. Keys are entry values, set through the sections' byte channels. A byte track with SetByteTrackEnum animates an enum property the same way. Raises if the enum is hidden in this editor or may not be referenced from this sequence. Available on UE 5.9; absent from UE 5.8. Params: track, inEnum", "epic_call_tool"),
  },
  epic_set_euler_transform: {
    epicSchema: S_epic_set_euler_transform,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_euler_transform", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set an EulerTransform control value at a specific frame. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, frame, location_x?, location_y?, location_z?, rotation_pitch?, rotation_yaw?, rotation_roll?, scale_x?, scale_y?, scale_z?, set_key?", "epic_call_tool"),
  },
  epic_set_evaluation_type: {
    epicSchema: S_epic_set_evaluation_type,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_evaluation_type", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the evaluation type of a sequence. Available on UE 5.8; absent from UE 5.9. Params: sequence, eval_type", "epic_call_tool"),
  },
  epic_set_evaluation_type__movie_scene_sequence_extensions: {
    epicSchema: S_epic_set_evaluation_type__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.SetEvaluationType", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Set the evaluation type for this sequence Available on UE 5.9; absent from UE 5.8. Params: inSequence, inEvaluationType", "epic_call_tool"),
  },
  epic_set_film_overlay: {
    epicSchema: S_epic_set_film_overlay,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetFilmOverlay", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Show one of GetFilmOverlayNames in a Cinematic Viewport, or None for no composition overlay. Raises when the viewport is not open or the name is unknown. Available on UE 5.9; absent from UE 5.8. Params: viewport, overlayName", "epic_call_tool"),
  },
  epic_set_film_overlay_frame_enabled: {
    epicSchema: S_epic_set_film_overlay_frame_enabled,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetFilmOverlayFrameEnabled", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Enable or disable a frame overlay, which every Cinematic Viewport shows while it is enabled. Raises when the name is unknown. Available on UE 5.9; absent from UE 5.8. Params: frameName, bEnabled", "epic_call_tool"),
  },
  epic_set_film_overlay_frame_tint: {
    epicSchema: S_epic_set_film_overlay_frame_tint,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetFilmOverlayFrameTint", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Set the color a frame overlay draws with. Raises when the name is unknown. Available on UE 5.9; absent from UE 5.8. Params: frameName, tint", "epic_call_tool"),
  },
  epic_set_film_overlay_tint: {
    epicSchema: S_epic_set_film_overlay_tint,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetFilmOverlayTint", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Set the tint a Cinematic Viewport applies to the composition overlay it shows, like the Film Overlay menu's tint control. Raises when the viewport is not open. Available on UE 5.9; absent from UE 5.8. Params: viewport, tint", "epic_call_tool"),
  },
  epic_set_fkcontrol_rig_controls_active: {
    epicSchema: S_epic_set_fkcontrol_rig_controls_active,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetFKControlRigControlsActive", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set whether an FK Control Rig animates controls, as its Select Bones To Animate dialog does: inactive controls stop driving their bones and are masked out of the rig's sections in the focused level sequence. Raises a script error if the sequence is not the one focused in Sequencer, the Control Rig is not an FK Control Rig, or a control does not exist. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlNames, bActive", "epic_call_tool"),
  },
  epic_set_float: {
    epicSchema: S_epic_set_float,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_float", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set a float control value at a specific frame. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, frame, value, set_key?", "epic_call_tool"),
  },
  epic_set_float_default: {
    epicSchema: S_epic_set_float_default,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.SetFloatDefault", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Set the float channel default value. Available on UE 5.9; absent from UE 5.8. Params: channel, inDefaultValue", "epic_call_tool"),
  },
  epic_set_float_key_value: {
    epicSchema: S_epic_set_float_key_value,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.SetFloatKeyValue", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Set a live float key's value. Available on UE 5.9; absent from UE 5.8. Params: channel, key, newValue", "epic_call_tool"),
  },
  epic_set_folder_color: {
    epicSchema: S_epic_set_folder_color,
    epicTool: { toolset: "SequencerScripting.MovieSceneFolderExtensions", name: "SequencerScripting.MovieSceneFolderExtensions.SetFolderColor", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneFolderExtensions] Set the display color of the given folder Available on UE 5.9; absent from UE 5.8. Params: folder, inFolderColor", "epic_call_tool"),
  },
  epic_set_folder_name: {
    epicSchema: S_epic_set_folder_name,
    epicTool: { toolset: "SequencerScripting.MovieSceneFolderExtensions", name: "SequencerScripting.MovieSceneFolderExtensions.SetFolderName", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneFolderExtensions] Set the name of the given folder Available on UE 5.9; absent from UE 5.8. Params: folder, inFolderName", "epic_call_tool"),
  },
  epic_set_function_settings: {
    epicSchema: S_epic_set_function_settings,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.SetFunctionSettings", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Set Function Settings Available on UE 5.9; absent from UE 5.8. Params: deformer, graphPath, category, bIsPublic", "epic_call_tool"),
  },
  epic_set_global_transform: {
    epicSchema: S_epic_set_global_transform,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.set_global_transform", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Set global transform of a hierarchy element. Params: control_rig, item, transform, initial?", "epic_call_tool"),
  },
  epic_set_int: {
    epicSchema: S_epic_set_int,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_int", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set an integer control value at a specific frame. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, frame, value, set_key?", "epic_call_tool"),
  },
  epic_set_kernel_additional_sources: {
    epicSchema: S_epic_set_kernel_additional_sources,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.SetKernelAdditionalSources", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Sets the shader source assets the kernel includes before its own source, e.g. to share HLSL functions between kernels. Available on UE 5.9; absent from UE 5.8. Params: deformer, nodePath, sourcePaths", "epic_call_tool"),
  },
  epic_set_kernel_binding_options: {
    epicSchema: S_epic_set_kernel_binding_options,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.SetKernelBindingOptions", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Sets the options of an output binding. Available on UE 5.9; absent from UE 5.8. Params: deformer, nodePath, bindingName, bSupportAtomic, bSupportRead", "epic_call_tool"),
  },
  epic_set_kernel_execution_domain: {
    epicSchema: S_epic_set_kernel_execution_domain,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.SetKernelExecutionDomain", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] SetKernelExecutionDomain Available on UE 5.9; absent from UE 5.8. Params: deformer, nodePath, executionDomain", "epic_call_tool"),
  },
  epic_set_kernel_group_size: {
    epicSchema: S_epic_set_kernel_group_size,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.SetKernelGroupSize", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] SetKernelGroupSize Available on UE 5.9; absent from UE 5.8. Params: deformer, nodePath, groupSize", "epic_call_tool"),
  },
  epic_set_kernel_name: {
    epicSchema: S_epic_set_kernel_name,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.SetKernelName", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] SetKernelName Available on UE 5.9; absent from UE 5.8. Params: deformer, nodePath, kernelName", "epic_call_tool"),
  },
  epic_set_kernel_source: {
    epicSchema: S_epic_set_kernel_source,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.SetKernelSource", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Sets the HLSL source. Errors in it are only reported by Compile. Available on UE 5.9; absent from UE 5.8. Params: deformer, nodePath, source", "epic_call_tool"),
  },
  epic_set_key_time: {
    epicSchema: S_epic_set_key_time,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.SetKeyTime", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Move a live key to NewFrameNumber. SubFrame is clamped to [0, 1) and ignored for tick-resolution frames. Available on UE 5.9; absent from UE 5.8. Params: channel, key, newFrameNumber, subFrame?, timeUnit?", "epic_call_tool"),
  },
  epic_set_layered_mode: {
    epicSchema: S_epic_set_layered_mode,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_layered_mode", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set a Control Rig track to layered or absolute mode. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, is_layered", "epic_call_tool"),
  },
  epic_set_letterbox_aspect_ratio: {
    epicSchema: S_epic_set_letterbox_aspect_ratio,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetLetterboxAspectRatio", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Set the width and height terms of the ratio the LetterBox frame overlay masks the viewport to. Raises when either term is outside the range the Film Overlay menu accepts. Available on UE 5.9; absent from UE 5.8. Params: width, height", "epic_call_tool"),
  },
  epic_set_linked_anim_sequence_settings: {
    epicSchema: S_epic_set_linked_anim_sequence_settings,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLinkedAnimSequenceSettings", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set a linked Anim Sequence's export settings, as Edit in Sequencer's Modify Level Sequence Link Settings does; the link's binding and Anim Sequence path are kept. Turning bAutoBake on adds the linked animation track that bakes when the level sequence is saved, as Add Auto Bake does; turning it off removes it. Raises a script error if the binding has no level sequence, the Anim Sequence is null or not linked to the binding, bAutoBake changes while the level sequence is not open in Sequencer, or bAutoBake turns on and the editor's asset reference filter refuses a reference from the level sequence to the Anim Sequence (as in UEFN). Available on UE 5.9; absent from UE 5.8. Params: binding, animSequence, settings", "epic_call_tool"),
  },
  epic_set_local_control_rig_bool: {
    epicSchema: S_epic_set_local_control_rig_bool,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigBool", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set ControlRig Control's bool value at specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, value, timeUnit?, bSetKey?", "epic_call_tool"),
  },
  epic_set_local_control_rig_bools: {
    epicSchema: S_epic_set_local_control_rig_bools,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigBools", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set ControlRig Control's bool values at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, values, timeUnit?", "epic_call_tool"),
  },
  epic_set_local_control_rig_euler_transform: {
    epicSchema: S_epic_set_local_control_rig_euler_transform,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigEulerTransform", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set ControlRig Control's EulerTransform value at specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, value, timeUnit?, bSetKey?", "epic_call_tool"),
  },
  epic_set_local_control_rig_euler_transforms: {
    epicSchema: S_epic_set_local_control_rig_euler_transforms,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigEulerTransforms", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set ControlRig Control's EulerTransform values at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, values, timeUnit?", "epic_call_tool"),
  },
  epic_set_local_control_rig_float: {
    epicSchema: S_epic_set_local_control_rig_float,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigFloat", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set ControlRig Control's float value at specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, value, timeUnit?, bSetKey?", "epic_call_tool"),
  },
  epic_set_local_control_rig_floats: {
    epicSchema: S_epic_set_local_control_rig_floats,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigFloats", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set ControlRig Control's float values at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, values, timeUnit?", "epic_call_tool"),
  },
  epic_set_local_control_rig_int: {
    epicSchema: S_epic_set_local_control_rig_int,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigInt", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set ControlRig Control's int value at specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, value, timeUnit?, bSetKey?", "epic_call_tool"),
  },
  epic_set_local_control_rig_ints: {
    epicSchema: S_epic_set_local_control_rig_ints,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigInts", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set ControlRig Control's int values at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, values, timeUnit?", "epic_call_tool"),
  },
  epic_set_local_control_rig_position: {
    epicSchema: S_epic_set_local_control_rig_position,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigPosition", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set ControlRig Control's Position value at specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, value, timeUnit?, bSetKey?", "epic_call_tool"),
  },
  epic_set_local_control_rig_positions: {
    epicSchema: S_epic_set_local_control_rig_positions,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigPositions", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set ControlRig Control's Position values at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, values, timeUnit?", "epic_call_tool"),
  },
  epic_set_local_control_rig_rotator: {
    epicSchema: S_epic_set_local_control_rig_rotator,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigRotator", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set ControlRig Control's Rotator value at specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, value, timeUnit?, bSetKey?", "epic_call_tool"),
  },
  epic_set_local_control_rig_rotators: {
    epicSchema: S_epic_set_local_control_rig_rotators,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigRotators", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set ControlRig Control's Rotator values at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, values, timeUnit?", "epic_call_tool"),
  },
  epic_set_local_control_rig_scale: {
    epicSchema: S_epic_set_local_control_rig_scale,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigScale", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set ControlRig Control's Scale value at specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, value, timeUnit?, bSetKey?", "epic_call_tool"),
  },
  epic_set_local_control_rig_scales: {
    epicSchema: S_epic_set_local_control_rig_scales,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigScales", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set ControlRig Control's Scale values at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, values, timeUnit?", "epic_call_tool"),
  },
  epic_set_local_control_rig_transform: {
    epicSchema: S_epic_set_local_control_rig_transform,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigTransform", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set ControlRig Control's Transform value at specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, value, timeUnit?, bSetKey?", "epic_call_tool"),
  },
  epic_set_local_control_rig_transform_no_scale: {
    epicSchema: S_epic_set_local_control_rig_transform_no_scale,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigTransformNoScale", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set ControlRig Control's TransformNoScale value at specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, value, timeUnit?, bSetKey?", "epic_call_tool"),
  },
  epic_set_local_control_rig_transform_no_scales: {
    epicSchema: S_epic_set_local_control_rig_transform_no_scales,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigTransformNoScales", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set ControlRig Control's TransformNoScale values at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, values, timeUnit?", "epic_call_tool"),
  },
  epic_set_local_control_rig_transforms: {
    epicSchema: S_epic_set_local_control_rig_transforms,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigTransforms", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set ControlRig Control's Transform values at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, values, timeUnit?", "epic_call_tool"),
  },
  epic_set_local_control_rig_vector2_d: {
    epicSchema: S_epic_set_local_control_rig_vector2_d,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigVector2D", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set ControlRig Control's Vector2D value at specific time Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, value, timeUnit?, bSetKey?", "epic_call_tool"),
  },
  epic_set_local_control_rig_vector2_ds: {
    epicSchema: S_epic_set_local_control_rig_vector2_ds,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetLocalControlRigVector2Ds", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set ControlRig Control's Vector2D values at specific times Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frames, values, timeUnit?", "epic_call_tool"),
  },
  epic_set_local_transform: {
    epicSchema: S_epic_set_local_transform,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.set_local_transform", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Set local transform of a hierarchy element. Params: control_rig, item, transform, initial?", "epic_call_tool"),
  },
  epic_set_lock_camera_cut_to_viewport_async: {
    epicSchema: S_epic_set_lock_camera_cut_to_viewport_async,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetLockCameraCutToViewportAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Set camera-cut viewport locking and wait for evaluation. Available on UE 5.9; absent from UE 5.8. Params: bLock", "epic_call_tool"),
  },
  epic_set_lock_level_sequence: {
    epicSchema: S_epic_set_lock_level_sequence,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SetLockLevelSequence", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Sets the lock for the current level sequence and its descendants for editing. Available on UE 5.9; absent from UE 5.8. Params: bLock", "epic_call_tool"),
  },
  epic_set_loop_count: {
    epicSchema: S_epic_set_loop_count,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.SetLoopCount", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] SetLoopCount Available on UE 5.9; absent from UE 5.8. Params: deformer, nodePath, count", "epic_call_tool"),
  },
  epic_set_loop_mode: {
    epicSchema: S_epic_set_loop_mode,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_loop_mode", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Enable or disable loop playback. Available on UE 5.8; absent from UE 5.9. Params: loop", "epic_call_tool"),
  },
  epic_set_loop_mode__level_sequence_editor_blueprint_library: {
    epicSchema: S_epic_set_loop_mode__level_sequence_editor_blueprint_library,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SetLoopMode", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Set loop mode (note this is a per user preference) Available on UE 5.9; absent from UE 5.8. Params: newLoopMode", "epic_call_tool"),
  },
  epic_set_marked_frames_shown_globally: {
    epicSchema: S_epic_set_marked_frames_shown_globally,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.SetMarkedFramesShownGlobally", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Show or hide the sequence's marks and mark ranges in Sequencer while another sequence in the hierarchy is focused. An open Sequencer draws the change after LevelSequenceEditorBlueprintLibrary.RefreshCurrentLevelSequence, like other scripted edits. Available on UE 5.9; absent from UE 5.8. Params: sequence, bShow", "epic_call_tool"),
  },
  epic_set_material: {
    epicSchema: S_epic_set_material,
    epicTool: { toolset: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", name: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.set_material", engines: ["5.8"], successors: [{ toolset: "EditorToolset.SkeletalMeshToolset", name: "EditorToolset.SkeletalMeshToolset.SetMaterial", engines: ["5.9"], schema: S_epic_set_material__1, rename: {"slot_name":"slotName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Assigns a material to a named slot on a skeletal mesh asset. This affects all instances of the mesh that do not override the slot material. Params: mesh, slot_name, material. On UE 5.9 it calls EditorToolset.SkeletalMeshToolset.SetMaterial (Params: mesh, slotName, material).", "epic_call_tool"),
  },
  epic_set_motion_trail_offset: {
    epicSchema: S_epic_set_motion_trail_offset,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.SetMotionTrailOffset", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Draw a pinned trail moved by an offset, in the space of the object at each frame, without changing any keys. Raises when the trail is not pinned or trails are hidden. Available on UE 5.9; absent from UE 5.8. Params: trailId, offset", "epic_call_tool"),
  },
  epic_set_motion_trail_settings: {
    epicSchema: S_epic_set_motion_trail_settings,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.SetMotionTrailSettings", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Set the motion trail display settings and save them to the editor preferences (KeyColor lasts the session only). The struct holds every field, so read it with GetMotionTrailSettings, change what you need and pass it back. Raises for a negative frame count or size, MaxNumberPinned outside 5 to 100, or the TimePosing style while Time Posing is off; nothing is changed then. Available on UE 5.9; absent from UE 5.8. Params: inSettings", "epic_call_tool"),
  },
  epic_set_motion_trail_space: {
    epicSchema: S_epic_set_motion_trail_space,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.SetMotionTrailSpace", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Draw a pinned trail relative to another actor, or to one of its components, sockets or bones, instead of world space. Raises when the trail is not pinned or SpaceActor is null. Available on UE 5.9; absent from UE 5.8. Params: trailId, spaceActor, componentOrSocketName", "epic_call_tool"),
  },
  epic_set_motion_trails_visible: {
    epicSchema: S_epic_set_motion_trails_visible,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.SetMotionTrailsVisible", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Show or hide actor and Control Rig motion trails, as the Animation Mode \"Motion Paths\" button does. Showing trails with nothing pinned pins the current selection. Raises when no level sequence is open in Sequencer. Available on UE 5.9; absent from UE 5.8. Params: bVisible", "epic_call_tool"),
  },
  epic_set_node_deactivated: {
    epicSchema: S_epic_set_node_deactivated,
    epicTool: { toolset: "animation_toolset.toolsets.outliner.SequencerOutlinerTools", name: "animation_toolset.toolsets.outliner.SequencerOutlinerTools.set_node_deactivated", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeDeactivated", engines: ["5.9"], schema: S_epic_set_node_deactivated__1, rename: {"deactivated":"bDeactivated"} }] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Deactivate or reactivate outliner nodes. Params: nodes, deactivated. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeDeactivated (Params: nodes, bDeactivated).", "epic_call_tool"),
  },
  epic_set_node_expanded: {
    epicSchema: S_epic_set_node_expanded,
    epicTool: { toolset: "animation_toolset.toolsets.outliner.SequencerOutlinerTools", name: "animation_toolset.toolsets.outliner.SequencerOutlinerTools.set_node_expanded", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeExpanded", engines: ["5.9"], schema: S_epic_set_node_expanded__1, rename: {"expanded":"bExpanded"} }] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Expand or collapse outliner nodes. Params: nodes, expanded. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeExpanded (Params: nodes, bExpanded).", "epic_call_tool"),
  },
  epic_set_node_group_filter_enabled: {
    epicSchema: S_epic_set_node_group_filter_enabled,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeGroupFilterEnabled", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Filter the outliner to the members of a Sequencer Group, as the Group Manager's filter toggle does, or stop filtering on it. The filter is editor state and is not saved with the sequence. Available on UE 5.9; absent from UE 5.8. Params: group, bEnabled", "epic_call_tool"),
  },
  epic_set_node_group_name: {
    epicSchema: S_epic_set_node_group_name,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeGroupName", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Rename a Sequencer Group of the focused sequence. Name is stored without surrounding whitespace and must not be blank. Renaming to the current name changes nothing. Available on UE 5.9; absent from UE 5.8. Params: group, name", "epic_call_tool"),
  },
  epic_set_node_hidden: {
    epicSchema: S_epic_set_node_hidden,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeHidden", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Hide nodes from the outliner, or show nodes hidden this way again. Hiding a node hides its descendants with it. Available on UE 5.9; absent from UE 5.8. Params: nodes, bHidden", "epic_call_tool"),
  },
  epic_set_node_isolated: {
    epicSchema: S_epic_set_node_isolated,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeIsolated", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Isolate nodes so the outliner shows only isolated nodes with their ancestors and descendants, or stop isolating them Available on UE 5.9; absent from UE 5.8. Params: nodes, bIsolated", "epic_call_tool"),
  },
  epic_set_node_locked: {
    epicSchema: S_epic_set_node_locked,
    epicTool: { toolset: "animation_toolset.toolsets.outliner.SequencerOutlinerTools", name: "animation_toolset.toolsets.outliner.SequencerOutlinerTools.set_node_locked", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeLocked", engines: ["5.9"], schema: S_epic_set_node_locked__1, rename: {"locked":"bLocked"} }] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Lock or unlock outliner nodes for editing. Params: nodes, locked. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeLocked (Params: nodes, bLocked).", "epic_call_tool"),
  },
  epic_set_node_muted: {
    epicSchema: S_epic_set_node_muted,
    epicTool: { toolset: "animation_toolset.toolsets.outliner.SequencerOutlinerTools", name: "animation_toolset.toolsets.outliner.SequencerOutlinerTools.set_node_muted", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeMuted", engines: ["5.9"], schema: S_epic_set_node_muted__1, rename: {"muted":"bMuted"} }] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Mute or unmute outliner nodes. Params: nodes, muted. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeMuted (Params: nodes, bMuted).", "epic_call_tool"),
  },
  epic_set_node_pinned: {
    epicSchema: S_epic_set_node_pinned,
    epicTool: { toolset: "animation_toolset.toolsets.outliner.SequencerOutlinerTools", name: "animation_toolset.toolsets.outliner.SequencerOutlinerTools.set_node_pinned", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodePinned", engines: ["5.9"], schema: S_epic_set_node_pinned__1, rename: {"pinned":"bPinned"} }] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Pin or unpin outliner nodes. Params: nodes, pinned. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodePinned (Params: nodes, bPinned).", "epic_call_tool"),
  },
  epic_set_node_position: {
    epicSchema: S_epic_set_node_position,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.set_node_position", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Set the position of a node in the graph editor. Params: control_rig, graph, node, position", "epic_call_tool"),
  },
  epic_set_node_position__deformer_graph_scripting_subsystem: {
    epicSchema: S_epic_set_node_position__deformer_graph_scripting_subsystem,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.SetNodePosition", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] SetNodePosition Available on UE 5.9; absent from UE 5.8. Params: deformer, nodePath, position", "epic_call_tool"),
  },
  epic_set_node_solo: {
    epicSchema: S_epic_set_node_solo,
    epicTool: { toolset: "animation_toolset.toolsets.outliner.SequencerOutlinerTools", name: "animation_toolset.toolsets.outliner.SequencerOutlinerTools.set_node_solo", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeSolo", engines: ["5.9"], schema: S_epic_set_node_solo__1, rename: {"soloed":"bSoloed"} }] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Solo or unsolo outliner nodes. Params: nodes, soloed. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodeSolo (Params: nodes, bSoloed).", "epic_call_tool"),
  },
  epic_set_nodes_expanded_by_path: {
    epicSchema: S_epic_set_nodes_expanded_by_path,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetNodesExpandedByPath", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Expand or collapse paths atomically. Missing or ambiguous paths raise an error. Available on UE 5.9; absent from UE 5.8. Params: paths, bExpanded", "epic_call_tool"),
  },
  epic_set_num_channels_used: {
    epicSchema: S_epic_set_num_channels_used,
    epicTool: { toolset: "SequencerScripting.MovieSceneDoubleVectorTrackExtensions", name: "SequencerScripting.MovieSceneDoubleVectorTrackExtensions.SetNumChannelsUsed", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneDoubleVectorTrackExtensions] Set the number of channels used for this track Available on UE 5.9; absent from UE 5.8. Params: track, inNumChannelsUsed", "epic_call_tool"),
  },
  epic_set_num_channels_used__movie_scene_float_vector_track_extensions: {
    epicSchema: S_epic_set_num_channels_used__movie_scene_float_vector_track_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneFloatVectorTrackExtensions", name: "SequencerScripting.MovieSceneFloatVectorTrackExtensions.SetNumChannelsUsed", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneFloatVectorTrackExtensions] Set the number of channels used for this track Available on UE 5.9; absent from UE 5.8. Params: track, inNumChannelsUsed", "epic_call_tool"),
  },
  epic_set_origin_override_mask: {
    epicSchema: S_epic_set_origin_override_mask,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.SetOriginOverrideMask", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Set the transform channels a shot or subsequence section's origin override animates. Only translation and rotation are supported. Available on UE 5.9; absent from UE 5.8. Params: section, channels", "epic_call_tool"),
  },
  epic_set_outliner_selection: {
    epicSchema: S_epic_set_outliner_selection,
    epicTool: { toolset: "animation_toolset.toolsets.outliner.SequencerOutlinerTools", name: "animation_toolset.toolsets.outliner.SequencerOutlinerTools.set_outliner_selection", engines: ["5.8"], successors: [{ toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetOutlinerSelection", engines: ["5.9"], schema: S_epic_set_outliner_selection__1 }] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.outliner.SequencerOutlinerTools] Set the outliner selection. Params: nodes. On UE 5.9 it calls LevelSequenceEditor.LevelSequenceEditorSubsystem.SetOutlinerSelection (Params: nodes).", "epic_call_tool"),
  },
  epic_set_perlin_noise_params: {
    epicSchema: S_epic_set_perlin_noise_params,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.SetPerlinNoiseParams", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Set the noise parameters of a channel overridden with Perlin noise. All values must be finite and Frequency non-negative. Available on UE 5.9; absent from UE 5.8. Params: section, channelName, params", "epic_call_tool"),
  },
  epic_set_pin_value: {
    epicSchema: S_epic_set_pin_value,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig.ControlRigTools", name: "animation_toolset.toolsets.controlrig.ControlRigTools.set_pin_value", engines: ["5.8","5.9"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig.ControlRigTools] Set the default value of a pin. Params: control_rig, graph, pin, value", "epic_call_tool"),
  },
  epic_set_pin_value__deformer_graph_scripting_subsystem: {
    epicSchema: S_epic_set_pin_value__deformer_graph_scripting_subsystem,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.SetPinValue", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Sets the value of a pin whose FDeformerGraphPinInfo::bHasValue is true. Available on UE 5.9; absent from UE 5.8. Params: deformer, pinPath, value", "epic_call_tool"),
  },
  epic_set_playback_context: {
    epicSchema: S_epic_set_playback_context,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetPlaybackContext", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Evaluate the open Level Sequence editor in World, previewing through Client, which must match one of GetPlaybackContextOptions. The choice lasts until PIE starts or ends, the map changes, or a world is added or removed; then the editor chooses automatically again. It applies from the next evaluation (see WaitForEvaluationAsync). Available on UE 5.9; absent from UE 5.8. Params: world, client?", "epic_call_tool"),
  },
  epic_set_playback_end: {
    epicSchema: S_epic_set_playback_end,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.SetPlaybackEnd", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Set playback end of this sequence Available on UE 5.9; absent from UE 5.8. Params: sequence, endFrame", "epic_call_tool"),
  },
  epic_set_playback_range: {
    epicSchema: S_epic_set_playback_range,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_playback_range", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the playback start and end frames of a sequence. Available on UE 5.8; absent from UE 5.9. Params: sequence, start_frame, end_frame", "epic_call_tool"),
  },
  epic_set_playback_range__movie_scene_sequence_extensions: {
    epicSchema: S_epic_set_playback_range__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.SetPlaybackRange", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Set both playback bounds atomically in display-rate frames. The start is inclusive and the end is exclusive; an equal pair is an empty range. Available on UE 5.9; absent from UE 5.8. Params: sequence, startFrame, endFrame", "epic_call_tool"),
  },
  epic_set_playback_range_locked: {
    epicSchema: S_epic_set_playback_range_locked,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_playback_range_locked", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Lock or unlock the playback range. Available on UE 5.8; absent from UE 5.9. Params: sequence, locked", "epic_call_tool"),
  },
  epic_set_playback_range_locked__movie_scene_sequence_extensions: {
    epicSchema: S_epic_set_playback_range_locked__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.SetPlaybackRangeLocked", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Set playback range locked @bInLocked Whether the movie scene playback range should be locked Available on UE 5.9; absent from UE 5.8. Params: sequence, bInLocked", "epic_call_tool"),
  },
  epic_set_playback_speed: {
    epicSchema: S_epic_set_playback_speed,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_playback_speed", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the playback speed multiplier. Available on UE 5.8; absent from UE 5.9. Params: speed", "epic_call_tool"),
  },
  epic_set_playback_speed__level_sequence_editor_blueprint_library: {
    epicSchema: S_epic_set_playback_speed__level_sequence_editor_blueprint_library,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SetPlaybackSpeed", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Set playback speed of the current level sequence. Negative speeds play in reverse; zero and non-finite speeds raise an error and leave the speed unchanged. Available on UE 5.9; absent from UE 5.8. Params: newPlaybackSpeed", "epic_call_tool"),
  },
  epic_set_playback_start: {
    epicSchema: S_epic_set_playback_start,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.SetPlaybackStart", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Set playback start of this sequence Available on UE 5.9; absent from UE 5.8. Params: sequence, startFrame", "epic_call_tool"),
  },
  epic_set_playhead_frame: {
    epicSchema: S_epic_set_playhead_frame,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_playhead_frame", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the playhead position in display rate frames. Available on UE 5.8; absent from UE 5.9. Params: frame", "epic_call_tool"),
  },
  epic_set_playhead_frame_async: {
    epicSchema: S_epic_set_playhead_frame_async,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetPlayheadFrameAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Move the global playhead to a frame of the root sequence, in its display rate, after pending structure changes settle, then wait for the pose to evaluate. Available on UE 5.9; absent from UE 5.8. Params: frame", "epic_call_tool"),
  },
  epic_set_position: {
    epicSchema: S_epic_set_position,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_position", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set a position control value at a specific frame. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, frame, x?, y?, z?, set_key?", "epic_call_tool"),
  },
  epic_set_post_roll_frames: {
    epicSchema: S_epic_set_post_roll_frames,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.SetPostRollFrames", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Set non-negative post-roll in tick-resolution frames. Available on UE 5.9; absent from UE 5.8. Params: section, inPostRollFrames", "epic_call_tool"),
  },
  epic_set_pre_roll_frames: {
    epicSchema: S_epic_set_pre_roll_frames,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.SetPreRollFrames", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Set non-negative pre-roll in tick-resolution frames. Available on UE 5.9; absent from UE 5.8. Params: section, inPreRollFrames", "epic_call_tool"),
  },
  epic_set_preview_language: {
    epicSchema: S_epic_set_preview_language,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetPreviewLanguage", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Set the culture, such as \"fr\", whose localized sounds and animations this sequence's own sections preview in the editor. Only each section's own asset is swapped, not the waves a cue or MetaSound references. Sub-sequences keep their own setting. An empty culture clears it. The setting is not saved with the sequence and is not part of undo. Available on UE 5.9; absent from UE 5.8. Params: sequence, culture", "epic_call_tool"),
  },
  epic_set_preview_mesh: {
    epicSchema: S_epic_set_preview_mesh,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.SetPreviewMesh", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] SetPreviewMesh Available on UE 5.9; absent from UE 5.8. Params: deformer, previewMesh", "epic_call_tool"),
  },
  epic_set_priority_order: {
    epicSchema: S_epic_set_priority_order,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_priority_order", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set the evaluation priority order of a Control Rig track. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, order", "epic_call_tool"),
  },
  epic_set_property_name_and_path: {
    epicSchema: S_epic_set_property_name_and_path,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_property_name_and_path", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Configure a property track to animate a specific UProperty. This binds a generic property track (Float, Bool, Byte, etc.) to a specific property on the bound object. For nested properties use dot notation in the path. Examples: display_name=\"Intensity\", property_path=\"Intensity\" display_name=\"Focus Distance\", property_path=\"FocusSettings.ManualFocusDistance\" display_name=\"Animation Mode\", property_path=\"AnimationMode\" Available on UE 5.8; absent from UE 5.9. Params: track, display_name, property_path", "epic_call_tool"),
  },
  epic_set_property_name_and_path__movie_scene_property_track_extensions: {
    epicSchema: S_epic_set_property_name_and_path__movie_scene_property_track_extensions,
    epicTool: { toolset: "SequencerScripting.MovieScenePropertyTrackExtensions", name: "SequencerScripting.MovieScenePropertyTrackExtensions.SetPropertyNameAndPath", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieScenePropertyTrackExtensions] Set this track's property name and path Available on UE 5.9; absent from UE 5.8. Params: track, inPropertyName, inPropertyPath", "epic_call_tool"),
  },
  epic_set_range: {
    epicSchema: S_epic_set_range,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.SetRange", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Set range Available on UE 5.9; absent from UE 5.8. Params: section, startFrame, endFrame", "epic_call_tool"),
  },
  epic_set_resource_component_binding: {
    epicSchema: S_epic_set_resource_component_binding,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.SetResourceComponentBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Sets the component binding that sizes a resource, which also resets its data domain to the binding's first execution domain: set the data domain afterwards. Available on UE 5.9; absent from UE 5.8. Params: deformer, resourceName, bindingName", "epic_call_tool"),
  },
  epic_set_resource_data_domain: {
    epicSchema: S_epic_set_resource_data_domain,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.SetResourceDataDomain", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Set Resource Data Domain Available on UE 5.9; absent from UE 5.8. Params: deformer, resourceName, dataDomain", "epic_call_tool"),
  },
  epic_set_resource_data_type: {
    epicSchema: S_epic_set_resource_data_type,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.SetResourceDataType", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Set Resource Data Type Available on UE 5.9; absent from UE 5.8. Params: deformer, resourceName, dataType", "epic_call_tool"),
  },
  epic_set_rotator: {
    epicSchema: S_epic_set_rotator,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_rotator", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set a rotator control value at a specific frame. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, frame, pitch?, yaw?, roll?, set_key?", "epic_call_tool"),
  },
  epic_set_safe_frame_size: {
    epicSchema: S_epic_set_safe_frame_size,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SetSafeFrameSize", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Set the inset of a safe frame overlay, from 1 to 99 percent of the viewport. Raises when the name is not a safe frame or the size is out of range. Available on UE 5.9; absent from UE 5.8. Params: frameName, sizePercentage", "epic_call_tool"),
  },
  epic_set_scale: {
    epicSchema: S_epic_set_scale,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_scale", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set a scale control value at a specific frame. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, frame, x?, y?, z?, set_key?", "epic_call_tool"),
  },
  epic_set_section_animation: {
    epicSchema: S_epic_set_section_animation,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_section_animation", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the animation asset on a skeletal animation section. After adding a MovieSceneSkeletalAnimationTrack and section, call this to assign which AnimSequence plays in that section. Available on UE 5.8; absent from UE 5.9. Params: section, anim_sequence_path", "epic_call_tool"),
  },
  epic_set_section_blend_type: {
    epicSchema: S_epic_set_section_blend_type,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_section_blend_type", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the blend type of a section. Valid values: 'Absolute', 'Additive', 'Relative', 'Override'. Available on UE 5.8; absent from UE 5.9. Params: section, blend_type", "epic_call_tool"),
  },
  epic_set_section_completion_mode: {
    epicSchema: S_epic_set_section_completion_mode,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_section_completion_mode", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the completion mode of a section. Valid values: 'KeepState', 'RestoreState', 'ProjectDefault'. Available on UE 5.8; absent from UE 5.9. Params: section, completion_mode", "epic_call_tool"),
  },
  epic_set_section_condition: {
    epicSchema: S_epic_set_section_condition,
    epicTool: { toolset: "animation_toolset.toolsets.conditions.SequencerConditionTools", name: "animation_toolset.toolsets.conditions.SequencerConditionTools.set_section_condition", engines: ["5.8"], successors: [{ toolset: "SequencerScripting.MovieSceneConditionExtensions", name: "SequencerScripting.MovieSceneConditionExtensions.SetSectionCondition", engines: ["5.9"], schema: S_epic_set_section_condition__1, rename: {"condition_class":"conditionClass"} }] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.conditions.SequencerConditionTools] Set a condition on a section. Common condition classes: - /Script/MovieSceneTracks.MovieScenePlatformCondition - /Script/MovieSceneTracks.MovieSceneDirectorBlueprintCondition - /Script/MovieScene.MovieSceneGroupCondition Params: section, condition_class. On UE 5.9 it calls SequencerScripting.MovieSceneConditionExtensions.SetSectionCondition (Params: section, conditionClass).", "epic_call_tool"),
  },
  epic_set_section_ease_in: {
    epicSchema: S_epic_set_section_ease_in,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_section_ease_in", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the ease-in duration of a section in frames. Enables manual ease override if not already active. Available on UE 5.8; absent from UE 5.9. Params: section, duration", "epic_call_tool"),
  },
  epic_set_section_ease_out: {
    epicSchema: S_epic_set_section_ease_out,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_section_ease_out", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the ease-out duration of a section in frames. Enables manual ease override if not already active. Available on UE 5.8; absent from UE 5.9. Params: section, duration", "epic_call_tool"),
  },
  epic_set_section_end_bounded: {
    epicSchema: S_epic_set_section_end_bounded,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_section_end_bounded", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set whether the section end frame is bounded or infinite. Available on UE 5.8; absent from UE 5.9. Params: section, bounded", "epic_call_tool"),
  },
  epic_set_section_play_rate: {
    epicSchema: S_epic_set_section_play_rate,
    epicTool: { toolset: "SequencerScripting.MovieSceneTimeWarpExtensions", name: "SequencerScripting.MovieSceneTimeWarpExtensions.SetSectionPlayRate", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneTimeWarpExtensions] Give a section a fixed play rate, replacing any custom time warp. A rate of 0 holds the section on its first frame; GetTimeWarpType then returns FixedTime. Available on UE 5.9; absent from UE 5.8. Params: section, playRate", "epic_call_tool"),
  },
  epic_set_section_post_roll_frames: {
    epicSchema: S_epic_set_section_post_roll_frames,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_section_post_roll_frames", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the number of frames to post-roll this section after it ends. Post-roll continues evaluation after the section's real end, useful for simulations that need to settle. Available on UE 5.8; absent from UE 5.9. Params: section, frames", "epic_call_tool"),
  },
  epic_set_section_pre_roll_frames: {
    epicSchema: S_epic_set_section_pre_roll_frames,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_section_pre_roll_frames", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the number of frames to pre-roll this section before it starts. Pre-roll evaluates the section before its real start so physics, cloth, or simulation state can warm up. The pre-roll frames do not affect the rendered output of the section. Available on UE 5.8; absent from UE 5.9. Params: section, frames", "epic_call_tool"),
  },
  epic_set_section_range: {
    epicSchema: S_epic_set_section_range,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_section_range", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the frame range of a section. Available on UE 5.8; absent from UE 5.9. Params: section, start_frame, end_frame", "epic_call_tool"),
  },
  epic_set_section_row_index: {
    epicSchema: S_epic_set_section_row_index,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.SetSectionRowIndex", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Set the zero-based track row. A negative row is invalid, and so is any row but 0 on a track that does not support multiple rows. The owning track may also refuse a row, for example an animation mixer layer that holds a child track; the section then stays where it was and an error is raised. Available on UE 5.9; absent from UE 5.8. Params: section, rowIndex", "epic_call_tool"),
  },
  epic_set_section_start_bounded: {
    epicSchema: S_epic_set_section_start_bounded,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_section_start_bounded", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set whether the section start frame is bounded or infinite. Available on UE 5.8; absent from UE 5.9. Params: section, bounded", "epic_call_tool"),
  },
  epic_set_selection_range: {
    epicSchema: S_epic_set_selection_range,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_selection_range", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the selection range (green bar) start and end frames. Available on UE 5.8; absent from UE 5.9. Params: start_frame, end_frame", "epic_call_tool"),
  },
  epic_set_selection_range_end: {
    epicSchema: S_epic_set_selection_range_end,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SetSelectionRangeEnd", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Set the selection range end frame. Available on UE 5.9; absent from UE 5.8. Params: newFrame", "epic_call_tool"),
  },
  epic_set_selection_range_start: {
    epicSchema: S_epic_set_selection_range_start,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SetSelectionRangeStart", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Set the selection range start frame. Available on UE 5.9; absent from UE 5.8. Params: newFrame", "epic_call_tool"),
  },
  epic_set_selection_set_actor_active: {
    epicSchema: S_epic_set_selection_set_actor_active,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetSelectionSetActorActive", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Mark an actor active or inactive for selection sets, as its checkbox in the panel does. Ignored while sets use the viewport selection. Raises a script error if no level sequence is open in Sequencer or the actor is null. Available on UE 5.9; absent from UE 5.8. Params: actor, bActive", "epic_call_tool"),
  },
  epic_set_selection_set_properties: {
    epicSchema: S_epic_set_selection_set_properties,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetSelectionSetProperties", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set a selection set's name, color and row. Only the fields that differ are applied, in one undo step. Raises a script error, changing nothing, if no level sequence is open in Sequencer, the set does not exist, the name is empty, or the row is negative. Available on UE 5.9; absent from UE 5.8. Params: id, properties", "epic_call_tool"),
  },
  epic_set_selection_set_visibility: {
    epicSchema: S_epic_set_selection_set_visibility,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetSelectionSetVisibility", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Show, hide or isolate a selection set's controls on the actors selected in the viewport, as the set's Hide, Show, Isolate and Show All menu entries do. Not undoable. Raises a script error if no level sequence is open in Sequencer, the set does not exist, or Animation Mode is not active. Available on UE 5.9; absent from UE 5.8. Params: id, visibility, bMirror?", "epic_call_tool"),
  },
  epic_set_selection_sets_use_viewport_selection: {
    epicSchema: S_epic_set_selection_sets_use_viewport_selection,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SetSelectionSetsUseViewportSelection", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Set whether selection sets act on the actors selected in the viewport, or on the actors marked active. Raises a script error if no level sequence is open in Sequencer. Available on UE 5.9; absent from UE 5.8. Params: bUseViewportSelection", "epic_call_tool"),
  },
  epic_set_sequence_flag: {
    epicSchema: S_epic_set_sequence_flag,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.SetSequenceFlag", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Set or clear one evaluation flag. Set Volatile on any sequence that will be changed procedurally at runtime. Available on UE 5.9; absent from UE 5.8. Params: sequence, flag, bEnabled", "epic_call_tool"),
  },
  epic_set_sequence_locked: {
    epicSchema: S_epic_set_sequence_locked,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_sequence_locked", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Lock or unlock the current sequence and its descendants. Available on UE 5.8; absent from UE 5.9. Params: lock", "epic_call_tool"),
  },
  epic_set_show_curve: {
    epicSchema: S_epic_set_show_curve,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.SetShowCurve", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Set whether the channel's curve is drawn in the Sequencer track area. The Curve Editor's own curve visibility is separate. Available on UE 5.9; absent from UE 5.8. Params: channel, bInShowCurve", "epic_call_tool"),
  },
  epic_set_skeletal_animation: {
    epicSchema: S_epic_set_skeletal_animation,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.SetSkeletalAnimation", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Assign an animation asset by content path to a skeletal animation section. Available on UE 5.9; absent from UE 5.8. Params: section, animationAssetPath", "epic_call_tool"),
  },
  epic_set_socket_transform: {
    epicSchema: S_epic_set_socket_transform,
    epicTool: { toolset: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools", name: "editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools.set_socket_transform", engines: ["5.8"], successors: [{ toolset: "EditorToolset.SkeletalMeshToolset", name: "EditorToolset.SkeletalMeshToolset.SetSocketTransform", engines: ["5.9"], schema: S_epic_set_socket_transform__1, rename: {"socket_name":"socketName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools] Sets the local transform of a socket relative to its parent bone. Params: mesh, socket_name, transform. On UE 5.9 it calls EditorToolset.SkeletalMeshToolset.SetSocketTransform (Params: mesh, socketName, translation?, rotation?, scale?).", "epic_call_tool"),
  },
  epic_set_sorting_order: {
    epicSchema: S_epic_set_sorting_order,
    epicTool: { toolset: "SequencerScripting.MovieSceneFolderExtensions", name: "SequencerScripting.MovieSceneFolderExtensions.SetSortingOrder", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneFolderExtensions] Set the sorting order of the given folder. Siblings that each have a custom order sort by it, with higher values further down the list. A sibling folder with a negative order compares by name instead. Available on UE 5.9; absent from UE 5.8. Params: folder, sortingOrder", "epic_call_tool"),
  },
  epic_set_space: {
    epicSchema: S_epic_set_space,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_space", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set the space for a Control Rig control at a given frame. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, space_type, frame, space_target", "epic_call_tool"),
  },
  epic_set_start_frame_bounded: {
    epicSchema: S_epic_set_start_frame_bounded,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.SetStartFrameBounded", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Set start frame bounded Available on UE 5.9; absent from UE 5.8. Params: section, bIsBounded", "epic_call_tool"),
  },
  epic_set_thumbnail_reference_offset: {
    epicSchema: S_epic_set_thumbnail_reference_offset,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.SetThumbnailReferenceOffset", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Set the time of a camera cut or cinematic shot section's thumbnail, in seconds from the section start. Editor only. Available on UE 5.9; absent from UE 5.8. Params: section, offsetSeconds", "epic_call_tool"),
  },
  epic_set_tick_resolution: {
    epicSchema: S_epic_set_tick_resolution,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_tick_resolution", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the internal tick resolution of a sequence. Available on UE 5.8; absent from UE 5.9. Params: sequence, numerator, denominator?", "epic_call_tool"),
  },
  epic_set_tick_resolution__movie_scene_sequence_extensions: {
    epicSchema: S_epic_set_tick_resolution__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.SetTickResolution", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Sets this sequence's tick resolution and migrates frame times Available on UE 5.9; absent from UE 5.8. Params: sequence, tickResolution", "epic_call_tool"),
  },
  epic_set_time_posing_active: {
    epicSchema: S_epic_set_time_posing_active,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.SetTimePosingActive", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Turn Time Posing on or off for a sequence. Turning it on places the window by LinkMode: around the current time, around the selection range, or on the selected keys for Fixed. While it is on, keyed edits at the current time spread across the window. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, bActive", "epic_call_tool"),
  },
  epic_set_time_posing_range: {
    epicSchema: S_epic_set_time_posing_range,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.SetTimePosingRange", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Set the Time Posing window, one undoable step; anchors snap to display frames. With LinkMode CurrentFrame the window then moves with the current time. Raises when Time Posing is off, LinkMode is AroundRange (its far anchors follow the selection range), or the anchors are out of order. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, inRange, timeUnit?", "epic_call_tool"),
  },
  epic_set_time_posing_settings: {
    epicSchema: S_epic_set_time_posing_settings,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.SetTimePosingSettings", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Set the Time Posing preferences and save them to the editor preferences. The struct holds every field, so read it with GetTimePosingSettings, change what you need and pass it back. Raises for a negative or non-finite CenterWidth, or a FalloffWidth below 0.01 or non-finite; nothing is changed then. Available on UE 5.9; absent from UE 5.8. Params: inSettings", "epic_call_tool"),
  },
  epic_set_track_condition: {
    epicSchema: S_epic_set_track_condition,
    epicTool: { toolset: "animation_toolset.toolsets.conditions.SequencerConditionTools", name: "animation_toolset.toolsets.conditions.SequencerConditionTools.set_track_condition", engines: ["5.8"], successors: [{ toolset: "SequencerScripting.MovieSceneConditionExtensions", name: "SequencerScripting.MovieSceneConditionExtensions.SetTrackCondition", engines: ["5.9"], schema: S_epic_set_track_condition__1, rename: {"condition_class":"conditionClass"} }] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.conditions.SequencerConditionTools] Set a condition on a track. Params: track, condition_class. On UE 5.9 it calls SequencerScripting.MovieSceneConditionExtensions.SetTrackCondition (Params: track, conditionClass).", "epic_call_tool"),
  },
  epic_set_track_display_name: {
    epicSchema: S_epic_set_track_display_name,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_track_display_name", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the display name of a track. Available on UE 5.8; absent from UE 5.9. Params: track, name", "epic_call_tool"),
  },
  epic_set_track_filter_active: {
    epicSchema: S_epic_set_track_filter_active,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_track_filter_active", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Enable or disable a track filter. Available on UE 5.8; absent from UE 5.9. Params: name, active", "epic_call_tool"),
  },
  epic_set_track_filter_active__level_sequence_editor_blueprint_library: {
    epicSchema: S_epic_set_track_filter_active__level_sequence_editor_blueprint_library,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SetTrackFilterActive", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Sets the specified track filter to be on or off by display name, ignoring case. Raises an error when no Sequencer is open or no filter has that name. Available on UE 5.9; absent from UE 5.8. Params: trackFilterName, bActive", "epic_call_tool"),
  },
  epic_set_track_filter_text: {
    epicSchema: S_epic_set_track_filter_text,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary", name: "LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary.SetTrackFilterText", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorBlueprintLibrary] Sets the filter bar's search text, as typing in the search box does. An empty string clears it. Raises an error when no Sequencer is open. Available on UE 5.9; absent from UE 5.8. Params: filterText", "epic_call_tool"),
  },
  epic_set_track_row_condition: {
    epicSchema: S_epic_set_track_row_condition,
    epicTool: { toolset: "animation_toolset.toolsets.conditions.SequencerConditionTools", name: "animation_toolset.toolsets.conditions.SequencerConditionTools.set_track_row_condition", engines: ["5.8"], successors: [{ toolset: "SequencerScripting.MovieSceneConditionExtensions", name: "SequencerScripting.MovieSceneConditionExtensions.SetTrackRowCondition", engines: ["5.9"], schema: S_epic_set_track_row_condition__1, rename: {"row_index":"rowIndex","condition_class":"conditionClass"} }] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.conditions.SequencerConditionTools] Set a condition on a specific track row. Params: track, row_index, condition_class. On UE 5.9 it calls SequencerScripting.MovieSceneConditionExtensions.SetTrackRowCondition (Params: track, rowIndex, conditionClass).", "epic_call_tool"),
  },
  epic_set_transform: {
    epicSchema: S_epic_set_transform,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_transform", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set a transform value on a Control Rig control and optionally key it. Uses ControlRigSequencerLibrary.set_local_control_rig_transform. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, frame, location_x?, location_y?, location_z?, rotation_pitch?, rotation_yaw?, rotation_roll?, set_key?", "epic_call_tool"),
  },
  epic_set_transform_mask: {
    epicSchema: S_epic_set_transform_mask,
    epicTool: { toolset: "SequencerScripting.MovieSceneSectionExtensions", name: "SequencerScripting.MovieSceneSectionExtensions.SetTransformMask", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSectionExtensions] Set the channels a 3D transform section animates. Disabled channels keep their keys. Available on UE 5.9; absent from UE 5.8. Params: section, channels", "epic_call_tool"),
  },
  epic_set_variable_data_type: {
    epicSchema: S_epic_set_variable_data_type,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.SetVariableDataType", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] Changes the type, which resets the default value and removes links the new type cannot keep. Available on UE 5.9; absent from UE 5.8. Params: deformer, variableName, dataType", "epic_call_tool"),
  },
  epic_set_variable_default_value: {
    epicSchema: S_epic_set_variable_default_value,
    epicTool: { toolset: "OptimusScripting.DeformerGraphScriptingSubsystem", name: "OptimusScripting.DeformerGraphScriptingSubsystem.SetVariableDefaultValue", engines: ["5.9"] },
    ...bp("mutate", "[Epic OptimusScripting.DeformerGraphScriptingSubsystem] SetVariableDefaultValue Available on UE 5.9; absent from UE 5.8. Params: deformer, variableName, defaultValue", "epic_call_tool"),
  },
  epic_set_vector2d: {
    epicSchema: S_epic_set_vector2d,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_vector2d", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set a Vector2D control value at a specific frame. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, frame, x?, y?, set_key?", "epic_call_tool"),
  },
  epic_set_view_range: {
    epicSchema: S_epic_set_view_range,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_view_range", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the visible time range in the Sequencer timeline. Available on UE 5.8; absent from UE 5.9. Params: sequence, start_seconds, end_seconds", "epic_call_tool"),
  },
  epic_set_view_range__movie_scene_sequence_extensions: {
    epicSchema: S_epic_set_view_range__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.SetViewRange", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Set both view range bounds atomically in seconds. Requires finite bounds with start before end. Available on UE 5.9; absent from UE 5.8. Params: sequence, startTimeInSeconds, endTimeInSeconds", "epic_call_tool"),
  },
  epic_set_work_range: {
    epicSchema: S_epic_set_work_range,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.set_work_range", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Set the work range of the sequence. Available on UE 5.8; absent from UE 5.9. Params: sequence, start_seconds, end_seconds", "epic_call_tool"),
  },
  epic_set_work_range__movie_scene_sequence_extensions: {
    epicSchema: S_epic_set_work_range__movie_scene_sequence_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.SetWorkRange", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Set both work range bounds atomically in seconds. Requires finite bounds with start before end. Available on UE 5.9; absent from UE 5.8. Params: sequence, startTimeInSeconds, endTimeInSeconds", "epic_call_tool"),
  },
  epic_set_world_transform: {
    epicSchema: S_epic_set_world_transform,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_world_transform", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Set a control's world-space transform at a specific frame. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_name, frame, location_x?, location_y?, location_z?, rotation_pitch?, rotation_yaw?, rotation_roll?, set_key?", "epic_call_tool"),
  },
  epic_show_all_controls: {
    epicSchema: S_epic_show_all_controls,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.show_all_controls", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Show all controls on a Control Rig section (unmask everything). Available on UE 5.8; absent from UE 5.9. Params: section", "epic_call_tool"),
  },
  epic_show_all_controls__control_rig_sequencer_editor_library: {
    epicSchema: S_epic_show_all_controls__control_rig_sequencer_editor_library,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.ShowAllControls", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Unmask every control on the section so all of them are animated. Available on UE 5.9; absent from UE 5.8. Params: inSection", "epic_call_tool"),
  },
  epic_show_all_nodes: {
    epicSchema: S_epic_show_all_nodes,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.ShowAllNodes", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Show every hidden and isolated node again, as the filter bar's Show All Tracks does Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_show_all_selection_set_actor_controls: {
    epicSchema: S_epic_show_all_selection_set_actor_controls,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.ShowAllSelectionSetActorControls", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Show every control on an actor's Control Rigs, as the actor's Show All in the Selection Sets panel does. Not undoable. Raises a script error if no level sequence is open in Sequencer, the actor is null, or it is not bound in the focused level sequence. Available on UE 5.9; absent from UE 5.8. Params: actor", "epic_call_tool"),
  },
  epic_show_curve: {
    epicSchema: S_epic_show_curve,
    epicTool: { toolset: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools", name: "animation_toolset.toolsets.keyframing.SequencerKeyframingTools.show_curve", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.keyframing.SequencerKeyframingTools] Show or hide a curve in the Curve Editor. Available on UE 5.8; absent from UE 5.9. Params: channel, show", "epic_call_tool"),
  },
  epic_show_curve__level_sequence_editor_subsystem: {
    epicSchema: S_epic_show_curve__level_sequence_editor_subsystem,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.ShowCurve", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Show or hide a channel's curve in the active Curve Editor. Available on UE 5.9; absent from UE 5.8. Params: channel, bShowCurve", "epic_call_tool"),
  },
  epic_smart_reduce_section: {
    epicSchema: S_epic_smart_reduce_section,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SmartReduceSection", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Perform the Smart Reduce filter over a Control Rig section in the level sequence open in Sequencer. Available on UE 5.9; absent from UE 5.8. Params: movieSceneSection, reduceParams", "epic_call_tool"),
  },
  epic_snap_control_rig: {
    epicSchema: S_epic_snap_control_rig,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.snap_control_rig", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Snap Control Rig controls to a target actor over a frame range. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, control_names, target_actor_name, start_frame, end_frame, keep_offset?, snap_position?, snap_rotation?, snap_scale?", "epic_call_tool"),
  },
  epic_snap_control_rig__control_rig_sequencer_editor_library: {
    epicSchema: S_epic_snap_control_rig__control_rig_sequencer_editor_library,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SnapControlRig", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Peform a Snap operation to snap the children to the parent. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, startFrame, endFrame, childrenToSnap, parentToSnap, snapSettings, timeUnit?", "epic_call_tool"),
  },
  epic_snap_control_rig_async: {
    epicSchema: S_epic_snap_control_rig_async,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SnapControlRigAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] SnapControlRig as an async operation. See SnapControlRig for the parameters. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, startFrame, endFrame, childrenToSnap, parentToSnap, snapSettings, timeUnit?", "epic_call_tool"),
  },
  epic_sort_all_curve_editor_nodes: {
    epicSchema: S_epic_sort_all_curve_editor_nodes,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SortAllCurveEditorNodes", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Sort the items of the active Sequencer's open Curve Editor tree. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_sort_all_nodes: {
    epicSchema: S_epic_sort_all_nodes,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SortAllNodes", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Sort every node by type and then by label, discarding stored sorting orders, as Sequencer's Sort All Nodes command does Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_sort_nodes_by_start_time: {
    epicSchema: S_epic_sort_nodes_by_start_time,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.SortNodesByStartTime", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Sort nodes among their siblings by the start of their earliest section, as the outliner's Sort by Start Time does, storing the result in their sorting orders. A folder in Nodes has its children sorted too. A node in Nodes with a descendant also in Nodes keeps its place, an unbounded section starts at its first key, and a node with no start, such as one with no sections or keys, counts as frame 0. Raises for a node that cannot be sorted, such as a channel row, and for a node under a binding, whose children keep their type and label order. Available on UE 5.9; absent from UE 5.8. Params: nodes, bDescending?", "epic_call_tool"),
  },
  epic_space_compensate: {
    epicSchema: S_epic_space_compensate,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SpaceCompensate", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Perform compensation for any spaces at the specified time for the specified control rig Available on UE 5.9; absent from UE 5.8. Params: inControlRig, inTime, timeUnit?", "epic_call_tool"),
  },
  epic_space_compensate_all: {
    epicSchema: S_epic_space_compensate_all,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.SpaceCompensateAll", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Perform compensation for all spaces for the specified control rig Available on UE 5.9; absent from UE 5.8. Params: inControlRig", "epic_call_tool"),
  },
  epic_tag_binding: {
    epicSchema: S_epic_tag_binding,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.tag_binding", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Attach a tag to a binding. If the tag has not been seen in the sequence before, it is automatically registered. Available on UE 5.8; absent from UE 5.9. Params: binding, tag_name", "epic_call_tool"),
  },
  epic_tag_binding__movie_scene_binding_tag_extensions: {
    epicSchema: S_epic_tag_binding__movie_scene_binding_tag_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneBindingTagExtensions", name: "SequencerScripting.MovieSceneBindingTagExtensions.TagBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneBindingTagExtensions] Attach a tag to the given binding. If the tag name has not been seen before, it is registered in the MovieScene's tag list automatically. Available on UE 5.9; absent from UE 5.8. Params: binding, tagName", "epic_call_tool"),
  },
  epic_time_pose_actor_world_transform: {
    epicSchema: S_epic_time_pose_actor_world_transform,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.TimePoseActorWorldTransform", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Key an actor's world transform at a frame and spread the change across the Time Posing window by weight, as moving the actor with Time Posing on does. One undoable step. Raises when Time Posing is off, the frame is not strictly inside the far anchors, or SetActorWorldTransform raises. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, actor, frame, worldTransform, timeUnit?", "epic_call_tool"),
  },
  epic_time_pose_control_rig_world_transform: {
    epicSchema: S_epic_time_pose_control_rig_world_transform,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.TimePoseControlRigWorldTransform", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Key a Control Rig control's world transform at a frame and spread the change across the Time Posing window by weight, as moving the control with Time Posing on does; the keys go to the control's section to key, so Anim Layers apply. One undoable step. Raises when Time Posing is off, the frame is not strictly inside the far anchors, or SetControlRigWorldTransform raises. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlName, frame, worldTransform, timeUnit?", "epic_call_tool"),
  },
  epic_transform_actors_about_pivot: {
    epicSchema: S_epic_transform_actors_about_pivot,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.TransformActorsAboutPivot", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Key actors transformed about a pivot at each frame, as the Edit Pivot tool does in Pose mode: the new world transform is World * Pivot^-1 * Delta * Pivot. With bKeepOrientation the actors orbit the pivot and keep their rotation and scale, as Shift does in the tool. Keys the actors in one undoable step. An actor that cannot be keyed (not bound in the sequence, or its Transform track has no Absolute section) is skipped and the reason is logged as an error. Raises when the sequence is not focused or is read-only, Actors or Frames is empty, an actor is null, or no actor can be keyed; nothing is keyed then. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, actors, frames, delta, pivotTransform, bKeepOrientation?, timeUnit?", "epic_call_tool"),
  },
  epic_transform_controls_about_pivot: {
    epicSchema: S_epic_transform_controls_about_pivot,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.TransformControlsAboutPivot", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Move, rotate or scale Control Rig controls about a pivot, keying them at each frame. At every frame a control's new world transform is World * Pivot^-1 * Delta * Pivot: Delta is applied in the pivot's space, so a 90 degree yaw turns the controls about the pivot's up axis. Controls are keyed with SetControlRigWorldTransforms, so constraints and spaces are compensated. Raises a script error if the sequence is not focused, a control does not exist, Frames is empty, or PivotObject names more than one object. Every control is checked for being driven by the rig at the first frame before any is keyed; if keying still fails partway, the controls keyed before the failure stay keyed, in the call's single transaction, so one undo reverts the whole call. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlNames, frames, delta, pivotObject, pivotTransform, timeUnit?", "epic_call_tool"),
  },
  epic_transform_controls_about_pivot_async: {
    epicSchema: S_epic_transform_controls_about_pivot_async,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.TransformControlsAboutPivotAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] TransformControlsAboutPivot as an async operation. See TransformControlsAboutPivot for the parameters. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, controlNames, frames, delta, pivotObject, pivotTransform, timeUnit?", "epic_call_tool"),
  },
  epic_tween_control_rig: {
    epicSchema: S_epic_tween_control_rig,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.tween_control_rig", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Perform a tween operation on a Control Rig at the current Sequencer time. The tween blends between the previous and next keyframe values. Available on UE 5.8; absent from UE 5.9. Params: sequence, control_rig_asset_path, tween_value", "epic_call_tool"),
  },
  epic_tween_control_rig__control_rig_sequencer_editor_library: {
    epicSchema: S_epic_tween_control_rig__control_rig_sequencer_editor_library,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.TweenControlRig", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Perform a Tween operation on the Control Rig's selected controls at the current Sequencer time. Available on UE 5.9; absent from UE 5.8. Params: levelSequence, controlRig, tweenValue", "epic_call_tool"),
  },
  epic_ungroup_section: {
    epicSchema: S_epic_ungroup_section,
    epicTool: { toolset: "SequencerScripting.MovieSceneSequenceExtensions", name: "SequencerScripting.MovieSceneSequenceExtensions.UngroupSection", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneSequenceExtensions] Remove a section from its group. Raises when the section is not grouped. A group left with one section is dissolved. Available on UE 5.9; absent from UE 5.8. Params: sequence, section", "epic_call_tool"),
  },
  epic_unlink_anim_sequence: {
    epicSchema: S_epic_unlink_anim_sequence,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.UnlinkAnimSequence", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Remove the link between a binding and one linked Anim Sequence, on both assets. Raises a script error if the binding has no level sequence or the Anim Sequence is null. Available on UE 5.9; absent from UE 5.8. Params: binding, animSequence", "epic_call_tool"),
  },
  epic_unpin_all_motion_trails: {
    epicSchema: S_epic_unpin_all_motion_trails,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.UnpinAllMotionTrails", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Unpin every actor, control and socket trail. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_unpin_motion_trail: {
    epicSchema: S_epic_unpin_motion_trail,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.UnpinMotionTrail", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Unpin a motion trail; it stays drawn while its actor or control is selected. Raises when the trail is not pinned. Available on UE 5.9; absent from UE 5.8. Params: trailId", "epic_call_tool"),
  },
  epic_unpin_selected_motion_trails: {
    epicSchema: S_epic_unpin_selected_motion_trails,
    epicTool: { toolset: "SequencerAnimTools.SequencerAnimToolsEditorLibrary", name: "SequencerAnimTools.SequencerAnimToolsEditorLibrary.UnpinSelectedMotionTrails", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerAnimTools.SequencerAnimToolsEditorLibrary] Unpin the pinned trails of the selected actors and controls, as Alt-clicking Motion Paths does. Available on UE 5.9; absent from UE 5.8. Params: none", "epic_call_tool"),
  },
  epic_untag_binding: {
    epicSchema: S_epic_untag_binding,
    epicTool: { toolset: "animation_toolset.toolsets.sequencer.SequencerTools", name: "animation_toolset.toolsets.sequencer.SequencerTools.untag_binding", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.sequencer.SequencerTools] Remove a tag from a binding. Available on UE 5.8; absent from UE 5.9. Params: binding, tag_name", "epic_call_tool"),
  },
  epic_untag_binding__movie_scene_binding_tag_extensions: {
    epicSchema: S_epic_untag_binding__movie_scene_binding_tag_extensions,
    epicTool: { toolset: "SequencerScripting.MovieSceneBindingTagExtensions", name: "SequencerScripting.MovieSceneBindingTagExtensions.UntagBinding", engines: ["5.9"] },
    ...bp("mutate", "[Epic SequencerScripting.MovieSceneBindingTagExtensions] Remove a tag from the given binding. Removing its last association also unregisters the tag. No-op if the binding lacks the tag. Available on UE 5.9; absent from UE 5.8. Params: binding, tagName", "epic_call_tool"),
  },
  epic_update_control_rig_pose_asset: {
    epicSchema: S_epic_update_control_rig_pose_asset,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.UpdateControlRigPoseAsset", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Replace a pose asset's pose with a Control Rig's current values, as the Pose Library's Update Pose does. Raises a script error if the pose asset or Control Rig is null, or bUseAllControls is false and no control is selected. Available on UE 5.9; absent from UE 5.8. Params: poseAsset, controlRig, bUseAllControls?", "epic_call_tool"),
  },
  epic_validate_sequence: {
    epicSchema: S_epic_validate_sequence,
    epicTool: { toolset: "SequenceValidator.SequenceValidatorSubsystem", name: "SequenceValidator.SequenceValidatorSubsystem.ValidateSequence", engines: ["5.9"] },
    ...bp("read", "[Epic SequenceValidator.SequenceValidatorSubsystem] Run validation rules on Sequence and its subsequences, as the Sequence Validator window does. The sequence is not modified. Findings in, or reached through, subsequences hidden in this editor are left out. Available on UE 5.9; absent from UE 5.8. Params: sequence, ruleNames", "epic_call_tool"),
  },
  epic_wait_for_evaluation_async: {
    epicSchema: S_epic_wait_for_evaluation_async,
    epicTool: { toolset: "LevelSequenceEditor.LevelSequenceEditorSubsystem", name: "LevelSequenceEditor.LevelSequenceEditorSubsystem.WaitForEvaluationAsync", engines: ["5.9"] },
    ...bp("mutate", "[Epic LevelSequenceEditor.LevelSequenceEditorSubsystem] Wait for a complete editor world tick after this call. TimeoutSeconds must be positive. Available on UE 5.9; absent from UE 5.8. Params: timeoutSeconds?", "epic_call_tool"),
  },
  epic_zero_transforms: {
    epicSchema: S_epic_zero_transforms,
    epicTool: { toolset: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools", name: "animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.zero_transforms", engines: ["5.8"] },
    ...bp("mutate", "[Epic animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools] Reset Control Rig transforms to their default (usually zero) values. Available on UE 5.8; absent from UE 5.9. Params: selection_only?, include_channels?", "epic_call_tool"),
  },
  epic_zero_transforms__control_rig_sequencer_editor_library: {
    epicSchema: S_epic_zero_transforms__control_rig_sequencer_editor_library,
    epicTool: { toolset: "ControlRigEditor.ControlRigSequencerEditorLibrary", name: "ControlRigEditor.ControlRigSequencerEditorLibrary.ZeroTransforms", engines: ["5.9"] },
    ...bp("mutate", "[Epic ControlRigEditor.ControlRigSequencerEditorLibrary] Reset Transforms to default values, usually zero values but which may not be zero if initial values are present. Applies at the current time only. Requires Animation Mode to be active. Available on UE 5.9; absent from UE 5.8. Params: bSelectionOnly, bIncludeChannels", "epic_call_tool"),
  },
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  active: z.boolean().optional(),
  actor: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The actor"),
  actor_class: z.string().optional(),
  actor_class_path: z.string().optional(),
  actor_name: z.string().optional(),
  actor_names: z.array(z.unknown()).optional(),
  actorClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  actorContext: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Optional level sequence actor in InWorld that plays InSequence. Its sequence player is used for the import; when null, a temporary player is created and destro…"),
  actorNames: z.array(z.unknown()).optional(),
  actors: z.array(z.unknown()).optional().describe("The actors, bound in the sequence"),
  anim_sequence: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  anim_sequence_path: z.string().optional(),
  animationAssetPath: z.string().optional(),
  animEndRange: z.record(z.string(), z.unknown()).optional().describe("Last frame of the anim sequence to load, in the anim sequence's own frames; used if bUseCustomAnimRange is true"),
  animLayer: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The anim layer to get the index for; it must belong to that sequence"),
  animSequence: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The existing AnimSequence asset to write into"),
  animStartRange: z.record(z.string(), z.unknown()).optional().describe("First frame of the anim sequence to load, in the anim sequence's own frames; used if bUseCustomAnimRange is true"),
  ascii: z.boolean().optional(),
  asset_name: z.string().optional(),
  assetName: z.string().optional().describe("The asset name"),
  assetOrClassPath: z.string().optional().describe("A Control Rig asset path (\"/Game/Rigs/CR_Body\" or \"/Game/Rigs/CR_Body.CR_Body\"), or a Control Rig class path (\"/Script/ControlRig.FKControlRig\" or \"/Game/Rigs/…"),
  assetPath: z.string().optional().describe("Path of the new asset, e.g. \"/Game/Deformers/DG_Inflate\". Virtual paths are accepted."),
  attachment: z.string().optional().describe("Control name for a Control Rig, or optional socket name for a component or actor"),
  atTimes: z.array(z.unknown()).optional().describe("The times to key"),
  bActive: z.boolean().optional().describe("Whether or not it's active"),
  bAdd: z.boolean().optional().describe("Add to the current selection instead of replacing it"),
  bAddIfDoesNotExist: z.boolean().optional().describe("If it doesn't exist create it"),
  bAdditive: z.boolean().optional().describe("Blend the pose on top of the current values"),
  bAddToExistingLayers: z.boolean().optional().describe("If a layer with the same name exists, add to it if true, otherwise create a new layer with a number added to the name"),
  bakeInterval: z.record(z.string(), z.unknown()).optional().describe("The interval, in display-rate frames, between baked keys; must be at least 1"),
  bAlreadySelected: z.boolean().optional().describe("If true, only select on the Control Rigs that already have a selected control, as the Select All Controls command does"),
  bAutoArrangeSections: z.boolean().optional(),
  bCallInEditor: z.boolean().optional().describe("When true, the event will also fire during editor scrub (matches the CustomEvent's CallInEditor flag). Payload parameters: this entry point supplies no payload…"),
  bCanBlend: z.boolean().optional(),
  bClearSelection: z.boolean().optional().describe("Whether to clear the current selection first"),
  bCompensate: z.boolean().optional().describe("If true re-key the child to keep its world transform, if false leave its keys as they are"),
  bCreateIfNeeded: z.boolean().optional().describe("Create the channel if it doesn't exist"),
  bCreateLink: z.boolean().optional().describe("If true will create a link between the animation sequence and the level sequence"),
  bCreatePhysicsAsset: z.boolean().optional().describe("When true, create a PhysicsAsset bound to the imported mesh's skeleton."),
  bDeactivated: z.boolean().optional(),
  bDescending: z.boolean().optional(),
  bEnabled: z.boolean().optional(),
  bExpanded: z.boolean().optional(),
  bHidden: z.boolean().optional(),
  bImportAnimations: z.boolean().optional().describe("When true, create AnimSequence assets for any animations contained in the file."),
  bImportMaterials: z.boolean().optional().describe("When true, create Material assets for any materials referenced in the file. When false, the imported mesh has no materials assigned."),
  bImportTextures: z.boolean().optional().describe("When true, create Texture2D assets for any textures referenced by the imported materials. Only effective when bImportMaterials is true."),
  bIncludeChannels: z.boolean().optional().describe("Include channels in addition to parent controls"),
  binding: z.record(z.string(), z.unknown()).optional().describe("The MovieSceneBindingProxy whose backing binding is a MovieSceneReplaceableDirectorBlueprintBinding (or a custom-binding variant carrying a FMovieSceneDynamicB…"),
  binding_type_class: z.string().optional(),
  bindingName: z.string().optional().describe("Made unique if needed."),
  bindings: z.array(z.unknown()).optional(),
  bindingType: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  bInLocked: z.boolean().optional(),
  bInShowCurve: z.boolean().optional(),
  bIsBounded: z.boolean().optional().describe("Whether to reset the upper bound to the playback end (or zero), or open it."),
  bIsLayeredControlRig: z.boolean().optional().describe("Create a layered (additive) rig; requires the rig to have a backwards solve event, or to be an FK rig"),
  bIsolate: z.boolean().optional().describe("True to isolate, false to un-isolate"),
  bIsolated: z.boolean().optional(),
  bIsPublic: z.boolean().optional().describe("If true, other deformers can call the function."),
  bKeepOrientation: z.boolean().optional().describe("Move only the locations"),
  bKey: z.boolean().optional().describe("Key the blended controls at the current Sequencer time"),
  bKeyReduce: z.boolean().optional().describe("If true do key reduction based upon Tolerance, if false don't"),
  blend_type: z.string().optional(),
  blend_value: z.number().optional(),
  blenderSystem: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  blendOperation: z.string().optional().describe("The operation to perform"),
  blendValue: z.number().optional().describe("0 keeps the current values, 1 pastes the pose"),
  bLock: z.boolean().optional(),
  bLocked: z.boolean().optional(),
  bMaintainOffset: z.boolean().optional().describe("Whether to maintain offset between child and parent when setting the constraint"),
  bMirror: z.boolean().optional().describe("Blend toward each control's mirrored counterpart"),
  bMuted: z.boolean().optional(),
  bone_name: z.string().optional(),
  boneName: z.string().optional().describe("The name of the bone to attach the socket to."),
  bOntoSelectedControls: z.boolean().optional().describe("if true will only load onto selected controls on the Control Rig, otherwise will load onto all controls that aren't masked out"),
  bOpen: z.boolean().optional().describe("Open (true) or close (false) it"),
  bounded: z.boolean().optional(),
  boundObject: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Object whose method or class member will be called by the event. Typically an actor instance resolved from a sequence binding."),
  bPin: z.boolean().optional(),
  bPinned: z.boolean().optional(),
  bReduceKeys: z.boolean().optional().describe("If true do key reduction based upon Tolerance, if false don't"),
  bReplaceExisting: z.boolean().optional().describe("Overwrite existing assets the import would produce instead of failing."),
  bResetControls: z.boolean().optional().describe("If true will reset all controls to initial value on every frame"),
  bSave: z.boolean().optional().describe("Save the imported assets; otherwise they are left unsaved."),
  bSelect: z.boolean().optional().describe("True to select the controls, false to deselect them"),
  bSelectedControlsOnly: z.boolean().optional().describe("True to key only the controls selected on the section's Control Rig"),
  bSelectionOnly: z.boolean().optional().describe("Only onto rigs with selected controls"),
  bSetIsLayered: z.boolean().optional().describe("Convert to layered rig if true, or absolute if false"),
  bSetKey: z.boolean().optional().describe("If true key the reset values; if false set them, keyed only when Sequencer's auto-key is on"),
  bShow: z.boolean().optional(),
  bShowCurve: z.boolean().optional(),
  bSoloed: z.boolean().optional(),
  bSpawnable: z.boolean().optional(),
  bSupportAtomic: z.boolean().optional().describe("Allow atomic writes when the data type supports them."),
  bSupportRead: z.boolean().optional().describe("Let the kernel read the output too."),
  bToggle: z.boolean().optional().describe("Toggle each item's selection instead of setting it"),
  bUseAllControls: z.boolean().optional().describe("Store every animatable control instead of only the selected ones"),
  bUseCustomAnimRange: z.boolean().optional().describe("If true we will load in a specific animation range from the anim sequence"),
  bUseViewportSelection: z.boolean().optional().describe("True to act on the viewport selection"),
  bVisible: z.boolean().optional().describe("Whether its controls are visible"),
  bWholePose: z.boolean().optional().describe("Save every control of the selected controls' rig, or the selected actor's rig, instead of only the selected controls"),
  camera_binding_id: z.string().optional(),
  cameraBinding: z.record(z.string(), z.unknown()).optional(),
  category: z.string().optional().describe("Category ListNodeTypes lists the function under."),
  channel: z.record(z.string(), z.unknown()).optional(),
  channel_name: z.string().optional(),
  channelName: z.string().optional(),
  channels: z.unknown().optional(),
  childAttachment: z.string().optional().describe("Control name for a Control Rig, or optional socket name for a component or actor"),
  childObject: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The object to constrain"),
  childrenToSnap: z.record(z.string(), z.unknown()).optional().describe("The children objects that snap and get keys set onto. They need to live in an active Sequencer in the level editor"),
  classToSpawn: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  client: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  clock_source: z.string().optional(),
  clockSource: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  color: z.record(z.string(), z.unknown()).optional(),
  comment: z.string().optional(),
  completion_mode: z.string().optional(),
  component: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The component that owns the socket or bone"),
  component_bindings: z.array(z.unknown()).optional(),
  component_name: z.string().optional(),
  componentBindings: z.array(z.unknown()).optional(),
  componentName: z.string().optional(),
  componentOrSocketName: z.string().optional().describe("A component, socket or bone of SpaceActor; None, or a name that does not resolve, uses its root component"),
  componentSource: z.string().optional().describe("Name from ListComponentSources."),
  condition_class: z.string().optional(),
  conditionClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  conditionHost: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("A UMovieSceneSection or UMovieSceneTrack to gate."),
  constraint: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The Constraint to bake. After baking it will be keyed to be inactive of the range of frames that are baked"),
  constraints: z.array(z.unknown()).optional().describe("The transform constraints to bake"),
  constraintSection: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Section containing the constraint channel; null uses the section keyed for the constraint's child"),
  control_name: z.string().optional(),
  control_names: z.array(z.unknown()).optional(),
  control_rig: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The Control Rig to add the graph to."),
  control_rig_asset_path: z.string().optional(),
  controlName: z.string().optional().describe("The name of the control"),
  controlNames: z.array(z.unknown()).optional().describe("Names of the Controls"),
  controlRig: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The Control Rig with the control"),
  controls: z.array(z.unknown()).optional().describe("The set of Controls with their transforms"),
  count: z.number().optional().describe("Number of iterations, at least 1."),
  cr_section: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  create_link: z.boolean().optional(),
  create_physics_asset: z.boolean().optional(),
  creationTime: z.string().optional().describe("Key the constraint active at the current time, from the start of the sequence, or with no keys so it is always active"),
  culture: z.string().optional(),
  currentName: z.string().optional(),
  customBindingType: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  dataDomain: z.string().optional().describe("\"\" for a parameter, which only inputs can be."),
  dataType: z.string().optional(),
  deactivated: z.boolean().optional(),
  default_value: z.string().optional().describe("Default value as string."),
  defaultValue: z.string().optional().describe("Unreal export text. Empty keeps the type default."),
  deformer: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  deleteIndex: z.number().optional(),
  delta: z.record(z.string(), z.unknown()).optional().describe("The transform to apply in pivot space"),
  denominator: z.number().optional(),
  destinationSequence: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The sequence that will own or resolve the resulting binding ID. For example, if the binding ID will be applied to a camera cut section pass the sequence that c…"),
  direction: z.string().optional().describe("\"Input\" or \"Output\"."),
  display_name: z.string().optional(),
  displayRate: z.record(z.string(), z.unknown()).optional().describe("The display rate that this sequence is displayed as"),
  duration: z.number().optional(),
  element_type: z.string().optional().describe("Type of element to create: \"bone\" or \"null\"."),
  enabled: z.boolean().optional(),
  end_frame: z.number().optional(),
  end_seconds: z.number().optional(),
  endFrame: z.unknown().optional().describe("End of the snap"),
  endTimeInSeconds: z.number().optional(),
  enum_class_path: z.string().optional(),
  eval_type: z.string().optional(),
  event_type: z.string().optional().describe("The event type to add (BACKWARD_SOLVE or INTERACTION)."),
  eventSection: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The MovieSceneEventTriggerSection (or compatible) that the key lands on."),
  executionDomain: z.string().optional().describe("Domain from ListComponentSources, e.g. \"Vertex\": the kernel runs once per element of it. A quoted expression, e.g. \"'Vertex * 2'\" or \"'1'\", sets the number of…"),
  expanded: z.boolean().optional(),
  export_file_path: z.string().optional(),
  exportFBXControlRigSettings: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Export settings, including the output file name"),
  exportOption: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The export options for the sequence, or null for defaults"),
  exportOptions: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Export options for creating an animation sequence"),
  fbx_file_path: z.string().optional(),
  filenameFormat: z.string().optional().describe("Filename pattern recorded for each shot's referenced movie file. A value other than \"{shot_name}\" adds a warning message to the log, since that is the only for…"),
  filter: z.string().optional().describe("Case-insensitive text matched against the type, display name and category. Empty lists everything."),
  filterText: z.string().optional(),
  flag: z.string().optional(),
  folder: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  folder_path: z.string().optional(),
  folderPath: z.string().optional().describe("The content-browser folder to create the asset in."),
  folders: z.array(z.unknown()).optional(),
  folderToAdd: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The child folder to be added"),
  folderToRemove: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The child folder to be removed"),
  frame: z.unknown().optional().describe("Display-rate frame on the section where the event key is placed."),
  frameName: z.string().optional(),
  frames: z.unknown().optional().describe("The frames to bake, if the array is empty it will use the active time ranges of the constraint to determine where it should bake"),
  functionName: z.string().optional().describe("Reflected UFUNCTION name on BoundObject's class (e.g. \"SetActorScale3D\"), NOT the spaced Blueprint-editor display name. CreateQuickBinding resolves it via UCla…"),
  graph: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The graph to add the event to."),
  graph_name: z.string().optional().describe("Name of the graph (e.g., \"RigVMModel\")."),
  graphName: z.string().optional().describe("Name of a trigger graph. Ignored for \"Setup\"."),
  graphPath: z.string().optional(),
  graphType: z.string().optional().describe("\"Setup\" (runs once when the deformer is applied; at most one) or \"Trigger\" (runs when triggered from gameplay)."),
  group: z.unknown().optional().describe("Input bindings only: a secondary input group from AddKernelInputGroup. Empty for the primary group."),
  groupName: z.string().optional().describe("Must be a valid HLSL identifier. Made unique if needed."),
  groupSize: z.record(z.string(), z.unknown()).optional().describe("Threads per thread group, each component at least 1. A total divisible by 32 is best."),
  handleFrames: z.number().optional().describe("Extra frames of handle included before and after each shot. Must not be negative."),
  height: z.number().optional(),
  hide: z.boolean().optional(),
  id: z.string().optional().describe("The set to add to"),
  import_animations: z.boolean().optional(),
  import_file_path: z.string().optional(),
  import_materials: z.boolean().optional(),
  import_settings: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  import_textures: z.boolean().optional(),
  importFBXControlRigSettings: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Settings to control import."),
  importFilename: z.string().optional().describe("Path of the existing FBX file to read"),
  inAnimSequence: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Animation asset to inspect."),
  inAnimSequences: z.array(z.unknown()).optional().describe("The anim sequences to link, one per binding in the same order. Must be the same length as InBindings, and each entry must be compatible with the skeleton of it…"),
  inApplyMode: z.string().optional().describe("Set the EControlRigFKRigExecuteMode mode (Replace, Additive or Direct)"),
  inBinding: z.record(z.string(), z.unknown()).optional().describe("The binding (actor binding) whose Control Rig components get tracks"),
  inBindings: z.array(z.unknown()).optional().describe("The bindings to configure for auto-baking"),
  inBlendType: z.string().optional(),
  inChild: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The handle to the transformable to be constrained"),
  inClockSource: z.string().optional().describe("The clock source to set for this sequence"),
  include_channels: z.boolean().optional(),
  inCompletionMode: z.string().optional(),
  inConstraint: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The transform constraint to compensate"),
  inControlName: z.string().optional().describe("The name of the existing Vector type control"),
  inControlNames: z.array(z.unknown()).optional().describe("The name of the Controls to bake"),
  inControlRig: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("ControlRig to bake"),
  inControlRigTrack: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("ControlRigTrack to replace"),
  inDefaultValue: z.number().optional(),
  index: z.number().optional().describe("The index of the anim layer"),
  indices: z.array(z.unknown()).optional().describe("The indices to merge"),
  inDisplayName: z.string().optional(),
  inDuration: z.number().optional(),
  inEnum: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The enum to set"),
  inEvaluationType: z.string().optional().describe("The evaluation type to set for this sequence"),
  inFolderColor: z.record(z.string(), z.unknown()).optional().describe("The new display color for the folder"),
  inFolderName: z.string().optional().describe("The new name for the folder"),
  inFrame: z.record(z.string(), z.unknown()).optional().describe("Time to set the value"),
  inImportFBXSettings: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Settings that control the import."),
  inImportFilename: z.string().optional().describe("Path to an existing FBX file to import from."),
  inInterpolation: z.string().optional(),
  initial: z.boolean().optional().describe("If True, returns the initial (setup) transform."),
  inLabel: z.string().optional(),
  inLevelSequence: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Level sequence to inspect."),
  inName: z.string().optional().describe("The name for this track"),
  inNewControlNames: z.array(z.unknown()).optional().describe("The name of the new Control Rig Channels"),
  inNewTime: z.record(z.string(), z.unknown()).optional().describe("New time for the constraint key"),
  inNumChannelsUsed: z.number().optional().describe("The number of channels to use for this track: 2, 3 or 4"),
  inObjectBinding: z.record(z.string(), z.unknown()).optional().describe("The binding to add. Only its GUID is stored; a supplied sequence must be accessible."),
  inOldControlNames: z.array(z.unknown()).optional().describe("The name of the old Control Rig Control Channels to change. Will be replaced by the corresponding name in the InNewControlNames array"),
  inParams: z.record(z.string(), z.unknown()).optional(),
  inParent: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The handle to the parent of the constraint"),
  inPostRollFrames: z.number().optional(),
  inPreRollFrames: z.number().optional(),
  inPropertyName: z.string().optional().describe("The property name"),
  inPropertyPath: z.string().optional().describe("The property path"),
  inputPinPath: z.string().optional(),
  inRange: z.record(z.string(), z.unknown()).optional().describe("The four anchors"),
  inRef: z.record(z.string(), z.unknown()).optional().describe("Strong reference identifying the Control Rig (class or runtime asset)"),
  inSection: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Section to bake time warp from; it must have a time warp"),
  inSequence: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Unused; the bake uses the sequence open in Sequencer"),
  inSettings: z.record(z.string(), z.unknown()).optional().describe("The settings for the bake, e.g, how long to bake, to key reduce etc."),
  inSettingsToOverride: z.record(z.string(), z.unknown()).optional().describe("Map of public variable name and value strings that should be applied to the rig."),
  inSpaceKey: z.record(z.string(), z.unknown()).optional().describe("The new space for the Control: another element, GetDefaultParentKey, or GetWorldSpaceReferenceKey"),
  inStartFrame: z.record(z.string(), z.unknown()).optional().describe("Frames after the playback range start to insert the animation at; ignored when the section has both a start and an end bound, where the animation starts at the…"),
  interpolation: z.string().optional().describe("The key interpolation type to set the keys, defaults to EMovieSceneKeyInterpolation::SmartAuto"),
  inTime: z.record(z.string(), z.unknown()).optional().describe("Time to compensate"),
  inTrack: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Track for layers to collapse"),
  inType: z.string().optional().describe("Type of constraint to create"),
  inWorld: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The world whose constraints to search"),
  is_getter: z.boolean().optional().describe("True for getter node, False for setter node."),
  is_layered: z.boolean().optional(),
  is_public: z.boolean().optional().describe("Whether the variable is exposed publicly."),
  is_read_only: z.boolean().optional().describe("Whether the variable is read-only."),
  item: z.record(z.string(), z.unknown()).optional().describe("The element to get children of."),
  json: z.string().optional().describe("The JSON to import"),
  keep_offset: z.boolean().optional(),
  kernelName: z.string().optional().describe("HLSL entry point name; must be a valid HLSL identifier."),
  key: z.record(z.string(), z.unknown()).optional(),
  key_reduce: z.boolean().optional(),
  label: z.string().optional().describe("The label for the new mark or mark range, or empty to generate a unique one"),
  levelSequence: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The LevelSequence we are baking; the binding must belong to it"),
  location_x: z.number().optional(),
  location_y: z.number().optional(),
  location_z: z.number().optional(),
  lock: z.boolean().optional(),
  locked: z.boolean().optional(),
  lod_index: z.number().optional(),
  lodIndex: z.number().optional().describe("The LOD index to query. Defaults to 0 (highest quality)."),
  loop: z.boolean().optional(),
  mask: z.record(z.string(), z.unknown()).optional().describe("The channels to enable"),
  material: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  mergeAnimLayerSettings: z.record(z.string(), z.unknown()).optional().describe("Settings with per frame or key options and key reduction"),
  mesh: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  moduleName: z.string().optional().describe("A module of a modular rig, or empty for the rig itself"),
  movieExtension: z.string().optional().describe("Movie file extension recorded for each shot's referenced movie file, for example \".avi\" or \".mov\"."),
  movieSceneSection: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The Control rig section we want to bake"),
  muted: z.boolean().optional(),
  name: z.string().optional().describe("Optional custom name (default: \"Backward Solve\")."),
  networkMask: z.string().optional(),
  new_frame: z.number().optional(),
  new_index: z.number().optional(),
  new_name: z.string().optional(),
  new_type: z.string().optional().describe("New type path (e.g., \"double\", \"float\")."),
  newControlRig: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("ControlRig to put on track"),
  newFolderName: z.string().optional().describe("The name to give the added folder"),
  newFrame: z.number().optional(),
  newFrameNumber: z.record(z.string(), z.unknown()).optional(),
  newIndex: z.number().optional().describe("The new where the anim layer exists, can not be zero, can not move into the base layer."),
  newLoopMode: z.string().optional().describe("Value display names: SLM_NoLoop = \"No Looping\", SLM_Loop = \"Loop\", SLM_LoopSelectionRange = \"Loop Selection Range\"."),
  newName: z.string().optional().describe("The new name to give the socket."),
  newPlaybackSpeed: z.number().optional(),
  newValue: z.number().optional(),
  node: z.unknown().optional().describe("The node to delete."),
  node_name: z.string().optional().describe("Optional custom name for the node."),
  node_type: z.string().optional().describe("RigUnit struct name (e.g., \"RigUnit_MathFloatAdd\") or full path (e.g., \"/Script/ControlRig.RigUnit_MathFloatAdd\")."),
  nodePath: z.string().optional().describe("Path of the sub-graph node."),
  nodePaths: z.array(z.unknown()).optional(),
  nodes: z.array(z.unknown()).optional(),
  nodeType: z.string().optional().describe("Node type from ListNodeTypes, e.g. \"DataInterface/OptimusSkinnedMeshDataInterface\"."),
  numerator: z.number().optional(),
  obj: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  object: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The constrained object: a Control Rig, a scene component or an actor"),
  objectBinding: z.record(z.string(), z.unknown()).optional(),
  objectBindings: z.array(z.unknown()).optional(),
  objectToSpawn: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  offset: z.record(z.string(), z.unknown()).optional().describe("The offset"),
  offsetSeconds: z.number().optional(),
  old_frame: z.number().optional(),
  old_index: z.number().optional(),
  old_name: z.string().optional(),
  oldIndex: z.number().optional().describe("The old index where the anim layer exists, can not be zero, can not move the base layer"),
  oldName: z.string().optional().describe("The current name of the socket."),
  only_rig: z.boolean().optional(),
  operation: z.string().optional(),
  optionalSectionToSet: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The section that should get the keys, if not set, it will use the current section to key setup on the track."),
  order: z.number().optional(),
  outBools: z.array(z.unknown()).optional().describe("Array of whether or not it's active at the specified times"),
  outFrames: z.array(z.unknown()).optional().describe("The Times for the keys"),
  outputPinPath: z.string().optional(),
  overlayName: z.string().optional(),
  override_options: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  overrideClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  package_path: z.string().optional(),
  packagePath: z.string().optional().describe("The folder, for example /Game/Poses"),
  params: z.record(z.string(), z.unknown()).optional(),
  parent: z.record(z.string(), z.unknown()).optional().describe("Parent element in the hierarchy. None creates at root level."),
  parent_folder: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  parentAttachment: z.string().optional().describe("Control name for a Control Rig, or optional socket name for a component or actor"),
  parentObject: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The parent of the constraint"),
  parentToSnap: z.record(z.string(), z.unknown()).optional().describe("The parent object to snap relative to. If animated, it needs to live in an active Sequencer in the level editor"),
  paste_frame: z.number().optional(),
  paste_token: z.string().optional(),
  pasteBindingsParams: z.record(z.string(), z.unknown()).optional(),
  pasteFoldersParams: z.record(z.string(), z.unknown()).optional(),
  pasteSectionsParams: z.record(z.string(), z.unknown()).optional(),
  pasteTracksParams: z.record(z.string(), z.unknown()).optional(),
  path: z.string().optional().describe("The content browser path for the new Control Rig."),
  paths: z.array(z.unknown()).optional(),
  physics_asset: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  physicsAsset: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The physics asset to assign."),
  pin: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The pin to query."),
  pinned: z.boolean().optional(),
  pinPath: z.string().optional().describe("The PinPath of the binding in GetKernelInfo, GetLoopInfo or GetGraphInfo. For a sub-graph, the pin on a node that runs it works too; for a loop, the binding's…"),
  pitch: z.number().optional(),
  pivotObject: z.record(z.string(), z.unknown()).optional().describe("One actor or one Control Rig control to pivot about, sampled at every frame; empty to pivot about PivotTransform in world space"),
  pivotTransform: z.record(z.string(), z.unknown()).optional().describe("The world-space pivot when PivotObject is empty; otherwise an offset in the pivot object's space"),
  playRate: z.number().optional(),
  poseAsset: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The pose to blend toward"),
  position: z.record(z.string(), z.unknown()).optional().describe("Position in the graph. Uses (200, 200) if None."),
  previewMesh: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The mesh the editor previews the deformer on. Null clears it."),
  priorityOrder: z.number().optional(),
  properties: z.record(z.string(), z.unknown()).optional().describe("The properties to set, typically from GetAnimLayerProperties with fields changed"),
  property_path: z.string().optional(),
  propertyPath: z.string().optional().describe("The control setting, in the forms SetControlRigOverride takes"),
  recursive: z.boolean().optional().describe("If True, returns all descendants recursively."),
  reduce_keys: z.boolean().optional(),
  reduceParams: z.record(z.string(), z.unknown()).optional().describe("Key reduction parameters"),
  referenceName: z.string().optional().describe("Optional socket or bone name on the component; NAME_None returns the component's transform"),
  reset_controls: z.boolean().optional(),
  resourceName: z.string().optional(),
  resX: z.number().optional().describe("Resolution width recorded in the exported file. Must be positive."),
  resY: z.number().optional().describe("Resolution height recorded in the exported file. Must be positive."),
  roll: z.number().optional(),
  rootSequence: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The root sequence that contains both the destination sequence (that will resolve the binding ID) and the target sequence that contains the actual binding"),
  rotation: z.record(z.string(), z.unknown()).optional().describe("The new local rotation, or unset to keep the current one."),
  rotation_pitch: z.number().optional(),
  rotation_roll: z.number().optional(),
  rotation_yaw: z.number().optional(),
  rotationOrder: z.string().optional().describe("The order to use"),
  row_index: z.number().optional(),
  rowIndex: z.number().optional().describe("For a track host, the row to gate; -1 gates the whole track. Ignored for a section host."),
  ruleNames: z.array(z.unknown()).optional().describe("The names of the rules to run, from GetValidationRules; a name runs every rule that has it. Empty runs every rule enabled by default."),
  saveFilename: z.string().optional().describe("Path to write the .edl file to."),
  scale: z.unknown().optional().describe("The new local scale, or unset to keep the current one."),
  scale_x: z.number().optional(),
  scale_y: z.number().optional(),
  scale_z: z.number().optional(),
  section: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  sectionClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  sections: z.array(z.unknown()).optional(),
  selected: z.boolean().optional(),
  selected_controls: z.array(z.unknown()).optional(),
  selectedControlRigNames: z.array(z.unknown()).optional().describe("List of selected control rig names. Will use them if ImportFBXControlRigSettings->bImportOntoSelectedControls is true"),
  selection_only: z.boolean().optional(),
  sequence: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  set_key: z.boolean().optional(),
  settingName: z.string().optional(),
  settings: z.record(z.string(), z.unknown()).optional().describe("Control settings (shape, color, limits). Uses defaults if None."),
  show: z.boolean().optional(),
  size: z.record(z.string(), z.unknown()).optional().describe("Size of the comment box, in graph units."),
  sizePercentage: z.number().optional(),
  skeletal_mesh: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The skeletal mesh to import bones from."),
  skeletalMeshComponent: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The SkeletalMeshComponent"),
  skeleton: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  skelMeshComp: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The skeletal mesh component the Control Rig is bound to; if null it is found from the section's binding, which requires the section's sequence to be focused in…"),
  slot_name: z.string().optional(),
  slotName: z.string().optional().describe("The name of the material slot to query."),
  snap_position: z.boolean().optional(),
  snap_rotation: z.boolean().optional(),
  snap_scale: z.boolean().optional(),
  snapSettings: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Settings to use. With BakingKeySettings KeysOnly, the default, only frames keyed on the children or the parent between StartFrame and EndFrame are snapped, and…"),
  socket_name: z.string().optional(),
  socketName: z.string().optional().describe("The name to give the new socket."),
  soloed: z.boolean().optional(),
  sortingOrder: z.number().optional().describe("The new sorting order for the folder"),
  source: z.string().optional().describe("Can call the functions in FDeformerGraphKernelInfo::Declarations; the invocation index is \"Index\". Put the kernel body in a KERNEL {} block when the source als…"),
  source_file: z.string().optional(),
  source_pin: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The source (output) pin."),
  sourceFile: z.string().optional().describe("The absolute path to the source mesh file on disk."),
  sourcePaths: z.array(z.unknown()).optional().describe("Asset paths of UComputeSource assets (e.g. Deformer Graph Source assets), in include order. Empty clears the list."),
  sourcePinPath: z.string().optional(),
  space: z.unknown().optional().describe("The rig element to list"),
  space_target: z.string().optional(),
  space_type: z.string().optional(),
  spaceActor: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The actor to draw relative to"),
  spaces: z.array(z.unknown()).optional().describe("Every space GetControlRigAvailableSpaces returns, in the new order"),
  spawnable: z.boolean().optional(),
  speed: z.number().optional(),
  start_frame: z.number().optional(),
  start_seconds: z.number().optional(),
  startFrame: z.unknown().optional().describe("Beginning of the snap"),
  startTimeInSeconds: z.number().optional(),
  sub_section: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  subFrame: z.number().optional(),
  subSection: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  tag_name: z.string().optional(),
  tagName: z.string().optional(),
  target_actor_name: z.string().optional(),
  target_bindings: z.array(z.unknown()).optional(),
  target_pin: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The target (input) pin."),
  target_tracks: z.array(z.unknown()).optional(),
  targetFolder: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The folder to add a child folder to"),
  targetNodePath: z.string().optional(),
  templateSequence: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  textToImport: z.string().optional(),
  tickResolution: z.record(z.string(), z.unknown()).optional().describe("The tick resolution of the sequence, defining the smallest unit of time representable on this sequence"),
  timeoutSeconds: z.number().optional(),
  timeUnit: z.string().optional().describe("Unit for all frame and time values, either in display rate or tick resolution"),
  timeWarpClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional(),
  tint: z.record(z.string(), z.unknown()).optional(),
  tolerance: z.number().optional().describe("If reducing keys, tolerance about which keys will be removed, smaller tolerance, more keys usually."),
  tool: z.string().optional().describe("The tool"),
  track: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  track_type: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  trackFilterName: z.string().optional(),
  tracks: z.array(z.unknown()).optional(),
  trackToRemove: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The track to remove"),
  trackType: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("A UMovieSceneTrack class type specifying the type of track to create"),
  trailId: z.string().optional().describe("The pinned trail"),
  transform: z.record(z.string(), z.unknown()).optional().describe("Initial world transform. Uses identity if None. Only specified fields are applied; unset fields use identity."),
  translation: z.record(z.string(), z.unknown()).optional().describe("The new local position, or unset to keep the current one."),
  tween_value: z.number().optional(),
  tweenValue: z.number().optional().describe("The tween value to use, range from -1(blend to previous) to 1(blend to next)"),
  type: z.string().optional().describe("Type of constraint to create"),
  type_filter: z.string().optional(),
  type_path: z.string().optional().describe("UE type path (e.g., \"double\", \"float\", \"/Script/CoreUObject.Vector\")."),
  typeFilter: z.string().optional(),
  value: z.unknown().optional().describe("The new value as a string. Format depends on pin type: - float/double: \"1.0\" - int: \"42\" - bool: \"true\" or \"false\" - Vector: \"(X=1.0,Y=2.0,Z=3.0)\" - Rotator: \"…"),
  values: z.array(z.unknown()).optional().describe("The values to set at those times; must have one value per frame"),
  variable_name: z.string().optional().describe("Name of the variable to reference."),
  variableName: z.string().optional(),
  viewMode: z.string().optional(),
  viewport: z.record(z.string(), z.unknown()).optional(),
  visibility: z.string().optional().describe("What to show or hide"),
  visible: z.boolean().optional(),
  weight: z.number().optional().describe("The weight to key, typically 0 to 1"),
  width: z.number().optional(),
  world: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  worldTransform: z.record(z.string(), z.unknown()).optional().describe("The world transform to reach"),
  worldTransforms: z.array(z.unknown()).optional().describe("The world transform to reach at each frame"),
  x: z.number().optional(),
  y: z.number().optional(),
  yaw: z.number().optional(),
  z: z.number().optional(),
};
