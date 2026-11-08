TSharedPtr<FJsonValue> FGameplayHandlers::AddStateTreeComponent(
TSharedPtr<FJsonValue> FGameplayHandlers::AddStateTreeComponent(const TSharedPtr<FJsonObject>& Params)
{
	return MCPBlueprintComponent::AddOfClass(Params, TEXT("add_state_tree_component"),
		TEXT("/Script/StateTreeModule.StateTreeComponent"), TEXT("StateTreeComp"), TEXT("Enable StateTree plugin."));
}
=====
TSharedPtr<FJsonValue> FGameplayHandlers::AddSmartObjectComponent(
TSharedPtr<FJsonValue> FGameplayHandlers::AddSmartObjectComponent(const TSharedPtr<FJsonObject>& Params)
{
	return MCPBlueprintComponent::AddOfClass(Params, TEXT("add_smart_object_component"),
		TEXT("/Script/SmartObjectsModule.SmartObjectComponent"), TEXT("SmartObjectComp"), TEXT("Enable SmartObjects plugin."));
}
