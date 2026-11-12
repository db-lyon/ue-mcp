// World Partition setup through UWorldPartitionSubsystem (#1828).
//
// UE 5.9 gave UWorldPartitionSubsystem the editor-scripting surface the World
// Partition editor windows drive: streaming on and off, the runtime hash class,
// runtime partitions and their HLOD setups, HLOD actor and world builds, HLOD
// stats, the minimap, and the World Partition validation and repair the Scene
// Outliner runs. Each handler here calls that function and returns what it
// returns, with the reason the subsystem reports when it refuses: the subsystem
// raises that reason through the script exception handler, which
// FMCPScopedScriptErrors captures for the length of the call.
//
// Before 5.9 the subsystem has none of these functions. Three of them have an
// older equivalent, and use it there: streaming goes through
// UWorldPartition::SetEnableStreaming, HLOD actors are built one at a time with
// AWorldPartitionHLOD::BuildHLOD, and the minimap actor comes from
// FWorldPartitionMiniMapHelper. The rest refuse with unsupported_engine_version.
//
// The loaded-region and actor-descriptor actions are elsewhere
// (LevelHandlers_WorldPartition.cpp), and the runtime hash's grid settings are
// level(get_world_partition_settings) and level(set_world_partition_settings).
//
// Translation-unit partition of FLevelHandlers; registration is
// RegisterWorldPartitionSubsystemHandlers below, called from
// LevelHandlers.cpp::RegisterHandlers.

#include "LevelHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "HandlerScriptErrors.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartitionMiniMap.h"
#include "WorldPartition/WorldPartitionMiniMapHelper.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#if UE_MCP_HAS_5_9_API
#include "WorldPartition/RuntimeHashSet/RuntimePartition.h"
#include "WorldPartition/HLOD/HLODStats.h"
#include "WorldPartition/ErrorHandling/WorldPartitionCheckMessage.h"
#endif
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
#if !UE_MCP_HAS_5_9_API
	/** The subsystem function named in a refusal before 5.9, as "UWorldPartitionSubsystem::Name". */
	TSharedPtr<FJsonValue> MCPWPSubUnsupported(const TCHAR* Function, const TCHAR* Detail = nullptr)
	{
		return MCPUnsupportedEngineError(
			FString::Printf(TEXT("UWorldPartitionSubsystem::%s"), Function), TEXT("5.9"),
			Detail ? FString(Detail) : FString());
	}
#else

	/** The editor world's World Partition subsystem, or an error to return. */
	UWorldPartitionSubsystem* MCPWPSubResolve(UWorld* World, TSharedPtr<FJsonValue>& OutError)
	{
		if (!World)
		{
			OutError = MCPError(TEXT("Editor world not available"));
			return nullptr;
		}
		UWorldPartitionSubsystem* Subsystem = World->GetSubsystem<UWorldPartitionSubsystem>();
		if (!Subsystem)
		{
			OutError = MCPError(FString::Printf(TEXT("World '%s' has no World Partition subsystem."), *World->GetName()));
			return nullptr;
		}
		return Subsystem;
	}
#endif

	/** A refusal that quotes what the subsystem reported, or Fallback when it reported nothing. */
	TSharedPtr<FJsonValue> MCPWPSubRefused(const FMCPScopedScriptErrors& Errors, const FString& Fallback)
	{
		return MCPError(Errors.Joined(Fallback));
	}

	/** Every editable property of Object, exported as text, keyed by property name. */
	TSharedPtr<FJsonObject> MCPWPSubEditableProperties(const UObject* Object)
	{
		TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
		if (!Object) return Props;
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			const FProperty* Prop = *It;
			if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit) || Prop->HasAnyPropertyFlags(CPF_Deprecated)) continue;
			Props->SetField(Prop->GetName(), MCPExportPropertyValue(Prop, Object));
		}
		return Props;
	}

#if UE_MCP_HAS_5_9_API
	TArray<TSharedPtr<FJsonValue>> MCPWPSubNames(const TArray<FName>& Names)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FName& Name : Names) Out.Add(MakeShared<FJsonValueString>(Name.ToString()));
		return Out;
	}

	TSharedPtr<FJsonObject> MCPWPSubBox(const FBox& Box)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("isValid"), Box.IsValid != 0);
		Obj->SetObjectField(TEXT("min"), MCPVec3ToJsonObject(Box.Min));
		Obj->SetObjectField(TEXT("max"), MCPVec3ToJsonObject(Box.Max));
		return Obj;
	}
#endif

	/** Describes the minimap actor: its path, label and editable settings. */
	TSharedPtr<FJsonObject> MCPWPSubDescribeMiniMap(AWorldPartitionMiniMap* MiniMap)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("actorPath"), MiniMap->GetPathName());
		Obj->SetStringField(TEXT("actorLabel"), MiniMap->GetActorLabel());
		Obj->SetObjectField(TEXT("properties"), MCPWPSubEditableProperties(MiniMap));
		return Obj;
	}

#if UE_MCP_HAS_5_9_API
	/** Loads each HLODLayer path; a path that loads nothing goes to OutMissing. */
	TArray<UHLODLayer*> MCPWPSubLoadLayers(const TArray<FString>& Paths, TArray<FString>& OutMissing)
	{
		TArray<UHLODLayer*> Layers;
		for (const FString& Path : Paths)
		{
			if (Path.IsEmpty()) continue;
			if (UHLODLayer* Layer = LoadAssetByPath<UHLODLayer>(Path)) Layers.AddUnique(Layer);
			else OutMissing.Add(Path);
		}
		return Layers;
	}

	TArray<TSharedPtr<FJsonValue>> MCPWPSubLayerPaths(const TArray<UHLODLayer*>& Layers)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const UHLODLayer* Layer : Layers)
		{
			if (Layer) Out.Add(MakeShared<FJsonValueString>(Layer->GetPathName()));
		}
		return Out;
	}

	/** The runtime partition, or the partition of one of its HLOD setups when SetupName is given. */
	URuntimePartition* MCPWPSubFindPartition(UWorldPartitionSubsystem* Subsystem, const FString& PartitionName, const FString& SetupName, bool bHasSetup)
	{
		return bHasSetup
			? Subsystem->GetHLODSetupPartition(FName(*PartitionName), FName(*SetupName))
			: Subsystem->GetRuntimePartition(FName(*PartitionName));
	}
#endif
}


// level(set_world_partition_streaming): UWorldPartitionSubsystem::SetStreamingEnabled.
TSharedPtr<FJsonValue> FLevelHandlers::SetWorldPartitionStreaming(const TSharedPtr<FJsonObject>& Params)
{
	const bool bHasEnabled = HasParam(Params, TEXT("enabled"));
	const bool bEnabled = OptionalBool(Params, TEXT("enabled"), true);
	if (!bHasEnabled) return MCPError(TEXT("Missing 'enabled': true to stream the world, false to keep every actor loaded."));
	REQUIRE_EDITOR_WORLD(World);

	UWorldPartition* WorldPartition = World->GetWorldPartition();
	if (!WorldPartition)
	{
		return MCPError(FString::Printf(TEXT("World '%s' is not partitioned, so it has no streaming to turn on or off."), *World->GetName()));
	}
	const bool bWas = WorldPartition->IsStreamingEnabled();

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("previousEnabled"), bWas);
	if (bWas == bEnabled)
	{
		Result->SetBoolField(TEXT("enabled"), bEnabled);
		Result->SetBoolField(TEXT("unchanged"), true);
		MCPSetNoRollback(Result, TEXT("Streaming was already in the requested state, so nothing changed and there is nothing to undo."));
		return MCPResult(Result);
	}

#if UE_MCP_HAS_5_9_API
	TSharedPtr<FJsonValue> Error;
	UWorldPartitionSubsystem* Subsystem = MCPWPSubResolve(World, Error);
	if (!Subsystem) return Error;
	FMCPScopedScriptErrors Errors;
	if (!Subsystem->SetStreamingEnabled(bEnabled))
	{
		return MCPWPSubRefused(Errors, TEXT("UWorldPartitionSubsystem::SetStreamingEnabled refused the change."));
	}
	Result->SetStringField(TEXT("engineFunction"), TEXT("UWorldPartitionSubsystem::SetStreamingEnabled"));
#else
	if (MCPIsPlayInEditorActive())
	{
		return MCPError(TEXT("Streaming is a setup change of the editor world, which cannot be made while a play-in-editor session is running. Stop play and retry."));
	}
	WorldPartition->SetEnableStreaming(bEnabled);
	if (World->GetOutermost()) World->GetOutermost()->MarkPackageDirty();
	Result->SetStringField(TEXT("engineFunction"), TEXT("UWorldPartition::SetEnableStreaming"));
#endif

	Result->SetBoolField(TEXT("enabled"), WorldPartition->IsStreamingEnabled());
	Result->SetBoolField(TEXT("changed"), true);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetBoolField(TEXT("enabled"), bWas);
	MCPSetRollback(Result, TEXT("set_world_partition_streaming"), Payload);
	return MCPResult(Result);
}


// level(set_runtime_hash_class): UWorldPartitionSubsystem::SetRuntimeHashClass.
TSharedPtr<FJsonValue> FLevelHandlers::SetRuntimeHashClass(const TSharedPtr<FJsonObject>& Params)
{
	FString ClassName;
	if (auto Err = RequireString(Params, TEXT("runtimeHashClass"), ClassName)) return Err;
#if UE_MCP_HAS_5_9_API
	REQUIRE_EDITOR_WORLD(World);
	UClass* HashClass = MCPResolveClassOfType(ClassName, UWorldPartitionRuntimeHash::StaticClass());
	if (!HashClass) return MCPClassNotFoundError(ClassName, TEXT("runtimeHashClass"));

	TSharedPtr<FJsonValue> Error;
	UWorldPartitionSubsystem* Subsystem = MCPWPSubResolve(World, Error);
	if (!Subsystem) return Error;

	FMCPScopedScriptErrors Errors;
	UWorldPartitionRuntimeHash* Before = Subsystem->GetRuntimeHash();
	if (!Before) return MCPWPSubRefused(Errors, FString::Printf(TEXT("World '%s' is not partitioned, so it has no runtime hash."), *World->GetName()));
	UClass* PreviousClass = Before->GetClass();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("previousClass"), PreviousClass->GetPathName());
	if (PreviousClass == HashClass)
	{
		Result->SetStringField(TEXT("runtimeHashClass"), HashClass->GetPathName());
		Result->SetBoolField(TEXT("unchanged"), true);
		MCPSetNoRollback(Result, TEXT("The runtime hash already had this class, so nothing changed and there is nothing to undo."));
		return MCPResult(Result);
	}
	if (!Subsystem->SetRuntimeHashClass(HashClass))
	{
		return MCPWPSubRefused(Errors, TEXT("UWorldPartitionSubsystem::SetRuntimeHashClass refused the conversion."));
	}
	UWorldPartitionRuntimeHash* After = Subsystem->GetRuntimeHash();
	Result->SetStringField(TEXT("runtimeHashClass"), After ? After->GetClass()->GetPathName() : FString());
	Result->SetBoolField(TEXT("changed"), true);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("runtimeHashClass"), PreviousClass->GetPathName());
	MCPSetRollback(Result, TEXT("set_runtime_hash_class"), Payload);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Converting back keeps only what the conversion supports, so settings the new class does not carry are not restored. The conversion is also on the editor's undo stack."));
	return MCPResult(Result);
#else
	return MCPWPSubUnsupported(TEXT("SetRuntimeHashClass"),
		TEXT("Older engines convert the runtime hash only from the World Settings details panel"));
#endif
}


// level(list_runtime_partitions): GetRuntimePartitionNames, with each GetRuntimePartition's class.
TSharedPtr<FJsonValue> FLevelHandlers::ListRuntimePartitions(const TSharedPtr<FJsonObject>& Params)
{
#if UE_MCP_HAS_5_9_API
	REQUIRE_EDITOR_WORLD(World);
	TSharedPtr<FJsonValue> Error;
	UWorldPartitionSubsystem* Subsystem = MCPWPSubResolve(World, Error);
	if (!Subsystem) return Error;

	FMCPScopedScriptErrors Errors;
	const TArray<FName> Names = Subsystem->GetRuntimePartitionNames();
	if (Names.Num() == 0 && Errors.HasMessages()) return MCPWPSubRefused(Errors, FString());

	TArray<TSharedPtr<FJsonValue>> Rows;
	for (const FName& Name : Names)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("name"), Name.ToString());
		if (URuntimePartition* Partition = Subsystem->GetRuntimePartition(Name))
		{
			Row->SetStringField(TEXT("class"), Partition->GetClass()->GetPathName());
		}
		Row->SetArrayField(TEXT("hlodSetups"), MCPWPSubNames(Subsystem->GetHLODSetupNames(Name)));
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}
	auto Result = MCPSuccess();
	if (UWorldPartitionRuntimeHash* Hash = Subsystem->GetRuntimeHash())
	{
		Result->SetStringField(TEXT("runtimeHashClass"), Hash->GetClass()->GetPathName());
	}
	Result->SetArrayField(TEXT("partitions"), Rows);
	Result->SetNumberField(TEXT("count"), Rows.Num());
	return MCPResult(Result);
#else
	return MCPWPSubUnsupported(TEXT("GetRuntimePartitionNames"));
#endif
}


// level(get_runtime_partition): GetRuntimePartition, or GetHLODSetupPartition with setupName.
TSharedPtr<FJsonValue> FLevelHandlers::GetRuntimePartition(const TSharedPtr<FJsonObject>& Params)
{
	FString PartitionName;
	if (auto Err = RequireString(Params, TEXT("partitionName"), PartitionName)) return Err;
	const bool bHasSetup = HasParam(Params, TEXT("setupName"));
	const FString SetupName = OptionalString(Params, TEXT("setupName"));
#if UE_MCP_HAS_5_9_API
	REQUIRE_EDITOR_WORLD(World);
	TSharedPtr<FJsonValue> Error;
	UWorldPartitionSubsystem* Subsystem = MCPWPSubResolve(World, Error);
	if (!Subsystem) return Error;

	FMCPScopedScriptErrors Errors;
	URuntimePartition* Partition = MCPWPSubFindPartition(Subsystem, PartitionName, SetupName, bHasSetup);
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("partitionName"), PartitionName);
	if (bHasSetup) Result->SetStringField(TEXT("setupName"), SetupName);
	if (!Partition)
	{
		if (Errors.HasMessages()) return MCPWPSubRefused(Errors, FString());
		if (!bHasSetup) return MCPError(FString::Printf(TEXT("No runtime partition named '%s'."), *PartitionName));
		// GetHLODSetupPartition answers None for a setup that has no partition of its own.
		Result->SetBoolField(TEXT("hasPartition"), false);
		return MCPResult(Result);
	}
	Result->SetBoolField(TEXT("hasPartition"), true);
	Result->SetStringField(TEXT("class"), Partition->GetClass()->GetPathName());
	Result->SetStringField(TEXT("objectPath"), Partition->GetPathName());
	Result->SetObjectField(TEXT("properties"), MCPWPSubEditableProperties(Partition));
	Result->SetStringField(TEXT("note"), TEXT("Write any of these properties with level(set_runtime_partition_properties)."));
	return MCPResult(Result);
#else
	return MCPWPSubUnsupported(bHasSetup ? TEXT("GetHLODSetupPartition") : TEXT("GetRuntimePartition"));
#endif
}


// level(add_runtime_partition): UWorldPartitionSubsystem::AddRuntimePartition.
TSharedPtr<FJsonValue> FLevelHandlers::AddRuntimePartition(const TSharedPtr<FJsonObject>& Params)
{
	FString PartitionName;
	if (auto Err = RequireString(Params, TEXT("partitionName"), PartitionName)) return Err;
	const FString ClassName = OptionalString(Params, TEXT("partitionClass"), TEXT("/Script/Engine.RuntimePartitionLHGrid"));
#if UE_MCP_HAS_5_9_API
	REQUIRE_EDITOR_WORLD(World);
	UClass* PartitionClass = MCPResolveClassOfType(ClassName, URuntimePartition::StaticClass());
	if (!PartitionClass) return MCPClassNotFoundError(ClassName, TEXT("partitionClass"));

	TSharedPtr<FJsonValue> Error;
	UWorldPartitionSubsystem* Subsystem = MCPWPSubResolve(World, Error);
	if (!Subsystem) return Error;

	FMCPScopedScriptErrors Errors;
	const TArray<FName> Existing = Subsystem->GetRuntimePartitionNames();
	if (Existing.Contains(FName(*PartitionName)))
	{
		auto Existed = MCPSuccess();
		MCPSetExisted(Existed);
		Existed->SetStringField(TEXT("partitionName"), PartitionName);
		if (URuntimePartition* Partition = Subsystem->GetRuntimePartition(FName(*PartitionName)))
		{
			Existed->SetStringField(TEXT("class"), Partition->GetClass()->GetPathName());
		}
		MCPSetNoRollback(Existed, TEXT("The runtime partition was already there, so this call added nothing to remove."));
		return MCPResult(Existed);
	}

	URuntimePartition* Added = Subsystem->AddRuntimePartition(FName(*PartitionName), PartitionClass);
	if (!Added) return MCPWPSubRefused(Errors, TEXT("UWorldPartitionSubsystem::AddRuntimePartition refused the partition."));

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("partitionName"), PartitionName);
	Result->SetStringField(TEXT("class"), Added->GetClass()->GetPathName());
	Result->SetArrayField(TEXT("partitions"), MCPWPSubNames(Subsystem->GetRuntimePartitionNames()));
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("partitionName"), PartitionName);
	MCPSetRollback(Result, TEXT("remove_runtime_partition"), Payload);
	return MCPResult(Result);
#else
	return MCPWPSubUnsupported(TEXT("AddRuntimePartition"));
#endif
}


// level(remove_runtime_partition): UWorldPartitionSubsystem::RemoveRuntimePartition.
TSharedPtr<FJsonValue> FLevelHandlers::RemoveRuntimePartition(const TSharedPtr<FJsonObject>& Params)
{
	FString PartitionName;
	if (auto Err = RequireString(Params, TEXT("partitionName"), PartitionName)) return Err;
#if UE_MCP_HAS_5_9_API
	REQUIRE_EDITOR_WORLD(World);
	TSharedPtr<FJsonValue> Error;
	UWorldPartitionSubsystem* Subsystem = MCPWPSubResolve(World, Error);
	if (!Subsystem) return Error;

	FMCPScopedScriptErrors Errors;
	const TArray<FName> Before = Subsystem->GetRuntimePartitionNames();
	if (Before.Num() == 0 && Errors.HasMessages()) return MCPWPSubRefused(Errors, FString());
	if (!Before.Contains(FName(*PartitionName)))
	{
		auto Absent = MCPSuccess();
		Absent->SetStringField(TEXT("partitionName"), PartitionName);
		Absent->SetBoolField(TEXT("alreadyRemoved"), true);
		Absent->SetArrayField(TEXT("partitions"), MCPWPSubNames(Before));
		MCPSetNoRollback(Absent, TEXT("No runtime partition had this name, so nothing was removed."));
		return MCPResult(Absent);
	}
	FString PreviousClass;
	if (URuntimePartition* Partition = Subsystem->GetRuntimePartition(FName(*PartitionName)))
	{
		PreviousClass = Partition->GetClass()->GetPathName();
	}
	if (!Subsystem->RemoveRuntimePartition(FName(*PartitionName)))
	{
		return MCPWPSubRefused(Errors, TEXT("UWorldPartitionSubsystem::RemoveRuntimePartition refused the removal."));
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("partitionName"), PartitionName);
	Result->SetStringField(TEXT("previousClass"), PreviousClass);
	Result->SetArrayField(TEXT("partitions"), MCPWPSubNames(Subsystem->GetRuntimePartitionNames()));
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("partitionName"), PartitionName);
	if (!PreviousClass.IsEmpty()) Payload->SetStringField(TEXT("partitionClass"), PreviousClass);
	MCPSetRollback(Result, TEXT("add_runtime_partition"), Payload);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("The rollback recreates a partition of the same name and class, so actors assigned to it stream from it again, but with default settings and without its HLOD setups. The removal itself is on the editor's undo stack."));
	return MCPResult(Result);
#else
	return MCPWPSubUnsupported(TEXT("RemoveRuntimePartition"));
#endif
}


// level(rename_runtime_partition): UWorldPartitionSubsystem::RenameRuntimePartition.
TSharedPtr<FJsonValue> FLevelHandlers::RenameRuntimePartition(const TSharedPtr<FJsonObject>& Params)
{
	FString PartitionName;
	FString NewName;
	if (auto Err = RequireString(Params, TEXT("partitionName"), PartitionName)) return Err;
	if (auto Err = RequireString(Params, TEXT("newName"), NewName)) return Err;
#if UE_MCP_HAS_5_9_API
	REQUIRE_EDITOR_WORLD(World);
	TSharedPtr<FJsonValue> Error;
	UWorldPartitionSubsystem* Subsystem = MCPWPSubResolve(World, Error);
	if (!Subsystem) return Error;

	FMCPScopedScriptErrors Errors;
	const TArray<FName> Before = Subsystem->GetRuntimePartitionNames();
	if (PartitionName == NewName || (!Before.Contains(FName(*PartitionName)) && Before.Contains(FName(*NewName))))
	{
		auto Same = MCPSuccess();
		Same->SetStringField(TEXT("partitionName"), NewName);
		Same->SetBoolField(TEXT("unchanged"), true);
		Same->SetArrayField(TEXT("partitions"), MCPWPSubNames(Before));
		MCPSetNoRollback(Same, TEXT("The partition already had this name, so nothing was renamed."));
		return MCPResult(Same);
	}
	if (!Subsystem->RenameRuntimePartition(FName(*PartitionName), FName(*NewName)))
	{
		return MCPWPSubRefused(Errors, TEXT("UWorldPartitionSubsystem::RenameRuntimePartition refused the rename."));
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("previousName"), PartitionName);
	Result->SetStringField(TEXT("partitionName"), NewName);
	Result->SetArrayField(TEXT("partitions"), MCPWPSubNames(Subsystem->GetRuntimePartitionNames()));
	Result->SetStringField(TEXT("note"), TEXT("Actors still assigned to the old name stream with the first partition until reassigned, and HLODs must be rebuilt."));
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("partitionName"), NewName);
	Payload->SetStringField(TEXT("newName"), PartitionName);
	MCPSetRollback(Result, TEXT("rename_runtime_partition"), Payload);
	return MCPResult(Result);
#else
	return MCPWPSubUnsupported(TEXT("RenameRuntimePartition"));
#endif
}


// level(set_runtime_partition_properties): writes the editor properties of the partition
// GetRuntimePartition or GetHLODSetupPartition returns, which the subsystem documents as
// the way to change a partition's settings.
TSharedPtr<FJsonValue> FLevelHandlers::SetRuntimePartitionProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString PartitionName;
	if (auto Err = RequireString(Params, TEXT("partitionName"), PartitionName)) return Err;
	const bool bHasSetup = HasParam(Params, TEXT("setupName"));
	const FString SetupName = OptionalString(Params, TEXT("setupName"));
	const TSharedPtr<FJsonObject>* PropertiesField = nullptr;
	const bool bHasProperties = TryGetObjectParam(Params, TEXT("properties"), PropertiesField);
	if (!bHasProperties || !PropertiesField || !(*PropertiesField).IsValid() || (*PropertiesField)->Values.Num() == 0)
	{
		return MCPError(TEXT("Missing 'properties': an object of property name -> value. level(get_runtime_partition) lists the names."));
	}
#if UE_MCP_HAS_5_9_API
	REQUIRE_EDITOR_WORLD(World);
	TSharedPtr<FJsonValue> Error;
	UWorldPartitionSubsystem* Subsystem = MCPWPSubResolve(World, Error);
	if (!Subsystem) return Error;

	FMCPScopedScriptErrors Errors;
	URuntimePartition* Partition = MCPWPSubFindPartition(Subsystem, PartitionName, SetupName, bHasSetup);
	if (!Partition)
	{
		return MCPWPSubRefused(Errors, bHasSetup
			? FString::Printf(TEXT("HLOD setup '%s' of runtime partition '%s' has no partition of its own to write."), *SetupName, *PartitionName)
			: FString::Printf(TEXT("No runtime partition named '%s'."), *PartitionName));
	}

	// Resolve every name before writing any of them, so a typo writes nothing.
	TArray<TPair<FProperty*, TSharedPtr<FJsonValue>>> Writes;
	for (const auto& Pair : (*PropertiesField)->Values)
	{
		const FString Key(Pair.Key);
		FProperty* Prop = MCPJsonProperty::FindMemberProperty(Partition->GetClass(), Key);
		if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit))
		{
			return MCPError(FString::Printf(
				TEXT("'%s' is not an editable property of %s. level(get_runtime_partition) lists the ones it has; nothing was written."),
				*Key, *Partition->GetClass()->GetName()));
		}
		Writes.Add(TPair<FProperty*, TSharedPtr<FJsonValue>>(Prop, Pair.Value));
	}

	FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "SetRuntimePartitionProperties", "Set Runtime Partition Properties"));
	Partition->Modify();
	TSharedPtr<FJsonObject> Previous = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> Written = MakeShared<FJsonObject>();
	for (const TPair<FProperty*, TSharedPtr<FJsonValue>>& Write : Writes)
	{
		FProperty* Prop = Write.Key;
		Previous->SetField(Prop->GetName(), MCPExportPropertyValue(Prop, Partition));
		static_cast<UObject*>(Partition)->PreEditChange(Prop);
		FString WriteError;
		if (!MCPJsonProperty::SetJsonOnProperty(Prop, Prop->ContainerPtrToValuePtr<void>(Partition), Write.Value, WriteError))
		{
			Transaction.Cancel();
			return MCPError(FString::Printf(TEXT("Could not write '%s': %s"), *Prop->GetName(), *WriteError));
		}
		FPropertyChangedEvent Event(Prop, EPropertyChangeType::ValueSet);
		static_cast<UObject*>(Partition)->PostEditChangeProperty(Event);
		Written->SetField(Prop->GetName(), MCPExportPropertyValue(Prop, Partition));
	}
	World->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("partitionName"), PartitionName);
	if (bHasSetup) Result->SetStringField(TEXT("setupName"), SetupName);
	Result->SetObjectField(TEXT("previous"), Previous);
	Result->SetObjectField(TEXT("readBack"), Written);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("partitionName"), PartitionName);
	if (bHasSetup) Payload->SetStringField(TEXT("setupName"), SetupName);
	Payload->SetObjectField(TEXT("properties"), Previous);
	MCPSetRollback(Result, TEXT("set_runtime_partition_properties"), Payload);
	return MCPResult(Result);
#else
	return MCPWPSubUnsupported(bHasSetup ? TEXT("GetHLODSetupPartition") : TEXT("GetRuntimePartition"));
#endif
}


// level(list_hlod_setups): GetHLODSetupNames, with each setup's GetHLODSetupPartition and GetHLODSetupLayers.
TSharedPtr<FJsonValue> FLevelHandlers::ListHLODSetups(const TSharedPtr<FJsonObject>& Params)
{
	FString PartitionName;
	if (auto Err = RequireString(Params, TEXT("partitionName"), PartitionName)) return Err;
#if UE_MCP_HAS_5_9_API
	REQUIRE_EDITOR_WORLD(World);
	TSharedPtr<FJsonValue> Error;
	UWorldPartitionSubsystem* Subsystem = MCPWPSubResolve(World, Error);
	if (!Subsystem) return Error;

	FMCPScopedScriptErrors Errors;
	const FName Partition(*PartitionName);
	const TArray<FName> Setups = Subsystem->GetHLODSetupNames(Partition);
	if (Errors.HasMessages()) return MCPWPSubRefused(Errors, FString());

	TArray<TSharedPtr<FJsonValue>> Rows;
	for (const FName& Setup : Setups)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("setupName"), Setup.ToString());
		Row->SetBoolField(TEXT("spatiallyLoaded"), !Setup.IsNone());
		if (URuntimePartition* SetupPartition = Subsystem->GetHLODSetupPartition(Partition, Setup))
		{
			Row->SetStringField(TEXT("partitionClass"), SetupPartition->GetClass()->GetPathName());
		}
		Row->SetArrayField(TEXT("hlodLayers"), MCPWPSubLayerPaths(Subsystem->GetHLODSetupLayers(Partition, Setup)));
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("partitionName"), PartitionName);
	Result->SetArrayField(TEXT("setups"), Rows);
	Result->SetNumberField(TEXT("count"), Rows.Num());
	return MCPResult(Result);
#else
	return MCPWPSubUnsupported(TEXT("GetHLODSetupNames"));
#endif
}


// level(add_hlod_setup): UWorldPartitionSubsystem::AddHLODSetup.
TSharedPtr<FJsonValue> FLevelHandlers::AddHLODSetup(const TSharedPtr<FJsonObject>& Params)
{
	FString PartitionName;
	FString SetupName;
	if (auto Err = RequireString(Params, TEXT("partitionName"), PartitionName)) return Err;
	if (auto Err = RequireString(Params, TEXT("setupName"), SetupName)) return Err;
#if UE_MCP_HAS_5_9_API
	REQUIRE_EDITOR_WORLD(World);
	TSharedPtr<FJsonValue> Error;
	UWorldPartitionSubsystem* Subsystem = MCPWPSubResolve(World, Error);
	if (!Subsystem) return Error;

	FMCPScopedScriptErrors Errors;
	const TArray<FName> Before = Subsystem->GetHLODSetupNames(FName(*PartitionName));
	if (Errors.HasMessages()) return MCPWPSubRefused(Errors, FString());
	if (Before.Contains(FName(*SetupName)))
	{
		auto Existed = MCPSuccess();
		MCPSetExisted(Existed);
		Existed->SetStringField(TEXT("partitionName"), PartitionName);
		Existed->SetStringField(TEXT("setupName"), SetupName);
		Existed->SetArrayField(TEXT("setups"), MCPWPSubNames(Before));
		MCPSetNoRollback(Existed, TEXT("The HLOD setup was already there, so this call added nothing to remove."));
		return MCPResult(Existed);
	}
	if (!Subsystem->AddHLODSetup(FName(*PartitionName), FName(*SetupName)))
	{
		return MCPWPSubRefused(Errors, TEXT("UWorldPartitionSubsystem::AddHLODSetup refused the setup."));
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("partitionName"), PartitionName);
	Result->SetStringField(TEXT("setupName"), SetupName);
	Result->SetArrayField(TEXT("setups"), MCPWPSubNames(Subsystem->GetHLODSetupNames(FName(*PartitionName))));
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("partitionName"), PartitionName);
	Payload->SetStringField(TEXT("setupName"), SetupName);
	MCPSetRollback(Result, TEXT("remove_hlod_setup"), Payload);
	return MCPResult(Result);
#else
	return MCPWPSubUnsupported(TEXT("AddHLODSetup"));
#endif
}


// level(remove_hlod_setup): UWorldPartitionSubsystem::RemoveHLODSetup.
TSharedPtr<FJsonValue> FLevelHandlers::RemoveHLODSetup(const TSharedPtr<FJsonObject>& Params)
{
	FString PartitionName;
	FString SetupName;
	if (auto Err = RequireString(Params, TEXT("partitionName"), PartitionName)) return Err;
	if (auto Err = RequireString(Params, TEXT("setupName"), SetupName)) return Err;
#if UE_MCP_HAS_5_9_API
	REQUIRE_EDITOR_WORLD(World);
	TSharedPtr<FJsonValue> Error;
	UWorldPartitionSubsystem* Subsystem = MCPWPSubResolve(World, Error);
	if (!Subsystem) return Error;

	FMCPScopedScriptErrors Errors;
	const TArray<FName> Before = Subsystem->GetHLODSetupNames(FName(*PartitionName));
	if (Errors.HasMessages()) return MCPWPSubRefused(Errors, FString());
	if (!Before.Contains(FName(*SetupName)))
	{
		auto Absent = MCPSuccess();
		Absent->SetStringField(TEXT("partitionName"), PartitionName);
		Absent->SetStringField(TEXT("setupName"), SetupName);
		Absent->SetBoolField(TEXT("alreadyRemoved"), true);
		Absent->SetArrayField(TEXT("setups"), MCPWPSubNames(Before));
		MCPSetNoRollback(Absent, TEXT("No HLOD setup had this name, so nothing was removed."));
		return MCPResult(Absent);
	}
	const TArray<UHLODLayer*> PreviousLayers = Subsystem->GetHLODSetupLayers(FName(*PartitionName), FName(*SetupName));
	if (!Subsystem->RemoveHLODSetup(FName(*PartitionName), FName(*SetupName)))
	{
		return MCPWPSubRefused(Errors, TEXT("UWorldPartitionSubsystem::RemoveHLODSetup refused the removal."));
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("partitionName"), PartitionName);
	Result->SetStringField(TEXT("setupName"), SetupName);
	Result->SetArrayField(TEXT("unassignedHlodLayers"), MCPWPSubLayerPaths(PreviousLayers));
	Result->SetArrayField(TEXT("setups"), MCPWPSubNames(Subsystem->GetHLODSetupNames(FName(*PartitionName))));
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("partitionName"), PartitionName);
	Payload->SetStringField(TEXT("setupName"), SetupName);
	MCPSetRollback(Result, TEXT("add_hlod_setup"), Payload);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("The rollback recreates the setup after the spatially loaded setups with a derived partition, not at its old position with its old settings, and without layers: set_hlod_setup_layers with unassignedHlodLayers puts them back. The removal itself is on the editor's undo stack."));
	return MCPResult(Result);
#else
	return MCPWPSubUnsupported(TEXT("RemoveHLODSetup"));
#endif
}


// level(set_hlod_setup_layers): UWorldPartitionSubsystem::SetHLODSetupLayers.
TSharedPtr<FJsonValue> FLevelHandlers::SetHLODSetupLayers(const TSharedPtr<FJsonObject>& Params)
{
	FString PartitionName;
	FString SetupName;
	if (auto Err = RequireString(Params, TEXT("partitionName"), PartitionName)) return Err;
	if (auto Err = RequireString(Params, TEXT("setupName"), SetupName)) return Err;
	const TArray<TSharedPtr<FJsonValue>>* LayersField = nullptr;
	if (!TryGetArrayParam(Params, TEXT("hlodLayers"), LayersField) || !LayersField)
	{
		return MCPError(TEXT("Missing 'hlodLayers': the HLODLayer asset paths to assign, or an empty array to leave the setup without layers."));
	}
	const TArray<FString> LayerPaths = JsonArrayToStringList(LayersField);
#if UE_MCP_HAS_5_9_API
	REQUIRE_EDITOR_WORLD(World);
	TArray<FString> Missing;
	const TArray<UHLODLayer*> Layers = MCPWPSubLoadLayers(LayerPaths, Missing);
	if (Missing.Num() > 0)
	{
		return MCPError(FString::Printf(TEXT("No HLODLayer asset at %s; nothing was assigned."), *FString::Join(Missing, TEXT(", "))));
	}

	TSharedPtr<FJsonValue> Error;
	UWorldPartitionSubsystem* Subsystem = MCPWPSubResolve(World, Error);
	if (!Subsystem) return Error;

	FMCPScopedScriptErrors Errors;
	const FName Partition(*PartitionName);
	const FName Setup(*SetupName);
	const TArray<UHLODLayer*> Before = Subsystem->GetHLODSetupLayers(Partition, Setup);
	if (Errors.HasMessages()) return MCPWPSubRefused(Errors, FString());

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("partitionName"), PartitionName);
	Result->SetStringField(TEXT("setupName"), SetupName);
	Result->SetArrayField(TEXT("previousHlodLayers"), MCPWPSubLayerPaths(Before));
	TSet<UHLODLayer*> BeforeSet(Before);
	TSet<UHLODLayer*> AfterSet(Layers);
	if (BeforeSet.Num() == AfterSet.Num() && BeforeSet.Includes(AfterSet))
	{
		Result->SetArrayField(TEXT("hlodLayers"), MCPWPSubLayerPaths(Before));
		Result->SetBoolField(TEXT("unchanged"), true);
		MCPSetNoRollback(Result, TEXT("The setup already held exactly these layers, so nothing changed."));
		return MCPResult(Result);
	}
	if (!Subsystem->SetHLODSetupLayers(Partition, Setup, Layers))
	{
		return MCPWPSubRefused(Errors, TEXT("UWorldPartitionSubsystem::SetHLODSetupLayers refused the assignment."));
	}
	Result->SetArrayField(TEXT("hlodLayers"), MCPWPSubLayerPaths(Subsystem->GetHLODSetupLayers(Partition, Setup)));
	Result->SetBoolField(TEXT("changed"), true);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("partitionName"), PartitionName);
	Payload->SetStringField(TEXT("setupName"), SetupName);
	Payload->SetArrayField(TEXT("hlodLayers"), MCPWPSubLayerPaths(Before));
	MCPSetRollback(Result, TEXT("set_hlod_setup_layers"), Payload);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("The rollback puts this setup's previous layers back. A layer this call moved here from another setup of the partition is not moved back to that setup."));
	return MCPResult(Result);
#else
	return MCPWPSubUnsupported(TEXT("SetHLODSetupLayers"));
#endif
}


// level(build_hlod_actors): UWorldPartitionSubsystem::BuildHLODActors; before 5.9,
// AWorldPartitionHLOD::BuildHLOD per actor, which is what that function's details-panel
// button calls.
TSharedPtr<FJsonValue> FLevelHandlers::BuildHLODActors(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* LabelsField = nullptr;
	const bool bHasLabels = TryGetArrayParam(Params, TEXT("actorLabels"), LabelsField);
	const TArray<FString> Labels = bHasLabels ? JsonArrayToStringList(LabelsField) : TArray<FString>();
	const bool bForce = OptionalBool(Params, TEXT("force"), false);
	REQUIRE_EDITOR_WORLD(World);
	if (!World->GetWorldPartition())
	{
		return MCPError(FString::Printf(TEXT("World '%s' is not partitioned, so it has no HLOD actors."), *World->GetName()));
	}

	TArray<AWorldPartitionHLOD*> Actors;
	if (bHasLabels)
	{
		for (const FString& Label : Labels)
		{
			TSharedPtr<FJsonValue> LookupError;
			FMCPActorSelector Selector;
			Selector.Match = EMCPActorMatch::LabelNameOrPath;
			AActor* Actor = MCPResolveActorToken(World, Label, LookupError, Selector);
			if (!Actor) return LookupError;
			AWorldPartitionHLOD* HLOD = Cast<AWorldPartitionHLOD>(Actor);
			if (!HLOD)
			{
				return MCPError(FString::Printf(TEXT("'%s' is a %s, not an HLOD actor; nothing was built."), *Label, *Actor->GetClass()->GetName()));
			}
			Actors.AddUnique(HLOD);
		}
	}
	else
	{
		for (TActorIterator<AWorldPartitionHLOD> It(World); It; ++It) Actors.Add(*It);
	}

	auto Result = MCPSuccess();
	TArray<TSharedPtr<FJsonValue>> Names;
	for (AWorldPartitionHLOD* Actor : Actors) Names.Add(MakeShared<FJsonValueString>(Actor->GetActorLabel()));
	Result->SetArrayField(TEXT("actors"), Names);
	Result->SetNumberField(TEXT("requested"), Actors.Num());
	if (Actors.Num() == 0)
	{
		Result->SetNumberField(TEXT("updated"), 0);
		Result->SetBoolField(TEXT("unchanged"), true);
		MCPSetNoRollback(Result, TEXT("No HLOD actor is loaded in this world, so nothing was built. Build the world's HLODs with level(build_world_partition_hlods) first."));
		return MCPResult(Result);
	}

#if UE_MCP_HAS_5_9_API
	TSharedPtr<FJsonValue> Error;
	UWorldPartitionSubsystem* Subsystem = MCPWPSubResolve(World, Error);
	if (!Subsystem) return Error;
	FMCPScopedScriptErrors Errors;
	const int32 Updated = Subsystem->BuildHLODActors(Actors, bForce);
	if (Updated < 0) return MCPWPSubRefused(Errors, TEXT("UWorldPartitionSubsystem::BuildHLODActors refused the build."));
	Result->SetNumberField(TEXT("updated"), Updated);
	Result->SetBoolField(TEXT("changed"), Updated > 0);
	Result->SetStringField(TEXT("engineFunction"), TEXT("UWorldPartitionSubsystem::BuildHLODActors"));
#else
	for (AWorldPartitionHLOD* Actor : Actors) Actor->BuildHLOD(bForce);
	// BuildHLOD reports nothing back, so every requested actor is counted as built.
	Result->SetNumberField(TEXT("built"), Actors.Num());
	Result->SetBoolField(TEXT("changed"), true);
	Result->SetStringField(TEXT("engineFunction"), TEXT("AWorldPartitionHLOD::BuildHLOD"));
#endif
	MCPSetNoRollback(Result, TEXT("An HLOD build regenerates the actors' proxy meshes in place and keeps no copy of the previous ones, so no call restores them. The actors are not saved."));
	return MCPResult(Result);
}


// level(build_world_partition_hlods): UWorldPartitionSubsystem::BuildHLODs.
TSharedPtr<FJsonValue> FLevelHandlers::BuildWorldPartitionHLODs(const TSharedPtr<FJsonObject>& Params)
{
	const FString LayerPath = OptionalString(Params, TEXT("hlodLayer"));
	const bool bForce = OptionalBool(Params, TEXT("force"), false);
#if UE_MCP_HAS_5_9_API
	REQUIRE_EDITOR_WORLD(World);
	UHLODLayer* Layer = nullptr;
	if (!LayerPath.IsEmpty())
	{
		Layer = LoadAssetByPath<UHLODLayer>(LayerPath);
		if (!Layer) return MCPAssetLoadError(LayerPath, TEXT("HLODLayer"));
	}
	const FString WorldPackage = World->GetOutermost()->GetName();

	FMCPScopedScriptErrors Errors;
	// The world is unloaded and reopened; World is not touched after this call.
	if (!UWorldPartitionSubsystem::BuildHLODs(World, Layer, bForce))
	{
		return MCPWPSubRefused(Errors, TEXT("UWorldPartitionSubsystem::BuildHLODs failed."));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("world"), WorldPackage);
	if (Layer) Result->SetStringField(TEXT("hlodLayer"), LayerPath);
	Result->SetBoolField(TEXT("force"), bForce);
	Result->SetBoolField(TEXT("changed"), true);
	Result->SetStringField(TEXT("note"), TEXT("The world was unloaded, built in an external process and reopened, so actor references taken before this call are stale."));
	MCPSetNoRollback(Result, TEXT("The HLOD build writes the world's HLOD actor packages in an external process and keeps no copy of the previous ones. level(delete_world_partition_hlods) removes them all rather than restoring the previous set."));
	return MCPResult(Result);
#else
	return MCPWPSubUnsupported(TEXT("BuildHLODs"),
		TEXT("On older engines editor(build_hlod) starts the editor's HLOD build"));
#endif
}


// level(delete_world_partition_hlods): UWorldPartitionSubsystem::DeleteHLODs.
TSharedPtr<FJsonValue> FLevelHandlers::DeleteWorldPartitionHLODs(const TSharedPtr<FJsonObject>& Params)
{
#if UE_MCP_HAS_5_9_API
	REQUIRE_EDITOR_WORLD(World);
	const FString WorldPackage = World->GetOutermost()->GetName();
	FMCPScopedScriptErrors Errors;
	if (!UWorldPartitionSubsystem::DeleteHLODs(World))
	{
		return MCPWPSubRefused(Errors, TEXT("UWorldPartitionSubsystem::DeleteHLODs failed."));
	}
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("world"), WorldPackage);
	Result->SetBoolField(TEXT("changed"), true);
	Result->SetStringField(TEXT("note"), TEXT("The world was unloaded and reopened, so actor references taken before this call are stale."));
	MCPSetNoRollback(Result, TEXT("The HLOD actor packages were deleted in an external process. level(build_world_partition_hlods) builds a new set; the deleted packages themselves come back only from source control."));
	return MCPResult(Result);
#else
	return MCPWPSubUnsupported(TEXT("DeleteHLODs"));
#endif
}


// level(get_hlod_stats): UWorldPartitionSubsystem::GetHLODStats.
TSharedPtr<FJsonValue> FLevelHandlers::GetHLODStats(const TSharedPtr<FJsonObject>& Params)
{
#if UE_MCP_HAS_5_9_API
	REQUIRE_EDITOR_WORLD(World);
	TSharedPtr<FJsonValue> Error;
	UWorldPartitionSubsystem* Subsystem = MCPWPSubResolve(World, Error);
	if (!Subsystem) return Error;

	FMCPScopedScriptErrors Errors;
	const TArray<FWorldPartitionHLODStatsEntry> Entries = Subsystem->GetHLODStats();
	if (Errors.HasMessages()) return MCPWPSubRefused(Errors, FString());

	TArray<TSharedPtr<FJsonValue>> Rows;
	for (const FWorldPartitionHLODStatsEntry& Entry : Entries)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("actorGuid"), Entry.ActorGuid.ToString(EGuidFormats::DigitsWithHyphens));
		Row->SetStringField(TEXT("name"), Entry.Name);
		Row->SetStringField(TEXT("hlodLayer"), Entry.HLODLayer);
		Row->SetBoolField(TEXT("spatiallyLoaded"), Entry.bIsSpatiallyLoaded);
		Row->SetArrayField(TEXT("dataLayers"), MCPStringListToJson(Entry.DataLayers));
		TSharedPtr<FJsonObject> Stats = MakeShared<FJsonObject>();
		for (const TPair<FName, int64>& Stat : Entry.Stats)
		{
			Stats->SetNumberField(Stat.Key.ToString(), static_cast<double>(Stat.Value));
		}
		Row->SetObjectField(TEXT("stats"), Stats);
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}
	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("hlodActors"), Rows);
	Result->SetNumberField(TEXT("count"), Rows.Num());
	Result->SetStringField(TEXT("note"), TEXT("Stats come from saved actor descriptors, so HLOD actors created since the last save are not listed yet."));
	return MCPResult(Result);
#else
	return MCPWPSubUnsupported(TEXT("GetHLODStats"));
#endif
}


// level(get_world_partition_minimap): GetMiniMap, GetMiniMapWorldBounds and GetMiniMapResolution.
TSharedPtr<FJsonValue> FLevelHandlers::GetWorldPartitionMiniMap(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	if (!World->GetWorldPartition())
	{
		return MCPError(FString::Printf(TEXT("World '%s' is not partitioned, so it has no World Partition minimap."), *World->GetName()));
	}
	auto Result = MCPSuccess();
#if UE_MCP_HAS_5_9_API
	TSharedPtr<FJsonValue> Error;
	UWorldPartitionSubsystem* Subsystem = MCPWPSubResolve(World, Error);
	if (!Subsystem) return Error;
	FMCPScopedScriptErrors Errors;
	AWorldPartitionMiniMap* MiniMap = Subsystem->GetMiniMap(/*bCreateIfMissing*/ false);
	if (!MiniMap)
	{
		if (Errors.HasMessages()) return MCPWPSubRefused(Errors, FString());
		Result->SetBoolField(TEXT("exists"), false);
		Result->SetStringField(TEXT("note"), TEXT("The world has no minimap actor. level(create_world_partition_minimap) spawns one."));
		return MCPResult(Result);
	}
	Result->SetBoolField(TEXT("exists"), true);
	Result->SetObjectField(TEXT("minimap"), MCPWPSubDescribeMiniMap(MiniMap));
	Result->SetObjectField(TEXT("worldBounds"), MCPWPSubBox(Subsystem->GetMiniMapWorldBounds()));
	{
		// GetMiniMapResolution raises when the settings leave an axis without a pixel;
		// that is a state to report, not a failure of this read.
		FMCPScopedScriptErrors ResolutionErrors;
		const FWorldPartitionMiniMapResolution Resolution = Subsystem->GetMiniMapResolution();
		if (ResolutionErrors.HasMessages())
		{
			Result->SetStringField(TEXT("resolutionError"), ResolutionErrors.Joined());
		}
		else
		{
			TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
			Res->SetNumberField(TEXT("imageSizeX"), Resolution.ImageSizeX);
			Res->SetNumberField(TEXT("imageSizeY"), Resolution.ImageSizeY);
			Res->SetNumberField(TEXT("worldUnitsPerPixel"), Resolution.WorldUnitsPerPixel);
			Result->SetObjectField(TEXT("resolution"), Res);
		}
	}
#else
	AWorldPartitionMiniMap* MiniMap = FWorldPartitionMiniMapHelper::GetWorldPartitionMiniMap(World, /*bCreateNewMiniMap*/ false);
	Result->SetBoolField(TEXT("exists"), MiniMap != nullptr);
	if (MiniMap) Result->SetObjectField(TEXT("minimap"), MCPWPSubDescribeMiniMap(MiniMap));
	Result->SetStringField(TEXT("note"), TEXT("Before UE 5.9 the capture bounds and resolution are not computed; MiniMapWorldBounds in properties holds the bounds of the last build."));
#endif
	return MCPResult(Result);
}


// level(create_world_partition_minimap): GetMiniMap(bCreateIfMissing=true).
TSharedPtr<FJsonValue> FLevelHandlers::CreateWorldPartitionMiniMap(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	if (!World->GetWorldPartition())
	{
		return MCPError(FString::Printf(TEXT("World '%s' is not partitioned, so it has no World Partition minimap."), *World->GetName()));
	}
#if UE_MCP_HAS_5_9_API
	TSharedPtr<FJsonValue> Error;
	UWorldPartitionSubsystem* Subsystem = MCPWPSubResolve(World, Error);
	if (!Subsystem) return Error;
	FMCPScopedScriptErrors Errors;
	AWorldPartitionMiniMap* Existing = Subsystem->GetMiniMap(false);
	if (Errors.HasMessages()) return MCPWPSubRefused(Errors, FString());
	AWorldPartitionMiniMap* MiniMap = Existing ? Existing : Subsystem->GetMiniMap(true);
#else
	AWorldPartitionMiniMap* Existing = FWorldPartitionMiniMapHelper::GetWorldPartitionMiniMap(World, false);
	if (!Existing && MCPIsPlayInEditorActive())
	{
		return MCPError(TEXT("The minimap actor is spawned in the editor world, which cannot be changed while a play-in-editor session is running. Stop play and retry."));
	}
	AWorldPartitionMiniMap* MiniMap = Existing ? Existing : FWorldPartitionMiniMapHelper::GetWorldPartitionMiniMap(World, true);
	FMCPScopedScriptErrors Errors;
#endif
	if (!MiniMap) return MCPWPSubRefused(Errors, TEXT("The minimap actor could not be spawned."));

	auto Result = MCPSuccess();
	Result->SetObjectField(TEXT("minimap"), MCPWPSubDescribeMiniMap(MiniMap));
	if (Existing)
	{
		MCPSetExisted(Result);
		MCPSetNoRollback(Result, TEXT("The world already had a minimap actor, so this call spawned nothing to delete."));
		return MCPResult(Result);
	}
	MCPSetCreated(Result);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorPath"), MiniMap->GetPathName());
	MCPSetRollback(Result, TEXT("delete_actor"), Payload);
	return MCPResult(Result);
}


// level(build_world_partition_minimap): UWorldPartitionSubsystem::BuildMiniMap.
TSharedPtr<FJsonValue> FLevelHandlers::BuildWorldPartitionMiniMap(const TSharedPtr<FJsonObject>& Params)
{
#if UE_MCP_HAS_5_9_API
	REQUIRE_EDITOR_WORLD(World);
	const FString WorldPackage = World->GetOutermost()->GetName();
	FMCPScopedScriptErrors Errors;
	if (!UWorldPartitionSubsystem::BuildMiniMap(World))
	{
		return MCPWPSubRefused(Errors, TEXT("UWorldPartitionSubsystem::BuildMiniMap failed."));
	}
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("world"), WorldPackage);
	Result->SetBoolField(TEXT("changed"), true);
	if (UWorld* Reopened = GetEditorWorld())
	{
		if (AWorldPartitionMiniMap* MiniMap = FWorldPartitionMiniMapHelper::GetWorldPartitionMiniMap(Reopened, false))
		{
			Result->SetObjectField(TEXT("minimap"), MCPWPSubDescribeMiniMap(MiniMap));
		}
	}
	Result->SetStringField(TEXT("note"), TEXT("The world was unloaded, the minimap built and saved in an external process, and the world reopened, so actor references taken before this call are stale."));
	MCPSetNoRollback(Result, TEXT("The build overwrites the saved minimap texture and keeps no copy of the previous one, so no call restores it."));
	return MCPResult(Result);
#else
	return MCPWPSubUnsupported(TEXT("BuildMiniMap"),
		TEXT("Older engines build the minimap from the Build menu's World Partition Editor Minimap entry"));
#endif
}


// level(check_world_partition_errors): UWorldPartitionSubsystem::CheckForErrors.
TSharedPtr<FJsonValue> FLevelHandlers::CheckWorldPartitionErrors(const TSharedPtr<FJsonObject>& Params)
{
#if UE_MCP_HAS_5_9_API
	REQUIRE_EDITOR_WORLD(World);
	TSharedPtr<FJsonValue> Error;
	UWorldPartitionSubsystem* Subsystem = MCPWPSubResolve(World, Error);
	if (!Subsystem) return Error;

	FMCPScopedScriptErrors Errors;
	const TArray<FWorldPartitionCheckMessage> Messages = Subsystem->CheckForErrors();
	if (Messages.Num() == 0 && Errors.HasMessages()) return MCPWPSubRefused(Errors, FString());

	const UEnum* TypeEnum = StaticEnum<EWorldPartitionCheckErrorType>();
	const UEnum* SeverityEnum = StaticEnum<EWorldPartitionCheckSeverity>();
	int32 ErrorCount = 0;
	int32 WarningCount = 0;
	TArray<TSharedPtr<FJsonValue>> Rows;
	for (const FWorldPartitionCheckMessage& Message : Messages)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("errorType"), TypeEnum->GetNameStringByValue(static_cast<int64>(Message.ErrorType)));
		Row->SetStringField(TEXT("severity"), SeverityEnum->GetNameStringByValue(static_cast<int64>(Message.Severity)));
		Row->SetStringField(TEXT("message"), Message.Message);
		if (Message.ActorGuid.IsValid()) Row->SetStringField(TEXT("actorGuid"), Message.ActorGuid.ToString(EGuidFormats::DigitsWithHyphens));
		if (Message.ActorPath.IsValid()) Row->SetStringField(TEXT("actorPath"), Message.ActorPath.ToString());
		if (!Message.ActorPackage.IsNone()) Row->SetStringField(TEXT("actorPackage"), Message.ActorPackage.ToString());
		if (Message.ReferencedActorGuid.IsValid()) Row->SetStringField(TEXT("referencedActorGuid"), Message.ReferencedActorGuid.ToString(EGuidFormats::DigitsWithHyphens));
		if (Message.ReferencedActorPath.IsValid()) Row->SetStringField(TEXT("referencedActorPath"), Message.ReferencedActorPath.ToString());
		if (Message.ObjectPath.IsValid()) Row->SetStringField(TEXT("objectPath"), Message.ObjectPath.ToString());
		if (Message.Severity == EWorldPartitionCheckSeverity::Error) ++ErrorCount;
		else if (Message.Severity == EWorldPartitionCheckSeverity::Warning) ++WarningCount;
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}
	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("messages"), Rows);
	Result->SetNumberField(TEXT("count"), Rows.Num());
	Result->SetNumberField(TEXT("errors"), ErrorCount);
	Result->SetNumberField(TEXT("warnings"), WarningCount);
	return MCPResult(Result);
#else
	return MCPWPSubUnsupported(TEXT("CheckForErrors"),
		TEXT("On older engines editor(run_map_check) runs the World Partition validation as part of Map Check"));
#endif
}


// level(repair_world_partition_errors): UWorldPartitionSubsystem::RepairErrors.
TSharedPtr<FJsonValue> FLevelHandlers::RepairWorldPartitionErrors(const TSharedPtr<FJsonObject>& Params)
{
#if UE_MCP_HAS_5_9_API
	REQUIRE_EDITOR_WORLD(World);
	TSharedPtr<FJsonValue> Error;
	UWorldPartitionSubsystem* Subsystem = MCPWPSubResolve(World, Error);
	if (!Subsystem) return Error;

	auto CountRepairable = [Subsystem]()
	{
		int32 Count = 0;
		for (const FWorldPartitionCheckMessage& Message : Subsystem->CheckForErrors())
		{
			if (Message.ErrorType == EWorldPartitionCheckErrorType::NotUsingExternalActors
				|| Message.ErrorType == EWorldPartitionCheckErrorType::WorldDataLayersNotSaved
				|| Message.ErrorType == EWorldPartitionCheckErrorType::InvalidActorFile)
			{
				++Count;
			}
		}
		return Count;
	};

	FMCPScopedScriptErrors Errors;
	const int32 Before = CountRepairable();
	if (!Subsystem->RepairErrors())
	{
		return MCPWPSubRefused(Errors, TEXT("UWorldPartitionSubsystem::RepairErrors refused the repair."));
	}
	const int32 After = CountRepairable();
	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("repairableBefore"), Before);
	Result->SetNumberField(TEXT("repairableAfter"), After);
	Result->SetBoolField(TEXT("changed"), Before > After);
	MCPSetNoRollback(Result, TEXT("RepairErrors saves the world and its data layers, deletes invalid actor files and moves actors into their own files; the engine documents it as not undoable and it clears the undo history when actors move."));
	return MCPResult(Result);
#else
	return MCPWPSubUnsupported(TEXT("RepairErrors"),
		TEXT("Older engines repair these from the Scene Outliner's Repair Errors button"));
#endif
}


void FLevelHandlers::RegisterWorldPartitionSubsystemHandlers(FMCPHandlerRegistry& Registry)
{
	using EType = EMCPParamType;
	const FMCPParamSpec PartitionNameSpec = MCPParam::Required(TEXT("partitionName"), EType::String, TEXT("Runtime partition name, as list_runtime_partitions reports it"));
	const FMCPParamSpec SetupNameSpec = MCPParam::Required(TEXT("setupName"), EType::String, TEXT("HLOD setup name, as list_hlod_setups reports it"));

	Registry.RegisterHandler(TEXT("set_world_partition_streaming"), &SetWorldPartitionStreaming, {
		MCPParam::Required(TEXT("enabled"), EType::Boolean, TEXT("true streams the world; false keeps every actor loaded")),
	}, MCPSpec::ContractExempt(TEXT("The contract boolean is a valid value, so on a partitioned map the call would change its streaming")));
	Registry.RegisterHandler(TEXT("set_runtime_hash_class"), &SetRuntimeHashClass, {
		MCPParam::Required(TEXT("runtimeHashClass"), EType::String, TEXT("Runtime hash class: WorldPartitionRuntimeHashSet, WorldPartitionRuntimeSpatialHash, or a /Script path")),
	});
	Registry.RegisterHandler(TEXT("list_runtime_partitions"), &ListRuntimePartitions, {});
	Registry.RegisterHandler(TEXT("get_runtime_partition"), &GetRuntimePartition, {
		PartitionNameSpec,
		MCPParam::Optional(TEXT("setupName"), EType::String, TEXT("Read this HLOD setup's partition instead of the main partition")),
	});
	Registry.RegisterHandler(TEXT("add_runtime_partition"), &AddRuntimePartition, {
		MCPParam::Required(TEXT("partitionName"), EType::String, TEXT("Name of the runtime partition to add")),
		MCPParam::Optional(TEXT("partitionClass"), EType::String, TEXT("RuntimePartition class (default /Script/Engine.RuntimePartitionLHGrid)")),
	}, MCPSpec::ContractExempt(TEXT("The contract name and the default class are valid, so on a runtime hash set map the call would add a partition")));
	Registry.RegisterHandler(TEXT("remove_runtime_partition"), &RemoveRuntimePartition, {
		PartitionNameSpec,
	});
	Registry.RegisterHandler(TEXT("rename_runtime_partition"), &RenameRuntimePartition, {
		PartitionNameSpec,
		MCPParam::Required(TEXT("newName"), EType::String, TEXT("New runtime partition name")),
	});
	Registry.RegisterHandler(TEXT("set_runtime_partition_properties"), &SetRuntimePartitionProperties, {
		PartitionNameSpec,
		MCPParam::Optional(TEXT("setupName"), EType::String, TEXT("Write this HLOD setup's partition instead of the main partition")),
		MCPParam::Required(TEXT("properties"), EType::Object, TEXT("Property name -> value, as get_runtime_partition lists them")),
	});
	Registry.RegisterHandler(TEXT("list_hlod_setups"), &ListHLODSetups, {
		PartitionNameSpec,
	});
	Registry.RegisterHandler(TEXT("add_hlod_setup"), &AddHLODSetup, {
		PartitionNameSpec,
		MCPParam::Required(TEXT("setupName"), EType::String, TEXT("Name of the spatially loaded HLOD setup to add")),
	});
	Registry.RegisterHandler(TEXT("remove_hlod_setup"), &RemoveHLODSetup, {
		PartitionNameSpec,
		SetupNameSpec,
	});
	Registry.RegisterHandler(TEXT("set_hlod_setup_layers"), &SetHLODSetupLayers, {
		PartitionNameSpec,
		SetupNameSpec,
		MCPParam::Required(TEXT("hlodLayers"), EType::Array, TEXT("HLODLayer asset paths to assign; empty leaves the setup without layers")).Items(EType::String),
	});
	// A build of many HLOD actors runs with a progress dialog on the game thread.
	Registry.RegisterHandlerWithTimeout(TEXT("build_hlod_actors"), &BuildHLODActors, 1800.0f, {
		MCPParam::Optional(TEXT("actorLabels"), EType::Array, TEXT("HLOD actors to build (default: every loaded HLOD actor)")).Items(EType::String),
		MCPParam::Optional(TEXT("force"), EType::Boolean, TEXT("Rebuild actors that are up to date (default false)")),
	}, MCPSpec::ContractExempt(TEXT("Without actorLabels the call builds every loaded HLOD actor")));
	// Both run the HLOD builder commandlet in an external process and block until it ends.
	Registry.RegisterHandlerWithTimeout(TEXT("build_world_partition_hlods"), &BuildWorldPartitionHLODs, 3600.0f, {
		MCPParam::Optional(TEXT("hlodLayer"), EType::String, TEXT("Build only the HLODs of this HLODLayer asset; omitted or null builds every layer")).Nullable(),
		MCPParam::Optional(TEXT("force"), EType::Boolean, TEXT("Rebuild HLODs that are up to date (default false)")),
	}, MCPSpec::ContractExempt(TEXT("Without hlodLayer the call builds every HLOD of the open world")));
	Registry.RegisterHandlerWithTimeout(TEXT("delete_world_partition_hlods"), &DeleteWorldPartitionHLODs, 3600.0f, {},
		MCPSpec::ContractExempt(TEXT("Takes no parameters and deletes every HLOD of the open world")));
	Registry.RegisterHandler(TEXT("get_hlod_stats"), &GetHLODStats, {});
	Registry.RegisterHandler(TEXT("get_world_partition_minimap"), &GetWorldPartitionMiniMap, {});
	Registry.RegisterHandler(TEXT("create_world_partition_minimap"), &CreateWorldPartitionMiniMap, {},
		MCPSpec::ContractExempt(TEXT("Takes no parameters and spawns the minimap actor when the world has none")));
	Registry.RegisterHandlerWithTimeout(TEXT("build_world_partition_minimap"), &BuildWorldPartitionMiniMap, 3600.0f, {},
		MCPSpec::ContractExempt(TEXT("Takes no parameters and builds the open world's minimap in an external process")));
	Registry.RegisterHandler(TEXT("check_world_partition_errors"), &CheckWorldPartitionErrors, {});
	Registry.RegisterHandler(TEXT("repair_world_partition_errors"), &RepairWorldPartitionErrors, {},
		MCPSpec::ContractExempt(TEXT("Takes no parameters and repairs, saving the world, when there is something to repair")));
}
