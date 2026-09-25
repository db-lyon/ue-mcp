// #1086: nodes authored through handlers must arrive with the flag the
// Blueprint editor otherwise repairs on open. Assets live in a private temp
// mount; this test never opens an asset editor or touches the user's undo stack.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/AnimationHandlers.h"
#include "Handlers/BlueprintHandlers.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "K2Node.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "Tests/MCPScopedTestMount.h"

namespace MCPBlueprintTransactionalTests
{
struct FProbe
{
	// First member, so it is destroyed last and evicts the probe package.
	FMCPScopedTestMount Mount;
	FString Root;
	FString PackageName;
	FString ObjectPath;

	FProbe()
		: Mount(TEXT("/UEMCPNodeFlags_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"), TEXT("UEMCPNodeFlags"))
		, Root(Mount.RootPath)
		, PackageName(Root + TEXT("BP_Probe"))
		, ObjectPath(PackageName + TEXT(".BP_Probe"))
	{
	}

	UBlueprint* Create(bool bAnimation = false) const
	{
		UPackage* Package = CreatePackage(*PackageName);
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			bAnimation ? UAnimInstance::StaticClass() : AActor::StaticClass(),
			Package, FName(TEXT("BP_Probe")), BPTYPE_Normal,
			bAnimation ? UAnimBlueprint::StaticClass() : UBlueprint::StaticClass(),
			bAnimation ? UAnimBlueprintGeneratedClass::StaticClass() : UBlueprintGeneratedClass::StaticClass());
		if (UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Blueprint))
		{
			// A template is a supported skeleton-free AnimBP, so the graph-node
			// regression needs neither project animation assets nor a fake rig.
			AnimBlueprint->bIsTemplate = true;
		}
		return Blueprint;
	}

	TSharedPtr<FJsonObject> Params() const
	{
		auto Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("assetPath"), PackageName);
		return Result;
	}

	void ReleasePackage() const
	{
		if (UPackage* Package = FindPackage(nullptr, *PackageName))
		{
			// Include the generated classes as well as the asset. Keeping any
			// standalone export alive would make a reload read the object hash.
			ForEachObjectWithPackage(Package, [](UObject* Object)
			{
				Object->ClearFlags(RF_Standalone);
				return true;
			});
			Package->SetDirtyFlag(false);
			Package->ClearFlags(RF_Standalone);
			ResetLoaders(Package);
		}
	}
};

static int32 CheckFlags(FAutomationTestBase& Test, UBlueprint* Blueprint, const FString& Stage)
{
	TArray<UEdGraphNode*> Nodes;
	FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, Nodes);
	Test.TestTrue(Stage + TEXT(": graph contains nodes"), Nodes.Num() > 0);
	for (UEdGraphNode* Node : Nodes)
	{
		Test.TestTrue(FString::Printf(TEXT("%s: %s is transactional"), *Stage, *Node->GetPathName()),
			Node->HasAnyFlags(RF_Transactional));
	}
	return Nodes.Num();
}

static bool Author(FAutomationTestBase& Test, FMCPHandlerRegistry& Registry,
	UBlueprint* Blueprint, const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
{
	TArray<UEdGraphNode*> Before;
	FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, Before);
	const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(Method, Params);
	const TSharedPtr<FJsonObject> Result = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
	bool bSuccess = false;
	FString Error;
	if (Result.IsValid())
	{
		Result->TryGetBoolField(TEXT("success"), bSuccess);
		Result->TryGetStringField(TEXT("error"), Error);
	}
	if (!Test.TestTrue(FString::Printf(TEXT("%s succeeds: %s"), Method, *Error), bSuccess)) return false;
	const int32 Count = CheckFlags(Test, Blueprint, Method);
	return Test.TestTrue(FString(Method) + TEXT(": created a node rather than taking an existing-node shortcut"), Count > Before.Num());
}

static bool CheckPersistenceAndRepair(FAutomationTestBase& Test, const FProbe& Probe, UBlueprint* Blueprint)
{
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	if (!Test.TestTrue(TEXT("authored Blueprint compiles"),
		Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings)) return false;
	const int32 SavedNodeCount = CheckFlags(Test, Blueprint, TEXT("before save"));
	FString Reason;
	const bool bSaved = SaveAssetPackageChecked(Blueprint, Reason);
	if (!Test.TestTrue(TEXT("authored Blueprint saves: ") + Reason, bSaved)) return false;
	const TWeakObjectPtr<UBlueprint> Previous(Blueprint);
	Blueprint = nullptr;
	Probe.ReleasePackage();
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	if (!Test.TestFalse(TEXT("the original Blueprint was evicted"), Previous.IsValid())) return false;
	if (!Test.TestNull(TEXT("reload cannot reuse the original Blueprint"), FindObject<UBlueprint>(nullptr, *Probe.ObjectPath))) return false;
	Blueprint = LoadObject<UBlueprint>(nullptr, *Probe.ObjectPath);
	if (!Test.TestNotNull(TEXT("authored Blueprint reloads from disk"), Blueprint)) return false;
	Test.TestEqual(TEXT("all authored graph nodes survived the save"),
		CheckFlags(Test, Blueprint, TEXT("after disk reload")), SavedNodeCount);

	// This is the repair step used when the Blueprint editor opens the asset.
	// Run it only after the disk assertions: it would repair a broken fixture.
	const EBlueprintStatus BeforeRepair = Blueprint->Status;
	if (!Test.TestTrue(TEXT("reloaded Blueprint is clean before the repair check"),
		BeforeRepair == BS_UpToDate || BeforeRepair == BS_UpToDateWithWarnings)) return false;
	FBlueprintEditorUtils::UpdateTransactionalFlags(Blueprint);
	Test.TestEqual(TEXT("opening the Blueprint needs no transactional repair"),
		static_cast<uint8>(Blueprint->Status), static_cast<uint8>(BeforeRepair));

	// Negative control: prove the engine check still notices a missing flag.
	TArray<UK2Node*> Nodes;
	FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, Nodes);
	if (!Test.TestTrue(TEXT("the fixture contains K2 nodes"), Nodes.Num() > 0)) return false;
	Nodes[0]->ClearFlags(RF_Transactional);
	FBlueprintEditorUtils::UpdateTransactionalFlags(Blueprint);
	Test.TestTrue(TEXT("missing flag triggers the editor repair"), Blueprint->Status == BS_Dirty);
	Test.TestTrue(TEXT("editor repair restores the flag"), Nodes[0]->HasAnyFlags(RF_Transactional));
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPBlueprintTransactionalNodesTest,
	"UE.MCP.Blueprint.TransactionalNodes.BlueprintHandlers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBlueprintTransactionalNodesTest::RunTest(const FString& Parameters)
{
	using namespace MCPBlueprintTransactionalTests;
	const FProbe Probe;
	UBlueprint* Blueprint = Probe.Create();
	if (!TestNotNull(TEXT("Blueprint fixture created"), Blueprint)) return false;
	FMCPHandlerRegistry Registry;
	FBlueprintHandlers::RegisterHandlers(Registry);

	auto Add = Probe.Params();
	Add->SetStringField(TEXT("nodeClass"), TEXT("CallFunction"));
	auto NodeParams = MakeShared<FJsonObject>();
	NodeParams->SetStringField(TEXT("functionName"), TEXT("PrintString"));
	NodeParams->SetStringField(TEXT("className"), TEXT("/Script/Engine.KismetSystemLibrary"));
	Add->SetObjectField(TEXT("nodeParams"), NodeParams);
	if (!Author(*this, Registry, Blueprint, TEXT("add_node"), Add)) return false;

	auto Event = Probe.Params();
	Event->SetStringField(TEXT("eventName"), TEXT("TransactionalProbe"));
	if (!Author(*this, Registry, Blueprint, TEXT("add_custom_event"), Event)) return false;

	auto Override = Probe.Params();
	Override->SetStringField(TEXT("functionName"), TEXT("ReceiveAnyDamage"));
	if (!Author(*this, Registry, Blueprint, TEXT("override_function"), Override)) return false;

	// The legacy and current parameter handlers have separate result-node
	// constructors. Give each an empty function so both constructors execute.
	for (const TCHAR* Method : {TEXT("add_function_parameter"), TEXT("edit_graph_parameters")})
	{
		const FString FunctionName = FString(TEXT("Probe_")) + Method;
		auto Function = Probe.Params();
		Function->SetStringField(TEXT("functionName"), FunctionName);
		if (!Author(*this, Registry, Blueprint, TEXT("create_function"), Function)) return false;
		auto Output = Probe.Params();
		Output->SetStringField(TEXT("functionName"), FunctionName);
		Output->SetStringField(TEXT("parameterName"), TEXT("Value"));
		Output->SetStringField(TEXT("parameterType"), TEXT("bool"));
		Output->SetBoolField(TEXT("isOutput"), true);
		Output->SetStringField(TEXT("op"), TEXT("add"));
		if (!Author(*this, Registry, Blueprint, Method, Output)) return false;
	}
	return CheckPersistenceAndRepair(*this, Probe, Blueprint);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPAnimationTransactionalNodesTest,
	"UE.MCP.Blueprint.TransactionalNodes.AnimationHandlers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAnimationTransactionalNodesTest::RunTest(const FString& Parameters)
{
	using namespace MCPBlueprintTransactionalTests;
	const FProbe Probe;
	UBlueprint* Blueprint = Probe.Create(true);
	if (!TestNotNull(TEXT("template AnimBlueprint fixture created"), Blueprint)) return false;
	FMCPHandlerRegistry Registry;
	FAnimationHandlers::RegisterHandlers(Registry);

	auto Machine = Probe.Params();
	Machine->SetStringField(TEXT("name"), TEXT("ProbeMachine"));
	if (!Author(*this, Registry, Blueprint, TEXT("create_state_machine"), Machine)) return false;
	// An unconnected evaluator exercises PlaceAnimNode without requiring an
	// animation sequence or PoseSearch/Chooser fixture unrelated to the flags.
	auto Evaluator = Probe.Params();
	Evaluator->SetBoolField(TEXT("connectToOutput"), false);
	if (!Author(*this, Registry, Blueprint, TEXT("add_sequence_evaluator"), Evaluator)) return false;
	return CheckPersistenceAndRepair(*this, Probe, Blueprint);
}

#endif // WITH_DEV_AUTOMATION_TESTS
