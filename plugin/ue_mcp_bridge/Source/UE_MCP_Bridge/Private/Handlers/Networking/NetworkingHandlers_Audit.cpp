// networking(audit_replication): one pass over every native and Blueprint class
// in scope, reporting what each one replicates, which RPCs it declares, the
// actor or component replication settings on its CDO, and the mistakes that
// are visible from the class alone.
//
// Everything here is read from reflection and from the class's own
// GetLifetimeReplicatedProps, the same list the replication system builds its
// layout from, so a condition reported here is the condition the engine uses.

#include "NetworkingHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "Misc/PackageName.h"
#include "ProjectDescriptor.h"
#include "PluginDescriptor.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/CoreNet.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace
{
	const TCHAR* NetAuditConditionName(int32 Condition)
	{
		switch (Condition)
		{
		case COND_None: return TEXT("COND_None");
		case COND_InitialOnly: return TEXT("COND_InitialOnly");
		case COND_OwnerOnly: return TEXT("COND_OwnerOnly");
		case COND_SkipOwner: return TEXT("COND_SkipOwner");
		case COND_SimulatedOnly: return TEXT("COND_SimulatedOnly");
		case COND_AutonomousOnly: return TEXT("COND_AutonomousOnly");
		case COND_SimulatedOrPhysics: return TEXT("COND_SimulatedOrPhysics");
		case COND_InitialOrOwner: return TEXT("COND_InitialOrOwner");
		case COND_Custom: return TEXT("COND_Custom");
		case COND_ReplayOrOwner: return TEXT("COND_ReplayOrOwner");
		case COND_ReplayOnly: return TEXT("COND_ReplayOnly");
		case COND_SimulatedOnlyNoReplay: return TEXT("COND_SimulatedOnlyNoReplay");
		case COND_SimulatedOrPhysicsNoReplay: return TEXT("COND_SimulatedOrPhysicsNoReplay");
		case COND_SkipReplay: return TEXT("COND_SkipReplay");
		case COND_Dynamic: return TEXT("COND_Dynamic");
		case COND_Never: return TEXT("COND_Never");
		default: return TEXT("COND_Unknown");
		}
	}

	/** The modules whose native classes count as the project's own: the
	 *  .uproject's modules and those of every enabled plugin loaded from it. */
	TSet<FString> NetAuditProjectModules()
	{
		TSet<FString> Modules;
		if (const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject())
		{
			for (const FModuleDescriptor& Module : Project->Modules)
			{
				Modules.Add(Module.Name.ToString());
			}
		}
		for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
		{
			if (Plugin->GetLoadedFrom() != EPluginLoadedFrom::Project) continue;
			for (const FModuleDescriptor& Module : Plugin->GetDescriptor().Modules)
			{
				Modules.Add(Module.Name.ToString());
			}
		}
		return Modules;
	}

	bool NetAuditSkipClass(const UClass* Class)
	{
		if (!Class) return true;
		if (Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Interface)) return true;
		const FString Name = Class->GetName();
		return Name.StartsWith(TEXT("SKEL_")) || Name.StartsWith(TEXT("REINST_")) || Name.StartsWith(TEXT("TRASHCLASS_"))
			|| Name.StartsWith(TEXT("HOTRELOADED_"));
	}

	double NetAuditReadFloat(const UObject* Object, const TCHAR* PropertyName, bool& bOutFound)
	{
		bOutFound = false;
		if (!Object) return 0.0;
		const FNumericProperty* Prop = CastField<FNumericProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!Prop) return 0.0;
		bOutFound = true;
		const void* Value = Prop->ContainerPtrToValuePtr<void>(Object);
		return Prop->IsFloatingPoint() ? Prop->GetFloatingPointPropertyValue(Value) : static_cast<double>(Prop->GetSignedIntPropertyValue(Value));
	}

	void NetAuditAddFinding(TArray<TSharedPtr<FJsonValue>>& Findings, TMap<FString, int32>& Totals,
		const TCHAR* Kind, const FString& Member, const FString& Message)
	{
		TSharedPtr<FJsonObject> Finding = MakeShared<FJsonObject>();
		Finding->SetStringField(TEXT("kind"), Kind);
		if (!Member.IsEmpty()) Finding->SetStringField(TEXT("member"), Member);
		Finding->SetStringField(TEXT("message"), Message);
		Findings.Add(MakeShared<FJsonValueObject>(Finding));
		++Totals.FindOrAdd(Kind);
	}

	struct FNetAuditRow
	{
		TSharedPtr<FJsonObject> Json;
		int32 FindingCount = 0;
		FString SortName;
	};

	/** Audit one class. Returns false when it declares nothing replication-related
	 *  and replicates nothing, so it does not take a row. */
	bool NetAuditClass(UClass* Class, const FString& Kind, const FString& AssetPath, bool bRequireServerValidation,
		TMap<FString, int32>& Totals, FNetAuditRow& OutRow)
	{
		UObject* CDO = Class->GetDefaultObject(true);
		if (!CDO) return false;

		// Lifetime properties carry the condition. RepIndex points into ClassReps,
		// which the engine fills in on first replication; fill it now if needed.
		if (!Class->HasAnyClassFlags(CLASS_ReplicationDataIsSetUp))
		{
			Class->SetUpRuntimeReplicationData();
		}
		TArray<FLifetimeProperty> Lifetime;
		CDO->GetLifetimeReplicatedProps(Lifetime);
		TMap<const FProperty*, const FLifetimeProperty*> LifetimeByProperty;
		for (const FLifetimeProperty& Entry : Lifetime)
		{
			if (Class->ClassReps.IsValidIndex(Entry.RepIndex))
			{
				LifetimeByProperty.Add(Class->ClassReps[Entry.RepIndex].Property, &Entry);
			}
		}

		const bool bIsActor = Class->IsChildOf(AActor::StaticClass());
		const bool bIsComponent = Class->IsChildOf(UActorComponent::StaticClass());
		bool bReplicates = false;
		if (bIsActor) bReplicates = CastChecked<AActor>(CDO)->GetIsReplicated();
		else if (bIsComponent) bReplicates = CastChecked<UActorComponent>(CDO)->GetIsReplicated();

		TArray<TSharedPtr<FJsonValue>> Findings;

		// Every RepNotify name the class and its supers expect, to tell an
		// orphaned OnRep_ function from a wired one.
		TSet<FName> RepNotifyNames;
		for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Net) && It->RepNotifyFunc != NAME_None)
			{
				RepNotifyNames.Add(It->RepNotifyFunc);
			}
		}

		TArray<TSharedPtr<FJsonValue>> Properties;
		for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			FProperty* Prop = *It;
			if (!Prop->HasAnyPropertyFlags(CPF_Net)) continue;
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("name"), Prop->GetName());
			Row->SetStringField(TEXT("type"), Prop->GetCPPType());
			const FLifetimeProperty* const* Entry = LifetimeByProperty.Find(Prop);
			Row->SetBoolField(TEXT("registered"), Entry != nullptr);
			if (Entry)
			{
				Row->SetStringField(TEXT("condition"), NetAuditConditionName(static_cast<int32>((*Entry)->Condition)));
				Row->SetBoolField(TEXT("pushBased"), (*Entry)->bIsPushBased);
			}
			if (Prop->RepNotifyFunc != NAME_None)
			{
				const FString Notify = Prop->RepNotifyFunc.ToString();
				Row->SetStringField(TEXT("repNotify"), Notify);
				if (Entry)
				{
					Row->SetStringField(TEXT("repNotifyCondition"),
						(*Entry)->RepNotifyCondition == REPNOTIFY_Always ? TEXT("REPNOTIFY_Always") : TEXT("REPNOTIFY_OnChanged"));
				}
				if (!Class->FindFunctionByName(Prop->RepNotifyFunc))
				{
					NetAuditAddFinding(Findings, Totals, TEXT("repNotifyFunctionMissing"), Prop->GetName(), FString::Printf(
						TEXT("'%s' names RepNotify function '%s', which the class does not have, so clients never get the notify."),
						*Prop->GetName(), *Notify));
				}
			}
			if (!Entry)
			{
				NetAuditAddFinding(Findings, Totals, TEXT("replicatedPropertyNotRegistered"), Prop->GetName(), FString::Printf(
					TEXT("'%s' is marked Replicated but GetLifetimeReplicatedProps does not list it, so it never replicates."),
					*Prop->GetName()));
			}
			Properties.Add(MakeShared<FJsonValueObject>(Row));
		}

		if ((bIsActor || bIsComponent) && !bReplicates && Properties.Num() > 0)
		{
			TArray<FString> Names;
			for (const TSharedPtr<FJsonValue>& P : Properties) Names.Add(P->AsObject()->GetStringField(TEXT("name")));
			NetAuditAddFinding(Findings, Totals, TEXT("replicatedPropertyOnNonReplicatingClass"), FString::Join(Names, TEXT(", ")), FString::Printf(
				TEXT("The %s does not replicate (bReplicates is false on its defaults), so its %d replicated propert%s never reach a client unless an instance turns replication on."),
				bIsActor ? TEXT("actor") : TEXT("component"), Names.Num(), Names.Num() == 1 ? TEXT("y") : TEXT("ies")));
		}

		TArray<TSharedPtr<FJsonValue>> Rpcs;
		int32 OnRepCount = 0;
		for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			UFunction* Func = *It;
			const FString FuncName = Func->GetName();
			if (FuncName.StartsWith(TEXT("OnRep_")))
			{
				++OnRepCount;
				if (!RepNotifyNames.Contains(Func->GetFName()))
				{
					NetAuditAddFinding(Findings, Totals, TEXT("onRepWithoutReplicatedProperty"), FuncName, FString::Printf(
						TEXT("'%s' looks like a RepNotify, but no replicated property on this class or its parents names it."),
						*FuncName));
				}
			}
			if (!Func->HasAnyFunctionFlags(FUNC_Net)) continue;
			const TCHAR* Type = Func->HasAnyFunctionFlags(FUNC_NetServer) ? TEXT("Server")
				: Func->HasAnyFunctionFlags(FUNC_NetClient) ? TEXT("Client")
				: Func->HasAnyFunctionFlags(FUNC_NetMulticast) ? TEXT("NetMulticast") : TEXT("Net");
			const bool bReliable = Func->HasAnyFunctionFlags(FUNC_NetReliable);
			const bool bValidated = Func->HasAnyFunctionFlags(FUNC_NetValidate);
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("name"), FuncName);
			Row->SetStringField(TEXT("type"), Type);
			Row->SetBoolField(TEXT("reliable"), bReliable);
			Row->SetBoolField(TEXT("validated"), bValidated);
			Rpcs.Add(MakeShared<FJsonValueObject>(Row));

			if (bReliable && Func->HasAnyFunctionFlags(FUNC_NetMulticast))
			{
				NetAuditAddFinding(Findings, Totals, TEXT("reliableMulticast"), FuncName, FString::Printf(
					TEXT("'%s' is a reliable NetMulticast RPC; every call queues a reliable bunch for every relevant connection, which can saturate the reliable buffer if it is called often."),
					*FuncName));
			}
			if (bRequireServerValidation && Func->HasAnyFunctionFlags(FUNC_NetServer) && !bValidated)
			{
				NetAuditAddFinding(Findings, Totals, TEXT("serverRpcWithoutValidation"), FuncName, FString::Printf(
					TEXT("'%s' is a Server RPC without WithValidation, and requireServerValidation asked for one on every Server RPC."),
					*FuncName));
			}
		}

		const bool bHasContent = Properties.Num() > 0 || Rpcs.Num() > 0 || OnRepCount > 0;
		if (!bHasContent && !(bReplicates && Kind == TEXT("blueprint")))
		{
			return false;
		}

		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("class"), Class->GetName());
		Json->SetStringField(TEXT("classPath"), Class->GetPathName());
		Json->SetStringField(TEXT("kind"), Kind);
		if (!AssetPath.IsEmpty()) Json->SetStringField(TEXT("assetPath"), AssetPath);
		if (UClass* Super = Class->GetSuperClass()) Json->SetStringField(TEXT("parentClass"), Super->GetPathName());
		Json->SetStringField(TEXT("role"), bIsActor ? TEXT("actor") : bIsComponent ? TEXT("component") : TEXT("object"));

		if (bIsActor || bIsComponent)
		{
			TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
			Settings->SetBoolField(TEXT("replicates"), bReplicates);
			if (bIsActor)
			{
				AActor* Actor = CastChecked<AActor>(CDO);
				const UEnum* DormancyEnum = StaticEnum<ENetDormancy>();
				Settings->SetStringField(TEXT("netDormancy"), DormancyEnum
					? DormancyEnum->GetNameStringByValue(static_cast<int64>(Actor->NetDormancy))
					: FString::FromInt(static_cast<int32>(Actor->NetDormancy)));
#if UE_MCP_HAS_5_5_API
				Settings->SetNumberField(TEXT("netUpdateFrequency"), Actor->GetNetUpdateFrequency());
				Settings->SetNumberField(TEXT("minNetUpdateFrequency"), Actor->GetMinNetUpdateFrequency());
#else
				Settings->SetNumberField(TEXT("netUpdateFrequency"), Actor->NetUpdateFrequency);
				Settings->SetNumberField(TEXT("minNetUpdateFrequency"), Actor->MinNetUpdateFrequency);
#endif
				bool bFound = false;
				const double Cull = NetAuditReadFloat(Actor, TEXT("NetCullDistanceSquared"), bFound);
				if (bFound) Settings->SetNumberField(TEXT("netCullDistanceSquared"), Cull);
				Settings->SetNumberField(TEXT("netPriority"), Actor->NetPriority);
				Settings->SetBoolField(TEXT("alwaysRelevant"), Actor->bAlwaysRelevant);
				Settings->SetBoolField(TEXT("onlyRelevantToOwner"), Actor->bOnlyRelevantToOwner);
				Settings->SetBoolField(TEXT("replicateMovement"), Actor->IsReplicatingMovement());
			}
			Json->SetObjectField(TEXT("replication"), Settings);
		}

		Json->SetArrayField(TEXT("properties"), Properties);
		Json->SetArrayField(TEXT("rpcs"), Rpcs);
		Json->SetArrayField(TEXT("findings"), Findings);
		OutRow.Json = Json;
		OutRow.FindingCount = Findings.Num();
		OutRow.SortName = Class->GetPathName();
		return true;
	}
}

TSharedPtr<FJsonValue> FNetworkingHandlers::AuditReplication(const TSharedPtr<FJsonObject>& Params)
{
	// Every parameter is read before anything is validated or loaded (#1057).
	const FString Path = OptionalString(Params, TEXT("path"), TEXT(""));
	const FString ClassName = OptionalString(Params, TEXT("className"), TEXT(""));
	const bool bIncludeNative = OptionalBool(Params, TEXT("includeNative"), true);
	const bool bIncludeBlueprints = OptionalBool(Params, TEXT("includeBlueprints"), true);
	const bool bIncludeEngine = OptionalBool(Params, TEXT("includeEngine"), false);
	const bool bRequireServerValidation = OptionalBool(Params, TEXT("requireServerValidation"), false);
	const bool bOnlyFindings = OptionalBool(Params, TEXT("onlyWithFindings"), false);
	const int32 MaxBlueprints = OptionalInt(Params, TEXT("maxBlueprints"), 500);
	const int32 Limit = OptionalInt(Params, TEXT("limit"), 200);
	const bool bDumpToFile = OptionalBool(Params, TEXT("dumpToFile"), false);
	const FString OutputPath = OptionalString(Params, TEXT("outputPath"), TEXT(""));

	if (Limit < 1 || Limit > 5000)
	{
		return MCPError(FString::Printf(TEXT("limit must be between 1 and 5000, got %d."), Limit));
	}
	if (MaxBlueprints < 1 || MaxBlueprints > 5000)
	{
		return MCPError(FString::Printf(TEXT("maxBlueprints must be between 1 and 5000, got %d."), MaxBlueprints));
	}
	if (!bIncludeNative && !bIncludeBlueprints)
	{
		return MCPError(TEXT("includeNative and includeBlueprints are both false, so there is nothing to audit."));
	}

	UClass* BaseClass = nullptr;
	if (!ClassName.IsEmpty())
	{
		BaseClass = MCPResolveClass(ClassName);
		if (!BaseClass)
		{
			if (UBlueprint* BaseBlueprint = LoadAssetByPath<UBlueprint>(ClassName))
			{
				BaseClass = BaseBlueprint->GeneratedClass;
			}
		}
		if (!BaseClass)
		{
			return MCPClassNotFoundError(ClassName);
		}
	}

	const bool bScriptPath = Path.StartsWith(TEXT("/Script/"));
	const FString BlueprintRoot = Path.IsEmpty() ? FString(TEXT("/Game")) : Path;

	TMap<FString, int32> Totals;
	TArray<FNetAuditRow> Rows;
	int32 NativeScanned = 0;
	int32 BlueprintsScanned = 0;
	bool bBlueprintsTruncated = false;
	TArray<TSharedPtr<FJsonValue>> FailedToLoad;

	if (bIncludeNative)
	{
		const TSet<FString> ProjectModules = bIncludeEngine ? TSet<FString>() : NetAuditProjectModules();
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (NetAuditSkipClass(Class) || !Class->HasAnyClassFlags(CLASS_Native)) continue;
			const FString PackageName = Class->GetOutermost()->GetName();
			if (!PackageName.StartsWith(TEXT("/Script/"))) continue;
			if (!Path.IsEmpty() && !PackageName.StartsWith(Path)) continue;
			if (!bIncludeEngine && !ProjectModules.Contains(PackageName.RightChop(8))) continue;
			if (BaseClass && !Class->IsChildOf(BaseClass)) continue;
			++NativeScanned;
			FNetAuditRow Row;
			if (NetAuditClass(Class, TEXT("native"), FString(), bRequireServerValidation, Totals, Row))
			{
				Rows.Add(MoveTemp(Row));
			}
		}
	}

	if (bIncludeBlueprints && !bScriptPath)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		FARFilter Filter;
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		Filter.PackagePaths.Add(FName(*BlueprintRoot));
		Filter.bRecursivePaths = true;
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);
		Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });
		if (Assets.Num() > MaxBlueprints)
		{
			bBlueprintsTruncated = true;
			Assets.SetNum(MaxBlueprints);
		}
		for (const FAssetData& Asset : Assets)
		{
			const FString AssetPath = Asset.GetObjectPathString();
			UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
			UClass* Class = Blueprint ? Blueprint->GeneratedClass.Get() : nullptr;
			if (!Class)
			{
				FailedToLoad.Add(MakeShared<FJsonValueString>(AssetPath));
				continue;
			}
			if (NetAuditSkipClass(Class)) continue;
			if (BaseClass && !Class->IsChildOf(BaseClass)) continue;
			++BlueprintsScanned;
			FNetAuditRow Row;
			if (NetAuditClass(Class, TEXT("blueprint"), Asset.PackageName.ToString(), bRequireServerValidation, Totals, Row))
			{
				Rows.Add(MoveTemp(Row));
			}
		}
	}

	if (bOnlyFindings)
	{
		Rows.RemoveAll([](const FNetAuditRow& Row) { return Row.FindingCount == 0; });
	}
	// Classes with findings first, most findings first, then by path.
	Rows.Sort([](const FNetAuditRow& A, const FNetAuditRow& B)
	{
		if (A.FindingCount != B.FindingCount) return A.FindingCount > B.FindingCount;
		return A.SortName < B.SortName;
	});

	int32 ClassesWithFindings = 0;
	int32 FindingCount = 0;
	for (const FNetAuditRow& Row : Rows)
	{
		if (Row.FindingCount > 0) ++ClassesWithFindings;
		FindingCount += Row.FindingCount;
	}

	TSharedPtr<FJsonObject> FindingTotals = MakeShared<FJsonObject>();
	for (const TPair<FString, int32>& Pair : Totals) FindingTotals->SetNumberField(Pair.Key, Pair.Value);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetNumberField(TEXT("nativeClassesScanned"), NativeScanned);
	Result->SetNumberField(TEXT("blueprintsScanned"), BlueprintsScanned);
	Result->SetNumberField(TEXT("classesReported"), Rows.Num());
	Result->SetNumberField(TEXT("classesWithFindings"), ClassesWithFindings);
	Result->SetNumberField(TEXT("findingCount"), FindingCount);
	Result->SetObjectField(TEXT("findingsByKind"), FindingTotals);
	Result->SetBoolField(TEXT("requireServerValidation"), bRequireServerValidation);
	if (!bIncludeEngine) Result->SetStringField(TEXT("nativeScope"), TEXT("project modules and project plugins (includeEngine=false)"));
	if (bBlueprintsTruncated)
	{
		Result->SetBoolField(TEXT("blueprintsTruncated"), true);
		Result->SetNumberField(TEXT("maxBlueprints"), MaxBlueprints);
	}
	if (FailedToLoad.Num() > 0) Result->SetArrayField(TEXT("failedToLoad"), FailedToLoad);

	TArray<TSharedPtr<FJsonValue>> AllRows;
	for (const FNetAuditRow& Row : Rows) AllRows.Add(MakeShared<FJsonValueObject>(Row.Json));

	if (bDumpToFile)
	{
		TSharedPtr<FJsonObject> Dump = MakeShared<FJsonObject>();
		Dump->SetObjectField(TEXT("findingsByKind"), FindingTotals);
		Dump->SetArrayField(TEXT("classes"), AllRows);
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		if (!FJsonSerializer::Serialize(Dump.ToSharedRef(), Writer))
		{
			return MCPError(TEXT("Failed to serialize the replication audit."));
		}
		const FString ResolvedPath = MCPResolveDumpPath(OutputPath.IsEmpty()
			? FString(TEXT("UE_MCP/Audits/replication_audit.json")) : OutputPath);
		FString DumpError;
		if (!MCPWriteDumpFile(ResolvedPath, Text, TEXT("replication audit"), DumpError))
		{
			return MCPError(DumpError);
		}
		Result->SetBoolField(TEXT("dumpedToFile"), true);
		Result->SetStringField(TEXT("outputPath"), ResolvedPath);
		return MCPResult(Result);
	}

	const bool bTruncated = AllRows.Num() > Limit;
	if (bTruncated) AllRows.SetNum(Limit);
	Result->SetArrayField(TEXT("classes"), AllRows);
	Result->SetNumberField(TEXT("returned"), AllRows.Num());
	if (bTruncated)
	{
		Result->SetBoolField(TEXT("rowsTruncated"), true);
		Result->SetNumberField(TEXT("limit"), Limit);
		Result->SetStringField(TEXT("hint"), TEXT("Pass dumpToFile=true for every row, or onlyWithFindings=true to keep only the classes with findings."));
	}
	return MCPResult(Result);
}
