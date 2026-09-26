// Translation-unit partition of FBlueprintHandlers, like the other
// BlueprintHandlers_*.cpp files. Registration stays in BlueprintHandlers.cpp.
//
// #1166: export_blueprint_batch writes many Blueprints to disk in one call,
// and audit_blueprint_dead_code reports unused and unreachable Blueprint logic
// across a directory. Both select Blueprints the same way: assetPaths, or the
// Asset Registry over a directory.

#include "BlueprintHandlers.h"
#include "BlueprintHandlers_Internal.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/MemberReference.h"
#include "Blueprint/BlueprintSupport.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_BaseMCDelegate.h"
#include "K2Node_ActorBoundEvent.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_FunctionTerminator.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Tunnel.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace MCPBlueprintBatch
{
	constexpr int32 DefaultMaxAssets = 200;
	constexpr int32 MaxMaxAssets = 5000;
	constexpr int32 DefaultAuditMaxBlueprints = 2000;
	constexpr int32 MaxAuditMaxBlueprints = 20000;
	constexpr int32 DefaultMaxSamples = 20;
	constexpr int32 MaxMaxSamples = 500;
	constexpr int32 DefaultAuditRowLimit = 200;
	constexpr int32 MaxAuditRowLimit = 5000;

	struct FTarget
	{
		FString ObjectPath;
		FString PackageName;
		bool bWorld = false;
	};

	struct FSelection
	{
		TArray<FTarget> Targets;
		FString Directory;
		bool bFromAssetPaths = false;
		bool bRecursive = true;
		int32 BlueprintsInDirectory = 0;
		int32 WorldsInDirectory = 0;
		int32 SkippedByClass = 0;
		bool bWaitedForRegistry = false;
	};

	IAssetRegistry& GetRegistry(bool& bOutWaited)
	{
		IAssetRegistry& Registry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		// A sweep during the initial scan would miss every asset not reached yet.
		bOutWaited = Registry.IsLoadingAssets();
		if (bOutWaited) Registry.WaitForCompletion();
		return Registry;
	}

	/** False only when the registry proves the Blueprint cannot derive from
	 *  FilterClass: a native filter and a native parent outside it. */
	bool MayDeriveFrom(const FAssetData& Asset, UClass* FilterClass)
	{
		if (!FilterClass || !FilterClass->HasAnyClassFlags(CLASS_Native)) return true;
		FString Tag;
		if (!Asset.GetTagValue(FBlueprintTags::NativeParentClassPath, Tag) || Tag.IsEmpty()) return true;
		const FString ClassPath = FPackageName::ExportTextPathToObjectPath(Tag);
		UClass* NativeParent = FindObject<UClass>(nullptr, *ClassPath);
		return !NativeParent || NativeParent->IsChildOf(FilterClass);
	}

	bool DerivesFrom(const UBlueprint* Blueprint, UClass* FilterClass)
	{
		if (!FilterClass) return true;
		if (Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(FilterClass)) return true;
		return Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(FilterClass);
	}

	/** The selection parameters, read before anything can fail (#1057). */
	struct FSelectionRequest
	{
		bool bRecursive = true;
		const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
		FString Directory;
	};

	FSelectionRequest ReadSelectionRequest(const TSharedPtr<FJsonObject>& Params)
	{
		FSelectionRequest Request;
		Request.bRecursive = OptionalBool(Params, TEXT("recursive"), true);
		TryGetArrayParam(Params, TEXT("assetPaths"), Request.Paths);
		Request.Directory = OptionalString(Params, TEXT("directory"), TEXT("/Game"));
		return Request;
	}

	/** assetPaths when given, otherwise the registry over directory. Returns an
	 *  error response, or nullptr. */
	TSharedPtr<FJsonValue> ReadSelection(
		const FSelectionRequest& Request, bool bIncludeLevelScripts, UClass* FilterClass, FSelection& Out)
	{
		Out.bRecursive = Request.bRecursive;
		const TArray<TSharedPtr<FJsonValue>>* Paths = Request.Paths;
		if (Paths && Paths->Num() > 0)
		{
			Out.bFromAssetPaths = true;
			TSet<FString> Seen;
			for (const TSharedPtr<FJsonValue>& Value : *Paths)
			{
				FString Path;
				if (!Value.IsValid() || !Value->TryGetString(Path)) continue;
				Path.TrimStartAndEndInline();
				if (Path.IsEmpty() || Seen.Contains(Path)) continue;
				Seen.Add(Path);
				FTarget Target;
				Target.ObjectPath = Path;
				Target.PackageName = FPackageName::ObjectPathToPackageName(Path);
				Out.Targets.Add(Target);
			}
			if (Out.Targets.Num() == 0)
			{
				return MCPError(TEXT("'assetPaths' holds no usable path"));
			}
			return nullptr;
		}

		FString Directory = Request.Directory;
		Directory.TrimStartAndEndInline();
		if (Directory.IsEmpty()) Directory = TEXT("/Game");
		while (Directory.Len() > 1 && Directory.EndsWith(TEXT("/"))) Directory.LeftChopInline(1);
		if (!Directory.StartsWith(TEXT("/")))
		{
			return MCPError(FString::Printf(
				TEXT("'directory' must be a mount-rooted content path such as /Game or /Game/AI, got '%s'"), *Directory));
		}
		Out.Directory = Directory;

		IAssetRegistry& Registry = GetRegistry(Out.bWaitedForRegistry);
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*Directory));
		Filter.bRecursivePaths = Out.bRecursive;
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		TArray<FAssetData> Blueprints;
		Registry.GetAssets(Filter, Blueprints);
		Out.BlueprintsInDirectory = Blueprints.Num();
		for (const FAssetData& Asset : Blueprints)
		{
			if (!MayDeriveFrom(Asset, FilterClass))
			{
				++Out.SkippedByClass;
				continue;
			}
			FTarget Target;
			Target.ObjectPath = Asset.GetObjectPathString();
			Target.PackageName = Asset.PackageName.ToString();
			Out.Targets.Add(Target);
		}

		// A level script lives inside its map package, so the Blueprint filter
		// never lists one (#942).
		if (bIncludeLevelScripts)
		{
			FARFilter WorldFilter;
			WorldFilter.PackagePaths.Add(FName(*Directory));
			WorldFilter.bRecursivePaths = Out.bRecursive;
			WorldFilter.ClassPaths.Add(UWorld::StaticClass()->GetClassPathName());
			TArray<FAssetData> Worlds;
			Registry.GetAssets(WorldFilter, Worlds);
			Out.WorldsInDirectory = Worlds.Num();
			for (const FAssetData& World : Worlds)
			{
				FTarget Target;
				Target.ObjectPath = World.GetObjectPathString();
				Target.PackageName = World.PackageName.ToString();
				Target.bWorld = true;
				Out.Targets.Add(Target);
			}
		}

		Out.Targets.Sort([](const FTarget& A, const FTarget& B) { return A.ObjectPath < B.ObjectPath; });
		return nullptr;
	}

	void WriteSelectionStats(const TSharedPtr<FJsonObject>& Stats, const FSelection& Selection)
	{
		if (Selection.bFromAssetPaths)
		{
			Stats->SetStringField(TEXT("selectedBy"), TEXT("assetPaths"));
			return;
		}
		Stats->SetStringField(TEXT("selectedBy"), TEXT("directory"));
		Stats->SetStringField(TEXT("directory"), Selection.Directory);
		Stats->SetBoolField(TEXT("recursive"), Selection.bRecursive);
		Stats->SetNumberField(TEXT("blueprintsInDirectory"), Selection.BlueprintsInDirectory);
		Stats->SetNumberField(TEXT("worldsInDirectory"), Selection.WorldsInDirectory);
		Stats->SetNumberField(TEXT("skippedByParentClass"), Selection.SkippedByClass);
		Stats->SetBoolField(TEXT("waitedForAssetRegistryScan"), Selection.bWaitedForRegistry);
	}

	/** Every graph the Blueprint owns, with the selector list_graphs reports. */
	void CollectGraphs(UBlueprint* Blueprint, TArray<UEdGraph*>& OutGraphs, TMap<const UEdGraph*, FString>& OutSelectors)
	{
		Blueprint->GetAllGraphs(OutGraphs);
		TMap<FString, int32> NameCounts;
		CountGraphNames(OutGraphs, NameCounts);
		TMap<FString, int32> SeenCounts;
		for (UEdGraph* Graph : OutGraphs)
		{
			if (!Graph) continue;
			const FString Name = Graph->GetName();
			const int32 Index = SeenCounts.FindOrAdd(Name)++;
			OutSelectors.Add(Graph, MakeGraphSelector(Name, Index, NameCounts.FindRef(Name)));
		}
	}

	FString GraphKind(const UBlueprint* Blueprint, UEdGraph* Graph)
	{
		if (Blueprint->UbergraphPages.Contains(Graph)) return TEXT("event_graph");
		if (Blueprint->FunctionGraphs.Contains(Graph)) return TEXT("function");
		if (Blueprint->MacroGraphs.Contains(Graph)) return TEXT("macro");
		if (Blueprint->DelegateSignatureGraphs.Contains(Graph)) return TEXT("delegate_signature");
		for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
		{
			if (Interface.Graphs.Contains(Graph)) return TEXT("interface");
		}
		return TEXT("subgraph");
	}

	/** True when the parent class already declares Name, so a graph or event of
	 *  that name is an override rather than a declaration of its own. */
	bool ParentDeclares(const UBlueprint* Blueprint, FName Name)
	{
		return Blueprint->ParentClass && Blueprint->ParentClass->FindFunctionByName(Name) != nullptr;
	}

	TArray<TSharedPtr<FJsonValue>> DescribeVariables(const UBlueprint* Blueprint)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			bool bRoundTrips = true;
			Obj->SetStringField(TEXT("name"), Var.VarName.ToString());
			Obj->SetStringField(TEXT("typeSpec"), FBlueprintHandlers::PinTypeSpec(Var.VarType, bRoundTrips));
			Obj->SetStringField(TEXT("pinCategory"), Var.VarType.PinCategory.ToString());
			Obj->SetStringField(TEXT("guid"), Var.VarGuid.ToString());
			Obj->SetStringField(TEXT("category"), Var.Category.ToString());
			Obj->SetBoolField(TEXT("instanceEditable"),
				(Var.PropertyFlags & CPF_Edit) != 0 && (Var.PropertyFlags & CPF_DisableEditOnInstance) == 0);
			Obj->SetBoolField(TEXT("exposeOnSpawn"), (Var.PropertyFlags & CPF_ExposeOnSpawn) != 0);
			Obj->SetBoolField(TEXT("replicated"), (Var.PropertyFlags & CPF_Net) != 0);
			if (!Var.RepNotifyFunc.IsNone())
			{
				Obj->SetStringField(TEXT("repNotify"), Var.RepNotifyFunc.ToString());
			}
			Out.Add(MakeShared<FJsonValueObject>(Obj));
		}
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> DescribeComponents(const UBlueprint* Blueprint)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		const USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
		if (!SCS) return Out;
		TArray<TPair<const USCS_Node*, FString>> Stack;
		for (const USCS_Node* Root : SCS->GetRootNodes())
		{
			if (Root) Stack.Emplace(Root, Root->ParentComponentOrVariableName.IsNone() ? FString() : Root->ParentComponentOrVariableName.ToString());
		}
		while (Stack.Num() > 0)
		{
			const TPair<const USCS_Node*, FString> Entry = Stack.Pop();
			const USCS_Node* Node = Entry.Key;
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
			Obj->SetStringField(TEXT("class"), Node->ComponentClass ? Node->ComponentClass->GetName() : FString());
			Obj->SetStringField(TEXT("parent"), Entry.Value);
			Out.Add(MakeShared<FJsonValueObject>(Obj));
			for (const USCS_Node* Child : Node->GetChildNodes())
			{
				if (Child) Stack.Emplace(Child, Node->GetVariableName().ToString());
			}
		}
		return Out;
	}

	TSharedPtr<FJsonObject> Callable(const FString& Name, const TCHAR* Kind, const FString& Selector)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Name);
		Obj->SetStringField(TEXT("kind"), Kind);
		Obj->SetStringField(TEXT("graphSelector"), Selector);
		return Obj;
	}

	TArray<TSharedPtr<FJsonValue>> DescribeCallables(
		const UBlueprint* Blueprint, const TArray<UEdGraph*>& Graphs, const TMap<const UEdGraph*, FString>& Selectors)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph) continue;
			const FString Kind = GraphKind(Blueprint, Graph);
			const FString Selector = Selectors.FindRef(Graph);
			if (Kind == TEXT("function"))
			{
				const bool bOverride = ParentDeclares(Blueprint, Graph->GetFName());
				Out.Add(MakeShared<FJsonValueObject>(Callable(Graph->GetName(), bOverride ? TEXT("override") : TEXT("function"), Selector)));
			}
			else if (Kind == TEXT("interface") || Kind == TEXT("macro"))
			{
				Out.Add(MakeShared<FJsonValueObject>(Callable(Graph->GetName(), *Kind, Selector)));
			}
			else if (Kind == TEXT("delegate_signature"))
			{
				Out.Add(MakeShared<FJsonValueObject>(Callable(Graph->GetName(), TEXT("dispatcher"), Selector)));
			}
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				const UK2Node_Event* Event = Cast<UK2Node_Event>(Node);
				if (!Event) continue;
				TSharedPtr<FJsonObject> Obj = Callable(Event->GetFunctionName().ToString(),
					Event->IsA<UK2Node_CustomEvent>() ? TEXT("custom_event") : TEXT("event"), Selector);
				Obj->SetStringField(TEXT("nodeId"), Event->NodeGuid.ToString());
				Out.Add(MakeShared<FJsonValueObject>(Obj));
			}
		}
		return Out;
	}

	/** outputDir: absolute, or relative to the project when it starts with
	 *  Saved/, or relative to Saved/ otherwise. A relative path may not leave
	 *  Saved/. */
	bool ResolveOutputDir(const FString& Requested, FString& OutDir, FString& OutError)
	{
		const FString SavedDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
		FString Dir = Requested;
		Dir.TrimStartAndEndInline();
		if (Dir.IsEmpty())
		{
			Dir = FPaths::Combine(SavedDir, TEXT("UE_MCP"), TEXT("BlueprintExport"));
		}
		else if (FPaths::IsRelative(Dir))
		{
			FPaths::NormalizeFilename(Dir);
			const bool bNamesSaved = Dir.Equals(TEXT("Saved"), ESearchCase::IgnoreCase)
				|| Dir.StartsWith(TEXT("Saved/"), ESearchCase::IgnoreCase);
			Dir = bNamesSaved ? FPaths::Combine(FPaths::ProjectDir(), Dir) : FPaths::Combine(SavedDir, Dir);
			Dir = FPaths::ConvertRelativePathToFull(Dir);
			if (!FPaths::IsUnderDirectory(Dir, SavedDir))
			{
				OutError = FString::Printf(
					TEXT("A relative outputDir must stay under the project's Saved folder (%s); '%s' leaves it. Pass an absolute path to write elsewhere."),
					*SavedDir, *Requested);
				return false;
			}
		}
		OutDir = FPaths::ConvertRelativePathToFull(Dir);
		FPaths::NormalizeDirectoryName(OutDir);
		return true;
	}

	/** "/Game/AI/BP_Foo" -> "Game/AI/BP_Foo", used as the per-asset file stem. */
	FString PackageStem(const FString& PackageName)
	{
		FString Stem = PackageName;
		while (Stem.StartsWith(TEXT("/"))) Stem.RightChopInline(1);
		return Stem;
	}

	FString SerializeJson(const TSharedPtr<FJsonObject>& Obj)
	{
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		return Text;
	}

	// ── Dead-code audit references ──────────────────────────────────────────

	FString OwnerKey(const UClass* Class)
	{
		if (!Class) return FString();
		if (const UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(Class)) return Blueprint->GetPathName();
		return Class->GetPathName();
	}

	FString MemberKey(const FString& Owner, FName Name)
	{
		return Owner + TEXT("|") + Name.ToString().ToLower();
	}

	/** What the scanned Blueprints reference. A reference whose owner class is
	 *  known is recorded against that class and every super class, so a call
	 *  through a child counts for the parent that declares the member. One whose
	 *  owner is unknown counts for every member of that name. */
	struct FReferences
	{
		TSet<FString> Functions;
		TSet<FString> Dispatchers;
		TSet<FString> VariableReads;
		TSet<FString> VariableWrites;
		TSet<FString> NameOnly;
		TSet<const UEdGraph*> Macros;

		void Add(TSet<FString>& Set, const UClass* Owner, FName Name)
		{
			if (Name.IsNone()) return;
			if (!Owner)
			{
				NameOnly.Add(Name.ToString().ToLower());
				return;
			}
			for (const UClass* Class = Owner; Class; Class = Class->GetSuperClass())
			{
				Set.Add(MemberKey(OwnerKey(Class), Name));
				if (!UBlueprint::GetBlueprintFromClass(Class)) break;
			}
		}

		bool Has(const TSet<FString>& Set, const UBlueprint* Blueprint, FName Name) const
		{
			return Set.Contains(MemberKey(Blueprint->GetPathName(), Name))
				|| NameOnly.Contains(Name.ToString().ToLower());
		}
	};

	/** A string literal on a pin named FunctionName (SetTimerByFunctionName and
	 *  friends) names a function by string, which no member reference carries. */
	void CollectStringFunctionNames(const UEdGraphNode* Node, FReferences& Refs)
	{
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input || Pin->LinkedTo.Num() > 0) continue;
			if (Pin->PinName != TEXT("FunctionName") || Pin->DefaultValue.IsEmpty()) continue;
			Refs.NameOnly.Add(Pin->DefaultValue.ToLower());
		}
	}

	/** Member references on node classes this file does not know (AnimGraph
	 *  function bindings and the like), read by reflection and counted by name. */
	void CollectReflectedMemberReferences(UEdGraphNode* Node, FReferences& Refs)
	{
		static const FName MemberReferenceName(TEXT("MemberReference"));
		for (TFieldIterator<FStructProperty> It(Node->GetClass()); It; ++It)
		{
			const FStructProperty* Prop = *It;
			if (!Prop->Struct || Prop->Struct->GetFName() != MemberReferenceName) continue;
			const FMemberReference* Ref = Prop->ContainerPtrToValuePtr<FMemberReference>(Node);
			if (Ref && !Ref->GetMemberName().IsNone())
			{
				Refs.NameOnly.Add(Ref->GetMemberName().ToString().ToLower());
			}
		}
	}

	/** UMG property bindings (UWidgetBlueprint::Bindings), read by reflection
	 *  so the module does not link UMGEditor. */
	void CollectWidgetBindings(UBlueprint* Blueprint, FReferences& Refs)
	{
		FArrayProperty* Array = FindFProperty<FArrayProperty>(Blueprint->GetClass(), TEXT("Bindings"));
		const FStructProperty* Inner = Array ? CastField<FStructProperty>(Array->Inner) : nullptr;
		if (!Inner || !Inner->Struct) return;
		const FNameProperty* FunctionName = FindFProperty<FNameProperty>(Inner->Struct, TEXT("FunctionName"));
		const FNameProperty* SourceProperty = FindFProperty<FNameProperty>(Inner->Struct, TEXT("SourceProperty"));
		const UClass* Self = Blueprint->SkeletonGeneratedClass.Get();
		if (!Self) Self = Blueprint->GeneratedClass.Get();
		FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(Blueprint));
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			const uint8* Element = Helper.GetRawPtr(Index);
			if (FunctionName) Refs.Add(Refs.Functions, Self, FunctionName->GetPropertyValue_InContainer(Element));
			if (SourceProperty) Refs.Add(Refs.VariableReads, Self, SourceProperty->GetPropertyValue_InContainer(Element));
		}
	}

	void CollectReferences(UBlueprint* Blueprint, FReferences& Refs)
	{
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph) continue;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;
				if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
				{
					const UFunction* Target = Call->GetTargetFunction();
					const UClass* Owner = Target
						? Target->GetOwnerClass()
						: Call->FunctionReference.GetMemberParentClass(Call->GetBlueprintClassFromNode());
					Refs.Add(Refs.Functions, Owner, Call->FunctionReference.GetMemberName());
					CollectStringFunctionNames(Node, Refs);
				}
				else if (const UK2Node_Variable* Var = Cast<UK2Node_Variable>(Node))
				{
					if (Var->VariableReference.IsLocalScope()) continue;
					const UClass* Owner = Var->VariableReference.GetMemberParentClass(Var->GetBlueprintClassFromNode());
					const bool bSet = Var->IsA<UK2Node_VariableSet>();
					const bool bGet = Var->IsA<UK2Node_VariableGet>();
					if (bGet || !bSet) Refs.Add(Refs.VariableReads, Owner, Var->GetVarName());
					if (bSet || !bGet) Refs.Add(Refs.VariableWrites, Owner, Var->GetVarName());
				}
				else if (const UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(Node))
				{
					if (const UEdGraph* MacroGraph = Macro->GetMacroGraph()) Refs.Macros.Add(MacroGraph);
				}
				else if (const UK2Node_CreateDelegate* Delegate = Cast<UK2Node_CreateDelegate>(Node))
				{
					Refs.Add(Refs.Functions, Delegate->GetScopeClass(), Delegate->GetFunctionName());
				}
				else if (const UK2Node_BaseMCDelegate* Dispatcher = Cast<UK2Node_BaseMCDelegate>(Node))
				{
					Refs.Add(Refs.Dispatchers,
						Dispatcher->DelegateReference.GetMemberParentClass(Dispatcher->GetBlueprintClassFromNode()),
						Dispatcher->GetPropertyName());
				}
				else if (const UK2Node_ComponentBoundEvent* ComponentEvent = Cast<UK2Node_ComponentBoundEvent>(Node))
				{
					Refs.Add(Refs.Dispatchers, ComponentEvent->DelegateOwnerClass, ComponentEvent->DelegatePropertyName);
				}
				else if (const UK2Node_ActorBoundEvent* ActorEvent = Cast<UK2Node_ActorBoundEvent>(Node))
				{
					Refs.Add(Refs.Dispatchers, ActorEvent->DelegateOwnerClass, ActorEvent->DelegatePropertyName);
				}
				else if (!Node->IsA<UK2Node_Event>() && !Node->IsA<UK2Node_FunctionTerminator>())
				{
					// Events and function entry/result nodes carry a member
					// reference to themselves, which is not a use.
					CollectReflectedMemberReferences(Node, Refs);
				}
			}
		}
		CollectWidgetBindings(Blueprint, Refs);
	}

	// ── Dead-code audit graph checks ────────────────────────────────────────

	struct FExecShape
	{
		bool bHasExecIn = false;
		bool bExecInLinked = false;
		bool bHasExecOut = false;
		bool bHasDataOut = false;
		bool bDataOutLinked = false;
	};

	FExecShape ReadShape(const UEdGraphNode* Node)
	{
		FExecShape Shape;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->bHidden || Pin->bOrphanedPin) continue;
			const bool bExec = Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
			const bool bLinked = Pin->LinkedTo.Num() > 0;
			if (bExec && Pin->Direction == EGPD_Input)
			{
				Shape.bHasExecIn = true;
				Shape.bExecInLinked |= bLinked;
			}
			else if (bExec)
			{
				Shape.bHasExecOut = true;
			}
			else if (Pin->Direction == EGPD_Output)
			{
				Shape.bHasDataOut = true;
				Shape.bDataOutLinked |= bLinked;
			}
		}
		return Shape;
	}

	/** Nodes reachable from Head along exec outputs, Head included. */
	int32 ExecIslandSize(const UEdGraphNode* Head)
	{
		TSet<const UEdGraphNode*> Seen;
		TArray<const UEdGraphNode*> Queue;
		Queue.Add(Head);
		Seen.Add(Head);
		while (Queue.Num() > 0)
		{
			const UEdGraphNode* Node = Queue.Pop();
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;
				for (const UEdGraphPin* Linked : Pin->LinkedTo)
				{
					const UEdGraphNode* Next = Linked ? Linked->GetOwningNodeUnchecked() : nullptr;
					if (Next && !Seen.Contains(Next))
					{
						Seen.Add(Next);
						Queue.Add(Next);
					}
				}
			}
		}
		return Seen.Num();
	}

	/** Findings of one kind: the complete count, and samples up to the cap. */
	struct FFindingList
	{
		int32 Count = 0;
		TArray<TSharedPtr<FJsonValue>> Samples;

		void Add(int32 MaxSamples, const TSharedPtr<FJsonObject>& Sample)
		{
			++Count;
			if (Samples.Num() < MaxSamples) Samples.Add(MakeShared<FJsonValueObject>(Sample));
		}
	};

	TSharedPtr<FJsonObject> NodeSample(const UEdGraphNode* Node, const FString& Selector)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("graphSelector"), Selector);
		Obj->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
		Obj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		Obj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		return Obj;
	}

	TSharedPtr<FJsonObject> NamedSample(const FString& Name, const FString& Selector)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Name);
		if (!Selector.IsEmpty()) Obj->SetStringField(TEXT("graphSelector"), Selector);
		return Obj;
	}

	/** The categories one audited Blueprint reports, in response order. */
	enum class EFinding : uint8
	{
		UnusedFunctions,
		UnusedMacros,
		UnusedCustomEvents,
		UnusedDispatchers,
		UnusedVariables,
		WriteOnlyVariables,
		DisconnectedFunctionBodies,
		UnreachableNodes,
		UnconnectedPureNodes,
		Num
	};

	const TCHAR* FindingName(EFinding Finding)
	{
		switch (Finding)
		{
		case EFinding::UnusedFunctions: return TEXT("unusedFunctions");
		case EFinding::UnusedMacros: return TEXT("unusedMacros");
		case EFinding::UnusedCustomEvents: return TEXT("unusedCustomEvents");
		case EFinding::UnusedDispatchers: return TEXT("unusedDispatchers");
		case EFinding::UnusedVariables: return TEXT("unusedVariables");
		case EFinding::WriteOnlyVariables: return TEXT("writeOnlyVariables");
		case EFinding::DisconnectedFunctionBodies: return TEXT("disconnectedFunctionBodies");
		case EFinding::UnreachableNodes: return TEXT("unreachableNodes");
		case EFinding::UnconnectedPureNodes: return TEXT("unconnectedPureNodes");
		default: return TEXT("unknown");
		}
	}

	struct FAuditRow
	{
		FFindingList Lists[static_cast<int32>(EFinding::Num)];
		FFindingList& operator[](EFinding Finding) { return Lists[static_cast<int32>(Finding)]; }
		const FFindingList& operator[](EFinding Finding) const { return Lists[static_cast<int32>(Finding)]; }

		int32 Total() const
		{
			int32 Sum = 0;
			for (const FFindingList& List : Lists) Sum += List.Count;
			return Sum;
		}
	};

	void AuditBlueprint(UBlueprint* Blueprint, const FReferences& Refs, int32 MaxSamples, FAuditRow& Row)
	{
		TArray<UEdGraph*> Graphs;
		TMap<const UEdGraph*, FString> Selectors;
		CollectGraphs(Blueprint, Graphs, Selectors);

		// Functions declared here. Overrides, the construction script and
		// CallInEditor functions are called from outside any graph.
		for (const UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (!Graph) continue;
			const FName Name = Graph->GetFName();
			if (Name == UEdGraphSchema_K2::FN_UserConstructionScript || ParentDeclares(Blueprint, Name)) continue;
			bool bCallInEditor = false;
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				if (const UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
				{
					bCallInEditor = Entry->MetaData.bCallInEditor;
					break;
				}
			}
			if (bCallInEditor) continue;
			if (!Refs.Has(Refs.Functions, Blueprint, Name))
			{
				Row[EFinding::UnusedFunctions].Add(MaxSamples, NamedSample(Name.ToString(), Selectors.FindRef(Graph)));
			}
		}

		for (const UEdGraph* Graph : Blueprint->MacroGraphs)
		{
			if (Graph && !Refs.Macros.Contains(Graph))
			{
				Row[EFinding::UnusedMacros].Add(MaxSamples, NamedSample(Graph->GetName(), Selectors.FindRef(Graph)));
			}
		}

		for (const UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
		{
			if (Graph && !Refs.Has(Refs.Dispatchers, Blueprint, Graph->GetFName()))
			{
				Row[EFinding::UnusedDispatchers].Add(MaxSamples, NamedSample(Graph->GetName(), Selectors.FindRef(Graph)));
			}
		}

		for (const FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			const bool bRead = Refs.Has(Refs.VariableReads, Blueprint, Var.VarName);
			const bool bWritten = Refs.Has(Refs.VariableWrites, Blueprint, Var.VarName);
			if (bRead) continue;
			TSharedPtr<FJsonObject> Sample = NamedSample(Var.VarName.ToString(), FString());
			Sample->SetStringField(TEXT("pinCategory"), Var.VarType.PinCategory.ToString());
			Sample->SetBoolField(TEXT("instanceEditable"),
				(Var.PropertyFlags & CPF_Edit) != 0 && (Var.PropertyFlags & CPF_DisableEditOnInstance) == 0);
			Sample->SetBoolField(TEXT("exposeOnSpawn"), (Var.PropertyFlags & CPF_ExposeOnSpawn) != 0);
			if (!Var.RepNotifyFunc.IsNone()) Sample->SetStringField(TEXT("repNotify"), Var.RepNotifyFunc.ToString());
			Row[bWritten ? EFinding::WriteOnlyVariables : EFinding::UnusedVariables].Add(MaxSamples, Sample);
		}

		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph) continue;
			const FString Selector = Selectors.FindRef(Graph);
			const bool bFunctionLike = Blueprint->FunctionGraphs.Contains(Graph)
				|| GraphKind(Blueprint, Graph) == TEXT("interface");

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UK2Node* K2 = Cast<UK2Node>(Node);
				if (!K2) continue;

				if (const UK2Node_CustomEvent* Custom = Cast<UK2Node_CustomEvent>(K2))
				{
					const FName Name = Custom->GetFunctionName();
					const UEdGraphPin* DelegatePin = Custom->FindPin(UK2Node_Event::DelegateOutputName);
					const bool bBound = DelegatePin && DelegatePin->LinkedTo.Num() > 0;
					if (!Custom->bCallInEditor && !bBound && !ParentDeclares(Blueprint, Name)
						&& !Refs.Has(Refs.Functions, Blueprint, Name))
					{
						TSharedPtr<FJsonObject> Sample = NamedSample(Name.ToString(), Selector);
						Sample->SetStringField(TEXT("nodeId"), Custom->NodeGuid.ToString());
						Row[EFinding::UnusedCustomEvents].Add(MaxSamples, Sample);
					}
					continue;
				}

				if (const UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(K2))
				{
					// A function whose entry exec pin is unwired while the
					// graph holds executable nodes: the body never runs.
					const UEdGraphPin* Then = Entry->FindPin(UEdGraphSchema_K2::PN_Then);
					if (bFunctionLike && Then && Then->LinkedTo.Num() == 0)
					{
						int32 BodyNodes = 0;
						for (const UEdGraphNode* Other : Graph->Nodes)
						{
							if (!Other || Other == Entry || Other->IsA<UK2Node_FunctionResult>()) continue;
							const FExecShape OtherShape = ReadShape(Other);
							if (OtherShape.bHasExecIn || OtherShape.bHasExecOut) ++BodyNodes;
						}
						if (BodyNodes > 0)
						{
							TSharedPtr<FJsonObject> Sample = NamedSample(Graph->GetName(), Selector);
							Sample->SetNumberField(TEXT("executableNodes"), BodyNodes);
							Row[EFinding::DisconnectedFunctionBodies].Add(MaxSamples, Sample);
						}
					}
					continue;
				}

				// Events and entries have no exec input; a bare tunnel is a
				// macro or collapsed graph's own gateway.
				if (K2->IsA<UK2Node_Event>() || K2->GetClass() == UK2Node_Tunnel::StaticClass()) continue;

				const FExecShape Shape = ReadShape(K2);
				if (Shape.bHasExecIn && !Shape.bExecInLinked)
				{
					TSharedPtr<FJsonObject> Sample = NodeSample(K2, Selector);
					Sample->SetNumberField(TEXT("islandSize"), ExecIslandSize(K2));
					Row[EFinding::UnreachableNodes].Add(MaxSamples, Sample);
				}
				else if (!Shape.bHasExecIn && !Shape.bHasExecOut && K2->IsNodePure()
					&& Shape.bHasDataOut && !Shape.bDataOutLinked)
				{
					Row[EFinding::UnconnectedPureNodes].Add(MaxSamples, NodeSample(K2, Selector));
				}
			}
		}
	}
}

// ---------------------------------------------------------------------------
// export_blueprint_batch (#1166)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::ExportBlueprintBatch(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPBlueprintBatch;

	// Every parameter is read before anything can fail (#1057).
	const FString ParentClassSpec = OptionalString(Params, TEXT("parentClass"), TEXT(""));
	const bool bIncludeT3D = OptionalBool(Params, TEXT("includeT3D"), false);
	const bool bIncludeLevelScripts = OptionalBool(Params, TEXT("includeLevelScripts"), false);
	const int32 MaxAssets = FMath::Clamp(OptionalInt(Params, TEXT("maxAssets"), DefaultMaxAssets), 1, MaxMaxAssets);
	const FString RequestedOutputDir = OptionalString(Params, TEXT("outputDir"), TEXT(""));
	const FSelectionRequest SelectionRequest = ReadSelectionRequest(Params);

	UClass* FilterClass = nullptr;
	if (!ParentClassSpec.IsEmpty())
	{
		FilterClass = MCPResolveClass(ParentClassSpec);
		if (!FilterClass) return MCPClassNotFoundError(ParentClassSpec, TEXT("parentClass"));
	}

	FString OutputDir;
	FString DirError;
	if (!ResolveOutputDir(RequestedOutputDir, OutputDir, DirError))
	{
		return MCPError(DirError);
	}

	FSelection Selection;
	if (TSharedPtr<FJsonValue> Err = ReadSelection(SelectionRequest, bIncludeLevelScripts, FilterClass, Selection)) return Err;

	const bool bTruncated = Selection.Targets.Num() > MaxAssets;
	if (bTruncated) Selection.Targets.SetNum(MaxAssets);

	TArray<TSharedPtr<FJsonValue>> Rows;
	TArray<TSharedPtr<FJsonValue>> Files;
	int32 Exported = 0;
	int32 Failed = 0;
	int32 SkippedByClassAfterLoad = 0;

	for (const FTarget& Target : Selection.Targets)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("assetPath"), Target.ObjectPath);
		auto Fail = [&](const FString& Error)
		{
			Row->SetBoolField(TEXT("ok"), false);
			Row->SetStringField(TEXT("error"), Error);
			Rows.Add(MakeShared<FJsonValueObject>(Row));
			++Failed;
		};

		UBlueprint* Blueprint = LoadBlueprint(Target.ObjectPath);
		if (!Blueprint)
		{
			Fail(Target.bWorld
				? TEXT("map has no level script Blueprint")
				: TEXT("did not load as a Blueprint"));
			continue;
		}
		if (!DerivesFrom(Blueprint, FilterClass))
		{
			++SkippedByClassAfterLoad;
			continue;
		}

		TArray<UEdGraph*> Graphs;
		TMap<const UEdGraph*, FString> Selectors;
		CollectGraphs(Blueprint, Graphs, Selectors);

		const FString Stem = FPaths::Combine(OutputDir, PackageStem(Target.PackageName));
		TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
		Summary->SetStringField(TEXT("assetPath"), Target.ObjectPath);
		Summary->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
		Summary->SetStringField(TEXT("packageName"), Target.PackageName);
		Summary->SetBoolField(TEXT("isLevelScript"), Blueprint->IsA<ULevelScriptBlueprint>());
		Summary->SetStringField(TEXT("blueprintClass"), Blueprint->GetClass()->GetName());
		Summary->SetStringField(TEXT("parentClass"), Blueprint->ParentClass ? Blueprint->ParentClass->GetPathName() : FString());
		Summary->SetArrayField(TEXT("variables"), DescribeVariables(Blueprint));
		Summary->SetArrayField(TEXT("functions"), DescribeCallables(Blueprint, Graphs, Selectors));
		Summary->SetArrayField(TEXT("components"), DescribeComponents(Blueprint));

		FMCPGraphNodeJsonOptions NodeOptions;
		NodeOptions.bLinks = true;
		TArray<TSharedPtr<FJsonValue>> GraphArray;
		TArray<TSharedPtr<FJsonValue>> RowFiles;
		FString WriteError;
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph) continue;
			const FString Selector = Selectors.FindRef(Graph);
			TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
			GraphObj->SetStringField(TEXT("name"), Graph->GetName());
			GraphObj->SetStringField(TEXT("selector"), Selector);
			GraphObj->SetStringField(TEXT("kind"), GraphKind(Blueprint, Graph));
			GraphObj->SetBoolField(TEXT("nested"), Graph->GetOuter() != Blueprint);
			GraphObj->SetStringField(TEXT("objectPath"), Graph->GetPathName());
			TArray<TSharedPtr<FJsonValue>> Nodes;
			TArray<UEdGraphNode*> GraphNodes;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;
				GraphNodes.Add(Node);
				Nodes.Add(MakeShared<FJsonValueObject>(MCPDescribeGraphNode(Node, NodeOptions)));
			}
			GraphObj->SetNumberField(TEXT("nodeCount"), Nodes.Num());
			GraphObj->SetArrayField(TEXT("nodes"), Nodes);

			if (bIncludeT3D && GraphNodes.Num() > 0 && WriteError.IsEmpty())
			{
				FString T3D;
				int32 Skipped = 0;
				const int32 Written = MCPExportNodesToT3D(GraphNodes, T3D, Skipped);
				if (Written > 0)
				{
					const FString T3DPath = FPaths::Combine(Stem, FPaths::MakeValidFileName(Selector) + TEXT(".t3d"));
					if (MCPWriteDumpFile(T3DPath, T3D, TEXT("graph T3D"), WriteError))
					{
						GraphObj->SetStringField(TEXT("t3dFile"), T3DPath);
						RowFiles.Add(MakeShared<FJsonValueString>(T3DPath));
					}
				}
				GraphObj->SetNumberField(TEXT("t3dNodeCount"), Written);
				GraphObj->SetNumberField(TEXT("t3dSkipped"), Skipped);
			}
			GraphArray.Add(MakeShared<FJsonValueObject>(GraphObj));
		}
		Summary->SetArrayField(TEXT("graphs"), GraphArray);

		const FString JsonPath = Stem + TEXT(".json");
		if (WriteError.IsEmpty())
		{
			MCPWriteDumpFile(JsonPath, SerializeJson(Summary), TEXT("Blueprint summary"), WriteError);
		}
		if (!WriteError.IsEmpty())
		{
			Row->SetArrayField(TEXT("files"), RowFiles);
			Files.Append(RowFiles);
			Fail(WriteError);
			continue;
		}
		RowFiles.Insert(MakeShared<FJsonValueString>(JsonPath), 0);
		Files.Append(RowFiles);
		Row->SetBoolField(TEXT("ok"), true);
		Row->SetStringField(TEXT("jsonFile"), JsonPath);
		Row->SetNumberField(TEXT("graphCount"), GraphArray.Num());
		Row->SetArrayField(TEXT("files"), RowFiles);
		Rows.Add(MakeShared<FJsonValueObject>(Row));
		++Exported;
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("outputDir"), OutputDir);
	Result->SetNumberField(TEXT("exported"), Exported);
	Result->SetNumberField(TEXT("failed"), Failed);
	Result->SetArrayField(TEXT("assets"), Rows);
	Result->SetArrayField(TEXT("files"), Files);
	Result->SetNumberField(TEXT("fileCount"), Files.Num());
	Result->SetBoolField(TEXT("includeT3D"), bIncludeT3D);
	// Files only: no package, asset or editor state is touched.
	Result->SetBoolField(TEXT("changed"), Files.Num() > 0);
	MCPSetNoRollback(Result, TEXT(
		"No bridge call deletes a file from disk, so the written files cannot be removed again. No asset, package "
		"or editor state was changed; a rerun overwrites the same paths."));
	TSharedPtr<FJsonObject> Stats = MakeShared<FJsonObject>();
	WriteSelectionStats(Stats, Selection);
	Stats->SetNumberField(TEXT("considered"), Selection.Targets.Num());
	Stats->SetNumberField(TEXT("skippedByParentClassAfterLoad"), SkippedByClassAfterLoad);
	Result->SetObjectField(TEXT("stats"), Stats);
	if (FilterClass) Result->SetStringField(TEXT("parentClass"), FilterClass->GetPathName());
	if (bTruncated)
	{
		Result->SetBoolField(TEXT("truncatedAtMaxAssets"), true);
		Result->SetNumberField(TEXT("maxAssets"), MaxAssets);
	}
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// audit_blueprint_dead_code (#1166). Report-only: loads, reads, writes nothing
// to any package.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::AuditDeadCode(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPBlueprintBatch;

	const bool bIncludeLevelScripts = OptionalBool(Params, TEXT("includeLevelScripts"), false);
	const bool bScanReferencers = OptionalBool(Params, TEXT("scanReferencers"), true);
	const int32 MaxBlueprints = FMath::Clamp(
		OptionalInt(Params, TEXT("maxBlueprints"), DefaultAuditMaxBlueprints), 1, MaxAuditMaxBlueprints);
	const int32 MaxSamples = FMath::Clamp(OptionalInt(Params, TEXT("maxSamples"), DefaultMaxSamples), 0, MaxMaxSamples);
	const int32 RowLimit = FMath::Clamp(OptionalInt(Params, TEXT("limit"), DefaultAuditRowLimit), 1, MaxAuditRowLimit);
	const bool bDumpToFile = OptionalBool(Params, TEXT("dumpToFile"), false);
	const FString OutputPath = OptionalString(Params, TEXT("outputPath"), TEXT(""));

	FSelection Selection;
	if (TSharedPtr<FJsonValue> Err = ReadSelection(ReadSelectionRequest(Params), bIncludeLevelScripts, nullptr, Selection)) return Err;

	const bool bTruncatedAtMaxBlueprints = Selection.Targets.Num() > MaxBlueprints;
	if (bTruncatedAtMaxBlueprints) Selection.Targets.SetNum(MaxBlueprints);

	// ── Load the audited Blueprints ─────────────────────────────────────────
	TArray<TPair<UBlueprint*, FString>> Audited;
	TSet<FString> LoadedPackages;
	TArray<TSharedPtr<FJsonValue>> FailedToLoad;
	for (const FTarget& Target : Selection.Targets)
	{
		UBlueprint* Blueprint = LoadBlueprint(Target.ObjectPath);
		if (!Blueprint)
		{
			if (FailedToLoad.Num() < 25) FailedToLoad.Add(MakeShared<FJsonValueString>(Target.ObjectPath));
			continue;
		}
		Audited.Emplace(Blueprint, Target.ObjectPath);
		LoadedPackages.Add(Target.PackageName);
	}

	FReferences Refs;
	for (const TPair<UBlueprint*, FString>& Entry : Audited)
	{
		CollectReferences(Entry.Key, Refs);
	}

	// ── Referencers outside the selection ───────────────────────────────────
	// A member is used when anything calls it, not only something inside the
	// audited directory, so every Blueprint package that depends on an audited
	// one is scanned for references too (bounded by the same load cap).
	int32 ReferencersScanned = 0;
	bool bReferencersTruncated = false;
	if (bScanReferencers && Audited.Num() > 0)
	{
		bool bWaited = false;
		IAssetRegistry& Registry = GetRegistry(bWaited);
		TArray<FName> Pending;
		TSet<FName> Queued;
		for (const FString& Package : LoadedPackages) Queued.Add(FName(*Package));
		for (const FString& Package : LoadedPackages)
		{
			TArray<FName> Referencers;
			Registry.GetReferencers(FName(*Package), Referencers, UE::AssetRegistry::EDependencyCategory::Package);
			for (const FName& Referencer : Referencers)
			{
				if (!Queued.Contains(Referencer))
				{
					Queued.Add(Referencer);
					Pending.Add(Referencer);
				}
			}
		}
		Pending.Sort(FNameLexicalLess());
		for (const FName& Package : Pending)
		{
			if (Audited.Num() + ReferencersScanned >= MaxBlueprints)
			{
				bReferencersTruncated = true;
				break;
			}
			TArray<FAssetData> Assets;
			Registry.GetAssetsByPackageName(Package, Assets);
			for (const FAssetData& Asset : Assets)
			{
				const UClass* AssetClass = Asset.GetClass();
				const bool bBlueprint = AssetClass && AssetClass->IsChildOf(UBlueprint::StaticClass());
				const bool bWorld = bIncludeLevelScripts && AssetClass && AssetClass->IsChildOf(UWorld::StaticClass());
				if (!bBlueprint && !bWorld) continue;
				if (UBlueprint* Blueprint = LoadBlueprint(Asset.GetObjectPathString()))
				{
					CollectReferences(Blueprint, Refs);
					++ReferencersScanned;
				}
				break;
			}
		}
	}

	// ── Audit ───────────────────────────────────────────────────────────────
	TArray<TSharedPtr<FJsonValue>> AllRows;
	int32 Totals[static_cast<int32>(EFinding::Num)] = {};
	int32 CleanBlueprints = 0;
	for (const TPair<UBlueprint*, FString>& Entry : Audited)
	{
		FAuditRow Row;
		AuditBlueprint(Entry.Key, Refs, MaxSamples, Row);
		if (Row.Total() == 0)
		{
			++CleanBlueprints;
			continue;
		}
		TSharedPtr<FJsonObject> RowObj = MakeShared<FJsonObject>();
		RowObj->SetStringField(TEXT("assetPath"), Entry.Value);
		RowObj->SetStringField(TEXT("blueprintPath"), Entry.Key->GetPathName());
		RowObj->SetBoolField(TEXT("isLevelScript"), Entry.Key->IsA<ULevelScriptBlueprint>());
		TSharedPtr<FJsonObject> Counts = MakeShared<FJsonObject>();
		for (int32 Index = 0; Index < static_cast<int32>(EFinding::Num); ++Index)
		{
			const EFinding Finding = static_cast<EFinding>(Index);
			Counts->SetNumberField(FindingName(Finding), Row[Finding].Count);
			Totals[Index] += Row[Finding].Count;
			if (Row[Finding].Count > 0)
			{
				RowObj->SetArrayField(FindingName(Finding), Row[Finding].Samples);
			}
		}
		RowObj->SetObjectField(TEXT("counts"), Counts);
		RowObj->SetNumberField(TEXT("findings"), Row.Total());
		AllRows.Add(MakeShared<FJsonValueObject>(RowObj));
	}

	auto Result = MCPSuccess();
	TSharedPtr<FJsonObject> TotalsObj = MakeShared<FJsonObject>();
	for (int32 Index = 0; Index < static_cast<int32>(EFinding::Num); ++Index)
	{
		TotalsObj->SetNumberField(FindingName(static_cast<EFinding>(Index)), Totals[Index]);
	}
	Result->SetObjectField(TEXT("totals"), TotalsObj);

	TSharedPtr<FJsonObject> Stats = MakeShared<FJsonObject>();
	WriteSelectionStats(Stats, Selection);
	Stats->SetNumberField(TEXT("blueprintsAudited"), Audited.Num());
	Stats->SetNumberField(TEXT("blueprintsWithFindings"), AllRows.Num());
	Stats->SetNumberField(TEXT("cleanBlueprints"), CleanBlueprints);
	Stats->SetBoolField(TEXT("scannedReferencers"), bScanReferencers);
	Stats->SetNumberField(TEXT("referencerBlueprintsScanned"), ReferencersScanned);
	Stats->SetBoolField(TEXT("includedLevelScripts"), bIncludeLevelScripts);
	if (FailedToLoad.Num() > 0) Stats->SetArrayField(TEXT("failedToLoad"), FailedToLoad);
	Result->SetObjectField(TEXT("stats"), Stats);
	if (bTruncatedAtMaxBlueprints) Result->SetBoolField(TEXT("truncatedAtMaxBlueprints"), true);
	if (bReferencersTruncated) Result->SetBoolField(TEXT("referencersTruncatedAtMaxBlueprints"), true);
	Result->SetNumberField(TEXT("maxBlueprints"), MaxBlueprints);
	Result->SetNumberField(TEXT("maxSamples"), MaxSamples);

	TArray<TSharedPtr<FJsonValue>> Caveats;
	Caveats.Add(MakeShared<FJsonValueString>(TEXT(
		"A member called only from C++, by a string name the audit cannot see (a timer set by function name is "
		"read when the name is a pin literal), through an AnimGraph property binding, or from a Blueprint outside "
		"the scanned set reads as unused. Treat findings as candidates to review, not a delete list.")));
	Caveats.Add(MakeShared<FJsonValueString>(TEXT(
		"Overrides of parent or interface functions, the construction script and CallInEditor functions and events "
		"are excluded, because the engine or the details panel calls them.")));
	Caveats.Add(MakeShared<FJsonValueString>(TEXT(
		"unreachableNodes lists the HEAD of each exec island with no incoming exec wire; islandSize counts the "
		"nodes downstream of it. unconnectedPureNodes are pure nodes whose outputs feed nothing.")));
	Result->SetArrayField(TEXT("caveats"), Caveats);

	if (bDumpToFile)
	{
		TSharedPtr<FJsonObject> Dump = MakeShared<FJsonObject>();
		Dump->SetObjectField(TEXT("totals"), TotalsObj);
		Dump->SetObjectField(TEXT("stats"), Stats);
		Dump->SetArrayField(TEXT("blueprints"), AllRows);
		FString ResolvedPath;
		FString DumpError;
		const FString Scope = Selection.bFromAssetPaths ? FString(TEXT("/Game/assetPaths")) : Selection.Directory;
		if (!WriteJsonObjectToFile(Dump, OutputPath, Scope, TEXT("dead_code_audit"), ResolvedPath, DumpError))
		{
			return MCPError(DumpError);
		}
		Result->SetBoolField(TEXT("dumpedToFile"), true);
		Result->SetStringField(TEXT("outputPath"), ResolvedPath);
		return MCPResult(Result);
	}

	const bool bRowsTruncated = AllRows.Num() > RowLimit;
	if (bRowsTruncated) AllRows.SetNum(RowLimit);
	Result->SetArrayField(TEXT("blueprints"), AllRows);
	Result->SetNumberField(TEXT("returned"), AllRows.Num());
	if (bRowsTruncated)
	{
		Result->SetBoolField(TEXT("rowsTruncated"), true);
		Result->SetNumberField(TEXT("limit"), RowLimit);
	}
	return MCPResult(Result);
}
