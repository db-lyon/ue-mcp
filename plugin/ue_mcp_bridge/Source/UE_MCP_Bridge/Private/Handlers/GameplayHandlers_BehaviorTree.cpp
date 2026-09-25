// BehaviorTree inspection and node-level authoring.
//
// Split out of GameplayHandlers.cpp when the BT surface grew past "what asset
// is this" into reading node configuration and writing individual owned node
// subobjects (#887 crash fix, #888 decorator config, #919 filtered reads and
// scoped nested writes, #940 BTTask_MoveTo FilterClass).
//
// Everything here reads through UPROPERTY reflection rather than typed casts to
// concrete engine node classes. A project's own decorator that does not declare
// BlackboardKey, or an engine version that renames a field, is then described
// by what it does have instead of failing the whole read.

#include "GameplayHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "HandlerPropertyText.h"

#include "JsonObjectConverter.h"
#include "Modules/ModuleManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/TopLevelAssetPath.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"

namespace
{
	// How deep a node walk goes before it stops. A BehaviorTree asset is a
	// tree, but a corrupted one is still an asset the bridge has to answer
	// about, and a handler must return a result rather than recurse forever.
	constexpr int32 MCPBTMaxDepth = 64;

	// Longest parent chain a Blackboard asset may declare before the walk stops.
	constexpr int32 MCPBTMaxBlackboardParents = 32;

	// How many node paths an ambiguous or empty selector lists back to the
	// caller. Enough to pick from, short enough to read.
	constexpr int32 MCPBTMaxReportedPaths = 40;

	// "BlackboardKeyType_Object" reads as "Object" everywhere a key type is
	// reported. The full class path travels alongside it.
	FString MCPBTShortKeyTypeName(const UClass* KeyTypeClass)
	{
		if (!KeyTypeClass) return FString();
		FString Name = KeyTypeClass->GetName();
		const FString Prefix = TEXT("BlackboardKeyType_");
		if (Name.StartsWith(Prefix)) Name = Name.RightChop(Prefix.Len());
		return Name;
	}

	// One property value as JSON. Enum-valued bytes and enum-class properties
	// are emitted as their enumerator name, which is what a caller reads and
	// writes back; everything else goes through the JSON converter, with the
	// export text as the last resort for a type it cannot express.
	TSharedPtr<FJsonValue> MCPBTPropertyToJson(FProperty* Prop, const void* Addr)
	{
		if (!Prop || !Addr) return nullptr;

		if (const FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			if (UEnum* Enum = ByteProp->Enum)
			{
				const int64 Raw = ByteProp->GetSignedIntPropertyValue(Addr);
				return MakeShared<FJsonValueString>(Enum->GetNameStringByValue(Raw));
			}
		}
		if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			if (UEnum* Enum = EnumProp->GetEnum())
			{
				const int64 Raw = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(Addr);
				return MakeShared<FJsonValueString>(Enum->GetNameStringByValue(Raw));
			}
		}
		if (const FClassProperty* ClassProp = CastField<FClassProperty>(Prop))
		{
			UClass* Value = Cast<UClass>(ClassProp->GetObjectPropertyValue(Addr));
			return MakeShared<FJsonValueString>(Value ? Value->GetPathName() : FString());
		}

		TSharedPtr<FJsonValue> Json = FJsonObjectConverter::UPropertyToJsonValue(Prop, Addr, 0, 0);
		if (Json.IsValid()) return Json;

		FString Text;
		Prop->ExportTextItem_Direct(Text, Addr, nullptr, nullptr, PPF_None);
		return MakeShared<FJsonValueString>(Text);
	}

	// The DefaultValue field of a UE 5.8 FValueOrBBKey_* struct, or null when
	// this is an ordinary struct.
	//
	// #940/#889: 5.8 replaced plain scalars on BT nodes (AcceptableRadius,
	// WaitTime, FilterClass) with a struct pairing a DefaultValue against an
	// optional blackboard Key that overrides it at runtime. A caller reading
	// FilterClass with get_editor_property sees an empty struct, and a caller
	// writing the scalar they see in the editor lands on the struct. Matched
	// structurally, so a project's own subclass of the family is handled too.
	FProperty* MCPBTValueOrBBKeyDefault(const FStructProperty* StructProp)
	{
		if (!StructProp || !StructProp->Struct) return nullptr;
		if (!StructProp->Struct->GetSuperStruct()) return nullptr;
		FProperty* Default = StructProp->Struct->FindPropertyByName(TEXT("DefaultValue"));
		if (!Default) return nullptr;
		if (!CastField<FNameProperty>(StructProp->Struct->FindPropertyByName(TEXT("Key")))) return nullptr;
		return Default;
	}

	// A FValueOrBBKey_* value, unpacked into the two halves that matter: the
	// literal DefaultValue and the blackboard Key that overrides it. The export
	// text travels too, because that is the form a struct round trip needs and
	// the form get_editor_property drops.
	TSharedPtr<FJsonObject> MCPBTDescribeValueOrBBKey(FStructProperty* StructProp, void* Addr)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		if (!StructProp || !StructProp->Struct || !Addr) return Out;

		Out->SetStringField(TEXT("struct"), StructProp->Struct->GetName());

		if (FNameProperty* KeyProp = CastField<FNameProperty>(StructProp->Struct->FindPropertyByName(TEXT("Key"))))
		{
			const FName Key = KeyProp->GetPropertyValue(KeyProp->ContainerPtrToValuePtr<void>(Addr));
			const FString KeyStr = Key.IsNone() ? FString() : Key.ToString();
			Out->SetStringField(TEXT("key"), KeyStr);
			Out->SetBoolField(TEXT("isBound"), !KeyStr.IsEmpty());
		}

		if (FProperty* DefaultProp = StructProp->Struct->FindPropertyByName(TEXT("DefaultValue")))
		{
			void* DefaultAddr = DefaultProp->ContainerPtrToValuePtr<void>(Addr);
			TSharedPtr<FJsonValue> Json = MCPBTPropertyToJson(DefaultProp, DefaultAddr);
			if (Json.IsValid()) Out->SetField(TEXT("defaultValue"), Json);
			else Out->SetField(TEXT("defaultValue"), MakeShared<FJsonValueNull>());

			if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(DefaultProp))
			{
				UObject* Value = ObjProp->GetObjectPropertyValue(DefaultAddr);
				Out->SetStringField(TEXT("defaultValueName"), Value ? Value->GetName() : FString());
			}
		}

		if (FObjectPropertyBase* BaseProp = CastField<FObjectPropertyBase>(StructProp->Struct->FindPropertyByName(TEXT("BaseClass"))))
		{
			UObject* Base = BaseProp->GetObjectPropertyValue(BaseProp->ContainerPtrToValuePtr<void>(Addr));
			Out->SetStringField(TEXT("baseClass"), Base ? Base->GetPathName() : FString());
		}

		FString Text;
		StructProp->ExportTextItem_Direct(Text, Addr, nullptr, nullptr, PPF_None);
		Out->SetStringField(TEXT("text"), Text);
		return Out;
	}

	// Which node kind this object is, in the vocabulary the BT editor uses.
	FString MCPBTKind(const UBTNode* Node)
	{
		if (!Node) return FString();
		if (Node->IsA<UBTCompositeNode>()) return TEXT("composite");
		if (Node->IsA<UBTTaskNode>()) return TEXT("task");
		if (Node->IsA<UBTDecorator>()) return TEXT("decorator");
		if (Node->IsA<UBTService>()) return TEXT("service");
		return TEXT("node");
	}

	// One node in a walked tree, with the structural address a caller passes
	// back to target it: "Root.Children[1].Decorators[0]".
	struct FMCPBTNodeRef
	{
		UBTNode* Node = nullptr;
		FString Path;
		FString ParentPath;
		FString Kind;
	};

	void MCPBTCollectFrom(UBTNode* Node, const FString& Path, const FString& ParentPath,
		TArray<FMCPBTNodeRef>& Out, TSet<UBTNode*>& Seen, int32 Depth)
	{
		if (!Node || Depth > MCPBTMaxDepth) return;

		bool bAlreadySeen = false;
		Seen.Add(Node, &bAlreadySeen);
		if (bAlreadySeen) return;

		FMCPBTNodeRef Ref;
		Ref.Node = Node;
		Ref.Path = Path;
		Ref.ParentPath = ParentPath;
		Ref.Kind = MCPBTKind(Node);
		Out.Add(MoveTemp(Ref));

		if (UBTCompositeNode* Comp = Cast<UBTCompositeNode>(Node))
		{
			for (int32 s = 0; s < Comp->Services.Num(); ++s)
			{
				if (UBTService* Svc = Comp->Services[s])
				{
					MCPBTCollectFrom(Svc, FString::Printf(TEXT("%s.Services[%d]"), *Path, s), Path, Out, Seen, Depth + 1);
				}
			}
			for (int32 c = 0; c < Comp->Children.Num(); ++c)
			{
				const FBTCompositeChild& Child = Comp->Children[c];
				const FString ChildPath = FString::Printf(TEXT("%s.Children[%d]"), *Path, c);
				for (int32 d = 0; d < Child.Decorators.Num(); ++d)
				{
					if (UBTDecorator* Dec = Child.Decorators[d])
					{
						MCPBTCollectFrom(Dec, FString::Printf(TEXT("%s.Decorators[%d]"), *ChildPath, d), ChildPath, Out, Seen, Depth + 1);
					}
				}
				if (Child.ChildComposite)
				{
					MCPBTCollectFrom(Child.ChildComposite, ChildPath, Path, Out, Seen, Depth + 1);
				}
				else if (Child.ChildTask)
				{
					MCPBTCollectFrom(Child.ChildTask, ChildPath, Path, Out, Seen, Depth + 1);
				}
			}
		}
		else if (UBTTaskNode* Task = Cast<UBTTaskNode>(Node))
		{
			for (int32 s = 0; s < Task->Services.Num(); ++s)
			{
				if (UBTService* Svc = Task->Services[s])
				{
					MCPBTCollectFrom(Svc, FString::Printf(TEXT("%s.Services[%d]"), *Path, s), Path, Out, Seen, Depth + 1);
				}
			}
		}
	}

	void MCPBTCollectTree(UBehaviorTree* BT, TArray<FMCPBTNodeRef>& Out)
	{
		if (!BT) return;
		TSet<UBTNode*> Seen;
		for (int32 d = 0; d < BT->RootDecorators.Num(); ++d)
		{
			if (UBTDecorator* Dec = BT->RootDecorators[d])
			{
				MCPBTCollectFrom(Dec, FString::Printf(TEXT("RootDecorators[%d]"), d), FString(), Out, Seen, 0);
			}
		}
		if (BT->RootNode)
		{
			MCPBTCollectFrom(BT->RootNode, TEXT("Root"), FString(), Out, Seen, 0);
		}
	}

	// The property allow-list a caller passed as propertyNames, lowercased so
	// the match ignores case the way FindPropertyByName does.
	TSet<FString> MCPBTPropertyFilter(const TSharedPtr<FJsonObject>& Params)
	{
		TSet<FString> Filter;
		const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
		if (Params.IsValid() && TryGetArrayParam(Params, TEXT("propertyNames"), Names) && Names)
		{
			for (const FString& Name : JsonArrayToStringList(Names))
			{
				const FString Trimmed = Name.TrimStartAndEnd();
				if (!Trimmed.IsEmpty()) Filter.Add(Trimmed.ToLower());
			}
		}
		return Filter;
	}

	// Node selection shared by the filtered read, the task inventory and the
	// scoped write. Every supplied filter has to match.
	struct FMCPBTSelector
	{
		UClass* NodeClass = nullptr;
		FString NodeClassSpec;
		FString NodeNameSpec;
		FString NodePathSpec;
		FString KindSpec;

		bool IsEmpty() const
		{
			return NodeClassSpec.IsEmpty() && NodeNameSpec.IsEmpty() && NodePathSpec.IsEmpty() && KindSpec.IsEmpty();
		}
	};

	FMCPBTSelector MCPBTReadSelector(const TSharedPtr<FJsonObject>& Params)
	{
		FMCPBTSelector Selector;
		Selector.NodeClassSpec = OptionalString(Params, TEXT("nodeClass")).TrimStartAndEnd();
		Selector.NodeNameSpec = OptionalString(Params, TEXT("nodeName")).TrimStartAndEnd();
		Selector.NodePathSpec = OptionalString(Params, TEXT("nodePath")).TrimStartAndEnd();
		Selector.KindSpec = OptionalString(Params, TEXT("kind")).TrimStartAndEnd();
		if (!Selector.NodeClassSpec.IsEmpty())
		{
			Selector.NodeClass = MCPResolveClass(Selector.NodeClassSpec, /*bAllowLoad*/ true);
		}
		return Selector;
	}

	bool MCPBTSelectorMatches(const FMCPBTSelector& Selector, const FMCPBTNodeRef& Ref)
	{
		UBTNode* Node = Ref.Node;
		if (!Node) return false;

		if (!Selector.NodePathSpec.IsEmpty() && !Ref.Path.Equals(Selector.NodePathSpec, ESearchCase::IgnoreCase))
		{
			return false;
		}
		if (!Selector.KindSpec.IsEmpty() && !Ref.Kind.Equals(Selector.KindSpec, ESearchCase::IgnoreCase))
		{
			return false;
		}
		if (!Selector.NodeClassSpec.IsEmpty())
		{
			// A resolved class matches the whole subtree below it, so
			// nodeClass="BTDecorator" selects every decorator. An unresolvable
			// spec falls back to a substring of the class name, which is how a
			// caller pastes a partial name out of the editor.
			const bool bMatches = Selector.NodeClass
				? Node->IsA(Selector.NodeClass)
				: Node->GetClass()->GetName().Contains(Selector.NodeClassSpec);
			if (!bMatches) return false;
		}
		if (!Selector.NodeNameSpec.IsEmpty())
		{
			const FString ObjectName = Node->GetName();
			const FString DisplayName = Node->NodeName;
			const bool bMatches =
				ObjectName.Equals(Selector.NodeNameSpec, ESearchCase::IgnoreCase) ||
				DisplayName.Equals(Selector.NodeNameSpec, ESearchCase::IgnoreCase) ||
				ObjectName.Contains(Selector.NodeNameSpec) ||
				DisplayName.Contains(Selector.NodeNameSpec);
			if (!bMatches) return false;
		}
		return true;
	}

	// The node addresses available on this asset, for an error that has to tell
	// the caller what they could have asked for.
	FString MCPBTDescribePaths(const TArray<FMCPBTNodeRef>& Nodes)
	{
		TArray<FString> Paths;
		for (const FMCPBTNodeRef& Ref : Nodes)
		{
			if (Paths.Num() >= MCPBTMaxReportedPaths) break;
			if (!Ref.Node) continue;
			Paths.Add(FString::Printf(TEXT("%s (%s)"), *Ref.Path, *Ref.Node->GetClass()->GetName()));
		}
		FString Joined = FString::Join(Paths, TEXT(", "));
		if (Nodes.Num() > Paths.Num())
		{
			Joined += FString::Printf(TEXT(", and %d more"), Nodes.Num() - Paths.Num());
		}
		return Joined;
	}

	// Load one BehaviorTree from a caller-supplied path.
	UBehaviorTree* MCPBTLoad(const FString& AssetPath)
	{
		if (UBehaviorTree* Direct = LoadObject<UBehaviorTree>(nullptr, *AssetPath)) return Direct;
		return Cast<UBehaviorTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	}
}

// -----------------------------------------------------------------
// Shared primitives (#889)
// -----------------------------------------------------------------

UBehaviorTree* FGameplayHandlers::LoadBehaviorTree(const FString& AssetPath)
{
	return MCPBTLoad(AssetPath);
}

void FGameplayHandlers::MapBTNodeAddresses(UBehaviorTree* Tree, TMap<UBTNode*, FString>& OutAddresses)
{
	OutAddresses.Reset();
	if (!Tree) return;

	TArray<FMCPBTNodeRef> Nodes;
	MCPBTCollectTree(Tree, Nodes);
	for (const FMCPBTNodeRef& Ref : Nodes)
	{
		if (Ref.Node) OutAddresses.Add(Ref.Node, Ref.Path);
	}
}

// -----------------------------------------------------------------
// Blackboard keys
// -----------------------------------------------------------------

TArray<TSharedPtr<FJsonValue>> FGameplayHandlers::DescribeBlackboardKeys(const UBlackboardData* Blackboard)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	if (!Blackboard) return Out;

	// #887: UBlackboardData::Keys is a TArray<FBlackboardEntry>, a struct
	// array. The previous reader took each element's address, reinterpreted it
	// as a UObject** and dereferenced the result, which is the entry's FName
	// read as a pointer. Every blackboard with at least one key therefore
	// access-violated the editor. FBlackboardEntry is read as the struct it is.
	for (const FBlackboardEntry& Entry : Blackboard->Keys)
	{
		TSharedPtr<FJsonObject> KeyObj = MakeShared<FJsonObject>();
		KeyObj->SetStringField(TEXT("name"), Entry.EntryName.ToString());

		const UBlackboardKeyType* KeyType = Entry.KeyType.Get();
		const UClass* KeyTypeClass = KeyType ? KeyType->GetClass() : nullptr;
		KeyObj->SetStringField(TEXT("type"), MCPBTShortKeyTypeName(KeyTypeClass));
		KeyObj->SetStringField(TEXT("typeClass"), KeyTypeClass ? KeyTypeClass->GetPathName() : FString());
		KeyObj->SetBoolField(TEXT("instanceSynced"), Entry.bInstanceSynced != 0);
		KeyObj->SetStringField(TEXT("owner"), Blackboard->GetPathName());
#if WITH_EDITORONLY_DATA
		KeyObj->SetStringField(TEXT("description"), Entry.EntryDescription);
		KeyObj->SetStringField(TEXT("category"), Entry.EntryCategory.ToString());
#endif
		Out.Add(MakeShared<FJsonValueObject>(KeyObj));
	}
	return Out;
}

// -----------------------------------------------------------------
// Node description (#888 / #919 / #940)
// -----------------------------------------------------------------

TSharedPtr<FJsonObject> FGameplayHandlers::DescribeBTNode(
	UBTNode* Node,
	const FString& NodePath,
	const FString& ParentPath,
	bool bIncludeProperties,
	bool bIncludeInherited,
	const TSet<FString>& PropertyFilter)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	if (!Node) return Obj;

	UClass* NodeClass = Node->GetClass();
	Obj->SetStringField(TEXT("path"), NodePath);
	Obj->SetStringField(TEXT("parentPath"), ParentPath);
	Obj->SetStringField(TEXT("kind"), MCPBTKind(Node));
	Obj->SetStringField(TEXT("class"), NodeClass->GetName());
	Obj->SetStringField(TEXT("classPath"), NodeClass->GetPathName());
	Obj->SetStringField(TEXT("objectName"), Node->GetName());
	// `name` is what read_behavior_tree_graph has always reported for a node.
	Obj->SetStringField(TEXT("name"), Node->GetName());
	Obj->SetStringField(TEXT("nodeName"), Node->NodeName);
	Obj->SetStringField(TEXT("staticDescription"), Node->GetStaticDescription());

	// #888: the runtime configuration of a decorator is what answers "the tree
	// is running but the wrong branch fires". BlackboardKey, the comparison
	// operation, the abort mode and the observer mode are all protected C++
	// fields, so reflection is the only path to them.
	auto AddField = [&Obj, Node, NodeClass](const TCHAR* PropertyName, const TCHAR* JsonKey)
	{
		FProperty* Prop = NodeClass->FindPropertyByName(FName(PropertyName));
		if (!Prop) return;
		TSharedPtr<FJsonValue> Json = MCPBTPropertyToJson(Prop, Prop->ContainerPtrToValuePtr<void>(Node));
		if (Json.IsValid()) Obj->SetField(JsonKey, Json);
	};

	if (FStructProperty* KeyProp = CastField<FStructProperty>(NodeClass->FindPropertyByName(TEXT("BlackboardKey"))))
	{
		void* KeyAddr = KeyProp->ContainerPtrToValuePtr<void>(Node);
		TSharedPtr<FJsonObject> KeyObj = MakeShared<FJsonObject>();
		if (FNameProperty* SelectedName = CastField<FNameProperty>(KeyProp->Struct->FindPropertyByName(TEXT("SelectedKeyName"))))
		{
			const FName Selected = SelectedName->GetPropertyValue(SelectedName->ContainerPtrToValuePtr<void>(KeyAddr));
			KeyObj->SetStringField(TEXT("selectedKeyName"), Selected.IsNone() ? FString() : Selected.ToString());
		}
		if (FObjectPropertyBase* SelectedType = CastField<FObjectPropertyBase>(KeyProp->Struct->FindPropertyByName(TEXT("SelectedKeyType"))))
		{
			const UClass* TypeClass = Cast<UClass>(SelectedType->GetObjectPropertyValue(SelectedType->ContainerPtrToValuePtr<void>(KeyAddr)));
			KeyObj->SetStringField(TEXT("selectedKeyType"), MCPBTShortKeyTypeName(TypeClass));
			KeyObj->SetStringField(TEXT("selectedKeyTypeClass"), TypeClass ? TypeClass->GetPathName() : FString());
		}
		Obj->SetObjectField(TEXT("blackboardKey"), KeyObj);
	}

	if (Node->IsA<UBTDecorator>())
	{
		AddField(TEXT("FlowAbortMode"), TEXT("flowAbortMode"));
		AddField(TEXT("bInverseCondition"), TEXT("inverseCondition"));
		AddField(TEXT("NotifyObserver"), TEXT("notifyObserver"));
		AddField(TEXT("BasicOperation"), TEXT("basicOperation"));
		AddField(TEXT("ArithmeticOperation"), TEXT("arithmeticOperation"));
		AddField(TEXT("TextOperation"), TEXT("textOperation"));
		AddField(TEXT("IntValue"), TEXT("intValue"));
		AddField(TEXT("FloatValue"), TEXT("floatValue"));
		AddField(TEXT("StringValue"), TEXT("stringValue"));
	}

	if (!bIncludeProperties) return Obj;

	// #919: the project-specific UPROPERTYs a custom task or composite carries.
	// Without a filter the dump starts at the node's own class, because
	// everything UBTNode declares (TreeAsset, ParentNode, NodeName) is tree
	// plumbing this record already reports by name.
	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Skipped;
	for (TFieldIterator<FProperty> It(NodeClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop || Prop->HasAnyPropertyFlags(CPF_Deprecated)) continue;

		const FString PropertyName = Prop->GetName();
		if (PropertyFilter.Num() > 0)
		{
			if (!PropertyFilter.Contains(PropertyName.ToLower())) continue;
		}
		else if (!bIncludeInherited)
		{
			UStruct* Owner = Prop->GetOwnerStruct();
			if (!Owner || UBTNode::StaticClass()->IsChildOf(Owner)) continue;
		}

		void* Addr = Prop->ContainerPtrToValuePtr<void>(Node);
		if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			if (MCPBTValueOrBBKeyDefault(StructProp))
			{
				Properties->SetObjectField(PropertyName, MCPBTDescribeValueOrBBKey(StructProp, Addr));
				continue;
			}
		}

		TSharedPtr<FJsonValue> Json = MCPBTPropertyToJson(Prop, Addr);
		if (Json.IsValid()) Properties->SetField(PropertyName, Json);
		else Skipped.Add(MakeShared<FJsonValueString>(PropertyName));
	}
	Obj->SetObjectField(TEXT("properties"), Properties);
	if (Skipped.Num() > 0) Obj->SetArrayField(TEXT("skippedProperties"), Skipped);
	return Obj;
}

// -----------------------------------------------------------------
// Node property read / write (#919 / #940)
// -----------------------------------------------------------------

TSharedPtr<FJsonValue> FGameplayHandlers::ReadBTNodeProperty(UBTNode* Node, const FString& PropertyPath, FString& OutError)
{
	if (!Node) { OutError = TEXT("null node"); return nullptr; }
	if (PropertyPath.IsEmpty()) { OutError = TEXT("empty property path"); return nullptr; }

	FProperty* Prop = nullptr;
	void* Addr = nullptr;
	UObject* LeafOwner = nullptr;
	if (!MCPJsonProperty::ResolveDottedPath(Node, PropertyPath, Prop, Addr, LeafOwner, OutError))
	{
		return nullptr;
	}

	if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
	{
		if (MCPBTValueOrBBKeyDefault(StructProp))
		{
			return MakeShared<FJsonValueObject>(MCPBTDescribeValueOrBBKey(StructProp, Addr));
		}
	}

	TSharedPtr<FJsonValue> Json = MCPBTPropertyToJson(Prop, Addr);
	if (!Json.IsValid())
	{
		OutError = FString::Printf(TEXT("property '%s' has no JSON representation"), *PropertyPath);
	}
	return Json;
}

bool FGameplayHandlers::WriteBTNodeProperty(UBTNode* Node, const FString& PropertyPath, const TSharedPtr<FJsonValue>& Value, FString& OutError)
{
	if (!Node) { OutError = TEXT("null node"); return false; }
	if (PropertyPath.IsEmpty()) { OutError = TEXT("empty property path"); return false; }
	if (!Value.IsValid()) { OutError = TEXT("null value"); return false; }

	FProperty* Prop = nullptr;
	void* Addr = nullptr;
	UObject* LeafOwner = nullptr;
	if (!MCPJsonProperty::ResolveDottedPath(Node, PropertyPath, Prop, Addr, LeafOwner, OutError))
	{
		return false;
	}

	// #940/#889: a scalar aimed at a FValueOrBBKey_* field is the literal the
	// editor shows, so it belongs on DefaultValue. An object value still writes
	// the struct's own fields, which is how {"Key": "AcceptRadius"} binds the
	// field to a blackboard key instead of pinning a literal.
	if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
	{
		if (Value->Type != EJson::Object)
		{
			if (FProperty* DefaultProp = MCPBTValueOrBBKeyDefault(StructProp))
			{
				Addr = DefaultProp->ContainerPtrToValuePtr<void>(Addr);
				Prop = DefaultProp;
			}
		}
	}

	// A class-valued leaf is resolved here rather than delegated, so a short
	// class name, a /Script path and a Blueprint asset path all land, and so
	// the class is checked against what the field will accept before it is
	// written.
	FObjectProperty* ObjLeaf = CastField<FObjectProperty>(Prop);
	if (ObjLeaf && ObjLeaf->PropertyClass && ObjLeaf->PropertyClass->IsChildOf(UClass::StaticClass()))
	{
		FString Spec;
		const bool bIsString = Value->TryGetString(Spec);
		if (Value->Type == EJson::Null || (bIsString && (Spec.TrimStartAndEnd().IsEmpty() || Spec == TEXT("None"))))
		{
			ObjLeaf->SetObjectPropertyValue(Addr, nullptr);
			return true;
		}
		if (bIsString)
		{
			UClass* Resolved = MCPResolveClass(Spec.TrimStartAndEnd(), /*bAllowLoad*/ true);
			if (!Resolved)
			{
				OutError = FString::Printf(TEXT("class not found: %s"), *Spec);
				return false;
			}
			if (FClassProperty* ClassLeaf = CastField<FClassProperty>(Prop))
			{
				if (ClassLeaf->MetaClass && !Resolved->IsChildOf(ClassLeaf->MetaClass))
				{
					OutError = FString::Printf(
						TEXT("%s does not derive from %s, which '%s' requires"),
						*Resolved->GetPathName(), *ClassLeaf->MetaClass->GetName(), *PropertyPath);
					return false;
				}
			}
			ObjLeaf->SetObjectPropertyValue(Addr, Resolved);
			return true;
		}
	}

	return MCPJsonProperty::SetJsonOnProperty(Prop, Addr, Value, OutError);
}

// -----------------------------------------------------------------
// get_behavior_tree_info (#887)
// -----------------------------------------------------------------

TSharedPtr<FJsonValue> FGameplayHandlers::GetBehaviorTreeInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("BehaviorTree not found: %s"), *AssetPath));
	}

	UBehaviorTree* BT = Cast<UBehaviorTree>(Asset);
	if (!BT)
	{
		return MCPError(FString::Printf(
			TEXT("%s is a %s. get_behavior_tree_info reads a BehaviorTree asset - use asset(read_properties) for other types."),
			*AssetPath, *Asset->GetClass()->GetName()));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("name"), BT->GetName());
	Result->SetStringField(TEXT("className"), BT->GetClass()->GetName());

	UBlackboardData* Blackboard = BT->BlackboardAsset;
	if (Blackboard)
	{
		Result->SetStringField(TEXT("blackboardAsset"), Blackboard->GetPathName());
		Result->SetArrayField(TEXT("blackboardKeys"), DescribeBlackboardKeys(Blackboard));

		// The parent chain contributes keys the tree can bind to just as well
		// as its own, and the seen set keeps a mis-authored cycle finite.
		TArray<TSharedPtr<FJsonValue>> Inherited;
		TSet<UBlackboardData*> Visited;
		Visited.Add(Blackboard);
		UBlackboardData* Parent = Blackboard->Parent;
		for (int32 Guard = 0; Parent && Guard < MCPBTMaxBlackboardParents; ++Guard)
		{
			bool bAlreadySeen = false;
			Visited.Add(Parent, &bAlreadySeen);
			if (bAlreadySeen) break;
			Inherited.Append(DescribeBlackboardKeys(Parent));
			Parent = Parent->Parent;
		}
		Result->SetArrayField(TEXT("inheritedBlackboardKeys"), Inherited);
	}
	else
	{
		Result->SetArrayField(TEXT("blackboardKeys"), TArray<TSharedPtr<FJsonValue>>());
	}

	if (BT->RootNode)
	{
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("path"), TEXT("Root"));
		Root->SetStringField(TEXT("class"), BT->RootNode->GetClass()->GetName());
		Root->SetStringField(TEXT("objectName"), BT->RootNode->GetName());
		Root->SetStringField(TEXT("nodeName"), BT->RootNode->NodeName);
		Result->SetObjectField(TEXT("rootNode"), Root);
	}

	TArray<FMCPBTNodeRef> Nodes;
	MCPBTCollectTree(BT, Nodes);
	Result->SetNumberField(TEXT("nodeCount"), Nodes.Num());
	Result->SetNumberField(TEXT("rootDecoratorCount"), BT->RootDecorators.Num());
	return MCPResult(Result);
}

// -----------------------------------------------------------------
// read_behavior_tree_graph (#124, decorator config added for #888)
// -----------------------------------------------------------------

TSharedPtr<FJsonValue> FGameplayHandlers::ReadBehaviorTreeGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UBehaviorTree* BT = MCPBTLoad(AssetPath);
	if (!BT) return MCPError(FString::Printf(TEXT("BehaviorTree not found: %s"), *AssetPath));

	const bool bIncludeProperties = OptionalBool(Params, TEXT("includeProperties"), false);
	const bool bIncludeInherited = OptionalBool(Params, TEXT("includeInherited"), false);
	const TSet<FString> Filter = MCPBTPropertyFilter(Params);

	int32 DecoratorCount = 0;
	int32 ServiceCount = 0;
	TSet<UBTNode*> Seen;

	auto Describe = [&](UBTNode* Node, const FString& Path, const FString& ParentPath)
	{
		return DescribeBTNode(Node, Path, ParentPath, bIncludeProperties, bIncludeInherited, Filter);
	};

	TFunction<TSharedPtr<FJsonObject>(UBTNode*, const FString&, const FString&, int32)> Walk;
	Walk = [&](UBTNode* Node, const FString& Path, const FString& ParentPath, int32 Depth) -> TSharedPtr<FJsonObject>
	{
		if (!Node || Depth > MCPBTMaxDepth) return nullptr;
		bool bAlreadySeen = false;
		Seen.Add(Node, &bAlreadySeen);
		if (bAlreadySeen) return nullptr;

		TSharedPtr<FJsonObject> NObj = Describe(Node, Path, ParentPath);

		if (UBTCompositeNode* Comp = Cast<UBTCompositeNode>(Node))
		{
			TArray<TSharedPtr<FJsonValue>> ChildrenArr;
			for (int32 c = 0; c < Comp->Children.Num(); ++c)
			{
				const FBTCompositeChild& Child = Comp->Children[c];
				const FString ChildPath = FString::Printf(TEXT("%s.Children[%d]"), *Path, c);

				TSharedPtr<FJsonObject> ChildEntry = MakeShared<FJsonObject>();
				ChildEntry->SetStringField(TEXT("path"), ChildPath);
				if (Child.ChildComposite)
				{
					if (TSharedPtr<FJsonObject> Sub = Walk(Child.ChildComposite, ChildPath, Path, Depth + 1))
					{
						ChildEntry->SetObjectField(TEXT("child"), Sub);
					}
				}
				else if (Child.ChildTask)
				{
					if (TSharedPtr<FJsonObject> Sub = Walk(Child.ChildTask, ChildPath, Path, Depth + 1))
					{
						ChildEntry->SetObjectField(TEXT("child"), Sub);
					}
				}

				TArray<TSharedPtr<FJsonValue>> Decorators;
				for (int32 d = 0; d < Child.Decorators.Num(); ++d)
				{
					UBTDecorator* Dec = Child.Decorators[d];
					if (!Dec) continue;
					const FString DecoratorPath = FString::Printf(TEXT("%s.Decorators[%d]"), *ChildPath, d);
					Decorators.Add(MakeShared<FJsonValueObject>(Describe(Dec, DecoratorPath, ChildPath)));
					++DecoratorCount;
				}
				ChildEntry->SetArrayField(TEXT("decorators"), Decorators);
				ChildrenArr.Add(MakeShared<FJsonValueObject>(ChildEntry));
			}
			NObj->SetArrayField(TEXT("children"), ChildrenArr);

			TArray<TSharedPtr<FJsonValue>> Services;
			for (int32 s = 0; s < Comp->Services.Num(); ++s)
			{
				UBTService* Svc = Comp->Services[s];
				if (!Svc) continue;
				Services.Add(MakeShared<FJsonValueObject>(Describe(Svc, FString::Printf(TEXT("%s.Services[%d]"), *Path, s), Path)));
				++ServiceCount;
			}
			NObj->SetArrayField(TEXT("services"), Services);
		}
		else if (UBTTaskNode* Task = Cast<UBTTaskNode>(Node))
		{
			TArray<TSharedPtr<FJsonValue>> Services;
			for (int32 s = 0; s < Task->Services.Num(); ++s)
			{
				UBTService* Svc = Task->Services[s];
				if (!Svc) continue;
				Services.Add(MakeShared<FJsonValueObject>(Describe(Svc, FString::Printf(TEXT("%s.Services[%d]"), *Path, s), Path)));
				++ServiceCount;
			}
			NObj->SetArrayField(TEXT("services"), Services);
		}
		return NObj;
	};

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), BT->GetName());
	if (BT->BlackboardAsset)
	{
		Result->SetStringField(TEXT("blackboardAsset"), BT->BlackboardAsset->GetPathName());
		Result->SetArrayField(TEXT("blackboardKeys"), DescribeBlackboardKeys(BT->BlackboardAsset));
	}

	TArray<TSharedPtr<FJsonValue>> RootDecorators;
	for (int32 d = 0; d < BT->RootDecorators.Num(); ++d)
	{
		UBTDecorator* Dec = BT->RootDecorators[d];
		if (!Dec) continue;
		RootDecorators.Add(MakeShared<FJsonValueObject>(
			Describe(Dec, FString::Printf(TEXT("RootDecorators[%d]"), d), FString())));
		++DecoratorCount;
	}
	Result->SetArrayField(TEXT("rootDecorators"), RootDecorators);

	if (BT->RootNode)
	{
		if (TSharedPtr<FJsonObject> Root = Walk(BT->RootNode, TEXT("Root"), FString(), 0))
		{
			Result->SetObjectField(TEXT("root"), Root);
		}
	}

	Result->SetNumberField(TEXT("decoratorCount"), DecoratorCount);
	Result->SetNumberField(TEXT("serviceCount"), ServiceCount);
	return MCPResult(Result);
}

// -----------------------------------------------------------------
// read_bt_node_properties (#919)
// -----------------------------------------------------------------

TSharedPtr<FJsonValue> FGameplayHandlers::ReadBTNodeProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UBehaviorTree* BT = MCPBTLoad(AssetPath);
	if (!BT) return MCPError(FString::Printf(TEXT("BehaviorTree not found: %s"), *AssetPath));

	TArray<FMCPBTNodeRef> Nodes;
	MCPBTCollectTree(BT, Nodes);

	const FMCPBTSelector Selector = MCPBTReadSelector(Params);
	const bool bIncludeInherited = OptionalBool(Params, TEXT("includeInherited"), false);
	const TSet<FString> Filter = MCPBTPropertyFilter(Params);

	TArray<TSharedPtr<FJsonValue>> Out;
	for (const FMCPBTNodeRef& Ref : Nodes)
	{
		if (!MCPBTSelectorMatches(Selector, Ref)) continue;
		Out.Add(MakeShared<FJsonValueObject>(
			DescribeBTNode(Ref.Node, Ref.Path, Ref.ParentPath, /*bIncludeProperties*/ true, bIncludeInherited, Filter)));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("nodeCount"), Nodes.Num());
	Result->SetNumberField(TEXT("matchCount"), Out.Num());
	Result->SetArrayField(TEXT("nodes"), Out);
	if (Out.Num() == 0 && Nodes.Num() > 0)
	{
		Result->SetStringField(TEXT("availableNodes"), MCPBTDescribePaths(Nodes));
	}
	return MCPResult(Result);
}

// -----------------------------------------------------------------
// list_bt_tasks (#940)
// -----------------------------------------------------------------

TSharedPtr<FJsonValue> FGameplayHandlers::ListBTTasks(const TSharedPtr<FJsonObject>& Params)
{
	const FString AssetPath = OptionalString(Params, TEXT("assetPath")).TrimStartAndEnd();
	FString Directory = OptionalString(Params, TEXT("directory")).TrimStartAndEnd();
	const bool bRecursive = OptionalBool(Params, TEXT("recursive"), true);
	const bool bFilterClassOnly = OptionalBool(Params, TEXT("filterClassOnly"), false);
	const int32 Limit = FMath::Clamp(OptionalInt(Params, TEXT("limit"), 200), 1, 5000);

	TArray<UBehaviorTree*> Trees;
	TArray<TSharedPtr<FJsonValue>> Unloadable;

	if (!AssetPath.IsEmpty())
	{
		UBehaviorTree* BT = MCPBTLoad(AssetPath);
		if (!BT) return MCPError(FString::Printf(TEXT("BehaviorTree not found: %s"), *AssetPath));
		Trees.Add(BT);
	}
	else
	{
		while (Directory.EndsWith(TEXT("/"))) Directory.LeftChopInline(1);

		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> Assets;
		AR.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/AIModule"), TEXT("BehaviorTree")), Assets, true);

		for (const FAssetData& Data : Assets)
		{
			if (Trees.Num() >= Limit) break;
			if (!Directory.IsEmpty())
			{
				const FString PackagePath = Data.PackagePath.ToString();
				const bool bInScope = bRecursive
					? (PackagePath == Directory || PackagePath.StartsWith(Directory + TEXT("/")))
					: (PackagePath == Directory);
				if (!bInScope) continue;
			}
			if (UBehaviorTree* BT = Cast<UBehaviorTree>(Data.GetAsset()))
			{
				Trees.Add(BT);
			}
			else
			{
				Unloadable.Add(MakeShared<FJsonValueString>(Data.GetObjectPathString()));
			}
		}
	}

	const FMCPBTSelector Selector = MCPBTReadSelector(Params);
	const FString TaskClassSpec = OptionalString(Params, TEXT("taskClass")).TrimStartAndEnd();
	UClass* TaskClass = TaskClassSpec.IsEmpty() ? nullptr : MCPResolveClass(TaskClassSpec, /*bAllowLoad*/ true);

	TArray<TSharedPtr<FJsonValue>> Out;
	for (UBehaviorTree* BT : Trees)
	{
		TArray<FMCPBTNodeRef> Nodes;
		MCPBTCollectTree(BT, Nodes);
		for (const FMCPBTNodeRef& Ref : Nodes)
		{
			if (Ref.Kind != TEXT("task")) continue;
			if (!MCPBTSelectorMatches(Selector, Ref)) continue;
			if (!TaskClassSpec.IsEmpty())
			{
				const bool bMatches = TaskClass
					? Ref.Node->IsA(TaskClass)
					: Ref.Node->GetClass()->GetName().Contains(TaskClassSpec);
				if (!bMatches) continue;
			}

			UBTNode* Node = Ref.Node;
			UClass* NodeClass = Node->GetClass();

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("assetPath"), BT->GetPathName());
			Entry->SetStringField(TEXT("path"), Ref.Path);
			Entry->SetStringField(TEXT("parentPath"), Ref.ParentPath);
			Entry->SetStringField(TEXT("class"), NodeClass->GetName());
			Entry->SetStringField(TEXT("classPath"), NodeClass->GetPathName());
			Entry->SetStringField(TEXT("objectName"), Node->GetName());
			Entry->SetStringField(TEXT("name"), Node->GetName());
			Entry->SetStringField(TEXT("nodeName"), Node->NodeName);
			Entry->SetStringField(TEXT("staticDescription"), Node->GetStaticDescription());

			// #940: on UE 5.8 FilterClass is a FValueOrBBKey_Class, so the
			// literal lives in DefaultValue and a blackboard binding in Key.
			// Earlier engines declare a bare TSubclassOf, which is read here
			// into the same shape so a caller writes one thing either way.
			bool bHasFilterClass = false;
			FProperty* FilterProp = NodeClass->FindPropertyByName(TEXT("FilterClass"));
			if (FStructProperty* FilterStruct = CastField<FStructProperty>(FilterProp))
			{
				if (MCPBTValueOrBBKeyDefault(FilterStruct))
				{
					Entry->SetObjectField(TEXT("filterClass"),
						MCPBTDescribeValueOrBBKey(FilterStruct, FilterStruct->ContainerPtrToValuePtr<void>(Node)));
					bHasFilterClass = true;
				}
			}
			else if (FObjectPropertyBase* FilterObject = CastField<FObjectPropertyBase>(FilterProp))
			{
				UObject* Value = FilterObject->GetObjectPropertyValue(FilterObject->ContainerPtrToValuePtr<void>(Node));
				TSharedPtr<FJsonObject> FilterJson = MakeShared<FJsonObject>();
				FilterJson->SetStringField(TEXT("struct"), FString());
				FilterJson->SetStringField(TEXT("key"), FString());
				FilterJson->SetBoolField(TEXT("isBound"), false);
				FilterJson->SetStringField(TEXT("defaultValue"), Value ? Value->GetPathName() : FString());
				FilterJson->SetStringField(TEXT("defaultValueName"), Value ? Value->GetName() : FString());
				Entry->SetObjectField(TEXT("filterClass"), FilterJson);
				bHasFilterClass = true;
			}
			Entry->SetBoolField(TEXT("hasFilterClass"), bHasFilterClass);

			if (bFilterClassOnly && !bHasFilterClass) continue;
			Out.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("assetCount"), Trees.Num());
	Result->SetNumberField(TEXT("taskCount"), Out.Num());
	Result->SetArrayField(TEXT("tasks"), Out);
	if (Unloadable.Num() > 0) Result->SetArrayField(TEXT("unloadableAssets"), Unloadable);
	return MCPResult(Result);
}

// -----------------------------------------------------------------
// set_bt_node_property / set_bt_task_property (#919 / #940)
// -----------------------------------------------------------------

TSharedPtr<FJsonValue> FGameplayHandlers::SetBTNodeProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UBehaviorTree* BT = MCPBTLoad(AssetPath);
	if (!BT) return MCPError(FString::Printf(TEXT("BehaviorTree not found: %s"), *AssetPath));

	TArray<FMCPBTNodeRef> Nodes;
	MCPBTCollectTree(BT, Nodes);
	if (Nodes.Num() == 0)
	{
		return MCPError(FString::Printf(TEXT("%s has no nodes to write to."), *AssetPath));
	}

	const FMCPBTSelector Selector = MCPBTReadSelector(Params);
	if (Selector.IsEmpty())
	{
		return MCPError(FString::Printf(
			TEXT("Pass nodePath, nodeName, nodeClass or kind to pick the node to write. Available: %s"),
			*MCPBTDescribePaths(Nodes)));
	}

	TArray<FMCPBTNodeRef> Matches;
	for (const FMCPBTNodeRef& Ref : Nodes)
	{
		if (MCPBTSelectorMatches(Selector, Ref)) Matches.Add(Ref);
	}
	if (Matches.Num() == 0)
	{
		return MCPError(FString::Printf(
			TEXT("No BT node matched. Available: %s"), *MCPBTDescribePaths(Nodes)));
	}
	if (Matches.Num() > 1)
	{
		return MCPError(FString::Printf(
			TEXT("%d BT nodes matched; the write targets exactly one. Narrow with nodePath. Matched: %s"),
			Matches.Num(), *MCPBTDescribePaths(Matches)));
	}

	UBTNode* Node = Matches[0].Node;

	// Gather the writes: a properties map of path to value, a single
	// property + value pair, or both.
	TArray<TPair<FString, TSharedPtr<FJsonValue>>> Writes;
	const TSharedPtr<FJsonObject>* PropertiesObj = nullptr;
	if (TryGetObjectParam(Params, TEXT("properties"), PropertiesObj) && PropertiesObj && (*PropertiesObj).IsValid())
	{
		for (const auto& Pair : (*PropertiesObj)->Values)
		{
			Writes.Emplace(Pair.Key, Pair.Value);
		}
	}
	const FString SingleProperty = OptionalString(Params, TEXT("property")).TrimStartAndEnd();
	if (!SingleProperty.IsEmpty())
	{
		TSharedPtr<FJsonValue> SingleValue = TryGetParam(Params, TEXT("value"));
		if (!SingleValue.IsValid())
		{
			return MCPError(TEXT("'property' needs a 'value' alongside it (pass null to clear an object reference)."));
		}
		Writes.Emplace(SingleProperty, SingleValue);
	}
	if (Writes.Num() == 0)
	{
		return MCPError(TEXT("Pass 'property' + 'value', or a 'properties' map of property path to value."));
	}

	// Snapshot every target before touching anything: the export text restores
	// the value if a later write in the same call fails, and the JSON form is
	// what the caller sees as previousValue and what rollback replays.
	struct FMCPBTWriteSnapshot
	{
		FString PropertyPath;
		FString PreviousText;
		TSharedPtr<FJsonValue> PreviousJson;
		bool bValueOrBBKey = false;
	};
	TArray<FMCPBTWriteSnapshot> Snapshots;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Write : Writes)
	{
		FProperty* Prop = nullptr;
		void* Addr = nullptr;
		UObject* LeafOwner = nullptr;
		FString ResolveError;
		if (!MCPJsonProperty::ResolveDottedPath(Node, Write.Key, Prop, Addr, LeafOwner, ResolveError))
		{
			return MCPError(FString::Printf(
				TEXT("%s on %s (%s): %s"),
				*Write.Key, *Matches[0].Path, *Node->GetClass()->GetName(), *ResolveError));
		}

		FMCPBTWriteSnapshot Snapshot;
		Snapshot.PropertyPath = Write.Key;
		Prop->ExportTextItem_Direct(Snapshot.PreviousText, Addr, nullptr, Node, PPF_None);
		FString ReadError;
		Snapshot.PreviousJson = ReadBTNodeProperty(Node, Write.Key, ReadError);
		if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			Snapshot.bValueOrBBKey = MCPBTValueOrBBKeyDefault(StructProp) != nullptr;
		}
		Snapshots.Add(MoveTemp(Snapshot));
	}

	BT->Modify();
	Node->Modify();

	for (int32 i = 0; i < Writes.Num(); ++i)
	{
		FString WriteError;
		if (WriteBTNodeProperty(Node, Writes[i].Key, Writes[i].Value, WriteError))
		{
			continue;
		}

		// Put back every write this call already made. A partially applied
		// batch is worse than a rejected one: the caller would have to guess
		// which half landed.
		for (int32 j = i - 1; j >= 0; --j)
		{
			FProperty* Prop = nullptr;
			void* Addr = nullptr;
			UObject* LeafOwner = nullptr;
			FString ResolveError;
			if (MCPJsonProperty::ResolveDottedPath(Node, Snapshots[j].PropertyPath, Prop, Addr, LeafOwner, ResolveError))
			{
				FString RestoreError;
				MCPPropertyText::ImportTextIntoProperty(Prop, Addr, Snapshots[j].PreviousText, Node, RestoreError);
			}
		}
		return MCPError(FString::Printf(
			TEXT("%s on %s (%s): %s"),
			*Writes[i].Key, *Matches[0].Path, *Node->GetClass()->GetName(), *WriteError));
	}

	Node->PostEditChange();
	BT->PostEditChange();
	const bool bSaved = SaveAssetPackage(BT);

	// #940 asks for the write to be proved rather than asserted: every target
	// is read back off the node and reported next to what it held before.
	TArray<TSharedPtr<FJsonValue>> Applied;
	TSharedPtr<FJsonObject> RollbackProperties = MakeShared<FJsonObject>();
	for (const FMCPBTWriteSnapshot& Snapshot : Snapshots)
	{
		FString ReadError;
		TSharedPtr<FJsonValue> Current = ReadBTNodeProperty(Node, Snapshot.PropertyPath, ReadError);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("property"), Snapshot.PropertyPath);
		if (Snapshot.PreviousJson.IsValid()) Entry->SetField(TEXT("previousValue"), Snapshot.PreviousJson);
		if (Current.IsValid()) Entry->SetField(TEXT("value"), Current);
		else if (!ReadError.IsEmpty()) Entry->SetStringField(TEXT("readBackError"), ReadError);
		Applied.Add(MakeShared<FJsonValueObject>(Entry));

		// A FValueOrBBKey_* previous value is a description, not something the
		// setter accepts back, so rollback names its two halves directly.
		if (Snapshot.bValueOrBBKey && Snapshot.PreviousJson.IsValid() && Snapshot.PreviousJson->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Previous = Snapshot.PreviousJson->AsObject();
			if (const TSharedPtr<FJsonValue> DefaultValue = Previous->TryGetField(TEXT("defaultValue")))
			{
				RollbackProperties->SetField(Snapshot.PropertyPath + TEXT(".DefaultValue"), DefaultValue);
			}
			FString PreviousKey;
			Previous->TryGetStringField(TEXT("key"), PreviousKey);
			RollbackProperties->SetStringField(Snapshot.PropertyPath + TEXT(".Key"),
				PreviousKey.IsEmpty() ? FString(TEXT("None")) : PreviousKey);
		}
		else if (Snapshot.PreviousJson.IsValid())
		{
			RollbackProperties->SetField(Snapshot.PropertyPath, Snapshot.PreviousJson);
		}
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("nodePath"), Matches[0].Path);
	Result->SetStringField(TEXT("nodeClass"), Node->GetClass()->GetName());
	Result->SetStringField(TEXT("objectName"), Node->GetName());
	Result->SetNumberField(TEXT("appliedCount"), Applied.Num());
	Result->SetArrayField(TEXT("applied"), Applied);
	Result->SetBoolField(TEXT("saved"), bSaved);

	if (RollbackProperties->Values.Num() > 0)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("nodePath"), Matches[0].Path);
		Payload->SetObjectField(TEXT("properties"), RollbackProperties);
		MCPSetRollback(Result, TEXT("set_bt_node_property"), Payload);
	}
	return MCPResult(Result);
}
