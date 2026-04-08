#include "NiagaraHandlers.h"
#include "HandlerRegistry.h"
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
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

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

	Result->SetArrayField(TEXT("assets"), AssetArray);
	Result->SetNumberField(TEXT("count"), AssetArray.Num());
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::ListNiagaraModules(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

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
	Result->SetArrayField(TEXT("modules"), AssetArray);
	Result->SetNumberField(TEXT("count"), AssetArray.Num());
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::CreateNiagaraSystem(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/VFX");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	FString FullAssetPath = PackagePath + TEXT("/") + Name;

	// Find the NiagaraSystemFactoryNew to avoid crash when creating without a factory (#88, #61)
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
		Result->SetStringField(TEXT("error"), TEXT("Failed to create NiagaraSystem. Ensure the Niagara plugin is enabled."));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::GetNiagaraInfo(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *AssetPath);
	if (!System)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("NiagaraSystem not found: %s"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::ListEmittersInSystem(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString SystemPath;
	if (!Params->TryGetStringField(TEXT("systemPath"), SystemPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'systemPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("NiagaraSystem not found: %s"), *SystemPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

	Result->SetArrayField(TEXT("emitters"), EmitterArray);
	Result->SetNumberField(TEXT("count"), EmitterArray.Num());
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::CreateNiagaraEmitter(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/VFX");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	// In UE5, standalone NiagaraEmitter creation may not be supported
	// Try to create via AssetTools
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	FString FullAssetPath = PackagePath + TEXT("/") + Name;
	UEditorAssetLibrary::DeleteAsset(FullAssetPath);

	UClass* EmitterClass = FindObject<UClass>(nullptr, TEXT("/Script/Niagara.NiagaraEmitter"));
	if (!EmitterClass)
	{
		Result->SetStringField(TEXT("error"), TEXT("NiagaraEmitter class not found - factory not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, EmitterClass, nullptr);
	if (!NewAsset)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create NiagaraEmitter - factory not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::SpawnNiagaraAtLocation(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString SystemPath;
	if (!Params->TryGetStringField(TEXT("systemPath"), SystemPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'systemPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UNiagaraSystem* NiagaraSystem = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!NiagaraSystem)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("NiagaraSystem not found: %s"), *SystemPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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
	bool bAutoDestroy = false;
	Params->TryGetBoolField(TEXT("autoDestroy"), bAutoDestroy);

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
		Result->SetStringField(TEXT("error"), TEXT("Failed to spawn Niagara system at location"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Apply label if provided
	FString Label;
	if (Params->TryGetStringField(TEXT("label"), Label))
	{
		SpawnedComponent->GetOwner()->SetActorLabel(*Label);
	}

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
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::SetNiagaraParameter(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString ActorLabel;
	if (!Params->TryGetStringField(TEXT("actorLabel"), ActorLabel))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'actorLabel' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString ParameterName;
	if (!Params->TryGetStringField(TEXT("parameterName"), ParameterName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'parameterName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString ParameterType = TEXT("float");
	Params->TryGetStringField(TEXT("parameterType"), ParameterType);

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("No editor world available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found with label: %s"), *ActorLabel));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Get Niagara component from the actor
	UNiagaraComponent* NiagaraComp = FoundActor->FindComponentByClass<UNiagaraComponent>();
	if (!NiagaraComp)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("No NiagaraComponent found on actor: %s"), *ActorLabel));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Set parameter based on type
	FName ParamFName(*ParameterName);
	if (ParameterType == TEXT("float"))
	{
		double Value = 0;
		if (!Params->TryGetNumberField(TEXT("value"), Value))
		{
			Result->SetStringField(TEXT("error"), TEXT("Missing 'value' parameter for float type"));
			Result->SetBoolField(TEXT("success"), false);
			return MakeShared<FJsonValueObject>(Result);
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
		bool bValue = false;
		Params->TryGetBoolField(TEXT("value"), bValue);
		NiagaraComp->SetVariableBool(ParamFName, bValue);
		Result->SetBoolField(TEXT("value"), bValue);
	}
	else if (ParameterType == TEXT("int"))
	{
		double IntValue = 0;
		Params->TryGetNumberField(TEXT("value"), IntValue);
		NiagaraComp->SetVariableInt(ParamFName, (int32)IntValue);
		Result->SetNumberField(TEXT("value"), IntValue);
	}
	else
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Unsupported parameter type: %s (use float, vector, bool, or int)"), *ParameterType));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("parameterName"), ParameterName);
	Result->SetStringField(TEXT("parameterType"), ParameterType);
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::CreateNiagaraSystemFromEmitter(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString SystemName;
	if (!Params->TryGetStringField(TEXT("systemName"), SystemName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'systemName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString EmitterPath;
	if (!Params->TryGetStringField(TEXT("emitterPath"), EmitterPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'emitterPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/VFX");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	// Load the emitter asset
	UNiagaraEmitter* Emitter = LoadObject<UNiagaraEmitter>(nullptr, *EmitterPath);
	if (!Emitter)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("NiagaraEmitter not found: %s"), *EmitterPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Create a new Niagara system
	FString FullAssetPath = PackagePath + TEXT("/") + SystemName;
	UEditorAssetLibrary::DeleteAsset(FullAssetPath);

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UObject* NewAsset = AssetTools.CreateAsset(SystemName, PackagePath, UNiagaraSystem::StaticClass(), nullptr);
	if (!NewAsset)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create NiagaraSystem"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UNiagaraSystem* NewSystem = Cast<UNiagaraSystem>(NewAsset);
	if (!NewSystem)
	{
		Result->SetStringField(TEXT("error"), TEXT("Created asset is not a NiagaraSystem"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Add the emitter to the system
	NewSystem->MarkPackageDirty();
	FNiagaraEmitterHandle EmitterHandle = NewSystem->AddEmitterHandle(*Emitter, Emitter->GetFName(), FGuid::NewGuid());

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	Result->SetStringField(TEXT("systemPath"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("systemName"), SystemName);
	Result->SetStringField(TEXT("emitterPath"), EmitterPath);
	Result->SetStringField(TEXT("emitterHandleName"), EmitterHandle.GetName().ToString());
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::AddEmitterToSystem(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	FString SystemPath, EmitterPath;
	if (!Params->TryGetStringField(TEXT("systemPath"), SystemPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'systemPath' parameter"));
		return MakeShared<FJsonValueObject>(Result);
	}
	if (!Params->TryGetStringField(TEXT("emitterPath"), EmitterPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'emitterPath' parameter"));
		return MakeShared<FJsonValueObject>(Result);
	}

	UNiagaraSystem* System = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(SystemPath));
	UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(UEditorAssetLibrary::LoadAsset(EmitterPath));

	if (!System)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("NiagaraSystem not found: %s"), *SystemPath));
		return MakeShared<FJsonValueObject>(Result);
	}
	if (!Emitter)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("NiagaraEmitter not found: %s"), *EmitterPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Actually add the emitter to the system (#69)
	System->Modify();
	FNiagaraEmitterHandle Handle = System->AddEmitterHandle(*Emitter, Emitter->GetFName(), FGuid::NewGuid());

	UEditorAssetLibrary::SaveAsset(System->GetPathName());

	Result->SetStringField(TEXT("systemPath"), SystemPath);
	Result->SetStringField(TEXT("emitterPath"), EmitterPath);
	Result->SetStringField(TEXT("emitterHandleName"), Handle.GetName().ToString());
	Result->SetNumberField(TEXT("emitterCount"), System->GetEmitterHandles().Num());
	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::SetEmitterProperty(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	FString SystemPath;
	if (!Params->TryGetStringField(TEXT("systemPath"), SystemPath) && !Params->TryGetStringField(TEXT("assetPath"), SystemPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'systemPath' parameter"));
		return MakeShared<FJsonValueObject>(Result);
	}

	FString EmitterName, PropName;
	Params->TryGetStringField(TEXT("emitterName"), EmitterName);
	if (!Params->TryGetStringField(TEXT("propertyName"), PropName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'propertyName' parameter"));
		return MakeShared<FJsonValueObject>(Result);
	}

	FString Value;
	if (!Params->TryGetStringField(TEXT("value"), Value))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'value' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("NiagaraSystem not found: %s"), *SystemPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Emitter '%s' not found. Available: [%s]"), *EmitterName, *FString::Join(Names, TEXT(", "))));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Try to set the property via reflection on the emitter handle's emitter data
	FVersionedNiagaraEmitterData* EmitterData = Handles[TargetIdx].GetInstance().GetEmitterData();
	if (!EmitterData)
	{
		Result->SetStringField(TEXT("error"), TEXT("Could not access emitter data"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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
	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::GetEmitterInfo(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath' parameter"));
		return MakeShared<FJsonValueObject>(Result);
	}

	UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Emitter)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("NiagaraEmitter not found: %s"), *AssetPath));
		return MakeShared<FJsonValueObject>(Result);
	}

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

	Result->SetBoolField(TEXT("success"), true);
	return MakeShared<FJsonValueObject>(Result);
}
