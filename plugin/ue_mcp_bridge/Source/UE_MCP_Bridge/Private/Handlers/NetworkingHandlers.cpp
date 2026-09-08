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
	Registry.RegisterHandler(TEXT("set_property_replicated"), &SetVariableReplication);
	Registry.RegisterHandler(TEXT("set_only_relevant_to_owner"), &SetOwnerOnlyRelevant);
	// New handlers
	Registry.RegisterHandler(TEXT("set_net_load_on_client"), &SetNetLoadOnClient);
	Registry.RegisterHandler(TEXT("configure_net_cull_distance"), &ConfigureNetCullDistance);
}

AActor* FNetworkingHandlers::LoadBlueprintCDO(const FString& BlueprintPath, TSharedPtr<FJsonObject>& OutResult)
{
	// Thin adapter over the shared ::LoadBlueprintCDO<T> helper in HandlerUtils.h.
	// Translates the helper's TSharedPtr<FJsonValue> error into the OutResult-style
	// {success:false, error:...} object the networking call sites accumulate into.
	TSharedPtr<FJsonValue> Err;
	AActor* CDO = ::LoadBlueprintCDO<AActor>(BlueprintPath, Err);
	if (!CDO)
	{
		FString ErrMsg = TEXT("Failed to load blueprint CDO");
		if (Err.IsValid())
		{
			if (TSharedPtr<FJsonObject> ErrObj = Err->AsObject())
			{
				ErrObj->TryGetStringField(TEXT("error"), ErrMsg);
			}
		}
		OutResult->SetStringField(TEXT("error"), ErrMsg);
		OutResult->SetBoolField(TEXT("success"), false);
	}
	return CDO;
}

void FNetworkingHandlers::SaveBlueprint(UBlueprint* Blueprint)
{
	if (!Blueprint) return;
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);
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
#if UE_MCP_HAS_5_5_API
	Result->SetNumberField(TEXT("netUpdateFrequency"), CDO->GetNetUpdateFrequency());
	Result->SetNumberField(TEXT("minNetUpdateFrequency"), CDO->GetMinNetUpdateFrequency());
#else
	Result->SetNumberField(TEXT("netUpdateFrequency"), CDO->NetUpdateFrequency);
	Result->SetNumberField(TEXT("minNetUpdateFrequency"), CDO->MinNetUpdateFrequency);
#endif
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

	Result->SetBoolField(TEXT("previousReplicates"), bPrev);

	if (bPrev == bReplicates)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The class already had this value, so nothing changed and there is nothing to undo."));
		return MCPResult(Result);
	}

	CDO->SetReplicates(bReplicates);
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	SaveBlueprint(Blueprint);

	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("unchanged"), false);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Payload->SetBoolField(TEXT("replicates"), bPrev);
	MCPSetRollback(Result, TEXT("set_replicates"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), false);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::ConfigureNetUpdateFrequency(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	AActor* CDO = LoadBlueprintCDO(BlueprintPath, Result);
	if (!CDO) return MCPResult(Result);

	// Both frequencies as they stand, read before either is written. The record
	// restores both regardless of which one this call was asked to change:
	// rewriting a value with the value it already has is a no-op, and carrying
	// the pair means an inverse cannot leave the two inconsistent.
#if UE_MCP_HAS_5_5_API
	const float PrevFrequency = CDO->GetNetUpdateFrequency();
	const float PrevMinFrequency = CDO->GetMinNetUpdateFrequency();
#else
	const float PrevFrequency = CDO->NetUpdateFrequency;
	const float PrevMinFrequency = CDO->MinNetUpdateFrequency;
#endif

	double NetUpdateFrequency = 0;
	if (Params->TryGetNumberField(TEXT("netUpdateFrequency"), NetUpdateFrequency))
	{
#if UE_MCP_HAS_5_5_API
		CDO->SetNetUpdateFrequency((float)NetUpdateFrequency);
#else
		CDO->NetUpdateFrequency = (float)NetUpdateFrequency;
#endif
	}
	double MinNetUpdateFrequency = 0;
	if (Params->TryGetNumberField(TEXT("minNetUpdateFrequency"), MinNetUpdateFrequency))
	{
#if UE_MCP_HAS_5_5_API
		CDO->SetMinNetUpdateFrequency((float)MinNetUpdateFrequency);
#else
		CDO->MinNetUpdateFrequency = (float)MinNetUpdateFrequency;
#endif
	}

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
#if UE_MCP_HAS_5_5_API
	const float NewFrequency = CDO->GetNetUpdateFrequency();
	const float NewMinFrequency = CDO->GetMinNetUpdateFrequency();
#else
	const float NewFrequency = CDO->NetUpdateFrequency;
	const float NewMinFrequency = CDO->MinNetUpdateFrequency;
#endif
	// Both sides are read off the same property, before and after, so an exact
	// comparison is the right one: a tolerance here would report "unchanged" for
	// a small real change and then emit no record to undo it.
	const bool bFrequencyChanged = PrevFrequency != NewFrequency || PrevMinFrequency != NewMinFrequency;

	// Saved only when something moved. Saving on a no-op would rewrite the
	// package on disk and then report unchanged in the same breath.
	if (bFrequencyChanged)
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
		SaveBlueprint(Blueprint);
	}
	Result->SetBoolField(TEXT("saved"), bFrequencyChanged);
	Result->SetNumberField(TEXT("netUpdateFrequency"), NewFrequency);
	Result->SetNumberField(TEXT("minNetUpdateFrequency"), NewMinFrequency);
	Result->SetNumberField(TEXT("previousNetUpdateFrequency"), PrevFrequency);
	Result->SetNumberField(TEXT("previousMinNetUpdateFrequency"), PrevMinFrequency);
	Result->SetBoolField(TEXT("success"), true);

	Result->SetBoolField(TEXT("unchanged"), !bFrequencyChanged);
	if (bFrequencyChanged)
	{
		MCPSetUpdated(Result);
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("blueprintPath"), BlueprintPath);
		Payload->SetNumberField(TEXT("netUpdateFrequency"), PrevFrequency);
		Payload->SetNumberField(TEXT("minNetUpdateFrequency"), PrevMinFrequency);
		MCPSetRollback(Result, TEXT("configure_net_update_frequency"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), false);
	}
	else
	{
		// Deliberately NOT MCPSetExisted: "existed" answers whether an entity
		// was already there, and both frequencies exist either way. The
		// question here is whether the write moved anything.
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Both frequencies already held these values, so nothing changed and there is nothing to undo."));
	}
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
		else
		{
			// A misspelled value used to fall through and leave the dormancy
			// where it was, and the call still reported success - which reads
			// exactly like a write that landed.
			return MCPError(FString::Printf(
				TEXT("Unknown dormancy '%s'. Use DORM_Never, DORM_Awake, DORM_DormantAll, DORM_DormantPartial or "
					 "DORM_Initial. The class currently has %s."),
				*Dormancy, *PrevDormStr));
		}
	}

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetNumberField(TEXT("netDormancy"), (int32)NewDormancy);
	Result->SetStringField(TEXT("dormancy"), DormancyToString(NewDormancy));
	Result->SetStringField(TEXT("previousDormancy"), PrevDormStr);
	Result->SetBoolField(TEXT("success"), true);

	if (NewDormancy == PrevDormancy)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), Dormancy.IsEmpty()
			? TEXT("No 'dormancy' was passed, so nothing was written and there is nothing to undo.")
			: TEXT("The class already had this dormancy, so nothing changed and there is nothing to undo."));
		return MCPResult(Result);
	}

	CDO->NetDormancy = NewDormancy;
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	SaveBlueprint(Blueprint);

	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("unchanged"), false);
	// PrevDormStr is one of the five spellings the parser above accepts, so the
	// record round-trips through this same handler.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Payload->SetStringField(TEXT("dormancy"), PrevDormStr);
	MCPSetRollback(Result, TEXT("set_net_dormancy"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), false);
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

	Result->SetBoolField(TEXT("previousAlwaysRelevant"), bPrev);

	if (bPrev == bAlwaysRelevant)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The class already had this value, so nothing changed and there is nothing to undo."));
		return MCPResult(Result);
	}

	CDO->bAlwaysRelevant = bAlwaysRelevant;
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	SaveBlueprint(Blueprint);

	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("unchanged"), false);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Payload->SetBoolField(TEXT("alwaysRelevant"), bPrev);
	MCPSetRollback(Result, TEXT("set_always_relevant"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), false);
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

	Result->SetNumberField(TEXT("previousNetPriority"), fPrev);

	if (FMath::IsNearlyEqual(fPrev, (float)NetPriority))
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The class already had this net priority, so nothing changed and there is nothing to undo."));
		return MCPResult(Result);
	}

	CDO->NetPriority = (float)NetPriority;
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	SaveBlueprint(Blueprint);

	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("unchanged"), false);
	// The previous value is carried as the float the property actually held, so
	// the replay writes back exactly what was overwritten.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Payload->SetNumberField(TEXT("netPriority"), fPrev);
	MCPSetRollback(Result, TEXT("set_net_priority"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), false);
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

	Result->SetBoolField(TEXT("previousReplicateMovement"), bPrev);

	if (bPrev == bReplicateMovement)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The class already had this value, so nothing changed and there is nothing to undo."));
		return MCPResult(Result);
	}

	CDO->SetReplicatingMovement(bReplicateMovement);
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	SaveBlueprint(Blueprint);

	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("unchanged"), false);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Payload->SetBoolField(TEXT("replicateMovement"), bPrev);
	MCPSetRollback(Result, TEXT("set_replicate_movement"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), false);
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

	// Capture previous state. The replication CONDITION is read here too,
	// before the write below resets it, so the note on the record can say
	// truthfully whether one was lost.
	const bool bWasNet = (VarDesc->PropertyFlags & CPF_Net) != 0;
	const bool bWasRepNotify = (VarDesc->PropertyFlags & CPF_RepNotify) != 0;
	const bool bHadReplicationCondition = VarDesc->ReplicationCondition != COND_None;
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
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetStringField(TEXT("previousReplicationType"), PrevType);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The variable already had this replication type, so nothing changed and there is nothing to undo."));
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
	Result->SetBoolField(TEXT("unchanged"), false);
	Result->SetStringField(TEXT("previousReplicationType"), PrevType);
	// The REGISTERED method name is set_property_replicated; set_variable_
	// replication is the C++ function's name and resolves to no handler, so a
	// replay of that record failed the bridge's own method lookup.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Payload->SetStringField(TEXT("variableName"), VariableName);
	Payload->SetStringField(TEXT("replicationType"), PrevType);
	MCPSetRollback(Result, TEXT("set_property_replicated"), Payload);
	// None/Replicated/RepNotify is the whole vocabulary this action writes and
	// the whole vocabulary it reads back, so the round trip is exact - with one
	// exception, named rather than hidden.
	Result->SetBoolField(TEXT("rollbackLossy"), bHadReplicationCondition);
	if (bHadReplicationCondition)
	{
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The replication type comes back, but the variable carried a ReplicationCondition other than "
				 "COND_None and this action resets that to COND_None whenever it makes a variable replicated. It has "
				 "no parameter to restore one, so the condition is not carried; set it back with "
				 "blueprint(set_variable_properties) if it mattered."));
	}
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
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The class already had this value, so nothing changed and there is nothing to undo."));
		return MCPResult(Result);
	}

	CDO->bOnlyRelevantToOwner = bOnlyRelevantToOwner;
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	SaveBlueprint(Blueprint);

	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("unchanged"), false);
	Result->SetBoolField(TEXT("previousOnlyRelevantToOwner"), bPrev);
	// The REGISTERED method name is set_only_relevant_to_owner; the C++
	// function is called SetOwnerOnlyRelevant and that name resolves to no
	// handler, so a replay of that record failed the bridge's method lookup.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Payload->SetBoolField(TEXT("onlyRelevantToOwner"), bPrev);
	MCPSetRollback(Result, TEXT("set_only_relevant_to_owner"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), false);
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
	Result->SetBoolField(TEXT("previousLoadOnClient"), bPrev);

	if (!Prop)
	{
		// Without the property there is nothing to read and nothing to write.
		// The previous value seeds itself from the request above, so this used
		// to fall into the "already had it" branch and report existed for a
		// class the handler never touched.
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetStringField(TEXT("warning"),
			TEXT("No 'bNetLoadOnClient' property on this class, so nothing was written."));
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Nothing was written, so there is nothing to undo."));
		return MCPResult(Result);
	}

	if (bPrev == bLoadOnClient)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The class already had this value, so nothing changed and there is nothing to undo."));
		return MCPResult(Result);
	}

	{
		bool* ValPtr = Prop->ContainerPtrToValuePtr<bool>(CDO);
		if (ValPtr) { *ValPtr = bLoadOnClient; }
	}

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (BP) SaveBlueprint(BP);

	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("unchanged"), false);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Payload->SetBoolField(TEXT("loadOnClient"), bPrev);
	MCPSetRollback(Result, TEXT("set_net_load_on_client"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), false);
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
	// The value that was there, read before the write, so the inverse restores
	// the actor's own cull distance rather than the engine default this action
	// falls back to.
	double PreviousDistance = 0.0;
	// What the property actually holds after the write, which is not always the
	// number asked for: a float property rounds it. Comparing the request
	// against the stored value would report a change on every repeat of the same
	// call, so the comparison below uses this instead.
	double StoredDistance = 0.0;
	bool bReadPrevious = false;
	if (Prop)
	{
		FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop);
		FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop);
		if (FloatProp)
		{
			PreviousDistance = FloatProp->GetPropertyValue_InContainer(CDO);
			bReadPrevious = true;
			FloatProp->SetPropertyValue_InContainer(CDO, static_cast<float>(Distance));
			StoredDistance = FloatProp->GetPropertyValue_InContainer(CDO);
		}
		else if (DoubleProp)
		{
			PreviousDistance = DoubleProp->GetPropertyValue_InContainer(CDO);
			bReadPrevious = true;
			DoubleProp->SetPropertyValue_InContainer(CDO, Distance);
			StoredDistance = DoubleProp->GetPropertyValue_InContainer(CDO);
		}
	}

	// Saved only when the value actually moved, so a no-op call does not rewrite
	// the package and then report that it changed nothing.
	const bool bDistanceChanged = bReadPrevious && PreviousDistance != StoredDistance;
	if (bDistanceChanged)
	{
		UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
		if (BP) SaveBlueprint(BP);
	}

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetNumberField(TEXT("netCullDistanceSquared"), Distance);
	Result->SetBoolField(TEXT("saved"), bDistanceChanged);
	Result->SetBoolField(TEXT("success"), true);

	if (!bReadPrevious)
	{
		// The property was not found, or was neither float nor double, so
		// nothing was written at all. Reporting that is what stops a caller
		// believing an engine-version rename landed silently.
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetStringField(TEXT("warning"),
			TEXT("No writable 'NetCullDistanceSquared' property on this class, so nothing was written."));
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Nothing was written, so there is nothing to undo."));
		return MCPResult(Result);
	}

	Result->SetNumberField(TEXT("previousNetCullDistanceSquared"), PreviousDistance);
	Result->SetNumberField(TEXT("storedNetCullDistanceSquared"), StoredDistance);
	if (!bDistanceChanged)
	{
		// Deliberately NOT MCPSetExisted; see the note on the frequency setter.
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The cull distance already held this value, so nothing changed and there is nothing to undo."));
		return MCPResult(Result);
	}

	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("unchanged"), false);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Payload->SetNumberField(TEXT("netCullDistanceSquared"), PreviousDistance);
	MCPSetRollback(Result, TEXT("configure_net_cull_distance"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), false);
	return MCPResult(Result);
}
