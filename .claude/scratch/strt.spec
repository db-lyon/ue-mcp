		const FString CompName = OptionalString(Params, TEXT("componentName"));
		TArray<FString> Candidates;
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (!Comp) continue;
			FStructProperty* IP = CastField<FStructProperty>(Comp->GetClass()->FindPropertyByName(TEXT("InstanceData")));
			FStructProperty* RP = CastField<FStructProperty>(Comp->GetClass()->FindPropertyByName(TEXT("StateTreeRef")));
			const bool bShaped = IP && IP->Struct == FStateTreeInstanceData::StaticStruct()
				&& RP && RP->Struct == FStateTreeReference::StaticStruct();
			if (!bShaped) continue;
			Candidates.Add(Comp->GetName());
			if (!CompName.IsEmpty() && Comp->GetName() != CompName) continue;

			FStateTreeReference* Ref = RP->ContainerPtrToValuePtr<FStateTreeReference>(Comp);
			Out.Component = Comp;
			Out.StateTree = Ref ? Ref->GetStateTree() : nullptr;
			Out.InstanceData = IP->ContainerPtrToValuePtr<FStateTreeInstanceData>(Comp);
			break;
		}

		if (!Out.Component)
		{
			if (Candidates.Num() > 0 && !CompName.IsEmpty())
			{
				return MCPError(FString::Printf(
					TEXT("'%s' has no StateTree component named '%s'. It has: %s."),
					*OutActorLabel, *CompName, *FString::Join(Candidates, TEXT(", "))));
			}
-----
		const FString CompName = OptionalString(Params, TEXT("componentName"));
		const MCPStateTreeRuntime::FComponent Found = MCPStateTreeRuntime::Find(Actor, CompName);
		Out.Component = Found.Component;
		Out.StateTree = Found.StateTree;
		Out.InstanceData = Found.InstanceData;

		if (!Out.Component)
		{
			if (Found.Candidates.Num() > 0 && !CompName.IsEmpty())
			{
				return MCPError(FString::Printf(
					TEXT("'%s' has no StateTree component named '%s'. It has: %s."),
					*OutActorLabel, *CompName, *FString::Join(Found.Candidates, TEXT(", "))));
			}
