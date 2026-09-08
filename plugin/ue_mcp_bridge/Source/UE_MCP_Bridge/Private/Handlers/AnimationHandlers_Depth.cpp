// Animation authoring depth: the removal half of the animation surface, the
// state machine's entry wiring, windowed notifies, and sync markers.
//
// All functions below are still members of FAnimationHandlers - this file is a
// translation-unit partition, not a new class. Handler registration stays in
// AnimationHandlers.cpp::RegisterHandlers.
//
// Why these and not more. The animation category already ships 100+ actions,
// and an audit against what an agent needs to author an animation system end
// to end found four holes rather than a broad shortfall:
//
//  1. A state machine authored through the bridge NEVER RAN. create_state_machine
//     places the container node and add_state adds states, but nothing ever
//     wired UAnimationStateMachineGraph::EntryNode to a state, so the machine
//     had no initial state. `set_state_machine_entry` closes that.
//  2. Five adds documented their own missing inverse in a source comment:
//     create_state_machine, add_state, add_transition, add_montage_section and
//     add_curve each end with "No rollback: no paired remove_* handler." Those
//     five removals are here.
//  3. NOTIFY STATES could not be authored at all. add_anim_notify only ever
//     writes FAnimNotifyEvent::Notify, never NotifyStateClass, and never sets a
//     duration, so every windowed notify (combo windows, hit windows, timed
//     particle effects) was out of reach. remove_anim_notify's class filter
//     likewise only inspects `Notify`, so it cannot see a state either.
//  4. SYNC MARKERS could not be authored. AuthoredSyncMarkers is a bare
//     UPROPERTY, so a raw property write reaches the array, but the markers
//     stay inert until UAnimSequence::RefreshSyncMarkerDataFromAuthored runs
//     and the names are registered on the skeleton. That is why this is an
//     "apply, refresh, register, read back" action and not a setter.
//
// Deliberately NOT built, because a property write already reaches them:
//  - Section next-section links. FCompositeSection::NextSectionName is a
//    UPROPERTY inside UAnimMontage::CompositeSections, so
//    asset(set_property, "CompositeSections[i].NextSectionName") sets it.
//  - Transition tuning (CrossfadeDuration, PriorityOrder, LogicType,
//    Bidirectional, bAutomaticRuleBasedOnSequencePlayerInState) and every
//    notify trigger setting (NotifyTriggerChance, NotifyFilterType,
//    bTriggerOnDedicatedServer and the rest). All plain UPROPERTYs.
//  - BlendSpace sample removal. populate_blendspace already replaces the whole
//    sample list, and clearExisting defaults to true.
//  - Generic anim graph node authoring. blueprint(add_node) resolves any
//    UEdGraphNode subclass including the UAnimGraphNode_* family, and
//    blueprint(connect_pins) uses the target graph's own schema, so blend
//    nodes and pose wiring are already reachable there.

#include "AnimationHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimData/CurveIdentifier.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimationStateMachineGraph.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

#include "EditorAssetLibrary.h"
#include "UObject/UnrealType.h"

// ── File-local helpers ───────────────────────────────────────────────────────
//
// These duplicate the SHAPE of the statics in AnimationHandlers_StateMachine.cpp
// but not their names. The module is a unity build, so two file-local functions
// with the same signature in two .cpp files that land in the same blob are a
// redefinition (C2084). The prefix is what keeps them apart; the alternative,
// promoting them into Public/HandlerUtils.h, would put anim-graph editor types
// into a header every handler includes.

static UAnimBlueprint* MCPAnimDepthLoadAnimBP(const FString& Path)
{
	return LoadAssetByPath<UAnimBlueprint>(Path);
}

/** The state machine container node whose graph is named MachineName. */
static UAnimGraphNode_StateMachine* MCPAnimDepthFindStateMachine(UBlueprint* BP, const FString& MachineName)
{
	TArray<UEdGraph*> All;
	BP->GetAllGraphs(All);
	for (UEdGraph* G : All)
	{
		if (!G) continue;
		for (UEdGraphNode* Node : G->Nodes)
		{
			UAnimGraphNode_StateMachine* SM = Cast<UAnimGraphNode_StateMachine>(Node);
			if (!SM) continue;
			if (UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SM->EditorStateMachineGraph))
			{
				if (SMGraph->GetName() == MachineName
					|| SM->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Contains(MachineName))
				{
					return SM;
				}
			}
		}
	}
	return nullptr;
}

/** Every state machine name in the blueprint, so a miss can name the hits. */
static TArray<FString> MCPAnimDepthListStateMachines(UBlueprint* BP)
{
	TArray<FString> Names;
	TArray<UEdGraph*> All;
	BP->GetAllGraphs(All);
	for (UEdGraph* G : All)
	{
		if (!G) continue;
		for (UEdGraphNode* Node : G->Nodes)
		{
			if (UAnimGraphNode_StateMachine* SM = Cast<UAnimGraphNode_StateMachine>(Node))
			{
				if (SM->EditorStateMachineGraph)
				{
					Names.AddUnique(SM->EditorStateMachineGraph->GetName());
				}
			}
		}
	}
	return Names;
}

static UAnimStateNode* MCPAnimDepthFindState(UAnimationStateMachineGraph* SMGraph, const FString& StateName)
{
	if (!SMGraph) return nullptr;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		if (UAnimStateNode* State = Cast<UAnimStateNode>(Node))
		{
			if (State->GetStateName() == StateName) return State;
		}
	}
	return nullptr;
}

static TArray<FString> MCPAnimDepthListStates(UAnimationStateMachineGraph* SMGraph)
{
	TArray<FString> Names;
	if (!SMGraph) return Names;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		if (UAnimStateNode* State = Cast<UAnimStateNode>(Node))
		{
			Names.AddUnique(State->GetStateName());
		}
	}
	return Names;
}

/**
 * Remove one state-like node and the graph it owns.
 *
 * UAnimStateNodeBase::DestroyNode() also disposes of the bound graph, but what
 * it does with it is not visible from the header, and a bound graph left in the
 * blueprint after its node is gone makes the next compile assert on a graph
 * with no owning node. So the sequence is explicit: break the links, detach the
 * bound graph from the node, drop the node, then remove the graph through
 * FBlueprintEditorUtils, which is the call that unregisters it from the
 * blueprint's graph lists. A transition may SHARE its rule graph with another
 * transition, and removing a shared graph would break the sibling, so that case
 * leaves the graph in place and the caller is told.
 */
static void MCPAnimDepthRemoveStateLikeNode(
	UBlueprint* BP,
	UEdGraph* OwningGraph,
	UAnimStateNodeBase* Node,
	bool& bOutBoundGraphKeptBecauseShared)
{
	bOutBoundGraphKeptBecauseShared = false;
	if (!Node || !OwningGraph) return;

	UEdGraph* Bound = Node->GetBoundGraph();
	if (UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(Node))
	{
		if (Bound && Transition->IsBoundGraphShared())
		{
			bOutBoundGraphKeptBecauseShared = true;
			Bound = nullptr;
		}
	}

	Node->BreakAllNodeLinks();
	Node->ClearBoundGraph();
	OwningGraph->RemoveNode(Node);

	if (Bound)
	{
		FBlueprintEditorUtils::RemoveGraph(BP, Bound, EGraphRemoveFlags::MarkTransient);
	}
}

static void MCPAnimDepthCompileAndSave(UBlueprint* BP)
{
	FKismetEditorUtilities::CompileBlueprint(BP);
	SaveAssetPackage(BP);
}

/** A transition described the way add_transition / read_state_machine report it. */
static TSharedPtr<FJsonObject> MCPAnimDepthDescribeTransition(UAnimStateTransitionNode* T)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	if (!T) return O;
	O->SetStringField(TEXT("transitionGuid"), T->NodeGuid.ToString());
	if (UAnimStateNodeBase* Prev = T->GetPreviousState())
	{
		O->SetStringField(TEXT("fromState"), Prev->GetStateName());
	}
	if (UAnimStateNodeBase* Next = T->GetNextState())
	{
		O->SetStringField(TEXT("toState"), Next->GetStateName());
	}
	if (T->BoundGraph)
	{
		O->SetStringField(TEXT("boundGraph"), T->BoundGraph->GetName());
	}
	O->SetNumberField(TEXT("blendDuration"), T->CrossfadeDuration);
	return O;
}

// ─────────────────────────────────────────────────────────────────────────────
// set_state_machine_entry
//
// The entry node is the only thing that says which state a machine starts in.
// Nothing in the bridge wired it, so every state machine authored here compiled
// with no initial state and produced the reference pose at runtime. There is no
// property to write: the link is an FEdGraphPin connection between
// UAnimationStateMachineGraph::EntryNode's output pin and the state's input pin.
// ─────────────────────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FAnimationHandlers::SetStateMachineEntry(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString SMName;
	if (auto Err = RequireString(Params, TEXT("stateMachineName"), SMName)) return Err;

	UAnimBlueprint* AnimBP = MCPAnimDepthLoadAnimBP(AssetPath);
	if (!AnimBP)
	{
		return MCPError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));
	}

	UAnimGraphNode_StateMachine* SMNode = MCPAnimDepthFindStateMachine(AnimBP, SMName);
	if (!SMNode)
	{
		const TArray<FString> Known = MCPAnimDepthListStateMachines(AnimBP);
		return MCPError(FString::Printf(
			TEXT("State machine '%s' not found in %s. State machines in this AnimBlueprint: %s"),
			*SMName, *AssetPath,
			Known.Num() > 0 ? *FString::Join(Known, TEXT(", ")) : TEXT("(none - call animation(create_state_machine) first)")));
	}

	UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
	if (!SMGraph)
	{
		return MCPError(FString::Printf(TEXT("State machine '%s' has no editor graph."), *SMName));
	}

	UAnimStateEntryNode* Entry = SMGraph->EntryNode;
	if (!Entry)
	{
		return MCPError(FString::Printf(
			TEXT("State machine '%s' has no entry node, which means its graph was never initialised. ")
			TEXT("Recreate it with animation(create_state_machine)."), *SMName));
	}

	UEdGraphPin* EntryPin = Entry->GetOutputPin();
	if (!EntryPin)
	{
		return MCPError(TEXT("The state machine's entry node has no output pin."));
	}

	// What it points at now, which is both the idempotency check and the
	// rollback payload.
	FString PreviousState;
	if (UEdGraphNode* Current = Entry->GetOutputNode())
	{
		if (UAnimStateNodeBase* CurrentState = Cast<UAnimStateNodeBase>(Current))
		{
			PreviousState = CurrentState->GetStateName();
		}
	}

	// An empty (or omitted) stateName clears the entry link, so the pair is a
	// complete set/clear rather than a one-way write.
	const FString StateName = OptionalString(Params, TEXT("stateName"));
	UAnimStateNode* Target = nullptr;
	if (!StateName.IsEmpty())
	{
		Target = MCPAnimDepthFindState(SMGraph, StateName);
		if (!Target)
		{
			const TArray<FString> Known = MCPAnimDepthListStates(SMGraph);
			return MCPError(FString::Printf(
				TEXT("State '%s' not found in state machine '%s'. States: %s"),
				*StateName, *SMName,
				Known.Num() > 0 ? *FString::Join(Known, TEXT(", ")) : TEXT("(none - call animation(add_state) first)")));
		}
		if (!Target->GetInputPin())
		{
			return MCPError(FString::Printf(TEXT("State '%s' has no input pin to link the entry to."), *StateName));
		}
	}

	auto AttachRollback = [&PreviousState, &AssetPath, &SMName](TSharedPtr<FJsonObject> Res)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("stateMachineName"), SMName);
		Payload->SetStringField(TEXT("stateName"), PreviousState);
		MCPSetRollback(Res, TEXT("set_state_machine_entry"), Payload);
	};

	if (PreviousState == StateName)
	{
		auto Unchanged = MCPSuccess();
		MCPSetExisted(Unchanged);
		Unchanged->SetBoolField(TEXT("unchanged"), true);
		Unchanged->SetStringField(TEXT("assetPath"), AssetPath);
		Unchanged->SetStringField(TEXT("stateMachineName"), SMName);
		Unchanged->SetStringField(TEXT("entryState"), PreviousState);
		AttachRollback(Unchanged);
		return MCPResult(Unchanged);
	}

	EntryPin->BreakAllPinLinks();
	if (Target)
	{
		EntryPin->MakeLinkTo(Target->GetInputPin());
	}

	MCPAnimDepthCompileAndSave(AnimBP);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("stateMachineName"), SMName);
	Result->SetStringField(TEXT("entryState"), StateName);
	Result->SetStringField(TEXT("previousEntryState"), PreviousState);
	Result->SetBoolField(TEXT("unchanged"), false);
	AttachRollback(Result);
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// remove_state
// ─────────────────────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FAnimationHandlers::RemoveState(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString SMName;
	if (auto Err = RequireString(Params, TEXT("stateMachineName"), SMName)) return Err;

	FString StateName;
	if (auto Err = RequireString(Params, TEXT("stateName"), StateName)) return Err;

	UAnimBlueprint* AnimBP = MCPAnimDepthLoadAnimBP(AssetPath);
	if (!AnimBP)
	{
		return MCPError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));
	}

	UAnimGraphNode_StateMachine* SMNode = MCPAnimDepthFindStateMachine(AnimBP, SMName);
	if (!SMNode)
	{
		const TArray<FString> Known = MCPAnimDepthListStateMachines(AnimBP);
		return MCPError(FString::Printf(
			TEXT("State machine '%s' not found in %s. State machines in this AnimBlueprint: %s"),
			*SMName, *AssetPath,
			Known.Num() > 0 ? *FString::Join(Known, TEXT(", ")) : TEXT("(none)")));
	}

	UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
	if (!SMGraph)
	{
		return MCPError(FString::Printf(TEXT("State machine '%s' has no editor graph."), *SMName));
	}

	UAnimStateNode* State = MCPAnimDepthFindState(SMGraph, StateName);
	if (!State)
	{
		// Idempotent: removing what is already gone is a success that says so.
		auto Noop = MCPSuccess();
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		Noop->SetStringField(TEXT("assetPath"), AssetPath);
		Noop->SetStringField(TEXT("stateMachineName"), SMName);
		Noop->SetStringField(TEXT("stateName"), StateName);
		Noop->SetStringField(TEXT("note"), TEXT("No state by that name; nothing was removed."));
		return MCPResult(Noop);
	}

	// Was this the entry state? Removing it silently would leave the machine
	// with no initial state, which is the failure this file exists to stop.
	bool bWasEntryState = false;
	if (SMGraph->EntryNode)
	{
		if (UEdGraphNode* Current = SMGraph->EntryNode->GetOutputNode())
		{
			bWasEntryState = (Current == State);
		}
	}

	// Every transition touching the state has to go with it: a transition whose
	// endpoint no longer exists fails the blueprint compile.
	TArray<UAnimStateTransitionNode*> Transitions;
	State->GetTransitionList(Transitions);

	TArray<TSharedPtr<FJsonValue>> RemovedTransitions;
	int32 SharedRuleGraphsKept = 0;
	for (UAnimStateTransitionNode* T : Transitions)
	{
		if (!T) continue;
		RemovedTransitions.Add(MakeShared<FJsonValueObject>(MCPAnimDepthDescribeTransition(T)));
		bool bShared = false;
		MCPAnimDepthRemoveStateLikeNode(AnimBP, SMGraph, T, bShared);
		if (bShared) SharedRuleGraphsKept++;
	}

	bool bStateGraphShared = false;
	MCPAnimDepthRemoveStateLikeNode(AnimBP, SMGraph, State, bStateGraphShared);

	MCPAnimDepthCompileAndSave(AnimBP);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("alreadyDeleted"), false);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("stateMachineName"), SMName);
	Result->SetStringField(TEXT("stateName"), StateName);
	Result->SetArrayField(TEXT("removedTransitions"), RemovedTransitions);
	Result->SetNumberField(TEXT("removedTransitionCount"), RemovedTransitions.Num());
	Result->SetNumberField(TEXT("sharedRuleGraphsKept"), SharedRuleGraphsKept);
	Result->SetBoolField(TEXT("wasEntryState"), bWasEntryState);
	if (bWasEntryState)
	{
		Result->SetStringField(TEXT("warning"),
			TEXT("This was the state machine's entry state, so the machine now has no initial state and will output the reference pose. ")
			TEXT("Point it at another state with animation(set_state_machine_entry)."));
	}

	// The inverse re-creates the state, and nothing else. Say so rather than
	// implying a clean undo: the state's inner graph (its pose nodes, its asset
	// player) and every removed transition's rule graph are gone with it.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("stateMachineName"), SMName);
	Payload->SetStringField(TEXT("stateName"), StateName);
	MCPSetRollback(Result, TEXT("add_state"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("add_state restores an EMPTY state. The state's own graph contents, and the removed transitions with their rule graphs, are not restored. ")
		TEXT("The removed transitions are listed in removedTransitions so they can be replayed with animation(add_transition)."));
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// remove_transition
// ─────────────────────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FAnimationHandlers::RemoveTransition(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString SMName;
	if (auto Err = RequireString(Params, TEXT("stateMachineName"), SMName)) return Err;

	const FString TransitionGuid = OptionalString(Params, TEXT("transitionGuid"));
	const FString FromState = OptionalString(Params, TEXT("fromState"));
	const FString ToState = OptionalString(Params, TEXT("toState"));
	if (TransitionGuid.IsEmpty() && (FromState.IsEmpty() || ToState.IsEmpty()))
	{
		return MCPError(TEXT("Address the transition either by 'transitionGuid' (from add_transition or read_state_machine) or by both 'fromState' and 'toState'."));
	}

	UAnimBlueprint* AnimBP = MCPAnimDepthLoadAnimBP(AssetPath);
	if (!AnimBP)
	{
		return MCPError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));
	}

	UAnimGraphNode_StateMachine* SMNode = MCPAnimDepthFindStateMachine(AnimBP, SMName);
	if (!SMNode)
	{
		const TArray<FString> Known = MCPAnimDepthListStateMachines(AnimBP);
		return MCPError(FString::Printf(
			TEXT("State machine '%s' not found in %s. State machines in this AnimBlueprint: %s"),
			*SMName, *AssetPath,
			Known.Num() > 0 ? *FString::Join(Known, TEXT(", ")) : TEXT("(none)")));
	}

	UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
	if (!SMGraph)
	{
		return MCPError(FString::Printf(TEXT("State machine '%s' has no editor graph."), *SMName));
	}

	// Match first, mutate second: a from/to pair can name several transitions,
	// and a half-applied removal leaves the graph in a state the caller did not
	// ask for.
	TArray<UAnimStateTransitionNode*> Matches;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		UAnimStateTransitionNode* T = Cast<UAnimStateTransitionNode>(Node);
		if (!T) continue;

		if (!TransitionGuid.IsEmpty())
		{
			if (T->NodeGuid.ToString() == TransitionGuid) Matches.Add(T);
			continue;
		}

		UAnimStateNodeBase* Prev = T->GetPreviousState();
		UAnimStateNodeBase* Next = T->GetNextState();
		if (Prev && Next && Prev->GetStateName() == FromState && Next->GetStateName() == ToState)
		{
			Matches.Add(T);
		}
	}

	if (Matches.Num() == 0)
	{
		auto Noop = MCPSuccess();
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		Noop->SetStringField(TEXT("assetPath"), AssetPath);
		Noop->SetStringField(TEXT("stateMachineName"), SMName);
		Noop->SetStringField(TEXT("transitionGuid"), TransitionGuid);
		Noop->SetStringField(TEXT("fromState"), FromState);
		Noop->SetStringField(TEXT("toState"), ToState);
		Noop->SetStringField(TEXT("note"), TEXT("No transition matched; nothing was removed."));
		return MCPResult(Noop);
	}

	TArray<TSharedPtr<FJsonValue>> Removed;
	int32 SharedRuleGraphsKept = 0;
	FString FirstFrom, FirstTo;
	for (UAnimStateTransitionNode* T : Matches)
	{
		TSharedPtr<FJsonObject> Described = MCPAnimDepthDescribeTransition(T);
		if (FirstFrom.IsEmpty()) Described->TryGetStringField(TEXT("fromState"), FirstFrom);
		if (FirstTo.IsEmpty()) Described->TryGetStringField(TEXT("toState"), FirstTo);
		Removed.Add(MakeShared<FJsonValueObject>(Described));

		bool bShared = false;
		MCPAnimDepthRemoveStateLikeNode(AnimBP, SMGraph, T, bShared);
		if (bShared) SharedRuleGraphsKept++;
	}

	MCPAnimDepthCompileAndSave(AnimBP);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("alreadyDeleted"), false);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("stateMachineName"), SMName);
	Result->SetArrayField(TEXT("removedTransitions"), Removed);
	Result->SetNumberField(TEXT("removedTransitionCount"), Removed.Num());
	Result->SetNumberField(TEXT("sharedRuleGraphsKept"), SharedRuleGraphsKept);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("stateMachineName"), SMName);
	Payload->SetStringField(TEXT("fromState"), FirstFrom);
	Payload->SetStringField(TEXT("toState"), FirstTo);
	MCPSetRollback(Result, TEXT("add_transition"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("add_transition restores the link with a fresh, empty rule graph and a new GUID. The transition's 'can enter transition' condition, ")
		TEXT("its blend duration and its priority are not restored, and a from/to pair that matched several transitions restores only one."));
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// remove_state_machine
// ─────────────────────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FAnimationHandlers::RemoveStateMachine(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString SMName;
	if (auto Err = RequireString(Params, TEXT("stateMachineName"), SMName)) return Err;

	UAnimBlueprint* AnimBP = MCPAnimDepthLoadAnimBP(AssetPath);
	if (!AnimBP)
	{
		return MCPError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));
	}

	UAnimGraphNode_StateMachine* SMNode = MCPAnimDepthFindStateMachine(AnimBP, SMName);
	if (!SMNode)
	{
		auto Noop = MCPSuccess();
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		Noop->SetStringField(TEXT("assetPath"), AssetPath);
		Noop->SetStringField(TEXT("stateMachineName"), SMName);
		Noop->SetStringField(TEXT("note"), TEXT("No state machine by that name; nothing was removed."));
		return MCPResult(Noop);
	}

	UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
	UEdGraph* OwningGraph = SMNode->GetGraph();
	if (!OwningGraph)
	{
		return MCPError(TEXT("The state machine node is not in a graph, so it cannot be removed."));
	}

	// The states and transitions own graphs of their own; leaving those behind
	// is what makes the next compile assert.
	int32 RemovedStates = 0;
	int32 RemovedTransitions = 0;
	int32 SharedRuleGraphsKept = 0;
	FString GraphName;
	if (SMGraph)
	{
		GraphName = SMGraph->GetName();

		TArray<UAnimStateNodeBase*> Inner;
		for (UEdGraphNode* Node : SMGraph->Nodes)
		{
			if (UAnimStateNodeBase* StateLike = Cast<UAnimStateNodeBase>(Node)) Inner.Add(StateLike);
		}
		// Transitions first so a state removal does not walk a list that a
		// transition removal already invalidated.
		for (UAnimStateNodeBase* Node : Inner)
		{
			if (!Node->IsA<UAnimStateTransitionNode>()) continue;
			bool bShared = false;
			MCPAnimDepthRemoveStateLikeNode(AnimBP, SMGraph, Node, bShared);
			if (bShared) SharedRuleGraphsKept++;
			RemovedTransitions++;
		}
		for (UAnimStateNodeBase* Node : Inner)
		{
			if (Node->IsA<UAnimStateTransitionNode>()) continue;
			bool bShared = false;
			MCPAnimDepthRemoveStateLikeNode(AnimBP, SMGraph, Node, bShared);
			RemovedStates++;
		}
	}

	SMNode->BreakAllNodeLinks();
	SMNode->EditorStateMachineGraph = nullptr;
	OwningGraph->RemoveNode(SMNode);
	if (SMGraph)
	{
		FBlueprintEditorUtils::RemoveGraph(AnimBP, SMGraph, EGraphRemoveFlags::MarkTransient);
	}

	MCPAnimDepthCompileAndSave(AnimBP);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("alreadyDeleted"), false);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("stateMachineName"), SMName);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetNumberField(TEXT("removedStates"), RemovedStates);
	Result->SetNumberField(TEXT("removedTransitions"), RemovedTransitions);
	Result->SetNumberField(TEXT("sharedRuleGraphsKept"), SharedRuleGraphsKept);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("name"), SMName);
	MCPSetRollback(Result, TEXT("create_state_machine"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"),
		FString::Printf(TEXT("create_state_machine restores an EMPTY machine. The %d state(s) and %d transition(s) inside it, and the AnimGraph pin ")
			TEXT("the machine was wired to, are not restored."), RemovedStates, RemovedTransitions));
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// remove_montage_section
// ─────────────────────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FAnimationHandlers::RemoveMontageSection(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString SectionName;
	if (auto Err = RequireString(Params, TEXT("sectionName"), SectionName)) return Err;

	UAnimMontage* Montage = LoadAssetByPath<UAnimMontage>(AssetPath);
	if (!Montage)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimMontage at '%s'"), *AssetPath));
	}

#if WITH_EDITOR
	const int32 Index = Montage->GetSectionIndex(FName(*SectionName));
	if (Index == INDEX_NONE)
	{
		TArray<FString> Known;
		for (const FCompositeSection& S : Montage->CompositeSections) Known.Add(S.SectionName.ToString());

		auto Noop = MCPSuccess();
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		Noop->SetStringField(TEXT("assetPath"), AssetPath);
		Noop->SetStringField(TEXT("sectionName"), SectionName);
		Noop->SetStringField(TEXT("note"), FString::Printf(
			TEXT("No section named '%s'. Sections on this montage: %s"),
			*SectionName,
			Known.Num() > 0 ? *FString::Join(Known, TEXT(", ")) : TEXT("(none)")));
		Noop->SetNumberField(TEXT("totalSections"), Montage->CompositeSections.Num());
		return MCPResult(Noop);
	}

	// Capture the section before it goes, because it is the rollback payload.
	const float StartTime = Montage->CompositeSections[Index].GetTime();
	const FName NextSection = Montage->CompositeSections[Index].NextSectionName;

	if (!Montage->DeleteAnimCompositeSection(Index))
	{
		return MCPError(FString::Printf(
			TEXT("UAnimMontage::DeleteAnimCompositeSection refused to remove section '%s' (index %d)."),
			*SectionName, Index));
	}

	// A section that pointed at the removed one now names a section that does
	// not exist, and a montage jumping to a missing section stops dead. Clearing
	// them is the honest fix, and the caller is told which ones changed so it
	// can re-point them.
	TArray<TSharedPtr<FJsonValue>> ClearedLinks;
	const FName RemovedName(*SectionName);
	for (FCompositeSection& S : Montage->CompositeSections)
	{
		if (S.NextSectionName == RemovedName)
		{
			S.NextSectionName = NAME_None;
			ClearedLinks.Add(MakeShared<FJsonValueString>(S.SectionName.ToString()));
		}
	}

	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("alreadyDeleted"), false);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("sectionName"), SectionName);
	Result->SetNumberField(TEXT("startTime"), StartTime);
	Result->SetStringField(TEXT("linkedSection"), NextSection.ToString());
	Result->SetArrayField(TEXT("clearedNextLinks"), ClearedLinks);
	Result->SetNumberField(TEXT("totalSections"), Montage->CompositeSections.Num());

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("sectionName"), SectionName);
	Payload->SetNumberField(TEXT("startTime"), StartTime);
	if (NextSection != NAME_None)
	{
		Payload->SetStringField(TEXT("linkedSection"), NextSection.ToString());
	}
	MCPSetRollback(Result, TEXT("add_montage_section"), Payload);
	if (ClearedLinks.Num() > 0)
	{
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("add_montage_section restores the section itself, but not the %d next-section link(s) cleared on other sections. ")
			TEXT("Re-point those with asset(set_property) on CompositeSections[i].NextSectionName."), ClearedLinks.Num()));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackLossy"), false);
	}
	return MCPResult(Result);
#else
	return MCPError(TEXT("Montage section editing requires an editor build."));
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// remove_anim_curve
// ─────────────────────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FAnimationHandlers::RemoveAnimCurve(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString CurveName;
	if (auto Err = RequireString(Params, TEXT("curveName"), CurveName)) return Err;

	UAnimSequence* AnimSeq = LoadAssetByPath<UAnimSequence>(AssetPath);
	if (!AnimSeq)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimSequence at '%s'"), *AssetPath));
	}

	// Count the keys before removing them, because that is what the caller
	// needs to know the rollback cannot restore.
	const FName CurveFName(*CurveName);
	int32 KeyCount = INDEX_NONE;
	bool bFound = false;
	TArray<FString> KnownCurves;
	for (const FFloatCurve& Curve : AnimSeq->GetCurveData().FloatCurves)
	{
		KnownCurves.Add(Curve.GetName().ToString());
		if (Curve.GetName() == CurveFName)
		{
			bFound = true;
			KeyCount = Curve.FloatCurve.GetNumKeys();
		}
	}

	if (!bFound)
	{
		auto Noop = MCPSuccess();
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		Noop->SetStringField(TEXT("assetPath"), AssetPath);
		Noop->SetStringField(TEXT("curveName"), CurveName);
		Noop->SetStringField(TEXT("note"), FString::Printf(
			TEXT("No float curve named '%s'. Float curves on this sequence: %s"),
			*CurveName,
			KnownCurves.Num() > 0 ? *FString::Join(KnownCurves, TEXT(", ")) : TEXT("(none)")));
		return MCPResult(Noop);
	}

	const FAnimationCurveIdentifier CurveId(CurveFName, ERawCurveTrackTypes::RCT_Float);
	IAnimationDataController& Controller = AnimSeq->GetController();
	Controller.OpenBracket(NSLOCTEXT("MCP", "RemoveCurve", "MCP Remove Curve"));
	const bool bRemoved = Controller.RemoveCurve(CurveId);
	Controller.CloseBracket();

	if (!bRemoved)
	{
		return MCPError(FString::Printf(
			TEXT("The animation data controller refused to remove curve '%s' from '%s'."), *CurveName, *AssetPath));
	}

	AnimSeq->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("alreadyDeleted"), false);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("curveName"), CurveName);
	Result->SetNumberField(TEXT("removedKeyCount"), KeyCount);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("curveName"), CurveName);
	MCPSetRollback(Result, TEXT("add_curve"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), KeyCount > 0);
	if (KeyCount > 0)
	{
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("add_curve restores an EMPTY curve of the same name. The %d key(s) it held are not restored; replay them with animation(set_anim_curve_keys)."),
			KeyCount));
	}
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// add_notify_state / remove_notify_state
//
// A notify STATE spans a window rather than firing at an instant, and it is a
// different field on the same struct: FAnimNotifyEvent::NotifyStateClass with a
// Duration and an EndLink, where a point notify uses ::Notify alone. add_notify
// only ever writes the point form, so combo windows, hit windows and timed
// effects had no route through the bridge at all.
// ─────────────────────────────────────────────────────────────────────────────

/** Every notify STATE on the asset, reported the way add_notify_state takes them. */
static TArray<TSharedPtr<FJsonValue>> MCPAnimDepthListNotifyStates(UAnimSequenceBase* Asset)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	if (!Asset) return Out;
	for (const FAnimNotifyEvent& Event : Asset->Notifies)
	{
		if (!Event.NotifyStateClass) continue;
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("notifyName"), Event.NotifyName.ToString());
		O->SetStringField(TEXT("notifyStateClass"), Event.NotifyStateClass->GetClass()->GetName());
		O->SetStringField(TEXT("objectPath"), Event.NotifyStateClass->GetPathName());
		O->SetNumberField(TEXT("triggerTime"), Event.GetTriggerTime());
		O->SetNumberField(TEXT("duration"), Event.GetDuration());
		O->SetNumberField(TEXT("endTime"), Event.GetEndTriggerTime());
		Out.Add(MakeShared<FJsonValueObject>(O));
	}
	return Out;
}

TSharedPtr<FJsonValue> FAnimationHandlers::AddNotifyState(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString NotifyName;
	if (auto Err = RequireString(Params, TEXT("notifyName"), NotifyName)) return Err;

	FString StateClassName;
	if (auto Err = RequireString(Params, TEXT("notifyStateClass"), StateClassName)) return Err;

	double TriggerTime = 0.0;
	if (!Params->TryGetNumberField(TEXT("triggerTime"), TriggerTime))
	{
		return MCPError(TEXT("Missing required parameter 'triggerTime' (seconds from the start of the animation)"));
	}
	double Duration = 0.0;
	if (!Params->TryGetNumberField(TEXT("duration"), Duration))
	{
		return MCPError(TEXT("Missing required parameter 'duration' (seconds; a notify state spans a window, which is what distinguishes it from add_notify)"));
	}
	if (Duration <= 0.0)
	{
		return MCPError(FString::Printf(
			TEXT("'duration' must be greater than 0 (got %.4f). A zero-length notify state never fires NotifyEnd; use animation(add_notify) for an instant notify."),
			Duration));
	}

	UAnimSequenceBase* AnimAsset = LoadAssetByPath<UAnimSequenceBase>(AssetPath);
	if (!AnimAsset)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimSequenceBase at '%s'"), *AssetPath));
	}

	// Resolve the class before anything is written. The same three spellings
	// add_notify accepts, plus the AnimNotifyState_ prefix its own family uses.
	UClass* StateClass = MCPResolveClassOfType(StateClassName, UAnimNotifyState::StaticClass());
	if (!StateClass)
	{
		StateClass = MCPResolveClassOfType(FString(TEXT("AnimNotifyState_")) + StateClassName, UAnimNotifyState::StaticClass());
	}
	if (!StateClass)
	{
		return MCPError(FString::Printf(
			TEXT("'%s' did not resolve to a UAnimNotifyState subclass. Pass a class name ('AnimNotifyState_TimedParticleEffect'), ")
			TEXT("a bare suffix ('TimedParticleEffect'), or a full /Script or Blueprint generated-class path. ")
			TEXT("Use reflection(list_classes, baseClass='AnimNotifyState') to see what this project has."),
			*StateClassName));
	}
	if (StateClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return MCPError(FString::Printf(TEXT("'%s' is abstract and cannot be instantiated."), *StateClass->GetName()));
	}

	const float PlayLength = AnimAsset->GetPlayLength();
	const float ClampedStart = FMath::Clamp(static_cast<float>(TriggerTime), 0.0f, PlayLength);
	const float ClampedDuration = FMath::Clamp(static_cast<float>(Duration), KINDA_SMALL_NUMBER, PlayLength - ClampedStart);
	if (ClampedDuration <= KINDA_SMALL_NUMBER)
	{
		return MCPError(FString::Printf(
			TEXT("triggerTime %.4f leaves no room for a window inside a %.4f second animation."),
			ClampedStart, PlayLength));
	}

	// Idempotency: same name, same class, same window is a replay.
	const FName NotifyFName(*NotifyName);
	for (const FAnimNotifyEvent& Existing : AnimAsset->Notifies)
	{
		if (!Existing.NotifyStateClass) continue;
		if (Existing.NotifyName != NotifyFName) continue;
		if (Existing.NotifyStateClass->GetClass() != StateClass) continue;
		if (!FMath::IsNearlyEqual(Existing.GetTime(), ClampedStart, 0.001f)) continue;
		if (!FMath::IsNearlyEqual(Existing.GetDuration(), ClampedDuration, 0.001f)) continue;

		auto Existed = MCPSuccess();
		MCPSetExisted(Existed);
		Existed->SetStringField(TEXT("assetPath"), AssetPath);
		Existed->SetStringField(TEXT("notifyName"), NotifyName);
		Existed->SetStringField(TEXT("notifyStateClass"), StateClass->GetName());
		Existed->SetNumberField(TEXT("triggerTime"), ClampedStart);
		Existed->SetNumberField(TEXT("duration"), ClampedDuration);
		Existed->SetArrayField(TEXT("notifyStates"), MCPAnimDepthListNotifyStates(AnimAsset));
		TSharedPtr<FJsonObject> ExistedPayload = MakeShared<FJsonObject>();
		ExistedPayload->SetStringField(TEXT("assetPath"), AssetPath);
		ExistedPayload->SetStringField(TEXT("notifyName"), NotifyName);
		ExistedPayload->SetStringField(TEXT("notifyStateClass"), StateClass->GetName());
		MCPSetRollback(Existed, TEXT("remove_anim_notify_state"), ExistedPayload);
		return MCPResult(Existed);
	}

	// Build and configure the state object BEFORE touching the notify array, so
	// a bad property name cannot leave a half-configured notify behind.
	UAnimNotifyState* StateObject = NewObject<UAnimNotifyState>(AnimAsset, StateClass);
	const TSharedPtr<FJsonObject>* NotifyProperties = nullptr;
	if (Params->TryGetObjectField(TEXT("notifyProperties"), NotifyProperties)
		&& NotifyProperties && (*NotifyProperties).IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*NotifyProperties)->Values)
		{
			FProperty* Property = StateClass->FindPropertyByName(FName(*Entry.Key));
			if (!Property)
			{
				TArray<FString> Valid;
				for (TFieldIterator<FProperty> It(StateClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
				{
					if (It->HasAnyPropertyFlags(CPF_Edit)) Valid.Add(It->GetName());
				}
				return MCPError(FString::Printf(
					TEXT("Notify state class '%s' has no property '%s'. Editable properties: %s"),
					*StateClass->GetName(), *Entry.Key,
					Valid.Num() > 0 ? *FString::Join(Valid, TEXT(", ")) : TEXT("(none)")));
			}
			FString PropertyError;
			if (!MCPJsonProperty::SetJsonOnProperty(
				Property,
				Property->ContainerPtrToValuePtr<void>(StateObject),
				Entry.Value,
				PropertyError))
			{
				return MCPError(FString::Printf(
					TEXT("Failed to set notify state property '%s' on '%s': %s"),
					*Entry.Key, *StateClass->GetName(), *PropertyError));
			}
		}
	}

	FAnimNotifyEvent& NewEvent = AnimAsset->Notifies.AddDefaulted_GetRef();
	NewEvent.NotifyName = NotifyFName;
	NewEvent.NotifyStateClass = StateObject;
	NewEvent.TrackIndex = 0;
	// Link both ends to the asset before SetDuration, which moves EndLink by
	// setting a time on it: an unlinked EndLink has no sequence to resolve
	// against and the window collapses to zero.
	NewEvent.Link(AnimAsset, ClampedStart);
	NewEvent.EndLink.Link(AnimAsset, ClampedStart + ClampedDuration);
	NewEvent.SetDuration(ClampedDuration);
	NewEvent.TriggerTimeOffset = GetTriggerTimeOffsetForType(AnimAsset->CalculateOffsetForNotify(ClampedStart));
	NewEvent.EndTriggerTimeOffset = GetTriggerTimeOffsetForType(AnimAsset->CalculateOffsetForNotify(ClampedStart + ClampedDuration));

	// Montage-only: a queued notify state ticks on the animation thread, a
	// branching point ticks inline and is the only form montage branching logic
	// observes. Left at the engine default unless asked, matching add_notify.
	bool bBranchingPoint = false;
	if (AnimAsset->IsA<UAnimMontage>() && Params->TryGetBoolField(TEXT("branchingPoint"), bBranchingPoint) && bBranchingPoint)
	{
		NewEvent.MontageTickType = EMontageNotifyTickType::BranchingPoint;
	}

	AnimAsset->SortNotifies();
	AnimAsset->RefreshCacheData();
	AnimAsset->PostEditChange();
	AnimAsset->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("notifyName"), NotifyName);
	Result->SetStringField(TEXT("notifyStateClass"), StateClass->GetName());
	Result->SetStringField(TEXT("objectPath"), StateObject->GetPathName());
	Result->SetNumberField(TEXT("triggerTime"), ClampedStart);
	Result->SetNumberField(TEXT("duration"), ClampedDuration);
	Result->SetNumberField(TEXT("endTime"), ClampedStart + ClampedDuration);
	Result->SetBoolField(TEXT("branchingPoint"), bBranchingPoint);
	Result->SetArrayField(TEXT("notifyStates"), MCPAnimDepthListNotifyStates(AnimAsset));
	Result->SetStringField(TEXT("note"),
		TEXT("Further fields on the spawned state object are plain UPROPERTYs: write them with editor(set_property) at the returned objectPath."));

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("notifyName"), NotifyName);
	Payload->SetStringField(TEXT("notifyStateClass"), StateClass->GetName());
	MCPSetRollback(Result, TEXT("remove_anim_notify_state"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAnimationHandlers::RemoveNotifyState(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	const FString NotifyName = OptionalString(Params, TEXT("notifyName"));
	const FString StateClassName = OptionalString(Params, TEXT("notifyStateClass"));
	if (NotifyName.IsEmpty() && StateClassName.IsEmpty())
	{
		return MCPError(TEXT("Pass at least one of 'notifyName' or 'notifyStateClass'. Both filters apply together, and removing every notify state on an asset unasked would be a worse default than refusing."));
	}

	UAnimSequenceBase* AnimAsset = LoadAssetByPath<UAnimSequenceBase>(AssetPath);
	if (!AnimAsset)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimSequenceBase at '%s'"), *AssetPath));
	}

	UClass* MatchClass = nullptr;
	if (!StateClassName.IsEmpty())
	{
		MatchClass = MCPResolveClassOfType(StateClassName, UAnimNotifyState::StaticClass());
		if (!MatchClass)
		{
			MatchClass = MCPResolveClassOfType(FString(TEXT("AnimNotifyState_")) + StateClassName, UAnimNotifyState::StaticClass());
		}
		if (!MatchClass)
		{
			TArray<FString> Present;
			for (const FAnimNotifyEvent& E : AnimAsset->Notifies)
			{
				if (E.NotifyStateClass) Present.AddUnique(E.NotifyStateClass->GetClass()->GetName());
			}
			return MCPError(FString::Printf(
				TEXT("'%s' did not resolve to a UAnimNotifyState subclass. Notify state classes present on this asset: %s"),
				*StateClassName,
				Present.Num() > 0 ? *FString::Join(Present, TEXT(", ")) : TEXT("(none)")));
		}
	}

	const FName NotifyFName(*NotifyName);
	TArray<TSharedPtr<FJsonValue>> Removed;
	for (int32 i = AnimAsset->Notifies.Num() - 1; i >= 0; --i)
	{
		const FAnimNotifyEvent& E = AnimAsset->Notifies[i];
		// This is the filter remove_anim_notify cannot express: it only ever
		// inspects FAnimNotifyEvent::Notify, so a notify state is invisible to it.
		if (!E.NotifyStateClass) continue;
		if (!NotifyName.IsEmpty() && E.NotifyName != NotifyFName) continue;
		if (MatchClass && !E.NotifyStateClass->GetClass()->IsChildOf(MatchClass)) continue;

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("notifyName"), E.NotifyName.ToString());
		O->SetStringField(TEXT("notifyStateClass"), E.NotifyStateClass->GetClass()->GetName());
		O->SetNumberField(TEXT("triggerTime"), E.GetTriggerTime());
		O->SetNumberField(TEXT("duration"), E.GetDuration());
		Removed.Add(MakeShared<FJsonValueObject>(O));
		AnimAsset->Notifies.RemoveAt(i);
	}

	if (Removed.Num() == 0)
	{
		auto Noop = MCPSuccess();
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		Noop->SetStringField(TEXT("assetPath"), AssetPath);
		Noop->SetStringField(TEXT("notifyName"), NotifyName);
		Noop->SetStringField(TEXT("notifyStateClass"), StateClassName);
		Noop->SetArrayField(TEXT("notifyStates"), MCPAnimDepthListNotifyStates(AnimAsset));
		Noop->SetStringField(TEXT("note"), TEXT("No notify state matched; nothing was removed. Point notifies are removed with animation(remove_notify)."));
		return MCPResult(Noop);
	}

	AnimAsset->SortNotifies();
	AnimAsset->RefreshCacheData();
	AnimAsset->PostEditChange();
	AnimAsset->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("alreadyDeleted"), false);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetArrayField(TEXT("removed"), Removed);
	Result->SetNumberField(TEXT("removedCount"), Removed.Num());
	Result->SetArrayField(TEXT("notifyStates"), MCPAnimDepthListNotifyStates(AnimAsset));

	// The inverse restores the first one removed, with default property values.
	const TSharedPtr<FJsonObject> First = Removed[0]->AsObject();
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("notifyName"), First->GetStringField(TEXT("notifyName")));
	Payload->SetStringField(TEXT("notifyStateClass"), First->GetStringField(TEXT("notifyStateClass")));
	Payload->SetNumberField(TEXT("triggerTime"), First->GetNumberField(TEXT("triggerTime")));
	Payload->SetNumberField(TEXT("duration"), First->GetNumberField(TEXT("duration")));
	MCPSetRollback(Result, TEXT("add_anim_notify_state"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
		TEXT("add_notify_state restores ONE notify state (the first of the %d removed) with its class defaults. ")
		TEXT("Properties written onto the removed state objects are not restored, and the rest of the batch is listed in 'removed' for replay."),
		Removed.Num()));
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_sync_markers
//
// AuthoredSyncMarkers is a bare UPROPERTY, so a raw property write reaches the
// array. It does not work: the runtime reads UniqueMarkerNames and the per-
// marker index built by RefreshSyncMarkerDataFromAuthored, and the animation
// editor only offers a marker name that the skeleton has seen before, which is
// USkeleton::ExistingMarkerNames. Writing the array alone leaves an animation
// whose markers exist on disk and are invisible to sync groups. This is an
// apply / refresh / register / read-back action for that reason, not a setter.
// ─────────────────────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FAnimationHandlers::SetSyncMarkers(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UAnimSequence* AnimSeq = LoadAssetByPath<UAnimSequence>(AssetPath);
	if (!AnimSeq)
	{
		return MCPError(FString::Printf(
			TEXT("Failed to load AnimSequence at '%s'. Sync markers live on UAnimSequence; a montage or composite has none."), *AssetPath));
	}

	const FString Mode = OptionalString(Params, TEXT("markerMode"), TEXT("replace")).ToLower();
	if (Mode != TEXT("replace") && Mode != TEXT("merge"))
	{
		return MCPError(FString::Printf(
			TEXT("markerMode '%s' is not valid. Use 'replace' (default: the markers array becomes the whole list, and an empty array clears them) or 'merge' (add or move only the named markers)."),
			*Mode));
	}

	const float PlayLength = AnimSeq->GetPlayLength();

	// Validate the whole batch before mutating anything: half a marker set is
	// worse than none, because the ones that landed look authoritative.
	struct FParsedMarker { FName Name; float Time; };
	TArray<FParsedMarker> Parsed;
	const TArray<TSharedPtr<FJsonValue>>* MarkerArray = nullptr;
	const bool bHasMarkers = Params->TryGetArrayField(TEXT("markers"), MarkerArray) && MarkerArray;
	if (bHasMarkers)
	{
		int32 Index = 0;
		for (const TSharedPtr<FJsonValue>& Entry : *MarkerArray)
		{
			const TSharedPtr<FJsonObject> O = Entry.IsValid() ? Entry->AsObject() : nullptr;
			if (!O.IsValid())
			{
				return MCPError(FString::Printf(TEXT("markers[%d] is not an object. Each entry is {name, time}."), Index));
			}
			FString Name;
			if (!O->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
			{
				return MCPError(FString::Printf(TEXT("markers[%d] has no 'name'. Each entry is {name, time}."), Index));
			}
			double Time = 0.0;
			if (!O->TryGetNumberField(TEXT("time"), Time))
			{
				return MCPError(FString::Printf(TEXT("markers[%d] ('%s') has no 'time' in seconds."), Index, *Name));
			}
			if (Time < 0.0 || Time > PlayLength)
			{
				return MCPError(FString::Printf(
					TEXT("markers[%d] ('%s') is at %.4f s, outside this animation's 0 to %.4f s range."),
					Index, *Name, Time, PlayLength));
			}
			Parsed.Add({ FName(*Name), static_cast<float>(Time) });
			Index++;
		}
	}

	TArray<FString> RemoveNames;
	const TArray<TSharedPtr<FJsonValue>>* RemoveArray = nullptr;
	if (Params->TryGetArrayField(TEXT("removeMarkers"), RemoveArray) && RemoveArray)
	{
		for (const TSharedPtr<FJsonValue>& Entry : *RemoveArray)
		{
			FString Name;
			if (Entry.IsValid() && Entry->TryGetString(Name) && !Name.IsEmpty()) RemoveNames.AddUnique(Name);
		}
	}

	if (!bHasMarkers && RemoveNames.Num() == 0)
	{
		return MCPError(TEXT("Pass 'markers' (the marker set to author) and/or 'removeMarkers' (names to drop). An empty 'markers' array with markerMode='replace' is the way to clear every marker."));
	}

	// The previous state, which is both the idempotency comparison and an exact
	// rollback payload.
	TArray<TSharedPtr<FJsonValue>> Before;
	for (const FAnimSyncMarker& Marker : AnimSeq->AuthoredSyncMarkers)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), Marker.MarkerName.ToString());
		O->SetNumberField(TEXT("time"), Marker.Time);
		Before.Add(MakeShared<FJsonValueObject>(O));
	}

	TArray<FAnimSyncMarker> Next;
	if (Mode == TEXT("replace"))
	{
		for (const FParsedMarker& P : Parsed)
		{
			FAnimSyncMarker Marker;
			Marker.MarkerName = P.Name;
			Marker.Time = P.Time;
#if WITH_EDITORONLY_DATA
			Marker.TrackIndex = 0;
			Marker.Guid = FGuid::NewGuid();
#endif
			Next.Add(Marker);
		}
	}
	else
	{
		Next = AnimSeq->AuthoredSyncMarkers;
		for (const FParsedMarker& P : Parsed)
		{
			bool bMoved = false;
			for (FAnimSyncMarker& Marker : Next)
			{
				if (Marker.MarkerName == P.Name)
				{
					Marker.Time = P.Time;
					bMoved = true;
					break;
				}
			}
			if (!bMoved)
			{
				FAnimSyncMarker Marker;
				Marker.MarkerName = P.Name;
				Marker.Time = P.Time;
#if WITH_EDITORONLY_DATA
				Marker.TrackIndex = 0;
				Marker.Guid = FGuid::NewGuid();
#endif
				Next.Add(Marker);
			}
		}
	}

	for (const FString& Name : RemoveNames)
	{
		const FName Target(*Name);
		Next.RemoveAll([Target](const FAnimSyncMarker& Marker) { return Marker.MarkerName == Target; });
	}

	Next.Sort([](const FAnimSyncMarker& A, const FAnimSyncMarker& B) { return A.Time < B.Time; });

	// Idempotency: same names at the same times is a replay, not a write.
	bool bUnchanged = (Next.Num() == AnimSeq->AuthoredSyncMarkers.Num());
	if (bUnchanged)
	{
		for (int32 i = 0; i < Next.Num(); ++i)
		{
			if (Next[i].MarkerName != AnimSeq->AuthoredSyncMarkers[i].MarkerName
				|| !FMath::IsNearlyEqual(Next[i].Time, AnimSeq->AuthoredSyncMarkers[i].Time, 0.0001f))
			{
				bUnchanged = false;
				break;
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> After;
	TArray<TSharedPtr<FJsonValue>> RegisteredOnSkeleton;

	if (!bUnchanged)
	{
		AnimSeq->AuthoredSyncMarkers = Next;

		// The names the skeleton has seen. Without this the markers are on the
		// sequence and the editor's marker picker cannot offer them, which reads
		// as "the write did not work".
		USkeleton* Skeleton = AnimSeq->GetSkeleton();
#if WITH_EDITOR
		if (Skeleton)
		{
			for (const FAnimSyncMarker& Marker : Next)
			{
				if (!Skeleton->GetExistingMarkerNames().Contains(Marker.MarkerName))
				{
					Skeleton->RegisterMarkerName(Marker.MarkerName);
					RegisteredOnSkeleton.Add(MakeShared<FJsonValueString>(Marker.MarkerName.ToString()));
				}
			}
			if (RegisteredOnSkeleton.Num() > 0)
			{
				Skeleton->MarkPackageDirty();
				UEditorAssetLibrary::SaveLoadedAsset(Skeleton, /*bOnlyIfIsDirty*/ true);
			}
		}
#endif

		// This is what turns the array into something the sync groups read.
		AnimSeq->RefreshSyncMarkerDataFromAuthored();
		AnimSeq->PostEditChange();
		AnimSeq->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(AssetPath);
	}

	// Read back what is actually on the asset, rather than what was requested.
	for (const FAnimSyncMarker& Marker : AnimSeq->AuthoredSyncMarkers)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), Marker.MarkerName.ToString());
		O->SetNumberField(TEXT("time"), Marker.Time);
		After.Add(MakeShared<FJsonValueObject>(O));
	}

	TArray<TSharedPtr<FJsonValue>> UniqueNames;
	for (const FName& Name : AnimSeq->UniqueMarkerNames)
	{
		UniqueNames.Add(MakeShared<FJsonValueString>(Name.ToString()));
	}

	auto Result = MCPSuccess();
	if (bUnchanged)
	{
		MCPSetExisted(Result);
	}
	else
	{
		MCPSetUpdated(Result);
	}
	Result->SetBoolField(TEXT("unchanged"), bUnchanged);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("markerMode"), Mode);
	Result->SetArrayField(TEXT("markers"), After);
	Result->SetNumberField(TEXT("markerCount"), After.Num());
	Result->SetArrayField(TEXT("uniqueMarkerNames"), UniqueNames);
	Result->SetArrayField(TEXT("registeredOnSkeleton"), RegisteredOnSkeleton);
	if (After.Num() > 0 && UniqueNames.Num() == 0)
	{
		Result->SetStringField(TEXT("warning"),
			TEXT("Markers were written but UniqueMarkerNames is empty, so the sequence will not participate in marker-based sync. This means the refresh did not take."));
	}

	// Exact inverse: replay the previous list wholesale.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("markerMode"), TEXT("replace"));
	Payload->SetArrayField(TEXT("markers"), Before);
	MCPSetRollback(Result, TEXT("set_sync_markers"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), false);
	return MCPResult(Result);
}
