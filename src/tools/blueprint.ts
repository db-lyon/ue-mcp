import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";
import { resolveCreateAssetPath } from "../asset-path.js";

export const blueprintTool: ToolDef = categoryTool(
  "blueprint",
  "Blueprint reading, authoring, and compilation, including AnimGraph/EventGraph node scripting: add_node, connect_pins, search_node_types, plus variables, functions, graphs, components, interfaces, and event dispatchers.",
  {
    read:              bp("Read BP structure incl. SCS components AND inherited native components from the CDO (CharacterMesh0, CharMoveComp, etc.). Params: assetPath, includeComponentProperties? (dump UPROPERTY name/type/value per component template; off by default) (#353/#370)", "read_blueprint", (p) => ({ path: p.assetPath, includeComponentProperties: p.includeComponentProperties })),
    list_variables:    bp("List variables with name, type, guid, category, tooltip, exposeOnSpawn, blueprintReadOnly, private, and editFlag ('EditAnywhere'|'EditDefaultsOnly'|'EditInstanceOnly'|'none'). instanceEditable is true only for EditAnywhere and EditInstanceOnly - EditDefaultsOnly is class-defaults-only and reports false (#744). private is reported separately because it is a Blueprint-graph access flag, not a details-panel one. Params: assetPath", "list_blueprint_variables", (p) => ({ path: p.assetPath })),
    list_functions:    bp("List the whole graph surface of a Blueprint, matching what list_graphs reports. Every entry carries kind ('function' own declaration | 'override' of a parent function | 'interface' implementation | 'event_graph' | 'event' entry point on an event graph | 'macro' | 'delegate_signature' | 'subgraph' collapsed graph | 'inherited'), source ('own'|'parent'|'interface'), graphName, and declaringClass/declaringClassPath where one applies. name and nodeCount are unchanged; nodeCount is 0 for entries that are not graphs. Params: assetPath, includeInherited? (append overridable parent/interface functions this Blueprint has not implemented, default false) (#809)", "list_blueprint_functions", (p) => ({ path: p.assetPath, includeInherited: p.includeInherited })),
    read_graph:        bp("Read graph nodes. Supports pagination, file dumps, and title/class node filters. With includeDefaults, literal FText pin values are returned inline (defaultValue falls back to the pin's text literal, plus an explicit defaultTextValue) and object-ref pins report defaultObject (#743). Params: assetPath, graphName, offset?, limit?, includePins?, includeDefaults?, includeComments?, dumpToFile?, outputPath?, titleFilter?, classFilter? (#560)", "read_blueprint_graph", (p) => ({ path: p.assetPath, graphName: p.graphName, offset: p.offset, limit: p.limit, includePins: p.includePins, includeDefaults: p.includeDefaults, includeComments: p.includeComments, dumpToFile: p.dumpToFile, outputPath: p.outputPath, titleFilter: p.titleFilter, classFilter: p.classFilter })),
    read_graph_summary: bp("Lightweight graph summary (nodes+edges only, ~10KB). Filterable node list. Params: assetPath, graphName?, titleFilter?, classFilter? (#560)", "read_blueprint_graph_summary", (p) => ({ path: p.assetPath, graphName: p.graphName, titleFilter: p.titleFilter, classFilter: p.classFilter })),
    get_execution_flow: bp("Trace exec pins from an entry point. Params: assetPath, graphName?, entryPoint?", "get_blueprint_execution_flow", (p) => ({ path: p.assetPath, graphName: p.graphName, entryPoint: p.entryPoint })),
    get_dependencies:   bp("Forward (classes/functions/assets) or reverse (referencers) deps. Params: assetPath, reverse?", "get_blueprint_dependencies", (p) => ({ path: p.assetPath, reverse: p.reverse })),
    diff:               bp("Semantic structural diff between two Blueprints (binary uassets are unreviewable in git). Compares parent class, variables (type/default), functions/macros, components, and per-graph node + connection deltas keyed on stable node GUIDs so edits are matched rather than shown as remove+add. Returns a structured delta plus a human summary and changeCount. Params: assetPath (base/A), otherPath (compare/B). Revision-based diffing (fromRevision/toRevision via source control) is a staged follow-up.", "diff_blueprint", (p) => ({ assetPath: p.assetPath, otherPath: p.otherPath, fromRevision: p.fromRevision, toRevision: p.toRevision })),
    create:            bp("Create Blueprint. assetPath is the full destination, e.g. /Game/Blueprints/BP_Example; a name plus packagePath pair is accepted as the same thing. A .uasset suffix, an object suffix, and backslashes are normalized away rather than reaching the editor as an invalid asset name. Params: assetPath, name?, packagePath?, parentClass?", "create_blueprint", (p) => ({ path: resolveCreateAssetPath(p), parentClass: p.parentClass })),
    add_variable:      bp("Add variable. varType accepts bool/int/float/string/name/text/byte/vector/rotator/transform/gameplaytag, object:/Script/Module.Class, struct:/Game/Path/To/Struct, or a full class/struct path. Unrecognized types are rejected rather than silently defaulting. Params: assetPath, name, varType (alias: type), onConflict? (skip|error, default skip) (#745)", "add_variable", (p) => ({ path: p.assetPath, name: p.name, type: p.varType ?? p.type, onConflict: p.onConflict })),
    set_variable_properties: bp("Edit variable properties. Note: replication is set with networking(set_property_replicated), and blueprintReadOnly is not writable here - list_variables reports it. Params: assetPath, name, editFlag? (EditAnywhere|EditDefaultsOnly|EditInstanceOnly|none - the exact value list_variables reports, and the only form that round-trips every state), instanceEditable? (two-state shorthand; mutually exclusive with editFlag), private?, category?, tooltip?, exposeOnSpawn?", "set_variable_properties", (p) => ({ path: p.assetPath, name: p.name, instanceEditable: p.instanceEditable, editFlag: p.editFlag, private: p.private, category: p.category, tooltip: p.tooltip, exposeOnSpawn: p.exposeOnSpawn })),
    create_function:   bp("Create function. Params: assetPath, functionName", "create_function", (p) => ({ path: p.assetPath, functionName: p.functionName })),
    delete_function:   bp("Delete function. Params: assetPath, functionName", "delete_function", (p) => ({ path: p.assetPath, functionName: p.functionName })),
    rename_function:   bp("Rename function. Params: assetPath, oldName, newName", "rename_function", (p) => ({ path: p.assetPath, oldName: p.oldName, newName: p.newName })),
    add_node:          bp("Add graph node. For a CallFunction node bound to a custom C++ UFUNCTION, pass nodeParams {functionName, className (or targetClass) = /Script/Module.Class}; the function also resolves against the BP's own component classes and an unambiguous loaded BlueprintCallable function, producing a bound node with pins instead of a stub (#546). nodeClass='CallParent' places a 'Parent: <Function>' call bound to the parent implementation (functionName resolves against ParentClass) so an override graph can chain to the base (#688). Params: assetPath, graphName?, nodeClass, nodeParams?", "add_node", (p) => ({ path: p.assetPath, graphName: p.graphName ?? "EventGraph", nodeClass: p.nodeClass, nodeParams: p.nodeParams })),
    delete_node:       bp("Delete node. Params: assetPath, graphName, nodeName", "delete_node", (p) => ({ path: p.assetPath, graphName: p.graphName, nodeName: p.nodeName })),
    set_node_property: bp("Set node pin default or struct property. Params: assetPath, graphName, nodeName, propertyName, value", "set_node_property", (p) => ({ path: p.assetPath, graphName: p.graphName, nodeName: p.nodeName, propertyName: p.propertyName, value: p.value })),
    connect_pins:      bp("Wire nodes. Params: sourceNode, sourcePin, targetNode, targetPin, assetPath, graphName?, breakExistingSource?, breakExistingTarget?", "connect_pins"),
    add_component:     bp("Add BP component. componentClass accepts short names (e.g. 'ChildActorComponent') or full paths. For a ChildActorComponent, pass childActorClass to set its ChildActorClass in the same call (a Blueprint path with or without _C, or a C++ class) (#526). Params: assetPath, componentClass, componentName?, parentComponent? (SCS parent for hierarchy — #115), childActorClass?", "add_component", (p) => ({ path: p.assetPath, componentClass: p.componentClass, componentName: p.componentName ?? p.componentClass, parentComponent: p.parentComponent, childActorClass: p.childActorClass })),
    remove_component:  bp("Remove SCS component. Params: assetPath, componentName", "remove_component", (p) => ({ path: p.assetPath, componentName: p.componentName })),
    set_component_property: bp("Set property on SCS or inherited component. Inherited components go through the child BP's InheritableComponentHandler override template so the parent stays untouched. Pass value=null to clear a TObjectPtr/SoftObject/WeakObject/UClass/Interface reference (e.g. clear AnimClass on CharacterMesh0) (#420). Params: assetPath, componentName, propertyName, value", "set_blueprint_component_property", (p) => ({ path: p.assetPath, componentName: p.componentName, propertyName: p.propertyName, value: p.value })),
    set_component_override_materials: bp("Write OverrideMaterials on a mesh-component template (StaticMeshComponent / SkeletalMeshComponent / any UMeshComponent). Pass materialPaths as a string[] of material asset paths (empty array clears). Avoids any TArray<UObject*> coercion on the generic set_component_property path (#442). Params: assetPath, componentName, materialPaths", "set_component_override_materials", (p) => ({ assetPath: p.assetPath, componentName: p.componentName, materialPaths: p.materialPaths })),
    add_timeline_track: bp("Add a track to a Blueprint timeline. Creates the UTimelineTemplate if missing, builds the matching curve asset (float/vector/color/event), applies keyframes, bumps TimelineLength to cover the last key, then recompiles so K2Node_Timeline regenerates its output pins. Params: assetPath, timelineName, trackName, trackType ('float'|'vector'|'color'|'event'), keyframes ([{time, value}]). value is a number for float/event, {x,y,z} for vector, {r,g,b,a} for color (#457)", "add_timeline_track", (p) => ({ assetPath: p.assetPath, timelineName: p.timelineName, trackName: p.trackName, trackType: p.trackType, keyframes: p.keyframes })),
    set_capsule_size: bp("Call UCapsuleComponent::SetCapsuleSize on a CapsuleComponent template (CharacterMovement-friendly path; raw property writes leave the visualizer stale). Pass either or both of halfHeight/radius. Returns the new + previous values. Params: assetPath, componentName, halfHeight?, radius? (#419)", "set_capsule_size", (p) => ({ path: p.assetPath, componentName: p.componentName, halfHeight: p.halfHeight, radius: p.radius })),
    get_component_property: bp("Read a single property value from an SCS or inherited component template. Returns the ICH override value for child BPs if one exists. Params: assetPath, componentName, propertyName", "get_blueprint_component_property", (p) => ({ path: p.assetPath, componentName: p.componentName, propertyName: p.propertyName })),
    set_class_default: bp("Set UPROPERTY on Blueprint CDO. Pass value=null to clear an object/class/interface reference (#420). Params: assetPath, propertyName, value", "set_class_default", (p) => ({ path: p.assetPath, propertyName: p.propertyName, value: p.value })),
    delete_variable:   bp("Delete a member variable. Params: assetPath, name", "delete_variable", (p) => ({ path: p.assetPath, name: p.name })),
    add_function_parameter: bp("Add input or output parameter to a function. Params: assetPath, functionName, parameterName, parameterType?, isOutput?", "add_function_parameter", (p) => ({ path: p.assetPath, functionName: p.functionName, parameterName: p.parameterName, parameterType: p.parameterType, isOutput: p.isOutput })),
    set_variable_default: bp("Set default value on a BP variable. Params: assetPath, name, value", "set_variable_default", (p) => ({ path: p.assetPath, name: p.name, value: p.value })),
    compile:           bp("Compile Blueprint. Params: assetPath", "compile_blueprint", (p) => ({ path: p.assetPath })),
    list_node_types:   bp("List node types. Params: category?, includeFunctions?", "list_node_types"),
    search_node_types: bp("Search placeable nodes across every loaded Blueprint function library (KismetMathLibrary, GameplayStatics, custom libraries) plus UEdGraphNode classes. Matches the C++ name, the palette label from DisplayName metadata, and Keywords metadata, ignoring case/spaces/underscores, so 'is point in box' finds IsPointInBox and 'vector length' finds VSize. Results are ranked and each carries an addNode block (nodeClass + nodeParams incl. targetClass) that add_node accepts verbatim. Params: query, limit? (default 50), className? (narrow to one owning class), includeGraphNodes? (#808)", "search_node_types"),
    create_interface:  bp("Create BP Interface. Params: assetPath", "create_blueprint_interface", (p) => ({ path: p.assetPath })),
    add_interface:     bp("Implement interface. Params: blueprintPath, interfacePath", "add_blueprint_interface"),
    override_function: bp("Override an inherited interface implementation (inherited via the parent class) or an overridable parent virtual function, with the matching signature so it actually binds as the override (create_function makes a blank, non-binding graph). Returns kind ('function' with a graphName, or 'event' with a nodeId in EventGraph). Event-shaped functions become override events unless preferFunction=true. Pass interfacePath to also implement the interface first if it is not present. Then wire the body with add_node (use nodeClass='CallParent' to chain to the base implementation). Params: assetPath, functionName, source? ('auto'|'interface'|'parent', advisory), preferFunction?, interfacePath? (#688)", "override_function", (p) => ({ path: p.assetPath, functionName: p.functionName, source: p.source, preferFunction: p.preferFunction, interfacePath: p.interfacePath })),
    list_overridable_functions: bp("List functions this Blueprint can override: inherited interface implementations and overridable parent virtuals. Each entry has name, source ('interface'|'parent'), declaringClass, canBeEvent. Params: assetPath (#688)", "list_overridable_functions", (p) => ({ path: p.assetPath })),
    list_graphs:       bp("List all graphs in a blueprint. Returns selector/objectPath plus duplicateIndex/duplicateCount; pass selector back as graphName for unambiguous nested graph edits. Params: assetPath", "list_blueprint_graphs", (p) => ({ path: p.assetPath })),
    resolve_graph:     bp("Resolve a Blueprint graph name to selectors accepted by read_graph/add_node/connect_pins and other graph actions. Duplicate nested AnimBP names such as Locomotion return every match with selectors like Locomotion[3], object paths, and ambiguity metadata. Params: assetPath, graphName", "resolve_blueprint_graph", (p) => ({ path: p.assetPath, graphName: p.graphName })),
    add_event_dispatcher: bp("Add event dispatcher (multicast delegate variable + signature graph + UFunction). Without parameters, broadcasters fire void(). With parameters, the signature graph gets typed user pins so K2Node_CallDelegate compiles cleanly (#276). Params: blueprintPath, name, parameters?: [{name, type}] where type is bool/int/float/string/name/text/vector/rotator/transform/object:/Script/Module.Class/struct:/Script/Module.Struct", "add_event_dispatcher"),
    duplicate:         bp("Duplicate blueprint asset. Params: sourcePath, destinationPath", "duplicate_blueprint"),
    add_local_variable: bp("Add function-scope local variable. Params: assetPath, functionName, name, varType? (alias: type, default bool)", "add_local_variable", (p) => ({ assetPath: p.assetPath, functionName: p.functionName, name: p.name, varType: p.varType ?? p.type })),
    list_local_variables: bp("List local variables in a function. Params: assetPath, functionName", "list_local_variables"),
    validate:          bp("Validate blueprint without saving (compile + collect diagnostics). Params: assetPath", "validate_blueprint"),
    read_component_properties: bp("Dump ALL UPROPERTYs on a BP component template incl. array contents (#105). Params: assetPath, componentName", "read_component_properties", (p) => ({ path: p.assetPath, componentName: p.componentName })),
    read_node_property: bp("Read a node pin default OR a reflected node property for verification (#102). Params: assetPath, graphName?, nodeName, propertyName", "read_node_property", (p) => ({ path: p.assetPath, graphName: p.graphName, nodeName: p.nodeName, propertyName: p.propertyName })),
    reparent_component: bp("Reparent an SCS component under a new parent (#115). Params: assetPath, componentName, newParent", "reparent_component", (p) => ({ path: p.assetPath, componentName: p.componentName, newParent: p.newParent })),
    reparent: bp("Change a Blueprint's ParentClass and recompile (#138). Params: assetPath, parentClass (short name or full path).", "reparent_blueprint", (p) => ({ path: p.assetPath, parentClass: p.parentClass })),
    flush_ich: bp("Flush orphaned InheritableComponentHandler override records (invalid component-override entries invisible to read/remove_component). Recompiles + saves. Params: assetPath. Returns recordsBefore/After/Removed (#580)", "flush_inheritable_component_handler", (p) => ({ path: p.assetPath })),
    set_actor_tick_settings: bp("Set actor CDO tick settings (#116). Params: assetPath, bCanEverTick?, bStartWithTickEnabled?, TickInterval?", "set_actor_tick_settings", (p) => ({ path: p.assetPath, bCanEverTick: p.bCanEverTick, bStartWithTickEnabled: p.bStartWithTickEnabled, TickInterval: p.TickInterval })),
    export_nodes_t3d:  bp("Export graph nodes as T3D text (Ctrl+C equivalent) for bulk round-trip (#130). Params: assetPath, graphName?, nodeIds? (omit = whole graph)", "export_nodes_t3d", (p) => ({ path: p.assetPath, graphName: p.graphName ?? "EventGraph", nodeIds: p.nodeIds })),
    import_nodes_t3d:  bp("Paste a T3D node blob into a graph (Ctrl+V equivalent) for bulk authoring (#130). Params: assetPath, graphName?, t3d, posX?, posY?", "import_nodes_t3d", (p) => ({ path: p.assetPath, graphName: p.graphName ?? "EventGraph", t3d: p.t3d, posX: p.posX, posY: p.posY })),
    set_cdo_property:  bp("Set UPROPERTY on any C++ class CDO (not just Blueprints). Params: className, propertyName, value (#182/#183)", "set_cdo_property"),
    get_cdo_properties: bp("Read UPROPERTY values from any C++ class CDO. Params: className, propertyNames? (#183)", "get_cdo_properties"),
    run_construction_script: bp("Spawn temp actor, run construction script, return generated components and transforms. Params: assetPath, location? (#195)", "run_construction_script", (p) => ({ path: p.assetPath, location: p.location })),
    compile_all: bp("Batch compile + save Blueprints. Params: assetPaths[], save? (default true). Returns per-path status (compiled/failed/not_found) (#284)", "compile_blueprints", (p) => ({ assetPaths: p.assetPaths, save: p.save })),
    author: {
      description: "Author a whole Blueprint in one call: optionally create it (parentClass), then add components, variables and function stubs, then compile - one agent-facing action instead of a dozen add_* round-trips. Params: assetPath, parentClass? (create/ensure the BP if given), components? [{componentClass, componentName?, parentComponent?, childActorClass?}], variables? [{name, varType}], functions? [{functionName}], compile? (default true). Returns per-step results + created flag. (#607)",
      handler: async (ctx, p) => {
        const assetPath = p.assetPath as string;
        if (!assetPath) throw new Error("Missing 'assetPath'");
        const steps: Array<{ step: string; target: string; ok: boolean; result?: unknown; error?: string }> = [];
        const run = async (step: string, target: string, method: string, params: Record<string, unknown>) => {
          try { const result = await ctx.bridge.call(method, params); steps.push({ step, target, ok: true, result }); }
          catch (e) { steps.push({ step, target, ok: false, error: e instanceof Error ? e.message : String(e) }); }
        };
        let created = false;
        if (p.parentClass) {
          try {
            const r = await ctx.bridge.call("create_blueprint", { path: assetPath, parentClass: p.parentClass }) as Record<string, unknown>;
            created = r?.created !== false;
            steps.push({ step: "create", target: assetPath, ok: true, result: r });
          } catch (e) { steps.push({ step: "create", target: assetPath, ok: false, error: e instanceof Error ? e.message : String(e) }); }
        }
        for (const c of (p.components as Array<Record<string, unknown>> ?? [])) {
          await run("add_component", String(c.componentClass ?? ""), "add_component", { path: assetPath, componentClass: c.componentClass, componentName: c.componentName ?? c.componentClass, parentComponent: c.parentComponent, childActorClass: c.childActorClass });
        }
        for (const v of (p.variables as Array<Record<string, unknown>> ?? [])) {
          await run("add_variable", String(v.name ?? ""), "add_variable", { path: assetPath, name: v.name, type: v.varType ?? v.type });
        }
        for (const f of (p.functions as Array<Record<string, unknown>> ?? [])) {
          await run("create_function", String(f.functionName ?? ""), "create_function", { path: assetPath, functionName: f.functionName });
        }
        if ((p.compile ?? true) !== false) {
          await run("compile", assetPath, "compile_blueprint", { path: assetPath });
        }
        const failed = steps.filter(s => !s.ok);
        return { assetPath, created, stepCount: steps.length, failedCount: failed.length, ok: failed.length === 0, steps };
      },
    },
    cleanup_graph: bp("Remove orphan/corrupted nodes (no class, blank title+no pins, missing target UFunction). Params: assetPath, graphName? (default: every graph) (#285)", "cleanup_graph", (p) => ({ assetPath: p.assetPath, graphName: p.graphName })),
    connect_pins_batch: bp("Apply many pin connections in one call (single compile + save). Params: assetPath, graphName?, connections[]: [{sourceNode, sourcePin, targetNode, targetPin}] (#267)", "connect_pins_batch", (p) => ({ assetPath: p.assetPath, graphName: p.graphName, connections: p.connections })),
    set_node_position: bp("Move a graph node to (posX, posY). Params: assetPath, graphName?, nodeId, posX, posY (#277)", "set_node_position", (p) => ({ assetPath: p.assetPath, graphName: p.graphName, nodeId: p.nodeId, posX: p.posX, posY: p.posY })),
    auto_layout: bp("Topological layered layout for a graph. Eliminates the (0,0) stack from programmatic add_node. Params: assetPath, graphName?, columnGap? (default 360), rowGap? (default 200) (#277)", "auto_layout_graph", (p) => ({ assetPath: p.assetPath, graphName: p.graphName, columnGap: p.columnGap, rowGap: p.rowGap })),
  },
  undefined,
  {
    assetPath: z.string().optional().describe("Blueprint asset path"),
    // Declared so the transport cannot strip them before `create` folds them
    // into the canonical assetPath (#798).
    path: z.string().optional().describe("Legacy alias for assetPath. Use assetPath (#798)"),
    packagePath: z.string().optional().describe("create: destination folder, used with name. assetPath is the canonical alternative (#798)"),
    graphName: z.string().optional(), functionName: z.string().optional(),
    name: z.string().optional(), varType: z.string().optional().describe("Variable type for add_variable/add_local_variable"),
    // `type` is the name callers reach for first; without it the value was
    // dropped on the floor and the handler silently produced a Float (#745).
    type: z.string().optional().describe("add_variable: alias for varType"),
    onConflict: z.string().optional().describe("add_variable: skip (default) or error when the variable already exists"),
    parentClass: z.string().optional(),
    components: z.array(z.record(z.unknown())).optional().describe("author: [{componentClass, componentName?, parentComponent?, childActorClass?}] (#607)"),
    variables: z.array(z.record(z.unknown())).optional().describe("author: [{name, varType}] (#607)"),
    functions: z.array(z.record(z.unknown())).optional().describe("author: [{functionName}] (#607)"),
    compile: z.boolean().optional().describe("author: compile after authoring (default true) (#607)"),
    category: z.string().optional(), tooltip: z.string().optional(),
    exposeOnSpawn: z.boolean().optional().describe("Toggle ExposeOnSpawn flag on a variable"),
    instanceEditable: z.boolean().optional().describe("set_variable_properties: true = EditAnywhere (per-instance), false = not instance-editable"),
    otherPath: z.string().optional().describe("diff: the compare-against Blueprint (B)"),
    // Forwarded so the handler's explanatory "not wired yet" error still fires.
    // Dropping them turned an explicit rejection into a silent two-asset diff.
    fromRevision: z.string().optional().describe("diff: reserved for revision diffing (not implemented; passing it is an explicit error)"),
    toRevision: z.string().optional().describe("diff: reserved for revision diffing (not implemented; passing it is an explicit error)"),
    editFlag: z.string().optional().describe("set_variable_properties: EditAnywhere|EditDefaultsOnly|EditInstanceOnly|none - the exact value list_variables reports (#744)"),
    private: z.boolean().optional().describe("set_variable_properties: the Blueprint editor's Private checkbox, independent of editFlag"),
    location: z.record(z.unknown()).optional().describe("run_construction_script: {x,y,z} spawn location for the temp actor"),
    oldName: z.string().optional(), newName: z.string().optional(),
    nodeClass: z.string().optional(), nodeParams: z.record(z.unknown()).optional(),
    nodeName: z.string().optional(), propertyName: z.string().optional(),
    value: z.unknown().optional(),
    sourceNode: z.string().optional(), sourcePin: z.string().optional(),
    targetNode: z.string().optional(), targetPin: z.string().optional(),
    breakExistingSource: z.boolean().optional().describe("connect_pins: break all existing links on the source pin before connecting"),
    breakExistingTarget: z.boolean().optional().describe("connect_pins: break all existing links on the target pin before connecting"),
    componentClass: z.string().optional(), componentName: z.string().optional(),
    materialPaths: z.array(z.string()).optional().describe("Material asset paths for set_component_override_materials (#442)"),
    timelineName: z.string().optional().describe("Timeline name for add_timeline_track (#457)"),
    trackName: z.string().optional().describe("Track name within the timeline (#457)"),
    trackType: z.enum(["float", "vector", "color", "event"]).optional().describe("add_timeline_track type (#457)"),
    keyframes: z.array(z.record(z.unknown())).optional().describe("add_timeline_track: [{time, value}] - value is number for float/event, {x,y,z} for vector, {r,g,b,a} for color (#457)"),
    halfHeight: z.number().optional().describe("set_capsule_size: capsule half height (unscaled)"),
    radius: z.number().optional().describe("set_capsule_size: capsule radius (unscaled)"),
    parentComponent: z.string().optional().describe("Parent SCS component for add_component hierarchy (#115)"),
    childActorClass: z.string().optional().describe("ChildActorClass for an added ChildActorComponent — Blueprint path (with/without _C) or C++ class (#526)"),
    newParent: z.string().optional().describe("New parent component name for reparent_component"),
    bCanEverTick: z.boolean().optional(),
    bStartWithTickEnabled: z.boolean().optional(),
    TickInterval: z.number().optional(),
    parameterName: z.string().optional(), parameterType: z.string().optional(),
    isOutput: z.boolean().optional(),
    source: z.enum(["auto", "interface", "parent"]).optional().describe("override_function: advisory hint for where the overridable function comes from (#688)"),
    preferFunction: z.boolean().optional().describe("override_function: force the function-graph form even when the function could be placed as an override event (#688)"),
    query: z.string().optional(),
    includeFunctions: z.boolean().optional(),
    includeInherited: z.boolean().optional().describe("list_functions: also list overridable parent/interface functions this Blueprint has not implemented (#809)"),
    blueprintPath: z.string().optional(), interfacePath: z.string().optional(),
    entryPoint: z.string().optional().describe("Event/function name to start exec-flow trace"),
    reverse: z.boolean().optional().describe("get_dependencies: true → referencers of this asset"),
    includeComponentProperties: z.boolean().optional().describe("read: dump UPROPERTY name/type/value per component template (#353/#370)"),
    sourcePath: z.string().optional(), destinationPath: z.string().optional(),
    nodeIds: z.array(z.string()).optional().describe("Node guids/names to export (omit = whole graph)"),
    t3d: z.string().optional().describe("T3D blob from export_nodes_t3d (or copied from the BP graph editor)"),
    className: z.string().optional().describe("C++ class name or path for set_cdo_property / get_cdo_properties"),
    propertyNames: z.array(z.string()).optional().describe("Property names to read for get_cdo_properties (omit = all)"),
    posX: z.number().optional().describe("Re-center pasted nodes around this X (with posY)"),
    posY: z.number().optional().describe("Re-center pasted nodes around this Y (with posX)"),
    offset: z.number().int().nonnegative().optional().describe("Pagination offset for read_graph"),
    limit: z.number().int().positive().optional().describe("Pagination limit for read_graph"),
    includePins: z.boolean().optional().describe("Include pins in read_graph results (default true)"),
    includeDefaults: z.boolean().optional().describe("Include pin default values in read_graph results (default true)"),
    includeComments: z.boolean().optional().describe("Include node comments in read_graph results (default true)"),
    dumpToFile: z.boolean().optional().describe("Write read_graph JSON to a file instead of returning nodes inline; without offset/limit this dumps the full graph, otherwise it dumps the requested slice"),
    outputPath: z.string().optional().describe("Absolute or Saved-relative JSON path for read_graph dumps; default path includes the asset, graph, and a path hash"),
    titleFilter: z.string().optional().describe("read_graph/read_graph_summary: case-insensitive substring match on node title (#560)"),
    classFilter: z.string().optional().describe("read_graph/read_graph_summary: case-insensitive substring match on node class name (#560)"),
    parameters: z.array(z.object({
      name: z.string(),
      type: z.string().optional(),
    })).optional().describe("add_event_dispatcher: typed signature parameters [{name, type}]. type is bool/int/float/string/name/text/vector/rotator/transform/object:/Script/Module.Class/struct:/Script/Module.Struct"),
    assetPaths: z.array(z.string()).optional().describe("Asset paths for compile_all"),
    save: z.boolean().optional().describe("compile_all: persist on success (default true)"),
    nodeId: z.string().optional().describe("Node guid or name (set_node_position)"),
    columnGap: z.number().optional().describe("auto_layout: horizontal spacing between columns (default 360)"),
    rowGap: z.number().optional().describe("auto_layout: vertical spacing between rows (default 200)"),
    connections: z.array(z.object({
      sourceNode: z.string(), sourcePin: z.string(),
      targetNode: z.string(), targetPin: z.string(),
    })).optional().describe("connect_pins_batch: per-connection records"),
  },
);
