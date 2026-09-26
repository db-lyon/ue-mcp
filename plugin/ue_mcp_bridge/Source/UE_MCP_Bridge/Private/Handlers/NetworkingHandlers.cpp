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
	// Reports parameters its handlers never read (#1057).
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("networking"));

	// #1057: a spec'd handler declares its parameters here and nowhere else; the
	// TS surface is generated from a recording of these.
	using EType = EMCPParamType;
	Registry.RegisterHandler(TEXT("get_networking_info"), &GetNetworkingInfo, {
		MCPParam::Required(TEXT("blueprintPath"), EType::String, TEXT("Actor Blueprint asset path")),
	});
	Registry.RegisterHandler(TEXT("set_replicates"), &SetReplicates, {
		MCPParam::Required(TEXT("blueprintPath"), EType::String, TEXT("Actor Blueprint asset path")),
		MCPParam::Optional(TEXT("replicates"), EType::Boolean, TEXT("Replicate the actor (default false)")),
	});
	Registry.RegisterHandler(TEXT("configure_net_update_frequency"), &ConfigureNetUpdateFrequency, {
		MCPParam::Required(TEXT("blueprintPath"), EType::String, TEXT("Actor Blueprint asset path")),
		MCPParam::Optional(TEXT("netUpdateFrequency"), EType::Number, TEXT("NetUpdateFrequency in updates per second (omit to leave it)")),
		MCPParam::Optional(TEXT("minNetUpdateFrequency"), EType::Number, TEXT("MinNetUpdateFrequency in updates per second (omit to leave it)")),
	});
	Registry.RegisterHandler(TEXT("set_net_dormancy"), &SetNetDormancy, {
		MCPParam::Required(TEXT("blueprintPath"), EType::String, TEXT("Actor Blueprint asset path")),
		MCPParam::Required(TEXT("dormancy"), EType::String, TEXT("DORM_Never | DORM_Awake | DORM_DormantAll | DORM_DormantPartial | DORM_Initial")),
	});
	Registry.RegisterHandler(TEXT("set_always_relevant"), &SetAlwaysRelevant, {
		MCPParam::Required(TEXT("blueprintPath"), EType::String, TEXT("Actor Blueprint asset path")),
		MCPParam::Optional(TEXT("alwaysRelevant"), EType::Boolean, TEXT("bAlwaysRelevant (default false)")),
	});
	Registry.RegisterHandler(TEXT("set_net_priority"), &SetNetPriority, {
		MCPParam::Required(TEXT("blueprintPath"), EType::String, TEXT("Actor Blueprint asset path")),
		MCPParam::Optional(TEXT("netPriority"), EType::Number, TEXT("NetPriority (default 1.0)")),
	});
	Registry.RegisterHandler(TEXT("set_replicate_movement"), &SetReplicateMovement, {
		MCPParam::Required(TEXT("blueprintPath"), EType::String, TEXT("Actor Blueprint asset path")),
		MCPParam::Optional(TEXT("replicateMovement"), EType::Boolean, TEXT("Replicate movement (default false)")),
	});
	Registry.RegisterHandler(TEXT("set_property_replicated"), &SetVariableReplication, {
		MCPParam::Required(TEXT("blueprintPath"), EType::String, TEXT("Actor Blueprint asset path")),
		MCPParam::Required(TEXT("variableName"), EType::String, TEXT("Blueprint variable name")).Alias(TEXT("propertyName")),
		MCPParam::Optional(TEXT("replicationType"), EType::String, TEXT("None | Replicated | RepNotify (default None). Wins over replicated and repNotify")),
		MCPParam::Optional(TEXT("replicated"), EType::Boolean, TEXT("Shorthand: true is Replicated, false is None")),
		MCPParam::Optional(TEXT("repNotify"), EType::Boolean, TEXT("Shorthand: true is RepNotify, and wins over replicated")),
	});
	Registry.RegisterHandler(TEXT("set_only_relevant_to_owner"), &SetOwnerOnlyRelevant, {
		MCPParam::Required(TEXT("blueprintPath"), EType::String, TEXT("Actor Blueprint asset path")),
		MCPParam::Optional(TEXT("onlyRelevantToOwner"), EType::Boolean, TEXT("bOnlyRelevantToOwner (default false)")),
	});
	Registry.RegisterHandler(TEXT("set_net_load_on_client"), &SetNetLoadOnClient, {
		MCPParam::Required(TEXT("blueprintPath"), EType::String, TEXT("Actor Blueprint asset path")),
		MCPParam::Optional(TEXT("loadOnClient"), EType::Boolean, TEXT("bNetLoadOnClient (default true)")),
	});
	Registry.RegisterHandler(TEXT("configure_net_cull_distance"), &ConfigureNetCullDistance, {
		MCPParam::Required(TEXT("blueprintPath"), EType::String, TEXT("Actor Blueprint asset path")),
		MCPParam::Optional(TEXT("netCullDistanceSquared"), EType::Number, TEXT("NetCullDistanceSquared (default 225000000)")),
	});
}

namespace
{
	/** Compile and save a Blueprint, recording on Result whether the write
	 *  reached disk (#931). A null Blueprint is a failed save, not a skipped one. */
	void NetSaveBlueprint(UBlueprint* Blueprint, const FString& BlueprintPath, const TSharedPtr<FJsonObject>& Result)
	{
		FString Reason;
		bool bSaved = false;
		if (!Blueprint)
		{
			Reason = TEXT("The class has no owning Blueprint asset to save.");
		}
		else
		{
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			bSaved = SaveAssetPackageChecked(Blueprint, Reason);
		}
		MCPNoteSaveOutcome(Result, BlueprintPath, bSaved, Reason);
	}

	/** Save the Blueprint whose generated class CDO belongs to. */
	void NetSaveOwningBlueprint(AActor* CDO, const FString& BlueprintPath, const TSharedPtr<FJsonObject>& Result)
	{
		NetSaveBlueprint(CDO ? UBlueprint::GetBlueprintFromClass(CDO->GetClass()) : nullptr, BlueprintPath, Result);
	}

	void NetSetJsonValue(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, bool Value) { Obj->SetBoolField(Key, Value); }
	void NetSetJsonValue(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, double Value) { Obj->SetNumberField(Key, Value); }
	bool NetValuesEqual(bool A, bool B) { return A == B; }
	bool NetValuesEqual(double A, double B) { return FMath::IsNearlyEqual(static_cast<float>(A), static_cast<float>(B)); }

	/** One CDO value: read it, answer unchanged when it already holds NewValue,
	 *  otherwise write it, save the owning Blueprint and emit the inverse record
	 *  under the registered action name. */
	template <typename TValue, typename TGetter, typename TSetter>
	TSharedPtr<FJsonValue> NetApplyCdoSetting(
		const FString& BlueprintPath,
		const TCHAR* Action,
		const TCHAR* ValueKey,
		const TCHAR* PreviousKey,
		TValue NewValue,
		TGetter Get,
		TSetter Set,
		const TCHAR* UnchangedNote)
	{
		TSharedPtr<FJsonValue> LoadError;
		AActor* CDO = LoadBlueprintCDO<AActor>(BlueprintPath, LoadError);
		if (!CDO) return LoadError;
		const TValue Previous = Get(CDO);

		TSharedPtr<FJsonObject> Result = MCPSuccess();
		Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
		NetSetJsonValue(Result, ValueKey, NewValue);
		NetSetJsonValue(Result, PreviousKey, Previous);

		if (NetValuesEqual(Previous, NewValue))
		{
			MCPSetExisted(Result);
			Result->SetBoolField(TEXT("updated"), false);
			Result->SetBoolField(TEXT("unchanged"), true);
			Result->SetBoolField(TEXT("rollbackPossible"), false);
			Result->SetStringField(TEXT("rollbackNote"), UnchangedNote);
			return MCPResult(Result);
		}

		Set(CDO, NewValue);
		NetSaveOwningBlueprint(CDO, BlueprintPath, Result);

		MCPSetUpdated(Result);
		Result->SetBoolField(TEXT("unchanged"), false);
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("blueprintPath"), BlueprintPath);
		NetSetJsonValue(Payload, ValueKey, Previous);
		MCPSetRollback(Result, Action, Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), false);
		return MCPResult(Result);
	}

	const TCHAR* NetUnchangedNote = TEXT("The class already had this value, so nothing changed and there is nothing to undo.");
}

TSharedPtr<FJsonValue> FNetworkingHandlers::GetNetworkingInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	TSharedPtr<FJsonValue> LoadError;
	AActor* CDO = LoadBlueprintCDO<AActor>(BlueprintPath, LoadError);
	if (!CDO) return LoadError;
	TSharedPtr<FJsonObject> Result = MCPSuccess();

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
	const bool NewValue = OptionalBool(Params, TEXT("replicates"), false);

	return NetApplyCdoSetting<bool>(BlueprintPath, TEXT("set_replicates"), TEXT("replicates"), TEXT("previousReplicates"), NewValue,
		[](AActor* CDO) -> bool { return CDO->GetIsReplicated(); },
		[](AActor* CDO, bool Value) { CDO->SetReplicates(Value); },
		NetUnchangedNote);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::ConfigureNetUpdateFrequency(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;
	double NetUpdateFrequency = 0;
	const bool bHasNetUpdateFrequency = TryGetNumberParam(Params, TEXT("netUpdateFrequency"), NetUpdateFrequency);
	double MinNetUpdateFrequency = 0;
	const bool bHasMinNetUpdateFrequency = TryGetNumberParam(Params, TEXT("minNetUpdateFrequency"), MinNetUpdateFrequency);

	TSharedPtr<FJsonValue> LoadError;
	AActor* CDO = LoadBlueprintCDO<AActor>(BlueprintPath, LoadError);
	if (!CDO) return LoadError;
	TSharedPtr<FJsonObject> Result = MCPSuccess();

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

	if (bHasNetUpdateFrequency)
	{
#if UE_MCP_HAS_5_5_API
		CDO->SetNetUpdateFrequency((float)NetUpdateFrequency);
#else
		CDO->NetUpdateFrequency = (float)NetUpdateFrequency;
#endif
	}
	if (bHasMinNetUpdateFrequency)
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
		NetSaveOwningBlueprint(CDO, BlueprintPath, Result);
	}
	else
	{
		Result->SetBoolField(TEXT("saved"), false);
	}
	Result->SetNumberField(TEXT("netUpdateFrequency"), NewFrequency);
	Result->SetNumberField(TEXT("minNetUpdateFrequency"), NewMinFrequency);
	Result->SetNumberField(TEXT("previousNetUpdateFrequency"), PrevFrequency);
	Result->SetNumberField(TEXT("previousMinNetUpdateFrequency"), PrevMinFrequency);

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

	FString Dormancy = OptionalString(Params, TEXT("dormancy"));

	TSharedPtr<FJsonValue> LoadError;
	AActor* CDO = LoadBlueprintCDO<AActor>(BlueprintPath, LoadError);
	if (!CDO) return LoadError;
	TSharedPtr<FJsonObject> Result = MCPSuccess();
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
	NetSaveOwningBlueprint(CDO, BlueprintPath, Result);

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
	const bool NewValue = OptionalBool(Params, TEXT("alwaysRelevant"), false);

	return NetApplyCdoSetting<bool>(BlueprintPath, TEXT("set_always_relevant"), TEXT("alwaysRelevant"), TEXT("previousAlwaysRelevant"), NewValue,
		[](AActor* CDO) -> bool { return CDO->bAlwaysRelevant; },
		[](AActor* CDO, bool Value) { CDO->bAlwaysRelevant = Value; },
		NetUnchangedNote);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::SetNetPriority(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;
	const double NewValue = OptionalNumber(Params, TEXT("netPriority"), 1.0);

	return NetApplyCdoSetting<double>(BlueprintPath, TEXT("set_net_priority"), TEXT("netPriority"), TEXT("previousNetPriority"), NewValue,
		[](AActor* CDO) -> double { return CDO->NetPriority; },
		[](AActor* CDO, double Value) { CDO->NetPriority = static_cast<float>(Value); },
		TEXT("The class already had this net priority, so nothing changed and there is nothing to undo."));
}

TSharedPtr<FJsonValue> FNetworkingHandlers::SetReplicateMovement(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;
	const bool NewValue = OptionalBool(Params, TEXT("replicateMovement"), false);

	return NetApplyCdoSetting<bool>(BlueprintPath, TEXT("set_replicate_movement"), TEXT("replicateMovement"), TEXT("previousReplicateMovement"), NewValue,
		[](AActor* CDO) -> bool { return CDO->IsReplicatingMovement(); },
		[](AActor* CDO, bool Value) { CDO->SetReplicatingMovement(Value); },
		NetUnchangedNote);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::SetVariableReplication(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	// propertyName is an alias the registry resolves (#1057).
	FString VariableName;
	if (auto Err = RequireString(Params, TEXT("variableName"), VariableName)) return Err;

	// replicationType names the type; replicated and repNotify are the boolean
	// shorthands (#768), read before the load can fail. An explicit type wins,
	// then repNotify=true, then replicated; none of them means None.
	FString ReplicationType = OptionalString(Params, TEXT("replicationType"));
	const bool bReplicated = OptionalBool(Params, TEXT("replicated"), false);
	const bool bRepNotify = OptionalBool(Params, TEXT("repNotify"), false);
	if (ReplicationType.IsEmpty())
	{
		if (bRepNotify) ReplicationType = TEXT("RepNotify");
		else if (bReplicated) ReplicationType = TEXT("Replicated");
		else ReplicationType = TEXT("None");
	}

	REQUIRE_ASSET(UBlueprint, Blueprint, BlueprintPath);

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

	NetSaveBlueprint(Blueprint, BlueprintPath, Result);

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
	const bool NewValue = OptionalBool(Params, TEXT("onlyRelevantToOwner"), false);

	return NetApplyCdoSetting<bool>(BlueprintPath, TEXT("set_only_relevant_to_owner"), TEXT("onlyRelevantToOwner"), TEXT("previousOnlyRelevantToOwner"), NewValue,
		[](AActor* CDO) -> bool { return CDO->bOnlyRelevantToOwner; },
		[](AActor* CDO, bool Value) { CDO->bOnlyRelevantToOwner = Value; },
		NetUnchangedNote);
}

TSharedPtr<FJsonValue> FNetworkingHandlers::SetNetLoadOnClient(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	bool bLoadOnClient = OptionalBool(Params, TEXT("loadOnClient"), true);

	TSharedPtr<FJsonValue> LoadError;
	AActor* CDO = LoadBlueprintCDO<AActor>(BlueprintPath, LoadError);
	if (!CDO) return LoadError;
	TSharedPtr<FJsonObject> Result = MCPSuccess();
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

	NetSaveOwningBlueprint(CDO, BlueprintPath, Result);

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

	double Distance = OptionalNumber(Params, TEXT("netCullDistanceSquared"), 225000000.0);

	TSharedPtr<FJsonValue> LoadError;
	AActor* CDO = LoadBlueprintCDO<AActor>(BlueprintPath, LoadError);
	if (!CDO) return LoadError;
	TSharedPtr<FJsonObject> Result = MCPSuccess();

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
		NetSaveOwningBlueprint(CDO, BlueprintPath, Result);
	}
	else
	{
		Result->SetBoolField(TEXT("saved"), false);
	}

	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetNumberField(TEXT("netCullDistanceSquared"), Distance);

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
