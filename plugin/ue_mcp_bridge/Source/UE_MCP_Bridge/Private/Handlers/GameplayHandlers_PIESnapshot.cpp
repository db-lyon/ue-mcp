// pie_snapshot: full UProperty dump of a live PIE actor to JSON. Complements
// the per-frame tracked.jsonl sampler (pos/rot/vel only) with a one-shot
// "freeze this actor's state" capture that includes every BlueprintVisible
// UProperty plus an optional component dump.

#include "GameplayHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "JsonSerializer.h"
#include "PIE/PIESequenceFormat.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	using namespace UEMCPPIE;

	AActor* FindActorById(UWorld* World, const FString& Id)
	{
		if (!World || Id.IsEmpty()) return nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* A = *It;
			if (!A) continue;
			if (A->GetName() == Id) return A;
			const UClass* C = A->GetClass();
			if (C && (C->GetName() == Id || C->GetPathName() == Id)) return A;
			if (A->GetPathName().EndsWith(Id)) return A;
		}
		return nullptr;
	}

	FString JsonToString(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieSnapshot(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Target;
	if (auto E = RequireString(Params, TEXT("target"), Target)) return E;

	UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
	if (!World) return MCPError(TEXT("PIE not running"));

	AActor* Actor = FindActorById(World, Target);
	if (!Actor) return MCPError(FString::Printf(TEXT("Actor not found: %s"), *Target));

	const bool bIncludeComponents = OptionalBool(Params, TEXT("include_components"), true);
	const FString RecordingId = OptionalString(Params, TEXT("recording_id"));
	const FString Root = OptionalString(Params, TEXT("recording_dir"), DefaultRecordingsRoot());
	const FString DefaultName = FString::Printf(TEXT("snapshot-%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
	const FString Name = OptionalString(Params, TEXT("name"), DefaultName);

	// Serialize actor + components via the existing BlueprintVisible walker.
	TSharedPtr<FJsonObject> ActorJson = FMCPJsonSerializer::SerializeObject(Actor);
	if (!ActorJson.IsValid()) ActorJson = MakeShared<FJsonObject>();

	int32 ComponentCount = 0;
	if (bIncludeComponents)
	{
		TArray<TSharedPtr<FJsonValue>> Comps;
		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* C : Components)
		{
			if (!C) continue;
			TSharedPtr<FJsonObject> CJ = FMCPJsonSerializer::SerializeObject(C);
			if (CJ.IsValid()) Comps.Add(MakeShared<FJsonValueObject>(CJ));
			ComponentCount++;
		}
		ActorJson->SetArrayField(TEXT("__components"), Comps);
	}

	// Stamp the snapshot with a small header so consumers can identify it.
	TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetNumberField(TEXT("version"), 1);
	Out->SetStringField(TEXT("captured_at"), FDateTime::Now().ToString(TEXT("%Y-%m-%dT%H:%M:%S")));
	Out->SetStringField(TEXT("target"), Target);
	Out->SetStringField(TEXT("actor_class"), Actor->GetClass()->GetPathName());
	Out->SetStringField(TEXT("actor_path"), Actor->GetPathName());
	Out->SetNumberField(TEXT("component_count"), ComponentCount);
	Out->SetObjectField(TEXT("actor"), ActorJson);

	// Destination directory: <recording_dir>/<id>/snapshots/ when recording_id
	// is supplied; Saved/MCPSnapshots/ otherwise.
	FString Dir;
	if (!RecordingId.IsEmpty())
	{
		Dir = Root / RecordingId / TEXT("snapshots");
	}
	else
	{
		Dir = FPaths::ProjectSavedDir() / TEXT("MCPSnapshots");
	}
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString FullPath = Dir / (Name + TEXT(".json"));

	const FString Body = JsonToString(Out);
	if (!FFileHelper::SaveStringToFile(Body, *FullPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		return MCPError(FString::Printf(TEXT("write failed: %s"), *FullPath));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), FullPath);
	Result->SetStringField(TEXT("target"), Target);
	Result->SetStringField(TEXT("actor_class"), Actor->GetClass()->GetPathName());
	Result->SetStringField(TEXT("actor_path"), Actor->GetPathName());
	Result->SetNumberField(TEXT("component_count"), ComponentCount);
	return MCPResult(Result);
}
