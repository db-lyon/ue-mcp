// #2003: edit_graph_parameters op=rename has to rename the parameter
// definition (the entry node's UserDefinedPins), not only the live pin, and
// carry the rename to every caller. The report's failure showed only after an
// editor restart, so this test saves, unloads every package and reads the
// function library and its caller back from disk, with the caller unloaded
// while the rename runs. Assets live in a private temp mount.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Blueprint/BlueprintHandlers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "Tests/MCPScopedTestMount.h"

namespace MCPParamRenamePersistTests
{
TSharedPtr<FJsonObject> Call(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
{
	const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(Method, Params);
	return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
}

bool Succeeded(const TSharedPtr<FJsonObject>& Result)
{
	bool bSuccess = false;
	return Result.IsValid() && Result->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
}

FString Describe(const TSharedPtr<FJsonObject>& Result)
{
	if (!Result.IsValid()) return TEXT("(no result)");
	FString Error;
	return Result->TryGetStringField(TEXT("error"), Error) ? Error : FString(TEXT("(no error field)"));
}

UEdGraphNode* FindByGuid(UEdGraph* Graph, const FString& Id)
{
	FGuid Guid;
	if (!Graph || !FGuid::Parse(Id, Guid)) return nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->NodeGuid == Guid) return Node;
	}
	return nullptr;
}

UK2Node_FunctionEntry* FindEntry(UBlueprint* Blueprint, const TCHAR* FunctionName)
{
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (!Graph || Graph->GetName() != FunctionName) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node)) return Entry;
		}
	}
	return nullptr;
}

bool HasUserPin(const UK2Node_FunctionEntry* Entry, const TCHAR* Name)
{
	for (const TSharedPtr<FUserPinInfo>& Info : Entry->UserDefinedPins)
	{
		if (Info.IsValid() && Info->PinName == FName(Name)) return true;
	}
	return false;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPBlueprintParameterRenamePersistsTest,
	"UE.MCP.Blueprint.EditGraphParameters.RenamePersists",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBlueprintParameterRenamePersistsTest::RunTest(const FString& Parameters)
{
	using namespace MCPParamRenamePersistTests;
	FMCPScopedTestMount Mount(
		TEXT("/UEMCPParamRename_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"), TEXT("UEMCPParamRename"));
	const FString LibPackage = Mount.RootPath + TEXT("BFL_Factions");
	const FString LibObjectPath = LibPackage + TEXT(".BFL_Factions");
	const FString CallerPackage = Mount.RootPath + TEXT("BP_Caller");
	const FString CallerObjectPath = CallerPackage + TEXT(".BP_Caller");

	UBlueprint* Lib = FKismetEditorUtilities::CreateBlueprint(
		UBlueprintFunctionLibrary::StaticClass(), CreatePackage(*LibPackage), FName(TEXT("BFL_Factions")),
		BPTYPE_FunctionLibrary, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	UBlueprint* Caller = FKismetEditorUtilities::CreateBlueprint(
		AActor::StaticClass(), CreatePackage(*CallerPackage), FName(TEXT("BP_Caller")),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("library created"), Lib) || !TestNotNull(TEXT("caller created"), Caller)) return false;
	const FString LibClassPath = LibObjectPath + TEXT("_C");
	const FString CallerGraph = Caller->UbergraphPages[0]->GetName();

	FMCPHandlerRegistry Registry;
	FBlueprintHandlers::RegisterHandlers(Registry);

	auto Create = MakeShared<FJsonObject>();
	Create->SetStringField(TEXT("assetPath"), LibPackage);
	Create->SetStringField(TEXT("functionName"), TEXT("Pick"));
	if (!TestTrue(TEXT("create_function succeeds"), Succeeded(Call(Registry, TEXT("create_function"), Create)))) return false;

	auto AddParam = MakeShared<FJsonObject>();
	AddParam->SetStringField(TEXT("assetPath"), LibPackage);
	AddParam->SetStringField(TEXT("functionName"), TEXT("Pick"));
	AddParam->SetStringField(TEXT("op"), TEXT("add"));
	AddParam->SetStringField(TEXT("parameterName"), TEXT("Faction"));
	AddParam->SetStringField(TEXT("parameterType"), TEXT("int"));
	const TSharedPtr<FJsonObject> ParamAdded = Call(Registry, TEXT("edit_graph_parameters"), AddParam);
	if (!TestTrue(TEXT("input parameter added: ") + Describe(ParamAdded), Succeeded(ParamAdded))) return false;

	// The caller wires an int variable into the parameter's pin, so a rename
	// that orphans the pin shows up as a lost wire.
	auto AddVar = MakeShared<FJsonObject>();
	AddVar->SetStringField(TEXT("assetPath"), CallerPackage);
	AddVar->SetStringField(TEXT("name"), TEXT("Team"));
	AddVar->SetStringField(TEXT("varType"), TEXT("int"));
	if (!TestTrue(TEXT("caller variable added"), Succeeded(Call(Registry, TEXT("add_variable"), AddVar)))) return false;

	auto AddNode = [&](const TCHAR* NodeClass, const TSharedPtr<FJsonObject>& NodeParams)
	{
		auto Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), CallerPackage);
		Params->SetStringField(TEXT("graphName"), CallerGraph);
		Params->SetStringField(TEXT("nodeClass"), NodeClass);
		Params->SetObjectField(TEXT("nodeParams"), NodeParams);
		return Call(Registry, TEXT("add_node"), Params);
	};
	auto CallParams = MakeShared<FJsonObject>();
	CallParams->SetStringField(TEXT("functionName"), TEXT("Pick"));
	CallParams->SetStringField(TEXT("className"), LibClassPath);
	const TSharedPtr<FJsonObject> CallNode = AddNode(TEXT("CallFunction"), CallParams);
	auto GetParams = MakeShared<FJsonObject>();
	GetParams->SetStringField(TEXT("variableName"), TEXT("Team"));
	const TSharedPtr<FJsonObject> Getter = AddNode(TEXT("GetVar"), GetParams);
	if (!TestTrue(TEXT("call node placed: ") + Describe(CallNode), Succeeded(CallNode))) return false;
	if (!TestTrue(TEXT("getter placed: ") + Describe(Getter), Succeeded(Getter))) return false;
	const FString CallId = CallNode->GetStringField(TEXT("nodeId"));

	auto Connect = MakeShared<FJsonObject>();
	Connect->SetStringField(TEXT("assetPath"), CallerPackage);
	Connect->SetStringField(TEXT("graphName"), CallerGraph);
	Connect->SetStringField(TEXT("sourceNodeId"), Getter->GetStringField(TEXT("nodeId")));
	Connect->SetStringField(TEXT("sourcePin"), TEXT("Team"));
	Connect->SetStringField(TEXT("targetNodeId"), CallId);
	Connect->SetStringField(TEXT("targetPin"), TEXT("Faction"));
	const TSharedPtr<FJsonObject> Connected = Call(Registry, TEXT("connect_pins"), Connect);
	if (!TestTrue(TEXT("variable wired into the parameter pin: ") + Describe(Connected), Succeeded(Connected))) return false;

	// Unload everything, then let the asset registry see the mount so the
	// caller is known as a referencer while it is not in memory.
	Lib = nullptr;
	Caller = nullptr;
	UEMCPTests::EvictPackagesUnderRoot(Mount.RootPath);
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AssetRegistry.ScanPathsSynchronous({ Mount.RootPath }, /*bForceRescan=*/true);

	auto Rename = MakeShared<FJsonObject>();
	Rename->SetStringField(TEXT("assetPath"), LibPackage);
	Rename->SetStringField(TEXT("functionName"), TEXT("Pick"));
	Rename->SetStringField(TEXT("op"), TEXT("rename"));
	Rename->SetStringField(TEXT("parameterName"), TEXT("Faction"));
	Rename->SetStringField(TEXT("newName"), TEXT("MyFaction"));
	const TSharedPtr<FJsonObject> Renamed = Call(Registry, TEXT("edit_graph_parameters"), Rename);
	if (!TestTrue(TEXT("rename succeeds: ") + Describe(Renamed), Succeeded(Renamed))) return false;
	TestEqual(TEXT("the caller's call site was renamed"), static_cast<int32>(Renamed->GetNumberField(TEXT("callSitesRenamed"))), 1);
	TestEqual(TEXT("the caller Blueprint was compiled and saved"), Renamed->GetArrayField(TEXT("callerBlueprints")).Num(), 1);

	auto Reserved = MakeShared<FJsonObject>();
	Reserved->Values = Rename->Values;
	Reserved->SetStringField(TEXT("parameterName"), TEXT("MyFaction"));
	Reserved->SetStringField(TEXT("newName"), TEXT("Self"));
	TestFalse(TEXT("a rename to the reserved name 'Self' is refused"), Succeeded(Call(Registry, TEXT("edit_graph_parameters"), Reserved)));

	// The restart from the report: unload both and read them back from disk.
	UEMCPTests::EvictPackagesUnderRoot(Mount.RootPath);
	UBlueprint* ReloadedLib = LoadObject<UBlueprint>(nullptr, *LibObjectPath);
	if (!TestNotNull(TEXT("library reloads"), ReloadedLib)) return false;
	UK2Node_FunctionEntry* Entry = FindEntry(ReloadedLib, TEXT("Pick"));
	if (!TestNotNull(TEXT("function entry reloads"), Entry)) return false;
	TestTrue(TEXT("the saved parameter definition carries the new name"), HasUserPin(Entry, TEXT("MyFaction")));
	TestFalse(TEXT("the saved parameter definition no longer carries the old name"), HasUserPin(Entry, TEXT("Faction")));
	TestNotNull(TEXT("the reloaded entry node has the renamed pin"), Entry->FindPin(TEXT("MyFaction")));
	TestNull(TEXT("the reloaded entry node has no pin under the old name"), Entry->FindPin(TEXT("Faction")));

	UBlueprint* ReloadedCaller = LoadObject<UBlueprint>(nullptr, *CallerObjectPath);
	if (!TestNotNull(TEXT("caller reloads"), ReloadedCaller)) return false;
	UEdGraphNode* ReloadedCall = FindByGuid(ReloadedCaller->UbergraphPages[0], CallId);
	if (!TestNotNull(TEXT("call node reloads"), ReloadedCall)) return false;
	const UEdGraphPin* NewPin = ReloadedCall->FindPin(TEXT("MyFaction"));
	TestTrue(TEXT("the call site has the renamed pin, still wired"), NewPin && !NewPin->bOrphanedPin && NewPin->LinkedTo.Num() == 1);
	TestNull(TEXT("the call site kept no pin under the old name"), ReloadedCall->FindPin(TEXT("Faction")));

	FCompilerResultsLog LibLog;
	FKismetEditorUtilities::CompileBlueprint(ReloadedLib, EBlueprintCompileOptions::None, &LibLog);
	TestEqual(TEXT("the reloaded library compiles clean"), LibLog.NumErrors, 0);
	FCompilerResultsLog CallerLog;
	FKismetEditorUtilities::CompileBlueprint(ReloadedCaller, EBlueprintCompileOptions::None, &CallerLog);
	TestEqual(TEXT("the reloaded caller compiles clean"), CallerLog.NumErrors, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
