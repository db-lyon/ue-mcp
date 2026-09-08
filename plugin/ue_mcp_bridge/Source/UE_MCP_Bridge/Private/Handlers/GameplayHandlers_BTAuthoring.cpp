// BehaviorTree editor-graph authoring: add, reparent, reorder and remove nodes,
// then recompile the graph into the runnable tree (#889, #947).
//
// Why this is native rather than a Python or reflection recipe.
//
// A UBehaviorTree asset holds two representations. The runtime one is
// UBehaviorTree::RootNode, a UBTCompositeNode tree whose child links, execution
// indices, memory offsets and tree depths are all derived data. The authored
// one is UBehaviorTree::BTGraph, a UBehaviorTreeGraph of UEdGraphNodes. Only
// UBehaviorTreeGraph::UpdateAsset() turns the second into the first, and that
// class lives in the editor-only BehaviorTreeEditor module with no script
// exposure at all. Spawning UBTNode subclasses by reflection therefore produces
// objects that are never wired into a runnable tree: InitializeNode is never
// called, so every node keeps MAX_uint16 as its execution index and the tree
// refuses to run. The orchestration, not the node types, is the missing piece.
//
// The sequence mirrors what the BehaviorTree editor itself performs:
//
//   1. Get or create BTGraph, seeding it with the schema's default root node
//      the way FBehaviorTreeEditor::RestoreBehaviorTree does.
//   2. Create the graph node, set its FGraphNodeClassData, and let
//      PostPlacedNewNode spawn the UBTNode instance under the asset.
//   3. Link children through the schema's TryCreateConnection, so the BT
//      schema's own rules (composite-only under root, single parent per child,
//      cycle rejection) decide what is legal instead of hand-written pin
//      arrays.
//   4. Attach decorators and services with AddSubNode. They are not children:
//      the compiler reads them off the owning graph node's Decorators and
//      Services arrays, and a decorator linked as a child would simply be
//      dropped.
//   5. Write properties, resolve blackboard selectors with InitializeFromAsset.
//   6. UpdateAsset() last, which is the compile.
//
// Child execution order, the thing Selector and Sequence are defined by, is not
// stored as a list. BTGraphHelpers::CreateChildren sorts each output pin's
// LinkedTo by FCompareNodeXLocation before walking it, so a child's ordinal is
// its graph node's NodePosX. Reordering is therefore a position rewrite, done
// here by MCPBTALayoutChildren rather than by touching the link array, which
// the compiler would re-sort anyway.
//
// UBehaviorTreeGraph::AutoArrange() is deliberately never called: it reads
// DEPRECATED_NodeWidget, the Slate widget that exists only while the graph is
// open in an editor tab, and would null-dereference on a headless call.

#include "GameplayHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Guid.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/Blackboard/BlackboardKey.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/Composites/BTComposite_SimpleParallel.h"
#include "BehaviorTree/Tasks/BTTask_RunBehavior.h"

#include "AIGraphTypes.h"
#include "BehaviorTreeEditorTypes.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraphNode_Composite.h"
#include "BehaviorTreeGraphNode_Decorator.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "BehaviorTreeGraphNode_Service.h"
#include "BehaviorTreeGraphNode_SimpleParallel.h"
#include "BehaviorTreeGraphNode_SubtreeTask.h"
#include "BehaviorTreeGraphNode_Task.h"
#include "EdGraphSchema_BehaviorTree.h"

namespace
{
	// Horizontal spacing between siblings. Only the relative order matters to
	// the compiler; the gap is what keeps the graph readable when a human opens
	// the asset afterwards.
	constexpr int32 MCPBTAChildSpacingX = 300;

	// How far below its parent a newly placed node sits.
	constexpr int32 MCPBTAChildOffsetY = 150;

	// Depth cap for the graph walk. A BehaviorTree graph is a tree, but a
	// hand-edited or partly corrupted asset is still one the bridge has to
	// answer about, so the walk terminates rather than recursing forever.
	constexpr int32 MCPBTAMaxDepth = 64;

	// How many node references an ambiguity error lists back to the caller.
	constexpr int32 MCPBTAMaxReported = 40;

	FString MCPBTAGuidString(const UEdGraphNode* Node)
	{
		return Node ? Node->NodeGuid.ToString(EGuidFormats::Digits) : FString();
	}

	// One editor-graph node, with everything a caller needs to address it and
	// everything the walk already established about where it sits.
	struct FMCPBTAEntry
	{
		UBehaviorTreeGraphNode* Node = nullptr;
		FString Category;
		UBehaviorTreeGraphNode* Parent = nullptr;
		/** Position among the parent's children / decorators / services. */
		int32 IndexInParent = INDEX_NONE;
		TArray<UBehaviorTreeGraphNode*> Children;
	};

	// The child graph nodes hanging off one node's output pins, in the order
	// the compiler will read them: pins in declaration order, and within a pin,
	// links sorted by node X position. A SimpleParallel declares two output
	// pins (the main task, then the background branch), which is why the pin
	// loop comes first and cannot be collapsed into a single pin lookup.
	//
	// The sort runs on a copy so that a read never reorders a caller's graph.
	void MCPBTACollectChildren(UBehaviorTreeGraphNode* Node, TArray<UBehaviorTreeGraphNode*>& Out)
	{
		Out.Reset();
		if (!Node) return;

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output) continue;

			TArray<UEdGraphPin*> Links = Pin->LinkedTo;
			Links.Sort(FCompareNodeXLocation());
			for (UEdGraphPin* Link : Links)
			{
				if (!Link) continue;
				if (UBehaviorTreeGraphNode* Child = Cast<UBehaviorTreeGraphNode>(Link->GetOwningNode()))
				{
					Out.Add(Child);
				}
			}
		}
	}

	// The graph's root node. UEdGraphSchema_BehaviorTree::CreateDefaultNodesForGraph
	// places exactly one, and UpdateAsset takes the first it finds, so this
	// matches what the compiler will treat as the root.
	UBehaviorTreeGraphNode_Root* MCPBTAFindRoot(UBehaviorTreeGraph* Graph)
	{
		if (!Graph) return nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UBehaviorTreeGraphNode_Root* Root = Cast<UBehaviorTreeGraphNode_Root>(Node))
			{
				return Root;
			}
		}
		return nullptr;
	}

	void MCPBTAWalk(UBehaviorTreeGraphNode* Node, UBehaviorTreeGraphNode* Parent,
		const FString& Category, int32 IndexInParent,
		TArray<FMCPBTAEntry>& Out, TSet<UBehaviorTreeGraphNode*>& Seen, int32 Depth)
	{
		if (!Node || Depth > MCPBTAMaxDepth) return;

		bool bAlreadySeen = false;
		Seen.Add(Node, &bAlreadySeen);
		if (bAlreadySeen) return;

		FMCPBTAEntry Entry;
		Entry.Node = Node;
		Entry.Category = Category;
		Entry.Parent = Parent;
		Entry.IndexInParent = IndexInParent;
		MCPBTACollectChildren(Node, Entry.Children);
		const TArray<UBehaviorTreeGraphNode*> Children = Entry.Children;
		Out.Add(MoveTemp(Entry));

		for (int32 d = 0; d < Node->Decorators.Num(); ++d)
		{
			MCPBTAWalk(Node->Decorators[d], Node, TEXT("decorator"), d, Out, Seen, Depth + 1);
		}
		for (int32 s = 0; s < Node->Services.Num(); ++s)
		{
			MCPBTAWalk(Node->Services[s], Node, TEXT("service"), s, Out, Seen, Depth + 1);
		}
		for (int32 c = 0; c < Children.Num(); ++c)
		{
			UBehaviorTreeGraphNode* Child = Children[c];
			const FString ChildCategory = (Child && Cast<UBTCompositeNode>(Child->NodeInstance))
				? TEXT("composite")
				: TEXT("task");
			MCPBTAWalk(Child, Node, ChildCategory, c, Out, Seen, Depth + 1);
		}
	}

	// Every node the compiler can see, walked from the root, followed by any
	// node the graph holds that the walk never reached. A disconnected node is
	// real authoring state (it is what a half-built tree looks like), and a
	// caller has to be able to address it in order to connect or delete it.
	void MCPBTACollectGraph(UBehaviorTreeGraph* Graph, TArray<FMCPBTAEntry>& Out)
	{
		Out.Reset();
		if (!Graph) return;

		TSet<UBehaviorTreeGraphNode*> Seen;
		if (UBehaviorTreeGraphNode_Root* Root = MCPBTAFindRoot(Graph))
		{
			MCPBTAWalk(Root, nullptr, TEXT("root"), INDEX_NONE, Out, Seen, 0);
		}

		for (UEdGraphNode* EdNode : Graph->Nodes)
		{
			UBehaviorTreeGraphNode* Node = Cast<UBehaviorTreeGraphNode>(EdNode);
			if (!Node || Seen.Contains(Node)) continue;

			// An orphan has no walked position, so its category comes from what
			// it is rather than from where it sits.
			FString Category = TEXT("node");
			if (Cast<UBTCompositeNode>(Node->NodeInstance)) Category = TEXT("composite");
			else if (Cast<UBTTaskNode>(Node->NodeInstance)) Category = TEXT("task");
			else if (Cast<UBTDecorator>(Node->NodeInstance)) Category = TEXT("decorator");
			else if (Cast<UBTService>(Node->NodeInstance)) Category = TEXT("service");

			MCPBTAWalk(Node, nullptr, Category, INDEX_NONE, Out, Seen, 0);
		}
	}

	const FMCPBTAEntry* MCPBTAFind(const TArray<FMCPBTAEntry>& Entries, const UBehaviorTreeGraphNode* Node)
	{
		for (const FMCPBTAEntry& Entry : Entries)
		{
			if (Entry.Node == Node) return &Entry;
		}
		return nullptr;
	}

	// What a caller could have passed, for an error that has to say so.
	FString MCPBTADescribe(const TArray<FMCPBTAEntry>& Entries)
	{
		TArray<FString> Parts;
		for (const FMCPBTAEntry& Entry : Entries)
		{
			if (Parts.Num() >= MCPBTAMaxReported) break;
			if (!Entry.Node) continue;
			const UObject* Instance = Entry.Node->NodeInstance;
			Parts.Add(FString::Printf(TEXT("%s (%s %s)"),
				*MCPBTAGuidString(Entry.Node),
				*Entry.Category,
				Instance ? *Instance->GetClass()->GetName() : TEXT("root")));
		}
		FString Joined = FString::Join(Parts, TEXT(", "));
		if (Entries.Num() > Parts.Num())
		{
			Joined += FString::Printf(TEXT(", and %d more"), Entries.Num() - Parts.Num());
		}
		return Joined;
	}

	// Resolve a caller's node reference against the graph.
	//
	// Guid is the primary key, because it survives every recompile and is
	// unaffected by inserting a sibling. The runtime address from
	// read_behavior_tree_graph is accepted too, so a caller who just read the
	// tree does not have to list the graph first, and it resolves by node
	// instance identity rather than by re-deriving the address, which keeps the
	// two surfaces from drifting. A bare name is last, and is rejected when it
	// matches more than one node.
	UBehaviorTreeGraphNode* MCPBTAResolve(
		const TArray<FMCPBTAEntry>& Entries,
		UBehaviorTree* Tree,
		const FString& Reference,
		FString& OutError)
	{
		const FString Spec = Reference.TrimStartAndEnd();
		if (Spec.IsEmpty())
		{
			OutError = TEXT("empty node reference");
			return nullptr;
		}

		if (Spec.Equals(TEXT("root"), ESearchCase::IgnoreCase))
		{
			for (const FMCPBTAEntry& Entry : Entries)
			{
				if (Entry.Category == TEXT("root")) return Entry.Node;
			}
			OutError = TEXT("this graph has no root node");
			return nullptr;
		}

		FGuid Parsed;
		if (FGuid::Parse(Spec, Parsed))
		{
			for (const FMCPBTAEntry& Entry : Entries)
			{
				if (Entry.Node && Entry.Node->NodeGuid == Parsed) return Entry.Node;
			}
			OutError = FString::Printf(TEXT("no graph node has guid %s"), *Spec);
			return nullptr;
		}

		// A structural address like "Root.Children[0].Decorators[1]".
		if (Spec.Contains(TEXT(".")) || Spec.Contains(TEXT("[")))
		{
			TMap<UBTNode*, FString> Addresses;
			FGameplayHandlers::MapBTNodeAddresses(Tree, Addresses);
			for (const FMCPBTAEntry& Entry : Entries)
			{
				UBTNode* Instance = Entry.Node ? Cast<UBTNode>(Entry.Node->NodeInstance) : nullptr;
				const FString* Address = Instance ? Addresses.Find(Instance) : nullptr;
				if (Address && Address->Equals(Spec, ESearchCase::IgnoreCase)) return Entry.Node;
			}
			OutError = FString::Printf(
				TEXT("no compiled node sits at '%s'. Addresses come from read_behavior_tree_graph and only "
					 "cover nodes reachable from the root; use a guid from list_bt_graph_nodes otherwise."),
				*Spec);
			return nullptr;
		}

		TArray<UBehaviorTreeGraphNode*> Matches;
		for (const FMCPBTAEntry& Entry : Entries)
		{
			UBTNode* Instance = Entry.Node ? Cast<UBTNode>(Entry.Node->NodeInstance) : nullptr;
			if (!Instance) continue;
			if (Instance->NodeName.Equals(Spec, ESearchCase::IgnoreCase) ||
				Instance->GetName().Equals(Spec, ESearchCase::IgnoreCase) ||
				Instance->GetClass()->GetName().Equals(Spec, ESearchCase::IgnoreCase))
			{
				Matches.Add(Entry.Node);
			}
		}
		if (Matches.Num() == 1) return Matches[0];
		if (Matches.Num() > 1)
		{
			TArray<FString> Guids;
			for (UBehaviorTreeGraphNode* Match : Matches) Guids.Add(MCPBTAGuidString(Match));
			OutError = FString::Printf(
				TEXT("'%s' matches %d nodes; pass one of these guids instead: %s"),
				*Spec, Matches.Num(), *FString::Join(Guids, TEXT(", ")));
			return nullptr;
		}

		OutError = FString::Printf(
			TEXT("no graph node matched '%s'. Available: %s"), *Spec, *MCPBTADescribe(Entries));
		return nullptr;
	}

	// Get the asset's editor graph, creating and seeding it when the asset has
	// never been opened in the BehaviorTree editor. gameplay(create_behavior_tree)
	// makes a bare UBehaviorTree with no graph at all, so this is the normal
	// path for a tree the bridge itself authored, not an edge case.
	UBehaviorTreeGraph* MCPBTAGetOrCreateGraph(UBehaviorTree* Tree, bool& bOutCreated)
	{
		bOutCreated = false;
		if (!Tree) return nullptr;

		if (UBehaviorTreeGraph* Existing = Cast<UBehaviorTreeGraph>(Tree->BTGraph))
		{
			return Existing;
		}

		Tree->Modify();
		Tree->BTGraph = FBlueprintEditorUtils::CreateNewGraph(
			Tree,
			FName(TEXT("Behavior Tree")),
			UBehaviorTreeGraph::StaticClass(),
			UEdGraphSchema_BehaviorTree::StaticClass());

		UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(Tree->BTGraph);
		if (!Graph) return nullptr;

		if (const UEdGraphSchema* Schema = Graph->GetSchema())
		{
			Schema->CreateDefaultNodesForGraph(*Graph);
		}
		Graph->OnCreated();
		bOutCreated = true;
		return Graph;
	}

	// The editor-graph node class that carries a given runtime node class.
	//
	// The engine's own menu builder compares class names exactly. IsChildOf is
	// used here instead, because the distinction is structural rather than
	// cosmetic: a SimpleParallel subclass still needs the two output pins that
	// UBehaviorTreeGraphNode_SimpleParallel allocates, and giving it the plain
	// composite node would leave its background branch unauthorable.
	UClass* MCPBTAGraphNodeClassFor(UClass* RuntimeClass, const FString& Category)
	{
		if (Category == TEXT("composite"))
		{
			return RuntimeClass && RuntimeClass->IsChildOf(UBTComposite_SimpleParallel::StaticClass())
				? UBehaviorTreeGraphNode_SimpleParallel::StaticClass()
				: UBehaviorTreeGraphNode_Composite::StaticClass();
		}
		if (Category == TEXT("task"))
		{
			return RuntimeClass && RuntimeClass->IsChildOf(UBTTask_RunBehavior::StaticClass())
				? UBehaviorTreeGraphNode_SubtreeTask::StaticClass()
				: UBehaviorTreeGraphNode_Task::StaticClass();
		}
		if (Category == TEXT("decorator")) return UBehaviorTreeGraphNode_Decorator::StaticClass();
		if (Category == TEXT("service")) return UBehaviorTreeGraphNode_Service::StaticClass();
		return nullptr;
	}

	/** The runtime base class a category requires, for validating nodeClass. */
	UClass* MCPBTARuntimeBaseFor(const FString& Category)
	{
		if (Category == TEXT("composite")) return UBTCompositeNode::StaticClass();
		if (Category == TEXT("task")) return UBTTaskNode::StaticClass();
		if (Category == TEXT("decorator")) return UBTDecorator::StaticClass();
		if (Category == TEXT("service")) return UBTService::StaticClass();
		return nullptr;
	}

	/** Which category a runtime node class belongs to, or empty. */
	FString MCPBTACategoryOf(UClass* RuntimeClass)
	{
		if (!RuntimeClass) return FString();
		if (RuntimeClass->IsChildOf(UBTCompositeNode::StaticClass())) return TEXT("composite");
		if (RuntimeClass->IsChildOf(UBTTaskNode::StaticClass())) return TEXT("task");
		if (RuntimeClass->IsChildOf(UBTDecorator::StaticClass())) return TEXT("decorator");
		if (RuntimeClass->IsChildOf(UBTService::StaticClass())) return TEXT("service");
		return FString();
	}

	/** Number of output pins a node declares. Two means a SimpleParallel. */
	int32 MCPBTAOutputPinCount(const UBehaviorTreeGraphNode* Node)
	{
		int32 Count = 0;
		if (!Node) return 0;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output) ++Count;
		}
		return Count;
	}

	// Rewrite sibling X positions so that Ordered[i] is the i-th child at
	// compile time. This is the whole of reordering: BTGraphHelpers::CreateChildren
	// sorts each output pin's links by X, so the position IS the order and
	// editing the link array would not survive the next compile.
	void MCPBTALayoutChildren(UBehaviorTreeGraphNode* Parent, const TArray<UBehaviorTreeGraphNode*>& Ordered)
	{
		if (!Parent) return;
		const int32 Count = Ordered.Num();
		const int32 FirstX = Parent->NodePosX - ((Count - 1) * MCPBTAChildSpacingX) / 2;
		for (int32 i = 0; i < Count; ++i)
		{
			UBehaviorTreeGraphNode* Child = Ordered[i];
			if (!Child) continue;
			Child->Modify();
			Child->NodePosX = FirstX + i * MCPBTAChildSpacingX;
			if (Child->NodePosY <= Parent->NodePosY)
			{
				Child->NodePosY = Parent->NodePosY + MCPBTAChildOffsetY;
			}
		}
	}

	// Is Candidate at or below Node? Used before a reparent, because the
	// schema's own cycle check walks input pins upward and cannot see the loop
	// once the moved node's old parent link is gone.
	bool MCPBTAIsInSubtree(UBehaviorTreeGraphNode* Node, UBehaviorTreeGraphNode* Candidate, int32 Depth = 0)
	{
		if (!Node || !Candidate || Depth > MCPBTAMaxDepth) return false;
		if (Node == Candidate) return true;

		TArray<UBehaviorTreeGraphNode*> Children;
		MCPBTACollectChildren(Node, Children);
		for (UBehaviorTreeGraphNode* Child : Children)
		{
			if (MCPBTAIsInSubtree(Child, Candidate, Depth + 1)) return true;
		}
		for (UBehaviorTreeGraphNode* Sub : Node->Decorators)
		{
			if (Sub == Candidate) return true;
		}
		for (UBehaviorTreeGraphNode* Sub : Node->Services)
		{
			if (Sub == Candidate) return true;
		}
		return false;
	}

	// Apply a properties map onto a node instance, reusing the same writer the
	// scoped set_bt_node_property write uses. That matters for more than tidiness:
	// on UE 5.8 BTTask_MoveTo::AcceptableRadius and BTTask_Wait::WaitTime are
	// FValueOrBBKey_Float structs rather than floats, and the shared writer is
	// where a bare JSON number is retargeted onto the struct's DefaultValue
	// instead of failing as "ImportText failed for '60'".
	bool MCPBTAApplyProperties(UBTNode* Instance, const TSharedPtr<FJsonObject>& Properties, FString& OutError)
	{
		if (!Instance || !Properties.IsValid()) return true;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
		{
			FString WriteError;
			if (!FGameplayHandlers::WriteBTNodeProperty(Instance, Pair.Key, Pair.Value, WriteError))
			{
				OutError = FString::Printf(TEXT("%s on %s: %s"),
					*Pair.Key, *Instance->GetClass()->GetName(), *WriteError);
				return false;
			}
		}
		return true;
	}

	// Point a node's FBlackboardKeySelector fields at named blackboard keys.
	//
	// A selector carries both the key name and the resolved key id, and only
	// the name can be written directly. The id is filled in by
	// InitializeFromAsset against the tree's blackboard, which is why the caller
	// gets a clear error here when the key is not on the blackboard: the write
	// would otherwise succeed and the node would silently never fire.
	bool MCPBTAApplyBlackboardKeys(
		UBTNode* Instance,
		UBehaviorTree* Tree,
		const TSharedPtr<FJsonObject>& Keys,
		TArray<TSharedPtr<FJsonValue>>& OutApplied,
		FString& OutError)
	{
		if (!Instance || !Keys.IsValid()) return true;

		UClass* NodeClass = Instance->GetClass();
		UBlackboardData* Blackboard = Tree ? Tree->BlackboardAsset.Get() : nullptr;

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Keys->Values)
		{
			FString KeyName;
			if (!Pair.Value.IsValid() || !Pair.Value->TryGetString(KeyName))
			{
				OutError = FString::Printf(TEXT("blackboardKeys['%s'] must be a blackboard key name"), *Pair.Key);
				return false;
			}

			FStructProperty* Prop = CastField<FStructProperty>(NodeClass->FindPropertyByName(FName(*Pair.Key)));
			if (!Prop || Prop->Struct != FBlackboardKeySelector::StaticStruct())
			{
				OutError = FString::Printf(
					TEXT("%s has no FBlackboardKeySelector property named '%s'"),
					*NodeClass->GetName(), *Pair.Key);
				return false;
			}

			// FBlackboard::FKey has no IsValid(): it compares against the
			// namespace sentinel, and does so in both the 16-bit default and
			// the AI_BLACKBOARD_KEY_SIZE_8 legacy typedef.
			if (Blackboard && Blackboard->GetKeyID(FName(*KeyName)) == FBlackboard::InvalidKey)
			{
				OutError = FString::Printf(
					TEXT("blackboard %s has no key '%s' for %s.%s"),
					*Blackboard->GetName(), *KeyName, *NodeClass->GetName(), *Pair.Key);
				return false;
			}

			void* Addr = Prop->ContainerPtrToValuePtr<void>(Instance);
			FBlackboardKeySelector* Selector = static_cast<FBlackboardKeySelector*>(Addr);
			Selector->SelectedKeyName = FName(*KeyName);
			Selector->InvalidateResolvedKey();
			if (Blackboard)
			{
				Selector->ResolveSelectedKey(*Blackboard);
			}

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("property"), Pair.Key);
			Entry->SetStringField(TEXT("selectedKeyName"), KeyName);
			Entry->SetBoolField(TEXT("resolved"), Selector->IsSet());
			OutApplied.Add(MakeShared<FJsonValueObject>(Entry));
		}
		return true;
	}

	/** One editor-graph node as JSON. Addresses come out alongside the guid. */
	TSharedPtr<FJsonObject> MCPBTADescribeEntry(
		const FMCPBTAEntry& Entry,
		const TMap<UBTNode*, FString>& Addresses)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		UBehaviorTreeGraphNode* Node = Entry.Node;
		if (!Node) return Obj;

		Obj->SetStringField(TEXT("guid"), MCPBTAGuidString(Node));
		Obj->SetStringField(TEXT("category"), Entry.Category);
		Obj->SetStringField(TEXT("parentGuid"), MCPBTAGuidString(Entry.Parent));
		Obj->SetNumberField(TEXT("indexInParent"), Entry.IndexInParent);
		Obj->SetStringField(TEXT("graphNodeClass"), Node->GetClass()->GetName());
		Obj->SetNumberField(TEXT("posX"), Node->NodePosX);
		Obj->SetNumberField(TEXT("posY"), Node->NodePosY);
		Obj->SetBoolField(TEXT("injected"), Node->bInjectedNode != 0);
		Obj->SetStringField(TEXT("errorMessage"), Node->ErrorMessage);

		UBTNode* Instance = Cast<UBTNode>(Node->NodeInstance);
		Obj->SetStringField(TEXT("class"), Instance ? Instance->GetClass()->GetName() : FString());
		Obj->SetStringField(TEXT("classPath"), Instance ? Instance->GetClass()->GetPathName() : FString());
		Obj->SetStringField(TEXT("nodeName"), Instance ? Instance->NodeName : FString());
		Obj->SetStringField(TEXT("objectName"), Instance ? Instance->GetName() : FString());

		// The address the read surface reports for this node, so a caller can
		// hand a guid to the authoring calls and the same node's address to
		// read_bt_node_properties without listing anything twice. Empty means
		// the node is not currently reachable from the compiled root.
		const FString* Address = Instance ? Addresses.Find(Instance) : nullptr;
		Obj->SetStringField(TEXT("runtimePath"), Address ? *Address : FString());

		// Generic because Decorators and Services are TArray<TObjectPtr<>>
		// while the walked child list is a plain pointer array.
		auto GuidArray = [](const auto& Nodes)
		{
			TArray<TSharedPtr<FJsonValue>> Out;
			for (const UBehaviorTreeGraphNode* Item : Nodes)
			{
				if (Item) Out.Add(MakeShared<FJsonValueString>(MCPBTAGuidString(Item)));
			}
			return Out;
		};

		Obj->SetArrayField(TEXT("childGuids"), GuidArray(Entry.Children));
		Obj->SetArrayField(TEXT("decoratorGuids"), GuidArray(Node->Decorators));
		Obj->SetArrayField(TEXT("serviceGuids"), GuidArray(Node->Services));
		return Obj;
	}

	/** Recompile the graph into the runnable tree and persist the asset. */
	bool MCPBTACompileAndSave(UBehaviorTree* Tree, UBehaviorTreeGraph* Graph)
	{
		if (!Graph || !Tree) return false;
		Graph->Modify();
		Graph->UpdateAsset();
		// A BehaviorTree editor tab open on this asset is listening. Without the
		// notification it keeps drawing the graph as it was before the call,
		// which reads as the call having done nothing.
		Graph->NotifyGraphChanged();
		Tree->PostEditChange();
		Tree->MarkPackageDirty();
		return SaveAssetPackage(Tree);
	}

	/** Load the tree and its graph, or return the error a handler should emit. */
	TSharedPtr<FJsonValue> MCPBTAOpen(
		const TSharedPtr<FJsonObject>& Params,
		bool bCreateGraph,
		FString& OutAssetPath,
		UBehaviorTree*& OutTree,
		UBehaviorTreeGraph*& OutGraph,
		bool& bOutGraphCreated)
	{
		OutTree = nullptr;
		OutGraph = nullptr;
		bOutGraphCreated = false;

		if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), OutAssetPath)) return Err;

		OutTree = FGameplayHandlers::LoadBehaviorTree(OutAssetPath);
		if (!OutTree)
		{
			return MCPError(FString::Printf(TEXT("BehaviorTree not found: %s"), *OutAssetPath));
		}

		if (bCreateGraph)
		{
			OutGraph = MCPBTAGetOrCreateGraph(OutTree, bOutGraphCreated);
			if (!OutGraph)
			{
				return MCPError(FString::Printf(
					TEXT("could not create an editor graph for %s"), *OutAssetPath));
			}
		}
		else
		{
			OutGraph = Cast<UBehaviorTreeGraph>(OutTree->BTGraph);
		}
		return nullptr;
	}
}

// -----------------------------------------------------------------
// list_bt_graph_nodes (#889)
// -----------------------------------------------------------------

TSharedPtr<FJsonValue> FGameplayHandlers::ListBTGraphNodes(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBehaviorTree* Tree = nullptr;
	UBehaviorTreeGraph* Graph = nullptr;
	bool bGraphCreated = false;
	if (auto Err = MCPBTAOpen(Params, /*bCreateGraph*/ false, AssetPath, Tree, Graph, bGraphCreated)) return Err;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetBoolField(TEXT("hasGraph"), Graph != nullptr);

	if (!Graph)
	{
		// A tree created by gameplay(create_behavior_tree) and never opened has
		// no graph yet. That is an answer, not a failure: add_bt_node seeds one.
		Result->SetNumberField(TEXT("nodeCount"), 0);
		Result->SetArrayField(TEXT("nodes"), TArray<TSharedPtr<FJsonValue>>());
		Result->SetStringField(TEXT("note"),
			TEXT("This BehaviorTree has no editor graph yet. add_bt_node creates one with its root node."));
		return MCPResult(Result);
	}

	TArray<FMCPBTAEntry> Entries;
	MCPBTACollectGraph(Graph, Entries);

	TMap<UBTNode*, FString> Addresses;
	MapBTNodeAddresses(Tree, Addresses);

	TArray<TSharedPtr<FJsonValue>> Nodes;
	for (const FMCPBTAEntry& Entry : Entries)
	{
		Nodes.Add(MakeShared<FJsonValueObject>(MCPBTADescribeEntry(Entry, Addresses)));
	}

	Result->SetStringField(TEXT("rootGuid"), MCPBTAGuidString(MCPBTAFindRoot(Graph)));
	Result->SetStringField(TEXT("blackboardAsset"),
		Tree->BlackboardAsset ? Tree->BlackboardAsset->GetPathName() : FString());
	Result->SetNumberField(TEXT("nodeCount"), Nodes.Num());
	Result->SetArrayField(TEXT("nodes"), Nodes);
	return MCPResult(Result);
}

// -----------------------------------------------------------------
// add_bt_node (#889)
// -----------------------------------------------------------------

TSharedPtr<FJsonValue> FGameplayHandlers::AddBTNode(const TSharedPtr<FJsonObject>& Params)
{
	// The graph is seeded only after every argument has been checked. Seeding
	// it is a change to the asset, and a call that is going to be rejected for
	// a bad nodeClass should leave the tree exactly as it found it.
	FString AssetPath;
	UBehaviorTree* Tree = nullptr;
	UBehaviorTreeGraph* Graph = nullptr;
	bool bGraphCreated = false;
	if (auto Err = MCPBTAOpen(Params, /*bCreateGraph*/ false, AssetPath, Tree, Graph, bGraphCreated)) return Err;

	FString NodeClassSpec;
	if (auto Err = RequireString(Params, TEXT("nodeClass"), NodeClassSpec)) return Err;

	UClass* RuntimeClass = MCPResolveClass(NodeClassSpec.TrimStartAndEnd(), /*bAllowLoad*/ true);
	if (!RuntimeClass)
	{
		return MCPError(FString::Printf(
			TEXT("BT node class not found: %s. list_bt_node_classes enumerates every class this build offers."),
			*NodeClassSpec));
	}
	if (RuntimeClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return MCPError(FString::Printf(
			TEXT("%s is abstract and cannot be placed. Pick one of its concrete subclasses."),
			*RuntimeClass->GetName()));
	}

	const FString DerivedCategory = MCPBTACategoryOf(RuntimeClass);
	if (DerivedCategory.IsEmpty())
	{
		return MCPError(FString::Printf(
			TEXT("%s is not a BehaviorTree node class. Expected a subclass of UBTCompositeNode, "
				 "UBTTaskNode, UBTDecorator or UBTService."),
			*RuntimeClass->GetName()));
	}

	FString Category = OptionalString(Params, TEXT("nodeCategory")).TrimStartAndEnd().ToLower();
	if (Category.IsEmpty())
	{
		Category = DerivedCategory;
	}
	else if (Category != DerivedCategory)
	{
		return MCPError(FString::Printf(
			TEXT("nodeCategory '%s' does not match %s, which is a %s."),
			*Category, *RuntimeClass->GetName(), *DerivedCategory));
	}

	UClass* GraphNodeClass = MCPBTAGraphNodeClassFor(RuntimeClass, Category);
	if (!GraphNodeClass)
	{
		return MCPError(FString::Printf(TEXT("unsupported nodeCategory '%s'"), *Category));
	}

	Graph = MCPBTAGetOrCreateGraph(Tree, bGraphCreated);
	if (!Graph)
	{
		return MCPError(FString::Printf(
			TEXT("could not create an editor graph for %s"), *AssetPath));
	}

	TArray<FMCPBTAEntry> Entries;
	MCPBTACollectGraph(Graph, Entries);

	const FString ParentSpec = OptionalString(Params, TEXT("parent"), TEXT("root"));
	FString ResolveError;
	UBehaviorTreeGraphNode* Parent = MCPBTAResolve(Entries, Tree, ParentSpec, ResolveError);
	if (!Parent) return MCPError(FString::Printf(TEXT("parent: %s"), *ResolveError));

	const bool bParentIsRoot = Parent->IsA<UBehaviorTreeGraphNode_Root>();
	const int32 RequestedIndex = OptionalInt(Params, TEXT("index"), INDEX_NONE);

	if ((Category == TEXT("decorator") || Category == TEXT("service")) &&
		(Parent->IsA<UBehaviorTreeGraphNode_Decorator>() || Parent->IsA<UBehaviorTreeGraphNode_Service>()))
	{
		return MCPError(TEXT(
			"decorators and services attach to a composite, a task or the root, never to another subnode."));
	}
	if (Category == TEXT("service") && bParentIsRoot)
	{
		return MCPError(TEXT(
			"the root node carries decorators only. Attach a service to the composite beneath it."));
	}

	Tree->Modify();
	Graph->Modify();

	UBehaviorTreeGraphNode* NewNode = nullptr;
	int32 PlacedIndex = INDEX_NONE;

	if (Category == TEXT("decorator") || Category == TEXT("service"))
	{
		// A subnode is owned by its parent graph node, not held in Graph->Nodes,
		// so it is constructed against the graph and handed to AddSubNode, which
		// does the guid, the instance spawn and the parent bookkeeping.
		UBehaviorTreeGraphNode* SubNode = NewObject<UBehaviorTreeGraphNode>(Graph, GraphNodeClass);
		SubNode->ClassData = FGraphNodeClassData(RuntimeClass, FString());
		Parent->AddSubNode(SubNode, Graph);
		NewNode = SubNode;

		TArray<TObjectPtr<UBehaviorTreeGraphNode>>& Bucket =
			(Category == TEXT("decorator")) ? Parent->Decorators : Parent->Services;
		PlacedIndex = Bucket.IndexOfByKey(SubNode);
		if (RequestedIndex >= 0 && PlacedIndex != INDEX_NONE)
		{
			const int32 Target = FMath::Clamp(RequestedIndex, 0, Bucket.Num() - 1);
			Bucket.RemoveAt(PlacedIndex);
			Bucket.Insert(SubNode, Target);
			PlacedIndex = Target;
		}
	}
	else
	{
		if (bParentIsRoot && Category == TEXT("task"))
		{
			return MCPError(TEXT(
				"the root node accepts a composite only. Add a Selector or Sequence first, then the task under it."));
		}

		UBehaviorTreeGraphNode* Created = nullptr;
		{
			FGraphNodeCreator<UBehaviorTreeGraphNode> Creator(*Graph);
			Created = Creator.CreateNode(/*bSelectNewNode*/ false, GraphNodeClass);
			if (Created)
			{
				// ClassData has to be in place before Finalize: PostPlacedNewNode
				// reads it to spawn the UBTNode instance under the asset.
				Created->ClassData = FGraphNodeClassData(RuntimeClass, FString());
			}
			Creator.Finalize();
		}
		if (!Created || !Created->NodeInstance)
		{
			if (Created) Created->DestroyNode();
			return MCPError(FString::Printf(
				TEXT("could not instance %s under %s."), *RuntimeClass->GetName(), *AssetPath));
		}

		// Which output pin. A SimpleParallel declares two (the main task, then
		// the background branch), and index picks between them rather than
		// ordering siblings, because each of those pins holds a single link.
		const int32 OutputPinCount = MCPBTAOutputPinCount(Parent);
		const int32 PinIndex = (OutputPinCount > 1 && RequestedIndex >= 0)
			? FMath::Clamp(RequestedIndex, 0, OutputPinCount - 1)
			: 0;

		UEdGraphPin* ParentPin = Parent->GetOutputPin(PinIndex);
		UEdGraphPin* ChildPin = Created->GetInputPin(0);
		const UEdGraphSchema* Schema = Graph->GetSchema();
		if (!ParentPin || !ChildPin || !Schema)
		{
			Created->DestroyNode();
			return MCPError(TEXT("the parent or the new node has no pin to connect."));
		}

		// The schema decides. Its CanCreateConnection is what enforces
		// composite-only under the root, one parent per child, and no cycles,
		// and its response is what says whether an existing link is replaced.
		const FPinConnectionResponse Response = Schema->CanCreateConnection(ParentPin, ChildPin);
		if (Response.Response == CONNECT_RESPONSE_DISALLOW || !Schema->TryCreateConnection(ParentPin, ChildPin))
		{
			const FString ParentName = Parent->NodeInstance
				? Parent->NodeInstance->GetClass()->GetName()
				: FString(TEXT("the root"));
			const FString Reason = Response.Message.IsEmpty()
				? FString(TEXT("the BehaviorTree schema rejected the link"))
				: Response.Message.ToString();
			Created->DestroyNode();
			return MCPError(FString::Printf(
				TEXT("%s cannot be a child of %s: %s"),
				*RuntimeClass->GetName(), *ParentName, *Reason));
		}

		NewNode = Created;

		Created->NodePosY = Parent->NodePosY + MCPBTAChildOffsetY;
		if (OutputPinCount > 1)
		{
			Created->NodePosX = Parent->NodePosX + (PinIndex - 1) * MCPBTAChildSpacingX;
			PlacedIndex = PinIndex;
		}
		else
		{
			TArray<UBehaviorTreeGraphNode*> Siblings;
			MCPBTACollectChildren(Parent, Siblings);
			Siblings.Remove(Created);
			const int32 Target = (RequestedIndex >= 0)
				? FMath::Clamp(RequestedIndex, 0, Siblings.Num())
				: Siblings.Num();
			Siblings.Insert(Created, Target);
			MCPBTALayoutChildren(Parent, Siblings);
			PlacedIndex = Target;
		}
	}

	UBTNode* Instance = Cast<UBTNode>(NewNode->NodeInstance);
	if (!Instance)
	{
		NewNode->DestroyNode();
		return MCPError(FString::Printf(
			TEXT("%s produced no runtime node instance."), *RuntimeClass->GetName()));
	}

	const FString NodeName = OptionalString(Params, TEXT("nodeName")).TrimStartAndEnd();
	if (!NodeName.IsEmpty())
	{
		Instance->NodeName = NodeName;
	}

	const TSharedPtr<FJsonObject>* PropertiesObj = nullptr;
	if (Params->TryGetObjectField(TEXT("properties"), PropertiesObj) && PropertiesObj)
	{
		FString WriteError;
		if (!MCPBTAApplyProperties(Instance, *PropertiesObj, WriteError))
		{
			// The node never became part of a compiled tree, so removing it
			// leaves the asset exactly as the call found it.
			NewNode->DestroyNode();
			Graph->UpdateAsset();
			return MCPError(WriteError);
		}
	}

	TArray<TSharedPtr<FJsonValue>> AppliedKeys;
	const TSharedPtr<FJsonObject>* KeysObj = nullptr;
	if (Params->TryGetObjectField(TEXT("blackboardKeys"), KeysObj) && KeysObj)
	{
		FString KeyError;
		if (!MCPBTAApplyBlackboardKeys(Instance, Tree, *KeysObj, AppliedKeys, KeyError))
		{
			NewNode->DestroyNode();
			Graph->UpdateAsset();
			return MCPError(KeyError);
		}
	}

	// Resolve the node's own selectors against the tree's blackboard before the
	// compile, so a node that binds a key is already valid when UpdateAsset
	// walks it. UpdateAsset re-runs this for every node afterwards; doing it
	// here means a project node that inspects its own key during compile sees
	// a resolved one.
	Instance->InitializeFromAsset(*Tree);

	const bool bSaved = MCPBTACompileAndSave(Tree, Graph);

	TArray<FMCPBTAEntry> AfterEntries;
	MCPBTACollectGraph(Graph, AfterEntries);
	TMap<UBTNode*, FString> Addresses;
	MapBTNodeAddresses(Tree, Addresses);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetBoolField(TEXT("graphCreated"), bGraphCreated);
	Result->SetStringField(TEXT("guid"), MCPBTAGuidString(NewNode));
	Result->SetStringField(TEXT("parentGuid"), MCPBTAGuidString(Parent));
	Result->SetStringField(TEXT("category"), Category);
	Result->SetStringField(TEXT("class"), RuntimeClass->GetName());
	Result->SetStringField(TEXT("classPath"), RuntimeClass->GetPathName());
	Result->SetNumberField(TEXT("index"), PlacedIndex);
	Result->SetBoolField(TEXT("saved"), bSaved);
	if (AppliedKeys.Num() > 0) Result->SetArrayField(TEXT("blackboardKeys"), AppliedKeys);

	if (const FMCPBTAEntry* Entry = MCPBTAFind(AfterEntries, NewNode))
	{
		Result->SetObjectField(TEXT("node"), MCPBTADescribeEntry(*Entry, Addresses));
	}

	// The compiled tree is what a caller checks the call against, so the node
	// count and the new node's address come back rather than being assumed.
	Result->SetNumberField(TEXT("compiledNodeCount"), Addresses.Num());

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("node"), MCPBTAGuidString(NewNode));
	MCPSetRollback(Result, TEXT("remove_bt_node"), Payload);
	return MCPResult(Result);
}

// -----------------------------------------------------------------
// move_bt_node - reparent and reorder (#947)
// -----------------------------------------------------------------

TSharedPtr<FJsonValue> FGameplayHandlers::MoveBTNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBehaviorTree* Tree = nullptr;
	UBehaviorTreeGraph* Graph = nullptr;
	bool bGraphCreated = false;
	if (auto Err = MCPBTAOpen(Params, /*bCreateGraph*/ false, AssetPath, Tree, Graph, bGraphCreated)) return Err;
	if (!Graph)
	{
		return MCPError(FString::Printf(
			TEXT("%s has no editor graph, so it has no nodes to move."), *AssetPath));
	}

	FString NodeSpec;
	if (auto Err = RequireStringAlt(Params, TEXT("node"), TEXT("nodePath"), NodeSpec)) return Err;

	TArray<FMCPBTAEntry> Entries;
	MCPBTACollectGraph(Graph, Entries);

	FString ResolveError;
	UBehaviorTreeGraphNode* Node = MCPBTAResolve(Entries, Tree, NodeSpec, ResolveError);
	if (!Node) return MCPError(FString::Printf(TEXT("node: %s"), *ResolveError));
	if (Node->IsA<UBehaviorTreeGraphNode_Root>())
	{
		return MCPError(TEXT("the root node has no parent and cannot be moved."));
	}

	const FMCPBTAEntry* Before = MCPBTAFind(Entries, Node);
	UBehaviorTreeGraphNode* OldParent = Before ? Before->Parent : nullptr;
	const int32 OldIndex = Before ? Before->IndexInParent : INDEX_NONE;
	const FString Category = Before ? Before->Category : FString();
	const bool bIsSubNode = (Category == TEXT("decorator") || Category == TEXT("service"));

	const FString ParentSpec = OptionalString(Params, TEXT("parent")).TrimStartAndEnd();
	const bool bHasIndex = Params.IsValid() && Params->HasField(TEXT("index"));
	const int32 RequestedIndex = OptionalInt(Params, TEXT("index"), INDEX_NONE);
	if (ParentSpec.IsEmpty() && !bHasIndex)
	{
		return MCPError(TEXT("pass 'parent' to reconnect the node, 'index' to reorder it, or both."));
	}

	UBehaviorTreeGraphNode* NewParent = OldParent;
	if (!ParentSpec.IsEmpty())
	{
		NewParent = MCPBTAResolve(Entries, Tree, ParentSpec, ResolveError);
		if (!NewParent) return MCPError(FString::Printf(TEXT("parent: %s"), *ResolveError));
	}
	if (!NewParent)
	{
		return MCPError(TEXT(
			"this node is not attached to anything, so 'parent' has to say where it should go."));
	}

	// The schema's cycle check walks input pins upward from the new parent. It
	// cannot see the loop that reparenting under a descendant would create,
	// because the moved node's own input link is about to be broken, so the
	// subtree test happens here instead.
	if (MCPBTAIsInSubtree(Node, NewParent))
	{
		return MCPError(TEXT("a node cannot be moved under itself or one of its own descendants."));
	}

	Tree->Modify();
	Graph->Modify();
	Node->Modify();

	int32 PlacedIndex = INDEX_NONE;

	if (bIsSubNode)
	{
		if (NewParent->IsA<UBehaviorTreeGraphNode_Decorator>() || NewParent->IsA<UBehaviorTreeGraphNode_Service>())
		{
			return MCPError(TEXT(
				"decorators and services attach to a composite, a task or the root, never to another subnode."));
		}
		if (Category == TEXT("service") && NewParent->IsA<UBehaviorTreeGraphNode_Root>())
		{
			return MCPError(TEXT(
				"the root node carries decorators only. Move the service onto the composite beneath it."));
		}

		if (OldParent && OldParent != NewParent)
		{
			OldParent->Modify();
			OldParent->RemoveSubNode(Node);
			NewParent->Modify();
			NewParent->AddSubNode(Node, Graph);
		}

		TArray<TObjectPtr<UBehaviorTreeGraphNode>>& Bucket =
			(Category == TEXT("decorator")) ? NewParent->Decorators : NewParent->Services;
		PlacedIndex = Bucket.IndexOfByKey(Node);
		if (bHasIndex && RequestedIndex >= 0 && PlacedIndex != INDEX_NONE)
		{
			const int32 Target = FMath::Clamp(RequestedIndex, 0, Bucket.Num() - 1);
			Bucket.RemoveAt(PlacedIndex);
			Bucket.Insert(Node, Target);
			PlacedIndex = Target;
		}
	}
	else
	{
		const int32 OutputPinCount = MCPBTAOutputPinCount(NewParent);
		const int32 PinIndex = (OutputPinCount > 1 && RequestedIndex >= 0)
			? FMath::Clamp(RequestedIndex, 0, OutputPinCount - 1)
			: 0;

		UEdGraphPin* ChildPin = Node->GetInputPin(0);
		UEdGraphPin* ParentPin = NewParent->GetOutputPin(PinIndex);
		const UEdGraphSchema* Schema = Graph->GetSchema();
		if (!ChildPin || !ParentPin || !Schema)
		{
			return MCPError(TEXT("the node or its new parent has no pin to connect."));
		}

		const bool bReparenting = (NewParent != OldParent) || (OutputPinCount > 1);
		if (bReparenting)
		{
			const FPinConnectionResponse Response = Schema->CanCreateConnection(ParentPin, ChildPin);
			if (Response.Response == CONNECT_RESPONSE_DISALLOW)
			{
				return MCPError(FString::Printf(
					TEXT("cannot reconnect: %s"), *Response.Message.ToString()));
			}

			// Break the old link explicitly. CanCreateConnection reports
			// BREAK_OTHERS for the child's single input pin, but only once the
			// link is gone is the parent's child list correct for the reorder
			// that follows.
			ChildPin->BreakAllPinLinks(true);
			if (!Schema->TryCreateConnection(ParentPin, ChildPin))
			{
				const FString ParentName = NewParent->NodeInstance
					? NewParent->NodeInstance->GetClass()->GetName()
					: FString(TEXT("the root"));
				return MCPError(FString::Printf(
					TEXT("the BehaviorTree schema refused to link this node under %s."), *ParentName));
			}
		}

		if (OutputPinCount > 1)
		{
			Node->NodePosX = NewParent->NodePosX + (PinIndex - 1) * MCPBTAChildSpacingX;
			Node->NodePosY = NewParent->NodePosY + MCPBTAChildOffsetY;
			PlacedIndex = PinIndex;
		}
		else
		{
			TArray<UBehaviorTreeGraphNode*> Siblings;
			MCPBTACollectChildren(NewParent, Siblings);
			Siblings.Remove(Node);
			const int32 Target = (bHasIndex && RequestedIndex >= 0)
				? FMath::Clamp(RequestedIndex, 0, Siblings.Num())
				: FMath::Min(OldIndex >= 0 ? OldIndex : Siblings.Num(), Siblings.Num());
			Siblings.Insert(Node, Target);
			MCPBTALayoutChildren(NewParent, Siblings);
			PlacedIndex = Target;
		}
	}

	const bool bSaved = MCPBTACompileAndSave(Tree, Graph);

	TArray<FMCPBTAEntry> AfterEntries;
	MCPBTACollectGraph(Graph, AfterEntries);
	TMap<UBTNode*, FString> Addresses;
	MapBTNodeAddresses(Tree, Addresses);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("guid"), MCPBTAGuidString(Node));
	Result->SetStringField(TEXT("category"), Category);
	Result->SetStringField(TEXT("previousParentGuid"), MCPBTAGuidString(OldParent));
	Result->SetNumberField(TEXT("previousIndex"), OldIndex);
	Result->SetStringField(TEXT("parentGuid"), MCPBTAGuidString(NewParent));
	Result->SetNumberField(TEXT("index"), PlacedIndex);
	Result->SetBoolField(TEXT("saved"), bSaved);

	if (const FMCPBTAEntry* Entry = MCPBTAFind(AfterEntries, Node))
	{
		Result->SetObjectField(TEXT("node"), MCPBTADescribeEntry(*Entry, Addresses));
	}

	// The sibling order after the compile, which is the thing a Selector or a
	// Sequence is defined by and the thing this call exists to change.
	if (const FMCPBTAEntry* ParentEntry = MCPBTAFind(AfterEntries, NewParent))
	{
		TArray<TSharedPtr<FJsonValue>> Order;
		for (const UBehaviorTreeGraphNode* Child : ParentEntry->Children)
		{
			if (Child) Order.Add(MakeShared<FJsonValueString>(MCPBTAGuidString(Child)));
		}
		Result->SetArrayField(TEXT("siblingOrder"), Order);
	}

	if (OldParent)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("node"), MCPBTAGuidString(Node));
		Payload->SetStringField(TEXT("parent"), MCPBTAGuidString(OldParent));
		Payload->SetNumberField(TEXT("index"), OldIndex);
		MCPSetRollback(Result, TEXT("move_bt_node"), Payload);
	}
	return MCPResult(Result);
}

// -----------------------------------------------------------------
// remove_bt_node (#889)
// -----------------------------------------------------------------

TSharedPtr<FJsonValue> FGameplayHandlers::RemoveBTNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	UBehaviorTree* Tree = nullptr;
	UBehaviorTreeGraph* Graph = nullptr;
	bool bGraphCreated = false;
	if (auto Err = MCPBTAOpen(Params, /*bCreateGraph*/ false, AssetPath, Tree, Graph, bGraphCreated)) return Err;
	if (!Graph)
	{
		return MCPError(FString::Printf(
			TEXT("%s has no editor graph, so it has no nodes to remove."), *AssetPath));
	}

	FString NodeSpec;
	if (auto Err = RequireStringAlt(Params, TEXT("node"), TEXT("nodePath"), NodeSpec)) return Err;

	TArray<FMCPBTAEntry> Entries;
	MCPBTACollectGraph(Graph, Entries);

	FString ResolveError;
	UBehaviorTreeGraphNode* Node = MCPBTAResolve(Entries, Tree, NodeSpec, ResolveError);
	if (!Node) return MCPError(FString::Printf(TEXT("node: %s"), *ResolveError));
	if (Node->IsA<UBehaviorTreeGraphNode_Root>())
	{
		return MCPError(TEXT("the root node is part of every BehaviorTree graph and cannot be removed."));
	}

	const FMCPBTAEntry* Entry = MCPBTAFind(Entries, Node);
	const FString Category = Entry ? Entry->Category : FString();
	UBehaviorTreeGraphNode* Parent = Entry ? Entry->Parent : nullptr;

	// Removing a composite removes the branch under it. Leaving the subtree
	// behind as loose graph nodes would make the next list call report nodes
	// that no longer run, which is worse than the deletion the caller asked for.
	TArray<UBehaviorTreeGraphNode*> Doomed;
	{
		TArray<UBehaviorTreeGraphNode*> Pending;
		Pending.Add(Node);
		TSet<UBehaviorTreeGraphNode*> Seen;
		while (Pending.Num() > 0 && Doomed.Num() < Graph->Nodes.Num() + 1)
		{
			UBehaviorTreeGraphNode* Current = Pending.Pop();
			if (!Current) continue;
			bool bAlreadySeen = false;
			Seen.Add(Current, &bAlreadySeen);
			if (bAlreadySeen) continue;

			Doomed.Add(Current);
			TArray<UBehaviorTreeGraphNode*> Children;
			MCPBTACollectChildren(Current, Children);
			Pending.Append(Children);
		}
	}

	TArray<TSharedPtr<FJsonValue>> Removed;
	for (const UBehaviorTreeGraphNode* Doom : Doomed)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("guid"), MCPBTAGuidString(Doom));
		Obj->SetStringField(TEXT("class"),
			Doom && Doom->NodeInstance ? Doom->NodeInstance->GetClass()->GetName() : FString());
		Removed.Add(MakeShared<FJsonValueObject>(Obj));
	}

	Tree->Modify();
	Graph->Modify();
	if (Parent) Parent->Modify();

	// Deepest first, so a parent is never destroyed while a child still points
	// at it. DestroyNode detaches a subnode from its owner and breaks a normal
	// node's links, and UpdateAsset's RemoveOrphanedNodes then moves the
	// stranded UBTNode instances out of the asset package.
	for (int32 i = Doomed.Num() - 1; i >= 0; --i)
	{
		if (Doomed[i]) Doomed[i]->DestroyNode();
	}

	const bool bSaved = MCPBTACompileAndSave(Tree, Graph);

	TMap<UBTNode*, FString> Addresses;
	MapBTNodeAddresses(Tree, Addresses);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("category"), Category);
	Result->SetStringField(TEXT("parentGuid"), MCPBTAGuidString(Parent));
	Result->SetNumberField(TEXT("removedCount"), Removed.Num());
	Result->SetArrayField(TEXT("removed"), Removed);
	Result->SetNumberField(TEXT("compiledNodeCount"), Addresses.Num());
	Result->SetBoolField(TEXT("saved"), bSaved);

	// add_bt_node is the opposite operation, not the inverse of this one. It
	// places a single fresh node under a new guid, so it cannot rebuild the
	// branch that came down with the removed node, nor the decorators and
	// services the compiler read off those graph nodes, nor the property values
	// written onto their instances. Naming it here would hand a flow a recovery
	// step that restores a fragment and reports a whole tree.
	MCPSetNoRollback(Result, TEXT(
		"The node, every node beneath it, and their decorators, services and property values are "
		"gone from the graph and out of the recompiled tree. add_bt_node places one fresh node at a "
		"time under a new guid, so no call in this surface puts a removed branch back with its old "
		"structure, ordering and values."));
	return MCPResult(Result);
}
