// Blueprint authoring depth (V9): the holes the existing 62 blueprint actions
// leave open.
//
// All functions below are still members of FBlueprintHandlers - this file is a
// translation-unit partition, not a new class. Registration stays in
// BlueprintHandlers.cpp::RegisterHandlers.
//
// V9 was raised on an action count rather than a proven missing capability, so
// the audit was the work. Most of what the ticket suspected was already here:
// node search (search_node_types, list_node_types, search_call_sites),
// graph authoring (add_node, connect_pins, connect_pins_batch, delete_node,
// auto_layout_graph, T3D import/export), interface CREATION and implementation
// (create_blueprint_interface, add_blueprint_interface), function creation and
// override (create_function, override_function, list_overridable_functions),
// local variable add and list, event dispatcher add, and compile with a real
// FCompilerResultsLog readout (compile_blueprint, #703). Component and CDO
// configuration is asset(set_property) / editor(set_property) territory and is
// deliberately not duplicated with typed setters.
//
// What was genuinely missing is everything below, and it falls into four kinds:
//
//   half-built CRUD  add_blueprint_interface, add_function_parameter and
//                    add_local_variable each carried a comment in the shipping
//                    source saying no paired remove existed, so none of them
//                    could emit a rollback. An add with no remove makes every
//                    flow containing it unrecoverable.
//   graph-only state function flags (pure, const, access specifier) and the
//                    FKismetUserDeclaredFunctionMetadata that carries category,
//                    tooltip, keywords, compact title, CallInEditor and thread
//                    safety live on a UK2Node_FunctionEntry inside a graph, not
//                    as UPROPERTYs on the asset, so no property write reaches
//                    them.
//   reference fixup  deleting and re-adding a variable loses every node that
//                    referenced it. RenameMemberVariable rewrites them.
//   never authorable macros could be listed and read but not created, and a
//                    custom event could be placed by name but never given a
//                    typed signature.
//
// Nothing here writes a UPROPERTY that asset(set_property) or
// editor(set_property) already reaches. Where a caller might expect one, the
// action returns objectPath and says where to aim instead.

#include "BlueprintHandlers.h"
#include "BlueprintHandlers_Internal.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Tunnel.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_Variable.h"
#include "UObject/UnrealType.h"
#include "UObject/TopLevelAssetPath.h"
#include "UObject/Interface.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Uniquely named: this module is a unity build, so a file-local helper sharing
// a name with one in another .cpp is a redefinition that only shows up once the
// adaptive-unity working set shifts.
namespace MCPBlueprintDepth
{

/** What kind of graph a name resolved to, reported so a caller can tell a
 *  macro from a function without a second call. */
enum class EGraphKind : uint8
{
	Function,
	Macro,
	DelegateSignature,
	Ubergraph,
	Unknown,
};

static const TCHAR* GraphKindName(EGraphKind Kind)
{
	switch (Kind)
	{
	case EGraphKind::Function:          return TEXT("function");
	case EGraphKind::Macro:             return TEXT("macro");
	case EGraphKind::DelegateSignature: return TEXT("delegateSignature");
	case EGraphKind::Ubergraph:         return TEXT("ubergraph");
	default:                            return TEXT("unknown");
	}
}

static UEdGraph* FindByName(const TArray<TObjectPtr<UEdGraph>>& Graphs, const FString& Name)
{
	for (UEdGraph* G : Graphs)
	{
		if (G && G->GetName() == Name) return G;
	}
	return nullptr;
}

/** Resolve a function, macro or dispatcher-signature graph by name. */
static UEdGraph* FindParameterisedGraph(UBlueprint* Blueprint, const FString& Name, EGraphKind& OutKind)
{
	if (UEdGraph* G = FindByName(Blueprint->FunctionGraphs, Name))          { OutKind = EGraphKind::Function; return G; }
	if (UEdGraph* G = FindByName(Blueprint->MacroGraphs, Name))             { OutKind = EGraphKind::Macro; return G; }
	if (UEdGraph* G = FindByName(Blueprint->DelegateSignatureGraphs, Name)) { OutKind = EGraphKind::DelegateSignature; return G; }
	for (const FBPInterfaceDescription& Impl : Blueprint->ImplementedInterfaces)
	{
		if (UEdGraph* G = FindByName(Impl.Graphs, Name)) { OutKind = EGraphKind::Function; return G; }
	}
	OutKind = EGraphKind::Unknown;
	return nullptr;
}

/** Every function, macro and dispatcher-signature graph, for an error that
 *  names the valid values rather than only rejecting the invalid one. */
static FString ParameterisedGraphList(UBlueprint* Blueprint)
{
	TArray<FString> Names;
	for (UEdGraph* G : Blueprint->FunctionGraphs)          { if (G) Names.Add(G->GetName() + TEXT(" (function)")); }
	for (UEdGraph* G : Blueprint->MacroGraphs)             { if (G) Names.Add(G->GetName() + TEXT(" (macro)")); }
	for (UEdGraph* G : Blueprint->DelegateSignatureGraphs) { if (G) Names.Add(G->GetName() + TEXT(" (dispatcher signature)")); }
	for (const FBPInterfaceDescription& Impl : Blueprint->ImplementedInterfaces)
	{
		for (UEdGraph* G : Impl.Graphs) { if (G) Names.Add(G->GetName() + TEXT(" (interface)")); }
	}
	return Names.Num() > 0 ? FString::Join(Names, TEXT(", ")) : FString(TEXT("(none)"));
}

/** The node that owns a graph's INPUT pins: the function entry, or the macro's
 *  entry tunnel (the one allowed to have outputs, which are the macro's
 *  inputs). */
static UK2Node_EditablePinBase* FindInputOwner(UEdGraph* Graph, EGraphKind Kind)
{
	if (Kind == EGraphKind::Macro)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_Tunnel* Tunnel = Cast<UK2Node_Tunnel>(Node);
			if (Tunnel && Tunnel->bCanHaveOutputs) return Tunnel;
		}
		return nullptr;
	}
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node)) return Entry;
	}
	return nullptr;
}

/** The node that owns a graph's OUTPUT pins: the function result node, or the
 *  macro's exit tunnel. bCreate mints a function result node when there is
 *  none, which is what makes the first output parameter possible. */
static UK2Node_EditablePinBase* FindOutputOwner(UEdGraph* Graph, EGraphKind Kind, bool bCreate)
{
	if (Kind == EGraphKind::Macro)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_Tunnel* Tunnel = Cast<UK2Node_Tunnel>(Node);
			if (Tunnel && Tunnel->bCanHaveInputs && !Tunnel->bCanHaveOutputs) return Tunnel;
		}
		return nullptr;
	}
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UK2Node_FunctionResult* Result = Cast<UK2Node_FunctionResult>(Node)) return Result;
	}
	if (!bCreate) return nullptr;

	UK2Node_FunctionResult* NewResult = NewObject<UK2Node_FunctionResult>(Graph);
	Graph->Modify();
	Graph->AddNode(NewResult, false, false);
	NewResult->CreateNewGuid();
	NewResult->AllocateDefaultPins();
	NewResult->PostPlacedNewNode();
	// Sit the return node to the right of the entry so the graph is readable
	// without a follow-up auto_layout_graph call.
	if (UK2Node_EditablePinBase* Entry = FindInputOwner(Graph, Kind))
	{
		NewResult->NodePosX = Entry->NodePosX + 480;
		NewResult->NodePosY = Entry->NodePosY;
	}
	return NewResult;
}

/** One user-defined pin as JSON, using the same type vocabulary the write half
 *  accepts, so a read can be echoed straight back as a call. */
static TSharedPtr<FJsonObject> UserPinJson(const TSharedPtr<FUserPinInfo>& Info, int32 Index)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetNumberField(TEXT("index"), Index);
	O->SetStringField(TEXT("name"), Info->PinName.ToString());
	bool bRoundTrips = true;
	O->SetStringField(TEXT("type"), FBlueprintHandlers::PinTypeSpec(Info->PinType, bRoundTrips));
	O->SetBoolField(TEXT("typeRoundTrips"), bRoundTrips);
	O->SetStringField(TEXT("defaultValue"), Info->PinDefaultValue);
	O->SetBoolField(TEXT("isReference"), Info->PinType.bIsReference);
	O->SetBoolField(TEXT("isConst"), Info->PinType.bIsConst);
	return O;
}

static int32 FindUserPin(const UK2Node_EditablePinBase* Node, const FName& PinName)
{
	for (int32 i = 0; i < Node->UserDefinedPins.Num(); ++i)
	{
		if (Node->UserDefinedPins[i].IsValid() && Node->UserDefinedPins[i]->PinName == PinName) return i;
	}
	return INDEX_NONE;
}

static FString UserPinNameList(const UK2Node_EditablePinBase* Node)
{
	TArray<FString> Names;
	if (Node)
	{
		for (const TSharedPtr<FUserPinInfo>& Info : Node->UserDefinedPins)
		{
			if (Info.IsValid()) Names.Add(Info->PinName.ToString());
		}
	}
	return Names.Num() > 0 ? FString::Join(Names, TEXT(", ")) : FString(TEXT("(none)"));
}

/** A custom event node by name, in the named graph or across every ubergraph
 *  page when no graph is given. */
static UK2Node_CustomEvent* FindCustomEvent(UBlueprint* Blueprint, const FString& GraphName, const FString& EventName)
{
	TArray<UEdGraph*> Search;
	if (GraphName.IsEmpty())
	{
		for (UEdGraph* G : Blueprint->UbergraphPages) { if (G) Search.Add(G); }
	}
	else
	{
		TArray<UEdGraph*> All;
		Blueprint->GetAllGraphs(All);
		for (UEdGraph* G : All) { if (G && G->GetName() == GraphName) Search.Add(G); }
	}
	for (UEdGraph* G : Search)
	{
		for (UEdGraphNode* Node : G->Nodes)
		{
			UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Node);
			if (Event && Event->CustomFunctionName.ToString() == EventName) return Event;
		}
	}
	return nullptr;
}

static FString CustomEventNameList(UBlueprint* Blueprint)
{
	TArray<FString> Names;
	for (UEdGraph* G : Blueprint->UbergraphPages)
	{
		if (!G) continue;
		for (UEdGraphNode* Node : G->Nodes)
		{
			if (UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Node))
			{
				Names.Add(FString::Printf(TEXT("%s (%s)"), *Event->CustomFunctionName.ToString(), *G->GetName()));
			}
		}
	}
	return Names.Num() > 0 ? FString::Join(Names, TEXT(", ")) : FString(TEXT("(none)"));
}

/** Metadata block for a function graph (entry node) or a macro graph (entry
 *  tunnel). Both carry the same struct; only the owner differs. */
static FKismetUserDeclaredFunctionMetadata* GraphMetadata(UEdGraph* Graph, EGraphKind Kind)
{
	if (Kind == EGraphKind::Macro)
	{
		return UK2Node_MacroInstance::GetAssociatedGraphMetadata(Graph);
	}
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node)) return &Entry->MetaData;
	}
	return nullptr;
}

static UK2Node_FunctionEntry* FindFunctionEntry(UEdGraph* Graph)
{
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node)) return Entry;
	}
	return nullptr;
}

/** How many nodes in the whole Blueprint reference a member variable by name.
 *  Counted before and after a rename so the result can state what was rewritten
 *  instead of asserting it. */
static int32 CountVariableReferences(UBlueprint* Blueprint, const FName& VarName)
{
	int32 Count = 0;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_Variable* VarNode = Cast<UK2Node_Variable>(Node))
			{
				if (VarNode->GetVarName() == VarName) ++Count;
			}
		}
	}
	return Count;
}

static FString MemberVariableNameList(UBlueprint* Blueprint)
{
	TArray<FString> Names;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables) Names.Add(Var.VarName.ToString());
	return Names.Num() > 0 ? FString::Join(Names, TEXT(", ")) : FString(TEXT("(none)"));
}

/** The multicast-delegate member variables, which is what an event dispatcher
 *  is on disk. */
static bool IsDispatcher(const FBPVariableDescription& Var)
{
	return Var.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate;
}

static FString DispatcherNameList(UBlueprint* Blueprint)
{
	TArray<FString> Names;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (IsDispatcher(Var)) Names.Add(Var.VarName.ToString());
	}
	return Names.Num() > 0 ? FString::Join(Names, TEXT(", ")) : FString(TEXT("(none)"));
}

/** Resolve a class from a path or a short name, restricted to interfaces. */
static UClass* ResolveInterfaceClass(const FString& Spec)
{
	UClass* Resolved = LoadObject<UClass>(nullptr, *Spec);
	if (!Resolved) Resolved = MCPResolveClass(Spec);
	if (!Resolved) return nullptr;
	// A Blueprint interface asset resolves to its generated class, which is what
	// ImplementedInterfaces stores, so accept either spelling.
	return Resolved->IsChildOf(UInterface::StaticClass()) || Resolved->HasAnyClassFlags(CLASS_Interface)
		? Resolved
		: nullptr;
}

/** The local-variable scope the FBlueprintEditorUtils local-variable API wants:
 *  the UFunction on the skeleton class matching the graph. */
static UStruct* FindLocalVariableScope(UBlueprint* Blueprint, const FString& FunctionName)
{
	if (UClass* Skeleton = Blueprint->SkeletonGeneratedClass)
	{
		if (UFunction* Fn = Skeleton->FindFunctionByName(FName(*FunctionName))) return Fn;
	}
	if (UClass* Generated = Blueprint->GeneratedClass)
	{
		if (UFunction* Fn = Generated->FindFunctionByName(FName(*FunctionName))) return Fn;
	}
	return nullptr;
}

/** Compile and persist after a structural edit, the way every other authoring
 *  handler in this category finishes. */
static void RecompileAfterStructuralEdit(UBlueprint* Blueprint)
{
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);
}

} // namespace MCPBlueprintDepth

// ─────────────────────────────────────────────────────────────────────────────
// list_blueprint_interfaces
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FBlueprintHandlers::ListBlueprintInterfaces(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPBlueprintDepth;

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("blueprintPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	// Which of the interface's functions actually has an implementation, and in
	// what form. add_blueprint_interface creates the contract; whether anything
	// answers it is a different question and this is the only place that asks.
	auto DescribeInterface = [Blueprint](UClass* IfaceClass, const TArray<TObjectPtr<UEdGraph>>* OwnGraphs, const TCHAR* Source)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), IfaceClass->GetName());
		Entry->SetStringField(TEXT("interfacePath"), IfaceClass->GetPathName());
		Entry->SetStringField(TEXT("source"), Source);

		TArray<TSharedPtr<FJsonValue>> Functions;
		int32 Missing = 0;
		for (TFieldIterator<UFunction> It(IfaceClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			UFunction* Fn = *It;
			TSharedPtr<FJsonObject> FnObj = MakeShared<FJsonObject>();
			FnObj->SetStringField(TEXT("name"), Fn->GetName());
			// An interface function with no outputs is placed as an event on the
			// event graph; one with outputs gets its own function graph. The two
			// look nothing alike in the asset, so both are searched.
			FnObj->SetBoolField(TEXT("hasOutputs"), Fn->HasAnyFunctionFlags(FUNC_HasOutParms));

			FString ImplementedAs = TEXT("missing");
			if (OwnGraphs && FindByName(*OwnGraphs, Fn->GetName())) ImplementedAs = TEXT("graph");
			if (ImplementedAs == TEXT("missing") && FindByName(Blueprint->FunctionGraphs, Fn->GetName()))
			{
				ImplementedAs = TEXT("graph");
			}
			if (ImplementedAs == TEXT("missing"))
			{
				for (UEdGraph* Page : Blueprint->UbergraphPages)
				{
					if (!Page) continue;
					for (UEdGraphNode* Node : Page->Nodes)
					{
						UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
						if (EventNode && EventNode->EventReference.GetMemberName() == Fn->GetFName())
						{
							ImplementedAs = TEXT("event");
							break;
						}
					}
					if (ImplementedAs != TEXT("missing")) break;
				}
			}
			if (ImplementedAs == TEXT("missing")) ++Missing;
			FnObj->SetStringField(TEXT("implementedAs"), ImplementedAs);
			Functions.Add(MakeShared<FJsonValueObject>(FnObj));
		}
		Entry->SetArrayField(TEXT("functions"), Functions);
		Entry->SetNumberField(TEXT("functionCount"), Functions.Num());
		Entry->SetNumberField(TEXT("unimplementedCount"), Missing);
		return Entry;
	};

	TArray<TSharedPtr<FJsonValue>> Interfaces;
	TSet<UClass*> Listed;
	for (const FBPInterfaceDescription& Impl : Blueprint->ImplementedInterfaces)
	{
		UClass* IfaceClass = Impl.Interface;
		if (!IfaceClass) continue;
		Listed.Add(IfaceClass);
		TSharedPtr<FJsonObject> Entry = DescribeInterface(IfaceClass, &Impl.Graphs, TEXT("own"));
		// Only an interface listed here can be removed; an inherited one belongs
		// to the class that declares it.
		Entry->SetBoolField(TEXT("removable"), true);
		Interfaces.Add(MakeShared<FJsonValueObject>(Entry));
	}
	// The inherited half walks the whole parent chain rather than reading
	// ParentClass->Interfaces, which is per-class and would leave an interface
	// declared on a GRANDPARENT out of a list whose whole job is to say what is
	// in force. It also walks each interface's own super chain, so a Blueprint
	// under a class implementing IUserObjectListEntry lists IUserListEntry too.
	// add_blueprint_interface and remove_blueprint_interface decide on this
	// same walk, which is written to the reach UClass::ImplementsInterface has,
	// so the set listed here and the answer those two give are not merely
	// consistent - they are the same relation read once.
	if (UClass* ParentClass = Blueprint->ParentClass.Get())
	{
		TArray<TPair<UClass*, UClass*>> Inherited;
		GatherImplementedInterfaces(ParentClass, Inherited);
		for (const TPair<UClass*, UClass*>& Pair : Inherited)
		{
			UClass* IfaceClass = Pair.Key;
			if (!IfaceClass || Listed.Contains(IfaceClass)) continue;
			Listed.Add(IfaceClass);
			TSharedPtr<FJsonObject> Entry = DescribeInterface(IfaceClass, nullptr, TEXT("inherited"));
			Entry->SetBoolField(TEXT("removable"), false);
			// Which ancestor declares it, which is not always the direct parent.
			// The gather only ever pairs an interface with the class it read it
			// off, and that class is the loop variable of a walk that starts at
			// a non-null class and stops when it becomes null, so Pair.Value is
			// never null and this field is never a guess and never absent.
			Entry->SetStringField(TEXT("inheritedFrom"), Pair.Value->GetPathName());
			Interfaces.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetArrayField(TEXT("interfaces"), Interfaces);
	Result->SetNumberField(TEXT("count"), Interfaces.Num());
	AnnotateResolvedBlueprint(Result, Blueprint);
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// remove_blueprint_interface
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FBlueprintHandlers::RemoveBlueprintInterface(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPBlueprintDepth;

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("blueprintPath"), TEXT("assetPath"), AssetPath)) return Err;
	FString InterfaceSpec;
	if (auto Err = RequireString(Params, TEXT("interfacePath"), InterfaceSpec)) return Err;

	const bool bPreserveFunctions = OptionalBool(Params, TEXT("preserveFunctions"), false);

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	UClass* InterfaceClass = ResolveInterfaceClass(InterfaceSpec);
	if (!InterfaceClass)
	{
		TArray<FString> Implemented;
		for (const FBPInterfaceDescription& Impl : Blueprint->ImplementedInterfaces)
		{
			if (Impl.Interface) Implemented.Add(Impl.Interface->GetPathName());
		}
		return MCPError(FString::Printf(
			TEXT("'%s' does not resolve to an interface class. Pass the path list_interfaces reports. This Blueprint implements: %s"),
			*InterfaceSpec,
			Implemented.Num() > 0 ? *FString::Join(Implemented, TEXT(", ")) : TEXT("(none)")));
	}

	const FBPInterfaceDescription* Found = nullptr;
	for (const FBPInterfaceDescription& Impl : Blueprint->ImplementedInterfaces)
	{
		if (Impl.Interface == InterfaceClass) { Found = &Impl; break; }
	}

	if (!Found)
	{
		// Distinguish "already gone" from "belongs to an ancestor", because the
		// second is never going away and a caller retrying will never succeed.
		// FindInterfaceProvider rather than a scan of ParentClass->Interfaces:
		// that array is per-class, so a scan sees one level and an interface
		// declared on a GRANDPARENT would be reported as already removed when
		// it is still in force. The walk has the reach UClass::ImplementsInterface
		// has and additionally names the class that declares it, which is the
		// class the caller has to edit, so it is asked once and its answer
		// decides both. add_blueprint_interface asks it the same way.
		if (UClass* ParentClass = Blueprint->ParentClass.Get())
		{
			if (UClass* Provider = FindInterfaceProvider(ParentClass, InterfaceClass))
			{
				return MCPError(FString::Printf(
					TEXT("'%s' is inherited: it is declared on ancestor class '%s' and reached through parent '%s', not implemented on this Blueprint, so it cannot be removed here. Reparent the Blueprint, or edit the class that declares it."),
					*InterfaceClass->GetName(), *Provider->GetPathName(), *ParentClass->GetPathName()));
			}
		}
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetBoolField(TEXT("alreadyRemoved"), true);
		Noop->SetStringField(TEXT("blueprintPath"), AssetPath);
		Noop->SetStringField(TEXT("interfacePath"), InterfaceClass->GetPathName());
		return MCPResult(Noop);
	}

	// Record what is about to be destroyed so the result can say what the
	// rollback will NOT bring back.
	TArray<TSharedPtr<FJsonValue>> RemovedGraphs;
	for (UEdGraph* G : Found->Graphs)
	{
		if (G) RemovedGraphs.Add(MakeShared<FJsonValueString>(G->GetName()));
	}

	FBlueprintEditorUtils::RemoveInterface(
		Blueprint, FTopLevelAssetPath(InterfaceClass->GetPathName()), bPreserveFunctions);
	RecompileAfterStructuralEdit(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("blueprintPath"), AssetPath);
	Result->SetStringField(TEXT("interfacePath"), InterfaceClass->GetPathName());
	Result->SetBoolField(TEXT("preserveFunctions"), bPreserveFunctions);
	Result->SetArrayField(TEXT("removedGraphs"), RemovedGraphs);

	// The inverse re-declares the contract. Say plainly what it restores: with
	// preserveFunctions the implementations survived as ordinary functions and
	// re-adding the interface rebinds them; without it, the bodies are gone and
	// re-adding produces empty stubs.
	Result->SetBoolField(TEXT("rollbackLossy"), !bPreserveFunctions && RemovedGraphs.Num() > 0);
	Result->SetStringField(TEXT("rollbackNote"), bPreserveFunctions
		? TEXT("The implementations were kept as ordinary Blueprint functions; re-adding the interface rebinds them.")
		: TEXT("The implementation graphs were destroyed with the interface. Re-adding it restores the contract and empty stubs, not the bodies. Pass preserveFunctions=true to keep them next time."));

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("blueprintPath"), AssetPath);
	Payload->SetStringField(TEXT("interfacePath"), InterfaceClass->GetPathName());
	MCPSetRollback(Result, TEXT("add_blueprint_interface"), Payload);
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_function_properties
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FBlueprintHandlers::SetFunctionProperties(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPBlueprintDepth;

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString FunctionName;
	if (auto Err = RequireString(Params, TEXT("functionName"), FunctionName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	EGraphKind Kind = EGraphKind::Unknown;
	UEdGraph* Graph = FindParameterisedGraph(Blueprint, FunctionName, Kind);
	if (!Graph)
	{
		return MCPError(FString::Printf(
			TEXT("No function or macro named '%s' on this Blueprint. Graphs: %s"),
			*FunctionName, *ParameterisedGraphList(Blueprint)));
	}

	if (Kind == EGraphKind::DelegateSignature)
	{
		return MCPError(FString::Printf(
			TEXT("'%s' is an event dispatcher signature graph, not a function. Its flags are fixed by the dispatcher and writing them here would break the binding. Functions and macros: %s"),
			*FunctionName, *ParameterisedGraphList(Blueprint)));
	}

	FKismetUserDeclaredFunctionMetadata* Meta = GraphMetadata(Graph, Kind);
	if (!Meta)
	{
		return MCPError(FString::Printf(
			TEXT("Graph '%s' has no entry node, so it carries no function metadata. Recompile the Blueprint and try again."),
			*FunctionName));
	}
	UK2Node_FunctionEntry* Entry = (Kind == EGraphKind::Macro) ? nullptr : FindFunctionEntry(Graph);

	// ── read every request and capture every previous value BEFORE writing ──
	bool bPure = false;      const bool bHasPure      = Params->TryGetBoolField(TEXT("pure"), bPure);
	bool bConst = false;     const bool bHasConst     = Params->TryGetBoolField(TEXT("isConst"), bConst);
	bool bCallInEditor = false; const bool bHasCallInEditor = Params->TryGetBoolField(TEXT("callInEditor"), bCallInEditor);
	bool bThreadSafe = false;   const bool bHasThreadSafe   = Params->TryGetBoolField(TEXT("threadSafe"), bThreadSafe);
	bool bDeprecated = false;   const bool bHasDeprecated   = Params->TryGetBoolField(TEXT("deprecated"), bDeprecated);

	FString Category;          const bool bHasCategory   = Params->TryGetStringField(TEXT("category"), Category);
	FString Tooltip;           const bool bHasTooltip    = Params->TryGetStringField(TEXT("tooltip"), Tooltip);
	FString Keywords;          const bool bHasKeywords   = Params->TryGetStringField(TEXT("keywords"), Keywords);
	FString CompactNodeTitle;  const bool bHasCompact    = Params->TryGetStringField(TEXT("compactNodeTitle"), CompactNodeTitle);
	FString DeprecationMessage;const bool bHasDeprMsg    = Params->TryGetStringField(TEXT("deprecationMessage"), DeprecationMessage);

	FString AccessSpecifier = OptionalString(Params, TEXT("accessSpecifier"));
	const bool bHasAccess = !AccessSpecifier.IsEmpty();
	if (bHasAccess)
	{
		const FString Lowered = AccessSpecifier.ToLower();
		if      (Lowered == TEXT("public"))    AccessSpecifier = TEXT("public");
		else if (Lowered == TEXT("protected")) AccessSpecifier = TEXT("protected");
		else if (Lowered == TEXT("private"))   AccessSpecifier = TEXT("private");
		else
		{
			return MCPError(FString::Printf(
				TEXT("Unknown accessSpecifier '%s'. Expected public, protected or private."), *AccessSpecifier));
		}
	}

	// The flag half only exists on a function entry node. Refuse it on a macro
	// naming what a macro DOES accept, rather than writing a flag nothing reads.
	if (!Entry && (bHasPure || bHasConst || bHasAccess || bHasCallInEditor || bHasThreadSafe))
	{
		return MCPError(FString::Printf(
			TEXT("'%s' is a macro. A macro has no function flags: pure, isConst, accessSpecifier, callInEditor and threadSafe apply to functions only. A macro accepts category, tooltip, keywords, compactNodeTitle, deprecated and deprecationMessage."),
			*FunctionName));
	}

	const int32 PrevFlags = Entry ? Entry->GetExtraFlags() : 0;
	const bool bPrevPure  = Entry && (PrevFlags & FUNC_BlueprintPure) != 0;
	const bool bPrevConst = Entry && (PrevFlags & FUNC_Const) != 0;
	const FString PrevAccess =
		!Entry                                  ? FString() :
		(PrevFlags & FUNC_Private) != 0         ? TEXT("private") :
		(PrevFlags & FUNC_Protected) != 0       ? TEXT("protected") :
		                                          TEXT("public");

	const FString PrevCategory  = Meta->Category.ToString();
	const FString PrevTooltip   = Meta->ToolTip.ToString();
	const FString PrevKeywords  = Meta->Keywords.ToString();
	const FString PrevCompact   = Meta->CompactNodeTitle.ToString();
	const FString PrevDeprMsg   = Meta->DeprecationMessage;
	const bool bPrevCallInEditor = Meta->bCallInEditor;
	const bool bPrevThreadSafe   = Meta->bThreadSafe;
	const bool bPrevDeprecated   = Meta->bIsDeprecated;

	const bool bAnyRequest = bHasPure || bHasConst || bHasAccess || bHasCallInEditor || bHasThreadSafe
		|| bHasDeprecated || bHasCategory || bHasTooltip || bHasKeywords || bHasCompact || bHasDeprMsg;
	if (!bAnyRequest)
	{
		return MCPError(TEXT("Nothing to set. Pass at least one of: pure, isConst, accessSpecifier, category, tooltip, keywords, compactNodeTitle, callInEditor, threadSafe, deprecated, deprecationMessage."));
	}

	const bool bAnyChange =
		(bHasPure         && bPure         != bPrevPure) ||
		(bHasConst        && bConst        != bPrevConst) ||
		(bHasAccess       && AccessSpecifier != PrevAccess) ||
		(bHasCallInEditor && bCallInEditor != bPrevCallInEditor) ||
		(bHasThreadSafe   && bThreadSafe   != bPrevThreadSafe) ||
		(bHasDeprecated   && bDeprecated   != bPrevDeprecated) ||
		(bHasCategory     && Category      != PrevCategory) ||
		(bHasTooltip      && Tooltip       != PrevTooltip) ||
		(bHasKeywords     && Keywords      != PrevKeywords) ||
		(bHasCompact      && CompactNodeTitle != PrevCompact) ||
		(bHasDeprMsg      && DeprecationMessage != PrevDeprMsg);

	if (!bAnyChange)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetBoolField(TEXT("unchanged"), true);
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("functionName"), FunctionName);
		Noop->SetStringField(TEXT("kind"), GraphKindName(Kind));
		return MCPResult(Noop);
	}

	// ── nothing above this line writes ──
	if (Entry)
	{
		Entry->Modify();
		if (bHasPure)
		{
			// The engine pairs Pure with Callable: a pure function still has to
			// be callable for its node to appear in the palette.
			if (bPure) Entry->AddExtraFlags(FUNC_BlueprintPure | FUNC_BlueprintCallable);
			else       Entry->ClearExtraFlags(FUNC_BlueprintPure);
		}
		if (bHasConst)
		{
			if (bConst) Entry->AddExtraFlags(FUNC_Const);
			else        Entry->ClearExtraFlags(FUNC_Const);
		}
		if (bHasAccess)
		{
			Entry->ClearExtraFlags(FUNC_Public | FUNC_Protected | FUNC_Private);
			if      (AccessSpecifier == TEXT("public"))    Entry->AddExtraFlags(FUNC_Public);
			else if (AccessSpecifier == TEXT("protected")) Entry->AddExtraFlags(FUNC_Protected);
			else                                           Entry->AddExtraFlags(FUNC_Private);
		}
	}

	if (bHasCategory)     Meta->Category = FText::FromString(Category);
	if (bHasTooltip)      Meta->ToolTip = FText::FromString(Tooltip);
	if (bHasKeywords)     Meta->Keywords = FText::FromString(Keywords);
	if (bHasCompact)      Meta->CompactNodeTitle = FText::FromString(CompactNodeTitle);
	if (bHasCallInEditor) Meta->bCallInEditor = bCallInEditor;
	if (bHasThreadSafe)   Meta->bThreadSafe = bThreadSafe;
	if (bHasDeprecated)   Meta->bIsDeprecated = bDeprecated;
	if (bHasDeprMsg)      Meta->DeprecationMessage = DeprecationMessage;

	RecompileAfterStructuralEdit(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("functionName"), FunctionName);
	Result->SetStringField(TEXT("kind"), GraphKindName(Kind));
	Result->SetStringField(TEXT("objectPath"), Graph->GetPathName());
	if (Entry)
	{
		Result->SetBoolField(TEXT("pure"), (Entry->GetExtraFlags() & FUNC_BlueprintPure) != 0);
		Result->SetBoolField(TEXT("isConst"), (Entry->GetExtraFlags() & FUNC_Const) != 0);
		Result->SetStringField(TEXT("accessSpecifier"),
			(Entry->GetExtraFlags() & FUNC_Private) != 0   ? TEXT("private") :
			(Entry->GetExtraFlags() & FUNC_Protected) != 0 ? TEXT("protected") : TEXT("public"));
	}
	Result->SetStringField(TEXT("category"), Meta->Category.ToString());
	Result->SetStringField(TEXT("tooltip"), Meta->ToolTip.ToString());
	Result->SetStringField(TEXT("keywords"), Meta->Keywords.ToString());
	Result->SetStringField(TEXT("compactNodeTitle"), Meta->CompactNodeTitle.ToString());
	Result->SetBoolField(TEXT("callInEditor"), Meta->bCallInEditor);
	Result->SetBoolField(TEXT("threadSafe"), Meta->bThreadSafe);
	Result->SetBoolField(TEXT("deprecated"), Meta->bIsDeprecated);
	Result->SetStringField(TEXT("deprecationMessage"), Meta->DeprecationMessage);

	// Exact inverse: only the values this call actually asked to change are
	// restored, so replaying it cannot clobber a field it never touched.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("functionName"), FunctionName);
	if (bHasPure)         Payload->SetBoolField(TEXT("pure"), bPrevPure);
	if (bHasConst)        Payload->SetBoolField(TEXT("isConst"), bPrevConst);
	if (bHasAccess)       Payload->SetStringField(TEXT("accessSpecifier"), PrevAccess);
	if (bHasCallInEditor) Payload->SetBoolField(TEXT("callInEditor"), bPrevCallInEditor);
	if (bHasThreadSafe)   Payload->SetBoolField(TEXT("threadSafe"), bPrevThreadSafe);
	if (bHasDeprecated)   Payload->SetBoolField(TEXT("deprecated"), bPrevDeprecated);
	if (bHasCategory)     Payload->SetStringField(TEXT("category"), PrevCategory);
	if (bHasTooltip)      Payload->SetStringField(TEXT("tooltip"), PrevTooltip);
	if (bHasKeywords)     Payload->SetStringField(TEXT("keywords"), PrevKeywords);
	if (bHasCompact)      Payload->SetStringField(TEXT("compactNodeTitle"), PrevCompact);
	if (bHasDeprMsg)      Payload->SetStringField(TEXT("deprecationMessage"), PrevDeprMsg);
	MCPSetRollback(Result, TEXT("set_function_properties"), Payload);
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// list_graph_parameters
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FBlueprintHandlers::ListGraphParameters(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPBlueprintDepth;

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	const FString FunctionName = OptionalString(Params, TEXT("functionName"));
	const FString EventName = OptionalString(Params, TEXT("eventName"));
	const FString GraphName = OptionalString(Params, TEXT("graphName"));

	if (FunctionName.IsEmpty() == EventName.IsEmpty())
	{
		return MCPError(FString::Printf(
			TEXT("Name exactly one target: 'functionName' for a function, macro or dispatcher signature, or 'eventName' for a custom event. Graphs: %s. Custom events: %s"),
			*ParameterisedGraphList(Blueprint), *CustomEventNameList(Blueprint)));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);

	UK2Node_EditablePinBase* InputOwner = nullptr;
	UK2Node_EditablePinBase* OutputOwner = nullptr;

	if (!EventName.IsEmpty())
	{
		UK2Node_CustomEvent* Event = FindCustomEvent(Blueprint, GraphName, EventName);
		if (!Event)
		{
			return MCPError(FString::Printf(
				TEXT("No custom event named '%s'%s. Custom events: %s"),
				*EventName,
				GraphName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" in graph '%s'"), *GraphName),
				*CustomEventNameList(Blueprint)));
		}
		InputOwner = Event;
		Result->SetStringField(TEXT("kind"), TEXT("customEvent"));
		Result->SetStringField(TEXT("eventName"), EventName);
		Result->SetStringField(TEXT("graphName"), Event->GetGraph() ? Event->GetGraph()->GetName() : FString());
		Result->SetStringField(TEXT("nodeId"), Event->NodeGuid.ToString());
		// An event has no return values at all; saying so beats an empty array
		// the caller has to interpret.
		Result->SetBoolField(TEXT("supportsOutputs"), false);
	}
	else
	{
		EGraphKind Kind = EGraphKind::Unknown;
		UEdGraph* Graph = FindParameterisedGraph(Blueprint, FunctionName, Kind);
		if (!Graph)
		{
			return MCPError(FString::Printf(
				TEXT("No function, macro or dispatcher signature named '%s'. Graphs: %s"),
				*FunctionName, *ParameterisedGraphList(Blueprint)));
		}
		InputOwner = FindInputOwner(Graph, Kind);
		OutputOwner = FindOutputOwner(Graph, Kind, /*bCreate=*/false);
		Result->SetStringField(TEXT("kind"), GraphKindName(Kind));
		Result->SetStringField(TEXT("functionName"), FunctionName);
		Result->SetStringField(TEXT("graphName"), Graph->GetName());
		Result->SetStringField(TEXT("objectPath"), Graph->GetPathName());
		Result->SetBoolField(TEXT("supportsOutputs"), Kind != EGraphKind::DelegateSignature);
	}

	TArray<TSharedPtr<FJsonValue>> Inputs;
	if (InputOwner)
	{
		for (int32 i = 0; i < InputOwner->UserDefinedPins.Num(); ++i)
		{
			if (InputOwner->UserDefinedPins[i].IsValid())
			{
				Inputs.Add(MakeShared<FJsonValueObject>(UserPinJson(InputOwner->UserDefinedPins[i], i)));
			}
		}
	}
	TArray<TSharedPtr<FJsonValue>> Outputs;
	if (OutputOwner)
	{
		for (int32 i = 0; i < OutputOwner->UserDefinedPins.Num(); ++i)
		{
			if (OutputOwner->UserDefinedPins[i].IsValid())
			{
				Outputs.Add(MakeShared<FJsonValueObject>(UserPinJson(OutputOwner->UserDefinedPins[i], i)));
			}
		}
	}

	Result->SetArrayField(TEXT("inputs"), Inputs);
	Result->SetArrayField(TEXT("outputs"), Outputs);
	Result->SetNumberField(TEXT("inputCount"), Inputs.Num());
	Result->SetNumberField(TEXT("outputCount"), Outputs.Num());
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// edit_graph_parameters
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FBlueprintHandlers::EditGraphParameters(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPBlueprintDepth;

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString Op;
	if (auto Err = RequireString(Params, TEXT("op"), Op)) return Err;

	static const TCHAR* OpList = TEXT("add, remove, rename, set_type, set_default, reorder");

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	const FString FunctionName = OptionalString(Params, TEXT("functionName"));
	const FString EventName = OptionalString(Params, TEXT("eventName"));
	const FString GraphName = OptionalString(Params, TEXT("graphName"));
	const bool bIsOutput = OptionalBool(Params, TEXT("isOutput"), false);

	if (FunctionName.IsEmpty() == EventName.IsEmpty())
	{
		return MCPError(FString::Printf(
			TEXT("Name exactly one target: 'functionName' for a function or macro, or 'eventName' for a custom event. Graphs: %s. Custom events: %s"),
			*ParameterisedGraphList(Blueprint), *CustomEventNameList(Blueprint)));
	}

	// Resolve the owner node the operation applies to. An output parameter on a
	// function may need a return node minted, which only happens once the
	// operation is known to be an `add`.
	UK2Node_EditablePinBase* Owner = nullptr;
	EGraphKind Kind = EGraphKind::Unknown;
	UEdGraph* Graph = nullptr;
	FString TargetLabel;

	if (!EventName.IsEmpty())
	{
		if (bIsOutput)
		{
			return MCPError(TEXT("A custom event has inputs only; it never returns a value. Drop isOutput, or target a function with 'functionName'."));
		}
		UK2Node_CustomEvent* Event = FindCustomEvent(Blueprint, GraphName, EventName);
		if (!Event)
		{
			return MCPError(FString::Printf(
				TEXT("No custom event named '%s'. Custom events: %s"), *EventName, *CustomEventNameList(Blueprint)));
		}
		Owner = Event;
		Graph = Event->GetGraph();
		TargetLabel = FString::Printf(TEXT("custom event '%s'"), *EventName);
	}
	else
	{
		Graph = FindParameterisedGraph(Blueprint, FunctionName, Kind);
		if (!Graph)
		{
			return MCPError(FString::Printf(
				TEXT("No function, macro or dispatcher signature named '%s'. Graphs: %s"),
				*FunctionName, *ParameterisedGraphList(Blueprint)));
		}
		if (bIsOutput && Kind == EGraphKind::DelegateSignature)
		{
			return MCPError(FString::Printf(
				TEXT("'%s' is an event dispatcher signature. A dispatcher broadcasts and never returns, so it has inputs only."),
				*FunctionName));
		}
		Owner = bIsOutput
			? FindOutputOwner(Graph, Kind, /*bCreate=*/false)
			: FindInputOwner(Graph, Kind);
		TargetLabel = FString::Printf(TEXT("%s '%s'"), GraphKindName(Kind), *FunctionName);
	}

	// A function graph with no output parameters yet has no return node. One is
	// minted for op=add, but only AFTER that request has fully validated, so a
	// bad name or type cannot leave a stray return node behind.
	const bool bOwnerIsCreatable =
		!Owner && bIsOutput && EventName.IsEmpty() && Op == TEXT("add") && Kind != EGraphKind::Macro;

	if (!Owner && !bOwnerIsCreatable)
	{
		return MCPError(FString::Printf(
			TEXT("%s has no %s node to hold parameters. A function graph grows its return node on the first op=add with isOutput=true; a macro graph gets its Outputs tunnel from create_macro, so a macro without one has to be recreated."),
			*TargetLabel, bIsOutput ? TEXT("return") : TEXT("entry")));
	}

	const TCHAR* Direction = bIsOutput ? TEXT("output") : TEXT("input");
	// Entry-node outputs are the function's INPUTS, and vice versa. The pin
	// direction is the mirror of the parameter direction.
	const EEdGraphPinDirection PinDirection = bIsOutput ? EGPD_Input : EGPD_Output;

	// Read the resulting parameter list BEFORE recompiling: a recompile
	// reinstances the generated class, and there is no reason to read node state
	// across it when the answer is already known.
	auto Finish = [&](TSharedPtr<FJsonObject> Result)
	{
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("op"), Op);
		if (!FunctionName.IsEmpty()) Result->SetStringField(TEXT("functionName"), FunctionName);
		if (!EventName.IsEmpty())    Result->SetStringField(TEXT("eventName"), EventName);
		Result->SetBoolField(TEXT("isOutput"), bIsOutput);
		TArray<TSharedPtr<FJsonValue>> Now;
		for (int32 i = 0; i < Owner->UserDefinedPins.Num(); ++i)
		{
			if (Owner->UserDefinedPins[i].IsValid())
			{
				Now.Add(MakeShared<FJsonValueObject>(UserPinJson(Owner->UserDefinedPins[i], i)));
			}
		}
		Result->SetArrayField(TEXT("parameters"), Now);
		RecompileAfterStructuralEdit(Blueprint);
		return MCPResult(Result);
	};

	// ── add ──────────────────────────────────────────────────────────────────
	if (Op == TEXT("add"))
	{
		FString ParameterName;
		if (auto Err = RequireString(Params, TEXT("parameterName"), ParameterName)) return Err;
		const FString TypeSpec = OptionalString(Params, TEXT("parameterType"), TEXT("bool"));

		FEdGraphPinType PinType;
		FString TypeError;
		if (!ParsePinTypeSpec(TypeSpec, PinType, TypeError)) return MCPError(TypeError);

		// Only now, with a name and a valid type in hand, is it safe to create
		// the return node this parameter will live on.
		if (!Owner)
		{
			Owner = FindOutputOwner(Graph, Kind, /*bCreate=*/true);
			if (!Owner)
			{
				return MCPError(FString::Printf(
					TEXT("%s has no return node and one could not be created."), *TargetLabel));
			}
		}

		const FName PinName(*ParameterName);
		if (FindUserPin(Owner, PinName) != INDEX_NONE)
		{
			auto Noop = MCPSuccess();
			MCPSetExisted(Noop);
			Noop->SetStringField(TEXT("path"), AssetPath);
			Noop->SetStringField(TEXT("op"), Op);
			Noop->SetStringField(TEXT("parameterName"), ParameterName);
			Noop->SetBoolField(TEXT("isOutput"), bIsOutput);
			return MCPResult(Noop);
		}

		FText Refusal;
		if (!Owner->CanCreateUserDefinedPin(PinType, PinDirection, Refusal))
		{
			return MCPError(FString::Printf(
				TEXT("%s will not accept a %s of type '%s': %s"),
				*TargetLabel, Direction, *TypeSpec, *Refusal.ToString()));
		}

		Owner->Modify();
		if (!Owner->CreateUserDefinedPin(PinName, PinType, PinDirection, /*bUseUniqueName=*/false))
		{
			return MCPError(FString::Printf(
				TEXT("The editor refused to create %s '%s' of type '%s' on %s; nothing was changed."),
				Direction, *ParameterName, *TypeSpec, *TargetLabel));
		}
		Owner->ReconstructNode();

		auto Result = MCPSuccess();
		MCPSetCreated(Result);
		Result->SetStringField(TEXT("parameterName"), ParameterName);
		Result->SetStringField(TEXT("parameterType"), TypeSpec);

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		if (!FunctionName.IsEmpty()) Payload->SetStringField(TEXT("functionName"), FunctionName);
		if (!EventName.IsEmpty())    Payload->SetStringField(TEXT("eventName"), EventName);
		Payload->SetStringField(TEXT("op"), TEXT("remove"));
		Payload->SetStringField(TEXT("parameterName"), ParameterName);
		Payload->SetBoolField(TEXT("isOutput"), bIsOutput);
		MCPSetRollback(Result, TEXT("edit_graph_parameters"), Payload);
		return Finish(Result);
	}

	// ── remove ───────────────────────────────────────────────────────────────
	if (Op == TEXT("remove"))
	{
		FString ParameterName;
		if (auto Err = RequireString(Params, TEXT("parameterName"), ParameterName)) return Err;

		const FName PinName(*ParameterName);
		const int32 At = FindUserPin(Owner, PinName);
		if (At == INDEX_NONE)
		{
			auto Noop = MCPSuccess();
			MCPSetExisted(Noop);
			Noop->SetBoolField(TEXT("alreadyRemoved"), true);
			Noop->SetStringField(TEXT("path"), AssetPath);
			Noop->SetStringField(TEXT("op"), Op);
			Noop->SetStringField(TEXT("parameterName"), ParameterName);
			Noop->SetBoolField(TEXT("isOutput"), bIsOutput);
			return MCPResult(Noop);
		}

		// Capture the type and default before the pin is gone, so the inverse
		// can put back the same parameter rather than an approximation.
		bool bRoundTrips = true;
		const FString PrevType = PinTypeSpec(Owner->UserDefinedPins[At]->PinType, bRoundTrips);
		const FString PrevDefault = Owner->UserDefinedPins[At]->PinDefaultValue;
		const int32 PrevIndex = At;

		Owner->Modify();
		Owner->RemoveUserDefinedPinByName(PinName);
		Owner->ReconstructNode();

		auto Result = MCPSuccess();
		MCPSetUpdated(Result);
		Result->SetStringField(TEXT("parameterName"), ParameterName);
		Result->SetStringField(TEXT("previousType"), PrevType);
		Result->SetNumberField(TEXT("previousIndex"), PrevIndex);
		// A parameter removal breaks every wire that fed it. Re-adding restores
		// the parameter, never the connections.
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Re-adding restores the parameter with its type and default. Wires that were connected to it, and its position in the parameter list, are not restored; use op=reorder afterwards to put it back in place."));

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		if (!FunctionName.IsEmpty()) Payload->SetStringField(TEXT("functionName"), FunctionName);
		if (!EventName.IsEmpty())    Payload->SetStringField(TEXT("eventName"), EventName);
		Payload->SetStringField(TEXT("op"), TEXT("add"));
		Payload->SetStringField(TEXT("parameterName"), ParameterName);
		Payload->SetStringField(TEXT("parameterType"), PrevType);
		Payload->SetStringField(TEXT("defaultValue"), PrevDefault);
		Payload->SetBoolField(TEXT("isOutput"), bIsOutput);
		MCPSetRollback(Result, TEXT("edit_graph_parameters"), Payload);
		return Finish(Result);
	}

	// ── rename ───────────────────────────────────────────────────────────────
	if (Op == TEXT("rename"))
	{
		FString ParameterName;
		if (auto Err = RequireString(Params, TEXT("parameterName"), ParameterName)) return Err;
		FString NewName;
		if (auto Err = RequireString(Params, TEXT("newName"), NewName)) return Err;

		const FName OldPin(*ParameterName);
		const FName NewPin(*NewName);
		if (OldPin == NewPin)
		{
			auto Noop = MCPSuccess();
			MCPSetExisted(Noop);
			Noop->SetBoolField(TEXT("unchanged"), true);
			Noop->SetStringField(TEXT("path"), AssetPath);
			Noop->SetStringField(TEXT("op"), Op);
			Noop->SetStringField(TEXT("parameterName"), ParameterName);
			return MCPResult(Noop);
		}
		if (FindUserPin(Owner, OldPin) == INDEX_NONE)
		{
			return MCPError(FString::Printf(
				TEXT("%s has no %s named '%s'. Parameters: %s"),
				*TargetLabel, Direction, *ParameterName, *UserPinNameList(Owner)));
		}

		// Ask before doing: the test pass reports a collision without leaving
		// the node half-renamed.
		const ERenamePinResult Probe = Owner->RenameUserDefinedPin(OldPin, NewPin, /*bTest=*/true);
		if (Probe == ERenamePinResult_NameCollision)
		{
			return MCPError(FString::Printf(
				TEXT("%s already has a pin named '%s'. Parameters: %s"),
				*TargetLabel, *NewName, *UserPinNameList(Owner)));
		}
		if (Probe != ERenamePinResult_Success)
		{
			return MCPError(FString::Printf(
				TEXT("%s cannot rename '%s' to '%s'. Parameters: %s"),
				*TargetLabel, *ParameterName, *NewName, *UserPinNameList(Owner)));
		}
		Owner->Modify();
		Owner->RenameUserDefinedPin(OldPin, NewPin, /*bTest=*/false);

		auto Result = MCPSuccess();
		MCPSetUpdated(Result);
		Result->SetStringField(TEXT("parameterName"), NewName);
		Result->SetStringField(TEXT("previousName"), ParameterName);

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		if (!FunctionName.IsEmpty()) Payload->SetStringField(TEXT("functionName"), FunctionName);
		if (!EventName.IsEmpty())    Payload->SetStringField(TEXT("eventName"), EventName);
		Payload->SetStringField(TEXT("op"), TEXT("rename"));
		Payload->SetStringField(TEXT("parameterName"), NewName);
		Payload->SetStringField(TEXT("newName"), ParameterName);
		Payload->SetBoolField(TEXT("isOutput"), bIsOutput);
		MCPSetRollback(Result, TEXT("edit_graph_parameters"), Payload);
		return Finish(Result);
	}

	// ── set_type ─────────────────────────────────────────────────────────────
	if (Op == TEXT("set_type"))
	{
		FString ParameterName;
		if (auto Err = RequireString(Params, TEXT("parameterName"), ParameterName)) return Err;
		FString TypeSpec;
		if (auto Err = RequireString(Params, TEXT("parameterType"), TypeSpec)) return Err;

		const int32 At = FindUserPin(Owner, FName(*ParameterName));
		if (At == INDEX_NONE)
		{
			return MCPError(FString::Printf(
				TEXT("%s has no %s named '%s'. Parameters: %s"),
				*TargetLabel, Direction, *ParameterName, *UserPinNameList(Owner)));
		}

		FEdGraphPinType PinType;
		FString TypeError;
		if (!ParsePinTypeSpec(TypeSpec, PinType, TypeError)) return MCPError(TypeError);

		bool bRoundTrips = true;
		const FString PrevType = PinTypeSpec(Owner->UserDefinedPins[At]->PinType, bRoundTrips);
		if (PrevType == TypeSpec)
		{
			auto Noop = MCPSuccess();
			MCPSetExisted(Noop);
			Noop->SetBoolField(TEXT("unchanged"), true);
			Noop->SetStringField(TEXT("path"), AssetPath);
			Noop->SetStringField(TEXT("op"), Op);
			Noop->SetStringField(TEXT("parameterName"), ParameterName);
			Noop->SetStringField(TEXT("parameterType"), PrevType);
			return MCPResult(Noop);
		}

		FText Refusal;
		if (!Owner->CanCreateUserDefinedPin(PinType, PinDirection, Refusal))
		{
			return MCPError(FString::Printf(
				TEXT("%s will not accept a %s of type '%s': %s. Nothing was changed."),
				*TargetLabel, Direction, *TypeSpec, *Refusal.ToString()));
		}

		Owner->Modify();
		Owner->UserDefinedPins[At]->PinType = PinType;
		// A retype invalidates whatever default was stored for the old type.
		Owner->UserDefinedPins[At]->PinDefaultValue.Reset();
		Owner->ReconstructNode();

		auto Result = MCPSuccess();
		MCPSetUpdated(Result);
		Result->SetStringField(TEXT("parameterName"), ParameterName);
		Result->SetStringField(TEXT("parameterType"), TypeSpec);
		Result->SetStringField(TEXT("previousType"), PrevType);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Restoring the type restores the parameter, not the default value it carried nor any wire the retype broke."));

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		if (!FunctionName.IsEmpty()) Payload->SetStringField(TEXT("functionName"), FunctionName);
		if (!EventName.IsEmpty())    Payload->SetStringField(TEXT("eventName"), EventName);
		Payload->SetStringField(TEXT("op"), TEXT("set_type"));
		Payload->SetStringField(TEXT("parameterName"), ParameterName);
		Payload->SetStringField(TEXT("parameterType"), PrevType);
		Payload->SetBoolField(TEXT("isOutput"), bIsOutput);
		MCPSetRollback(Result, TEXT("edit_graph_parameters"), Payload);
		return Finish(Result);
	}

	// ── set_default ──────────────────────────────────────────────────────────
	if (Op == TEXT("set_default"))
	{
		FString ParameterName;
		if (auto Err = RequireString(Params, TEXT("parameterName"), ParameterName)) return Err;
		FString DefaultValue;
		if (!Params->TryGetStringField(TEXT("defaultValue"), DefaultValue))
		{
			return MCPError(TEXT("Missing required parameter 'defaultValue': the value as Unreal export text (e.g. '5', 'true', '(X=1.000000,Y=0.000000,Z=0.000000)'). Pass an empty string to clear it."));
		}

		const int32 At = FindUserPin(Owner, FName(*ParameterName));
		if (At == INDEX_NONE)
		{
			return MCPError(FString::Printf(
				TEXT("%s has no %s named '%s'. Parameters: %s"),
				*TargetLabel, Direction, *ParameterName, *UserPinNameList(Owner)));
		}

		const FString PrevDefault = Owner->UserDefinedPins[At]->PinDefaultValue;
		if (PrevDefault == DefaultValue)
		{
			auto Noop = MCPSuccess();
			MCPSetExisted(Noop);
			Noop->SetBoolField(TEXT("unchanged"), true);
			Noop->SetStringField(TEXT("path"), AssetPath);
			Noop->SetStringField(TEXT("op"), Op);
			Noop->SetStringField(TEXT("parameterName"), ParameterName);
			Noop->SetStringField(TEXT("defaultValue"), PrevDefault);
			return MCPResult(Noop);
		}

		Owner->Modify();
		if (!Owner->ModifyUserDefinedPinDefaultValue(Owner->UserDefinedPins[At], DefaultValue))
		{
			bool bRoundTrips = true;
			return MCPError(FString::Printf(
				TEXT("The editor refused '%s' as the default for %s '%s' (type %s). It has to be Unreal export text for that type; nothing was changed."),
				*DefaultValue, Direction, *ParameterName,
				*PinTypeSpec(Owner->UserDefinedPins[At]->PinType, bRoundTrips)));
		}

		auto Result = MCPSuccess();
		MCPSetUpdated(Result);
		Result->SetStringField(TEXT("parameterName"), ParameterName);
		Result->SetStringField(TEXT("defaultValue"), DefaultValue);
		Result->SetStringField(TEXT("previousDefaultValue"), PrevDefault);

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		if (!FunctionName.IsEmpty()) Payload->SetStringField(TEXT("functionName"), FunctionName);
		if (!EventName.IsEmpty())    Payload->SetStringField(TEXT("eventName"), EventName);
		Payload->SetStringField(TEXT("op"), TEXT("set_default"));
		Payload->SetStringField(TEXT("parameterName"), ParameterName);
		Payload->SetStringField(TEXT("defaultValue"), PrevDefault);
		Payload->SetBoolField(TEXT("isOutput"), bIsOutput);
		MCPSetRollback(Result, TEXT("edit_graph_parameters"), Payload);
		return Finish(Result);
	}

	// ── reorder ──────────────────────────────────────────────────────────────
	if (Op == TEXT("reorder"))
	{
		const TArray<TSharedPtr<FJsonValue>>* Order = nullptr;
		if (!Params->TryGetArrayField(TEXT("order"), Order) || !Order)
		{
			return MCPError(FString::Printf(
				TEXT("Missing required parameter 'order': the COMPLETE list of %s names in their desired order. Parameters: %s"),
				Direction, *UserPinNameList(Owner)));
		}
		if (Order->Num() != Owner->UserDefinedPins.Num())
		{
			return MCPError(FString::Printf(
				TEXT("'order' lists %d entries but %s has %d %ss. Pass the complete order, not a partial one. Parameters: %s"),
				Order->Num(), *TargetLabel, Owner->UserDefinedPins.Num(), Direction, *UserPinNameList(Owner)));
		}

		// Validate the whole batch before a single pin moves.
		TArray<int32> Desired;
		TSet<int32> Seen;
		for (int32 Slot = 0; Slot < Order->Num(); ++Slot)
		{
			FString Spelling;
			if (!(*Order)[Slot]->TryGetString(Spelling) || Spelling.IsEmpty())
			{
				return MCPError(FString::Printf(
					TEXT("order[%d] is not a parameter name. Parameters: %s"), Slot, *UserPinNameList(Owner)));
			}
			const int32 At = FindUserPin(Owner, FName(*Spelling));
			if (At == INDEX_NONE)
			{
				return MCPError(FString::Printf(
					TEXT("order[%d] ('%s') is not a %s of %s. Parameters: %s"),
					Slot, *Spelling, Direction, *TargetLabel, *UserPinNameList(Owner)));
			}
			if (Seen.Contains(At))
			{
				return MCPError(FString::Printf(
					TEXT("order[%d] repeats '%s'. Every parameter must appear exactly once."), Slot, *Spelling));
			}
			Seen.Add(At);
			Desired.Add(At);
		}

		TArray<TSharedPtr<FJsonValue>> PreviousOrder;
		bool bSame = true;
		for (int32 i = 0; i < Owner->UserDefinedPins.Num(); ++i)
		{
			PreviousOrder.Add(MakeShared<FJsonValueString>(Owner->UserDefinedPins[i]->PinName.ToString()));
			if (Desired[i] != i) bSame = false;
		}
		if (bSame)
		{
			auto Noop = MCPSuccess();
			MCPSetExisted(Noop);
			Noop->SetBoolField(TEXT("unchanged"), true);
			Noop->SetStringField(TEXT("path"), AssetPath);
			Noop->SetStringField(TEXT("op"), Op);
			Noop->SetArrayField(TEXT("order"), PreviousOrder);
			return MCPResult(Noop);
		}

		TArray<TSharedPtr<FUserPinInfo>> Reordered;
		Reordered.Reserve(Desired.Num());
		for (const int32 From : Desired) Reordered.Add(Owner->UserDefinedPins[From]);

		Owner->Modify();
		Owner->UserDefinedPins = Reordered;
		Owner->ReconstructNode();

		auto Result = MCPSuccess();
		MCPSetUpdated(Result);

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		if (!FunctionName.IsEmpty()) Payload->SetStringField(TEXT("functionName"), FunctionName);
		if (!EventName.IsEmpty())    Payload->SetStringField(TEXT("eventName"), EventName);
		Payload->SetStringField(TEXT("op"), TEXT("reorder"));
		Payload->SetArrayField(TEXT("order"), PreviousOrder);
		Payload->SetBoolField(TEXT("isOutput"), bIsOutput);
		MCPSetRollback(Result, TEXT("edit_graph_parameters"), Payload);
		return Finish(Result);
	}

	return MCPError(FString::Printf(TEXT("Unknown op '%s'. Expected one of: %s"), *Op, OpList));
}

// ─────────────────────────────────────────────────────────────────────────────
// rename_blueprint_variable
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FBlueprintHandlers::RenameBlueprintVariable(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPBlueprintDepth;

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString OldName;
	if (auto Err = RequireString(Params, TEXT("oldName"), OldName)) return Err;
	FString NewName;
	if (auto Err = RequireString(Params, TEXT("newName"), NewName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	const FName OldVar(*OldName);
	const FName NewVar(*NewName);

	if (OldVar == NewVar)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetBoolField(TEXT("unchanged"), true);
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("name"), OldName);
		return MCPResult(Noop);
	}

	// Both checks scan the whole array: stopping at the first match would miss a
	// collision declared after the variable being renamed.
	const FBPVariableDescription* Found = nullptr;
	bool bNewNameTaken = false;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == OldVar) Found = &Var;
		if (Var.VarName == NewVar) bNewNameTaken = true;
	}
	if (!Found)
	{
		return MCPError(FString::Printf(
			TEXT("No variable named '%s' declared on this Blueprint (inherited variables belong to the parent class). Variables: %s"),
			*OldName, *MemberVariableNameList(Blueprint)));
	}
	if (bNewNameTaken)
	{
		return MCPError(FString::Printf(
			TEXT("This Blueprint already has a variable named '%s'. Variables: %s"),
			*NewName, *MemberVariableNameList(Blueprint)));
	}
	if (UClass* ParentClass = Blueprint->ParentClass.Get())
	{
		if (ParentClass->FindPropertyByName(NewVar))
		{
			return MCPError(FString::Printf(
				TEXT("Parent class '%s' already declares '%s'. A Blueprint variable cannot shadow a parent property; pick another name."),
				*ParentClass->GetName(), *NewName));
		}
	}

	const int32 ReferencesBefore = CountVariableReferences(Blueprint, OldVar);

	// The reason this is not delete + add: RenameMemberVariable rewrites every
	// K2Node_VariableGet / K2Node_VariableSet in this Blueprint's graphs, plus
	// the RepNotify function name and the SCS entry when the variable is a
	// component. Recreating the variable would leave all of them dangling.
	FBlueprintEditorUtils::RenameMemberVariable(Blueprint, OldVar, NewVar);
	RecompileAfterStructuralEdit(Blueprint);

	const int32 ReferencesAfter = CountVariableReferences(Blueprint, NewVar);
	const int32 StillOld = CountVariableReferences(Blueprint, OldVar);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("oldName"), OldName);
	Result->SetStringField(TEXT("newName"), NewName);
	Result->SetNumberField(TEXT("referencesBefore"), ReferencesBefore);
	Result->SetNumberField(TEXT("referencesUpdated"), ReferencesAfter);
	// Counted rather than asserted: a node still on the old name after the
	// rename is a fixup the engine did not perform, and the caller should see it.
	Result->SetNumberField(TEXT("referencesStillOnOldName"), StillOld);
	Result->SetStringField(TEXT("referenceScope"), TEXT("thisBlueprint"));
	Result->SetStringField(TEXT("externalReferences"),
		TEXT("Other Blueprints bind this member through its variable GUID, which the rename preserves, so they rebind on load. Run asset(get_asset_referencers) on this Blueprint and blueprint(compile_all) over the result to confirm."));

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("oldName"), NewName);
	Payload->SetStringField(TEXT("newName"), OldName);
	MCPSetRollback(Result, TEXT("rename_blueprint_variable"), Payload);
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// get_blueprint_variable_metadata
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FBlueprintHandlers::GetBlueprintVariableMetadata(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPBlueprintDepth;

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString VarName;
	if (auto Err = RequireString(Params, TEXT("name"), VarName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	const FString FunctionName = OptionalString(Params, TEXT("functionName"));
	const FName VarFName(*VarName);
	const FBPVariableDescription* Found = nullptr;

	if (!FunctionName.IsEmpty())
	{
		UEdGraph* Graph = FindByName(Blueprint->FunctionGraphs, FunctionName);
		if (!Graph)
		{
			return MCPError(FString::Printf(
				TEXT("No function named '%s'. Graphs: %s"), *FunctionName, *ParameterisedGraphList(Blueprint)));
		}
		if (UK2Node_FunctionEntry* Entry = FindFunctionEntry(Graph))
		{
			for (const FBPVariableDescription& Var : Entry->LocalVariables)
			{
				if (Var.VarName == VarFName) { Found = &Var; break; }
			}
		}
		if (!Found)
		{
			return MCPError(FString::Printf(
				TEXT("Function '%s' has no local variable named '%s'. Use list_local_variables to see what it has."),
				*FunctionName, *VarName));
		}
	}
	else
	{
		for (const FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			if (Var.VarName == VarFName) { Found = &Var; break; }
		}
		if (!Found)
		{
			return MCPError(FString::Printf(
				TEXT("No variable named '%s' declared on this Blueprint. Variables: %s. For a local variable, pass functionName."),
				*VarName, *MemberVariableNameList(Blueprint)));
		}
	}

	TSharedPtr<FJsonObject> Metadata = MakeShared<FJsonObject>();
	for (const FBPVariableMetaDataEntry& Entry : Found->MetaDataArray)
	{
		Metadata->SetStringField(Entry.DataKey.ToString(), Entry.DataValue);
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("name"), VarName);
	if (!FunctionName.IsEmpty()) Result->SetStringField(TEXT("functionName"), FunctionName);
	Result->SetStringField(TEXT("scope"), FunctionName.IsEmpty() ? TEXT("member") : TEXT("local"));
	Result->SetObjectField(TEXT("metadata"), Metadata);
	Result->SetNumberField(TEXT("count"), Found->MetaDataArray.Num());
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_blueprint_variable_metadata
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FBlueprintHandlers::SetBlueprintVariableMetadata(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPBlueprintDepth;

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString VarName;
	if (auto Err = RequireString(Params, TEXT("name"), VarName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	const TSharedPtr<FJsonObject>* MetaObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("metadata"), MetaObj) || !MetaObj || !MetaObj->IsValid())
	{
		return MCPError(TEXT("Missing required parameter 'metadata': an object of key/value strings (ClampMin, UIMin, EditCondition, Bitmask, BitmaskEnum, MakeEditWidget, MultiLine, ...). A JSON null value removes that key."));
	}

	const FString FunctionName = OptionalString(Params, TEXT("functionName"));
	const FName VarFName(*VarName);

	// Resolve the scope, and prove the variable exists in it, before anything is
	// written. SetBlueprintVariableMetaData silently does nothing for a name it
	// cannot find, which would otherwise report success for a no-op.
	UStruct* LocalScope = nullptr;
	const FBPVariableDescription* Found = nullptr;
	if (!FunctionName.IsEmpty())
	{
		UEdGraph* Graph = FindByName(Blueprint->FunctionGraphs, FunctionName);
		if (!Graph)
		{
			return MCPError(FString::Printf(
				TEXT("No function named '%s'. Graphs: %s"), *FunctionName, *ParameterisedGraphList(Blueprint)));
		}
		LocalScope = FindLocalVariableScope(Blueprint, FunctionName);
		if (!LocalScope)
		{
			return MCPError(FString::Printf(
				TEXT("Function '%s' has no compiled signature yet, so its local variables cannot be addressed. Run blueprint(compile) first."),
				*FunctionName));
		}
		if (UK2Node_FunctionEntry* Entry = FindFunctionEntry(Graph))
		{
			for (const FBPVariableDescription& Var : Entry->LocalVariables)
			{
				if (Var.VarName == VarFName) { Found = &Var; break; }
			}
		}
		if (!Found)
		{
			return MCPError(FString::Printf(
				TEXT("Function '%s' has no local variable named '%s'."), *FunctionName, *VarName));
		}
	}
	else
	{
		for (const FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			if (Var.VarName == VarFName) { Found = &Var; break; }
		}
		if (!Found)
		{
			return MCPError(FString::Printf(
				TEXT("No variable named '%s' declared on this Blueprint. Variables: %s. For a local variable, pass functionName."),
				*VarName, *MemberVariableNameList(Blueprint)));
		}
	}

	// Whole-batch validation, then a change plan, then the writes.
	struct FPlannedMeta
	{
		FName Key;
		bool bRemove = false;
		FString Value;
		bool bHadPrevious = false;
		FString PreviousValue;
	};
	TArray<FPlannedMeta> Planned;

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*MetaObj)->Values)
	{
		if (Pair.Key.IsEmpty()) return MCPError(TEXT("metadata carries an empty key."));
		FPlannedMeta Plan;
		Plan.Key = FName(*Pair.Key);
		if (!Pair.Value.IsValid() || Pair.Value->Type == EJson::Null)
		{
			Plan.bRemove = true;
		}
		else if (!Pair.Value->TryGetString(Plan.Value))
		{
			return MCPError(FString::Printf(
				TEXT("metadata['%s'] must be a string or null; Unreal stores every metadata value as text (ClampMin: '0', EditCondition: 'bEnabled'). Null removes the key."),
				*Pair.Key));
		}
		const int32 At = Found->FindMetaDataEntryIndexForKey(Plan.Key);
		Plan.bHadPrevious = At != INDEX_NONE;
		if (Plan.bHadPrevious) Plan.PreviousValue = Found->MetaDataArray[At].DataValue;
		Planned.Add(Plan);
	}

	if (Planned.Num() == 0)
	{
		return MCPError(TEXT("'metadata' is empty. Pass at least one key."));
	}

	bool bAnyChange = false;
	for (const FPlannedMeta& Plan : Planned)
	{
		if (Plan.bRemove ? Plan.bHadPrevious : (!Plan.bHadPrevious || Plan.PreviousValue != Plan.Value))
		{
			bAnyChange = true;
			break;
		}
	}
	if (!bAnyChange)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetBoolField(TEXT("unchanged"), true);
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("name"), VarName);
		return MCPResult(Noop);
	}

	for (const FPlannedMeta& Plan : Planned)
	{
		if (Plan.bRemove)
		{
			if (Plan.bHadPrevious)
			{
				FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(Blueprint, VarFName, LocalScope, Plan.Key);
			}
		}
		else if (!Plan.bHadPrevious || Plan.PreviousValue != Plan.Value)
		{
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, VarFName, LocalScope, Plan.Key, Plan.Value);
		}
	}

	RecompileAfterStructuralEdit(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("name"), VarName);
	if (!FunctionName.IsEmpty()) Result->SetStringField(TEXT("functionName"), FunctionName);
	Result->SetStringField(TEXT("scope"), FunctionName.IsEmpty() ? TEXT("member") : TEXT("local"));
	Result->SetNumberField(TEXT("keysWritten"), Planned.Num());

	// Exact inverse, including removal: a key that did not exist before is
	// restored to null, which this same action reads as "remove it again".
	TSharedPtr<FJsonObject> Previous = MakeShared<FJsonObject>();
	for (const FPlannedMeta& Plan : Planned)
	{
		if (Plan.bHadPrevious) Previous->SetStringField(Plan.Key.ToString(), Plan.PreviousValue);
		else                   Previous->SetField(Plan.Key.ToString(), MakeShared<FJsonValueNull>());
	}
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("name"), VarName);
	if (!FunctionName.IsEmpty()) Payload->SetStringField(TEXT("functionName"), FunctionName);
	Payload->SetObjectField(TEXT("metadata"), Previous);
	MCPSetRollback(Result, TEXT("set_blueprint_variable_metadata"), Payload);
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// edit_local_variable
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FBlueprintHandlers::EditLocalVariable(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPBlueprintDepth;

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString FunctionName;
	if (auto Err = RequireString(Params, TEXT("functionName"), FunctionName)) return Err;
	FString VarName;
	if (auto Err = RequireString(Params, TEXT("name"), VarName)) return Err;
	FString Op;
	if (auto Err = RequireString(Params, TEXT("op"), Op)) return Err;

	static const TCHAR* OpList = TEXT("rename, remove, set_type, set_default");

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	UEdGraph* Graph = FindByName(Blueprint->FunctionGraphs, FunctionName);
	if (!Graph)
	{
		return MCPError(FString::Printf(
			TEXT("No function named '%s' on this Blueprint. Local variables live on function graphs. Graphs: %s"),
			*FunctionName, *ParameterisedGraphList(Blueprint)));
	}
	UK2Node_FunctionEntry* Entry = FindFunctionEntry(Graph);
	if (!Entry)
	{
		return MCPError(FString::Printf(TEXT("Function '%s' has no entry node."), *FunctionName));
	}

	const FName VarFName(*VarName);
	int32 At = INDEX_NONE;
	for (int32 i = 0; i < Entry->LocalVariables.Num(); ++i)
	{
		if (Entry->LocalVariables[i].VarName == VarFName) { At = i; break; }
	}

	TArray<FString> Existing;
	for (const FBPVariableDescription& Var : Entry->LocalVariables) Existing.Add(Var.VarName.ToString());
	const FString ExistingList = Existing.Num() > 0 ? FString::Join(Existing, TEXT(", ")) : FString(TEXT("(none)"));

	// `remove` is idempotent: a variable that is already gone is not an error.
	if (At == INDEX_NONE && Op != TEXT("remove"))
	{
		return MCPError(FString::Printf(
			TEXT("Function '%s' has no local variable named '%s'. Local variables: %s"),
			*FunctionName, *VarName, *ExistingList));
	}

	// The engine's local-variable API addresses a variable through the compiled
	// function that owns it, so a Blueprint that has never compiled cannot be
	// edited this way. Say that rather than fail silently.
	UStruct* Scope = FindLocalVariableScope(Blueprint, FunctionName);

	auto RequireScope = [&]() -> TSharedPtr<FJsonValue>
	{
		if (Scope) return nullptr;
		return MCPError(FString::Printf(
			TEXT("Function '%s' has no compiled signature, so its local variables cannot be addressed. Run blueprint(compile) on this Blueprint first."),
			*FunctionName));
	};

	if (Op == TEXT("remove"))
	{
		if (At == INDEX_NONE)
		{
			auto Noop = MCPSuccess();
			MCPSetExisted(Noop);
			Noop->SetBoolField(TEXT("alreadyRemoved"), true);
			Noop->SetStringField(TEXT("path"), AssetPath);
			Noop->SetStringField(TEXT("functionName"), FunctionName);
			Noop->SetStringField(TEXT("name"), VarName);
			return MCPResult(Noop);
		}
		if (auto Err = RequireScope()) return Err;

		bool bRoundTrips = true;
		const FString PrevType = PinTypeSpec(Entry->LocalVariables[At].VarType, bRoundTrips);
		const FString PrevDefault = Entry->LocalVariables[At].DefaultValue;

		FBlueprintEditorUtils::RemoveLocalVariable(Blueprint, Scope, VarFName);
		RecompileAfterStructuralEdit(Blueprint);

		auto Result = MCPSuccess();
		MCPSetUpdated(Result);
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("functionName"), FunctionName);
		Result->SetStringField(TEXT("name"), VarName);
		Result->SetStringField(TEXT("previousType"), PrevType);
		Result->SetStringField(TEXT("previousDefaultValue"), PrevDefault);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), PrevType.Contains(TEXT("[")) || PrevType.Contains(TEXT("<"))
			? FString::Printf(
				TEXT("RemoveLocalVariable also deletes every get and set node in the function that referenced it, and those stay deleted. This variable's type ('%s') is a container, which add_local_variable cannot declare, so the rollback restores the element type only."),
				*PrevType)
			: FString(TEXT("RemoveLocalVariable also deletes every get and set node in the function that referenced it. Re-adding restores the declaration with its type; the nodes and their wires stay deleted.")));

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("functionName"), FunctionName);
		Payload->SetStringField(TEXT("name"), VarName);
		Payload->SetStringField(TEXT("varType"), PrevType);
		MCPSetRollback(Result, TEXT("add_local_variable"), Payload);
		return MCPResult(Result);
	}

	if (Op == TEXT("rename"))
	{
		FString NewName;
		if (auto Err = RequireString(Params, TEXT("newName"), NewName)) return Err;
		const FName NewVar(*NewName);
		if (NewVar == VarFName)
		{
			auto Noop = MCPSuccess();
			MCPSetExisted(Noop);
			Noop->SetBoolField(TEXT("unchanged"), true);
			Noop->SetStringField(TEXT("path"), AssetPath);
			Noop->SetStringField(TEXT("functionName"), FunctionName);
			Noop->SetStringField(TEXT("name"), VarName);
			return MCPResult(Noop);
		}
		for (const FBPVariableDescription& Var : Entry->LocalVariables)
		{
			if (Var.VarName == NewVar)
			{
				return MCPError(FString::Printf(
					TEXT("Function '%s' already has a local variable named '%s'. Local variables: %s"),
					*FunctionName, *NewName, *ExistingList));
			}
		}
		if (auto Err = RequireScope()) return Err;

		// RenameLocalVariable rewrites the get/set nodes inside the function,
		// which a remove-then-add pair would destroy.
		FBlueprintEditorUtils::RenameLocalVariable(Blueprint, Scope, VarFName, NewVar);
		RecompileAfterStructuralEdit(Blueprint);

		auto Result = MCPSuccess();
		MCPSetUpdated(Result);
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("functionName"), FunctionName);
		Result->SetStringField(TEXT("name"), NewName);
		Result->SetStringField(TEXT("previousName"), VarName);

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("functionName"), FunctionName);
		Payload->SetStringField(TEXT("name"), NewName);
		Payload->SetStringField(TEXT("op"), TEXT("rename"));
		Payload->SetStringField(TEXT("newName"), VarName);
		MCPSetRollback(Result, TEXT("edit_local_variable"), Payload);
		return MCPResult(Result);
	}

	if (Op == TEXT("set_type"))
	{
		FString TypeSpec;
		if (auto Err = RequireString(Params, TEXT("varType"), TypeSpec)) return Err;

		FEdGraphPinType PinType;
		FString TypeError;
		if (!ParsePinTypeSpec(TypeSpec, PinType, TypeError)) return MCPError(TypeError);

		bool bRoundTrips = true;
		const FString PrevType = PinTypeSpec(Entry->LocalVariables[At].VarType, bRoundTrips);
		if (PrevType == TypeSpec)
		{
			auto Noop = MCPSuccess();
			MCPSetExisted(Noop);
			Noop->SetBoolField(TEXT("unchanged"), true);
			Noop->SetStringField(TEXT("path"), AssetPath);
			Noop->SetStringField(TEXT("functionName"), FunctionName);
			Noop->SetStringField(TEXT("name"), VarName);
			Noop->SetStringField(TEXT("varType"), PrevType);
			return MCPResult(Noop);
		}
		if (auto Err = RequireScope()) return Err;

		FBlueprintEditorUtils::ChangeLocalVariableType(Blueprint, Scope, VarFName, PinType);
		RecompileAfterStructuralEdit(Blueprint);

		auto Result = MCPSuccess();
		MCPSetUpdated(Result);
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("functionName"), FunctionName);
		Result->SetStringField(TEXT("name"), VarName);
		Result->SetStringField(TEXT("varType"), TypeSpec);
		Result->SetStringField(TEXT("previousType"), PrevType);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Restoring the type restores the declaration, not the wires a retype broke on the get/set nodes."));

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("functionName"), FunctionName);
		Payload->SetStringField(TEXT("name"), VarName);
		Payload->SetStringField(TEXT("op"), TEXT("set_type"));
		Payload->SetStringField(TEXT("varType"), PrevType);
		MCPSetRollback(Result, TEXT("edit_local_variable"), Payload);
		return MCPResult(Result);
	}

	if (Op == TEXT("set_default"))
	{
		FString DefaultValue;
		if (!Params->TryGetStringField(TEXT("defaultValue"), DefaultValue))
		{
			return MCPError(TEXT("Missing required parameter 'defaultValue': the value as Unreal export text. Pass an empty string to clear it."));
		}
		const FString PrevDefault = Entry->LocalVariables[At].DefaultValue;
		if (PrevDefault == DefaultValue)
		{
			auto Noop = MCPSuccess();
			MCPSetExisted(Noop);
			Noop->SetBoolField(TEXT("unchanged"), true);
			Noop->SetStringField(TEXT("path"), AssetPath);
			Noop->SetStringField(TEXT("functionName"), FunctionName);
			Noop->SetStringField(TEXT("name"), VarName);
			Noop->SetStringField(TEXT("defaultValue"), PrevDefault);
			return MCPResult(Noop);
		}

		// A local variable's default is a plain string on the entry node's own
		// description array, which is where add_local_variable put the variable
		// in the first place. There is no engine helper and none is needed.
		Entry->Modify();
		Entry->LocalVariables[At].DefaultValue = DefaultValue;
		RecompileAfterStructuralEdit(Blueprint);

		auto Result = MCPSuccess();
		MCPSetUpdated(Result);
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("functionName"), FunctionName);
		Result->SetStringField(TEXT("name"), VarName);
		Result->SetStringField(TEXT("defaultValue"), DefaultValue);
		Result->SetStringField(TEXT("previousDefaultValue"), PrevDefault);

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("functionName"), FunctionName);
		Payload->SetStringField(TEXT("name"), VarName);
		Payload->SetStringField(TEXT("op"), TEXT("set_default"));
		Payload->SetStringField(TEXT("defaultValue"), PrevDefault);
		MCPSetRollback(Result, TEXT("edit_local_variable"), Payload);
		return MCPResult(Result);
	}

	return MCPError(FString::Printf(TEXT("Unknown op '%s'. Expected one of: %s"), *Op, OpList));
}

// ─────────────────────────────────────────────────────────────────────────────
// list_event_dispatchers
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FBlueprintHandlers::ListEventDispatchers(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPBlueprintDepth;

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("blueprintPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	TArray<TSharedPtr<FJsonValue>> Dispatchers;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (!IsDispatcher(Var)) continue;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Var.VarName.ToString());
		Entry->SetStringField(TEXT("guid"), Var.VarGuid.ToString());
		Entry->SetStringField(TEXT("category"), Var.Category.ToString());

		// The signature lives in a separate graph, and a dispatcher whose graph
		// is missing is exactly the state that produced "No SignatureFunction in
		// MulticastDelegateProperty" at compile time, so report it.
		UEdGraph* Signature = FindByName(Blueprint->DelegateSignatureGraphs, Var.VarName.ToString());
		Entry->SetBoolField(TEXT("hasSignatureGraph"), Signature != nullptr);
		Entry->SetStringField(TEXT("signatureGraph"), Signature ? Signature->GetName() : FString());

		TArray<TSharedPtr<FJsonValue>> Parameters;
		if (Signature)
		{
			if (UK2Node_EditablePinBase* Owner = FindInputOwner(Signature, EGraphKind::DelegateSignature))
			{
				for (int32 i = 0; i < Owner->UserDefinedPins.Num(); ++i)
				{
					if (Owner->UserDefinedPins[i].IsValid())
					{
						Parameters.Add(MakeShared<FJsonValueObject>(UserPinJson(Owner->UserDefinedPins[i], i)));
					}
				}
			}
		}
		Entry->SetArrayField(TEXT("parameters"), Parameters);
		Dispatchers.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetArrayField(TEXT("dispatchers"), Dispatchers);
	Result->SetNumberField(TEXT("count"), Dispatchers.Num());
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// remove_event_dispatcher
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FBlueprintHandlers::RemoveEventDispatcher(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPBlueprintDepth;

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("blueprintPath"), TEXT("assetPath"), AssetPath)) return Err;
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	const FName DispatcherName(*Name);
	const FBPVariableDescription* Found = nullptr;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == DispatcherName) { Found = &Var; break; }
	}
	UEdGraph* Signature = FindByName(Blueprint->DelegateSignatureGraphs, Name);

	if (!Found && !Signature)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetBoolField(TEXT("alreadyRemoved"), true);
		Noop->SetStringField(TEXT("blueprintPath"), AssetPath);
		Noop->SetStringField(TEXT("name"), Name);
		return MCPResult(Noop);
	}
	if (Found && !IsDispatcher(*Found))
	{
		return MCPError(FString::Printf(
			TEXT("'%s' is a variable but not an event dispatcher (its type is '%s', not a multicast delegate). Use delete_variable for an ordinary variable. Dispatchers: %s"),
			*Name, *Found->VarType.PinCategory.ToString(), *DispatcherNameList(Blueprint)));
	}

	// Capture the signature so the inverse re-declares the same parameters
	// rather than a bare delegate that no existing binding matches.
	TArray<TSharedPtr<FJsonValue>> SignatureParams;
	TArray<FString> UnreplayableTypes;
	if (Signature)
	{
		if (UK2Node_EditablePinBase* Owner = FindInputOwner(Signature, EGraphKind::DelegateSignature))
		{
			for (const TSharedPtr<FUserPinInfo>& Info : Owner->UserDefinedPins)
			{
				if (!Info.IsValid()) continue;
				bool bRoundTrips = true;
				FString TypeSpec = PinTypeSpec(Info->PinType, bRoundTrips);
				// add_event_dispatcher reads a narrower type vocabulary than
				// PinTypeSpec writes: it spells the double-precision real as
				// "float", and it has no form at all for a container or a
				// wrapper type. Translate what translates, and name what does
				// not so the rollback record is not read as exact.
				if (TypeSpec == TEXT("double")) TypeSpec = TEXT("float");
				if (!bRoundTrips || Info->PinType.ContainerType != EPinContainerType::None
					|| TypeSpec.StartsWith(TEXT("TSubclassOf<")) || TypeSpec.StartsWith(TEXT("TSoftObjectPtr<"))
					|| TypeSpec.StartsWith(TEXT("TSoftClassPtr<")) || TypeSpec.StartsWith(TEXT("enum:")))
				{
					UnreplayableTypes.Add(FString::Printf(TEXT("%s (%s)"), *Info->PinName.ToString(), *TypeSpec));
				}
				TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
				P->SetStringField(TEXT("name"), Info->PinName.ToString());
				P->SetStringField(TEXT("type"), TypeSpec);
				SignatureParams.Add(MakeShared<FJsonValueObject>(P));
			}
		}
	}

	// Both halves, in this order: add_event_dispatcher creates a member variable
	// AND a signature graph, so removing only the variable (which is all
	// delete_variable does) leaves an orphan graph the compiler still walks.
	if (Found)
	{
		FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, DispatcherName);
	}
	if (Signature)
	{
		FBlueprintEditorUtils::RemoveGraph(Blueprint, Signature);
	}
	RecompileAfterStructuralEdit(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("blueprintPath"), AssetPath);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetBoolField(TEXT("removedVariable"), Found != nullptr);
	Result->SetBoolField(TEXT("removedSignatureGraph"), Signature != nullptr);
	Result->SetArrayField(TEXT("parameters"), SignatureParams);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"), UnreplayableTypes.Num() > 0
		? FString::Printf(
			TEXT("Re-adding restores the dispatcher, and Bind, Unbind and Call nodes that referenced it are removed with the variable and stay removed. These parameters carry a type add_event_dispatcher cannot declare and come back as wildcards: %s."),
			*FString::Join(UnreplayableTypes, TEXT(", ")))
		: FString(TEXT("Re-adding restores the dispatcher and its parameter signature. Bind, Unbind and Call nodes that referenced it are removed with the variable and are not restored.")));

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("blueprintPath"), AssetPath);
	Payload->SetStringField(TEXT("name"), Name);
	if (SignatureParams.Num() > 0) Payload->SetArrayField(TEXT("parameters"), SignatureParams);
	MCPSetRollback(Result, TEXT("add_event_dispatcher"), Payload);
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// add_custom_event
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FBlueprintHandlers::AddCustomEvent(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPBlueprintDepth;

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString EventName;
	if (auto Err = RequireString(Params, TEXT("eventName"), EventName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	const FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	// Idempotent on the event NAME, which is the identity a custom event has.
	// add_node has no natural key and cannot do this, which is why placing a
	// custom event through it twice silently produces two.
	if (UK2Node_CustomEvent* Existing = FindCustomEvent(Blueprint, GraphName, EventName))
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("graphName"), GraphName);
		Noop->SetStringField(TEXT("eventName"), EventName);
		Noop->SetStringField(TEXT("nodeId"), Existing->NodeGuid.ToString());
		return MCPResult(Noop);
	}

	// ── validate the whole request before creating anything ──
	FString NetMode = OptionalString(Params, TEXT("netMode"), TEXT("none"));
	{
		const FString Lowered = NetMode.ToLower();
		if      (Lowered == TEXT("none"))      NetMode = TEXT("none");
		else if (Lowered == TEXT("multicast")) NetMode = TEXT("multicast");
		else if (Lowered == TEXT("server"))    NetMode = TEXT("server");
		else if (Lowered == TEXT("client"))    NetMode = TEXT("client");
		else
		{
			return MCPError(FString::Printf(
				TEXT("Unknown netMode '%s'. Expected none, multicast, server or client."), *NetMode));
		}
	}
	const bool bReliable = OptionalBool(Params, TEXT("reliable"), true);
	const bool bCallInEditor = OptionalBool(Params, TEXT("callInEditor"), false);

	struct FPlannedParam { FName Name; FEdGraphPinType Type; FString Spec; };
	TArray<FPlannedParam> PlannedParams;
	const TArray<TSharedPtr<FJsonValue>>* ParamArray = nullptr;
	if (Params->TryGetArrayField(TEXT("parameters"), ParamArray) && ParamArray)
	{
		TSet<FName> SeenNames;
		for (int32 Slot = 0; Slot < ParamArray->Num(); ++Slot)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!(*ParamArray)[Slot]->TryGetObject(Obj) || !Obj || !Obj->IsValid())
			{
				return MCPError(FString::Printf(
					TEXT("parameters[%d] is not an object. Each entry is {name, type}."), Slot));
			}
			FString PName;
			if (!(*Obj)->TryGetStringField(TEXT("name"), PName) || PName.IsEmpty())
			{
				return MCPError(FString::Printf(TEXT("parameters[%d] carries no 'name'."), Slot));
			}
			const FName PinName(*PName);
			if (SeenNames.Contains(PinName))
			{
				return MCPError(FString::Printf(
					TEXT("parameters[%d] repeats the name '%s'. Every parameter needs a distinct name."), Slot, *PName));
			}
			SeenNames.Add(PinName);

			const FString TypeSpec = OptionalString(*Obj, TEXT("type"), TEXT("bool"));
			FPlannedParam Plan;
			Plan.Name = PinName;
			Plan.Spec = TypeSpec;
			FString TypeError;
			if (!ParsePinTypeSpec(TypeSpec, Plan.Type, TypeError))
			{
				return MCPError(FString::Printf(TEXT("parameters[%d] ('%s'): %s"), Slot, *PName, *TypeError));
			}
			PlannedParams.Add(Plan);
		}
	}

	// ── nothing above this line creates anything ──
	UK2Node_CustomEvent* Event = NewObject<UK2Node_CustomEvent>(Graph);
	Event->CustomFunctionName = FName(*EventName);
	Event->bCallInEditor = bCallInEditor;
	Event->bIsEditable = true;

	uint32 FunctionFlags = FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public;
	if (NetMode == TEXT("multicast")) FunctionFlags |= FUNC_Net | FUNC_NetMulticast;
	else if (NetMode == TEXT("server")) FunctionFlags |= FUNC_Net | FUNC_NetServer;
	else if (NetMode == TEXT("client")) FunctionFlags |= FUNC_Net | FUNC_NetClient;
	if (NetMode != TEXT("none") && bReliable) FunctionFlags |= FUNC_NetReliable;
	Event->FunctionFlags = FunctionFlags;

	Event->NodePosX = OptionalInt(Params, TEXT("posX"), 0);
	Event->NodePosY = OptionalInt(Params, TEXT("posY"), 0);

	Graph->Modify();
	Graph->AddNode(Event, false, false);
	Event->CreateNewGuid();
	// AllocateDefaultPins before PostPlacedNewNode, matching the engine's own
	// spawner order and the reasoning already recorded in add_node (#627).
	Event->AllocateDefaultPins();
	Event->PostPlacedNewNode();

	TArray<TSharedPtr<FJsonValue>> AddedParams;
	for (const FPlannedParam& Plan : PlannedParams)
	{
		// An event's parameters are OUTPUT pins on the event node: the event
		// hands them to whatever it triggers.
		Event->CreateUserDefinedPin(Plan.Name, Plan.Type, EGPD_Output, /*bUseUniqueName=*/false);
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("name"), Plan.Name.ToString());
		P->SetStringField(TEXT("type"), Plan.Spec);
		AddedParams.Add(MakeShared<FJsonValueObject>(P));
	}
	Event->ReconstructNode();

	Graph->NotifyGraphChanged();
	RecompileAfterStructuralEdit(Blueprint);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetStringField(TEXT("eventName"), EventName);
	Result->SetStringField(TEXT("nodeId"), Event->NodeGuid.ToString());
	Result->SetStringField(TEXT("netMode"), NetMode);
	Result->SetBoolField(TEXT("reliable"), NetMode != TEXT("none") && bReliable);
	Result->SetBoolField(TEXT("callInEditor"), bCallInEditor);
	Result->SetArrayField(TEXT("parameters"), AddedParams);

	TArray<TSharedPtr<FJsonValue>> PinsArray;
	for (UEdGraphPin* Pin : Event->Pins)
	{
		if (!Pin) continue;
		TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
		PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
		PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
		PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
	}
	Result->SetArrayField(TEXT("pins"), PinsArray);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("graphName"), GraphName);
	Payload->SetStringField(TEXT("nodeId"), Event->NodeGuid.ToString());
	MCPSetRollback(Result, TEXT("delete_node"), Payload);
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// create_macro
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FBlueprintHandlers::CreateMacro(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPBlueprintDepth;

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString MacroName;
	if (auto Err = RequireString(Params, TEXT("macroName"), MacroName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	if (UEdGraph* Existing = FindByName(Blueprint->MacroGraphs, MacroName))
	{
		if (OnConflict == TEXT("error"))
		{
			return MCPError(FString::Printf(TEXT("Macro '%s' already exists on this Blueprint."), *MacroName));
		}
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("macroName"), MacroName);
		Noop->SetStringField(TEXT("objectPath"), Existing->GetPathName());
		return MCPResult(Noop);
	}

	EGraphKind ClashKind = EGraphKind::Unknown;
	if (FindParameterisedGraph(Blueprint, MacroName, ClashKind))
	{
		return MCPError(FString::Printf(
			TEXT("This Blueprint already has a %s named '%s'. Graph names share one namespace; pick another name."),
			GraphKindName(ClashKind), *MacroName));
	}

	// Validate every declared parameter before the graph exists, so a bad type
	// on the fourth input does not leave a half-built macro behind.
	struct FPlannedPin { FName Name; FEdGraphPinType Type; FString Spec; bool bOutput = false; };
	TArray<FPlannedPin> PlannedPins;

	auto PlanPins = [&](const TCHAR* Field, bool bOutput) -> TSharedPtr<FJsonValue>
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Params->TryGetArrayField(Field, Arr) || !Arr) return nullptr;
		for (int32 Slot = 0; Slot < Arr->Num(); ++Slot)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!(*Arr)[Slot]->TryGetObject(Obj) || !Obj || !Obj->IsValid())
			{
				return MCPError(FString::Printf(TEXT("%s[%d] is not an object. Each entry is {name, type}."), Field, Slot));
			}
			FString PName;
			if (!(*Obj)->TryGetStringField(TEXT("name"), PName) || PName.IsEmpty())
			{
				return MCPError(FString::Printf(TEXT("%s[%d] carries no 'name'."), Field, Slot));
			}
			FPlannedPin Plan;
			Plan.Name = FName(*PName);
			Plan.bOutput = bOutput;
			Plan.Spec = OptionalString(*Obj, TEXT("type"), TEXT("bool"));
			FString TypeError;
			if (!ParsePinTypeSpec(Plan.Spec, Plan.Type, TypeError))
			{
				return MCPError(FString::Printf(TEXT("%s[%d] ('%s'): %s"), Field, Slot, *PName, *TypeError));
			}
			for (const FPlannedPin& Seen : PlannedPins)
			{
				if (Seen.Name == Plan.Name && Seen.bOutput == Plan.bOutput)
				{
					return MCPError(FString::Printf(
						TEXT("%s[%d] repeats the name '%s'."), Field, Slot, *PName));
				}
			}
			PlannedPins.Add(Plan);
		}
		return nullptr;
	};
	if (auto Err = PlanPins(TEXT("inputs"), false)) return Err;
	if (auto Err = PlanPins(TEXT("outputs"), true)) return Err;

	// ── nothing above this line creates anything ──
	UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint, FName(*MacroName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	if (!Graph)
	{
		return MCPError(FString::Printf(TEXT("Failed to create macro graph '%s'."), *MacroName));
	}
	// bIsUserCreated makes the tunnel nodes editable, which is what allows the
	// inputs and outputs below to be declared at all.
	FBlueprintEditorUtils::AddMacroGraph(Blueprint, Graph, /*bIsUserCreated=*/true, /*SignatureFromClass=*/nullptr);

	UK2Node_EditablePinBase* InputOwner = FindInputOwner(Graph, EGraphKind::Macro);
	UK2Node_EditablePinBase* OutputOwner = FindOutputOwner(Graph, EGraphKind::Macro, /*bCreate=*/false);

	TArray<TSharedPtr<FJsonValue>> AddedInputs;
	TArray<TSharedPtr<FJsonValue>> AddedOutputs;
	TArray<FString> Unplaced;
	for (const FPlannedPin& Plan : PlannedPins)
	{
		UK2Node_EditablePinBase* Owner = Plan.bOutput ? OutputOwner : InputOwner;
		if (!Owner)
		{
			Unplaced.Add(Plan.Name.ToString());
			continue;
		}
		Owner->Modify();
		// The tunnel mirrors direction: the macro's inputs are the entry
		// tunnel's outputs, and its outputs are the exit tunnel's inputs.
		Owner->CreateUserDefinedPin(Plan.Name, Plan.Type, Plan.bOutput ? EGPD_Input : EGPD_Output, /*bUseUniqueName=*/false);
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("name"), Plan.Name.ToString());
		P->SetStringField(TEXT("type"), Plan.Spec);
		(Plan.bOutput ? AddedOutputs : AddedInputs).Add(MakeShared<FJsonValueObject>(P));
	}
	if (InputOwner) InputOwner->ReconstructNode();
	if (OutputOwner) OutputOwner->ReconstructNode();

	RecompileAfterStructuralEdit(Blueprint);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("macroName"), MacroName);
	Result->SetStringField(TEXT("graphName"), Graph->GetName());
	Result->SetStringField(TEXT("objectPath"), Graph->GetPathName());
	Result->SetArrayField(TEXT("inputs"), AddedInputs);
	Result->SetArrayField(TEXT("outputs"), AddedOutputs);
	if (Unplaced.Num() > 0)
	{
		// Reported rather than swallowed: the macro exists, these parameters do
		// not, and the caller needs to know which.
		Result->SetStringField(TEXT("warning"), FString::Printf(
			TEXT("The macro graph has no tunnel node for these parameters, so they were not declared: %s. Add them with edit_graph_parameters once the graph is open."),
			*FString::Join(Unplaced, TEXT(", "))));
	}

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("macroName"), MacroName);
	MCPSetRollback(Result, TEXT("delete_macro"), Payload);
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// delete_macro
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FBlueprintHandlers::DeleteMacro(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPBlueprintDepth;

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString MacroName;
	if (auto Err = RequireString(Params, TEXT("macroName"), MacroName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	UEdGraph* Graph = FindByName(Blueprint->MacroGraphs, MacroName);
	if (!Graph)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("macroName"), MacroName);
		return MCPResult(Noop);
	}

	// Record the signature so the inverse re-declares the same parameters, and
	// count the instances that are about to be orphaned so the caller learns it
	// here rather than from a compile error.
	TArray<TSharedPtr<FJsonValue>> Inputs;
	TArray<TSharedPtr<FJsonValue>> Outputs;
	auto Capture = [&](UK2Node_EditablePinBase* Owner, TArray<TSharedPtr<FJsonValue>>& Out)
	{
		if (!Owner) return;
		for (const TSharedPtr<FUserPinInfo>& Info : Owner->UserDefinedPins)
		{
			if (!Info.IsValid()) continue;
			bool bRoundTrips = true;
			TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("name"), Info->PinName.ToString());
			P->SetStringField(TEXT("type"), PinTypeSpec(Info->PinType, bRoundTrips));
			Out.Add(MakeShared<FJsonValueObject>(P));
		}
	};
	Capture(FindInputOwner(Graph, EGraphKind::Macro), Inputs);
	Capture(FindOutputOwner(Graph, EGraphKind::Macro, /*bCreate=*/false), Outputs);

	int32 InstanceCount = 0;
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);
	for (UEdGraph* Other : AllGraphs)
	{
		if (!Other || Other == Graph) continue;
		for (UEdGraphNode* Node : Other->Nodes)
		{
			UK2Node_MacroInstance* Instance = Cast<UK2Node_MacroInstance>(Node);
			if (Instance && Instance->GetMacroGraph() == Graph) ++InstanceCount;
		}
	}

	FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph);
	RecompileAfterStructuralEdit(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("macroName"), MacroName);
	Result->SetBoolField(TEXT("deleted"), true);
	Result->SetArrayField(TEXT("inputs"), Inputs);
	Result->SetArrayField(TEXT("outputs"), Outputs);
	Result->SetNumberField(TEXT("orphanedInstances"), InstanceCount);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Re-creating restores the macro's name and its input and output signature. The macro BODY is not restored, and any macro instance node that referenced it stays broken; blueprint(cleanup_graph) removes those."));

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("macroName"), MacroName);
	if (Inputs.Num() > 0)  Payload->SetArrayField(TEXT("inputs"), Inputs);
	if (Outputs.Num() > 0) Payload->SetArrayField(TEXT("outputs"), Outputs);
	MCPSetRollback(Result, TEXT("create_macro"), Payload);
	return MCPResult(Result);
}
