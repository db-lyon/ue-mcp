// A node removal's rollback payload must replay: the inverse add it names has
// to accept it and put a node back, for a native task and for a
// Blueprint-backed one, whose values live on an instance object the add can
// never have and so are reported beside the payload instead.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/StateTree/StateTreeHandlers.h"
#include "Handlers/StateTree/StateTreeEditTraits.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Tests/MCPScopedTestMount.h"
#include "Tests/MCPStateTreeTestBase.h"

#if UE_MCP_HAS_5_5_API

#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "StateTreeState.h"

namespace MCPStateTreeSlotRollbackTests
{
	using UEMCPStateTreeTests::Call;
	using UEMCPStateTreeTests::Succeeded;

	TSharedPtr<FJsonObject> RootArgs(const FString& Tree)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("assetPath"), Tree);
		Out->SetStringField(TEXT("statePath"), TEXT("Root"));
		return Out;
	}

	/** Remove task 0 of Root, then run the inverse the result names. */
	TSharedPtr<FJsonObject> RemoveAndReplay(FAutomationTestBase& Test, FMCPHandlerRegistry& Registry, const FString& Tree, TSharedPtr<FJsonObject>& OutRemoved)
	{
		TSharedPtr<FJsonObject> Remove = RootArgs(Tree);
		Remove->SetNumberField(TEXT("taskIndex"), 0);
		OutRemoved = Call(Registry, TEXT("remove_state_tree_task"), Remove);
		const TSharedPtr<FJsonObject>* Rollback = nullptr;
		if (!Test.TestTrue(TEXT("the removal names an inverse"), Succeeded(OutRemoved) && OutRemoved->TryGetObjectField(TEXT("rollback"), Rollback)))
		{
			return nullptr;
		}
		const FString Method = (*Rollback)->GetStringField(TEXT("method"));
		return Call(Registry, *Method, (*Rollback)->GetObjectField(TEXT("payload")));
	}
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FMCPStateTreeSlotRollbackTest, FMCPStateTreeTestBase,
	"UE.MCP.Families.Slots.RemovalPayloadReplays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPStateTreeSlotRollbackTest::RunTest(const FString& Parameters)
{
	using namespace MCPStateTreeSlotRollbackTests;
	FMCPScopedTestMount Mount(TEXT("/UEMCPSlotRollback/"), TEXT("UEMCPSlotRollback"));
	const FString Tree = Mount.RootPath + TEXT("ST_Rollback");
	UEMCPStateTreeTests::MakeBareTree(Tree);
	FMCPHandlerRegistry Registry;
	FStateTreeHandlers::RegisterHandlers(Registry);

	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("assetPath"), Tree);
	if (!TestTrue(TEXT("set_schema gives the tree a root"), Succeeded(Call(Registry, TEXT("set_state_tree_schema"), Schema)))) return false;
	UStateTreeEditorData* Data = MCPStateTree::EditorData(LoadAssetByPath<UStateTree>(Tree));
	UStateTreeState* Root = Data && Data->SubTrees.Num() > 0 ? Data->SubTrees[0].Get() : nullptr;
	if (!TestNotNull(TEXT("root state"), Root)) return false;

	// A native task with a value that is not the default.
	{
		TSharedPtr<FJsonObject> Add = RootArgs(Tree);
		Add->SetStringField(TEXT("structType"), TEXT("StateTreeDelayTask"));
		TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
		Props->SetNumberField(TEXT("Duration"), 2.5);
		Add->SetObjectField(TEXT("instanceProperties"), Props);
		if (!TestTrue(TEXT("native task added"), Succeeded(Call(Registry, TEXT("add_state_tree_task"), Add)))) return false;

		TSharedPtr<FJsonObject> Removed;
		const TSharedPtr<FJsonObject> Replayed = RemoveAndReplay(*this, Registry, Tree, Removed);
		TestTrue(TEXT("the native inverse replays"), Replayed.IsValid() && Succeeded(Replayed));
		TestEqual(TEXT("and puts the task back"), Root->Tasks.Num(), 1);
		TestFalse(TEXT("a native removal reports no instance object"), Removed.IsValid() && Removed->HasField(TEXT("removedInstanceObjectProperties")));
		Root->Tasks.Reset();
	}

	// A Blueprint-backed task: the wrapper plus a class whose instance is an object.
	{
		const FString BlueprintPackage = Mount.RootPath + TEXT("BP_RollbackTask");
		UPackage* Package = CreatePackage(*BlueprintPackage);
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			UStateTreeTaskBlueprintBase::StaticClass(), Package, FName(TEXT("BP_RollbackTask")), BPTYPE_Normal,
			UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		if (!TestNotNull(TEXT("fixture task Blueprint"), Blueprint)) return false;
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (!TestNotNull(TEXT("it has a generated class"), Blueprint->GeneratedClass.Get())) return false;

		TSharedPtr<FJsonObject> Add = RootArgs(Tree);
		Add->SetStringField(TEXT("structType"), TEXT("StateTreeBlueprintTaskWrapper"));
		const TSharedPtr<FJsonObject> Added = Call(Registry, TEXT("add_state_tree_task"), Add);
		if (!TestTrue(TEXT("wrapper task added"), Succeeded(Added))) return false;

		TSharedPtr<FJsonObject> SetClass = MakeShared<FJsonObject>();
		SetClass->SetStringField(TEXT("assetPath"), Tree);
		SetClass->SetStringField(TEXT("nodeId"), Added->GetStringField(TEXT("nodeId")));
		SetClass->SetStringField(TEXT("nodeClass"), Blueprint->GeneratedClass->GetPathName());
		const TSharedPtr<FJsonObject> ClassSet = Call(Registry, TEXT("set_state_tree_node_class"), SetClass);
		bool bHasObject = false;
		if (!TestTrue(TEXT("the class gives the node an instance object"),
			Succeeded(ClassSet) && ClassSet->TryGetBoolField(TEXT("hasInstanceObject"), bHasObject) && bHasObject)) return false;

		TSharedPtr<FJsonObject> Removed;
		const TSharedPtr<FJsonObject> Replayed = RemoveAndReplay(*this, Registry, Tree, Removed);
		TestTrue(TEXT("the Blueprint inverse replays"), Replayed.IsValid() && Succeeded(Replayed));
		TestEqual(TEXT("and puts a wrapper back"), Root->Tasks.Num(), 1);
		TestTrue(TEXT("the instance object's values are reported beside the payload"),
			Removed.IsValid() && Removed->HasField(TEXT("removedInstanceObjectProperties")));
		Package->SetDirtyFlag(false);
	}
	return true;
}

#endif // UE_MCP_HAS_5_5_API
#endif // WITH_DEV_AUTOMATION_TESTS
