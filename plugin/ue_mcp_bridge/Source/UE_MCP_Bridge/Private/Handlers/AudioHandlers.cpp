#include "AudioHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerPagination.h"
#include "HandlerAssetCreate.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
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
#include "Misc/Base64.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/AmbientSound.h"
#include "Components/AudioComponent.h"
#include "EngineUtils.h"

void FAudioHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("list_sound_assets"), &ListSoundAssets);
	Registry.RegisterHandler(TEXT("extract_sound_wave_pcm"), &ExtractSoundWavePCM);
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
	Registry.RegisterHandler(TEXT("metasound_read_document"), &MetaSoundReadDocument);
	Registry.RegisterHandler(TEXT("metasound_list_connections"), &MetaSoundListConnections);
	Registry.RegisterHandler(TEXT("metasound_list_variables"), &MetaSoundListVariables);
	Registry.RegisterHandler(TEXT("metasound_search_nodes"), &MetaSoundSearchNodes);
	Registry.RegisterHandler(TEXT("metasound_inspect_node"), &MetaSoundInspectNode);
	Registry.RegisterHandler(TEXT("metasound_list_node_pins"), &MetaSoundListNodePins);
	Registry.RegisterHandler(TEXT("metasound_validate"), &MetaSoundValidate);

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

	// ── Authoring depth (AudioHandlers_Depth.cpp) ───────────────────────
	Registry.RegisterHandler(TEXT("metasound_remove_node"), &MetaSoundRemoveNode);
	Registry.RegisterHandler(TEXT("metasound_disconnect"), &MetaSoundDisconnect);
	Registry.RegisterHandler(TEXT("metasound_remove_member"), &MetaSoundRemoveMember);
	Registry.RegisterHandler(TEXT("metasound_rename_member"), &MetaSoundRenameMember);
	Registry.RegisterHandler(TEXT("soundcue_remove_node"), &SoundCueRemoveNode);
	Registry.RegisterHandler(TEXT("soundcue_disconnect"), &SoundCueDisconnect);
	Registry.RegisterHandler(TEXT("set_sound_class_parent"), &SetSoundClassParent);
	Registry.RegisterHandler(TEXT("read_sound_routing"), &ReadSoundRouting);
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

	// #730: the old implementation ignored `directory`, had no result cap, and
	// serialized SoundWave + SoundCue + MetaSoundSource for the whole project in
	// one response. On projects with hundreds of SoundWaves that response could
	// exceed the WebSocket framing threshold and drop the bridge. Honor the
	// directory, filter recursively via a single FARFilter query, and paginate.
	const FString Directory = OptionalString(Params, TEXT("directory"), TEXT("/Game"));
	const bool bRecursive = OptionalBool(Params, TEXT("recursive"), true);

	// T3: the row offset #730 introduced is replaced by the shared cursor. An
	// offset re-read a moved library at a row number and could not tell that it
	// had moved; a cursor names the row it resumes after and says so when that
	// row shifted or was deleted.
	if (Params.IsValid() && Params->HasField(TEXT("offset")))
	{
		return MCPError(TEXT(
			"'offset' is no longer how list_sound_assets pages, because a row number cannot tell you "
			"the library changed underneath it. Pass the 'nextCursor' this action returned as 'cursor', "
			"and size the page with 'limit' (1 to 5000, default 1000). Omit both for the first page."));
	}
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_sound_assets|directory=%s|recursive=%d"), *Directory, bRecursive ? 1 : 0),
			/*DefaultLimit*/ 1000, /*MaxLimit*/ 5000, Page))
	{
		return Err;
	}

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("SoundWave")));
	Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("SoundCue")));
	Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/MetasoundEngine"), TEXT("MetaSoundSource")));
	Filter.bRecursiveClasses = true;
	Filter.PackagePaths.Add(FName(*Directory));
	Filter.bRecursivePaths = bRecursive;

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	// Stable ordering so paging is deterministic across calls: the asset
	// registry does not promise an enumeration order, and a cursor over an
	// unordered enumeration cannot find its anchor again.
	AssetDataList.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return A.GetObjectPathString() < B.GetObjectPathString();
	});

	TArray<MCPPagination::FPageRow> Rows;
	Rows.Reserve(AssetDataList.Num());
	for (const FAssetData& AssetData : AssetDataList)
	{
		const FString ObjectPath = AssetData.GetObjectPathString();
		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		AssetObj->SetStringField(TEXT("path"), ObjectPath);
		AssetObj->SetStringField(TEXT("class"), AssetData.AssetClassPath.GetAssetName().ToString());
		AssetObj->SetStringField(TEXT("packagePath"), AssetData.PackagePath.ToString());
		// The object path is the anchor: unique across the project, and the
		// same string the next enumeration produces for that sound.
		Rows.Add({ ObjectPath, MakeShared<FJsonValueObject>(AssetObj) });
	}

	Result->SetStringField(TEXT("directory"), Directory);
	MCPPagination::EmitPage(Page, Rows, TEXT("assets"), Result);

	return MCPResult(Result);
}

// #729: decode a USoundWave's imported audio to in-memory PCM. UE Python does
// not expose USoundWave::GetImportedSoundWaveData, so a semantic-search pipeline
// (CLAP etc.) previously had no way to reach the samples without relying on the
// original import file, which may have moved. This returns interleaved signed
// 16-bit PCM, base64-encoded, plus the format metadata needed to feed a model.
TSharedPtr<FJsonValue> FAudioHandlers::ExtractSoundWavePCM(const TSharedPtr<FJsonObject>& Params)
{
	FString SoundPath;
	if (auto Err = RequireString(Params, TEXT("soundPath"), SoundPath)) return Err;

	USoundWave* Wave = LoadObject<USoundWave>(nullptr, *SoundPath);
	if (!Wave)
	{
		return MCPError(FString::Printf(TEXT("SoundWave not found: %s"), *SoundPath));
	}

#if WITH_EDITOR
	TArray<uint8> RawPCM;
	uint32 SampleRate = 0;
	uint16 NumChannels = 0;
	if (!Wave->GetImportedSoundWaveData(RawPCM, SampleRate, NumChannels)
		|| RawPCM.Num() == 0 || SampleRate == 0 || NumChannels == 0)
	{
		return MCPError(TEXT("Failed to decode imported SoundWave data (no editor source data available for this asset)"));
	}

	// RawPCM is interleaved signed 16-bit little-endian across NumChannels.
	int32 TotalFrames = (RawPCM.Num() / (int32)sizeof(int16)) / NumChannels;

	// Optional decode window so callers can bound the response size (CLAP-style
	// pipelines only need a few seconds). Default is the whole asset.
	const double MaxSeconds = OptionalNumber(Params, TEXT("maxSeconds"), 0.0);
	if (MaxSeconds > 0.0)
	{
		const int32 FrameCap = FMath::Clamp(FMath::FloorToInt(MaxSeconds * (double)SampleRate), 0, TotalFrames);
		TotalFrames = FrameCap;
	}

	const bool bDownmix = OptionalBool(Params, TEXT("downmixMono"), false);
	const int16* Samples = reinterpret_cast<const int16*>(RawPCM.GetData());

	TArray<uint8> OutBytes;
	int32 OutChannels = NumChannels;
	if (bDownmix && NumChannels > 1)
	{
		OutChannels = 1;
		OutBytes.SetNumUninitialized(TotalFrames * (int32)sizeof(int16));
		int16* Dst = reinterpret_cast<int16*>(OutBytes.GetData());
		for (int32 Frame = 0; Frame < TotalFrames; ++Frame)
		{
			int32 Acc = 0;
			for (int32 Ch = 0; Ch < NumChannels; ++Ch)
			{
				Acc += Samples[Frame * NumChannels + Ch];
			}
			Dst[Frame] = static_cast<int16>(Acc / NumChannels);
		}
	}
	else
	{
		const int32 ByteCount = TotalFrames * NumChannels * (int32)sizeof(int16);
		OutBytes.Append(RawPCM.GetData(), ByteCount);
	}

	const FString Base64 = FBase64::Encode(OutBytes);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("soundPath"), SoundPath);
	Result->SetNumberField(TEXT("sampleRate"), static_cast<double>(SampleRate));
	Result->SetNumberField(TEXT("numChannels"), static_cast<double>(OutChannels));
	Result->SetNumberField(TEXT("numFrames"), static_cast<double>(TotalFrames));
	Result->SetNumberField(TEXT("durationSeconds"), SampleRate > 0 ? static_cast<double>(TotalFrames) / static_cast<double>(SampleRate) : 0.0);
	Result->SetStringField(TEXT("format"), TEXT("pcm_s16le"));
	Result->SetStringField(TEXT("pcmBase64"), Base64);
	return MCPResult(Result);
#else
	return MCPError(TEXT("extract_sound_wave_pcm requires an editor build"));
#endif
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

	// No rollback: destructive/external - playing a one-shot sound has no inverse.
	// Replays produce a new audible event; not natural-key idempotent.
	UGameplayStatics::PlaySoundAtLocation(World, Sound, Location, static_cast<float>(Volume), static_cast<float>(Pitch));

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), SoundPath);
	// Stated in the result, not just in the comment above, so a flow can read
	// the answer rather than infer it from a missing field.
	MCPSetNoRollback(Result,
		TEXT("A one-shot sound is an event, not a state. It is already audible by the time this returns and nothing changed on disk or ")
		TEXT("in the level, so there is nothing to undo and no action that would undo it. Calling again plays it again."));

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
