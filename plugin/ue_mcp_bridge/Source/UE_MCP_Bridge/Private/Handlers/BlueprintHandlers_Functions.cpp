// Split from BlueprintHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FBlueprintHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in BlueprintHandlers.cpp::RegisterHandlers.

#include "BlueprintHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "BlueprintEditorLibrary.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_Event.h"
#include "ObjectEditorUtils.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/BlueprintFactory.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "UObject/TopLevelAssetPath.h"
#include "Misc/PackageName.h"
#include "Internationalization/Text.h"
#include "Editor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"


TSharedPtr<FJsonValue> FBlueprintHandlers::AddBlueprintInterface(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	FString InterfacePathStr;
	if (auto Err = RequireString(Params, TEXT("interfacePath"), InterfacePathStr)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
	}

	UClass* InterfaceClass = LoadObject<UClass>(nullptr, *InterfacePathStr);
	if (!InterfaceClass)
	{
		return MCPError(FString::Printf(TEXT("Interface not found: %s"), *InterfacePathStr));
	}

	// Idempotency: check if interface already implemented on this blueprint
	FTopLevelAssetPath InterfaceAssetPath(InterfaceClass->GetPathName());
	for (const FBPInterfaceDescription& Impl : Blueprint->ImplementedInterfaces)
	{
		if (Impl.Interface == InterfaceClass)
		{
			auto Existed = MCPSuccess();
			MCPSetExisted(Existed);
			Existed->SetStringField(TEXT("blueprintPath"), BlueprintPath);
			Existed->SetStringField(TEXT("interfacePath"), InterfacePathStr);
			return MCPResult(Existed);
		}
	}

	// Use FBlueprintEditorUtils to add interface
	FBlueprintEditorUtils::ImplementNewInterface(Blueprint, InterfaceAssetPath);

	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	// Save asset
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetStringField(TEXT("interfacePath"), InterfacePathStr);
	// No rollback: no paired remove_blueprint_interface handler yet.
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::CreateFunction(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString FunctionName;
	if (auto Err = RequireString(Params, TEXT("functionName"), FunctionName)) return Err;

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Idempotency: existing function graph short-circuits.
	for (UEdGraph* G : Blueprint->FunctionGraphs)
	{
		if (G && G->GetName() == FunctionName)
		{
			if (OnConflict == TEXT("error"))
			{
				return MCPError(FString::Printf(TEXT("Function '%s' already exists"), *FunctionName));
			}
			auto Existing = MCPSuccess();
			MCPSetExisted(Existing);
			Existing->SetStringField(TEXT("path"), AssetPath);
			Existing->SetStringField(TEXT("functionName"), FunctionName);
			return MCPResult(Existing);
		}
	}

	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint,
		FName(*FunctionName),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass()
	);
	if (!NewGraph)
	{
		return MCPError(FString::Printf(TEXT("Failed to create function: %s"), *FunctionName));
	}

	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, NewGraph, /*bIsUserCreated=*/true, /*SignatureFromObject=*/nullptr);

	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("functionName"), FunctionName);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), AssetPath);
	Payload->SetStringField(TEXT("functionName"), FunctionName);
	MCPSetRollback(Result, TEXT("delete_function"), Payload);

	return MCPResult(Result);
}


// #809: list_functions reported Blueprint->FunctionGraphs and nothing else, so
// interface implementations, the EventGraph, macros and collapsed graphs never
// showed up even though list_graphs reported them for the same asset. An audit
// built on the old output under-reported the consumers of a variable, the
// Blueprint still compiled, and the miss only surfaced as a PIE runtime error.
// Every graph the Blueprint owns is enumerated now, each entry tagged with
// `kind` and `source` so a caller can tell an interface implementation from a
// function it declared itself. `name` and `nodeCount` keep their old meaning.
namespace
{
	TSharedPtr<FJsonObject> MakeFunctionEntry(
		const FString& Name,
		const TCHAR* Kind,
		const TCHAR* Source,
		int32 NodeCount)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Name);
		Obj->SetNumberField(TEXT("nodeCount"), NodeCount);
		Obj->SetStringField(TEXT("kind"), Kind);
		Obj->SetStringField(TEXT("source"), Source);
		return Obj;
	}

	TSharedPtr<FJsonObject> MakeFunctionGraphEntry(UEdGraph* Graph, const TCHAR* Kind, const TCHAR* Source)
	{
		TSharedPtr<FJsonObject> Obj = MakeFunctionEntry(Graph->GetName(), Kind, Source, Graph->Nodes.Num());
		Obj->SetStringField(TEXT("graphName"), Graph->GetName());
		Obj->SetStringField(TEXT("objectPath"), Graph->GetPathName());
		return Obj;
	}

	/** Interface declaring FnName, whether implemented on this Blueprint or inherited. */
	UClass* FindDeclaringInterface(UBlueprint* Blueprint, const FName& FnName)
	{
		for (const FBPInterfaceDescription& Impl : Blueprint->ImplementedInterfaces)
		{
			UClass* IfaceClass = Impl.Interface;
			if (IfaceClass && IfaceClass->FindFunctionByName(FnName)) return IfaceClass;
		}
		if (UClass* ParentClass = Blueprint->ParentClass.Get())
		{
			for (const FImplementedInterface& Inherited : ParentClass->Interfaces)
			{
				UClass* IfaceClass = Inherited.Class;
				if (IfaceClass && IfaceClass->FindFunctionByName(FnName)) return IfaceClass;
			}
		}
		return nullptr;
	}

	/** Collapsed / nested graphs, recursively. list_graphs reports these too. */
	void CollectFunctionSubGraphs(UEdGraph* Parent, TSet<UEdGraph*>& Seen, TArray<TSharedPtr<FJsonValue>>& Out)
	{
		if (!Parent) return;
		for (UEdGraph* Sub : Parent->SubGraphs)
		{
			if (!Sub || Seen.Contains(Sub)) continue;
			Seen.Add(Sub);
			TSharedPtr<FJsonObject> Obj = MakeFunctionGraphEntry(Sub, TEXT("subgraph"), TEXT("own"));
			Obj->SetStringField(TEXT("parentGraph"), Parent->GetName());
			Out.Add(MakeShared<FJsonValueObject>(Obj));
			CollectFunctionSubGraphs(Sub, Seen, Out);
		}
	}
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ListBlueprintFunctions(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	// Off by default: the inherited surface is large and list_overridable_functions
	// already owns it. Opt in when one combined view of the callable surface is wanted.
	const bool bIncludeInherited = OptionalBool(Params, TEXT("includeInherited"), false);

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Skeleton class super is the reliable parent during an in-editor edit; fall
	// back to ParentClass when the skeleton has not been generated yet.
	UClass* ParentClass = Blueprint->SkeletonGeneratedClass
		? Blueprint->SkeletonGeneratedClass->GetSuperClass()
		: Blueprint->ParentClass.Get();

	TArray<TSharedPtr<FJsonValue>> Functions;
	TSet<FString> ImplementedNames;
	// A graph reachable from two of the arrays below is reported once.
	TSet<UEdGraph*> SeenGraphs;

	// 1. Function graphs on this Blueprint. A name that resolves against the
	//    parent chain is an override, and one that resolves against an interface
	//    is an implementation of that interface.
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (!Graph || SeenGraphs.Contains(Graph)) continue;
		SeenGraphs.Add(Graph);
		const FString Name = Graph->GetName();
		const FName FnName(*Name);
		ImplementedNames.Add(Name);

		UClass* DeclaringInterface = FindDeclaringInterface(Blueprint, FnName);
		UFunction* ParentFn = ParentClass ? ParentClass->FindFunctionByName(FnName) : nullptr;

		TSharedPtr<FJsonObject> Obj;
		if (DeclaringInterface)
		{
			Obj = MakeFunctionGraphEntry(Graph, TEXT("interface"), TEXT("interface"));
			Obj->SetStringField(TEXT("declaringClass"), DeclaringInterface->GetName());
			Obj->SetStringField(TEXT("declaringClassPath"), DeclaringInterface->GetPathName());
		}
		else if (ParentFn)
		{
			Obj = MakeFunctionGraphEntry(Graph, TEXT("override"), TEXT("parent"));
			if (UClass* Owner = ParentFn->GetOwnerClass())
			{
				Obj->SetStringField(TEXT("declaringClass"), Owner->GetName());
				Obj->SetStringField(TEXT("declaringClassPath"), Owner->GetPathName());
			}
		}
		else
		{
			Obj = MakeFunctionGraphEntry(Graph, TEXT("function"), TEXT("own"));
		}
		Functions.Add(MakeShared<FJsonValueObject>(Obj));
		CollectFunctionSubGraphs(Graph, SeenGraphs, Functions);
	}

	// 2. Interface implementations. Non-event interface functions get their own
	//    graph under the interface description, never under FunctionGraphs, which
	//    is exactly why they were invisible before.
	for (const FBPInterfaceDescription& Impl : Blueprint->ImplementedInterfaces)
	{
		UClass* IfaceClass = Impl.Interface;
		for (UEdGraph* Graph : Impl.Graphs)
		{
			if (!Graph || SeenGraphs.Contains(Graph)) continue;
			SeenGraphs.Add(Graph);
			ImplementedNames.Add(Graph->GetName());
			TSharedPtr<FJsonObject> Obj = MakeFunctionGraphEntry(Graph, TEXT("interface"), TEXT("interface"));
			if (IfaceClass)
			{
				Obj->SetStringField(TEXT("declaringClass"), IfaceClass->GetName());
				Obj->SetStringField(TEXT("declaringClassPath"), IfaceClass->GetPathName());
			}
			Functions.Add(MakeShared<FJsonValueObject>(Obj));
			CollectFunctionSubGraphs(Graph, SeenGraphs, Functions);
		}
	}

	// 3. Ubergraph pages (EventGraph and friends) plus every entry point on them.
	//    Custom events and implemented events are callable surface, and the pages
	//    themselves hold the node bodies an audit has to read.
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (!Graph || SeenGraphs.Contains(Graph)) continue;
		SeenGraphs.Add(Graph);
		Functions.Add(MakeShared<FJsonValueObject>(MakeFunctionGraphEntry(Graph, TEXT("event_graph"), TEXT("own"))));

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			FString EventName;
			const TCHAR* EventSource = TEXT("own");
			UClass* DeclaringClass = nullptr;

			if (UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Node))
			{
				EventName = CustomEvent->CustomFunctionName.ToString();
			}
			else if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
			{
				EventName = EventNode->EventReference.GetMemberName().ToString();
				DeclaringClass = EventNode->EventReference.GetMemberParentClass();
				EventSource = (DeclaringClass && DeclaringClass->HasAnyClassFlags(CLASS_Interface))
					? TEXT("interface")
					: TEXT("parent");
			}
			else
			{
				continue;
			}
			if (EventName.IsEmpty()) continue;

			ImplementedNames.Add(EventName);
			// nodeCount is 0 for an entry point: it is a node inside graphName, not a graph.
			TSharedPtr<FJsonObject> Obj = MakeFunctionEntry(EventName, TEXT("event"), EventSource, 0);
			Obj->SetStringField(TEXT("graphName"), Graph->GetName());
			if (DeclaringClass)
			{
				Obj->SetStringField(TEXT("declaringClass"), DeclaringClass->GetName());
				Obj->SetStringField(TEXT("declaringClassPath"), DeclaringClass->GetPathName());
			}
			Functions.Add(MakeShared<FJsonValueObject>(Obj));
		}
	}

	// 4. Macros and event dispatcher signatures. Both are graphs list_graphs
	//    reports, and macro bodies reference member variables like any function.
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (!Graph || SeenGraphs.Contains(Graph)) continue;
		SeenGraphs.Add(Graph);
		Functions.Add(MakeShared<FJsonValueObject>(MakeFunctionGraphEntry(Graph, TEXT("macro"), TEXT("own"))));
		CollectFunctionSubGraphs(Graph, SeenGraphs, Functions);
	}
	for (UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
	{
		if (!Graph || SeenGraphs.Contains(Graph)) continue;
		SeenGraphs.Add(Graph);
		Functions.Add(MakeShared<FJsonValueObject>(MakeFunctionGraphEntry(Graph, TEXT("delegate_signature"), TEXT("own"))));
	}

	// 5. Opt-in: inherited functions this Blueprint could override but has not.
	if (bIncludeInherited && ParentClass)
	{
		for (TFieldIterator<UFunction> It(ParentClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			UFunction* Function = *It;
			if (!UEdGraphSchema_K2::CanKismetOverrideFunction(Function)) continue;
			if (ImplementedNames.Contains(Function->GetName())) continue;
			if (FObjectEditorUtils::IsFunctionHiddenFromClass(Function, ParentClass)) continue;
			if (!Blueprint->AllowFunctionOverride(Function)) continue;

			UClass* OuterClass = Cast<UClass>(Function->GetOuter());
			if (OuterClass && FBlueprintEditorUtils::FindOverrideForFunction(Blueprint, OuterClass, Function->GetFName())) continue;

			const bool bIsInterface = OuterClass && OuterClass->HasAnyClassFlags(CLASS_Interface);
			TSharedPtr<FJsonObject> Obj = MakeFunctionEntry(
				Function->GetName(),
				TEXT("inherited"),
				bIsInterface ? TEXT("interface") : TEXT("parent"),
				0);
			Obj->SetBoolField(TEXT("canBeEvent"), UEdGraphSchema_K2::FunctionCanBePlacedAsEvent(Function));
			if (OuterClass)
			{
				Obj->SetStringField(TEXT("declaringClass"), OuterClass->GetName());
				Obj->SetStringField(TEXT("declaringClassPath"), OuterClass->GetPathName());
			}
			Functions.Add(MakeShared<FJsonValueObject>(Obj));
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetArrayField(TEXT("functions"), Functions);
	Result->SetNumberField(TEXT("count"), Functions.Num());
	Result->SetBoolField(TEXT("includeInherited"), bIncludeInherited);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::AddEventDispatcher(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	FString DispatcherName;
	if (auto Err = RequireString(Params, TEXT("name"), DispatcherName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
	}

	const FName DispatcherFName(*DispatcherName);

	// Idempotency: if a variable with this name already exists, short-circuit.
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == DispatcherFName)
		{
			auto Existed = MCPSuccess();
			MCPSetExisted(Existed);
			Existed->SetStringField(TEXT("blueprintPath"), BlueprintPath);
			Existed->SetStringField(TEXT("name"), DispatcherName);
			return MCPResult(Existed);
		}
	}

	// #276: Mirror BlueprintEditor.cpp's "Add New Event Dispatcher" path
	// exactly. The previous implementation created the variable with a
	// MemberReference pointing at the signature graph's GUID and skipped
	// CreateFunctionGraphTerminators / AddExtraFunctionFlags / MarkFunctionEntryAsEditable.
	// Result: the BP compiler had no UFunction to bind to the multicast
	// delegate property, so any K2Node_CallDelegate referencing it failed
	// to compile with "No SignatureFunction in MulticastDelegateProperty".
	// The canonical pattern (UnrealEngine BlueprintEditor.cpp:9620) is:
	//   1. AddMemberVariable with bare PC_MCDelegate type (no GUID link)
	//   2. CreateNewGraph
	//   3. CreateDefaultNodesForGraph + CreateFunctionGraphTerminators
	//   4. AddExtraFunctionFlags(Callable | Event | Public)
	//   5. MarkFunctionEntryAsEditable
	//   6. Add to DelegateSignatureGraphs
	//   7. MarkBlueprintAsStructurallyModified

	FEdGraphPinType DelegateType;
	DelegateType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;

	const bool bVarOk = FBlueprintEditorUtils::AddMemberVariable(Blueprint, DispatcherFName, DelegateType);
	if (!bVarOk)
	{
		return MCPError(FString::Printf(TEXT("Failed to add event dispatcher variable: %s"), *DispatcherName));
	}

	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint, DispatcherFName,
		UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	if (!NewGraph)
	{
		// Roll the variable creation back so we don't leave a dangling delegate
		// without a signature graph (which is exactly the bug we're fixing).
		FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, DispatcherFName);
		return MCPError(FString::Printf(TEXT("Failed to create signature graph for: %s"), *DispatcherName));
	}

	NewGraph->bEditable = false;

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	K2Schema->CreateDefaultNodesForGraph(*NewGraph);
	K2Schema->CreateFunctionGraphTerminators(*NewGraph, (UClass*)nullptr);
	K2Schema->AddExtraFunctionFlags(NewGraph, (FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public));
	K2Schema->MarkFunctionEntryAsEditable(NewGraph, true);

	Blueprint->DelegateSignatureGraphs.Add(NewGraph);

	// Optional: declare typed parameters on the dispatcher signature.
	// Params: parameters: [{ name, type }] where type is a K2 pin category
	// shorthand ("bool", "int", "float", "string", "name", "vector",
	// "rotator", "object:/Script/Module.ClassName", "struct:/Script/...").
	const TArray<TSharedPtr<FJsonValue>>* ParamsArr = nullptr;
	if (Params->TryGetArrayField(TEXT("parameters"), ParamsArr) && ParamsArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *ParamsArr)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!V.IsValid() || !V->TryGetObject(Obj) || !Obj || !Obj->IsValid()) continue;
			FString PName, PType;
			if (!(*Obj)->TryGetStringField(TEXT("name"), PName) || PName.IsEmpty()) continue;
			(*Obj)->TryGetStringField(TEXT("type"), PType);

			FEdGraphPinType PinType;
			PinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
			const FString T = PType.ToLower();
			if (T == TEXT("bool"))                 PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
			else if (T == TEXT("int") || T == TEXT("integer")) PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
			else if (T == TEXT("float") || T == TEXT("real"))  { PinType.PinCategory = UEdGraphSchema_K2::PC_Real; PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double; }
			else if (T == TEXT("string"))          PinType.PinCategory = UEdGraphSchema_K2::PC_String;
			else if (T == TEXT("name"))            PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
			else if (T == TEXT("text"))            PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
			else if (T == TEXT("vector"))          { PinType.PinCategory = UEdGraphSchema_K2::PC_Struct; PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get(); }
			else if (T == TEXT("rotator"))         { PinType.PinCategory = UEdGraphSchema_K2::PC_Struct; PinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get(); }
			else if (T == TEXT("transform"))       { PinType.PinCategory = UEdGraphSchema_K2::PC_Struct; PinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get(); }
			else if (T.StartsWith(TEXT("object:")))
			{
				PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
				const FString ClassPath = PType.Mid(7);
				PinType.PinSubCategoryObject = LoadObject<UClass>(nullptr, *ClassPath);
				if (!PinType.PinSubCategoryObject.IsValid()) PinType.PinSubCategoryObject = UObject::StaticClass();
			}
			else if (T.StartsWith(TEXT("struct:")))
			{
				PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
				PinType.PinSubCategoryObject = LoadObject<UScriptStruct>(nullptr, *PType.Mid(7));
			}

			// Pin is added to the function entry node's user-defined pin list.
			// Nodes were created above by CreateFunctionGraphTerminators.
			TArray<UK2Node_EditablePinBase*> EntryNodes;
			NewGraph->GetNodesOfClass(EntryNodes);
			if (EntryNodes.Num() > 0 && EntryNodes[0])
			{
				EntryNodes[0]->CreateUserDefinedPin(FName(*PName), PinType, EGPD_Output);
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetStringField(TEXT("name"), DispatcherName);
	Result->SetStringField(TEXT("signatureGraph"), NewGraph->GetName());

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), BlueprintPath);
	Payload->SetStringField(TEXT("name"), DispatcherName);
	MCPSetRollback(Result, TEXT("delete_variable"), Payload);

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FBlueprintHandlers::RenameFunction(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString OldName;
	if (auto Err = RequireString(Params, TEXT("oldName"), OldName)) return Err;

	FString NewName;
	if (auto Err = RequireString(Params, TEXT("newName"), NewName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Find the function graph
	UEdGraph* FoundGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetName() == OldName)
		{
			FoundGraph = Graph;
			break;
		}
	}

	if (!FoundGraph)
	{
		return MCPError(FString::Printf(TEXT("Function not found: %s"), *OldName));
	}

	FBlueprintEditorUtils::RenameGraph(FoundGraph, NewName);

	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("oldName"), OldName);
	Result->SetStringField(TEXT("newName"), NewName);

	// Self-inverse: rename back.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), AssetPath);
	Payload->SetStringField(TEXT("oldName"), NewName);
	Payload->SetStringField(TEXT("newName"), OldName);
	MCPSetRollback(Result, TEXT("rename_function"), Payload);

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FBlueprintHandlers::DeleteFunction(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString FunctionName;
	if (auto Err = RequireString(Params, TEXT("functionName"), FunctionName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	UEdGraph* FoundGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetName() == FunctionName)
		{
			FoundGraph = Graph;
			break;
		}
	}

	// Idempotent: no function to delete is a no-op.
	if (!FoundGraph)
	{
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("functionName"), FunctionName);
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Noop);
	}

	FBlueprintEditorUtils::RemoveGraph(Blueprint, FoundGraph);

	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("functionName"), FunctionName);
	Result->SetBoolField(TEXT("deleted"), true);
	// Delete of a function is not reversible by default.
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FBlueprintHandlers::CreateBlueprintInterface(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	// Idempotency: check if asset already exists.
	if (UBlueprint* Existing = LoadBlueprint(AssetPath))
	{
		if (OnConflict == TEXT("error"))
		{
			return MCPError(FString::Printf(TEXT("Interface '%s' already exists"), *AssetPath));
		}
		auto Result = MCPSuccess();
		MCPSetExisted(Result);
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("name"), Existing->GetName());
		return MCPResult(Result);
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	FString PackageName;
	FString AssetName;
	AssetPath.Split(TEXT("/"), &PackageName, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);

	UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
	BlueprintFactory->BlueprintType = BPTYPE_Interface;
	BlueprintFactory->ParentClass = UInterface::StaticClass();

	UBlueprint* NewInterface = Cast<UBlueprint>(AssetTools.CreateAsset(AssetName, PackageName, UBlueprint::StaticClass(), BlueprintFactory));
	if (!NewInterface)
	{
		return MCPError(TEXT("Failed to create Blueprint Interface"));
	}

	FKismetEditorUtilities::CompileBlueprint(NewInterface);

	const FString ObjectPath = NewInterface->GetPathName();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("name"), NewInterface->GetName());

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), ObjectPath);
	MCPSetRollback(Result, TEXT("delete_asset"), Payload);

	return MCPResult(Result);
}


// #688: Create an override for an inherited interface implementation (inherited
// via the parent class) or an overridable parent virtual function, with the
// matching signature so it actually binds as the override. Mirrors the editor's
// SMyBlueprint::ImplementFunction path: resolve the overridable UFunction via
// GetOverrideFunctionClass, then either place a bOverrideFunction event node
// (for event-shaped functions) or create a function graph seeded from the
// override class so the entry/result terminators match the base signature.
TSharedPtr<FJsonValue> FBlueprintHandlers::OverrideFunction(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString FunctionName;
	if (auto Err = RequireString(Params, TEXT("functionName"), FunctionName)) return Err;

	// 'source' is advisory ("auto"|"interface"|"parent"). GetOverrideFunctionClass
	// resolves both inherited-interface and parent-virtual functions uniformly, so
	// we do not branch on it, but it is echoed back for the caller's clarity.
	const FString Source = OptionalString(Params, TEXT("source"), TEXT("auto"));
	// Force the function-graph form even when the function could be placed as an
	// event (e.g. you need an explicit function body with locals / a return path).
	const bool bPreferFunction = OptionalBool(Params, TEXT("preferFunction"), false);

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	const FName FuncName(*FunctionName);

	// If the caller names an interface that is not yet implemented on this
	// Blueprint (nor inherited), implement it first so its functions become
	// overridable. Inherited-via-parent interfaces need no such step.
	FString InterfacePathStr;
	if (Params->TryGetStringField(TEXT("interfacePath"), InterfacePathStr) && !InterfacePathStr.IsEmpty())
	{
		if (UClass* InterfaceClass = LoadObject<UClass>(nullptr, *InterfacePathStr))
		{
			bool bAlready = Blueprint->ParentClass && Blueprint->ParentClass->ImplementsInterface(InterfaceClass);
			for (const FBPInterfaceDescription& Impl : Blueprint->ImplementedInterfaces)
			{
				if (Impl.Interface == InterfaceClass) { bAlready = true; break; }
			}
			if (!bAlready)
			{
				FBlueprintEditorUtils::ImplementNewInterface(Blueprint, FTopLevelAssetPath(InterfaceClass->GetPathName()));
			}
		}
	}

	// Ensure interface conformance so a freshly-added interface function is picked up.
	FBlueprintEditorUtils::ConformImplementedInterfaces(Blueprint);

	// Idempotency: an override graph with this name already exists.
	for (UEdGraph* G : Blueprint->FunctionGraphs)
	{
		if (G && G->GetName() == FunctionName)
		{
			auto Existed = MCPSuccess();
			MCPSetExisted(Existed);
			Existed->SetStringField(TEXT("path"), AssetPath);
			Existed->SetStringField(TEXT("functionName"), FunctionName);
			Existed->SetStringField(TEXT("kind"), TEXT("function"));
			Existed->SetStringField(TEXT("graphName"), FunctionName);
			return MCPResult(Existed);
		}
	}

	UFunction* OverrideFunc = nullptr;
	UClass* OverrideFuncClass = FBlueprintEditorUtils::GetOverrideFunctionClass(Blueprint, FuncName, &OverrideFunc);
	if (!OverrideFuncClass || !OverrideFunc)
	{
		// Build a short candidate list to guide the caller.
		TArray<FString> Candidates;
		UClass* ParentClass = Blueprint->SkeletonGeneratedClass
			? Blueprint->SkeletonGeneratedClass->GetSuperClass()
			: Blueprint->ParentClass.Get();
		if (ParentClass)
		{
			for (TFieldIterator<UFunction> It(ParentClass, EFieldIteratorFlags::IncludeSuper); It && Candidates.Num() < 40; ++It)
			{
				if (UEdGraphSchema_K2::CanKismetOverrideFunction(*It))
				{
					Candidates.AddUnique(It->GetName());
				}
			}
		}
		return MCPError(FString::Printf(
			TEXT("No overridable function named '%s' found on the parent class or inherited interfaces. Overridable functions include: %s. Use list_overridable_functions for the full list."),
			*FunctionName, Candidates.Num() > 0 ? *FString::Join(Candidates, TEXT(", ")) : TEXT("(none)")));
	}

	// Event-shaped functions (BlueprintImplementableEvent / no return value that
	// can be placed as an event) go into the event graph as an override event,
	// matching the editor default, unless the caller forces the function form.
	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	const bool bAsEvent = UEdGraphSchema_K2::FunctionCanBePlacedAsEvent(OverrideFunc) && !bPreferFunction && EventGraph != nullptr;

	if (bAsEvent)
	{
		// Idempotency: this override event already exists.
		if (UK2Node_Event* Existing = FBlueprintEditorUtils::FindOverrideForFunction(Blueprint, OverrideFuncClass, FuncName))
		{
			auto Existed = MCPSuccess();
			MCPSetExisted(Existed);
			Existed->SetStringField(TEXT("path"), AssetPath);
			Existed->SetStringField(TEXT("functionName"), FunctionName);
			Existed->SetStringField(TEXT("kind"), TEXT("event"));
			Existed->SetStringField(TEXT("graphName"), EventGraph->GetName());
			Existed->SetStringField(TEXT("nodeId"), Existing->NodeGuid.ToString());
			return MCPResult(Existed);
		}

		UK2Node_Event* NewEventNode = NewObject<UK2Node_Event>(EventGraph);
		NewEventNode->EventReference.SetExternalMember(FuncName, OverrideFuncClass);
		NewEventNode->bOverrideFunction = true;

		EventGraph->Modify();
		const FVector2D Location = EventGraph->GetGoodPlaceForNewNode();
		EventGraph->AddNode(NewEventNode, false, false);
		NewEventNode->NodePosX = Location.X;
		NewEventNode->NodePosY = Location.Y;
		NewEventNode->CreateNewGuid();
		NewEventNode->PostPlacedNewNode();
		NewEventNode->AllocateDefaultPins();
		NewEventNode->ReconstructNode();

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		SaveAssetPackage(Blueprint);

		auto Result = MCPSuccess();
		MCPSetCreated(Result);
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("functionName"), FunctionName);
		Result->SetStringField(TEXT("kind"), TEXT("event"));
		Result->SetStringField(TEXT("graphName"), EventGraph->GetName());
		Result->SetStringField(TEXT("nodeId"), NewEventNode->NodeGuid.ToString());
		Result->SetStringField(TEXT("sourceClass"), OverrideFuncClass->GetPathName());
		Result->SetStringField(TEXT("source"), Source);

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("path"), AssetPath);
		Payload->SetStringField(TEXT("graphName"), EventGraph->GetName());
		Payload->SetStringField(TEXT("nodeId"), NewEventNode->NodeGuid.ToString());
		MCPSetRollback(Result, TEXT("delete_node"), Payload);
		return MCPResult(Result);
	}

	// Function-form override: create the graph seeded from the override class so
	// the entry/result terminators carry the base function's exact signature.
	// (This is the fix for #688 - create_function produced a blank graph that
	// never bound as the override.)
	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint, FuncName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	if (!NewGraph)
	{
		return MCPError(FString::Printf(TEXT("Failed to create override graph: %s"), *FunctionName));
	}

	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, NewGraph, /*bIsUserCreated=*/false, OverrideFuncClass);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("functionName"), FunctionName);
	Result->SetStringField(TEXT("kind"), TEXT("function"));
	Result->SetStringField(TEXT("graphName"), NewGraph->GetName());
	Result->SetStringField(TEXT("sourceClass"), OverrideFuncClass->GetPathName());
	Result->SetStringField(TEXT("source"), Source);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), AssetPath);
	Payload->SetStringField(TEXT("functionName"), FunctionName);
	MCPSetRollback(Result, TEXT("delete_function"), Payload);
	return MCPResult(Result);
}


// #688: List the functions this Blueprint can override - inherited interface
// implementations and overridable parent virtuals - so an agent can discover the
// exact name to pass to override_function. Mirrors the editor's overridable
// function collection (SMyBlueprint::CollectAllActions).
TSharedPtr<FJsonValue> FBlueprintHandlers::ListOverridableFunctions(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FBlueprintEditorUtils::ConformImplementedInterfaces(Blueprint);

	UClass* ParentClass = Blueprint->SkeletonGeneratedClass
		? Blueprint->SkeletonGeneratedClass->GetSuperClass()
		: Blueprint->ParentClass.Get();
	if (!ParentClass)
	{
		return MCPError(TEXT("Blueprint has no resolvable parent class"));
	}

	// Names already implemented as function graphs on this Blueprint are skipped.
	TSet<FString> ImplementedNames;
	for (UEdGraph* G : Blueprint->FunctionGraphs)
	{
		if (G) ImplementedNames.Add(G->GetName());
	}

	TArray<TSharedPtr<FJsonValue>> Functions;
	for (TFieldIterator<UFunction> It(ParentClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		UFunction* Function = *It;
		const FName FnName = Function->GetFName();
		if (!UEdGraphSchema_K2::CanKismetOverrideFunction(Function)) continue;
		if (ImplementedNames.Contains(Function->GetName())) continue;
		if (FObjectEditorUtils::IsFunctionHiddenFromClass(Function, ParentClass)) continue;
		if (!Blueprint->AllowFunctionOverride(Function)) continue;

		UClass* OuterClass = Cast<UClass>(Function->GetOuter());
		if (OuterClass && FBlueprintEditorUtils::FindOverrideForFunction(Blueprint, OuterClass, FnName)) continue;

		TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
		FuncObj->SetStringField(TEXT("name"), Function->GetName());
		FuncObj->SetBoolField(TEXT("canBeEvent"), UEdGraphSchema_K2::FunctionCanBePlacedAsEvent(Function));
		const bool bIsInterface = OuterClass && OuterClass->HasAnyClassFlags(CLASS_Interface);
		FuncObj->SetStringField(TEXT("source"), bIsInterface ? TEXT("interface") : TEXT("parent"));
		if (OuterClass) FuncObj->SetStringField(TEXT("declaringClass"), OuterClass->GetName());
		Functions.Add(MakeShared<FJsonValueObject>(FuncObj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("parentClass"), ParentClass->GetName());
	Result->SetArrayField(TEXT("functions"), Functions);
	Result->SetNumberField(TEXT("count"), Functions.Num());
	return MCPResult(Result);
}
