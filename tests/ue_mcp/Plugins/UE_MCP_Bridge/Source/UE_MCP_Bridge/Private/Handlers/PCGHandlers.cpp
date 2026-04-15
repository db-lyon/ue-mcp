#include "PCGHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/TopLevelAssetPath.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PCGGraph.h"
// PCGGraphInterface.h may not be directly includable in 5.7
#include "PCGComponent.h"
#include "PCGNode.h"
#include "PCGSettings.h"
#include "PCGPin.h"
#include "PCGEdge.h"
#include "PCGVolume.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "GameFramework/Actor.h"

void FPCGHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("list_pcg_graphs"), &ListPCGGraphs);
	Registry.RegisterHandler(TEXT("get_pcg_components"), &GetPCGComponents);
	Registry.RegisterHandler(TEXT("create_pcg_graph"), &CreatePCGGraph);
	Registry.RegisterHandler(TEXT("read_pcg_graph"), &ReadPCGGraph);
	Registry.RegisterHandler(TEXT("add_pcg_node"), &AddPCGNode);
	Registry.RegisterHandler(TEXT("connect_pcg_nodes"), &ConnectPCGNodes);
	Registry.RegisterHandler(TEXT("remove_pcg_node"), &RemovePCGNode);
	Registry.RegisterHandler(TEXT("set_pcg_node_settings"), &SetPCGNodeSettings);
	Registry.RegisterHandler(TEXT("execute_pcg_graph"), &ExecutePCGGraph);
	Registry.RegisterHandler(TEXT("spawn_pcg_volume"), &SpawnPCGVolume);
	Registry.RegisterHandler(TEXT("add_pcg_volume"), &SpawnPCGVolume);
	Registry.RegisterHandler(TEXT("read_pcg_node_settings"), &ReadPCGNodeSettings);
	Registry.RegisterHandler(TEXT("get_pcg_component_details"), &GetPCGComponentDetails);
}

TSharedPtr<FJsonValue> FPCGHandlers::ListPCGGraphs(const TSharedPtr<FJsonObject>& Params)
{
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> Assets;
	AR.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/PCG"), TEXT("PCGGraph")), Assets, true);

	TArray<TSharedPtr<FJsonValue>> AssetArray;
	for (const FAssetData& Asset : Assets)
	{
		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		AssetObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		AssetArray.Add(MakeShared<FJsonValueObject>(AssetObj));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("graphs"), AssetArray);
	Result->SetNumberField(TEXT("count"), AssetArray.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::GetPCGComponents(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	TArray<TSharedPtr<FJsonValue>> CompArray;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (!Actor) continue;

		TArray<UPCGComponent*> PCGComps;
		Actor->GetComponents<UPCGComponent>(PCGComps);
		for (UPCGComponent* PCGComp : PCGComps)
		{
			if (!PCGComp) continue;
			TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
			CompObj->SetStringField(TEXT("actorName"), Actor->GetActorLabel());
			CompObj->SetStringField(TEXT("actorClass"), Actor->GetClass()->GetName());
			CompObj->SetStringField(TEXT("componentName"), PCGComp->GetName());
			if (PCGComp->GetGraph())
			{
				CompObj->SetStringField(TEXT("graphName"), PCGComp->GetGraph()->GetName());
			}
			CompArray.Add(MakeShared<FJsonValueObject>(CompObj));
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("components"), CompArray);
	Result->SetNumberField(TEXT("count"), CompArray.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::CreatePCGGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/PCG"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (auto Existing = MCPCheckAssetExists(PackagePath, Name, OnConflict, TEXT("PCGGraph")))
	{
		return Existing;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, UPCGGraph::StaticClass(), nullptr);
	if (!NewAsset)
	{
		return MCPError(TEXT("Failed to create PCGGraph"));
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Result, NewAsset->GetPathName());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::ReadPCGGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph)
	{
		return MCPError(FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("name"), Graph->GetName());
	Result->SetStringField(TEXT("path"), AssetPath);

	const auto& Nodes = Graph->GetNodes();
	TArray<TSharedPtr<FJsonValue>> NodeArray;
	for (const UPCGNode* Node : Nodes)
	{
		if (!Node) continue;
		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("name"), Node->GetName());
		NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(EPCGNodeTitleType::ListView).ToString());
		NodeArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	Result->SetArrayField(TEXT("nodes"), NodeArray);
	Result->SetNumberField(TEXT("nodeCount"), NodeArray.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::AddPCGNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString NodeType;
	if (auto Err = RequireString(Params, TEXT("nodeType"), NodeType)) return Err;

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph)
	{
		return MCPError(FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));
	}

	// Find the settings class by name
	UClass* SettingsClass = FindObject<UClass>(nullptr, *NodeType);
	if (!SettingsClass)
	{
		// Try with /Script/PCG prefix
		SettingsClass = FindObject<UClass>(nullptr, *(TEXT("/Script/PCG.") + NodeType));
	}
	if (!SettingsClass)
	{
		// Try with U prefix stripped
		FString CleanName = NodeType;
		if (!CleanName.StartsWith(TEXT("U")))
		{
			SettingsClass = FindObject<UClass>(nullptr, *(TEXT("/Script/PCG.U") + CleanName));
		}
	}
	if (!SettingsClass || !SettingsClass->IsChildOf(UPCGSettings::StaticClass()))
	{
		return MCPError(FString::Printf(TEXT("PCG settings class not found or invalid: %s"), *NodeType));
	}

	// Create default settings object
	UPCGSettings* DefaultSettings = NewObject<UPCGSettings>(GetTransientPackage(), SettingsClass);
	if (!DefaultSettings)
	{
		return MCPError(TEXT("Failed to create PCG settings instance"));
	}

	// Add node to graph
	UPCGNode* NewNode = Graph->AddNode(DefaultSettings);
	if (!NewNode)
	{
		return MCPError(TEXT("Failed to add node to PCG graph"));
	}

	// Set position if provided
	double PosX = 0, PosY = 0;
	if (Params->TryGetNumberField(TEXT("posX"), PosX) || Params->TryGetNumberField(TEXT("posY"), PosY))
	{
		NewNode->PositionX = (int32)PosX;
		NewNode->PositionY = (int32)PosY;
	}

	// Save the graph asset
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	const FString NewNodeName = NewNode->GetName();
	Result->SetStringField(TEXT("nodeName"), NewNodeName);
	Result->SetStringField(TEXT("nodeType"), NodeType);
	Result->SetStringField(TEXT("nodeTitle"), NewNode->GetNodeTitle(EPCGNodeTitleType::ListView).ToString());

	// Rollback: remove_pcg_node
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("nodeName"), NewNodeName);
	MCPSetRollback(Result, TEXT("remove_pcg_node"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::ConnectPCGNodes(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString SourceNodeName;
	if (auto Err = RequireStringAlt(Params, TEXT("sourceNodeName"), TEXT("sourceNode"), SourceNodeName)) return Err;

	FString TargetNodeName;
	if (auto Err = RequireStringAlt(Params, TEXT("targetNodeName"), TEXT("targetNode"), TargetNodeName)) return Err;

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph)
	{
		return MCPError(FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));
	}

	// Find source and target nodes
	UPCGNode* SourceNode = nullptr;
	UPCGNode* TargetNode = nullptr;
	const auto& Nodes = Graph->GetNodes();
	for (UPCGNode* Node : Nodes)
	{
		if (!Node) continue;
		if (Node->GetName() == SourceNodeName)
		{
			SourceNode = Node;
		}
		if (Node->GetName() == TargetNodeName)
		{
			TargetNode = Node;
		}
	}

	// Also check the input and output nodes
	if (!SourceNode && Graph->GetInputNode() && Graph->GetInputNode()->GetName() == SourceNodeName)
	{
		SourceNode = Graph->GetInputNode();
	}
	if (!TargetNode && Graph->GetOutputNode() && Graph->GetOutputNode()->GetName() == TargetNodeName)
	{
		TargetNode = Graph->GetOutputNode();
	}

	if (!SourceNode)
	{
		return MCPError(FString::Printf(TEXT("Source node not found: %s"), *SourceNodeName));
	}
	if (!TargetNode)
	{
		return MCPError(FString::Printf(TEXT("Target node not found: %s"), *TargetNodeName));
	}

	// Get pin labels if specified, otherwise use the first available pins
	FString SourcePinLabel;
	if (!Params->TryGetStringField(TEXT("sourcePinLabel"), SourcePinLabel))
	{
		Params->TryGetStringField(TEXT("sourcePin"), SourcePinLabel);
	}
	FString TargetPinLabel;
	if (!Params->TryGetStringField(TEXT("targetPinLabel"), TargetPinLabel))
	{
		Params->TryGetStringField(TEXT("targetPin"), TargetPinLabel);
	}

	// UE 5.7: Pin and edge APIs refactored; use Graph->AddEdge() with node+label
	// Resolve the pin labels to use for the connection
	FName ResolvedSourcePinLabel = NAME_None;
	FName ResolvedTargetPinLabel = NAME_None;

	if (SourcePinLabel.IsEmpty())
	{
		// Use the first output pin's label
		const TArray<TObjectPtr<UPCGPin>>& OutPins = SourceNode->GetOutputPins();
		if (OutPins.Num() > 0 && OutPins[0])
		{
			ResolvedSourcePinLabel = OutPins[0]->Properties.Label;
		}
	}
	else
	{
		ResolvedSourcePinLabel = FName(*SourcePinLabel);
	}

	if (TargetPinLabel.IsEmpty())
	{
		// Use the first input pin's label
		const TArray<TObjectPtr<UPCGPin>>& InPins = TargetNode->GetInputPins();
		if (InPins.Num() > 0 && InPins[0])
		{
			ResolvedTargetPinLabel = InPins[0]->Properties.Label;
		}
	}
	else
	{
		ResolvedTargetPinLabel = FName(*TargetPinLabel);
	}

	if (ResolvedSourcePinLabel == NAME_None)
	{
		return MCPError(TEXT("No suitable output pin found on source node"));
	}
	if (ResolvedTargetPinLabel == NAME_None)
	{
		return MCPError(TEXT("No suitable input pin found on target node"));
	}

	UPCGNode* ResultNode = Graph->AddEdge(SourceNode, ResolvedSourcePinLabel, TargetNode, ResolvedTargetPinLabel);
	if (!ResultNode)
	{
		return MCPError(TEXT("Failed to connect pins - connection may already exist or be incompatible"));
	}

	// Save the graph asset
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("sourceNodeName"), SourceNodeName);
	Result->SetStringField(TEXT("targetNodeName"), TargetNodeName);
	Result->SetStringField(TEXT("sourcePinLabel"), ResolvedSourcePinLabel.ToString());
	Result->SetStringField(TEXT("targetPinLabel"), ResolvedTargetPinLabel.ToString());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::RemovePCGNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString NodeName;
	if (auto Err = RequireString(Params, TEXT("nodeName"), NodeName)) return Err;

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph)
	{
		return MCPError(FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));
	}

	// Find the node by name
	UPCGNode* FoundNode = nullptr;
	const auto& Nodes = Graph->GetNodes();
	for (UPCGNode* Node : Nodes)
	{
		if (Node && Node->GetName() == NodeName)
		{
			FoundNode = Node;
			break;
		}
	}

	if (!FoundNode)
	{
		// Idempotent: node already absent
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("assetPath"), AssetPath);
		Noop->SetStringField(TEXT("nodeName"), NodeName);
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Noop);
	}

	// Remove the node from the graph
	Graph->RemoveNode(FoundNode);

	// Save the graph asset
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("removedNodeName"), NodeName);
	Result->SetBoolField(TEXT("deleted"), true);
	// No rollback: removal of PCG node not reversible without snapshotting settings + connections.
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::SetPCGNodeSettings(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString NodeName;
	if (auto Err = RequireString(Params, TEXT("nodeName"), NodeName)) return Err;

	// Accept either a 'settings' object (key-value pairs) or individual 'propertyName'/'propertyValue'
	const TSharedPtr<FJsonObject>* SettingsObj = nullptr;
	FString PropertyName;
	FString PropertyValue;
	bool bUseSettingsObject = Params->TryGetObjectField(TEXT("settings"), SettingsObj) && SettingsObj && (*SettingsObj).IsValid();
	bool bUseSingleProperty = !bUseSettingsObject && Params->TryGetStringField(TEXT("propertyName"), PropertyName);

	if (!bUseSettingsObject && !bUseSingleProperty)
	{
		return MCPError(TEXT("Missing 'settings' object or 'propertyName'/'propertyValue' parameters"));
	}

	if (bUseSingleProperty && !Params->TryGetStringField(TEXT("propertyValue"), PropertyValue))
	{
		return MCPError(TEXT("Missing 'propertyValue' parameter"));
	}

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph)
	{
		return MCPError(FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));
	}

	// Find the node by name
	UPCGNode* FoundNode = nullptr;
	const auto& Nodes = Graph->GetNodes();
	for (UPCGNode* Node : Nodes)
	{
		if (Node && Node->GetName() == NodeName)
		{
			FoundNode = Node;
			break;
		}
	}

	if (!FoundNode)
	{
		return MCPError(FString::Printf(TEXT("Node not found: %s"), *NodeName));
	}

	// Get the settings object from the node
	UPCGSettings* Settings = const_cast<UPCGSettings*>(FoundNode->GetSettings());
	if (!Settings)
	{
		return MCPError(TEXT("Node has no settings object"));
	}

	// Build a list of property name/value pairs to set
	TArray<TPair<FString, FString>> PropertiesToSet;
	if (bUseSettingsObject)
	{
		for (const auto& Pair : (*SettingsObj)->Values)
		{
			FString ValStr;
			if (Pair.Value->TryGetString(ValStr))
			{
				PropertiesToSet.Add(TPair<FString, FString>(Pair.Key, ValStr));
			}
			else if (Pair.Value->Type == EJson::Number)
			{
				ValStr = FString::SanitizeFloat(Pair.Value->AsNumber());
				PropertiesToSet.Add(TPair<FString, FString>(Pair.Key, ValStr));
			}
			else if (Pair.Value->Type == EJson::Boolean)
			{
				ValStr = Pair.Value->AsBool() ? TEXT("true") : TEXT("false");
				PropertiesToSet.Add(TPair<FString, FString>(Pair.Key, ValStr));
			}
			else
			{
				// For complex types, serialize to string
				ValStr = Pair.Value->AsString();
				PropertiesToSet.Add(TPair<FString, FString>(Pair.Key, ValStr));
			}
		}
	}
	else
	{
		PropertiesToSet.Add(TPair<FString, FString>(PropertyName, PropertyValue));
	}

	TSharedPtr<FJsonObject> SetResults = MakeShared<FJsonObject>();
	TArray<FString> Errors;

	for (const auto& Prop : PropertiesToSet)
	{
		FProperty* Property = Settings->GetClass()->FindPropertyByName(FName(*Prop.Key));
		if (!Property)
		{
			Errors.Add(FString::Printf(TEXT("Property '%s' not found on settings"), *Prop.Key));
			continue;
		}

		void* PropertyAddr = Property->ContainerPtrToValuePtr<void>(Settings);
		const TCHAR* ImportResult = Property->ImportText_Direct(*Prop.Value, PropertyAddr, Settings, PPF_None);
		if (ImportResult == nullptr)
		{
			Errors.Add(FString::Printf(TEXT("Failed to set property '%s' to value '%s'"), *Prop.Key, *Prop.Value));
		}
		else
		{
			SetResults->SetStringField(Prop.Key, Prop.Value);
		}
	}

	// Save the graph asset
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("nodeName"), NodeName);
	Result->SetObjectField(TEXT("setProperties"), SetResults);
	if (Errors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrorArray;
		for (const FString& Err : Errors)
		{
			ErrorArray.Add(MakeShared<FJsonValueString>(Err));
		}
		Result->SetArrayField(TEXT("errors"), ErrorArray);
	}
	Result->SetBoolField(TEXT("success"), Errors.Num() == 0);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::ExecutePCGGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	// Find actor by label
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (Actor && Actor->GetActorLabel() == ActorLabel)
		{
			FoundActor = Actor;
			break;
		}
	}

	if (!FoundActor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found with label: %s"), *ActorLabel));
	}

	// Get PCG component from the actor
	UPCGComponent* PCGComp = FoundActor->FindComponentByClass<UPCGComponent>();
	if (!PCGComp)
	{
		return MCPError(FString::Printf(TEXT("No PCGComponent found on actor: %s"), *ActorLabel));
	}

	// Set seed if provided
	double Seed = 0;
	if (Params->TryGetNumberField(TEXT("seed"), Seed))
	{
		PCGComp->Seed = (int32)Seed;
	}

	// Trigger generation
	PCGComp->Generate();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("componentName"), PCGComp->GetName());
	if (PCGComp->GetGraph())
	{
		Result->SetStringField(TEXT("graphName"), PCGComp->GetGraph()->GetName());
	}
	Result->SetNumberField(TEXT("seed"), PCGComp->Seed);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::SpawnPCGVolume(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	const FString Label = OptionalString(Params, TEXT("label"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (!Label.IsEmpty())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() == Label)
			{
				if (OnConflict == TEXT("error"))
				{
					return MCPError(FString::Printf(TEXT("PCGVolume '%s' already exists"), *Label));
				}
				auto Existing = MCPSuccess();
				MCPSetExisted(Existing);
				Existing->SetStringField(TEXT("actorLabel"), Label);
				return MCPResult(Existing);
			}
		}
	}

	// Parse location
	FVector Location = FVector::ZeroVector;
	double X = 0, Y = 0, Z = 0;
	Params->TryGetNumberField(TEXT("x"), X);
	Params->TryGetNumberField(TEXT("y"), Y);
	Params->TryGetNumberField(TEXT("z"), Z);
	Location = FVector(X, Y, Z);

	// Parse bounds extent
	FVector Extent = FVector(500.0, 500.0, 500.0);
	double ExtentX = 500, ExtentY = 500, ExtentZ = 500;
	if (Params->TryGetNumberField(TEXT("extentX"), ExtentX) ||
		Params->TryGetNumberField(TEXT("extentY"), ExtentY) ||
		Params->TryGetNumberField(TEXT("extentZ"), ExtentZ))
	{
		Extent = FVector(ExtentX, ExtentY, ExtentZ);
	}

	// Spawn PCG Volume actor
	FTransform SpawnTransform(FRotator::ZeroRotator, Location);
	APCGVolume* PCGVolumeActor = World->SpawnActor<APCGVolume>(APCGVolume::StaticClass(), SpawnTransform);
	if (!PCGVolumeActor)
	{
		return MCPError(TEXT("Failed to spawn PCGVolume actor"));
	}

	PCGVolumeActor->SetActorScale3D(Extent / 100.0);

	if (!Label.IsEmpty())
	{
		PCGVolumeActor->SetActorLabel(Label);
	}

	FString GraphPath = OptionalString(Params, TEXT("graphPath"));
	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	TSharedPtr<FJsonObject> RBPayload = MakeShared<FJsonObject>();
	RBPayload->SetStringField(TEXT("actorLabel"), PCGVolumeActor->GetActorLabel());
	MCPSetRollback(Result, TEXT("delete_actor"), RBPayload);

	if (!GraphPath.IsEmpty())
	{
		UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
		if (Graph)
		{
			UPCGComponent* PCGComp = PCGVolumeActor->FindComponentByClass<UPCGComponent>();
			if (PCGComp)
			{
				PCGComp->SetGraph(Graph);
				Result->SetStringField(TEXT("graphPath"), GraphPath);
				Result->SetStringField(TEXT("graphName"), Graph->GetName());
			}
		}
		else
		{
			Result->SetStringField(TEXT("warning"), FString::Printf(TEXT("PCGGraph not found: %s - volume spawned without graph"), *GraphPath));
		}
	}

	Result->SetStringField(TEXT("actorName"), PCGVolumeActor->GetActorLabel());
	Result->SetStringField(TEXT("actorClass"), PCGVolumeActor->GetClass()->GetName());

	TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
	LocationObj->SetNumberField(TEXT("x"), Location.X);
	LocationObj->SetNumberField(TEXT("y"), Location.Y);
	LocationObj->SetNumberField(TEXT("z"), Location.Z);
	Result->SetObjectField(TEXT("location"), LocationObj);

	TSharedPtr<FJsonObject> ExtentObj = MakeShared<FJsonObject>();
	ExtentObj->SetNumberField(TEXT("x"), Extent.X);
	ExtentObj->SetNumberField(TEXT("y"), Extent.Y);
	ExtentObj->SetNumberField(TEXT("z"), Extent.Z);
	Result->SetObjectField(TEXT("extent"), ExtentObj);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::ReadPCGNodeSettings(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString NodeName;
	if (auto Err = RequireString(Params, TEXT("nodeName"), NodeName)) return Err;

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph)
	{
		return MCPError(FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));
	}

	// Find the node by name
	UPCGNode* FoundNode = nullptr;
	const auto& Nodes = Graph->GetNodes();
	for (UPCGNode* Node : Nodes)
	{
		if (Node && Node->GetName() == NodeName)
		{
			FoundNode = Node;
			break;
		}
	}

	// Also check input/output nodes
	if (!FoundNode && Graph->GetInputNode() && Graph->GetInputNode()->GetName() == NodeName)
	{
		FoundNode = Graph->GetInputNode();
	}
	if (!FoundNode && Graph->GetOutputNode() && Graph->GetOutputNode()->GetName() == NodeName)
	{
		FoundNode = Graph->GetOutputNode();
	}

	if (!FoundNode)
	{
		return MCPError(FString::Printf(TEXT("Node not found: %s"), *NodeName));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("nodeName"), FoundNode->GetName());
	Result->SetStringField(TEXT("nodeTitle"), FoundNode->GetNodeTitle(EPCGNodeTitleType::ListView).ToString());

	// Read the settings object properties
	const UPCGSettings* Settings = FoundNode->GetSettings();
	if (!Settings)
	{
		Result->SetStringField(TEXT("note"), TEXT("Node has no settings object"));
		return MCPResult(Result);
	}

	Result->SetStringField(TEXT("settingsClass"), Settings->GetClass()->GetName());

	// Enumerate all editable properties on the settings
	TSharedPtr<FJsonObject> PropertiesObj = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> PropIt(Settings->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property) continue;

		// Only include properties that are editable and visible
		if (!Property->HasAnyPropertyFlags(CPF_Edit)) continue;

		FString PropertyName = Property->GetName();
		FString PropertyValue;
		const void* PropertyAddr = Property->ContainerPtrToValuePtr<void>(Settings);
		Property->ExportTextItem_Direct(PropertyValue, PropertyAddr, nullptr, nullptr, PPF_None);

		TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
		PropObj->SetStringField(TEXT("value"), PropertyValue);
		PropObj->SetStringField(TEXT("type"), Property->GetCPPType());
		PropertiesObj->SetObjectField(PropertyName, PropObj);
	}

	Result->SetObjectField(TEXT("settings"), PropertiesObj);

	// List input/output pins
	TArray<TSharedPtr<FJsonValue>> InputPinsArray;
	const TArray<TObjectPtr<UPCGPin>>& InPins = FoundNode->GetInputPins();
	for (const TObjectPtr<UPCGPin>& Pin : InPins)
	{
		if (!Pin) continue;
		TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("label"), Pin->Properties.Label.ToString());
		InputPinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
	}
	Result->SetArrayField(TEXT("inputPins"), InputPinsArray);

	TArray<TSharedPtr<FJsonValue>> OutputPinsArray;
	const TArray<TObjectPtr<UPCGPin>>& OutPins = FoundNode->GetOutputPins();
	for (const TObjectPtr<UPCGPin>& Pin : OutPins)
	{
		if (!Pin) continue;
		TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("label"), Pin->Properties.Label.ToString());
		OutputPinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
	}
	Result->SetArrayField(TEXT("outputPins"), OutputPinsArray);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::GetPCGComponentDetails(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	// Find actor by label
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (Actor && Actor->GetActorLabel() == ActorLabel)
		{
			FoundActor = Actor;
			break;
		}
	}

	if (!FoundActor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found with label: %s"), *ActorLabel));
	}

	// Get all PCG components on the actor
	TArray<UPCGComponent*> PCGComps;
	FoundActor->GetComponents<UPCGComponent>(PCGComps);

	if (PCGComps.Num() == 0)
	{
		return MCPError(FString::Printf(TEXT("No PCGComponent found on actor: %s"), *ActorLabel));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorClass"), FoundActor->GetClass()->GetName());

	TArray<TSharedPtr<FJsonValue>> ComponentsArray;
	for (UPCGComponent* PCGComp : PCGComps)
	{
		if (!PCGComp) continue;

		TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
		CompObj->SetStringField(TEXT("componentName"), PCGComp->GetName());
		CompObj->SetNumberField(TEXT("seed"), PCGComp->Seed);
		CompObj->SetBoolField(TEXT("activated"), PCGComp->bActivated);

		// Generation trigger
		FString GenTriggerStr;
		switch (PCGComp->GenerationTrigger)
		{
		case EPCGComponentGenerationTrigger::GenerateOnLoad:
			GenTriggerStr = TEXT("GenerateOnLoad");
			break;
		case EPCGComponentGenerationTrigger::GenerateOnDemand:
			GenTriggerStr = TEXT("GenerateOnDemand");
			break;
		default:
			GenTriggerStr = TEXT("Unknown");
			break;
		}
		CompObj->SetStringField(TEXT("generationTrigger"), GenTriggerStr);

		// Graph details
		UPCGGraph* Graph = const_cast<UPCGGraph*>(PCGComp->GetGraph());
		if (Graph)
		{
			TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
			GraphObj->SetStringField(TEXT("name"), Graph->GetName());
			GraphObj->SetStringField(TEXT("path"), Graph->GetPathName());
			GraphObj->SetNumberField(TEXT("nodeCount"), Graph->GetNodes().Num());
			CompObj->SetObjectField(TEXT("graph"), GraphObj);
		}

		// Location info
		FVector CompLocation = PCGComp->GetOwner() ? PCGComp->GetOwner()->GetActorLocation() : FVector::ZeroVector;
		TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
		LocObj->SetNumberField(TEXT("x"), CompLocation.X);
		LocObj->SetNumberField(TEXT("y"), CompLocation.Y);
		LocObj->SetNumberField(TEXT("z"), CompLocation.Z);
		CompObj->SetObjectField(TEXT("location"), LocObj);

		ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
	}

	Result->SetArrayField(TEXT("components"), ComponentsArray);
	Result->SetNumberField(TEXT("componentCount"), ComponentsArray.Num());
	return MCPResult(Result);
}
