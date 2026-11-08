	USCS_Node* TargetNode = nullptr;
	bool bCreated = false;
	if (BP->SimpleConstructionScript)
	{
		for (USCS_Node* N : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (N && N->ComponentTemplate && N->ComponentTemplate->GetClass() == CompClass)
			{
				TargetNode = N;
				break;
			}
		}
	}
	if (!TargetNode)
	{
		if (!BP->SimpleConstructionScript)
		{
			return MCPError(TEXT("Blueprint has no SimpleConstructionScript (not an Actor blueprint?), so it cannot hold an AIPerceptionComponent."));
		}
		if (auto Err = Edit.Begin()) return Err;
		TargetNode = BP->SimpleConstructionScript->CreateNode(CompClass, TEXT("AIPerceptionComp"));
		if (!TargetNode)
		{
			return MCPError(TEXT("Failed to create the AIPerceptionComponent SCS node."));
		}
		BP->SimpleConstructionScript->AddNode(TargetNode);
		bCreated = true;
	}
-----
	if (auto Err = MCPWorldSpawn::RequireConstructionScript(BP)) return Err;
	USCS_Node* TargetNode = MCPWorldSpawn::FindTemplateOfClass(BP, CompClass);
	const bool bCreated = TargetNode == nullptr;
	if (bCreated)
	{
		if (auto Err = Edit.Begin()) return Err;
		TargetNode = MCPWorldSpawn::AddTemplate(BP, CompClass, TEXT("AIPerceptionComp"));
		if (!TargetNode)
		{
			Edit.Cancel();
			return MCPError(TEXT("Failed to create the AIPerceptionComponent construction-script node."));
		}
	}
=====
	auto Result = MCPSuccess();
	if (bCreated) MCPSetCreated(Result); else MCPSetExisted(Result);
	Result->SetStringField(TEXT("blueprintPath"), BPPath);
	Result->SetStringField(TEXT("component"), TargetNode->GetVariableName().ToString());
	Result->SetArrayField(TEXT("senses"), ConfiguredSenses);
	if (AlreadyConfigured.Num() > 0)
	{
		Result->SetArrayField(TEXT("sensesAlreadyPresent"), AlreadyConfigured);
	}

	// Only offer the remove-component inverse when this call actually created
	// it. Removing a component that already existed is not an undo of adding
	// senses to it - it destroys something the caller did not ask us to touch.
	if (bCreated)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("path"), BPPath);
		Payload->SetStringField(TEXT("componentName"), TargetNode->GetVariableName().ToString());
		MCPSetRollback(Result, TEXT("remove_component"), Payload);
	}
-----
	auto Result = MCPSuccess();
	if (bCreated) MCPSetCreated(Result); else MCPSetExisted(Result);
	MCPWorldSpawn::DescribeTemplate(Result, BPPath, TargetNode);
	Result->SetArrayField(TEXT("senses"), ConfiguredSenses);
	if (AlreadyConfigured.Num() > 0)
	{
		Result->SetArrayField(TEXT("sensesAlreadyPresent"), AlreadyConfigured);
	}
	// The remove-component inverse only undoes a component this call created;
	// removing one that was already there would destroy what the caller kept.
	if (!bCreated) Result->RemoveField(TEXT("rollback"));
