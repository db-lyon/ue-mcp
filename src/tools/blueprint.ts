import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const blueprintTools: ToolDef[] = [
  bt("read_blueprint",
    "Read a Blueprint asset's full structure: parent class, interfaces, variables with defaults, " +
    "functions with parameters, graph names, and Simple Construction Script components.",
    { assetPath: z.string().describe("Path to the Blueprint asset") },
    "read_blueprint", (p) => ({ path: p.assetPath })),

  bt("list_blueprint_variables",
    "List all variables defined in a Blueprint, including their types, flags, and default values.",
    { assetPath: z.string().describe("Path to the Blueprint asset") },
    "list_blueprint_variables", (p) => ({ path: p.assetPath })),

  bt("list_blueprint_functions",
    "List all functions defined in a Blueprint, including their parameters and flags.",
    { assetPath: z.string().describe("Path to the Blueprint asset") },
    "list_blueprint_functions", (p) => ({ path: p.assetPath })),

  bt("read_blueprint_graph",
    "Read a specific graph within a Blueprint (e.g. EventGraph, a custom function graph). " +
    "Returns the nodes in the graph with their types and property data.",
    {
      assetPath: z.string().describe("Path to the Blueprint asset"),
      graphName: z.string().describe("Name of the graph to read (e.g. 'EventGraph', 'MyFunction')"),
    },
    "read_blueprint_graph", (p) => ({ path: p.assetPath, graphName: p.graphName })),

  bt("create_blueprint",
    "Create a new Blueprint asset with a specified parent class.",
    {
      path: z.string().describe("Asset path for the new Blueprint (e.g. '/Game/Blueprints/BP_MyActor')"),
      parentClass: z.string().optional().describe("Parent class name. Default: 'Actor'"),
    },
    "create_blueprint"),

  bt("add_blueprint_variable",
    "Add a new variable to a Blueprint.",
    {
      assetPath: z.string().describe("Path to the Blueprint asset"),
      name: z.string().describe("Variable name"),
      type: z.string().describe("Variable type (e.g. 'bool', 'float', 'int', 'Vector', 'String')"),
    },
    "add_variable", (p) => ({ path: p.assetPath, name: p.name, type: p.type })),

  bt("set_blueprint_variable_properties",
    "Set properties on a Blueprint variable: instance editable, blueprint read only, category, tooltip, replication.",
    {
      assetPath: z.string().describe("Path to the Blueprint asset"),
      name: z.string().describe("Name of the variable to modify"),
      instanceEditable: z.boolean().optional().describe("Make variable editable per-instance"),
      blueprintReadOnly: z.boolean().optional().describe("Make variable read-only in Blueprint graphs"),
      category: z.string().optional().describe("Variable category in the details panel"),
      tooltip: z.string().optional().describe("Tooltip text"),
      replicationType: z.string().optional().describe("Replication: 'none', 'replicated', or 'repNotify'"),
    },
    "set_variable_properties", (p) => ({ path: p.assetPath, name: p.name, instanceEditable: p.instanceEditable, blueprintReadOnly: p.blueprintReadOnly, category: p.category, tooltip: p.tooltip, replicationType: p.replicationType })),

  bt("create_blueprint_function",
    "Create a new function graph in a Blueprint.",
    {
      assetPath: z.string().describe("Path to the Blueprint asset"),
      functionName: z.string().describe("Name for the new function"),
    },
    "create_function", (p) => ({ path: p.assetPath, functionName: p.functionName })),

  bt("delete_blueprint_function",
    "Delete a function graph from a Blueprint.",
    {
      assetPath: z.string().describe("Path to the Blueprint asset"),
      functionName: z.string().describe("Name of the function to delete"),
    },
    "delete_function", (p) => ({ path: p.assetPath, functionName: p.functionName })),

  bt("rename_blueprint_function",
    "Rename a function or graph in a Blueprint.",
    {
      assetPath: z.string().describe("Path to the Blueprint asset"),
      oldName: z.string().describe("Current name"),
      newName: z.string().describe("New name"),
    },
    "rename_function", (p) => ({ path: p.assetPath, oldName: p.oldName, newName: p.newName })),

  bt("add_blueprint_node",
    "Add a node to a Blueprint graph. Use search_node_types to find the right node class first.",
    {
      assetPath: z.string().describe("Path to the Blueprint asset"),
      graphName: z.string().optional().describe("Graph name. Default: 'EventGraph'"),
      nodeClass: z.string().describe("Node class (e.g. 'K2Node_CallFunction', 'K2Node_IfThenElse')"),
      nodeParams: z.record(z.unknown()).optional().describe("Node-specific parameters (e.g. {memberName, targetClass})"),
    },
    "add_node", (p) => ({ path: p.assetPath, graphName: p.graphName ?? "EventGraph", nodeClass: p.nodeClass, nodeParams: p.nodeParams })),

  bt("delete_blueprint_node",
    "Delete a node from a Blueprint graph.",
    {
      assetPath: z.string().describe("Path to the Blueprint asset"),
      graphName: z.string().describe("Name of the graph containing the node"),
      nodeName: z.string().describe("Name of the node to delete"),
    },
    "delete_node", (p) => ({ path: p.assetPath, graphName: p.graphName, nodeName: p.nodeName })),

  bt("set_blueprint_node_property",
    "Set a property on a Blueprint graph node.",
    {
      assetPath: z.string().describe("Path to the Blueprint asset"),
      graphName: z.string().describe("Graph name"),
      nodeName: z.string().describe("Node name"),
      propertyName: z.string().describe("Property name"),
      value: z.unknown().describe("New value (as JSON)"),
    },
    "set_node_property", (p) => ({ path: p.assetPath, graphName: p.graphName, nodeName: p.nodeName, propertyName: p.propertyName, value: p.value })),

  bt("connect_blueprint_pins",
    "Connect two pins between Blueprint nodes.",
    {
      assetPath: z.string().describe("Path to the Blueprint asset"),
      sourceNode: z.string().describe("Source node name"),
      sourcePin: z.string().describe("Source pin name"),
      targetNode: z.string().describe("Target node name"),
      targetPin: z.string().describe("Target pin name"),
      graphName: z.string().optional().describe("Graph name. Default: 'EventGraph'"),
    },
    "connect_pins"),

  bt("add_blueprint_component",
    "Add a component to a Blueprint (e.g. StaticMeshComponent, BoxCollisionComponent).",
    {
      assetPath: z.string().describe("Path to the Blueprint asset"),
      componentClass: z.string().describe("Component class name"),
      componentName: z.string().optional().describe("Name for the component"),
    },
    "add_component", (p) => ({ path: p.assetPath, componentClass: p.componentClass, componentName: p.componentName ?? p.componentClass })),

  bt("compile_blueprint",
    "Compile a Blueprint and return the result.",
    { assetPath: z.string().describe("Path to the Blueprint asset") },
    "compile_blueprint", (p) => ({ path: p.assetPath })),

  bt("list_node_types",
    "List available Blueprint node types by category.",
    {
      category: z.string().optional().describe("Filter by category name"),
      includeFunctions: z.boolean().optional().describe("Include callable library functions"),
    },
    "list_node_types"),

  bt("search_node_types",
    "Search for Blueprint node types by name or description.",
    { query: z.string().describe("Search query") },
    "search_node_types"),

  bt("create_blueprint_interface",
    "Create a Blueprint Interface asset for polymorphic communication between Blueprints.",
    { path: z.string().describe("Asset path for the interface") },
    "create_blueprint_interface"),

  bt("add_blueprint_interface",
    "Add a Blueprint Interface to a Blueprint's implemented interfaces list.",
    {
      blueprintPath: z.string().describe("Path to the Blueprint"),
      interfacePath: z.string().describe("Path to the Blueprint Interface"),
    },
    "add_blueprint_interface"),

  bt("add_event_dispatcher",
    "Add an event dispatcher (multicast delegate) to a Blueprint.",
    {
      blueprintPath: z.string().describe("Path to the Blueprint"),
      name: z.string().describe("Name for the event dispatcher"),
    },
    "add_event_dispatcher"),
];
