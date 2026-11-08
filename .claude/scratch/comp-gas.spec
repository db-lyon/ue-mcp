TSharedPtr<FJsonValue> FGasHandlers::AddAbilitySystemComponent(
TSharedPtr<FJsonValue> FGasHandlers::AddAbilitySystemComponent(const TSharedPtr<FJsonObject>& Params)
{
	return MCPBlueprintComponent::AddOfClass(Params, TEXT("add_ability_system_component"),
		TEXT("/Script/GameplayAbilities.AbilitySystemComponent"), TEXT("AbilitySystemComp"), TEXT("Enable GameplayAbilities plugin."));
}
