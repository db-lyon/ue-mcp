// Split from BlueprintHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FBlueprintHandlers - this file
// is a translation-unit partition, not a new class. The original registers
// these handlers in BlueprintHandlers.cpp::RegisterHandlers.

#include "BlueprintHandlers.h"
#include "BlueprintHandlers_Internal.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "BlueprintEditorLibrary.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphUtilities.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_ConstructObjectFromClass.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "UObject/TopLevelAssetPath.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Containers/Queue.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Kismet libraries used by K2 node construction (AddNode etc.)
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/KismetArrayLibrary.h"

// SCS component access (ResolveComponentTemplate)
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/InheritableComponentHandler.h"


TSharedPtr<FJsonValue> FBlueprintHandlers::AddNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	FString NodeClass;
	if (auto Err = RequireString(Params, TEXT("nodeClass"), NodeClass)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Find the target graph
	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);

	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	// Get optional node params
	const TSharedPtr<FJsonObject>* NodeParams = nullptr;
	Params->TryGetObjectField(TEXT("nodeParams"), NodeParams);

	// Resolve short aliases to full class names
	FString ResolvedClass = NodeClass;
	if (NodeClass == TEXT("CallFunction"))  ResolvedClass = TEXT("K2Node_CallFunction");
	else if (NodeClass == TEXT("Event"))    ResolvedClass = TEXT("K2Node_Event");
	else if (NodeClass == TEXT("GetVar"))   ResolvedClass = TEXT("K2Node_VariableGet");
	else if (NodeClass == TEXT("SetVar"))   ResolvedClass = TEXT("K2Node_VariableSet");
	else if (NodeClass == TEXT("Branch"))   ResolvedClass = TEXT("K2Node_IfThenElse");
	else if (NodeClass == TEXT("CustomEvent")) ResolvedClass = TEXT("K2Node_CustomEvent");

	// Find the UEdGraphNode subclass by name (works for K2, AnimGraph, and any other graph node types)
	UClass* NodeUClass = nullptr;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->GetName() == ResolvedClass && It->IsChildOf(UEdGraphNode::StaticClass()))
		{
			NodeUClass = *It;
			break;
		}
	}

	if (!NodeUClass)
	{
		return MCPError(FString::Printf(TEXT("Node class not found: %s (must be a UEdGraphNode subclass)"), *NodeClass));
	}

	// Create node instance
	UEdGraphNode* NewNode = NewObject<UEdGraphNode>(TargetGraph, NodeUClass);
	if (!NewNode)
	{
		return MCPError(TEXT("Failed to create node"));
	}

	// Special-case initialization for known types (must happen BEFORE AllocateDefaultPins)
	if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(NewNode))
	{
		if (NodeParams)
		{
			FString FunctionName;
			FString TargetClassName;

			// Accept flat params: functionName, targetClass
			if (!(*NodeParams)->TryGetStringField(TEXT("functionName"), FunctionName))
				(*NodeParams)->TryGetStringField(TEXT("memberName"), FunctionName);
			if (!(*NodeParams)->TryGetStringField(TEXT("targetClass"), TargetClassName))
				(*NodeParams)->TryGetStringField(TEXT("memberParent"), TargetClassName);

			// Also accept nested: {"FunctionReference":{"MemberName":"X","MemberParent":"Y"}}
			if (FunctionName.IsEmpty())
			{
				const TSharedPtr<FJsonObject>* FuncRef = nullptr;
				if ((*NodeParams)->TryGetObjectField(TEXT("FunctionReference"), FuncRef))
				{
					(*FuncRef)->TryGetStringField(TEXT("MemberName"), FunctionName);
					if (TargetClassName.IsEmpty())
						(*FuncRef)->TryGetStringField(TEXT("MemberParent"), TargetClassName);
				}
			}

			// Handle full path format: "/Script/Engine.GameplayStatics:GetGameMode"
			if (!FunctionName.IsEmpty() && FunctionName.Contains(TEXT(":")))
			{
				FString ClassPath, FuncPart;
				FunctionName.Split(TEXT(":"), &ClassPath, &FuncPart);
				FunctionName = FuncPart;
				if (TargetClassName.IsEmpty())
				{
					TargetClassName = ClassPath;
				}
			}

			if (!FunctionName.IsEmpty())
			{
				UFunction* FoundFunc = nullptr;

				// 1. Try explicit target class
				if (!TargetClassName.IsEmpty())
				{
					UClass* TargetClass = LoadObject<UClass>(nullptr, *TargetClassName);
					if (!TargetClass)
					{
						TargetClass = FindClassByShortName(TargetClassName);
					}
					if (TargetClass)
					{
						FoundFunc = TargetClass->FindFunctionByName(FName(*FunctionName));
					}
				}

				// 2. Try blueprint parent class
				if (!FoundFunc && Blueprint->ParentClass)
				{
					FoundFunc = Blueprint->ParentClass->FindFunctionByName(FName(*FunctionName));
				}

				// 3. Search common library classes
				if (!FoundFunc)
				{
					static UClass* LibraryClasses[] = {
						UGameplayStatics::StaticClass(),
						UKismetSystemLibrary::StaticClass(),
						UKismetMathLibrary::StaticClass(),
						UKismetStringLibrary::StaticClass(),
						UKismetArrayLibrary::StaticClass(),
					};
					for (UClass* Lib : LibraryClasses)
					{
						FoundFunc = Lib->FindFunctionByName(FName(*FunctionName));
						if (FoundFunc) break;
					}
				}

				if (FoundFunc)
				{
					CallNode->SetFromFunction(FoundFunc);
				}
			}
		}
	}
	else if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(NewNode))
	{
		if (NodeParams)
		{
			FString EventName;
			FString EventClassName;

			if (!(*NodeParams)->TryGetStringField(TEXT("eventName"), EventName))
				(*NodeParams)->TryGetStringField(TEXT("memberName"), EventName);
			if (!(*NodeParams)->TryGetStringField(TEXT("eventClass"), EventClassName))
				(*NodeParams)->TryGetStringField(TEXT("memberParent"), EventClassName);

			// Also accept nested: {"EventReference":{"MemberName":"X","MemberParent":"Y"}}
			if (EventName.IsEmpty())
			{
				const TSharedPtr<FJsonObject>* EvtRef = nullptr;
				if ((*NodeParams)->TryGetObjectField(TEXT("EventReference"), EvtRef))
				{
					(*EvtRef)->TryGetStringField(TEXT("MemberName"), EventName);
					if (EventClassName.IsEmpty())
						(*EvtRef)->TryGetStringField(TEXT("MemberParent"), EventClassName);
				}
			}

			if (!EventName.IsEmpty())
			{

				if (!EventClassName.IsEmpty())
				{
					// Engine event override -- bind via EventReference
					UClass* EventClass = FindClassByShortName(EventClassName);
					if (!EventClass) EventClass = Blueprint->ParentClass;

					if (EventClass)
					{
						UFunction* EventFunc = EventClass->FindFunctionByName(FName(*EventName));
						if (EventFunc)
						{
							bool bIsSelf = Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(EventClass);
							EventNode->EventReference.SetFromField<UFunction>(EventFunc, bIsSelf);
						}
					}
				}
				else
				{
					// Custom event -- just set the name
					EventNode->CustomFunctionName = FName(*EventName);
				}
			}
		}
	}
	else if (UK2Node_VariableGet* GetNode = Cast<UK2Node_VariableGet>(NewNode))
	{
		if (NodeParams)
		{
			FString VarName;
			FString OwnerClass;
			if (!(*NodeParams)->TryGetStringField(TEXT("variableName"), VarName))
			{
				// Also accept {"VariableReference":{"MemberName":"X"}} format
				const TSharedPtr<FJsonObject>* VarRef = nullptr;
				if ((*NodeParams)->TryGetObjectField(TEXT("VariableReference"), VarRef))
					(*VarRef)->TryGetStringField(TEXT("MemberName"), VarName);
			}
			(*NodeParams)->TryGetStringField(TEXT("ownerClass"), OwnerClass);

			if (!VarName.IsEmpty())
			{
				if (!OwnerClass.IsEmpty())
				{
					// #118: external class member get — typed Target input pin
					UClass* Owner = LoadClass<UObject>(nullptr, *OwnerClass);
					if (!Owner) Owner = LoadObject<UClass>(nullptr, *OwnerClass);
					if (!Owner && !OwnerClass.EndsWith(TEXT("_C")))
					{
						Owner = LoadClass<UObject>(nullptr, *(OwnerClass + TEXT("_C")));
					}
					if (!Owner) Owner = FindClassByShortName(OwnerClass);
					if (Owner)
					{
						GetNode->VariableReference.SetExternalMember(FName(*VarName), Owner);
					}
					else
					{
						GetNode->VariableReference.SetSelfMember(FName(*VarName));
					}
				}
				else
				{
					GetNode->VariableReference.SetSelfMember(FName(*VarName));
				}
			}
		}
	}
	else if (UK2Node_VariableSet* SetNode = Cast<UK2Node_VariableSet>(NewNode))
	{
		if (NodeParams)
		{
			FString VarName;
			if (!(*NodeParams)->TryGetStringField(TEXT("variableName"), VarName))
			{
				const TSharedPtr<FJsonObject>* VarRef = nullptr;
				if ((*NodeParams)->TryGetObjectField(TEXT("VariableReference"), VarRef))
					(*VarRef)->TryGetStringField(TEXT("MemberName"), VarName);
			}
			if (!VarName.IsEmpty())
			{
				SetNode->VariableReference.SetSelfMember(FName(*VarName));
			}
		}
	}
	else if (UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(NewNode))
	{
		// #101/#118: resolve BP-generated class paths as well as native classes
		if (NodeParams)
		{
			FString TargetType;
			if (!(*NodeParams)->TryGetStringField(TEXT("targetClass"), TargetType))
				(*NodeParams)->TryGetStringField(TEXT("TargetType"), TargetType);
			if (!TargetType.IsEmpty())
			{
				UClass* CastTargetClass = nullptr;
				// Try LoadClass (handles Blueprint_C paths like /Game/.../BP_Foo.BP_Foo_C)
				CastTargetClass = LoadClass<UObject>(nullptr, *TargetType);
				if (!CastTargetClass)
				{
					// Try LoadObject as UClass
					CastTargetClass = LoadObject<UClass>(nullptr, *TargetType);
				}
				if (!CastTargetClass && !TargetType.EndsWith(TEXT("_C")))
				{
					// Try appending _C for Blueprint generated classes
					FString WithSuffix = TargetType + TEXT("_C");
					CastTargetClass = LoadClass<UObject>(nullptr, *WithSuffix);
					if (!CastTargetClass) CastTargetClass = LoadObject<UClass>(nullptr, *WithSuffix);
				}
				if (!CastTargetClass)
				{
					CastTargetClass = FindClassByShortName(TargetType);
				}
				if (CastTargetClass)
				{
					CastNode->TargetType = CastTargetClass;
				}
			}
		}
	}

	// #189: K2Node_CallDelegate — bind the DelegateReference so the node resolves
	// its signature and generates correct pins for multicast delegate invocation.
	else if (UK2Node_CallDelegate* DelegateNode = Cast<UK2Node_CallDelegate>(NewNode))
	{
		if (NodeParams)
		{
			FString DelegateName;
			FString OwnerClass;

			// Accept flat params: delegateName / functionName, ownerClass / targetClass
			if (!(*NodeParams)->TryGetStringField(TEXT("delegateName"), DelegateName))
			{
				if (!(*NodeParams)->TryGetStringField(TEXT("functionName"), DelegateName))
					(*NodeParams)->TryGetStringField(TEXT("memberName"), DelegateName);
			}
			if (!(*NodeParams)->TryGetStringField(TEXT("ownerClass"), OwnerClass))
			{
				if (!(*NodeParams)->TryGetStringField(TEXT("targetClass"), OwnerClass))
					(*NodeParams)->TryGetStringField(TEXT("memberParent"), OwnerClass);
			}

			// Also accept nested: {"DelegateReference":{"MemberName":"X","MemberParent":"Y"}}
			if (DelegateName.IsEmpty())
			{
				const TSharedPtr<FJsonObject>* DelRef = nullptr;
				if ((*NodeParams)->TryGetObjectField(TEXT("DelegateReference"), DelRef))
				{
					(*DelRef)->TryGetStringField(TEXT("MemberName"), DelegateName);
					if (OwnerClass.IsEmpty())
						(*DelRef)->TryGetStringField(TEXT("MemberParent"), OwnerClass);
				}
			}

			if (!DelegateName.IsEmpty())
			{
				if (!OwnerClass.IsEmpty())
				{
					UClass* Owner = LoadObject<UClass>(nullptr, *OwnerClass);
					if (!Owner) Owner = FindClassByShortName(OwnerClass);
					if (Owner)
					{
						// Check if the property is a multicast delegate on the owner class
						FProperty* Prop = Owner->FindPropertyByName(FName(*DelegateName));
						if (Prop)
						{
							bool bIsSelf = Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(Owner);
							DelegateNode->SetFromProperty(Prop, bIsSelf, Owner);
						}
						else
						{
							// Fall back to setting the member reference directly
							bool bIsSelf = Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(Owner);
							if (bIsSelf)
								DelegateNode->DelegateReference.SetSelfMember(FName(*DelegateName));
							else
								DelegateNode->DelegateReference.SetExternalMember(FName(*DelegateName), Owner);
						}
					}
				}
				else
				{
					// Self member — delegate belongs to the Blueprint's own class
					DelegateNode->DelegateReference.SetSelfMember(FName(*DelegateName));
				}
			}
		}
	}

	// #201/#231: K2Node_ConstructObjectFromClass-derived nodes (SpawnActorFromClass,
	// ConstructObject, AddComponent, etc.) assert in PostPlacedNewNode if the
	// owning graph has not been Modify()'d first - the assert lives in
	// EdGraphNode.h around the schema lookup that PostPlacedNewNode triggers.
	// FEdGraphSchemaAction_K2NewNode::PerformAction does this Modify; mirror it
	// here so any K2 node derived from ConstructObjectFromClass is placeable.
	TargetGraph->Modify();
	TargetGraph->AddNode(NewNode, false, false);
	NewNode->CreateNewGuid();
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();

	// #101/#118: after AllocateDefaultPins, force ReconstructNode so typed output pin
	// ("As ClassName") appears for DynamicCast and typed pins appear for VariableGet.
	// Skip ReconstructNode for ConstructObjectFromClass: at this point the Class
	// pin is unset and ReconstructNode for SpawnActor walks pin defaults that
	// expect a non-null class, asserting before AutowireNewNode would fix it.
	if (UK2Node* K2 = Cast<UK2Node>(NewNode))
	{
		if (!K2->IsA<UK2Node_ConstructObjectFromClass>())
		{
			K2->ReconstructNode();
		}
	}

	// #152: function graphs need the structural-modification signal for the
	// skeleton class to pick up new nodes. MarkBlueprintAsStructurallyModified
	// triggers that plus invalidates cached CDO info; CompileBlueprint alone
	// was leaving nodes in newly-created function graphs in a half-initialized
	// state where pins appeared but the underlying function binding didn't.
	TargetGraph->NotifyGraphChanged();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetStringField(TEXT("nodeClass"), NewNode->GetClass()->GetName());
	const FString NodeIdStr = NewNode->NodeGuid.ToString();
	Result->SetStringField(TEXT("nodeId"), NodeIdStr);
	Result->SetStringField(TEXT("title"), NewNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

	// Return pin info so the caller knows what to connect
	TArray<TSharedPtr<FJsonValue>> PinsArray;
	for (UEdGraphPin* Pin : NewNode->Pins)
	{
		if (!Pin) continue;
		TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
		PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
		PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
		PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
	}
	Result->SetArrayField(TEXT("pins"), PinsArray);

	// Rollback: delete the node we just created by guid.
	// Note: add_node has no natural key, so we cannot short-circuit on replay.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), AssetPath);
	Payload->SetStringField(TEXT("graphName"), GraphName);
	Payload->SetStringField(TEXT("nodeId"), NodeIdStr);
	MCPSetRollback(Result, TEXT("delete_node"), Payload);

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FBlueprintHandlers::ReadBlueprintGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Find the graph in UbergraphPages and FunctionGraphs
	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);

	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	TArray<TSharedPtr<FJsonValue>> Nodes;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (!Node) continue;

		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("id"), Node->NodeGuid.ToString());
		NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		NodeObj->SetNumberField(TEXT("posX"), Node->NodePosX);
		NodeObj->SetNumberField(TEXT("posY"), Node->NodePosY);
		NodeObj->SetStringField(TEXT("comment"), Node->NodeComment);

		// List pins
		TArray<TSharedPtr<FJsonValue>> Pins;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin) continue;
			TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
			PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
			PinObj->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
			PinObj->SetBoolField(TEXT("connected"), Pin->LinkedTo.Num() > 0);
			Pins.Add(MakeShared<FJsonValueObject>(PinObj));
		}
		NodeObj->SetArrayField(TEXT("pins"), Pins);

		Nodes.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetArrayField(TEXT("nodes"), Nodes);
	Result->SetNumberField(TEXT("nodeCount"), Nodes.Num());
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FBlueprintHandlers::ConnectPins(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	FString SourceNodeId;
	if (auto Err = RequireStringAlt(Params, TEXT("sourceNodeId"), TEXT("sourceNode"), SourceNodeId)) return Err;

	FString SourcePinName;
	if (auto Err = RequireStringAlt(Params, TEXT("sourcePinName"), TEXT("sourcePin"), SourcePinName)) return Err;

	FString TargetNodeId;
	if (auto Err = RequireStringAlt(Params, TEXT("targetNodeId"), TEXT("targetNode"), TargetNodeId)) return Err;

	FString TargetPinName;
	if (auto Err = RequireStringAlt(Params, TEXT("targetPinName"), TEXT("targetPin"), TargetPinName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);
	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	// Find source node
	UEdGraphNode* SourceNode = FindNodeByGuidOrName(TargetGraph, SourceNodeId);
	if (!SourceNode)
	{
		return MCPError(FString::Printf(TEXT("Source node not found: %s"), *SourceNodeId));
	}

	// Find target node
	UEdGraphNode* TargetNode = FindNodeByGuidOrName(TargetGraph, TargetNodeId);
	if (!TargetNode)
	{
		return MCPError(FString::Printf(TEXT("Target node not found: %s"), *TargetNodeId));
	}

	// Find source pin
	UEdGraphPin* SourcePin = nullptr;
	for (UEdGraphPin* Pin : SourceNode->Pins)
	{
		if (Pin && Pin->PinName.ToString() == SourcePinName)
		{
			SourcePin = Pin;
			break;
		}
	}
	if (!SourcePin)
	{
		return MCPError(FString::Printf(TEXT("Source pin not found: '%s' on node '%s'"), *SourcePinName, *SourceNodeId));
	}

	// Find target pin
	UEdGraphPin* TargetPin = nullptr;
	for (UEdGraphPin* Pin : TargetNode->Pins)
	{
		if (Pin && Pin->PinName.ToString() == TargetPinName)
		{
			TargetPin = Pin;
			break;
		}
	}
	if (!TargetPin)
	{
		return MCPError(FString::Printf(TEXT("Target pin not found: '%s' on node '%s'"), *TargetPinName, *TargetNodeId));
	}

	// Idempotency: if already linked between these two pins, short-circuit
	if (SourcePin->LinkedTo.Contains(TargetPin))
	{
		auto Existed = MCPSuccess();
		MCPSetExisted(Existed);
		Existed->SetStringField(TEXT("path"), AssetPath);
		Existed->SetStringField(TEXT("graphName"), GraphName);
		Existed->SetStringField(TEXT("sourceNodeId"), SourceNodeId);
		Existed->SetStringField(TEXT("sourcePinName"), SourcePinName);
		Existed->SetStringField(TEXT("targetNodeId"), TargetNodeId);
		Existed->SetStringField(TEXT("targetPinName"), TargetPinName);
		return MCPResult(Existed);
	}

	// Use the graph's own schema (K2 for EventGraphs, AnimationGraph schema for AnimGraphs, etc.)
	const UEdGraphSchema* Schema = TargetGraph->GetSchema();
	if (!Schema)
	{
		return MCPError(TEXT("Graph has no schema"));
	}

	bool bConnected = Schema->TryCreateConnection(SourcePin, TargetPin);

	if (bConnected)
	{
		// Compile and save
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		SaveAssetPackage(Blueprint);

		auto Result = MCPSuccess();
		MCPSetCreated(Result);
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("graphName"), GraphName);
		Result->SetStringField(TEXT("sourceNodeId"), SourceNodeId);
		Result->SetStringField(TEXT("sourcePinName"), SourcePinName);
		Result->SetStringField(TEXT("targetNodeId"), TargetNodeId);
		Result->SetStringField(TEXT("targetPinName"), TargetPinName);
		// No rollback: no paired disconnect_pins handler.
		return MCPResult(Result);
	}
	else
	{
		// Get more info about why it failed
		FString ErrorMsg = TEXT("TryCreateConnection failed. Pins may be incompatible.");
		FPinConnectionResponse Response = Schema->CanCreateConnection(SourcePin, TargetPin);
		if (!Response.Message.IsEmpty())
		{
			ErrorMsg = FString::Printf(TEXT("Connection failed: %s"), *Response.Message.ToString());
		}
		return MCPError(ErrorMsg);
	}
}


TSharedPtr<FJsonValue> FBlueprintHandlers::DeleteNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	FString NodeId;
	if (auto Err = RequireStringAlt(Params, TEXT("nodeId"), TEXT("nodeName"), NodeId)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);
	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	UEdGraphNode* NodeToDelete = FindNodeByGuidOrName(TargetGraph, NodeId);
	if (!NodeToDelete)
	{
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("graphName"), GraphName);
		Noop->SetStringField(TEXT("nodeId"), NodeId);
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Noop);
	}

	NodeToDelete->BreakAllNodeLinks();
	TargetGraph->RemoveNode(NodeToDelete);

	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetStringField(TEXT("nodeId"), NodeId);
	Result->SetBoolField(TEXT("deleted"), true);
	// Not reversible by default.
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FBlueprintHandlers::SetNodeProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	FString NodeId;
	if (auto Err = RequireStringAlt(Params, TEXT("nodeId"), TEXT("nodeName"), NodeId)) return Err;

	FString PinName;
	if (auto Err = RequireStringAlt(Params, TEXT("pinName"), TEXT("propertyName"), PinName)) return Err;

	FString DefaultValue;
	if (auto Err = RequireStringAlt(Params, TEXT("defaultValue"), TEXT("value"), DefaultValue)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);
	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	// Find the node
	UEdGraphNode* TargetNode = FindNodeByGuidOrName(TargetGraph, NodeId);
	if (!TargetNode)
	{
		return MCPError(FString::Printf(TEXT("Node not found: %s"), *NodeId));
	}

	// First try to find a pin with this name
	UEdGraphPin* TargetPin = nullptr;
	for (UEdGraphPin* Pin : TargetNode->Pins)
	{
		if (Pin && Pin->PinName.ToString() == PinName)
		{
			TargetPin = Pin;
			break;
		}
	}

	bool bSetViaPin = false;
	bool bSetViaProperty = false;
	FString PrevValue;
	bool bHasPrevValue = false;

	if (TargetPin)
	{
		// Capture previous pin default for rollback
		PrevValue = TargetPin->DefaultValue;
		bHasPrevValue = true;
		// No-op short-circuit: pin default already matches
		if (PrevValue == DefaultValue)
		{
			auto Noop = MCPSuccess();
			MCPSetExisted(Noop);
			Noop->SetStringField(TEXT("path"), AssetPath);
			Noop->SetStringField(TEXT("graphName"), GraphName);
			Noop->SetStringField(TEXT("nodeId"), NodeId);
			Noop->SetStringField(TEXT("propertyName"), PinName);
			Noop->SetStringField(TEXT("value"), DefaultValue);
			return MCPResult(Noop);
		}
		// Set pin default value using the graph's own schema
		const UEdGraphSchema* Schema = TargetGraph->GetSchema();
		if (Schema)
		{
			Schema->TrySetDefaultValue(*TargetPin, DefaultValue);
			TargetNode->PinDefaultValueChanged(TargetPin);
			bSetViaPin = true;
		}
	}

	if (!bSetViaPin)
	{
		// No pin found -- try setting as a node property via reflection.
		// Supports dotted paths like "Node.IKBone.BoneName" for AnimGraph inner structs.
		TArray<FString> PathParts;
		PinName.ParseIntoArray(PathParts, TEXT("."));

		UStruct* CurrentStruct = TargetNode->GetClass();
		void* CurrentContainer = TargetNode;
		FProperty* FinalProp = nullptr;

		for (int32 i = 0; i < PathParts.Num(); i++)
		{
			FProperty* Prop = CurrentStruct->FindPropertyByName(FName(*PathParts[i]));
			if (!Prop) break;

			if (i < PathParts.Num() - 1)
			{
				// Intermediate path segment -- drill into struct
				FStructProperty* StructProp = CastField<FStructProperty>(Prop);
				if (!StructProp) break;
				CurrentContainer = StructProp->ContainerPtrToValuePtr<void>(CurrentContainer);
				CurrentStruct = StructProp->Struct;
			}
			else
			{
				FinalProp = Prop;
			}
		}

		if (FinalProp)
		{
			void* ValuePtr = FinalProp->ContainerPtrToValuePtr<void>(CurrentContainer);
			// ImportText_Direct returns nullptr on parse failure. Previously we
			// ignored that - a malformed DefaultValue silently corrupted the
			// node's default. Now we surface the failure as an error so callers
			// see the bad input immediately rather than at compile time.
			const TCHAR* Parsed = FinalProp->ImportText_Direct(*DefaultValue, ValuePtr, nullptr, PPF_None);
			if (Parsed == nullptr)
			{
				return MCPError(FString::Printf(
					TEXT("DefaultValue '%s' is not valid for property '%s' (type %s). Use UE's text format (e.g. `(X=1,Y=2,Z=3)` for FVector)."),
					*DefaultValue, *FinalProp->GetName(), *FinalProp->GetCPPType()));
			}
			TargetNode->PostEditChange();
			bSetViaProperty = true;
		}
	}

	if (!bSetViaPin && !bSetViaProperty)
	{
		// Neither pin nor property found -- build a helpful error
		TArray<FString> PinNames;
		for (UEdGraphPin* Pin : TargetNode->Pins)
		{
			if (Pin) PinNames.Add(Pin->PinName.ToString());
		}

		TArray<FString> PropNames;
		for (TFieldIterator<FProperty> It(TargetNode->GetClass()); It; ++It)
		{
			PropNames.Add(It->GetName());
		}

		return MCPError(FString::Printf(
			TEXT("'%s' not found as pin or property. Pins: [%s]. Properties: [%s]"),
			*PinName, *FString::Join(PinNames, TEXT(", ")), *FString::Join(PropNames, TEXT(", "))));
	}

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetStringField(TEXT("nodeId"), NodeId);
	Result->SetStringField(TEXT("propertyName"), PinName);
	Result->SetStringField(TEXT("value"), DefaultValue);
	Result->SetStringField(TEXT("setVia"), bSetViaPin ? TEXT("pin") : TEXT("property"));

	// Rollback: self-inverse with previous pin default value (pin path only; property path has no reliable previous capture)
	if (bSetViaPin && bHasPrevValue)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("path"), AssetPath);
		Payload->SetStringField(TEXT("graphName"), GraphName);
		Payload->SetStringField(TEXT("nodeId"), NodeId);
		Payload->SetStringField(TEXT("pinName"), PinName);
		Payload->SetStringField(TEXT("defaultValue"), PrevValue);
		MCPSetRollback(Result, TEXT("set_node_property"), Payload);
	}
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// Resolve a component template on a Blueprint by name.
//
// Child Blueprints do NOT own inherited components in their own SCS — the
// component templates live on the parent Blueprint's SCS. Writing through
// the parent's template corrupts the parent for every descendant. For
// inherited components we must route writes through the child's
// UInheritableComponentHandler, which stores per-child override templates
// (equivalent of SubobjectDataBlueprintFunctionLibrary.get_object_for_blueprint
// in Python, as opposed to the read-only shared get_object).
//
// bForWrite=true: always returns a write-safe template. For inherited
//   components this means the child's ICH override (creating one if none
//   exists yet). Never returns the shared parent template for a write.
//
// bForWrite=false: returns the child's ICH override if one exists
//   (so reads reflect what writes would mutate), otherwise falls back to
//   the parent template (which holds the effective default).
//
// OutAvailable is populated with candidate component names (own SCS +
// inherited + CDO) for error messages when the lookup fails.
UActorComponent* ResolveComponentTemplate(
	UBlueprint* Blueprint,
	const FString& ComponentName,
	bool bForWrite,
	bool& bOutIsInherited,
	TArray<FString>& OutAvailable)
{
	bOutIsInherited = false;
	OutAvailable.Reset();
	if (!Blueprint) return nullptr;

	// 1) Own SCS — child's own components, write directly.
	if (USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			if (!Node || !Node->ComponentTemplate) continue;
			OutAvailable.AddUnique(Node->GetVariableName().ToString());
			if (Node->GetVariableName().ToString() == ComponentName ||
				Node->ComponentTemplate->GetName() == ComponentName)
			{
				return Node->ComponentTemplate;
			}
		}
	}

	// 2) Walk parent BP chain for inherited SCS components.
	USCS_Node* InheritedNode = nullptr;
	UClass* ParentClass = Blueprint->ParentClass;
	while (ParentClass && !InheritedNode)
	{
		if (UBlueprint* ParentBP = Cast<UBlueprint>(ParentClass->ClassGeneratedBy))
		{
			if (USimpleConstructionScript* ParentSCS = ParentBP->SimpleConstructionScript)
			{
				for (USCS_Node* Node : ParentSCS->GetAllNodes())
				{
					if (!Node || !Node->ComponentTemplate) continue;
					OutAvailable.AddUnique(Node->GetVariableName().ToString());
					if (Node->GetVariableName().ToString() == ComponentName ||
						Node->ComponentTemplate->GetName() == ComponentName)
					{
						InheritedNode = Node;
						break;
					}
				}
			}
			ParentClass = ParentBP->ParentClass;
		}
		else
		{
			break;
		}
	}

	if (InheritedNode)
	{
		bOutIsInherited = true;
		FComponentKey Key(InheritedNode);

		// Look up any existing override on this specific child BP.
		UInheritableComponentHandler* ICH =
			Blueprint->GetInheritableComponentHandler(/*bCreateIfNecessary=*/bForWrite);
		UActorComponent* Override = ICH ? ICH->GetOverridenComponentTemplate(Key) : nullptr;

		if (bForWrite)
		{
			// Must never write through the shared parent template — that
			// would mutate the parent and every other descendant.
			if (!Override && ICH)
			{
				Override = ICH->CreateOverridenComponentTemplate(Key);
			}
			return Override; // null only if ICH creation failed
		}
		else
		{
			return Override ? Override : ToRawPtr(InheritedNode->ComponentTemplate);
		}
	}

	// 3) CDO fallback — catches native C++ components and anything the
	// SCS walk missed. The CDO component pointer is the right write target
	// for default-value overrides on inherited native components: mutating
	// it lands on the Blueprint's GeneratedClass CDO, which is what spawns
	// new instances at runtime. Names match by component instance name,
	// instance prefix, or FObjectProperty variable name (#211: declarations
	// like UPROPERTY() UCharacterMovementComponent* CharacterMovement; expose
	// the property name "CharacterMovement", which is what callers reach
	// for, not the construct-time instance name "CharMoveComp").
	if (Blueprint->GeneratedClass)
	{
		if (AActor* ActorCDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject(false)))
		{
			TInlineComponentArray<UActorComponent*> Components;
			ActorCDO->GetComponents(Components);
			for (UActorComponent* C : Components)
			{
				if (!C) continue;
				OutAvailable.AddUnique(C->GetName());
				if (C->GetName() == ComponentName ||
					C->GetName().StartsWith(ComponentName + TEXT("_")) ||
					C->GetFName().ToString() == ComponentName)
				{
					return C;
				}
			}
			// FObjectProperty lookup: walk class properties for an
			// FObjectProperty named ComponentName whose value points at a
			// component on the CDO.
			if (FObjectProperty* OP = CastField<FObjectProperty>(ActorCDO->GetClass()->FindPropertyByName(FName(*ComponentName))))
			{
				if (OP->PropertyClass && OP->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
				{
					if (UObject* Obj = OP->GetObjectPropertyValue_InContainer(ActorCDO))
					{
						if (UActorComponent* AC = Cast<UActorComponent>(Obj))
						{
							OutAvailable.AddUnique(ComponentName);
							return AC;
						}
					}
				}
			}
		}
	}

	return nullptr;
}

// set_component_property -- Set a property on an SCS component template
// Params: assetPath, componentName, propertyName, value
// ---------------------------------------------------------------------------


// ─── #102 read_node_property ────────────────────────────────────────
TSharedPtr<FJsonValue> FBlueprintHandlers::ReadNodeProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));
	FString NodeId;
	if (auto Err = RequireStringAlt(Params, TEXT("nodeId"), TEXT("nodeName"), NodeId)) return Err;
	FString PinOrProp;
	if (auto Err = RequireStringAlt(Params, TEXT("propertyName"), TEXT("pinName"), PinOrProp)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(TEXT("Blueprint not found"));
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph) return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	UEdGraphNode* Node = FindNodeByGuidOrName(Graph, NodeId);
	if (!Node) return MCPError(FString::Printf(TEXT("Node not found: %s"), *NodeId));

	auto Result = MCPSuccess();
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->PinName.ToString() == PinOrProp)
		{
			Result->SetStringField(TEXT("pinName"), PinOrProp);
			Result->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
			if (Pin->DefaultObject)
			{
				Result->SetStringField(TEXT("defaultObject"), Pin->DefaultObject->GetPathName());
			}
			return MCPResult(Result);
		}
	}

	TArray<FString> Parts; PinOrProp.ParseIntoArray(Parts, TEXT("."));
	UStruct* Cur = Node->GetClass();
	void* Container = Node;
	FProperty* Final = nullptr;
	for (int32 i = 0; i < Parts.Num(); ++i)
	{
		FProperty* P = Cur->FindPropertyByName(FName(*Parts[i]));
		if (!P) return MCPError(FString::Printf(TEXT("Property '%s' not found"), *Parts[i]));
		if (i < Parts.Num() - 1)
		{
			FStructProperty* SP = CastField<FStructProperty>(P);
			if (!SP) return MCPError(FString::Printf(TEXT("Not a struct: %s"), *Parts[i]));
			Container = SP->ContainerPtrToValuePtr<void>(Container);
			Cur = SP->Struct;
		}
		else Final = P;
	}
	if (!Final) return MCPError(TEXT("Property path unresolved"));

	FString ValStr;
	const void* ValPtr = Final->ContainerPtrToValuePtr<void>(Container);
	Final->ExportText_Direct(ValStr, ValPtr, ValPtr, Node, PPF_None);
	Result->SetStringField(TEXT("propertyName"), PinOrProp);
	Result->SetStringField(TEXT("type"), Final->GetCPPType());
	Result->SetStringField(TEXT("value"), ValStr);
	return MCPResult(Result);
}

// ─── #115 reparent_component ────────────────────────────────────────


// ---------------------------------------------------------------------------
// v0.7.17 issue #130: bulk graph node import via T3D copy/paste.
// Mirrors the editor's Ctrl+C / Ctrl+V flow (FBlueprintEditor::CopySelectedNodes
// / PasteNodesHere) so callers can author whole subgraphs offline and import
// them atomically rather than building one node at a time.
// ---------------------------------------------------------------------------

TSharedPtr<FJsonValue> FBlueprintHandlers::ExportNodesT3D(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);
	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	// Collect nodes to export. If nodeIds is omitted/empty, export all nodes
	// in the graph (whole-subgraph round-trip).
	TArray<UEdGraphNode*> SelectedNodes;
	const TArray<TSharedPtr<FJsonValue>>* IdsArrayPtr = nullptr;
	if (Params.IsValid() && Params->TryGetArrayField(TEXT("nodeIds"), IdsArrayPtr) && IdsArrayPtr && IdsArrayPtr->Num() > 0)
	{
		for (const TSharedPtr<FJsonValue>& Val : *IdsArrayPtr)
		{
			if (!Val.IsValid()) continue;
			const FString NodeId = Val->AsString();
			if (NodeId.IsEmpty()) continue;
			UEdGraphNode* Node = FindNodeByGuidOrName(TargetGraph, NodeId);
			if (!Node)
			{
				return MCPError(FString::Printf(TEXT("Node not found: %s"), *NodeId));
			}
			SelectedNodes.AddUnique(Node);
		}
	}
	else
	{
		for (UEdGraphNode* Node : TargetGraph->Nodes)
		{
			if (Node) SelectedNodes.Add(Node);
		}
	}

	if (SelectedNodes.Num() == 0)
	{
		return MCPError(TEXT("No nodes to export"));
	}

	// FEdGraphUtilities::ExportNodesToText only writes nodes that are flagged
	// CanDuplicateNode == true; mirrors how the editor's Copy filters root
	// entry/return nodes. Pre-filter so the count we report matches reality.
	TSet<UObject*> NodeSet;
	int32 SkippedCount = 0;
	for (UEdGraphNode* Node : SelectedNodes)
	{
		if (Node && Node->CanDuplicateNode())
		{
			Node->PrepareForCopying();
			NodeSet.Add(Node);
		}
		else
		{
			++SkippedCount;
		}
	}

	if (NodeSet.Num() == 0)
	{
		return MCPError(TEXT("No nodes are duplicatable (entry/return nodes cannot be exported)"));
	}

	FString ExportedText;
	FEdGraphUtilities::ExportNodesToText(NodeSet, ExportedText);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetStringField(TEXT("t3d"), ExportedText);
	Result->SetNumberField(TEXT("count"), NodeSet.Num());
	Result->SetNumberField(TEXT("skipped"), SkippedCount);
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FBlueprintHandlers::ImportNodesT3D(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	FString T3D;
	if (auto Err = RequireStringAlt(Params, TEXT("t3d"), TEXT("text"), T3D)) return Err;
	if (T3D.IsEmpty())
	{
		return MCPError(TEXT("t3d text is empty"));
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);
	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	if (!FEdGraphUtilities::CanImportNodesFromText(TargetGraph, T3D))
	{
		return MCPError(TEXT("T3D text is not importable into this graph (schema mismatch or malformed)"));
	}

	TSet<UEdGraphNode*> PastedNodes;
	FEdGraphUtilities::ImportNodesFromText(TargetGraph, T3D, /*out*/ PastedNodes);

	if (PastedNodes.Num() == 0)
	{
		return MCPError(TEXT("ImportNodesFromText produced no nodes"));
	}

	// Re-center pasted nodes around an explicit (posX, posY) anchor when given,
	// otherwise keep their exported positions. Mirrors PasteNodesHere.
	const bool bRecenter = Params.IsValid() && Params->HasField(TEXT("posX")) && Params->HasField(TEXT("posY"));
	double AnchorX = 0.0, AnchorY = 0.0;
	if (bRecenter)
	{
		Params->TryGetNumberField(TEXT("posX"), AnchorX);
		Params->TryGetNumberField(TEXT("posY"), AnchorY);

		double AvgX = 0.0, AvgY = 0.0;
		for (UEdGraphNode* Node : PastedNodes)
		{
			AvgX += Node->NodePosX;
			AvgY += Node->NodePosY;
		}
		AvgX /= PastedNodes.Num();
		AvgY /= PastedNodes.Num();

		for (UEdGraphNode* Node : PastedNodes)
		{
			Node->NodePosX = (Node->NodePosX - AvgX) + AnchorX;
			Node->NodePosY = (Node->NodePosY - AvgY) + AnchorY;
		}
	}

	// Fresh GUIDs so the pasted nodes don't collide with the originals when
	// pasting back into the same graph (e.g. round-trip duplicate).
	TArray<TSharedPtr<FJsonValue>> NodeIds;
	for (UEdGraphNode* Node : PastedNodes)
	{
		Node->CreateNewGuid();
		Node->PostPasteNode();
		if (UK2Node* K2 = Cast<UK2Node>(Node))
		{
			K2->ReconstructNode();
		}
		NodeIds.Add(MakeShared<FJsonValueString>(Node->NodeGuid.ToString()));
	}

	TargetGraph->NotifyGraphChanged();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetArrayField(TEXT("nodeIds"), NodeIds);
	Result->SetNumberField(TEXT("count"), NodeIds.Num());
	// No rollback: delete_node only deletes one node at a time and the bulk
	// import has no natural key. Caller must clean up by node id if needed.
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// v1.0.0-rc.15 — agent-friendly BP authoring (#284 #285 #267 #277)
// ---------------------------------------------------------------------------

// #284 — compile_blueprints: batch compile + save with per-path status.
TSharedPtr<FJsonValue> FBlueprintHandlers::CompileBlueprints(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* PathsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("assetPaths"), PathsArray) || !PathsArray)
	{
		return MCPError(TEXT("Missing 'assetPaths' (array of blueprint asset paths)"));
	}

	bool bSave = OptionalBool(Params, TEXT("save"), true);

	TArray<TSharedPtr<FJsonValue>> Results;
	int32 Compiled = 0, Failed = 0, NotFound = 0;
	for (const TSharedPtr<FJsonValue>& Entry : *PathsArray)
	{
		FString AssetPath = Entry->AsString();
		TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("path"), AssetPath);

		UBlueprint* Blueprint = LoadBlueprint(AssetPath);
		if (!Blueprint)
		{
			Item->SetStringField(TEXT("status"), TEXT("not_found"));
			NotFound++;
			Results.Add(MakeShared<FJsonValueObject>(Item));
			continue;
		}

		FCompilerResultsLog CompileLog;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &CompileLog);

		if (CompileLog.NumErrors > 0)
		{
			Item->SetStringField(TEXT("status"), TEXT("failed"));
			Item->SetNumberField(TEXT("errors"), CompileLog.NumErrors);
			Item->SetNumberField(TEXT("warnings"), CompileLog.NumWarnings);
			Failed++;
		}
		else
		{
			if (bSave) SaveAssetPackage(Blueprint);
			Item->SetStringField(TEXT("status"), TEXT("compiled"));
			Item->SetNumberField(TEXT("warnings"), CompileLog.NumWarnings);
			Compiled++;
		}
		Results.Add(MakeShared<FJsonValueObject>(Item));
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("total"), PathsArray->Num());
	Result->SetNumberField(TEXT("compiled"), Compiled);
	Result->SetNumberField(TEXT("failed"), Failed);
	Result->SetNumberField(TEXT("notFound"), NotFound);
	Result->SetArrayField(TEXT("results"), Results);
	return MCPResult(Result);
}

// #285 — cleanup_graph: remove orphan nodes (no pins, missing class, blank
// title). Iterates one graph if graphName given, else every graph on the BP.
TSharedPtr<FJsonValue> FBlueprintHandlers::CleanupGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

	const FString GraphName = OptionalString(Params, TEXT("graphName"));

	TArray<UEdGraph*> Graphs;
	if (!GraphName.IsEmpty())
	{
		if (UEdGraph* G = FindGraph(Blueprint, GraphName)) Graphs.Add(G);
		else return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}
	else
	{
		Blueprint->GetAllGraphs(Graphs);
	}

	int32 Removed = 0;
	TArray<TSharedPtr<FJsonValue>> RemovedIds;
	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph) continue;
		// Iterate a copy because RemoveNode mutates Graph->Nodes.
		TArray<UEdGraphNode*> Snapshot = Graph->Nodes;
		for (UEdGraphNode* Node : Snapshot)
		{
			if (!Node) continue;
			bool bOrphan = false;
			// Missing class (often the symptom of a node that lost its parent UClass)
			if (!Node->GetClass()) bOrphan = true;
			// Empty title + no pins is the canonical "corrupted node" signature.
			else if (Node->Pins.Num() == 0 && Node->GetNodeTitle(ENodeTitleType::ListView).IsEmpty()) bOrphan = true;
			// Function-call nodes whose target UFunction has been deleted.
			else if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
			{
				if (!CallNode->GetTargetFunction()) bOrphan = true;
			}

			if (bOrphan)
			{
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("graph"), Graph->GetName());
				Entry->SetStringField(TEXT("nodeGuid"), Node->NodeGuid.ToString());
				Entry->SetStringField(TEXT("class"), Node->GetClass() ? Node->GetClass()->GetName() : TEXT("<null>"));
				RemovedIds.Add(MakeShared<FJsonValueObject>(Entry));
				Graph->RemoveNode(Node);
				Removed++;
			}
		}
	}

	if (Removed > 0)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		SaveAssetPackage(Blueprint);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetNumberField(TEXT("removed"), Removed);
	Result->SetArrayField(TEXT("removedNodes"), RemovedIds);
	return MCPResult(Result);
}

// #267 — connect_pins_batch: apply many wirings in one call, single compile.
TSharedPtr<FJsonValue> FBlueprintHandlers::ConnectPinsBatch(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	const FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("connections"), ConnectionsArray) || !ConnectionsArray)
	{
		return MCPError(TEXT("Missing 'connections' array — each entry: {sourceNode, sourcePin, targetNode, targetPin}"));
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);
	if (!TargetGraph) return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	const UEdGraphSchema* Schema = TargetGraph->GetSchema();
	if (!Schema) return MCPError(TEXT("Graph has no schema"));

	auto FindPin = [](UEdGraphNode* N, const FString& Name) -> UEdGraphPin*
	{
		for (UEdGraphPin* P : N->Pins)
		{
			if (P && P->PinName.ToString() == Name) return P;
		}
		return nullptr;
	};

	int32 Connected = 0, Existed = 0, Failed = 0;
	TArray<TSharedPtr<FJsonValue>> Detail;
	for (const TSharedPtr<FJsonValue>& Entry : *ConnectionsArray)
	{
		const TSharedPtr<FJsonObject> Obj = Entry->AsObject();
		if (!Obj.IsValid()) { Failed++; continue; }

		FString SrcId, SrcPin, TgtId, TgtPin;
		Obj->TryGetStringField(TEXT("sourceNode"), SrcId);
		Obj->TryGetStringField(TEXT("sourcePin"), SrcPin);
		Obj->TryGetStringField(TEXT("targetNode"), TgtId);
		Obj->TryGetStringField(TEXT("targetPin"), TgtPin);

		TSharedPtr<FJsonObject> EntryResult = MakeShared<FJsonObject>();
		EntryResult->SetStringField(TEXT("sourceNode"), SrcId);
		EntryResult->SetStringField(TEXT("sourcePin"), SrcPin);
		EntryResult->SetStringField(TEXT("targetNode"), TgtId);
		EntryResult->SetStringField(TEXT("targetPin"), TgtPin);

		UEdGraphNode* Src = FindNodeByGuidOrName(TargetGraph, SrcId);
		UEdGraphNode* Tgt = FindNodeByGuidOrName(TargetGraph, TgtId);
		if (!Src || !Tgt)
		{
			EntryResult->SetStringField(TEXT("status"), TEXT("node_not_found"));
			Detail.Add(MakeShared<FJsonValueObject>(EntryResult));
			Failed++;
			continue;
		}
		UEdGraphPin* SP = FindPin(Src, SrcPin);
		UEdGraphPin* TP = FindPin(Tgt, TgtPin);
		if (!SP || !TP)
		{
			EntryResult->SetStringField(TEXT("status"), TEXT("pin_not_found"));
			Detail.Add(MakeShared<FJsonValueObject>(EntryResult));
			Failed++;
			continue;
		}
		if (SP->LinkedTo.Contains(TP))
		{
			EntryResult->SetStringField(TEXT("status"), TEXT("existed"));
			Detail.Add(MakeShared<FJsonValueObject>(EntryResult));
			Existed++;
			continue;
		}
		if (Schema->TryCreateConnection(SP, TP))
		{
			EntryResult->SetStringField(TEXT("status"), TEXT("connected"));
			Connected++;
		}
		else
		{
			FPinConnectionResponse Resp = Schema->CanCreateConnection(SP, TP);
			EntryResult->SetStringField(TEXT("status"), TEXT("failed"));
			EntryResult->SetStringField(TEXT("reason"), Resp.Message.ToString());
			Failed++;
		}
		Detail.Add(MakeShared<FJsonValueObject>(EntryResult));
	}

	if (Connected > 0)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		SaveAssetPackage(Blueprint);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetNumberField(TEXT("total"), ConnectionsArray->Num());
	Result->SetNumberField(TEXT("connected"), Connected);
	Result->SetNumberField(TEXT("existed"), Existed);
	Result->SetNumberField(TEXT("failed"), Failed);
	Result->SetArrayField(TEXT("results"), Detail);
	return MCPResult(Result);
}

// #277 — set_node_position: write NodePosX/NodePosY on a target node.
TSharedPtr<FJsonValue> FBlueprintHandlers::SetNodePosition(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	const FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));
	FString NodeId;
	if (auto Err = RequireStringAlt(Params, TEXT("nodeId"), TEXT("nodeName"), NodeId)) return Err;

	int32 PosX = OptionalInt(Params, TEXT("posX"), 0);
	int32 PosY = OptionalInt(Params, TEXT("posY"), 0);

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph) return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	UEdGraphNode* Node = FindNodeByGuidOrName(Graph, NodeId);
	if (!Node) return MCPError(FString::Printf(TEXT("Node not found: %s"), *NodeId));

	Node->NodePosX = PosX;
	Node->NodePosY = PosY;
	Node->Modify();
	Graph->NotifyGraphChanged();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetStringField(TEXT("nodeId"), NodeId);
	Result->SetNumberField(TEXT("posX"), PosX);
	Result->SetNumberField(TEXT("posY"), PosY);
	return MCPResult(Result);
}

// #277 — auto_layout_graph: simple topological layered layout. Each node is
// placed in a column derived from longest predecessor path; rows are stacked
// with a fixed gap. Not Sugiyama-perfect but eliminates the (0,0) stack that
// programmatic add_node leaves behind.
TSharedPtr<FJsonValue> FBlueprintHandlers::AutoLayoutGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	const FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));
	int32 ColumnGap = OptionalInt(Params, TEXT("columnGap"), 360);
	int32 RowGap = OptionalInt(Params, TEXT("rowGap"), 200);

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph) return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

	// Compute a column index per node: longest predecessor chain through exec
	// pins (or any input pin if exec is absent).
	TMap<UEdGraphNode*, int32> Column;
	TArray<UEdGraphNode*> Order = Graph->Nodes;

	auto GetIncoming = [](UEdGraphNode* Node) -> TArray<UEdGraphNode*>
	{
		TArray<UEdGraphNode*> Result;
		for (UEdGraphPin* P : Node->Pins)
		{
			if (!P || P->Direction != EGPD_Input) continue;
			for (UEdGraphPin* Linked : P->LinkedTo)
			{
				if (Linked && Linked->GetOwningNode()) Result.AddUnique(Linked->GetOwningNode());
			}
		}
		return Result;
	};

	// Iterate to fixed point — simple but adequate for the typical 5-50 node
	// graphs the bridge produces.
	for (int32 Iter = 0; Iter < Order.Num() + 1; Iter++)
	{
		bool bChanged = false;
		for (UEdGraphNode* Node : Order)
		{
			int32 Col = 0;
			for (UEdGraphNode* Inc : GetIncoming(Node))
			{
				if (int32* InCol = Column.Find(Inc)) Col = FMath::Max(Col, *InCol + 1);
			}
			int32* Existing = Column.Find(Node);
			if (!Existing || *Existing != Col)
			{
				Column.Add(Node, Col);
				bChanged = true;
			}
		}
		if (!bChanged) break;
	}

	// Bucket nodes per column, then assign rows in stable order.
	TMap<int32, TArray<UEdGraphNode*>> Buckets;
	for (UEdGraphNode* Node : Order)
	{
		int32 Col = Column.FindRef(Node);
		Buckets.FindOrAdd(Col).Add(Node);
	}

	int32 Repositioned = 0;
	for (auto& Pair : Buckets)
	{
		int32 Col = Pair.Key;
		int32 Row = 0;
		for (UEdGraphNode* Node : Pair.Value)
		{
			Node->NodePosX = Col * ColumnGap;
			Node->NodePosY = Row * RowGap;
			Node->Modify();
			Repositioned++;
			Row++;
		}
	}
	Graph->NotifyGraphChanged();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetNumberField(TEXT("repositioned"), Repositioned);
	Result->SetNumberField(TEXT("columns"), Buckets.Num());
	return MCPResult(Result);
}
