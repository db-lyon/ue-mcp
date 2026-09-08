// Split from BlueprintHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FBlueprintHandlers - this file
// is a translation-unit partition, not a new class. The original registers
// these handlers in BlueprintHandlers.cpp::RegisterHandlers.

#include "BlueprintHandlers.h"
#include "BlueprintHandlers_Internal.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "BlueprintEditorLibrary.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphUtilities.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionTerminator.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Tunnel.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Composite.h"
#include "K2Node_AddComponent.h"
#include "K2Node_Timeline.h"
// K2Node_AddComponent.h only forward-declares UActorComponent, and the delete
// path below names one to report which template it left registered.
#include "Components/ActorComponent.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_BaseMCDelegate.h"
#include "K2Node_ConstructObjectFromClass.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "UObject/TopLevelAssetPath.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Containers/Queue.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Kismet libraries used by K2 node construction (AddNode etc.)
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/KismetArrayLibrary.h"

// SCS component access (ResolveComponentTemplate)
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/InheritableComponentHandler.h"

namespace
{
	constexpr int32 DefaultSafeReadGraphLimit = 128;

	// #743: a hand-typed literal on an FText pin lives in DefaultTextValue, not
	// DefaultValue, so reporting DefaultValue alone made every such pin look
	// empty - indistinguishable from a genuinely unset one. Any UI string typed
	// straight into a node was invisible to the read path. Emit the text value
	// (and the object ref) alongside so a caller sees the whole default.
	void WritePinDefaults(const TSharedPtr<FJsonObject>& PinObj, const UEdGraphPin* Pin)
	{
		if (!PinObj.IsValid() || !Pin) return;

		const bool bHasText = !Pin->DefaultTextValue.IsEmpty();
		if (bHasText)
		{
			PinObj->SetStringField(TEXT("defaultTextValue"), Pin->DefaultTextValue.ToString());
		}
		// Prefer the literal that actually holds the value so callers reading
		// only defaultValue stop silently under-reporting the graph.
		PinObj->SetStringField(TEXT("defaultValue"),
			Pin->DefaultValue.IsEmpty() && bHasText
				? Pin->DefaultTextValue.ToString()
				: Pin->DefaultValue);

		if (Pin->DefaultObject)
		{
			PinObj->SetStringField(TEXT("defaultObject"), Pin->DefaultObject->GetPathName());
		}
	}
}


TSharedPtr<FJsonValue> FBlueprintHandlers::AddNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	FString NodeClass;
	if (auto Err = RequireString(Params, TEXT("nodeClass"), NodeClass)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return BlueprintNotFoundError(AssetPath);
	}

	// Find the target graph
	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);

	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	// Get optional node params
	const TSharedPtr<FJsonObject>* NodeParams = nullptr;
	Params->TryGetObjectField(TEXT("nodeParams"), NodeParams);

	// Resolve short aliases to full class names
	FString ResolvedClass = NodeClass;
	if (NodeClass == TEXT("CallFunction"))  ResolvedClass = TEXT("K2Node_CallFunction");
	// #688: "Parent: <Function>" call. Binds to the parent implementation of an
	// overridden function so an override graph can chain to the base. Uses the
	// existing K2Node_CallFunction resolution path below (SetFromFunction is
	// virtual on the parent-call subclass); with no explicit targetClass the
	// function resolves against Blueprint->ParentClass.
	else if (NodeClass == TEXT("CallParent") || NodeClass == TEXT("ParentFunction") || NodeClass == TEXT("CallParentFunction")) ResolvedClass = TEXT("K2Node_CallParentFunction");
	else if (NodeClass == TEXT("Event"))    ResolvedClass = TEXT("K2Node_Event");
	else if (NodeClass == TEXT("GetVar"))   ResolvedClass = TEXT("K2Node_VariableGet");
	else if (NodeClass == TEXT("SetVar"))   ResolvedClass = TEXT("K2Node_VariableSet");
	else if (NodeClass == TEXT("Branch"))   ResolvedClass = TEXT("K2Node_IfThenElse");
	else if (NodeClass == TEXT("CustomEvent")) ResolvedClass = TEXT("K2Node_CustomEvent");
	// #427: extra well-known node aliases. The literal K2Node_* names still
	// work, but these short forms match what the agent reaches for first.
	else if (NodeClass == TEXT("Cast") || NodeClass == TEXT("DynamicCast")) ResolvedClass = TEXT("K2Node_DynamicCast");
	else if (NodeClass == TEXT("Sequence"))     ResolvedClass = TEXT("K2Node_ExecutionSequence");
	else if (NodeClass == TEXT("ForEachLoop") || NodeClass == TEXT("ForEach")) ResolvedClass = TEXT("K2Node_CallFunction"); // resolved later via Array_ForEach
	else if (NodeClass == TEXT("Select"))       ResolvedClass = TEXT("K2Node_Select");
	else if (NodeClass == TEXT("Switch") || NodeClass == TEXT("SwitchInt")) ResolvedClass = TEXT("K2Node_SwitchInteger");
	else if (NodeClass == TEXT("SwitchEnum"))   ResolvedClass = TEXT("K2Node_SwitchEnum");
	else if (NodeClass == TEXT("SwitchString")) ResolvedClass = TEXT("K2Node_SwitchString");
	else if (NodeClass == TEXT("MakeStruct"))   ResolvedClass = TEXT("K2Node_MakeStruct");
	// #981: UMG "Create Widget". The literal K2Node_CreateWidget already
	// resolved through the UEdGraphNode lookup below; this is the short form
	// callers reach for first, matching every other alias here.
	else if (NodeClass == TEXT("CreateWidget")) ResolvedClass = TEXT("K2Node_CreateWidget");
	else if (NodeClass == TEXT("BreakStruct"))  ResolvedClass = TEXT("K2Node_BreakStruct");
	else if (NodeClass == TEXT("MakeArray"))    ResolvedClass = TEXT("K2Node_MakeArray");
	else if (NodeClass == TEXT("Return") || NodeClass == TEXT("FunctionResult")) ResolvedClass = TEXT("K2Node_FunctionResult");
	else if (NodeClass == TEXT("ComponentBoundEvent")) ResolvedClass = TEXT("K2Node_ComponentBoundEvent");
	else if (NodeClass == TEXT("InputAction") || NodeClass == TEXT("EnhancedInputAction")) ResolvedClass = TEXT("K2Node_EnhancedInputAction");

	// Find the UEdGraphNode subclass by name (works for K2, AnimGraph, and any
	// other graph node types). #823: shared resolution, restricted to graph
	// nodes so "UK2Node_CallFunction" resolves while an unrelated class that
	// happens to share a leaf name cannot win.
	UClass* NodeUClass = MCPResolveClassOfType(ResolvedClass, UEdGraphNode::StaticClass());
	if (!NodeUClass)
	{
		return MCPError(FString::Printf(TEXT("Node class not found: %s (must be a UEdGraphNode subclass)"), *NodeClass));
	}

	// Create node instance
	UEdGraphNode* NewNode = NewObject<UEdGraphNode>(TargetGraph, NodeUClass);
	if (!NewNode)
	{
		return MCPError(TEXT("Failed to create node"));
	}

	// Special-case initialization for known types (must happen BEFORE AllocateDefaultPins)
	if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(NewNode))
	{
		if (NodeParams)
		{
			FString FunctionName;
			FString TargetClassName;

			// Accept flat params: functionName, targetClass. className is also
			// accepted (#546) - agents commonly pass the owning class as
			// `className` for a custom C++ UFUNCTION, which previously bound to
			// nothing and produced an unbound stub.
			if (!(*NodeParams)->TryGetStringField(TEXT("functionName"), FunctionName))
				(*NodeParams)->TryGetStringField(TEXT("memberName"), FunctionName);
			if (!(*NodeParams)->TryGetStringField(TEXT("targetClass"), TargetClassName))
				if (!(*NodeParams)->TryGetStringField(TEXT("memberParent"), TargetClassName))
					(*NodeParams)->TryGetStringField(TEXT("className"), TargetClassName);

			// Also accept nested: {"FunctionReference":{"MemberName":"X","MemberParent":"Y"}}
			if (FunctionName.IsEmpty())
			{
				const TSharedPtr<FJsonObject>* FuncRef = nullptr;
				if ((*NodeParams)->TryGetObjectField(TEXT("FunctionReference"), FuncRef))
				{
					(*FuncRef)->TryGetStringField(TEXT("MemberName"), FunctionName);
					if (TargetClassName.IsEmpty())
						(*FuncRef)->TryGetStringField(TEXT("MemberParent"), TargetClassName);
				}
			}

			// Handle full path format: "/Script/Engine.GameplayStatics:GetGameMode"
			if (!FunctionName.IsEmpty() && FunctionName.Contains(TEXT(":")))
			{
				FString ClassPath, FuncPart;
				FunctionName.Split(TEXT(":"), &ClassPath, &FuncPart);
				FunctionName = FuncPart;
				if (TargetClassName.IsEmpty())
				{
					TargetClassName = ClassPath;
				}
			}

			if (!FunctionName.IsEmpty())
			{
				UFunction* FoundFunc = nullptr;

				// 1. Try explicit target class
				if (!TargetClassName.IsEmpty())
				{
					UClass* TargetClass = LoadObject<UClass>(nullptr, *TargetClassName);
					if (!TargetClass)
					{
						TargetClass = FindClassByShortName(TargetClassName);
					}
					if (TargetClass)
					{
						FoundFunc = TargetClass->FindFunctionByName(FName(*FunctionName));
					}
				}

				// 2. Try blueprint parent class
				if (!FoundFunc && Blueprint->ParentClass)
				{
					FoundFunc = Blueprint->ParentClass->FindFunctionByName(FName(*FunctionName));
				}

				// 3. Search common library classes
				if (!FoundFunc)
				{
					static UClass* LibraryClasses[] = {
						UGameplayStatics::StaticClass(),
						UKismetSystemLibrary::StaticClass(),
						UKismetMathLibrary::StaticClass(),
						UKismetStringLibrary::StaticClass(),
						UKismetArrayLibrary::StaticClass(),
					};
					for (UClass* Lib : LibraryClasses)
					{
						FoundFunc = Lib->FindFunctionByName(FName(*FunctionName));
						if (FoundFunc) break;
					}
				}

				// 4. #546: search the Blueprint's own component classes - a very
				// common case is calling a BlueprintCallable UFUNCTION on a custom
				// C++ component the BP owns, without naming the class explicitly.
				if (!FoundFunc && Blueprint->SimpleConstructionScript)
				{
					for (USCS_Node* SCSNode : Blueprint->SimpleConstructionScript->GetAllNodes())
					{
						if (SCSNode && SCSNode->ComponentClass)
						{
							FoundFunc = SCSNode->ComponentClass->FindFunctionByName(FName(*FunctionName));
							if (FoundFunc) break;
						}
					}
				}

				// 5. #546: last resort - scan loaded classes for a single
				// BlueprintCallable function with this exact name. Resolves
				// freshly-compiled custom C++ UFUNCTIONs that the palette index
				// has not picked up. Only binds on an unambiguous match.
				if (!FoundFunc)
				{
					UFunction* UniqueMatch = nullptr;
					int32 MatchCount = 0;
					for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
					{
						UFunction* Candidate = ClassIt->FindFunctionByName(FName(*FunctionName), EIncludeSuperFlag::ExcludeSuper);
						if (Candidate && Candidate->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
						{
							if (Candidate != UniqueMatch)
							{
								UniqueMatch = Candidate;
								if (++MatchCount > 1) break;
							}
						}
					}
					if (MatchCount == 1) FoundFunc = UniqueMatch;
				}

				if (FoundFunc)
				{
					CallNode->SetFromFunction(FoundFunc);
				}
			}
		}
	}
	else if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(NewNode))
	{
		if (NodeParams)
		{
			FString EventName;
			FString EventClassName;

			if (!(*NodeParams)->TryGetStringField(TEXT("eventName"), EventName))
				(*NodeParams)->TryGetStringField(TEXT("memberName"), EventName);
			if (!(*NodeParams)->TryGetStringField(TEXT("eventClass"), EventClassName))
				(*NodeParams)->TryGetStringField(TEXT("memberParent"), EventClassName);

			// Also accept nested: {"EventReference":{"MemberName":"X","MemberParent":"Y"}}
			if (EventName.IsEmpty())
			{
				const TSharedPtr<FJsonObject>* EvtRef = nullptr;
				if ((*NodeParams)->TryGetObjectField(TEXT("EventReference"), EvtRef))
				{
					(*EvtRef)->TryGetStringField(TEXT("MemberName"), EventName);
					if (EventClassName.IsEmpty())
						(*EvtRef)->TryGetStringField(TEXT("MemberParent"), EventClassName);
				}
			}

			if (!EventName.IsEmpty())
			{

				if (!EventClassName.IsEmpty())
				{
					// Engine event override -- bind via EventReference
					UClass* EventClass = FindClassByShortName(EventClassName);
					if (!EventClass) EventClass = Blueprint->ParentClass;

					if (EventClass)
					{
						UFunction* EventFunc = EventClass->FindFunctionByName(FName(*EventName));
						if (EventFunc)
						{
							bool bIsSelf = Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(EventClass);
							EventNode->EventReference.SetFromField<UFunction>(EventFunc, bIsSelf);
						}
					}
				}
				else
				{
					// Custom event -- just set the name
					EventNode->CustomFunctionName = FName(*EventName);
				}
			}
		}
	}
	else if (UK2Node_VariableGet* GetNode = Cast<UK2Node_VariableGet>(NewNode))
	{
		if (NodeParams)
		{
			FString VarName;
			FString OwnerClass;
			if (!(*NodeParams)->TryGetStringField(TEXT("variableName"), VarName))
			{
				// Also accept {"VariableReference":{"MemberName":"X"}} format
				const TSharedPtr<FJsonObject>* VarRef = nullptr;
				if ((*NodeParams)->TryGetObjectField(TEXT("VariableReference"), VarRef))
					(*VarRef)->TryGetStringField(TEXT("MemberName"), VarName);
			}
			(*NodeParams)->TryGetStringField(TEXT("ownerClass"), OwnerClass);

			if (!VarName.IsEmpty())
			{
				if (!OwnerClass.IsEmpty())
				{
					// #118: external class member get - typed Target input pin
					UClass* Owner = LoadClass<UObject>(nullptr, *OwnerClass);
					if (!Owner) Owner = LoadObject<UClass>(nullptr, *OwnerClass);
					if (!Owner && !OwnerClass.EndsWith(TEXT("_C")))
					{
						Owner = LoadClass<UObject>(nullptr, *(OwnerClass + TEXT("_C")));
					}
					if (!Owner) Owner = FindClassByShortName(OwnerClass);
					if (Owner)
					{
						GetNode->VariableReference.SetExternalMember(FName(*VarName), Owner);
					}
					else
					{
						GetNode->VariableReference.SetSelfMember(FName(*VarName));
					}
				}
				else
				{
					GetNode->VariableReference.SetSelfMember(FName(*VarName));
				}
			}
		}
	}
	else if (UK2Node_VariableSet* SetNode = Cast<UK2Node_VariableSet>(NewNode))
	{
		if (NodeParams)
		{
			FString VarName;
			if (!(*NodeParams)->TryGetStringField(TEXT("variableName"), VarName))
			{
				const TSharedPtr<FJsonObject>* VarRef = nullptr;
				if ((*NodeParams)->TryGetObjectField(TEXT("VariableReference"), VarRef))
					(*VarRef)->TryGetStringField(TEXT("MemberName"), VarName);
			}
			if (!VarName.IsEmpty())
			{
				SetNode->VariableReference.SetSelfMember(FName(*VarName));
			}
		}
	}
	else if (UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(NewNode))
	{
		// #101/#118: resolve BP-generated class paths as well as native classes
		if (NodeParams)
		{
			FString TargetType;
			if (!(*NodeParams)->TryGetStringField(TEXT("targetClass"), TargetType))
				(*NodeParams)->TryGetStringField(TEXT("TargetType"), TargetType);
			if (!TargetType.IsEmpty())
			{
				UClass* CastTargetClass = nullptr;
				// Try LoadClass (handles Blueprint_C paths like /Game/.../BP_Foo.BP_Foo_C)
				CastTargetClass = LoadClass<UObject>(nullptr, *TargetType);
				if (!CastTargetClass)
				{
					// Try LoadObject as UClass
					CastTargetClass = LoadObject<UClass>(nullptr, *TargetType);
				}
				if (!CastTargetClass && !TargetType.EndsWith(TEXT("_C")))
				{
					// Try appending _C for Blueprint generated classes
					FString WithSuffix = TargetType + TEXT("_C");
					CastTargetClass = LoadClass<UObject>(nullptr, *WithSuffix);
					if (!CastTargetClass) CastTargetClass = LoadObject<UClass>(nullptr, *WithSuffix);
				}
				if (!CastTargetClass)
				{
					CastTargetClass = FindClassByShortName(TargetType);
				}
				if (CastTargetClass)
				{
					CastNode->TargetType = CastTargetClass;
				}
			}
		}
	}

	// #189: K2Node_CallDelegate - bind the DelegateReference so the node resolves
	// its signature and generates correct pins for multicast delegate invocation.
	else if (UK2Node_CallDelegate* DelegateNode = Cast<UK2Node_CallDelegate>(NewNode))
	{
		if (NodeParams)
		{
			FString DelegateName;
			FString OwnerClass;

			// Accept flat params: delegateName / functionName, ownerClass / targetClass
			if (!(*NodeParams)->TryGetStringField(TEXT("delegateName"), DelegateName))
			{
				if (!(*NodeParams)->TryGetStringField(TEXT("functionName"), DelegateName))
					(*NodeParams)->TryGetStringField(TEXT("memberName"), DelegateName);
			}
			if (!(*NodeParams)->TryGetStringField(TEXT("ownerClass"), OwnerClass))
			{
				if (!(*NodeParams)->TryGetStringField(TEXT("targetClass"), OwnerClass))
					(*NodeParams)->TryGetStringField(TEXT("memberParent"), OwnerClass);
			}

			// Also accept nested: {"DelegateReference":{"MemberName":"X","MemberParent":"Y"}}
			if (DelegateName.IsEmpty())
			{
				const TSharedPtr<FJsonObject>* DelRef = nullptr;
				if ((*NodeParams)->TryGetObjectField(TEXT("DelegateReference"), DelRef))
				{
					(*DelRef)->TryGetStringField(TEXT("MemberName"), DelegateName);
					if (OwnerClass.IsEmpty())
						(*DelRef)->TryGetStringField(TEXT("MemberParent"), OwnerClass);
				}
			}

			if (!DelegateName.IsEmpty())
			{
				if (!OwnerClass.IsEmpty())
				{
					UClass* Owner = LoadObject<UClass>(nullptr, *OwnerClass);
					if (!Owner) Owner = FindClassByShortName(OwnerClass);
					if (Owner)
					{
						// Check if the property is a multicast delegate on the owner class
						FProperty* Prop = Owner->FindPropertyByName(FName(*DelegateName));
						if (Prop)
						{
							bool bIsSelf = Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(Owner);
							DelegateNode->SetFromProperty(Prop, bIsSelf, Owner);
						}
						else
						{
							// Fall back to setting the member reference directly
							bool bIsSelf = Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(Owner);
							if (bIsSelf)
								DelegateNode->DelegateReference.SetSelfMember(FName(*DelegateName));
							else
								DelegateNode->DelegateReference.SetExternalMember(FName(*DelegateName), Owner);
						}
					}
				}
				else
				{
					// Self member - delegate belongs to the Blueprint's own class
					DelegateNode->DelegateReference.SetSelfMember(FName(*DelegateName));
				}
			}
		}
	}

	// #627: bind-style multicast-delegate nodes (K2Node_AddDelegate and its child
	// K2Node_AssignDelegate, plus Remove/ClearDelegate). CallDelegate is matched by the
	// earlier else-if, so this branch only catches the bind family. Setting the
	// DelegateReference BEFORE AllocateDefaultPins makes the "Delegate" pin resolve to
	// the dispatcher's signature; combined with the corrected
	// AllocateDefaultPins-before-PostPlacedNewNode order, AssignDelegate's
	// PostPlacedNewNode then auto-creates and wires the paired Custom Event - a fully
	// bound "Bind Event to <Dispatcher>" in one add_node call. Without these params the
	// node still places (unbound) and does not crash.
	if (UK2Node_BaseMCDelegate* MCDelegateNode = Cast<UK2Node_BaseMCDelegate>(NewNode))
	{
		if (NodeParams && !MCDelegateNode->IsA<UK2Node_CallDelegate>())
		{
			FString DelegateName;
			FString OwnerClass;

			if (!(*NodeParams)->TryGetStringField(TEXT("delegateName"), DelegateName))
			{
				if (!(*NodeParams)->TryGetStringField(TEXT("functionName"), DelegateName))
					(*NodeParams)->TryGetStringField(TEXT("memberName"), DelegateName);
			}
			if (!(*NodeParams)->TryGetStringField(TEXT("ownerClass"), OwnerClass))
			{
				if (!(*NodeParams)->TryGetStringField(TEXT("targetClass"), OwnerClass))
					(*NodeParams)->TryGetStringField(TEXT("memberParent"), OwnerClass);
			}

			if (DelegateName.IsEmpty())
			{
				const TSharedPtr<FJsonObject>* DelRef = nullptr;
				if ((*NodeParams)->TryGetObjectField(TEXT("DelegateReference"), DelRef))
				{
					(*DelRef)->TryGetStringField(TEXT("MemberName"), DelegateName);
					if (OwnerClass.IsEmpty())
						(*DelRef)->TryGetStringField(TEXT("MemberParent"), OwnerClass);
				}
			}

			if (!DelegateName.IsEmpty())
			{
				if (!OwnerClass.IsEmpty())
				{
					UClass* Owner = LoadObject<UClass>(nullptr, *OwnerClass);
					if (!Owner && !OwnerClass.EndsWith(TEXT("_C")))
						Owner = LoadObject<UClass>(nullptr, *(OwnerClass + TEXT("_C")));
					if (!Owner) Owner = FindClassByShortName(OwnerClass);
					if (Owner)
					{
						FProperty* Prop = Owner->FindPropertyByName(FName(*DelegateName));
						bool bIsSelf = Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(Owner);
						if (Prop)
							MCDelegateNode->SetFromProperty(Prop, bIsSelf, Owner);
						else if (bIsSelf)
							MCDelegateNode->DelegateReference.SetSelfMember(FName(*DelegateName));
						else
							MCDelegateNode->DelegateReference.SetExternalMember(FName(*DelegateName), Owner);
					}
				}
				else
				{
					// Self member - dispatcher belongs to the Blueprint's own class
					MCDelegateNode->DelegateReference.SetSelfMember(FName(*DelegateName));
				}
			}
		}
	}

	// #443: K2Node_EnhancedInputAction.InputAction must be set before AllocateDefaultPins,
	// otherwise pins like ActionValue come out as bool instead of Vector2D and the
	// node title stays "EnhancedInputAction None". Accept inputAction (path) or
	// InputAction (nodeParams nested key).
	if (NodeParams && NewNode->GetClass()->GetName() == TEXT("K2Node_EnhancedInputAction"))
	{
		FString InputActionPath;
		if (!(*NodeParams)->TryGetStringField(TEXT("inputAction"), InputActionPath))
			(*NodeParams)->TryGetStringField(TEXT("InputAction"), InputActionPath);
		if (!InputActionPath.IsEmpty())
		{
			if (UObject* IA = StaticLoadObject(UObject::StaticClass(), nullptr, *InputActionPath))
			{
				if (FObjectProperty* Prop = CastField<FObjectProperty>(NewNode->GetClass()->FindPropertyByName(TEXT("InputAction"))))
				{
					Prop->SetObjectPropertyValue_InContainer(NewNode, IA);
				}
			}
		}
	}

	// #427: K2Node_ComponentBoundEvent identity = (componentName,
	// delegateName). Without these the node title shows "BoundEvent None"
	// and pin types don't match the delegate signature. Resolve the
	// component from the BP's SCS by name, find the multicast delegate by
	// name on the component class, and call InitializeComponentBoundEventParams.
	if (NodeParams && NewNode->GetClass()->GetName() == TEXT("K2Node_ComponentBoundEvent"))
	{
		FString ComponentName;
		FString DelegateName;
		(*NodeParams)->TryGetStringField(TEXT("componentName"), ComponentName);
		if (!(*NodeParams)->TryGetStringField(TEXT("delegateName"), DelegateName))
			(*NodeParams)->TryGetStringField(TEXT("eventName"), DelegateName);

		if (!ComponentName.IsEmpty() && !DelegateName.IsEmpty())
		{
			// Find the FObjectProperty on the BP's generated class for the component.
			FObjectProperty* CompProp = nullptr;
			if (Blueprint->SkeletonGeneratedClass)
			{
				for (TFieldIterator<FObjectProperty> It(Blueprint->SkeletonGeneratedClass); It; ++It)
				{
					if (It->GetName() == ComponentName) { CompProp = *It; break; }
				}
			}
			if (CompProp)
			{
				FMulticastDelegateProperty* DelegateProp = nullptr;
				if (UClass* CompClass = CompProp->PropertyClass)
				{
					for (TFieldIterator<FMulticastDelegateProperty> It(CompClass); It; ++It)
					{
						if (It->GetName() == DelegateName) { DelegateProp = *It; break; }
					}
				}
				if (DelegateProp)
				{
					if (auto* BoundEvent = Cast<UK2Node_ComponentBoundEvent>(NewNode))
					{
						BoundEvent->InitializeComponentBoundEventParams(CompProp, DelegateProp);
					}
				}
			}
		}
	}

	// #443: K2Node_GetSubsystem (and PC variant) need CustomClass set so pin types
	// resolve to the concrete subsystem rather than UInvalidSubsystem. Accept
	// customClass / CustomClass / subsystemClass (string class path).
	if (NodeParams && (NewNode->GetClass()->GetName() == TEXT("K2Node_GetSubsystem")
		|| NewNode->GetClass()->GetName() == TEXT("K2Node_GetSubsystemFromPC")))
	{
		FString SubsystemClass;
		if (!(*NodeParams)->TryGetStringField(TEXT("customClass"), SubsystemClass))
			if (!(*NodeParams)->TryGetStringField(TEXT("CustomClass"), SubsystemClass))
				(*NodeParams)->TryGetStringField(TEXT("subsystemClass"), SubsystemClass);
		if (!SubsystemClass.IsEmpty())
		{
			UClass* Resolved = LoadClass<UObject>(nullptr, *SubsystemClass);
			if (!Resolved) Resolved = LoadObject<UClass>(nullptr, *SubsystemClass);
			if (!Resolved) Resolved = FindClassByShortName(SubsystemClass);
			if (Resolved)
			{
				if (FClassProperty* Prop = CastField<FClassProperty>(NewNode->GetClass()->FindPropertyByName(TEXT("CustomClass"))))
				{
					Prop->SetObjectPropertyValue_InContainer(NewNode, Resolved);
				}
			}
		}
	}

	// #201/#231: K2Node_ConstructObjectFromClass-derived nodes (SpawnActorFromClass,
	// ConstructObject, AddComponent, etc.) assert in PostPlacedNewNode if the
	// owning graph has not been Modify()'d first - the assert lives in
	// EdGraphNode.h around the schema lookup that PostPlacedNewNode triggers.
	// FEdGraphSchemaAction_K2NewNode::PerformAction does this Modify; mirror it
	// here so any K2 node derived from ConstructObjectFromClass is placeable.
	TargetGraph->Modify();
	TargetGraph->AddNode(NewNode, false, false);
	NewNode->CreateNewGuid();

	// #627: AllocateDefaultPins MUST run BEFORE PostPlacedNewNode. The engine's own
	// spawner (UBlueprintNodeSpawner::SpawnEdGraphNode) allocates pins first and only
	// then calls PostPlacedNewNode. Several node types dereference their own pins inside
	// PostPlacedNewNode, so if the pins have not been allocated yet the lookup crashes
	// the editor:
	//   - K2Node_ConstructObjectFromClass (incl. K2Node_SpawnActorFromClass) reaches
	//     GetResultPin() -> FindPinChecked(PN_ReturnValue), which asserts at
	//     EdGraphNode.h:586 (check(Result) in FindPinChecked) when the result pin is absent.
	//   - K2Node_AssignDelegate dereferences GetDelegatePin()->LinkedTo, a null-deref when
	//     the "Delegate" pin has not been created yet.
	// Allocating first matches the engine order and fixes both node families. (The
	// AddDelegate base unconditionally creates the "Delegate" pin in AllocateDefaultPins,
	// so AssignDelegate is safe even when its delegate reference is unbound.)
	NewNode->AllocateDefaultPins();
	NewNode->PostPlacedNewNode();

	// #101/#118: after AllocateDefaultPins, force ReconstructNode so typed output pin
	// ("As ClassName") appears for DynamicCast and typed pins appear for VariableGet.
	// Skip ReconstructNode for ConstructObjectFromClass: at this point the Class
	// pin is unset and ReconstructNode for SpawnActor walks pin defaults that
	// expect a non-null class, asserting before AutowireNewNode would fix it.
	if (UK2Node* K2 = Cast<UK2Node>(NewNode))
	{
		if (!K2->IsA<UK2Node_ConstructObjectFromClass>())
		{
			K2->ReconstructNode();
		}
	}

	// #152: function graphs need the structural-modification signal for the
	// skeleton class to pick up new nodes. MarkBlueprintAsStructurallyModified
	// triggers that plus invalidates cached CDO info; CompileBlueprint alone
	// was leaving nodes in newly-created function graphs in a half-initialized
	// state where pins appeared but the underlying function binding didn't.
	TargetGraph->NotifyGraphChanged();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetStringField(TEXT("nodeClass"), NewNode->GetClass()->GetName());
	const FString NodeIdStr = NewNode->NodeGuid.ToString();
	Result->SetStringField(TEXT("nodeId"), NodeIdStr);
	Result->SetStringField(TEXT("title"), NewNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

	// Return pin info so the caller knows what to connect
	TArray<TSharedPtr<FJsonValue>> PinsArray;
	for (UEdGraphPin* Pin : NewNode->Pins)
	{
		if (!Pin) continue;
		TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
		PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
		PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
		PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
	}
	Result->SetArrayField(TEXT("pins"), PinsArray);

	// Rollback: delete the node we just created by guid.
	// Note: add_node has no natural key, so we cannot short-circuit on replay.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), AssetPath);
	Payload->SetStringField(TEXT("graphName"), GraphName);
	Payload->SetStringField(TEXT("nodeId"), NodeIdStr);
	MCPSetRollback(Result, TEXT("delete_node"), Payload);

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FBlueprintHandlers::ReadBlueprintGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));
	const bool bIncludePins = OptionalBool(Params, TEXT("includePins"), true);
	const bool bIncludeDefaults = OptionalBool(Params, TEXT("includeDefaults"), true);
	const bool bIncludeComments = OptionalBool(Params, TEXT("includeComments"), true);
	const bool bDumpToFile = OptionalBool(Params, TEXT("dumpToFile"), false);
	const FString OutputPath = OptionalString(Params, TEXT("outputPath"), TEXT(""));
	// #560 optional node filters (case-insensitive substring match)
	const FString TitleFilter = OptionalString(Params, TEXT("titleFilter"), TEXT(""));
	const FString ClassFilter = OptionalString(Params, TEXT("classFilter"), TEXT(""));
	const bool bHasOffset = Params->HasField(TEXT("offset"));
	const bool bHasLimit = Params->HasField(TEXT("limit"));
	const int32 RequestedOffset = FMath::Max(0, OptionalInt(Params, TEXT("offset"), 0));

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return BlueprintNotFoundError(AssetPath);
	}

	// Find the graph in UbergraphPages and FunctionGraphs
	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);

	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	// #560 build the working node list, applying title/class filters when present.
	// Pagination below operates over FilteredNodes so offset/limit stay consistent.
	const bool bFiltering = !TitleFilter.IsEmpty() || !ClassFilter.IsEmpty();
	TArray<UEdGraphNode*> FilteredNodes;
	FilteredNodes.Reserve(TargetGraph->Nodes.Num());
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (!Node) continue;
		if (!TitleFilter.IsEmpty())
		{
			const FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
			if (!NodeTitle.Contains(TitleFilter, ESearchCase::IgnoreCase)) continue;
		}
		if (!ClassFilter.IsEmpty())
		{
			if (!Node->GetClass()->GetName().Contains(ClassFilter, ESearchCase::IgnoreCase)) continue;
		}
		FilteredNodes.Add(Node);
	}

	const int32 TotalNodeCount = FilteredNodes.Num();
	int32 EffectiveLimit = OptionalInt(Params, TEXT("limit"), -1);
	if (EffectiveLimit <= 0)
	{
		EffectiveLimit = bDumpToFile
			? TotalNodeCount
			: (bHasLimit ? TotalNodeCount : FMath::Min(TotalNodeCount, DefaultSafeReadGraphLimit));
	}
	EffectiveLimit = FMath::Max(0, EffectiveLimit);

	const int32 StartIndex = FMath::Min(RequestedOffset, TotalNodeCount);
	const int32 EndIndex = FMath::Min(TotalNodeCount, StartIndex + EffectiveLimit);
	const bool bAutoPaginated = !bDumpToFile && !bHasLimit && EndIndex < TotalNodeCount;

	auto BuildGraphResult = [&](int32 SliceStart, int32 SliceEnd) -> TSharedPtr<FJsonObject>
	{
		TArray<TSharedPtr<FJsonValue>> Nodes;
		for (int32 Index = SliceStart; Index < SliceEnd; ++Index)
		{
			UEdGraphNode* Node = FilteredNodes[Index];
			if (!Node) continue;

			TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
			NodeObj->SetStringField(TEXT("id"), Node->NodeGuid.ToString());
			NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
			NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
			NodeObj->SetNumberField(TEXT("posX"), Node->NodePosX);
			NodeObj->SetNumberField(TEXT("posY"), Node->NodePosY);
			if (bIncludeComments)
			{
				NodeObj->SetStringField(TEXT("comment"), Node->NodeComment);
			}

			if (bIncludePins)
			{
				TArray<TSharedPtr<FJsonValue>> Pins;
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (!Pin) continue;
					TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
					PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
					PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
					PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
					if (bIncludeDefaults)
					{
						WritePinDefaults(PinObj, Pin);
					}
					PinObj->SetBoolField(TEXT("connected"), Pin->LinkedTo.Num() > 0);
					Pins.Add(MakeShared<FJsonValueObject>(PinObj));
				}
				NodeObj->SetArrayField(TEXT("pins"), Pins);
			}

			Nodes.Add(MakeShared<FJsonValueObject>(NodeObj));
		}

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("path"), AssetPath);
		AnnotateResolvedBlueprint(Result, Blueprint);
		Result->SetStringField(TEXT("graphName"), GraphName);
		Result->SetArrayField(TEXT("nodes"), Nodes);
		Result->SetNumberField(TEXT("nodeCount"), Nodes.Num());
		Result->SetNumberField(TEXT("totalNodeCount"), TotalNodeCount);
		if (bFiltering)
		{
			Result->SetBoolField(TEXT("filtered"), true);
			Result->SetNumberField(TEXT("unfilteredNodeCount"), TargetGraph->Nodes.Num());
			if (!TitleFilter.IsEmpty()) Result->SetStringField(TEXT("titleFilter"), TitleFilter);
			if (!ClassFilter.IsEmpty()) Result->SetStringField(TEXT("classFilter"), ClassFilter);
		}
		Result->SetNumberField(TEXT("offset"), SliceStart);
		Result->SetNumberField(TEXT("limit"), SliceEnd - SliceStart);
		Result->SetBoolField(TEXT("hasMore"), SliceEnd < TotalNodeCount);
		Result->SetNumberField(TEXT("nextOffset"), SliceEnd < TotalNodeCount ? SliceEnd : -1);
		if (bAutoPaginated)
		{
			Result->SetBoolField(TEXT("autoPaginated"), true);
		}
		if (!bIncludePins)
		{
			Result->SetBoolField(TEXT("includePins"), false);
		}
		if (!bIncludeDefaults)
		{
			Result->SetBoolField(TEXT("includeDefaults"), false);
		}
		if (!bIncludeComments)
		{
			Result->SetBoolField(TEXT("includeComments"), false);
		}
		return Result;
	};

	if (bDumpToFile)
	{
		const int32 DumpStartIndex = bHasOffset ? StartIndex : 0;
		const int32 DumpEndIndex = (bHasOffset || bHasLimit) ? EndIndex : TotalNodeCount;
		const TSharedPtr<FJsonObject> DumpResult = BuildGraphResult(DumpStartIndex, DumpEndIndex);
		FString ResolvedDumpPath;
		FString DumpError;
		if (!WriteJsonObjectToFile(DumpResult, OutputPath, AssetPath, GraphName, ResolvedDumpPath, DumpError))
		{
			return MCPError(DumpError);
		}

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("path"), AssetPath);
		AnnotateResolvedBlueprint(Result, Blueprint);
		Result->SetStringField(TEXT("graphName"), GraphName);
		Result->SetBoolField(TEXT("dumpedToFile"), true);
		Result->SetStringField(TEXT("outputPath"), ResolvedDumpPath);
		Result->SetNumberField(TEXT("nodeCount"), DumpResult->GetNumberField(TEXT("nodeCount")));
		Result->SetNumberField(TEXT("totalNodeCount"), TotalNodeCount);
		Result->SetNumberField(TEXT("offset"), DumpStartIndex);
		Result->SetNumberField(TEXT("limit"), DumpEndIndex - DumpStartIndex);
		Result->SetBoolField(TEXT("hasMore"), DumpEndIndex < TotalNodeCount);
		Result->SetNumberField(TEXT("nextOffset"), DumpEndIndex < TotalNodeCount ? DumpEndIndex : -1);
		if (bAutoPaginated)
		{
			Result->SetBoolField(TEXT("autoPaginated"), true);
		}
		return MCPResult(Result);
	}

	return MCPResult(BuildGraphResult(StartIndex, EndIndex));
}


TSharedPtr<FJsonValue> FBlueprintHandlers::ConnectPins(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	FString SourceNodeId;
	if (auto Err = RequireStringAlt(Params, TEXT("sourceNodeId"), TEXT("sourceNode"), SourceNodeId)) return Err;

	FString SourcePinName;
	if (auto Err = RequireStringAlt(Params, TEXT("sourcePinName"), TEXT("sourcePin"), SourcePinName)) return Err;

	FString TargetNodeId;
	if (auto Err = RequireStringAlt(Params, TEXT("targetNodeId"), TEXT("targetNode"), TargetNodeId)) return Err;

	FString TargetPinName;
	if (auto Err = RequireStringAlt(Params, TEXT("targetPinName"), TEXT("targetPin"), TargetPinName)) return Err;

	bool bBreakExistingSource = false;
	bool bBreakExistingTarget = false;
	Params->TryGetBoolField(TEXT("breakExistingSource"), bBreakExistingSource);
	Params->TryGetBoolField(TEXT("breakExistingTarget"), bBreakExistingTarget);

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return BlueprintNotFoundError(AssetPath);
	}

	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);
	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	// Find source node
	UEdGraphNode* SourceNode = FindNodeByGuidOrName(TargetGraph, SourceNodeId);
	if (!SourceNode)
	{
		return MCPError(FString::Printf(TEXT("Source node not found: %s"), *SourceNodeId));
	}

	// Find target node
	UEdGraphNode* TargetNode = FindNodeByGuidOrName(TargetGraph, TargetNodeId);
	if (!TargetNode)
	{
		return MCPError(FString::Printf(TEXT("Target node not found: %s"), *TargetNodeId));
	}

	// Find source pin
	UEdGraphPin* SourcePin = nullptr;
	for (UEdGraphPin* Pin : SourceNode->Pins)
	{
		if (Pin && Pin->PinName.ToString() == SourcePinName)
		{
			SourcePin = Pin;
			break;
		}
	}
	if (!SourcePin)
	{
		return MCPError(FString::Printf(TEXT("Source pin not found: '%s' on node '%s'"), *SourcePinName, *SourceNodeId));
	}

	// Find target pin
	UEdGraphPin* TargetPin = nullptr;
	for (UEdGraphPin* Pin : TargetNode->Pins)
	{
		if (Pin && Pin->PinName.ToString() == TargetPinName)
		{
			TargetPin = Pin;
			break;
		}
	}
	if (!TargetPin)
	{
		return MCPError(FString::Printf(TEXT("Target pin not found: '%s' on node '%s'"), *TargetPinName, *TargetNodeId));
	}

	// Compiling a Blueprint can reinstate pins while leaving links that identify the
	// same logical pin through a different pointer. Compare stable pin/node identity
	// as well as pointer identity so an idempotent replay does not break/recreate it.
	auto IsLogicallyLinkedTo = [](const UEdGraphPin* FromPin, const UEdGraphPin* ToPin)
	{
		if (!FromPin || !ToPin)
		{
			return false;
		}

		for (const UEdGraphPin* LinkedPin : FromPin->LinkedTo)
		{
			if (!LinkedPin)
			{
				continue;
			}
			if (LinkedPin == ToPin ||
				(LinkedPin->PinId.IsValid() && ToPin->PinId.IsValid() && LinkedPin->PinId == ToPin->PinId))
			{
				return true;
			}

			const UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
			const UEdGraphNode* ToNode = ToPin->GetOwningNode();
			if (LinkedNode && ToNode && LinkedNode->NodeGuid == ToNode->NodeGuid &&
				LinkedPin->PinName == ToPin->PinName && LinkedPin->Direction == ToPin->Direction)
			{
				return true;
			}
		}

		return false;
	};

	// Idempotency: if already linked between these two pins, short-circuit.
	if (IsLogicallyLinkedTo(SourcePin, TargetPin) || IsLogicallyLinkedTo(TargetPin, SourcePin))
	{
		auto Existed = MCPSuccess();
		MCPSetExisted(Existed);
		Existed->SetStringField(TEXT("path"), AssetPath);
		Existed->SetStringField(TEXT("graphName"), GraphName);
		Existed->SetStringField(TEXT("sourceNodeId"), SourceNodeId);
		Existed->SetStringField(TEXT("sourcePinName"), SourcePinName);
		Existed->SetStringField(TEXT("targetNodeId"), TargetNodeId);
		Existed->SetStringField(TEXT("targetPinName"), TargetPinName);
		Existed->SetNumberField(TEXT("brokenSourceLinks"), 0);
		Existed->SetNumberField(TEXT("brokenTargetLinks"), 0);
		return MCPResult(Existed);
	}

	// Use the graph's own schema (K2 for EventGraphs, AnimationGraph schema for AnimGraphs, etc.)
	const UEdGraphSchema* Schema = TargetGraph->GetSchema();
	if (!Schema)
	{
		return MCPError(TEXT("Graph has no schema"));
	}

	TArray<UEdGraphPin*> PreviousSourceLinks;
	TArray<UEdGraphPin*> PreviousTargetLinks;
	if (bBreakExistingSource)
	{
		PreviousSourceLinks = SourcePin->LinkedTo;
	}
	if (bBreakExistingTarget)
	{
		PreviousTargetLinks = TargetPin->LinkedTo;
	}
	const int32 BrokenSourceLinks = PreviousSourceLinks.Num();
	const int32 BrokenTargetLinks = PreviousTargetLinks.Num();

	// Identity of every link about to be broken, recorded while the pins are
	// still linked. There is no action that breaks a pin link, so the only undo
	// is re-making these with connect_pins; a result that said "re-connect them"
	// while reporting nothing but a COUNT prescribed something the caller had no
	// way to do.
	auto DescribeLinks = [](const TArray<UEdGraphPin*>& Links)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const UEdGraphPin* LinkedPin : Links)
		{
			if (!LinkedPin) continue;
			const UEdGraphNode* Owner = LinkedPin->GetOwningNode();
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("nodeId"), Owner ? Owner->NodeGuid.ToString() : FString());
			O->SetStringField(TEXT("pinName"), LinkedPin->PinName.ToString());
			O->SetStringField(TEXT("direction"),
				LinkedPin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
			Out.Add(MakeShared<FJsonValueObject>(O));
		}
		return Out;
	};
	const TArray<TSharedPtr<FJsonValue>> BrokenSourceDetail = DescribeLinks(PreviousSourceLinks);
	const TArray<TSharedPtr<FJsonValue>> BrokenTargetDetail = DescribeLinks(PreviousTargetLinks);
	UPackage* Package = Blueprint->GetOutermost();
	const bool bWasPackageDirty = Package && Package->IsDirty();

	if (bBreakExistingSource)
	{
		SourcePin->BreakAllPinLinks();
	}
	if (bBreakExistingTarget)
	{
		TargetPin->BreakAllPinLinks();
	}

	bool bConnected = Schema->TryCreateConnection(SourcePin, TargetPin);

	if (bConnected)
	{
		for (UEdGraphPin* PreviousPin : PreviousSourceLinks)
		{
			if (PreviousPin && PreviousPin != TargetPin && PreviousPin->GetOwningNode())
			{
				PreviousPin->GetOwningNode()->PinConnectionListChanged(PreviousPin);
			}
		}
		for (UEdGraphPin* PreviousPin : PreviousTargetLinks)
		{
			if (PreviousPin && PreviousPin != SourcePin && PreviousPin->GetOwningNode())
			{
				PreviousPin->GetOwningNode()->PinConnectionListChanged(PreviousPin);
			}
		}

		// Compile and save
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		SaveAssetPackage(Blueprint);

		auto Result = MCPSuccess();
		MCPSetCreated(Result);
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("graphName"), GraphName);
		Result->SetStringField(TEXT("sourceNodeId"), SourceNodeId);
		Result->SetStringField(TEXT("sourcePinName"), SourcePinName);
		Result->SetStringField(TEXT("targetNodeId"), TargetNodeId);
		Result->SetStringField(TEXT("targetPinName"), TargetPinName);
		Result->SetNumberField(TEXT("brokenSourceLinks"), BrokenSourceLinks);
		Result->SetNumberField(TEXT("brokenTargetLinks"), BrokenTargetLinks);
		Result->SetArrayField(TEXT("brokenSourceLinkDetail"), BrokenSourceDetail);
		Result->SetArrayField(TEXT("brokenTargetLinkDetail"), BrokenTargetDetail);
		// Undoing a wire means breaking it, and the Blueprint surface has no
		// action that breaks a pin link: delete_node removes whole nodes, which
		// would destroy work this call never touched. Nothing is invented here.
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), (BrokenSourceLinks + BrokenTargetLinks) > 0
			? FString::Printf(TEXT(
				"No action breaks a Blueprint pin link, so this connection has no inverse call. delete_node is not "
				"it: it would remove a whole node this call only wired up. The %d link(s) that breakExistingSource "
				"or breakExistingTarget severed are named in brokenSourceLinkDetail and brokenTargetLinkDetail, "
				"each with its nodeId, pinName and direction; feed those back through connect_pins to re-make them."),
				BrokenSourceLinks + BrokenTargetLinks)
			: FString(TEXT(
				"No action breaks a Blueprint pin link, so this connection has no inverse call. delete_node is not "
				"it: it would remove a whole node this call only wired up. This call broke no existing links, so "
				"the only thing to undo is the one wire it made.")));
		return MCPResult(Result);
	}
	else
	{
		// Capture the failure before restoring the exact previous graph state.
		FString ErrorMsg = TEXT("TryCreateConnection failed. Pins may be incompatible.");
		FPinConnectionResponse Response = Schema->CanCreateConnection(SourcePin, TargetPin);
		if (!Response.Message.IsEmpty())
		{
			ErrorMsg = FString::Printf(TEXT("Connection failed: %s"), *Response.Message.ToString());
		}

		for (UEdGraphPin* PreviousPin : PreviousSourceLinks)
		{
			if (PreviousPin && !SourcePin->LinkedTo.Contains(PreviousPin))
			{
				SourcePin->MakeLinkTo(PreviousPin);
			}
		}
		for (UEdGraphPin* PreviousPin : PreviousTargetLinks)
		{
			if (PreviousPin && !TargetPin->LinkedTo.Contains(PreviousPin))
			{
				TargetPin->MakeLinkTo(PreviousPin);
			}
		}
		if (Package && !bWasPackageDirty)
		{
			Package->SetDirtyFlag(false);
		}
		return MCPError(ErrorMsg);
	}
}


TSharedPtr<FJsonValue> FBlueprintHandlers::DeleteNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	FString NodeId;
	if (auto Err = RequireStringAlt(Params, TEXT("nodeId"), TEXT("nodeName"), NodeId)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return BlueprintNotFoundError(AssetPath);
	}

	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);
	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	UEdGraphNode* NodeToDelete = FindNodeByGuidOrName(TargetGraph, NodeId);
	if (!NodeToDelete)
	{
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("graphName"), GraphName);
		Noop->SetStringField(TEXT("nodeId"), NodeId);
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Noop);
	}

	const FString NodeClassName = NodeToDelete->GetClass()->GetName();

	// Refuse what the engine itself refuses. CanUserDeleteNode is the per-node
	// authority the editor's own delete consults, and it is context sensitive in
	// a way no class list here could be: a function entry always returns false,
	// a function result returns false only while its signature is not editable,
	// a tunnel returns false inside a tunnel graph but true for a stray one at
	// top level, and macro instances and composites return true. The AnimGraph
	// singletons answer it too - AnimGraphNode_Root, AnimGraphNode_StateResult,
	// AnimGraphNode_TransitionResult and AnimStateEntryNode all return false -
	// and delete_node reaches those graphs, because FindGraph searches every
	// graph the Blueprint owns. Destroying an output pose node or a state
	// machine entry and answering success:true with a note about there being no
	// way to undo it was the wrong question answered politely: the call should
	// not have run.
	if (!NodeToDelete->CanUserDeleteNode())
	{
		return MCPError(FString::Printf(TEXT(
			"Node '%s' (%s) in graph '%s' cannot be deleted: the engine marks it CanUserDeleteNode=false, which is "
			"what a function entry, a locked function return, a tunnel inside its own tunnel graph, an AnimGraph "
			"output pose or a state machine entry node reports. The editor refuses this delete too, and there is no "
			"way to put the node back afterwards, so it is refused here rather than performed and then reported as "
			"unrecoverable. Delete the owning function, macro or state instead if that is what you meant."),
			*NodeId, *NodeClassName, *GraphName));
	}

	// Everything below runs only on a node the engine agreed may be deleted.
	// PrepareForCopying is the editor's own pre-Copy step and the editor always
	// pairs it with PostCopyNode. Here the node is destroyed a few lines later
	// instead, which reaches the same end state, but ONLY because the node is
	// destroyed. So it is called after the CanUserDeleteNode guard, never
	// before: a refused node survives the call and would be left carrying
	// whatever PrepareForCopying did to it with no PostCopyNode to undo it. The
	// base implementation is empty, but it is a real override on several
	// classes (UK2Node_Timeline, UK2Node_AddComponent and
	// UAnimStateTransitionNode among them), and CanUserDeleteNode is a per-node
	// answer any instance can give, so the ordering is what makes the pairing
	// safe rather than any class list.
	//
	// Nothing after the delete can read the node back, so the text the inverse
	// would paste is captured while the node still exists.
	//
	// The EXPORT gate is CanDuplicateNode(), and only that. It says nothing
	// about whether the delete is allowed - CanUserDeleteNode above is the sole
	// authority on that - it decides whether there is any point capturing
	// rollback text, and it is the same question the paste side asks:
	// FGraphObjectTextFactory::CanCreateClass reads CanDuplicateNode off the
	// class default object and skips the whole block when it answers false
	// (EdGraphUtilities.cpp:125-150). Text no importer would accept is text
	// worth not capturing.
	//
	// There is no additional exclusion by node class, because the round trip
	// was traced rather than assumed. FEdGraphUtilities::ExportNodesToText
	// builds an FExportObjectInnerContext and hands it to
	// UExporter::ExportToOutputDevice (EdGraphUtilities.cpp:458-481); the
	// context maps every object to its inners (UnrealExporter.cpp:515-522) and
	// UObjectExporterT3D::ExportText calls ExportObjectInner
	// (EditorExporters.cpp:331-338), which emits each inner as its own nested
	// Begin Object block (UnrealExporter.cpp:571-623). Coming back,
	// FCustomizableTextObjectFactory::ProcessBuffer captures those nested
	// blocks as the node's property text and ImportObjectProperties recreates
	// them as subobjects of the new node (EditorFactories.cpp:5347-5383,
	// EditorObject.cpp:449 and 720-800). So a subgraph a node OWNS travels with
	// it, and one it only references is left alone by the delete and still
	// resolves on paste. What the paste does not restore is named in the
	// rollback note instead of being used to withhold the rollback.
	FString DeletedT3D;
	int32 DeletedLinkCount = 0;
	const bool bIsComposite = NodeToDelete->IsA<UK2Node_Composite>();
	const bool bIsMacroInstance = NodeToDelete->IsA<UK2Node_MacroInstance>();
	const bool bIsEventNode = NodeToDelete->IsA<UK2Node_Event>();
	const bool bIsAddComponent = NodeToDelete->IsA<UK2Node_AddComponent>();
	const bool bIsTimeline = NodeToDelete->IsA<UK2Node_Timeline>();

	// Read off the node while it still exists, because the rollback note built
	// after the removal names both and nothing down there can reach the node
	// again. Each has a stand-in for the case where there is nothing to name,
	// so neither ever prints an empty pair of quotes.
	FString BoundGraphName;
	if (const UK2Node_Composite* AsComposite = Cast<UK2Node_Composite>(NodeToDelete))
	{
		if (AsComposite->BoundGraph) BoundGraphName = AsComposite->BoundGraph->GetName();
	}
	FString AddComponentTemplateName(TEXT("(none)"));
	if (const UK2Node_AddComponent* AsAddComponent = Cast<UK2Node_AddComponent>(NodeToDelete))
	{
		if (const UActorComponent* Template = AsAddComponent->GetTemplateFromNode())
		{
			AddComponentTemplateName = Template->GetName();
		}
	}
	for (const UEdGraphPin* Pin : NodeToDelete->Pins)
	{
		if (Pin) DeletedLinkCount += Pin->LinkedTo.Num();
	}
	if (NodeToDelete->CanDuplicateNode())
	{
		NodeToDelete->PrepareForCopying();
		TSet<UObject*> NodeSet;
		NodeSet.Add(NodeToDelete);
		FEdGraphUtilities::ExportNodesToText(NodeSet, DeletedT3D);
	}

	// Two removal paths, and the question that picks between them is what the
	// node's DestroyNode override does that the T3D just captured cannot carry
	// back. Engine bodies below were read in a 5.7.4 source tree; every
	// declaration they rest on is present unchanged in the 5.8 headers this
	// plugin builds against.
	//
	// FBlueprintEditorUtils::RemoveNode is the editor's own node deletion
	// (BlueprintEditorUtils.cpp:2751-2790). It clears breakpoints and pin
	// watches, breaks the links through the schema, and then calls
	// DestroyNode. UEdGraph::RemoveNode (EdGraph.cpp:260-279) drops the node
	// out of the Nodes array and breaks its links and stops. The two are not
	// far apart, because UEdGraphNode::DestroyNode's base implementation IS
	// ParentGraph->RemoveNode(this) (EdGraphNode.cpp:554-561): the whole
	// difference is the derived override.
	//
	// Most overrides only release what the node OWNS or unpick references to
	// it, and the delete is wrong without them. UK2Node_Composite::DestroyNode
	// hands BoundGraph to FBlueprintEditorUtils::RemoveGraph
	// (K2Node_Composite.cpp:73-84); skip it and the collapsed graph outlives
	// the node that owned it and stays in the parent graph's SubGraphs with
	// nothing pointing at it. The AnimGraph state, conduit, transition,
	// state-machine and blend-space nodes do the same for their bound graphs,
	// UK2Node_ActorBoundEvent unbinds the level delegate it registered, and
	// UK2Node_Tunnel clears its twin's back-pointer. None of that costs the
	// rollback the subgraph: every one of those graphs is created with the
	// NODE as its outer - CreateNewGraph(this, ...) in AnimStateNode.cpp:134,
	// AnimStateConduitNode.cpp:73, AnimStateTransitionNode.cpp:675 and 698,
	// AnimGraphNode_StateMachineBase.cpp:147 and
	// AnimGraphNode_BlendSpaceGraphBase.cpp:223, 265 and 437 - so it is an
	// inner, the export already carried it, and the paste rebuilds it.
	//
	// Exactly two overrides do something else. They unregister a Blueprint
	// LEVEL archetype that is outered to the generated class rather than to
	// the node, so it is not an inner and the T3D above never carried it:
	//
	//   UK2Node_AddComponent::DestroyNode does
	//   BlueprintObj->ComponentTemplates.Remove(Template)
	//   (K2Node_AddComponent.cpp:304-322). PrepareForCopying writes only
	//   TemplateBlueprint (:324-327), so the export carries the template's
	//   NAME and nothing else. On the way back MakeNewComponentTemplate
	//   (:560-611) resolves that name through Blueprint->FindTemplateByName,
	//   which reads the very array the removal emptied, and its
	//   TemplateBlueprint fallback resolves to the same Blueprint and fails
	//   the same way. Worse, the compile below only preserves what is in that
	//   array: FKismetCompilerContext::SaveSubObjectsFromCleanAndSanitizeClass
	//   seeds its keep-list from Blueprint->ComponentTemplates and
	//   Blueprint->Timelines (KismetCompiler.cpp:755-758), so an unregistered
	//   template is sanitized away before any rollback can run. Deleting a
	//   configured Add Component node down this path and replaying the
	//   rollback yields a node with a blank default template.
	//
	//   UK2Node_Timeline::DestroyNode calls RemoveTimeline and then renames
	//   the template into the transient package (K2Node_Timeline.cpp:204-218).
	//   RemoveTimeline drops it from Blueprint->Timelines and calls
	//   MarkAsGarbage on it (BlueprintEditorUtils.cpp:7977-7990). PostPasteNode
	//   recovers the curve tracks only by finding a live UTimelineTemplate with
	//   a matching TimelineGuid through TObjectIterator (:220-238), and a
	//   garbage-marked object survives that search only until the next
	//   collection.
	//
	// So those two keep the plain graph removal, which is what this handler
	// did for every class before the composite fix and which leaves the
	// archetype registered for the rollback to find. Two costs, both stated
	// rather than hidden: the template stays in the Blueprint after the delete,
	// which the rollback note below says outright, and a breakpoint or pin
	// watch on the node is not cleared, because that cleanup lives in
	// FBlueprintEditorUtils::RemoveNode alongside the DestroyNode call this
	// branch is avoiding. A stale breakpoint keeps the removed node alive and
	// displays nowhere; losing a configured component template is worse.
	// Neither class owns a subgraph -
	// UK2Node_Composite is the only override of UEdGraphNode::GetSubGraphs in
	// BlueprintGraph (K2Node_Composite.h:50) - so the orphaned-subgraph bug
	// cannot reach them.
	//
	// bDontRecompile is true because the compile immediately below is this
	// handler's own. It is not a promise that nothing recompiles: a composite
	// reaches RemoveGraph with EGraphRemoveFlags::Recompile, and that calls
	// MarkBlueprintAsStructurallyModified on its own way out
	// (BlueprintEditorUtils.cpp:2575-2578).
	const bool bDestroyUnregistersArchetype = bIsAddComponent || bIsTimeline;
	if (bDestroyUnregistersArchetype)
	{
		NodeToDelete->BreakAllNodeLinks();
		TargetGraph->RemoveNode(NodeToDelete);
	}
	else
	{
		FBlueprintEditorUtils::RemoveNode(Blueprint, NodeToDelete, /*bDontRecompile*/ true);
	}

	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetStringField(TEXT("nodeId"), NodeId);
	Result->SetBoolField(TEXT("deleted"), true);

	// The inverse is import_nodes_t3d fed the node's own exported text. It puts
	// an equivalent node back in the same graph, but either removal path above
	// severed every wire and the paste mints a fresh GUID, so this is lossy in
	// at least two named ways rather than a clean undo.
	if (!DeletedT3D.IsEmpty())
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("path"), AssetPath);
		Payload->SetStringField(TEXT("graphName"), GraphName);
		Payload->SetStringField(TEXT("t3d"), DeletedT3D);
		MCPSetRollback(Result, TEXT("import_nodes_t3d"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);

		// Everything appended here is a property of the class that was deleted,
		// and each clause is a behaviour that was read out of the engine rather
		// than inferred from the node's name.
		FString Note = FString::Printf(TEXT(
			"import_nodes_t3d pastes the node back carrying the properties the export captured, but the %d pin "
			"link(s) this delete broke do NOT come back and have to be re-made with connect_pins, and the pasted "
			"node gets a new nodeId, so '%s' will not address it afterwards."),
			DeletedLinkCount, *NodeId);
		if (bIsComposite)
		{
			Note += TEXT(
				" The collapsed graph comes back with the node, under a DIFFERENT NAME. A composite's BoundGraph is "
				"created with the node itself as its outer (UK2Node_Composite::PostPlacedNewNode), so the export "
				"carries the subgraph and its contents inline rather than a reference to them, and "
				"UK2Node_Composite::PostPasteNode rewires the entry and exit tunnels of the graph the paste rebuilt. "
				"That same PostPasteNode ends by calling RenameBoundGraphCloseToName, which starts its counter at 2 "
				"and always formats '<name>_<n>' without ever trying the bare name.");
			Note += BoundGraphName.IsEmpty()
				? FString(TEXT(
					" So the restored subgraph is addressed by a suffixed name, not the one it had. Read it back with "
					"list_graphs before addressing it by graphName."))
				: FString::Printf(TEXT(
					" So the subgraph named '%s' comes back as '%s_2', or the next free index if that is taken. "
					"Address it by that name afterwards, not by the old one."),
					*BoundGraphName, *BoundGraphName);
		}
		if (bIsAddComponent)
		{
			Note += FString::Printf(TEXT(
				" This node's component template survives the delete on purpose. The template is outered to the "
				"generated class rather than to the node, so it is not carried by the exported text, and "
				"UK2Node_AddComponent::PostPasteNode resolves it by NAME through Blueprint->FindTemplateByName. The "
				"delete therefore leaves '%s' registered in the Blueprint's ComponentTemplates so the paste can copy "
				"its properties onto the new template it mints. The cost is an orphan either way: until you roll "
				"back, the Blueprint holds a component template no node references, and after a rollback that "
				"original stays alongside the copy the paste created."),
				*AddComponentTemplateName);
		}
		if (bIsTimeline)
		{
			Note += TEXT(
				" This node's timeline template survives the delete on purpose, for the same reason: the template is "
				"outered to the generated class, the exported text carries only the node's TimelineGuid, and "
				"UK2Node_Timeline::PostPasteNode recovers the curve tracks by finding a live UTimelineTemplate with "
				"that guid. Destroying the node would have dropped the template from Blueprint->Timelines and marked "
				"it garbage, which makes the curve tracks unrecoverable at the next garbage collection - and the "
				"compile this delete runs can be the thing that triggers it. What the paste does NOT restore is the "
				"timeline's NAME: PostPasteNode assigns FindUniqueTimelineName unconditionally, and that returns "
				"'Timeline' or 'Timeline_<n>' regardless of what the timeline was called, so the timeline variable "
				"comes back renamed and anything referring to it by the old name has to be repointed. The original "
				"template also stays in the Blueprint alongside the copy the paste duplicates from.");
		}
		if (bIsMacroInstance)
		{
			Note += TEXT(
				" The macro definition itself was never touched: a macro instance holds only an "
				"FGraphReference to a graph that lives in the macro library or in this Blueprint's own macro "
				"graphs, so deleting the instance leaves the definition in place and the exported reference still "
				"resolves when the node is pasted back.");
		}
		if (bIsEventNode)
		{
			Note += TEXT(
				" One case is not a clean restore: if an event of the same name exists again by the time the paste "
				"runs, UK2Node_Event::CanPasteHere refuses and the paste substitutes a Custom Event in its place "
				"(UEdGraphSchema_K2::CreateSubstituteNode), which overrides nothing. Paste back into the state the "
				"delete left, not after re-adding the event.");
		}
		Result->SetStringField(TEXT("rollbackNote"), Note);
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(TEXT(
			"Node '%s' (%s) reported itself non-duplicatable through CanDuplicateNode, or exported to empty "
			"text, so there is nothing for import_nodes_t3d to paste back and no inverse is offered. The paste side "
			"reads the same CanDuplicateNode and would refuse the block, so exporting it would have produced a "
			"rollback that does nothing. Rebuild the node with add_node."),
			*NodeId, *NodeClassName));
	}
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FBlueprintHandlers::SetNodeProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	FString NodeId;
	if (auto Err = RequireStringAlt(Params, TEXT("nodeId"), TEXT("nodeName"), NodeId)) return Err;

	FString PinName;
	if (auto Err = RequireStringAlt(Params, TEXT("pinName"), TEXT("propertyName"), PinName)) return Err;

	FString DefaultValue;
	if (auto Err = RequireStringAlt(Params, TEXT("defaultValue"), TEXT("value"), DefaultValue)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return BlueprintNotFoundError(AssetPath);
	}

	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);
	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	// Find the node
	UEdGraphNode* TargetNode = FindNodeByGuidOrName(TargetGraph, NodeId);
	if (!TargetNode)
	{
		return MCPError(FString::Printf(TEXT("Node not found: %s"), *NodeId));
	}

	// First try to find a pin with this name
	UEdGraphPin* TargetPin = nullptr;
	for (UEdGraphPin* Pin : TargetNode->Pins)
	{
		if (Pin && Pin->PinName.ToString() == PinName)
		{
			TargetPin = Pin;
			break;
		}
	}

	bool bSetViaPin = false;
	bool bSetViaProperty = false;
	FString PrevValue;
	bool bHasPrevValue = false;

	// #743: FText pins keep their literal in DefaultTextValue. Reading and
	// writing DefaultValue for them compared an always-empty string, so the
	// no-op check never fired and rollback captured the wrong previous value.
	const bool bIsTextPin = TargetPin
		&& TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Text;

	if (TargetPin)
	{
		// Capture previous pin default for rollback
		PrevValue = bIsTextPin ? TargetPin->DefaultTextValue.ToString() : TargetPin->DefaultValue;
		bHasPrevValue = true;
		// No-op short-circuit: pin default already matches
		if (PrevValue == DefaultValue)
		{
			auto Noop = MCPSuccess();
			MCPSetExisted(Noop);
			Noop->SetStringField(TEXT("path"), AssetPath);
			Noop->SetStringField(TEXT("graphName"), GraphName);
			Noop->SetStringField(TEXT("nodeId"), NodeId);
			Noop->SetStringField(TEXT("propertyName"), PinName);
			Noop->SetStringField(TEXT("value"), DefaultValue);
			return MCPResult(Noop);
		}
		// Set pin default value using the graph's own schema
		const UEdGraphSchema* Schema = TargetGraph->GetSchema();
		if (Schema)
		{
			if (bIsTextPin)
			{
				Schema->TrySetDefaultText(*TargetPin, FText::FromString(DefaultValue));
			}
			else
			{
				Schema->TrySetDefaultValue(*TargetPin, DefaultValue);
			}
			TargetNode->PinDefaultValueChanged(TargetPin);
			bSetViaPin = true;
		}
	}

	if (!bSetViaPin)
	{
		// No pin found -- try setting as a node property via reflection.
		// Supports dotted paths like "Node.IKBone.BoneName" for AnimGraph inner structs.
		TArray<FString> PathParts;
		PinName.ParseIntoArray(PathParts, TEXT("."));

		UStruct* CurrentStruct = TargetNode->GetClass();
		void* CurrentContainer = TargetNode;
		FProperty* FinalProp = nullptr;

		for (int32 i = 0; i < PathParts.Num(); i++)
		{
			FProperty* Prop = CurrentStruct->FindPropertyByName(FName(*PathParts[i]));
			if (!Prop) break;

			if (i < PathParts.Num() - 1)
			{
				// Intermediate path segment -- drill into struct
				FStructProperty* StructProp = CastField<FStructProperty>(Prop);
				if (!StructProp) break;
				CurrentContainer = StructProp->ContainerPtrToValuePtr<void>(CurrentContainer);
				CurrentStruct = StructProp->Struct;
			}
			else
			{
				FinalProp = Prop;
			}
		}

		if (FinalProp)
		{
			void* ValuePtr = FinalProp->ContainerPtrToValuePtr<void>(CurrentContainer);
			// ImportText_Direct returns nullptr on parse failure. Previously we
			// ignored that - a malformed DefaultValue silently corrupted the
			// node's default. Now we surface the failure as an error so callers
			// see the bad input immediately rather than at compile time.
			const TCHAR* Parsed = FinalProp->ImportText_Direct(*DefaultValue, ValuePtr, nullptr, PPF_None);
			if (Parsed == nullptr)
			{
				return MCPError(FString::Printf(
					TEXT("DefaultValue '%s' is not valid for property '%s' (type %s). Use UE's text format (e.g. `(X=1,Y=2,Z=3)` for FVector)."),
					*DefaultValue, *FinalProp->GetName(), *FinalProp->GetCPPType()));
			}
			TargetNode->PostEditChange();
			// #325: writing pin-defining properties on a K2Node (e.g. VariableReference
			// on K2Node_VariableGet, FunctionReference on K2Node_CallFunction) leaves
			// the node's pins frozen at the old class until ReconstructNode is called.
			// PostEditChange alone does not rebuild pins. Run reconstruct so any
			// subsequent connect_pins call sees the up-to-date pin types.
			if (UK2Node* AsK2 = Cast<UK2Node>(TargetNode))
			{
				AsK2->ReconstructNode();
			}
			bSetViaProperty = true;
		}
	}

	if (!bSetViaPin && !bSetViaProperty)
	{
		// Neither pin nor property found -- build a helpful error
		TArray<FString> PinNames;
		for (UEdGraphPin* Pin : TargetNode->Pins)
		{
			if (Pin) PinNames.Add(Pin->PinName.ToString());
		}

		TArray<FString> PropNames;
		for (TFieldIterator<FProperty> It(TargetNode->GetClass()); It; ++It)
		{
			PropNames.Add(It->GetName());
		}

		return MCPError(FString::Printf(
			TEXT("'%s' not found as pin or property. Pins: [%s]. Properties: [%s]"),
			*PinName, *FString::Join(PinNames, TEXT(", ")), *FString::Join(PropNames, TEXT(", "))));
	}

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetStringField(TEXT("nodeId"), NodeId);
	Result->SetStringField(TEXT("propertyName"), PinName);
	Result->SetStringField(TEXT("value"), DefaultValue);
	Result->SetStringField(TEXT("setVia"), bSetViaPin ? TEXT("pin") : TEXT("property"));

	// Rollback: self-inverse with previous pin default value (pin path only; property path has no reliable previous capture)
	if (bSetViaPin && bHasPrevValue)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("path"), AssetPath);
		Payload->SetStringField(TEXT("graphName"), GraphName);
		Payload->SetStringField(TEXT("nodeId"), NodeId);
		Payload->SetStringField(TEXT("pinName"), PinName);
		Payload->SetStringField(TEXT("defaultValue"), PrevValue);
		MCPSetRollback(Result, TEXT("set_node_property"), Payload);
	}
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// Resolve a component template on a Blueprint by name.
//
// Child Blueprints do NOT own inherited components in their own SCS - the
// component templates live on the parent Blueprint's SCS. Writing through
// the parent's template corrupts the parent for every descendant. For
// inherited components we must route writes through the child's
// UInheritableComponentHandler, which stores per-child override templates
// (equivalent of SubobjectDataBlueprintFunctionLibrary.get_object_for_blueprint
// in Python, as opposed to the read-only shared get_object).
//
// bForWrite=true: always returns a write-safe template. For inherited
//   components this means the child's ICH override (creating one if none
//   exists yet). Never returns the shared parent template for a write.
//
// bForWrite=false: returns the child's ICH override if one exists
//   (so reads reflect what writes would mutate), otherwise falls back to
//   the parent template (which holds the effective default).
//
// OutAvailable is populated with candidate component names (own SCS +
// inherited + CDO) for error messages when the lookup fails.
UActorComponent* ResolveComponentTemplate(
	UBlueprint* Blueprint,
	const FString& ComponentName,
	bool bForWrite,
	bool& bOutIsInherited,
	TArray<FString>& OutAvailable)
{
	bOutIsInherited = false;
	OutAvailable.Reset();
	if (!Blueprint) return nullptr;

	// 1) Own SCS - child's own components, write directly.
	if (USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			if (!Node || !Node->ComponentTemplate) continue;
			OutAvailable.AddUnique(Node->GetVariableName().ToString());
			if (Node->GetVariableName().ToString() == ComponentName ||
				Node->ComponentTemplate->GetName() == ComponentName)
			{
				return Node->ComponentTemplate;
			}
		}
	}

	// 2) Walk parent BP chain for inherited SCS components.
	USCS_Node* InheritedNode = nullptr;
	UClass* ParentClass = Blueprint->ParentClass;
	while (ParentClass && !InheritedNode)
	{
		if (UBlueprint* ParentBP = Cast<UBlueprint>(ParentClass->ClassGeneratedBy))
		{
			if (USimpleConstructionScript* ParentSCS = ParentBP->SimpleConstructionScript)
			{
				for (USCS_Node* Node : ParentSCS->GetAllNodes())
				{
					if (!Node || !Node->ComponentTemplate) continue;
					OutAvailable.AddUnique(Node->GetVariableName().ToString());
					if (Node->GetVariableName().ToString() == ComponentName ||
						Node->ComponentTemplate->GetName() == ComponentName)
					{
						InheritedNode = Node;
						break;
					}
				}
			}
			ParentClass = ParentBP->ParentClass;
		}
		else
		{
			break;
		}
	}

	if (InheritedNode)
	{
		bOutIsInherited = true;
		FComponentKey Key(InheritedNode);

		// Look up any existing override on this specific child BP.
		UInheritableComponentHandler* ICH =
			Blueprint->GetInheritableComponentHandler(/*bCreateIfNecessary=*/bForWrite);
		UActorComponent* Override = ICH ? ICH->GetOverridenComponentTemplate(Key) : nullptr;

		if (bForWrite)
		{
			// Must never write through the shared parent template - that
			// would mutate the parent and every other descendant.
			if (!Override && ICH)
			{
				Override = ICH->CreateOverridenComponentTemplate(Key);
			}
			return Override; // null only if ICH creation failed
		}
		else
		{
			return Override ? Override : ToRawPtr(InheritedNode->ComponentTemplate);
		}
	}

	// 3) CDO fallback - catches native C++ components and anything the
	// SCS walk missed. The CDO component pointer is the right write target
	// for default-value overrides on inherited native components: mutating
	// it lands on the Blueprint's GeneratedClass CDO, which is what spawns
	// new instances at runtime. Names match by component instance name,
	// instance prefix, or FObjectProperty variable name (#211: declarations
	// like UPROPERTY() UCharacterMovementComponent* CharacterMovement; expose
	// the property name "CharacterMovement", which is what callers reach
	// for, not the construct-time instance name "CharMoveComp").
	if (Blueprint->GeneratedClass)
	{
		if (AActor* ActorCDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject(false)))
		{
			TInlineComponentArray<UActorComponent*> Components;
			ActorCDO->GetComponents(Components);
			for (UActorComponent* C : Components)
			{
				if (!C) continue;
				OutAvailable.AddUnique(C->GetName());
				if (C->GetName() == ComponentName ||
					C->GetName().StartsWith(ComponentName + TEXT("_")) ||
					C->GetFName().ToString() == ComponentName)
				{
					return C;
				}
			}
			// FObjectProperty lookup: walk class properties for an
			// FObjectProperty named ComponentName whose value points at a
			// component on the CDO.
			if (FObjectProperty* OP = CastField<FObjectProperty>(ActorCDO->GetClass()->FindPropertyByName(FName(*ComponentName))))
			{
				if (OP->PropertyClass && OP->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
				{
					if (UObject* Obj = OP->GetObjectPropertyValue_InContainer(ActorCDO))
					{
						if (UActorComponent* AC = Cast<UActorComponent>(Obj))
						{
							OutAvailable.AddUnique(ComponentName);
							return AC;
						}
					}
				}
			}
		}
	}

	return nullptr;
}

// set_component_property -- Set a property on an SCS component template
// Params: assetPath, componentName, propertyName, value
// ---------------------------------------------------------------------------


// ─── #102 read_node_property ────────────────────────────────────────
TSharedPtr<FJsonValue> FBlueprintHandlers::ReadNodeProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));
	FString NodeId;
	if (auto Err = RequireStringAlt(Params, TEXT("nodeId"), TEXT("nodeName"), NodeId)) return Err;
	FString PinOrProp;
	if (auto Err = RequireStringAlt(Params, TEXT("propertyName"), TEXT("pinName"), PinOrProp)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(TEXT("Blueprint not found"));
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph) return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	UEdGraphNode* Node = FindNodeByGuidOrName(Graph, NodeId);
	if (!Node) return MCPError(FString::Printf(TEXT("Node not found: %s"), *NodeId));

	auto Result = MCPSuccess();
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->PinName.ToString() == PinOrProp)
		{
			Result->SetStringField(TEXT("pinName"), PinOrProp);
			WritePinDefaults(Result, Pin);
			return MCPResult(Result);
		}
	}

	TArray<FString> Parts; PinOrProp.ParseIntoArray(Parts, TEXT("."));
	UStruct* Cur = Node->GetClass();
	void* Container = Node;
	FProperty* Final = nullptr;
	for (int32 i = 0; i < Parts.Num(); ++i)
	{
		FProperty* P = Cur->FindPropertyByName(FName(*Parts[i]));
		if (!P) return MCPError(FString::Printf(TEXT("Property '%s' not found"), *Parts[i]));
		if (i < Parts.Num() - 1)
		{
			FStructProperty* SP = CastField<FStructProperty>(P);
			if (!SP) return MCPError(FString::Printf(TEXT("Not a struct: %s"), *Parts[i]));
			Container = SP->ContainerPtrToValuePtr<void>(Container);
			Cur = SP->Struct;
		}
		else Final = P;
	}
	if (!Final) return MCPError(TEXT("Property path unresolved"));

	FString ValStr;
	const void* ValPtr = Final->ContainerPtrToValuePtr<void>(Container);
	Final->ExportText_Direct(ValStr, ValPtr, ValPtr, Node, PPF_None);
	Result->SetStringField(TEXT("propertyName"), PinOrProp);
	Result->SetStringField(TEXT("type"), Final->GetCPPType());
	Result->SetStringField(TEXT("value"), ValStr);
	return MCPResult(Result);
}

// ─── #115 reparent_component ────────────────────────────────────────


// ---------------------------------------------------------------------------
// v0.7.17 issue #130: bulk graph node import via T3D copy/paste.
// Mirrors the editor's Ctrl+C / Ctrl+V flow (FBlueprintEditor::CopySelectedNodes
// / PasteNodesHere) so callers can author whole subgraphs offline and import
// them atomically rather than building one node at a time.
// ---------------------------------------------------------------------------

TSharedPtr<FJsonValue> FBlueprintHandlers::ExportNodesT3D(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return BlueprintNotFoundError(AssetPath);
	}

	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);
	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	// Collect nodes to export. If nodeIds is omitted/empty, export all nodes
	// in the graph (whole-subgraph round-trip).
	TArray<UEdGraphNode*> SelectedNodes;
	const TArray<TSharedPtr<FJsonValue>>* IdsArrayPtr = nullptr;
	if (Params.IsValid() && Params->TryGetArrayField(TEXT("nodeIds"), IdsArrayPtr) && IdsArrayPtr && IdsArrayPtr->Num() > 0)
	{
		for (const TSharedPtr<FJsonValue>& Val : *IdsArrayPtr)
		{
			if (!Val.IsValid()) continue;
			const FString NodeId = Val->AsString();
			if (NodeId.IsEmpty()) continue;
			UEdGraphNode* Node = FindNodeByGuidOrName(TargetGraph, NodeId);
			if (!Node)
			{
				return MCPError(FString::Printf(TEXT("Node not found: %s"), *NodeId));
			}
			SelectedNodes.AddUnique(Node);
		}
	}
	else
	{
		for (UEdGraphNode* Node : TargetGraph->Nodes)
		{
			if (Node) SelectedNodes.Add(Node);
		}
	}

	if (SelectedNodes.Num() == 0)
	{
		return MCPError(TEXT("No nodes to export"));
	}

	// FEdGraphUtilities::ExportNodesToText only writes nodes flagged
	// CanDuplicateNode == true, so they are pre-filtered here for the count to
	// match reality.
	//
	// That flag does NOT filter entry and return nodes, which an older comment
	// here claimed. UEdGraphNode's base implementation returns true and
	// UK2Node_Tunnel is the only override in BlueprintGraph - and it returns
	// true as well (K2Node_Tunnel.cpp:151-154), so it filters nothing either.
	// UK2Node_FunctionEntry and UK2Node_FunctionResult descend from
	// UK2Node_FunctionTerminator and UK2Node_Event from
	// UK2Node_EditablePinBase, so all of them export happily.
	// That is fine for an export, which is a read. It is the import that has to
	// be careful, and the reason differs by class rather than being one rule:
	// a second UK2Node_FunctionEntry in one function graph is a compile error
	// the compiler raises by name ("Expected only one function entry node in
	// graph", KismetCompiler.cpp:2224-2232), and a second event node for the
	// same event produces a second function context with the same name, which
	// is the DuplicateFunctionName error at KismetCompiler.cpp:2265-2275. A
	// second UK2Node_FunctionResult is NOT an error - the engine supports
	// several result nodes in one graph, which is why
	// UK2Node_FunctionResult::CanUserDeleteNode lets a locked one go when
	// another is present - but a pasted one re-syncs its pins to the entry node
	// and to the primary result node (PostPasteNode, K2Node_FunctionResult.cpp:
	// 269-279), so it does not necessarily arrive with the pins it left with.
	// Each one carries its own `reason` in `singularNodes`, because "strip them
	// first" is right for an entry node and wrong for a result node.
	//
	// The tunnel test is on the EXACT class, not IsA. UK2Node_Composite and
	// UK2Node_MacroInstance both derive from UK2Node_Tunnel
	// (K2Node_Composite.h:26, K2Node_MacroInstance.h:36), so an IsA test listed
	// every collapsed graph and every macro instance as singular and warned that
	// pasting one back stops the Blueprint compiling. Neither is true: a
	// composite carries its bound graph inline as an inner and round-trips, and
	// a macro instance holds a reference to a definition the export never
	// touched. What IS singular is the entry or exit tunnel of a tunnel graph,
	// which is a bare UK2Node_Tunnel, and the engine draws that distinction the
	// same way - UK2Node_Composite::PostPasteNode tests
	// Node->GetClass() == UK2Node_Tunnel::StaticClass() with the comment
	// "Intentional that we check for exact class here!"
	// (K2Node_Composite.cpp:116-121).
	TSet<UObject*> NodeSet;
	int32 SkippedCount = 0;
	TArray<TSharedPtr<FJsonValue>> SingularNodes;
	for (UEdGraphNode* Node : SelectedNodes)
	{
		const bool bIsBareTunnel = Node && Node->GetClass() == UK2Node_Tunnel::StaticClass();
		const TCHAR* SingularReason = nullptr;
		if (Node && Node->CanDuplicateNode())
		{
			if (Node->IsA<UK2Node_FunctionEntry>())
			{
				SingularReason = TEXT(
					"A function graph holds one entry node. A second one is the compile error the compiler raises by "
					"name, 'Expected only one function entry node in graph' (KismetCompiler.cpp:2224-2232). Strip this "
					"before importing into a graph that already has an entry node.");
			}
			else if (Node->IsA<UK2Node_Event>())
			{
				SingularReason = TEXT(
					"An event produces a function context named after the event, and a second one for the same event "
					"is the DuplicateFunctionName compile error (KismetCompiler.cpp:2265-2275). The paste side does "
					"try to save you: UK2Node_Event::CanPasteHere refuses and the schema substitutes a Custom Event, "
					"which overrides nothing. Strip this if the target graph already has the event.");
			}
			else if (Node->IsA<UK2Node_FunctionResult>())
			{
				SingularReason = TEXT(
					"NOT a compile error. A function graph supports several result nodes, which is why "
					"UK2Node_FunctionResult::CanUserDeleteNode releases a locked one while another is present. What "
					"it does not survive intact is the paste: PostPasteNode re-syncs its pins to the entry node and "
					"to the primary result node (K2Node_FunctionResult.cpp:269-279), so it can arrive with different "
					"pins than it left with. Import it, then check its pins.");
			}
			else if (bIsBareTunnel)
			{
				SingularReason = TEXT(
					"A bare UK2Node_Tunnel is the entry or exit gateway of a tunnel graph, and a graph has one of "
					"each. Pasting a second leaves two nodes claiming the same gateway. Strip this unless you are "
					"importing into an empty tunnel graph.");
			}
		}
		if (SingularReason)
		{
			TSharedPtr<FJsonObject> Singular = MakeShared<FJsonObject>();
			Singular->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
			Singular->SetStringField(TEXT("class"), Node->GetClass()->GetName());
			Singular->SetStringField(TEXT("reason"), SingularReason);
			SingularNodes.Add(MakeShared<FJsonValueObject>(Singular));
		}
		if (Node && Node->CanDuplicateNode())
		{
			Node->PrepareForCopying();
			NodeSet.Add(Node);
		}
		else
		{
			++SkippedCount;
		}
	}

	if (NodeSet.Num() == 0)
	{
		// Not "entry/return nodes cannot be exported", which this said before and
		// which the comment above disproves: those report CanDuplicateNode true
		// and export happily. The classes that answer false are elsewhere, mostly
		// in AnimGraph.
		return MCPError(TEXT(
			"No selected node reports CanDuplicateNode true, so FEdGraphUtilities::ExportNodesToText would write "
			"nothing. Check the class of the nodes you selected: the AnimGraph singletons (output pose, state "
			"result, transition result, state machine entry) answer false."));
	}

	FString ExportedText;
	FEdGraphUtilities::ExportNodesToText(NodeSet, ExportedText);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetStringField(TEXT("t3d"), ExportedText);
	Result->SetNumberField(TEXT("count"), NodeSet.Num());
	Result->SetNumberField(TEXT("skipped"), SkippedCount);
	Result->SetArrayField(TEXT("singularNodes"), SingularNodes);
	if (SingularNodes.Num() > 0)
	{
		Result->SetStringField(TEXT("warning"), FString::Printf(TEXT(
			"%d exported node(s) do not simply paste back into a graph that already has their counterpart, and the "
			"reason differs by node rather than being one rule. Each is listed in singularNodes with its own reason: "
			"a second function ENTRY node and a second EVENT node for the same event are compile errors the Kismet "
			"compiler raises by name, while a second function RESULT node is legal - a function graph supports "
			"several - but arrives with its pins re-synced to the entry and primary result nodes rather than the "
			"pins it left with. A bare tunnel is the gateway of a tunnel graph and a graph has one of each. Read the "
			"per-node reason before deciding what to strip; collapsed graphs and macro instances are NOT in this "
			"list and round-trip as they are."),
			SingularNodes.Num()));
	}
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FBlueprintHandlers::ImportNodesT3D(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	FString T3D;
	if (auto Err = RequireStringAlt(Params, TEXT("t3d"), TEXT("text"), T3D)) return Err;
	if (T3D.IsEmpty())
	{
		return MCPError(TEXT("t3d text is empty"));
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return BlueprintNotFoundError(AssetPath);
	}

	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);
	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	if (!FEdGraphUtilities::CanImportNodesFromText(TargetGraph, T3D))
	{
		return MCPError(TEXT("T3D text is not importable into this graph (schema mismatch or malformed)"));
	}

	TSet<UEdGraphNode*> PastedNodes;
	FEdGraphUtilities::ImportNodesFromText(TargetGraph, T3D, /*out*/ PastedNodes);

	if (PastedNodes.Num() == 0)
	{
		return MCPError(TEXT("ImportNodesFromText produced no nodes"));
	}

	// Re-center pasted nodes around an explicit (posX, posY) anchor when given,
	// otherwise keep their exported positions. Mirrors PasteNodesHere.
	const bool bRecenter = Params.IsValid() && Params->HasField(TEXT("posX")) && Params->HasField(TEXT("posY"));
	double AnchorX = 0.0, AnchorY = 0.0;
	if (bRecenter)
	{
		Params->TryGetNumberField(TEXT("posX"), AnchorX);
		Params->TryGetNumberField(TEXT("posY"), AnchorY);

		double AvgX = 0.0, AvgY = 0.0;
		for (UEdGraphNode* Node : PastedNodes)
		{
			AvgX += Node->NodePosX;
			AvgY += Node->NodePosY;
		}
		AvgX /= PastedNodes.Num();
		AvgY /= PastedNodes.Num();

		for (UEdGraphNode* Node : PastedNodes)
		{
			Node->NodePosX = (Node->NodePosX - AvgX) + AnchorX;
			Node->NodePosY = (Node->NodePosY - AvgY) + AnchorY;
		}
	}

	// Fresh GUIDs so the pasted nodes don't collide with the originals when
	// pasting back into the same graph (e.g. round-trip duplicate). That is the
	// ONLY per-node step left here, and it is the only one the editor's own
	// paste does either (FBlueprintEditor::PasteNodesHere, BlueprintEditor.cpp:
	// 7733-7734).
	//
	// This loop used to call PostPasteNode() and ReconstructNode() as well.
	// FEdGraphUtilities::ImportNodesFromText has already called
	// PostProcessPastedNodes, which fixes the pin cross-links and then calls
	// Node->PostPasteNode(), Node->ReconstructNode() and SetFlags(RF_Transactional)
	// on every spawned node (EdGraphUtilities.cpp:232-248, called from :491). So
	// the pass here added nothing - the engine's ReconstructNode is on every
	// UEdGraphNode where this one was only on UK2Node - and PostPasteNode is not
	// idempotent, which made the second call actively destructive:
	//
	//   UK2Node_Composite::PostPasteNode ends with
	//   RenameBoundGraphCloseToName(BoundGraph->GetName()), and that function
	//   starts its counter at 2 and always formats '<name>_<n>' without ever
	//   trying the bare name (K2Node_Composite.cpp:155 and 258-281). Run twice,
	//   a collapsed graph named MyGraph arrived as MyGraph_2_2 and every later
	//   call addressing it by graphName missed.
	//
	//   UK2Node_AddComponent::PostPasteNode calls MakeNewComponentTemplate,
	//   which mints a template and registers it (K2Node_AddComponent.cpp:329-335
	//   and 560-611); the second call minted a second one and left the first
	//   registered and unreferenced. UK2Node_Timeline::PostPasteNode does the
	//   same for a timeline template (K2Node_Timeline.cpp:220-265).
	//
	// Engine bodies read in a 5.7.4 source tree; the declarations they rest on
	// are present unchanged in the 5.8 headers this plugin builds against.
	TArray<TSharedPtr<FJsonValue>> NodeIds;
	for (UEdGraphNode* Node : PastedNodes)
	{
		Node->CreateNewGuid();
		NodeIds.Add(MakeShared<FJsonValueString>(Node->NodeGuid.ToString()));
	}

	TargetGraph->NotifyGraphChanged();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetArrayField(TEXT("nodeIds"), NodeIds);
	Result->SetNumberField(TEXT("count"), NodeIds.Num());

	// A rollback record is one call, and delete_node takes one nodeId. A paste
	// of exactly one node therefore has an exact inverse; a bulk paste does not,
	// and the ids are reported above so a caller can undo it deliberately.
	if (NodeIds.Num() == 1)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("path"), AssetPath);
		Payload->SetStringField(TEXT("graphName"), GraphName);
		Payload->SetStringField(TEXT("nodeId"), NodeIds[0]->AsString());
		MCPSetRollback(Result, TEXT("delete_node"), Payload);
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(TEXT(
			"This paste created %d nodes and delete_node removes one nodeId per call, so no single inverse call "
			"undoes it. Every new nodeId is listed in nodeIds; delete them individually to reverse this import."),
			NodeIds.Num()));
	}
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// v1.0.0-rc.15 - agent-friendly BP authoring (#284 #285 #267 #277)
// ---------------------------------------------------------------------------

// #284 - compile_blueprints: batch compile + save with per-path status.
TSharedPtr<FJsonValue> FBlueprintHandlers::CompileBlueprints(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* PathsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("assetPaths"), PathsArray) || !PathsArray)
	{
		return MCPError(TEXT("Missing 'assetPaths' (array of blueprint asset paths)"));
	}

	bool bSave = OptionalBool(Params, TEXT("save"), true);

	TArray<TSharedPtr<FJsonValue>> Results;
	int32 Compiled = 0, Failed = 0, NotFound = 0, AlreadyUpToDate = 0;
	// Blueprints whose compiled status actually moved: one that came in stale,
	// or one that came out in error. A batch where every entry was already up
	// to date and compiled clean ends where it started.
	int32 StatusChanged = 0;
	for (const TSharedPtr<FJsonValue>& Entry : *PathsArray)
	{
		FString AssetPath = Entry->AsString();
		TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("path"), AssetPath);

		UBlueprint* Blueprint = LoadBlueprint(AssetPath);
		if (!Blueprint)
		{
			Item->SetStringField(TEXT("status"), TEXT("not_found"));
			NotFound++;
			Results.Add(MakeShared<FJsonValueObject>(Item));
			continue;
		}

		// Asked before the compile: afterwards every Blueprint reads as up to
		// date. BS_UpToDateWithWarnings is a DISTINCT status from BS_UpToDate
		// and has to be counted here too, or a Blueprint that compiles clean
		// with warnings reports itself stale forever.
		const bool bWasUpToDate =
			Blueprint->Status == EBlueprintStatus::BS_UpToDate
			|| Blueprint->Status == EBlueprintStatus::BS_UpToDateWithWarnings;
		Item->SetBoolField(TEXT("wasUpToDate"), bWasUpToDate);
		if (bWasUpToDate) AlreadyUpToDate++;

		FCompilerResultsLog CompileLog;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &CompileLog);

		const bool bItemChanged = !bWasUpToDate || CompileLog.NumErrors > 0;
		Item->SetBoolField(TEXT("changed"), bItemChanged);
		if (bItemChanged) StatusChanged++;

		if (CompileLog.NumErrors > 0)
		{
			Item->SetStringField(TEXT("status"), TEXT("failed"));
			Item->SetNumberField(TEXT("errors"), CompileLog.NumErrors);
			Item->SetNumberField(TEXT("warnings"), CompileLog.NumWarnings);
			Failed++;
		}
		else
		{
			if (bSave) SaveAssetPackage(Blueprint);
			Item->SetStringField(TEXT("status"), TEXT("compiled"));
			Item->SetNumberField(TEXT("warnings"), CompileLog.NumWarnings);
			Compiled++;
		}
		Results.Add(MakeShared<FJsonValueObject>(Item));
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("total"), PathsArray->Num());
	Result->SetNumberField(TEXT("compiled"), Compiled);
	Result->SetNumberField(TEXT("failed"), Failed);
	Result->SetNumberField(TEXT("notFound"), NotFound);
	Result->SetNumberField(TEXT("alreadyUpToDate"), AlreadyUpToDate);
	Result->SetNumberField(TEXT("statusChanged"), StatusChanged);
	Result->SetArrayField(TEXT("results"), Results);

	// The work and the outcome are separate answers. CompileBlueprint rebuilds
	// and reinstances on every call regardless of status, so a batch where
	// every Blueprint was already up to date still did the full work, which is
	// what idempotent=false says. `changed` is the outcome: false when no
	// Blueprint's compiled status moved, which is what a retried batch needs.
	Result->SetBoolField(TEXT("idempotent"), false);
	Result->SetBoolField(TEXT("changed"), StatusChanged > 0);
	Result->SetStringField(TEXT("idempotencyNote"),
		TEXT("A compile always rebuilds each generated class and reinstances its objects, so calling twice does "
		     "real work twice. alreadyUpToDate counts the Blueprints whose status said up to date BEFORE this ran, "
		     "and statusChanged counts the ones whose status actually moved; neither claims the call was free."));

	// Same as the single-Blueprint compile: a compile rebuilds the generated
	// class from graphs that were already saved, and nothing un-compiles one.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), TEXT(
		"Compiling rebuilds each generated class from graphs that were already saved. There is no inverse action, "
		"and the pre-compile generated classes are not retained to restore from."));
	return MCPResult(Result);
}

// #285 - cleanup_graph: remove orphan nodes (no pins, missing class, blank
// title). Iterates one graph if graphName given, else every graph on the BP.
TSharedPtr<FJsonValue> FBlueprintHandlers::CleanupGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	const FString GraphName = OptionalString(Params, TEXT("graphName"));

	TArray<UEdGraph*> Graphs;
	if (!GraphName.IsEmpty())
	{
		if (UEdGraph* G = FindGraph(Blueprint, GraphName)) Graphs.Add(G);
		else return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}
	else
	{
		Blueprint->GetAllGraphs(Graphs);
	}

	int32 Removed = 0;
	int32 Restorable = 0;
	TArray<TSharedPtr<FJsonValue>> RemovedIds;
	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph) continue;
		// Iterate a copy because RemoveNode mutates Graph->Nodes.
		TArray<UEdGraphNode*> Snapshot = Graph->Nodes;
		for (UEdGraphNode* Node : Snapshot)
		{
			if (!Node) continue;
			bool bOrphan = false;
			// Missing class (often the symptom of a node that lost its parent UClass)
			if (!Node->GetClass()) bOrphan = true;
			// Empty title + no pins is the canonical "corrupted node" signature.
			else if (Node->Pins.Num() == 0 && Node->GetNodeTitle(ENodeTitleType::ListView).IsEmpty()) bOrphan = true;
			// Function-call nodes whose target UFunction has been deleted.
			else if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
			{
				if (!CallNode->GetTargetFunction()) bOrphan = true;
			}

			if (bOrphan)
			{
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("graph"), Graph->GetName());
				Entry->SetStringField(TEXT("nodeGuid"), Node->NodeGuid.ToString());
				Entry->SetStringField(TEXT("class"), Node->GetClass() ? Node->GetClass()->GetName() : TEXT("<null>"));

				// Not every orphan is unrecoverable. The first two tests catch
				// genuinely corrupt nodes with nothing to export, but the third
				// catches an ordinary UK2Node_CallFunction whose target UFunction
				// is merely absent right now: a renamed function, or a plugin
				// that is not loaded in this session. That node exports fine, so
				// its text is captured per node and reported rather than thrown
				// away on the claim that it could not be exported.
				FString OrphanT3D;
				if (Node->GetClass() && Node->CanDuplicateNode()
					&& !Node->IsA<UK2Node_FunctionTerminator>()
					&& !Node->IsA<UK2Node_Tunnel>()
					&& !Node->IsA<UK2Node_Event>())
				{
					Node->PrepareForCopying();
					TSet<UObject*> OrphanSet;
					OrphanSet.Add(Node);
					FEdGraphUtilities::ExportNodesToText(OrphanSet, OrphanT3D);
				}
				Entry->SetStringField(TEXT("t3d"), OrphanT3D);
				Entry->SetBoolField(TEXT("restorable"), !OrphanT3D.IsEmpty());
				if (!OrphanT3D.IsEmpty()) Restorable++;

				RemovedIds.Add(MakeShared<FJsonValueObject>(Entry));
				Graph->RemoveNode(Node);
				Removed++;
			}
		}
	}

	if (Removed > 0)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		SaveAssetPackage(Blueprint);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetNumberField(TEXT("removed"), Removed);
	Result->SetArrayField(TEXT("removedNodes"), RemovedIds);
	Result->SetBoolField(TEXT("unchanged"), Removed == 0);

	// This sweeps whole graphs and removes as many nodes as match, so there is
	// no single inverse call: import_nodes_t3d pastes into ONE named graph and a
	// rollback record carries one call. That is the honest reason. It is NOT
	// that the nodes are unrecoverable: the third orphan test catches an
	// ordinary call node whose target UFunction is merely missing right now (a
	// renamed function, an unloaded plugin), and those export cleanly. Each
	// removed node therefore carries its own t3d and a `restorable` flag.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), FString::Printf(TEXT(
		"No single inverse call exists: this sweeps every graph on the Blueprint and import_nodes_t3d pastes into "
		"one named graph per call. %d of the %d removed node(s) are restorable and carry their exported text in "
		"removedNodes[].t3d, with the owning graph in removedNodes[].graph; replay them one graph at a time. The "
		"rest had no UClass or no pins and could not be exported. Pin links are not restored either way. Note that "
		"a call node whose target function is merely renamed or in an unloaded plugin looks like an orphan here, "
		"which is the case worth checking before treating this cleanup as final."),
		Restorable, Removed));
	return MCPResult(Result);
}

// #267 - connect_pins_batch: apply many wirings in one call, single compile.
TSharedPtr<FJsonValue> FBlueprintHandlers::ConnectPinsBatch(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	const FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("connections"), ConnectionsArray) || !ConnectionsArray)
	{
		return MCPError(TEXT("Missing 'connections' array - each entry: {sourceNode, sourcePin, targetNode, targetPin}"));
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);

	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);
	if (!TargetGraph) return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	const UEdGraphSchema* Schema = TargetGraph->GetSchema();
	if (!Schema) return MCPError(TEXT("Graph has no schema"));

	auto FindPin = [](UEdGraphNode* N, const FString& Name) -> UEdGraphPin*
	{
		for (UEdGraphPin* P : N->Pins)
		{
			if (P && P->PinName.ToString() == Name) return P;
		}
		return nullptr;
	};

	int32 Connected = 0, Existed = 0, Failed = 0;
	TArray<TSharedPtr<FJsonValue>> Detail;
	for (const TSharedPtr<FJsonValue>& Entry : *ConnectionsArray)
	{
		const TSharedPtr<FJsonObject> Obj = Entry->AsObject();
		if (!Obj.IsValid()) { Failed++; continue; }

		FString SrcId, SrcPin, TgtId, TgtPin;
		Obj->TryGetStringField(TEXT("sourceNode"), SrcId);
		Obj->TryGetStringField(TEXT("sourcePin"), SrcPin);
		Obj->TryGetStringField(TEXT("targetNode"), TgtId);
		Obj->TryGetStringField(TEXT("targetPin"), TgtPin);

		TSharedPtr<FJsonObject> EntryResult = MakeShared<FJsonObject>();
		EntryResult->SetStringField(TEXT("sourceNode"), SrcId);
		EntryResult->SetStringField(TEXT("sourcePin"), SrcPin);
		EntryResult->SetStringField(TEXT("targetNode"), TgtId);
		EntryResult->SetStringField(TEXT("targetPin"), TgtPin);

		UEdGraphNode* Src = FindNodeByGuidOrName(TargetGraph, SrcId);
		UEdGraphNode* Tgt = FindNodeByGuidOrName(TargetGraph, TgtId);
		if (!Src || !Tgt)
		{
			EntryResult->SetStringField(TEXT("status"), TEXT("node_not_found"));
			Detail.Add(MakeShared<FJsonValueObject>(EntryResult));
			Failed++;
			continue;
		}
		UEdGraphPin* SP = FindPin(Src, SrcPin);
		UEdGraphPin* TP = FindPin(Tgt, TgtPin);
		if (!SP || !TP)
		{
			EntryResult->SetStringField(TEXT("status"), TEXT("pin_not_found"));
			Detail.Add(MakeShared<FJsonValueObject>(EntryResult));
			Failed++;
			continue;
		}
		if (SP->LinkedTo.Contains(TP))
		{
			EntryResult->SetStringField(TEXT("status"), TEXT("existed"));
			Detail.Add(MakeShared<FJsonValueObject>(EntryResult));
			Existed++;
			continue;
		}
		if (Schema->TryCreateConnection(SP, TP))
		{
			EntryResult->SetStringField(TEXT("status"), TEXT("connected"));
			Connected++;
		}
		else
		{
			FPinConnectionResponse Resp = Schema->CanCreateConnection(SP, TP);
			EntryResult->SetStringField(TEXT("status"), TEXT("failed"));
			EntryResult->SetStringField(TEXT("reason"), Resp.Message.ToString());
			Failed++;
		}
		Detail.Add(MakeShared<FJsonValueObject>(EntryResult));
	}

	if (Connected > 0)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		SaveAssetPackage(Blueprint);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetNumberField(TEXT("total"), ConnectionsArray->Num());
	Result->SetNumberField(TEXT("connected"), Connected);
	Result->SetNumberField(TEXT("existed"), Existed);
	Result->SetNumberField(TEXT("failed"), Failed);
	Result->SetArrayField(TEXT("results"), Detail);
	Result->SetBoolField(TEXT("unchanged"), Connected == 0);

	// Same hole as connect_pins: nothing in the Blueprint surface breaks a pin
	// link, so a batch of wirings has no inverse call either.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), TEXT(
		"No action breaks a Blueprint pin link, so these connections have no inverse call. delete_node is not it: it "
		"would remove nodes this call only wired up. The per-connection results say which links were newly made, "
		"which is what a manual undo would have to break."));
	return MCPResult(Result);
}

// #277 - set_node_position: write NodePosX/NodePosY on a target node.
TSharedPtr<FJsonValue> FBlueprintHandlers::SetNodePosition(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	const FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));
	FString NodeId;
	if (auto Err = RequireStringAlt(Params, TEXT("nodeId"), TEXT("nodeName"), NodeId)) return Err;

	int32 PosX = OptionalInt(Params, TEXT("posX"), 0);
	int32 PosY = OptionalInt(Params, TEXT("posY"), 0);

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph) return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	UEdGraphNode* Node = FindNodeByGuidOrName(Graph, NodeId);
	if (!Node) return MCPError(FString::Printf(TEXT("Node not found: %s"), *NodeId));

	// Where it was, which is both the idempotency answer and the exact inverse.
	const int32 PrevPosX = Node->NodePosX;
	const int32 PrevPosY = Node->NodePosY;
	const bool bUnchanged = (PrevPosX == PosX && PrevPosY == PosY);

	// A node already at these coordinates is not dirtied, not marked modified
	// and does not notify the graph: writing the same numbers back and then
	// reporting unchanged:true beside MCPSetUpdated was two contradictory
	// claims in one result.
	if (!bUnchanged)
	{
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;
		Node->Modify();
		Graph->NotifyGraphChanged();
	}

	auto Result = MCPSuccess();
	if (bUnchanged) MCPSetExisted(Result); else MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetStringField(TEXT("nodeId"), NodeId);
	Result->SetNumberField(TEXT("posX"), PosX);
	Result->SetNumberField(TEXT("posY"), PosY);
	Result->SetNumberField(TEXT("previousPosX"), PrevPosX);
	Result->SetNumberField(TEXT("previousPosY"), PrevPosY);
	Result->SetBoolField(TEXT("unchanged"), bUnchanged);

	if (bUnchanged)
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The node was already at these coordinates, so nothing was moved and there is nothing to undo."));
	}
	else
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("path"), AssetPath);
		Payload->SetStringField(TEXT("graphName"), GraphName);
		Payload->SetStringField(TEXT("nodeId"), NodeId);
		Payload->SetNumberField(TEXT("posX"), PrevPosX);
		Payload->SetNumberField(TEXT("posY"), PrevPosY);
		MCPSetRollback(Result, TEXT("set_node_position"), Payload);
	}
	return MCPResult(Result);
}

// #277 - auto_layout_graph: simple topological layered layout. Each node is
// placed in a column derived from longest predecessor path; rows are stacked
// with a fixed gap. Not Sugiyama-perfect but eliminates the (0,0) stack that
// programmatic add_node leaves behind.
TSharedPtr<FJsonValue> FBlueprintHandlers::AutoLayoutGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	const FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));
	int32 ColumnGap = OptionalInt(Params, TEXT("columnGap"), 360);
	int32 RowGap = OptionalInt(Params, TEXT("rowGap"), 200);

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return BlueprintNotFoundError(AssetPath);
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph) return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

	// Compute a column index per node: longest predecessor chain through exec
	// pins (or any input pin if exec is absent).
	TMap<UEdGraphNode*, int32> Column;
	TArray<UEdGraphNode*> Order = Graph->Nodes;

	auto GetIncoming = [](UEdGraphNode* Node) -> TArray<UEdGraphNode*>
	{
		TArray<UEdGraphNode*> Result;
		for (UEdGraphPin* P : Node->Pins)
		{
			if (!P || P->Direction != EGPD_Input) continue;
			for (UEdGraphPin* Linked : P->LinkedTo)
			{
				if (Linked && Linked->GetOwningNode()) Result.AddUnique(Linked->GetOwningNode());
			}
		}
		return Result;
	};

	// Iterate to fixed point - simple but adequate for the typical 5-50 node
	// graphs the bridge produces.
	for (int32 Iter = 0; Iter < Order.Num() + 1; Iter++)
	{
		bool bChanged = false;
		for (UEdGraphNode* Node : Order)
		{
			int32 Col = 0;
			for (UEdGraphNode* Inc : GetIncoming(Node))
			{
				if (int32* InCol = Column.Find(Inc)) Col = FMath::Max(Col, *InCol + 1);
			}
			int32* Existing = Column.Find(Node);
			if (!Existing || *Existing != Col)
			{
				Column.Add(Node, Col);
				bChanged = true;
			}
		}
		if (!bChanged) break;
	}

	// Bucket nodes per column, then assign rows in stable order.
	TMap<int32, TArray<UEdGraphNode*>> Buckets;
	for (UEdGraphNode* Node : Order)
	{
		int32 Col = Column.FindRef(Node);
		Buckets.FindOrAdd(Col).Add(Node);
	}

	// Where every node sat before the layout ran. There is no bulk position
	// writer to hand this back to, so it is reported instead of thrown away:
	// it is what a caller needs to walk set_node_position back node by node.
	//
	// It is capped, and off by default above the cap. An EventGraph can hold
	// hundreds of nodes and this is a rollback aid, not a graph dump: emitting
	// one object per node unconditionally would put an unbounded array in every
	// response. `capturePreviousPositions` forces it on, `previousPositionsLimit`
	// raises the cap, and the result always says which of the two happened.
	const int32 NodeCount = Order.Num();
	const int32 PositionsLimit = FMath::Max(0, OptionalInt(Params, TEXT("previousPositionsLimit"), 200));
	const bool bForcePositions = OptionalBool(Params, TEXT("capturePreviousPositions"), false);
	const bool bCapturePositions = bForcePositions || NodeCount <= PositionsLimit;

	int32 Repositioned = 0;
	int32 Moved = 0;
	TArray<TSharedPtr<FJsonValue>> PreviousPositions;
	for (auto& Pair : Buckets)
	{
		int32 Col = Pair.Key;
		int32 Row = 0;
		for (UEdGraphNode* Node : Pair.Value)
		{
			const int32 PrevPosX = Node->NodePosX;
			const int32 PrevPosY = Node->NodePosY;
			const int32 NewPosX = Col * ColumnGap;
			const int32 NewPosY = Row * RowGap;

			if (bCapturePositions)
			{
				TSharedPtr<FJsonObject> Was = MakeShared<FJsonObject>();
				Was->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
				Was->SetNumberField(TEXT("posX"), PrevPosX);
				Was->SetNumberField(TEXT("posY"), PrevPosY);
				PreviousPositions.Add(MakeShared<FJsonValueObject>(Was));
			}

			// Only a node that actually moves is marked modified. Calling
			// Modify() on every node dirtied the package for a layout that
			// produced the coordinates it found, and the result then reported
			// unchanged:true beside N nodes marked dirty: the same
			// claim-and-contradiction the marker fix two lines below removed.
			if (PrevPosX != NewPosX || PrevPosY != NewPosY)
			{
				Node->NodePosX = NewPosX;
				Node->NodePosY = NewPosY;
				Node->Modify();
				Moved++;
			}
			Repositioned++;
			Row++;
		}
	}
	// Nothing moved means nothing to notify. The graph is only told it changed
	// when it did.
	if (Moved > 0)
	{
		Graph->NotifyGraphChanged();
	}

	auto Result = MCPSuccess();
	// `repositioned` counts nodes this walked; `moved` counts the ones whose
	// coordinates actually differ. A layout that produced the same positions it
	// found is not an update, and saying MCPSetUpdated for it contradicted the
	// unchanged flag sitting next to it.
	if (Moved == 0) MCPSetExisted(Result); else MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetNumberField(TEXT("repositioned"), Repositioned);
	Result->SetNumberField(TEXT("moved"), Moved);
	Result->SetBoolField(TEXT("unchanged"), Moved == 0);
	Result->SetNumberField(TEXT("columns"), Buckets.Num());
	Result->SetArrayField(TEXT("previousPositions"), PreviousPositions);
	Result->SetBoolField(TEXT("previousPositionsCaptured"), bCapturePositions);

	// A layout rewrites the position of every node in the graph, and
	// set_node_position moves one node per call, so no single inverse call
	// restores it. The previous coordinates are reported above rather than a
	// rollback being invented for them.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), bCapturePositions
		? FString(TEXT(
			"A layout rewrites every node's position and set_node_position moves one node per call, so there is no "
			"single inverse call. previousPositions carries each node's coordinates from before this ran; replay "
			"them through set_node_position to restore the old layout."))
		: FString::Printf(TEXT(
			"A layout rewrites every node's position and set_node_position moves one node per call, so there is no "
			"single inverse call. This graph has %d nodes, over the %d-node cap, so previousPositions was NOT "
			"captured and the old layout is gone. Pass capturePreviousPositions=true (or raise "
			"previousPositionsLimit) BEFORE running this if you need to be able to put it back."),
			NodeCount, PositionsLimit));
	return MCPResult(Result);
}
