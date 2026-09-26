#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/Gameplay/GameplayHandlers.h"
#include "HandlerRegistry.h"
#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "BehaviorTree/BTNode.h"

namespace
{
	// The BT node classes under test are resolved by path rather than included
	// by header, so a build without one of them skips the case instead of
	// failing to link.
	UClass* MCPBTTestFindClass(const TCHAR* ClassPath)
	{
		return FindObject<UClass>(nullptr, ClassPath);
	}

	UBTNode* MCPBTTestMakeNode(const TCHAR* ClassPath)
	{
		UClass* Class = MCPBTTestFindClass(ClassPath);
		if (!Class) return nullptr;
		return NewObject<UBTNode>(GetTransientPackage(), Class);
	}

	FString MCPBTTestGetString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		FString Value;
		if (Object.IsValid()) Object->TryGetStringField(Field, Value);
		return Value;
	}
}

// #887: get_behavior_tree_info walked UBlackboardData::Keys as if it were an
// array of UObject pointers. It is an array of FBlackboardEntry structs, so the
// walk read each entry's FName as a pointer and dereferenced it, and every
// blackboard with at least one key took the editor down with an access
// violation. This test builds that exact shape in memory.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBehaviorTreeBlackboardKeysTest,
	"UE.MCP.Gameplay.BehaviorTree.BlackboardKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBehaviorTreeBlackboardKeysTest::RunTest(const FString& Parameters)
{
	UBlackboardData* Blackboard = NewObject<UBlackboardData>(GetTransientPackage());
	TestNotNull(TEXT("blackboard was created"), Blackboard);
	if (!Blackboard) return false;
	const int32 InitialKeyCount = Blackboard->Keys.Num();

	UClass* ObjectKeyClass = MCPBTTestFindClass(TEXT("/Script/AIModule.BlackboardKeyType_Object"));

	FBlackboardEntry Typed;
	Typed.EntryName = TEXT("TargetActor");
	if (ObjectKeyClass)
	{
		Typed.KeyType = NewObject<UBlackboardKeyType>(Blackboard, ObjectKeyClass);
	}
	Blackboard->Keys.Add(Typed);

	// A key whose type was never picked is a real authoring state, and the
	// reader has to describe it rather than dereference it.
	FBlackboardEntry Untyped;
	Untyped.EntryName = TEXT("PatrolIndex");
	Blackboard->Keys.Add(Untyped);

	const TArray<TSharedPtr<FJsonValue>> Keys = FGameplayHandlers::DescribeBlackboardKeys(Blackboard);
	TestEqual(TEXT("the added keys are described"), Keys.Num(), InitialKeyCount + 2);
	if (Keys.Num() != InitialKeyCount + 2) return false;

	const TSharedPtr<FJsonObject> First = Keys[InitialKeyCount]->AsObject();
	const TSharedPtr<FJsonObject> Second = Keys[InitialKeyCount + 1]->AsObject();
	TestEqual(TEXT("first key name"), MCPBTTestGetString(First, TEXT("name")), FString(TEXT("TargetActor")));
	TestEqual(TEXT("second key name"), MCPBTTestGetString(Second, TEXT("name")), FString(TEXT("PatrolIndex")));
	TestEqual(TEXT("untyped key reports an empty type instead of crashing"),
		MCPBTTestGetString(Second, TEXT("type")), FString());
	if (ObjectKeyClass)
	{
		TestEqual(TEXT("typed key reports its short type name"),
			MCPBTTestGetString(First, TEXT("type")), FString(TEXT("Object")));
	}

	// A null blackboard is an empty answer, never a dereference.
	TestEqual(TEXT("null blackboard yields no keys"),
		FGameplayHandlers::DescribeBlackboardKeys(nullptr).Num(), 0);
	return true;
}

// #888: BTDecorator_Blackboard keeps its whole configuration in protected C++
// fields, so the graph read used to report class and name only. Every field a
// "the tree runs but the wrong branch fires" investigation needs comes back now.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBehaviorTreeDecoratorConfigTest,
	"UE.MCP.Gameplay.BehaviorTree.DecoratorConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBehaviorTreeDecoratorConfigTest::RunTest(const FString& Parameters)
{
	UBTNode* Decorator = MCPBTTestMakeNode(TEXT("/Script/AIModule.BTDecorator_Blackboard"));
	if (!Decorator)
	{
		AddInfo(TEXT("BTDecorator_Blackboard is not present in this build; skipping."));
		return true;
	}

	// The nested write is the #919 path: a dotted address into a protected
	// struct field on an owned node subobject.
	FString Error;
	TestTrue(TEXT("selected blackboard key is writable through a dotted path"),
		FGameplayHandlers::WriteBTNodeProperty(Decorator, TEXT("BlackboardKey.SelectedKeyName"),
			MakeShared<FJsonValueString>(TEXT("TargetActor")), Error));
	TestEqual(TEXT("no error writing the selected key"), Error, FString());

#if WITH_EDITORONLY_DATA
	// The comparison operation is authored data, so the engine declares it
	// editor-only. It is the field that says whether the branch tests for
	// "is set" or "is not set".
	TestTrue(TEXT("basic operation is writable by enumerator name"),
		FGameplayHandlers::WriteBTNodeProperty(Decorator, TEXT("BasicOperation"),
			MakeShared<FJsonValueString>(TEXT("NotSet")), Error));
#endif
	TestTrue(TEXT("flow abort mode is writable by enumerator name"),
		FGameplayHandlers::WriteBTNodeProperty(Decorator, TEXT("FlowAbortMode"),
			MakeShared<FJsonValueString>(TEXT("LowerPriority")), Error));
	TestTrue(TEXT("notify observer is writable by enumerator name"),
		FGameplayHandlers::WriteBTNodeProperty(Decorator, TEXT("NotifyObserver"),
			MakeShared<FJsonValueString>(TEXT("ResultChange")), Error));

	const TSharedPtr<FJsonObject> Described = FGameplayHandlers::DescribeBTNode(
		Decorator, TEXT("Root.Children[0].Decorators[0]"), TEXT("Root.Children[0]"),
		/*bIncludeProperties*/ false, /*bIncludeInherited*/ false, TSet<FString>());

	TestEqual(TEXT("kind"), MCPBTTestGetString(Described, TEXT("kind")), FString(TEXT("decorator")));
	TestEqual(TEXT("path"), MCPBTTestGetString(Described, TEXT("path")), FString(TEXT("Root.Children[0].Decorators[0]")));
	TestEqual(TEXT("parentPath"), MCPBTTestGetString(Described, TEXT("parentPath")), FString(TEXT("Root.Children[0]")));
	TestEqual(TEXT("class"), MCPBTTestGetString(Described, TEXT("class")), FString(TEXT("BTDecorator_Blackboard")));
	TestTrue(TEXT("inverseCondition is reported"), Described->HasField(TEXT("inverseCondition")));
	TestEqual(TEXT("flowAbortMode"), MCPBTTestGetString(Described, TEXT("flowAbortMode")), FString(TEXT("LowerPriority")));
	TestEqual(TEXT("notifyObserver"), MCPBTTestGetString(Described, TEXT("notifyObserver")), FString(TEXT("ResultChange")));
#if WITH_EDITORONLY_DATA
	TestEqual(TEXT("basicOperation"), MCPBTTestGetString(Described, TEXT("basicOperation")), FString(TEXT("NotSet")));
#endif

	const TSharedPtr<FJsonObject>* BlackboardKey = nullptr;
	TestTrue(TEXT("blackboardKey is reported"), Described->TryGetObjectField(TEXT("blackboardKey"), BlackboardKey));
	if (BlackboardKey && (*BlackboardKey).IsValid())
	{
		TestEqual(TEXT("selectedKeyName"),
			MCPBTTestGetString(*BlackboardKey, TEXT("selectedKeyName")), FString(TEXT("TargetActor")));
	}

	// A null node is answered, not dereferenced.
	const TSharedPtr<FJsonObject> Empty = FGameplayHandlers::DescribeBTNode(
		nullptr, TEXT("Root"), FString(), false, false, TSet<FString>());
	TestTrue(TEXT("a null node yields an empty object"), Empty.IsValid() && Empty->Values.Num() == 0);
	return true;
}

// #940: on UE 5.8 BTTask_MoveTo::FilterClass is a FValueOrBBKey_Class, so the
// class lives in DefaultValue and get_editor_property reads the field as empty.
// #889 is the same struct shape one field over: AcceptableRadius is a
// FValueOrBBKey_Float, so the number a caller sends has to reach DefaultValue
// rather than bounce off the struct's export-text importer.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBehaviorTreeMoveToFilterClassTest,
	"UE.MCP.Gameplay.BehaviorTree.MoveToFilterClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBehaviorTreeMoveToFilterClassTest::RunTest(const FString& Parameters)
{
	UBTNode* MoveTo = MCPBTTestMakeNode(TEXT("/Script/AIModule.BTTask_MoveTo"));
	if (!MoveTo)
	{
		AddInfo(TEXT("BTTask_MoveTo is not present in this build; skipping."));
		return true;
	}

	FString Error;

	// A JSON number aimed at the struct lands on its DefaultValue.
	TestTrue(TEXT("AcceptableRadius accepts a bare JSON number"),
		FGameplayHandlers::WriteBTNodeProperty(MoveTo, TEXT("AcceptableRadius"),
			MakeShared<FJsonValueNumber>(60.0), Error));
	TestEqual(TEXT("no error writing AcceptableRadius"), Error, FString());

	TSharedPtr<FJsonValue> Radius = FGameplayHandlers::ReadBTNodeProperty(MoveTo, TEXT("AcceptableRadius"), Error);
	TestTrue(TEXT("AcceptableRadius reads back as an object"), Radius.IsValid() && Radius->Type == EJson::Object);
	if (Radius.IsValid() && Radius->Type == EJson::Object)
	{
		double Stored = 0.0;
		Radius->AsObject()->TryGetNumberField(TEXT("defaultValue"), Stored);
		TestEqual(TEXT("AcceptableRadius DefaultValue"), Stored, 60.0);
	}

	// #919: the filtered property read names what it wants and gets that, with
	// the FValueOrBBKey_* struct already unpacked.
	TSet<FString> Filter;
	Filter.Add(TEXT("acceptableradius"));
	const TSharedPtr<FJsonObject> Filtered = FGameplayHandlers::DescribeBTNode(
		MoveTo, TEXT("Root.Children[0]"), TEXT("Root"),
		/*bIncludeProperties*/ true, /*bIncludeInherited*/ false, Filter);
	const TSharedPtr<FJsonObject>* Properties = nullptr;
	TestTrue(TEXT("a filtered read reports properties"), Filtered->TryGetObjectField(TEXT("properties"), Properties));
	if (Properties && (*Properties).IsValid())
	{
		TestEqual(TEXT("only the named property is reported"), (*Properties)->Values.Num(), 1);
		TestTrue(TEXT("the named property is the one asked for"), (*Properties)->HasField(TEXT("AcceptableRadius")));
	}

	// FilterClass is the same shape holding a class.
	UClass* FilterClass = MCPBTTestFindClass(TEXT("/Script/NavigationSystem.RecastFilter_UseDefaultArea"));
	if (!FilterClass)
	{
		AddInfo(TEXT("RecastFilter_UseDefaultArea is not present in this build; skipping the class half."));
		return true;
	}
	const FString FilterClassPath = FilterClass->GetPathName();

	TestTrue(TEXT("FilterClass accepts a class path"),
		FGameplayHandlers::WriteBTNodeProperty(MoveTo, TEXT("FilterClass"),
			MakeShared<FJsonValueString>(FilterClassPath), Error));
	TestEqual(TEXT("no error writing FilterClass"), Error, FString());

	TSharedPtr<FJsonValue> FilterValue = FGameplayHandlers::ReadBTNodeProperty(MoveTo, TEXT("FilterClass"), Error);
	TestTrue(TEXT("FilterClass reads back as an object"), FilterValue.IsValid() && FilterValue->Type == EJson::Object);
	if (FilterValue.IsValid() && FilterValue->Type == EJson::Object)
	{
		TestEqual(TEXT("FilterClass DefaultValue is the class that was written"),
			MCPBTTestGetString(FilterValue->AsObject(), TEXT("defaultValue")), FilterClassPath);
		TestTrue(TEXT("FilterClass export text retains the class path"),
			MCPBTTestGetString(FilterValue->AsObject(), TEXT("text")).Contains(FilterClassPath));
		bool bBound = true;
		FilterValue->AsObject()->TryGetBoolField(TEXT("isBound"), bBound);
		TestFalse(TEXT("a literal class is not a blackboard binding"), bBound);
	}

	// An object value writes the struct's own fields, which is how the field
	// is bound to a blackboard key instead of pinned to a literal.
	TSharedPtr<FJsonObject> Binding = MakeShared<FJsonObject>();
	Binding->SetStringField(TEXT("Key"), TEXT("NavFilter"));
	TestTrue(TEXT("FilterClass accepts a blackboard key binding"),
		FGameplayHandlers::WriteBTNodeProperty(MoveTo, TEXT("FilterClass"),
			MakeShared<FJsonValueObject>(Binding), Error));

	FilterValue = FGameplayHandlers::ReadBTNodeProperty(MoveTo, TEXT("FilterClass"), Error);
	if (FilterValue.IsValid() && FilterValue->Type == EJson::Object)
	{
		TestEqual(TEXT("bound key name"),
			MCPBTTestGetString(FilterValue->AsObject(), TEXT("key")), FString(TEXT("NavFilter")));
		bool bBound = false;
		FilterValue->AsObject()->TryGetBoolField(TEXT("isBound"), bBound);
		TestTrue(TEXT("a key name reads as bound"), bBound);
	}

	// Null clears the class without touching the binding.
	TestTrue(TEXT("FilterClass accepts null to clear the class"),
		FGameplayHandlers::WriteBTNodeProperty(MoveTo, TEXT("FilterClass"),
			MakeShared<FJsonValueNull>(), Error));
	FilterValue = FGameplayHandlers::ReadBTNodeProperty(MoveTo, TEXT("FilterClass"), Error);
	if (FilterValue.IsValid() && FilterValue->Type == EJson::Object)
	{
		TestEqual(TEXT("cleared DefaultValue path"),
			MCPBTTestGetString(FilterValue->AsObject(), TEXT("defaultValue")), FString());
		TestEqual(TEXT("cleared DefaultValue"),
			MCPBTTestGetString(FilterValue->AsObject(), TEXT("defaultValueName")), FString());
	}

	// A property the node does not declare is a structured error, never a
	// silent no-op and never a dereference.
	TestFalse(TEXT("an unknown property is rejected"),
		FGameplayHandlers::WriteBTNodeProperty(MoveTo, TEXT("NotAProperty"),
			MakeShared<FJsonValueNumber>(1.0), Error));
	TestTrue(TEXT("the rejection names the property"), Error.Contains(TEXT("NotAProperty")));

	// A null node is answered, not dereferenced.
	TestFalse(TEXT("a null node is rejected"),
		FGameplayHandlers::WriteBTNodeProperty(nullptr, TEXT("AcceptableRadius"),
			MakeShared<FJsonValueNumber>(1.0), Error));
	return true;
}

// The BT actions have to be reachable by the names the TypeScript schema
// advertises, and each has to answer a call with no parameters with a
// structured error rather than a crash.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBehaviorTreeRegistrationTest,
	"UE.MCP.Gameplay.BehaviorTree.RegistrationAndPreflight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBehaviorTreeRegistrationTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FGameplayHandlers::RegisterHandlers(Registry);

	const TCHAR* Methods[] = {
		TEXT("get_behavior_tree_info"),
		TEXT("read_behavior_tree_graph"),
		TEXT("read_bt_node_properties"),
		TEXT("list_bt_tasks"),
		TEXT("set_bt_node_property"),
		TEXT("set_bt_task_property"),
	};
	for (const TCHAR* Method : Methods)
	{
		TestTrue(FString::Printf(TEXT("%s is registered"), Method), Registry.HasHandler(Method));
	}

	// Every action that needs an asset path says so, and list_bt_tasks answers
	// without one because a directory sweep is its whole point.
	const TCHAR* NeedAssetPath[] = {
		TEXT("get_behavior_tree_info"),
		TEXT("read_behavior_tree_graph"),
		TEXT("read_bt_node_properties"),
		TEXT("set_bt_node_property"),
		TEXT("set_bt_task_property"),
	};
	for (const TCHAR* Method : NeedAssetPath)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, MakeShared<FJsonObject>());
		TestTrue(FString::Printf(TEXT("%s returns an object"), Method),
			Response.IsValid() && Response->Type == EJson::Object);
		if (!Response.IsValid() || Response->Type != EJson::Object) continue;

		const TSharedPtr<FJsonObject> Object = Response->AsObject();
		TestFalse(FString::Printf(TEXT("%s rejects an empty call"), Method), Object->GetBoolField(TEXT("success")));
		TestTrue(FString::Printf(TEXT("%s names assetPath"), Method),
			Object->GetStringField(TEXT("error")).Contains(TEXT("assetPath")));
	}
	return true;
}

#endif
