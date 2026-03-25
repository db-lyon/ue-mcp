import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const blueprintTool: ToolDef = categoryTool(
  "blueprint",
  "Blueprint reading, authoring, and compilation. Covers variables, functions, graphs, nodes, components, interfaces, and event dispatchers.",
  {
    read:              bp("read_blueprint", (p) => ({ path: p.assetPath })),
    list_variables:    bp("list_blueprint_variables", (p) => ({ path: p.assetPath })),
    list_functions:    bp("list_blueprint_functions", (p) => ({ path: p.assetPath })),
    read_graph:        bp("read_blueprint_graph", (p) => ({ path: p.assetPath, graphName: p.graphName })),
    create:            bp("create_blueprint", (p) => ({ path: p.assetPath, parentClass: p.parentClass })),
    add_variable:      bp("add_variable", (p) => ({ path: p.assetPath, name: p.name, type: p.varType })),
    set_variable_properties: bp("set_variable_properties", (p) => ({ path: p.assetPath, name: p.name, instanceEditable: p.instanceEditable, blueprintReadOnly: p.blueprintReadOnly, category: p.category, tooltip: p.tooltip, replicationType: p.replicationType })),
    create_function:   bp("create_function", (p) => ({ path: p.assetPath, functionName: p.functionName })),
    delete_function:   bp("delete_function", (p) => ({ path: p.assetPath, functionName: p.functionName })),
    rename_function:   bp("rename_function", (p) => ({ path: p.assetPath, oldName: p.oldName, newName: p.newName })),
    add_node:          bp("add_node", (p) => ({ path: p.assetPath, graphName: p.graphName ?? "EventGraph", nodeClass: p.nodeClass, nodeParams: p.nodeParams })),
    delete_node:       bp("delete_node", (p) => ({ path: p.assetPath, graphName: p.graphName, nodeName: p.nodeName })),
    set_node_property: bp("set_node_property", (p) => ({ path: p.assetPath, graphName: p.graphName, nodeName: p.nodeName, propertyName: p.propertyName, value: p.value })),
    connect_pins:      bp("connect_pins"),
    add_component:     bp("add_component", (p) => ({ path: p.assetPath, componentClass: p.componentClass, componentName: p.componentName ?? p.componentClass })),
    set_component_property: bp("set_component_property", (p) => ({ path: p.assetPath, componentName: p.componentName, propertyName: p.propertyName, value: p.value })),
    compile:           bp("compile_blueprint", (p) => ({ path: p.assetPath })),
    list_node_types:   bp("list_node_types"),
    search_node_types: bp("search_node_types"),
    create_interface:  bp("create_blueprint_interface", (p) => ({ path: p.assetPath })),
    add_interface:     bp("add_blueprint_interface"),
    list_graphs:           bp("list_blueprint_graphs", (p) => ({ path: p.assetPath })),
    add_event_dispatcher: bp("add_event_dispatcher"),
  },
  `- read: Read BP structure incl. SCS components (name, class, mesh, materials, transform). Params: assetPath
- list_variables: List variables. Params: assetPath
- list_functions: List functions/graphs. Params: assetPath
- list_graphs: List all graphs in a blueprint (EventGraph, AnimGraph, functions). Params: assetPath
- read_graph: Read graph nodes. Params: assetPath, graphName
- create: Create Blueprint. Params: assetPath, parentClass?
- add_variable: Add variable. Params: assetPath, name, varType
- set_variable_properties: Edit variable. Params: assetPath, name, instanceEditable?, blueprintReadOnly?, category?, tooltip?, replicationType?
- create_function: Create function. Params: assetPath, functionName
- delete_function: Delete function. Params: assetPath, functionName
- rename_function: Rename function. Params: assetPath, oldName, newName
- add_node: Add graph node (any UEdGraphNode subclass). Aliases: CallFunction, Event, CustomEvent, GetVar, SetVar, Branch. nodeParams: functionName+targetClass for CallFunction, eventName+eventClass? for Event, variableName for GetVar/SetVar. Params: assetPath, graphName?, nodeClass, nodeParams?
- delete_node: Delete node. Params: assetPath, graphName, nodeName
- set_node_property: Set node pin default or struct property (supports dot paths like "Node.IKBone.BoneName"). Params: assetPath, graphName, nodeName, propertyName, value
- connect_pins: Wire nodes. Params: sourceNode, sourcePin, targetNode, targetPin, assetPath, graphName?
- add_component: Add BP component. Params: assetPath, componentClass, componentName?
- set_component_property: Set property on SCS component template (supports dot paths, asset paths for object refs). Params: assetPath, componentName, propertyName, value
- compile: Compile Blueprint. Params: assetPath
- list_node_types: List node types. Params: category?, includeFunctions?
- search_node_types: Search nodes. Params: query
- create_interface: Create BP Interface. Params: assetPath
- add_interface: Implement interface. Params: blueprintPath, interfacePath
- add_event_dispatcher: Add dispatcher. Params: blueprintPath, name`,
  {
    assetPath: z.string().optional().describe("Blueprint asset path"),
    graphName: z.string().optional(), functionName: z.string().optional(),
    name: z.string().optional(), varType: z.string().optional().describe("Variable type"),
    parentClass: z.string().optional(),
    instanceEditable: z.boolean().optional(), blueprintReadOnly: z.boolean().optional(),
    category: z.string().optional(), tooltip: z.string().optional(),
    replicationType: z.string().optional(),
    oldName: z.string().optional(), newName: z.string().optional(),
    nodeClass: z.string().optional(), nodeParams: z.record(z.unknown()).optional(),
    nodeName: z.string().optional(), propertyName: z.string().optional(),
    value: z.unknown().optional(),
    sourceNode: z.string().optional(), sourcePin: z.string().optional(),
    targetNode: z.string().optional(), targetPin: z.string().optional(),
    componentClass: z.string().optional(), componentName: z.string().optional(),
    query: z.string().optional(),
    includeFunctions: z.boolean().optional(),
    blueprintPath: z.string().optional(), interfacePath: z.string().optional(),
  },
);
