// Shared teardown for anim state machine nodes.
//
// animation(remove_state) and blueprint(delete_node) must remove a state the
// same way: every transition touching it, then the state, each with its bound
// graph. A transition whose endpoint is gone, or a bound graph with no owning
// node, fails the next compile. The module is a unity build, so this lives in
// a header rather than being copied into both translation units.
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "AnimationStateMachineGraph.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimStateNode.h"
#include "HandlerUtils.h"

namespace MCPAnimStateGraph
{
	/**
	 * Remove one state-like node and the graph it owns: break the links, detach
	 * the bound graph, drop the node, then unregister the graph through
	 * FBlueprintEditorUtils. A transition may share its rule graph with a
	 * sibling, so a shared graph is kept and the caller is told.
	 */
	inline void RemoveStateLikeNode(
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

	/** A transition described the way add_transition / read_state_machine report it. */
	inline TSharedPtr<FJsonObject> DescribeTransition(UAnimStateTransitionNode* T)
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

	/** What RemoveStateWithTransitions took with it. */
	struct FStateTeardown
	{
		TArray<TSharedPtr<FJsonValue>> RemovedTransitions;
		int32 SharedRuleGraphsKept = 0;
		bool bWasEntryState = false;
	};

	/**
	 * Remove a state, conduit or alias with every transition that touches it.
	 * A transition node passed here is removed on its own. Does not compile.
	 */
	inline FStateTeardown RemoveStateWithTransitions(UBlueprint* BP, UEdGraph* OwningGraph, UAnimStateNodeBase* State)
	{
		FStateTeardown Out;
		if (!BP || !OwningGraph || !State) return Out;

		if (UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(OwningGraph))
		{
			if (SMGraph->EntryNode)
			{
				Out.bWasEntryState = SMGraph->EntryNode->GetOutputNode() == State;
			}
		}

		if (!State->IsA<UAnimStateTransitionNode>())
		{
			// A self transition is listed from both of its pins.
			TArray<UAnimStateTransitionNode*> Listed;
			State->GetTransitionList(Listed);
			TArray<UAnimStateTransitionNode*> Transitions;
			for (UAnimStateTransitionNode* T : Listed)
			{
				if (T) Transitions.AddUnique(T);
			}
			for (UAnimStateTransitionNode* T : Transitions)
			{
				Out.RemovedTransitions.Add(MakeShared<FJsonValueObject>(DescribeTransition(T)));
				bool bShared = false;
				RemoveStateLikeNode(BP, OwningGraph, T, bShared);
				if (bShared) Out.SharedRuleGraphsKept++;
			}
		}

		bool bStateGraphShared = false;
		RemoveStateLikeNode(BP, OwningGraph, State, bStateGraphShared);
		if (bStateGraphShared) Out.SharedRuleGraphsKept++;
		return Out;
	}

	/** The graph named Name anywhere in BP (AnimGraph, a state's inner graph). */
	inline UEdGraph* FindGraphByName(UBlueprint* BP, const FString& Name)
	{
		if (!BP) return nullptr;
		TArray<UEdGraph*> All;
		BP->GetAllGraphs(All);
		for (UEdGraph* G : All)
		{
			if (G && G->GetName() == Name) return G;
		}
		return nullptr;
	}

	/** The state machine container node whose graph is named MachineName, or
	 *  whose title contains it. */
	inline UAnimGraphNode_StateMachine* FindStateMachineNode(UBlueprint* BP, const FString& MachineName)
	{
		if (!BP) return nullptr;
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

	/** Every state machine name in BP, so a miss can name the hits. */
	inline TArray<FString> ListStateMachines(UBlueprint* BP)
	{
		TArray<FString> Names;
		if (!BP) return Names;
		TArray<UEdGraph*> All;
		BP->GetAllGraphs(All);
		for (UEdGraph* G : All)
		{
			if (!G) continue;
			for (UEdGraphNode* Node : G->Nodes)
			{
				UAnimGraphNode_StateMachine* SM = Cast<UAnimGraphNode_StateMachine>(Node);
				if (SM && SM->EditorStateMachineGraph)
				{
					Names.AddUnique(SM->EditorStateMachineGraph->GetName());
				}
			}
		}
		return Names;
	}

	/** The state named StateName in SMGraph. */
	inline UAnimStateNode* FindStateNode(UAnimationStateMachineGraph* SMGraph, const FString& StateName)
	{
		if (!SMGraph) return nullptr;
		for (UEdGraphNode* Node : SMGraph->Nodes)
		{
			UAnimStateNode* State = Cast<UAnimStateNode>(Node);
			if (State && State->GetStateName() == StateName) return State;
		}
		return nullptr;
	}

	/** Every state name in SMGraph. */
	inline TArray<FString> ListStates(UAnimationStateMachineGraph* SMGraph)
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

	/** Compile BP and save its package, recording saved/saveError on Result.
	 *  A save that did not reach disk fails the result. Returns whether it saved. */
	inline bool CompileAndSave(UBlueprint* BP, const TSharedPtr<FJsonObject>& Result, const FString& AssetPath)
	{
		FString SaveError;
		bool bSaved = false;
		if (BP)
		{
			FKismetEditorUtilities::CompileBlueprint(BP);
			bSaved = SaveAssetPackageChecked(BP, SaveError);
		}
		else
		{
			SaveError = TEXT("No blueprint to save.");
		}
		MCPNoteSaveOutcome(Result, AssetPath, bSaved, SaveError);
		return bSaved;
	}
}
