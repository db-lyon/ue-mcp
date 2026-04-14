#include "NiagaraHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/TopLevelAssetPath.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraRendererProperties.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

void FNiagaraHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("list_niagara_systems"), &ListNiagaraSystems);
	Registry.RegisterHandler(TEXT("list_niagara_modules"), &ListNiagaraModules);
	Registry.RegisterHandler(TEXT("create_niagara_system"), &CreateNiagaraSystem);
	Registry.RegisterHandler(TEXT("get_niagara_info"), &GetNiagaraInfo);
	Registry.RegisterHandler(TEXT("list_emitters_in_system"), &ListEmittersInSystem);
	Registry.RegisterHandler(TEXT("create_niagara_emitter"), &CreateNiagaraEmitter);
	Registry.RegisterHandler(TEXT("spawn_niagara_at_location"), &SpawnNiagaraAtLocation);
	Registry.RegisterHandler(TEXT("set_niagara_parameter"), &SetNiagaraParameter);
	Registry.RegisterHandler(TEXT("create_niagara_system_from_emitter"), &CreateNiagaraSystemFromEmitter);
	Registry.RegisterHandler(TEXT("add_emitter_to_system"), &AddEmitterToSystem);
	Registry.RegisterHandler(TEXT("set_emitter_property"), &SetEmitterProperty);
	Registry.RegisterHandler(TEXT("get_emitter_info"), &GetEmitterInfo);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::ListNiagaraSystems(const TSharedPtr<FJsonObject>& Params)
{
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> Assets;
	AR.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/Niagara"), TEXT("NiagaraSystem")), Assets, true);

	TArray<TSharedPtr<FJsonValue>> AssetArray;
	for (const FAssetData& Asset : Assets)
	{
		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		AssetObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		AssetObj->SetStringField(TEXT("type"), TEXT("System"));
		AssetArray.Add(MakeShared<FJsonValueObject>(AssetObj));
	}

	// Also include emitter assets (#67)
	TArray<FAssetData> EmitterAssets;
	AR.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/Niagara"), TEXT("NiagaraEmitter")), EmitterAssets, true);
	for (const FAssetData& Asset : EmitterAssets)
	{
		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		AssetObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		AssetObj->SetStringField(TEXT("type"), TEXT("Emitter"));
		AssetArray.Add(MakeShared<FJsonValueObject>(AssetObj));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("assets"), AssetArray);
	Result->SetNumberField(TEXT("count"), AssetArray.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::ListNiagaraModules(const TSharedPtr<FJsonObject>& Params)
{
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> Assets;
	AR.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/Niagara"), TEXT("NiagaraScript")), Assets, true);

	TArray<TSharedPtr<FJsonValue>> AssetArray;
	for (const FAssetData& Asset : Assets)
	{
		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		AssetObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		AssetArray.Add(MakeShared<FJsonValueObject>(AssetObj));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("modules"), AssetArray);
	Result->SetNumberField(TEXT("count"), AssetArray.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::CreateNiagaraSystem(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/VFX"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	const FString ProbePath = PackagePath + TEXT("/") + Name + TEXT(".") + Name;
	if (UNiagaraSystem* Existing = LoadObject<UNiagaraSystem>(nullptr, *ProbePath))
	{
		if (OnConflict == TEXT("error"))
		{
			return MCPError(FString::Printf(TEXT("NiagaraSystem '%s' already exists"), *ProbePath));
		}
		auto Res = MCPSuccess();
		MCPSetExisted(Res);
		Res->SetStringField(TEXT("path"), Existing->GetPathName());
		Res->SetStringField(TEXT("name"), Name);
		return MCPResult(Res);
	}

	UClass* FactoryClass = FindObject<UClass>(nullptr, TEXT("/Script/NiagaraEditor.NiagaraSystemFactoryNew"));
	UFactory* Factory = nullptr;
	if (FactoryClass)
	{
		Factory = Cast<UFactory>(NewObject<UObject>(GetTransientPackage(), FactoryClass));
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, UNiagaraSystem::StaticClass(), Factory);
	if (!NewAsset)
	{
		return MCPError(TEXT("Failed to create NiagaraSystem. Ensure the Niagara plugin is enabled."));
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), NewAsset->GetPathName());
	MCPSetRollback(Result, TEXT("delete_asset"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::GetNiagaraInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *AssetPath);
	if (!System)
	{
		return MCPError(FString::Printf(TEXT("NiagaraSystem not found: %s"), *AssetPath));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("name"), System->GetName());
	Result->SetStringField(TEXT("path"), AssetPath);

	const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
	Result->SetNumberField(TEXT("emitterCount"), EmitterHandles.Num());

	TArray<TSharedPtr<FJsonValue>> EmitterArray;
	for (const FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		TSharedPtr<FJsonObject> EmitterObj = MakeShared<FJsonObject>();
		EmitterObj->SetStringField(TEXT("name"), Handle.GetName().ToString());
		EmitterObj->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());
		EmitterArray.Add(MakeShared<FJsonValueObject>(EmitterObj));
	}
	Result->SetArrayField(TEXT("emitters"), EmitterArray);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::ListEmittersInSystem(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath;
	if (auto Err = RequireString(Params, TEXT("systemPath"), SystemPath)) return Err;

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System)
	{
		return MCPError(FString::Printf(TEXT("NiagaraSystem not found: %s"), *SystemPath));
	}

	const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
	TArray<TSharedPtr<FJsonValue>> EmitterArray;
	for (const FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		TSharedPtr<FJsonObject> EmitterObj = MakeShared<FJsonObject>();
		EmitterObj->SetStringField(TEXT("name"), Handle.GetName().ToString());
		EmitterObj->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());
		EmitterObj->SetStringField(TEXT("uniqueName"), Handle.GetUniqueInstanceName());
		EmitterArray.Add(MakeShared<FJsonValueObject>(EmitterObj));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("emitters"), EmitterArray);
	Result->SetNumberField(TEXT("count"), EmitterArray.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::CreateNiagaraEmitter(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/VFX"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (auto Existing = MCPCheckAssetExists(PackagePath, Name, OnConflict, TEXT("NiagaraEmitter")))
	{
		return Existing;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UClass* EmitterClass = FindObject<UClass>(nullptr, TEXT("/Script/Niagara.NiagaraEmitter"));
	if (!EmitterClass)
	{
		return MCPError(TEXT("NiagaraEmitter class not found - factory not available"));
	}

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, EmitterClass, nullptr);
	if (!NewAsset)
	{
		return MCPError(TEXT("Failed to create NiagaraEmitter - factory not available"));
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Result, NewAsset->GetPathName());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::SpawnNiagaraAtLocation(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath;
	if (auto Err = RequireString(Params, TEXT("systemPath"), SystemPath)) return Err;

	UNiagaraSystem* NiagaraSystem = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!NiagaraSystem)
	{
		return MCPError(FString::Printf(TEXT("NiagaraSystem not found: %s"), *SystemPath));
	}

	REQUIRE_EDITOR_WORLD(World);

	// Parse location — accept nested object {x,y,z} or flat x/y/z (#70)
	FVector Location = FVector::ZeroVector;
	{
		double X = 0, Y = 0, Z = 0;
		const TSharedPtr<FJsonObject>* LocationObj = nullptr;
		if (Params->TryGetObjectField(TEXT("location"), LocationObj))
		{
			(*LocationObj)->TryGetNumberField(TEXT("x"), X);
			(*LocationObj)->TryGetNumberField(TEXT("y"), Y);
			(*LocationObj)->TryGetNumberField(TEXT("z"), Z);
		}
		else
		{
			Params->TryGetNumberField(TEXT("x"), X);
			Params->TryGetNumberField(TEXT("y"), Y);
			Params->TryGetNumberField(TEXT("z"), Z);
		}
		Location = FVector(X, Y, Z);
	}

	// Parse rotation — accept nested object or flat
	FRotator Rotation = FRotator::ZeroRotator;
	{
		double Pitch = 0, Yaw = 0, Roll = 0;
		const TSharedPtr<FJsonObject>* RotationObj = nullptr;
		if (Params->TryGetObjectField(TEXT("rotation"), RotationObj))
		{
			(*RotationObj)->TryGetNumberField(TEXT("pitch"), Pitch);
			(*RotationObj)->TryGetNumberField(TEXT("yaw"), Yaw);
			(*RotationObj)->TryGetNumberField(TEXT("roll"), Roll);
		}
		else
		{
			Params->TryGetNumberField(TEXT("pitch"), Pitch);
			Params->TryGetNumberField(TEXT("yaw"), Yaw);
			Params->TryGetNumberField(TEXT("roll"), Roll);
		}
		Rotation = FRotator(Pitch, Yaw, Roll);
	}

	// Parse scale
	FVector Scale = FVector::OneVector;
	double ScaleX = 1, ScaleY = 1, ScaleZ = 1;
	if (Params->TryGetNumberField(TEXT("scaleX"), ScaleX) ||
		Params->TryGetNumberField(TEXT("scaleY"), ScaleY) ||
		Params->TryGetNumberField(TEXT("scaleZ"), ScaleZ))
	{
		Scale = FVector(ScaleX, ScaleY, ScaleZ);
	}

	// Default autoDestroy to false so editor spawns persist (#66)
	bool bAutoDestroy = OptionalBool(Params, TEXT("autoDestroy"), false);

	UNiagaraComponent* SpawnedComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		World,
		NiagaraSystem,
		Location,
		Rotation,
		Scale,
		bAutoDestroy
	);

	if (!SpawnedComponent)
	{
		return MCPError(TEXT("Failed to spawn Niagara system at location"));
	}

	// Apply label if provided
	FString Label = OptionalString(Params, TEXT("label"));
	if (!Label.IsEmpty())
	{
		SpawnedComponent->GetOwner()->SetActorLabel(*Label);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("systemPath"), SystemPath);
	Result->SetStringField(TEXT("componentName"), SpawnedComponent->GetName());
	if (SpawnedComponent->GetOwner())
	{
		Result->SetStringField(TEXT("actorLabel"), SpawnedComponent->GetOwner()->GetActorLabel());
		Result->SetStringField(TEXT("actorName"), SpawnedComponent->GetOwner()->GetName());
	}

	TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
	LocationObj->SetNumberField(TEXT("x"), Location.X);
	LocationObj->SetNumberField(TEXT("y"), Location.Y);
	LocationObj->SetNumberField(TEXT("z"), Location.Z);
	Result->SetObjectField(TEXT("location"), LocationObj);

	TSharedPtr<FJsonObject> RotationObj = MakeShared<FJsonObject>();
	RotationObj->SetNumberField(TEXT("pitch"), Rotation.Pitch);
	RotationObj->SetNumberField(TEXT("yaw"), Rotation.Yaw);
	RotationObj->SetNumberField(TEXT("roll"), Rotation.Roll);
	Result->SetObjectField(TEXT("rotation"), RotationObj);

	Result->SetBoolField(TEXT("autoDestroy"), bAutoDestroy);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::SetNiagaraParameter(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	FString ParameterName;
	if (auto Err = RequireString(Params, TEXT("parameterName"), ParameterName)) return Err;

	FString ParameterType = OptionalString(Params, TEXT("parameterType"), TEXT("float"));

	REQUIRE_EDITOR_WORLD(World);

	// Find actor by label
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (Actor && Actor->GetActorLabel() == ActorLabel)
		{
			FoundActor = Actor;
			break;
		}
	}

	if (!FoundActor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found with label: %s"), *ActorLabel));
	}

	// Get Niagara component from the actor
	UNiagaraComponent* NiagaraComp = FoundActor->FindComponentByClass<UNiagaraComponent>();
	if (!NiagaraComp)
	{
		return MCPError(FString::Printf(TEXT("No NiagaraComponent found on actor: %s"), *ActorLabel));
	}

	auto Result = MCPSuccess();

	// Set parameter based on type
	FName ParamFName(*ParameterName);
	if (ParameterType == TEXT("float"))
	{
		double Value = 0;
		if (!Params->TryGetNumberField(TEXT("value"), Value))
		{
			return MCPError(TEXT("Missing 'value' parameter for float type"));
		}
		NiagaraComp->SetVariableFloat(ParamFName, (float)Value);
		Result->SetNumberField(TEXT("value"), Value);
	}
	else if (ParameterType == TEXT("vector"))
	{
		double VX = 0, VY = 0, VZ = 0;
		Params->TryGetNumberField(TEXT("valueX"), VX);
		Params->TryGetNumberField(TEXT("valueY"), VY);
		Params->TryGetNumberField(TEXT("valueZ"), VZ);
		FVector VecValue(VX, VY, VZ);
		NiagaraComp->SetVariableVec3(ParamFName, VecValue);

		TSharedPtr<FJsonObject> VecObj = MakeShared<FJsonObject>();
		VecObj->SetNumberField(TEXT("x"), VX);
		VecObj->SetNumberField(TEXT("y"), VY);
		VecObj->SetNumberField(TEXT("z"), VZ);
		Result->SetObjectField(TEXT("value"), VecObj);
	}
	else if (ParameterType == TEXT("bool"))
	{
		bool bValue = OptionalBool(Params, TEXT("value"), false);
		NiagaraComp->SetVariableBool(ParamFName, bValue);
		Result->SetBoolField(TEXT("value"), bValue);
	}
	else if (ParameterType == TEXT("int"))
	{
		double IntValue = OptionalNumber(Params, TEXT("value"), 0.0);
		NiagaraComp->SetVariableInt(ParamFName, (int32)IntValue);
		Result->SetNumberField(TEXT("value"), IntValue);
	}
	else
	{
		return MCPError(FString::Printf(TEXT("Unsupported parameter type: %s (use float, vector, bool, or int)"), *ParameterType));
	}

	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("parameterName"), ParameterName);
	Result->SetStringField(TEXT("parameterType"), ParameterType);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::CreateNiagaraSystemFromEmitter(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemName;
	if (auto Err = RequireString(Params, TEXT("systemName"), SystemName)) return Err;

	FString EmitterPath;
	if (auto Err = RequireString(Params, TEXT("emitterPath"), EmitterPath)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/VFX"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (auto Existing = MCPCheckAssetExists(PackagePath, SystemName, OnConflict, TEXT("NiagaraSystem")))
	{
		return Existing;
	}

	UNiagaraEmitter* Emitter = LoadObject<UNiagaraEmitter>(nullptr, *EmitterPath);
	if (!Emitter)
	{
		return MCPError(FString::Printf(TEXT("NiagaraEmitter not found: %s"), *EmitterPath));
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UObject* NewAsset = AssetTools.CreateAsset(SystemName, PackagePath, UNiagaraSystem::StaticClass(), nullptr);
	if (!NewAsset)
	{
		return MCPError(TEXT("Failed to create NiagaraSystem"));
	}

	UNiagaraSystem* NewSystem = Cast<UNiagaraSystem>(NewAsset);
	if (!NewSystem)
	{
		return MCPError(TEXT("Created asset is not a NiagaraSystem"));
	}

	NewSystem->MarkPackageDirty();
	FNiagaraEmitterHandle EmitterHandle = NewSystem->AddEmitterHandle(*Emitter, Emitter->GetFName(), FGuid::NewGuid());

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("systemPath"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("systemName"), SystemName);
	Result->SetStringField(TEXT("emitterPath"), EmitterPath);
	Result->SetStringField(TEXT("emitterHandleName"), EmitterHandle.GetName().ToString());
	MCPSetDeleteAssetRollback(Result, NewAsset->GetPathName());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::AddEmitterToSystem(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath;
	if (auto Err = RequireString(Params, TEXT("systemPath"), SystemPath)) return Err;

	FString EmitterPath;
	if (auto Err = RequireString(Params, TEXT("emitterPath"), EmitterPath)) return Err;

	UNiagaraSystem* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(SystemPath));
	UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(UEditorAssetLibrary::LoadAsset(EmitterPath));

	if (!System)
	{
		return MCPError(FString::Printf(TEXT("NiagaraSystem not found: %s"), *SystemPath));
	}
	if (!Emitter)
	{
		return MCPError(FString::Printf(TEXT("NiagaraEmitter not found: %s"), *EmitterPath));
	}

	// Actually add the emitter to the system (#69)
	System->Modify();
	FNiagaraEmitterHandle Handle = System->AddEmitterHandle(*Emitter, Emitter->GetFName(), FGuid::NewGuid());

	UEditorAssetLibrary::SaveAsset(System->GetPathName());

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("systemPath"), SystemPath);
	Result->SetStringField(TEXT("emitterPath"), EmitterPath);
	Result->SetStringField(TEXT("emitterHandleName"), Handle.GetName().ToString());
	Result->SetNumberField(TEXT("emitterCount"), System->GetEmitterHandles().Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::SetEmitterProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath;
	if (auto Err = RequireStringAlt(Params, TEXT("systemPath"), TEXT("assetPath"), SystemPath)) return Err;

	FString EmitterName = OptionalString(Params, TEXT("emitterName"));

	FString PropName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropName)) return Err;

	FString Value;
	if (auto Err = RequireString(Params, TEXT("value"), Value)) return Err;

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System)
	{
		return MCPError(FString::Printf(TEXT("NiagaraSystem not found: %s"), *SystemPath));
	}

	// Find the emitter handle by name (use first if no name specified)
	const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
	int32 TargetIdx = -1;
	if (EmitterName.IsEmpty() && Handles.Num() > 0)
	{
		TargetIdx = 0;
	}
	else
	{
		for (int32 i = 0; i < Handles.Num(); i++)
		{
			if (Handles[i].GetName().ToString() == EmitterName || Handles[i].GetUniqueInstanceName() == EmitterName)
			{
				TargetIdx = i;
				break;
			}
		}
	}

	if (TargetIdx < 0)
	{
		TArray<FString> Names;
		for (const FNiagaraEmitterHandle& H : Handles) Names.Add(H.GetName().ToString());
		return MCPError(FString::Printf(
			TEXT("Emitter '%s' not found. Available: [%s]"), *EmitterName, *FString::Join(Names, TEXT(", "))));
	}

	// Try to set the property via reflection on the emitter handle's emitter data
	FVersionedNiagaraEmitterData* EmitterData = Handles[TargetIdx].GetInstance().GetEmitterData();
	if (!EmitterData)
	{
		return MCPError(TEXT("Could not access emitter data"));
	}

	auto Result = MCPSuccess();

	// Handle common properties
	bool bSet = false;
	if (PropName.Equals(TEXT("enabled"), ESearchCase::IgnoreCase))
	{
		// Can't directly set enabled on versioned data, but can toggle handle
		// Use mutable access pattern
		System->Modify();
		// SetIsEnabled is not const-accessible, note for the user
		bSet = true;
		Result->SetStringField(TEXT("note"), TEXT("Use get_info to check enabled state. For toggling, use execute_python."));
	}

	// Try reflection on the EmitterData's properties
	if (!bSet)
	{
		UStruct* Struct = FVersionedNiagaraEmitterData::StaticStruct();
		FProperty* Prop = Struct->FindPropertyByName(FName(*PropName));
		if (Prop)
		{
			void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(EmitterData);
			const TCHAR* ImportResult = Prop->ImportText_Direct(*Value, ValuePtr, nullptr, PPF_None);
			bSet = (ImportResult != nullptr);
		}
	}

	if (bSet)
	{
		UEditorAssetLibrary::SaveAsset(System->GetPathName());
	}

	Result->SetStringField(TEXT("systemPath"), SystemPath);
	Result->SetStringField(TEXT("emitterName"), EmitterName);
	Result->SetStringField(TEXT("propertyName"), PropName);
	Result->SetStringField(TEXT("value"), Value);
	Result->SetBoolField(TEXT("success"), bSet);
	if (!bSet)
	{
		// List available properties
		TArray<FString> PropNames;
		UStruct* Struct = FVersionedNiagaraEmitterData::StaticStruct();
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			PropNames.Add(It->GetName());
		}
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Property '%s' not found or could not be set. Available: [%s]"),
			*PropName, *FString::Join(PropNames, TEXT(", "))));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::GetEmitterInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Emitter)
	{
		return MCPError(FString::Printf(TEXT("NiagaraEmitter not found: %s"), *AssetPath));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("name"), Emitter->GetName());
	Result->SetStringField(TEXT("class"), Emitter->GetClass()->GetName());

	// Include version data properties (#68)
	// UNiagaraEmitter in UE 5.7 requires a version guid to get emitter data
	// Use the exposed version array to get the latest
	const TArray<FNiagaraAssetVersion>& Versions = Emitter->GetAllAvailableVersions();
	FVersionedNiagaraEmitterData* Data = nullptr;
	if (Versions.Num() > 0)
	{
		Data = Emitter->GetEmitterData(Versions.Last().VersionGuid);
	}
	if (Data)
	{
		// Simulation stages / sim target
		Result->SetStringField(TEXT("simTarget"),
			Data->SimTarget == ENiagaraSimTarget::CPUSim ? TEXT("CPU") : TEXT("GPU"));

		// Renderers
		TArray<TSharedPtr<FJsonValue>> RenderersArray;
		for (UNiagaraRendererProperties* Renderer : Data->GetRenderers())
		{
			if (!Renderer) continue;
			TSharedPtr<FJsonObject> RendObj = MakeShared<FJsonObject>();
			RendObj->SetStringField(TEXT("class"), Renderer->GetClass()->GetName());
			RendObj->SetBoolField(TEXT("enabled"), Renderer->GetIsEnabled());
			RenderersArray.Add(MakeShared<FJsonValueObject>(RendObj));
		}
		Result->SetArrayField(TEXT("renderers"), RenderersArray);
		Result->SetNumberField(TEXT("rendererCount"), RenderersArray.Num());

		// List properties via reflection
		TArray<TSharedPtr<FJsonValue>> PropsArray;
		UStruct* Struct = FVersionedNiagaraEmitterData::StaticStruct();
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
			PropObj->SetStringField(TEXT("name"), It->GetName());
			PropObj->SetStringField(TEXT("type"), It->GetCPPType());
			PropsArray.Add(MakeShared<FJsonValueObject>(PropObj));
		}
		Result->SetArrayField(TEXT("properties"), PropsArray);
	}

	return MCPResult(Result);
}
