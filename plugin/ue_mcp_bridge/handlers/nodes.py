"""
Blueprint node discovery handlers — close the discoverability loop for Blueprint authoring.
Without these, add_blueprint_node requires memorizing K2Node class names.
"""

try:
    import unreal
    HAS_UNREAL = True
except ImportError:
    HAS_UNREAL = False


COMMON_NODES = {
    "Flow Control": [
        {"class": "K2Node_IfThenElse", "name": "Branch", "description": "If/else based on a boolean condition"},
        {"class": "K2Node_ExecutionSequence", "name": "Sequence", "description": "Execute multiple output pins in order"},
        {"class": "K2Node_SwitchInteger", "name": "Switch on Int", "description": "Switch execution based on integer value"},
        {"class": "K2Node_SwitchString", "name": "Switch on String", "description": "Switch execution based on string value"},
        {"class": "K2Node_SwitchName", "name": "Switch on Name", "description": "Switch execution based on FName value"},
        {"class": "K2Node_SwitchEnum", "name": "Switch on Enum", "description": "Switch execution based on enum value"},
        {"class": "K2Node_MultiGate", "name": "MultiGate", "description": "Route execution through multiple outputs"},
        {"class": "K2Node_DoOnceMultiInput", "name": "Do Once", "description": "Execute only once, then block"},
        {"class": "K2Node_FlipFlop", "name": "Flip Flop", "description": "Alternate between A and B outputs"},
        {"class": "K2Node_WhileLoop", "name": "While Loop", "description": "Loop while condition is true"},
        {"class": "K2Node_ForLoop", "name": "For Loop", "description": "Loop from start to end index"},
        {"class": "K2Node_ForEachLoop", "name": "For Each Loop", "description": "Iterate over an array"},
        {"class": "K2Node_Gate", "name": "Gate", "description": "Controllable gate for execution flow"},
    ],
    "Events": [
        {"class": "K2Node_Event", "name": "Custom Event", "description": "Define a custom event entry point"},
        {"class": "K2Node_InputAction", "name": "Input Action", "description": "Triggered by an input action binding"},
        {"class": "K2Node_InputKey", "name": "Input Key", "description": "Triggered by a specific key press"},
        {"class": "K2Node_ComponentBoundEvent", "name": "Component Event", "description": "Triggered by a component delegate"},
        {"class": "K2Node_ActorBoundEvent", "name": "Actor Event", "description": "Triggered by an actor delegate"},
    ],
    "Functions": [
        {"class": "K2Node_CallFunction", "name": "Call Function", "description": "Call a function by name. Set memberName param."},
        {"class": "K2Node_CallParentFunction", "name": "Call Parent Function", "description": "Call the parent class implementation"},
        {"class": "K2Node_CallArrayFunction", "name": "Call Array Function", "description": "Array utility function call"},
        {"class": "K2Node_CallDataTableFunction", "name": "DataTable Function", "description": "DataTable operation function call"},
        {"class": "K2Node_CallDelegate", "name": "Call Delegate", "description": "Invoke a delegate/dispatcher"},
        {"class": "K2Node_CreateDelegate", "name": "Create Delegate", "description": "Create a delegate binding"},
        {"class": "K2Node_AddDelegate", "name": "Bind Event", "description": "Bind a function to a delegate"},
        {"class": "K2Node_RemoveDelegate", "name": "Unbind Event", "description": "Remove a function binding from a delegate"},
    ],
    "Variables": [
        {"class": "K2Node_VariableGet", "name": "Get Variable", "description": "Read a variable value"},
        {"class": "K2Node_VariableSet", "name": "Set Variable", "description": "Write a variable value"},
        {"class": "K2Node_Self", "name": "Self Reference", "description": "Reference to the owning actor/object"},
        {"class": "K2Node_PromotableOperator", "name": "Promotable Operator", "description": "Math/comparison that auto-promotes types"},
    ],
    "Casting & Type": [
        {"class": "K2Node_DynamicCast", "name": "Cast To", "description": "Dynamic cast to a specific class"},
        {"class": "K2Node_ClassDynamicCast", "name": "Class Cast", "description": "Cast a class reference"},
        {"class": "K2Node_CastByteToEnum", "name": "Byte to Enum", "description": "Convert byte to enum value"},
    ],
    "Object Lifecycle": [
        {"class": "K2Node_SpawnActorFromClass", "name": "Spawn Actor", "description": "Spawn an actor of a given class in the world"},
        {"class": "K2Node_ConstructObjectFromClass", "name": "Construct Object", "description": "Construct a UObject of a given class"},
        {"class": "K2Node_DestroyActor", "name": "Destroy Actor", "description": "Destroy the calling actor (or self)"},
    ],
    "Struct": [
        {"class": "K2Node_BreakStruct", "name": "Break Struct", "description": "Split a struct into its individual fields"},
        {"class": "K2Node_MakeStruct", "name": "Make Struct", "description": "Construct a struct from individual field values"},
        {"class": "K2Node_SetFieldsInStruct", "name": "Set Fields in Struct", "description": "Set specific fields on an existing struct"},
    ],
    "Math": [
        {"class": "K2Node_CommutativeAssociativeBinaryOperator", "name": "Math Operator", "description": "Add, multiply, etc. Auto-promotes types."},
        {"class": "K2Node_MakeArray", "name": "Make Array", "description": "Construct an array from individual elements"},
        {"class": "K2Node_MakeMap", "name": "Make Map", "description": "Construct a map from key-value pairs"},
        {"class": "K2Node_MakeSet", "name": "Make Set", "description": "Construct a set from elements"},
    ],
    "Utility": [
        {"class": "K2Node_Delay", "name": "Delay", "description": "Wait for a duration then continue execution"},
        {"class": "K2Node_Timeline", "name": "Timeline", "description": "Keyframe-driven value interpolation over time"},
        {"class": "K2Node_PrintString", "name": "Print String", "description": "Print a debug string to screen and/or log"},
        {"class": "K2Node_Select", "name": "Select", "description": "Choose between values based on index or bool"},
        {"class": "K2Node_FormatText", "name": "Format Text", "description": "Format text with named arguments"},
        {"class": "K2Node_GetArrayItem", "name": "Get Array Element", "description": "Get an element from an array by index"},
    ],
}


def list_node_types(params: dict) -> dict:
    """List available Blueprint node types by category."""
    category = params.get("category", None)
    include_function_nodes = params.get("includeFunctions", False)

    if category:
        matching = {}
        for cat, nodes in COMMON_NODES.items():
            if category.lower() in cat.lower():
                matching[cat] = nodes
        if not matching:
            return {
                "error": f"Category '{category}' not found",
                "availableCategories": list(COMMON_NODES.keys()),
            }
        result = {
            "categories": matching,
            "totalNodes": sum(len(n) for n in matching.values()),
        }
    else:
        result = {
            "categories": COMMON_NODES,
            "totalNodes": sum(len(n) for n in COMMON_NODES.values()),
        }

    if include_function_nodes and HAS_UNREAL:
        try:
            function_nodes = _discover_callable_functions()
            result["callableFunctions"] = function_nodes
        except Exception as e:
            result["callableFunctionsError"] = str(e)

    return result


def search_node_types(params: dict) -> dict:
    """Search for Blueprint node types by name or description."""
    query = params.get("query", "").lower()
    if not query:
        return {"error": "query parameter is required"}

    results = []
    for cat, nodes in COMMON_NODES.items():
        for node in nodes:
            if (query in node["name"].lower() or
                query in node["class"].lower() or
                query in node["description"].lower()):
                results.append({**node, "category": cat})

    if HAS_UNREAL:
        try:
            fn_results = _search_callable_functions(query)
            results.extend(fn_results)
        except Exception:
            pass

    return {
        "query": query,
        "resultCount": len(results),
        "results": results,
    }


def _discover_callable_functions() -> list[dict]:
    """Discover commonly-used BlueprintCallable functions from loaded classes."""
    results = []
    common_classes = ["KismetSystemLibrary", "KismetMathLibrary", "KismetStringLibrary",
                      "KismetArrayLibrary", "GameplayStatics", "KismetTextLibrary"]

    for class_name in common_classes:
        cls = getattr(unreal, class_name, None)
        if cls is None:
            continue
        for name in dir(cls):
            if name.startswith("_"):
                continue
            attr = getattr(cls, name, None)
            if callable(attr) and not isinstance(attr, type):
                doc = getattr(attr, "__doc__", "") or ""
                results.append({
                    "class": "K2Node_CallFunction",
                    "name": f"{class_name}::{name}",
                    "description": doc[:100] if doc else f"Call {class_name}.{name}",
                    "nodeParams": f'{{"memberName": "{name}", "targetClass": "{class_name}"}}',
                    "category": "Library Functions",
                })

    return results[:200]


def _search_callable_functions(query: str) -> list[dict]:
    """Search for callable functions matching a query."""
    all_funcs = _discover_callable_functions()
    return [f for f in all_funcs if query in f["name"].lower() or query in f["description"].lower()][:50]


HANDLERS = {
    "list_node_types": list_node_types,
    "search_node_types": search_node_types,
}
