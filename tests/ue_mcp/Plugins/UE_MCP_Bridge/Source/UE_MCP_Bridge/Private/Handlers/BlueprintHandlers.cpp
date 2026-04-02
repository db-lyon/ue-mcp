#include "BlueprintHandlers.h"
#include "HandlerRegistry.h"
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

// Helper: find a UClass by short name (e.g. "AnimInstance" finds UAnimInstance)
static UClass* FindClassByShortName(const FString& ClassName)
{
	UClass* PrefixedMatch = nullptr;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		const FString& Name = It->GetName();
		if (Name == ClassName) return *It;
		if (!PrefixedMatch && (Name == TEXT("U") + ClassName || Name == TEXT("A") + ClassName))
		{
			PrefixedMatch = *It;
		}
	}
	return PrefixedMatch;
}

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
}

UBlueprint* FBlueprintHandlers::LoadBlueprint(const FString& AssetPath)
{
	return LoadObject<UBlueprint>(nullptr, *AssetPath);
}

// ---------------------------------------------------------------------------
// list_blueprint_graphs — List all graphs in a blueprint (EventGraph, AnimGraph, functions, etc.)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::ListGraphs(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath) && !Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetArrayField(TEXT("graphs"), GraphsArray);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
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
	else
	{
		// Default to real/float
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
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
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath) && !Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString ParentClassName = TEXT("Actor");
	Params->TryGetStringField(TEXT("parentClass"), ParentClassName);

	// Find parent class
	UClass* ParentClass = nullptr;
	ParentClass = FindObject<UClass>(nullptr, *ParentClassName);
	if (!ParentClass)
	{
		ParentClass = FindObject<UClass>(nullptr, *(TEXT("A") + ParentClassName));
	}
	if (!ParentClass)
	{
		ParentClass = FindObject<UClass>(nullptr, *(TEXT("U") + ParentClassName));
	}

	if (!ParentClass)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Parent class not found: %s"), *ParentClassName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Create blueprint
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	FString PackageName;
	FString AssetName;
	AssetPath.Split(TEXT("/"), &PackageName, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	PackageName = PackageName.LeftChop(1); // Remove trailing /

	UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
	UBlueprint* NewBlueprint = Cast<UBlueprint>(AssetTools.CreateAsset(AssetName, PackageName, UBlueprint::StaticClass(), BlueprintFactory));
	if (!NewBlueprint)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create Blueprint"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	NewBlueprint->ParentClass = ParentClass;
	FKismetEditorUtilities::CompileBlueprint(NewBlueprint);

	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("className"), NewBlueprint->GetName());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ReadBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath) && !Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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

	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::AddVariable(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath) && !Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString VarName;
	if (!Params->TryGetStringField(TEXT("name"), VarName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString VarType = TEXT("Float");
	Params->TryGetStringField(TEXT("type"), VarType);

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("variableName"), VarName);
		Result->SetStringField(TEXT("variableType"), VarType);
		Result->SetBoolField(TEXT("success"), true);
	}
	else
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to add variable - FBlueprintEditorUtils::AddMemberVariable returned false"));
		Result->SetBoolField(TEXT("success"), false);
	}

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::AddComponent(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath) && !Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString ComponentClass;
	if (!Params->TryGetStringField(TEXT("componentClass"), ComponentClass))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'componentClass' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString ComponentName = ComponentClass;
	Params->TryGetStringField(TEXT("componentName"), ComponentName);

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find component class
	UClass* CompClass = FindObject<UClass>(nullptr, *ComponentClass);
	if (!CompClass)
	{
		CompClass = FindObject<UClass>(nullptr, *(TEXT("U") + ComponentClass));
	}

	if (!CompClass)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Component class not found: %s"), *ComponentClass));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("componentClass"), ComponentClass);
		Result->SetStringField(TEXT("componentName"), ComponentName);
		Result->SetBoolField(TEXT("success"), true);
	}
	else
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to add component via SubobjectDataSubsystem"));
		Result->SetBoolField(TEXT("success"), false);
	}

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::AddBlueprintInterface(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString BlueprintPath;
	if (!Params->TryGetStringField(TEXT("blueprintPath"), BlueprintPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'blueprintPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString InterfacePathStr;
	if (!Params->TryGetStringField(TEXT("interfacePath"), InterfacePathStr))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'interfacePath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* Blueprint = LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UClass* InterfaceClass = LoadObject<UClass>(nullptr, *InterfacePathStr);
	if (!InterfaceClass)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Interface not found: %s"), *InterfacePathStr));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

		Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
		Result->SetStringField(TEXT("interfacePath"), InterfacePathStr);
		Result->SetBoolField(TEXT("success"), true);
	}
	else
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to add interface - FBlueprintEditorUtils::AddInterfaceToBlueprint returned false"));
		Result->SetBoolField(TEXT("success"), false);
	}

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::CompileBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath) && !Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::SearchNodeTypes(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Query;
	if (!Params->TryGetStringField(TEXT("query"), Query))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'query' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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

	Result->SetArrayField(TEXT("results"), MatchingTypes);
	Result->SetNumberField(TEXT("count"), MatchingTypes.Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ListNodeTypes(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Category = TEXT("Utilities");
	Params->TryGetStringField(TEXT("category"), Category);

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

	Result->SetStringField(TEXT("category"), Category);
	Result->SetArrayField(TEXT("nodeTypes"), NodeTypes);
	Result->SetNumberField(TEXT("count"), NodeTypes.Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ListBlueprintVariables(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath) && !Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetArrayField(TEXT("variables"), Variables);
	Result->SetNumberField(TEXT("count"), Variables.Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::SetVariableProperties(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath) && !Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString VarName;
	if (!Params->TryGetStringField(TEXT("name"), VarName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Variable not found: %s"), *VarName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("variableName"), VarName);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::CreateFunction(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath) && !Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString FunctionName;
	if (!Params->TryGetStringField(TEXT("functionName"), FunctionName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'functionName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint,
		FName(*FunctionName),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass()
	);
	if (!NewGraph)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to create function: %s"), *FunctionName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("functionName"), FunctionName);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ListBlueprintFunctions(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath) && !Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetArrayField(TEXT("functions"), Functions);
	Result->SetNumberField(TEXT("count"), Functions.Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::AddNode(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath) && !Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString GraphName = TEXT("EventGraph");
	Params->TryGetStringField(TEXT("graphName"), GraphName);

	FString NodeClass;
	if (!Params->TryGetStringField(TEXT("nodeClass"), NodeClass))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'nodeClass' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find the target graph
	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);

	if (!TargetGraph)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Graph not found: %s"), *GraphName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Node class not found: %s (must be a UEdGraphNode subclass)"), *NodeClass));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Create node instance
	UEdGraphNode* NewNode = NewObject<UEdGraphNode>(TargetGraph, NodeUClass);
	if (!NewNode)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create node"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

			if (!FunctionName.IsEmpty())
			{

				UClass* TargetClass = nullptr;
				if (!TargetClassName.IsEmpty())
				{
					TargetClass = FindClassByShortName(TargetClassName);
				}
				if (!TargetClass)
				{
					TargetClass = Blueprint->ParentClass;
				}

				if (TargetClass)
				{
					UFunction* Func = TargetClass->FindFunctionByName(FName(*FunctionName));
					if (Func)
					{
						CallNode->SetFromFunction(Func);
					}
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
					// Engine event override — bind via EventReference
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
					// Custom event — just set the name
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

	// Common initialization (works for all UEdGraphNode subclasses — K2, AnimGraph, etc.)
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
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ReadBlueprintGraph(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath) && !Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString GraphName = TEXT("EventGraph");
	Params->TryGetStringField(TEXT("graphName"), GraphName);

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find the graph in UbergraphPages and FunctionGraphs
	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);

	if (!TargetGraph)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Graph not found: %s"), *GraphName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetArrayField(TEXT("nodes"), Nodes);
	Result->SetNumberField(TEXT("nodeCount"), Nodes.Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::AddEventDispatcher(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString BlueprintPath;
	if (!Params->TryGetStringField(TEXT("blueprintPath"), BlueprintPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'blueprintPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString DispatcherName;
	if (!Params->TryGetStringField(TEXT("name"), DispatcherName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* Blueprint = LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Add multicast delegate variable
	FEdGraphPinType PinType;
	PinType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;

	FName DispatcherFName(*DispatcherName);
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

		Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
		Result->SetStringField(TEXT("name"), DispatcherName);
		Result->SetBoolField(TEXT("success"), true);
	}
	else
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to add event dispatcher: %s"), *DispatcherName));
		Result->SetBoolField(TEXT("success"), false);
	}

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::RenameFunction(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath) && !Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString OldName;
	if (!Params->TryGetStringField(TEXT("oldName"), OldName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'oldName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString NewName;
	if (!Params->TryGetStringField(TEXT("newName"), NewName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'newName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Function not found: %s"), *OldName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("oldName"), OldName);
	Result->SetStringField(TEXT("newName"), NewName);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::DeleteFunction(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath) && !Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString FunctionName;
	if (!Params->TryGetStringField(TEXT("functionName"), FunctionName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'functionName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Function not found: %s"), *FunctionName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("functionName"), FunctionName);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::CreateBlueprintInterface(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath) && !Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Create a Blueprint Interface asset
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	FString PackageName;
	FString AssetName;
	AssetPath.Split(TEXT("/"), &PackageName, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	PackageName = PackageName.LeftChop(1); // Remove trailing /

	UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
	BlueprintFactory->BlueprintType = BPTYPE_Interface;
	BlueprintFactory->ParentClass = UInterface::StaticClass();

	UBlueprint* NewInterface = Cast<UBlueprint>(AssetTools.CreateAsset(AssetName, PackageName, UBlueprint::StaticClass(), BlueprintFactory));
	if (!NewInterface)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create Blueprint Interface"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FKismetEditorUtilities::CompileBlueprint(NewInterface);

	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("name"), NewInterface->GetName());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

// ============================================================================
// NEW HANDLERS
// ============================================================================

TSharedPtr<FJsonValue> FBlueprintHandlers::ListNodeTypesDetailed(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

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

	FString FilterCategory;
	Params->TryGetStringField(TEXT("category"), FilterCategory);
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

	Result->SetArrayField(TEXT("nodeTypes"), NodeTypesArray);
	Result->SetNumberField(TEXT("count"), NodeTypesArray.Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::SearchCallableFunctions(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Query;
	if (!Params->TryGetStringField(TEXT("query"), Query))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'query' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	int32 MaxResults = 50;
	Params->TryGetNumberField(TEXT("maxResults"), MaxResults);

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
	FString TargetClassName;
	if (Params->TryGetStringField(TEXT("targetClass"), TargetClassName))
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

	Result->SetArrayField(TEXT("results"), ResultsArray);
	Result->SetNumberField(TEXT("count"), ResultsArray.Num());
	Result->SetStringField(TEXT("query"), Query);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ConnectPins(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath) && !Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString GraphName = TEXT("EventGraph");
	Params->TryGetStringField(TEXT("graphName"), GraphName);

	FString SourceNodeId;
	if (!Params->TryGetStringField(TEXT("sourceNodeId"), SourceNodeId) && !Params->TryGetStringField(TEXT("sourceNode"), SourceNodeId))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'sourceNode' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString SourcePinName;
	if (!Params->TryGetStringField(TEXT("sourcePinName"), SourcePinName) && !Params->TryGetStringField(TEXT("sourcePin"), SourcePinName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'sourcePin' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString TargetNodeId;
	if (!Params->TryGetStringField(TEXT("targetNodeId"), TargetNodeId) && !Params->TryGetStringField(TEXT("targetNode"), TargetNodeId))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'targetNode' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString TargetPinName;
	if (!Params->TryGetStringField(TEXT("targetPinName"), TargetPinName) && !Params->TryGetStringField(TEXT("targetPin"), TargetPinName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'targetPin' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);
	if (!TargetGraph)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Graph not found: %s"), *GraphName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find source node
	UEdGraphNode* SourceNode = FindNodeByGuidOrName(TargetGraph, SourceNodeId);
	if (!SourceNode)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Source node not found: %s"), *SourceNodeId));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find target node
	UEdGraphNode* TargetNode = FindNodeByGuidOrName(TargetGraph, TargetNodeId);
	if (!TargetNode)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Target node not found: %s"), *TargetNodeId));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Source pin not found: '%s' on node '%s'"), *SourcePinName, *SourceNodeId));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Target pin not found: '%s' on node '%s'"), *TargetPinName, *TargetNodeId));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Use the graph's own schema (K2 for EventGraphs, AnimationGraph schema for AnimGraphs, etc.)
	const UEdGraphSchema* Schema = TargetGraph->GetSchema();
	if (!Schema)
	{
		Result->SetStringField(TEXT("error"), TEXT("Graph has no schema"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("graphName"), GraphName);
		Result->SetStringField(TEXT("sourceNodeId"), SourceNodeId);
		Result->SetStringField(TEXT("sourcePinName"), SourcePinName);
		Result->SetStringField(TEXT("targetNodeId"), TargetNodeId);
		Result->SetStringField(TEXT("targetPinName"), TargetPinName);
		Result->SetBoolField(TEXT("success"), true);
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
		Result->SetStringField(TEXT("error"), ErrorMsg);
		Result->SetBoolField(TEXT("success"), false);
	}

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::DeleteNode(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath) && !Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString GraphName = TEXT("EventGraph");
	Params->TryGetStringField(TEXT("graphName"), GraphName);

	FString NodeId;
	if (!Params->TryGetStringField(TEXT("nodeId"), NodeId) && !Params->TryGetStringField(TEXT("nodeName"), NodeId))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'nodeId' or 'nodeName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);
	if (!TargetGraph)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Graph not found: %s"), *GraphName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find the node
	UEdGraphNode* NodeToDelete = FindNodeByGuidOrName(TargetGraph, NodeId);
	if (!NodeToDelete)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Node not found: %s"), *NodeId));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetStringField(TEXT("nodeId"), NodeId);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::SetNodeProperty(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath) && !Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString GraphName = TEXT("EventGraph");
	Params->TryGetStringField(TEXT("graphName"), GraphName);

	FString NodeId;
	if (!Params->TryGetStringField(TEXT("nodeId"), NodeId) && !Params->TryGetStringField(TEXT("nodeName"), NodeId))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'nodeId' or 'nodeName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PinName;
	if (!Params->TryGetStringField(TEXT("pinName"), PinName) && !Params->TryGetStringField(TEXT("propertyName"), PinName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'pinName' or 'propertyName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString DefaultValue;
	if (!Params->TryGetStringField(TEXT("defaultValue"), DefaultValue) && !Params->TryGetStringField(TEXT("value"), DefaultValue))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'defaultValue' or 'value' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);
	if (!TargetGraph)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Graph not found: %s"), *GraphName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find the node
	UEdGraphNode* TargetNode = FindNodeByGuidOrName(TargetGraph, NodeId);
	if (!TargetNode)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Node not found: %s"), *NodeId));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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
		// No pin found — try setting as a node property via reflection.
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
				// Intermediate path segment — drill into struct
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
		// Neither pin nor property found — build a helpful error
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

		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("'%s' not found as pin or property. Pins: [%s]. Properties: [%s]"),
			*PinName, *FString::Join(PinNames, TEXT(", ")), *FString::Join(PropNames, TEXT(", "))));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetStringField(TEXT("nodeId"), NodeId);
	Result->SetStringField(TEXT("propertyName"), PinName);
	Result->SetStringField(TEXT("value"), DefaultValue);
	Result->SetStringField(TEXT("setVia"), bSetViaPin ? TEXT("pin") : TEXT("property"));
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

// ---------------------------------------------------------------------------
// set_component_property — Set a property on an SCS component template
// Params: assetPath, componentName, propertyName, value
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::SetComponentProperty(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath) && !Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString ComponentName;
	if (!Params->TryGetStringField(TEXT("componentName"), ComponentName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'componentName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PropertyName;
	if (!Params->TryGetStringField(TEXT("propertyName"), PropertyName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'propertyName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString Value;
	if (!Params->TryGetStringField(TEXT("value"), Value))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'value' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	if (!SCS)
	{
		Result->SetStringField(TEXT("error"), TEXT("Blueprint has no SimpleConstructionScript (not an Actor blueprint?)"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

	if (!TargetNode || !TargetNode->ComponentTemplate)
	{
		// List available component names for error message
		TArray<FString> Names;
		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			if (Node && Node->ComponentTemplate)
			{
				Names.Add(Node->GetVariableName().ToString());
			}
		}
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Component '%s' not found. Available: [%s]"),
			*ComponentName, *FString::Join(Names, TEXT(", "))));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UActorComponent* Template = TargetNode->ComponentTemplate;

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
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Property '%s' not found on %s. Properties: [%s]"),
			*PropertyName, *Template->GetClass()->GetName(),
			*FString::Join(PropNames, TEXT(", "))));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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
			Result->SetStringField(TEXT("error"), FString::Printf(
				TEXT("Could not load object at '%s' for property '%s'"), *Value, *PropertyName));
			Result->SetBoolField(TEXT("success"), false);
			return MakeShared<FJsonValueObject>(Result);
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

	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("componentName"), ComponentName);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("value"), Value);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

// ---------------------------------------------------------------------------
// set_class_default — Set a UPROPERTY on a Blueprint's Class Default Object
// Params: assetPath, propertyName, value
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::SetClassDefault(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath) && !Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PropertyName;
	if (!Params->TryGetStringField(TEXT("propertyName"), PropertyName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'propertyName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString Value;
	if (!Params->TryGetStringField(TEXT("value"), Value))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'value' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UClass* GenClass = Blueprint->GeneratedClass;
	if (!GenClass)
	{
		Result->SetStringField(TEXT("error"), TEXT("Blueprint has no GeneratedClass (needs compilation first?)"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* CDO = GenClass->GetDefaultObject();
	if (!CDO)
	{
		Result->SetStringField(TEXT("error"), TEXT("Could not get Class Default Object"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Property '%s' not found on %s. Properties: [%s]"),
			*PropertyName, *GenClass->GetName(),
			*FString::Join(PropNames, TEXT(", "))));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	void* ValuePtr = FinalProp->ContainerPtrToValuePtr<void>(CurrentContainer);

	if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(FinalProp))
	{
		UObject* LoadedObj = LoadObject<UObject>(nullptr, *Value);
		if (LoadedObj)
		{
			ObjProp->SetObjectPropertyValue(ValuePtr, LoadedObj);
		}
		else
		{
			Result->SetStringField(TEXT("error"), FString::Printf(
				TEXT("Could not load object at '%s' for property '%s'"), *Value, *PropertyName));
			Result->SetBoolField(TEXT("success"), false);
			return MakeShared<FJsonValueObject>(Result);
		}
	}
	else if (FSoftObjectProperty* SoftProp = CastField<FSoftObjectProperty>(FinalProp))
	{
		FSoftObjectPath SoftPath(Value);
		SoftProp->SetPropertyValue(ValuePtr, FSoftObjectPtr(SoftPath));
	}
	else
	{
		// Generic: ImportText handles FName, int, float, bool, FVector, TArray, TMap, etc.
		FinalProp->ImportText_Direct(*Value, ValuePtr, CDO, PPF_None);
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

	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("value"), Value);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}
