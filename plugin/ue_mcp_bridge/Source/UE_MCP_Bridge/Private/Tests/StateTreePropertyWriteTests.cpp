// A StateTree property write that lands nowhere must fail: an unknown
// instanceProperties key, a value that does not import, or a set_* value that
// does not parse. The tree lives in a private temp mount.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/StateTree/StateTreeHandlers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "UObject/Package.h"
#include "Tests/MCPScopedTestMount.h"

namespace MCPStateTreeWriteTests
{
	TSharedPtr<FJsonObject> Call(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
	{
		const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(Method, Params);
		return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : MakeShared<FJsonObject>();
	}

	bool Succeeded(const TSharedPtr<FJsonObject>& Result)
	{
		bool bSuccess = false;
		return Result.IsValid() && Result->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
	}

	/** The task's Duration, read through reflection so no task header is needed. */
	bool ReadDuration(const UStateTreeState* State, float& OutValue)
	{
		if (!State || State->Tasks.Num() != 1 || !State->Tasks[0].Instance.IsValid()) return false;
		const UScriptStruct* Struct = State->Tasks[0].Instance.GetScriptStruct();
		const FFloatProperty* Prop = CastField<FFloatProperty>(Struct->FindPropertyByName(TEXT("Duration")));
		if (!Prop) return false;
		OutValue = Prop->GetPropertyValue_InContainer(State->Tasks[0].Instance.GetMemory());
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPStateTreePropertyWriteTest,
	"UE.MCP.StateTree.PropertyWrite.RefusesDroppedWrites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPStateTreePropertyWriteTest::RunTest(const FString& Parameters)
{
	using namespace MCPStateTreeWriteTests;
	// The fixture carries no schema, and the editor says so when it validates.
	AddExpectedError(TEXT("does not have a schema"), EAutomationExpectedErrorFlags::Contains, 0);
	FMCPScopedTestMount Mount(
		TEXT("/UEMCPStateTreeWrite_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"), TEXT("UEMCPStateTreeWrite"));
	const FString PackageName = Mount.RootPath + TEXT("ST_WriteProbe");

	UPackage* Package = CreatePackage(*PackageName);
	UStateTree* Tree = NewObject<UStateTree>(Package, TEXT("ST_WriteProbe"), RF_Public | RF_Standalone | RF_Transactional);
	UStateTreeEditorData* EditorData = NewObject<UStateTreeEditorData>(Tree, NAME_None, RF_Transactional);
	Tree->EditorData = EditorData;

	FMCPHandlerRegistry Registry;
	FStateTreeHandlers::RegisterHandlers(Registry);

	auto AddRoot = MakeShared<FJsonObject>();
	AddRoot->SetStringField(TEXT("assetPath"), PackageName);
	AddRoot->SetStringField(TEXT("name"), TEXT("Root"));
	if (!TestTrue(TEXT("root state added"), Succeeded(Call(Registry, TEXT("add_state_tree_state"), AddRoot)))) return false;
	if (!TestEqual(TEXT("one root state"), EditorData->SubTrees.Num(), 1)) return false;
	UStateTreeState* Root = EditorData->SubTrees[0];

	auto AddTask = [&](const TSharedPtr<FJsonObject>& InstanceProperties)
	{
		auto Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("assetPath"), PackageName);
		Args->SetStringField(TEXT("statePath"), TEXT("Root"));
		Args->SetStringField(TEXT("structType"), TEXT("StateTreeDelayTask"));
		Args->SetObjectField(TEXT("instanceProperties"), InstanceProperties);
		return Call(Registry, TEXT("add_state_tree_task"), Args);
	};

	{
		auto Unknown = MakeShared<FJsonObject>();
		Unknown->SetNumberField(TEXT("NoSuchProperty"), 1.0);
		const TSharedPtr<FJsonObject> Result = AddTask(Unknown);
		TestFalse(TEXT("an unknown instanceProperties key fails the add"), Succeeded(Result));
		TestEqual(TEXT("a failed add leaves no half-built task behind"), Root->Tasks.Num(), 0);
	}
	{
		auto Bad = MakeShared<FJsonObject>();
		Bad->SetStringField(TEXT("Duration"), TEXT("banana"));
		TestFalse(TEXT("a value that does not import fails the add"), Succeeded(AddTask(Bad)));
		TestEqual(TEXT("still no task after a failed import"), Root->Tasks.Num(), 0);
	}
	{
		auto Good = MakeShared<FJsonObject>();
		Good->SetNumberField(TEXT("Duration"), 2.5);
		if (!TestTrue(TEXT("a valid instanceProperties object adds the task"), Succeeded(AddTask(Good)))) return false;
		float Duration = 0.f;
		if (!TestTrue(TEXT("the task's Duration is readable"), ReadDuration(Root, Duration))) return false;
		TestEqual(TEXT("the instance property was written"), Duration, 2.5f);
	}
	{
		auto Set = MakeShared<FJsonObject>();
		Set->SetStringField(TEXT("assetPath"), PackageName);
		Set->SetStringField(TEXT("statePath"), TEXT("Root"));
		Set->SetNumberField(TEXT("taskIndex"), 0);
		Set->SetStringField(TEXT("propertyName"), TEXT("Duration"));
		Set->SetStringField(TEXT("value"), TEXT("banana"));
		TestFalse(TEXT("set_state_tree_task_instance_property refuses a value that does not parse"),
			Succeeded(Call(Registry, TEXT("set_state_tree_task_instance_property"), Set)));
		float Duration = 0.f;
		ReadDuration(Root, Duration);
		TestEqual(TEXT("the refused write left the previous value"), Duration, 2.5f);
	}

	Package->SetDirtyFlag(false);
	return true;
}

#endif
