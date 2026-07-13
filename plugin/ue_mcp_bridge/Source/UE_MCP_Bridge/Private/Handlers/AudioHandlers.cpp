#include "AudioHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundWave.h"
#include "Factories/SoundCueFactoryNew.h"
#include "AssetImportTask.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/AmbientSound.h"
#include "Components/AudioComponent.h"
#include "EngineUtils.h"

void FAudioHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("list_sound_assets"), &ListSoundAssets);
	Registry.RegisterHandler(TEXT("import_audio"), &ImportAudio);
	Registry.RegisterHandler(TEXT("create_sound_cue"), &CreateSoundCue);
	Registry.RegisterHandler(TEXT("create_metasound_source"), &CreateMetaSoundSource);
	Registry.RegisterHandler(TEXT("play_sound_at_location"), &PlaySoundAtLocation);
	Registry.RegisterHandler(TEXT("spawn_ambient_sound"), &SpawnAmbientSound);

	// MetaSound graph authoring (AudioHandlers_MetaSound.cpp)
	Registry.RegisterHandler(TEXT("metasound_author"), &MetaSoundAuthor);
	Registry.RegisterHandler(TEXT("metasound_list_node_classes"), &MetaSoundListNodeClasses);
	Registry.RegisterHandler(TEXT("metasound_get_graph"), &MetaSoundGetGraph);
	Registry.RegisterHandler(TEXT("metasound_add_node"), &MetaSoundAddNode);
	Registry.RegisterHandler(TEXT("metasound_add_graph_input"), &MetaSoundAddGraphInput);
	Registry.RegisterHandler(TEXT("metasound_add_graph_output"), &MetaSoundAddGraphOutput);
	Registry.RegisterHandler(TEXT("metasound_connect"), &MetaSoundConnect);
	Registry.RegisterHandler(TEXT("metasound_connect_graph_input"), &MetaSoundConnectGraphInput);
	Registry.RegisterHandler(TEXT("metasound_connect_graph_output"), &MetaSoundConnectGraphOutput);
	Registry.RegisterHandler(TEXT("metasound_connect_audio_out"), &MetaSoundConnectAudioOut);
	Registry.RegisterHandler(TEXT("metasound_set_input_default"), &MetaSoundSetInputDefault);
	Registry.RegisterHandler(TEXT("metasound_build"), &MetaSoundBuild);

	// SoundCue graph authoring (AudioHandlers_SoundCue.cpp)
	Registry.RegisterHandler(TEXT("soundcue_author"), &SoundCueAuthor);
	Registry.RegisterHandler(TEXT("soundcue_add_node"), &SoundCueAddNode);
	Registry.RegisterHandler(TEXT("soundcue_connect"), &SoundCueConnect);
	Registry.RegisterHandler(TEXT("soundcue_get_graph"), &SoundCueGetGraph);

	// Mixing + routing + spatialization (AudioHandlers_Mixing.cpp)
	Registry.RegisterHandler(TEXT("create_submix"), &CreateSubmix);
	Registry.RegisterHandler(TEXT("set_submix_parent"), &SetSubmixParent);
	Registry.RegisterHandler(TEXT("add_submix_effect"), &AddSubmixEffect);
	Registry.RegisterHandler(TEXT("create_sound_class"), &CreateSoundClass);
	Registry.RegisterHandler(TEXT("create_sound_mix"), &CreateSoundMix);
	Registry.RegisterHandler(TEXT("create_concurrency"), &CreateConcurrency);
	Registry.RegisterHandler(TEXT("create_attenuation"), &CreateAttenuation);
	Registry.RegisterHandler(TEXT("set_sound_submix"), &SetSoundSubmix);
	Registry.RegisterHandler(TEXT("add_sound_submix_send"), &AddSoundSubmixSend);
	Registry.RegisterHandler(TEXT("set_sound_class"), &SetSoundClass);
	Registry.RegisterHandler(TEXT("set_sound_attenuation"), &SetSoundAttenuation);
	Registry.RegisterHandler(TEXT("set_sound_concurrency"), &SetSoundConcurrency);
	Registry.RegisterHandler(TEXT("set_audio_property"), &SetAudioProperty);
}

// #664: import a WAV/OGG/FLAC file as a USoundWave. Passing a null factory lets
// AssetTools auto-select the sound-import factory from the file extension.
TSharedPtr<FJsonValue> FAudioHandlers::ImportAudio(const TSharedPtr<FJsonObject>& Params)
{
	FString FileName;
	if (auto Err = RequireStringAlt(Params, TEXT("filename"), TEXT("filePath"), FileName)) return Err;
	if (!FPaths::FileExists(FileName))
	{
		return MCPError(FString::Printf(TEXT("File not found: %s"), *FileName));
	}

	FString DestinationPath = OptionalString(Params, TEXT("destinationPath"), TEXT("/Game/Audio"));
	{
		const FString PkgPath = OptionalString(Params, TEXT("packagePath"));
		if (!PkgPath.IsEmpty()) DestinationPath = PkgPath;
	}

	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	FGCRootScope TaskRoot(Task);
	Task->bAutomated = true;
	Task->bReplaceExisting = OptionalBool(Params, TEXT("replaceExisting"), true);
	Task->bSave = false;
	Task->Filename = FileName;
	Task->DestinationPath = DestinationPath;
	// Factory left null: AssetTools resolves USoundFactory for wav/ogg/flac.

	FString AssetName;
	if (!Params->TryGetStringField(TEXT("assetName"), AssetName))
	{
		Params->TryGetStringField(TEXT("name"), AssetName);
	}
	if (!AssetName.IsEmpty()) Task->DestinationName = AssetName;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	TArray<UAssetImportTask*> Tasks;
	Tasks.Add(Task);
	AssetToolsModule.Get().ImportAssetTasks(Tasks);

	TArray<TSharedPtr<FJsonValue>> ImportedPaths;
	USoundWave* ImportedWave = nullptr;
	for (UObject* Obj : Task->GetObjects())
	{
		if (!Obj) continue;
		ImportedPaths.Add(MakeShared<FJsonValueString>(Obj->GetPathName()));
		if (!ImportedWave) ImportedWave = Cast<USoundWave>(Obj);
	}

	// Optional looping toggle on the resulting SoundWave.
	if (ImportedWave && Params->HasField(TEXT("looping")))
	{
		ImportedWave->bLooping = OptionalBool(Params, TEXT("looping"), false);
		SaveAssetPackage(ImportedWave);
	}

	auto Result = MCPSuccess();
	if (ImportedPaths.Num() > 0) MCPSetCreated(Result);
	Result->SetStringField(TEXT("filename"), FileName);
	Result->SetStringField(TEXT("destinationPath"), DestinationPath);
	Result->SetArrayField(TEXT("importedAssets"), ImportedPaths);
	Result->SetNumberField(TEXT("importedCount"), ImportedPaths.Num());
	Result->SetBoolField(TEXT("success"), ImportedPaths.Num() > 0);
	if (ImportedWave)
	{
		Result->SetNumberField(TEXT("durationSeconds"), ImportedWave->GetDuration());
		Result->SetNumberField(TEXT("numChannels"), ImportedWave->NumChannels);
		Result->SetBoolField(TEXT("looping"), ImportedWave->bLooping);
	}
	if (ImportedPaths.Num() == 0)
	{
		Result->SetStringField(TEXT("error"), TEXT("Import task completed but no SoundWave was produced (unsupported format?)"));
	}
	else if (ImportedPaths.Num() == 1)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ImportedPaths[0]->AsString());
		MCPSetRollback(Result, TEXT("delete_asset"), Payload);
	}
	return MCPResult(Result);
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

	USoundCueFactoryNew* SoundCueFactory = NewObject<USoundCueFactoryNew>();
	auto Created = MCPCreateAssetIdempotent<USoundCue>(Name, PackagePath, OnConflict, TEXT("SoundCue"), SoundCueFactory);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	UEditorAssetLibrary::SaveAsset(Created.Asset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), Created.Asset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Result, Created.Asset->GetPathName());

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

	const FVector Location = OptionalVec3(Params, TEXT("location"));

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

	if (auto Existing = MCPCheckActorLabelExists(World, Label, OnConflict, TEXT("AmbientSound")))
	{
		return Existing;
	}

	const FVector Location = OptionalVec3(Params, TEXT("location"));
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
