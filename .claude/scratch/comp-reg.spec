	Registry.RegisterHandler(TEXT("add_state_tree_component"), &AddStateTreeComponent, {
		MCPParam::Required(TEXT("blueprintPath"), EType::String, TEXT("Blueprint asset path")),
	});
	Registry.RegisterHandler(TEXT("add_smart_object_component"), &AddSmartObjectComponent, {
		MCPParam::Required(TEXT("blueprintPath"), EType::String, TEXT("Blueprint asset path")),
	});
-----
	Registry.RegisterHandler(TEXT("add_state_tree_component"), &AddStateTreeComponent, {
		MCPParam::Required(TEXT("blueprintPath"), EType::String, TEXT("Blueprint asset path")),
		MCPParam::Optional(TEXT("componentName"), EType::String, TEXT("Name of the new component (default StateTreeComp)")),
	});
	Registry.RegisterHandler(TEXT("add_smart_object_component"), &AddSmartObjectComponent, {
		MCPParam::Required(TEXT("blueprintPath"), EType::String, TEXT("Blueprint asset path")),
		MCPParam::Optional(TEXT("componentName"), EType::String, TEXT("Name of the new component (default SmartObjectComp)")),
	});
