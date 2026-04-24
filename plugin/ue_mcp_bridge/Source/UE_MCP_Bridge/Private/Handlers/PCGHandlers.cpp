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
#include "Elements/PCGStaticMeshSpawner.h"
#include "MeshSelectors/PCGMeshSelectorWeighted.h"
#include "Engine/StaticMesh.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/SoftObjectPtr.h"
#include "ScopedTransaction.h"

namespace
{
	// #149: recursive JSON→FProperty setter used by set_pcg_node_settings.
	// Handles TArray, TSet, nested structs (JSON objects), UObject/Class refs
	// from string paths, and soft references. Falls back to ImportText for scalars.
	static bool SetJsonOnProperty(FProperty* Prop, void* ValueAddr, const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		if (!Prop || !Value.IsValid() || !ValueAddr) { OutError = TEXT("null property/value/addr"); return false; }

		// TArray
		if (FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
		{
			const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
			if (!Value->TryGetArray(Items) || !Items) { OutError = TEXT("expected JSON array"); return false; }
			FScriptArrayHelper H(ArrProp, ValueAddr);
			H.Resize(Items->Num());
			for (int32 i = 0; i < Items->Num(); ++i)
			{
				FString E;
				if (!SetJsonOnProperty(ArrProp->Inner, H.GetRawPtr(i), (*Items)[i], E))
				{
					OutError = FString::Printf(TEXT("[%d]: %s"), i, *E); return false;
				}
			}
			return true;
		}

		// TSet
		if (FSetProperty* SetProp = CastField<FSetProperty>(Prop))
		{
			const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
			if (!Value->TryGetArray(Items) || !Items) { OutError = TEXT("expected JSON array for TSet"); return false; }
			FScriptSetHelper H(SetProp, ValueAddr);
			H.EmptyElements();
			for (const TSharedPtr<FJsonValue>& V : *Items)
			{
				const int32 Idx = H.AddDefaultValue_Invalid_NeedsRehash();
				uint8* ElemAddr = H.GetElementPtr(Idx);
				FString E;
				if (!SetJsonOnProperty(SetProp->ElementProp, ElemAddr, V, E)) { OutError = E; return false; }
			}
			H.Rehash();
			return true;
		}

		// Struct: recurse on JSON object fields; otherwise fall through to ImportText
		if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			const TSharedPtr<FJsonObject>* SubObj = nullptr;
			if (Value->TryGetObject(SubObj) && SubObj && (*SubObj).IsValid())
			{
				for (const auto& Pair : (*SubObj)->Values)
				{
					FProperty* SubProp = StructProp->Struct->FindPropertyByName(FName(*Pair.Key));
					if (!SubProp) { OutError = FString::Printf(TEXT("struct field '%s' not found"), *Pair.Key); return false; }
					void* SubAddr = SubProp->ContainerPtrToValuePtr<void>(ValueAddr);
					FString E;
					if (!SetJsonOnProperty(SubProp, SubAddr, Pair.Value, E))
					{
						OutError = FString::Printf(TEXT("%s.%s: %s"), *StructProp->GetName(), *Pair.Key, *E); return false;
					}
				}
				return true;
			}
		}

		// Hard UObject ref — accept asset path
		if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
		{
			FString Path;
			if (Value->TryGetString(Path) && !Path.IsEmpty())
			{
				UObject* Loaded = StaticLoadObject(ObjProp->PropertyClass, nullptr, *Path);
				if (!Loaded) { OutError = FString::Printf(TEXT("asset not found: %s"), *Path); return false; }
				ObjProp->SetObjectPropertyValue(ValueAddr, Loaded);
				return true;
			}
		}

		// Hard UClass ref — accept class path
		if (FClassProperty* ClassProp = CastField<FClassProperty>(Prop))
		{
			FString Path;
			if (Value->TryGetString(Path) && !Path.IsEmpty())
			{
				UClass* Loaded = LoadClass<UObject>(nullptr, *Path);
				if (!Loaded) { OutError = FString::Printf(TEXT("class not found: %s"), *Path); return false; }
				ClassProp->SetObjectPropertyValue(ValueAddr, Loaded);
				return true;
			}
		}

		// Soft object / soft class — accept path string
		if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Prop))
		{
			FString Path;
			if (Value->TryGetString(Path))
			{
				FSoftObjectPath PathObj(Path);
				FSoftObjectPtr Ptr(PathObj);
				SoftObjProp->SetPropertyValue(ValueAddr, Ptr);
				return true;
			}
		}

		// Fallback: coerce JSON to string, run ImportText_Direct
		FString Str;
		if (Value->TryGetString(Str)) {}
		else if (Value->Type == EJson::Number) Str = FString::SanitizeFloat(Value->AsNumber());
		else if (Value->Type == EJson::Boolean) Str = Value->AsBool() ? TEXT("true") : TEXT("false");
		else Str = Value->AsString();

		const TCHAR* R = Prop->ImportText_Direct(*Str, ValueAddr, nullptr, PPF_None);
		if (R == nullptr) { OutError = FString::Printf(TEXT("ImportText failed for '%s'"), *Str); return false; }
		return true;
	}

	// #149: walk dotted property names into nested structs before assigning.
	// Enables "SplineMeshDescriptor.StaticMesh" style keys.
	static bool SetDottedPropertyFromJson(UObject* Owner, const FString& DottedName, const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		TArray<FString> Parts;
		DottedName.ParseIntoArray(Parts, TEXT("."));
		if (Parts.Num() == 0) { OutError = TEXT("empty property name"); return false; }

		void* Container = Owner;
		UStruct* ContainerStruct = Owner->GetClass();
		FProperty* Prop = nullptr;
		for (int32 i = 0; i < Parts.Num(); ++i)
		{
			Prop = ContainerStruct->FindPropertyByName(FName(*Parts[i]));
			if (!Prop) { OutError = FString::Printf(TEXT("property '%s' not found at '%s'"), *Parts[i], *DottedName); return false; }
			if (i < Parts.Num() - 1)
			{
				FStructProperty* SP = CastField<FStructProperty>(Prop);
				if (!SP) { OutError = FString::Printf(TEXT("'%s' is not a struct — cannot descend"), *Parts[i]); return false; }
				Container = SP->ContainerPtrToValuePtr<void>(Container);
				ContainerStruct = SP->Struct;
			}
		}
		void* ValueAddr = Prop->ContainerPtrToValuePtr<void>(Container);
		return SetJsonOnProperty(Prop, ValueAddr, Value, OutError);
	}
}

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
	Registry.RegisterHandler(TEXT("set_static_mesh_spawner_meshes"), &SetStaticMeshSpawnerMeshes);
	// #146: force_regenerate / cleanup / toggle_graph on PCG components
	Registry.RegisterHandler(TEXT("force_regenerate_pcg"), &ForceRegeneratePCG);
	Registry.RegisterHandler(TEXT("cleanup_pcg"), &CleanupPCG);
	Registry.RegisterHandler(TEXT("toggle_pcg_graph"), &ToggleGraphPCG);
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

	UEditorAssetLibrary::SaveLoadedAsset(NewAsset, /*bOnlyIfIsDirty=*/false);

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

	// #157: wrap mutation in a scoped transaction, outer settings to the graph
	// (so they serialize with the package), and force SaveLoadedAsset on the
	// exact Graph instance. The prior AddNode + SaveAsset(path) combination
	// could silently drop mutations when save-by-path resolved to a different
	// loaded instance than the one we mutated.
	FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "AddPCGNode", "Add PCG Node"));
	Graph->Modify();

	UPCGSettings* DefaultSettings = NewObject<UPCGSettings>(Graph, SettingsClass, NAME_None, RF_Transactional);
	if (!DefaultSettings)
	{
		return MCPError(TEXT("Failed to create PCG settings instance"));
	}

	// AddNodeInstance matches the proven Python path: wraps settings in a
	// UPCGSettingsInstance parented to the new node. AddNode(UPCGSettings*)
	// still persists but diverges from how the PCG editor authors nodes.
	UPCGNode* NewNode = Graph->AddNodeInstance(DefaultSettings);
	if (!NewNode)
	{
		return MCPError(TEXT("Failed to add node to PCG graph"));
	}

	// Keep settings parented to the node so duplicate/rename of the graph
	// carries settings with it. AddNodeInstance already does this for the
	// SettingsInstance wrapper; we also reparent the underlying settings.
	if (DefaultSettings->GetOuter() != NewNode && !DefaultSettings->GetOuter()->IsA<UPackage>())
	{
		DefaultSettings->Rename(nullptr, NewNode, REN_DontCreateRedirectors | REN_DoNotDirty);
	}
	DefaultSettings->PostEditChange();

	double PosX = 0, PosY = 0;
	if (Params->TryGetNumberField(TEXT("posX"), PosX) || Params->TryGetNumberField(TEXT("posY"), PosY))
	{
		NewNode->PositionX = (int32)PosX;
		NewNode->PositionY = (int32)PosY;
	}

	NewNode->PostEditChange();
	Graph->PostEditChange();
	if (UPackage* Pkg = Graph->GetOutermost()) { Pkg->MarkPackageDirty(); }

	UEditorAssetLibrary::SaveLoadedAsset(Graph, /*bOnlyIfIsDirty=*/false);

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

	FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "ConnectPCGNodes", "Connect PCG Nodes"));
	Graph->Modify();

	UPCGNode* ResultNode = Graph->AddEdge(SourceNode, ResolvedSourcePinLabel, TargetNode, ResolvedTargetPinLabel);
	if (!ResultNode)
	{
		return MCPError(TEXT("Failed to connect pins - connection may already exist or be incompatible"));
	}

	Graph->PostEditChange();
	if (UPackage* Pkg = Graph->GetOutermost()) { Pkg->MarkPackageDirty(); }

	UEditorAssetLibrary::SaveLoadedAsset(Graph, /*bOnlyIfIsDirty=*/false);

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

	FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "RemovePCGNode", "Remove PCG Node"));
	Graph->Modify();

	Graph->RemoveNode(FoundNode);

	Graph->PostEditChange();
	if (UPackage* Pkg = Graph->GetOutermost()) { Pkg->MarkPackageDirty(); }

	UEditorAssetLibrary::SaveLoadedAsset(Graph, /*bOnlyIfIsDirty=*/false);

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

	// #149: JSON values drive a recursive property setter that handles
	// TSet/TArray, nested struct objects, UObject/class references by path,
	// and dotted property names (e.g. "SplineMeshDescriptor.StaticMesh").
	TArray<TPair<FString, TSharedPtr<FJsonValue>>> PropertiesToSet;
	if (bUseSettingsObject)
	{
		for (const auto& Pair : (*SettingsObj)->Values)
		{
			PropertiesToSet.Add(TPair<FString, TSharedPtr<FJsonValue>>(Pair.Key, Pair.Value));
		}
	}
	else
	{
		// Wrap the string value in a JsonValueString so we run through the same path.
		PropertiesToSet.Add(TPair<FString, TSharedPtr<FJsonValue>>(PropertyName, MakeShared<FJsonValueString>(PropertyValue)));
	}

	TSharedPtr<FJsonObject> SetResults = MakeShared<FJsonObject>();
	TArray<FString> Errors;

	Settings->Modify();

	for (const auto& Prop : PropertiesToSet)
	{
		FString SubErr;
		if (SetDottedPropertyFromJson(Settings, Prop.Key, Prop.Value, SubErr))
		{
			SetResults->SetField(Prop.Key, Prop.Value);
		}
		else
		{
			Errors.Add(FString::Printf(TEXT("Property '%s': %s"), *Prop.Key, *SubErr));
		}
	}

	Settings->PostEditChange();

	Graph->PostEditChange();
	if (UPackage* Pkg = Graph->GetOutermost()) { Pkg->MarkPackageDirty(); }

	UEditorAssetLibrary::SaveLoadedAsset(Graph, /*bOnlyIfIsDirty=*/false);

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

// ─── #145 set_static_mesh_spawner_meshes ────────────────────────────
// Populates the MeshEntries array on a PCGStaticMeshSpawner node's
// weighted mesh selector. set_pcg_node_settings can't reach into
// instanced subobject properties (MeshSelectorParameters.MeshEntries),
// so this dedicated helper handles the common scatter-graph authoring case.
TSharedPtr<FJsonValue> FPCGHandlers::SetStaticMeshSpawnerMeshes(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString NodeName;
	if (auto Err = RequireString(Params, TEXT("nodeName"), NodeName)) return Err;

	const TArray<TSharedPtr<FJsonValue>>* EntriesArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("entries"), EntriesArr) || !EntriesArr)
	{
		return MCPError(TEXT("Missing 'entries' array — each item should be {mesh: <path>, weight?: <int>}"));
	}

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph) return MCPError(FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));

	UPCGNode* FoundNode = nullptr;
	for (UPCGNode* Node : Graph->GetNodes())
	{
		if (Node && Node->GetName() == NodeName)
		{
			FoundNode = Node;
			break;
		}
	}
	if (!FoundNode) return MCPError(FString::Printf(TEXT("Node not found: %s"), *NodeName));

	UPCGStaticMeshSpawnerSettings* SpawnerSettings = Cast<UPCGStaticMeshSpawnerSettings>(const_cast<UPCGSettings*>(FoundNode->GetSettings()));
	if (!SpawnerSettings)
	{
		return MCPError(FString::Printf(TEXT("Node '%s' is not a PCGStaticMeshSpawner"), *NodeName));
	}

	// Ensure the selector is UPCGMeshSelectorWeighted; instantiate if missing/mismatched.
	UPCGMeshSelectorWeighted* WeightedSelector = Cast<UPCGMeshSelectorWeighted>(SpawnerSettings->MeshSelectorParameters);
	if (!WeightedSelector)
	{
		SpawnerSettings->SetMeshSelectorType(UPCGMeshSelectorWeighted::StaticClass());
		WeightedSelector = Cast<UPCGMeshSelectorWeighted>(SpawnerSettings->MeshSelectorParameters);
	}
	if (!WeightedSelector)
	{
		return MCPError(TEXT("Failed to configure UPCGMeshSelectorWeighted on spawner"));
	}

	const bool bReplace = OptionalBool(Params, TEXT("replace"), true);
	TArray<FPCGMeshSelectorWeightedEntry> Rebuilt;
	if (!bReplace)
	{
		Rebuilt = WeightedSelector->MeshEntries;
	}

	int32 Added = 0;
	for (const TSharedPtr<FJsonValue>& V : *EntriesArr)
	{
		const TSharedPtr<FJsonObject>* EObj = nullptr;
		if (!V.IsValid() || !V->TryGetObject(EObj) || !EObj) continue;
		FString MeshPath;
		if (!(*EObj)->TryGetStringField(TEXT("mesh"), MeshPath) || MeshPath.IsEmpty()) continue;

		int32 Weight = 1;
		double WeightD = 1.0;
		if ((*EObj)->TryGetNumberField(TEXT("weight"), WeightD)) Weight = FMath::Max(0, (int32)WeightD);

		TSoftObjectPtr<UStaticMesh> MeshRef;
		MeshRef = FSoftObjectPath(MeshPath);
		FPCGMeshSelectorWeightedEntry Entry(MeshRef, Weight);
		Rebuilt.Add(MoveTemp(Entry));
		Added++;
	}

	WeightedSelector->Modify();
	WeightedSelector->MeshEntries = MoveTemp(Rebuilt);
#if WITH_EDITOR
	WeightedSelector->RefreshDisplayNames();
#endif

	SpawnerSettings->Modify();

	// Persist the asset
	SaveAssetPackage(Graph);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("nodeName"), NodeName);
	Result->SetNumberField(TEXT("entriesAdded"), Added);
	Result->SetNumberField(TEXT("totalEntries"), WeightedSelector->MeshEntries.Num());
	Result->SetBoolField(TEXT("replaced"), bReplace);
	return MCPResult(Result);
}

namespace
{
	// Locate a UPCGComponent by actor label. Returns error JsonValue on failure.
	TSharedPtr<FJsonValue> FindPCGComponentByLabel(const FString& ActorLabel, UPCGComponent*& OutComp, AActor*& OutActor)
	{
		OutComp = nullptr;
		OutActor = nullptr;

		UWorld* World = nullptr;
		if (GEditor && GEditor->GetEditorWorldContext().World())
		{
			World = GEditor->GetEditorWorldContext().World();
		}
		if (!World) return MCPError(TEXT("Editor world not available"));

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* A = *It;
			if (A && A->GetActorLabel() == ActorLabel)
			{
				OutActor = A;
				OutComp = A->FindComponentByClass<UPCGComponent>();
				break;
			}
		}
		if (!OutActor) return MCPError(FString::Printf(TEXT("Actor not found with label: %s"), *ActorLabel));
		if (!OutComp) return MCPError(FString::Printf(TEXT("No PCGComponent on actor: %s"), *ActorLabel));
		return nullptr;
	}
}

// ─── #146 force_regenerate / cleanup / toggle_graph ──────────────────
// The set-graph-to-null + re-set workaround is the only reliable way to
// unstick a PCG component whose graph state desynced from the executor
// (common after editor restart or graph edits). The feedback log showed
// this pattern worked consistently; raw Generate(true) alone did not.
TSharedPtr<FJsonValue> FPCGHandlers::ForceRegeneratePCG(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	UPCGComponent* PCGComp = nullptr; AActor* Actor = nullptr;
	if (auto Err = FindPCGComponentByLabel(ActorLabel, PCGComp, Actor)) return Err;

	UPCGGraph* OriginalGraph = PCGComp->GetGraph();
	if (!OriginalGraph)
	{
		return MCPError(FString::Printf(TEXT("PCGComponent on '%s' has no graph assigned"), *ActorLabel));
	}

	// Full reset: clear → re-assign → cleanup → generate. UE 5.7's
	// UPCGComponent::Cleanup takes a single bRemoveComponents arg.
	PCGComp->SetGraph(nullptr);
	PCGComp->SetGraph(OriginalGraph);
	PCGComp->Cleanup(/*bRemoveComponents*/ true);
	PCGComp->Generate(/*bForce*/ true);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("componentName"), PCGComp->GetName());
	Result->SetStringField(TEXT("graphName"), OriginalGraph->GetName());
	Result->SetStringField(TEXT("graphPath"), OriginalGraph->GetPathName());
	Result->SetBoolField(TEXT("regenerated"), true);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::CleanupPCG(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	UPCGComponent* PCGComp = nullptr; AActor* Actor = nullptr;
	if (auto Err = FindPCGComponentByLabel(ActorLabel, PCGComp, Actor)) return Err;

	const bool bRemoveComponents = OptionalBool(Params, TEXT("removeComponents"), true);
	PCGComp->Cleanup(bRemoveComponents);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("componentName"), PCGComp->GetName());
	Result->SetBoolField(TEXT("removeComponents"), bRemoveComponents);
	Result->SetBoolField(TEXT("cleaned"), true);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::ToggleGraphPCG(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	UPCGComponent* PCGComp = nullptr; AActor* Actor = nullptr;
	if (auto Err = FindPCGComponentByLabel(ActorLabel, PCGComp, Actor)) return Err;

	// If the caller supplies graphPath, load and use that; otherwise re-apply the current graph.
	FString GraphPath;
	UPCGGraph* TargetGraph = nullptr;
	if (Params->TryGetStringField(TEXT("graphPath"), GraphPath) && !GraphPath.IsEmpty())
	{
		TargetGraph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
		if (!TargetGraph) return MCPError(FString::Printf(TEXT("PCGGraph not found: %s"), *GraphPath));
	}
	else
	{
		TargetGraph = PCGComp->GetGraph();
		if (!TargetGraph) return MCPError(FString::Printf(TEXT("No graph on '%s' and no graphPath provided"), *ActorLabel));
	}

	PCGComp->SetGraph(nullptr);
	PCGComp->SetGraph(TargetGraph);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("componentName"), PCGComp->GetName());
	Result->SetStringField(TEXT("graphName"), TargetGraph->GetName());
	Result->SetStringField(TEXT("graphPath"), TargetGraph->GetPathName());
	Result->SetBoolField(TEXT("toggled"), true);
	return MCPResult(Result);
}
