#if WITH_DEV_AUTOMATION_TESTS

// The engine contracts that BehaviorTree graph authoring rests on (#889, #947).
//
// add_bt_node, move_bt_node and remove_bt_node need a real asset path, so these
// tests do not call the handlers. They build the same BehaviorTree + graph pair
// in memory and drive the same engine calls in the same order, which is what
// actually needs pinning: every one of these behaviours is undocumented, and a
// silent change to any of them would leave the handlers compiling and producing
// trees that do not run.
//
// The contract that matters most is the ordering one. Child execution order is
// not stored anywhere: BTGraphHelpers::CreateChildren sorts each output pin's
// links by node X position on every compile, so a composite's child order IS
// the graph layout. Reordering a Selector's branches is therefore a position
// rewrite, and if that ever stops being true, reordering silently becomes a
// no-op rather than failing. FMCPBTAuthoringChildOrderTest is what would catch
// it.

#include "Handlers/Gameplay/GameplayHandlers.h"
#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTTaskNode.h"

#include "AIGraphTypes.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraphNode_Composite.h"
#include "BehaviorTreeGraphNode_Decorator.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "BehaviorTreeGraphNode_Task.h"
#include "EdGraphSchema_BehaviorTree.h"

namespace
{
	// Node classes are resolved by path rather than pulled in by header, so a
	// build missing one of them skips the case instead of failing to link.
	UClass* MCPBTAuthTestClass(const TCHAR* ClassPath)
	{
		return FindObject<UClass>(nullptr, ClassPath);
	}

	// A BehaviorTree with a freshly seeded editor graph, built exactly the way
	// the authoring handlers seed one for a tree that has never been opened in
	// the BehaviorTree editor.
	struct FMCPBTAuthTestTree
	{
		UBehaviorTree* Tree = nullptr;
		UBehaviorTreeGraph* Graph = nullptr;
		UBehaviorTreeGraphNode_Root* Root = nullptr;

		bool IsValid() const { return Tree && Graph && Root; }
	};

	FMCPBTAuthTestTree MCPBTAuthTestMakeTree()
	{
		FMCPBTAuthTestTree Out;
		Out.Tree = NewObject<UBehaviorTree>(GetTransientPackage());
		if (!Out.Tree) return Out;

		Out.Tree->BTGraph = FBlueprintEditorUtils::CreateNewGraph(
			Out.Tree,
			FName(TEXT("Behavior Tree")),
			UBehaviorTreeGraph::StaticClass(),
			UEdGraphSchema_BehaviorTree::StaticClass());

		Out.Graph = Cast<UBehaviorTreeGraph>(Out.Tree->BTGraph);
		if (!Out.Graph) return Out;

		if (const UEdGraphSchema* Schema = Out.Graph->GetSchema())
		{
			Schema->CreateDefaultNodesForGraph(*Out.Graph);
		}
		Out.Graph->OnCreated();

		for (UEdGraphNode* Node : Out.Graph->Nodes)
		{
			if (UBehaviorTreeGraphNode_Root* Root = Cast<UBehaviorTreeGraphNode_Root>(Node))
			{
				Out.Root = Root;
				break;
			}
		}
		return Out;
	}

	// Place a graph node carrying RuntimeClass, the way add_bt_node does:
	// ClassData first, then Finalize, which runs PostPlacedNewNode and spawns
	// the UBTNode instance under the asset.
	UBehaviorTreeGraphNode* MCPBTAuthTestPlace(
		UBehaviorTreeGraph* Graph, UClass* GraphNodeClass, UClass* RuntimeClass, int32 PosX, int32 PosY)
	{
		if (!Graph || !GraphNodeClass || !RuntimeClass) return nullptr;

		UBehaviorTreeGraphNode* Created = nullptr;
		{
			FGraphNodeCreator<UBehaviorTreeGraphNode> Creator(*Graph);
			Created = Creator.CreateNode(/*bSelectNewNode*/ false, GraphNodeClass);
			if (Created)
			{
				Created->ClassData = FGraphNodeClassData(RuntimeClass, FString());
			}
			Creator.Finalize();
		}
		if (Created)
		{
			Created->NodePosX = PosX;
			Created->NodePosY = PosY;
		}
		return Created;
	}

	bool MCPBTAuthTestLink(UBehaviorTreeGraph* Graph, UBehaviorTreeGraphNode* Parent, UBehaviorTreeGraphNode* Child)
	{
		if (!Graph || !Parent || !Child) return false;
		const UEdGraphSchema* Schema = Graph->GetSchema();
		UEdGraphPin* Out = Parent->GetOutputPin(0);
		UEdGraphPin* In = Child->GetInputPin(0);
		if (!Schema || !Out || !In) return false;
		return Schema->TryCreateConnection(Out, In);
	}
}

// The whole authoring path in one pass: seed a graph, place a composite under
// the root, place two tasks under the composite, compile, then prove the
// compiled runtime tree is the one that was drawn.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBTAuthoringCompileTest,
	"UE.MCP.Gameplay.BehaviorTree.AuthoringCompile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBTAuthoringCompileTest::RunTest(const FString& Parameters)
{
	UClass* SelectorClass = MCPBTAuthTestClass(TEXT("/Script/AIModule.BTComposite_Selector"));
	UClass* WaitClass = MCPBTAuthTestClass(TEXT("/Script/AIModule.BTTask_Wait"));
	if (!SelectorClass || !WaitClass)
	{
		AddInfo(TEXT("BTComposite_Selector or BTTask_Wait is not present in this build; skipping."));
		return true;
	}

	FMCPBTAuthTestTree Fixture = MCPBTAuthTestMakeTree();
	TestTrue(TEXT("a BehaviorTree graph can be seeded with its root node"), Fixture.IsValid());
	if (!Fixture.IsValid()) return false;

	// The seeded graph compiles to an empty tree, which is the state a caller
	// starts from after gameplay(create_behavior_tree).
	Fixture.Graph->UpdateAsset();
	TestNull(TEXT("a graph with only a root compiles to no runtime root"), Fixture.Tree->RootNode.Get());

	UBehaviorTreeGraphNode* Selector = MCPBTAuthTestPlace(
		Fixture.Graph, UBehaviorTreeGraphNode_Composite::StaticClass(), SelectorClass, 0, 200);
	TestNotNull(TEXT("the composite graph node was placed"), Selector);
	if (!Selector) return false;

	// PostPlacedNewNode is what turns ClassData into a runtime node instance
	// owned by the asset. Without it every later step operates on nothing.
	TestNotNull(TEXT("placing the node spawned its runtime instance"), Selector->NodeInstance.Get());
	TestTrue(TEXT("the instance is owned by the BehaviorTree asset"),
		Selector->NodeInstance && Selector->NodeInstance->GetOuter() == Fixture.Tree);

	TestTrue(TEXT("the schema links the composite under the root"),
		MCPBTAuthTestLink(Fixture.Graph, Fixture.Root, Selector));

	UBehaviorTreeGraphNode* Left = MCPBTAuthTestPlace(
		Fixture.Graph, UBehaviorTreeGraphNode_Task::StaticClass(), WaitClass, -300, 400);
	UBehaviorTreeGraphNode* Right = MCPBTAuthTestPlace(
		Fixture.Graph, UBehaviorTreeGraphNode_Task::StaticClass(), WaitClass, 300, 400);
	TestTrue(TEXT("both task nodes were placed"), Left != nullptr && Right != nullptr);
	if (!Left || !Right) return false;

	TestTrue(TEXT("the first task links under the composite"), MCPBTAuthTestLink(Fixture.Graph, Selector, Left));
	TestTrue(TEXT("the second task links under the composite"), MCPBTAuthTestLink(Fixture.Graph, Selector, Right));

	// A task cannot sit directly under the root: the root's output pin is
	// declared SingleComposite. The schema is what enforces this, which is why
	// add_bt_node routes every link through it rather than writing pins.
	UBehaviorTreeGraphNode* Stray = MCPBTAuthTestPlace(
		Fixture.Graph, UBehaviorTreeGraphNode_Task::StaticClass(), WaitClass, 900, 400);
	if (Stray)
	{
		TestFalse(TEXT("the schema refuses a task directly under the root"),
			MCPBTAuthTestLink(Fixture.Graph, Fixture.Root, Stray));
		Stray->DestroyNode();
	}

	Fixture.Graph->UpdateAsset();

	UBTCompositeNode* RuntimeRoot = Fixture.Tree->RootNode.Get();
	TestNotNull(TEXT("the graph compiled into a runtime root"), RuntimeRoot);
	if (!RuntimeRoot) return false;
	TestTrue(TEXT("the runtime root is the composite that was placed"),
		static_cast<UObject*>(RuntimeRoot) == Selector->NodeInstance.Get());
	TestEqual(TEXT("both tasks compiled in as children"), RuntimeRoot->Children.Num(), 2);

	// A compiled node carries a real execution index. This is the step that
	// reflection alone can never reach: a UBTNode spawned by script keeps
	// MAX_uint16 here and the tree refuses to run.
	TestTrue(TEXT("the runtime root has a real execution index"),
		RuntimeRoot->GetExecutionIndex() != MAX_uint16);

	// The addresses the read surface reports, derived from the compiled tree.
	TMap<UBTNode*, FString> Addresses;
	FGameplayHandlers::MapBTNodeAddresses(Fixture.Tree, Addresses);
	const FString* RootAddress = Addresses.Find(RuntimeRoot);
	TestTrue(TEXT("the compiled root has an address"), RootAddress != nullptr);
	if (RootAddress) TestEqual(TEXT("the compiled root addresses as Root"), *RootAddress, FString(TEXT("Root")));

	if (RuntimeRoot->Children.Num() == 2)
	{
		const FString* FirstChild = Addresses.Find(Cast<UBTNode>(RuntimeRoot->Children[0].ChildTask.Get()));
		TestTrue(TEXT("the first child has an address"), FirstChild != nullptr);
		if (FirstChild)
		{
			TestEqual(TEXT("the first child addresses as Root.Children[0]"),
				*FirstChild, FString(TEXT("Root.Children[0]")));
		}
	}
	return true;
}

// Child execution order is the graph's X layout, re-derived on every compile.
// This is the contract move_bt_node's reorder depends on, and the one that
// would break silently.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBTAuthoringChildOrderTest,
	"UE.MCP.Gameplay.BehaviorTree.AuthoringChildOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBTAuthoringChildOrderTest::RunTest(const FString& Parameters)
{
	UClass* SequenceClass = MCPBTAuthTestClass(TEXT("/Script/AIModule.BTComposite_Sequence"));
	UClass* WaitClass = MCPBTAuthTestClass(TEXT("/Script/AIModule.BTTask_Wait"));
	if (!SequenceClass || !WaitClass)
	{
		AddInfo(TEXT("BTComposite_Sequence or BTTask_Wait is not present in this build; skipping."));
		return true;
	}

	FMCPBTAuthTestTree Fixture = MCPBTAuthTestMakeTree();
	if (!Fixture.IsValid())
	{
		AddError(TEXT("could not seed a BehaviorTree graph"));
		return false;
	}

	UBehaviorTreeGraphNode* Sequence = MCPBTAuthTestPlace(
		Fixture.Graph, UBehaviorTreeGraphNode_Composite::StaticClass(), SequenceClass, 0, 200);
	UBehaviorTreeGraphNode* First = MCPBTAuthTestPlace(
		Fixture.Graph, UBehaviorTreeGraphNode_Task::StaticClass(), WaitClass, -300, 400);
	UBehaviorTreeGraphNode* Second = MCPBTAuthTestPlace(
		Fixture.Graph, UBehaviorTreeGraphNode_Task::StaticClass(), WaitClass, 300, 400);
	if (!Sequence || !First || !Second)
	{
		AddError(TEXT("could not place the nodes under test"));
		return false;
	}

	MCPBTAuthTestLink(Fixture.Graph, Fixture.Root, Sequence);
	MCPBTAuthTestLink(Fixture.Graph, Sequence, First);
	MCPBTAuthTestLink(Fixture.Graph, Sequence, Second);
	Fixture.Graph->UpdateAsset();

	UBTCompositeNode* RuntimeRoot = Fixture.Tree->RootNode.Get();
	if (!RuntimeRoot || RuntimeRoot->Children.Num() != 2)
	{
		AddError(TEXT("the sequence did not compile with two children"));
		return false;
	}

	TestTrue(TEXT("the leftmost node compiles as child 0"),
		static_cast<UObject*>(RuntimeRoot->Children[0].ChildTask.Get()) == First->NodeInstance.Get());
	TestTrue(TEXT("the rightmost node compiles as child 1"),
		static_cast<UObject*>(RuntimeRoot->Children[1].ChildTask.Get()) == Second->NodeInstance.Get());

	// Swap the positions and nothing else. If ordering were held in the link
	// array, or anywhere but the layout, this recompile would not move.
	const int32 FirstX = First->NodePosX;
	First->NodePosX = Second->NodePosX;
	Second->NodePosX = FirstX;
	Fixture.Graph->UpdateAsset();

	RuntimeRoot = Fixture.Tree->RootNode.Get();
	if (!RuntimeRoot || RuntimeRoot->Children.Num() != 2)
	{
		AddError(TEXT("the sequence lost its children on recompile"));
		return false;
	}

	TestTrue(TEXT("swapping X positions swaps the execution order"),
		static_cast<UObject*>(RuntimeRoot->Children[0].ChildTask.Get()) == Second->NodeInstance.Get());
	TestTrue(TEXT("and the former first child now runs second"),
		static_cast<UObject*>(RuntimeRoot->Children[1].ChildTask.Get()) == First->NodeInstance.Get());
	return true;
}

// Reparenting: breaking a child's input link and relinking it under a different
// composite moves the whole branch, which is move_bt_node's reconnect path.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBTAuthoringReparentTest,
	"UE.MCP.Gameplay.BehaviorTree.AuthoringReparent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBTAuthoringReparentTest::RunTest(const FString& Parameters)
{
	UClass* SelectorClass = MCPBTAuthTestClass(TEXT("/Script/AIModule.BTComposite_Selector"));
	UClass* SequenceClass = MCPBTAuthTestClass(TEXT("/Script/AIModule.BTComposite_Sequence"));
	UClass* WaitClass = MCPBTAuthTestClass(TEXT("/Script/AIModule.BTTask_Wait"));
	if (!SelectorClass || !SequenceClass || !WaitClass)
	{
		AddInfo(TEXT("the composite or task classes under test are absent from this build; skipping."));
		return true;
	}

	FMCPBTAuthTestTree Fixture = MCPBTAuthTestMakeTree();
	if (!Fixture.IsValid())
	{
		AddError(TEXT("could not seed a BehaviorTree graph"));
		return false;
	}

	UBehaviorTreeGraphNode* Selector = MCPBTAuthTestPlace(
		Fixture.Graph, UBehaviorTreeGraphNode_Composite::StaticClass(), SelectorClass, 0, 200);
	UBehaviorTreeGraphNode* Sequence = MCPBTAuthTestPlace(
		Fixture.Graph, UBehaviorTreeGraphNode_Composite::StaticClass(), SequenceClass, -300, 400);
	UBehaviorTreeGraphNode* Task = MCPBTAuthTestPlace(
		Fixture.Graph, UBehaviorTreeGraphNode_Task::StaticClass(), WaitClass, 300, 400);
	if (!Selector || !Sequence || !Task)
	{
		AddError(TEXT("could not place the nodes under test"));
		return false;
	}

	MCPBTAuthTestLink(Fixture.Graph, Fixture.Root, Selector);
	MCPBTAuthTestLink(Fixture.Graph, Selector, Sequence);
	MCPBTAuthTestLink(Fixture.Graph, Selector, Task);
	Fixture.Graph->UpdateAsset();

	UBTCompositeNode* RuntimeRoot = Fixture.Tree->RootNode.Get();
	TestEqual(TEXT("the selector starts with two children"), RuntimeRoot ? RuntimeRoot->Children.Num() : 0, 2);

	// Move the task under the sequence. The old link has to go first: the
	// child's input pin holds a single connection, and the compiled child list
	// is only correct once it has.
	if (UEdGraphPin* TaskIn = Task->GetInputPin(0))
	{
		TaskIn->BreakAllPinLinks(true);
	}
	Task->NodePosX = -300;
	Task->NodePosY = 600;
	TestTrue(TEXT("the task relinks under the sequence"), MCPBTAuthTestLink(Fixture.Graph, Sequence, Task));
	Fixture.Graph->UpdateAsset();

	RuntimeRoot = Fixture.Tree->RootNode.Get();
	if (!RuntimeRoot)
	{
		AddError(TEXT("the tree lost its root on recompile"));
		return false;
	}
	TestEqual(TEXT("the selector is down to one child"), RuntimeRoot->Children.Num(), 1);
	if (RuntimeRoot->Children.Num() != 1) return false;

	UBTCompositeNode* RuntimeSequence = RuntimeRoot->Children[0].ChildComposite.Get();
	TestTrue(TEXT("that child is the sequence"),
		static_cast<UObject*>(RuntimeSequence) == Sequence->NodeInstance.Get());
	if (!RuntimeSequence) return false;
	TestEqual(TEXT("the sequence picked up the task"), RuntimeSequence->Children.Num(), 1);
	if (RuntimeSequence->Children.Num() == 1)
	{
		TestTrue(TEXT("and it is the task that was moved"),
			static_cast<UObject*>(RuntimeSequence->Children[0].ChildTask.Get()) == Task->NodeInstance.Get());
	}

	// Addresses follow the move, which is what lets a caller hand a guid to the
	// authoring calls and the same node's address to the read calls.
	TMap<UBTNode*, FString> Addresses;
	FGameplayHandlers::MapBTNodeAddresses(Fixture.Tree, Addresses);
	if (const FString* TaskAddress = Addresses.Find(Cast<UBTNode>(Task->NodeInstance.Get())))
	{
		TestEqual(TEXT("the moved task readdresses under the sequence"),
			*TaskAddress, FString(TEXT("Root.Children[0].Children[0]")));
	}
	else
	{
		AddError(TEXT("the moved task has no address in the compiled tree"));
	}
	return true;
}

// Decorators are subnodes, not children. AddSubNode is what puts one on a node,
// and the compiler reads it off the owner's Decorators array into the parent
// composite's child entry. Linking a decorator as a child instead would compile
// to nothing at all.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBTAuthoringSubNodeTest,
	"UE.MCP.Gameplay.BehaviorTree.AuthoringSubNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBTAuthoringSubNodeTest::RunTest(const FString& Parameters)
{
	UClass* SelectorClass = MCPBTAuthTestClass(TEXT("/Script/AIModule.BTComposite_Selector"));
	UClass* WaitClass = MCPBTAuthTestClass(TEXT("/Script/AIModule.BTTask_Wait"));
	UClass* DecoratorClass = MCPBTAuthTestClass(TEXT("/Script/AIModule.BTDecorator_Blackboard"));
	UClass* BoolKeyClass = MCPBTAuthTestClass(TEXT("/Script/AIModule.BlackboardKeyType_Bool"));
	if (!SelectorClass || !WaitClass || !DecoratorClass)
	{
		AddInfo(TEXT("the composite, task or decorator classes under test are absent from this build; skipping."));
		return true;
	}

	FMCPBTAuthTestTree Fixture = MCPBTAuthTestMakeTree();
	if (!Fixture.IsValid())
	{
		AddError(TEXT("could not seed a BehaviorTree graph"));
		return false;
	}

	// A blackboard with one key, so the decorator's FBlackboardKeySelector has
	// something real to resolve against when InitializeFromAsset runs.
	UBlackboardData* Blackboard = NewObject<UBlackboardData>(Fixture.Tree);
	FBlackboardEntry Entry;
	Entry.EntryName = TEXT("IsAlive");
	if (BoolKeyClass)
	{
		Entry.KeyType = NewObject<UBlackboardKeyType>(Blackboard, BoolKeyClass);
	}
	Blackboard->Keys.Add(Entry);
	Fixture.Tree->BlackboardAsset = Blackboard;

	UBehaviorTreeGraphNode* Selector = MCPBTAuthTestPlace(
		Fixture.Graph, UBehaviorTreeGraphNode_Composite::StaticClass(), SelectorClass, 0, 200);
	UBehaviorTreeGraphNode* Task = MCPBTAuthTestPlace(
		Fixture.Graph, UBehaviorTreeGraphNode_Task::StaticClass(), WaitClass, 0, 400);
	if (!Selector || !Task)
	{
		AddError(TEXT("could not place the nodes under test"));
		return false;
	}

	MCPBTAuthTestLink(Fixture.Graph, Fixture.Root, Selector);
	MCPBTAuthTestLink(Fixture.Graph, Selector, Task);

	// The subnode is constructed against the graph and handed to AddSubNode,
	// which does the guid, the instance spawn and the parent bookkeeping.
	UBehaviorTreeGraphNode* SubNode = NewObject<UBehaviorTreeGraphNode>(
		Fixture.Graph, UBehaviorTreeGraphNode_Decorator::StaticClass());
	SubNode->ClassData = FGraphNodeClassData(DecoratorClass, FString());
	Task->AddSubNode(SubNode, Fixture.Graph);

	TestEqual(TEXT("the decorator landed in the task's Decorators list"), Task->Decorators.Num(), 1);
	TestEqual(TEXT("and not in its Services list"), Task->Services.Num(), 0);
	TestNotNull(TEXT("AddSubNode spawned the decorator instance"), SubNode->NodeInstance.Get());

	UBTNode* DecoratorInstance = Cast<UBTNode>(SubNode->NodeInstance);
	if (DecoratorInstance)
	{
		FString WriteError;
		TestTrue(TEXT("the decorator's blackboard key is writable before the compile"),
			FGameplayHandlers::WriteBTNodeProperty(DecoratorInstance, TEXT("BlackboardKey.SelectedKeyName"),
				MakeShared<FJsonValueString>(TEXT("IsAlive")), WriteError));
	}

	Fixture.Graph->UpdateAsset();

	UBTCompositeNode* RuntimeRoot = Fixture.Tree->RootNode.Get();
	if (!RuntimeRoot || RuntimeRoot->Children.Num() != 1)
	{
		AddError(TEXT("the selector did not compile with its one child"));
		return false;
	}

	// The decorator is stored on the PARENT composite's child entry, not on the
	// task. That asymmetry between the graph and the runtime tree is why the
	// read surface addresses it as Root.Children[0].Decorators[0].
	TestEqual(TEXT("the decorator compiled onto the child entry"),
		RuntimeRoot->Children[0].Decorators.Num(), 1);

	TMap<UBTNode*, FString> Addresses;
	FGameplayHandlers::MapBTNodeAddresses(Fixture.Tree, Addresses);
	if (DecoratorInstance)
	{
		if (const FString* Address = Addresses.Find(DecoratorInstance))
		{
			TestEqual(TEXT("the decorator addresses under its child entry"),
				*Address, FString(TEXT("Root.Children[0].Decorators[0]")));
		}
		else
		{
			AddError(TEXT("the compiled decorator has no address"));
		}

		// UpdateAsset re-runs InitializeFromAsset on every node, which is what
		// turns the key name into a resolved key id. A selector that never
		// resolves leaves the decorator permanently false at runtime.
		if (BoolKeyClass)
		{
			FString ReadError;
			const TSharedPtr<FJsonValue> Read = FGameplayHandlers::ReadBTNodeProperty(
				DecoratorInstance, TEXT("BlackboardKey.SelectedKeyName"), ReadError);
			FString ReadName;
			if (Read.IsValid()) Read->TryGetString(ReadName);
			TestEqual(TEXT("the key name survives the compile"), ReadName, FString(TEXT("IsAlive")));
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
