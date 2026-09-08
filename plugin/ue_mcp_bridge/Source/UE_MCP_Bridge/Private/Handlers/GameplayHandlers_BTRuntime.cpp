// BehaviorTree RUNTIME: what a tree is actually doing, and control over it.
//
// The authoring half of BehaviorTrees is complete (add_bt_node, move_bt_node,
// remove_bt_node, read_behavior_tree_graph, list_bt_graph_nodes). The runtime
// half was missing entirely: UBehaviorTreeComponent and UBlackboardComponent
// were included by the gameplay handlers and never referenced once. So an agent
// could author a tree, hand it to a pawn, and have no way to ask whether it ran
// at all. The reasonable next step from there is to report success, because
// every call the agent made succeeded, and the tree that never ticked is
// invisible.
//
// These six actions close that: which node is executing and under which
// ancestors, what the live blackboard holds, a typed blackboard write that
// actually notifies observers, start, stop, and a way to discover the agents in
// the first place.
//
// Translation-unit partition of FGameplayHandlers; registration lives in
// GameplayHandlers.cpp.

#include "GameplayHandlers.h"

#include "HandlerUtils.h"

#include "AIController.h"
#include "BrainComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BTAuxiliaryNode.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/Blackboard/BlackboardKeyAllTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "UObject/Class.h"

namespace
{
	// How deep an aux-node walk descends before it stops. A BehaviorTree is a
	// tree, but a corrupt one is still an asset a live component may be holding,
	// and a handler has to answer rather than recurse forever.
	constexpr int32 MCPBTLiveMaxDepth = 64;

	// Longest ancestor chain reported off the active node. A tree deeper than
	// this is reporting a cycle, not a hierarchy.
	constexpr int32 MCPBTLiveMaxAncestors = 64;

	// How many agents list_ai_agents reports before it truncates.
	constexpr int32 MCPBTLiveDefaultAgentLimit = 200;

	/** "BlackboardKeyType_Object" reads as "Object" wherever a key type is
	 *  reported. The full class path travels alongside it. */
	FString MCPBTLiveKeyTypeShortName(const UClass* KeyTypeClass)
	{
		if (!KeyTypeClass) return FString();
		FString Name = KeyTypeClass->GetName();
		const FString Prefix = TEXT("BlackboardKeyType_");
		if (Name.StartsWith(Prefix)) Name = Name.RightChop(Prefix.Len());
		return Name;
	}

	/** The UEnum behind an enum-typed blackboard key, from either key type.
	 *
	 *  UBlackboardKeyType_NativeEnum is the deprecated spelling and derives from
	 *  UBlackboardKeyType directly rather than from UBlackboardKeyType_Enum, so
	 *  one cast does not cover both. Both keep the UEnum on their own CDO, which
	 *  is the object the key was configured on. */
	const UEnum* MCPBTLiveResolveKeyEnum(UClass* KeyTypeClass)
	{
		if (!KeyTypeClass) return nullptr;
		UObject* CDO = KeyTypeClass->GetDefaultObject();
		if (const UBlackboardKeyType_Enum* AsEnum = Cast<UBlackboardKeyType_Enum>(CDO))
		{
			return AsEnum->EnumType;
		}
		if (const UBlackboardKeyType_NativeEnum* AsNative = Cast<UBlackboardKeyType_NativeEnum>(CDO))
		{
			return AsNative->EnumType;
		}
		return nullptr;
	}

	/** Which BT family a node belongs to, as the caller's vocabulary. */
	const TCHAR* MCPBTLiveNodeKind(const UBTNode* Node)
	{
		if (!Node) return TEXT("none");
		if (Node->IsA<UBTTaskNode>()) return TEXT("task");
		if (Node->IsA<UBTDecorator>()) return TEXT("decorator");
		if (Node->IsA<UBTService>()) return TEXT("service");
		if (Node->IsA<UBTCompositeNode>()) return TEXT("composite");
		return TEXT("node");
	}

	/** EBTTaskStatus as a word. GetTaskStatus answers Inactive for a task that
	 *  is not the active one, which is a real answer rather than an error. */
	const TCHAR* MCPBTLiveTaskStatusName(EBTTaskStatus::Type Status)
	{
		switch (Status)
		{
			case EBTTaskStatus::Active:   return TEXT("active");
			case EBTTaskStatus::Aborting: return TEXT("aborting");
			case EBTTaskStatus::Inactive: return TEXT("inactive");
			default:                      return TEXT("unknown");
		}
	}

	/** One BT node's identity, as much of it as is readable without touching
	 *  the protected instance stack. */
	TSharedPtr<FJsonObject> MCPBTLiveDescribeNode(const UBTNode* Node)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		if (!Node) return Out;
		Out->SetStringField(TEXT("nodeName"), Node->GetNodeName());
		Out->SetStringField(TEXT("nodeClass"), Node->GetClass()->GetName());
		Out->SetStringField(TEXT("nodeClassPath"), Node->GetClass()->GetPathName());
		Out->SetStringField(TEXT("kind"), MCPBTLiveNodeKind(Node));
		Out->SetNumberField(TEXT("executionIndex"), Node->GetExecutionIndex());
		Out->SetNumberField(TEXT("treeDepth"), Node->GetTreeDepth());
		Out->SetBoolField(TEXT("isInstanced"), Node->IsInstanced());
		Out->SetBoolField(TEXT("isInjected"), Node->IsInjected());
		Out->SetStringField(TEXT("staticDescription"), Node->GetStaticDescription());
		if (const UBehaviorTree* Owner = Node->GetTreeAsset())
		{
			Out->SetStringField(TEXT("treeAsset"), Owner->GetPathName());
		}
		return Out;
	}

	/** Every decorator and service reachable from a tree asset, paired with
	 *  whether the live component currently has it registered as active.
	 *
	 *  The component's InstanceStack is protected, so the active aux set cannot
	 *  be read out of it directly. IsAuxNodeActive is the public question, and
	 *  the tree asset supplies the candidates to ask it about. */
	void MCPBTLiveCollectAuxNodes(
		const UBehaviorTreeComponent& Comp,
		UBTCompositeNode* Composite,
		const FString& Path,
		int32 Depth,
		TArray<TSharedPtr<FJsonValue>>& OutActive,
		int32& OutConsidered)
	{
		if (!Composite || Depth > MCPBTLiveMaxDepth) return;

		const auto Consider = [&Comp, &OutActive, &OutConsidered](
			UBTAuxiliaryNode* Aux, const FString& AuxPath)
		{
			if (!Aux) return;
			++OutConsidered;
			if (!Comp.IsAuxNodeActive(Aux)) return;
			TSharedPtr<FJsonObject> Row = MCPBTLiveDescribeNode(Aux);
			Row->SetStringField(TEXT("nodePath"), AuxPath);
			OutActive.Add(MakeShared<FJsonValueObject>(Row));
		};

		for (int32 Index = 0; Index < Composite->Services.Num(); ++Index)
		{
			Consider(Composite->Services[Index].Get(),
				FString::Printf(TEXT("%s.Services[%d]"), *Path, Index));
		}

		for (int32 ChildIndex = 0; ChildIndex < Composite->Children.Num(); ++ChildIndex)
		{
			const FBTCompositeChild& Child = Composite->Children[ChildIndex];
			const FString ChildPath = FString::Printf(TEXT("%s.Children[%d]"), *Path, ChildIndex);
			for (int32 DecIndex = 0; DecIndex < Child.Decorators.Num(); ++DecIndex)
			{
				Consider(Child.Decorators[DecIndex].Get(),
					FString::Printf(TEXT("%s.Decorators[%d]"), *ChildPath, DecIndex));
			}
			if (Child.ChildComposite)
			{
				MCPBTLiveCollectAuxNodes(Comp, Child.ChildComposite, ChildPath, Depth + 1, OutActive, OutConsidered);
			}
		}
	}

	/** The actors that could plausibly carry the brain for a selected actor:
	 *  the actor itself, the pawn's controller, and a controller's pawn.
	 *
	 *  A caller who selected the pawn in the outliner is asking about the AI on
	 *  it, and the BehaviorTreeComponent lives on the AIController, not the
	 *  pawn. Failing with "no BehaviorTreeComponent on BP_Enemy" when the tree
	 *  is plainly running is the same dead end this file exists to remove. */
	void MCPBTLiveBrainCandidates(AActor* Actor, TArray<AActor*>& Out)
	{
		Out.Reset();
		if (!IsValid(Actor)) return;
		Out.Add(Actor);
		if (APawn* Pawn = Cast<APawn>(Actor))
		{
			if (AController* Controller = Pawn->GetController())
			{
				Out.AddUnique(Controller);
			}
		}
		if (AController* AsController = Cast<AController>(Actor))
		{
			if (APawn* Pawn = AsController->GetPawn())
			{
				Out.AddUnique(Pawn);
			}
		}
	}

	/** Everything the runtime actions resolve before they diverge. */
	struct FMCPBTLiveTarget
	{
		UWorld* World = nullptr;
		/** The actor the caller named. */
		AActor* Actor = nullptr;
		/** The actor the brain component is actually attached to. */
		AActor* BrainOwner = nullptr;
		UBrainComponent* Brain = nullptr;
		/** Null when the brain is some other UBrainComponent subclass. */
		UBehaviorTreeComponent* BTComp = nullptr;
		UBlackboardComponent* Blackboard = nullptr;
		AAIController* AIController = nullptr;
	};

	/** Name every component on the candidate actors, so a miss says what was
	 *  searched rather than only that it failed. */
	FString MCPBTLiveDescribeComponents(const TArray<AActor*>& Candidates)
	{
		TArray<FString> Lines;
		for (AActor* Candidate : Candidates)
		{
			if (!IsValid(Candidate)) continue;
			TArray<FString> Names;
			for (UActorComponent* Comp : Candidate->GetComponents())
			{
				if (Comp) Names.Add(FString::Printf(TEXT("%s (%s)"), *Comp->GetName(), *Comp->GetClass()->GetName()));
			}
			Lines.Add(FString::Printf(
				TEXT("%s: [%s]"),
				*Candidate->GetActorLabel(),
				Names.Num() > 0 ? *FString::Join(Names, TEXT(", ")) : TEXT("no components")));
		}
		return Lines.Num() > 0 ? FString::Join(Lines, TEXT("; ")) : FString(TEXT("(no candidate actors)"));
	}

	/**
	 * Resolve world, actor, brain component and blackboard from the params.
	 *
	 * bRequireBehaviorTree decides whether a UBrainComponent that is not a
	 * UBehaviorTreeComponent is an error. list_ai_agents reports those; the
	 * BT-specific actions refuse, because a StateTree brain has no tree to
	 * start, stop or inspect and pretending otherwise would report success for
	 * a call that did nothing.
	 */
	bool MCPBTLiveResolveTarget(
		const TSharedPtr<FJsonObject>& Params,
		bool bRequireBehaviorTree,
		FMCPBTLiveTarget& Out,
		TSharedPtr<FJsonValue>& OutError)
	{
		// Default "auto": a running tree is almost always a PIE tree, and an
		// editor-world fallback still answers for an actor placed in the level.
		const FString Scope = OptionalString(Params, TEXT("world"), TEXT("auto"));
		Out.World = ResolveWorldFromParams(Params, *Scope);
		if (!Out.World)
		{
			OutError = MCPError(FString::Printf(
				TEXT("No world available for scope '%s'. A BehaviorTree only runs in a game world, so start Play-In-Editor first, or pass world=\"editor\" to inspect a placed actor. editor(list_pie_instances) lists the running PIE worlds."),
				*Scope));
			return false;
		}

		FMCPActorSelector Selector;
		Selector.Match = EMCPActorMatch::LabelNameOrPath;
		Selector.WorldLabel = Out.World->IsGameWorld() ? TEXT("PIE") : TEXT("editor");
		Out.Actor = MCPResolveActor(Out.World, Params, OutError, Selector);
		if (!Out.Actor) return false;

		TArray<AActor*> Candidates;
		MCPBTLiveBrainCandidates(Out.Actor, Candidates);

		for (AActor* Candidate : Candidates)
		{
			if (!IsValid(Candidate)) continue;
			if (AAIController* AsAI = Cast<AAIController>(Candidate))
			{
				Out.AIController = AsAI;
				if (UBrainComponent* FromController = AsAI->GetBrainComponent())
				{
					Out.Brain = FromController;
					Out.BrainOwner = Candidate;
					break;
				}
			}
			if (UBrainComponent* Found = Candidate->FindComponentByClass<UBrainComponent>())
			{
				Out.Brain = Found;
				Out.BrainOwner = Candidate;
				break;
			}
		}

		if (!Out.Brain)
		{
			OutError = MCPError(FString::Printf(
				TEXT("No BrainComponent on '%s' or its paired pawn/controller, so no BehaviorTree is attached to it. Searched these actors and their components: %s. gameplay(list_ai_agents) lists every actor in the %s world that does carry one; gameplay(run_behavior_tree) attaches a tree to an AIController that has none."),
				*Out.Actor->GetActorLabel(),
				*MCPBTLiveDescribeComponents(Candidates),
				Selector.WorldLabel));
			return false;
		}

		Out.BTComp = Cast<UBehaviorTreeComponent>(Out.Brain);
		if (bRequireBehaviorTree && !Out.BTComp)
		{
			OutError = MCPError(FString::Printf(
				TEXT("'%s' carries a %s, which is a BrainComponent but not a BehaviorTreeComponent, so it has no BehaviorTree to inspect or control. For a StateTree brain call gameplay(get_state_tree_runtime) instead."),
				*Out.BrainOwner->GetActorLabel(),
				*Out.Brain->GetClass()->GetName()));
			return false;
		}

		if (!Out.AIController)
		{
			Out.AIController = Cast<AAIController>(Out.BrainOwner);
		}

		Out.Blackboard = Out.Brain->GetBlackboardComponent();
		if (!Out.Blackboard && Out.AIController)
		{
			Out.Blackboard = Out.AIController->GetBlackboardComponent();
		}
		if (!Out.Blackboard && Out.BrainOwner)
		{
			Out.Blackboard = Out.BrainOwner->FindComponentByClass<UBlackboardComponent>();
		}
		return true;
	}

	/** Identity fields every runtime result carries, so a caller can prove which
	 *  object answered without a second call.
	 *
	 *  Every pointer is checked: run_behavior_tree reaches this with a target
	 *  whose brain the AIController was supposed to have just created, and a
	 *  controller that reported success while creating nothing would otherwise
	 *  crash the editor instead of reporting the odd state. */
	void MCPBTLiveWriteTargetFields(TSharedPtr<FJsonObject> Obj, const FMCPBTLiveTarget& Target)
	{
		if (Target.Actor)
		{
			Obj->SetStringField(TEXT("actorLabel"), Target.Actor->GetActorLabel());
			Obj->SetStringField(TEXT("actorPath"), Target.Actor->GetPathName());
		}
		if (Target.BrainOwner)
		{
			Obj->SetStringField(TEXT("brainOwnerLabel"), Target.BrainOwner->GetActorLabel());
			Obj->SetStringField(TEXT("brainOwnerPath"), Target.BrainOwner->GetPathName());
		}
		if (Target.Brain)
		{
			Obj->SetStringField(TEXT("brainComponent"), Target.Brain->GetName());
			Obj->SetStringField(TEXT("brainComponentClass"), Target.Brain->GetClass()->GetName());
		}
		else
		{
			Obj->SetBoolField(TEXT("brainComponentMissing"), true);
		}
		if (Target.World)
		{
			Obj->SetBoolField(TEXT("isPlayInEditor"), Target.World->IsPlayInEditor());
			Obj->SetStringField(TEXT("world"), Target.World->GetName());
		}
		if (Target.AIController)
		{
			Obj->SetStringField(TEXT("aiController"), Target.AIController->GetPathName());
		}
		if (Target.Blackboard)
		{
			Obj->SetStringField(TEXT("blackboardComponent"), Target.Blackboard->GetName());
			if (const UBlackboardData* Data = Target.Blackboard->GetBlackboardAsset())
			{
				Obj->SetStringField(TEXT("blackboardAsset"), Data->GetPathName());
			}
			Obj->SetNumberField(TEXT("blackboardKeyCount"), Target.Blackboard->GetNumKeys());
		}
	}

	/** The blackboard, or the error that says why there is not one. */
	UBlackboardComponent* MCPBTLiveRequireBlackboard(
		const FMCPBTLiveTarget& Target,
		TSharedPtr<FJsonValue>& OutError)
	{
		if (Target.Blackboard && Target.Blackboard->HasValidAsset()) return Target.Blackboard;
		if (Target.Blackboard)
		{
			OutError = MCPError(FString::Printf(
				TEXT("'%s' has a BlackboardComponent with no BlackboardData asset initialized, so it holds no keys. A tree started through gameplay(run_behavior_tree) initializes the blackboard from the tree's own BlackboardAsset; set one with gameplay(set_behavior_tree_blackboard) if the tree has none."),
				*Target.BrainOwner->GetActorLabel()));
			return nullptr;
		}
		OutError = MCPError(FString::Printf(
			TEXT("No BlackboardComponent reachable from '%s'. Searched the BrainComponent, the AIController and the actor's own components. AAIController::RunBehaviorTree creates one from the tree's BlackboardAsset, so call gameplay(run_behavior_tree) with a tree whose blackboard is set (gameplay(set_behavior_tree_blackboard))."),
			*Target.BrainOwner->GetActorLabel()));
		return nullptr;
	}

	/** Verbosity word to the engine's description mode. */
	bool MCPBTLiveParseVerbosity(const FString& Word, EBlackboardDescription::Type& Out)
	{
		if (Word == TEXT("onlyvalue"))    { Out = EBlackboardDescription::OnlyValue; return true; }
		if (Word == TEXT("keywithvalue")) { Out = EBlackboardDescription::KeyWithValue; return true; }
		if (Word == TEXT("detailed"))     { Out = EBlackboardDescription::DetailedKeyWithValue; return true; }
		if (Word == TEXT("full"))         { Out = EBlackboardDescription::Full; return true; }
		return false;
	}

	/** One blackboard key as JSON: identity, the engine's own value description,
	 *  and a typed value for the key types that have one.
	 *
	 *  DescribeKeyValue is the type-agnostic answer and is always present, which
	 *  is what makes a struct or a custom key type still readable. The typed
	 *  field is what a caller compares against a number it just wrote. */
	TSharedPtr<FJsonObject> MCPBTLiveDescribeKey(
		const UBlackboardComponent& BB,
		FBlackboard::FKey KeyID,
		EBlackboardDescription::Type Verbosity)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		const FName KeyName = BB.GetKeyName(KeyID);
		const TSubclassOf<UBlackboardKeyType> KeyType = BB.GetKeyType(KeyID);
		UClass* TypeClass = KeyType.Get();

		Row->SetNumberField(TEXT("keyId"), static_cast<int32>(KeyID));
		Row->SetStringField(TEXT("key"), KeyName.ToString());
		Row->SetStringField(TEXT("type"), MCPBTLiveKeyTypeShortName(TypeClass));
		if (TypeClass) Row->SetStringField(TEXT("typeClass"), TypeClass->GetPathName());
		Row->SetBoolField(TEXT("instanceSynced"), BB.IsKeyInstanceSynced(KeyID));
		Row->SetStringField(TEXT("value"), BB.DescribeKeyValue(KeyID, EBlackboardDescription::OnlyValue));
		Row->SetStringField(TEXT("described"), BB.DescribeKeyValue(KeyID, Verbosity));

		if (!TypeClass) return Row;

		if (TypeClass == UBlackboardKeyType_Bool::StaticClass())
		{
			Row->SetBoolField(TEXT("typedValue"), BB.GetValueAsBool(KeyName));
		}
		else if (TypeClass == UBlackboardKeyType_Int::StaticClass())
		{
			Row->SetNumberField(TEXT("typedValue"), BB.GetValueAsInt(KeyName));
		}
		else if (TypeClass == UBlackboardKeyType_Float::StaticClass())
		{
			Row->SetNumberField(TEXT("typedValue"), BB.GetValueAsFloat(KeyName));
		}
		else if (TypeClass == UBlackboardKeyType_String::StaticClass())
		{
			Row->SetStringField(TEXT("typedValue"), BB.GetValueAsString(KeyName));
		}
		else if (TypeClass == UBlackboardKeyType_Name::StaticClass())
		{
			Row->SetStringField(TEXT("typedValue"), BB.GetValueAsName(KeyName).ToString());
		}
		else if (TypeClass == UBlackboardKeyType_Vector::StaticClass())
		{
			// A vector key can be explicitly unset, which is not the same as
			// (0,0,0) and is what an "is the target known" check reads.
			Row->SetBoolField(TEXT("isSet"), BB.IsVectorValueSet(KeyName));
			const FVector Value = BB.GetValueAsVector(KeyName);
			TSharedPtr<FJsonObject> Vec = MakeShared<FJsonObject>();
			Vec->SetNumberField(TEXT("x"), Value.X);
			Vec->SetNumberField(TEXT("y"), Value.Y);
			Vec->SetNumberField(TEXT("z"), Value.Z);
			Row->SetObjectField(TEXT("typedValue"), Vec);
		}
		else if (TypeClass == UBlackboardKeyType_Rotator::StaticClass())
		{
			const FRotator Value = BB.GetValueAsRotator(KeyName);
			TSharedPtr<FJsonObject> Rot = MakeShared<FJsonObject>();
			Rot->SetNumberField(TEXT("pitch"), Value.Pitch);
			Rot->SetNumberField(TEXT("yaw"), Value.Yaw);
			Rot->SetNumberField(TEXT("roll"), Value.Roll);
			Row->SetObjectField(TEXT("typedValue"), Rot);
		}
		else if (TypeClass == UBlackboardKeyType_Object::StaticClass())
		{
			UObject* Value = BB.GetValueAsObject(KeyName);
			if (Value)
			{
				Row->SetStringField(TEXT("typedValue"), Value->GetPathName());
				Row->SetStringField(TEXT("objectClass"), Value->GetClass()->GetName());
				if (const AActor* AsActor = Cast<AActor>(Value))
				{
					Row->SetStringField(TEXT("objectActorLabel"), AsActor->GetActorLabel());
				}
			}
			else
			{
				Row->SetField(TEXT("typedValue"), MakeShared<FJsonValueNull>());
			}
		}
		else if (TypeClass == UBlackboardKeyType_Class::StaticClass())
		{
			UClass* Value = BB.GetValueAsClass(KeyName);
			if (Value) Row->SetStringField(TEXT("typedValue"), Value->GetPathName());
			else Row->SetField(TEXT("typedValue"), MakeShared<FJsonValueNull>());
		}
		else if (TypeClass == UBlackboardKeyType_Enum::StaticClass()
			|| TypeClass == UBlackboardKeyType_NativeEnum::StaticClass())
		{
			const uint8 Raw = BB.GetValueAsEnum(KeyName);
			Row->SetNumberField(TEXT("typedValue"), Raw);
			// The enumerator name is what a caller reads and writes back.
			if (const UEnum* Enum = MCPBTLiveResolveKeyEnum(TypeClass))
			{
				Row->SetStringField(TEXT("enumerator"), Enum->GetNameStringByValue(Raw));
				Row->SetStringField(TEXT("enumType"), Enum->GetPathName());
			}
		}
		return Row;
	}

	/** Resolve a JSON value to a UObject for an Object-typed key: an actor by
	 *  path, an asset by path, then an actor by label or internal name. */
	UObject* MCPBTLiveResolveObjectValue(
		UWorld* World,
		const FString& Spec,
		TSharedPtr<FJsonValue>& OutError)
	{
		if (AActor* ByPath = MCPFindActorByPath(World, Spec)) return ByPath;
		if (UObject* Asset = MCPLoadAssetObject(Spec)) return Asset;

		FMCPActorSelector Selector;
		Selector.LabelKey = TEXT("value");
		Selector.PathKey = TEXT("value");
		Selector.Match = EMCPActorMatch::LabelNameOrPath;
		Selector.WorldLabel = World && World->IsGameWorld() ? TEXT("PIE") : TEXT("editor");
		return MCPResolveActorToken(World, Spec, OutError, Selector);
	}

	/** A summary row for one AI agent, shared by list_ai_agents. */
	TSharedPtr<FJsonObject> MCPBTLiveDescribeAgent(
		AActor* BrainOwner,
		UBrainComponent* Brain)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("brainOwnerLabel"), BrainOwner->GetActorLabel());
		Row->SetStringField(TEXT("brainOwnerPath"), BrainOwner->GetPathName());
		Row->SetStringField(TEXT("brainOwnerClass"), BrainOwner->GetClass()->GetName());
		Row->SetStringField(TEXT("brainComponent"), Brain->GetName());
		Row->SetStringField(TEXT("brainComponentClass"), Brain->GetClass()->GetName());
		Row->SetBoolField(TEXT("running"), Brain->IsRunning());
		Row->SetBoolField(TEXT("paused"), Brain->IsPaused());

		if (AAIController* AsAI = Cast<AAIController>(BrainOwner))
		{
			if (APawn* Pawn = AsAI->GetPawn())
			{
				Row->SetStringField(TEXT("pawnLabel"), Pawn->GetActorLabel());
				Row->SetStringField(TEXT("pawnPath"), Pawn->GetPathName());
				Row->SetStringField(TEXT("pawnClass"), Pawn->GetClass()->GetName());
			}
		}

		UBehaviorTreeComponent* BTComp = Cast<UBehaviorTreeComponent>(Brain);
		Row->SetBoolField(TEXT("isBehaviorTree"), BTComp != nullptr);
		if (BTComp)
		{
			Row->SetBoolField(TEXT("treeHasBeenStarted"), BTComp->TreeHasBeenStarted());
			if (const UBehaviorTree* Current = BTComp->GetCurrentTree())
			{
				Row->SetStringField(TEXT("currentTree"), Current->GetPathName());
			}
			if (const UBehaviorTree* Root = BTComp->GetRootTree())
			{
				Row->SetStringField(TEXT("rootTree"), Root->GetPathName());
			}
			if (const UBTNode* Active = BTComp->GetActiveNode())
			{
				Row->SetStringField(TEXT("activeNode"), Active->GetNodeName());
				Row->SetStringField(TEXT("activeNodeClass"), Active->GetClass()->GetName());
			}
		}
		else
		{
			// A StateTree brain is a legitimate agent and belongs in the
			// listing, but naming the action that reads it saves the caller a
			// failed BT call.
			Row->SetStringField(TEXT("note"),
				TEXT("This brain is not a BehaviorTreeComponent. gameplay(get_bt_runtime) refuses it; read a StateTree brain with gameplay(get_state_tree_runtime)."));
		}

		if (const UBlackboardComponent* BB = Brain->GetBlackboardComponent())
		{
			if (const UBlackboardData* Data = BB->GetBlackboardAsset())
			{
				Row->SetStringField(TEXT("blackboardAsset"), Data->GetPathName());
			}
			Row->SetNumberField(TEXT("blackboardKeyCount"), BB->GetNumKeys());
		}
		return Row;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Reads
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FGameplayHandlers::GetBtRuntime(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FMCPBTLiveTarget Target;
	TSharedPtr<FJsonValue> Err;
	if (!MCPBTLiveResolveTarget(Params, /*bRequireBehaviorTree*/ true, Target, Err)) return Err;

	UBehaviorTreeComponent* Comp = Target.BTComp;

	auto Result = MCPSuccess();
	MCPBTLiveWriteTargetFields(Result, Target);

	const bool bRunning = Comp->IsRunning();
	Result->SetBoolField(TEXT("running"), bRunning);
	Result->SetBoolField(TEXT("paused"), Comp->IsPaused());
	Result->SetBoolField(TEXT("treeHasBeenStarted"), Comp->TreeHasBeenStarted());
	Result->SetBoolField(TEXT("instanceStackEmpty"), Comp->IsInstanceStackEmpty());
	Result->SetBoolField(TEXT("restartPending"), Comp->IsRestartPending());
	Result->SetBoolField(TEXT("abortPending"), Comp->IsAbortPending());
	Result->SetNumberField(TEXT("activeInstanceIdx"), Comp->GetActiveInstanceIdx());
	Result->SetNumberField(TEXT("accumulatedTickDeltaTime"), Comp->GetAccumulatedTickDeltaTime());

	UBehaviorTree* CurrentTree = Comp->GetCurrentTree();
	UBehaviorTree* RootTree = Comp->GetRootTree();
	if (CurrentTree)
	{
		Result->SetStringField(TEXT("currentTree"), CurrentTree->GetPathName());
		Result->SetStringField(TEXT("currentTreeName"), CurrentTree->GetName());
	}
	if (RootTree)
	{
		Result->SetStringField(TEXT("rootTree"), RootTree->GetPathName());
		// A subtree is running when these differ, which is the answer to "the
		// node names do not match the asset I authored".
		Result->SetBoolField(TEXT("inSubtree"), CurrentTree != RootTree);
	}

	// The active node, and the chain of composites above it. That chain is what
	// says WHICH branch of a Selector is live, and it is readable without the
	// protected instance stack because every node keeps its parent pointer.
	const UBTNode* ActiveNode = Comp->GetActiveNode();
	if (ActiveNode)
	{
		TSharedPtr<FJsonObject> NodeObj = MCPBTLiveDescribeNode(ActiveNode);
		if (const UBTTaskNode* AsTask = Cast<const UBTTaskNode>(ActiveNode))
		{
			NodeObj->SetStringField(TEXT("taskStatus"), MCPBTLiveTaskStatusName(Comp->GetTaskStatus(AsTask)));
		}
		Result->SetObjectField(TEXT("activeNode"), NodeObj);

		TArray<TSharedPtr<FJsonValue>> Ancestors;
		const UBTNode* Walk = ActiveNode->GetParentNode();
		int32 Guard = 0;
		while (Walk && Guard++ < MCPBTLiveMaxAncestors)
		{
			Ancestors.Add(MakeShared<FJsonValueObject>(MCPBTLiveDescribeNode(Walk)));
			Walk = Walk->GetParentNode();
		}
		Result->SetArrayField(TEXT("ancestors"), Ancestors);
		Result->SetNumberField(TEXT("ancestorCount"), Ancestors.Num());
		if (Guard >= MCPBTLiveMaxAncestors)
		{
			Result->SetBoolField(TEXT("ancestorsTruncated"), true);
		}
	}
	else
	{
		Result->SetField(TEXT("activeNode"), MakeShared<FJsonValueNull>());
	}

	// Active decorators and services. IsAuxNodeActive is the public question;
	// the tree asset supplies the candidates, since InstanceStack is protected.
	if (OptionalBool(Params, TEXT("includeAuxNodes"), true) && CurrentTree)
	{
		TArray<TSharedPtr<FJsonValue>> ActiveAux;
		int32 Considered = 0;
		for (int32 Index = 0; Index < CurrentTree->RootDecorators.Num(); ++Index)
		{
			UBTDecorator* Decorator = CurrentTree->RootDecorators[Index].Get();
			if (!Decorator) continue;
			++Considered;
			if (!Comp->IsAuxNodeActive(Decorator)) continue;
			TSharedPtr<FJsonObject> Row = MCPBTLiveDescribeNode(Decorator);
			Row->SetStringField(TEXT("nodePath"), FString::Printf(TEXT("Root.RootDecorators[%d]"), Index));
			ActiveAux.Add(MakeShared<FJsonValueObject>(Row));
		}
		MCPBTLiveCollectAuxNodes(*Comp, CurrentTree->RootNode.Get(), TEXT("Root"), 0, ActiveAux, Considered);
		Result->SetArrayField(TEXT("activeAuxNodes"), ActiveAux);
		Result->SetNumberField(TEXT("activeAuxNodeCount"), ActiveAux.Num());
		Result->SetNumberField(TEXT("auxNodesConsidered"), Considered);
	}

	// The engine's own dumps. They carry the per-instance detail the protected
	// instance stack would otherwise be the only source of, and they are the
	// same text the BT debugger shows.
	if (OptionalBool(Params, TEXT("includeDebugStrings"), true))
	{
		Result->SetStringField(TEXT("activeTasks"), Comp->DescribeActiveTasks());
		Result->SetStringField(TEXT("activeTrees"), Comp->DescribeActiveTrees());
		Result->SetStringField(TEXT("debugInfo"), Comp->GetDebugInfoString());
	}

	if (!bRunning)
	{
		Result->SetStringField(TEXT("note"), FString::Printf(
			TEXT("No BehaviorTree is running on '%s'%s. Start one with gameplay(run_behavior_tree). A tree started this frame reports no active node until the component next ticks, so read it again after the tree has ticked."),
			*Target.BrainOwner->GetActorLabel(),
			Comp->IsPaused() ? TEXT(" (the logic is PAUSED; resume it with gameplay(stop_behavior_tree) mode=\"resume\")") : TEXT("")));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::GetLiveBlackboard(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FMCPBTLiveTarget Target;
	TSharedPtr<FJsonValue> Err;
	if (!MCPBTLiveResolveTarget(Params, /*bRequireBehaviorTree*/ false, Target, Err)) return Err;

	UBlackboardComponent* BB = MCPBTLiveRequireBlackboard(Target, Err);
	if (!BB) return Err;

	const FString VerbosityWord = OptionalString(Params, TEXT("verbosity"), TEXT("detailed")).ToLower();
	EBlackboardDescription::Type Verbosity = EBlackboardDescription::DetailedKeyWithValue;
	if (!MCPBTLiveParseVerbosity(VerbosityWord, Verbosity))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown verbosity '%s'. Use \"onlyValue\", \"keyWithValue\", \"detailed\" or \"full\"."),
			*VerbosityWord));
	}

	auto Result = MCPSuccess();
	MCPBTLiveWriteTargetFields(Result, Target);
	Result->SetStringField(TEXT("verbosity"), VerbosityWord);

	const int32 NumKeys = BB->GetNumKeys();

	// One named key, when the caller already knows which one it wants.
	const FString SingleKey = OptionalString(Params, TEXT("key"));
	if (!SingleKey.IsEmpty())
	{
		const FBlackboard::FKey KeyID = BB->GetKeyID(FName(*SingleKey));
		if (KeyID == FBlackboard::InvalidKey)
		{
			TArray<FString> Known;
			for (int32 Index = 0; Index < NumKeys; ++Index)
			{
				Known.Add(BB->GetKeyName(FBlackboard::FKey(Index)).ToString());
			}
			return MCPError(FString::Printf(
				TEXT("Blackboard key '%s' does not exist on '%s'. The live blackboard has %d keys: [%s]. Key names are case-sensitive; gameplay(read_blackboard) reads the same names off the BlackboardData asset."),
				*SingleKey,
				*Target.BrainOwner->GetActorLabel(),
				NumKeys,
				Known.Num() > 0 ? *FString::Join(Known, TEXT(", ")) : TEXT("none")));
		}
		Result->SetObjectField(TEXT("keyEntry"), MCPBTLiveDescribeKey(*BB, KeyID, Verbosity));
		Result->SetNumberField(TEXT("keyCount"), NumKeys);
		return MCPResult(Result);
	}

	TArray<TSharedPtr<FJsonValue>> Rows;
	for (int32 Index = 0; Index < NumKeys; ++Index)
	{
		Rows.Add(MakeShared<FJsonValueObject>(
			MCPBTLiveDescribeKey(*BB, FBlackboard::FKey(Index), Verbosity)));
	}
	Result->SetArrayField(TEXT("keys"), Rows);
	Result->SetNumberField(TEXT("keyCount"), Rows.Num());
	// The engine's own dump, so a key type this handler has no typed reader for
	// is still reported as something rather than as an empty field.
	Result->SetStringField(TEXT("debugInfo"), BB->GetDebugInfoString(Verbosity));
	if (Rows.Num() == 0)
	{
		Result->SetStringField(TEXT("note"), FString::Printf(
			TEXT("The BlackboardData asset on '%s' declares no keys. Add them with gameplay(add_blackboard_key) and restart the tree with gameplay(run_behavior_tree)."),
			*Target.BrainOwner->GetActorLabel()));
	}
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// Writes
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FGameplayHandlers::SetLiveBlackboard(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FString KeyName;
	if (auto Err = RequireString(Params, TEXT("key"), KeyName)) return Err;

	const bool bClear = OptionalBool(Params, TEXT("clear"), false);
	const TSharedPtr<FJsonValue> Value = Params->TryGetField(TEXT("value"));
	if (!bClear && !Value.IsValid())
	{
		return MCPError(TEXT("Missing required parameter 'value'. Pass clear=true to reset the key to its unset state instead."));
	}

	FMCPBTLiveTarget Target;
	TSharedPtr<FJsonValue> Err;
	if (!MCPBTLiveResolveTarget(Params, /*bRequireBehaviorTree*/ false, Target, Err)) return Err;

	UBlackboardComponent* BB = MCPBTLiveRequireBlackboard(Target, Err);
	if (!BB) return Err;

	const FName Key(*KeyName);
	const FBlackboard::FKey KeyID = BB->GetKeyID(Key);
	if (KeyID == FBlackboard::InvalidKey)
	{
		TArray<FString> Known;
		const int32 NumKeys = BB->GetNumKeys();
		for (int32 Index = 0; Index < NumKeys; ++Index)
		{
			Known.Add(BB->GetKeyName(FBlackboard::FKey(Index)).ToString());
		}
		return MCPError(FString::Printf(
			TEXT("Blackboard key '%s' does not exist on '%s'. The live blackboard has %d keys: [%s]. Key names are case-sensitive. Add a key to the asset with gameplay(add_blackboard_key), then restart the tree so the running component picks it up."),
			*KeyName,
			*Target.BrainOwner->GetActorLabel(),
			NumKeys,
			Known.Num() > 0 ? *FString::Join(Known, TEXT(", ")) : TEXT("none")));
	}

	UClass* TypeClass = BB->GetKeyType(KeyID).Get();
	const FString TypeName = MCPBTLiveKeyTypeShortName(TypeClass);

	// Captured BEFORE the write, because the rollback record IS the previous
	// value and there is no second chance to read it.
	TSharedPtr<FJsonObject> Before = MCPBTLiveDescribeKey(*BB, KeyID, EBlackboardDescription::DetailedKeyWithValue);
	FString PreviousDescribed;
	Before->TryGetStringField(TEXT("value"), PreviousDescribed);

	// The rollback payload's own 'value', typed the same way the write is, so
	// replaying it lands on the same SetValueAs* path rather than on a string
	// parse of a description. Left unset when the previous value cannot be
	// expressed, and the result then says the rollback is lossy.
	TSharedPtr<FJsonValue> PreviousTyped;
	bool bPreviousWasNull = false;

	// Every write below goes through a typed SetValueAs* accessor rather than
	// touching GetKeyRawData. That is the whole point of this action: SetValueAs*
	// calls the key type's SetValue, which raises the key-change notification the
	// blackboard broadcasts to its registered observers. A raw memory write
	// stores the same bytes and notifies NOTHING, so every observing decorator
	// keeps its stale evaluation, no branch re-runs, and the tree carries on down
	// the path the old value chose while the blackboard reads back "correct".
	bool bApplied = false;
	FString UnsupportedReason;

	if (bClear)
	{
		BB->ClearValue(KeyID);
		bApplied = true;
		PreviousTyped = Before->TryGetField(TEXT("typedValue"));
	}
	else if (!TypeClass)
	{
		UnsupportedReason = TEXT("the key has no key type class, so the blackboard asset is malformed");
	}
	else if (TypeClass == UBlackboardKeyType_Bool::StaticClass())
	{
		bool Raw = false;
		if (!Value->TryGetBool(Raw))
		{
			return MCPError(FString::Printf(
				TEXT("Blackboard key '%s' is a Bool key; 'value' must be true or false."), *KeyName));
		}
		PreviousTyped = MakeShared<FJsonValueBoolean>(BB->GetValueAsBool(Key));
		BB->SetValueAsBool(Key, Raw);
		bApplied = true;
	}
	else if (TypeClass == UBlackboardKeyType_Int::StaticClass())
	{
		double Raw = 0.0;
		if (!Value->TryGetNumber(Raw))
		{
			return MCPError(FString::Printf(
				TEXT("Blackboard key '%s' is an Int key; 'value' must be a number."), *KeyName));
		}
		PreviousTyped = MakeShared<FJsonValueNumber>(BB->GetValueAsInt(Key));
		BB->SetValueAsInt(Key, FMath::RoundToInt(Raw));
		bApplied = true;
	}
	else if (TypeClass == UBlackboardKeyType_Float::StaticClass())
	{
		double Raw = 0.0;
		if (!Value->TryGetNumber(Raw))
		{
			return MCPError(FString::Printf(
				TEXT("Blackboard key '%s' is a Float key; 'value' must be a number."), *KeyName));
		}
		PreviousTyped = MakeShared<FJsonValueNumber>(BB->GetValueAsFloat(Key));
		BB->SetValueAsFloat(Key, static_cast<float>(Raw));
		bApplied = true;
	}
	else if (TypeClass == UBlackboardKeyType_String::StaticClass())
	{
		FString Raw;
		if (!Value->TryGetString(Raw))
		{
			return MCPError(FString::Printf(
				TEXT("Blackboard key '%s' is a String key; 'value' must be a string."), *KeyName));
		}
		PreviousTyped = MakeShared<FJsonValueString>(BB->GetValueAsString(Key));
		BB->SetValueAsString(Key, Raw);
		bApplied = true;
	}
	else if (TypeClass == UBlackboardKeyType_Name::StaticClass())
	{
		FString Raw;
		if (!Value->TryGetString(Raw))
		{
			return MCPError(FString::Printf(
				TEXT("Blackboard key '%s' is a Name key; 'value' must be a string."), *KeyName));
		}
		PreviousTyped = MakeShared<FJsonValueString>(BB->GetValueAsName(Key).ToString());
		BB->SetValueAsName(Key, FName(*Raw));
		bApplied = true;
	}
	else if (TypeClass == UBlackboardKeyType_Vector::StaticClass())
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Value->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
		{
			return MCPError(FString::Printf(
				TEXT("Blackboard key '%s' is a Vector key; 'value' must be an object {x, y, z}."), *KeyName));
		}
		const FVector Old = BB->GetValueAsVector(Key);
		FVector NewValue = Old;
		ReadVec3Fields(*Obj, NewValue);
		TSharedPtr<FJsonObject> OldObj = MakeShared<FJsonObject>();
		OldObj->SetNumberField(TEXT("x"), Old.X);
		OldObj->SetNumberField(TEXT("y"), Old.Y);
		OldObj->SetNumberField(TEXT("z"), Old.Z);
		PreviousTyped = MakeShared<FJsonValueObject>(OldObj);
		// A vector key that was never set has no meaningful previous vector, so
		// the rollback is a clear rather than a write of (0,0,0).
		bPreviousWasNull = !BB->IsVectorValueSet(Key);
		BB->SetValueAsVector(Key, NewValue);
		bApplied = true;
	}
	else if (TypeClass == UBlackboardKeyType_Rotator::StaticClass())
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Value->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
		{
			return MCPError(FString::Printf(
				TEXT("Blackboard key '%s' is a Rotator key; 'value' must be an object {pitch, yaw, roll}."), *KeyName));
		}
		const FRotator Old = BB->GetValueAsRotator(Key);
		FRotator NewValue = Old;
		ReadRotatorFields(*Obj, NewValue);
		TSharedPtr<FJsonObject> OldObj = MakeShared<FJsonObject>();
		OldObj->SetNumberField(TEXT("pitch"), Old.Pitch);
		OldObj->SetNumberField(TEXT("yaw"), Old.Yaw);
		OldObj->SetNumberField(TEXT("roll"), Old.Roll);
		PreviousTyped = MakeShared<FJsonValueObject>(OldObj);
		BB->SetValueAsRotator(Key, NewValue);
		bApplied = true;
	}
	else if (TypeClass == UBlackboardKeyType_Object::StaticClass())
	{
		UObject* Old = BB->GetValueAsObject(Key);
		PreviousTyped = Old
			? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueString>(Old->GetPathName()))
			: StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>());
		bPreviousWasNull = Old == nullptr;

		if (Value->Type == EJson::Null)
		{
			BB->SetValueAsObject(Key, nullptr);
			bApplied = true;
		}
		else
		{
			FString Spec;
			if (!Value->TryGetString(Spec) || Spec.IsEmpty())
			{
				return MCPError(FString::Printf(
					TEXT("Blackboard key '%s' is an Object key; 'value' must be an actor label, an actor object path, or an asset path (or null to clear it)."),
					*KeyName));
			}
			TSharedPtr<FJsonValue> ResolveErr;
			UObject* Resolved = MCPBTLiveResolveObjectValue(Target.World, Spec, ResolveErr);
			if (!Resolved) return ResolveErr.IsValid() ? ResolveErr : MCPAssetNotFoundError(Spec, TEXT("Blackboard object value"));
			BB->SetValueAsObject(Key, Resolved);
			bApplied = true;
		}
	}
	else if (TypeClass == UBlackboardKeyType_Class::StaticClass())
	{
		UClass* Old = BB->GetValueAsClass(Key);
		PreviousTyped = Old
			? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueString>(Old->GetPathName()))
			: StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>());
		bPreviousWasNull = Old == nullptr;

		if (Value->Type == EJson::Null)
		{
			BB->SetValueAsClass(Key, nullptr);
			bApplied = true;
		}
		else
		{
			FString Spec;
			if (!Value->TryGetString(Spec) || Spec.IsEmpty())
			{
				return MCPError(FString::Printf(
					TEXT("Blackboard key '%s' is a Class key; 'value' must be a class path, a Blueprint asset path, or a short class name (or null to clear it)."),
					*KeyName));
			}
			UClass* Resolved = LoadObject<UClass>(nullptr, *Spec);
			if (!Resolved) Resolved = LoadObject<UClass>(nullptr, *(Spec + TEXT("_C")));
			if (!Resolved) Resolved = FindClassByShortName(Spec);
			if (!Resolved)
			{
				if (UBlueprint* AsBlueprint = LoadAssetByPath<UBlueprint>(Spec))
				{
					Resolved = AsBlueprint->GeneratedClass;
				}
			}
			if (!Resolved)
			{
				return MCPError(FString::Printf(
					TEXT("Could not resolve '%s' to a UClass for Class key '%s'. Tried the path as a class, the path with a '_C' suffix, a short native class name, and the Blueprint at that path. reflection(find_classes) lists the real class names."),
					*Spec, *KeyName));
			}
			BB->SetValueAsClass(Key, Resolved);
			bApplied = true;
		}
	}
	else if (TypeClass == UBlackboardKeyType_Enum::StaticClass()
		|| TypeClass == UBlackboardKeyType_NativeEnum::StaticClass())
	{
		const uint8 Old = BB->GetValueAsEnum(Key);
		PreviousTyped = MakeShared<FJsonValueNumber>(Old);

		double Raw = 0.0;
		FString AsWord;
		if (Value->TryGetNumber(Raw))
		{
			BB->SetValueAsEnum(Key, static_cast<uint8>(FMath::RoundToInt(Raw)));
			bApplied = true;
		}
		else if (Value->TryGetString(AsWord))
		{
			const UEnum* Enum = MCPBTLiveResolveKeyEnum(TypeClass);
			const int64 Found = Enum ? Enum->GetValueByNameString(AsWord) : INDEX_NONE;
			if (Found == INDEX_NONE)
			{
				return MCPError(FString::Printf(
					TEXT("'%s' is not an enumerator of the enum behind Blackboard key '%s'%s. Pass the numeric value instead, or read the current enumerator with gameplay(get_live_blackboard)."),
					*AsWord, *KeyName,
					Enum ? *FString::Printf(TEXT(" (%s)"), *Enum->GetPathName()) : TEXT(" (the key type CDO exposes no UEnum)")));
			}
			BB->SetValueAsEnum(Key, static_cast<uint8>(Found));
			bApplied = true;
		}
		else
		{
			return MCPError(FString::Printf(
				TEXT("Blackboard key '%s' is an Enum key; 'value' must be a number or an enumerator name."), *KeyName));
		}
	}
	else
	{
		UnsupportedReason = FString::Printf(
			TEXT("key type '%s' has no typed setter on UBlackboardComponent"), *TypeName);
	}

	if (!bApplied)
	{
		return MCPError(FString::Printf(
			TEXT("Cannot write Blackboard key '%s' on '%s': %s. Writable key types are Bool, Int, Float, String, Name, Vector, Rotator, Object, Class and Enum. A Struct key is only writable through the node that owns it; read its current value with gameplay(get_live_blackboard)."),
			*KeyName, *Target.BrainOwner->GetActorLabel(), *UnsupportedReason));
	}

	TSharedPtr<FJsonObject> After = MCPBTLiveDescribeKey(*BB, KeyID, EBlackboardDescription::DetailedKeyWithValue);
	FString NewDescribed;
	After->TryGetStringField(TEXT("value"), NewDescribed);
	const bool bChanged = NewDescribed != PreviousDescribed;

	auto Result = MCPSuccess();
	MCPBTLiveWriteTargetFields(Result, Target);
	Result->SetStringField(TEXT("key"), KeyName);
	Result->SetStringField(TEXT("keyType"), TypeName);
	Result->SetBoolField(TEXT("cleared"), bClear);
	Result->SetObjectField(TEXT("previous"), Before);
	Result->SetObjectField(TEXT("current"), After);
	Result->SetStringField(TEXT("previousValue"), PreviousDescribed);
	Result->SetStringField(TEXT("currentValue"), NewDescribed);
	Result->SetBoolField(TEXT("changed"), bChanged);
	if (bChanged)
	{
		MCPSetUpdated(Result);
	}
	else
	{
		// Idempotent replay: the key already held this value, so nothing moved
		// and no observer fired. Saying so is the difference between "the write
		// did nothing" and "the write was unnecessary".
		Result->SetBoolField(TEXT("alreadySet"), true);
	}
	Result->SetStringField(TEXT("observerNote"),
		TEXT("Written through the typed SetValueAs* accessor, so the blackboard raised its key-change notification and every observing decorator re-evaluated. Read gameplay(get_bt_runtime) again to see which branch the tree moved to."));

	// The inverse is a write back to the value captured above. Object and Class
	// keys roll back by path, which resolves to the same object as long as it
	// still exists; a previously-unset key rolls back with clear.
	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	RollbackPayload->SetStringField(TEXT("actorPath"), Target.Actor->GetPathName());
	RollbackPayload->SetStringField(TEXT("world"), OptionalString(Params, TEXT("world"), TEXT("auto")));
	RollbackPayload->SetStringField(TEXT("key"), KeyName);
	if (bPreviousWasNull)
	{
		RollbackPayload->SetBoolField(TEXT("clear"), true);
	}
	else if (PreviousTyped.IsValid())
	{
		RollbackPayload->SetField(TEXT("value"), PreviousTyped);
	}
	MCPSetRollback(Result, TEXT("set_live_blackboard"), RollbackPayload);
	if (!bPreviousWasNull && !PreviousTyped.IsValid())
	{
		// Never pretend. A rollback record that cannot restore the old value is
		// worse than none, because a FlowRunner would replay it and believe it.
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The previous value of '%s' could not be expressed as a typed value, so the rollback record carries no value and replaying it would be a no-op. previousValue holds the engine's description of it: %s"),
			*KeyName, *PreviousDescribed));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::RunBehaviorTree(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("behaviorTreePath"), AssetPath)) return Err;

	FMCPBTLiveTarget Target;
	TSharedPtr<FJsonValue> Err;
	if (!MCPBTLiveResolveTarget(Params, /*bRequireBehaviorTree*/ false, Target, Err))
	{
		// An AIController with no brain at all is the normal case for
		// RunBehaviorTree, because AAIController::RunBehaviorTree is what
		// creates the component. So a missing brain is only fatal when the
		// actor is not an AIController either.
		const FString FallbackScope = OptionalString(Params, TEXT("world"), TEXT("auto"));
		UWorld* World = ResolveWorldFromParams(Params, *FallbackScope);
		TSharedPtr<FJsonValue> ActorErr;
		FMCPActorSelector Selector;
		Selector.Match = EMCPActorMatch::LabelNameOrPath;
		Selector.WorldLabel = World && World->IsGameWorld() ? TEXT("PIE") : TEXT("editor");
		AActor* Actor = World ? MCPResolveActor(World, Params, ActorErr, Selector) : nullptr;
		if (!Actor) return Err;

		TArray<AActor*> Candidates;
		MCPBTLiveBrainCandidates(Actor, Candidates);
		AAIController* Controller = nullptr;
		for (AActor* Candidate : Candidates)
		{
			if (AAIController* AsAI = Cast<AAIController>(Candidate)) { Controller = AsAI; break; }
		}
		if (!Controller) return Err;

		Target.World = World;
		Target.Actor = Actor;
		Target.AIController = Controller;
		Target.BrainOwner = Controller;
	}

	UObject* AssetObject = MCPRequireAssetObject(AssetPath, Err, TEXT("BehaviorTree"));
	if (!AssetObject) return Err;
	UBehaviorTree* Tree = Cast<UBehaviorTree>(AssetObject);
	if (!Tree) return MCPAssetWrongTypeError(AssetPath, AssetObject, TEXT("BehaviorTree"));

	const FString ModeWord = OptionalString(Params, TEXT("executionMode"), TEXT("looped")).ToLower();
	EBTExecutionMode::Type ExecutionMode = EBTExecutionMode::Looped;
	if (ModeWord == TEXT("singlerun")) ExecutionMode = EBTExecutionMode::SingleRun;
	else if (ModeWord != TEXT("looped"))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown executionMode '%s'. Use \"looped\" (restart from the root when the tree finishes) or \"singleRun\" (stop after one pass)."),
			*ModeWord));
	}

	const bool bRestartIfRunning = OptionalBool(Params, TEXT("restartIfRunning"), false);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), Tree->GetPathName());
	Result->SetStringField(TEXT("executionMode"), ModeWord);

	// Idempotency: the same tree already running is not a reason to tear it
	// down and lose its state. A second call reports what is there.
	if (Target.BTComp && Target.BTComp->IsRunning() && Target.BTComp->GetRootTree() == Tree && !bRestartIfRunning)
	{
		MCPBTLiveWriteTargetFields(Result, Target);
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("started"), false);
		Result->SetBoolField(TEXT("alreadyRunning"), true);
		Result->SetBoolField(TEXT("running"), true);
		Result->SetBoolField(TEXT("paused"), Target.BTComp->IsPaused());
		if (const UBTNode* Active = Target.BTComp->GetActiveNode())
		{
			Result->SetObjectField(TEXT("activeNode"), MCPBTLiveDescribeNode(Active));
		}
		Result->SetStringField(TEXT("note"),
			TEXT("This tree is already the running root tree on this agent, so nothing was restarted. Pass restartIfRunning=true to force a fresh start from the root."));
		return MCPResult(Result);
	}

	// What was running before, so the rollback can put it back rather than only
	// stopping whatever this call started.
	UBehaviorTree* PreviousTree = Target.BTComp ? Target.BTComp->GetRootTree() : nullptr;
	const bool bWasRunning = Target.BTComp && Target.BTComp->IsRunning();

	bool bStarted = false;
	FString StartRoute;
	if (Target.AIController)
	{
		// The controller route also creates the BehaviorTreeComponent and
		// initializes the blackboard from the tree's own BlackboardAsset, which
		// is why it is preferred over StartTree on an existing component.
		bStarted = Target.AIController->RunBehaviorTree(Tree);
		StartRoute = TEXT("AAIController::RunBehaviorTree");
		if (bStarted)
		{
			// Re-resolve: the controller may have just created the component.
			if (UBrainComponent* Brain = Target.AIController->GetBrainComponent())
			{
				Target.Brain = Brain;
				Target.BrainOwner = Target.AIController;
				Target.BTComp = Cast<UBehaviorTreeComponent>(Brain);
				Target.Blackboard = Brain->GetBlackboardComponent();
				if (!Target.Blackboard) Target.Blackboard = Target.AIController->GetBlackboardComponent();
			}
		}
	}
	else if (Target.BTComp)
	{
		Target.BTComp->StartTree(*Tree, ExecutionMode);
		StartRoute = TEXT("UBehaviorTreeComponent::StartTree");
		bStarted = true;
	}

	if (!bStarted)
	{
		return MCPError(FString::Printf(
			TEXT("Could not start '%s' on '%s'. %s Reached the actor and %s, and the start call refused. AAIController::RunBehaviorTree refuses a tree whose BlackboardAsset is unset when the controller has no blackboard yet: set one with gameplay(set_behavior_tree_blackboard). gameplay(list_ai_agents) shows which agents carry a brain at all."),
			*Tree->GetPathName(),
			*Target.Actor->GetActorLabel(),
			Target.World->IsGameWorld()
				? TEXT("")
				: TEXT("This is the EDITOR world, where no AI ticks; start Play-In-Editor and pass world=\"pie\"."),
			Target.AIController ? TEXT("its AIController") : TEXT("its BehaviorTreeComponent")));
	}

	MCPBTLiveWriteTargetFields(Result, Target);
	MCPSetCreated(Result);
	Result->SetBoolField(TEXT("started"), true);
	Result->SetBoolField(TEXT("alreadyRunning"), false);
	Result->SetBoolField(TEXT("restartedRunningTree"), bWasRunning);
	Result->SetStringField(TEXT("startRoute"), StartRoute);
	if (PreviousTree)
	{
		Result->SetStringField(TEXT("previousTree"), PreviousTree->GetPathName());
	}
	if (Target.BTComp)
	{
		Result->SetBoolField(TEXT("running"), Target.BTComp->IsRunning());
		Result->SetBoolField(TEXT("paused"), Target.BTComp->IsPaused());
		Result->SetBoolField(TEXT("treeHasBeenStarted"), Target.BTComp->TreeHasBeenStarted());
		if (const UBehaviorTree* Current = Target.BTComp->GetCurrentTree())
		{
			Result->SetStringField(TEXT("currentTree"), Current->GetPathName());
		}
		if (const UBTNode* Active = Target.BTComp->GetActiveNode())
		{
			Result->SetObjectField(TEXT("activeNode"), MCPBTLiveDescribeNode(Active));
		}
		else
		{
			Result->SetField(TEXT("activeNode"), MakeShared<FJsonValueNull>());
		}
	}
	Result->SetStringField(TEXT("note"),
		TEXT("The first search runs on the component's next tick, so activeNode is usually null in this response even on a healthy start. Call gameplay(get_bt_runtime) after a tick to see which node is executing."));

	// The inverse of a start is a stop, or a restart of what was running before.
	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	RollbackPayload->SetStringField(TEXT("actorPath"), Target.Actor->GetPathName());
	RollbackPayload->SetStringField(TEXT("world"), OptionalString(Params, TEXT("world"), TEXT("auto")));
	if (bWasRunning && PreviousTree && PreviousTree != Tree)
	{
		RollbackPayload->SetStringField(TEXT("assetPath"), PreviousTree->GetPathName());
		RollbackPayload->SetBoolField(TEXT("restartIfRunning"), true);
		MCPSetRollback(Result, TEXT("run_behavior_tree"), RollbackPayload);
		// A restarted tree comes back at its root, not at the node it was on.
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("Rollback restarts '%s' from its root. The execution position, node memory and blackboard state that tree had before this call are gone and cannot be restored."),
			*PreviousTree->GetPathName()));
	}
	else
	{
		RollbackPayload->SetStringField(TEXT("mode"), TEXT("stop"));
		MCPSetRollback(Result, TEXT("stop_behavior_tree"), RollbackPayload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::StopBehaviorTree(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	const FString Mode = OptionalString(Params, TEXT("mode"), TEXT("stop")).ToLower();
	if (Mode != TEXT("stop") && Mode != TEXT("forced") && Mode != TEXT("restart")
		&& Mode != TEXT("pause") && Mode != TEXT("resume"))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown mode '%s'. Use \"stop\" (StopTree Safe: the active task aborts cleanly), \"forced\" (StopTree Forced: no abort latency), \"restart\" (RestartTree from the root, keeping the same asset), \"pause\" (PauseLogic: the tree stops ticking and keeps its state) or \"resume\" (ResumeLogic)."),
			*Mode));
	}

	FMCPBTLiveTarget Target;
	TSharedPtr<FJsonValue> Err;
	if (!MCPBTLiveResolveTarget(Params, /*bRequireBehaviorTree*/ true, Target, Err)) return Err;

	UBehaviorTreeComponent* Comp = Target.BTComp;
	const FString Reason = OptionalString(Params, TEXT("reason"), TEXT("ue-mcp stop_behavior_tree"));

	const bool bWasRunning = Comp->IsRunning();
	const bool bWasPaused = Comp->IsPaused();
	UBehaviorTree* RunningTree = Comp->GetRootTree();

	auto Result = MCPSuccess();
	MCPBTLiveWriteTargetFields(Result, Target);
	Result->SetStringField(TEXT("mode"), Mode);
	Result->SetStringField(TEXT("reason"), Reason);
	Result->SetBoolField(TEXT("wasRunning"), bWasRunning);
	Result->SetBoolField(TEXT("wasPaused"), bWasPaused);
	if (RunningTree) Result->SetStringField(TEXT("tree"), RunningTree->GetPathName());

	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	RollbackPayload->SetStringField(TEXT("actorPath"), Target.Actor->GetPathName());
	RollbackPayload->SetStringField(TEXT("world"), OptionalString(Params, TEXT("world"), TEXT("auto")));

	if (Mode == TEXT("stop") || Mode == TEXT("forced"))
	{
		// Idempotent: a second stop on a stopped tree is a no-op that reports
		// itself rather than an error, so a rollback replay is safe.
		if (!bWasRunning && Comp->IsInstanceStackEmpty())
		{
			Result->SetBoolField(TEXT("stopped"), false);
			Result->SetBoolField(TEXT("alreadyStopped"), true);
			Result->SetStringField(TEXT("note"), FString::Printf(
				TEXT("No tree was running on '%s', so nothing was stopped. gameplay(run_behavior_tree) starts one."),
				*Target.BrainOwner->GetActorLabel()));
			return MCPResult(Result);
		}
		Comp->StopTree(Mode == TEXT("forced") ? EBTStopMode::Forced : EBTStopMode::Safe);
		Result->SetBoolField(TEXT("stopped"), true);
		Result->SetBoolField(TEXT("alreadyStopped"), false);
		MCPSetUpdated(Result);

		if (RunningTree)
		{
			RollbackPayload->SetStringField(TEXT("assetPath"), RunningTree->GetPathName());
			RollbackPayload->SetBoolField(TEXT("restartIfRunning"), true);
			MCPSetRollback(Result, TEXT("run_behavior_tree"), RollbackPayload);
			// Honest about what a restart does and does not restore.
			Result->SetBoolField(TEXT("rollbackLossy"), true);
			Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("Rollback restarts '%s' from its root. Stopping a tree destroys its instance stack and node memory, so the execution position this agent was at cannot be restored by any call."),
				*RunningTree->GetPathName()));
		}
		else
		{
			Result->SetBoolField(TEXT("rollbackLossy"), true);
			Result->SetStringField(TEXT("rollbackNote"),
				TEXT("No root tree was readable at stop time, so no rollback record was emitted: there is no asset path to restart. Start a tree explicitly with gameplay(run_behavior_tree)."));
		}
	}
	else if (Mode == TEXT("restart"))
	{
		if (!bWasRunning)
		{
			Result->SetBoolField(TEXT("restarted"), false);
			Result->SetStringField(TEXT("note"), FString::Printf(
				TEXT("No tree is running on '%s', so there is nothing to restart. gameplay(run_behavior_tree) with assetPath starts one."),
				*Target.BrainOwner->GetActorLabel()));
			return MCPResult(Result);
		}
		const bool bComplete = OptionalBool(Params, TEXT("completeRestart"), false);
		Comp->RestartTree(bComplete ? EBTRestartMode::CompleteRestart : EBTRestartMode::ForceReevaluateRootNode);
		Result->SetBoolField(TEXT("restarted"), true);
		Result->SetBoolField(TEXT("completeRestart"), bComplete);
		MCPSetUpdated(Result);
		// A restart has no inverse: the position it left is gone.
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("A restart has no inverse, so no rollback record was emitted. The execution position the tree was at before this call is destroyed by the restart and cannot be re-entered."));
	}
	else if (Mode == TEXT("pause"))
	{
		if (bWasPaused)
		{
			Result->SetBoolField(TEXT("paused"), true);
			Result->SetBoolField(TEXT("alreadyPaused"), true);
			Result->SetStringField(TEXT("note"), TEXT("The logic was already paused, so nothing changed."));
			return MCPResult(Result);
		}
		Comp->PauseLogic(Reason);
		Result->SetBoolField(TEXT("paused"), Comp->IsPaused());
		Result->SetBoolField(TEXT("alreadyPaused"), false);
		MCPSetUpdated(Result);
		RollbackPayload->SetStringField(TEXT("mode"), TEXT("resume"));
		MCPSetRollback(Result, TEXT("stop_behavior_tree"), RollbackPayload);
	}
	else // resume
	{
		if (!bWasPaused)
		{
			Result->SetBoolField(TEXT("resumed"), false);
			Result->SetBoolField(TEXT("alreadyRunning"), true);
			Result->SetStringField(TEXT("note"), TEXT("The logic was not paused, so nothing was resumed."));
			return MCPResult(Result);
		}
		const EAILogicResuming::Type Resumed = Comp->ResumeLogic(Reason);
		Result->SetBoolField(TEXT("resumed"), true);
		Result->SetBoolField(TEXT("alreadyRunning"), false);
		// RestartedInstead means the component could not continue where it was
		// and started over, which changes what the caller is looking at.
		Result->SetStringField(TEXT("resumeResult"),
			Resumed == EAILogicResuming::RestartedInstead ? TEXT("restartedInstead") : TEXT("continue"));
		MCPSetUpdated(Result);
		RollbackPayload->SetStringField(TEXT("mode"), TEXT("pause"));
		MCPSetRollback(Result, TEXT("stop_behavior_tree"), RollbackPayload);
	}

	Result->SetBoolField(TEXT("running"), Comp->IsRunning());
	Result->SetBoolField(TEXT("isPaused"), Comp->IsPaused());
	Result->SetBoolField(TEXT("instanceStackEmpty"), Comp->IsInstanceStackEmpty());
	if (const UBTNode* Active = Comp->GetActiveNode())
	{
		Result->SetObjectField(TEXT("activeNode"), MCPBTLiveDescribeNode(Active));
	}
	else
	{
		Result->SetField(TEXT("activeNode"), MakeShared<FJsonValueNull>());
	}
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// Discovery
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FGameplayHandlers::ListAiAgents(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	const FString Scope = OptionalString(Params, TEXT("world"), TEXT("auto"));
	UWorld* World = ResolveWorldFromParams(Params, *Scope);
	if (!World)
	{
		return MCPError(FString::Printf(
			TEXT("No world available for scope '%s'. AI only runs in a game world, so start Play-In-Editor first, or pass world=\"editor\" to enumerate placed actors. editor(list_pie_instances) lists the running PIE worlds."),
			*Scope));
	}

	const bool bRunningOnly = OptionalBool(Params, TEXT("runningOnly"), false);
	const bool bBehaviorTreeOnly = OptionalBool(Params, TEXT("behaviorTreeOnly"), false);
	const FString ClassFilter = OptionalString(Params, TEXT("classFilter"));
	int32 Limit = OptionalInt(Params, TEXT("limit"), MCPBTLiveDefaultAgentLimit);
	if (Limit <= 0) Limit = MCPBTLiveDefaultAgentLimit;

	// Keyed on the brain component so a pawn and its controller do not produce
	// two rows for one agent.
	TSet<UBrainComponent*> Seen;
	TArray<TSharedPtr<FJsonValue>> Rows;
	int32 ActorsScanned = 0;
	int32 BrainsFound = 0;
	int32 FilteredOut = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor)) continue;
		++ActorsScanned;

		UBrainComponent* Brain = Actor->FindComponentByClass<UBrainComponent>();
		if (!Brain) continue;
		if (Seen.Contains(Brain)) continue;
		Seen.Add(Brain);
		++BrainsFound;

		const bool bIsBT = Brain->IsA<UBehaviorTreeComponent>();
		if (bBehaviorTreeOnly && !bIsBT) { ++FilteredOut; continue; }
		if (bRunningOnly && !Brain->IsRunning()) { ++FilteredOut; continue; }
		if (!ClassFilter.IsEmpty()
			&& !Actor->GetClass()->GetName().Contains(ClassFilter, ESearchCase::IgnoreCase))
		{
			++FilteredOut;
			continue;
		}

		if (Rows.Num() >= Limit) continue;
		Rows.Add(MakeShared<FJsonValueObject>(MCPBTLiveDescribeAgent(Actor, Brain)));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("world"), World->GetName());
	Result->SetStringField(TEXT("worldScope"), Scope);
	Result->SetBoolField(TEXT("isPlayInEditor"), World->IsPlayInEditor());
	Result->SetBoolField(TEXT("isGameWorld"), World->IsGameWorld());
	Result->SetArrayField(TEXT("agents"), Rows);
	Result->SetNumberField(TEXT("count"), Rows.Num());
	Result->SetNumberField(TEXT("brainsFound"), BrainsFound);
	Result->SetNumberField(TEXT("actorsScanned"), ActorsScanned);
	Result->SetNumberField(TEXT("filteredOut"), FilteredOut);
	if (BrainsFound > Rows.Num() + FilteredOut)
	{
		Result->SetBoolField(TEXT("truncated"), true);
		Result->SetNumberField(TEXT("limit"), Limit);
	}
	MCPNoteLoadedOnlyEnumeration(World, Result);

	if (Rows.Num() == 0)
	{
		Result->SetStringField(TEXT("note"), FString::Printf(
			TEXT("No AI agent matched. Scanned %d actors in the %s world '%s' for a UBrainComponent and found %d, of which %d were filtered out. A BehaviorTree only runs on an actor with a brain, which is normally an AAIController possessing a pawn, and controllers exist only in a game world: start Play-In-Editor and call again with world=\"pie\"."),
			ActorsScanned,
			World->IsGameWorld() ? TEXT("game") : TEXT("editor"),
			*World->GetName(),
			BrainsFound,
			FilteredOut));
	}
	return MCPResult(Result);
}
