#include "PCGHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "VolumeHelpers_Internal.h"
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
#include "Nodes/PCGEditorGraphNodeBase.h"
#include "UObject/UObjectIterator.h"
#include "Engine/StaticMesh.h"
#include "Engine/Brush.h"
#include "Components/BrushComponent.h"
#include "Builders/CubeBuilder.h"
#include "BSPOps.h"
#include "Engine/Polys.h"
#include "Model.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/SoftObjectPtr.h"
#include "ScopedTransaction.h"
#include <functional>

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

	// #213: shared class lookup. Mirrors AddPCGNode's tolerant resolver — accepts
	// short name, "/Script/PCG.X" path, or "U"-prefixed short name.
	static UClass* FindPCGSettingsClass(const FString& ClassName)
	{
		if (ClassName.IsEmpty()) return nullptr;
		UClass* Cls = FindObject<UClass>(nullptr, *ClassName);
		if (!Cls) Cls = FindObject<UClass>(nullptr, *(TEXT("/Script/PCG.") + ClassName));
		if (!Cls && !ClassName.StartsWith(TEXT("U")))
		{
			Cls = FindObject<UClass>(nullptr, *(TEXT("/Script/PCG.U") + ClassName));
		}
		if (Cls && !Cls->IsChildOf(UPCGSettings::StaticClass())) return nullptr;
		return Cls;
	}

	// #213: locate a node by name within a graph, including Input/Output specials.
	static UPCGNode* FindNodeByName(UPCGGraph* Graph, const FString& Name)
	{
		if (!Graph || Name.IsEmpty()) return nullptr;
		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (Node && Node->GetName() == Name) return Node;
		}
		if (UPCGNode* In = Graph->GetInputNode(); In && In->GetName() == Name) return In;
		if (UPCGNode* Out = Graph->GetOutputNode(); Out && Out->GetName() == Name) return Out;
		return nullptr;
	}

	// #213: structured property serializer used by export_pcg_graph. Emits every
	// editable property recursively as JSON so the result round-trips through
	// SetDottedPropertyFromJson on import. Mirrors the lambda in
	// ReadPCGNodeSettings (#214/#215) but kept as a standalone function so both
	// callers can share it without rebuilding the closure each call.
	static TSharedPtr<FJsonValue> SerializePropForExport(FProperty* Prop, const void* Addr)
	{
		if (!Prop || !Addr) return MakeShared<FJsonValueNull>();

		if (FStructProperty* SP = CastField<FStructProperty>(Prop))
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			for (TFieldIterator<FProperty> It(SP->Struct); It; ++It)
			{
				FProperty* Inner = *It;
				if (!Inner) continue;
				const void* InnerAddr = Inner->ContainerPtrToValuePtr<void>(Addr);
				Obj->SetField(Inner->GetName(), SerializePropForExport(Inner, InnerAddr));
			}
			return MakeShared<FJsonValueObject>(Obj);
		}
		if (FArrayProperty* AP = CastField<FArrayProperty>(Prop))
		{
			TArray<TSharedPtr<FJsonValue>> Items;
			FScriptArrayHelper H(AP, Addr);
			for (int32 i = 0; i < H.Num(); ++i)
			{
				Items.Add(SerializePropForExport(AP->Inner, H.GetRawPtr(i)));
			}
			return MakeShared<FJsonValueArray>(Items);
		}
		if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
		{
			return MakeShared<FJsonValueBoolean>(BP->GetPropertyValue(Addr));
		}
		if (FNumericProperty* NP = CastField<FNumericProperty>(Prop))
		{
			if (NP->IsFloatingPoint())
			{
				return MakeShared<FJsonValueNumber>(NP->GetFloatingPointPropertyValue(Addr));
			}
			return MakeShared<FJsonValueNumber>((double)NP->GetSignedIntPropertyValue(Addr));
		}
		if (FObjectProperty* OP = CastField<FObjectProperty>(Prop))
		{
			UObject* Ref = OP->GetObjectPropertyValue(Addr);
			return MakeShared<FJsonValueString>(Ref ? Ref->GetPathName() : TEXT(""));
		}
		FString Out;
		Prop->ExportTextItem_Direct(Out, Addr, nullptr, nullptr, PPF_None);
		return MakeShared<FJsonValueString>(Out);
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

	// #213: bulk JSON-driven graph authoring (mirrors material.import_graph).
	Registry.RegisterHandler(TEXT("import_pcg_graph"), &ImportGraph);
	Registry.RegisterHandler(TEXT("export_pcg_graph"), &ExportGraph);
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

	// #239: surface the implicit DefaultInputNode / DefaultOutputNode so
	// callers can wire to them via connect_pcg_nodes. They aren't part of
	// GetNodes() but their names are required to thread the PCG component's
	// bounds into samplers (e.g. SurfaceSampler.BoundingShape <- graph input).
	auto SerializeImplicitNode = [](const UPCGNode* Node) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		if (!Node) return Obj;
		Obj->SetStringField(TEXT("name"), Node->GetName());
		Obj->SetStringField(TEXT("title"), Node->GetNodeTitle(EPCGNodeTitleType::ListView).ToString());
		TArray<TSharedPtr<FJsonValue>> InPins, OutPins;
		for (const TObjectPtr<UPCGPin>& P : Node->GetInputPins())
		{
			if (P) InPins.Add(MakeShared<FJsonValueString>(P->Properties.Label.ToString()));
		}
		for (const TObjectPtr<UPCGPin>& P : Node->GetOutputPins())
		{
			if (P) OutPins.Add(MakeShared<FJsonValueString>(P->Properties.Label.ToString()));
		}
		Obj->SetArrayField(TEXT("inputPins"), InPins);
		Obj->SetArrayField(TEXT("outputPins"), OutPins);
		return Obj;
	};
	if (const UPCGNode* InputNode = Graph->GetInputNode())
	{
		Result->SetObjectField(TEXT("inputNode"), SerializeImplicitNode(InputNode));
	}
	if (const UPCGNode* OutputNode = Graph->GetOutputNode())
	{
		Result->SetObjectField(TEXT("outputNode"), SerializeImplicitNode(OutputNode));
	}

	// #217: include edges so callers can verify wiring without dropping into
	// Python. Walk every node's output pins and serialise each edge as
	// {from, fromPin, to, toPin}. Input/Output graph nodes participate too.
	auto EmitEdgesFromNode = [](const UPCGNode* From, TArray<TSharedPtr<FJsonValue>>& OutEdges)
	{
		if (!From) return;
		for (const TObjectPtr<UPCGPin>& OutPin : From->GetOutputPins())
		{
			if (!OutPin) continue;
			for (const TObjectPtr<UPCGEdge>& Edge : OutPin->Edges)
			{
				if (!Edge) continue;
				const UPCGPin* OtherPin = Edge->InputPin == OutPin ? Edge->OutputPin.Get() : Edge->InputPin.Get();
				const UPCGNode* ToNode = OtherPin ? OtherPin->Node.Get() : nullptr;
				if (!OtherPin || !ToNode) continue;
				TSharedPtr<FJsonObject> EdgeObj = MakeShared<FJsonObject>();
				EdgeObj->SetStringField(TEXT("from"), From->GetName());
				EdgeObj->SetStringField(TEXT("fromPin"), OutPin->Properties.Label.ToString());
				EdgeObj->SetStringField(TEXT("to"), ToNode->GetName());
				EdgeObj->SetStringField(TEXT("toPin"), OtherPin->Properties.Label.ToString());
				OutEdges.Add(MakeShared<FJsonValueObject>(EdgeObj));
			}
		}
	};

	TArray<TSharedPtr<FJsonValue>> EdgeArray;
	if (const UPCGNode* InputNode = Graph->GetInputNode())
	{
		EmitEdgesFromNode(InputNode, EdgeArray);
	}
	for (const UPCGNode* Node : Nodes)
	{
		EmitEdgesFromNode(Node, EdgeArray);
	}
	Result->SetArrayField(TEXT("edges"), EdgeArray);
	Result->SetNumberField(TEXT("edgeCount"), EdgeArray.Num());

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

	// #239: callers commonly try "Input"/"Output" - point them at the actual
	// implicit node names (DefaultInputNode/DefaultOutputNode) which are now
	// surfaced by read_pcg_graph.
	auto ImplicitHint = [&](const FString& Name) -> FString
	{
		if (Name.Equals(TEXT("Input"), ESearchCase::IgnoreCase) ||
			Name.Equals(TEXT("InputNode"), ESearchCase::IgnoreCase) ||
			Name.Equals(TEXT("GraphInput"), ESearchCase::IgnoreCase))
		{
			return Graph->GetInputNode() ? FString::Printf(TEXT(" - did you mean '%s'?"), *Graph->GetInputNode()->GetName()) : FString();
		}
		if (Name.Equals(TEXT("Output"), ESearchCase::IgnoreCase) ||
			Name.Equals(TEXT("OutputNode"), ESearchCase::IgnoreCase) ||
			Name.Equals(TEXT("GraphOutput"), ESearchCase::IgnoreCase))
		{
			return Graph->GetOutputNode() ? FString::Printf(TEXT(" - did you mean '%s'?"), *Graph->GetOutputNode()->GetName()) : FString();
		}
		return FString();
	};
	if (!SourceNode)
	{
		return MCPError(FString::Printf(TEXT("Source node not found: %s%s"), *SourceNodeName, *ImplicitHint(SourceNodeName)));
	}
	if (!TargetNode)
	{
		return MCPError(FString::Printf(TEXT("Target node not found: %s%s"), *TargetNodeName, *ImplicitHint(TargetNodeName)));
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

	// #218: location/extent ship as nested {x,y,z} objects per the TS schema
	// (Vec3). Older calls may pass flat x/y/z and extentX/Y/Z; accept both.
	auto ReadVec3 = [&](const TCHAR* ObjKey, const TCHAR* FlatX, const TCHAR* FlatY, const TCHAR* FlatZ, FVector& Out) -> bool
	{
		bool bAny = false;
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (Params->TryGetObjectField(ObjKey, Obj) && Obj && Obj->IsValid())
		{
			double V = 0;
			if ((*Obj)->TryGetNumberField(TEXT("x"), V)) { Out.X = V; bAny = true; }
			if ((*Obj)->TryGetNumberField(TEXT("y"), V)) { Out.Y = V; bAny = true; }
			if ((*Obj)->TryGetNumberField(TEXT("z"), V)) { Out.Z = V; bAny = true; }
		}
		double V = 0;
		if (Params->TryGetNumberField(FlatX, V)) { Out.X = V; bAny = true; }
		if (Params->TryGetNumberField(FlatY, V)) { Out.Y = V; bAny = true; }
		if (Params->TryGetNumberField(FlatZ, V)) { Out.Z = V; bAny = true; }
		return bAny;
	};

	FVector Location = FVector::ZeroVector;
	ReadVec3(TEXT("location"), TEXT("x"), TEXT("y"), TEXT("z"), Location);

	FVector Extent(500.0, 500.0, 500.0);
	ReadVec3(TEXT("extent"), TEXT("extentX"), TEXT("extentY"), TEXT("extentZ"), Extent);

	// Spawn PCG Volume actor
	FTransform SpawnTransform(FRotator::ZeroRotator, Location);
	APCGVolume* PCGVolumeActor = World->SpawnActor<APCGVolume>(APCGVolume::StaticClass(), SpawnTransform);
	if (!PCGVolumeActor)
	{
		return MCPError(TEXT("Failed to spawn PCGVolume actor"));
	}

	UEMCP::BuildVolumeAsCube(World, PCGVolumeActor, Extent);

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

	// #214 / #215: also emit a fully-keyed structured form alongside the
	// ExportText round-trip. UScriptStruct::ExportText skips fields equal to
	// the struct's CDO, so callers that diffed write/read couldn't see whether
	// a key was actually applied (e.g. ActorSelection=ByTag, bMustOverlapSelf=false).
	// The structured form recurses into struct fields and emits every key.
	std::function<TSharedPtr<FJsonValue>(FProperty*, const void*)> SerializeProp;
	SerializeProp = [&SerializeProp](FProperty* Prop, const void* Addr) -> TSharedPtr<FJsonValue>
	{
		if (!Prop || !Addr) return MakeShared<FJsonValueNull>();

		if (FStructProperty* SP = CastField<FStructProperty>(Prop))
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			for (TFieldIterator<FProperty> It(SP->Struct); It; ++It)
			{
				FProperty* Inner = *It;
				if (!Inner) continue;
				const void* InnerAddr = Inner->ContainerPtrToValuePtr<void>(Addr);
				Obj->SetField(Inner->GetName(), SerializeProp(Inner, InnerAddr));
			}
			return MakeShared<FJsonValueObject>(Obj);
		}
		if (FArrayProperty* AP = CastField<FArrayProperty>(Prop))
		{
			TArray<TSharedPtr<FJsonValue>> Items;
			FScriptArrayHelper H(AP, Addr);
			for (int32 i = 0; i < H.Num(); ++i)
			{
				Items.Add(SerializeProp(AP->Inner, H.GetRawPtr(i)));
			}
			return MakeShared<FJsonValueArray>(Items);
		}
		if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
		{
			return MakeShared<FJsonValueBoolean>(BP->GetPropertyValue(Addr));
		}
		if (FNumericProperty* NP = CastField<FNumericProperty>(Prop))
		{
			if (NP->IsFloatingPoint())
			{
				return MakeShared<FJsonValueNumber>(NP->GetFloatingPointPropertyValue(Addr));
			}
			return MakeShared<FJsonValueNumber>((double)NP->GetSignedIntPropertyValue(Addr));
		}
		if (FObjectProperty* OP = CastField<FObjectProperty>(Prop))
		{
			UObject* Ref = OP->GetObjectPropertyValue(Addr);
			return MakeShared<FJsonValueString>(Ref ? Ref->GetPathName() : TEXT(""));
		}
		// Enums and remaining scalars: ExportTextItem with no Defaults emits the literal.
		FString Out;
		Prop->ExportTextItem_Direct(Out, Addr, nullptr, nullptr, PPF_None);
		return MakeShared<FJsonValueString>(Out);
	};

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
		PropObj->SetField(TEXT("structured"), SerializeProp(Property, PropertyAddr));
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

// ─── #213 import_pcg_graph / export_pcg_graph ────────────────────────
// Bulk JSON-driven graph authoring. Mirrors material.import_graph: one tool
// call replaces N add_node + M connect_nodes + K set_node_settings round-trips.
// Operates directly on the runtime UPCGGraph (no editor-graph required).
TSharedPtr<FJsonValue> FPCGHandlers::ImportGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr)
	{
		return MCPError(TEXT("Missing 'nodes' array"));
	}

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph)
	{
		return MCPError(FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));
	}

	const bool bReplace = OptionalBool(Params, TEXT("replace"), false);

	FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "ImportPCGGraph", "Import PCG Graph"));
	Graph->Modify();

	int32 Removed = 0;
	if (bReplace)
	{
		// Snapshot first; RemoveNode mutates the graph's node list.
		TArray<UPCGNode*> Existing;
		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (Node) Existing.Add(Node);
		}
		for (UPCGNode* Node : Existing)
		{
			Graph->RemoveNode(Node);
			++Removed;
		}
	}

	// User-supplied JSON `name` is a local identifier only; the engine assigns
	// the actual UPCGNode name on creation. Edges reference these local names.
	TMap<FString, UPCGNode*> ByLocalName;
	TArray<TSharedPtr<FJsonValue>> Warnings;
	int32 NodesCreated = 0;
	int32 SettingsApplied = 0;

	for (const TSharedPtr<FJsonValue>& V : *NodesArr)
	{
		const TSharedPtr<FJsonObject>* NodeObj = nullptr;
		if (!V.IsValid() || !V->TryGetObject(NodeObj) || !NodeObj || !(*NodeObj).IsValid()) continue;

		FString LocalName;
		(*NodeObj)->TryGetStringField(TEXT("name"), LocalName);

		FString ClassName;
		if (!(*NodeObj)->TryGetStringField(TEXT("class"), ClassName) || ClassName.IsEmpty())
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("node '%s': missing 'class'"), *LocalName)));
			continue;
		}

		UClass* SettingsClass = FindPCGSettingsClass(ClassName);
		if (!SettingsClass)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("node '%s': class not found or invalid: %s"), *LocalName, *ClassName)));
			continue;
		}

		UPCGSettings* DefaultSettings = NewObject<UPCGSettings>(Graph, SettingsClass, NAME_None, RF_Transactional);
		if (!DefaultSettings)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("node '%s': failed to instantiate settings"), *LocalName)));
			continue;
		}

		UPCGNode* NewNode = Graph->AddNodeInstance(DefaultSettings);
		if (!NewNode)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("node '%s': AddNodeInstance failed"), *LocalName)));
			continue;
		}

		if (DefaultSettings->GetOuter() != NewNode && !DefaultSettings->GetOuter()->IsA<UPackage>())
		{
			DefaultSettings->Rename(nullptr, NewNode, REN_DontCreateRedirectors | REN_DoNotDirty);
		}

		// #236: preserve the user-supplied node name when there's no collision.
		// AddNodeInstance assigns engine-derived names (e.g. SurfaceSampler_0),
		// which makes export -> import not name-stable. Rename the new node
		// to the local id when nothing else in the graph already uses it.
		if (!LocalName.IsEmpty() && NewNode->GetName() != LocalName)
		{
			const bool bClashes = (StaticFindObject(nullptr, NewNode->GetOuter(), *LocalName, EFindObjectFlags::ExactClass) != nullptr);
			if (!bClashes)
			{
				NewNode->Rename(*LocalName, NewNode->GetOuter(), REN_DontCreateRedirectors | REN_DoNotDirty);
			}
			else
			{
				Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("node '%s': name collision, kept engine-assigned '%s'"), *LocalName, *NewNode->GetName())));
			}
		}

		double PosX = 0, PosY = 0;
		if ((*NodeObj)->TryGetNumberField(TEXT("posX"), PosX)) NewNode->PositionX = (int32)PosX;
		if ((*NodeObj)->TryGetNumberField(TEXT("posY"), PosY)) NewNode->PositionY = (int32)PosY;

		// Apply settings via the same dotted-path JSON setter used by
		// set_pcg_node_settings (#149).
		const TSharedPtr<FJsonObject>* SettingsObj = nullptr;
		if ((*NodeObj)->TryGetObjectField(TEXT("settings"), SettingsObj) && SettingsObj && (*SettingsObj).IsValid())
		{
			DefaultSettings->Modify();
			for (const auto& Pair : (*SettingsObj)->Values)
			{
				FString SubErr;
				if (SetDottedPropertyFromJson(DefaultSettings, Pair.Key, Pair.Value, SubErr))
				{
					++SettingsApplied;
				}
				else
				{
					Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("node '%s' setting '%s': %s"), *LocalName, *Pair.Key, *SubErr)));
				}
			}
			DefaultSettings->PostEditChange();
		}

		NewNode->PostEditChange();

		if (!LocalName.IsEmpty())
		{
			ByLocalName.Add(LocalName, NewNode);
		}
		++NodesCreated;
	}

	// Connections. Resolve names via the local-name map first, then fall back
	// to the graph's own nodes (Input/Output/already-existing) by engine name.
	const TArray<TSharedPtr<FJsonValue>>* ConnsArr = nullptr;
	int32 ConnectionsMade = 0;
	if (Params->TryGetArrayField(TEXT("connections"), ConnsArr) && ConnsArr)
	{
		auto Resolve = [&](const FString& Name) -> UPCGNode*
		{
			if (UPCGNode** Found = ByLocalName.Find(Name); Found && *Found) return *Found;
			return FindNodeByName(Graph, Name);
		};

		auto FirstOutputPin = [](UPCGNode* N) -> FName
		{
			if (!N) return NAME_None;
			const auto& Pins = N->GetOutputPins();
			return (Pins.Num() > 0 && Pins[0]) ? Pins[0]->Properties.Label : NAME_None;
		};

		auto FirstInputPin = [](UPCGNode* N) -> FName
		{
			if (!N) return NAME_None;
			const auto& Pins = N->GetInputPins();
			return (Pins.Num() > 0 && Pins[0]) ? Pins[0]->Properties.Label : NAME_None;
		};

		for (const TSharedPtr<FJsonValue>& V : *ConnsArr)
		{
			const TSharedPtr<FJsonObject>* CObj = nullptr;
			if (!V.IsValid() || !V->TryGetObject(CObj) || !CObj || !(*CObj).IsValid()) continue;

			FString From, To, FromPin, ToPin;
			(*CObj)->TryGetStringField(TEXT("from"), From);
			(*CObj)->TryGetStringField(TEXT("to"), To);
			(*CObj)->TryGetStringField(TEXT("fromPin"), FromPin);
			(*CObj)->TryGetStringField(TEXT("toPin"), ToPin);

			UPCGNode* SrcNode = Resolve(From);
			UPCGNode* DstNode = Resolve(To);
			if (!SrcNode)
			{
				Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("connection: source not found: %s"), *From)));
				continue;
			}
			if (!DstNode)
			{
				Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("connection: target not found: %s"), *To)));
				continue;
			}

			const FName SrcPinName = FromPin.IsEmpty() ? FirstOutputPin(SrcNode) : FName(*FromPin);
			const FName DstPinName = ToPin.IsEmpty() ? FirstInputPin(DstNode) : FName(*ToPin);

			if (SrcPinName == NAME_None || DstPinName == NAME_None)
			{
				Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("connection %s.%s -> %s.%s: pin not resolvable"), *From, *FromPin, *To, *ToPin)));
				continue;
			}

			if (Graph->AddEdge(SrcNode, SrcPinName, DstNode, DstPinName))
			{
				++ConnectionsMade;
			}
			else
			{
				Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("connection %s.%s -> %s.%s: AddEdge failed (incompatible or duplicate?)"), *From, *SrcPinName.ToString(), *To, *DstPinName.ToString())));
			}
		}
	}

	Graph->PostEditChange();
	if (UPackage* Pkg = Graph->GetOutermost()) { Pkg->MarkPackageDirty(); }
	UEditorAssetLibrary::SaveLoadedAsset(Graph, /*bOnlyIfIsDirty=*/false);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetBoolField(TEXT("replace"), bReplace);
	Result->SetNumberField(TEXT("nodesRemoved"), Removed);
	Result->SetNumberField(TEXT("nodesCreated"), NodesCreated);
	Result->SetNumberField(TEXT("connectionsMade"), ConnectionsMade);
	Result->SetNumberField(TEXT("settingsApplied"), SettingsApplied);
	if (Warnings.Num() > 0)
	{
		Result->SetArrayField(TEXT("warnings"), Warnings);
	}
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FPCGHandlers::ExportGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph)
	{
		return MCPError(FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));
	}

	const bool bIncludeSettings = OptionalBool(Params, TEXT("includeSettings"), true);

	// #235: editor layout (NodePosX/NodePosY) lives on UPCGEditorGraphNodeBase
	// in the asset's UPCGEditorGraph. The runtime UPCGNode::PositionX/Y is
	// only populated when the node was authored through this bridge - the PCG
	// editor never writes back to it. Build a lookup so editor-authored
	// graphs round-trip their hand-laid-out positions.
	TMap<const UPCGNode*, const UPCGEditorGraphNodeBase*> EditorNodeByPCGNode;
	for (TObjectIterator<UPCGEditorGraphNodeBase> It; It; ++It)
	{
		const UPCGEditorGraphNodeBase* EdNode = *It;
		if (!EdNode || EdNode->IsTemplate()) continue;
		const UPCGNode* PCGN = EdNode->GetPCGNode();
		if (!PCGN) continue;
		if (PCGN->GetTypedOuter<UPCGGraph>() == Graph)
		{
			EditorNodeByPCGNode.Add(PCGN, EdNode);
		}
	}

	TArray<TSharedPtr<FJsonValue>> NodesArr;
	for (const UPCGNode* Node : Graph->GetNodes())
	{
		if (!Node) continue;

		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("name"), Node->GetName());

		double PosX = Node->PositionX;
		double PosY = Node->PositionY;
		if (const UPCGEditorGraphNodeBase* const* EdNodePtr = EditorNodeByPCGNode.Find(Node); EdNodePtr && *EdNodePtr)
		{
			PosX = (*EdNodePtr)->NodePosX;
			PosY = (*EdNodePtr)->NodePosY;
		}
		NodeObj->SetNumberField(TEXT("posX"), PosX);
		NodeObj->SetNumberField(TEXT("posY"), PosY);

		const UPCGSettings* Settings = Node->GetSettings();
		if (Settings)
		{
			NodeObj->SetStringField(TEXT("class"), Settings->GetClass()->GetName());

			if (bIncludeSettings)
			{
				TSharedPtr<FJsonObject> SettingsOut = MakeShared<FJsonObject>();
				for (TFieldIterator<FProperty> It(Settings->GetClass()); It; ++It)
				{
					FProperty* Prop = *It;
					if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit)) continue;
					const void* Addr = Prop->ContainerPtrToValuePtr<void>(Settings);
					SettingsOut->SetField(Prop->GetName(), SerializePropForExport(Prop, Addr));
				}
				NodeObj->SetObjectField(TEXT("settings"), SettingsOut);
			}
		}

		NodesArr.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	// Edges — same shape as read_pcg_graph (#217), reused so import/export speak
	// the same vocabulary. Walk every output pin, including Input/Output specials.
	auto EmitEdgesFromNode = [](const UPCGNode* From, TArray<TSharedPtr<FJsonValue>>& OutEdges)
	{
		if (!From) return;
		for (const TObjectPtr<UPCGPin>& OutPin : From->GetOutputPins())
		{
			if (!OutPin) continue;
			for (const TObjectPtr<UPCGEdge>& Edge : OutPin->Edges)
			{
				if (!Edge) continue;
				const UPCGPin* OtherPin = Edge->InputPin == OutPin ? Edge->OutputPin.Get() : Edge->InputPin.Get();
				const UPCGNode* ToNode = OtherPin ? OtherPin->Node.Get() : nullptr;
				if (!OtherPin || !ToNode) continue;
				TSharedPtr<FJsonObject> EdgeObj = MakeShared<FJsonObject>();
				EdgeObj->SetStringField(TEXT("from"), From->GetName());
				EdgeObj->SetStringField(TEXT("fromPin"), OutPin->Properties.Label.ToString());
				EdgeObj->SetStringField(TEXT("to"), ToNode->GetName());
				EdgeObj->SetStringField(TEXT("toPin"), OtherPin->Properties.Label.ToString());
				OutEdges.Add(MakeShared<FJsonValueObject>(EdgeObj));
			}
		}
	};

	TArray<TSharedPtr<FJsonValue>> ConnsArr;
	if (const UPCGNode* InputNode = Graph->GetInputNode())
	{
		EmitEdgesFromNode(InputNode, ConnsArr);
	}
	for (const UPCGNode* Node : Graph->GetNodes())
	{
		EmitEdgesFromNode(Node, ConnsArr);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), Graph->GetName());
	Result->SetArrayField(TEXT("nodes"), NodesArr);
	Result->SetArrayField(TEXT("connections"), ConnsArr);
	Result->SetNumberField(TEXT("nodeCount"), NodesArr.Num());
	Result->SetNumberField(TEXT("connectionCount"), ConnsArr.Num());
	return MCPResult(Result);
}
