// The F1 edit scope, driven through StateTree as its first asset type: undo
// and redo of a committed edit, no undo record for a refusal or a no-op, the
// compile-failure policies, the #932 guard and a save that fails after the
// guard passed, and several targets in one scope. Trees live in a private mount.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/StateTree/StateTreeHandlers.h"
#include "Handlers/StateTree/StateTreeEditTraits.h"
#include "Families/MCPEditScope.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/AutomationTest.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "Tests/MCPScopedTestMount.h"
#include "Tests/MCPStateTreeTestBase.h"

#if UE_MCP_HAS_5_5_API

namespace MCPEditScopeTests
{
	using UEMCPStateTreeTests::Call;
	using UEMCPStateTreeTests::Succeeded;

	TSharedPtr<FJsonObject> Args(const FString& AssetPath)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("assetPath"), AssetPath);
		return Out;
	}

	/** A tree with a schema, a root state and one child, saved once through set_schema. */
	UStateTreeState* MakeTree(FMCPHandlerRegistry& Registry, const FString& AssetPath)
	{
		UEMCPStateTreeTests::MakeBareTree(AssetPath);
		if (!Succeeded(Call(Registry, TEXT("set_state_tree_schema"), Args(AssetPath)))) return nullptr;
		TSharedPtr<FJsonObject> Child = Args(AssetPath);
		Child->SetStringField(TEXT("statePath"), TEXT("Root"));
		Child->SetStringField(TEXT("name"), TEXT("Child"));
		if (!Succeeded(Call(Registry, TEXT("add_state_tree_state"), Child))) return nullptr;
		UStateTree* Tree = LoadAssetByPath<UStateTree>(AssetPath);
		UStateTreeEditorData* Data = MCPStateTree::EditorData(Tree);
		return Data && Data->SubTrees.Num() > 0 && Data->SubTrees[0]->Children.Num() > 0 ? Data->SubTrees[0]->Children[0].Get() : nullptr;
	}

	/** A GotoState transition whose target does not exist, which the compiler refuses. */
	void BreakCompile(UStateTreeState* State)
	{
		FStateTreeTransition& Transition = State->AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::GotoState, nullptr);
		Transition.State.ID = FGuid::NewGuid();
	}

	FString PackageFile(const FString& AssetPath)
	{
		FString File;
		UStateTree* Tree = LoadAssetByPath<UStateTree>(AssetPath);
		if (Tree) ResolvePackageFileName(Tree->GetOutermost(), File);
		return File;
	}

	int32 UndoDepth()
	{
		return GEditor && GEditor->Trans ? GEditor->Trans->GetQueueLength() : -1;
	}
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FMCPEditScopeUndoRedoTest, FMCPStateTreeTestBase,
	"UE.MCP.Families.EditScope.UndoRedo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPEditScopeUndoRedoTest::RunTest(const FString& Parameters)
{
	using namespace MCPEditScopeTests;
	FMCPScopedTestMount Mount(TEXT("/UEMCPEditScopeUndo/"), TEXT("UEMCPEditScopeUndo"));
	const FString Tree = Mount.RootPath + TEXT("ST_Undo");
	FMCPHandlerRegistry Registry;
	FStateTreeHandlers::RegisterHandlers(Registry);
	UStateTreeState* Child = MakeTree(Registry, Tree);
	if (!TestNotNull(TEXT("fixture tree with a child state"), Child)) return false;
	if (!TestTrue(TEXT("the editor has an undo buffer"), UndoDepth() >= 0)) return false;

	TSharedPtr<FJsonObject> Add = Args(Tree);
	Add->SetStringField(TEXT("statePath"), TEXT("Root.Child"));
	Add->SetStringField(TEXT("structType"), TEXT("StateTreeDelayTask"));

	const int32 Before = UndoDepth();
	if (!TestTrue(TEXT("add_state_tree_task succeeds"), Succeeded(Call(Registry, TEXT("add_state_tree_task"), Add)))) return false;
	TestEqual(TEXT("a committed edit leaves one undo record"), UndoDepth(), Before + 1);
	TestEqual(TEXT("the task landed"), Child->Tasks.Num(), 1);

	GEditor->UndoTransaction();
	TestEqual(TEXT("undo takes the task back out"), Child->Tasks.Num(), 0);
	GEditor->RedoTransaction();
	TestEqual(TEXT("redo puts it back"), Child->Tasks.Num(), 1);

	{
		TSharedPtr<FJsonObject> Bad = Args(Tree);
		Bad->SetStringField(TEXT("statePath"), TEXT("Root.Child"));
		Bad->SetStringField(TEXT("structType"), TEXT("StateTreeDelayTask"));
		TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
		Props->SetNumberField(TEXT("NoSuchProperty"), 1.0);
		Bad->SetObjectField(TEXT("instanceProperties"), Props);
		const int32 Depth = UndoDepth();
		TestFalse(TEXT("a refused add fails"), Succeeded(Call(Registry, TEXT("add_state_tree_task"), Bad)));
		TestEqual(TEXT("a refusal leaves no undo record"), UndoDepth(), Depth);
		TestEqual(TEXT("and no half-built task"), Child->Tasks.Num(), 1);
	}
	{
		TSharedPtr<FJsonObject> Clear = Args(Tree);
		Clear->SetStringField(TEXT("statePath"), TEXT("Root.Child"));
		TestTrue(TEXT("clear with a task succeeds"), Succeeded(Call(Registry, TEXT("clear_state_tree_state_nodes"), Clear)));
		const int32 Depth = UndoDepth();
		const TSharedPtr<FJsonObject> Again = Call(Registry, TEXT("clear_state_tree_state_nodes"), Clear);
		bool bUnchanged = false;
		TestTrue(TEXT("a second clear reports unchanged"), Again->TryGetBoolField(TEXT("unchanged"), bUnchanged) && bUnchanged);
		TestEqual(TEXT("a no-op leaves no undo record"), UndoDepth(), Depth);
	}
	{
		TSharedPtr<FJsonObject> Set = Args(Tree);
		Set->SetStringField(TEXT("statePath"), TEXT("Root.Child"));
		Set->SetStringField(TEXT("propertyName"), TEXT("weight"));
		Set->SetStringField(TEXT("value"), TEXT("3"));
		const TSharedPtr<FJsonObject> Result = Call(Registry, TEXT("set_state_tree_state_property"), Set);
		FString Previous;
		TestTrue(TEXT("a property write reports the value it replaced"), Result->TryGetStringField(TEXT("previousValue"), Previous));
		TestEqual(TEXT("the write landed"), Child->Weight, 3.f);
		GEditor->UndoTransaction();
		TestEqual(TEXT("undo restores the previous weight"), FString::SanitizeFloat(Child->Weight), Previous);
	}
	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FMCPEditScopeCompileFailureTest, FMCPStateTreeTestBase,
	"UE.MCP.Families.EditScope.CompileFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPEditScopeCompileFailureTest::RunTest(const FString& Parameters)
{
	using namespace MCPEditScopeTests;
	FMCPScopedTestMount Mount(TEXT("/UEMCPEditScopeCompile/"), TEXT("UEMCPEditScopeCompile"));
	const FString Tree = Mount.RootPath + TEXT("ST_Broken");
	FMCPHandlerRegistry Registry;
	FStateTreeHandlers::RegisterHandlers(Registry);
	UStateTreeState* Child = MakeTree(Registry, Tree);
	if (!TestNotNull(TEXT("fixture tree with a child state"), Child)) return false;
	BreakCompile(Child);
	UPackage* Package = LoadAssetByPath<UStateTree>(Tree)->GetOutermost();
	Package->MarkPackageDirty();

	// SkipSave, through the handler: the call succeeds and says nothing was written.
	const TSharedPtr<FJsonObject> Compiled = Call(Registry, TEXT("compile_state_tree"), Args(Tree));
	bool bCompiled = true, bSaved = true, bUpdated = false;
	TestTrue(TEXT("a failed compile keeps success"), Succeeded(Compiled));
	TestTrue(TEXT("compiled is reported false"), Compiled->TryGetBoolField(TEXT("compiled"), bCompiled) && !bCompiled);
	TestTrue(TEXT("saved is reported false"), Compiled->TryGetBoolField(TEXT("saved"), bSaved) && !bSaved);
	TestFalse(TEXT("a failed compile is not an update"), Compiled->TryGetBoolField(TEXT("updated"), bUpdated) && bUpdated);
	TestTrue(TEXT("the compiler's errors are reported"), Compiled->GetArrayField(TEXT("errors")).Num() > 0);
	TestTrue(TEXT("the package was not written"), Package->IsDirty());

	// Refuse: the call fails and nothing is written.
	{
		FMCPEditScope Edit(Args(Tree), TEXT("test_refuse"), TEXT("assetPath"),
			FMCPCommitPolicy::CompileSave(EMCPCompileFailure::Refuse, /*bInEvenIfUnchanged=*/true));
		if (!TestFalse(TEXT("opens"), Edit.Open<UStateTree>().IsValid())) return false;
		TSharedPtr<FJsonObject> Result = MCPSuccess();
		Edit.Finish(Result);
		TestFalse(TEXT("Refuse fails the call"), Succeeded(Result));
		TestTrue(TEXT("Refuse writes nothing"), Package->IsDirty());
	}
	// SaveAnyway: written although it did not compile.
	{
		FMCPEditScope Edit(Args(Tree), TEXT("test_save_anyway"), TEXT("assetPath"),
			FMCPCommitPolicy::CompileSave(EMCPCompileFailure::SaveAnyway, /*bInEvenIfUnchanged=*/true));
		if (!TestFalse(TEXT("opens"), Edit.Open<UStateTree>().IsValid())) return false;
		TSharedPtr<FJsonObject> Result = MCPSuccess();
		Edit.Finish(Result);
		bool bWritten = false;
		TestTrue(TEXT("SaveAnyway saves"), Result->TryGetBoolField(TEXT("saved"), bWritten) && bWritten);
		TestFalse(TEXT("Compiled() reports the failure"), Edit.Compiled());
		TestFalse(TEXT("the package is clean"), Package->IsDirty());
	}
	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FMCPEditScopeSaveFailureTest, FMCPStateTreeTestBase,
	"UE.MCP.Families.EditScope.SaveFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPEditScopeSaveFailureTest::RunTest(const FString& Parameters)
{
	using namespace MCPEditScopeTests;
	FMCPScopedTestMount Mount(TEXT("/UEMCPEditScopeSave/"), TEXT("UEMCPEditScopeSave"));
	const FString Tree = Mount.RootPath + TEXT("ST_ReadOnly");
	FMCPHandlerRegistry Registry;
	FStateTreeHandlers::RegisterHandlers(Registry);
	if (!TestNotNull(TEXT("fixture tree"), MakeTree(Registry, Tree))) return false;
	const FString File = PackageFile(Tree);
	if (!TestTrue(TEXT("set_schema wrote the package"), IFileManager::Get().FileExists(*File))) return false;
	IPlatformFile& Platform = FPlatformFileManager::Get().GetPlatformFile();

	// The scope opens while the file is writable; the write then fails at save time.
	{
		FMCPEditScope Edit(Args(Tree), TEXT("test_save_failure"), TEXT("assetPath"),
			FMCPCommitPolicy::CompileSave(EMCPCompileFailure::SaveAnyway, /*bInEvenIfUnchanged=*/true));
		if (!TestFalse(TEXT("opens"), Edit.Open<UStateTree>().IsValid())) return false;
		Platform.SetReadOnly(*File, true);
		TSharedPtr<FJsonObject> Result = MCPSuccess();
		Edit.Finish(Result);
		bool bSaved = true;
		FString SaveError;
		TestTrue(TEXT("a failed save reports saved:false"), Result->TryGetBoolField(TEXT("saved"), bSaved) && !bSaved);
		TestFalse(TEXT("a failed save fails the call"), Succeeded(Result));
		TestTrue(TEXT("saveError says why"), Result->TryGetStringField(TEXT("saveError"), SaveError) && !SaveError.IsEmpty());
	}
	// #932: with the file read-only from the start, a saving action refuses before any change.
	{
		const TSharedPtr<FJsonObject> Refused = Call(Registry, TEXT("compile_state_tree"), Args(Tree));
		FString Reason;
		TestFalse(TEXT("compile on a read-only package is refused"), Succeeded(Refused));
		TestTrue(TEXT("the refusal names the cause"), Refused->TryGetStringField(TEXT("reason"), Reason) && Reason == TEXT("package_not_writable"));
		TestFalse(TEXT("nothing was compiled"), Refused->HasField(TEXT("compiled")));
	}
	Platform.SetReadOnly(*File, false);
	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FMCPEditScopeMultiplicityTest, FMCPStateTreeTestBase,
	"UE.MCP.Families.EditScope.Multiplicity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPEditScopeMultiplicityTest::RunTest(const FString& Parameters)
{
	using namespace MCPEditScopeTests;
	FMCPScopedTestMount Mount(TEXT("/UEMCPEditScopeMany/"), TEXT("UEMCPEditScopeMany"));
	const FString First = Mount.RootPath + TEXT("ST_First");
	const FString Second = Mount.RootPath + TEXT("ST_Second");
	FMCPHandlerRegistry Registry;
	FStateTreeHandlers::RegisterHandlers(Registry);
	if (!TestNotNull(TEXT("first tree"), MakeTree(Registry, First))) return false;
	if (!TestNotNull(TEXT("second tree"), MakeTree(Registry, Second))) return false;

	auto Paths = [](std::initializer_list<FString> In)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Items;
		for (const FString& Path : In) Items.Add(MakeShared<FJsonValueString>(Path));
		Out->SetArrayField(TEXT("assetPath"), Items);
		return Out;
	};

	{
		FMCPEditScope Edit(Paths({ First, Second }), TEXT("test_many"), TEXT("assetPath"),
			FMCPCommitPolicy::CompileSave(EMCPCompileFailure::SkipSave, /*bInEvenIfUnchanged=*/true));
		if (!TestFalse(TEXT("opens both"), Edit.Open<UStateTree>().IsValid())) return false;
		TestEqual(TEXT("two targets"), Edit.Num(), 2);
		TestEqual(TEXT("in the order given"), Edit.Path(1), Second);
		TSharedPtr<FJsonObject> Result = MCPSuccess();
		Edit.Finish(Result);
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (TestTrue(TEXT("one row per target"), Result->TryGetArrayField(TEXT("targets"), Rows) && Rows->Num() == 2))
		{
			for (const TSharedPtr<FJsonValue>& Row : *Rows)
			{
				TestTrue(TEXT("each row carries its compile result"), Row->AsObject()->HasField(TEXT("compiled")));
				TestTrue(TEXT("and its save result"), Row->AsObject()->HasField(TEXT("saved")));
			}
		}
	}
	{
		FMCPEditScope Edit(Paths({ First, Mount.RootPath + TEXT("Missing") }), TEXT("test_many"), TEXT("assetPath"), FMCPCommitPolicy::Read());
		TestTrue(TEXT("one missing target refuses the whole call"), Edit.Open<UStateTree>().IsValid());
	}
	return true;
}

#endif // UE_MCP_HAS_5_5_API
#endif // WITH_DEV_AUTOMATION_TESTS
