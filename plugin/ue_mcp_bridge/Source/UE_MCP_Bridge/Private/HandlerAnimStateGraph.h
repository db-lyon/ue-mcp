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
}
