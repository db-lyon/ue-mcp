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
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/MessageDialog.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"

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
}

UBlueprint* FBlueprintHandlers::LoadBlueprint(const FString& AssetPath)
{
	return LoadObject<UBlueprint>(nullptr, *AssetPath);
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

TSharedPtr<FJsonValue> FBlueprintHandlers::CreateBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' parameter"));
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
	if (!Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' parameter"));
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
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::AddVariable(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' parameter"));
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
	if (!Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' parameter"));
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
	if (!Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' parameter"));
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
	if (!Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' parameter"));
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
	if (!Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' parameter"));
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
	if (!Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' parameter"));
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

	UEdGraph* NewGraph = FBlueprintEditorUtils::AddFunction(Blueprint, FName(*FunctionName));
	if (!NewGraph)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to create function: %s"), *FunctionName));
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
	Result->SetStringField(TEXT("functionName"), FunctionName);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ListBlueprintFunctions(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' parameter"));
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
	if (!Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' parameter"));
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
	UEdGraph* TargetGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph && Graph->GetName() == GraphName)
		{
			TargetGraph = Graph;
			break;
		}
	}
	if (!TargetGraph)
	{
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph && Graph->GetName() == GraphName)
			{
				TargetGraph = Graph;
				break;
			}
		}
	}

	if (!TargetGraph)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Graph not found: %s"), *GraphName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Get optional node params
	const TSharedPtr<FJsonObject>* NodeParams = nullptr;
	Params->TryGetObjectField(TEXT("nodeParams"), NodeParams);

	UK2Node* NewNode = nullptr;

	if (NodeClass == TEXT("K2Node_CallFunction") || NodeClass == TEXT("CallFunction"))
	{
		UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(TargetGraph);
		if (CallNode)
		{
			// Set function reference from nodeParams
			if (NodeParams)
			{
				FString FunctionName;
				if ((*NodeParams)->TryGetStringField(TEXT("functionName"), FunctionName))
				{
					FString TargetClassName;
					if ((*NodeParams)->TryGetStringField(TEXT("targetClass"), TargetClassName))
					{
						UClass* TargetClass = FindObject<UClass>(nullptr, *TargetClassName);
						if (!TargetClass)
						{
							TargetClass = FindObject<UClass>(nullptr, *(TEXT("U") + TargetClassName));
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
					else
					{
						// Try finding function by name on the blueprint's parent class
						if (Blueprint->ParentClass)
						{
							UFunction* Func = Blueprint->ParentClass->FindFunctionByName(FName(*FunctionName));
							if (Func)
							{
								CallNode->SetFromFunction(Func);
							}
						}
					}
				}
			}

			TargetGraph->AddNode(CallNode, false, false);
			CallNode->CreateNewGuid();
			CallNode->PostPlacedNewNode();
			CallNode->AllocateDefaultPins();
			NewNode = CallNode;
		}
	}
	else if (NodeClass == TEXT("K2Node_Event") || NodeClass == TEXT("Event"))
	{
		UK2Node_Event* EventNode = NewObject<UK2Node_Event>(TargetGraph);
		if (EventNode)
		{
			if (NodeParams)
			{
				FString EventName;
				if ((*NodeParams)->TryGetStringField(TEXT("eventName"), EventName))
				{
					EventNode->CustomFunctionName = FName(*EventName);
				}
			}

			TargetGraph->AddNode(EventNode, false, false);
			EventNode->CreateNewGuid();
			EventNode->PostPlacedNewNode();
			EventNode->AllocateDefaultPins();
			NewNode = EventNode;
		}
	}
	else
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Unsupported node class: %s. Supported: K2Node_CallFunction, K2Node_Event"), *NodeClass));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	if (!NewNode)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create node"));
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
	Result->SetStringField(TEXT("nodeClass"), NodeClass);
	Result->SetStringField(TEXT("nodeId"), NewNode->NodeGuid.ToString());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ReadBlueprintGraph(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' parameter"));
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
	UEdGraph* TargetGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph && Graph->GetName() == GraphName)
		{
			TargetGraph = Graph;
			break;
		}
	}
	if (!TargetGraph)
	{
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph && Graph->GetName() == GraphName)
			{
				TargetGraph = Graph;
				break;
			}
		}
	}

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
	if (!Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' parameter"));
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
	if (!Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' parameter"));
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
	if (!Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' parameter"));
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
