// Split from AnimationHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FAnimationHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in AnimationHandlers.cpp::RegisterHandlers.

#include "AnimationHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Engine/Blueprint.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "AnimationBlueprintLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Factories/AnimBlueprintFactory.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargeter.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "Rig/IKRigDefinition.h"
#include "RigEditor/IKRigController.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "EditorAssetLibrary.h"
#include "Editor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"


// ─── State Machine Helpers ────────────────────────────────────────

static UAnimBlueprint* LoadAnimBP(const FString& Path)
{
	return LoadObject<UAnimBlueprint>(nullptr, *Path);
}


static UEdGraph* FindGraphByName(UBlueprint* BP, const FString& Name)
{
	TArray<UEdGraph*> All;
	BP->GetAllGraphs(All);
	for (UEdGraph* G : All)
	{
		if (G && G->GetName() == Name) return G;
	}
	return nullptr;
}

// Find the SM container node (UAnimGraphNode_StateMachine) by its machine name


// Find the SM container node (UAnimGraphNode_StateMachine) by its machine name
static UAnimGraphNode_StateMachine* FindStateMachineNode(UBlueprint* BP, const FString& MachineName)
{
	TArray<UEdGraph*> All;
	BP->GetAllGraphs(All);
	for (UEdGraph* G : All)
	{
		for (UEdGraphNode* Node : G->Nodes)
		{
			if (UAnimGraphNode_StateMachine* SM = Cast<UAnimGraphNode_StateMachine>(Node))
			{
				if (UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SM->EditorStateMachineGraph))
				{
					if (SMGraph->GetName() == MachineName || SM->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Contains(MachineName))
					{
						return SM;
					}
				}
			}
		}
	}
	return nullptr;
}

// Find a state node by name within a state machine graph


// Find a state node by name within a state machine graph
static UAnimStateNode* FindStateNode(UAnimationStateMachineGraph* SMGraph, const FString& StateName)
{
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		if (UAnimStateNode* State = Cast<UAnimStateNode>(Node))
		{
			if (State->GetStateName() == StateName)
			{
				return State;
			}
		}
	}
	return nullptr;
}


static void CompileAndSave(UBlueprint* BP)
{
	FKismetEditorUtilities::CompileBlueprint(BP);
	SaveAssetPackage(BP);
}

// ─── State Machine Handlers ──────────────────────────────────────


// ─── State Machine Handlers ──────────────────────────────────────

TSharedPtr<FJsonValue> FAnimationHandlers::CreateStateMachine(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString Name = OptionalString(Params, TEXT("name"), TEXT("NewStateMachine"));
	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("AnimGraph"));

	UAnimBlueprint* AnimBP = LoadAnimBP(AssetPath);
	if (!AnimBP)
	{
		return MCPError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));
	}

	UEdGraph* TargetGraph = FindGraphByName(AnimBP, GraphName);
	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	// Idempotency: check for existing state machine by name
	if (FindStateMachineNode(AnimBP, Name))
	{
		auto Existed = MCPSuccess();
		MCPSetExisted(Existed);
		Existed->SetStringField(TEXT("assetPath"), AssetPath);
		Existed->SetStringField(TEXT("name"), Name);
		Existed->SetStringField(TEXT("graphName"), GraphName);
		return MCPResult(Existed);
	}

	// Create the state machine container node in the AnimGraph
	UAnimGraphNode_StateMachine* SMNode = NewObject<UAnimGraphNode_StateMachine>(TargetGraph);
	TargetGraph->AddNode(SMNode, false, false);
	SMNode->CreateNewGuid();
	SMNode->PostPlacedNewNode();  // This creates the EditorStateMachineGraph sub-graph
	SMNode->AllocateDefaultPins();
	SMNode->NodePosX = 200;
	SMNode->NodePosY = 0;

	// Rename the state machine graph to the desired name
	if (SMNode->EditorStateMachineGraph)
	{
		SMNode->EditorStateMachineGraph->Rename(*Name);
	}

	CompileAndSave(AnimBP);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("graphName"), GraphName);
	// No rollback: no paired remove_state_machine handler.

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FAnimationHandlers::AddState(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString SMName;
	if (auto Err = RequireString(Params, TEXT("stateMachineName"), SMName)) return Err;

	FString StateName;
	if (auto Err = RequireString(Params, TEXT("stateName"), StateName)) return Err;

	UAnimBlueprint* AnimBP = LoadAnimBP(AssetPath);
	if (!AnimBP)
	{
		return MCPError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));
	}

	UAnimGraphNode_StateMachine* SMNode = FindStateMachineNode(AnimBP, SMName);
	if (!SMNode)
	{
		return MCPError(FString::Printf(TEXT("State machine '%s' not found"), *SMName));
	}

	UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
	if (!SMGraph)
	{
		return MCPError(TEXT("State machine has no editor graph"));
	}

	// Idempotency: existing state with this name short-circuits
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	if (FindStateNode(SMGraph, StateName))
	{
		if (OnConflict == TEXT("error"))
		{
			return MCPError(FString::Printf(TEXT("State '%s' already exists"), *StateName));
		}
		auto Existed = MCPSuccess();
		MCPSetExisted(Existed);
		Existed->SetStringField(TEXT("assetPath"), AssetPath);
		Existed->SetStringField(TEXT("stateMachineName"), SMName);
		Existed->SetStringField(TEXT("stateName"), StateName);
		return MCPResult(Existed);
	}

	// Create state node
	UAnimStateNode* NewState = NewObject<UAnimStateNode>(SMGraph);
	SMGraph->AddNode(NewState, false, false);
	NewState->CreateNewGuid();
	NewState->PostPlacedNewNode();
	NewState->AllocateDefaultPins();

	// Set the state name via the BoundGraph (the state's internal graph)
	if (NewState->BoundGraph)
	{
		NewState->BoundGraph->Rename(*StateName);
	}

	// Position states in a grid
	int32 StateCount = 0;
	for (UEdGraphNode* N : SMGraph->Nodes) { if (Cast<UAnimStateNode>(N)) StateCount++; }
	NewState->NodePosX = 300 + ((StateCount - 1) % 4) * 300;
	NewState->NodePosY = ((StateCount - 1) / 4) * 200;

	CompileAndSave(AnimBP);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("stateMachineName"), SMName);
	Result->SetStringField(TEXT("stateName"), StateName);
	// No rollback: no paired remove_state handler.

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FAnimationHandlers::AddTransition(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString SMName;
	if (auto Err = RequireString(Params, TEXT("stateMachineName"), SMName)) return Err;

	FString FromState;
	if (auto Err = RequireString(Params, TEXT("fromState"), FromState)) return Err;

	FString ToState;
	if (auto Err = RequireString(Params, TEXT("toState"), ToState)) return Err;

	UAnimBlueprint* AnimBP = LoadAnimBP(AssetPath);
	if (!AnimBP)
	{
		return MCPError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));
	}

	UAnimGraphNode_StateMachine* SMNode = FindStateMachineNode(AnimBP, SMName);
	if (!SMNode)
	{
		return MCPError(FString::Printf(TEXT("State machine '%s' not found"), *SMName));
	}

	UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
	UAnimStateNode* From = FindStateNode(SMGraph, FromState);
	UAnimStateNode* To = FindStateNode(SMGraph, ToState);
	if (!From)
	{
		return MCPError(FString::Printf(TEXT("State '%s' not found"), *FromState));
	}
	if (!To)
	{
		return MCPError(FString::Printf(TEXT("State '%s' not found"), *ToState));
	}

	// Idempotency: check if a transition From→To already exists
	UEdGraphPin* FromOutPin = From->GetOutputPin();
	if (FromOutPin)
	{
		for (UEdGraphPin* Linked : FromOutPin->LinkedTo)
		{
			if (!Linked || !Linked->GetOwningNode()) continue;
			UAnimStateTransitionNode* ExistingTrans = Cast<UAnimStateTransitionNode>(Linked->GetOwningNode());
			if (!ExistingTrans) continue;
			UEdGraphPin* ExistingTransOut = ExistingTrans->GetOutputPin();
			if (!ExistingTransOut) continue;
			for (UEdGraphPin* ToLinked : ExistingTransOut->LinkedTo)
			{
				if (ToLinked && ToLinked->GetOwningNode() == To)
				{
					auto ExistedRes = MCPSuccess();
					MCPSetExisted(ExistedRes);
					ExistedRes->SetStringField(TEXT("assetPath"), AssetPath);
					ExistedRes->SetStringField(TEXT("stateMachineName"), SMName);
					ExistedRes->SetStringField(TEXT("fromState"), FromState);
					ExistedRes->SetStringField(TEXT("toState"), ToState);
					return MCPResult(ExistedRes);
				}
			}
		}
	}

	// Create transition node
	UAnimStateTransitionNode* TransNode = NewObject<UAnimStateTransitionNode>(SMGraph);
	SMGraph->AddNode(TransNode, false, false);
	TransNode->CreateNewGuid();
	TransNode->PostPlacedNewNode();
	TransNode->AllocateDefaultPins();

	// Position between the two states
	TransNode->NodePosX = (From->NodePosX + To->NodePosX) / 2;
	TransNode->NodePosY = (From->NodePosY + To->NodePosY) / 2;

	// Wire: From output → Transition input, Transition output → To input
	UEdGraphPin* FromOut = From->GetOutputPin();
	UEdGraphPin* TransIn = TransNode->GetInputPin();
	UEdGraphPin* TransOut = TransNode->GetOutputPin();
	UEdGraphPin* ToIn = To->GetInputPin();

	if (FromOut && TransIn)
	{
		FromOut->MakeLinkTo(TransIn);
	}
	if (TransOut && ToIn)
	{
		TransOut->MakeLinkTo(ToIn);
	}

	CompileAndSave(AnimBP);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("stateMachineName"), SMName);
	Result->SetStringField(TEXT("fromState"), FromState);
	Result->SetStringField(TEXT("toState"), ToState);
	// No rollback: no paired remove_transition handler.

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FAnimationHandlers::SetStateAnimation(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString SMName, StateName, AnimAssetPath;
	if (!Params->TryGetStringField(TEXT("stateMachineName"), SMName) ||
		!Params->TryGetStringField(TEXT("stateName"), StateName) ||
		!Params->TryGetStringField(TEXT("animAssetPath"), AnimAssetPath))
	{
		return MCPError(TEXT("Missing required params: stateMachineName, stateName, animAssetPath"));
	}

	UAnimBlueprint* AnimBP = LoadAnimBP(AssetPath);
	if (!AnimBP)
	{
		return MCPError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));
	}

	UAnimGraphNode_StateMachine* SMNode = FindStateMachineNode(AnimBP, SMName);
	if (!SMNode)
	{
		return MCPError(FString::Printf(TEXT("State machine '%s' not found"), *SMName));
	}

	UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
	UAnimStateNode* State = FindStateNode(SMGraph, StateName);
	if (!State)
	{
		return MCPError(FString::Printf(TEXT("State '%s' not found"), *StateName));
	}

	// Load the animation asset
	UAnimationAsset* AnimAsset = LoadObject<UAnimationAsset>(nullptr, *AnimAssetPath);
	if (!AnimAsset)
	{
		return MCPError(FString::Printf(TEXT("Animation asset not found: %s"), *AnimAssetPath));
	}

	// Get the state's bound graph (internal graph that plays the animation)
	UEdGraph* StateGraph = State->BoundGraph;
	if (!StateGraph)
	{
		return MCPError(TEXT("State has no BoundGraph"));
	}

	// Find or create the appropriate player node inside the state graph
	// Look for existing sequence player or blendspace player
	UAnimGraphNode_SequencePlayer* SeqPlayer = nullptr;
	UAnimGraphNode_BlendSpacePlayer* BSPlayer = nullptr;
	for (UEdGraphNode* Node : StateGraph->Nodes)
	{
		if (!SeqPlayer) SeqPlayer = Cast<UAnimGraphNode_SequencePlayer>(Node);
		if (!BSPlayer) BSPlayer = Cast<UAnimGraphNode_BlendSpacePlayer>(Node);
	}

	if (UAnimSequence* Seq = Cast<UAnimSequence>(AnimAsset))
	{
		if (!SeqPlayer)
		{
			SeqPlayer = NewObject<UAnimGraphNode_SequencePlayer>(StateGraph);
			StateGraph->AddNode(SeqPlayer, false, false);
			SeqPlayer->CreateNewGuid();
			SeqPlayer->PostPlacedNewNode();
			SeqPlayer->AllocateDefaultPins();
		}
		SeqPlayer->SetAnimationAsset(Seq);
	}
	else if (UBlendSpace* BS = Cast<UBlendSpace>(AnimAsset))
	{
		if (!BSPlayer)
		{
			BSPlayer = NewObject<UAnimGraphNode_BlendSpacePlayer>(StateGraph);
			StateGraph->AddNode(BSPlayer, false, false);
			BSPlayer->CreateNewGuid();
			BSPlayer->PostPlacedNewNode();
			BSPlayer->AllocateDefaultPins();
		}
		BSPlayer->SetAnimationAsset(BS);
	}
	else
	{
		return MCPError(FString::Printf(TEXT("Unsupported animation asset type: %s"), *AnimAsset->GetClass()->GetName()));
	}

	CompileAndSave(AnimBP);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("stateName"), StateName);
	Result->SetStringField(TEXT("animAssetPath"), AnimAssetPath);

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FAnimationHandlers::SetTransitionBlend(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString SMName, FromState, ToState;
	if (!Params->TryGetStringField(TEXT("stateMachineName"), SMName) ||
		!Params->TryGetStringField(TEXT("fromState"), FromState) ||
		!Params->TryGetStringField(TEXT("toState"), ToState))
	{
		return MCPError(TEXT("Missing required params: stateMachineName, fromState, toState"));
	}

	UAnimBlueprint* AnimBP = LoadAnimBP(AssetPath);
	if (!AnimBP)
	{
		return MCPError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));
	}

	UAnimGraphNode_StateMachine* SMNode = FindStateMachineNode(AnimBP, SMName);
	if (!SMNode)
	{
		return MCPError(FString::Printf(TEXT("State machine '%s' not found"), *SMName));
	}

	UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);

	// Find the transition between fromState and toState
	UAnimStateTransitionNode* TransNode = nullptr;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		if (UAnimStateTransitionNode* T = Cast<UAnimStateTransitionNode>(Node))
		{
			UAnimStateNode* Prev = Cast<UAnimStateNode>(T->GetPreviousState());
			UAnimStateNode* Next = Cast<UAnimStateNode>(T->GetNextState());
			if (Prev && Next && Prev->GetStateName() == FromState && Next->GetStateName() == ToState)
			{
				TransNode = T;
				break;
			}
		}
	}

	if (!TransNode)
	{
		return MCPError(FString::Printf(TEXT("No transition from '%s' to '%s'"), *FromState, *ToState));
	}

	// Set blend duration
	double BlendDuration = 0.2;
	if (Params->TryGetNumberField(TEXT("blendDuration"), BlendDuration))
	{
		TransNode->CrossfadeDuration = static_cast<float>(BlendDuration);
	}

	// Set blend logic (Standard vs Inertialization)
	FString BlendLogic;
	if (Params->TryGetStringField(TEXT("blendLogic"), BlendLogic))
	{
		if (BlendLogic.Equals(TEXT("Inertialization"), ESearchCase::IgnoreCase))
		{
			TransNode->BlendMode = EAlphaBlendOption::Linear;
			TransNode->LogicType = ETransitionLogicType::TLT_Inertialization;
		}
		else // Standard
		{
			TransNode->LogicType = ETransitionLogicType::TLT_StandardBlend;
		}
	}

	CompileAndSave(AnimBP);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("fromState"), FromState);
	Result->SetStringField(TEXT("toState"), ToState);
	Result->SetNumberField(TEXT("blendDuration"), BlendDuration);

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FAnimationHandlers::ReadStateMachine(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString SMName;
	if (auto Err = RequireString(Params, TEXT("stateMachineName"), SMName)) return Err;

	UAnimBlueprint* AnimBP = LoadAnimBP(AssetPath);
	if (!AnimBP)
	{
		return MCPError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));
	}

	UAnimGraphNode_StateMachine* SMNode = FindStateMachineNode(AnimBP, SMName);
	if (!SMNode)
	{
		return MCPError(FString::Printf(TEXT("State machine '%s' not found"), *SMName));
	}

	UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
	if (!SMGraph)
	{
		return MCPError(TEXT("State machine has no editor graph"));
	}

	auto Result = MCPSuccess();

	// Enumerate states
	TArray<TSharedPtr<FJsonValue>> StatesArray;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		if (UAnimStateNode* State = Cast<UAnimStateNode>(Node))
		{
			TSharedPtr<FJsonObject> StateObj = MakeShared<FJsonObject>();
			StateObj->SetStringField(TEXT("name"), State->GetStateName());

			// Check for animation asset inside the state
			if (State->BoundGraph)
			{
				for (UEdGraphNode* Inner : State->BoundGraph->Nodes)
				{
					if (UAnimGraphNode_AssetPlayerBase* AssetNode = Cast<UAnimGraphNode_AssetPlayerBase>(Inner))
					{
						if (UAnimationAsset* Asset = AssetNode->GetAnimationAsset())
						{
							StateObj->SetStringField(TEXT("animAsset"), Asset->GetPathName());
						}
					}
				}
			}

			StatesArray.Add(MakeShared<FJsonValueObject>(StateObj));
		}
	}
	Result->SetArrayField(TEXT("states"), StatesArray);

	// Enumerate transitions
	TArray<TSharedPtr<FJsonValue>> TransArray;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		if (UAnimStateTransitionNode* T = Cast<UAnimStateTransitionNode>(Node))
		{
			TSharedPtr<FJsonObject> TransObj = MakeShared<FJsonObject>();

			UAnimStateNode* Prev = Cast<UAnimStateNode>(T->GetPreviousState());
			UAnimStateNode* Next = Cast<UAnimStateNode>(T->GetNextState());
			if (Prev) TransObj->SetStringField(TEXT("fromState"), Prev->GetStateName());
			if (Next) TransObj->SetStringField(TEXT("toState"), Next->GetStateName());

			TransObj->SetNumberField(TEXT("blendDuration"), T->CrossfadeDuration);
			TransObj->SetStringField(TEXT("logicType"),
				T->LogicType == ETransitionLogicType::TLT_Inertialization ? TEXT("Inertialization") : TEXT("Standard"));

			TransArray.Add(MakeShared<FJsonValueObject>(TransObj));
		}
	}
	Result->SetArrayField(TEXT("transitions"), TransArray);

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("stateMachineName"), SMName);

	return MCPResult(Result);
}

// ─── #23 / #91  read_anim_graph ─────────────────────────────────────


// ─── #23 / #91  read_anim_graph ─────────────────────────────────────

TSharedPtr<FJsonValue> FAnimationHandlers::ReadAnimGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("AnimGraph"));

	UAnimBlueprint* AnimBP = LoadAnimBP(AssetPath);
	if (!AnimBP)
	{
		return MCPError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));
	}

	UEdGraph* TargetGraph = FindGraphByName(AnimBP, GraphName);
	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	auto Result = MCPSuccess();

	TArray<TSharedPtr<FJsonValue>> NodesArray;
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

		// ── Serialize editable properties via reflection ──
		TArray<TSharedPtr<FJsonValue>> PropsArray;
		UClass* NodeClass = Node->GetClass();
		for (TFieldIterator<FProperty> PropIt(NodeClass, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit)) continue;

			// Skip internal / noise properties
			FString PropName = Prop->GetName();
			if (PropName.StartsWith(TEXT("Node")) || PropName == TEXT("ErrorMsg") || PropName == TEXT("bHasCompilerMessage"))
				continue;

			TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
			PropObj->SetStringField(TEXT("name"), PropName);
			PropObj->SetStringField(TEXT("type"), Prop->GetCPPType());

			// Attempt to export the value to a human-readable string
			FString ValueStr;
			const void* Container = Node;
			Prop->ExportTextItem_Direct(ValueStr, Prop->ContainerPtrToValuePtr<void>(Container), nullptr, nullptr, PPF_None);
			PropObj->SetStringField(TEXT("value"), ValueStr);

			PropsArray.Add(MakeShared<FJsonValueObject>(PropObj));
		}
		NodeObj->SetArrayField(TEXT("properties"), PropsArray);

		// ── Pins ──
		TArray<TSharedPtr<FJsonValue>> PinsArray;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin) continue;
			TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
			PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
			PinObj->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
			PinObj->SetBoolField(TEXT("connected"), Pin->LinkedTo.Num() > 0);
			PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
		}
		NodeObj->SetArrayField(TEXT("pins"), PinsArray);

		NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetArrayField(TEXT("nodes"), NodesArray);
	Result->SetNumberField(TEXT("nodeCount"), NodesArray.Num());

	return MCPResult(Result);
}

// ─── #79 / #24  add_curve ───────────────────────────────────────────


// ─── #93  create_ik_rig ─────────────────────────────────────────────

TSharedPtr<FJsonValue> FAnimationHandlers::CreateIKRig(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString SkeletalMeshPath;
	if (auto Err = RequireString(Params, TEXT("skeletalMeshPath"), SkeletalMeshPath)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (auto Hit = MCPCheckAssetExists(PackagePath, Name, OnConflict, TEXT("IKRigDefinition")))
	{
		return Hit;
	}

	// Load the skeletal mesh to get the skeleton
	USkeletalMesh* SkelMesh = LoadObject<USkeletalMesh>(nullptr, *SkeletalMeshPath);
	if (!SkelMesh)
	{
		return MCPError(FString::Printf(TEXT("Failed to load SkeletalMesh at '%s'"), *SkeletalMeshPath));
	}

	// Create the IKRigDefinition asset via AssetTools
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FindObject<UClass>(nullptr, TEXT("/Script/IKRigEditor.IKRigDefinitionFactory")));
	if (!Factory)
	{
		return MCPError(TEXT("IKRigDefinitionFactory not found — is the IKRig plugin enabled?"));
	}

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, UIKRigDefinition::StaticClass(), Factory);
	UIKRigDefinition* IKRig = Cast<UIKRigDefinition>(NewAsset);
	if (!IKRig)
	{
		return MCPError(TEXT("Failed to create IKRigDefinition asset"));
	}

	// Prefer IKRigController to atomically configure the rig (#97, #103)
	int32 ChainsAdded = 0;
	TArray<FString> ChainErrors;
	FString RetargetRoot;
	Params->TryGetStringField(TEXT("retargetRoot"), RetargetRoot);

	if (UIKRigController* Controller = UIKRigController::GetController(IKRig))
	{
		Controller->SetSkeletalMesh(SkelMesh);

		if (!RetargetRoot.IsEmpty())
		{
			Controller->SetRetargetRoot(FName(*RetargetRoot));
		}

		const TArray<TSharedPtr<FJsonValue>>* ChainsArr = nullptr;
		if (Params->TryGetArrayField(TEXT("chains"), ChainsArr))
		{
			for (const TSharedPtr<FJsonValue>& V : *ChainsArr)
			{
				if (!V.IsValid() || V->Type != EJson::Object) continue;
				auto O = V->AsObject();
				FString CName = O->GetStringField(TEXT("name"));
				FString SBone = O->GetStringField(TEXT("startBone"));
				FString EBone = O->GetStringField(TEXT("endBone"));
				FString Goal;
				O->TryGetStringField(TEXT("goal"), Goal);
				if (CName.IsEmpty() || SBone.IsEmpty() || EBone.IsEmpty())
				{
					ChainErrors.Add(FString::Printf(TEXT("Chain missing name/startBone/endBone: %s"), *CName));
					continue;
				}
				FName AddedName = Controller->AddRetargetChain(
					FName(*CName),
					FName(*SBone),
					FName(*EBone),
					Goal.IsEmpty() ? NAME_None : FName(*Goal));
				if (AddedName != NAME_None)
				{
					ChainsAdded++;
				}
				else
				{
					ChainErrors.Add(FString::Printf(TEXT("Failed to add chain: %s"), *CName));
				}
			}
		}
	}
	else
	{
		// Fallback if controller not available: basic preview mesh only
		IKRig->SetPreviewMesh(SkelMesh, false);
	}

	IKRig->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(IKRig->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), IKRig->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("skeletalMeshPath"), SkeletalMeshPath);
	if (!RetargetRoot.IsEmpty()) Result->SetStringField(TEXT("retargetRoot"), RetargetRoot);
	Result->SetNumberField(TEXT("chainsAdded"), ChainsAdded);
	if (ChainErrors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrArr;
		for (const FString& E : ChainErrors) ErrArr.Add(MakeShared<FJsonValueString>(E));
		Result->SetArrayField(TEXT("chainErrors"), ErrArr);
	}
	MCPSetDeleteAssetRollback(Result, IKRig->GetPathName());

	return MCPResult(Result);
}

// ─── #93  read_ik_rig ───────────────────────────────────────────────


// ─── #93  read_ik_rig ───────────────────────────────────────────────

TSharedPtr<FJsonValue> FAnimationHandlers::ReadIKRig(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UIKRigDefinition* IKRig = Cast<UIKRigDefinition>(LoadedAsset);
	if (!IKRig)
	{
		return MCPError(FString::Printf(TEXT("Failed to load IKRigDefinition at '%s'"), *AssetPath));
	}

	auto Result = MCPSuccess();

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), IKRig->GetName());

	// Preview mesh
	USkeletalMesh* PreviewMesh = IKRig->GetPreviewMesh();
	if (PreviewMesh)
	{
		Result->SetStringField(TEXT("previewMesh"), PreviewMesh->GetPathName());
	}

	// Skeleton
	const FIKRigSkeleton& RigSkeleton = IKRig->GetSkeleton();
	TArray<TSharedPtr<FJsonValue>> BonesArray;
	for (int32 i = 0; i < RigSkeleton.BoneNames.Num(); ++i)
	{
		TSharedPtr<FJsonObject> BoneObj = MakeShared<FJsonObject>();
		BoneObj->SetStringField(TEXT("name"), RigSkeleton.BoneNames[i].ToString());
		BoneObj->SetNumberField(TEXT("index"), i);
		if (i < RigSkeleton.ParentIndices.Num())
		{
			BoneObj->SetNumberField(TEXT("parentIndex"), RigSkeleton.ParentIndices[i]);
		}
		BonesArray.Add(MakeShared<FJsonValueObject>(BoneObj));
	}
	Result->SetArrayField(TEXT("bones"), BonesArray);

	// Retarget chains
	const TArray<FBoneChain>& Chains = IKRig->GetRetargetChains();
	TArray<TSharedPtr<FJsonValue>> ChainsArray;
	for (const FBoneChain& Chain : Chains)
	{
		TSharedPtr<FJsonObject> ChainObj = MakeShared<FJsonObject>();
		ChainObj->SetStringField(TEXT("name"), Chain.ChainName.ToString());
		ChainObj->SetStringField(TEXT("startBone"), Chain.StartBone.BoneName.ToString());
		ChainObj->SetStringField(TEXT("endBone"), Chain.EndBone.BoneName.ToString());
		ChainsArray.Add(MakeShared<FJsonValueObject>(ChainObj));
	}
	Result->SetArrayField(TEXT("retargetChains"), ChainsArray);

	// Solvers — enumerate via reflection since GetSolverArray not available in all UE versions
	TArray<TSharedPtr<FJsonValue>> SolversArray;
	FProperty* SolversProp = IKRig->GetClass()->FindPropertyByName(TEXT("Solvers"));
	if (SolversProp)
	{
		FString SolversStr;
		const void* ValPtr = SolversProp->ContainerPtrToValuePtr<void>(IKRig);
		SolversProp->ExportText_Direct(SolversStr, ValPtr, ValPtr, IKRig, PPF_None);
		if (!SolversStr.IsEmpty())
		{
			TSharedPtr<FJsonObject> SolverInfo = MakeShared<FJsonObject>();
			SolverInfo->SetStringField(TEXT("raw"), SolversStr);
			SolversArray.Add(MakeShared<FJsonValueObject>(SolverInfo));
		}
	}
	Result->SetArrayField(TEXT("solvers"), SolversArray);

	return MCPResult(Result);
}

// ─── #11  list_control_rig_variables ────────────────────────────────


// ─── #98 create_ik_retargeter ────────────────────────────────────────
TSharedPtr<FJsonValue> FAnimationHandlers::CreateIKRetargeter(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game"));
	FString SourceRigPath = OptionalString(Params, TEXT("sourceRig"));
	FString TargetRigPath = OptionalString(Params, TEXT("targetRig"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (auto Hit = MCPCheckAssetExists(PackagePath, Name, OnConflict, TEXT("IKRetargeter")))
	{
		return Hit;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UClass* FactoryClass = FindObject<UClass>(nullptr, TEXT("/Script/IKRigEditor.IKRetargetFactory"));
	if (!FactoryClass)
	{
		return MCPError(TEXT("IKRetargetFactory not found — is the IKRig plugin enabled?"));
	}
	UClass* RetargeterClass = FindObject<UClass>(nullptr, TEXT("/Script/IKRig.IKRetargeter"));
	if (!RetargeterClass)
	{
		return MCPError(TEXT("IKRetargeter class not found"));
	}
	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, RetargeterClass, Factory);
	if (!NewAsset)
	{
		return MCPError(TEXT("Failed to create IKRetargeter asset"));
	}

	// Optionally set source / target IK Rigs via reflection
	auto SetRigProperty = [&](const FString& PropName, const FString& Path) -> FString
	{
		if (Path.IsEmpty()) return TEXT("");
		UObject* Rig = LoadObject<UObject>(nullptr, *Path);
		if (!Rig) return FString::Printf(TEXT("IKRig not found: %s"), *Path);
		FProperty* Prop = NewAsset->GetClass()->FindPropertyByName(FName(*PropName));
		if (!Prop) return FString::Printf(TEXT("Property not found: %s"), *PropName);
		FString Export = Rig->GetPathName();
		void* Addr = Prop->ContainerPtrToValuePtr<void>(NewAsset);
		return Prop->ImportText_Direct(*Export, Addr, NewAsset, PPF_None) ? TEXT("") : FString::Printf(TEXT("Failed to set %s"), *PropName);
	};

	FString SrcErr = SetRigProperty(TEXT("SourceIKRigAsset"), SourceRigPath);
	FString TgtErr = SetRigProperty(TEXT("TargetIKRigAsset"), TargetRigPath);

	// UE 5.7+ ops-stack initialization (#246). After CreateAsset the per-op
	// IK Rig refs and chain mappings are unset, so the retargeter cannot be
	// driven by an Anim Graph. Mirror what the Python workaround does:
	// AssignIKRigToAllOps(SOURCE/TARGET) + AutoMapChains.
	bool bAutoMap = OptionalBool(Params, TEXT("autoMapChains"), true);
	int32 ChainsMapped = 0;
	FString OpsWarning;
	if (bAutoMap)
	{
		UIKRetargeter* Retargeter = Cast<UIKRetargeter>(NewAsset);
		if (Retargeter)
		{
			UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
			if (Controller)
			{
				if (!SourceRigPath.IsEmpty())
				{
					if (UIKRigDefinition* SrcRig = Cast<UIKRigDefinition>(LoadObject<UObject>(nullptr, *SourceRigPath)))
					{
						Controller->AssignIKRigToAllOps(ERetargetSourceOrTarget::Source, SrcRig);
					}
				}
				if (!TargetRigPath.IsEmpty())
				{
					if (UIKRigDefinition* TgtRig = Cast<UIKRigDefinition>(LoadObject<UObject>(nullptr, *TargetRigPath)))
					{
						Controller->AssignIKRigToAllOps(ERetargetSourceOrTarget::Target, TgtRig);
					}
				}
				Controller->AutoMapChains(EAutoMapChainType::Exact, true);
				// AutoMapChains returns void in UE 5.7; count populated mappings by re-querying.
				if (const UIKRigDefinition* TgtRig2 = Controller->GetIKRig(ERetargetSourceOrTarget::Target))
				{
					for (const FBoneChain& C : TgtRig2->GetRetargetChains())
					{
						if (!Controller->GetSourceChain(C.ChainName).IsNone()) ChainsMapped++;
					}
				}
			}
			else
			{
				OpsWarning = TEXT("IKRetargeterController unavailable — chains not mapped (call autoMapChains=false to suppress)");
			}
		}
	}

	NewAsset->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	if (!SrcErr.IsEmpty()) Result->SetStringField(TEXT("sourceRigWarning"), SrcErr);
	if (!TgtErr.IsEmpty()) Result->SetStringField(TEXT("targetRigWarning"), TgtErr);
	if (bAutoMap)
	{
		Result->SetNumberField(TEXT("chainsMapped"), ChainsMapped);
		if (!OpsWarning.IsEmpty()) Result->SetStringField(TEXT("opsWarning"), OpsWarning);
	}
	MCPSetDeleteAssetRollback(Result, NewAsset->GetPathName());
	return MCPResult(Result);
}

// ─── #246  read_ik_retargeter ──────────────────────────────────────────
TSharedPtr<FJsonValue> FAnimationHandlers::ReadIKRetargeter(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	UIKRetargeter* Retargeter = Cast<UIKRetargeter>(LoadObject<UObject>(nullptr, *AssetPath));
	if (!Retargeter)
	{
		return MCPError(FString::Printf(TEXT("IKRetargeter not found: %s"), *AssetPath));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);

	UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
	if (!Controller)
	{
		Result->SetStringField(TEXT("warning"), TEXT("IKRetargeterController unavailable — returning shallow data"));
		return MCPResult(Result);
	}

	const UIKRigDefinition* SrcRig = Controller->GetIKRig(ERetargetSourceOrTarget::Source);
	const UIKRigDefinition* TgtRig = Controller->GetIKRig(ERetargetSourceOrTarget::Target);
	Result->SetStringField(TEXT("sourceRig"), SrcRig ? SrcRig->GetPathName() : TEXT(""));
	Result->SetStringField(TEXT("targetRig"), TgtRig ? TgtRig->GetPathName() : TEXT(""));

	// Chain mappings: for each target chain, report the source chain it's mapped to.
	TArray<TSharedPtr<FJsonValue>> Mappings;
	if (TgtRig)
	{
		const TArray<FBoneChain>& TargetChains = TgtRig->GetRetargetChains();
		for (const FBoneChain& TgtChain : TargetChains)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("targetChain"), TgtChain.ChainName.ToString());
			FName SourceChain = Controller->GetSourceChain(TgtChain.ChainName);
			Entry->SetStringField(TEXT("sourceChain"), SourceChain.IsNone() ? TEXT("") : SourceChain.ToString());
			Mappings.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}
	Result->SetArrayField(TEXT("chainMappings"), Mappings);
	return MCPResult(Result);
}

// ─── #99 set_anim_blueprint_skeleton ──────────────────────────────────


// ===========================================================================
// v0.7.15 — PoseSearch (motion matching)
// ===========================================================================

TSharedPtr<FJsonValue> FAnimationHandlers::CreatePoseSearchDatabase(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/MotionMatching"));
	const FString SchemaPath = OptionalString(Params, TEXT("schemaPath"), TEXT(""));

	if (auto Hit = MCPCheckAssetExists(PackagePath, Name, OptionalString(Params, TEXT("onConflict"), TEXT("skip")), TEXT("PoseSearchDatabase")))
	{
		return Hit;
	}

	const FString PkgName = PackagePath + TEXT("/") + Name;
	UPackage* Package = CreatePackage(*PkgName);
	UPoseSearchDatabase* Database = NewObject<UPoseSearchDatabase>(Package, UPoseSearchDatabase::StaticClass(), *Name, RF_Public | RF_Standalone);
	if (!Database) return MCPError(TEXT("Failed to create PoseSearchDatabase"));

	if (!SchemaPath.IsEmpty())
	{
		UPoseSearchSchema* Schema = Cast<UPoseSearchSchema>(UEditorAssetLibrary::LoadAsset(SchemaPath));
		if (!Schema) return MCPError(FString::Printf(TEXT("Schema not found: %s"), *SchemaPath));
		Database->Schema = Schema;
	}

	FAssetRegistryModule::AssetCreated(Database);
	Database->MarkPackageDirty();
	Package->SetDirtyFlag(true);
	UEditorAssetLibrary::SaveLoadedAsset(Database);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetCreated(Res);
	Res->SetStringField(TEXT("path"), Database->GetPathName());
	Res->SetStringField(TEXT("name"), Name);
	Res->SetStringField(TEXT("packagePath"), PackagePath);
	Res->SetStringField(TEXT("schemaPath"), SchemaPath);
	MCPSetDeleteAssetRollback(Res, Database->GetPathName());
	return MCPResult(Res);
}


TSharedPtr<FJsonValue> FAnimationHandlers::SetPoseSearchSchema(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	FString SchemaPath;
	if (auto Err = RequireString(Params, TEXT("schemaPath"), SchemaPath)) return Err;

	UPoseSearchDatabase* Database = Cast<UPoseSearchDatabase>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Database) return MCPError(FString::Printf(TEXT("PoseSearchDatabase not found: %s"), *AssetPath));

	UPoseSearchSchema* Schema = Cast<UPoseSearchSchema>(UEditorAssetLibrary::LoadAsset(SchemaPath));
	if (!Schema) return MCPError(FString::Printf(TEXT("Schema not found: %s"), *SchemaPath));

	const FString PrevSchemaPath = Database->Schema ? Database->Schema->GetPathName() : FString();
	Database->Modify();
	Database->Schema = Schema;
	Database->PostEditChange();
	UEditorAssetLibrary::SaveLoadedAsset(Database);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetStringField(TEXT("schemaPath"), SchemaPath);
	Res->SetStringField(TEXT("previousSchemaPath"), PrevSchemaPath);

	TSharedPtr<FJsonObject> RbPayload = MakeShared<FJsonObject>();
	RbPayload->SetStringField(TEXT("assetPath"), AssetPath);
	RbPayload->SetStringField(TEXT("schemaPath"), PrevSchemaPath);
	if (!PrevSchemaPath.IsEmpty()) MCPSetRollback(Res, TEXT("set_pose_search_schema"), RbPayload);
	return MCPResult(Res);
}


TSharedPtr<FJsonValue> FAnimationHandlers::AddPoseSearchSequence(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	FString SequencePath;
	if (auto Err = RequireString(Params, TEXT("sequencePath"), SequencePath)) return Err;

	UPoseSearchDatabase* Database = Cast<UPoseSearchDatabase>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Database) return MCPError(FString::Printf(TEXT("PoseSearchDatabase not found: %s"), *AssetPath));

	UObject* AnimAsset = UEditorAssetLibrary::LoadAsset(SequencePath);
	if (!AnimAsset) return MCPError(FString::Printf(TEXT("Animation asset not found: %s"), *SequencePath));

	// PoseSearch accepts AnimSequence, AnimComposite, AnimMontage, BlendSpace, MultiAnimAsset.
	if (!AnimAsset->IsA<UAnimSequenceBase>() && !AnimAsset->IsA<UBlendSpace>())
	{
		return MCPError(FString::Printf(TEXT("Animation asset type not supported by PoseSearch: %s"), *AnimAsset->GetClass()->GetName()));
	}

	const int32 PrevCount = Database->GetNumAnimationAssets();
	FPoseSearchDatabaseAnimationAsset NewEntry;
	NewEntry.AnimAsset = AnimAsset;
	Database->Modify();
	Database->AddAnimationAsset(NewEntry);
	Database->PostEditChange();
	UEditorAssetLibrary::SaveLoadedAsset(Database);

	const int32 NewCount = Database->GetNumAnimationAssets();

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	MCPSetUpdated(Res);
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetStringField(TEXT("sequencePath"), SequencePath);
	Res->SetNumberField(TEXT("previousCount"), PrevCount);
	Res->SetNumberField(TEXT("newCount"), NewCount);
	Res->SetNumberField(TEXT("addedIndex"), NewCount - 1);
	return MCPResult(Res);
}


TSharedPtr<FJsonValue> FAnimationHandlers::BuildPoseSearchIndex(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	const bool bWait = OptionalBool(Params, TEXT("wait"), true);

	UPoseSearchDatabase* Database = Cast<UPoseSearchDatabase>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Database) return MCPError(FString::Printf(TEXT("PoseSearchDatabase not found: %s"), *AssetPath));
	if (!Database->Schema) return MCPError(TEXT("Database has no Schema set — call set_pose_search_schema first"));
	if (Database->GetNumAnimationAssets() == 0) return MCPError(TEXT("Database has no animation assets — call add_pose_search_sequence first"));

	using namespace UE::PoseSearch;
	const ERequestAsyncBuildFlag Flag = bWait
		? (ERequestAsyncBuildFlag::NewRequest | ERequestAsyncBuildFlag::WaitForCompletion)
		: ERequestAsyncBuildFlag::NewRequest;
	const EAsyncBuildIndexResult Result = FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database, Flag);

	FString ResultStr;
	bool bSuccess = false;
	switch (Result)
	{
		case EAsyncBuildIndexResult::Success:    ResultStr = TEXT("Success"); bSuccess = true; break;
		case EAsyncBuildIndexResult::InProgress: ResultStr = TEXT("InProgress"); bSuccess = true; break;
		case EAsyncBuildIndexResult::Failed:     ResultStr = TEXT("Failed"); break;
	}

	UEditorAssetLibrary::SaveLoadedAsset(Database);

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	if (bSuccess) MCPSetUpdated(Res);
	Res->SetBoolField(TEXT("success"), bSuccess);
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetStringField(TEXT("result"), ResultStr);
	Res->SetBoolField(TEXT("waitedForCompletion"), bWait);
	Res->SetNumberField(TEXT("animationAssetCount"), Database->GetNumAnimationAssets());
	return MCPResult(Res);
}


TSharedPtr<FJsonValue> FAnimationHandlers::ReadPoseSearchDatabase(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UPoseSearchDatabase* Database = Cast<UPoseSearchDatabase>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Database) return MCPError(FString::Printf(TEXT("PoseSearchDatabase not found: %s"), *AssetPath));

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("path"), AssetPath);
	Res->SetStringField(TEXT("name"), Database->GetName());
	Res->SetStringField(TEXT("schemaPath"), Database->Schema ? Database->Schema->GetPathName() : FString());
	Res->SetNumberField(TEXT("continuingPoseCostBias"), Database->ContinuingPoseCostBias);
	Res->SetNumberField(TEXT("baseCostBias"), Database->BaseCostBias);
	Res->SetNumberField(TEXT("loopingCostBias"), Database->LoopingCostBias);
	Res->SetNumberField(TEXT("kdTreeQueryNumNeighbors"), Database->KDTreeQueryNumNeighbors);

	const int32 AssetCount = Database->GetNumAnimationAssets();
	Res->SetNumberField(TEXT("animationAssetCount"), AssetCount);

	TArray<TSharedPtr<FJsonValue>> Animations;
	for (int32 i = 0; i < AssetCount; ++i)
	{
		const FPoseSearchDatabaseAnimationAsset* Entry = Database->GetDatabaseAnimationAsset(i);
		if (!Entry) continue;
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetNumberField(TEXT("index"), i);
		A->SetStringField(TEXT("assetPath"), Entry->AnimAsset ? Entry->AnimAsset->GetPathName() : FString());
		A->SetStringField(TEXT("assetClass"), Entry->AnimAsset ? Entry->AnimAsset->GetClass()->GetName() : FString());
		A->SetBoolField(TEXT("isLooping"), Entry->IsLooping());
		A->SetBoolField(TEXT("isRootMotionEnabled"), Entry->IsRootMotionEnabled());
		Animations.Add(MakeShared<FJsonValueObject>(A));
	}
	Res->SetArrayField(TEXT("animationAssets"), Animations);

	TArray<TSharedPtr<FJsonValue>> Tags;
	for (const FName& T : Database->Tags) Tags.Add(MakeShared<FJsonValueString>(T.ToString()));
	Res->SetArrayField(TEXT("tags"), Tags);

	if (Database->Schema)
	{
		TSharedPtr<FJsonObject> SchemaObj = MakeShared<FJsonObject>();
		SchemaObj->SetStringField(TEXT("path"), Database->Schema->GetPathName());
		SchemaObj->SetNumberField(TEXT("sampleRate"), Database->Schema->SampleRate);
		// Channels/Skeletons are private members; use the public GetChannels() accessor
		// (returns the finalized channel set) and fall back to reflection for Skeletons.
		SchemaObj->SetNumberField(TEXT("channelCount"), Database->Schema->GetChannels().Num());
		int32 SkeletonCount = 0;
		if (FProperty* SkelProp = Database->Schema->GetClass()->FindPropertyByName(TEXT("Skeletons")))
		{
			if (FArrayProperty* ArrProp = CastField<FArrayProperty>(SkelProp))
			{
				FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>(Database->Schema));
				SkeletonCount = Helper.Num();
			}
		}
		SchemaObj->SetNumberField(TEXT("skeletonCount"), SkeletonCount);
		Res->SetObjectField(TEXT("schema"), SchemaObj);
	}

	return MCPResult(Res);
}
