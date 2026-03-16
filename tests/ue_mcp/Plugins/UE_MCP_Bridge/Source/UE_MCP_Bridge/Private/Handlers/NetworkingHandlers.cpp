#include "NetworkingHandlers.h"
#include "HandlerRegistry.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UnrealType.h"

void FNetworkingHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("get_networking_info"), &GetNetworkingInfo);
	Registry.RegisterHandler(TEXT("set_replicates"), &SetReplicates);
	Registry.RegisterHandler(TEXT("configure_net_update_frequency"), &ConfigureNetUpdateFrequency);
	Registry.RegisterHandler(TEXT("set_net_dormancy"), &SetNetDormancy);
	Registry.RegisterHandler(TEXT("set_always_relevant"), &SetAlwaysRelevant);
	Registry.RegisterHandler(TEXT("set_net_priority"), &SetNetPriority);
	Registry.RegisterHandler(TEXT("set_replicate_movement"), &SetReplicateMovement);
	Registry.RegisterHandler(TEXT("set_variable_replication"), &SetVariableReplication);
	Registry.RegisterHandler(TEXT("get_replication_info"), &GetReplicationInfo);
	Registry.RegisterHandler(TEXT("set_owner_only_relevant"), &SetOwnerOnlyRelevant);
}

AActor* FNetworkingHandlers::LoadBlueprintCDO(const FString& BlueprintPath, TSharedPtr<FJsonObject>& OutResult)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		OutResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found or has no generated class: %s"), *BlueprintPath));
		OutResult->SetBoolField(TEXT("success"), false);
		return nullptr;
	}

	AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject());
	if (!CDO)
	{
		OutResult->SetStringField(TEXT("error"), TEXT("CDO is not an Actor"));
		OutResult->SetBoolField(TEXT("success"), false);
		return nullptr;
	}

	return CDO;
}

void FNetworkingHandlers::SaveBlueprint(UBlueprint* Blueprint)
{
	if (!Blueprint) return;
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
}

TSharedPtr<FJsonValue> FNetworkingHandlers::GetNetworkingInfo(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString BlueprintPath;
	if (!Params->TryGetStringField(TEXT("blueprintPath"), BlueprintPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'blueprintPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	AActor* CDO = LoadBlueprintCDO(BlueprintPath, Result);
	if (!CDO) return MakeShared<FJsonValueObject>(Result);

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetBoolField(TEXT("replicates"), CDO->GetIsReplicated());
	Result->SetNumberField(TEXT("netUpdateFrequency"), CDO->NetUpdateFrequency);
	Result->SetNumberField(TEXT("minNetUpdateFrequency"), CDO->MinNetUpdateFrequency);
	Result->SetNumberField(TEXT("netPriority"), CDO->NetPriority);
	Result->SetBoolField(TEXT("alwaysRelevant"), CDO->bAlwaysRelevant);
	Result->SetBoolField(TEXT("replicateMovement"), CDO->IsReplicatingMovement());
	Result->SetNumberField(TEXT("netDormancy"), (int32)CDO->NetDormancy);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::SetReplicates(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString BlueprintPath;
	if (!Params->TryGetStringField(TEXT("blueprintPath"), BlueprintPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'blueprintPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	AActor* CDO = LoadBlueprintCDO(BlueprintPath, Result);
	if (!CDO) return MakeShared<FJsonValueObject>(Result);

	bool bReplicates = false;
	Params->TryGetBoolField(TEXT("replicates"), bReplicates);
	CDO->SetReplicates(bReplicates);

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	SaveBlueprint(Blueprint);

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetBoolField(TEXT("replicates"), bReplicates);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::ConfigureNetUpdateFrequency(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString BlueprintPath;
	if (!Params->TryGetStringField(TEXT("blueprintPath"), BlueprintPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'blueprintPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	AActor* CDO = LoadBlueprintCDO(BlueprintPath, Result);
	if (!CDO) return MakeShared<FJsonValueObject>(Result);

	double NetUpdateFrequency = 0;
	if (Params->TryGetNumberField(TEXT("netUpdateFrequency"), NetUpdateFrequency))
	{
		CDO->NetUpdateFrequency = (float)NetUpdateFrequency;
	}
	double MinNetUpdateFrequency = 0;
	if (Params->TryGetNumberField(TEXT("minNetUpdateFrequency"), MinNetUpdateFrequency))
	{
		CDO->MinNetUpdateFrequency = (float)MinNetUpdateFrequency;
	}

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	SaveBlueprint(Blueprint);

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetNumberField(TEXT("netUpdateFrequency"), CDO->NetUpdateFrequency);
	Result->SetNumberField(TEXT("minNetUpdateFrequency"), CDO->MinNetUpdateFrequency);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::SetNetDormancy(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString BlueprintPath;
	if (!Params->TryGetStringField(TEXT("blueprintPath"), BlueprintPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'blueprintPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	AActor* CDO = LoadBlueprintCDO(BlueprintPath, Result);
	if (!CDO) return MakeShared<FJsonValueObject>(Result);

	FString Dormancy;
	if (Params->TryGetStringField(TEXT("dormancy"), Dormancy))
	{
		if (Dormancy == TEXT("DORM_Never"))
			CDO->NetDormancy = DORM_Never;
		else if (Dormancy == TEXT("DORM_Awake"))
			CDO->NetDormancy = DORM_Awake;
		else if (Dormancy == TEXT("DORM_DormantAll"))
			CDO->NetDormancy = DORM_DormantAll;
		else if (Dormancy == TEXT("DORM_DormantPartial"))
			CDO->NetDormancy = DORM_DormantPartial;
		else if (Dormancy == TEXT("DORM_Initial"))
			CDO->NetDormancy = DORM_Initial;
	}

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	SaveBlueprint(Blueprint);

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetNumberField(TEXT("netDormancy"), (int32)CDO->NetDormancy);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::SetAlwaysRelevant(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString BlueprintPath;
	if (!Params->TryGetStringField(TEXT("blueprintPath"), BlueprintPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'blueprintPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	AActor* CDO = LoadBlueprintCDO(BlueprintPath, Result);
	if (!CDO) return MakeShared<FJsonValueObject>(Result);

	bool bAlwaysRelevant = false;
	Params->TryGetBoolField(TEXT("alwaysRelevant"), bAlwaysRelevant);
	CDO->bAlwaysRelevant = bAlwaysRelevant;

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	SaveBlueprint(Blueprint);

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetBoolField(TEXT("alwaysRelevant"), bAlwaysRelevant);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::SetNetPriority(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString BlueprintPath;
	if (!Params->TryGetStringField(TEXT("blueprintPath"), BlueprintPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'blueprintPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	AActor* CDO = LoadBlueprintCDO(BlueprintPath, Result);
	if (!CDO) return MakeShared<FJsonValueObject>(Result);

	double NetPriority = 1.0;
	if (Params->TryGetNumberField(TEXT("netPriority"), NetPriority))
	{
		CDO->NetPriority = (float)NetPriority;
	}

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	SaveBlueprint(Blueprint);

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetNumberField(TEXT("netPriority"), CDO->NetPriority);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::SetReplicateMovement(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString BlueprintPath;
	if (!Params->TryGetStringField(TEXT("blueprintPath"), BlueprintPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'blueprintPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	AActor* CDO = LoadBlueprintCDO(BlueprintPath, Result);
	if (!CDO) return MakeShared<FJsonValueObject>(Result);

	bool bReplicateMovement = false;
	Params->TryGetBoolField(TEXT("replicateMovement"), bReplicateMovement);
	CDO->SetReplicatingMovement(bReplicateMovement);

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	SaveBlueprint(Blueprint);

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetBoolField(TEXT("replicateMovement"), bReplicateMovement);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::SetVariableReplication(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString BlueprintPath;
	if (!Params->TryGetStringField(TEXT("blueprintPath"), BlueprintPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'blueprintPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString VariableName;
	if (!Params->TryGetStringField(TEXT("variableName"), VariableName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'variableName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString ReplicationType = TEXT("None");
	Params->TryGetStringField(TEXT("replicationType"), ReplicationType);

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!Blueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find the variable in the blueprint
	FName VarFName(*VariableName);
	FBPVariableDescription* VarDesc = nullptr;
	for (FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == VarFName)
		{
			VarDesc = &Var;
			break;
		}
	}

	if (!VarDesc)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Variable '%s' not found in blueprint"), *VariableName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Set the replication condition
	if (ReplicationType == TEXT("Replicated"))
	{
		VarDesc->PropertyFlags |= CPF_Net;
		VarDesc->PropertyFlags &= ~CPF_RepNotify;
		VarDesc->ReplicationCondition = COND_None;
		FBlueprintEditorUtils::SetBlueprintVariableRepCondition(Blueprint, VarFName, COND_None);
	}
	else if (ReplicationType == TEXT("RepNotify"))
	{
		VarDesc->PropertyFlags |= CPF_Net;
		VarDesc->PropertyFlags |= CPF_RepNotify;
		VarDesc->ReplicationCondition = COND_None;
		FBlueprintEditorUtils::SetBlueprintVariableRepCondition(Blueprint, VarFName, COND_None);
	}
	else // "None"
	{
		VarDesc->PropertyFlags &= ~CPF_Net;
		VarDesc->PropertyFlags &= ~CPF_RepNotify;
	}

	SaveBlueprint(Blueprint);

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetStringField(TEXT("variableName"), VariableName);
	Result->SetStringField(TEXT("replicationType"), ReplicationType);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::GetReplicationInfo(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString BlueprintPath;
	if (!Params->TryGetStringField(TEXT("blueprintPath"), BlueprintPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'blueprintPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found or has no generated class: %s"), *BlueprintPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject());
	if (!CDO)
	{
		Result->SetStringField(TEXT("error"), TEXT("CDO is not an Actor"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// General replication info
	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetBoolField(TEXT("replicates"), CDO->GetIsReplicated());
	Result->SetBoolField(TEXT("replicateMovement"), CDO->IsReplicatingMovement());
	Result->SetBoolField(TEXT("alwaysRelevant"), CDO->bAlwaysRelevant);
	Result->SetBoolField(TEXT("onlyRelevantToOwner"), CDO->bOnlyRelevantToOwner);
	Result->SetNumberField(TEXT("netUpdateFrequency"), CDO->NetUpdateFrequency);
	Result->SetNumberField(TEXT("netPriority"), CDO->NetPriority);
	Result->SetNumberField(TEXT("netDormancy"), (int32)CDO->NetDormancy);

	// Iterate all properties on the CDO class looking for replicated ones (CPF_Net flag)
	TArray<TSharedPtr<FJsonValue>> ReplicatedProps;
	for (TFieldIterator<FProperty> PropIt(Blueprint->GeneratedClass); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property) continue;

		if (Property->HasAnyPropertyFlags(CPF_Net))
		{
			TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
			PropObj->SetStringField(TEXT("name"), Property->GetName());
			PropObj->SetStringField(TEXT("type"), Property->GetCPPType());
			PropObj->SetBoolField(TEXT("isRepNotify"), Property->HasAnyPropertyFlags(CPF_RepNotify));

			// Determine replication condition from the blueprint variable description
			FString ConditionStr = TEXT("COND_None");
			for (const FBPVariableDescription& Var : Blueprint->NewVariables)
			{
				if (Var.VarName == Property->GetFName())
				{
					switch (Var.ReplicationCondition)
					{
					case COND_None:             ConditionStr = TEXT("COND_None"); break;
					case COND_InitialOnly:      ConditionStr = TEXT("COND_InitialOnly"); break;
					case COND_OwnerOnly:         ConditionStr = TEXT("COND_OwnerOnly"); break;
					case COND_SkipOwner:         ConditionStr = TEXT("COND_SkipOwner"); break;
					case COND_SimulatedOnly:     ConditionStr = TEXT("COND_SimulatedOnly"); break;
					case COND_AutonomousOnly:    ConditionStr = TEXT("COND_AutonomousOnly"); break;
					case COND_SimulatedOrPhysics: ConditionStr = TEXT("COND_SimulatedOrPhysics"); break;
					case COND_InitialOrOwner:    ConditionStr = TEXT("COND_InitialOrOwner"); break;
					case COND_Custom:            ConditionStr = TEXT("COND_Custom"); break;
					case COND_ReplayOrOwner:     ConditionStr = TEXT("COND_ReplayOrOwner"); break;
					case COND_ReplayOnly:        ConditionStr = TEXT("COND_ReplayOnly"); break;
					case COND_SkipReplay:        ConditionStr = TEXT("COND_SkipReplay"); break;
					case COND_Dynamic:           ConditionStr = TEXT("COND_Dynamic"); break;
					case COND_Never:             ConditionStr = TEXT("COND_Never"); break;
					default:                     ConditionStr = TEXT("Unknown"); break;
					}
					break;
				}
			}
			PropObj->SetStringField(TEXT("replicationCondition"), ConditionStr);

			// Rep notify function name
			if (Property->HasAnyPropertyFlags(CPF_RepNotify) && Property->RepNotifyFunc != NAME_None)
			{
				PropObj->SetStringField(TEXT("repNotifyFunc"), Property->RepNotifyFunc.ToString());
			}

			ReplicatedProps.Add(MakeShared<FJsonValueObject>(PropObj));
		}
	}

	Result->SetArrayField(TEXT("replicatedProperties"), ReplicatedProps);
	Result->SetNumberField(TEXT("replicatedPropertyCount"), ReplicatedProps.Num());
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::SetOwnerOnlyRelevant(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString BlueprintPath;
	if (!Params->TryGetStringField(TEXT("blueprintPath"), BlueprintPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'blueprintPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	AActor* CDO = LoadBlueprintCDO(BlueprintPath, Result);
	if (!CDO) return MakeShared<FJsonValueObject>(Result);

	bool bOnlyRelevantToOwner = false;
	Params->TryGetBoolField(TEXT("onlyRelevantToOwner"), bOnlyRelevantToOwner);
	CDO->bOnlyRelevantToOwner = bOnlyRelevantToOwner;

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	SaveBlueprint(Blueprint);

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetBoolField(TEXT("onlyRelevantToOwner"), bOnlyRelevantToOwner);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}
