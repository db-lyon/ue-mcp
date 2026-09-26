#include "BlueprintHandlers.h"
#include "BlueprintHandlers_Internal.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerPagination.h"
#include "HandlerJsonProperty.h"
#include "JsonSerializer.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "BlueprintEditorLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
// #942: World -> level script Blueprint resolution.
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptBlueprint.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "SubobjectDataSubsystem.h"
#include "SubobjectDataHandle.h"
#include "SubobjectData.h"
#include "SubobjectDataBlueprintFunctionLibrary.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "Internationalization/Text.h"
#include "UObject/TopLevelAssetPath.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "PackageTools.h"
#include "Factories/BlueprintFactory.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_CallFunction.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateNodeBase.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_AddComponent.h"
#include "K2Node_VariableGet.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_VariableSet.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallDelegate.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/MessageDialog.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/KismetArrayLibrary.h"

// SCS component access
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/InheritableComponentHandler.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ChildActorComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "EditorAssetLibrary.h"
#include "Containers/Queue.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Logging/TokenizedMessage.h"
#include "Kismet2/CompilerResultsLog.h"
#include "EdGraphUtilities.h"

void FBlueprintHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	// Reports parameters its handlers never read (#1057).
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("blueprint"));
	constexpr float ReadBlueprintGraphTimeoutSeconds = 180.0f;
	// #945: a first sweep on a cold project pays for every package load the
	// registry could not rule out, which the default request timeout does not
	// come close to covering.
	constexpr float SearchCallSitesTimeoutSeconds = 600.0f;

	// #1057: a spec'd handler declares its parameters here and nowhere else; the
	// TS surface is generated from a recording of them. A handler registered with
	// a timeout is registered with its spec first; the timed registration keeps
	// the spec.
	using EType = EMCPParamType;
	const FMCPParamSpec SpecAssetPath = MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint")).Alias(TEXT("path"));
	const FMCPParamSpec SpecAssetPathOrBlueprintPath = MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("Blueprint asset path. Read and graph actions also accept a World/umap path, resolved to that map's level script Blueprint")).Alias(TEXT("blueprintPath"));
	const FMCPParamSpec SpecEnumPath = MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("UserDefinedEnum asset path")).Alias(TEXT("path"));
	const FMCPParamSpec SpecStructPath = MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("UserDefinedStruct asset path")).Alias(TEXT("path"));
	const FMCPParamSpec SpecGraphName = MCPParam::Optional(TEXT("graphName"), EType::String, TEXT("Graph name or the selector list_graphs reports (default EventGraph)"));
	const FMCPParamSpec SpecGraphSelector = MCPParam::Optional(TEXT("graphSelector"), EType::String, TEXT("The exact selector list_graphs reports, which separates two graphs sharing a name. Wins over graphName"));
	const FMCPParamSpec SpecNodeId = MCPParam::Required(TEXT("nodeId"), EType::String, TEXT("Node GUID, as get_connections and find_nodes report it, or the node name")).Alias(TEXT("nodeName"));
	const FMCPParamSpec SpecVarName = MCPParam::Required(TEXT("name"), EType::String, TEXT("Variable name"));
	const FMCPParamSpec SpecFunctionName = MCPParam::Required(TEXT("functionName"), EType::String, TEXT("Function name"));
	const FMCPParamSpec SpecComponentName = MCPParam::Required(TEXT("componentName"), EType::String, TEXT("SCS or inherited component name"));
	const FMCPParamSpec SpecPropertyName = MCPParam::Required(TEXT("propertyName"), EType::String, TEXT("Property name"));
	const FMCPParamSpec SpecValue = MCPParam::Required(TEXT("value"), EType::Any, TEXT("Value to write: a JSON value, or Unreal export text. null clears an object, class or interface reference"));
	const FMCPParamSpec SpecOnConflict = MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("skip (default) reports the existing entry; error refuses"));
	const FMCPParamSpec SpecVarType = MCPParam::Optional(TEXT("varType"), EType::String, TEXT("Variable type: bool, int, float, string, name, text, byte, vector, rotator, transform, object:/Script/Module.Class, struct:/Game/Path, enum:/Game/Path.Enum, or a container over any of those (Type[], set<Type>, map<Key,Value>)")).Alias(TEXT("type"));
	const FMCPParamSpec SpecTitleFilter = MCPParam::Optional(TEXT("titleFilter"), EType::String, TEXT("Case-insensitive substring match on node title"));
	const FMCPParamSpec SpecClassFilter = MCPParam::Optional(TEXT("classFilter"), EType::String, TEXT("Case-insensitive substring match on node class name"));
	const FMCPParamSpec SpecPosX = MCPParam::Optional(TEXT("posX"), EType::Number, TEXT("Node X position in the graph"));
	const FMCPParamSpec SpecPosY = MCPParam::Optional(TEXT("posY"), EType::Number, TEXT("Node Y position in the graph"));
	const FMCPParamSpec SpecDefaultValue = MCPParam::Optional(TEXT("defaultValue"), EType::Any, TEXT("Default value as Unreal export text; an empty string clears it"));
	const FMCPParamSpec SpecOrder = MCPParam::Optional(TEXT("order"), EType::Array, TEXT("The COMPLETE desired order, never a partial list"));
	const FMCPParamSpec SpecDumpToFile = MCPParam::Optional(TEXT("dumpToFile"), EType::Boolean, TEXT("Write the whole result to a JSON file instead of returning it inline"));
	const FMCPParamSpec SpecOutputPath = MCPParam::Optional(TEXT("outputPath"), EType::String, TEXT("Absolute or Saved-relative JSON path for the dump"));
	const FMCPParamSpec SpecIncludeNestedGraphs = MCPParam::Optional(TEXT("includeNestedGraphs"), EType::Boolean, TEXT("Also walk collapsed and nested graphs (default true)"));
	const FMCPParamSpec SpecIncludeLevelScripts = MCPParam::Optional(TEXT("includeLevelScripts"), EType::Boolean, TEXT("Also sweep the level script Blueprint of every map under the directory, at a full map load each (default false)"));
	const FMCPParamSpec SpecDirectory = MCPParam::Optional(TEXT("directory"), EType::String, TEXT("Content path to search under (default /Game)"));
	const FMCPParamSpec SpecRecursive = MCPParam::Optional(TEXT("recursive"), EType::Boolean, TEXT("Include subfolders of directory (default true)"));
	const FMCPParamSpec SpecAssetPaths = MCPParam::Optional(TEXT("assetPaths"), EType::Array, TEXT("Blueprint asset paths; a World path resolves to its level script")).Items(EType::String);
	const FMCPParamSpec SpecMaxBlueprints = MCPParam::Optional(TEXT("maxBlueprints"), EType::Integer, TEXT("Cap on Blueprints loaded (default 2000)"));
	const FMCPParamSpec SpecCursor = MCPParam::Optional(TEXT("cursor"), EType::String, TEXT("Resume a paged read: pass back the nextCursor from the previous page, unmodified"));
	const FMCPParamSpec SpecParameters = MCPParam::Optional(TEXT("parameters"), EType::Array, TEXT("Typed signature parameters [{name, type}]; type takes the add_variable vocabulary, containers included")).Items(EType::Object);

	// The contract values name no class, so the parent class lookup refuses the
	// call before anything is created, on either branch.
	Registry.RegisterHandler(TEXT("create_blueprint"), &CreateBlueprint, {
		MCPParam::Optional(TEXT("assetPath"), EType::String, TEXT("Full destination, e.g. /Game/Blueprints/BP_Example. A .uasset suffix, an object suffix and backslashes are normalized away")).Alias(TEXT("path")),
		MCPParam::Optional(TEXT("name"), EType::String, TEXT("Asset name; with packagePath, the same destination as assetPath")),
		MCPParam::Optional(TEXT("packagePath"), EType::String, TEXT("Destination folder, used with name")),
		MCPParam::Optional(TEXT("parentClass"), EType::String, TEXT("Parent class: short name or full path (default Actor)")),
		SpecOnConflict,
	}, MCPSpec::ExactlyOne({ { TEXT("assetPath") }, { TEXT("name"), TEXT("packagePath") } }));
	Registry.RegisterHandler(TEXT("read_blueprint"), &ReadBlueprint, {
		SpecAssetPath,
		MCPParam::Optional(TEXT("includeComponentProperties"), EType::Boolean, TEXT("Dump UPROPERTY name, type and value per component template (default false)")),
	});
	Registry.RegisterHandler(TEXT("add_variable"), &AddVariable, {
		SpecAssetPath,
		SpecVarName,
		SpecVarType,
		SpecOnConflict,
	});
	Registry.RegisterHandler(TEXT("add_component"), &AddComponent, {
		SpecAssetPath,
		MCPParam::Required(TEXT("componentClass"), EType::String, TEXT("Component class: a short name such as ChildActorComponent, or a full path")),
		MCPParam::Optional(TEXT("componentName"), EType::String, TEXT("Name of the new component (default: componentClass)")),
		SpecOnConflict,
		MCPParam::Optional(TEXT("parentComponent"), EType::String, TEXT("SCS parent component for the hierarchy")),
		MCPParam::Optional(TEXT("childActorClass"), EType::String, TEXT("ChildActorClass for an added ChildActorComponent: a Blueprint path with or without _C, or a C++ class")),
	});
	Registry.RegisterHandler(TEXT("add_blueprint_interface"), &AddBlueprintInterface, {
		MCPParam::Required(TEXT("blueprintPath"), EType::String, TEXT("Blueprint asset path")),
		MCPParam::Required(TEXT("interfacePath"), EType::String, TEXT("Interface class path")),
	});
	Registry.RegisterHandler(TEXT("compile_blueprint"), &CompileBlueprint, {
		SpecAssetPath,
	});
	Registry.RegisterHandler(TEXT("search_node_types"), &SearchNodeTypes, {
		MCPParam::Required(TEXT("query"), EType::String, TEXT("Words to match against the C++ name, the palette label and Keywords metadata")),
		MCPParam::Optional(TEXT("className"), EType::String, TEXT("Narrow to one owning class, by short name or object path")).Alias(TEXT("classFilter")),
		MCPParam::Optional(TEXT("includeGraphNodes"), EType::Boolean, TEXT("Include UEdGraphNode classes alongside function-library entries (default true)")),
		SpecCursor,
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Rows to return on this page (default 50, max 500)")),
	});
	Registry.RegisterHandler(TEXT("list_node_types"), &ListNodeTypes, {
		MCPParam::Optional(TEXT("category"), EType::String, TEXT("Node category: utilities (default), math, string, gameplay, actor and so on")),
		SpecCursor,
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Rows to return on this page (default 100, max 1000)")),
	});
	Registry.RegisterHandler(TEXT("list_blueprint_variables"), &ListBlueprintVariables, {
		SpecAssetPath,
		MCPParam::Optional(TEXT("includeValues"), EType::Boolean, TEXT("Also resolve each variable's default off the generated-class CDO, and report whether the package holds unsaved changes (default false)")),
		SpecCursor,
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Rows to return on this page (default 200, max 2000)")),
	});
	Registry.RegisterHandler(TEXT("set_variable_properties"), &SetVariableProperties, {
		SpecAssetPath,
		SpecVarName,
		MCPParam::Optional(TEXT("editFlag"), EType::String, TEXT("EditAnywhere, EditDefaultsOnly, EditInstanceOnly or none: the value list_variables reports")),
		MCPParam::Optional(TEXT("instanceEditable"), EType::Boolean, TEXT("Two-state shorthand for editFlag; mutually exclusive with it")),
		MCPParam::Optional(TEXT("private"), EType::Boolean, TEXT("The Blueprint editor's Private checkbox, independent of editFlag")),
		MCPParam::Optional(TEXT("category"), EType::String, TEXT("Category")),
		MCPParam::Optional(TEXT("tooltip"), EType::String, TEXT("Tooltip")),
		MCPParam::Optional(TEXT("exposeOnSpawn"), EType::Boolean, TEXT("ExposeOnSpawn flag")),
	});
	Registry.RegisterHandler(TEXT("create_function"), &CreateFunction, {
		SpecAssetPath,
		SpecFunctionName,
		SpecOnConflict,
	});
	Registry.RegisterHandler(TEXT("list_blueprint_functions"), &ListBlueprintFunctions, {
		SpecAssetPath,
		MCPParam::Optional(TEXT("includeInherited"), EType::Boolean, TEXT("Append overridable parent and interface functions this Blueprint has not implemented (default false)")),
		SpecCursor,
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Rows to return on this page (default 200, max 2000)")),
	});
	Registry.RegisterHandler(TEXT("add_node"), &AddNode, {
		SpecAssetPath,
		SpecGraphName,
		MCPParam::Required(TEXT("nodeClass"), EType::String, TEXT("Node class or short alias such as CallFunction, CallParent or CustomEvent")),
		MCPParam::Optional(TEXT("nodeParams"), EType::Object, TEXT("Node-specific settings, such as functionName and className for a CallFunction node")),
	});
	Registry.RegisterHandler(TEXT("read_blueprint_graph"), &ReadBlueprintGraph, {
		SpecAssetPath,
		SpecGraphName,
		MCPParam::Optional(TEXT("offset"), EType::Integer, TEXT("Row offset into the (filtered) node list")),
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Nodes to return; omit for a safe default page, or dump the whole graph with dumpToFile")),
		MCPParam::Optional(TEXT("includePins"), EType::Boolean, TEXT("Include pins (default true)")),
		MCPParam::Optional(TEXT("includeDefaults"), EType::Boolean, TEXT("Include pin default values (default true)")),
		MCPParam::Optional(TEXT("includeComments"), EType::Boolean, TEXT("Include node comments (default true)")),
		SpecDumpToFile,
		SpecOutputPath,
		SpecTitleFilter,
		SpecClassFilter,
	});
	Registry.RegisterHandlerWithTimeout(TEXT("read_blueprint_graph"), &ReadBlueprintGraph, ReadBlueprintGraphTimeoutSeconds);
	Registry.RegisterHandler(TEXT("add_event_dispatcher"), &AddEventDispatcher, {
		MCPParam::Required(TEXT("blueprintPath"), EType::String, TEXT("Blueprint asset path")),
		MCPParam::Required(TEXT("name"), EType::String, TEXT("Dispatcher name")),
		SpecParameters,
	});
	Registry.RegisterHandler(TEXT("rename_function"), &RenameFunction, {
		SpecAssetPath,
		MCPParam::Required(TEXT("oldName"), EType::String, TEXT("Current name")),
		MCPParam::Required(TEXT("newName"), EType::String, TEXT("New name")),
	});
	Registry.RegisterHandler(TEXT("delete_function"), &DeleteFunction, {
		SpecAssetPath,
		SpecFunctionName,
	});
	Registry.RegisterHandler(TEXT("create_blueprint_interface"), &CreateBlueprintInterface, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("Full destination of the new Blueprint Interface")).Alias(TEXT("path")),
		SpecOnConflict,
	}, MCPSpec::ContractExempt(TEXT("Creates and saves an interface at the contract path; nothing it reads fails first")));
	Registry.RegisterHandler(TEXT("override_function"), &OverrideFunction, {
		SpecAssetPath,
		SpecFunctionName,
		MCPParam::Optional(TEXT("source"), EType::String, TEXT("Advisory hint for where the function comes from: auto (default), interface or parent. Echoed back")),
		MCPParam::Optional(TEXT("preferFunction"), EType::Boolean, TEXT("Force the function-graph form even when the function could be placed as an override event")),
		MCPParam::Optional(TEXT("interfacePath"), EType::String, TEXT("Implement this interface first when it is not present")),
	});
	Registry.RegisterHandler(TEXT("list_overridable_functions"), &ListOverridableFunctions, {
		SpecAssetPath,
	});
	Registry.RegisterHandler(TEXT("connect_pins"), &ConnectPins, {
		SpecAssetPath,
		SpecGraphName,
		SpecGraphSelector,
		MCPParam::Required(TEXT("sourceNodeId"), EType::String, TEXT("Source node GUID, as get_connections and find_nodes report it, or its title")).Alias(TEXT("sourceNode")),
		MCPParam::Required(TEXT("sourcePin"), EType::String, TEXT("Source pin name")).Alias(TEXT("sourcePinName")),
		MCPParam::Required(TEXT("targetNodeId"), EType::String, TEXT("Target node GUID, as get_connections and find_nodes report it, or its title")).Alias(TEXT("targetNode")),
		MCPParam::Required(TEXT("targetPin"), EType::String, TEXT("Target pin name")).Alias(TEXT("targetPinName")),
		MCPParam::Optional(TEXT("breakExistingSource"), EType::Boolean, TEXT("Break every existing link on the source pin first")),
		MCPParam::Optional(TEXT("breakExistingTarget"), EType::Boolean, TEXT("Break every existing link on the target pin first")),
	});
	Registry.RegisterHandler(TEXT("delete_node"), &DeleteNode, {
		SpecAssetPath,
		SpecGraphName,
		SpecGraphSelector,
		SpecNodeId,
	});
	Registry.RegisterHandler(TEXT("refresh_node"), &RefreshNode, {
		SpecAssetPath,
		SpecGraphName,
		SpecGraphSelector,
		SpecNodeId,
		MCPParam::Optional(TEXT("breakOrphanedPins"), EType::Boolean, TEXT("Also break the links holding orphaned pins alive, which removes those pins")),
	});
	Registry.RegisterHandler(TEXT("disconnect_pins"), &DisconnectPins, {
		SpecAssetPath,
		SpecGraphName,
		SpecGraphSelector,
		SpecNodeId,
		MCPParam::Required(TEXT("pinName"), EType::String, TEXT("The pin whose links are broken")),
		MCPParam::Optional(TEXT("linkedNodeId"), EType::String, TEXT("Break only links to this node (GUID or name). Omit to break every link on the pin")),
		MCPParam::Optional(TEXT("linkedPinName"), EType::String, TEXT("With linkedNodeId, break only the link to this pin")),
	});
	Registry.RegisterHandler(TEXT("set_node_property"), &SetNodeProperty, {
		SpecAssetPath,
		SpecGraphName,
		SpecNodeId,
		MCPParam::Required(TEXT("propertyName"), EType::String, TEXT("Pin or struct property name")).Alias(TEXT("pinName")),
		MCPParam::Required(TEXT("value"), EType::Any, TEXT("New pin default or property value, as a string")).Alias(TEXT("defaultValue")),
	});
	Registry.RegisterHandler(TEXT("list_blueprint_graphs"), &ListGraphs, {
		SpecAssetPath,
	});
	Registry.RegisterHandler(TEXT("resolve_blueprint_graph"), &ResolveGraph, {
		SpecAssetPath,
		MCPParam::Required(TEXT("graphName"), EType::String, TEXT("Graph name to resolve")),
	});
	Registry.RegisterHandler(TEXT("set_blueprint_component_property"), &SetComponentProperty, {
		SpecAssetPath,
		SpecComponentName,
		SpecPropertyName,
		SpecValue,
	});
	// #442: dedicated OverrideMaterials writer that takes a materialPaths array
	// directly, avoiding any value coercion concerns on the generic path.
	Registry.RegisterHandler(TEXT("set_component_override_materials"), &SetComponentOverrideMaterials, {
		SpecAssetPath,
		SpecComponentName,
		MCPParam::Required(TEXT("materialPaths"), EType::Array, TEXT("Material asset paths; an empty array clears")).Items(EType::String),
	});
	// #457: timeline track authoring (float/vector/color/event) on a Blueprint.
	Registry.RegisterHandler(TEXT("add_timeline_track"), &AddTimelineTrack, {
		SpecAssetPath,
		MCPParam::Required(TEXT("timelineName"), EType::String, TEXT("Timeline name")),
		MCPParam::Required(TEXT("trackName"), EType::String, TEXT("Track name within the timeline")),
		MCPParam::Optional(TEXT("trackType"), EType::String, TEXT("float (default), vector, color or event")),
		MCPParam::Optional(TEXT("keyframes"), EType::Array, TEXT("[{time, value}]: value is a number for float and event, {x,y,z} for vector, {r,g,b,a} for color")).Items(EType::Object),
	});
	Registry.RegisterHandler(TEXT("set_capsule_size"), &SetCapsuleSize, {
		SpecAssetPath,
		SpecComponentName,
		MCPParam::Optional(TEXT("halfHeight"), EType::Number, TEXT("Capsule half height (unscaled)")),
		MCPParam::Optional(TEXT("radius"), EType::Number, TEXT("Capsule radius (unscaled)")),
	});
	Registry.RegisterHandler(TEXT("set_class_default"), &SetClassDefault, {
		SpecAssetPath,
		SpecPropertyName,
		SpecValue,
	});
	Registry.RegisterHandler(TEXT("remove_component"), &RemoveComponent, {
		SpecAssetPath,
		SpecComponentName,
	});
	Registry.RegisterHandler(TEXT("delete_variable"), &DeleteVariable, {
		SpecAssetPath,
		SpecVarName,
	});
	Registry.RegisterHandler(TEXT("add_function_parameter"), &AddFunctionParameter, {
		SpecAssetPath,
		SpecFunctionName,
		MCPParam::Required(TEXT("parameterName"), EType::String, TEXT("Parameter name")),
		MCPParam::Optional(TEXT("parameterType"), EType::String, TEXT("Parameter type in the add_variable vocabulary, containers included (default float)")),
		MCPParam::Optional(TEXT("isOutput"), EType::Boolean, TEXT("An output parameter rather than an input (default false)")),
	});
	Registry.RegisterHandler(TEXT("set_variable_default"), &SetVariableDefault, {
		SpecAssetPath,
		SpecVarName,
		MCPParam::Required(TEXT("value"), EType::Any, TEXT("New default value, as a string")),
	});
	Registry.RegisterHandler(TEXT("get_blueprint_variable_default"), &GetVariableDefault, {
		SpecAssetPath,
		SpecVarName,
	});

	Registry.RegisterHandler(TEXT("read_blueprint_graph_summary"), &ReadBlueprintGraphSummary, {
		SpecAssetPath,
		SpecGraphName,
		SpecTitleFilter,
		SpecClassFilter,
	});
	Registry.RegisterHandler(TEXT("get_blueprint_execution_flow"), &GetBlueprintExecutionFlow, {
		SpecAssetPath,
		SpecGraphName,
		MCPParam::Optional(TEXT("entryPoint"), EType::String, TEXT("Event or function name to start the exec-flow trace from")),
	});
	Registry.RegisterHandler(TEXT("get_blueprint_dependencies"), &GetBlueprintDependencies, {
		SpecAssetPath,
		MCPParam::Optional(TEXT("reverse"), EType::Boolean, TEXT("true reports the referencers of this asset instead")),
	});

	// v0.7.11 - BP authoring depth
	Registry.RegisterHandler(TEXT("duplicate_blueprint"), &DuplicateBlueprint, {
		MCPParam::Required(TEXT("sourcePath"), EType::String, TEXT("Blueprint asset to duplicate")),
		MCPParam::Required(TEXT("destinationPath"), EType::String, TEXT("Full destination asset path")),
	});
	Registry.RegisterHandler(TEXT("add_local_variable"), &AddLocalVariable, {
		SpecAssetPath,
		SpecFunctionName,
		SpecVarName,
		SpecVarType,
	});
	Registry.RegisterHandler(TEXT("list_local_variables"), &ListLocalVariables, {
		SpecAssetPath,
		SpecFunctionName,
	});
	Registry.RegisterHandler(TEXT("validate_blueprint"), &ValidateBlueprint, {
		SpecAssetPath,
	});

	// v0.7.11 - issue fixes
	Registry.RegisterHandler(TEXT("read_component_properties"), &ReadComponentProperties, {
		SpecAssetPath,
		SpecComponentName,
	});
	Registry.RegisterHandler(TEXT("read_node_property"), &ReadNodeProperty, {
		SpecAssetPath,
		SpecGraphName,
		SpecNodeId,
		MCPParam::Required(TEXT("propertyName"), EType::String, TEXT("Pin or reflected node property name")).Alias(TEXT("pinName")),
	});
	Registry.RegisterHandler(TEXT("reparent_component"), &ReparentComponent, {
		SpecAssetPath,
		SpecComponentName,
		MCPParam::Required(TEXT("newParent"), EType::String, TEXT("New parent component name")),
	});
	Registry.RegisterHandler(TEXT("reparent_blueprint"), &ReparentBlueprint, {
		SpecAssetPath,
		MCPParam::Required(TEXT("parentClass"), EType::String, TEXT("New parent class: short name or full path")),
	});
	Registry.RegisterHandler(TEXT("flush_inheritable_component_handler"), &FlushInheritableComponentHandler, {
		SpecAssetPath,
	});
	Registry.RegisterHandler(TEXT("flush_blueprint_component_templates"), &FlushComponentTemplates, {
		SpecAssetPath,
	});
	Registry.RegisterHandler(TEXT("set_actor_tick_settings"), &SetActorTickSettings, {
		SpecAssetPath,
		MCPParam::Optional(TEXT("bCanEverTick"), EType::Boolean, TEXT("PrimaryActorTick.bCanEverTick")),
		MCPParam::Optional(TEXT("bStartWithTickEnabled"), EType::Boolean, TEXT("PrimaryActorTick.bStartWithTickEnabled")),
		MCPParam::Optional(TEXT("TickInterval"), EType::Number, TEXT("PrimaryActorTick.TickInterval in seconds")),
	});

	// v0.7.12 - issue #128 - single-property read (inherited-aware)
	Registry.RegisterHandler(TEXT("get_blueprint_component_property"), &GetComponentProperty, {
		SpecAssetPath,
		SpecComponentName,
		SpecPropertyName,
	});

	// v0.7.17 issue #130: bulk graph node import via T3D copy/paste
	Registry.RegisterHandler(TEXT("export_nodes_t3d"), &ExportNodesT3D, {
		SpecAssetPath,
		SpecGraphName,
		MCPParam::Optional(TEXT("nodeIds"), EType::Array, TEXT("Node GUIDs or names to export (omit for the whole graph)")).Items(EType::String),
	});
	Registry.RegisterHandler(TEXT("import_nodes_t3d"), &ImportNodesT3D, {
		SpecAssetPath,
		SpecGraphName,
		MCPParam::Required(TEXT("t3d"), EType::String, TEXT("T3D blob from export_nodes_t3d, or copied from the Blueprint graph editor")).Alias(TEXT("text")),
		MCPParam::Optional(TEXT("posX"), EType::Number, TEXT("Re-center pasted nodes around this X (with posY)")),
		MCPParam::Optional(TEXT("posY"), EType::Number, TEXT("Re-center pasted nodes around this Y (with posX)")),
	});

	// issues #182/#183: C++ class CDO property access
	Registry.RegisterHandler(TEXT("set_cdo_property"), &SetCdoProperty, {
		MCPParam::Required(TEXT("className"), EType::String, TEXT("C++ class name or path")),
		SpecPropertyName,
		SpecValue,
	});
	Registry.RegisterHandler(TEXT("get_cdo_properties"), &GetCdoProperties, {
		MCPParam::Required(TEXT("className"), EType::String, TEXT("C++ class name or path")),
		MCPParam::Optional(TEXT("propertyNames"), EType::Array, TEXT("Property names to read (omit for all)")).Items(EType::String),
	});

	// issue #195: run construction script and inspect resulting components
	Registry.RegisterHandler(TEXT("run_construction_script"), &RunConstructionScript, {
		SpecAssetPath,
		MCPParam::Optional(TEXT("location"), EType::Vec3, TEXT("Spawn location for the temporary actor")),
	});

	// v1.0.0-rc.15 - agent-friendly BP authoring
	Registry.RegisterHandler(TEXT("compile_blueprints"), &CompileBlueprints, {
		MCPParam::Required(TEXT("assetPaths"), EType::Array, TEXT("Blueprint asset paths to compile")).Items(EType::String),
		MCPParam::Optional(TEXT("save"), EType::Boolean, TEXT("Persist on success (default true)")),
	});
	Registry.RegisterHandler(TEXT("cleanup_graph"), &CleanupGraph, {
		SpecAssetPath,
		MCPParam::Optional(TEXT("graphName"), EType::String, TEXT("Graph to clean (default: every graph)")),
	});
	Registry.RegisterHandler(TEXT("connect_pins_batch"), &ConnectPinsBatch, {
		SpecAssetPath,
		SpecGraphName,
		MCPParam::Required(TEXT("connections"), EType::Array, TEXT("[{sourceNode, sourcePin, targetNode, targetPin}]")).Items(EType::Object),
	});
	Registry.RegisterHandler(TEXT("set_node_position"), &SetNodePosition, {
		SpecAssetPath,
		SpecGraphName,
		SpecNodeId,
		SpecPosX,
		SpecPosY,
	});
	Registry.RegisterHandler(TEXT("auto_layout_graph"), &AutoLayoutGraph, {
		SpecAssetPath,
		SpecGraphName,
		MCPParam::Optional(TEXT("columnGap"), EType::Integer, TEXT("Horizontal spacing between columns (default 360)")),
		MCPParam::Optional(TEXT("rowGap"), EType::Integer, TEXT("Vertical spacing between rows (default 200)")),
		MCPParam::Optional(TEXT("previousPositionsLimit"), EType::Integer, TEXT("Capture each node's pre-layout coordinates only when the graph has at most this many nodes (default 200)")),
		MCPParam::Optional(TEXT("capturePreviousPositions"), EType::Boolean, TEXT("Capture previousPositions regardless of previousPositionsLimit (default false)")),
	});

	// #945: project-wide call-site audit (BlueprintHandlers_Search.cpp).
	Registry.RegisterHandler(TEXT("search_blueprint_call_sites"), &SearchCallSites, {
		MCPParam::Required(TEXT("functionNames"), EType::Array, TEXT("Function names to find call sites for, max 50 per request")).Items(EType::String),
		MCPParam::Optional(TEXT("className"), EType::String, TEXT("Declaring class to narrow the functions to")),
		SpecDirectory,
		SpecIncludeNestedGraphs,
		SpecIncludeLevelScripts,
		MCPParam::Optional(TEXT("includeNeighbours"), EType::Boolean, TEXT("Include each hit's immediate execution and data neighbours")),
		MCPParam::Optional(TEXT("narrowByRegistry"), EType::Boolean, TEXT("Use Asset Registry dependencies to rule out Blueprints before loading them (default true)")),
		MCPParam::Optional(TEXT("offset"), EType::Integer, TEXT("Row offset, the older non-resumable form of paging")),
		SpecCursor,
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Rows to return on this page (default 200, max 1000)")),
		SpecMaxBlueprints,
		SpecDumpToFile,
		SpecOutputPath,
	});
	Registry.RegisterHandlerWithTimeout(TEXT("search_blueprint_call_sites"), &SearchCallSites, SearchCallSitesTimeoutSeconds);
	Registry.RegisterHandler(TEXT("search_blueprint_nodes"), &SearchNodes, {
		SpecAssetPathOrBlueprintPath,
		MCPParam::Optional(TEXT("titles"), EType::Array, TEXT("Case-insensitive substrings matched against each node title")).Items(EType::String),
		MCPParam::Optional(TEXT("nodeClasses"), EType::Array, TEXT("Exact node class names, such as K2Node_VariableGet")).Items(EType::String),
		MCPParam::Optional(TEXT("variableName"), EType::String, TEXT("The member a node reads or writes")),
		MCPParam::Optional(TEXT("variableAccess"), EType::String, TEXT("Narrow variableName to get, set or any (default any)")),
		SpecIncludeNestedGraphs,
		MCPParam::Optional(TEXT("authoredOnly"), EType::Boolean, TEXT("Leave out transient and generated compiler graphs (default true)")),
		SpecCursor,
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Rows to return on this page (default 200, max 1000)")),
	});
	Registry.RegisterHandlerWithTimeout(TEXT("search_blueprint_nodes"), &SearchNodes, SearchCallSitesTimeoutSeconds);
	Registry.RegisterHandler(TEXT("get_blueprint_connections"), &GetConnections, {
		SpecAssetPath,
		SpecGraphName,
		SpecGraphSelector,
		MCPParam::Optional(TEXT("nodeId"), EType::String, TEXT("Report only the edges into and out of this node (GUID or name)")),
		MCPParam::Optional(TEXT("kind"), EType::String, TEXT("exec, data or all (default all)")),
		SpecIncludeNestedGraphs,
		SpecCursor,
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Rows to return on this page (default 200, max 1000)")),
	});
	Registry.RegisterHandlerWithTimeout(TEXT("get_blueprint_connections"), &GetConnections, SearchCallSitesTimeoutSeconds);

	// #1166: batch export to disk and the dead-code audit (BlueprintHandlers_Audit.cpp).
	// export_blueprint_batch writes files, and its contract values are safe
	// because the parentClass they carry resolves to no class, which is refused
	// before anything is enumerated or written.
	Registry.RegisterHandler(TEXT("export_blueprint_batch"), &ExportBlueprintBatch, {
		SpecAssetPaths,
		SpecDirectory,
		SpecRecursive,
		MCPParam::Optional(TEXT("parentClass"), EType::String, TEXT("Only Blueprints deriving from this class")),
		SpecIncludeLevelScripts,
		MCPParam::Optional(TEXT("outputDir"), EType::String, TEXT("Absolute directory, or one relative to Saved/ (default Saved/UE_MCP/BlueprintExport)")),
		MCPParam::Optional(TEXT("maxAssets"), EType::Integer, TEXT("Cap on Blueprints exported (default 200, max 5000)")),
		MCPParam::Optional(TEXT("includeT3D"), EType::Boolean, TEXT("Also write one T3D file per graph (default false)")),
	});
	Registry.RegisterHandlerWithTimeout(TEXT("export_blueprint_batch"), &ExportBlueprintBatch, SearchCallSitesTimeoutSeconds);
	Registry.RegisterHandler(TEXT("audit_blueprint_dead_code"), &AuditDeadCode, {
		SpecDirectory,
		SpecAssetPaths,
		SpecRecursive,
		SpecIncludeLevelScripts,
		MCPParam::Optional(TEXT("scanReferencers"), EType::Boolean, TEXT("Also scan every Blueprint package that depends on an audited one for references (default true)")),
		SpecMaxBlueprints,
		MCPParam::Optional(TEXT("maxSamples"), EType::Integer, TEXT("Samples listed per finding kind per Blueprint (default 20, max 500); counts stay complete")),
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Rows to return (default 200, max 5000)")),
		SpecDumpToFile,
		SpecOutputPath,
	});
	Registry.RegisterHandlerWithTimeout(TEXT("audit_blueprint_dead_code"), &AuditDeadCode, SearchCallSitesTimeoutSeconds);

	// V9 Blueprint depth (BlueprintHandlers_Depth.cpp). Interface removal and
	// listing, function flags and metadata, parameter CRUD across functions,
	// macros, dispatcher signatures and custom events, member and local
	// variable rename plus metadata, dispatcher removal, custom events with a
	// typed signature, and macro authoring.
	Registry.RegisterHandler(TEXT("list_blueprint_interfaces"), &ListBlueprintInterfaces, {
		SpecAssetPathOrBlueprintPath,
	});
	Registry.RegisterHandler(TEXT("remove_blueprint_interface"), &RemoveBlueprintInterface, {
		SpecAssetPathOrBlueprintPath,
		MCPParam::Required(TEXT("interfacePath"), EType::String, TEXT("Interface class path, as list_interfaces reports it")),
		MCPParam::Optional(TEXT("preserveFunctions"), EType::Boolean, TEXT("Keep the implementations as ordinary Blueprint functions (default false)")),
	});
	Registry.RegisterHandler(TEXT("set_function_properties"), &SetFunctionProperties, {
		SpecAssetPath,
		SpecFunctionName,
		MCPParam::Optional(TEXT("pure"), EType::Boolean, TEXT("BlueprintPure: no side effects, and the node loses its exec pins")),
		MCPParam::Optional(TEXT("isConst"), EType::Boolean, TEXT("const: the function only reads state")),
		MCPParam::Optional(TEXT("accessSpecifier"), EType::String, TEXT("public, protected or private")),
		MCPParam::Optional(TEXT("category"), EType::String, TEXT("Category")),
		MCPParam::Optional(TEXT("tooltip"), EType::String, TEXT("Tooltip")),
		MCPParam::Optional(TEXT("keywords"), EType::String, TEXT("Extra palette search keywords")),
		MCPParam::Optional(TEXT("compactNodeTitle"), EType::String, TEXT("Render the node in compact form under this title")),
		MCPParam::Optional(TEXT("callInEditor"), EType::Boolean, TEXT("Expose a details-panel button that runs it on a selected instance")),
		MCPParam::Optional(TEXT("threadSafe"), EType::Boolean, TEXT("Safe to call off the game thread")),
		MCPParam::Optional(TEXT("deprecated"), EType::Boolean, TEXT("Mark the function deprecated so its call sites warn")),
		MCPParam::Optional(TEXT("deprecationMessage"), EType::String, TEXT("Text shown at a deprecated function's call sites")),
	});
	Registry.RegisterHandler(TEXT("list_graph_parameters"), &ListGraphParameters, {
		SpecAssetPath,
		MCPParam::Optional(TEXT("functionName"), EType::String, TEXT("Function, macro or dispatcher signature to read; name exactly one of functionName and eventName")),
		MCPParam::Optional(TEXT("eventName"), EType::String, TEXT("Custom event to read")),
		MCPParam::Optional(TEXT("graphName"), EType::String, TEXT("Narrows an eventName search to one graph")),
	});
	Registry.RegisterHandler(TEXT("edit_graph_parameters"), &EditGraphParameters, {
		SpecAssetPath,
		MCPParam::Required(TEXT("op"), EType::String, TEXT("add, remove, rename, set_type, set_default or reorder")),
		MCPParam::Optional(TEXT("functionName"), EType::String, TEXT("Function, macro or dispatcher signature to edit; name exactly one of functionName and eventName")),
		MCPParam::Optional(TEXT("eventName"), EType::String, TEXT("Custom event to edit")),
		MCPParam::Optional(TEXT("graphName"), EType::String, TEXT("Narrows an eventName search to one graph")),
		MCPParam::Optional(TEXT("isOutput"), EType::Boolean, TEXT("Edit the return side (default false)")),
		MCPParam::Optional(TEXT("parameterName"), EType::String, TEXT("Parameter to act on")),
		MCPParam::Optional(TEXT("parameterType"), EType::String, TEXT("Type in the add_variable vocabulary, for add and set_type")),
		MCPParam::Optional(TEXT("newName"), EType::String, TEXT("New name, for rename")),
		SpecDefaultValue,
		SpecOrder,
	});
	Registry.RegisterHandler(TEXT("rename_blueprint_variable"), &RenameBlueprintVariable, {
		SpecAssetPath,
		MCPParam::Required(TEXT("oldName"), EType::String, TEXT("Current name")),
		MCPParam::Required(TEXT("newName"), EType::String, TEXT("New name")),
	});
	Registry.RegisterHandler(TEXT("get_blueprint_variable_metadata"), &GetBlueprintVariableMetadata, {
		SpecAssetPath,
		SpecVarName,
		MCPParam::Optional(TEXT("functionName"), EType::String, TEXT("The function owning a local variable; omit for a member variable")),
	});
	Registry.RegisterHandler(TEXT("set_blueprint_variable_metadata"), &SetBlueprintVariableMetadata, {
		SpecAssetPath,
		SpecVarName,
		MCPParam::Required(TEXT("metadata"), EType::Object, TEXT("{key: 'value'} pairs; a null value removes that key. Unreal stores every metadata value as text")),
		MCPParam::Optional(TEXT("functionName"), EType::String, TEXT("The function owning a local variable; omit for a member variable")),
	});
	Registry.RegisterHandler(TEXT("edit_local_variable"), &EditLocalVariable, {
		SpecAssetPath,
		SpecFunctionName,
		SpecVarName,
		MCPParam::Required(TEXT("op"), EType::String, TEXT("rename, remove, set_type or set_default")),
		MCPParam::Optional(TEXT("newName"), EType::String, TEXT("New name, for rename")),
		SpecVarType,
		SpecDefaultValue,
	});
	Registry.RegisterHandler(TEXT("list_event_dispatchers"), &ListEventDispatchers, {
		SpecAssetPathOrBlueprintPath,
	});
	Registry.RegisterHandler(TEXT("remove_event_dispatcher"), &RemoveEventDispatcher, {
		SpecAssetPathOrBlueprintPath,
		MCPParam::Required(TEXT("name"), EType::String, TEXT("Dispatcher name")),
	});
	Registry.RegisterHandler(TEXT("add_custom_event"), &AddCustomEvent, {
		SpecAssetPath,
		MCPParam::Required(TEXT("eventName"), EType::String, TEXT("Custom event name")),
		SpecGraphName,
		SpecParameters,
		MCPParam::Optional(TEXT("netMode"), EType::String, TEXT("none (default), multicast, server or client")),
		MCPParam::Optional(TEXT("reliable"), EType::Boolean, TEXT("Send the replicated event reliably (default true; ignored when netMode is none)")),
		MCPParam::Optional(TEXT("callInEditor"), EType::Boolean, TEXT("Expose a details-panel button that runs it on a selected instance")),
		SpecPosX,
		SpecPosY,
	});
	Registry.RegisterHandler(TEXT("create_macro"), &CreateMacro, {
		SpecAssetPath,
		MCPParam::Required(TEXT("macroName"), EType::String, TEXT("Macro graph name")),
		MCPParam::Optional(TEXT("inputs"), EType::Array, TEXT("Input parameters [{name, type}]")).Items(EType::Object),
		MCPParam::Optional(TEXT("outputs"), EType::Array, TEXT("Output parameters [{name, type}]")).Items(EType::Object),
		SpecOnConflict,
	});
	Registry.RegisterHandler(TEXT("delete_macro"), &DeleteMacro, {
		SpecAssetPath,
		MCPParam::Required(TEXT("macroName"), EType::String, TEXT("Macro graph name")),
	});
	Registry.RegisterHandler(TEXT("delete_graph"), &DeleteGraph, {
		SpecAssetPath,
		SpecGraphName,
		SpecGraphSelector,
		MCPParam::Optional(TEXT("force"), EType::Boolean, TEXT("Remove a graph still owned by a live node, or an event graph; both are refused without it")),
	});

	// V14 user-type authoring (BlueprintHandlers_UserTypes.cpp). Creation and
	// the coarse entry CRUD stay on the asset category; these cover ordering,
	// entry and field metadata, field defaults, and the whole-definition read.
	Registry.RegisterHandler(TEXT("read_user_defined_enum"), &ReadUserDefinedEnum, {
		SpecEnumPath,
	});
	Registry.RegisterHandler(TEXT("reorder_enum_values"), &ReorderEnumValues, {
		SpecEnumPath,
		MCPParam::Required(TEXT("order"), EType::Array, TEXT("The COMPLETE desired order, never a partial list")),
	});
	Registry.RegisterHandler(TEXT("set_enum_metadata"), &SetEnumMetadata, {
		SpecEnumPath,
		MCPParam::Optional(TEXT("bitflags"), EType::Boolean, TEXT("Mark the enum as a bitflags type")),
		MCPParam::Optional(TEXT("entries"), EType::Array, TEXT("[{name or index, tooltip}] per-enumerator tooltips")).Items(EType::Object),
	});
	Registry.RegisterHandler(TEXT("read_user_defined_struct"), &ReadUserDefinedStruct, {
		SpecStructPath,
	});
	Registry.RegisterHandler(TEXT("set_struct_field_default"), &SetStructFieldDefault, {
		SpecStructPath,
		MCPParam::Required(TEXT("defaultValue"), EType::Any, TEXT("Default value as Unreal export text; an empty string clears it")),
		MCPParam::Optional(TEXT("fieldName"), EType::String, TEXT("Resolve the member by its display or internal name")),
		MCPParam::Optional(TEXT("fieldGuid"), EType::String, TEXT("Resolve the member by its GUID, which is stable across renames")),
	});
	Registry.RegisterHandler(TEXT("reorder_struct_fields"), &ReorderStructFields, {
		SpecStructPath,
		MCPParam::Required(TEXT("order"), EType::Array, TEXT("The COMPLETE desired order, never a partial list")),
	});
	Registry.RegisterHandler(TEXT("edit_struct_metadata"), &EditStructMetadata, {
		SpecStructPath,
		MCPParam::Optional(TEXT("tooltip"), EType::String, TEXT("Tooltip")),
		MCPParam::Optional(TEXT("fields"), EType::Array, TEXT("[{fieldName or fieldGuid, tooltip?, editableOnInstance?, saveGame?, multiLineText?, widget3D?, metadata?}]")).Items(EType::Object),
	});
}

// ---------------------------------------------------------------------------
// Graph summaries, execution flow and dependency reads
// ---------------------------------------------------------------------------

TSharedPtr<FJsonValue> FBlueprintHandlers::ReadBlueprintGraphSummary(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));
	// #560 optional node filters (case-insensitive substring); edges are left
	// complete so a caller can still see what connects to a matched node.
	const FString TitleFilter = OptionalString(Params, TEXT("titleFilter"), TEXT(""));
	const FString ClassFilter = OptionalString(Params, TEXT("classFilter"), TEXT(""));
	const bool bFiltering = !TitleFilter.IsEmpty() || !ClassFilter.IsEmpty();

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph) return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

	// Nodes: id + class + concise title only. No pin defaults, no positions, no comments.
	TArray<TSharedPtr<FJsonValue>> Nodes;
	TArray<TSharedPtr<FJsonValue>> ExecEdges;
	TArray<TSharedPtr<FJsonValue>> DataEdges;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;

		const FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
		bool bIncludeNode = true;
		if (!TitleFilter.IsEmpty() && !NodeTitle.Contains(TitleFilter, ESearchCase::IgnoreCase)) bIncludeNode = false;
		if (!ClassFilter.IsEmpty() && !Node->GetClass()->GetName().Contains(ClassFilter, ESearchCase::IgnoreCase)) bIncludeNode = false;

		if (bIncludeNode)
		{
			TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
			N->SetStringField(TEXT("id"), Node->NodeGuid.ToString(EGuidFormats::Short));
			N->SetStringField(TEXT("class"), Node->GetClass()->GetName());
			N->SetStringField(TEXT("title"), NodeTitle);
			Nodes.Add(MakeShared<FJsonValueObject>(N));
		}

		// Walk output pins only (one edge per connection, no dup).
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output) continue;
			const bool bExec = (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec);
			for (UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (!Linked || !Linked->GetOwningNode()) continue;
				TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
				E->SetStringField(TEXT("from"), Node->NodeGuid.ToString(EGuidFormats::Short));
				E->SetStringField(TEXT("fromPin"), Pin->PinName.ToString());
				E->SetStringField(TEXT("to"), Linked->GetOwningNode()->NodeGuid.ToString(EGuidFormats::Short));
				E->SetStringField(TEXT("toPin"), Linked->PinName.ToString());
				(bExec ? ExecEdges : DataEdges).Add(MakeShared<FJsonValueObject>(E));
			}
		}
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	AnnotateResolvedBlueprint(Result, Blueprint);
	Result->SetStringField(TEXT("graphName"), GraphName);
	// #298: identify graph type so callers can tell ubergraph / construction
	// script / function / macro apart without having to grep node titles.
	{
		FString GraphType = TEXT("Other");
		if (Blueprint->UbergraphPages.Contains(Graph)) GraphType = TEXT("Ubergraph");
		for (UEdGraph* G : Blueprint->FunctionGraphs)
		{
			if (G == Graph) { GraphType = (G->GetFName() == UEdGraphSchema_K2::FN_UserConstructionScript) ? TEXT("ConstructionScript") : TEXT("Function"); break; }
		}
		for (UEdGraph* G : Blueprint->MacroGraphs)        { if (G == Graph) { GraphType = TEXT("Macro"); break; } }
		for (UEdGraph* G : Blueprint->DelegateSignatureGraphs) { if (G == Graph) { GraphType = TEXT("DelegateSignature"); break; } }
		for (UEdGraph* G : Blueprint->IntermediateGeneratedGraphs) { if (G == Graph) { GraphType = TEXT("Intermediate"); break; } }
		if (Graph && Graph->Schema)
		{
			Result->SetStringField(TEXT("schemaClass"), Graph->Schema->GetName());
		}
		Result->SetStringField(TEXT("graphType"), GraphType);
	}
	Result->SetArrayField(TEXT("nodes"), Nodes);
	Result->SetArrayField(TEXT("execEdges"), ExecEdges);
	Result->SetArrayField(TEXT("dataEdges"), DataEdges);
	Result->SetNumberField(TEXT("nodeCount"), Nodes.Num());
	if (bFiltering)
	{
		Result->SetBoolField(TEXT("filtered"), true);
		if (!TitleFilter.IsEmpty()) Result->SetStringField(TEXT("titleFilter"), TitleFilter);
		if (!ClassFilter.IsEmpty()) Result->SetStringField(TEXT("classFilter"), ClassFilter);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::GetBlueprintExecutionFlow(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));
	FString EntryPoint = OptionalString(Params, TEXT("entryPoint"), TEXT(""));

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph) return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

	// Locate entry node. If EntryPoint is given, match by title. Else pick first
	// K2Node_Event / K2Node_FunctionEntry / K2Node_CustomEvent encountered.
	UEdGraphNode* Entry = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;
		const bool bIsEntry =
			Node->IsA<UK2Node_Event>() ||
			Node->IsA<UK2Node_FunctionEntry>() ||
			Node->IsA<UK2Node_CustomEvent>();
		if (!bIsEntry) continue;
		if (EntryPoint.IsEmpty())
		{
			Entry = Node;
			break;
		}
		if (Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Contains(EntryPoint))
		{
			Entry = Node;
			break;
		}
	}

	if (!Entry)
	{
		return MCPError(EntryPoint.IsEmpty()
			? TEXT("No event or function entry node found")
			: FString::Printf(TEXT("Entry node not found: %s"), *EntryPoint));
	}

	// BFS through exec output pins. Track visited node guids to break cycles.
	TArray<TSharedPtr<FJsonValue>> Steps;
	TSet<FGuid> Visited;
	TQueue<UEdGraphNode*> Queue;
	Queue.Enqueue(Entry);

	while (!Queue.IsEmpty())
	{
		UEdGraphNode* Cur = nullptr;
		Queue.Dequeue(Cur);
		if (!Cur || Visited.Contains(Cur->NodeGuid)) continue;
		Visited.Add(Cur->NodeGuid);

		TSharedPtr<FJsonObject> Step = MakeShared<FJsonObject>();
		Step->SetStringField(TEXT("id"), Cur->NodeGuid.ToString(EGuidFormats::Short));
		Step->SetStringField(TEXT("class"), Cur->GetClass()->GetName());
		Step->SetStringField(TEXT("title"), Cur->GetNodeTitle(ENodeTitleType::ListView).ToString());

		// Enumerate exec branches from this node, one per output exec pin.
		TArray<TSharedPtr<FJsonValue>> Branches;
		for (UEdGraphPin* Pin : Cur->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output) continue;
			if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;

			for (UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (!Linked || !Linked->GetOwningNode()) continue;
				UEdGraphNode* Next = Linked->GetOwningNode();

				TSharedPtr<FJsonObject> B = MakeShared<FJsonObject>();
				B->SetStringField(TEXT("pin"), Pin->PinName.ToString());
				B->SetStringField(TEXT("toId"), Next->NodeGuid.ToString(EGuidFormats::Short));
				Branches.Add(MakeShared<FJsonValueObject>(B));

				if (!Visited.Contains(Next->NodeGuid))
				{
					Queue.Enqueue(Next);
				}
			}
		}
		Step->SetArrayField(TEXT("branches"), Branches);
		Steps.Add(MakeShared<FJsonValueObject>(Step));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	AnnotateResolvedBlueprint(Result, Blueprint);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetStringField(TEXT("entryPoint"), Entry->GetNodeTitle(ENodeTitleType::ListView).ToString());
	Result->SetStringField(TEXT("entryId"), Entry->NodeGuid.ToString(EGuidFormats::Short));
	Result->SetArrayField(TEXT("steps"), Steps);
	Result->SetNumberField(TEXT("stepCount"), Steps.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::GetBlueprintDependencies(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	const bool bReverse = OptionalBool(Params, TEXT("reverse"), false);

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = AssetRegistryModule.Get();
	const FName PackageName = Blueprint->GetOutermost()->GetFName();

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetBoolField(TEXT("reverse"), bReverse);

	if (bReverse)
	{
		TArray<FName> Referencers;
		Registry.GetReferencers(PackageName, Referencers, UE::AssetRegistry::EDependencyCategory::Package);
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Reserve(Referencers.Num());
		for (const FName& Ref : Referencers)
		{
			Arr.Add(MakeShared<FJsonValueString>(Ref.ToString()));
		}
		Result->SetArrayField(TEXT("referencers"), Arr);
		Result->SetNumberField(TEXT("referencerCount"), Arr.Num());
		return MCPResult(Result);
	}

	// Forward: asset-level deps from registry + class-level walk.
	TArray<FName> AssetDeps;
	Registry.GetDependencies(PackageName, AssetDeps, UE::AssetRegistry::EDependencyCategory::Package);
	TArray<TSharedPtr<FJsonValue>> AssetArr;
	AssetArr.Reserve(AssetDeps.Num());
	for (const FName& Dep : AssetDeps)
	{
		AssetArr.Add(MakeShared<FJsonValueString>(Dep.ToString()));
	}

	// Classes referenced by variables + function signatures + parent class.
	TSet<FString> Classes;
	if (UClass* ParentClass = Blueprint->ParentClass)
	{
		Classes.Add(ParentClass->GetPathName());
	}
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (UObject* Sub = Var.VarType.PinSubCategoryObject.Get())
		{
			Classes.Add(Sub->GetPathName());
		}
	}

	// Functions called via K2Node_CallFunction across all graphs.
	TSet<FString> Functions;
	auto VisitGraph = [&Functions](UEdGraph* G)
	{
		if (!G) return;
		for (UEdGraphNode* Node : G->Nodes)
		{
			if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
			{
				if (UFunction* Fn = Call->GetTargetFunction())
				{
					Functions.Add(Fn->GetPathName());
				}
			}
		}
	};
	for (UEdGraph* G : Blueprint->UbergraphPages) VisitGraph(G);
	for (UEdGraph* G : Blueprint->FunctionGraphs) VisitGraph(G);

	TArray<TSharedPtr<FJsonValue>> ClassArr;
	for (const FString& C : Classes) ClassArr.Add(MakeShared<FJsonValueString>(C));
	TArray<TSharedPtr<FJsonValue>> FnArr;
	for (const FString& F : Functions) FnArr.Add(MakeShared<FJsonValueString>(F));

	Result->SetArrayField(TEXT("assets"), AssetArr);
	Result->SetArrayField(TEXT("classes"), ClassArr);
	Result->SetArrayField(TEXT("functions"), FnArr);
	Result->SetNumberField(TEXT("assetCount"), AssetArr.Num());
	Result->SetNumberField(TEXT("classCount"), ClassArr.Num());
	Result->SetNumberField(TEXT("functionCount"), FnArr.Num());
	return MCPResult(Result);
}

namespace
{
	// #942: resolve a World/umap path to the level script Blueprint that lives
	// inside it. The level script is a subobject of the persistent level, never
	// an asset of its own, so loading a UBlueprint from "/Game/Maps/SomeLevel"
	// can never find it however the path is spelled.
	//
	// bDontCreate is deliberate. A map that has never had a Level Blueprint
	// opened has no level script object, and a READ must not author one as a
	// side effect: it would dirty the map package and write a new subobject
	// into somebody's level for asking a question about it.
	ULevelScriptBlueprint* ResolveLevelScriptBlueprint(const FString& AssetPath)
	{
		UWorld* World = LoadAssetByPath<UWorld>(AssetPath);
		if (!World || !World->PersistentLevel) return nullptr;
		return World->PersistentLevel->GetLevelScriptBlueprint(/*bDontCreate=*/true);
	}
}

UBlueprint* FBlueprintHandlers::LoadBlueprint(const FString& AssetPath)
{
	if (UBlueprint* Direct = LoadAssetByPath<UBlueprint>(AssetPath))
	{
		return Direct;
	}
	// #942: one resolution point, so every action that reaches a Blueprint
	// through this function accepts a umap path on exactly the same terms.
	return ResolveLevelScriptBlueprint(AssetPath);
}

TSharedPtr<FJsonValue> BlueprintNotFoundError(const FString& AssetPath)
{
	if (UWorld* World = LoadAssetByPath<UWorld>(AssetPath))
	{
		return MCPError(FString::Printf(
			TEXT("'%s' is a World, and its level script Blueprint does not exist yet, so there is nothing to read. Open the map's Level Blueprint in the editor once (that creates it), then retry this call with the same path. When it exists it resolves to %s:PersistentLevel.%s"),
			*AssetPath, *World->GetPathName(), *World->GetName()));
	}
	return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
}

void AnnotateResolvedBlueprint(const TSharedPtr<FJsonObject>& Result, UBlueprint* Blueprint)
{
	if (!Result.IsValid() || !Blueprint) return;
	Result->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
	Result->SetBoolField(TEXT("isLevelScript"), Blueprint->IsA<ULevelScriptBlueprint>());
}

// ---------------------------------------------------------------------------
// list_blueprint_graphs -- List all graphs in a blueprint (EventGraph, AnimGraph, functions, etc.)
// ---------------------------------------------------------------------------
namespace
{
	TSharedPtr<FJsonObject> MakeGraphDescriptor(
		UEdGraph* Graph,
		const TMap<FString, int32>& NameCounts,
		TMap<FString, int32>& SeenCounts)
	{
		const FString Name = Graph->GetName();
		const int32 DuplicateIndex = SeenCounts.FindOrAdd(Name)++;
		const int32 DuplicateCount = NameCounts.FindRef(Name);
		// #945: one selector rule, shared with search_call_sites so the two
		// cannot disagree about how to address the same graph.
		const FString Selector = MakeGraphSelector(Name, DuplicateIndex, DuplicateCount);

		TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
		GraphObj->SetStringField(TEXT("name"), Name);
		GraphObj->SetStringField(TEXT("selector"), Selector);
		GraphObj->SetStringField(TEXT("objectPath"), Graph->GetPathName());
		GraphObj->SetStringField(TEXT("class"), Graph->GetClass()->GetName());
		GraphObj->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());
		GraphObj->SetNumberField(TEXT("duplicateIndex"), DuplicateIndex);
		GraphObj->SetNumberField(TEXT("duplicateCount"), DuplicateCount);
		return GraphObj;
	}
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ListGraphs(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return BlueprintNotFoundError(AssetPath);
	}

	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);

	TMap<FString, int32> NameCounts;
	TMap<FString, int32> SeenCounts;
	CountGraphNames(AllGraphs, NameCounts);

	TArray<TSharedPtr<FJsonValue>> GraphsArray;
	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) continue;
		GraphsArray.Add(MakeShared<FJsonValueObject>(MakeGraphDescriptor(Graph, NameCounts, SeenCounts)));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	AnnotateResolvedBlueprint(Result, Blueprint);
	Result->SetArrayField(TEXT("graphs"), GraphsArray);

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// resolve_blueprint_graph -- Resolve a graph name to selectors accepted by
// read_graph/add_node/connect_pins/etc. Duplicate nested AnimBP graphs commonly
// share names such as "Locomotion" or "Transition"; callers can pass the
// returned selector (for example "Locomotion[3]") back as graphName.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::ResolveGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	FString RequestedName;
	if (auto Err = RequireString(Params, TEXT("graphName"), RequestedName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return BlueprintNotFoundError(AssetPath);
	}

	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);

	TMap<FString, int32> NameCounts;
	CountGraphNames(AllGraphs, NameCounts);

	TArray<UEdGraph*> Matches;
	const int32 LeftBracket = RequestedName.Find(TEXT("["));
	const int32 RightBracket = RequestedName.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	const bool bIndexedSelector = LeftBracket != INDEX_NONE && RightBracket > LeftBracket;

	if (bIndexedSelector)
	{
		if (UEdGraph* Resolved = FindGraph(Blueprint, RequestedName))
		{
			Matches.Add(Resolved);
		}
	}
	else
	{
		for (UEdGraph* Graph : AllGraphs)
		{
			if (Graph && Graph->GetName().Equals(RequestedName, ESearchCase::IgnoreCase))
			{
				Matches.Add(Graph);
			}
		}

		// Preserve the existing object-path/suffix addressing behavior when the
		// request is not a bare graph name.
		if (Matches.IsEmpty())
		{
			if (UEdGraph* Resolved = FindGraph(Blueprint, RequestedName))
			{
				Matches.Add(Resolved);
			}
		}
	}

	TMap<FString, int32> SeenCounts;
	TMap<UEdGraph*, TSharedPtr<FJsonObject>> DescriptorsByGraph;
	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) continue;
		DescriptorsByGraph.Add(Graph, MakeGraphDescriptor(Graph, NameCounts, SeenCounts));
	}

	TArray<TSharedPtr<FJsonValue>> MatchArray;
	for (UEdGraph* Graph : Matches)
	{
		if (const TSharedPtr<FJsonObject>* Descriptor = DescriptorsByGraph.Find(Graph))
		{
			MatchArray.Add(MakeShared<FJsonValueObject>(*Descriptor));
		}
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	AnnotateResolvedBlueprint(Result, Blueprint);
	Result->SetStringField(TEXT("requestedGraphName"), RequestedName);
	Result->SetNumberField(TEXT("matchCount"), MatchArray.Num());
	Result->SetBoolField(TEXT("ambiguous"), MatchArray.Num() > 1);
	Result->SetArrayField(TEXT("matches"), MatchArray);
	if (MatchArray.IsEmpty())
	{
		Result->SetStringField(TEXT("message"), FString::Printf(TEXT("No graph matched: %s"), *RequestedName));
	}
	else if (MatchArray.Num() > 1)
	{
		Result->SetStringField(TEXT("message"), TEXT("Multiple graphs share this name; pass a returned selector as graphName."));
	}
	return MCPResult(Result);
}

FEdGraphPinType FBlueprintHandlers::MakePinType(const FString& TypeStr)
{
	FEdGraphPinType PinType;
	PinType.PinCategory = NAME_None;
	PinType.PinSubCategory = NAME_None;

	FString LowerType = TypeStr.ToLower();

	// (#140) Object-reference types: "Actor", "Actor*", "APawn*", full class paths
	// like "/Script/Engine.Actor", and soft-ref variants "SoftActor" or "SoftClassPtr<Foo>".
	// Previously these fell through to the struct resolver and ultimately defaulted to
	// PC_Real (float), breaking any function parameter that takes an object-ref.
	auto TryResolveObjectPin = [&PinType](const FString& Raw) -> bool
	{
		FString Trimmed = Raw;
		Trimmed.TrimStartAndEndInline();
		// Strip trailing asterisks (AActor*, AActor**)
		while (Trimmed.EndsWith(TEXT("*"))) Trimmed = Trimmed.LeftChop(1);
		Trimmed.TrimStartAndEndInline();

		// SoftClassPtr<Foo> / TSubclassOf<Foo> / TSoftObjectPtr<Foo>
		bool bIsSoftClass = false;
		bool bIsClass = false;
		bool bIsSoftObject = false;
		auto UnwrapTemplate = [&](const TCHAR* Prefix) -> bool
		{
			if (Trimmed.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				int32 Open = Trimmed.Find(TEXT("<"));
				int32 Close = Trimmed.Find(TEXT(">"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
				if (Open != INDEX_NONE && Close != INDEX_NONE && Close > Open)
				{
					Trimmed = Trimmed.Mid(Open + 1, Close - Open - 1).TrimStartAndEnd();
					return true;
				}
			}
			return false;
		};
		if (UnwrapTemplate(TEXT("TSubclassOf"))) bIsClass = true;
		else if (UnwrapTemplate(TEXT("TSoftClassPtr")) || UnwrapTemplate(TEXT("SoftClassPtr"))) bIsSoftClass = true;
		else if (UnwrapTemplate(TEXT("TSoftObjectPtr")) || UnwrapTemplate(TEXT("SoftObjectPtr"))) bIsSoftObject = true;

		UClass* Resolved = nullptr;
		if (Trimmed.Contains(TEXT("/")) || Trimmed.Contains(TEXT(".")))
		{
			Resolved = LoadObject<UClass>(nullptr, *Trimmed);
		}
		if (!Resolved)
		{
			Resolved = MCPResolveClass(Trimmed);
		}
		if (!Resolved) return false;

		if (bIsSoftClass)
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_SoftClass;
		}
		else if (bIsClass)
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Class;
		}
		else if (bIsSoftObject)
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_SoftObject;
		}
		else
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		}
		PinType.PinSubCategoryObject = Resolved;
		return true;
	};

	// (#787) Explicit disambiguating prefixes, the same syntax add_event_dispatcher
	// accepts. Documented for add_variable but never implemented here: the string
	// still contained "object:"/"struct:" when it reached the resolvers below, so
	// every one of them missed and the type came back unresolved.
	if (TypeStr.StartsWith(TEXT("object:"), ESearchCase::IgnoreCase))
	{
		TryResolveObjectPin(TypeStr.Mid(7).TrimStartAndEnd());
		// On failure PinCategory stays NAME_None and the caller reports it; do
		// not fall through to the numeric default, which would silently make a Float.
		return PinType;
	}
	if (TypeStr.StartsWith(TEXT("struct:"), ESearchCase::IgnoreCase))
	{
		FString Inner = TypeStr.Mid(7).TrimStartAndEnd();
		UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *Inner);
		if (!Struct && Inner.StartsWith(TEXT("/")) && !Inner.Contains(TEXT(".")))
		{
			// Asset path without the object suffix: /Game/Foo/S_Bar -> ...S_Bar.S_Bar
			FString AssetName;
			Inner.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			Struct = LoadObject<UScriptStruct>(nullptr, *(Inner + TEXT(".") + AssetName));
		}
		if (!Struct)
		{
			FString ShortName = Inner;
			if (ShortName.Len() > 1 && ShortName[0] == 'F' && FChar::IsUpper(ShortName[1])) ShortName = ShortName.Mid(1);
			for (TObjectIterator<UScriptStruct> It; It; ++It)
			{
				if (It->GetName() == Inner || It->GetName() == ShortName) { Struct = *It; break; }
			}
		}
		if (Struct)
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			PinType.PinSubCategoryObject = Struct;
		}
		return PinType;
	}

	// If the caller passed an asterisk or a class path, treat as object-ref first.
	if (TypeStr.Contains(TEXT("*")) || TypeStr.Contains(TEXT("/")))
	{
		if (TryResolveObjectPin(TypeStr)) return PinType;
	}

	// Map type strings to pin categories
	if (LowerType == TEXT("bool") || LowerType == TEXT("boolean"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}
	else if (LowerType == TEXT("int") || LowerType == TEXT("integer") || LowerType == TEXT("int32"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	else if (LowerType == TEXT("int64"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
	}
	else if (LowerType == TEXT("float") || LowerType == TEXT("double") || LowerType == TEXT("real"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
	}
	else if (LowerType == TEXT("string") || LowerType == TEXT("str"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_String;
	}
	else if (LowerType == TEXT("name"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	}
	else if (LowerType == TEXT("text"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
	}
	else if (LowerType == TEXT("object"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
	}
	else if (LowerType == TEXT("class"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Class;
	}
	else if (LowerType == TEXT("softobject") || LowerType == TEXT("softobjectreference"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_SoftObject;
	}
	else if (LowerType == TEXT("softclass") || LowerType == TEXT("softclassreference"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_SoftClass;
	}
	else if (LowerType == TEXT("byte"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
	}
	else if (LowerType == TEXT("enum"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
	}
	// (#428) Explicit enum reference: "enum:/Game/Path/E_Foo[.E_Foo]" or
	// "enum:/Script/Module.EEnumName". Used for user-defined enums where the
	// short-name resolver can't reach them.
	else if (TypeStr.StartsWith(TEXT("enum:")))
	{
		FString EnumPath = TypeStr.Mid(5);
		EnumPath.TrimStartAndEndInline();
		UEnum* Enum = LoadObject<UEnum>(nullptr, *EnumPath);
		if (!Enum && !EnumPath.Contains(TEXT(".")))
		{
			// Try object-path form ("/Game/Foo/Bar" -> "/Game/Foo/Bar.Bar")
			FString AssetName;
			EnumPath.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			Enum = LoadObject<UEnum>(nullptr, *(EnumPath + TEXT(".") + AssetName));
		}
		if (Enum)
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			PinType.PinSubCategoryObject = Enum;
		}
	}
	// (#286) Resolve named enums by full path (/Script/Module.EEnumName) or
	// short name (EMyEnum / E_MyEnum). UE pin types for enums use PC_Byte with
	// PinSubCategoryObject = UEnum*.
	else if (TypeStr.StartsWith(TEXT("/Script/")) && TypeStr.Contains(TEXT(".")))
	{
		if (UEnum* Enum = LoadObject<UEnum>(nullptr, *TypeStr))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			PinType.PinSubCategoryObject = Enum;
		}
		// fall through to default-handling below if it's not actually an enum;
		// LoadObject returning nullptr leaves PinCategory NAME_None which the
		// next branch can still try as a struct or class.
		else if (TryResolveObjectPin(TypeStr))
		{
			// resolved as object/class
		}
	}
	// (#428) Bare /Game/... path - assume user-defined enum.
	else if (TypeStr.StartsWith(TEXT("/Game/")))
	{
		FString EnumPath = TypeStr;
		UEnum* Enum = LoadObject<UEnum>(nullptr, *EnumPath);
		if (!Enum && !EnumPath.Contains(TEXT(".")))
		{
			FString AssetName;
			EnumPath.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			Enum = LoadObject<UEnum>(nullptr, *(EnumPath + TEXT(".") + AssetName));
		}
		if (Enum)
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			PinType.PinSubCategoryObject = Enum;
		}
	}
	else
	{
		// Try short-name enum lookup before the struct resolver - many engine
		// enums (EAttachmentRule, EMovementMode) match the convention E* but
		// would otherwise fall through and return an empty PinType. (#286)
		auto TryResolveEnumShort = [&](const FString& Name) -> UEnum*
		{
			if (Name.Len() < 2) return nullptr;
			if (Name[0] != 'E') return nullptr;
			for (TObjectIterator<UEnum> It; It; ++It)
			{
				if (It->GetName() == Name) return *It;
			}
			return nullptr;
		};
		if (UEnum* ShortEnum = TryResolveEnumShort(TypeStr))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			PinType.PinSubCategoryObject = ShortEnum;
			return PinType;
		}

		// Try to resolve as a struct type (FVector, FRotator, FTransform, FLinearColor, FGameplayTag, etc.)
		// Strip leading 'F' for lookup if present
		FString StructName = TypeStr;
		static const TMap<FString, FString> StructAliases = {
			{ TEXT("vector"),       TEXT("Vector") },
			{ TEXT("fvector"),      TEXT("Vector") },
			{ TEXT("rotator"),      TEXT("Rotator") },
			{ TEXT("frotator"),     TEXT("Rotator") },
			{ TEXT("transform"),    TEXT("Transform") },
			{ TEXT("ftransform"),   TEXT("Transform") },
			{ TEXT("linearcolor"),  TEXT("LinearColor") },
			{ TEXT("flinearcolor"), TEXT("LinearColor") },
			{ TEXT("color"),        TEXT("Color") },
			{ TEXT("fcolor"),       TEXT("Color") },
			{ TEXT("vector2d"),     TEXT("Vector2D") },
			{ TEXT("fvector2d"),    TEXT("Vector2D") },
			{ TEXT("gameplaytag"),      TEXT("GameplayTag") },
			{ TEXT("fgameplaytag"),     TEXT("GameplayTag") },
			{ TEXT("gameplaytagcontainer"), TEXT("GameplayTagContainer") },
			{ TEXT("fgameplaytagcontainer"), TEXT("GameplayTagContainer") },
		};

		const FString* Alias = StructAliases.Find(LowerType);
		if (Alias)
		{
			StructName = *Alias;
		}
		else if (StructName.Len() > 1 && StructName[0] == 'F' && FChar::IsUpper(StructName[1]))
		{
			StructName = StructName.Mid(1);
		}

		UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *(FString(TEXT("/Script/CoreUObject.")) + StructName));
		if (!Struct)
		{
			Struct = FindObject<UScriptStruct>(nullptr, *(FString(TEXT("/Script/GameplayTags.")) + StructName));
		}
		if (!Struct)
		{
			// Broad search
			for (TObjectIterator<UScriptStruct> It; It; ++It)
			{
				if (It->GetName() == StructName)
				{
					Struct = *It;
					break;
				}
			}
		}

		if (Struct)
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			PinType.PinSubCategoryObject = Struct;
		}
		else if (TryResolveObjectPin(TypeStr))
		{
			// (#140) Last-ditch: treat as a bare class name (e.g. "Actor", "Pawn", "PlayerController").
		}
		// else: PinCategory remains NAME_None - caller must check for unresolved type (#181)
	}

	return PinType;
}

UEdGraph* FBlueprintHandlers::FindGraph(UBlueprint* Blueprint, const FString& GraphName)
{
	if (!Blueprint) return nullptr;

	// Search ALL graphs (UbergraphPages, FunctionGraphs, AnimGraphs, etc.)
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);

	// #209: state-pair addressing "Idle to Resting" / "Idle->Resting" for
	// AnimBP transition condition graphs. The internal graph name is always
	// "Transition" so callers couldn't target a specific transition by name.
	auto SplitStatePair = [](const FString& In, FString& OutFrom, FString& OutTo) -> bool
	{
		const TCHAR* Seps[] = { TEXT(" to "), TEXT("->"), TEXT("→"), TEXT(" -> ") };
		for (const TCHAR* Sep : Seps)
		{
			int32 At = In.Find(Sep);
			if (At != INDEX_NONE)
			{
				OutFrom = In.Left(At).TrimStartAndEnd();
				OutTo = In.Mid(At + FCString::Strlen(Sep)).TrimStartAndEnd();
				return !OutFrom.IsEmpty() && !OutTo.IsEmpty();
			}
		}
		return false;
	};
	FString FromState, ToState;
	if (SplitStatePair(GraphName, FromState, ToState))
	{
		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph) continue;
			if (UAnimStateTransitionNode* Trans = Cast<UAnimStateTransitionNode>(Graph->GetOuter()))
			{
				const FString PrevName = Trans->GetPreviousState() ? Trans->GetPreviousState()->GetStateName() : FString();
				const FString NextName = Trans->GetNextState() ? Trans->GetNextState()->GetStateName() : FString();
				if (PrevName.Equals(FromState, ESearchCase::IgnoreCase) && NextName.Equals(ToState, ESearchCase::IgnoreCase))
				{
					return Graph;
				}
			}
		}
	}

	// #119: support indexed addressing "Transition[4]" for disambiguating the N'th graph
	// with that name (AnimBP state-machine transition graphs all share name "Transition")
	FString BaseName = GraphName;
	int32 Index = -1;
	int32 LB = GraphName.Find(TEXT("["));
	int32 RB = GraphName.Find(TEXT("]"));
	if (LB != INDEX_NONE && RB != INDEX_NONE && RB > LB)
	{
		BaseName = GraphName.Left(LB);
		FString IdxStr = GraphName.Mid(LB + 1, RB - LB - 1);
		Index = FCString::Atoi(*IdxStr);
	}

	int32 Matched = 0;
	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph && Graph->GetName() == BaseName)
		{
			if (Index < 0) return Graph;
			if (Matched == Index) return Graph;
			Matched++;
		}
	}

	// Also support object-path addressing "Outer.Graph" by matching suffix
	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph && Graph->GetPathName().EndsWith(TEXT(".") + GraphName))
		{
			return Graph;
		}
	}
	return nullptr;
}

UEdGraphNode* FBlueprintHandlers::FindNodeByGuidOrName(UEdGraph* Graph, const FString& NodeId)
{
	if (!Graph) return nullptr;

	// Try to parse as GUID first
	FGuid SearchGuid;
	if (FGuid::Parse(NodeId, SearchGuid))
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == SearchGuid)
			{
				return Node;
			}
		}
	}

	// Fallback: search by name/title
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;
		if (Node->GetName() == NodeId)
		{
			return Node;
		}
		if (Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString() == NodeId)
		{
			return Node;
		}
	}

	return nullptr;
}

namespace
{
	const TCHAR* const CreateBlueprintPathHelp =
		TEXT("Expected an Unreal package path such as '/Game/Blueprints/BP_Example'. ")
		TEXT("A '.uasset' suffix, an object suffix such as '.BP_Example', and backslashes are accepted and normalized. ")
		TEXT("A filesystem path or a path that does not start with '/' is not.");

	/** #798: fold the spellings a caller uses for a create destination (a .uasset
	 *  suffix, an object suffix, backslashes, a {refPath} reference serialized as
	 *  JSON) into a long package path, and refuse what cannot be repaired
	 *  without guessing. */
	TSharedPtr<FJsonValue> NormalizeCreateBlueprintDestination(const FString& Raw, const TCHAR* Field, FString& OutPath)
	{
		FString Value = Raw.TrimStartAndEnd();
		if (Value.StartsWith(TEXT("{")))
		{
			TSharedPtr<FJsonObject> Reference;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Value);
			if (FJsonSerializer::Deserialize(Reader, Reference) && Reference.IsValid())
			{
				for (const TCHAR* Key : { TEXT("refPath"), TEXT("assetPath"), TEXT("path") })
				{
					FString Inner;
					if (Reference->TryGetStringField(Key, Inner) && !Inner.TrimStartAndEnd().IsEmpty())
					{
						Value = Inner.TrimStartAndEnd();
						break;
					}
				}
			}
		}

		Value.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (Value.IsEmpty())
		{
			return MCPError(FString::Printf(TEXT("%s must not be empty. %s"), Field, CreateBlueprintPathHelp));
		}
		const bool bDrivePath = Value.Len() >= 3 && FChar::IsAlpha(Value[0]) && Value[1] == TEXT(':') && Value[2] == TEXT('/');
		if (bDrivePath || Value.StartsWith(TEXT("file:")))
		{
			return MCPError(FString::Printf(TEXT("%s '%s' is a filesystem path. %s"), Field, *Raw, CreateBlueprintPathHelp));
		}

		while (Value.Contains(TEXT("//")))
		{
			Value.ReplaceInline(TEXT("//"), TEXT("/"));
		}
		while (Value.Len() > 1 && Value.EndsWith(TEXT("/")))
		{
			Value = Value.LeftChop(1);
		}
		if (!Value.StartsWith(TEXT("/")))
		{
			return MCPError(FString::Printf(TEXT("%s '%s' is not a mount-rooted path. %s"), Field, *Raw, CreateBlueprintPathHelp));
		}

		// Everything from the first dot of the last segment is an extension or an
		// object suffix; the bridge addresses the package.
		int32 Cut = INDEX_NONE;
		Value.FindLastChar(TEXT('/'), Cut);
		const FString Dir = Value.Left(Cut);
		FString Leaf = Value.Mid(Cut + 1);
		int32 Dot = INDEX_NONE;
		if (Leaf.FindChar(TEXT('.'), Dot))
		{
			Leaf = Leaf.Left(Dot);
		}
		if (Leaf.IsEmpty())
		{
			return MCPError(FString::Printf(TEXT("%s '%s' has no asset name. %s"), Field, *Raw, CreateBlueprintPathHelp));
		}

		const FString Normalized = Dir + TEXT("/") + Leaf;
		TArray<FString> Segments;
		Normalized.ParseIntoArray(Segments, TEXT("/"), true);
		if (Segments.Num() < 2)
		{
			return MCPError(FString::Printf(TEXT("%s '%s' is missing a mount point. %s"), Field, *Raw, CreateBlueprintPathHelp));
		}
		OutPath = Normalized;
		return nullptr;
	}
}

TSharedPtr<FJsonValue> FBlueprintHandlers::CreateBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	// The destination is assetPath ('path' is its alias) or a name plus
	// packagePath pair. Everything is read before anything can fail (#1057).
	const FString RawAssetPath = OptionalString(Params, TEXT("assetPath")).TrimStartAndEnd();
	const FString RawName = OptionalString(Params, TEXT("name")).TrimStartAndEnd();
	const FString RawPackagePath = OptionalString(Params, TEXT("packagePath")).TrimStartAndEnd();
	const FString ParentClassName = OptionalString(Params, TEXT("parentClass"), TEXT("Actor"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	FString AssetPath;
	if (!RawAssetPath.IsEmpty())
	{
		if (auto Err = NormalizeCreateBlueprintDestination(RawAssetPath, TEXT("assetPath"), AssetPath)) return Err;
	}
	else if (!RawName.IsEmpty() && !RawPackagePath.IsEmpty())
	{
		if (auto Err = NormalizeCreateBlueprintDestination(RawPackagePath + TEXT("/") + RawName, TEXT("packagePath + name"), AssetPath)) return Err;
	}
	else
	{
		return MCPError(FString::Printf(
			TEXT("Missing required parameter 'assetPath'. %s A 'name' plus 'packagePath' pair is accepted as the same thing."),
			CreateBlueprintPathHelp));
	}

	// Find parent class -- try multiple resolution strategies
	UClass* ParentClass = nullptr;

	// 1. Try silent short-name search first (handles "Actor", "AActor", "UAnimInstance" etc.)
	ParentClass = MCPResolveClass(ParentClassName);

	// 2. Try as full class path (e.g. "/Script/Engine.Actor" or "/Script/MyModule.MyClass")
	if (!ParentClass)
	{
		ParentClass = LoadObject<UClass>(nullptr, *ParentClassName);
	}

	if (!ParentClass)
	{
		return MCPError(FString::Printf(
			TEXT("Parent class not found: '%s'. Try the full path (e.g. '/Script/Engine.Actor') or the class name without prefix (e.g. 'Actor', 'Pawn', 'Character')."),
			*ParentClassName));
	}

	// Create blueprint
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	FString PackageName;
	FString AssetName;
	AssetPath.Split(TEXT("/"), &PackageName, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);

	// Idempotent: if asset already exists, return it.
	UBlueprint* ExistingBP = LoadBlueprint(AssetPath);
	if (ExistingBP)
	{
		if (OnConflict == TEXT("error"))
		{
			return MCPError(FString::Printf(TEXT("Blueprint '%s' already exists"), *AssetPath));
		}
		FString ObjectPath = ExistingBP->GetPathName();
		auto Result = MCPSuccess();
		MCPSetExisted(Result);
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("objectPath"), ObjectPath);
		Result->SetStringField(TEXT("className"), ExistingBP->GetName());
		if (ExistingBP->ParentClass)
		{
			Result->SetStringField(TEXT("parentClass"), ExistingBP->ParentClass->GetPathName());
		}
		return MCPResult(Result);
	}

	UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
	BlueprintFactory->ParentClass = ParentClass;
	UBlueprint* NewBlueprint = Cast<UBlueprint>(AssetTools.CreateAsset(AssetName, PackageName, UBlueprint::StaticClass(), BlueprintFactory));
	if (!NewBlueprint)
	{
		return MCPError(TEXT("Failed to create Blueprint"));
	}

	FKismetEditorUtilities::CompileBlueprint(NewBlueprint);

	const FString ObjectPath = NewBlueprint->GetPathName();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("objectPath"), ObjectPath);
	Result->SetStringField(TEXT("className"), NewBlueprint->GetName());
	Result->SetStringField(TEXT("parentClass"), ParentClass->GetPathName());

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), ObjectPath);
	MCPSetRollback(Result, TEXT("delete_asset"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ReadBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	// #353/#370: per-component property dump on demand. Off by default so the
	// common read stays small; flip on when the caller wants the full UPROPERTY
	// values from each component template (e.g. AIPerceptionStimuliSourceComponent's
	// bAutoRegisterAsSource for a read-then-modify flow). Read before the load
	// can fail (#1057).
	const bool bIncludeComponentProperties = OptionalBool(Params, TEXT("includeComponentProperties"));

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return BlueprintNotFoundError(AssetPath);
	}

	auto AppendComponentProperties = [&bIncludeComponentProperties](TSharedPtr<FJsonObject> CompObj, UActorComponent* Template)
	{
		if (!bIncludeComponentProperties || !Template) return;
		TArray<TSharedPtr<FJsonValue>> Props;
		for (TFieldIterator<FProperty> PIt(Template->GetClass()); PIt; ++PIt)
		{
			FProperty* Prop = *PIt;
			if (!Prop) continue;
			if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient)) continue;
			TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("name"), Prop->GetName());
			P->SetStringField(TEXT("type"), Prop->GetCPPType());
			FString ValueStr;
			const void* VP = Prop->ContainerPtrToValuePtr<void>(Template);
			Prop->ExportText_Direct(ValueStr, VP, VP, Template, PPF_None);
			P->SetStringField(TEXT("value"), ValueStr);
			Props.Add(MakeShared<FJsonValueObject>(P));
		}
		CompObj->SetArrayField(TEXT("properties"), Props);
	};

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	AnnotateResolvedBlueprint(Result, Blueprint);
	Result->SetStringField(TEXT("className"), Blueprint->GetName());
	if (Blueprint->ParentClass)
	{
		Result->SetStringField(TEXT("parentClass"), Blueprint->ParentClass->GetName());
	}

	// Enumerate SCS components
	TArray<TSharedPtr<FJsonValue>> ComponentsArray;
	if (USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript)
	{
		// Build child->parent map from the tree
		TMap<USCS_Node*, USCS_Node*> ParentMap;
		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			if (!Node) continue;
			for (USCS_Node* Child : Node->ChildNodes)
			{
				if (Child) ParentMap.Add(Child, Node);
			}
		}

		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			if (!Node || !Node->ComponentTemplate) continue;

			UActorComponent* Template = Node->ComponentTemplate;
			TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
			CompObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
			CompObj->SetStringField(TEXT("class"), Template->GetClass()->GetName());

			// Parent component
			if (USCS_Node** ParentPtr = ParentMap.Find(Node))
			{
				CompObj->SetStringField(TEXT("parent"), (*ParentPtr)->GetVariableName().ToString());
			}

			// Transform for SceneComponents
			if (USceneComponent* SceneComp = Cast<USceneComponent>(Template))
			{
				TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
				Loc->SetNumberField(TEXT("x"), SceneComp->GetRelativeLocation().X);
				Loc->SetNumberField(TEXT("y"), SceneComp->GetRelativeLocation().Y);
				Loc->SetNumberField(TEXT("z"), SceneComp->GetRelativeLocation().Z);
				CompObj->SetObjectField(TEXT("relativeLocation"), Loc);

				TSharedPtr<FJsonObject> Rot = MakeShared<FJsonObject>();
				Rot->SetNumberField(TEXT("pitch"), SceneComp->GetRelativeRotation().Pitch);
				Rot->SetNumberField(TEXT("yaw"), SceneComp->GetRelativeRotation().Yaw);
				Rot->SetNumberField(TEXT("roll"), SceneComp->GetRelativeRotation().Roll);
				CompObj->SetObjectField(TEXT("relativeRotation"), Rot);

				TSharedPtr<FJsonObject> Scale = MakeShared<FJsonObject>();
				Scale->SetNumberField(TEXT("x"), SceneComp->GetRelativeScale3D().X);
				Scale->SetNumberField(TEXT("y"), SceneComp->GetRelativeScale3D().Y);
				Scale->SetNumberField(TEXT("z"), SceneComp->GetRelativeScale3D().Z);
				CompObj->SetObjectField(TEXT("relativeScale3D"), Scale);
			}

			// StaticMesh info
			if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Template))
			{
				if (UStaticMesh* Mesh = SMC->GetStaticMesh())
				{
					CompObj->SetStringField(TEXT("staticMesh"), Mesh->GetPathName());
				}
				// Material overrides
				TArray<TSharedPtr<FJsonValue>> Mats;
				for (int32 i = 0; i < SMC->GetNumMaterials(); i++)
				{
					if (UMaterialInterface* Mat = SMC->GetMaterial(i))
					{
						Mats.Add(MakeShared<FJsonValueString>(Mat->GetPathName()));
					}
					else
					{
						Mats.Add(MakeShared<FJsonValueNull>());
					}
				}
				if (Mats.Num() > 0)
				{
					CompObj->SetArrayField(TEXT("materials"), Mats);
				}
			}

			// SkeletalMesh info
			if (USkeletalMeshComponent* SkMC = Cast<USkeletalMeshComponent>(Template))
			{
				if (USkeletalMesh* Mesh = SkMC->GetSkeletalMeshAsset())
				{
					CompObj->SetStringField(TEXT("skeletalMesh"), Mesh->GetPathName());
				}
				TArray<TSharedPtr<FJsonValue>> Mats;
				for (int32 i = 0; i < SkMC->GetNumMaterials(); i++)
				{
					if (UMaterialInterface* Mat = SkMC->GetMaterial(i))
					{
						Mats.Add(MakeShared<FJsonValueString>(Mat->GetPathName()));
					}
					else
					{
						Mats.Add(MakeShared<FJsonValueNull>());
					}
				}
				if (Mats.Num() > 0)
				{
					CompObj->SetArrayField(TEXT("materials"), Mats);
				}
			}

			AppendComponentProperties(CompObj, Template);
			ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
		}
	}

	// #353: inherited native components (e.g. CharacterMesh0, CharMoveComp on
	// ACharacter) live on the parent class' CDO, not in the BP's SCS. Walk the
	// generated class' default subobjects so the response covers the full
	// effective component list, not just the BP-authored slice.
	if (UClass* GenClass = Blueprint->GeneratedClass)
	{
		if (AActor* CDOActor = Cast<AActor>(GenClass->GetDefaultObject()))
		{
			TArray<UActorComponent*> AllComps;
			CDOActor->GetComponents(AllComps);
			for (UActorComponent* Comp : AllComps)
			{
				if (!Comp) continue;
				// Skip components that came from the SCS (already emitted above).
				if (Comp->CreationMethod == EComponentCreationMethod::SimpleConstructionScript) continue;
				TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
				CompObj->SetStringField(TEXT("name"), Comp->GetName());
				CompObj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
				CompObj->SetStringField(TEXT("origin"), TEXT("native"));
				if (USceneComponent* SC = Cast<USceneComponent>(Comp))
				{
					TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
					Loc->SetNumberField(TEXT("x"), SC->GetRelativeLocation().X);
					Loc->SetNumberField(TEXT("y"), SC->GetRelativeLocation().Y);
					Loc->SetNumberField(TEXT("z"), SC->GetRelativeLocation().Z);
					CompObj->SetObjectField(TEXT("relativeLocation"), Loc);
				}
				AppendComponentProperties(CompObj, Comp);
				ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
			}
		}
	}
	Result->SetArrayField(TEXT("components"), ComponentsArray);

	// #116: expose actor tick settings from the CDO
	if (Blueprint->GeneratedClass)
	{
		if (AActor* CDOActor = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject(false)))
		{
			TSharedPtr<FJsonObject> TickObj = MakeShared<FJsonObject>();
			TickObj->SetBoolField(TEXT("bCanEverTick"), CDOActor->PrimaryActorTick.bCanEverTick);
			TickObj->SetBoolField(TEXT("bStartWithTickEnabled"), CDOActor->PrimaryActorTick.bStartWithTickEnabled);
			TickObj->SetNumberField(TEXT("TickInterval"), CDOActor->PrimaryActorTick.TickInterval);
			Result->SetObjectField(TEXT("actorTick"), TickObj);
		}
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::AddVariable(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	FString VarName;
	if (auto Err = RequireString(Params, TEXT("name"), VarName)) return Err;

	FString VarType = OptionalString(Params, TEXT("varType"), TEXT("Float"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return BlueprintNotFoundError(AssetPath);
	}

	// Idempotency: if the variable already exists on the blueprint, short-circuit.
	const FName VarNameFName(*VarName);
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == VarNameFName)
		{
			if (OnConflict == TEXT("error"))
			{
				return MCPError(FString::Printf(TEXT("Variable '%s' already exists"), *VarName));
			}
			auto Existing = MCPSuccess();
			MCPSetExisted(Existing);
			Existing->SetStringField(TEXT("path"), AssetPath);
			Existing->SetStringField(TEXT("variableName"), VarName);
			return MCPResult(Existing);
		}
	}

	// Containers (int[], set<Name>, map<enum:/Game/E_Foo.E_Foo,float>) share
	// the vocabulary edit_graph_parameters and list_graph_parameters use.
	FEdGraphPinType PinType;
	FString TypeError;
	if (!ParsePinTypeSpec(VarType, PinType, TypeError))
	{
		return MCPError(FString::Printf(TEXT("Unrecognized variable type: %s"), *TypeError));
	}

	bool bSuccess = FBlueprintEditorUtils::AddMemberVariable(Blueprint, VarNameFName, PinType);

	if (bSuccess)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		SaveAssetPackage(Blueprint);

		auto Result = MCPSuccess();
		MCPSetCreated(Result);
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("variableName"), VarName);
		Result->SetStringField(TEXT("variableType"), VarType);
		bool bTypeRoundTrips = true;
		Result->SetStringField(TEXT("resolvedType"), PinTypeSpec(PinType, bTypeRoundTrips));

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("path"), AssetPath);
		// delete_variable reads the variable name from `name`. The `variableName`
		// key below is what this handler REPORTS, and a payload carrying only
		// that spelling made the inverse fail its own required-parameter check
		// every time a flow tried to replay it.
		Payload->SetStringField(TEXT("name"), VarName);
		Payload->SetStringField(TEXT("variableName"), VarName);
		MCPSetRollback(Result, TEXT("delete_variable"), Payload);

		return MCPResult(Result);
	}
	else
	{
		return MCPError(TEXT("Failed to add variable - FBlueprintEditorUtils::AddMemberVariable returned false"));
	}
}

TSharedPtr<FJsonValue> FBlueprintHandlers::AddComponent(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	FString ComponentClass;
	if (auto Err = RequireString(Params, TEXT("componentClass"), ComponentClass)) return Err;

	FString ComponentName = OptionalString(Params, TEXT("componentName"), ComponentClass);
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	// #115: optional parentComponent - makes this component a child in the SCS
	// hierarchy. #526: childActorClass sets a ChildActorComponent's class in the
	// same call. Both are read before the load can fail (#1057).
	const FString ParentComponent = OptionalString(Params, TEXT("parentComponent"));
	const FString ChildActorClassPath = OptionalString(Params, TEXT("childActorClass"));

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return BlueprintNotFoundError(AssetPath);
	}

	// Idempotency: existing SCS component with same name short-circuits.
	if (USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			if (Node && Node->GetVariableName().ToString() == ComponentName)
			{
				if (OnConflict == TEXT("error"))
				{
					return MCPError(FString::Printf(TEXT("Component '%s' already exists"), *ComponentName));
				}
				auto Existing = MCPSuccess();
				MCPSetExisted(Existing);
				Existing->SetStringField(TEXT("path"), AssetPath);
				Existing->SetStringField(TEXT("componentName"), ComponentName);
				Existing->SetStringField(TEXT("componentClass"), ComponentClass);
				return MCPResult(Existing);
			}
		}
	}

	// Find component class: accept full paths, short names ("StaticMeshComponent"),
	// short names with U prefix, and engine-module implicit resolution.
	// (#136, #137) Previously only literal FindObject + "U"+name worked, so standard
	// engine components like SceneComponent/SphereComponent/NiagaraComponent failed.
	UClass* CompClass = nullptr;
	if (ComponentClass.Contains(TEXT("/")) || ComponentClass.Contains(TEXT(".")))
	{
		CompClass = LoadObject<UClass>(nullptr, *ComponentClass);
	}
	if (!CompClass)
	{
		CompClass = MCPResolveClass(ComponentClass);
	}
	if (!CompClass)
	{
		CompClass = LoadObject<UClass>(nullptr, *(FString(TEXT("/Script/Engine.")) + ComponentClass));
	}

	if (!CompClass)
	{
		return MCPError(FString::Printf(TEXT("Component class not found: %s. Try the short name (e.g. 'StaticMeshComponent') or the full path ('/Script/Engine.StaticMeshComponent')."), *ComponentClass));
	}

	// Try using SubobjectDataSubsystem (UE5 method)
	bool bSuccess = false;
	if (USubobjectDataSubsystem* Subsystem = GEngine->GetEngineSubsystem<USubobjectDataSubsystem>())
	{
		// Get blueprint handles using K2 function
		TArray<FSubobjectDataHandle> Handles;
		Subsystem->K2_GatherSubobjectDataForBlueprint(Blueprint, Handles);
		if (Handles.Num() > 0)
		{
			FSubobjectDataHandle RootHandle = Handles[0];

			// Resolve parentComponent to its handle if specified
			if (!ParentComponent.IsEmpty())
			{
				for (const FSubobjectDataHandle& H : Handles)
				{
					if (const FSubobjectData* Data = H.GetData())
					{
						if (UObject* Obj = const_cast<UObject*>(Data->GetObject()))
						{
							if (Obj->GetName() == ParentComponent || Obj->GetName().StartsWith(ParentComponent))
							{
								RootHandle = H;
								break;
							}
						}
					}
				}
			}

			FAddNewSubobjectParams AddParams;
			AddParams.ParentHandle = RootHandle;
			AddParams.NewClass = CompClass;
			AddParams.BlueprintContext = Blueprint;

			FText FailReason;
			FSubobjectDataHandle NewHandle = Subsystem->AddNewSubobject(AddParams, FailReason);
			if (NewHandle.IsValid())
			{
				// Rename if needed
				if (ComponentName != ComponentClass)
				{
					Subsystem->RenameSubobject(NewHandle, FText::FromString(ComponentName));
				}

				// #526: when adding a ChildActorComponent, let callers set its
				// ChildActorClass in the same call. Accepts a Blueprint asset path
				// (with or without the _C generated-class suffix) or a C++ class.
				if (!ChildActorClassPath.IsEmpty())
				{
					if (const FSubobjectData* NewData = NewHandle.GetData())
					{
						if (UChildActorComponent* CAC = Cast<UChildActorComponent>(const_cast<UObject*>(NewData->GetObject())))
						{
							UClass* ChildCls = LoadClass<AActor>(nullptr, *ChildActorClassPath);
							if (!ChildCls && !ChildActorClassPath.EndsWith(TEXT("_C")))
							{
								ChildCls = LoadClass<AActor>(nullptr, *(ChildActorClassPath + TEXT("_C")));
							}
							if (!ChildCls)
							{
								if (UBlueprint* ChildBP = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *ChildActorClassPath)))
								{
									ChildCls = ChildBP->GeneratedClass;
								}
							}
							if (ChildCls)
							{
								CAC->Modify();
								CAC->SetChildActorClass(ChildCls);
							}
						}
					}
				}

				bSuccess = true;
			}
		}
	}

	if (bSuccess)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		// Save asset
		SaveAssetPackage(Blueprint);

		auto Result = MCPSuccess();
		MCPSetCreated(Result);
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("componentClass"), ComponentClass);
		Result->SetStringField(TEXT("componentName"), ComponentName);

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("path"), AssetPath);
		Payload->SetStringField(TEXT("componentName"), ComponentName);
		MCPSetRollback(Result, TEXT("remove_component"), Payload);

		return MCPResult(Result);
	}
	else
	{
		return MCPError(TEXT("Failed to add component via SubobjectDataSubsystem"));
	}
}
TSharedPtr<FJsonValue> FBlueprintHandlers::CompileBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return BlueprintNotFoundError(AssetPath);
	}

	// Asked before the compile, because afterwards every Blueprint reads as up
	// to date. BS_UpToDateWithWarnings is a DISTINCT status from BS_UpToDate,
	// so a Blueprint that compiles clean with warnings has to be counted here
	// too or it reports itself stale forever.
	const bool bWasUpToDate =
		Blueprint->Status == EBlueprintStatus::BS_UpToDate
		|| Blueprint->Status == EBlueprintStatus::BS_UpToDateWithWarnings;

	// #703: capture the compiler log and report real status instead of always
	// returning success. Mirrors the batch compile_blueprints path.
	FCompilerResultsLog CompileLog;
	CompileLog.SetSourcePath(Blueprint->GetPathName());
	CompileLog.BeginEvent(TEXT("Compile"));
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &CompileLog);
	CompileLog.EndEvent();

	TArray<TSharedPtr<FJsonValue>> Messages;
	for (const TSharedRef<FTokenizedMessage>& Msg : CompileLog.Messages)
	{
		Messages.Add(MakeShared<FJsonValueString>(Msg->ToText().ToString()));
	}

	const bool bCompiled = CompileLog.NumErrors == 0 &&
		Blueprint->Status != EBlueprintStatus::BS_Error;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetBoolField(TEXT("compiled"), bCompiled);
	Result->SetNumberField(TEXT("errors"), CompileLog.NumErrors);
	Result->SetNumberField(TEXT("warnings"), CompileLog.NumWarnings);
	Result->SetArrayField(TEXT("messages"), Messages);

	// No no-op flag is reported here, and the earlier draft that reported one
	// was wrong. FKismetEditorUtilities::CompileBlueprint rebuilds the generated
	// class and reinstances every live object of it on every call, whatever the
	// Blueprint's status was: there is no path through it that does nothing.
	// `wasUpToDate` is the honest fact - what the status said on the way in -
	// and it is not a claim that this call did no work.
	Result->SetBoolField(TEXT("wasUpToDate"), bWasUpToDate);
	Result->SetBoolField(TEXT("idempotent"), false);
	// The two questions a caller has are different and both get an answer.
	// `idempotent` is about the work: a compile always rebuilds the generated
	// class and reinstances its objects, so calling twice does real work twice.
	// `changed` is about the outcome: a Blueprint that came in up to date and
	// compiled clean ends where it started, so a retry after a timeout learns
	// that its second call moved nothing even though it was not free.
	Result->SetBoolField(TEXT("changed"), !bWasUpToDate || !bCompiled);
	Result->SetStringField(TEXT("idempotencyNote"),
		TEXT("A compile always rebuilds the generated class and reinstances its objects, so calling twice does real "
		     "work twice and there is no 'already compiled' short circuit to report. wasUpToDate says what the "
		     "Blueprint's status was before this ran, and changed says whether that status moved; neither claims the "
		     "call itself was free."));

	// A compile has no inverse. It rebuilds the generated class from the graphs
	// that are already saved; there is no call that un-compiles a Blueprint, and
	// the previous generated class is not kept anywhere to restore from.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Compiling rebuilds the generated class from graphs that were already saved. There is no inverse action: "
		     "nothing un-compiles a Blueprint, and the pre-compile generated class is not retained to restore from. "
		     "Undo the graph edits that made the compile necessary instead."));
	return MCPResult(Result);
}

namespace MCPNodeSearch
{
	// #808: the old search compared the raw query against raw C++ function names
	// on five hard-coded classes. Two things made that miss almost everything an
	// agent types. Engine names carry no spaces ("IsPointInBox") while callers
	// type the palette label ("Is Point in Box"), and the palette label often
	// only exists as DisplayName metadata ("VSize" is shown as "Vector Length").
	// Folding both sides to lowercase alphanumerics makes spacing, casing, and
	// underscores stop mattering.
	static FString Normalize(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (const TCHAR C : In)
		{
			if (FChar::IsAlnum(C))
			{
				Out.AppendChar(FChar::ToLower(C));
			}
		}
		return Out;
	}

	/** 0 when the candidate does not match at all; higher is a better match. */
	static int32 ScoreTerm(const FString& NormCandidate, const FString& NormQuery)
	{
		if (NormCandidate.IsEmpty() || NormQuery.IsEmpty()) return 0;
		if (NormCandidate == NormQuery) return 100;
		if (NormCandidate.StartsWith(NormQuery, ESearchCase::CaseSensitive)) return 70;
		if (NormCandidate.Contains(NormQuery, ESearchCase::CaseSensitive)) return 45;
		return 0;
	}

	/** Every query word present somewhere in the candidate, order-independent. */
	static bool ContainsAllTokens(const FString& NormCandidate, const TArray<FString>& NormTokens)
	{
		if (NormTokens.Num() < 2) return false;
		for (const FString& Token : NormTokens)
		{
			if (!NormCandidate.Contains(Token, ESearchCase::CaseSensitive)) return false;
		}
		return true;
	}

	/** The authored palette label, or empty when the function has none. */
	static FString DisplayNameMetaFor(const UFunction* Func)
	{
		static const FName NAME_DisplayNameMeta(TEXT("DisplayName"));
		if (const FString* Meta = Func->FindMetaData(NAME_DisplayNameMeta))
		{
			if (!Meta->IsEmpty()) return *Meta;
		}
		return FString();
	}

	/** Label shown in the palette. Only reported for hits: the fallback spacing
	 *  pass takes a lock and scans an exemption list, which is too expensive to
	 *  run over every loaded function, and it cannot change a match anyway since
	 *  it only inserts spaces that normalization strips again. */
	static FString PaletteLabelFor(const UFunction* Func, const FString& KnownDisplayNameMeta)
	{
		if (!KnownDisplayNameMeta.IsEmpty()) return KnownDisplayNameMeta;
		// An exact name match short-circuits the metadata read, so look once more
		// before falling back to the spaced name.
		const FString Meta = DisplayNameMetaFor(Func);
		return Meta.IsEmpty() ? FName::NameToDisplayString(Func->GetName(), false) : Meta;
	}

	/** Skip engine bookkeeping classes that hold no placeable nodes. */
	static bool IsSearchableClass(const UClass* Class)
	{
		if (!Class) return false;
		if (Class->HasAnyClassFlags(CLASS_NewerVersionExists)) return false;
		const FString Name = Class->GetName();
		return !Name.StartsWith(TEXT("SKEL_"))
			&& !Name.StartsWith(TEXT("REINST_"))
			&& !Name.StartsWith(TEXT("TRASHCLASS_"))
			&& !Name.StartsWith(TEXT("PLACEHOLDER-"));
	}

	struct FHit
	{
		int32 Score = 0;
		FString SortName;
		// The row's stable identity for paging: the function's or node class's
		// full object path. Two classes can each declare a Cast function, and a
		// page boundary has to name exactly one of them.
		FString Id;
		TSharedPtr<FJsonObject> Entry;
	};
}

TSharedPtr<FJsonValue> FBlueprintHandlers::SearchNodeTypes(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPNodeSearch;

	FString Query;
	if (auto Err = RequireString(Params, TEXT("query"), Query)) return Err;
	// Every parameter, paging included, is read before anything can fail (#1057).
	const bool bIncludeGraphNodes = OptionalBool(Params, TEXT("includeGraphNodes"), true);
	// Optional narrowing to one owning class, by short name or object path.
	const FString ClassFilter = OptionalString(Params, TEXT("className"));

	// T3: paged. This used to score every match, return the top `limit` and set
	// `truncated`, which told a caller there was more without giving it any way
	// to read the rest. The whole ranked list is enumerated and paged instead.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("search_node_types|query=%s|className=%s|includeGraphNodes=%d"),
				*Query, *ClassFilter, bIncludeGraphNodes ? 1 : 0),
			/*DefaultLimit*/ 50, /*MaxLimit*/ 500, Page))
	{
		return Err;
	}

	const FString NormQuery = Normalize(Query);
	if (NormQuery.IsEmpty())
	{
		return MCPError(TEXT("Parameter 'query' must contain at least one letter or digit"));
	}

	TArray<FString> RawTokens;
	Query.ParseIntoArrayWS(RawTokens);
	TArray<FString> NormTokens;
	for (const FString& Token : RawTokens)
	{
		const FString NormToken = Normalize(Token);
		if (!NormToken.IsEmpty()) NormTokens.Add(NormToken);
	}

	UClass* FilterClass = nullptr;
	if (!ClassFilter.IsEmpty())
	{
		if (ClassFilter.Contains(TEXT("/")))
		{
			FilterClass = LoadObject<UClass>(nullptr, *ClassFilter);
		}
		if (!FilterClass) FilterClass = MCPResolveClass(ClassFilter);
		if (!FilterClass)
		{
			return MCPError(FString::Printf(TEXT("Class not found: %s"), *ClassFilter));
		}
	}

	static const FName NAME_KeywordsMeta(TEXT("Keywords"));
	static const FName NAME_CategoryMeta(TEXT("Category"));
	static const FName NAME_DeprecatedFunctionMeta(TEXT("DeprecatedFunction"));
	static const FName NAME_BlueprintInternalUseOnlyMeta(TEXT("BlueprintInternalUseOnly"));

	TArray<FHit> Hits;

	// Every loaded class is walked, not a hard-coded shortlist, so the whole
	// UBlueprintFunctionLibrary surface (KismetMathLibrary and friends) plus
	// member functions on gameplay classes are all reachable from one query.
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* OwnerClass = *ClassIt;
		if (!IsSearchableClass(OwnerClass)) continue;
		if (FilterClass && OwnerClass != FilterClass) continue;

		// ExcludeSuper: report each function once, at the class that declares it.
		for (TFieldIterator<UFunction> FuncIt(OwnerClass, EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
		{
			UFunction* Func = *FuncIt;
			if (!Func) continue;
			if (!Func->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure)) continue;

			const FString FuncName = Func->GetName();
			const FString NormName = Normalize(FuncName);

			// Cheapest test first. The metadata reads below are per-object map
			// lookups and this loop runs over every loaded blueprint function.
			int32 Score = ScoreTerm(NormName, NormQuery);

			FString DisplayNameMeta;
			FString NormDisplay;
			if (Score < 100)
			{
				DisplayNameMeta = DisplayNameMetaFor(Func);
				NormDisplay = Normalize(DisplayNameMeta);
				Score = FMath::Max(Score, ScoreTerm(NormDisplay, NormQuery));
			}

			FString Keywords;
			if (Score == 0)
			{
				if (const FString* KeywordMeta = Func->FindMetaData(NAME_KeywordsMeta))
				{
					Keywords = *KeywordMeta;
					if (Normalize(Keywords).Contains(NormQuery, ESearchCase::CaseSensitive))
					{
						Score = 25;
					}
				}
			}

			if (Score == 0 && (ContainsAllTokens(NormName, NormTokens) || ContainsAllTokens(NormDisplay, NormTokens)))
			{
				Score = 20;
			}

			if (Score == 0) continue;
			if (Func->HasMetaData(NAME_BlueprintInternalUseOnlyMeta)) continue;

			const FString DisplayName = PaletteLabelFor(Func, DisplayNameMeta);
			if (Keywords.IsEmpty())
			{
				if (const FString* KeywordMeta = Func->FindMetaData(NAME_KeywordsMeta))
				{
					Keywords = *KeywordMeta;
				}
			}

			const bool bDeprecated = Func->HasMetaData(NAME_DeprecatedFunctionMeta);
			if (bDeprecated) Score -= 50;

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("type"), TEXT("function"));
			Entry->SetStringField(TEXT("name"), FuncName);
			Entry->SetStringField(TEXT("displayName"), DisplayName);
			Entry->SetStringField(TEXT("class"), OwnerClass->GetName());
			Entry->SetStringField(TEXT("classPath"), OwnerClass->GetPathName());
			Entry->SetStringField(TEXT("fullPath"), Func->GetPathName());
			Entry->SetBoolField(TEXT("pure"), Func->HasAnyFunctionFlags(FUNC_BlueprintPure));
			Entry->SetBoolField(TEXT("static"), Func->HasAnyFunctionFlags(FUNC_Static));
			if (bDeprecated) Entry->SetBoolField(TEXT("deprecated"), true);
			if (!Keywords.IsEmpty()) Entry->SetStringField(TEXT("keywords"), Keywords);
			if (const FString* CategoryMeta = Func->FindMetaData(NAME_CategoryMeta))
			{
				Entry->SetStringField(TEXT("category"), *CategoryMeta);
			}

			// The exact arguments add_node needs, so a search result can be
			// placed without the caller guessing an identifier format.
			TSharedPtr<FJsonObject> NodeParams = MakeShared<FJsonObject>();
			NodeParams->SetStringField(TEXT("functionName"), FuncName);
			NodeParams->SetStringField(TEXT("targetClass"), OwnerClass->GetPathName());
			TSharedPtr<FJsonObject> AddNodeCall = MakeShared<FJsonObject>();
			AddNodeCall->SetStringField(TEXT("nodeClass"), TEXT("CallFunction"));
			AddNodeCall->SetObjectField(TEXT("nodeParams"), NodeParams);
			Entry->SetObjectField(TEXT("addNode"), AddNodeCall);

			Hits.Add(FHit{ Score, FuncName, Func->GetPathName(), Entry });
		}
	}

	// AnimGraph node types and any other UEdGraphNode subclass, placed by class name.
	if (bIncludeGraphNodes)
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* NodeClass = *It;
			if (!NodeClass->IsChildOf(UEdGraphNode::StaticClass())) continue;
			if (NodeClass == UEdGraphNode::StaticClass()) continue;
			if (NodeClass->HasAnyClassFlags(CLASS_Abstract)) continue;
			if (!IsSearchableClass(NodeClass)) continue;

			const FString ClassName = NodeClass->GetName();
			// K2Node_IfThenElse should answer a search for "if then else" as well
			// as one for the bare class name.
			const FString BareName = ClassName.StartsWith(TEXT("K2Node_"))
				? ClassName.RightChop(7)
				: ClassName;

			int32 Score = FMath::Max(ScoreTerm(Normalize(ClassName), NormQuery), ScoreTerm(Normalize(BareName), NormQuery));
			if (Score == 0 && ContainsAllTokens(Normalize(ClassName), NormTokens)) Score = 20;
			if (Score == 0) continue;

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("type"), TEXT("graphNode"));
			Entry->SetStringField(TEXT("name"), ClassName);
			Entry->SetStringField(TEXT("class"), NodeClass->GetSuperClass() ? NodeClass->GetSuperClass()->GetName() : TEXT(""));
			Entry->SetStringField(TEXT("fullPath"), NodeClass->GetPathName());

			TSharedPtr<FJsonObject> AddNodeCall = MakeShared<FJsonObject>();
			AddNodeCall->SetStringField(TEXT("nodeClass"), ClassName);
			Entry->SetObjectField(TEXT("addNode"), AddNodeCall);

			Hits.Add(FHit{ Score, ClassName, NodeClass->GetPathName(), Entry });
		}
	}

	// TObjectIterator walks the object hash, whose order is not a contract, so
	// the ranking is completed by the object path: without that last tiebreak
	// two functions of the same name and score can swap places between two
	// calls, and a page anchor cannot resume into a sequence that reshuffles.
	Hits.Sort([](const FHit& A, const FHit& B)
	{
		if (A.Score != B.Score) return A.Score > B.Score;
		if (A.SortName.Len() != B.SortName.Len()) return A.SortName.Len() < B.SortName.Len();
		if (A.SortName != B.SortName) return A.SortName < B.SortName;
		return A.Id < B.Id;
	});

	TArray<MCPPagination::FPageRow> Rows;
	Rows.Reserve(Hits.Num());
	for (const FHit& Hit : Hits)
	{
		Rows.Add({ Hit.Id, MakeShared<FJsonValueObject>(Hit.Entry) });
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("query"), Query);
	Result->SetNumberField(TEXT("totalMatches"), Hits.Num());
	MCPPagination::EmitPage(Page, Rows, TEXT("results"), Result);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ListNodeTypes(const TSharedPtr<FJsonObject>& Params)
{
	FString Category = OptionalString(Params, TEXT("category"), TEXT("Utilities"));

	// T3: paged. KismetMathLibrary alone declares several hundred callable
	// functions, so the default category returns a list no caller reads whole.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_node_types|category=%s"), *Category),
			/*DefaultLimit*/ 100, /*MaxLimit*/ 1000, Page))
	{
		return Err;
	}

	TArray<MCPPagination::FPageRow> Rows;
	FString LowerCategory = Category.ToLower();

	// Map categories to relevant classes and function sets
	TArray<UClass*> ClassesToSearch;

	if (LowerCategory == TEXT("utilities"))
	{
		ClassesToSearch.Add(UKismetSystemLibrary::StaticClass());
	}
	else if (LowerCategory == TEXT("math"))
	{
		ClassesToSearch.Add(UKismetMathLibrary::StaticClass());
	}
	else if (LowerCategory == TEXT("string"))
	{
		ClassesToSearch.Add(UKismetStringLibrary::StaticClass());
	}
	else if (LowerCategory == TEXT("gameplay"))
	{
		ClassesToSearch.Add(UGameplayStatics::StaticClass());
	}
	else if (LowerCategory == TEXT("actor"))
	{
		ClassesToSearch.Add(AActor::StaticClass());
	}
	else
	{
		// Default: search all common classes
		ClassesToSearch.Add(UKismetSystemLibrary::StaticClass());
		ClassesToSearch.Add(UKismetMathLibrary::StaticClass());
		ClassesToSearch.Add(UGameplayStatics::StaticClass());
	}

	// TFieldIterator here includes inherited functions, and the default
	// category searches three classes that share a base, so one function can be
	// reached twice. A page anchor has to name exactly one row, so the second
	// sighting is dropped rather than emitted as a duplicate.
	TSet<FString> SeenFunctionPaths;
	for (UClass* SearchClass : ClassesToSearch)
	{
		if (!SearchClass) continue;
		for (TFieldIterator<UFunction> FuncIt(SearchClass); FuncIt; ++FuncIt)
		{
			UFunction* Func = *FuncIt;
			if (!Func) continue;
			if (!Func->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure)) continue;
			bool bAlreadySeen = false;
			SeenFunctionPaths.Add(Func->GetPathName(), &bAlreadySeen);
			if (bAlreadySeen) continue;

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Func->GetName());
			Entry->SetStringField(TEXT("class"), SearchClass->GetName());
			Entry->SetStringField(TEXT("fullPath"), Func->GetPathName());
			// The function's object path is the page anchor: it names the
			// declaring class as well as the function, and TFieldIterator can
			// reach the same name through two of the classes searched.
			Rows.Add({ Func->GetPathName(), MakeShared<FJsonValueObject>(Entry) });
		}
	}

	// TFieldIterator walks a class's field list, whose order is not a contract
	// and which recompiles differently for a Blueprint-declared class, so the
	// rows are sorted before paging. The path sorts by declaring class first,
	// which keeps the old grouping.
	Rows.Sort([](const MCPPagination::FPageRow& A, const MCPPagination::FPageRow& B)
		{ return A.Id < B.Id; });

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("category"), Category);
	MCPPagination::EmitPage(Page, Rows, TEXT("nodeTypes"), Result);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// #902 / #931: resolved variable defaults, and whether they are on disk.
//
// list_variables could confirm a variable existed but nothing returned the
// value the generated class actually resolved to, so a write-compile-readback
// loop was impossible natively and callers dropped to Python for
// get_default_object(bp.generated_class()).get_editor_property(name).
//
// The value lives on the CDO, not on FBPVariableDescription::DefaultValue. The
// engine documents that string as an "optional new default value", and it is
// empty for most variables, so reading it answers a different question than
// the one being asked. Anything that reads it instead reports an empty default
// for a variable that plainly has one.
//
// Persistence is reported separately, and that separation is the point (#931).
// A write that sets a CDO property and marks the package dirty without saving
// it reads back correctly for the rest of the session and is gone on the next
// editor start. UPackage::IsDirty is the engine's own record of exactly that
// state, so `persisted` is false while the package holds unsaved changes: the
// readback distinguishes "this value is on disk" from "this value is in this
// process", instead of echoing the write back at the caller either way.
// ---------------------------------------------------------------------------
namespace
{
	struct FResolvedVariableDefault
	{
		FProperty* Property = nullptr;
		const void* ValueAddress = nullptr;
		FString ValueText;
		TSharedPtr<FJsonValue> Value;
		FString DeclaringClass;
		FString DeclaringClassPath;
		bool bInherited = false;
	};

	// Resolve one variable's compiled default off the Blueprint's generated
	// class CDO. Returns false with a caller-facing reason on any miss.
	bool ResolveVariableDefault(
		UBlueprint* Blueprint,
		const FString& VarName,
		FResolvedVariableDefault& Out,
		FString& OutError)
	{
		if (!Blueprint)
		{
			OutError = TEXT("No Blueprint to resolve a variable default from");
			return false;
		}

		UClass* GeneratedClass = Blueprint->GeneratedClass.Get();
		if (!GeneratedClass)
		{
			OutError = FString::Printf(
				TEXT("Blueprint '%s' has no generated class, so it has no resolved defaults yet. Compile it first (blueprint compile)."),
				*Blueprint->GetName());
			return false;
		}

		UObject* CDO = GeneratedClass->GetDefaultObject();
		if (!CDO)
		{
			OutError = FString::Printf(
				TEXT("Generated class '%s' has no class default object"), *GeneratedClass->GetName());
			return false;
		}

		FProperty* Prop = GeneratedClass->FindPropertyByName(FName(*VarName));
		if (!Prop)
		{
			// Name the variables that DO resolve, so a caller that has just
			// added one can see whether the compile carried it through.
			TArray<FString> Available;
			for (TFieldIterator<FProperty> It(GeneratedClass); It && Available.Num() < 60; ++It)
			{
				if (*It) Available.Add((*It)->GetName());
			}
			OutError = FString::Printf(
				TEXT("Variable '%s' has no property on generated class '%s'. If it was just added, compile the Blueprint. Resolved properties: [%s]"),
				*VarName, *GeneratedClass->GetName(), *FString::Join(Available, TEXT(", ")));
			return false;
		}

		Out.Property = Prop;
		Out.ValueAddress = Prop->ContainerPtrToValuePtr<void>(CDO);
		Prop->ExportText_Direct(Out.ValueText, Out.ValueAddress, Out.ValueAddress, CDO, PPF_None);
		Out.Value = FMCPJsonSerializer::SerializeValue(Out.ValueAddress, Prop);

		if (UClass* Owner = Prop->GetOwnerClass())
		{
			Out.DeclaringClass = Owner->GetName();
			Out.DeclaringClassPath = Owner->GetPathName();
			Out.bInherited = Owner != GeneratedClass;
		}
		return true;
	}

	// Write the resolved value onto a JSON object. Shared so list_variables and
	// get_variable_default cannot report the same value under different names.
	void WriteResolvedVariableDefault(const TSharedPtr<FJsonObject>& Obj, const FResolvedVariableDefault& Resolved)
	{
		if (!Obj.IsValid() || !Resolved.Property) return;
		Obj->SetField(TEXT("value"), Resolved.Value.IsValid() ? Resolved.Value : MakeShared<FJsonValueNull>());
		Obj->SetStringField(TEXT("valueText"), Resolved.ValueText);
		Obj->SetStringField(TEXT("cppType"), Resolved.Property->GetCPPType());
		if (!Resolved.DeclaringClass.IsEmpty())
		{
			Obj->SetStringField(TEXT("declaringClass"), Resolved.DeclaringClass);
			Obj->SetStringField(TEXT("declaringClassPath"), Resolved.DeclaringClassPath);
		}
		Obj->SetBoolField(TEXT("inherited"), Resolved.bInherited);
	}

	// #931: state whether what was just read is on disk. The package's own
	// dirty flag is the answer: it is set by every write that reaches the
	// object and cleared by a successful save, so a value that reads back
	// correctly out of a dirty package has not been persisted and will be gone
	// after a restart.
	void WriteDefaultPersistence(const TSharedPtr<FJsonObject>& Obj, UBlueprint* Blueprint)
	{
		if (!Obj.IsValid() || !Blueprint) return;
		UPackage* Package = Blueprint->GetOutermost();
		const bool bDirty = Package && Package->IsDirty();
		if (Package)
		{
			Obj->SetStringField(TEXT("packageName"), Package->GetName());
		}
		Obj->SetBoolField(TEXT("packageDirty"), bDirty);
		Obj->SetBoolField(TEXT("persisted"), !bDirty);
		if (bDirty)
		{
			Obj->SetStringField(TEXT("persistenceNote"),
				TEXT("This value is live in the editor but its package has unsaved changes, so it is not on disk and will revert on the next editor start. Save the Blueprint (blueprint compile_all with save, or asset save) and read again to confirm it persisted."));
		}
	}
}

TSharedPtr<FJsonValue> FBlueprintHandlers::GetVariableDefault(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	FString VarName;
	if (auto Err = RequireString(Params, TEXT("name"), VarName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return BlueprintNotFoundError(AssetPath);
	}

	FResolvedVariableDefault Resolved;
	FString ResolveError;
	if (!ResolveVariableDefault(Blueprint, VarName, Resolved, ResolveError))
	{
		return MCPError(ResolveError);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	AnnotateResolvedBlueprint(Result, Blueprint);
	Result->SetStringField(TEXT("name"), VarName);
	WriteResolvedVariableDefault(Result, Resolved);
	WriteDefaultPersistence(Result, Blueprint);

	// The authored string, when there is one, is reported alongside rather than
	// instead of the resolved value. It is advisory: the engine treats it as an
	// optional override, so an empty one is normal and says nothing. A non-empty
	// one that disagrees with the CDO means the next recompile can move the
	// value, which is worth seeing in a verification loop.
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName.ToString() != VarName) continue;
		Result->SetBoolField(TEXT("declaredOnThisBlueprint"), true);
		if (Var.DefaultValue.IsEmpty()) break;

		Result->SetStringField(TEXT("authoredDefault"), Var.DefaultValue);
		FDefaultConstructedPropertyElement Authored(Resolved.Property);
		// No owning object on purpose: this is a read, and an owner is what
		// lets the importer construct instanced subobjects under the real
		// asset. A question about a value must not touch it.
		const bool bParsed = FBlueprintEditorUtils::PropertyValueFromString_Direct(
			Resolved.Property,
			Var.DefaultValue,
			static_cast<uint8*>(Authored.GetObjAddress()),
			/*OwningObject=*/nullptr);
		if (bParsed)
		{
			Result->SetBoolField(TEXT("matchesAuthoredDefault"),
				Resolved.Property->Identical(Resolved.ValueAddress, Authored.GetObjAddress(), PPF_None));
		}
		break;
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ListBlueprintVariables(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	// #902: resolved values are opt-in. They cost a CDO property read and a
	// JSON serialization per variable, and every existing caller of this action
	// wants the declaration list, so the default payload is unchanged and a
	// verification loop asks for the values it needs.
	const bool bIncludeValues = OptionalBool(Params, TEXT("includeValues"), false);

	// T3: paged. A Blueprint carrying a hundred variables with includeValues on
	// is one of the largest reads on this category.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_blueprint_variables|path=%s|includeValues=%d"),
				*AssetPath, bIncludeValues ? 1 : 0),
			/*DefaultLimit*/ 200, /*MaxLimit*/ 2000, Page))
	{
		return Err;
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return BlueprintNotFoundError(AssetPath);
	}

	TArray<MCPPagination::FPageRow> Rows;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
		VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
		// type stays the pin category; typeSpec is the full add_variable spelling, containers included.
		bool bSpecRoundTrips = false;
		VarObj->SetStringField(TEXT("typeSpec"), PinTypeSpec(Var.VarType, bSpecRoundTrips));
		VarObj->SetStringField(TEXT("guid"), Var.VarGuid.ToString());

		// Check metadata
		// Report the VALUE, matching set_variable_properties and the engine's own
		// readers: a key present with "false" is not private. Reporting on mere
		// presence made read -> write -> read disagree with itself.
		if (Var.HasMetaData(FBlueprintMetadata::MD_Private))
		{
			VarObj->SetBoolField(TEXT("private"),
				Var.GetMetaData(FBlueprintMetadata::MD_Private).ToBool());
		}
		if (Var.HasMetaData(FBlueprintMetadata::MD_FunctionCategory))
		{
			VarObj->SetStringField(TEXT("category"), Var.GetMetaData(FBlueprintMetadata::MD_FunctionCategory));
		}
		if (Var.HasMetaData(FBlueprintMetadata::MD_Tooltip))
		{
			VarObj->SetStringField(TEXT("tooltip"), Var.GetMetaData(FBlueprintMetadata::MD_Tooltip));
		}

		// #744: CPF_Edit alone is set by BOTH EditAnywhere and EditDefaultsOnly,
		// so testing it reported instanceEditable=true for variables the
		// Blueprint deliberately locks to class defaults - an actively wrong
		// answer, not a missing one. What "Instance Editable" unticks is
		// CPF_DisableEditOnInstance. Report the raw specifier too so callers
		// never have to infer it from a boolean again.
		const bool bEditable = (Var.PropertyFlags & CPF_Edit) != 0;
		const bool bNoInstanceEdit = (Var.PropertyFlags & CPF_DisableEditOnInstance) != 0;
		const bool bNoTemplateEdit = (Var.PropertyFlags & CPF_DisableEditOnTemplate) != 0;
		// MD_Private is NOT folded in. It is a Blueprint-GRAPH access flag - it
		// hides the variable from other Blueprints' graphs, not from a placed
		// instance's details panel - so an EditAnywhere private variable IS
		// instance editable. Including it here made this reader disagree with
		// set_variable_properties, which reported a real change as a no-op.
		// `private` is reported on its own above.
		VarObj->SetBoolField(TEXT("instanceEditable"),
			bEditable && !bNoInstanceEdit);

		const TCHAR* EditFlag = TEXT("none");
		if (bEditable)
		{
			if (bNoInstanceEdit)      EditFlag = TEXT("EditDefaultsOnly");
			else if (bNoTemplateEdit) EditFlag = TEXT("EditInstanceOnly");
			else                      EditFlag = TEXT("EditAnywhere");
		}
		VarObj->SetStringField(TEXT("editFlag"), EditFlag);
		VarObj->SetBoolField(TEXT("blueprintReadOnly"), (Var.PropertyFlags & CPF_BlueprintReadOnly) != 0);

		// Effective state, not key presence: the key can exist with "false".
		VarObj->SetBoolField(TEXT("exposeOnSpawn"),
			(Var.PropertyFlags & CPF_ExposeOnSpawn) != 0 ||
			(Var.HasMetaData(FBlueprintMetadata::MD_ExposeOnSpawn) &&
			 Var.GetMetaData(FBlueprintMetadata::MD_ExposeOnSpawn).ToBool()));

		if (bIncludeValues)
		{
			FResolvedVariableDefault Resolved;
			FString ResolveError;
			if (ResolveVariableDefault(Blueprint, Var.VarName.ToString(), Resolved, ResolveError))
			{
				WriteResolvedVariableDefault(VarObj, Resolved);
			}
			else
			{
				// A variable that has no compiled property is a real state
				// (added but not compiled yet), so say so per variable rather
				// than failing the whole listing.
				VarObj->SetStringField(TEXT("valueError"), ResolveError);
			}
		}

		// The variable NAME is the page anchor. NewVariables is authored order,
		// which carries meaning in the details panel, so the rows are
		// deliberately not sorted; a name is unique within that array and a
		// reorder is exactly the change the anchor is there to report.
		Rows.Add({ Var.VarName.ToString(), MakeShared<FJsonValueObject>(VarObj) });
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	AnnotateResolvedBlueprint(Result, Blueprint);
	MCPPagination::EmitPage(Page, Rows, TEXT("variables"), Result);
	if (bIncludeValues)
	{
		// Persistence is a property of the package, not of any one variable, so
		// it is stated once for the whole listing.
		WriteDefaultPersistence(Result, Blueprint);
	}
	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FBlueprintHandlers::RemoveComponent(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	FString ComponentName;
	if (auto Err = RequireString(Params, TEXT("componentName"), ComponentName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return BlueprintNotFoundError(AssetPath);
	}

	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	if (!SCS)
	{
		return MCPError(TEXT("Blueprint has no SimpleConstructionScript (not an Actor blueprint?)"));
	}

	// Find the SCS node by variable name or component template name
	USCS_Node* TargetNode = nullptr;
	for (USCS_Node* Node : SCS->GetAllNodes())
	{
		if (!Node || !Node->ComponentTemplate) continue;
		if (Node->GetVariableName().ToString() == ComponentName ||
			Node->ComponentTemplate->GetName() == ComponentName)
		{
			TargetNode = Node;
			break;
		}
	}

	// Idempotent: nothing to remove is a no-op.
	if (!TargetNode)
	{
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("componentName"), ComponentName);
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Noop);
	}

	// Read the identity the inverse needs BEFORE the node is destroyed: which
	// class to re-add, under which SCS parent, and how much of the node the
	// re-add cannot bring back.
	const FString RemovedClassPath = TargetNode->ComponentTemplate->GetClass()->GetPathName();
	const FString RemovedVariableName = TargetNode->GetVariableName().ToString();
	const int32 RemovedChildCount = TargetNode->GetChildNodes().Num();
	FString RemovedParentName;
	bool bParentAddressable = false;
	bool bParentIsNative = false;

	// FindParentNode only ever answers with another USCS_Node, and the ordinary
	// case does not have one. A component attached under a NATIVE inherited
	// component - anything under a Character's Mesh or CapsuleComponent - is
	// itself an SCS ROOT that records its parent in bIsParentComponentNative
	// plus ParentComponentOrVariableName, so FindParentNode returns nullptr and
	// the previous parent looked like "there wasn't one". The rollback then put
	// the component back at the root under a different parent and said nothing,
	// which is the silent wrong-parent restore this whole check exists to stop.
	if (TargetNode->bIsParentComponentNative && !TargetNode->ParentComponentOrVariableName.IsNone())
	{
		RemovedParentName = TargetNode->ParentComponentOrVariableName.ToString();
		bParentIsNative = true;
		// Deliberately NOT addressable. add_component matches parentComponent
		// against subobject OBJECT names, and a native component's object name
		// is not its variable name (a Character's "Mesh" is "CharacterMesh0"),
		// so the name recorded here would either miss and fall back to the root
		// or prefix-match something unrelated. Saying so beats guessing.
	}
	else if (USCS_Node* ParentNode = SCS->FindParentNode(TargetNode))
	{
		RemovedParentName = ParentNode->GetVariableName().ToString();

		// add_component does not resolve parentComponent against the SCS
		// variable name. It walks the subobject handles and takes the FIRST
		// object whose name equals the string or merely STARTS WITH it, and
		// falls back to the actor root when nothing matches. A parent named
		// "Mesh" therefore prefix-matches a sibling's "Mesh2_GEN_VARIABLE", and
		// a rollback built on that would succeed while silently reparenting the
		// component under the wrong node. So the name is tested here, against
		// the same handle set and the same rule add_component will apply, and
		// it is only sent when it selects the intended template and nothing
		// else.
		if (USubobjectDataSubsystem* ParentProbe = GEngine->GetEngineSubsystem<USubobjectDataSubsystem>())
		{
			TArray<FSubobjectDataHandle> ProbeHandles;
			ParentProbe->K2_GatherSubobjectDataForBlueprint(Blueprint, ProbeHandles);
			const UObject* ParentTemplate = ParentNode->ComponentTemplate;
			int32 NameMatches = 0;
			bool bFirstMatchIsParent = false;
			for (const FSubobjectDataHandle& ProbeHandle : ProbeHandles)
			{
				const FSubobjectData* ProbeData = ProbeHandle.GetData();
				if (!ProbeData) continue;
				UObject* ProbeObject = const_cast<UObject*>(ProbeData->GetObject());
				if (!ProbeObject) continue;
				const FString ProbeName = ProbeObject->GetName();
				if (ProbeName == RemovedParentName || ProbeName.StartsWith(RemovedParentName))
				{
					if (NameMatches == 0)
					{
						bFirstMatchIsParent = (ProbeObject == ParentTemplate);
					}
					++NameMatches;
				}
			}
			bParentAddressable = (NameMatches == 1 && bFirstMatchIsParent);
		}
	}

	// Remove via SubobjectDataSubsystem if available
	bool bRemoved = false;
	if (USubobjectDataSubsystem* Subsystem = GEngine->GetEngineSubsystem<USubobjectDataSubsystem>())
	{
		TArray<FSubobjectDataHandle> Handles;
		Subsystem->K2_GatherSubobjectDataForBlueprint(Blueprint, Handles);

		FSubobjectDataHandle ContextHandle = Handles.Num() > 0 ? Handles[0] : FSubobjectDataHandle();
		for (const FSubobjectDataHandle& Handle : Handles)
		{
			const FSubobjectData* Data = Handle.GetData();
			if (Data && Data->GetComponentTemplate() == TargetNode->ComponentTemplate)
			{
				TArray<FSubobjectDataHandle> ToDelete;
				ToDelete.Add(Handle);
				int32 Removed = Subsystem->DeleteSubobjects(ContextHandle, ToDelete, Blueprint);
				bRemoved = (Removed > 0);
				break;
			}
		}
	}

	// Fallback: direct SCS removal
	if (!bRemoved)
	{
		SCS->RemoveNode(TargetNode);
		bRemoved = true;
	}

	if (bRemoved)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		SaveAssetPackage(Blueprint);

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("componentName"), ComponentName);
		Result->SetBoolField(TEXT("deleted"), true);

		// The inverse is add_component, which re-adds a component of the same
		// class under the same SCS parent with the same variable name. What it
		// cannot do is restore the template's property values, so this rollback
		// is lossy and says so.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("path"), AssetPath);
		Payload->SetStringField(TEXT("componentClass"), RemovedClassPath);
		Payload->SetStringField(TEXT("componentName"), RemovedVariableName);
		if (bParentAddressable)
		{
			Payload->SetStringField(TEXT("parentComponent"), RemovedParentName);
		}
		MCPSetRollback(Result, TEXT("add_component"), Payload);
		Result->SetStringField(TEXT("previousParent"), RemovedParentName);
		Result->SetBoolField(TEXT("previousParentAddressable"), bParentAddressable);
		Result->SetBoolField(TEXT("previousParentIsNative"), bParentIsNative);
		Result->SetBoolField(TEXT("hadParent"), !RemovedParentName.IsEmpty());
		Result->SetBoolField(TEXT("rollbackLossy"), true);

		FString LossyNote = FString::Printf(TEXT(
			"add_component re-adds a default-constructed '%s' named '%s'. Every property override on the removed "
			"template is gone, and any Blueprint node that referenced the component is not restored by re-adding it."),
			*RemovedClassPath, *RemovedVariableName);
		if (RemovedChildCount > 0)
		{
			LossyNote += FString::Printf(TEXT(
				" The %d component(s) parented under it went with it: the inverse restores one component, not the "
				"subtree."),
				RemovedChildCount);
		}
		if (bParentIsNative)
		{
			LossyNote += FString::Printf(TEXT(
				" It was attached under the NATIVE inherited component '%s' and comes back at the SCS root instead. "
				"add_component resolves parentComponent against subobject object names, which for a native "
				"component is not its variable name, so no parent is sent rather than one that would land it "
				"somewhere else. reparent_component cannot put it back either - that action only resolves SCS "
				"nodes - so re-attaching to '%s' has to be done in the editor."),
				*RemovedParentName, *RemovedParentName);
		}
		else if (!RemovedParentName.IsEmpty() && !bParentAddressable)
		{
			LossyNote += FString::Printf(TEXT(
				" It also comes back at the SCS ROOT rather than under '%s': add_component matches parentComponent "
				"by name prefix against the component templates and that name does not select this one uniquely, so "
				"sending it would have reparented under a sibling instead. Follow the rollback with "
				"reparent_component to put it back."),
				*RemovedParentName);
		}
		Result->SetStringField(TEXT("rollbackNote"), LossyNote);
		return MCPResult(Result);
	}
	else
	{
		return MCPError(TEXT("Failed to remove component"));
	}
}

// ---------------------------------------------------------------------------
// delete_variable -- Delete a member variable from a Blueprint
// Params: assetPath, name
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::DeleteVariable(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	FString VarName;
	if (auto Err = RequireString(Params, TEXT("name"), VarName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return BlueprintNotFoundError(AssetPath);
	}

	bool bFound = false;
	FEdGraphPinType RemovedType;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName.ToString() == VarName)
		{
			bFound = true;
			RemovedType = Var.VarType;
			break;
		}
	}

	// Idempotent: nothing to delete is a no-op.
	if (!bFound)
	{
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("variableName"), VarName);
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Noop);
	}

	FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, FName(*VarName));

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("variableName"), VarName);
	Result->SetBoolField(TEXT("deleted"), true);

	// The inverse is add_variable, which re-declares the variable with the same
	// name and type. It is offered only when the type survives the round trip
	// through add_variable's own vocabulary (ParsePinTypeSpec, containers included).
	bool bTypeRoundTrips = true;
	const FString RemovedTypeSpec = PinTypeSpec(RemovedType, bTypeRoundTrips);
	if (bTypeRoundTrips)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("path"), AssetPath);
		Payload->SetStringField(TEXT("name"), VarName);
		Payload->SetStringField(TEXT("type"), RemovedTypeSpec);
		MCPSetRollback(Result, TEXT("add_variable"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);

		FString VarLossyNote = FString::Printf(TEXT(
			"add_variable re-declares '%s' as %s and nothing else. The default value, category, tooltip, "
			"instance-editable and replication flags are gone, and RemoveMemberVariable already deleted every get "
			"and set node that referenced the variable: re-declaring it does not put those nodes back."),
			*VarName, *RemovedTypeSpec);

		// "float" is not a round trip. PinTypeSpec spells both PC_Float and a
		// float-subcategoried PC_Real as "float", and MakePinType turns "float"
		// into PC_Real with the DOUBLE subcategory, which is what the editor
		// gives you for a Float variable in UE5. The width differs from what was
		// deleted, so it is named rather than left for the caller to discover.
		const bool bWasNarrowFloat =
			RemovedType.PinCategory == UEdGraphSchema_K2::PC_Float
			|| (RemovedType.PinCategory == UEdGraphSchema_K2::PC_Real
				&& RemovedType.PinSubCategory == UEdGraphSchema_K2::PC_Float);
		if (bWasNarrowFloat)
		{
			VarLossyNote += TEXT(
				" The variable was a single-precision float and comes back as the double-precision Real that "
				"add_variable's 'float' maps to, so anything binding to its exact pin type has to be rewired.");
		}
		Result->SetStringField(TEXT("rollbackNote"), VarLossyNote);
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(TEXT(
			"No rollback offered: the deleted variable's type is '%s', which add_variable's 'type' parameter cannot "
			"express, so the inverse would fail on replay rather than restore anything."),
			*RemovedTypeSpec));
	}
	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FBlueprintHandlers::DuplicateBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString SourcePath;
	if (auto Err = RequireString(Params, TEXT("sourcePath"), SourcePath)) return Err;
	FString DestinationPath;
	if (auto Err = RequireString(Params, TEXT("destinationPath"), DestinationPath)) return Err;

	// A missing source is refused here; the asset library logs it at a level that varies by engine.
	if (!MCPLoadAssetObject(SourcePath)) return MCPAssetNotFoundError(SourcePath);

	UObject* Dup = UEditorAssetLibrary::DuplicateAsset(SourcePath, DestinationPath);
	if (!Dup)
	{
		// #441: DoesAssetExist can return false for valid Blueprint paths in
		// 5.7. Fall back to loading the source and driving AssetTools directly.
		UObject* SourceObj = UEditorAssetLibrary::LoadAsset(SourcePath);
		if (!SourceObj) SourceObj = LoadObject<UObject>(nullptr, *SourcePath);
		if (SourceObj)
		{
			FString DestPkg, DestName;
			if (DestinationPath.Split(TEXT("/"), &DestPkg, &DestName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
			{
				IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
				Dup = AssetTools.DuplicateAsset(DestName, DestPkg, SourceObj);
			}
		}
	}
	if (!Dup) return MCPError(FString::Printf(TEXT("Failed to duplicate '%s'"), *SourcePath));

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("sourcePath"), SourcePath);
	Result->SetStringField(TEXT("destinationPath"), Dup->GetPathName());
	MCPSetDeleteAssetRollback(Result, Dup->GetPathName());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::AddLocalVariable(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString FunctionName;
	if (auto Err = RequireString(Params, TEXT("functionName"), FunctionName)) return Err;
	FString VarName;
	if (auto Err = RequireString(Params, TEXT("name"), VarName)) return Err;
	FString TypeStr = OptionalString(Params, TEXT("varType"), TEXT("bool"));

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	// Find the function graph and its FunctionEntry node.
	UEdGraph* FuncGraph = nullptr;
	for (UEdGraph* G : Blueprint->FunctionGraphs)
	{
		if (G && G->GetName() == FunctionName) { FuncGraph = G; break; }
	}
	if (!FuncGraph) return MCPError(FString::Printf(TEXT("Function not found: %s"), *FunctionName));

	UK2Node_FunctionEntry* Entry = nullptr;
	for (UEdGraphNode* Node : FuncGraph->Nodes)
	{
		if (UK2Node_FunctionEntry* E = Cast<UK2Node_FunctionEntry>(Node)) { Entry = E; break; }
	}
	if (!Entry) return MCPError(TEXT("Function has no entry node"));

	// Idempotency: check if local variable already exists on the entry node
	const FName VarFName(*VarName);
	for (const FBPVariableDescription& Existing : Entry->LocalVariables)
	{
		if (Existing.VarName == VarFName)
		{
			auto ExistedRes = MCPSuccess();
			MCPSetExisted(ExistedRes);
			ExistedRes->SetStringField(TEXT("path"), AssetPath);
			ExistedRes->SetStringField(TEXT("functionName"), FunctionName);
			ExistedRes->SetStringField(TEXT("name"), VarName);
			return MCPResult(ExistedRes);
		}
	}

	FEdGraphPinType PinType;
	FString TypeError;
	if (!ParsePinTypeSpec(TypeStr, PinType, TypeError))
	{
		return MCPError(FString::Printf(TEXT("Unrecognized variable type: %s"), *TypeError));
	}

	FBPVariableDescription NewVar;
	NewVar.VarName = VarFName;
	NewVar.VarGuid = FGuid::NewGuid();
	NewVar.VarType = PinType;
	NewVar.FriendlyName = VarName;
	Entry->Modify();
	Entry->LocalVariables.Add(NewVar);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("functionName"), FunctionName);
	Result->SetStringField(TEXT("name"), VarName);

	// edit_local_variable op=remove is the paired remove this handler used to
	// say it did not have. It drops the declaration from the same function
	// entry node this call added it to, and is itself idempotent.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("functionName"), FunctionName);
	Payload->SetStringField(TEXT("name"), VarName);
	Payload->SetStringField(TEXT("op"), TEXT("remove"));
	MCPSetRollback(Result, TEXT("edit_local_variable"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ListLocalVariables(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString FunctionName;
	if (auto Err = RequireString(Params, TEXT("functionName"), FunctionName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	UEdGraph* FuncGraph = nullptr;
	for (UEdGraph* G : Blueprint->FunctionGraphs)
	{
		if (G && G->GetName() == FunctionName) { FuncGraph = G; break; }
	}
	if (!FuncGraph) return MCPError(FString::Printf(TEXT("Function not found: %s"), *FunctionName));

	UK2Node_FunctionEntry* Entry = nullptr;
	for (UEdGraphNode* Node : FuncGraph->Nodes)
	{
		if (UK2Node_FunctionEntry* E = Cast<UK2Node_FunctionEntry>(Node)) { Entry = E; break; }
	}

	TArray<TSharedPtr<FJsonValue>> Arr;
	if (Entry)
	{
		for (const FBPVariableDescription& Var : Entry->LocalVariables)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"), Var.VarName.ToString());
			O->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
			Arr.Add(MakeShared<FJsonValueObject>(O));
		}
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("functionName"), FunctionName);
	Result->SetArrayField(TEXT("variables"), Arr);
	Result->SetNumberField(TEXT("variableCount"), Arr.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ValidateBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	// Run compile without saving; collect diagnostics from the compiler result log.
	FCompilerResultsLog Log;
	Log.bSilentMode = true;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave, &Log);

	TArray<TSharedPtr<FJsonValue>> Errors;
	for (TSharedRef<FTokenizedMessage> Msg : Log.Messages)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("severity"), Msg->GetSeverity() == EMessageSeverity::Error ? TEXT("Error")
			: Msg->GetSeverity() == EMessageSeverity::Warning ? TEXT("Warning") : TEXT("Info"));
		O->SetStringField(TEXT("message"), Msg->ToText().ToString());
		Errors.Add(MakeShared<FJsonValueObject>(O));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetNumberField(TEXT("errorCount"), Log.NumErrors);
	Result->SetNumberField(TEXT("warningCount"), Log.NumWarnings);
	Result->SetBoolField(TEXT("valid"), Log.NumErrors == 0);
	Result->SetArrayField(TEXT("messages"), Errors);
	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FBlueprintHandlers::ReparentComponent(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString ComponentName;
	if (auto Err = RequireString(Params, TEXT("componentName"), ComponentName)) return Err;
	FString NewParent;
	if (auto Err = RequireString(Params, TEXT("newParent"), NewParent)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(TEXT("Blueprint not found"));
	if (auto Blocked = MCPAssetWriteBlockedError(Blueprint, AssetPath, TEXT("reparent this component"))) return Blocked;
	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	if (!SCS) return MCPError(TEXT("Blueprint has no SCS"));

	USCS_Node* Child = nullptr; USCS_Node* Parent = nullptr;
	for (USCS_Node* N : SCS->GetAllNodes())
	{
		if (!N) continue;
		if (N->GetVariableName().ToString() == ComponentName) Child = N;
		if (N->GetVariableName().ToString() == NewParent) Parent = N;
	}
	if (!Child) return MCPError(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
	if (!Parent) return MCPError(FString::Printf(TEXT("Parent not found: %s"), *NewParent));

	// Where it hung before, which is both the idempotency answer and the exact
	// inverse. An SCS root node has no parent name to hand back, and newParent
	// is required, so that case gets no rollback rather than a guessed one.
	USCS_Node* PreviousParent = SCS->FindParentNode(Child);
	const FString PreviousParentName = PreviousParent ? PreviousParent->GetVariableName().ToString() : FString();

	if (PreviousParent == Parent)
	{
		auto NoOp = MCPSuccess();
		MCPSetExisted(NoOp);
		NoOp->SetBoolField(TEXT("unchanged"), true);
		NoOp->SetStringField(TEXT("path"), AssetPath);
		NoOp->SetStringField(TEXT("componentName"), ComponentName);
		NoOp->SetStringField(TEXT("newParent"), NewParent);
		return MCPResult(NoOp);
	}

	SCS->RemoveNode(Child);
	Parent->AddChildNode(Child);

	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	FString SaveReason;
	const bool bSaved = SaveAssetPackageChecked(Blueprint, SaveReason);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("componentName"), ComponentName);
	Result->SetStringField(TEXT("newParent"), NewParent);
	if (!PreviousParentName.IsEmpty())
	{
		Result->SetStringField(TEXT("previousParent"), PreviousParentName);
	}
	MCPNoteSaveOutcome(Result, AssetPath, bSaved, SaveReason);

	if (!PreviousParentName.IsEmpty())
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("path"), AssetPath);
		Payload->SetStringField(TEXT("componentName"), ComponentName);
		Payload->SetStringField(TEXT("newParent"), PreviousParentName);
		MCPSetRollback(Result, TEXT("reparent_component"), Payload);
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(TEXT(
			"'%s' was a root node of the SimpleConstructionScript, not a child of another component. "
			"reparent_component requires a newParent, and there is no component name that means 'back to the root', "
			"so no inverse is offered rather than one that would reparent it under the wrong node."),
			*ComponentName));
	}
	return MCPResult(Result);
}

// ─── #138 reparent_blueprint ────────────────────────────────────────
// Changes a Blueprint's ParentClass (equivalent to
// unreal.BlueprintEditorLibrary.reparent_blueprint + compile + save).
TSharedPtr<FJsonValue> FBlueprintHandlers::ReparentBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString ParentClassName;
	if (auto Err = RequireString(Params, TEXT("parentClass"), ParentClassName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	// #932: reparenting reparents, recompiles AND saves, and the save is not
	// optional. A .uasset that was never checked out of source control is
	// read-only on disk, and asking the engine to write it turned the failed
	// save into a FATAL error that took the whole editor process down. The
	// asset itself was fine and the call replayed cleanly after a checkout, so
	// the only thing missing was this question, asked before the Blueprint is
	// touched rather than after it has already been reparented and recompiled.
	if (auto Blocked = MCPAssetWriteBlockedError(Blueprint, AssetPath, TEXT("reparent this Blueprint"))) return Blocked;

	// Resolve parent class: full path > short name > engine-module implicit.
	UClass* NewParent = nullptr;
	if (ParentClassName.Contains(TEXT("/")) || ParentClassName.Contains(TEXT(".")))
	{
		NewParent = LoadObject<UClass>(nullptr, *ParentClassName);
	}
	if (!NewParent)
	{
		NewParent = MCPResolveClass(ParentClassName);
	}
	if (!NewParent)
	{
		return MCPError(FString::Printf(TEXT("Parent class not found: '%s'. Try the full path ('/Script/Engine.Actor') or the bare class name."), *ParentClassName));
	}

	// Reject invalid parents to avoid engine-side asserts
	if (NewParent->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return MCPError(FString::Printf(TEXT("Parent class '%s' is deprecated or superseded"), *NewParent->GetPathName()));
	}
	if (Blueprint->GeneratedClass && NewParent == Blueprint->GeneratedClass)
	{
		return MCPError(TEXT("Cannot reparent a Blueprint to its own generated class"));
	}
	if (NewParent->IsChildOf(Blueprint->GeneratedClass))
	{
		return MCPError(TEXT("Cannot reparent to a subclass of this Blueprint (cycle)"));
	}

	UClass* OldParent = Blueprint->ParentClass;
	if (OldParent == NewParent)
	{
		auto NoOp = MCPSuccess();
		MCPSetExisted(NoOp);
		NoOp->SetStringField(TEXT("path"), AssetPath);
		NoOp->SetStringField(TEXT("parentClass"), NewParent->GetPathName());
		return MCPResult(NoOp);
	}

	// Prefer the canonical UBlueprintEditorLibrary path (matches the Python API
	// users have been falling back to in the workaround).
	UBlueprintEditorLibrary::ReparentBlueprint(Blueprint, NewParent);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	FString SaveReason;
	const bool bSaved = SaveAssetPackageChecked(Blueprint, SaveReason);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("parentClass"), NewParent->GetPathName());
	if (OldParent)
	{
		Result->SetStringField(TEXT("previousParent"), OldParent->GetPathName());
	}
	MCPNoteSaveOutcome(Result, AssetPath, bSaved, SaveReason);

	// Reparenting back to the previous class restores the hierarchy. The
	// deprecated-class check above guards NewParent only, and reparenting AWAY
	// from a deprecated base is the ordinary migration case, so the OLD parent
	// is tested here as well: naming a class this handler would refuse on
	// replay is a rollback that cannot run. What the round trip does not restore
	// is the data the first reparent dropped, so it is marked lossy.
	const bool bOldParentReusable = OldParent
		&& !OldParent->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists);
	if (bOldParentReusable)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("path"), AssetPath);
		Payload->SetStringField(TEXT("parentClass"), OldParent->GetPathName());
		MCPSetRollback(Result, TEXT("reparent_blueprint"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(TEXT(
			"Reparenting back to '%s' restores the class hierarchy, not the state this reparent discarded. "
			"CDO overrides for properties that exist only on the old parent were reset when the class was rebuilt, "
			"and inherited component overrides and graph nodes that lost their target function stay broken."),
			*OldParent->GetPathName()));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), OldParent
			? FString::Printf(TEXT(
				"The previous parent '%s' is deprecated or superseded, and this handler refuses those as a "
				"parentClass. Naming it as the inverse would produce a rollback that errors instead of restoring "
				"anything, so none is offered: this reparent was a migration off a dead class and is meant to stick."),
				*OldParent->GetPathName())
			: FString(TEXT(
				"This Blueprint had no ParentClass to reparent back to, so there is no previous value for the "
				"inverse to restore and none is guessed.")));
	}
	return MCPResult(Result);
}

// #580 flush orphaned InheritableComponentHandler records. Invalid override
// records (e.g. for components removed from a parent) survive read/remove_
// component because they're keyed by a now-dead component key. ValidateTemplates()
// drops them; this exposes that cleanup natively.
TSharedPtr<FJsonValue> FBlueprintHandlers::FlushInheritableComponentHandler(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	UInheritableComponentHandler* ICH = Blueprint->GetInheritableComponentHandler(/*bCreateIfNecessary=*/false);
	if (!ICH)
	{
		// The genuine no-op: nothing to flush, nothing touched, nothing saved.
		auto NoIch = MCPSuccess();
		MCPSetExisted(NoIch);
		NoIch->SetStringField(TEXT("path"), AssetPath);
		NoIch->SetBoolField(TEXT("hadInheritableComponentHandler"), false);
		NoIch->SetBoolField(TEXT("flushed"), false);
		NoIch->SetBoolField(TEXT("unchanged"), true);
		NoIch->SetNumberField(TEXT("recordsRemoved"), 0);
		NoIch->SetBoolField(TEXT("rollbackPossible"), false);
		NoIch->SetStringField(TEXT("rollbackNote"),
			TEXT("This Blueprint has no InheritableComponentHandler, so nothing was flushed and there is nothing "
			     "to undo."));
		return MCPResult(NoIch);
	}

	// Count override records before/after via reflection (Records is private).
	auto CountRecords = [ICH]() -> int32
	{
		if (FArrayProperty* RP = CastField<FArrayProperty>(ICH->GetClass()->FindPropertyByName(TEXT("Records"))))
		{
			FScriptArrayHelper H(RP, RP->ContainerPtrToValuePtr<void>(ICH));
			return H.Num();
		}
		return -1;
	};

	const int32 Before = CountRecords();

	// A handler with no records has nothing to validate, so the whole
	// Modify/ValidateTemplates/compile/save sequence is skipped rather than run
	// for its own sake. This is the difference between "unchanged" meaning the
	// record set is the same and "unchanged" meaning the call did no work; the
	// second is the one a replayed flow step needs, and it was not true before
	// because the compile and the save happened either way.
	if (Before == 0)
	{
		auto NoOp = MCPSuccess();
		MCPSetExisted(NoOp);
		NoOp->SetStringField(TEXT("path"), AssetPath);
		NoOp->SetBoolField(TEXT("hadInheritableComponentHandler"), true);
		NoOp->SetBoolField(TEXT("flushed"), false);
		NoOp->SetBoolField(TEXT("isEmpty"), ICH->IsEmpty());
		NoOp->SetNumberField(TEXT("recordsBefore"), 0);
		NoOp->SetNumberField(TEXT("recordsAfter"), 0);
		NoOp->SetNumberField(TEXT("recordsRemoved"), 0);
		NoOp->SetBoolField(TEXT("unchanged"), true);
		NoOp->SetBoolField(TEXT("rollbackPossible"), false);
		NoOp->SetStringField(TEXT("rollbackNote"),
			TEXT("The InheritableComponentHandler held no override records, so nothing was flushed, the Blueprint "
			     "was not recompiled or saved, and there is nothing to undo."));
		return MCPResult(NoOp);
	}

	Blueprint->Modify();
	ICH->ValidateTemplates();
	const int32 After = CountRecords();

	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetBoolField(TEXT("hadInheritableComponentHandler"), true);
	Result->SetBoolField(TEXT("flushed"), true);
	Result->SetBoolField(TEXT("isEmpty"), ICH->IsEmpty());
	Result->SetBoolField(TEXT("recordCountKnown"), Before >= 0);
	if (Before >= 0)
	{
		Result->SetNumberField(TEXT("recordsBefore"), Before);
		Result->SetNumberField(TEXT("recordsAfter"), After);
		Result->SetNumberField(TEXT("recordsRemoved"), FMath::Max(0, Before - After));
		Result->SetBoolField(TEXT("recordsUnchanged"), Before == After);
	}
	// Reached only when there WAS something to validate, so this call always
	// recompiled and saved the Blueprint. `recordsUnchanged` above says whether
	// any record was actually dropped; it does not say the call was a no-op,
	// and the flat `unchanged` that used to sit here claimed exactly that.
	Result->SetBoolField(TEXT("unchanged"), false);

	// ValidateTemplates drops override records whose component key no longer
	// resolves to anything. They were already dead, and no action re-creates an
	// override for a component that does not exist, so there is nothing to undo
	// to and none is invented.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), TEXT(
		"This drops InheritableComponentHandler records whose component key no longer resolves, which is why the "
		"engine considers them invalid. No action re-creates an override for a component that does not exist, and "
		"the dropped records are not retained anywhere, so there is no inverse call."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::FlushComponentTemplates(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	TArray<UK2Node_AddComponent*> ComponentNodes;
	FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, ComponentNodes);

	TArray<UActorComponent*> ReferencedTemplates;
	bool bNeedsUpdate = false;
	for (UK2Node_AddComponent* ComponentNode : ComponentNodes)
	{
		UActorComponent* Template = ComponentNode ? ComponentNode->GetTemplateFromNode() : nullptr;
		if (!Template)
		{
			continue;
		}
		if (ReferencedTemplates.Contains(Template))
		{
			bNeedsUpdate = true;
			continue;
		}
		ReferencedTemplates.Add(Template);
		bNeedsUpdate |= !Template->HasAllFlags(RF_ArchetypeObject | RF_Transactional);
	}

	const int32 RecordsBefore = Blueprint->ComponentTemplates.Num();
	if (RecordsBefore != ReferencedTemplates.Num())
	{
		bNeedsUpdate = true;
	}
	else
	{
		for (int32 Index = 0; Index < RecordsBefore; ++Index)
		{
			if (Blueprint->ComponentTemplates[Index].Get() != ReferencedTemplates[Index])
			{
				bNeedsUpdate = true;
				break;
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> RemovedTemplates;
	TArray<UActorComponent*> OrphanTemplates;
	for (UActorComponent* Template : Blueprint->ComponentTemplates)
	{
		if (Template && !ReferencedTemplates.Contains(Template))
		{
			OrphanTemplates.AddUnique(Template);
		}
	}
	if (Blueprint->GeneratedClass)
	{
		// Through the shared helper rather than GetObjectsWithOuter directly:
		// EGetObjectsFlags is 5.8 and later only, and the helper in
		// HandlerUtils.h is the one place that spelling is gated.
		TArray<UObject*> OwnedObjects;
		MCPGetDirectSubobjects(Blueprint->GeneratedClass, OwnedObjects);
		for (UObject* OwnedObject : OwnedObjects)
		{
			UActorComponent* Template = Cast<UActorComponent>(OwnedObject);
			if (Template
				&& Template->GetName().StartsWith(UK2Node_AddComponent::ComponentTemplateNamePrefix)
				&& !ReferencedTemplates.Contains(Template))
			{
				OrphanTemplates.AddUnique(Template);
			}
		}
	}
	bNeedsUpdate |= !OrphanTemplates.IsEmpty();

	for (UActorComponent* Template : OrphanTemplates)
	{
		TSharedPtr<FJsonObject> Identity = MakeShared<FJsonObject>();
		Identity->SetStringField(TEXT("name"), Template->GetName());
		Identity->SetStringField(TEXT("objectPath"), Template->GetPathName());
		Identity->SetStringField(TEXT("classPath"), Template->GetClass()->GetPathName());
		RemovedTemplates.Add(MakeShared<FJsonValueObject>(Identity));
	}

	if (bNeedsUpdate)
	{
		Blueprint->Modify();
		FBlueprintEditorUtils::UpdateComponentTemplates(Blueprint);
		if (UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass))
		{
			for (UActorComponent* Template : OrphanTemplates)
			{
				GeneratedClass->ComponentTemplates.Remove(Template);
			}
		}
		for (UActorComponent* Template : OrphanTemplates)
		{
			Template->Modify();
			Template->ClearFlags(RF_Public | RF_Standalone);
			// REN_AllowPackageLinkerMismatch is 5.5 and newer. The rename that
			// needs it is the same rename either way; on 5.4 the flag is simply
			// not part of the set.
#if UE_MCP_HAS_5_5_API
			constexpr ERenameFlags RetireFlags =
				REN_DoNotDirty | REN_DontCreateRedirectors | REN_AllowPackageLinkerMismatch | REN_NonTransactional;
#else
			constexpr ERenameFlags RetireFlags =
				REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional;
#endif
			if (!Template->Rename(nullptr, GetTransientPackage(), RetireFlags))
			{
				return MCPError(FString::Printf(TEXT("Failed to retire orphan component template: %s"), *Template->GetPathName()));
			}
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		const int32 RecordsAfter = Blueprint->ComponentTemplates.Num();
		if (!SaveAssetPackage(Blueprint))
		{
			return MCPError(FString::Printf(TEXT("Failed to save Blueprint after flushing component templates: %s"), *AssetPath));
		}

		if (!OrphanTemplates.IsEmpty())
		{
			FText ReloadError;
			TArray<UPackage*> PackagesToReload{Blueprint->GetOutermost()};
			if (!UPackageTools::ReloadPackages(
				PackagesToReload, ReloadError, EReloadPackagesInteractionMode::AssumePositive))
			{
				return MCPError(FString::Printf(TEXT("Failed to reload Blueprint after retiring orphan templates: %s"), *ReloadError.ToString()));
			}
			Blueprint = LoadBlueprint(AssetPath);
			if (!Blueprint || !SaveAssetPackage(Blueprint))
			{
				return MCPError(FString::Printf(TEXT("Failed final Blueprint save after retiring orphan templates: %s"), *AssetPath));
			}
		}

		auto Result = MCPSuccess();
		MCPSetUpdated(Result);
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetNumberField(TEXT("recordsBefore"), RecordsBefore);
		Result->SetNumberField(TEXT("recordsAfter"), RecordsAfter);
		Result->SetNumberField(TEXT("recordsRemoved"), RemovedTemplates.Num());
		Result->SetArrayField(TEXT("removedTemplates"), RemovedTemplates);
		Result->SetBoolField(TEXT("reloadedAfterCleanup"), !OrphanTemplates.IsEmpty());
		Result->SetBoolField(TEXT("unchanged"), false);
		// Orphan templates are renamed into the transient package and the
		// Blueprint's package is reloaded on top of the result. Nothing brings
		// a transient object back, and no action re-registers a component
		// template that no Add Component node refers to.
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), TEXT(
			"Orphan component templates are renamed into the transient package and the Blueprint package is reloaded "
			"afterwards, so the removed templates no longer exist to restore. No action re-registers a component "
			"template that no Add Component node refers to, and none is invented here."));
		return MCPResult(Result);
	}

	auto Result = MCPSuccess();
	MCPSetExisted(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetNumberField(TEXT("recordsBefore"), RecordsBefore);
	Result->SetNumberField(TEXT("recordsAfter"), Blueprint->ComponentTemplates.Num());
	Result->SetNumberField(TEXT("recordsRemoved"), RemovedTemplates.Num());
	Result->SetArrayField(TEXT("removedTemplates"), RemovedTemplates);
	Result->SetBoolField(TEXT("reloadedAfterCleanup"), false);
	Result->SetBoolField(TEXT("unchanged"), true);
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), TEXT(
		"Nothing was flushed, so there is nothing to undo."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::RunConstructionScript(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	// Optional spawn location; a missing axis is 0. Read before the load can
	// fail (#1057).
	const FVector SpawnLocation = OptionalVec3(Params, TEXT("location"), FVector::ZeroVector);

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return BlueprintNotFoundError(AssetPath);
	}

	UClass* SpawnClass = Blueprint->GeneratedClass;
	if (!SpawnClass)
	{
		return MCPError(TEXT("Blueprint has no GeneratedClass (needs compilation first?)"));
	}

	REQUIRE_EDITOR_WORLD(World);

	// Spawn a temporary actor
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.bNoFail = true;
	SpawnParams.ObjectFlags |= RF_Transient; // Mark transient so it won't be saved

	FRotator SpawnRotation = FRotator::ZeroRotator;
	AActor* TempActor = World->SpawnActor<AActor>(SpawnClass, SpawnLocation, SpawnRotation, SpawnParams);
	if (!TempActor)
	{
		return MCPError(TEXT("Failed to spawn temporary actor from Blueprint"));
	}

	// Collect component info
	TArray<TSharedPtr<FJsonValue>> ComponentsArr;
	TArray<UActorComponent*> Components;
	TempActor->GetComponents(Components);

	for (UActorComponent* Comp : Components)
	{
		if (!Comp) continue;

		TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
		CompObj->SetStringField(TEXT("name"), Comp->GetName());
		CompObj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());

		// If it's a scene component, include transform info
		if (USceneComponent* SceneComp = Cast<USceneComponent>(Comp))
		{
			FTransform RelTrans = SceneComp->GetRelativeTransform();
			FVector Loc = RelTrans.GetLocation();
			FRotator Rot = RelTrans.GetRotation().Rotator();
			FVector Scale = RelTrans.GetScale3D();

			TSharedPtr<FJsonObject> TransObj = MakeShared<FJsonObject>();
			TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
			LocObj->SetNumberField(TEXT("x"), Loc.X);
			LocObj->SetNumberField(TEXT("y"), Loc.Y);
			LocObj->SetNumberField(TEXT("z"), Loc.Z);
			TransObj->SetObjectField(TEXT("location"), LocObj);

			TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
			RotObj->SetNumberField(TEXT("pitch"), Rot.Pitch);
			RotObj->SetNumberField(TEXT("yaw"), Rot.Yaw);
			RotObj->SetNumberField(TEXT("roll"), Rot.Roll);
			TransObj->SetObjectField(TEXT("rotation"), RotObj);

			TSharedPtr<FJsonObject> ScaleObj = MakeShared<FJsonObject>();
			ScaleObj->SetNumberField(TEXT("x"), Scale.X);
			ScaleObj->SetNumberField(TEXT("y"), Scale.Y);
			ScaleObj->SetNumberField(TEXT("z"), Scale.Z);
			TransObj->SetObjectField(TEXT("scale"), ScaleObj);

			CompObj->SetObjectField(TEXT("relativeTransform"), TransObj);

			// Is it the root?
			CompObj->SetBoolField(TEXT("isRoot"), SceneComp == TempActor->GetRootComponent());
		}

		ComponentsArr.Add(MakeShared<FJsonValueObject>(CompObj));
	}

	// Destroy the temporary actor
	World->DestroyActor(TempActor);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("className"), SpawnClass->GetName());
	Result->SetArrayField(TEXT("components"), ComponentsArr);
	Result->SetNumberField(TEXT("componentCount"), ComponentsArr.Num());

	// Nothing to undo, and the reason is worth stating rather than leaving as
	// an absent field. This spawns one RF_Transient actor, reads the component
	// list its construction script produced, and destroys it above before
	// building this result. The blueprint is not touched, nothing is saved, and
	// the probe actor does not outlive the call.
	//
	// It stays classified as a MUTATION even so, because the construction
	// script is the user's own graph and it runs. A graph that spawns child
	// actors, writes to a referenced asset or drives an editor subsystem has
	// done that by the time this returns, and none of it is visible from here.
	// So the routing gate keeps asking for an explicit editor, on the grounds
	// that arbitrary user code should not run in whichever project happens to
	// be active, and this note says the probe itself left nothing behind.
	MCPSetNoRollback(Result, TEXT(
		"Spawned a transient actor from the generated class, read the components its construction script "
		"built, and destroyed it before returning. The blueprint was not modified and nothing was saved, "
		"so there is nothing to undo. Whatever the construction script itself did while it ran is not "
		"observable from here and is not covered by this statement."));
	MCPSetIdempotencyUnobservable(Result, TEXT(
		"Every call spawns a fresh probe actor and runs the construction script again, so the work is "
		"repeated rather than skipped. Whether that run changed anything outside the probe is decided by "
		"the user's own graph and is not reported back to this handler."));

	return MCPResult(Result);
}
