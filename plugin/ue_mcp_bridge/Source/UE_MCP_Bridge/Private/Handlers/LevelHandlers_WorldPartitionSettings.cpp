// World Partition streaming settings and runtime cell transformers (#985).
//
// A city-scale World Partition map is tuned through three things the bridge
// could not reach at all:
//
//  1. The runtime hash's grid settings, CellSize and LoadingRange. This is the
//     primary streaming knob on any WP map and there was no native read or
//     write of it.
//  2. RuntimeCellsTransformerStack. The transformer object inside each entry is
//     an INSTANCED subobject of the world partition, which is read-only through
//     Python, so the reporter set it by hand in the editor UI. A manual fallback
//     in the editor UI is exactly what this bridge exists to remove.
//  3. HLOD layer assignment per actor, in bulk. That one lives with the other
//     selector-driven batch writes, in LevelHandlers_BatchWrite.cpp.
//
// These are actions in the `level` category rather than a new `worldpartition`
// category on purpose: a new category means a new tool, a new schema file and a
// new entry in every surface that enumerates categories, for three actions that
// are all about the level that is currently open. The level category already
// owns list_actor_descs and load_actor_descs, which are World Partition only for
// the same reason.
//
// Everything here works through REFLECTION rather than against the concrete
// runtime hash classes. Two reasons, and both are load-bearing. The grid
// settings live in different places on the two hashes Unreal ships
// (URuntimePartition under UWorldPartitionRuntimeHashSet, FSpatialHashRuntimeGrid
// under UWorldPartitionRuntimeSpatialHash), and a third could appear; and
// RuntimeCellsTransformerStack is a private UPROPERTY whose add helper is not
// guaranteed to be linkable from an external module on every engine version the
// plugin builds against. A reflected walk answers for all of them and cannot
// fail to link.
//
// Translation-unit partition of FLevelHandlers - registration lives in
// LevelHandlers.cpp::RegisterHandlers.

#include "LevelHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"

#include "Editor.h"
#include "Engine/World.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
// UWorldPartition forward-declares both of these, and the reads below convert
// them to UObject*, which needs the complete type.
#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeCellTransformer.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	/** How deep the reflected walk looks for grid settings. */
	constexpr int32 MCPWorldPartitionWalkDepth = 5;
	/** Cap on discovered grids, so a pathological graph cannot run away. */
	constexpr int32 MCPWorldPartitionMaxGrids = 64;

	/** The world partition of the open map, or a reason there is none. */
	UWorldPartition* MCPResolveWorldPartition(UWorld* World, FString& OutError)
	{
		if (!World)
		{
			OutError = TEXT("No editor world");
			return nullptr;
		}
		UWorldPartition* WorldPartition = World->GetWorldPartition();
		if (!WorldPartition)
		{
			OutError = FString::Printf(
				TEXT("'%s' is not a World Partition map, so it has no runtime hash or cell transformer stack."),
				*World->GetName());
			return nullptr;
		}
		return WorldPartition;
	}

	/** Read any numeric property as a double, whatever its width or sign. */
	bool MCPWPReadNumeric(const FProperty* Prop, const void* Container, double& OutValue)
	{
		const FNumericProperty* Numeric = CastField<FNumericProperty>(Prop);
		if (!Numeric || !Container) return false;
		const void* Addr = Prop->ContainerPtrToValuePtr<void>(Container);
		OutValue = Numeric->IsFloatingPoint()
			? Numeric->GetFloatingPointPropertyValue(Addr)
			: static_cast<double>(Numeric->GetSignedIntPropertyValue(Addr));
		return true;
	}

	/** One discovered streaming grid, addressed by a dotted path a setter can reuse. */
	struct FMCPWorldPartitionGrid
	{
		FString Path;        // dotted, rooted at the world partition object
		FString Name;        // the grid's own Name / GridName, when it has one
		FString TypeName;    // the class or struct that carries the settings
		double CellSize = 0.0;
		double LoadingRange = 0.0;
	};

	/** The grid's own name field. Both shipped hashes have one, under two names. */
	FString MCPWPReadGridName(const UStruct* Struct, const void* Container)
	{
		for (const TCHAR* Candidate : { TEXT("Name"), TEXT("GridName") })
		{
			if (const FNameProperty* NameProp =
				CastField<FNameProperty>(Struct->FindPropertyByName(FName(Candidate))))
			{
				const FName Value = NameProp->GetPropertyValue_InContainer(Container);
				if (!Value.IsNone()) return Value.ToString();
			}
		}
		return FString();
	}

	/**
	 * Walk the reflected graph under the world partition looking for anything
	 * that declares both CellSize and LoadingRange. That pair IS the streaming
	 * grid, wherever the engine version happens to keep it.
	 *
	 * Object references are followed only into objects owned by the world
	 * partition, so the walk cannot wander out into referenced assets.
	 */
	void MCPWPCollectGrids(
		const UObject* Root,
		const UStruct* Struct,
		const void* Container,
		const FString& PathPrefix,
		int32 Depth,
		TArray<FMCPWorldPartitionGrid>& OutGrids)
	{
		if (!Struct || !Container || Depth > MCPWorldPartitionWalkDepth) return;
		if (OutGrids.Num() >= MCPWorldPartitionMaxGrids) return;

		const FProperty* CellSizeProp = Struct->FindPropertyByName(TEXT("CellSize"));
		const FProperty* LoadingRangeProp = Struct->FindPropertyByName(TEXT("LoadingRange"));
		double CellSize = 0.0;
		double LoadingRange = 0.0;
		if (CellSizeProp && LoadingRangeProp &&
			MCPWPReadNumeric(CellSizeProp, Container, CellSize) &&
			MCPWPReadNumeric(LoadingRangeProp, Container, LoadingRange))
		{
			FMCPWorldPartitionGrid Grid;
			Grid.Path = PathPrefix;
			Grid.Name = MCPWPReadGridName(Struct, Container);
			Grid.TypeName = Struct->GetName();
			Grid.CellSize = CellSize;
			Grid.LoadingRange = LoadingRange;
			OutGrids.Add(Grid);
			// A grid does not contain another grid, so this branch stops here.
			return;
		}

		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			FProperty* Prop = *It;
			if (!Prop) continue;
			const FString Name = Prop->GetName();
			const FString ChildPath = PathPrefix.IsEmpty() ? Name : (PathPrefix + TEXT(".") + Name);
			const void* ValueAddr = Prop->ContainerPtrToValuePtr<void>(Container);

			if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
			{
				MCPWPCollectGrids(Root, StructProp->Struct, ValueAddr, ChildPath, Depth + 1, OutGrids);
				continue;
			}
			if (const FObjectProperty* ObjectProp = CastField<FObjectProperty>(Prop))
			{
				// A class reference names a type, not an instance to walk into.
				if (CastField<FClassProperty>(Prop)) continue;
				UObject* Sub = ObjectProp->GetObjectPropertyValue(ValueAddr);
				if (!Sub || !Sub->IsIn(Root)) continue;
				MCPWPCollectGrids(Root, Sub->GetClass(), Sub, ChildPath, Depth + 1, OutGrids);
				continue;
			}
			if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
			{
				FScriptArrayHelper Helper(ArrayProp, ValueAddr);
				for (int32 Index = 0; Index < Helper.Num(); ++Index)
				{
					const FString ElementPath = FString::Printf(TEXT("%s[%d]"), *ChildPath, Index);
					const void* ElementAddr = Helper.GetRawPtr(Index);
					if (const FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProp->Inner))
					{
						MCPWPCollectGrids(Root, InnerStruct->Struct, ElementAddr, ElementPath, Depth + 1, OutGrids);
					}
					else if (const FObjectProperty* InnerObject = CastField<FObjectProperty>(ArrayProp->Inner))
					{
						if (CastField<FClassProperty>(ArrayProp->Inner)) continue;
						UObject* Sub = InnerObject->GetObjectPropertyValue(ElementAddr);
						if (!Sub || !Sub->IsIn(Root)) continue;
						MCPWPCollectGrids(Root, Sub->GetClass(), Sub, ElementPath, Depth + 1, OutGrids);
					}
				}
			}
		}
	}

	/** The RuntimeCellsTransformerStack array property, or null on an engine without it. */
	FArrayProperty* MCPWPTransformerStackProperty()
	{
		return CastField<FArrayProperty>(
			UWorldPartition::StaticClass()->FindPropertyByName(TEXT("RuntimeCellsTransformerStack")));
	}

	/** The Class and Instance members of one FRuntimeCellTransformerInstance. */
	struct FMCPWPTransformerFields
	{
		FStructProperty* EntryProp = nullptr;
		FObjectPropertyBase* ClassProp = nullptr;
		FObjectProperty* InstanceProp = nullptr;
		bool IsValid() const { return EntryProp && ClassProp && InstanceProp; }
	};

	FMCPWPTransformerFields MCPWPTransformerFields(FArrayProperty* ArrayProp)
	{
		FMCPWPTransformerFields Fields;
		if (!ArrayProp) return Fields;
		Fields.EntryProp = CastField<FStructProperty>(ArrayProp->Inner);
		if (!Fields.EntryProp || !Fields.EntryProp->Struct) return Fields;
		Fields.ClassProp = CastField<FObjectPropertyBase>(
			Fields.EntryProp->Struct->FindPropertyByName(TEXT("Class")));
		Fields.InstanceProp = CastField<FObjectProperty>(
			Fields.EntryProp->Struct->FindPropertyByName(TEXT("Instance")));
		return Fields;
	}

	/** Every EditAnywhere property on a transformer instance, with its value. */
	TArray<TSharedPtr<FJsonValue>> MCPWPDescribeInstanceProperties(UObject* Instance)
	{
		TArray<TSharedPtr<FJsonValue>> Rows;
		if (!Instance) return Rows;
		for (TFieldIterator<FProperty> It(Instance->GetClass()); It; ++It)
		{
			FProperty* Prop = *It;
			if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit)) continue;
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("name"), Prop->GetName());
			Row->SetStringField(TEXT("type"), Prop->GetCPPType());
			Row->SetField(TEXT("value"), MCPExportPropertyValue(Prop, Instance));
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
		return Rows;
	}

	/** The transformer stack as JSON, each entry addressable by its dotted path. */
	TArray<TSharedPtr<FJsonValue>> MCPWPDescribeTransformerStack(UWorldPartition* WorldPartition)
	{
		TArray<TSharedPtr<FJsonValue>> Rows;
		FArrayProperty* ArrayProp = MCPWPTransformerStackProperty();
		const FMCPWPTransformerFields Fields = MCPWPTransformerFields(ArrayProp);
		if (!Fields.IsValid()) return Rows;

		FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(WorldPartition));
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			void* EntryAddr = Helper.GetRawPtr(Index);
			UObject* ClassObject = Fields.ClassProp->GetObjectPropertyValue(
				Fields.ClassProp->ContainerPtrToValuePtr<void>(EntryAddr));
			UObject* Instance = Fields.InstanceProp->GetObjectPropertyValue(
				Fields.InstanceProp->ContainerPtrToValuePtr<void>(EntryAddr));

			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetNumberField(TEXT("index"), Index);
			Row->SetStringField(TEXT("class"), ClassObject ? ClassObject->GetPathName() : FString());
			// The path a caller passes to set_world_partition_settings to write
			// a property on the instanced transformer object.
			Row->SetStringField(TEXT("instancePath"),
				FString::Printf(TEXT("RuntimeCellsTransformerStack[%d].Instance"), Index));
			Row->SetBoolField(TEXT("hasInstance"), Instance != nullptr);
			if (Instance)
			{
				Row->SetStringField(TEXT("instanceClass"), Instance->GetClass()->GetPathName());
				Row->SetArrayField(TEXT("properties"), MCPWPDescribeInstanceProperties(Instance));
			}
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
		return Rows;
	}

	/**
	 * Export a property from its VALUE address rather than from a container.
	 *
	 * A dotted path can end inside a struct element of an array, where the
	 * nearest UObject is not the property's container, so the usual
	 * container-based export would read the wrong address. Every setting this
	 * action writes is a scalar, which is what makes the direct export safe
	 * here: it is the one case where a fixed array would need the indexed form.
	 */
	TSharedPtr<FJsonValue> MCPWPExportAtAddress(const FProperty* Prop, const void* ValueAddr)
	{
		FString Text;
		if (Prop && ValueAddr)
		{
			Prop->ExportTextItem_Direct(Text, ValueAddr, nullptr, nullptr, PPF_None);
		}
		return MakeShared<FJsonValueString>(Text);
	}

	/** Mark the world's own package dirty after a world partition edit. */
	void MCPWPMarkDirty(UWorld* World, UWorldPartition* WorldPartition)
	{
		if (WorldPartition) WorldPartition->PostEditChange();
		if (World && World->GetOutermost()) World->GetOutermost()->MarkPackageDirty();
	}
}


// level(get_world_partition_settings): the streaming knobs on the open map.
TSharedPtr<FJsonValue> FLevelHandlers::GetWorldPartitionSettings(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	FString ResolveError;
	UWorldPartition* WorldPartition = MCPResolveWorldPartition(World, ResolveError);
	if (!WorldPartition) return MCPError(ResolveError);

	TArray<FMCPWorldPartitionGrid> Grids;
	MCPWPCollectGrids(WorldPartition, WorldPartition->GetClass(), WorldPartition, FString(), 0, Grids);

	TArray<TSharedPtr<FJsonValue>> GridRows;
	for (const FMCPWorldPartitionGrid& Grid : Grids)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("name"), Grid.Name);
		Row->SetStringField(TEXT("path"), Grid.Path);
		Row->SetStringField(TEXT("type"), Grid.TypeName);
		Row->SetNumberField(TEXT("cellSize"), Grid.CellSize);
		Row->SetNumberField(TEXT("loadingRange"), Grid.LoadingRange);
		GridRows.Add(MakeShared<FJsonValueObject>(Row));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("world"), World->GetName());
	Result->SetStringField(TEXT("worldPartition"), WorldPartition->GetPathName());
	Result->SetStringField(TEXT("worldPartitionClass"), WorldPartition->GetClass()->GetPathName());
	Result->SetBoolField(TEXT("enableStreaming"), WorldPartition->IsStreamingEnabled());
	if (UObject* RuntimeHash = WorldPartition->RuntimeHash)
	{
		Result->SetStringField(TEXT("runtimeHashClass"), RuntimeHash->GetClass()->GetPathName());
	}
	if (UObject* DefaultHLOD = WorldPartition->GetDefaultHLODLayer())
	{
		Result->SetStringField(TEXT("defaultHLODLayer"), DefaultHLOD->GetPathName());
	}
	Result->SetArrayField(TEXT("grids"), GridRows);
	Result->SetNumberField(TEXT("gridCount"), GridRows.Num());
	Result->SetArrayField(TEXT("cellTransformers"), MCPWPDescribeTransformerStack(WorldPartition));
	Result->SetStringField(TEXT("note"),
		TEXT("Each grid's 'path' and each transformer's 'instancePath' are dotted paths rooted at the world partition object; pass either to level(set_world_partition_settings) in its 'settings' map to write anything they contain."));
	return MCPResult(Result);
}


// level(set_world_partition_settings): write the streaming knobs, and anything
// else reachable from the world partition, including the properties of an
// INSTANCED runtime cell transformer, which is the object the reporter had to
// configure by hand in the editor UI.
TSharedPtr<FJsonValue> FLevelHandlers::SetWorldPartitionSettings(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	FString ResolveError;
	UWorldPartition* WorldPartition = MCPResolveWorldPartition(World, ResolveError);
	if (!WorldPartition) return MCPError(ResolveError);

	// Every write is a dotted path rooted at the world partition. cellSize and
	// loadingRange are the two that get a shorthand, because they are the
	// primary streaming knob and asking a caller to spell out
	// RuntimeHash.RuntimePartitions[0].MainLayer.CellSize for them would be a
	// riddle rather than an interface.
	TArray<TPair<FString, TSharedPtr<FJsonValue>>> Writes;

	const bool bWantsCellSize = Params->HasField(TEXT("cellSize"));
	const bool bWantsLoadingRange = Params->HasField(TEXT("loadingRange"));
	if (bWantsCellSize || bWantsLoadingRange)
	{
		TArray<FMCPWorldPartitionGrid> Grids;
		MCPWPCollectGrids(WorldPartition, WorldPartition->GetClass(), WorldPartition, FString(), 0, Grids);
		if (Grids.Num() == 0)
		{
			return MCPError(TEXT("This world partition exposes no streaming grid with a CellSize and a LoadingRange. Read level(get_world_partition_settings) to see what it does expose."));
		}

		const FString WantedGrid = OptionalString(Params, TEXT("grid"));
		const FString ExplicitPath = OptionalString(Params, TEXT("gridPath"));
		const FMCPWorldPartitionGrid* Target = nullptr;
		if (!ExplicitPath.IsEmpty())
		{
			Target = Grids.FindByPredicate([&ExplicitPath](const FMCPWorldPartitionGrid& G)
			{
				return G.Path.Equals(ExplicitPath, ESearchCase::IgnoreCase);
			});
			if (!Target)
			{
				return MCPError(FString::Printf(
					TEXT("No streaming grid at path '%s'. Read level(get_world_partition_settings) for the paths this map has."),
					*ExplicitPath));
			}
		}
		else if (!WantedGrid.IsEmpty())
		{
			Target = Grids.FindByPredicate([&WantedGrid](const FMCPWorldPartitionGrid& G)
			{
				return G.Name.Equals(WantedGrid, ESearchCase::IgnoreCase);
			});
			if (!Target)
			{
				TArray<FString> Names;
				for (const FMCPWorldPartitionGrid& G : Grids) Names.Add(G.Name.IsEmpty() ? G.Path : G.Name);
				return MCPError(FString::Printf(
					TEXT("No streaming grid named '%s'. This map has: [%s]"),
					*WantedGrid, *FString::Join(Names, TEXT(", "))));
			}
		}
		else if (Grids.Num() == 1)
		{
			Target = &Grids[0];
		}
		else
		{
			TArray<FString> Names;
			for (const FMCPWorldPartitionGrid& G : Grids) Names.Add(G.Name.IsEmpty() ? G.Path : G.Name);
			return MCPError(FString::Printf(
				TEXT("This map has %d streaming grids; pass 'grid' (name) or 'gridPath' to choose. Available: [%s]"),
				Grids.Num(), *FString::Join(Names, TEXT(", "))));
		}

		if (bWantsCellSize)
		{
			const double CellSize = OptionalNumber(Params, TEXT("cellSize"), 0.0);
			if (CellSize <= 0.0) return MCPError(TEXT("'cellSize' must be positive (world centimetres per streaming cell)"));
			Writes.Add(TPair<FString, TSharedPtr<FJsonValue>>(
				Target->Path + TEXT(".CellSize"), MakeShared<FJsonValueNumber>(CellSize)));
		}
		if (bWantsLoadingRange)
		{
			const double LoadingRange = OptionalNumber(Params, TEXT("loadingRange"), 0.0);
			if (LoadingRange <= 0.0) return MCPError(TEXT("'loadingRange' must be positive (world centimetres of streaming radius)"));
			Writes.Add(TPair<FString, TSharedPtr<FJsonValue>>(
				Target->Path + TEXT(".LoadingRange"), MakeShared<FJsonValueNumber>(LoadingRange)));
		}
	}

	const TSharedPtr<FJsonObject>* SettingsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("settings"), SettingsObj) && SettingsObj && SettingsObj->IsValid())
	{
		for (const auto& Pair : (*SettingsObj)->Values)
		{
			Writes.Add(TPair<FString, TSharedPtr<FJsonValue>>(FString(*Pair.Key), Pair.Value));
		}
	}

	if (Writes.Num() == 0)
	{
		return MCPError(TEXT("Nothing to write. Pass cellSize and/or loadingRange, or a 'settings' object of dotted path -> value rooted at the world partition (see level(get_world_partition_settings))."));
	}

	// Resolve every path before writing any of them. Streaming settings are the
	// kind of thing a caller changes in one batch, and a half-applied batch
	// leaves the map streaming on a mixture nobody asked for.
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Write : Writes)
	{
		FProperty* Prop = nullptr;
		void* ValueAddr = nullptr;
		UObject* Owner = nullptr;
		FString PathError;
		if (!MCPJsonProperty::ResolveDottedPath(WorldPartition, Write.Key, Prop, ValueAddr, Owner, PathError))
		{
			return MCPError(FString::Printf(
				TEXT("'%s' does not resolve from the world partition (%s). Nothing was written. Read level(get_world_partition_settings) for the paths this map has."),
				*Write.Key, *PathError));
		}
	}

	TArray<TSharedPtr<FJsonValue>> Applied;
	int32 FailureCount = 0;
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("MCP world partition settings")));
		WorldPartition->Modify();

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Write : Writes)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), Write.Key);

			FProperty* Prop = nullptr;
			void* ValueAddr = nullptr;
			UObject* Owner = nullptr;
			FString PathError;
			if (!MCPJsonProperty::ResolveDottedPath(WorldPartition, Write.Key, Prop, ValueAddr, Owner, PathError))
			{
				Entry->SetBoolField(TEXT("applied"), false);
				Entry->SetStringField(TEXT("error"), PathError);
				Applied.Add(MakeShared<FJsonValueObject>(Entry));
				++FailureCount;
				continue;
			}
			// The leaf owner is what actually changes, and on a transformer
			// instance that is a different object from the world partition.
			if (Owner && Owner != WorldPartition) Owner->Modify();
			Entry->SetField(TEXT("previousValue"), MCPWPExportAtAddress(Prop, ValueAddr));

			FString SetError;
			if (!MCPJsonProperty::SetJsonOnProperty(Prop, ValueAddr, Write.Value, SetError))
			{
				Entry->SetBoolField(TEXT("applied"), false);
				Entry->SetStringField(TEXT("error"), SetError);
				++FailureCount;
			}
			else
			{
				Entry->SetBoolField(TEXT("applied"), true);
				Entry->SetStringField(TEXT("owner"), Owner ? Owner->GetPathName() : FString());
				// Read back rather than echo, so the response says what the
				// object holds now.
				Entry->SetField(TEXT("value"), MCPWPExportAtAddress(Prop, ValueAddr));
				if (Owner) Owner->PostEditChange();
			}
			Applied.Add(MakeShared<FJsonValueObject>(Entry));
		}

		MCPWPMarkDirty(World, WorldPartition);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("worldPartition"), WorldPartition->GetPathName());
	Result->SetNumberField(TEXT("appliedCount"), Applied.Num() - FailureCount);
	Result->SetNumberField(TEXT("failedCount"), FailureCount);
	Result->SetArrayField(TEXT("writes"), Applied);
	if (FailureCount > 0) Result->SetBoolField(TEXT("success"), false);

	// Every write is a dotted path rooted at the world partition and every one
	// recorded the value it replaced, so the inverse is this same action with
	// those values.
	//
	// EVERY path that applied is restated, including ones whose value looks
	// unmoved. The only available comparison is export text against export
	// text, which cannot prove a value stayed put: two different values that
	// print the same read as equal. Dropping a path on that evidence would
	// leave a write that landed with no way back, which is the worse of the
	// two errors. The change test therefore drives the idempotency markers
	// only, never what goes in the payload.
	{
		TSharedPtr<FJsonObject> InverseSettings = MakeShared<FJsonObject>();
		bool bAnyValueChanged = false;
		// A grid shorthand resolves to a path like
		// RuntimeHash.StreamingGrids[1].CellSize, so the grid's POSITION IN THE
		// ARRAY is inside the key. That position is not a stable identity, and
		// the note below has to say so rather than imply the opposite.
		bool bIndexAddressed = false;
		for (const TSharedPtr<FJsonValue>& Value : Applied)
		{
			const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Entry.IsValid()) continue;
			bool bApplied = false;
			if (!Entry->TryGetBoolField(TEXT("applied"), bApplied) || !bApplied) continue;
			FString Path;
			if (!Entry->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty()) continue;
			const TSharedPtr<FJsonValue> Previous = Entry->TryGetField(TEXT("previousValue"));
			if (!Previous.IsValid()) continue;

			// previousValue and value are both export text from the same
			// property, so comparing them says whether this path moved.
			FString PreviousText, NewText;
			if (Previous->TryGetString(PreviousText) && Entry->TryGetStringField(TEXT("value"), NewText) &&
				PreviousText != NewText)
			{
				bAnyValueChanged = true;
			}
			if (Path.Contains(TEXT("["))) bIndexAddressed = true;
			InverseSettings->SetField(Path, Previous);
		}

		if (bAnyValueChanged) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("unchanged"), !bAnyValueChanged);

		// Not gated on bAnyValueChanged: see above. A write landed on every
		// path in InverseSettings.
		if (InverseSettings->Values.Num() > 0)
		{
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetObjectField(TEXT("settings"), InverseSettings);
			MCPSetRollback(Result, TEXT("set_world_partition_settings"), Payload);
			Result->SetBoolField(TEXT("rollbackIndexAddressed"), bIndexAddressed);
			if (bIndexAddressed)
			{
				Result->SetBoolField(TEXT("rollbackLossy"), true);
				Result->SetStringField(TEXT("rollbackNote"),
					TEXT("At least one restored path carries an array position, such as RuntimeHash.StreamingGrids[1].CellSize, because that is the only addressing this action has. An array position is not a stable identity: if a streaming grid is added or removed between this write and the replay, the same path resolves to a DIFFERENT grid and the inverse writes the old value onto it, reporting applied:true while the grid this call changed keeps its new value. Nothing detects that. Replay the inverse before any grid is added or removed, or check writes[].path still names the grid you meant. The inverse also restores the settings only, not the streaming data built from them: rebuild that afterwards exactly as after the forward write."));
			}
			else
			{
				Result->SetStringField(TEXT("rollbackNote"),
					TEXT("Every restored path is a fixed dotted path with no array position in it, so it resolves to the same property on replay. The inverse restores the settings, not the streaming data built from them: rebuild that afterwards exactly as after the forward write."));
			}
		}
	}
	Result->SetStringField(TEXT("note"),
		TEXT("Streaming settings only take effect for cells generated after the change, so rebuild the streaming data (or re-cook) before measuring. The map package is left dirty and is NOT saved."));
	return MCPResult(Result);
}


// level(add_runtime_cell_transformer): append a transformer to
// RuntimeCellsTransformerStack and configure its instanced object in the same
// call. Adding the entry without being able to configure the instance would
// leave the manual editor step exactly where it was.
TSharedPtr<FJsonValue> FLevelHandlers::AddRuntimeCellTransformer(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	FString ResolveError;
	UWorldPartition* WorldPartition = MCPResolveWorldPartition(World, ResolveError);
	if (!WorldPartition) return MCPError(ResolveError);

	FString ClassName;
	if (auto Err = RequireStringAlt(Params, TEXT("transformerClass"), TEXT("className"), ClassName)) return Err;

	// The base check and the concrete check both come from the shared guard, so
	// a wrong class here reads the same way it does everywhere else in the
	// bridge rather than growing its own phrasing.
	UClass* TransformerClass = MCPResolveClass(ClassName);
	if (auto Err = MCPCheckClassUsable(
		ClassName, TransformerClass, UWorldPartitionRuntimeCellTransformer::StaticClass()))
	{
		return Err;
	}

	FArrayProperty* ArrayProp = MCPWPTransformerStackProperty();
	const FMCPWPTransformerFields Fields = MCPWPTransformerFields(ArrayProp);
	if (!Fields.IsValid())
	{
		return MCPError(TEXT("This engine's UWorldPartition does not expose RuntimeCellsTransformerStack with the Class and Instance members this action writes."));
	}

	const bool bSkipIfPresent = OptionalBool(Params, TEXT("skipIfPresent"), true);
	FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(WorldPartition));

	if (bSkipIfPresent)
	{
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			void* EntryAddr = Helper.GetRawPtr(Index);
			UObject* ExistingClass = Fields.ClassProp->GetObjectPropertyValue(
				Fields.ClassProp->ContainerPtrToValuePtr<void>(EntryAddr));
			if (ExistingClass == TransformerClass)
			{
				auto Existing = MCPSuccess();
				MCPSetExisted(Existing);
				Existing->SetNumberField(TEXT("index"), Index);
				Existing->SetStringField(TEXT("class"), TransformerClass->GetPathName());
				Existing->SetStringField(TEXT("instancePath"),
					FString::Printf(TEXT("RuntimeCellsTransformerStack[%d].Instance"), Index));
				Existing->SetStringField(TEXT("note"),
					TEXT("A transformer of this class is already in the stack; nothing was added. Pass skipIfPresent=false to add a second one, or write its properties with level(set_world_partition_settings) using instancePath."));
				return MCPResult(Existing);
			}
		}
	}

	// -1 (the default) appends. Any other position inserts there, because the
	// stack is ordered and a transformer that has to run before another one has
	// no other way to say so.
	const int32 RequestedPosition = OptionalInt(Params, TEXT("position"), -1);
	if (RequestedPosition < -1 || RequestedPosition > Helper.Num())
	{
		return MCPError(FString::Printf(
			TEXT("'position' %d is out of range for a stack of %d entries (use -1 to append)"),
			RequestedPosition, Helper.Num()));
	}

	const TSharedPtr<FJsonObject>* PropertiesField = nullptr;
	TSharedPtr<FJsonObject> Properties;
	if (Params->TryGetObjectField(TEXT("properties"), PropertiesField) && PropertiesField && (*PropertiesField).IsValid())
	{
		Properties = *PropertiesField;
	}

	int32 NewIndex = INDEX_NONE;
	TArray<TSharedPtr<FJsonValue>> AppliedProperties;
	TArray<FString> PropertyErrors;
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("MCP add runtime cell transformer")));
		WorldPartition->Modify();

		if (RequestedPosition < 0)
		{
			NewIndex = Helper.AddValue();
		}
		else
		{
			Helper.InsertValues(RequestedPosition, 1);
			NewIndex = RequestedPosition;
		}

		void* EntryAddr = Helper.GetRawPtr(NewIndex);
		Fields.ClassProp->SetObjectPropertyValue(
			Fields.ClassProp->ContainerPtrToValuePtr<void>(EntryAddr), TransformerClass);

		// The instanced object is the part Python could not write, so it is
		// created here rather than left for the editor UI to fill in.
		UObject* Instance = NewObject<UObject>(
			WorldPartition, TransformerClass, NAME_None, RF_Transactional);
		Fields.InstanceProp->SetObjectPropertyValue(
			Fields.InstanceProp->ContainerPtrToValuePtr<void>(EntryAddr), Instance);

		if (Instance && Properties.IsValid())
		{
			Instance->Modify();
			for (const auto& Pair : Properties->Values)
			{
				const FString PropertyPath(*Pair.Key);
				FString SetError;
				if (MCPJsonProperty::SetDottedPropertyFromJson(Instance, PropertyPath, Pair.Value, SetError))
				{
					AppliedProperties.Add(MakeShared<FJsonValueString>(PropertyPath));
				}
				else
				{
					PropertyErrors.Add(FString::Printf(TEXT("%s: %s"), *PropertyPath, *SetError));
				}
			}
			Instance->PostEditChange();
		}

		MCPWPMarkDirty(World, WorldPartition);
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("worldPartition"), WorldPartition->GetPathName());
	Result->SetNumberField(TEXT("index"), NewIndex);
	Result->SetNumberField(TEXT("stackSize"), Helper.Num());
	Result->SetStringField(TEXT("class"), TransformerClass->GetPathName());
	Result->SetStringField(TEXT("instancePath"),
		FString::Printf(TEXT("RuntimeCellsTransformerStack[%d].Instance"), NewIndex));
	Result->SetArrayField(TEXT("appliedProperties"), AppliedProperties);
	if (PropertyErrors.Num() > 0)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("The transformer was added but %d of its properties would not apply: %s"),
			PropertyErrors.Num(), *FString::Join(PropertyErrors, TEXT("; "))));
	}
	Result->SetStringField(TEXT("note"),
		TEXT("Transformers run when streaming cells are generated, so rebuild the streaming data before measuring. The map package is left dirty and is NOT saved."));
	// No action removes ONE entry from RuntimeCellsTransformerStack. What
	// set_world_partition_settings can do is rewrite the whole array from a
	// JSON array, which SetJsonOnProperty rebuilds element by element, so it
	// could empty the stack: that is a blanket replacement of every
	// transformer, including the ones this call never touched, and it
	// reconstructs each surviving entry's instanced sub-object rather than
	// restoring the object that is there. Undoing an append with it would be a
	// wider and lossier write than the append itself, so it is not the inverse.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
		TEXT("There is no action that removes one runtime cell transformer. Rewriting the whole RuntimeCellsTransformerStack through set_world_partition_settings would replace every entry, not just the one added at index %d, and would rebuild the other transformers' instanced objects instead of leaving them alone. Remove the entry in the editor's world settings, or undo the transaction while it is still on the stack."),
		NewIndex));
	return MCPResult(Result);
}
