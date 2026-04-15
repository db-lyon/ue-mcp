#include "AudioHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "Sound/SoundCue.h"
#include "Factories/SoundCueFactoryNew.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/AmbientSound.h"
#include "Components/AudioComponent.h"
#include "EngineUtils.h"

void FAudioHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("list_sound_assets"), &ListSoundAssets);
	Registry.RegisterHandler(TEXT("create_sound_cue"), &CreateSoundCue);
	Registry.RegisterHandler(TEXT("create_metasound_source"), &CreateMetaSoundSource);
	Registry.RegisterHandler(TEXT("play_sound_at_location"), &PlaySoundAtLocation);
	Registry.RegisterHandler(TEXT("spawn_ambient_sound"), &SpawnAmbientSound);
}

TSharedPtr<FJsonValue> FAudioHandlers::ListSoundAssets(const TSharedPtr<FJsonObject>& Params)
{
	auto Result = MCPSuccess();

	bool bRecursive = OptionalBool(Params, TEXT("recursive"), true);

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FTopLevelAssetPath> ClassPaths;
	ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("SoundWave")));
	ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("SoundCue")));
	ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/MetasoundEngine"), TEXT("MetaSoundSource")));

	TArray<TSharedPtr<FJsonValue>> AssetsArray;

	for (const FTopLevelAssetPath& ClassPath : ClassPaths)
	{
		TArray<FAssetData> AssetDataList;
		AssetRegistry.GetAssetsByClass(ClassPath, AssetDataList, bRecursive);

		for (const FAssetData& AssetData : AssetDataList)
		{
			TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
			AssetObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
			AssetObj->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
			AssetObj->SetStringField(TEXT("class"), AssetData.AssetClassPath.GetAssetName().ToString());
			AssetObj->SetStringField(TEXT("packagePath"), AssetData.PackagePath.ToString());
			AssetsArray.Add(MakeShared<FJsonValueObject>(AssetObj));
		}
	}

	Result->SetArrayField(TEXT("assets"), AssetsArray);
	Result->SetNumberField(TEXT("count"), AssetsArray.Num());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAudioHandlers::CreateSoundCue(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Audio/SoundCues"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	const FString ProbePath = PackagePath + TEXT("/") + Name + TEXT(".") + Name;
	if (USoundCue* Existing = LoadObject<USoundCue>(nullptr, *ProbePath))
	{
		if (OnConflict == TEXT("error"))
		{
			return MCPError(FString::Printf(TEXT("SoundCue '%s' already exists"), *ProbePath));
		}
		auto Res = MCPSuccess();
		MCPSetExisted(Res);
		Res->SetStringField(TEXT("path"), Existing->GetPathName());
		Res->SetStringField(TEXT("name"), Name);
		return MCPResult(Res);
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	USoundCueFactoryNew* SoundCueFactory = NewObject<USoundCueFactoryNew>();
	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, USoundCue::StaticClass(), SoundCueFactory);
	if (!NewAsset)
	{
		return MCPError(TEXT("Failed to create SoundCue"));
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

TSharedPtr<FJsonValue> FAudioHandlers::CreateMetaSoundSource(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Audio/MetaSounds"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	UClass* MetaSoundSourceClass = FindObject<UClass>(nullptr, TEXT("/Script/MetasoundEngine.MetaSoundSource"));
	if (!MetaSoundSourceClass)
	{
		return MCPError(TEXT("MetaSoundSource class not found. Enable MetaSound plugin."));
	}

	const FString ProbePath = PackagePath + TEXT("/") + Name + TEXT(".") + Name;
	if (UObject* Existing = LoadObject<UObject>(nullptr, *ProbePath))
	{
		if (OnConflict == TEXT("error"))
		{
			return MCPError(FString::Printf(TEXT("MetaSoundSource '%s' already exists"), *ProbePath));
		}
		auto Res = MCPSuccess();
		MCPSetExisted(Res);
		Res->SetStringField(TEXT("path"), Existing->GetPathName());
		Res->SetStringField(TEXT("name"), Name);
		return MCPResult(Res);
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, MetaSoundSourceClass, nullptr);
	if (!NewAsset)
	{
		return MCPError(TEXT("Failed to create MetaSoundSource"));
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

TSharedPtr<FJsonValue> FAudioHandlers::PlaySoundAtLocation(const TSharedPtr<FJsonObject>& Params)
{
	// Get required sound asset path
	FString SoundPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), SoundPath)) return Err;

	// Load the sound asset
	USoundBase* Sound = Cast<USoundBase>(UEditorAssetLibrary::LoadAsset(SoundPath));
	if (!Sound)
	{
		return MCPError(FString::Printf(TEXT("Sound not found: %s"), *SoundPath));
	}

	// Get the editor world
	REQUIRE_EDITOR_WORLD(World);

	// Parse location from JSON object (defaults to origin)
	FVector Location = FVector::ZeroVector;
	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	if (Params->TryGetObjectField(TEXT("location"), LocationObj))
	{
		(*LocationObj)->TryGetNumberField(TEXT("x"), Location.X);
		(*LocationObj)->TryGetNumberField(TEXT("y"), Location.Y);
		(*LocationObj)->TryGetNumberField(TEXT("z"), Location.Z);
	}

	// Parse optional volume and pitch multipliers (accept both short and long names)
	double Volume = 1.0;
	double Pitch = 1.0;
	if (!Params->TryGetNumberField(TEXT("volume"), Volume))
	{
		Params->TryGetNumberField(TEXT("volumeMultiplier"), Volume);
	}
	if (!Params->TryGetNumberField(TEXT("pitch"), Pitch))
	{
		Params->TryGetNumberField(TEXT("pitchMultiplier"), Pitch);
	}

	// No rollback: destructive/external — playing a one-shot sound has no inverse.
	// Replays produce a new audible event; not natural-key idempotent.
	UGameplayStatics::PlaySoundAtLocation(World, Sound, Location, static_cast<float>(Volume), static_cast<float>(Pitch));

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), SoundPath);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAudioHandlers::SpawnAmbientSound(const TSharedPtr<FJsonObject>& Params)
{
	// Get required sound asset path
	FString SoundPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), SoundPath)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	const FString Label = OptionalString(Params, TEXT("label"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (!Label.IsEmpty())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() == Label)
			{
				if (OnConflict == TEXT("error"))
				{
					return MCPError(FString::Printf(TEXT("AmbientSound '%s' already exists"), *Label));
				}
				auto Existing = MCPSuccess();
				MCPSetExisted(Existing);
				Existing->SetStringField(TEXT("label"), Label);
				Existing->SetStringField(TEXT("assetPath"), SoundPath);
				return MCPResult(Existing);
			}
		}
	}

	FVector Location = FVector::ZeroVector;
	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	if (Params->TryGetObjectField(TEXT("location"), LocationObj))
	{
		(*LocationObj)->TryGetNumberField(TEXT("x"), Location.X);
		(*LocationObj)->TryGetNumberField(TEXT("y"), Location.Y);
		(*LocationObj)->TryGetNumberField(TEXT("z"), Location.Z);
	}

	FTransform SpawnTransform(FRotator::ZeroRotator, Location);
	AAmbientSound* AmbientSoundActor = World->SpawnActor<AAmbientSound>(AAmbientSound::StaticClass(), SpawnTransform);
	if (!AmbientSoundActor)
	{
		return MCPError(TEXT("Failed to spawn AmbientSound actor"));
	}

	if (!Label.IsEmpty())
	{
		AmbientSoundActor->SetActorLabel(Label);
	}

	// Load and assign the sound asset to the AudioComponent
	USoundBase* Sound = Cast<USoundBase>(UEditorAssetLibrary::LoadAsset(SoundPath));
	if (Sound)
	{
		UAudioComponent* AudioComp = AmbientSoundActor->GetAudioComponent();
		if (AudioComp)
		{
			AudioComp->SetSound(Sound);

			// Apply optional volume multiplier
			double Volume = 1.0;
			if (Params->TryGetNumberField(TEXT("volume"), Volume))
			{
				AudioComp->VolumeMultiplier = static_cast<float>(Volume);
			}
		}
	}

	const FString FinalLabel = AmbientSoundActor->GetActorLabel();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), SoundPath);
	Result->SetStringField(TEXT("label"), FinalLabel);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), FinalLabel);
	MCPSetRollback(Result, TEXT("delete_actor"), Payload);

	return MCPResult(Result);
}
