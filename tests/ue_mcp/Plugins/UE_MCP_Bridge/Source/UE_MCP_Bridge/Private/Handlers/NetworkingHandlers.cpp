#include "NetworkingHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
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
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"

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

	// Aliases
	Registry.RegisterHandler(TEXT("set_property_replicated"), &SetVariableReplication);
	Registry.RegisterHandler(TEXT("set_only_relevant_to_owner"), &SetOwnerOnlyRelevant);
	// New handlers
	Registry.RegisterHandler(TEXT("set_net_load_on_client"), &SetNetLoadOnClient);
	Registry.RegisterHandler(TEXT("configure_net_cull_distance"), &ConfigureNetCullDistance);
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
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	AActor* CDO = LoadBlueprintCDO(BlueprintPath, Result);
	if (!CDO) return MCPResult(Result);

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetBoolField(TEXT("replicates"), CDO->GetIsReplicated());
	Result->SetNumberField(TEXT("netUpdateFrequency"), CDO->GetNetUpdateFrequency());
	Result->SetNumberField(TEXT("minNetUpdateFrequency"), CDO->GetMinNetUpdateFrequency());
	Result->SetNumberField(TEXT("netPriority"), CDO->NetPriority);
	Result->SetBoolField(TEXT("alwaysRelevant"), CDO->bAlwaysRelevant);
	Result->SetBoolField(TEXT("replicateMovement"), CDO->IsReplicatingMovement());
	Result->SetNumberField(TEXT("netDormancy"), (int32)CDO->NetDormancy);
	Result->SetBoolField(TEXT("success"), true);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::SetReplicates(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	AActor* CDO = LoadBlueprintCDO(BlueprintPath, Result);
	if (!CDO) return MCPResult(Result);

	bool bReplicates = OptionalBool(Params, TEXT("replicates"), false);
	const bool bPrev = CDO->GetIsReplicated();

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetBoolField(TEXT("replicates"), bReplicates);
	Result->SetBoolField(TEXT("success"), true);

	if (bPrev == bReplicates)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		return MCPResult(Result);
	}

	CDO->SetReplicates(bReplicates);
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	SaveBlueprint(Blueprint);

	MCPSetUpdated(Result);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Payload->SetBoolField(TEXT("replicates"), bPrev);
	MCPSetRollback(Result, TEXT("set_replicates"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::ConfigureNetUpdateFrequency(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	AActor* CDO = LoadBlueprintCDO(BlueprintPath, Result);
	if (!CDO) return MCPResult(Result);

	double NetUpdateFrequency = 0;
	if (Params->TryGetNumberField(TEXT("netUpdateFrequency"), NetUpdateFrequency))
	{
		CDO->SetNetUpdateFrequency((float)NetUpdateFrequency);
	}
	double MinNetUpdateFrequency = 0;
	if (Params->TryGetNumberField(TEXT("minNetUpdateFrequency"), MinNetUpdateFrequency))
	{
		CDO->SetMinNetUpdateFrequency((float)MinNetUpdateFrequency);
	}

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	SaveBlueprint(Blueprint);

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetNumberField(TEXT("netUpdateFrequency"), CDO->GetNetUpdateFrequency());
	Result->SetNumberField(TEXT("minNetUpdateFrequency"), CDO->GetMinNetUpdateFrequency());
	Result->SetBoolField(TEXT("success"), true);
	return MCPResult(Result);
}

static FString DormancyToString(ENetDormancy D)
{
	switch (D)
	{
	case DORM_Never: return TEXT("DORM_Never");
	case DORM_Awake: return TEXT("DORM_Awake");
	case DORM_DormantAll: return TEXT("DORM_DormantAll");
	case DORM_DormantPartial: return TEXT("DORM_DormantPartial");
	case DORM_Initial: return TEXT("DORM_Initial");
	default: return TEXT("DORM_Awake");
	}
}

TSharedPtr<FJsonValue> FNetworkingHandlers::SetNetDormancy(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	AActor* CDO = LoadBlueprintCDO(BlueprintPath, Result);
	if (!CDO) return MCPResult(Result);

	FString Dormancy = OptionalString(Params, TEXT("dormancy"));
	const ENetDormancy PrevDormancy = CDO->NetDormancy;
	const FString PrevDormStr = DormancyToString(PrevDormancy);
	ENetDormancy NewDormancy = PrevDormancy;
	if (!Dormancy.IsEmpty())
	{
		if (Dormancy == TEXT("DORM_Never")) NewDormancy = DORM_Never;
		else if (Dormancy == TEXT("DORM_Awake")) NewDormancy = DORM_Awake;
		else if (Dormancy == TEXT("DORM_DormantAll")) NewDormancy = DORM_DormantAll;
		else if (Dormancy == TEXT("DORM_DormantPartial")) NewDormancy = DORM_DormantPartial;
		else if (Dormancy == TEXT("DORM_Initial")) NewDormancy = DORM_Initial;
	}

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetNumberField(TEXT("netDormancy"), (int32)NewDormancy);
	Result->SetBoolField(TEXT("success"), true);

	if (NewDormancy == PrevDormancy)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		return MCPResult(Result);
	}

	CDO->NetDormancy = NewDormancy;
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	SaveBlueprint(Blueprint);

	MCPSetUpdated(Result);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Payload->SetStringField(TEXT("dormancy"), PrevDormStr);
	MCPSetRollback(Result, TEXT("set_net_dormancy"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::SetAlwaysRelevant(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	AActor* CDO = LoadBlueprintCDO(BlueprintPath, Result);
	if (!CDO) return MCPResult(Result);

	bool bAlwaysRelevant = OptionalBool(Params, TEXT("alwaysRelevant"), false);
	const bool bPrev = CDO->bAlwaysRelevant;

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetBoolField(TEXT("alwaysRelevant"), bAlwaysRelevant);
	Result->SetBoolField(TEXT("success"), true);

	if (bPrev == bAlwaysRelevant)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		return MCPResult(Result);
	}

	CDO->bAlwaysRelevant = bAlwaysRelevant;
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	SaveBlueprint(Blueprint);

	MCPSetUpdated(Result);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Payload->SetBoolField(TEXT("alwaysRelevant"), bPrev);
	MCPSetRollback(Result, TEXT("set_always_relevant"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::SetNetPriority(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	AActor* CDO = LoadBlueprintCDO(BlueprintPath, Result);
	if (!CDO) return MCPResult(Result);

	double NetPriority = OptionalNumber(Params, TEXT("netPriority"), 1.0);
	const float fPrev = CDO->NetPriority;

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetNumberField(TEXT("netPriority"), NetPriority);
	Result->SetBoolField(TEXT("success"), true);

	if (FMath::IsNearlyEqual(fPrev, (float)NetPriority))
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		return MCPResult(Result);
	}

	CDO->NetPriority = (float)NetPriority;
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	SaveBlueprint(Blueprint);

	MCPSetUpdated(Result);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Payload->SetNumberField(TEXT("netPriority"), fPrev);
	MCPSetRollback(Result, TEXT("set_net_priority"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::SetReplicateMovement(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	AActor* CDO = LoadBlueprintCDO(BlueprintPath, Result);
	if (!CDO) return MCPResult(Result);

	bool bReplicateMovement = OptionalBool(Params, TEXT("replicateMovement"), false);
	const bool bPrev = CDO->IsReplicatingMovement();

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetBoolField(TEXT("replicateMovement"), bReplicateMovement);
	Result->SetBoolField(TEXT("success"), true);

	if (bPrev == bReplicateMovement)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		return MCPResult(Result);
	}

	CDO->SetReplicatingMovement(bReplicateMovement);
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	SaveBlueprint(Blueprint);

	MCPSetUpdated(Result);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Payload->SetBoolField(TEXT("replicateMovement"), bPrev);
	MCPSetRollback(Result, TEXT("set_replicate_movement"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::SetVariableReplication(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	FString VariableName;
	if (auto Err = RequireString(Params, TEXT("variableName"), VariableName)) return Err;

	FString ReplicationType = OptionalString(Params, TEXT("replicationType"), TEXT("None"));

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
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
		return MCPError(FString::Printf(TEXT("Variable '%s' not found in blueprint"), *VariableName));
	}

	// Capture previous state
	const bool bWasNet = (VarDesc->PropertyFlags & CPF_Net) != 0;
	const bool bWasRepNotify = (VarDesc->PropertyFlags & CPF_RepNotify) != 0;
	FString PrevType = TEXT("None");
	if (bWasNet && bWasRepNotify) PrevType = TEXT("RepNotify");
	else if (bWasNet) PrevType = TEXT("Replicated");

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetStringField(TEXT("variableName"), VariableName);
	Result->SetStringField(TEXT("replicationType"), ReplicationType);

	if (PrevType == ReplicationType)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		return MCPResult(Result);
	}

	// Set the replication condition
	if (ReplicationType == TEXT("Replicated"))
	{
		VarDesc->PropertyFlags |= CPF_Net;
		VarDesc->PropertyFlags &= ~CPF_RepNotify;
		VarDesc->ReplicationCondition = COND_None;
	}
	else if (ReplicationType == TEXT("RepNotify"))
	{
		VarDesc->PropertyFlags |= CPF_Net;
		VarDesc->PropertyFlags |= CPF_RepNotify;
		VarDesc->ReplicationCondition = COND_None;
	}
	else // "None"
	{
		VarDesc->PropertyFlags &= ~CPF_Net;
		VarDesc->PropertyFlags &= ~CPF_RepNotify;
	}

	SaveBlueprint(Blueprint);

	MCPSetUpdated(Result);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Payload->SetStringField(TEXT("variableName"), VariableName);
	Payload->SetStringField(TEXT("replicationType"), PrevType);
	MCPSetRollback(Result, TEXT("set_variable_replication"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::GetReplicationInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found or has no generated class: %s"), *BlueprintPath));
	}

	AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject());
	if (!CDO)
	{
		return MCPError(TEXT("CDO is not an Actor"));
	}

	// General replication info
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetBoolField(TEXT("replicates"), CDO->GetIsReplicated());
	Result->SetBoolField(TEXT("replicateMovement"), CDO->IsReplicatingMovement());
	Result->SetBoolField(TEXT("alwaysRelevant"), CDO->bAlwaysRelevant);
	Result->SetBoolField(TEXT("onlyRelevantToOwner"), CDO->bOnlyRelevantToOwner);
	Result->SetNumberField(TEXT("netUpdateFrequency"), CDO->GetNetUpdateFrequency());
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
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::SetOwnerOnlyRelevant(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	AActor* CDO = LoadBlueprintCDO(BlueprintPath, Result);
	if (!CDO) return MCPResult(Result);

	bool bOnlyRelevantToOwner = OptionalBool(Params, TEXT("onlyRelevantToOwner"), false);
	const bool bPrev = CDO->bOnlyRelevantToOwner;

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetBoolField(TEXT("onlyRelevantToOwner"), bOnlyRelevantToOwner);
	Result->SetBoolField(TEXT("success"), true);

	if (bPrev == bOnlyRelevantToOwner)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		return MCPResult(Result);
	}

	CDO->bOnlyRelevantToOwner = bOnlyRelevantToOwner;
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	SaveBlueprint(Blueprint);

	MCPSetUpdated(Result);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Payload->SetBoolField(TEXT("onlyRelevantToOwner"), bPrev);
	MCPSetRollback(Result, TEXT("set_owner_only_relevant"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::SetNetLoadOnClient(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	AActor* CDO = LoadBlueprintCDO(BlueprintPath, Result);
	if (!CDO) return MCPResult(Result);

	bool bLoadOnClient = OptionalBool(Params, TEXT("loadOnClient"), true);
	bool bPrev = bLoadOnClient;

	FProperty* Prop = CDO->GetClass()->FindPropertyByName(TEXT("bNetLoadOnClient"));
	if (Prop)
	{
		bool* ValPtr = Prop->ContainerPtrToValuePtr<bool>(CDO);
		if (ValPtr)
		{
			bPrev = *ValPtr;
		}
	}

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetBoolField(TEXT("loadOnClient"), bLoadOnClient);
	Result->SetBoolField(TEXT("success"), true);

	if (bPrev == bLoadOnClient)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		return MCPResult(Result);
	}

	if (Prop)
	{
		bool* ValPtr = Prop->ContainerPtrToValuePtr<bool>(CDO);
		if (ValPtr) { *ValPtr = bLoadOnClient; }
	}

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (BP) SaveBlueprint(BP);

	MCPSetUpdated(Result);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Payload->SetBoolField(TEXT("loadOnClient"), bPrev);
	MCPSetRollback(Result, TEXT("set_net_load_on_client"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::ConfigureNetCullDistance(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	AActor* CDO = LoadBlueprintCDO(BlueprintPath, Result);
	if (!CDO) return MCPResult(Result);

	double Distance = OptionalNumber(Params, TEXT("netCullDistanceSquared"), 225000000.0);

	FProperty* Prop = CDO->GetClass()->FindPropertyByName(TEXT("NetCullDistanceSquared"));
	if (Prop)
	{
		FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop);
		FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop);
		if (FloatProp)
		{
			FloatProp->SetPropertyValue_InContainer(CDO, static_cast<float>(Distance));
		}
		else if (DoubleProp)
		{
			DoubleProp->SetPropertyValue_InContainer(CDO, Distance);
		}
	}

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (BP) SaveBlueprint(BP);

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetNumberField(TEXT("netCullDistanceSquared"), Distance);
	Result->SetBoolField(TEXT("success"), true);
	return MCPResult(Result);
}
