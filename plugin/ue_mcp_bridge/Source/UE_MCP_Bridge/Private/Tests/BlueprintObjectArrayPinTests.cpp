// #2004: an object-array variable has to be usable as an array in a graph.
// add_node used to place KismetArrayLibrary functions on a plain call node,
// whose wildcard array pins never take the wired-in type, and compile_blueprint
// reported "0 errors" for such a node whenever nothing on the execution path
// reached it. Assets live in a private temp mount and are read back from disk.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Blueprint/BlueprintHandlers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetArrayLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#include "Tests/MCPScopedTestMount.h"

namespace MCPObjectArrayPinTests
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

UBlueprint* MakeBlueprint(const FString& PackageName, const TCHAR* Name)
{
	UPackage* Package = CreatePackage(*PackageName);
	return FKismetEditorUtilities::CreateBlueprint(
		AActor::StaticClass(), Package, FName(Name), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
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

TSharedPtr<FJsonObject> AddNode(FMCPHandlerRegistry& Registry, const FString& AssetPath, const FString& GraphName,
	const TCHAR* NodeClass, const TSharedPtr<FJsonObject>& NodeParams)
{
	auto Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("assetPath"), AssetPath);
	Params->SetStringField(TEXT("graphName"), GraphName);
	Params->SetStringField(TEXT("nodeClass"), NodeClass);
	Params->SetObjectField(TEXT("nodeParams"), NodeParams);
	Params->SetBoolField(TEXT("deferCompile"), true);
	return Call(Registry, TEXT("add_node"), Params);
}

TSharedPtr<FJsonObject> ArrayCall(const TCHAR* Function)
{
	auto NodeParams = MakeShared<FJsonObject>();
	NodeParams->SetStringField(TEXT("functionName"), Function);
	NodeParams->SetStringField(TEXT("className"), TEXT("/Script/Engine.KismetArrayLibrary"));
	return NodeParams;
}

FString PinSpec(const TSharedPtr<FJsonObject>& AddResult, const TCHAR* PinName)
{
	const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
	if (!AddResult.IsValid() || !AddResult->TryGetArrayField(TEXT("pins"), Pins)) return FString();
	for (const TSharedPtr<FJsonValue>& Pin : *Pins)
	{
		const TSharedPtr<FJsonObject> Obj = Pin->AsObject();
		if (Obj.IsValid() && Obj->GetStringField(TEXT("name")) == PinName) return Obj->GetStringField(TEXT("typeSpec"));
	}
	return FString();
}

bool IsObjectArrayOf(const UEdGraphPin* Pin, const UClass* ElementClass)
{
	return Pin
		&& Pin->PinType.IsArray()
		&& Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object
		&& Pin->PinType.PinSubCategoryObject.Get() == ElementClass;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPBlueprintObjectArrayPinTest,
	"UE.MCP.Blueprint.ObjectArray.PinTypesAndCompile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBlueprintObjectArrayPinTest::RunTest(const FString& Parameters)
{
	using namespace MCPObjectArrayPinTests;
	FMCPScopedTestMount Mount(
		TEXT("/UEMCPObjectArray_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"), TEXT("UEMCPObjectArray"));
	const FString ItemPackage = Mount.RootPath + TEXT("BP_Item");
	const FString HolderPackage = Mount.RootPath + TEXT("BP_Holder");
	const FString HolderObjectPath = HolderPackage + TEXT(".BP_Holder");

	UBlueprint* Item = MakeBlueprint(ItemPackage, TEXT("BP_Item"));
	UBlueprint* Holder = MakeBlueprint(HolderPackage, TEXT("BP_Holder"));
	if (!TestNotNull(TEXT("element Blueprint created"), Item) || !TestNotNull(TEXT("holder Blueprint created"), Holder)) return false;
	FString SaveReason;
	if (!TestTrue(TEXT("element Blueprint saved: ") + SaveReason, SaveAssetPackageChecked(Item, SaveReason))) return false;
	UClass* ItemClass = Item->GeneratedClass;
	const FString ItemClassPath = ItemClass->GetPathName();
	const FString GraphName = Holder->UbergraphPages[0]->GetName();

	FMCPHandlerRegistry Registry;
	FBlueprintHandlers::RegisterHandlers(Registry);

	// The spelling from the report: a Blueprint class inside TArray<>.
	auto AddVar = MakeShared<FJsonObject>();
	AddVar->SetStringField(TEXT("assetPath"), HolderPackage);
	AddVar->SetStringField(TEXT("name"), TEXT("Items"));
	AddVar->SetStringField(TEXT("varType"), FString::Printf(TEXT("TArray<object:%s>"), *ItemClassPath));
	const TSharedPtr<FJsonObject> Added = Call(Registry, TEXT("add_variable"), AddVar);
	if (!TestTrue(TEXT("add_variable succeeds: ") + Describe(Added), Succeeded(Added))) return false;
	TestEqual(TEXT("the variable resolves to an object array of the element class"),
		Added->GetStringField(TEXT("resolvedType")), FString::Printf(TEXT("object:%s[]"), *ItemClassPath));

	auto GetParams = MakeShared<FJsonObject>();
	GetParams->SetStringField(TEXT("variableName"), TEXT("Items"));
	const TSharedPtr<FJsonObject> Getter = AddNode(Registry, HolderPackage, GraphName, TEXT("GetVar"), GetParams);
	if (!TestTrue(TEXT("getter placed: ") + Describe(Getter), Succeeded(Getter))) return false;
	TestEqual(TEXT("add_node reports the getter pin as the full array type, not just 'object'"),
		PinSpec(Getter, TEXT("Items")), FString::Printf(TEXT("object:%s[]"), *ItemClassPath));

	const TSharedPtr<FJsonObject> Length = AddNode(Registry, HolderPackage, GraphName, TEXT("CallFunction"), ArrayCall(TEXT("Array_Length")));
	const TSharedPtr<FJsonObject> Clear = AddNode(Registry, HolderPackage, GraphName, TEXT("CallFunction"), ArrayCall(TEXT("Array_Clear")));
	if (!TestTrue(TEXT("Array_Length placed: ") + Describe(Length), Succeeded(Length))) return false;
	if (!TestTrue(TEXT("Array_Clear placed: ") + Describe(Clear), Succeeded(Clear))) return false;
	TestEqual(TEXT("Array_Length gets the array-function node class the palette uses"),
		Length->GetStringField(TEXT("nodeClass")), FString(TEXT("K2Node_CallArrayFunction")));
	TestEqual(TEXT("Array_Clear gets the array-function node class the palette uses"),
		Clear->GetStringField(TEXT("nodeClass")), FString(TEXT("K2Node_CallArrayFunction")));

	for (const TSharedPtr<FJsonObject>& Target : { Length, Clear })
	{
		auto Connect = MakeShared<FJsonObject>();
		Connect->SetStringField(TEXT("assetPath"), HolderPackage);
		Connect->SetStringField(TEXT("graphName"), GraphName);
		Connect->SetStringField(TEXT("sourceNodeId"), Getter->GetStringField(TEXT("nodeId")));
		Connect->SetStringField(TEXT("sourcePin"), TEXT("Items"));
		Connect->SetStringField(TEXT("targetNodeId"), Target->GetStringField(TEXT("nodeId")));
		Connect->SetStringField(TEXT("targetPin"), TEXT("TargetArray"));
		Connect->SetBoolField(TEXT("deferCompile"), true);
		const TSharedPtr<FJsonObject> Connected = Call(Registry, TEXT("connect_pins"), Connect);
		TestTrue(TEXT("getter wires into the array function: ") + Describe(Connected), Succeeded(Connected));
	}

	auto CompileParams = MakeShared<FJsonObject>();
	CompileParams->SetStringField(TEXT("assetPath"), HolderPackage);
	const TSharedPtr<FJsonObject> Compiled = Call(Registry, TEXT("compile_blueprint"), CompileParams);
	if (!TestTrue(TEXT("compile_blueprint answers"), Succeeded(Compiled))) return false;
	TestTrue(TEXT("the wired array graph compiles"), Compiled->GetBoolField(TEXT("compiled")));
	TestEqual(TEXT("no compile errors"), static_cast<int32>(Compiled->GetNumberField(TEXT("errors"))), 0);
	TestEqual(TEXT("no undetermined pins"), Compiled->GetArrayField(TEXT("undeterminedPins")).Num(), 0);

	// A plain call node over an array function is what add_node used to make.
	// Wired but off the execution path, it must fail the compile, not report 0.
	UEdGraph* Graph = Holder->UbergraphPages[0];
	UK2Node_CallFunction* Legacy = NewObject<UK2Node_CallFunction>(Graph, NAME_None, RF_Transactional);
	Legacy->SetFromFunction(UKismetArrayLibrary::StaticClass()->FindFunctionByName(TEXT("Array_Clear")));
	Graph->AddNode(Legacy, false, false);
	Legacy->CreateNewGuid();
	Legacy->AllocateDefaultPins();
	UEdGraphNode* GetterNode = FindByGuid(Graph, Getter->GetStringField(TEXT("nodeId")));
	if (!TestNotNull(TEXT("getter node found"), GetterNode)) return false;
	GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(GetterNode->FindPin(TEXT("Items")), Legacy->FindPin(TEXT("TargetArray")));
	const TSharedPtr<FJsonObject> Broken = Call(Registry, TEXT("compile_blueprint"), CompileParams);
	if (!TestTrue(TEXT("compile_blueprint answers for the broken graph"), Succeeded(Broken))) return false;
	TestFalse(TEXT("a wired node with an undetermined pin fails the compile"), Broken->GetBoolField(TEXT("compiled")));
	TestTrue(TEXT("the undetermined pin is counted as an error"), Broken->GetNumberField(TEXT("errors")) >= 1);
	bool bNamed = false;
	for (const TSharedPtr<FJsonValue>& Entry : Broken->GetArrayField(TEXT("undeterminedPins")))
	{
		const TSharedPtr<FJsonObject> Obj = Entry->AsObject();
		bNamed |= Obj.IsValid() && Obj->GetStringField(TEXT("nodeId")) == Legacy->NodeGuid.ToString()
			&& Obj->GetStringField(TEXT("severity")) == TEXT("error");
	}
	TestTrue(TEXT("undeterminedPins names the broken node"), bNamed);
	Graph->RemoveNode(Legacy);

	// Read the graph back from disk: the types have to be what was saved.
	FKismetEditorUtilities::CompileBlueprint(Holder);
	if (!TestTrue(TEXT("holder saved: ") + SaveReason, SaveAssetPackageChecked(Holder, SaveReason))) return false;
	const FString LengthId = Length->GetStringField(TEXT("nodeId"));
	const FString ClearId = Clear->GetStringField(TEXT("nodeId"));
	const FString GetterId = Getter->GetStringField(TEXT("nodeId"));
	Item = nullptr;
	Holder = nullptr;
	Graph = nullptr;
	GetterNode = nullptr;
	Legacy = nullptr;
	ItemClass = nullptr;
	UEMCPTests::EvictPackagesUnderRoot(Mount.RootPath);

	UBlueprint* Reloaded = LoadObject<UBlueprint>(nullptr, *HolderObjectPath);
	if (!TestNotNull(TEXT("holder reloads from disk"), Reloaded)) return false;
	UClass* ReloadedItemClass = LoadObject<UClass>(nullptr, *ItemClassPath);
	if (!TestNotNull(TEXT("element class reloads"), ReloadedItemClass)) return false;
	UEdGraph* ReloadedGraph = Reloaded->UbergraphPages[0];

	UEdGraphNode* ReloadedGetter = FindByGuid(ReloadedGraph, GetterId);
	if (!TestNotNull(TEXT("getter came back"), ReloadedGetter)) return false;
	TestTrue(TEXT("getter pin is an array of the element class after reload"),
		IsObjectArrayOf(ReloadedGetter->FindPin(TEXT("Items")), ReloadedItemClass));
	for (const FString& Id : { LengthId, ClearId })
	{
		UEdGraphNode* Node = FindByGuid(ReloadedGraph, Id);
		if (!TestNotNull(TEXT("array function node came back"), Node)) continue;
		TestTrue(TEXT("array function node is a K2Node_CallArrayFunction after reload"), Node->IsA<UK2Node_CallArrayFunction>());
		const UEdGraphPin* TargetArray = Node->FindPin(TEXT("TargetArray"));
		TestTrue(TEXT("TargetArray took the element type and is still linked after reload"),
			IsObjectArrayOf(TargetArray, ReloadedItemClass) && TargetArray->LinkedTo.Num() == 1);
	}

	const TSharedPtr<FJsonObject> Recompiled = Call(Registry, TEXT("compile_blueprint"), CompileParams);
	TestTrue(TEXT("the reloaded graph compiles clean"), Succeeded(Recompiled) && Recompiled->GetBoolField(TEXT("compiled")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
