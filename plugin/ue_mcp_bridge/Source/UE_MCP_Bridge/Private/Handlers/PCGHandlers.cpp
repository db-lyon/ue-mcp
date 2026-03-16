#include "PCGHandlers.h"
#include "HandlerRegistry.h"
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
#include "PCGGraphInterface.h"
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
}

TSharedPtr<FJsonValue> FPCGHandlers::ListPCGGraphs(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

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
	Result->SetArrayField(TEXT("graphs"), AssetArray);
	Result->SetNumberField(TEXT("count"), AssetArray.Num());
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::GetPCGComponents(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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

	Result->SetArrayField(TEXT("components"), CompArray);
	Result->SetNumberField(TEXT("count"), CompArray.Num());
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::CreatePCGGraph(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/PCG");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	FString FullAssetPath = PackagePath + TEXT("/") + Name;
	UEditorAssetLibrary::DeleteAsset(FullAssetPath);

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, UPCGGraph::StaticClass(), nullptr);
	if (!NewAsset)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create PCGGraph"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::ReadPCGGraph(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	Result->SetStringField(TEXT("name"), Graph->GetName());
	Result->SetStringField(TEXT("path"), AssetPath);

	const auto& Nodes = Graph->GetNodes();
	TArray<TSharedPtr<FJsonValue>> NodeArray;
	for (const UPCGNode* Node : Nodes)
	{
		if (!Node) continue;
		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("name"), Node->GetName());
		NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle().ToString());
		NodeArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	Result->SetArrayField(TEXT("nodes"), NodeArray);
	Result->SetNumberField(TEXT("nodeCount"), NodeArray.Num());
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::AddPCGNode(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString NodeType;
	if (!Params->TryGetStringField(TEXT("nodeType"), NodeType))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'nodeType' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("PCG settings class not found or invalid: %s"), *NodeType));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Create default settings object
	UPCGSettings* DefaultSettings = NewObject<UPCGSettings>(GetTransientPackage(), SettingsClass);
	if (!DefaultSettings)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create PCG settings instance"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Add node to graph
	UPCGNode* NewNode = Graph->AddNode(DefaultSettings);
	if (!NewNode)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to add node to PCG graph"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Set position if provided
	// UE 5.7: PositionX/PositionY are no longer public; use SetPosition() instead
	double PosX = 0, PosY = 0;
	if (Params->TryGetNumberField(TEXT("posX"), PosX) || Params->TryGetNumberField(TEXT("posY"), PosY))
	{
		NewNode->SetPosition(PosX, PosY);
	}

	// Save the graph asset
	UEditorAssetLibrary::SaveAsset(AssetPath);

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("nodeName"), NewNode->GetName());
	Result->SetStringField(TEXT("nodeType"), NodeType);
	Result->SetStringField(TEXT("nodeTitle"), NewNode->GetNodeTitle().ToString());
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::ConnectPCGNodes(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString SourceNodeName;
	if (!Params->TryGetStringField(TEXT("sourceNodeName"), SourceNodeName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'sourceNodeName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString TargetNodeName;
	if (!Params->TryGetStringField(TEXT("targetNodeName"), TargetNodeName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'targetNodeName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Source node not found: %s"), *SourceNodeName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}
	if (!TargetNode)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Target node not found: %s"), *TargetNodeName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Get pin labels if specified, otherwise use the first available pins
	FString SourcePinLabel;
	Params->TryGetStringField(TEXT("sourcePinLabel"), SourcePinLabel);
	FString TargetPinLabel;
	Params->TryGetStringField(TEXT("targetPinLabel"), TargetPinLabel);

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
		Result->SetStringField(TEXT("error"), TEXT("No suitable output pin found on source node"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}
	if (ResolvedTargetPinLabel == NAME_None)
	{
		Result->SetStringField(TEXT("error"), TEXT("No suitable input pin found on target node"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// UE 5.7: AddEdgeTo() was removed from UPCGPin; use Graph->AddEdge() instead
	bool bConnected = Graph->AddEdge(SourceNode, ResolvedSourcePinLabel, TargetNode, ResolvedTargetPinLabel);
	if (!bConnected)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to connect pins - connection may already exist or be incompatible"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Save the graph asset
	UEditorAssetLibrary::SaveAsset(AssetPath);

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("sourceNodeName"), SourceNodeName);
	Result->SetStringField(TEXT("targetNodeName"), TargetNodeName);
	Result->SetStringField(TEXT("sourcePinLabel"), ResolvedSourcePinLabel.ToString());
	Result->SetStringField(TEXT("targetPinLabel"), ResolvedTargetPinLabel.ToString());
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::RemovePCGNode(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString NodeName;
	if (!Params->TryGetStringField(TEXT("nodeName"), NodeName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'nodeName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Node not found: %s"), *NodeName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Remove the node from the graph
	Graph->RemoveNode(FoundNode);

	// Save the graph asset
	UEditorAssetLibrary::SaveAsset(AssetPath);

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("removedNodeName"), NodeName);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::SetPCGNodeSettings(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString NodeName;
	if (!Params->TryGetStringField(TEXT("nodeName"), NodeName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'nodeName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PropertyName;
	if (!Params->TryGetStringField(TEXT("propertyName"), PropertyName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'propertyName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PropertyValue;
	if (!Params->TryGetStringField(TEXT("propertyValue"), PropertyValue))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'propertyValue' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Node not found: %s"), *NodeName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Get the settings object from the node
	// UE 5.7: GetSettings() returns const; use GetMutableSettings() for write access
	UPCGSettings* Settings = FoundNode->GetMutableSettings();
	if (!Settings)
	{
		Result->SetStringField(TEXT("error"), TEXT("Node has no settings object"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find the property on the settings object
	FProperty* Property = Settings->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Property)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Property '%s' not found on settings"), *PropertyName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Set the property value from string
	void* PropertyAddr = Property->ContainerPtrToValuePtr<void>(Settings);
	bool bSetSuccess = Property->ImportText_Direct(*PropertyValue, PropertyAddr, Settings, PPF_None);
	if (!bSetSuccess)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to set property '%s' to value '%s'"), *PropertyName, *PropertyValue));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Save the graph asset
	UEditorAssetLibrary::SaveAsset(AssetPath);

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("nodeName"), NodeName);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("propertyValue"), PropertyValue);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::ExecutePCGGraph(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString ActorLabel;
	if (!Params->TryGetStringField(TEXT("actorLabel"), ActorLabel))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'actorLabel' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found with label: %s"), *ActorLabel));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Get PCG component from the actor
	UPCGComponent* PCGComp = FoundActor->FindComponentByClass<UPCGComponent>();
	if (!PCGComp)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("No PCGComponent found on actor: %s"), *ActorLabel));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Set seed if provided
	double Seed = 0;
	if (Params->TryGetNumberField(TEXT("seed"), Seed))
	{
		PCGComp->Seed = (int32)Seed;
	}

	// Trigger generation
	PCGComp->Generate();

	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("componentName"), PCGComp->GetName());
	if (PCGComp->GetGraph())
	{
		Result->SetStringField(TEXT("graphName"), PCGComp->GetGraph()->GetName());
	}
	Result->SetNumberField(TEXT("seed"), PCGComp->Seed);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::SpawnPCGVolume(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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
		Result->SetStringField(TEXT("error"), TEXT("Failed to spawn PCGVolume actor"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Set the extent/scale of the volume
	PCGVolumeActor->SetActorScale3D(Extent / 100.0);

	// Set label if provided
	FString Label;
	if (Params->TryGetStringField(TEXT("label"), Label))
	{
		PCGVolumeActor->SetActorLabel(Label);
	}

	// Set graph reference if provided
	FString GraphPath;
	if (Params->TryGetStringField(TEXT("graphPath"), GraphPath))
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

	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}
