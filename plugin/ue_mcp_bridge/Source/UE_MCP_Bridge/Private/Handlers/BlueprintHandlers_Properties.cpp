// Split from BlueprintHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FBlueprintHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in BlueprintHandlers.cpp::RegisterHandlers.

#include "BlueprintHandlers.h"
#include "BlueprintHandlers_Internal.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "BlueprintEditorLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/InheritableComponentHandler.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_EditablePinBase.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "UObject/TopLevelAssetPath.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "EditorAssetLibrary.h"
#include "Editor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"


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

	// Capture previous values for rollback
	const bool bPrevInstanceEditable = (FoundVar->PropertyFlags & CPF_Edit) != 0;
	FString PrevCategory;
	if (FoundVar->HasMetaData(FBlueprintMetadata::MD_FunctionCategory))
	{
		PrevCategory = FoundVar->GetMetaData(FBlueprintMetadata::MD_FunctionCategory);
	}
	FString PrevTooltip;
	if (FoundVar->HasMetaData(FBlueprintMetadata::MD_Tooltip))
	{
		PrevTooltip = FoundVar->GetMetaData(FBlueprintMetadata::MD_Tooltip);
	}

	// Set expose on spawn
	bool bExposeOnSpawn = false;
	const bool bHasExposeOnSpawn = Params->TryGetBoolField(TEXT("exposeOnSpawn"), bExposeOnSpawn);
	const bool bPrevExposeOnSpawn = FoundVar->HasMetaData(FBlueprintMetadata::MD_ExposeOnSpawn);
	if (bHasExposeOnSpawn)
	{
		if (bExposeOnSpawn)
		{
			FoundVar->SetMetaData(FBlueprintMetadata::MD_ExposeOnSpawn, TEXT("true"));
			FoundVar->PropertyFlags |= CPF_ExposeOnSpawn;
		}
		else
		{
			FoundVar->RemoveMetaData(FBlueprintMetadata::MD_ExposeOnSpawn);
			FoundVar->PropertyFlags &= ~CPF_ExposeOnSpawn;
		}
	}

	// Set instance editable
	bool bInstanceEditable = false;
	const bool bHasInstanceEditable = Params->TryGetBoolField(TEXT("instanceEditable"), bInstanceEditable);
	if (bHasInstanceEditable)
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
	const bool bHasCategory = Params->TryGetStringField(TEXT("category"), CategoryStr);
	if (bHasCategory)
	{
		FoundVar->SetMetaData(FBlueprintMetadata::MD_FunctionCategory, *CategoryStr);
	}

	// Set tooltip
	FString TooltipStr;
	const bool bHasTooltip = Params->TryGetStringField(TEXT("tooltip"), TooltipStr);
	if (bHasTooltip)
	{
		FoundVar->SetMetaData(FBlueprintMetadata::MD_Tooltip, *TooltipStr);
	}

	// Detect no-op: nothing requested OR every requested field already matches
	const bool bAnyChanged =
		(bHasExposeOnSpawn && bExposeOnSpawn != bPrevExposeOnSpawn) ||
		(bHasInstanceEditable && bInstanceEditable != bPrevInstanceEditable) ||
		(bHasCategory && CategoryStr != PrevCategory) ||
		(bHasTooltip && TooltipStr != PrevTooltip);
	if (!bAnyChanged)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("name"), VarName);
		return MCPResult(Noop);
	}

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("variableName"), VarName);

	// Rollback: call set_variable_properties with previous values
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), AssetPath);
	Payload->SetStringField(TEXT("name"), VarName);
	if (bHasExposeOnSpawn) Payload->SetBoolField(TEXT("exposeOnSpawn"), bPrevExposeOnSpawn);
	if (bHasInstanceEditable) Payload->SetBoolField(TEXT("instanceEditable"), bPrevInstanceEditable);
	if (bHasCategory) Payload->SetStringField(TEXT("category"), PrevCategory);
	if (bHasTooltip) Payload->SetStringField(TEXT("tooltip"), PrevTooltip);
	MCPSetRollback(Result, TEXT("set_variable_properties"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::SetComponentProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString ComponentName;
	if (auto Err = RequireString(Params, TEXT("componentName"), ComponentName)) return Err;

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	// #152: accept any JSON value type — scalars, numbers, booleans, or structured
	// objects like {x,y,z} for FVector. Previous impl only accepted strings, so
	// RelativeLocation etc. couldn't be set without pre-formatting "(X=1,Y=2,Z=3)".
	TSharedPtr<FJsonValue> ValueField = Params->TryGetField(TEXT("value"));
	if (!ValueField.IsValid())
	{
		return MCPError(TEXT("Missing 'value' parameter"));
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	bool bIsInherited = false;
	TArray<FString> Available;
	UActorComponent* Template = ResolveComponentTemplate(
		Blueprint, ComponentName, /*bForWrite=*/true, bIsInherited, Available);

	if (!Template)
	{
		return MCPError(FString::Printf(
			TEXT("Component '%s' not found. Available: [%s]"),
			*ComponentName, *FString::Join(Available, TEXT(", "))));
	}

	// Walk dotted path to the final property. The helper from HandlerJsonProperty.h
	// does the assignment (handles FVector objects, object refs, arrays, etc.);
	// here we duplicate the walk once so we can also capture PrevValue for rollback.
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

	void* ValuePtr = FinalProp->ContainerPtrToValuePtr<void>(CurrentContainer);

	// Capture previous value for rollback via ExportText (always a string).
	FString PrevValue;
	FinalProp->ExportText_Direct(PrevValue, ValuePtr, ValuePtr, nullptr, PPF_None);

	Template->Modify();
	FString SetErr;
	if (!MCPJsonProperty::SetJsonOnProperty(FinalProp, ValuePtr, ValueField, SetErr))
	{
		return MCPError(FString::Printf(TEXT("Failed to set '%s': %s"), *PropertyName, *SetErr));
	}

	// Re-export for the rollback payload and no-op detection.
	FString NewValue;
	FinalProp->ExportText_Direct(NewValue, ValuePtr, ValuePtr, nullptr, PPF_None);
	if (NewValue == PrevValue)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("componentName"), ComponentName);
		Noop->SetStringField(TEXT("propertyName"), PropertyName);
		Noop->SetStringField(TEXT("value"), NewValue);
		return MCPResult(Noop);
	}

	Template->PostEditChange();

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("componentName"), ComponentName);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("value"), NewValue);
	Result->SetBoolField(TEXT("inherited"), bIsInherited);

	// Rollback: self-inverse with previous value
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), AssetPath);
	Payload->SetStringField(TEXT("componentName"), ComponentName);
	Payload->SetStringField(TEXT("propertyName"), PropertyName);
	Payload->SetStringField(TEXT("value"), PrevValue);
	MCPSetRollback(Result, TEXT("set_component_property"), Payload);

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_class_default -- Set a UPROPERTY on a Blueprint's Class Default Object
// Params: assetPath, propertyName, value
// ---------------------------------------------------------------------------


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

	// Accept value as any JSON type (string, number, bool, object, array).
	// This enables setting TArray<FStruct> with nested UObject refs via JSON
	// instead of requiring arcane ImportText format strings (#196, #199).
	TSharedPtr<FJsonValue> ValueField = Params->TryGetField(TEXT("value"));
	if (!ValueField.IsValid())
	{
		return MCPError(TEXT("Missing 'value' parameter"));
	}

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

	// Capture previous value for rollback and idempotency
	FString PrevValue;
	FinalProp->ExportText_Direct(PrevValue, ValuePtr, ValuePtr, nullptr, PPF_None);

	CDO->Modify();
	FString SetErr;
	if (!MCPJsonProperty::SetJsonOnProperty(FinalProp, ValuePtr, ValueField, SetErr))
	{
		return MCPError(FString::Printf(TEXT("Failed to set '%s': %s"), *PropertyName, *SetErr));
	}

	// Re-export for rollback payload and no-op detection
	FString NewValue;
	FinalProp->ExportText_Direct(NewValue, ValuePtr, ValuePtr, nullptr, PPF_None);
	if (NewValue == PrevValue)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("propertyName"), PropertyName);
		Noop->SetStringField(TEXT("value"), NewValue);
		return MCPResult(Noop);
	}

	CDO->PostEditChange();

	// Save
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("value"), NewValue);

	// Rollback: self-inverse with previous value
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), AssetPath);
	Payload->SetStringField(TEXT("propertyName"), PropertyName);
	Payload->SetStringField(TEXT("value"), PrevValue);
	MCPSetRollback(Result, TEXT("set_class_default"), Payload);

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// remove_component -- Remove an SCS component from a Blueprint
// Params: assetPath, componentName
// ---------------------------------------------------------------------------


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

	// Idempotency: check if parameter already exists
	const FName ParamFName(*ParamName);
	if (!bIsOutput)
	{
		for (const TSharedPtr<FUserPinInfo>& Info : EntryNode->UserDefinedPins)
		{
			if (Info.IsValid() && Info->PinName == ParamFName)
			{
				auto Existed = MCPSuccess();
				MCPSetExisted(Existed);
				Existed->SetStringField(TEXT("path"), AssetPath);
				Existed->SetStringField(TEXT("functionName"), FunctionName);
				Existed->SetStringField(TEXT("parameterName"), ParamName);
				Existed->SetBoolField(TEXT("isOutput"), false);
				return MCPResult(Existed);
			}
		}
	}
	else
	{
		for (UEdGraphNode* Node : FuncGraph->Nodes)
		{
			if (Node && Node->GetClass()->GetName() == TEXT("K2Node_FunctionResult"))
			{
				if (UK2Node_EditablePinBase* R = Cast<UK2Node_EditablePinBase>(Node))
				{
					for (const TSharedPtr<FUserPinInfo>& Info : R->UserDefinedPins)
					{
						if (Info.IsValid() && Info->PinName == ParamFName)
						{
							auto Existed = MCPSuccess();
							MCPSetExisted(Existed);
							Existed->SetStringField(TEXT("path"), AssetPath);
							Existed->SetStringField(TEXT("functionName"), FunctionName);
							Existed->SetStringField(TEXT("parameterName"), ParamName);
							Existed->SetBoolField(TEXT("isOutput"), true);
							return MCPResult(Existed);
						}
					}
				}
			}
		}
	}

	FEdGraphPinType PinType = MakePinType(ParamType);

	if (PinType.PinCategory == NAME_None)
	{
		return MCPError(FString::Printf(TEXT("Unrecognized parameter type: '%s'. Use a known type (Bool, Int, Float, String, Name, Text, Byte, Object, Vector, Rotator, Transform, GameplayTag, etc.) or a full class/struct path."), *ParamType));
	}

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
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("functionName"), FunctionName);
	Result->SetStringField(TEXT("parameterName"), ParamName);
	Result->SetStringField(TEXT("parameterType"), ParamType);
	Result->SetBoolField(TEXT("isOutput"), bIsOutput);
	// No rollback: no paired remove_function_parameter handler yet.
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_variable_default -- Set the default value of a Blueprint variable
// Bypasses CDO restrictions by setting via FBlueprintEditorUtils on the BP variable description
// Params: assetPath, name, value
// ---------------------------------------------------------------------------


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

	// Capture previous value for rollback and idempotency
	const FString PrevValue = FoundVar->DefaultValue;
	if (PrevValue == Value)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("variableName"), VarName);
		Noop->SetStringField(TEXT("value"), Value);
		return MCPResult(Noop);
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
					// For arrays (including TArray<TSubclassOf<>>), use ImportText.
					// A parse failure here just means the CDO mirror did not take;
					// the authoritative string default set on FoundVar->DefaultValue
					// above still applies on the next compile, so we do not reject
					// the whole request, but the caller gets a warning.
					if (!Prop->ImportText_Direct(*Value, ValuePtr, CDO, PPF_None))
					{
						UE_LOG(LogTemp, Warning, TEXT("set_blueprint_variable_default_value: ImportText_Direct failed for array property '%s' value '%s' - default string was still written and will take effect on recompile."), *VarName, *Value);
					}
				}
				else
				{
					if (!Prop->ImportText_Direct(*Value, ValuePtr, CDO, PPF_None))
					{
						UE_LOG(LogTemp, Warning, TEXT("set_blueprint_variable_default_value: ImportText_Direct failed for property '%s' value '%s' - default string was still written and will take effect on recompile."), *VarName, *Value);
					}
				}

				CDO->PostEditChange();
			}
		}
	}

	// Compile and save
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("variableName"), VarName);
	Result->SetStringField(TEXT("value"), Value);

	// Rollback: self-inverse with previous value
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), AssetPath);
	Payload->SetStringField(TEXT("name"), VarName);
	Payload->SetStringField(TEXT("value"), PrevValue);
	MCPSetRollback(Result, TEXT("set_variable_default"), Payload);

	return MCPResult(Result);
}

// ===========================================================================
// v0.7.11 — Blueprint authoring depth
// ===========================================================================


// ─── #105 read_component_properties ─────────────────────────────────
TSharedPtr<FJsonValue> FBlueprintHandlers::ReadComponentProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString ComponentName;
	if (auto Err = RequireString(Params, TEXT("componentName"), ComponentName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

	bool bIsInherited = false;
	TArray<FString> Available;
	UActorComponent* Template = ResolveComponentTemplate(
		Blueprint, ComponentName, /*bForWrite=*/false, bIsInherited, Available);
	if (!Template)
	{
		return MCPError(FString::Printf(
			TEXT("Component '%s' not found. Available: [%s]"),
			*ComponentName, *FString::Join(Available, TEXT(", "))));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("componentName"), Template->GetName());
	Result->SetStringField(TEXT("componentClass"), Template->GetClass()->GetName());
	Result->SetBoolField(TEXT("inherited"), bIsInherited);

	TArray<TSharedPtr<FJsonValue>> PropsArr;
	for (TFieldIterator<FProperty> It(Template->GetClass()); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop) continue;

		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("name"), Prop->GetName());
		P->SetStringField(TEXT("type"), Prop->GetCPPType());

		FString ValueStr;
		const void* ValPtr = Prop->ContainerPtrToValuePtr<void>(Template);
		Prop->ExportText_Direct(ValueStr, ValPtr, ValPtr, Template, PPF_None);
		P->SetStringField(TEXT("value"), ValueStr);

		if (FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
		{
			FScriptArrayHelper Helper(ArrProp, ValPtr);
			P->SetNumberField(TEXT("count"), Helper.Num());
			TArray<TSharedPtr<FJsonValue>> Elems;
			for (int32 i = 0; i < Helper.Num(); ++i)
			{
				FString ElemStr;
				ArrProp->Inner->ExportText_Direct(ElemStr, Helper.GetRawPtr(i), Helper.GetRawPtr(i), Template, PPF_None);
				Elems.Add(MakeShared<FJsonValueString>(ElemStr));
			}
			P->SetArrayField(TEXT("elements"), Elems);
		}
		PropsArr.Add(MakeShared<FJsonValueObject>(P));
	}
	Result->SetArrayField(TEXT("properties"), PropsArr);
	Result->SetNumberField(TEXT("propertyCount"), PropsArr.Num());
	return MCPResult(Result);
}


// ─── #116 set_actor_tick_settings ───────────────────────────────────
TSharedPtr<FJsonValue> FBlueprintHandlers::SetActorTickSettings(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint || !Blueprint->GeneratedClass) return MCPError(TEXT("Blueprint not found or not compiled"));
	AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject(true));
	if (!CDO) return MCPError(TEXT("Blueprint is not an Actor"));

	bool bCanEverTick = CDO->PrimaryActorTick.bCanEverTick;
	bool bStartWithTickEnabled = CDO->PrimaryActorTick.bStartWithTickEnabled;
	double TickInterval = CDO->PrimaryActorTick.TickInterval;

	Params->TryGetBoolField(TEXT("bCanEverTick"), bCanEverTick);
	Params->TryGetBoolField(TEXT("bStartWithTickEnabled"), bStartWithTickEnabled);
	Params->TryGetNumberField(TEXT("TickInterval"), TickInterval);

	CDO->PrimaryActorTick.bCanEverTick = bCanEverTick;
	CDO->PrimaryActorTick.bStartWithTickEnabled = bStartWithTickEnabled;
	CDO->PrimaryActorTick.TickInterval = (float)TickInterval;

	Blueprint->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("bCanEverTick"), bCanEverTick);
	Result->SetBoolField(TEXT("bStartWithTickEnabled"), bStartWithTickEnabled);
	Result->SetNumberField(TEXT("TickInterval"), TickInterval);
	return MCPResult(Result);
}

// ─── #128 get_component_property — inherited-aware single-prop read ──
// Params: assetPath, componentName, propertyName
// Returns the effective default for the given child BP: the ICH override
// if one exists, otherwise the parent template value. Supports dotted
// property paths ("RelativeLocation.X").


// ─── #128 get_component_property — inherited-aware single-prop read ──
// Params: assetPath, componentName, propertyName
// Returns the effective default for the given child BP: the ICH override
// if one exists, otherwise the parent template value. Supports dotted
// property paths ("RelativeLocation.X").
TSharedPtr<FJsonValue> FBlueprintHandlers::GetComponentProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	FString ComponentName;
	if (auto Err = RequireString(Params, TEXT("componentName"), ComponentName)) return Err;
	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

	bool bIsInherited = false;
	TArray<FString> Available;
	UActorComponent* Template = ResolveComponentTemplate(
		Blueprint, ComponentName, /*bForWrite=*/false, bIsInherited, Available);

	if (!Template)
	{
		return MCPError(FString::Printf(
			TEXT("Component '%s' not found. Available: [%s]"),
			*ComponentName, *FString::Join(Available, TEXT(", "))));
	}

	TArray<FString> PathParts;
	PropertyName.ParseIntoArray(PathParts, TEXT("."));

	UStruct* CurrentStruct = Template->GetClass();
	void* CurrentContainer = Template;
	FProperty* FinalProp = nullptr;

	for (int32 i = 0; i < PathParts.Num(); ++i)
	{
		FProperty* Prop = CurrentStruct->FindPropertyByName(FName(*PathParts[i]));
		if (!Prop)
		{
			return MCPError(FString::Printf(
				TEXT("Property '%s' not found on %s"), *PathParts[i], *CurrentStruct->GetName()));
		}
		if (i < PathParts.Num() - 1)
		{
			FStructProperty* StructProp = CastField<FStructProperty>(Prop);
			if (!StructProp)
			{
				return MCPError(FString::Printf(TEXT("Not a struct: %s"), *PathParts[i]));
			}
			CurrentContainer = StructProp->ContainerPtrToValuePtr<void>(CurrentContainer);
			CurrentStruct = StructProp->Struct;
		}
		else
		{
			FinalProp = Prop;
		}
	}
	if (!FinalProp) return MCPError(TEXT("Property path unresolved"));

	FString ValueStr;
	const void* ValPtr = FinalProp->ContainerPtrToValuePtr<void>(CurrentContainer);
	FinalProp->ExportText_Direct(ValueStr, ValPtr, ValPtr, Template, PPF_None);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("componentName"), ComponentName);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("type"), FinalProp->GetCPPType());
	Result->SetStringField(TEXT("value"), ValueStr);
	Result->SetBoolField(TEXT("inherited"), bIsInherited);
	Result->SetStringField(TEXT("templateClass"), Template->GetClass()->GetName());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::SetCdoProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString ClassName;
	if (auto Err = RequireString(Params, TEXT("className"), ClassName)) return Err;

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	// Accept value as any JSON type (string, number, bool, object, array).
	// Enables setting TArray<FStruct> with nested UObject refs via JSON (#196, #199).
	TSharedPtr<FJsonValue> ValueField = Params->TryGetField(TEXT("value"));
	if (!ValueField.IsValid())
	{
		return MCPError(TEXT("Missing 'value' parameter"));
	}

	// Resolve UClass: try full path first (e.g. "/Script/Engine.Actor"), then short name
	UClass* Class = LoadObject<UClass>(nullptr, *ClassName);
	if (!Class)
	{
		Class = FindClassByShortName(ClassName);
	}
	if (!Class)
	{
		return MCPError(FString::Printf(TEXT("Class not found: %s"), *ClassName));
	}

	UObject* CDO = Class->GetDefaultObject();
	if (!CDO)
	{
		return MCPError(FString::Printf(TEXT("Could not get CDO for class: %s"), *Class->GetName()));
	}

	// Navigate dotted property paths (e.g. "SomeStruct.Field")
	TArray<FString> PathParts;
	PropertyName.ParseIntoArray(PathParts, TEXT("."));

	UStruct* CurrentStruct = Class;
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
		for (TFieldIterator<FProperty> It(Class); It; ++It)
		{
			PropNames.Add(It->GetName());
		}
		return MCPError(FString::Printf(
			TEXT("Property '%s' not found on %s. Properties: [%s]"),
			*PropertyName, *Class->GetName(),
			*FString::Join(PropNames, TEXT(", "))));
	}

	void* ValuePtr = FinalProp->ContainerPtrToValuePtr<void>(CurrentContainer);

	// Capture previous value for rollback / idempotency
	FString PrevValue;
	FinalProp->ExportText_Direct(PrevValue, ValuePtr, ValuePtr, nullptr, PPF_None);

	CDO->Modify();
	FString SetErr;
	if (!MCPJsonProperty::SetJsonOnProperty(FinalProp, ValuePtr, ValueField, SetErr))
	{
		return MCPError(FString::Printf(TEXT("Failed to set '%s': %s"), *PropertyName, *SetErr));
	}

	// Re-export for rollback payload and no-op detection
	FString NewValue;
	FinalProp->ExportText_Direct(NewValue, ValuePtr, ValuePtr, nullptr, PPF_None);
	if (NewValue == PrevValue)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("className"), ClassName);
		Noop->SetStringField(TEXT("propertyName"), PropertyName);
		Noop->SetStringField(TEXT("value"), NewValue);
		return MCPResult(Noop);
	}

	CDO->PostEditChange();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("className"), ClassName);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("value"), NewValue);

	// Rollback: restore the previous value
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("className"), ClassName);
	Payload->SetStringField(TEXT("propertyName"), PropertyName);
	Payload->SetStringField(TEXT("value"), PrevValue);
	MCPSetRollback(Result, TEXT("set_cdo_property"), Payload);

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// get_cdo_properties -- Read properties from any C++ class CDO
// Params: className (required), propertyNames (optional array of strings)
// Issue #183
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// get_cdo_properties -- Read properties from any C++ class CDO
// Params: className (required), propertyNames (optional array of strings)
// Issue #183
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::GetCdoProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString ClassName;
	if (auto Err = RequireString(Params, TEXT("className"), ClassName)) return Err;

	// Resolve UClass
	UClass* Class = LoadObject<UClass>(nullptr, *ClassName);
	if (!Class)
	{
		Class = FindClassByShortName(ClassName);
	}
	if (!Class)
	{
		return MCPError(FString::Printf(TEXT("Class not found: %s"), *ClassName));
	}

	UObject* CDO = Class->GetDefaultObject();
	if (!CDO)
	{
		return MCPError(FString::Printf(TEXT("Could not get CDO for class: %s"), *Class->GetName()));
	}

	// Optional filter: specific property names
	TSet<FString> Filter;
	const TArray<TSharedPtr<FJsonValue>>* PropNamesArr = nullptr;
	if (Params->TryGetArrayField(TEXT("propertyNames"), PropNamesArr) && PropNamesArr)
	{
		for (const auto& V : *PropNamesArr)
		{
			FString S;
			if (V->TryGetString(S))
			{
				Filter.Add(S);
			}
		}
	}

	TSharedPtr<FJsonObject> PropsObj = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> It(Class); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop) continue;

		const FString PropName = Prop->GetName();
		if (Filter.Num() > 0 && !Filter.Contains(PropName))
		{
			continue;
		}

		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(CDO);
		FString ExportedValue;
		Prop->ExportText_Direct(ExportedValue, ValuePtr, ValuePtr, nullptr, PPF_None);
		PropsObj->SetStringField(PropName, ExportedValue);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("className"), Class->GetPathName());
	Result->SetStringField(TEXT("classShortName"), Class->GetName());
	Result->SetObjectField(TEXT("properties"), PropsObj);
	Result->SetNumberField(TEXT("count"), PropsObj->Values.Num());

	return MCPResult(Result);
}

// #442: dedicated OverrideMaterials writer for mesh-component templates
// (StaticMeshComponent, SkeletalMeshComponent, anything deriving from
// UMeshComponent). Takes materialPaths directly so the caller never has to
// route a TArray through the generic set_component_property path.
//
// Params: assetPath, componentName, materialPaths (string[]). Pass an empty
// array to clear the override list.
TSharedPtr<FJsonValue> FBlueprintHandlers::SetComponentOverrideMaterials(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	FString ComponentName;
	if (auto Err = RequireString(Params, TEXT("componentName"), ComponentName)) return Err;

	const TArray<TSharedPtr<FJsonValue>>* PathsArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("materialPaths"), PathsArr) || !PathsArr)
	{
		return MCPError(TEXT("Missing 'materialPaths' (string array)"));
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

	bool bIsInherited = false;
	TArray<FString> Available;
	UActorComponent* Template = ResolveComponentTemplate(
		Blueprint, ComponentName, /*bForWrite*/ true, bIsInherited, Available);
	if (!Template)
	{
		return MCPError(FString::Printf(TEXT("Component '%s' not found. Available: [%s]"),
			*ComponentName, *FString::Join(Available, TEXT(", "))));
	}

	UMeshComponent* MeshComp = Cast<UMeshComponent>(Template);
	if (!MeshComp)
	{
		return MCPError(FString::Printf(TEXT("Component '%s' is %s, not a MeshComponent"),
			*ComponentName, *Template->GetClass()->GetName()));
	}

	TArray<UMaterialInterface*> Loaded;
	TArray<FString> ResolvedPaths;
	for (const TSharedPtr<FJsonValue>& V : *PathsArr)
	{
		FString Path;
		if (!V->TryGetString(Path) || Path.IsEmpty())
		{
			Loaded.Add(nullptr);
			ResolvedPaths.Add(TEXT("None"));
			continue;
		}
		UMaterialInterface* M = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *Path));
		if (!M) return MCPError(FString::Printf(TEXT("Material not found: %s"), *Path));
		Loaded.Add(M);
		ResolvedPaths.Add(M->GetPathName());
	}

	// Capture the prior OverrideMaterials list for rollback.
	TArray<TSharedPtr<FJsonValue>> PrevPaths;
	for (UMaterialInterface* Prev : MeshComp->OverrideMaterials)
	{
		PrevPaths.Add(MakeShared<FJsonValueString>(Prev ? Prev->GetPathName() : FString(TEXT("None"))));
	}

	Template->Modify();
	MeshComp->OverrideMaterials = Loaded;
	Template->PostEditChange();

	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("componentName"), ComponentName);
	Result->SetNumberField(TEXT("materialCount"), Loaded.Num());
	TArray<TSharedPtr<FJsonValue>> ResolvedArr;
	for (const FString& P : ResolvedPaths) ResolvedArr.Add(MakeShared<FJsonValueString>(P));
	Result->SetArrayField(TEXT("materialPaths"), ResolvedArr);
	Result->SetBoolField(TEXT("inherited"), bIsInherited);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), AssetPath);
	Payload->SetStringField(TEXT("componentName"), ComponentName);
	Payload->SetArrayField(TEXT("materialPaths"), PrevPaths);
	MCPSetRollback(Result, TEXT("set_component_override_materials"), Payload);

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_capsule_size -- Call UCapsuleComponent::SetCapsuleSize on a BP capsule
// component template. Property writes alone leave the visualizer stale; the
// UFUNCTION setter propagates scaled/unscaled state correctly (#419).
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::SetCapsuleSize(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	FString ComponentName;
	if (auto Err = RequireString(Params, TEXT("componentName"), ComponentName)) return Err;

	const bool bHasHalfHeight = Params->HasField(TEXT("halfHeight"));
	const bool bHasRadius = Params->HasField(TEXT("radius"));
	if (!bHasHalfHeight && !bHasRadius)
	{
		return MCPError(TEXT("Pass at least one of 'halfHeight' or 'radius'"));
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

	bool bIsInherited = false;
	TArray<FString> Available;
	UActorComponent* Template = ResolveComponentTemplate(
		Blueprint, ComponentName, /*bForWrite*/ true, bIsInherited, Available);
	if (!Template)
	{
		return MCPError(FString::Printf(TEXT("Component '%s' not found. Available: [%s]"),
			*ComponentName, *FString::Join(Available, TEXT(", "))));
	}

	UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Template);
	if (!Capsule)
	{
		return MCPError(FString::Printf(TEXT("Component '%s' is %s, not a CapsuleComponent"),
			*ComponentName, *Template->GetClass()->GetName()));
	}

	const float PrevHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
	const float PrevRadius = Capsule->GetUnscaledCapsuleRadius();

	float NewHalfHeight = PrevHalfHeight;
	float NewRadius = PrevRadius;
	if (bHasHalfHeight)
	{
		double H = PrevHalfHeight;
		Params->TryGetNumberField(TEXT("halfHeight"), H);
		NewHalfHeight = (float)H;
	}
	if (bHasRadius)
	{
		double R = PrevRadius;
		Params->TryGetNumberField(TEXT("radius"), R);
		NewRadius = (float)R;
	}

	Capsule->Modify();
	Capsule->SetCapsuleSize(NewRadius, NewHalfHeight, /*bUpdateOverlaps*/ true);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("componentName"), ComponentName);
	Result->SetNumberField(TEXT("halfHeight"), NewHalfHeight);
	Result->SetNumberField(TEXT("radius"), NewRadius);
	Result->SetNumberField(TEXT("previousHalfHeight"), PrevHalfHeight);
	Result->SetNumberField(TEXT("previousRadius"), PrevRadius);
	Result->SetBoolField(TEXT("inherited"), bIsInherited);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), AssetPath);
	Payload->SetStringField(TEXT("componentName"), ComponentName);
	Payload->SetNumberField(TEXT("halfHeight"), PrevHalfHeight);
	Payload->SetNumberField(TEXT("radius"), PrevRadius);
	MCPSetRollback(Result, TEXT("set_capsule_size"), Payload);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// run_construction_script -- Spawn a temp actor from a Blueprint, run its
// construction script, collect resulting component info, then destroy it.
// Params: assetPath (required), location (optional {x,y,z})
// Issue #195
// ---------------------------------------------------------------------------
