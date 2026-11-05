// pcg(read_generated_output): the data a PCG component produced, per output pin, or one node's output under inspection (#1309).
#include "PCGHandlers.h"
#include "PCGHandlers_Internal.h"
#include "HandlerUtils.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGData.h"
#include "PCGParamData.h"
#include "PCGSubsystem.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"
#include "Runtime/Launch/Resources/Version.h"
#include <type_traits>

// UPCGComponent::ForEachConstManagedResource and AreManagedResourcesAccessible are public from 5.7.
// Older engines fall back to the components on the owner tagged as PCG generated.
#define UE_MCP_HAS_PCG_MANAGED_RESOURCE_ITERATION (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7))
#if UE_MCP_HAS_PCG_MANAGED_RESOURCE_ITERATION
#include "PCGManagedResource.h"
#endif

#define UE_MCP_HAS_PCG_NODE_INSPECTION (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8))
#if UE_MCP_HAS_PCG_NODE_INSPECTION
#include "PCGGraphExecutionInspection.h"
#include "PCGGraphExecutionStateInterface.h"
#endif

namespace
{
	TSharedPtr<FJsonValue> MCPPCGNumbersJson(std::initializer_list<TPair<const TCHAR*, double>> Fields)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		for (const TPair<const TCHAR*, double>& F : Fields) O->SetNumberField(F.Key, F.Value);
		return MakeShared<FJsonValueObject>(O);
	}

	template <typename T>
	TSharedPtr<FJsonValue> MCPPCGValueJson(const T& V)
	{
		if constexpr (std::is_same_v<T, bool>) return MakeShared<FJsonValueBoolean>(V);
		else if constexpr (std::is_arithmetic_v<T>) return MakeShared<FJsonValueNumber>(static_cast<double>(V));
		else if constexpr (std::is_same_v<T, FVector>) return MCPPCGNumbersJson({ {TEXT("x"), V.X}, {TEXT("y"), V.Y}, {TEXT("z"), V.Z} });
		else if constexpr (std::is_same_v<T, FVector2D>) return MCPPCGNumbersJson({ {TEXT("x"), V.X}, {TEXT("y"), V.Y} });
		else if constexpr (std::is_same_v<T, FVector4>) return MCPPCGNumbersJson({ {TEXT("x"), V.X}, {TEXT("y"), V.Y}, {TEXT("z"), V.Z}, {TEXT("w"), V.W} });
		else if constexpr (std::is_same_v<T, FQuat>) return MCPPCGNumbersJson({ {TEXT("x"), V.X}, {TEXT("y"), V.Y}, {TEXT("z"), V.Z}, {TEXT("w"), V.W} });
		else if constexpr (std::is_same_v<T, FRotator>) return MCPPCGNumbersJson({ {TEXT("pitch"), V.Pitch}, {TEXT("yaw"), V.Yaw}, {TEXT("roll"), V.Roll} });
		else if constexpr (std::is_same_v<T, FTransform>)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetField(TEXT("location"), MCPPCGValueJson(V.GetLocation()));
			O->SetField(TEXT("rotation"), MCPPCGValueJson(V.Rotator()));
			O->SetField(TEXT("scale"), MCPPCGValueJson(V.GetScale3D()));
			return MakeShared<FJsonValueObject>(O);
		}
		else if constexpr (std::is_same_v<T, FString>) return MakeShared<FJsonValueString>(V);
		else if constexpr (std::is_same_v<T, FName>) return MakeShared<FJsonValueString>(V.ToString());
		else if constexpr (std::is_same_v<T, FSoftObjectPath> || std::is_same_v<T, FSoftClassPath>) return MakeShared<FJsonValueString>(V.ToString());
		else return MakeShared<FJsonValueNull>();
	}

	/** Values Offset..Offset+Count of one selector on Data, or false when the selector does not resolve on it. */
	bool MCPPCGReadSelector(const UPCGData* Data, const FString& Text, int32 Offset, int32 Count, TArray<TSharedPtr<FJsonValue>>& Out, int32& OutNum)
	{
		const FPCGAttributePropertyInputSelector Selector =
			FPCGAttributePropertySelector::CreateSelectorFromString<FPCGAttributePropertyInputSelector>(Text).CopyAndFixLast(Data);
		TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(Data, Selector, /*bQuiet*/ true);
		TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(Data, Selector);
		if (!Accessor || !Keys) return false;
		OutNum = Keys->GetNum();
		const int32 N = FMath::Clamp(OutNum - Offset, 0, Count);
		if (N == 0) return true;
		bool bRead = false;
		PCGMetadataAttribute::CallbackWithRightType(Accessor->GetUnderlyingType(), [&](auto Dummy)
		{
			using T = decltype(Dummy);
			TArray<T> Values;
			Values.SetNum(N);
			if (!Accessor->GetRange<T>(TArrayView<T>(Values), Offset, *Keys, EPCGAttributeAccessorFlags::AllowBroadcast)) return;
			for (const T& V : Values) Out.Add(MCPPCGValueJson(V));
			bRead = true;
		});
		return bRead;
	}

	/** One tagged data: pin, tags, class, entry count, and the requested attributes for a page of entries. */
	TSharedPtr<FJsonObject> MCPPCGDescribeData(const FPCGTaggedData& Tagged, const TArray<FString>& Wanted, int32 Offset, int32 Limit)
	{
		const UPCGData* Data = Tagged.Data;
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("pin"), Tagged.Pin.ToString());
		TArray<TSharedPtr<FJsonValue>> Tags;
		for (const FString& Tag : Tagged.Tags) Tags.Add(MakeShared<FJsonValueString>(Tag));
		Row->SetArrayField(TEXT("tags"), Tags);
		if (!Data)
		{
			Row->SetStringField(TEXT("dataClass"), TEXT("None"));
			return Row;
		}
		Row->SetStringField(TEXT("dataClass"), Data->GetClass()->GetName());
		const bool bParams = Data->IsA<UPCGParamData>();

		TArray<FString> Selectors = Wanted;
		if (Selectors.Num() == 0)
		{
			if (!bParams)
			{
				Selectors = { TEXT("$Position"), TEXT("$Rotation"), TEXT("$Scale"), TEXT("$BoundsMin"), TEXT("$BoundsMax"), TEXT("$Density"), TEXT("$Seed") };
			}
			if (const UPCGMetadata* Metadata = Data->ConstMetadata())
			{
				TArray<FName> Names;
				TArray<EPCGMetadataTypes> Types;
				Metadata->GetAttributes(Names, Types);
				for (const FName& Name : Names) Selectors.Add(Name.ToString());
			}
		}

		int32 Count = -1;
		TArray<FString> Missing;
		TMap<FString, TArray<TSharedPtr<FJsonValue>>> Columns;
		for (const FString& Text : Selectors)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			int32 Num = 0;
			if (!MCPPCGReadSelector(Data, Text, Offset, Limit, Values, Num))
			{
				Missing.Add(Text);
				continue;
			}
			Count = FMath::Max(Count, Num);
			Columns.Add(Text, MoveTemp(Values));
		}
		Row->SetStringField(TEXT("kind"), bParams ? TEXT("attributeSet") : (Columns.Contains(TEXT("$Position")) || Wanted.Num() > 0 ? TEXT("points") : TEXT("other")));
		Row->SetNumberField(TEXT("count"), FMath::Max(Count, 0));
		Row->SetNumberField(TEXT("offset"), Offset);
		TArray<TSharedPtr<FJsonValue>> Entries;
		const int32 Returned = FMath::Clamp(Count - Offset, 0, Limit);
		for (int32 i = 0; i < Returned; ++i)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("index"), Offset + i);
			for (const auto& Column : Columns)
			{
				if (Column.Value.IsValidIndex(i)) Entry->SetField(Column.Key, Column.Value[i]);
			}
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Row->SetArrayField(TEXT("entries"), Entries);
		if (Offset + Returned < Count) Row->SetNumberField(TEXT("nextOffset"), Offset + Returned);
		if (Missing.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> MissingJson;
			for (const FString& M : Missing) MissingJson.Add(MakeShared<FJsonValueString>(M));
			Row->SetArrayField(TEXT("unresolvedAttributes"), MissingJson);
		}
		return Row;
	}

	/**
	 * Every object reference a managed resource holds, read by reflection so the resource classes (one component,
	 * a component list, spawned actors) need no per-engine API: soft or hard, alone or in an array or set.
	 */
	void MCPPCGCollectResourceRefs(const UObject* Resource, TFunctionRef<void(UObject*)> Take)
	{
		for (TFieldIterator<FProperty> It(Resource->GetClass()); It; ++It)
		{
			const void* Addr = It->ContainerPtrToValuePtr<void>(Resource);
			if (const FObjectPropertyBase* Ref = CastField<FObjectPropertyBase>(*It))
			{
				Take(Ref->GetObjectPropertyValue(Addr));
			}
			else if (const FArrayProperty* Arr = CastField<FArrayProperty>(*It))
			{
				const FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(Arr->Inner);
				FScriptArrayHelper Helper(Arr, Addr);
				for (int32 i = 0; Inner && i < Helper.Num(); ++i) Take(Inner->GetObjectPropertyValue(Helper.GetRawPtr(i)));
			}
			else if (const FSetProperty* Set = CastField<FSetProperty>(*It))
			{
				const FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(Set->ElementProp);
				FScriptSetHelper Helper(Set, Addr);
				for (int32 i = 0; Inner && i < Helper.GetMaxIndex(); ++i)
				{
					if (Helper.IsValidIndex(i)) Take(Inner->GetObjectPropertyValue(Helper.GetElementPtr(i)));
				}
			}
		}
	}

	/**
	 * What the component spawned: its managed components (an ISM with its mesh and instance count) and actors, so a
	 * graph whose output node receives nothing still shows what it built.
	 */
	void MCPPCGDescribeSpawned(UPCGComponent* Comp, const TSharedPtr<FJsonObject>& Row)
	{
		TArray<UActorComponent*> Components;
		TArray<AActor*> Actors;
		TSet<const UObject*> Seen;
		AActor* Owner = Comp->GetOwner();
		auto Take = [&](UObject* Obj)
		{
			if (!Obj || Obj == Comp || Obj == Owner || Seen.Contains(Obj)) return;
			Seen.Add(Obj);
			if (UActorComponent* AsComponent = Cast<UActorComponent>(Obj)) Components.Add(AsComponent);
			else if (AActor* AsActor = Cast<AActor>(Obj)) Actors.Add(AsActor);
		};
		FString Source;
#if UE_MCP_HAS_PCG_MANAGED_RESOURCE_ITERATION
		if (Comp->AreManagedResourcesAccessible())
		{
			Source = TEXT("managedResources");
			Comp->ForEachConstManagedResource([&](const UPCGManagedResource* Resource)
			{
				if (Resource) MCPPCGCollectResourceRefs(Resource, Take);
			});
		}
#endif
		if (Source.IsEmpty() && Owner)
		{
			// The tag PCG puts on what it generates (PCGHelpers::DefaultPCGTag).
			Source = TEXT("generatedTag");
			for (UActorComponent* Candidate : Owner->GetComponents())
			{
				if (Candidate && Candidate->ComponentTags.Contains(FName(TEXT("PCG Generated Component")))) Take(Candidate);
			}
		}

		int64 TotalInstances = 0;
		TArray<TSharedPtr<FJsonValue>> ComponentRows;
		for (UActorComponent* Spawned : Components)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Spawned->GetName());
			Entry->SetStringField(TEXT("class"), Spawned->GetClass()->GetName());
			if (const AActor* SpawnedOwner = Spawned->GetOwner()) Entry->SetStringField(TEXT("owner"), SpawnedOwner->GetActorLabel());
			if (const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Spawned))
			{
				const int32 Count = ISM->GetInstanceCount();
				TotalInstances += Count;
				Entry->SetNumberField(TEXT("instanceCount"), Count);
				if (const UStaticMesh* Mesh = ISM->GetStaticMesh()) Entry->SetStringField(TEXT("mesh"), Mesh->GetPathName());
			}
			ComponentRows.Add(MakeShared<FJsonValueObject>(Entry));
		}
		TArray<TSharedPtr<FJsonValue>> ActorRows;
		for (const AActor* Spawned : Actors) ActorRows.Add(MakeShared<FJsonValueString>(Spawned->GetActorLabel()));
		Row->SetArrayField(TEXT("spawnedComponents"), ComponentRows);
		Row->SetNumberField(TEXT("spawnedInstanceCount"), static_cast<double>(TotalInstances));
		Row->SetArrayField(TEXT("spawnedActors"), ActorRows);
		Row->SetStringField(TEXT("spawnedSource"), Source);
	}

	TArray<TSharedPtr<FJsonValue>> MCPPCGDescribeCollection(const FPCGDataCollection& Collection, const FString& Pin,
		const TArray<FString>& Wanted, int32 Offset, int32 Limit)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FPCGTaggedData& Tagged : Collection.TaggedData)
		{
			if (!Pin.IsEmpty() && !Tagged.Pin.ToString().Equals(Pin, ESearchCase::IgnoreCase)) continue;
			Out.Add(MakeShared<FJsonValueObject>(MCPPCGDescribeData(Tagged, Wanted, Offset, Limit)));
		}
		return Out;
	}
}

TSharedPtr<FJsonValue> FPCGHandlers::ReadGeneratedOutput(const TSharedPtr<FJsonObject>& Params)
{
	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	const FString Pin = OptionalString(Params, TEXT("pin"));
	const FString NodeName = OptionalString(Params, TEXT("nodeName"));
	const FString GraphPath = OptionalString(Params, TEXT("graphPath"));
	const int32 Offset = FMath::Max(0, OptionalInt(Params, TEXT("offset"), 0));
	const int32 Limit = FMath::Clamp(OptionalInt(Params, TEXT("limit"), 100), 0, 10000);
	const bool bLocals = OptionalBool(Params, TEXT("includeLocalComponents"), true);
	TArray<FString> Wanted;
	const TArray<TSharedPtr<FJsonValue>>* AttrArr = nullptr;
	if (TryGetArrayParam(Params, TEXT("attributes"), AttrArr) && AttrArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *AttrArr)
		{
			FString A;
			if (V.IsValid() && V->TryGetString(A) && !A.IsEmpty()) Wanted.Add(A);
		}
	}

	REQUIRE_EDITOR_WORLD(World);
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	TArray<UPCGComponent*> Components;
	Actor->GetComponents<UPCGComponent>(Components);
	Components.RemoveAll([&ComponentName](const UPCGComponent* C) { return !C || (!ComponentName.IsEmpty() && C->GetName() != ComponentName); });
	if (Components.Num() == 0)
	{
		return MCPError(FString::Printf(TEXT("No PCGComponent%s on actor %s."),
			ComponentName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" named '%s'"), *ComponentName), *Actor->GetActorLabel()));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	TArray<TSharedPtr<FJsonValue>> Rows;

	auto Describe = [&](UPCGComponent* Comp, bool bLocal) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("componentName"), Comp->GetName());
		Row->SetStringField(TEXT("componentPath"), Comp->GetPathName());
		Row->SetBoolField(TEXT("local"), bLocal);
		Row->SetBoolField(TEXT("isGenerating"), Comp->IsGenerating());
		Row->SetBoolField(TEXT("partitioned"), Comp->IsPartitioned());
		if (const UPCGGraph* Graph = Comp->GetGraph()) Row->SetStringField(TEXT("graph"), Graph->GetPathName());
		return Row;
	};

	double TotalSpawnedInstances = 0;
	int32 TotalSpawnedComponents = 0;
	// Spawners hand their points to nothing, so an empty data list beside spawned content is expected, not a fault.
	auto NoteSpawnedWithoutData = [](const TSharedPtr<FJsonObject>& Row)
	{
		if (Row->GetArrayField(TEXT("data")).Num() == 0 && Row->GetArrayField(TEXT("spawnedComponents")).Num() > 0)
		{
			Row->SetStringField(TEXT("note"), TEXT("Nothing reached the graph's output node; what the graph spawned is in spawnedComponents and spawnedInstanceCount."));
		}
	};
	for (UPCGComponent* Comp : Components)
	{
		if (NodeName.IsEmpty())
		{
			TSharedPtr<FJsonObject> Row = Describe(Comp, false);
			Row->SetStringField(TEXT("source"), TEXT("generated"));
			Row->SetArrayField(TEXT("data"), MCPPCGDescribeCollection(Comp->GetGeneratedGraphOutput(), Pin, Wanted, Offset, Limit));
			MCPPCGDescribeSpawned(Comp, Row);
			NoteSpawnedWithoutData(Row);
			TotalSpawnedInstances += Row->GetNumberField(TEXT("spawnedInstanceCount"));
			TotalSpawnedComponents += Row->GetArrayField(TEXT("spawnedComponents")).Num();
			Rows.Add(MakeShared<FJsonValueObject>(Row));
			// A partitioned component's output lives on the local components the partition subsystem owns.
			UPCGSubsystem* Subsystem = (bLocals && Comp->IsPartitioned()) ? UPCGSubsystem::GetInstance(Comp->GetWorld()) : nullptr;
			if (Subsystem)
			{
				Subsystem->ForAllRegisteredLocalComponents(Comp, [&](auto* Local)
				{
					if (!Local) return;
					TSharedPtr<FJsonObject> LocalRow = Describe(Local, true);
					LocalRow->SetStringField(TEXT("source"), TEXT("generated"));
					LocalRow->SetArrayField(TEXT("data"), MCPPCGDescribeCollection(Local->GetGeneratedGraphOutput(), Pin, Wanted, Offset, Limit));
					MCPPCGDescribeSpawned(Local, LocalRow);
					NoteSpawnedWithoutData(LocalRow);
					TotalSpawnedInstances += LocalRow->GetNumberField(TEXT("spawnedInstanceCount"));
					TotalSpawnedComponents += LocalRow->GetArrayField(TEXT("spawnedComponents")).Num();
					Rows.Add(MakeShared<FJsonValueObject>(LocalRow));
				});
			}
			continue;
		}

#if UE_MCP_HAS_PCG_NODE_INSPECTION
		UPCGGraph* Graph = GraphPath.IsEmpty() ? Comp->GetGraph() : LoadAssetByPath<UPCGGraph>(GraphPath);
		if (!Graph) return GraphPath.IsEmpty() ? MCPError(TEXT("The component has no graph; pass graphPath.")) : MCPAssetLoadError(GraphPath, TEXT("PCGGraph"));
		const UPCGNode* Node = MCPPCG::FindNode(Graph, NodeName);
		if (!Node) return MCPPCG::NodeNotFoundError(Graph, NodeName);
		FPCGGraphExecutionInspection& Inspection = Comp->GetExecutionState().GetInspection();
		TSharedPtr<FJsonObject> Row = Describe(Comp, false);
		Row->SetStringField(TEXT("source"), TEXT("inspection"));
		Row->SetStringField(TEXT("nodeName"), Node->GetName());
		// The executor keeps a node's output only while the component is inspecting
		// and its graph is inspecting this component's stack, which is what the PCG
		// editor's debug object sets; the component flag alone records nothing (#1309).
		UPCGGraph* TopGraph = Comp->GetGraph();
		FPCGStack RootStack;
		RootStack.PushFrame(Comp);
		RootStack.PushFrame(TopGraph);
		const bool bGraphInspecting = TopGraph && TopGraph->IsInspecting() && TopGraph->IsSameInspectedStack(RootStack);
		if (!Inspection.IsInspecting() || !bGraphInspecting)
		{
			// Inspection records node outputs only from the next generation on.
			if (!Inspection.IsInspecting()) Inspection.EnableInspection();
			if (TopGraph && !bGraphInspecting) TopGraph->EnableInspection(RootStack);
			Row->SetBoolField(TEXT("inspectionEnabled"), true);
			Row->SetStringField(TEXT("note"), TEXT("Inspection was off, so no node output was recorded. It is on now: regenerate (execute or force_regenerate with wait=true), then call again."));
			Rows.Add(MakeShared<FJsonValueObject>(Row));
			continue;
		}
		TArray<TSharedPtr<FJsonValue>> Stacks;
		for (const FPCGGraphExecutionInspection::FNodeExecutedNotificationData& Executed : Inspection.GetExecutedNodeStacks(Node))
		{
			TSharedPtr<FJsonObject> StackRow = MakeShared<FJsonObject>();
			FString StackText;
			Executed.Stack.CreateStackFramePath(StackText);
			StackRow->SetStringField(TEXT("stack"), StackText);
			// Inspection keeps each output pin's data under the executed stack with
			// the node and then the pin pushed (#1309).
			TArray<TSharedPtr<FJsonValue>> Data;
			bool bFound = false;
			for (const UPCGPin* OutPin : Node->GetOutputPins())
			{
				if (!OutPin) continue;
				FPCGStack PinStack = Executed.Stack;
				PinStack.PushFrame(Node);
				PinStack.PushFrame(OutPin);
				bFound |= Inspection.InspectData(PinStack, [&](const FPCGDataCollection& Collection)
				{
					Data.Append(MCPPCGDescribeCollection(Collection, Pin, Wanted, Offset, Limit));
				});
			}
			StackRow->SetBoolField(TEXT("recorded"), bFound);
			StackRow->SetArrayField(TEXT("data"), Data);
			Stacks.Add(MakeShared<FJsonValueObject>(StackRow));
		}
		Row->SetArrayField(TEXT("executions"), Stacks);
		if (Stacks.Num() == 0) Row->SetStringField(TEXT("note"), TEXT("The node did not run in the last generation inspected; regenerate and call again."));
		Rows.Add(MakeShared<FJsonValueObject>(Row));
#else
		return MCPUnsupportedEngineError(TEXT("Reading one node's output (nodeName)"), TEXT("5.8"),
			TEXT("The generated output of a component is readable on this engine; omit nodeName"));
#endif
	}
	Result->SetArrayField(TEXT("components"), Rows);
	if (NodeName.IsEmpty())
	{
		Result->SetNumberField(TEXT("spawnedComponentCount"), TotalSpawnedComponents);
		Result->SetNumberField(TEXT("spawnedInstanceCount"), TotalSpawnedInstances);
	}
	return MCPResult(Result);
}
