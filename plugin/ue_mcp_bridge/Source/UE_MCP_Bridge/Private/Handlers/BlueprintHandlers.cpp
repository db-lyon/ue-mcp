#include "BlueprintHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "SubobjectDataSubsystem.h"
#include "SubobjectDataHandle.h"
#include "SubobjectData.h"
#include "SubobjectDataBlueprintFunctionLibrary.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "Internationalization/Text.h"
#include "UObject/TopLevelAssetPath.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/BlueprintFactory.h"
// BlueprintGraphDefinitions.h removed - not available in UE 5.7
#include "EdGraph/EdGraph.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_CustomEvent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/MessageDialog.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/KismetArrayLibrary.h"

// SCS component access
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "EditorAssetLibrary.h"

void FBlueprintHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("create_blueprint"), &CreateBlueprint);
	Registry.RegisterHandler(TEXT("read_blueprint"), &ReadBlueprint);
	Registry.RegisterHandler(TEXT("add_variable"), &AddVariable);
	Registry.RegisterHandler(TEXT("add_component"), &AddComponent);
	Registry.RegisterHandler(TEXT("add_blueprint_interface"), &AddBlueprintInterface);
	Registry.RegisterHandler(TEXT("compile_blueprint"), &CompileBlueprint);
	Registry.RegisterHandler(TEXT("search_node_types"), &SearchNodeTypes);
	Registry.RegisterHandler(TEXT("list_node_types"), &ListNodeTypes);
	Registry.RegisterHandler(TEXT("list_blueprint_variables"), &ListBlueprintVariables);
	Registry.RegisterHandler(TEXT("set_variable_properties"), &SetVariableProperties);
	Registry.RegisterHandler(TEXT("create_function"), &CreateFunction);
	Registry.RegisterHandler(TEXT("list_blueprint_functions"), &ListBlueprintFunctions);
	Registry.RegisterHandler(TEXT("add_node"), &AddNode);
	Registry.RegisterHandler(TEXT("read_blueprint_graph"), &ReadBlueprintGraph);
	Registry.RegisterHandler(TEXT("add_event_dispatcher"), &AddEventDispatcher);
	Registry.RegisterHandler(TEXT("rename_function"), &RenameFunction);
	Registry.RegisterHandler(TEXT("delete_function"), &DeleteFunction);
	Registry.RegisterHandler(TEXT("create_blueprint_interface"), &CreateBlueprintInterface);
	Registry.RegisterHandler(TEXT("list_node_types_detailed"), &ListNodeTypesDetailed);
	Registry.RegisterHandler(TEXT("search_callable_functions"), &SearchCallableFunctions);
	Registry.RegisterHandler(TEXT("connect_pins"), &ConnectPins);
	Registry.RegisterHandler(TEXT("delete_node"), &DeleteNode);
	Registry.RegisterHandler(TEXT("set_node_property"), &SetNodeProperty);
	Registry.RegisterHandler(TEXT("list_blueprint_graphs"), &ListGraphs);
	Registry.RegisterHandler(TEXT("set_blueprint_component_property"), &SetComponentProperty);
	Registry.RegisterHandler(TEXT("set_class_default"), &SetClassDefault);
	Registry.RegisterHandler(TEXT("remove_component"), &RemoveComponent);
	Registry.RegisterHandler(TEXT("delete_variable"), &DeleteVariable);
	Registry.RegisterHandler(TEXT("add_function_parameter"), &AddFunctionParameter);
	Registry.RegisterHandler(TEXT("set_variable_default"), &SetVariableDefault);
}

UBlueprint* FBlueprintHandlers::LoadBlueprint(const FString& AssetPath)
{
	// Try exact path first (handles both package path and full object path)
	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *AssetPath);
	if (BP) return BP;

	// If the path looks like a package path (no '.' after last '/'), try object path format
	// e.g. "/Game/Foo/BP_Bar" -> "/Game/Foo/BP_Bar.BP_Bar"
	if (!AssetPath.Contains(TEXT(".")))
	{
		FString AssetName;
		AssetPath.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		FString ObjectPath = AssetPath + TEXT(".") + AssetName;
		BP = LoadObject<UBlueprint>(nullptr, *ObjectPath);
	}
	return BP;
}

// ---------------------------------------------------------------------------
// list_blueprint_graphs -- List all graphs in a blueprint (EventGraph, AnimGraph, functions, etc.)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::ListGraphs(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);

	TArray<TSharedPtr<FJsonValue>> GraphsArray;
	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) continue;
		TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
		GraphObj->SetStringField(TEXT("name"), Graph->GetName());
		GraphObj->SetStringField(TEXT("class"), Graph->GetClass()->GetName());
		GraphObj->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());
		GraphsArray.Add(MakeShared<FJsonValueObject>(GraphObj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetArrayField(TEXT("graphs"), GraphsArray);

	return MCPResult(Result);
}

FEdGraphPinType FBlueprintHandlers::MakePinType(const FString& TypeStr)
{
	FEdGraphPinType PinType;
	PinType.PinCategory = NAME_None;
	PinType.PinSubCategory = NAME_None;

	FString LowerType = TypeStr.ToLower();

	// Map type strings to pin categories
	if (LowerType == TEXT("bool") || LowerType == TEXT("boolean"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}
	else if (LowerType == TEXT("int") || LowerType == TEXT("integer") || LowerType == TEXT("int32"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	else if (LowerType == TEXT("int64"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
	}
	else if (LowerType == TEXT("float") || LowerType == TEXT("double") || LowerType == TEXT("real"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
	}
	else if (LowerType == TEXT("string") || LowerType == TEXT("str"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_String;
	}
	else if (LowerType == TEXT("name"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	}
	else if (LowerType == TEXT("text"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
	}
	else if (LowerType == TEXT("object"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
	}
	else if (LowerType == TEXT("class"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Class;
	}
	else if (LowerType == TEXT("softobject") || LowerType == TEXT("softobjectreference"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_SoftObject;
	}
	else if (LowerType == TEXT("softclass") || LowerType == TEXT("softclassreference"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_SoftClass;
	}
	else if (LowerType == TEXT("byte"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
	}
	else if (LowerType == TEXT("enum"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
	}
	else
	{
		// Try to resolve as a struct type (FVector, FRotator, FTransform, FLinearColor, FGameplayTag, etc.)
		// Strip leading 'F' for lookup if present
		FString StructName = TypeStr;
		static const TMap<FString, FString> StructAliases = {
			{ TEXT("vector"),       TEXT("Vector") },
			{ TEXT("fvector"),      TEXT("Vector") },
			{ TEXT("rotator"),      TEXT("Rotator") },
			{ TEXT("frotator"),     TEXT("Rotator") },
			{ TEXT("transform"),    TEXT("Transform") },
			{ TEXT("ftransform"),   TEXT("Transform") },
			{ TEXT("linearcolor"),  TEXT("LinearColor") },
			{ TEXT("flinearcolor"), TEXT("LinearColor") },
			{ TEXT("color"),        TEXT("Color") },
			{ TEXT("fcolor"),       TEXT("Color") },
			{ TEXT("vector2d"),     TEXT("Vector2D") },
			{ TEXT("fvector2d"),    TEXT("Vector2D") },
			{ TEXT("gameplaytag"),      TEXT("GameplayTag") },
			{ TEXT("fgameplaytag"),     TEXT("GameplayTag") },
			{ TEXT("gameplaytagcontainer"), TEXT("GameplayTagContainer") },
			{ TEXT("fgameplaytagcontainer"), TEXT("GameplayTagContainer") },
		};

		const FString* Alias = StructAliases.Find(LowerType);
		if (Alias)
		{
			StructName = *Alias;
		}
		else if (StructName.Len() > 1 && StructName[0] == 'F' && FChar::IsUpper(StructName[1]))
		{
			StructName = StructName.Mid(1);
		}

		UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *(FString(TEXT("/Script/CoreUObject.")) + StructName));
		if (!Struct)
		{
			Struct = FindObject<UScriptStruct>(nullptr, *(FString(TEXT("/Script/GameplayTags.")) + StructName));
		}
		if (!Struct)
		{
			// Broad search
			for (TObjectIterator<UScriptStruct> It; It; ++It)
			{
				if (It->GetName() == StructName)
				{
					Struct = *It;
					break;
				}
			}
		}

		if (Struct)
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			PinType.PinSubCategoryObject = Struct;
		}
		else
		{
			// Default to real/float
			PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		}
	}

	return PinType;
}

UEdGraph* FBlueprintHandlers::FindGraph(UBlueprint* Blueprint, const FString& GraphName)
{
	if (!Blueprint) return nullptr;

	// Search ALL graphs (UbergraphPages, FunctionGraphs, AnimGraphs, etc.)
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph && Graph->GetName() == GraphName)
		{
			return Graph;
		}
	}
	return nullptr;
}

UEdGraphNode* FBlueprintHandlers::FindNodeByGuidOrName(UEdGraph* Graph, const FString& NodeId)
{
	if (!Graph) return nullptr;

	// Try to parse as GUID first
	FGuid SearchGuid;
	if (FGuid::Parse(NodeId, SearchGuid))
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == SearchGuid)
			{
				return Node;
			}
		}
	}

	// Fallback: search by name/title
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;
		if (Node->GetName() == NodeId)
		{
			return Node;
		}
		if (Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString() == NodeId)
		{
			return Node;
		}
	}

	return nullptr;
}

TSharedPtr<FJsonValue> FBlueprintHandlers::CreateBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString ParentClassName = OptionalString(Params, TEXT("parentClass"), TEXT("Actor"));

	// Find parent class -- try multiple resolution strategies
	UClass* ParentClass = nullptr;

	// 1. Try silent short-name search first (handles "Actor", "AActor", "UAnimInstance" etc.)
	ParentClass = FindClassByShortName(ParentClassName);

	// 2. Try as full class path (e.g. "/Script/Engine.Actor" or "/Script/MyModule.MyClass")
	if (!ParentClass)
	{
		ParentClass = LoadObject<UClass>(nullptr, *ParentClassName);
	}

	if (!ParentClass)
	{
		return MCPError(FString::Printf(
			TEXT("Parent class not found: '%s'. Try the full path (e.g. '/Script/Engine.Actor') or the class name without prefix (e.g. 'Actor', 'Pawn', 'Character')."),
			*ParentClassName));
	}

	// Create blueprint
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	FString PackageName;
	FString AssetName;
	AssetPath.Split(TEXT("/"), &PackageName, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);

	// Idempotent: if asset already exists, return it
	UBlueprint* ExistingBP = LoadBlueprint(AssetPath);
	if (ExistingBP)
	{
		FString ObjectPath = ExistingBP->GetPathName();
		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("objectPath"), ObjectPath);
		Result->SetStringField(TEXT("className"), ExistingBP->GetName());
		if (ExistingBP->ParentClass)
		{
			Result->SetStringField(TEXT("parentClass"), ExistingBP->ParentClass->GetPathName());
		}
		Result->SetBoolField(TEXT("alreadyExisted"), true);
		return MCPResult(Result);
	}

	UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
	BlueprintFactory->ParentClass = ParentClass;
	UBlueprint* NewBlueprint = Cast<UBlueprint>(AssetTools.CreateAsset(AssetName, PackageName, UBlueprint::StaticClass(), BlueprintFactory));
	if (!NewBlueprint)
	{
		return MCPError(TEXT("Failed to create Blueprint"));
	}

	FKismetEditorUtilities::CompileBlueprint(NewBlueprint);

	// Return both the package path and canonical object path
	FString ObjectPath = NewBlueprint->GetPathName();
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("objectPath"), ObjectPath);
	Result->SetStringField(TEXT("className"), NewBlueprint->GetName());
	Result->SetStringField(TEXT("parentClass"), ParentClass->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ReadBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("className"), Blueprint->GetName());
	if (Blueprint->ParentClass)
	{
		Result->SetStringField(TEXT("parentClass"), Blueprint->ParentClass->GetName());
	}

	// Enumerate SCS components
	TArray<TSharedPtr<FJsonValue>> ComponentsArray;
	if (USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript)
	{
		// Build child->parent map from the tree
		TMap<USCS_Node*, USCS_Node*> ParentMap;
		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			if (!Node) continue;
			for (USCS_Node* Child : Node->ChildNodes)
			{
				if (Child) ParentMap.Add(Child, Node);
			}
		}

		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			if (!Node || !Node->ComponentTemplate) continue;

			UActorComponent* Template = Node->ComponentTemplate;
			TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
			CompObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
			CompObj->SetStringField(TEXT("class"), Template->GetClass()->GetName());

			// Parent component
			if (USCS_Node** ParentPtr = ParentMap.Find(Node))
			{
				CompObj->SetStringField(TEXT("parent"), (*ParentPtr)->GetVariableName().ToString());
			}

			// Transform for SceneComponents
			if (USceneComponent* SceneComp = Cast<USceneComponent>(Template))
			{
				TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
				Loc->SetNumberField(TEXT("x"), SceneComp->GetRelativeLocation().X);
				Loc->SetNumberField(TEXT("y"), SceneComp->GetRelativeLocation().Y);
				Loc->SetNumberField(TEXT("z"), SceneComp->GetRelativeLocation().Z);
				CompObj->SetObjectField(TEXT("relativeLocation"), Loc);

				TSharedPtr<FJsonObject> Rot = MakeShared<FJsonObject>();
				Rot->SetNumberField(TEXT("pitch"), SceneComp->GetRelativeRotation().Pitch);
				Rot->SetNumberField(TEXT("yaw"), SceneComp->GetRelativeRotation().Yaw);
				Rot->SetNumberField(TEXT("roll"), SceneComp->GetRelativeRotation().Roll);
				CompObj->SetObjectField(TEXT("relativeRotation"), Rot);

				TSharedPtr<FJsonObject> Scale = MakeShared<FJsonObject>();
				Scale->SetNumberField(TEXT("x"), SceneComp->GetRelativeScale3D().X);
				Scale->SetNumberField(TEXT("y"), SceneComp->GetRelativeScale3D().Y);
				Scale->SetNumberField(TEXT("z"), SceneComp->GetRelativeScale3D().Z);
				CompObj->SetObjectField(TEXT("relativeScale3D"), Scale);
			}

			// StaticMesh info
			if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Template))
			{
				if (UStaticMesh* Mesh = SMC->GetStaticMesh())
				{
					CompObj->SetStringField(TEXT("staticMesh"), Mesh->GetPathName());
				}
				// Material overrides
				TArray<TSharedPtr<FJsonValue>> Mats;
				for (int32 i = 0; i < SMC->GetNumMaterials(); i++)
				{
					if (UMaterialInterface* Mat = SMC->GetMaterial(i))
					{
						Mats.Add(MakeShared<FJsonValueString>(Mat->GetPathName()));
					}
					else
					{
						Mats.Add(MakeShared<FJsonValueNull>());
					}
				}
				if (Mats.Num() > 0)
				{
					CompObj->SetArrayField(TEXT("materials"), Mats);
				}
			}

			// SkeletalMesh info
			if (USkeletalMeshComponent* SkMC = Cast<USkeletalMeshComponent>(Template))
			{
				if (USkeletalMesh* Mesh = SkMC->GetSkeletalMeshAsset())
				{
					CompObj->SetStringField(TEXT("skeletalMesh"), Mesh->GetPathName());
				}
				TArray<TSharedPtr<FJsonValue>> Mats;
				for (int32 i = 0; i < SkMC->GetNumMaterials(); i++)
				{
					if (UMaterialInterface* Mat = SkMC->GetMaterial(i))
					{
						Mats.Add(MakeShared<FJsonValueString>(Mat->GetPathName()));
					}
					else
					{
						Mats.Add(MakeShared<FJsonValueNull>());
					}
				}
				if (Mats.Num() > 0)
				{
					CompObj->SetArrayField(TEXT("materials"), Mats);
				}
			}

			ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
		}
	}
	Result->SetArrayField(TEXT("components"), ComponentsArray);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::AddVariable(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString VarName;
	if (auto Err = RequireString(Params, TEXT("name"), VarName)) return Err;

	FString VarType = OptionalString(Params, TEXT("type"), TEXT("Float"));

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Create pin type
	FEdGraphPinType PinType = MakePinType(VarType);

	// Use FBlueprintEditorUtils to add variable
	FName VarNameFName(*VarName);
	bool bSuccess = FBlueprintEditorUtils::AddMemberVariable(Blueprint, VarNameFName, PinType);

	if (bSuccess)
	{
		// Compile and save
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		// Save asset
		UPackage* Package = Blueprint->GetOutermost();
		if (Package)
		{
			Package->MarkPackageDirty();
			FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
		}

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("variableName"), VarName);
		Result->SetStringField(TEXT("variableType"), VarType);
		return MCPResult(Result);
	}
	else
	{
		return MCPError(TEXT("Failed to add variable - FBlueprintEditorUtils::AddMemberVariable returned false"));
	}
}

TSharedPtr<FJsonValue> FBlueprintHandlers::AddComponent(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString ComponentClass;
	if (auto Err = RequireString(Params, TEXT("componentClass"), ComponentClass)) return Err;

	FString ComponentName = OptionalString(Params, TEXT("componentName"), ComponentClass);

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Find component class
	UClass* CompClass = FindObject<UClass>(nullptr, *ComponentClass);
	if (!CompClass)
	{
		CompClass = FindObject<UClass>(nullptr, *(TEXT("U") + ComponentClass));
	}

	if (!CompClass)
	{
		return MCPError(FString::Printf(TEXT("Component class not found: %s"), *ComponentClass));
	}

	// Try using SubobjectDataSubsystem (UE5 method)
	bool bSuccess = false;
	if (USubobjectDataSubsystem* Subsystem = GEngine->GetEngineSubsystem<USubobjectDataSubsystem>())
	{
		// Get blueprint handles using K2 function
		TArray<FSubobjectDataHandle> Handles;
		Subsystem->K2_GatherSubobjectDataForBlueprint(Blueprint, Handles);
		if (Handles.Num() > 0)
		{
			FSubobjectDataHandle RootHandle = Handles[0];

			FAddNewSubobjectParams AddParams;
			AddParams.ParentHandle = RootHandle;
			AddParams.NewClass = CompClass;
			AddParams.BlueprintContext = Blueprint;

			FText FailReason;
			FSubobjectDataHandle NewHandle = Subsystem->AddNewSubobject(AddParams, FailReason);
			if (NewHandle.IsValid())
			{
				// Rename if needed
				if (ComponentName != ComponentClass)
				{
					Subsystem->RenameSubobject(NewHandle, FText::FromString(ComponentName));
				}
				bSuccess = true;
			}
		}
	}

	if (bSuccess)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		// Save asset
		UPackage* Package = Blueprint->GetOutermost();
		if (Package)
		{
			Package->MarkPackageDirty();
			FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
		}

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("componentClass"), ComponentClass);
		Result->SetStringField(TEXT("componentName"), ComponentName);
		return MCPResult(Result);
	}
	else
	{
		return MCPError(TEXT("Failed to add component via SubobjectDataSubsystem"));
	}
}

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

	// Use FBlueprintEditorUtils to add interface
	FTopLevelAssetPath InterfaceAssetPath(InterfaceClass->GetPathName());
	FBlueprintEditorUtils::ImplementNewInterface(Blueprint, InterfaceAssetPath);
	bool bSuccess = true;

	if (bSuccess)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		// Save asset
		UPackage* Package = Blueprint->GetOutermost();
		if (Package)
		{
			Package->MarkPackageDirty();
			FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
		}

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
		Result->SetStringField(TEXT("interfacePath"), InterfacePathStr);
		return MCPResult(Result);
	}
	else
	{
		return MCPError(TEXT("Failed to add interface - FBlueprintEditorUtils::AddInterfaceToBlueprint returned false"));
	}
}

TSharedPtr<FJsonValue> FBlueprintHandlers::CompileBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::SearchNodeTypes(const TSharedPtr<FJsonObject>& Params)
{
	FString Query;
	if (auto Err = RequireString(Params, TEXT("query"), Query)) return Err;

	TArray<TSharedPtr<FJsonValue>> MatchingTypes;
	FString LowerQuery = Query.ToLower();

	// Search UFunction names across common engine classes
	TArray<UClass*> ClassesToSearch;
	ClassesToSearch.Add(AActor::StaticClass());
	ClassesToSearch.Add(UGameplayStatics::StaticClass());
	ClassesToSearch.Add(UKismetSystemLibrary::StaticClass());
	ClassesToSearch.Add(UKismetMathLibrary::StaticClass());
	ClassesToSearch.Add(UKismetStringLibrary::StaticClass());

	for (UClass* SearchClass : ClassesToSearch)
	{
		if (!SearchClass) continue;
		for (TFieldIterator<UFunction> FuncIt(SearchClass); FuncIt; ++FuncIt)
		{
			UFunction* Func = *FuncIt;
			if (!Func) continue;

			FString FuncName = Func->GetName();
			if (FuncName.ToLower().Contains(LowerQuery))
			{
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), FuncName);
				Entry->SetStringField(TEXT("class"), SearchClass->GetName());
				Entry->SetStringField(TEXT("fullPath"), Func->GetPathName());
				MatchingTypes.Add(MakeShared<FJsonValueObject>(Entry));
			}
		}
	}

	// Also search AnimGraph node types and other UEdGraphNode subclasses
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (!It->IsChildOf(UEdGraphNode::StaticClass())) continue;
		if (*It == UEdGraphNode::StaticClass()) continue;

		FString ClassName = It->GetName();
		if (ClassName.ToLower().Contains(LowerQuery))
		{
			// Avoid duplicates from function search above
			bool bAlreadyListed = false;
			for (const TSharedPtr<FJsonValue>& Existing : MatchingTypes)
			{
				if (Existing->AsObject()->GetStringField(TEXT("name")) == ClassName)
				{
					bAlreadyListed = true;
					break;
				}
			}
			if (!bAlreadyListed)
			{
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), ClassName);
				Entry->SetStringField(TEXT("class"), It->GetSuperClass() ? It->GetSuperClass()->GetName() : TEXT(""));
				Entry->SetStringField(TEXT("fullPath"), It->GetPathName());
				Entry->SetStringField(TEXT("type"), TEXT("graphNode"));
				MatchingTypes.Add(MakeShared<FJsonValueObject>(Entry));
			}
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("results"), MatchingTypes);
	Result->SetNumberField(TEXT("count"), MatchingTypes.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ListNodeTypes(const TSharedPtr<FJsonObject>& Params)
{
	FString Category = OptionalString(Params, TEXT("category"), TEXT("Utilities"));

	TArray<TSharedPtr<FJsonValue>> NodeTypes;
	FString LowerCategory = Category.ToLower();

	// Map categories to relevant classes and function sets
	TArray<UClass*> ClassesToSearch;

	if (LowerCategory == TEXT("utilities"))
	{
		ClassesToSearch.Add(UKismetSystemLibrary::StaticClass());
	}
	else if (LowerCategory == TEXT("math"))
	{
		ClassesToSearch.Add(UKismetMathLibrary::StaticClass());
	}
	else if (LowerCategory == TEXT("string"))
	{
		ClassesToSearch.Add(UKismetStringLibrary::StaticClass());
	}
	else if (LowerCategory == TEXT("gameplay"))
	{
		ClassesToSearch.Add(UGameplayStatics::StaticClass());
	}
	else if (LowerCategory == TEXT("actor"))
	{
		ClassesToSearch.Add(AActor::StaticClass());
	}
	else
	{
		// Default: search all common classes
		ClassesToSearch.Add(UKismetSystemLibrary::StaticClass());
		ClassesToSearch.Add(UKismetMathLibrary::StaticClass());
		ClassesToSearch.Add(UGameplayStatics::StaticClass());
	}

	for (UClass* SearchClass : ClassesToSearch)
	{
		if (!SearchClass) continue;
		for (TFieldIterator<UFunction> FuncIt(SearchClass); FuncIt; ++FuncIt)
		{
			UFunction* Func = *FuncIt;
			if (!Func) continue;
			if (!Func->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure)) continue;

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Func->GetName());
			Entry->SetStringField(TEXT("class"), SearchClass->GetName());
			NodeTypes.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("category"), Category);
	Result->SetArrayField(TEXT("nodeTypes"), NodeTypes);
	Result->SetNumberField(TEXT("count"), NodeTypes.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ListBlueprintVariables(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	TArray<TSharedPtr<FJsonValue>> Variables;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
		VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
		VarObj->SetStringField(TEXT("guid"), Var.VarGuid.ToString());

		// Check metadata
		if (Var.HasMetaData(FBlueprintMetadata::MD_Private))
		{
			VarObj->SetBoolField(TEXT("private"), true);
		}
		if (Var.HasMetaData(FBlueprintMetadata::MD_FunctionCategory))
		{
			VarObj->SetStringField(TEXT("category"), Var.GetMetaData(FBlueprintMetadata::MD_FunctionCategory));
		}
		if (Var.HasMetaData(FBlueprintMetadata::MD_Tooltip))
		{
			VarObj->SetStringField(TEXT("tooltip"), Var.GetMetaData(FBlueprintMetadata::MD_Tooltip));
		}

		VarObj->SetBoolField(TEXT("instanceEditable"),
			!Var.HasMetaData(FBlueprintMetadata::MD_Private) && Var.PropertyFlags & CPF_Edit);

		Variables.Add(MakeShared<FJsonValueObject>(VarObj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetArrayField(TEXT("variables"), Variables);
	Result->SetNumberField(TEXT("count"), Variables.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::SetVariableProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString VarName;
	if (auto Err = RequireString(Params, TEXT("name"), VarName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Find the variable
	FBPVariableDescription* FoundVar = nullptr;
	for (FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName.ToString() == VarName)
		{
			FoundVar = &Var;
			break;
		}
	}

	if (!FoundVar)
	{
		return MCPError(FString::Printf(TEXT("Variable not found: %s"), *VarName));
	}

	// Set instance editable
	bool bInstanceEditable = false;
	if (Params->TryGetBoolField(TEXT("instanceEditable"), bInstanceEditable))
	{
		if (bInstanceEditable)
		{
			FoundVar->PropertyFlags |= CPF_Edit;
			FoundVar->RemoveMetaData(FBlueprintMetadata::MD_Private);
		}
		else
		{
			FoundVar->PropertyFlags &= ~CPF_Edit;
		}
	}

	// Set category
	FString CategoryStr;
	if (Params->TryGetStringField(TEXT("category"), CategoryStr))
	{
		FoundVar->SetMetaData(FBlueprintMetadata::MD_FunctionCategory, *CategoryStr);
	}

	// Set tooltip
	FString TooltipStr;
	if (Params->TryGetStringField(TEXT("tooltip"), TooltipStr))
	{
		FoundVar->SetMetaData(FBlueprintMetadata::MD_Tooltip, *TooltipStr);
	}

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	UPackage* Package = Blueprint->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("variableName"), VarName);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::CreateFunction(const TSharedPtr<FJsonObject>& Params)
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

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	UPackage* Package = Blueprint->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("functionName"), FunctionName);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ListBlueprintFunctions(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	TArray<TSharedPtr<FJsonValue>> Functions;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (!Graph) continue;
		TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
		FuncObj->SetStringField(TEXT("name"), Graph->GetName());
		FuncObj->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());
		Functions.Add(MakeShared<FJsonValueObject>(FuncObj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetArrayField(TEXT("functions"), Functions);
	Result->SetNumberField(TEXT("count"), Functions.Num());
	return MCPResult(Result);
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
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
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
	else if (NodeClass == TEXT("Event"))    ResolvedClass = TEXT("K2Node_Event");
	else if (NodeClass == TEXT("GetVar"))   ResolvedClass = TEXT("K2Node_VariableGet");
	else if (NodeClass == TEXT("SetVar"))   ResolvedClass = TEXT("K2Node_VariableSet");
	else if (NodeClass == TEXT("Branch"))   ResolvedClass = TEXT("K2Node_IfThenElse");
	else if (NodeClass == TEXT("CustomEvent")) ResolvedClass = TEXT("K2Node_CustomEvent");

	// Find the UEdGraphNode subclass by name (works for K2, AnimGraph, and any other graph node types)
	UClass* NodeUClass = nullptr;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->GetName() == ResolvedClass && It->IsChildOf(UEdGraphNode::StaticClass()))
		{
			NodeUClass = *It;
			break;
		}
	}

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

			// Accept flat params: functionName, targetClass
			if (!(*NodeParams)->TryGetStringField(TEXT("functionName"), FunctionName))
				(*NodeParams)->TryGetStringField(TEXT("memberName"), FunctionName);
			if (!(*NodeParams)->TryGetStringField(TEXT("targetClass"), TargetClassName))
				(*NodeParams)->TryGetStringField(TEXT("memberParent"), TargetClassName);

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
			if (!(*NodeParams)->TryGetStringField(TEXT("variableName"), VarName))
			{
				// Also accept {"VariableReference":{"MemberName":"X"}} format
				const TSharedPtr<FJsonObject>* VarRef = nullptr;
				if ((*NodeParams)->TryGetObjectField(TEXT("VariableReference"), VarRef))
					(*VarRef)->TryGetStringField(TEXT("MemberName"), VarName);
			}
			if (!VarName.IsEmpty())
			{
				GetNode->VariableReference.SetSelfMember(FName(*VarName));
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

	// Common initialization (works for all UEdGraphNode subclasses -- K2, AnimGraph, etc.)
	TargetGraph->AddNode(NewNode, false, false);
	NewNode->CreateNewGuid();
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	UPackage* Package = Blueprint->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetStringField(TEXT("nodeClass"), NewNode->GetClass()->GetName());
	Result->SetStringField(TEXT("nodeId"), NewNode->NodeGuid.ToString());
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

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ReadBlueprintGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Find the graph in UbergraphPages and FunctionGraphs
	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);

	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	TArray<TSharedPtr<FJsonValue>> Nodes;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (!Node) continue;

		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("id"), Node->NodeGuid.ToString());
		NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		NodeObj->SetNumberField(TEXT("posX"), Node->NodePosX);
		NodeObj->SetNumberField(TEXT("posY"), Node->NodePosY);
		NodeObj->SetStringField(TEXT("comment"), Node->NodeComment);

		// List pins
		TArray<TSharedPtr<FJsonValue>> Pins;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin) continue;
			TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
			PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
			PinObj->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
			PinObj->SetBoolField(TEXT("connected"), Pin->LinkedTo.Num() > 0);
			Pins.Add(MakeShared<FJsonValueObject>(PinObj));
		}
		NodeObj->SetArrayField(TEXT("pins"), Pins);

		Nodes.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetArrayField(TEXT("nodes"), Nodes);
	Result->SetNumberField(TEXT("nodeCount"), Nodes.Num());
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

	// Add multicast delegate variable with a proper signature graph
	FName DispatcherFName(*DispatcherName);

	// Create the delegate signature graph so the compiler has a function to reference.
	// Convention: "<Name>__DelegateSignature"
	FString SigGraphName = DispatcherName + TEXT("__DelegateSignature");
	UEdGraph* SigGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint, FName(*SigGraphName),
		UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	if (SigGraph)
	{
		Blueprint->DelegateSignatureGraphs.AddUnique(SigGraph);
		SigGraph->SetFlags(RF_Transactional);
		// Schema creates the proper function entry node for us
		SigGraph->GetSchema()->CreateDefaultNodesForGraph(*SigGraph);
	}

	FEdGraphPinType PinType;
	PinType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
	if (SigGraph)
	{
		PinType.PinSubCategoryMemberReference.MemberName = SigGraph->GetFName();
		PinType.PinSubCategoryMemberReference.MemberGuid = SigGraph->GraphGuid;
	}

	bool bSuccess = FBlueprintEditorUtils::AddMemberVariable(Blueprint, DispatcherFName, PinType);

	if (bSuccess)
	{
		// Compile and save
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		UPackage* Package = Blueprint->GetOutermost();
		if (Package)
		{
			Package->MarkPackageDirty();
			FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
		}

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
		Result->SetStringField(TEXT("name"), DispatcherName);
		return MCPResult(Result);
	}
	else
	{
		return MCPError(FString::Printf(TEXT("Failed to add event dispatcher: %s"), *DispatcherName));
	}
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

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	UPackage* Package = Blueprint->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("oldName"), OldName);
	Result->SetStringField(TEXT("newName"), NewName);
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

	// Find the function graph
	UEdGraph* FoundGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetName() == FunctionName)
		{
			FoundGraph = Graph;
			break;
		}
	}

	if (!FoundGraph)
	{
		return MCPError(FString::Printf(TEXT("Function not found: %s"), *FunctionName));
	}

	FBlueprintEditorUtils::RemoveGraph(Blueprint, FoundGraph);

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	UPackage* Package = Blueprint->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("functionName"), FunctionName);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::CreateBlueprintInterface(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	// Create a Blueprint Interface asset
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

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("name"), NewInterface->GetName());
	return MCPResult(Result);
}

// ============================================================================
// NEW HANDLERS
// ============================================================================

TSharedPtr<FJsonValue> FBlueprintHandlers::ListNodeTypesDetailed(const TSharedPtr<FJsonObject>& Params)
{
	// Static catalog of common K2Node types with descriptions and categories
	struct FNodeTypeEntry
	{
		const TCHAR* Name;
		const TCHAR* Category;
		const TCHAR* Description;
		const TCHAR* ClassName;
	};

	static const FNodeTypeEntry CommonNodes[] =
	{
		// Flow Control
		{ TEXT("Branch"), TEXT("Flow Control"), TEXT("If/else conditional branch"), TEXT("K2Node_IfThenElse") },
		{ TEXT("Sequence"), TEXT("Flow Control"), TEXT("Execute multiple outputs in order"), TEXT("K2Node_ExecutionSequence") },
		{ TEXT("DoOnce"), TEXT("Flow Control"), TEXT("Execute only the first time"), TEXT("K2Node_DoOnceMultiInput") },
		{ TEXT("FlipFlop"), TEXT("Flow Control"), TEXT("Alternates between two outputs"), TEXT("K2Node_FlipFlop") },
		{ TEXT("Gate"), TEXT("Flow Control"), TEXT("Open/close execution gate"), TEXT("K2Node_Gate") },
		{ TEXT("ForEachLoop"), TEXT("Flow Control"), TEXT("Loop over array elements"), TEXT("K2Node_ForEachElementInEnum") },
		{ TEXT("WhileLoop"), TEXT("Flow Control"), TEXT("Loop while condition is true"), TEXT("K2Node_WhileLoop") },
		{ TEXT("Select"), TEXT("Flow Control"), TEXT("Select output based on index"), TEXT("K2Node_Select") },
		{ TEXT("Switch"), TEXT("Flow Control"), TEXT("Switch on value (int, string, enum, name)"), TEXT("K2Node_Switch") },
		{ TEXT("Delay"), TEXT("Flow Control"), TEXT("Wait for specified time before continuing"), TEXT("K2Node_Delay") },

		// Events
		{ TEXT("EventBeginPlay"), TEXT("Events"), TEXT("Called when play begins"), TEXT("K2Node_Event") },
		{ TEXT("EventTick"), TEXT("Events"), TEXT("Called every frame"), TEXT("K2Node_Event") },
		{ TEXT("EventActorBeginOverlap"), TEXT("Events"), TEXT("Called when an actor overlaps"), TEXT("K2Node_Event") },
		{ TEXT("EventHit"), TEXT("Events"), TEXT("Called when actor is hit"), TEXT("K2Node_Event") },
		{ TEXT("EventAnyDamage"), TEXT("Events"), TEXT("Called when any damage is received"), TEXT("K2Node_Event") },
		{ TEXT("CustomEvent"), TEXT("Events"), TEXT("User-defined custom event"), TEXT("K2Node_CustomEvent") },
		{ TEXT("EventDispatcher"), TEXT("Events"), TEXT("Multicast delegate event dispatcher"), TEXT("K2Node_CreateDelegate") },
		{ TEXT("InputAction"), TEXT("Events"), TEXT("Respond to input action"), TEXT("K2Node_InputAction") },
		{ TEXT("InputKey"), TEXT("Events"), TEXT("Respond to key press/release"), TEXT("K2Node_InputKey") },

		// Functions
		{ TEXT("CallFunction"), TEXT("Functions"), TEXT("Call a function by name"), TEXT("K2Node_CallFunction") },
		{ TEXT("PrintString"), TEXT("Functions"), TEXT("Print text to screen/log"), TEXT("K2Node_CallFunction") },
		{ TEXT("SpawnActor"), TEXT("Functions"), TEXT("Spawn an actor from class"), TEXT("K2Node_SpawnActorFromClass") },
		{ TEXT("DestroyActor"), TEXT("Functions"), TEXT("Destroy an actor"), TEXT("K2Node_CallFunction") },
		{ TEXT("GetAllActorsOfClass"), TEXT("Functions"), TEXT("Get all actors of a specific class"), TEXT("K2Node_CallFunction") },
		{ TEXT("SetTimer"), TEXT("Functions"), TEXT("Set a timer by function name or event"), TEXT("K2Node_CallFunction") },
		{ TEXT("ClearTimer"), TEXT("Functions"), TEXT("Clear/invalidate a timer"), TEXT("K2Node_CallFunction") },

		// Variables
		{ TEXT("VariableGet"), TEXT("Variables"), TEXT("Get the value of a variable"), TEXT("K2Node_VariableGet") },
		{ TEXT("VariableSet"), TEXT("Variables"), TEXT("Set the value of a variable"), TEXT("K2Node_VariableSet") },
		{ TEXT("MakeArray"), TEXT("Variables"), TEXT("Construct an array from elements"), TEXT("K2Node_MakeArray") },
		{ TEXT("MakeStruct"), TEXT("Variables"), TEXT("Construct a struct from members"), TEXT("K2Node_MakeStruct") },
		{ TEXT("BreakStruct"), TEXT("Variables"), TEXT("Break a struct into its members"), TEXT("K2Node_BreakStruct") },

		// Math
		{ TEXT("Add"), TEXT("Math"), TEXT("Add two values (int, float, vector)"), TEXT("K2Node_CallFunction") },
		{ TEXT("Subtract"), TEXT("Math"), TEXT("Subtract two values"), TEXT("K2Node_CallFunction") },
		{ TEXT("Multiply"), TEXT("Math"), TEXT("Multiply two values"), TEXT("K2Node_CallFunction") },
		{ TEXT("Divide"), TEXT("Math"), TEXT("Divide two values"), TEXT("K2Node_CallFunction") },
		{ TEXT("RandomFloat"), TEXT("Math"), TEXT("Generate random float in range"), TEXT("K2Node_CallFunction") },
		{ TEXT("Clamp"), TEXT("Math"), TEXT("Clamp value between min and max"), TEXT("K2Node_CallFunction") },
		{ TEXT("Lerp"), TEXT("Math"), TEXT("Linear interpolation"), TEXT("K2Node_CallFunction") },
		{ TEXT("VectorLength"), TEXT("Math"), TEXT("Get length of a vector"), TEXT("K2Node_CallFunction") },
		{ TEXT("Normalize"), TEXT("Math"), TEXT("Normalize a vector"), TEXT("K2Node_CallFunction") },

		// Casting & Type
		{ TEXT("Cast"), TEXT("Casting"), TEXT("Cast to a specific class"), TEXT("K2Node_DynamicCast") },
		{ TEXT("IsValid"), TEXT("Casting"), TEXT("Check if object reference is valid"), TEXT("K2Node_CallFunction") },
		{ TEXT("ClassIsChildOf"), TEXT("Casting"), TEXT("Check class inheritance"), TEXT("K2Node_CallFunction") },

		// String
		{ TEXT("Format"), TEXT("String"), TEXT("Format text with arguments"), TEXT("K2Node_FormatText") },
		{ TEXT("Append"), TEXT("String"), TEXT("Concatenate strings"), TEXT("K2Node_CallFunction") },
		{ TEXT("Contains"), TEXT("String"), TEXT("Check if string contains substring"), TEXT("K2Node_CallFunction") },

		// Utility
		{ TEXT("CreateWidget"), TEXT("Utility"), TEXT("Create a UMG widget instance"), TEXT("K2Node_CreateWidget") },
		{ TEXT("Macro"), TEXT("Utility"), TEXT("Instance of a macro graph"), TEXT("K2Node_MacroInstance") },
		{ TEXT("Comment"), TEXT("Utility"), TEXT("Comment box for organizing graphs"), TEXT("EdGraphNode_Comment") },
		{ TEXT("Reroute"), TEXT("Utility"), TEXT("Reroute node for cleaner wiring"), TEXT("K2Node_Knot") },
	};

	FString FilterCategory = OptionalString(Params, TEXT("category"));
	FString LowerFilter = FilterCategory.ToLower();

	TArray<TSharedPtr<FJsonValue>> NodeTypesArray;
	for (const FNodeTypeEntry& Entry : CommonNodes)
	{
		if (!LowerFilter.IsEmpty())
		{
			FString EntryCat = FString(Entry.Category).ToLower();
			if (!EntryCat.Contains(LowerFilter))
			{
				continue;
			}
		}

		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("name"), Entry.Name);
		NodeObj->SetStringField(TEXT("category"), Entry.Category);
		NodeObj->SetStringField(TEXT("description"), Entry.Description);
		NodeObj->SetStringField(TEXT("className"), Entry.ClassName);
		NodeTypesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("nodeTypes"), NodeTypesArray);
	Result->SetNumberField(TEXT("count"), NodeTypesArray.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::SearchCallableFunctions(const TSharedPtr<FJsonObject>& Params)
{
	FString Query;
	if (auto Err = RequireString(Params, TEXT("query"), Query)) return Err;

	int32 MaxResults = OptionalInt(Params, TEXT("maxResults"), 50);

	FString LowerQuery = Query.ToLower();

	// Build the list of library classes to search
	TArray<UClass*> LibraryClasses;
	LibraryClasses.Add(UKismetSystemLibrary::StaticClass());
	LibraryClasses.Add(UGameplayStatics::StaticClass());
	LibraryClasses.Add(UKismetMathLibrary::StaticClass());
	LibraryClasses.Add(UKismetStringLibrary::StaticClass());
	LibraryClasses.Add(UKismetArrayLibrary::StaticClass());
	LibraryClasses.Add(AActor::StaticClass());
	LibraryClasses.Add(APawn::StaticClass());
	LibraryClasses.Add(APlayerController::StaticClass());

	// Optionally filter by a specific class
	FString TargetClassName = OptionalString(Params, TEXT("targetClass"));
	if (!TargetClassName.IsEmpty())
	{
		UClass* TargetClass = FindObject<UClass>(nullptr, *TargetClassName);
		if (!TargetClass)
		{
			TargetClass = FindObject<UClass>(nullptr, *(TEXT("U") + TargetClassName));
		}
		if (!TargetClass)
		{
			TargetClass = FindObject<UClass>(nullptr, *(TEXT("A") + TargetClassName));
		}
		if (TargetClass)
		{
			LibraryClasses.Empty();
			LibraryClasses.Add(TargetClass);
		}
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;

	for (UClass* SearchClass : LibraryClasses)
	{
		if (!SearchClass) continue;

		for (TFieldIterator<UFunction> FuncIt(SearchClass); FuncIt; ++FuncIt)
		{
			UFunction* Func = *FuncIt;
			if (!Func) continue;

			// Only include blueprint-callable functions
			if (!Func->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure)) continue;

			FString FuncName = Func->GetName();
			FString LowerFuncName = FuncName.ToLower();

			if (!LowerFuncName.Contains(LowerQuery)) continue;

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), FuncName);
			Entry->SetStringField(TEXT("class"), SearchClass->GetName());
			Entry->SetStringField(TEXT("fullPath"), Func->GetPathName());

			// Pure vs impure
			Entry->SetBoolField(TEXT("isPure"), Func->HasAnyFunctionFlags(FUNC_BlueprintPure));
			Entry->SetBoolField(TEXT("isStatic"), Func->HasAnyFunctionFlags(FUNC_Static));

			// Collect parameters info
			TArray<TSharedPtr<FJsonValue>> ParamsArray;
			FString ReturnType;
			for (TFieldIterator<FProperty> PropIt(Func); PropIt; ++PropIt)
			{
				FProperty* Prop = *PropIt;
				if (!Prop) continue;

				if (Prop->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					ReturnType = Prop->GetCPPType();
				}
				else if (Prop->HasAnyPropertyFlags(CPF_Parm))
				{
					TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
					ParamObj->SetStringField(TEXT("name"), Prop->GetName());
					ParamObj->SetStringField(TEXT("type"), Prop->GetCPPType());
					ParamObj->SetBoolField(TEXT("isOutput"), Prop->HasAnyPropertyFlags(CPF_OutParm));
					ParamsArray.Add(MakeShared<FJsonValueObject>(ParamObj));
				}
			}
			Entry->SetArrayField(TEXT("parameters"), ParamsArray);
			if (!ReturnType.IsEmpty())
			{
				Entry->SetStringField(TEXT("returnType"), ReturnType);
			}

			// Tooltip from metadata
			FString Tooltip = Func->GetMetaData(TEXT("ToolTip"));
			if (!Tooltip.IsEmpty())
			{
				Entry->SetStringField(TEXT("tooltip"), Tooltip);
			}

			ResultsArray.Add(MakeShared<FJsonValueObject>(Entry));

			if (ResultsArray.Num() >= MaxResults)
			{
				break;
			}
		}

		if (ResultsArray.Num() >= MaxResults)
		{
			break;
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("results"), ResultsArray);
	Result->SetNumberField(TEXT("count"), ResultsArray.Num());
	Result->SetStringField(TEXT("query"), Query);
	return MCPResult(Result);
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

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
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

	// Use the graph's own schema (K2 for EventGraphs, AnimationGraph schema for AnimGraphs, etc.)
	const UEdGraphSchema* Schema = TargetGraph->GetSchema();
	if (!Schema)
	{
		return MCPError(TEXT("Graph has no schema"));
	}

	bool bConnected = Schema->TryCreateConnection(SourcePin, TargetPin);

	if (bConnected)
	{
		// Compile and save
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		UPackage* Package = Blueprint->GetOutermost();
		if (Package)
		{
			Package->MarkPackageDirty();
			FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
		}

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("graphName"), GraphName);
		Result->SetStringField(TEXT("sourceNodeId"), SourceNodeId);
		Result->SetStringField(TEXT("sourcePinName"), SourcePinName);
		Result->SetStringField(TEXT("targetNodeId"), TargetNodeId);
		Result->SetStringField(TEXT("targetPinName"), TargetPinName);
		return MCPResult(Result);
	}
	else
	{
		// Get more info about why it failed
		FString ErrorMsg = TEXT("TryCreateConnection failed. Pins may be incompatible.");
		FPinConnectionResponse Response = Schema->CanCreateConnection(SourcePin, TargetPin);
		if (!Response.Message.IsEmpty())
		{
			ErrorMsg = FString::Printf(TEXT("Connection failed: %s"), *Response.Message.ToString());
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
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);
	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	// Find the node
	UEdGraphNode* NodeToDelete = FindNodeByGuidOrName(TargetGraph, NodeId);
	if (!NodeToDelete)
	{
		return MCPError(FString::Printf(TEXT("Node not found: %s"), *NodeId));
	}

	// Break all pin links before removing
	NodeToDelete->BreakAllNodeLinks();

	// Remove node from graph
	TargetGraph->RemoveNode(NodeToDelete);

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	UPackage* Package = Blueprint->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetStringField(TEXT("nodeId"), NodeId);
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
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
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

	if (TargetPin)
	{
		// Set pin default value using the graph's own schema
		const UEdGraphSchema* Schema = TargetGraph->GetSchema();
		if (Schema)
		{
			Schema->TrySetDefaultValue(*TargetPin, DefaultValue);
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
			FinalProp->ImportText_Direct(*DefaultValue, ValuePtr, nullptr, PPF_None);
			TargetNode->PostEditChange();
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
	UPackage* Package = Blueprint->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetStringField(TEXT("nodeId"), NodeId);
	Result->SetStringField(TEXT("propertyName"), PinName);
	Result->SetStringField(TEXT("value"), DefaultValue);
	Result->SetStringField(TEXT("setVia"), bSetViaPin ? TEXT("pin") : TEXT("property"));
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_component_property -- Set a property on an SCS component template
// Params: assetPath, componentName, propertyName, value
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::SetComponentProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString ComponentName;
	if (auto Err = RequireString(Params, TEXT("componentName"), ComponentName)) return Err;

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	FString Value;
	if (auto Err = RequireString(Params, TEXT("value"), Value)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	if (!SCS)
	{
		return MCPError(TEXT("Blueprint has no SimpleConstructionScript (not an Actor blueprint?)"));
	}

	// Find the SCS node by variable name or component template name
	USCS_Node* TargetNode = nullptr;
	for (USCS_Node* Node : SCS->GetAllNodes())
	{
		if (!Node || !Node->ComponentTemplate) continue;
		if (Node->GetVariableName().ToString() == ComponentName ||
			Node->ComponentTemplate->GetName() == ComponentName)
		{
			TargetNode = Node;
			break;
		}
	}

	UActorComponent* Template = TargetNode ? TargetNode->ComponentTemplate : nullptr;

	// If not found in SCS, search inherited components on the CDO
	if (!Template && Blueprint->GeneratedClass)
	{
		UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
		if (CDO)
		{
			TInlineComponentArray<UActorComponent*> Components;
			if (AActor* ActorCDO = Cast<AActor>(CDO))
			{
				ActorCDO->GetComponents(Components);
			}
			for (UActorComponent* Comp : Components)
			{
				if (Comp && (Comp->GetName() == ComponentName ||
					Comp->GetName().StartsWith(ComponentName + TEXT("_")) ||
					Comp->GetFName().ToString() == ComponentName))
				{
					Template = Comp;
					break;
				}
			}
		}
	}

	if (!Template)
	{
		// List available component names for error message
		TArray<FString> Names;
		if (SCS)
		{
			for (USCS_Node* Node : SCS->GetAllNodes())
			{
				if (Node && Node->ComponentTemplate)
				{
					Names.Add(Node->GetVariableName().ToString());
				}
			}
		}
		if (Blueprint->GeneratedClass)
		{
			if (AActor* ActorCDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()))
			{
				TInlineComponentArray<UActorComponent*> Components;
				ActorCDO->GetComponents(Components);
				for (UActorComponent* Comp : Components)
				{
					if (Comp) Names.AddUnique(Comp->GetName());
				}
			}
		}
		return MCPError(FString::Printf(
			TEXT("Component '%s' not found. Available: [%s]"),
			*ComponentName, *FString::Join(Names, TEXT(", "))));
	}

	// Navigate dotted property paths (e.g. "RelativeLocation.X")
	TArray<FString> PathParts;
	PropertyName.ParseIntoArray(PathParts, TEXT("."));

	UStruct* CurrentStruct = Template->GetClass();
	void* CurrentContainer = Template;
	FProperty* FinalProp = nullptr;

	for (int32 i = 0; i < PathParts.Num(); i++)
	{
		FProperty* Prop = CurrentStruct->FindPropertyByName(FName(*PathParts[i]));
		if (!Prop) break;

		if (i < PathParts.Num() - 1)
		{
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

	if (!FinalProp)
	{
		// List available properties for error message
		TArray<FString> PropNames;
		for (TFieldIterator<FProperty> It(Template->GetClass()); It; ++It)
		{
			PropNames.Add(It->GetName());
		}
		return MCPError(FString::Printf(
			TEXT("Property '%s' not found on %s. Properties: [%s]"),
			*PropertyName, *Template->GetClass()->GetName(),
			*FString::Join(PropNames, TEXT(", "))));
	}

	// For object reference properties, try loading the asset by path first
	void* ValuePtr = FinalProp->ContainerPtrToValuePtr<void>(CurrentContainer);

	if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(FinalProp))
	{
		UObject* LoadedObj = LoadObject<UObject>(nullptr, *Value);
		if (!LoadedObj)
		{
			// Try with common prefixes stripped
			LoadedObj = LoadObject<UObject>(nullptr, *Value);
		}
		if (LoadedObj)
		{
			ObjProp->SetObjectPropertyValue(ValuePtr, LoadedObj);
		}
		else
		{
			return MCPError(FString::Printf(
				TEXT("Could not load object at '%s' for property '%s'"), *Value, *PropertyName));
		}
	}
	else if (FSoftObjectProperty* SoftProp = CastField<FSoftObjectProperty>(FinalProp))
	{
		FSoftObjectPath SoftPath(Value);
		SoftProp->SetPropertyValue(ValuePtr, FSoftObjectPtr(SoftPath));
	}
	else
	{
		// Generic: use ImportText for value types (int, float, bool, FVector, FName, etc.)
		FinalProp->ImportText_Direct(*Value, ValuePtr, nullptr, PPF_None);
	}

	Template->PostEditChange();

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	UPackage* Package = Blueprint->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("componentName"), ComponentName);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("value"), Value);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_class_default -- Set a UPROPERTY on a Blueprint's Class Default Object
// Params: assetPath, propertyName, value
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::SetClassDefault(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	FString Value;
	if (auto Err = RequireString(Params, TEXT("value"), Value)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	UClass* GenClass = Blueprint->GeneratedClass;
	if (!GenClass)
	{
		return MCPError(TEXT("Blueprint has no GeneratedClass (needs compilation first?)"));
	}

	UObject* CDO = GenClass->GetDefaultObject();
	if (!CDO)
	{
		return MCPError(TEXT("Could not get Class Default Object"));
	}

	// Navigate dotted property paths (e.g. "EjectConfigs.Cork.Force")
	TArray<FString> PathParts;
	PropertyName.ParseIntoArray(PathParts, TEXT("."));

	UStruct* CurrentStruct = GenClass;
	void* CurrentContainer = CDO;
	FProperty* FinalProp = nullptr;

	for (int32 i = 0; i < PathParts.Num(); i++)
	{
		FProperty* Prop = CurrentStruct->FindPropertyByName(FName(*PathParts[i]));
		if (!Prop) break;

		if (i < PathParts.Num() - 1)
		{
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

	if (!FinalProp)
	{
		TArray<FString> PropNames;
		for (TFieldIterator<FProperty> It(GenClass); It; ++It)
		{
			PropNames.Add(It->GetName());
		}
		return MCPError(FString::Printf(
			TEXT("Property '%s' not found on %s. Properties: [%s]"),
			*PropertyName, *GenClass->GetName(),
			*FString::Join(PropNames, TEXT(", "))));
	}

	void* ValuePtr = FinalProp->ContainerPtrToValuePtr<void>(CurrentContainer);

	if (FClassProperty* ClassProp = CastField<FClassProperty>(FinalProp))
	{
		// TSubclassOf<T> -- load the class by path or short name
		UClass* ClassVal = LoadObject<UClass>(nullptr, *Value);
		if (!ClassVal) ClassVal = FindClassByShortName(Value);
		if (ClassVal)
		{
			ClassProp->SetObjectPropertyValue(ValuePtr, ClassVal);
		}
		else
		{
			return MCPError(FString::Printf(
				TEXT("Could not find class '%s' for property '%s'"), *Value, *PropertyName));
		}
	}
	else if (FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(FinalProp))
	{
		SoftClassProp->SetPropertyValue(ValuePtr, FSoftObjectPtr(FSoftObjectPath(Value)));
	}
	else if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(FinalProp))
	{
		UObject* LoadedObj = LoadObject<UObject>(nullptr, *Value);
		if (LoadedObj)
		{
			ObjProp->SetObjectPropertyValue(ValuePtr, LoadedObj);
		}
		else
		{
			return MCPError(FString::Printf(
				TEXT("Could not load object at '%s' for property '%s'"), *Value, *PropertyName));
		}
	}
	else if (FSoftObjectProperty* SoftProp = CastField<FSoftObjectProperty>(FinalProp))
	{
		FSoftObjectPath SoftPath(Value);
		SoftProp->SetPropertyValue(ValuePtr, FSoftObjectPtr(SoftPath));
	}
	else
	{
		// Generic: ImportText handles FName, int, float, bool, FVector, FGameplayTag,
		// FGameplayTagContainer, TArray, TMap, etc.
		const TCHAR* ImportResult = FinalProp->ImportText_Direct(*Value, ValuePtr, CDO, PPF_None);
		if (!ImportResult)
		{
			return MCPError(FString::Printf(
				TEXT("ImportText failed for property '%s' with value '%s'. Check UE text format."), *PropertyName, *Value));
		}
	}

	CDO->PostEditChange();

	// Save
	UPackage* Package = Blueprint->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("value"), Value);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// remove_component -- Remove an SCS component from a Blueprint
// Params: assetPath, componentName
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::RemoveComponent(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString ComponentName;
	if (auto Err = RequireString(Params, TEXT("componentName"), ComponentName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	if (!SCS)
	{
		return MCPError(TEXT("Blueprint has no SimpleConstructionScript (not an Actor blueprint?)"));
	}

	// Find the SCS node by variable name or component template name
	USCS_Node* TargetNode = nullptr;
	for (USCS_Node* Node : SCS->GetAllNodes())
	{
		if (!Node || !Node->ComponentTemplate) continue;
		if (Node->GetVariableName().ToString() == ComponentName ||
			Node->ComponentTemplate->GetName() == ComponentName)
		{
			TargetNode = Node;
			break;
		}
	}

	if (!TargetNode)
	{
		TArray<FString> Names;
		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			if (Node && Node->ComponentTemplate)
			{
				Names.Add(Node->GetVariableName().ToString());
			}
		}
		return MCPError(FString::Printf(
			TEXT("Component '%s' not found. Available: [%s]"),
			*ComponentName, *FString::Join(Names, TEXT(", "))));
	}

	// Remove via SubobjectDataSubsystem if available
	bool bRemoved = false;
	if (USubobjectDataSubsystem* Subsystem = GEngine->GetEngineSubsystem<USubobjectDataSubsystem>())
	{
		TArray<FSubobjectDataHandle> Handles;
		Subsystem->K2_GatherSubobjectDataForBlueprint(Blueprint, Handles);

		FSubobjectDataHandle ContextHandle = Handles.Num() > 0 ? Handles[0] : FSubobjectDataHandle();
		for (const FSubobjectDataHandle& Handle : Handles)
		{
			const FSubobjectData* Data = Handle.GetData();
			if (Data && Data->GetComponentTemplate() == TargetNode->ComponentTemplate)
			{
				TArray<FSubobjectDataHandle> ToDelete;
				ToDelete.Add(Handle);
				int32 Removed = Subsystem->DeleteSubobjects(ContextHandle, ToDelete, Blueprint);
				bRemoved = (Removed > 0);
				break;
			}
		}
	}

	// Fallback: direct SCS removal
	if (!bRemoved)
	{
		SCS->RemoveNode(TargetNode);
		bRemoved = true;
	}

	if (bRemoved)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		UPackage* Package = Blueprint->GetOutermost();
		if (Package)
		{
			Package->MarkPackageDirty();
			FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
		}

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("componentName"), ComponentName);
		return MCPResult(Result);
	}
	else
	{
		return MCPError(TEXT("Failed to remove component"));
	}
}

// ---------------------------------------------------------------------------
// delete_variable -- Delete a member variable from a Blueprint
// Params: assetPath, name
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::DeleteVariable(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString VarName;
	if (auto Err = RequireString(Params, TEXT("name"), VarName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Check that the variable exists
	bool bFound = false;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName.ToString() == VarName)
		{
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		TArray<FString> Names;
		for (const FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			Names.Add(Var.VarName.ToString());
		}
		return MCPError(FString::Printf(
			TEXT("Variable '%s' not found. Available: [%s]"),
			*VarName, *FString::Join(Names, TEXT(", "))));
	}

	FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, FName(*VarName));

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	UPackage* Package = Blueprint->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("variableName"), VarName);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// add_function_parameter -- Add an input or output parameter to a Blueprint function
// Params: assetPath, functionName, parameterName, parameterType, isOutput?
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::AddFunctionParameter(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString FunctionName;
	if (auto Err = RequireString(Params, TEXT("functionName"), FunctionName)) return Err;

	FString ParamName;
	if (auto Err = RequireString(Params, TEXT("parameterName"), ParamName)) return Err;

	FString ParamType = OptionalString(Params, TEXT("parameterType"), TEXT("Float"));

	bool bIsOutput = OptionalBool(Params, TEXT("isOutput"), false);

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Find the function graph
	UEdGraph* FuncGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetName() == FunctionName)
		{
			FuncGraph = Graph;
			break;
		}
	}

	if (!FuncGraph)
	{
		return MCPError(FString::Printf(TEXT("Function not found: %s"), *FunctionName));
	}

	// Find the function entry node (K2Node_FunctionEntry) or result node
	UK2Node_FunctionEntry* EntryNode = nullptr;
	for (UEdGraphNode* Node : FuncGraph->Nodes)
	{
		if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
		{
			EntryNode = Entry;
			break;
		}
	}

	if (!EntryNode)
	{
		return MCPError(TEXT("Function entry node not found in graph"));
	}

	FEdGraphPinType PinType = MakePinType(ParamType);

	if (bIsOutput)
	{
		// For output parameters, find or create the function result node
		UK2Node_FunctionEntry* ResultAsEntry = nullptr; // K2Node_FunctionResult also inherits UK2Node_EditablePinBase
		UK2Node_EditablePinBase* ResultNode = nullptr;
		for (UEdGraphNode* Node : FuncGraph->Nodes)
		{
			if (Node->GetClass()->GetName() == TEXT("K2Node_FunctionResult"))
			{
				ResultNode = Cast<UK2Node_EditablePinBase>(Node);
				break;
			}
		}

		if (!ResultNode)
		{
			// Create a result node
			UClass* ResultNodeClass = nullptr;
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (It->GetName() == TEXT("K2Node_FunctionResult") && It->IsChildOf(UEdGraphNode::StaticClass()))
				{
					ResultNodeClass = *It;
					break;
				}
			}
			if (ResultNodeClass)
			{
				UEdGraphNode* NewResultNode = NewObject<UEdGraphNode>(FuncGraph, ResultNodeClass);
				FuncGraph->AddNode(NewResultNode, false, false);
				NewResultNode->CreateNewGuid();
				NewResultNode->PostPlacedNewNode();
				NewResultNode->AllocateDefaultPins();
				ResultNode = Cast<UK2Node_EditablePinBase>(NewResultNode);
			}
		}

		if (ResultNode)
		{
			TSharedPtr<FUserPinInfo> PinInfo = MakeShared<FUserPinInfo>();
			PinInfo->PinName = FName(*ParamName);
			PinInfo->PinType = PinType;
			PinInfo->DesiredPinDirection = EGPD_Input;
			ResultNode->UserDefinedPins.Add(PinInfo);
			ResultNode->ReconstructNode();
		}
	}
	else
	{
		// Input parameter: add a user-defined pin to the function entry node
		TSharedPtr<FUserPinInfo> PinInfo = MakeShared<FUserPinInfo>();
		PinInfo->PinName = FName(*ParamName);
		PinInfo->PinType = PinType;
		PinInfo->DesiredPinDirection = EGPD_Output; // Entry outputs are function inputs
		EntryNode->UserDefinedPins.Add(PinInfo);
		EntryNode->ReconstructNode();
	}

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	UPackage* Package = Blueprint->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("functionName"), FunctionName);
	Result->SetStringField(TEXT("parameterName"), ParamName);
	Result->SetStringField(TEXT("parameterType"), ParamType);
	Result->SetBoolField(TEXT("isOutput"), bIsOutput);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_variable_default -- Set the default value of a Blueprint variable
// Bypasses CDO restrictions by setting via FBlueprintEditorUtils on the BP variable description
// Params: assetPath, name, value
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::SetVariableDefault(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString VarName;
	if (auto Err = RequireString(Params, TEXT("name"), VarName)) return Err;

	FString Value;
	if (auto Err = RequireString(Params, TEXT("value"), Value)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Find the variable description
	FBPVariableDescription* FoundVar = nullptr;
	for (FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName.ToString() == VarName)
		{
			FoundVar = &Var;
			break;
		}
	}

	if (!FoundVar)
	{
		TArray<FString> Names;
		for (const FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			Names.Add(Var.VarName.ToString());
		}
		return MCPError(FString::Printf(
			TEXT("Variable '%s' not found. Available: [%s]"),
			*VarName, *FString::Join(Names, TEXT(", "))));
	}

	// Set default value string on the variable description.
	// This is the text representation that the BP serialization system uses.
	FoundVar->DefaultValue = Value;

	// Also try to set it on the CDO property if possible (for immediate reflection)
	if (Blueprint->GeneratedClass)
	{
		UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
		if (CDO)
		{
			FProperty* Prop = Blueprint->GeneratedClass->FindPropertyByName(FName(*VarName));
			if (Prop)
			{
				void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(CDO);

				if (FClassProperty* ClassProp = CastField<FClassProperty>(Prop))
				{
					UClass* ClassVal = LoadObject<UClass>(nullptr, *Value);
					if (!ClassVal) ClassVal = FindClassByShortName(Value);
					if (ClassVal) ClassProp->SetObjectPropertyValue(ValuePtr, ClassVal);
				}
				else if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
				{
					UObject* LoadedObj = LoadObject<UObject>(nullptr, *Value);
					if (LoadedObj) ObjProp->SetObjectPropertyValue(ValuePtr, LoadedObj);
				}
				else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
				{
					// For arrays (including TArray<TSubclassOf<>>), use ImportText
					Prop->ImportText_Direct(*Value, ValuePtr, CDO, PPF_None);
				}
				else
				{
					Prop->ImportText_Direct(*Value, ValuePtr, CDO, PPF_None);
				}

				CDO->PostEditChange();
			}
		}
	}

	// Compile and save
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	UPackage* Package = Blueprint->GetOutermost();
	if (Package)
	{
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("variableName"), VarName);
	Result->SetStringField(TEXT("value"), Value);
	return MCPResult(Result);
}
