#include "Handlers/GameplayHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

// Behavior Tree node authoring. Builds the runtime tree (UBehaviorTree::RootNode,
// UBTCompositeNode::Children/Services, FBTCompositeChild::Decorators) directly,
// reusing exactly the classes and fields the read path in GameplayHandlers.cpp
// already walks. At runtime UBehaviorTreeManager assigns execution indices when
// the tree loads, so a hand-built tree executes.
//
// Caveat (documented in the TS action + returned as `note`): this authors the
// runtime tree, not the editor's visual UBehaviorTreeGraph. The generated BT
// runs correctly, but opening it in the Behavior Tree editor and re-saving would
// regenerate the runtime tree from an empty graph. Editor-graph sync is a
// follow-up phase. These are programmatic BTs you run, not hand-edit.

namespace
{
	const TCHAR* kBTAuthorNote = TEXT("Authored the runtime tree (executes at runtime). The visual Behavior Tree editor graph is not generated - opening and re-saving in the BT editor would overwrite this. Editor-graph sync is a follow-up.");

	UBehaviorTree* LoadBT(const FString& Path)
	{
		return LoadAssetByPath<UBehaviorTree>(Path);
	}

	/** Resolve a node class by friendly alias, short name, or full path, and
	 *  verify it derives from RequiredBase. */
	UClass* ResolveBTClass(const FString& Name, UClass* RequiredBase)
	{
		FString Resolved = Name;
		// Convenience aliases for the built-in composites.
		if (Name.Equals(TEXT("Selector"), ESearchCase::IgnoreCase)) Resolved = TEXT("BTComposite_Selector");
		else if (Name.Equals(TEXT("Sequence"), ESearchCase::IgnoreCase)) Resolved = TEXT("BTComposite_Sequence");
		else if (Name.Equals(TEXT("SimpleParallel"), ESearchCase::IgnoreCase)) Resolved = TEXT("BTComposite_SimpleParallel");

		UClass* Cls = nullptr;
		if (Resolved.Contains(TEXT("/")))
		{
			Cls = LoadObject<UClass>(nullptr, *Resolved);
		}
		if (!Cls)
		{
			Cls = FindClassByShortName(Resolved);
		}
		if (!Cls || !RequiredBase || !Cls->IsChildOf(RequiredBase)) return nullptr;
		if (Cls->HasAnyClassFlags(CLASS_Abstract)) return nullptr;
		return Cls;
	}

	/** Collect every node in the tree (composites, tasks, decorators, services,
	 *  root decorators) for name lookup. */
	void WalkAll(UBehaviorTree* BT, TArray<UBTNode*>& Out)
	{
		for (UBTDecorator* D : BT->RootDecorators) if (D) Out.Add(D);
		TFunction<void(UBTCompositeNode*)> Recurse = [&](UBTCompositeNode* Comp)
		{
			if (!Comp) return;
			Out.Add(Comp);
			for (UBTService* S : Comp->Services) if (S) Out.Add(S);
			for (const FBTCompositeChild& Child : Comp->Children)
			{
				for (UBTDecorator* D : Child.Decorators) if (D) Out.Add(D);
				if (Child.ChildComposite) Recurse(Child.ChildComposite);
				else if (Child.ChildTask) Out.Add(Child.ChildTask);
			}
		};
		Recurse(BT->RootNode);
	}

	/** Find a node by object name or friendly NodeName. "root"/"" resolves to
	 *  the root composite. */
	UBTNode* FindNode(UBehaviorTree* BT, const FString& Name)
	{
		if (Name.IsEmpty() || Name.Equals(TEXT("root"), ESearchCase::IgnoreCase)) return BT->RootNode;
		TArray<UBTNode*> All;
		WalkAll(BT, All);
		for (UBTNode* N : All)
		{
			if (N->GetName() == Name || N->NodeName == Name) return N;
		}
		return nullptr;
	}

	/** Locate the composite + child index whose slot holds Target. */
	bool FindParentSlot(UBehaviorTree* BT, UBTNode* Target, UBTCompositeNode*& OutParent, int32& OutIndex)
	{
		TFunction<bool(UBTCompositeNode*)> Recurse = [&](UBTCompositeNode* Comp) -> bool
		{
			if (!Comp) return false;
			for (int32 i = 0; i < Comp->Children.Num(); ++i)
			{
				const FBTCompositeChild& Child = Comp->Children[i];
				if (Child.ChildComposite == Target || Child.ChildTask == Target)
				{
					OutParent = Comp;
					OutIndex = i;
					return true;
				}
				if (Child.ChildComposite && Recurse(Child.ChildComposite)) return true;
			}
			return false;
		};
		return Recurse(BT->RootNode);
	}

	FName UniqueNodeName(UBehaviorTree* BT, UClass* Cls, const FString& Desired)
	{
		const FString Base = Desired.IsEmpty() ? Cls->GetName() : Desired;
		return MakeUniqueObjectName(BT, Cls, FName(*Base));
	}

	void FinishNode(UBTNode* Node, const FString& DisplayName)
	{
		// Set the friendly NodeName via reflection to match the read path and
		// stay agnostic to the field's access level across engine versions.
		if (DisplayName.IsEmpty()) return;
		if (FProperty* Prop = Node->GetClass()->FindPropertyByName(TEXT("NodeName")))
		{
			if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
			{
				StrProp->SetPropertyValue_InContainer(Node, DisplayName);
			}
		}
	}

	TSharedPtr<FJsonValue> SaveBTResult(UBehaviorTree* BT, TSharedPtr<FJsonObject> Result)
	{
		SaveAssetPackage(BT);
		Result->SetStringField(TEXT("note"), kBTAuthorNote);
		return MCPResult(Result);
	}
}

TSharedPtr<FJsonValue> FGameplayHandlers::SetBTRoot(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (TSharedPtr<FJsonValue> Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString ClassName;
	if (TSharedPtr<FJsonValue> Err = RequireStringAlt(Params, TEXT("compositeClass"), TEXT("nodeClass"), ClassName)) return Err;

	UBehaviorTree* BT = LoadBT(AssetPath);
	if (!BT) return MCPError(FString::Printf(TEXT("BehaviorTree not found: %s"), *AssetPath));

	UClass* Cls = ResolveBTClass(ClassName, UBTCompositeNode::StaticClass());
	if (!Cls) return MCPError(FString::Printf(TEXT("'%s' is not a usable UBTCompositeNode subclass (try Selector, Sequence, or a BTComposite_ class)"), *ClassName));

	const FString DisplayName = OptionalString(Params, TEXT("nodeName"));
	UBTCompositeNode* Root = NewObject<UBTCompositeNode>(BT, Cls, UniqueNodeName(BT, Cls, DisplayName), RF_Transactional);
	FinishNode(Root, DisplayName);
	BT->RootNode = Root;

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("rootName"), Root->GetName());
	Result->SetStringField(TEXT("rootClass"), Cls->GetName());
	return SaveBTResult(BT, Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::AddBTChild(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (TSharedPtr<FJsonValue> Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString ClassName;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("nodeClass"), ClassName)) return Err;

	UBehaviorTree* BT = LoadBT(AssetPath);
	if (!BT) return MCPError(FString::Printf(TEXT("BehaviorTree not found: %s"), *AssetPath));

	const FString ParentName = OptionalString(Params, TEXT("parentName"), TEXT("root"));
	UBTNode* ParentNode = FindNode(BT, ParentName);
	UBTCompositeNode* Parent = Cast<UBTCompositeNode>(ParentNode);
	if (!Parent) return MCPError(FString::Printf(TEXT("Parent composite '%s' not found (set_bt_root first, or pass a composite node name)"), *ParentName));

	// Resolve as composite first, then task.
	UClass* Cls = ResolveBTClass(ClassName, UBTCompositeNode::StaticClass());
	bool bIsComposite = Cls != nullptr;
	if (!Cls) Cls = ResolveBTClass(ClassName, UBTTaskNode::StaticClass());
	if (!Cls) return MCPError(FString::Printf(TEXT("'%s' is not a usable UBTCompositeNode or UBTTaskNode subclass"), *ClassName));

	const FString DisplayName = OptionalString(Params, TEXT("nodeName"));
	FBTCompositeChild NewChild;
	FString ChildName;
	if (bIsComposite)
	{
		UBTCompositeNode* Node = NewObject<UBTCompositeNode>(BT, Cls, UniqueNodeName(BT, Cls, DisplayName), RF_Transactional);
		FinishNode(Node, DisplayName);
		NewChild.ChildComposite = Node;
		ChildName = Node->GetName();
	}
	else
	{
		UBTTaskNode* Node = NewObject<UBTTaskNode>(BT, Cls, UniqueNodeName(BT, Cls, DisplayName), RF_Transactional);
		FinishNode(Node, DisplayName);
		NewChild.ChildTask = Node;
		ChildName = Node->GetName();
	}
	const int32 Index = Parent->Children.Add(NewChild);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("nodeName"), ChildName);
	Result->SetStringField(TEXT("nodeClass"), Cls->GetName());
	Result->SetStringField(TEXT("kind"), bIsComposite ? TEXT("composite") : TEXT("task"));
	Result->SetStringField(TEXT("parent"), Parent->GetName());
	Result->SetNumberField(TEXT("childIndex"), Index);
	return SaveBTResult(BT, Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::AddBTDecorator(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (TSharedPtr<FJsonValue> Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString ClassName;
	if (TSharedPtr<FJsonValue> Err = RequireStringAlt(Params, TEXT("decoratorClass"), TEXT("nodeClass"), ClassName)) return Err;

	UBehaviorTree* BT = LoadBT(AssetPath);
	if (!BT) return MCPError(FString::Printf(TEXT("BehaviorTree not found: %s"), *AssetPath));

	FString TargetName;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("targetName"), TargetName)) return Err;

	UClass* Cls = ResolveBTClass(ClassName, UBTDecorator::StaticClass());
	if (!Cls) return MCPError(FString::Printf(TEXT("'%s' is not a usable UBTDecorator subclass"), *ClassName));

	const FString DisplayName = OptionalString(Params, TEXT("nodeName"));
	UBTDecorator* Dec = NewObject<UBTDecorator>(BT, Cls, UniqueNodeName(BT, Cls, DisplayName), RF_Transactional);
	FinishNode(Dec, DisplayName);

	UBTNode* Target = FindNode(BT, TargetName);
	if (!Target) return MCPError(FString::Printf(TEXT("Target node '%s' not found"), *TargetName));

	FString Attached;
	if (Target == BT->RootNode)
	{
		BT->RootDecorators.Add(Dec);
		Attached = TEXT("root");
	}
	else
	{
		UBTCompositeNode* Parent = nullptr;
		int32 Index = INDEX_NONE;
		if (!FindParentSlot(BT, Target, Parent, Index))
		{
			return MCPError(FString::Printf(TEXT("Could not locate the tree slot for '%s' (decorators attach to a composite/task child slot)"), *TargetName));
		}
		Parent->Children[Index].Decorators.Add(Dec);
		Attached = Target->GetName();
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("nodeName"), Dec->GetName());
	Result->SetStringField(TEXT("nodeClass"), Cls->GetName());
	Result->SetStringField(TEXT("attachedTo"), Attached);
	return SaveBTResult(BT, Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::AddBTService(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (TSharedPtr<FJsonValue> Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString ClassName;
	if (TSharedPtr<FJsonValue> Err = RequireStringAlt(Params, TEXT("serviceClass"), TEXT("nodeClass"), ClassName)) return Err;

	UBehaviorTree* BT = LoadBT(AssetPath);
	if (!BT) return MCPError(FString::Printf(TEXT("BehaviorTree not found: %s"), *AssetPath));

	const FString CompositeName = OptionalString(Params, TEXT("compositeName"), TEXT("root"));
	UBTCompositeNode* Comp = Cast<UBTCompositeNode>(FindNode(BT, CompositeName));
	if (!Comp) return MCPError(FString::Printf(TEXT("Composite '%s' not found (services attach to composite nodes)"), *CompositeName));

	UClass* Cls = ResolveBTClass(ClassName, UBTService::StaticClass());
	if (!Cls) return MCPError(FString::Printf(TEXT("'%s' is not a usable UBTService subclass"), *ClassName));

	const FString DisplayName = OptionalString(Params, TEXT("nodeName"));
	UBTService* Svc = NewObject<UBTService>(BT, Cls, UniqueNodeName(BT, Cls, DisplayName), RF_Transactional);
	FinishNode(Svc, DisplayName);
	Comp->Services.Add(Svc);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("nodeName"), Svc->GetName());
	Result->SetStringField(TEXT("nodeClass"), Cls->GetName());
	Result->SetStringField(TEXT("composite"), Comp->GetName());
	return SaveBTResult(BT, Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::RemoveBTNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (TSharedPtr<FJsonValue> Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString NodeName;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("nodeName"), NodeName)) return Err;

	UBehaviorTree* BT = LoadBT(AssetPath);
	if (!BT) return MCPError(FString::Printf(TEXT("BehaviorTree not found: %s"), *AssetPath));

	UBTNode* Target = FindNode(BT, NodeName);
	if (!Target) return MCPError(FString::Printf(TEXT("Node '%s' not found"), *NodeName));

	bool bRemoved = false;
	if (Target == BT->RootNode)
	{
		BT->RootNode = nullptr;
		bRemoved = true;
	}
	else if (UBTDecorator* AsDec = Cast<UBTDecorator>(Target))
	{
		if (BT->RootDecorators.Remove(AsDec) > 0) bRemoved = true;
		else
		{
			TFunction<bool(UBTCompositeNode*)> Recurse = [&](UBTCompositeNode* Comp) -> bool
			{
				if (!Comp) return false;
				for (FBTCompositeChild& Child : Comp->Children)
				{
					if (Child.Decorators.Remove(AsDec) > 0) return true;
					if (Child.ChildComposite && Recurse(Child.ChildComposite)) return true;
				}
				return false;
			};
			bRemoved = Recurse(BT->RootNode);
		}
	}
	else if (UBTService* AsSvc = Cast<UBTService>(Target))
	{
		TFunction<bool(UBTCompositeNode*)> Recurse = [&](UBTCompositeNode* Comp) -> bool
		{
			if (!Comp) return false;
			if (Comp->Services.Remove(AsSvc) > 0) return true;
			for (const FBTCompositeChild& Child : Comp->Children)
			{
				if (Child.ChildComposite && Recurse(Child.ChildComposite)) return true;
			}
			return false;
		};
		bRemoved = Recurse(BT->RootNode);
	}
	else
	{
		// Composite or task child: drop its slot (removes the subtree).
		UBTCompositeNode* Parent = nullptr;
		int32 Index = INDEX_NONE;
		if (FindParentSlot(BT, Target, Parent, Index))
		{
			Parent->Children.RemoveAt(Index);
			bRemoved = true;
		}
	}

	if (!bRemoved) return MCPError(FString::Printf(TEXT("Could not remove '%s' from the tree"), *NodeName));

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetBoolField(TEXT("removed"), true);
	Result->SetStringField(TEXT("nodeName"), NodeName);
	return SaveBTResult(BT, Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::SetBTNodeProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (TSharedPtr<FJsonValue> Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString NodeName;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("nodeName"), NodeName)) return Err;
	FString PropertyName;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	UBehaviorTree* BT = LoadBT(AssetPath);
	if (!BT) return MCPError(FString::Printf(TEXT("BehaviorTree not found: %s"), *AssetPath));

	UBTNode* Node = FindNode(BT, NodeName);
	if (!Node) return MCPError(FString::Printf(TEXT("Node '%s' not found"), *NodeName));

	const TSharedPtr<FJsonValue> Value = Params->TryGetField(TEXT("value"));
	if (!Value.IsValid()) return MCPError(TEXT("Missing required parameter 'value'"));

	FString SetErr;
	if (!MCPJsonProperty::SetDottedPropertyFromJson(Node, PropertyName, Value, SetErr))
	{
		return MCPError(FString::Printf(TEXT("Failed to set '%s' on '%s': %s"), *PropertyName, *NodeName, *SetErr));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("nodeName"), Node->GetName());
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	return SaveBTResult(BT, Result);
}
