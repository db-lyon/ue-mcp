// Declarative scenario handlers (Roadmap v2, F2): scenario_scaffold, scenario_validate.
// The committed arrange/act/assert unit. scenario_run (live orchestration of arrange
// + act + assert) lands with the increment that can verify it in a real game.

#include "Handlers/PIE/PIEHandlers.h"
#include "HandlerUtils.h"
#include "Handlers/PIE/PIEScenario.h"
#include "Handlers/PIE/PIEPredicateEvaluator.h"
#include "Handlers/PIE/PIESequenceFormat.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	using namespace MCPPIE;

	FString ResolveFolder(const TSharedPtr<FJsonObject>& Params)
	{
		const FString Id = OptionalString(Params, TEXT("recording_id"));
		const FString RecDir = OptionalString(Params, TEXT("recording_dir"));
		FString Folder;
		if (!RecDir.IsEmpty() && Id.IsEmpty()) Folder = RecDir;
		else Folder = (RecDir.IsEmpty() ? (FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("MCPRecordings")) : RecDir) / Id;
		Folder.RemoveFromEnd(TEXT("/"));
		Folder.RemoveFromEnd(TEXT("\\"));
		return Folder;
	}

	FString SeriesCsv(const FString& Folder)
	{
		const FString Obs = Folder / TEXT("observation.csv");
		if (FPaths::FileExists(Obs)) return Obs;
		return Folder / TEXT("recording.csv");
	}

	TSharedRef<FJsonObject> ValidationBlock(const FScenario& S)
	{
		TArray<FString> Errors;
		const bool bValid = FPIEScenario::Validate(S, Errors);
		TSharedRef<FJsonObject> V = MakeShared<FJsonObject>();
		V->SetBoolField(TEXT("valid"), bValid);
		V->SetNumberField(TEXT("arrange_count"), S.Arrange.Num());
		V->SetNumberField(TEXT("act_count"), S.Act.Num());
		V->SetNumberField(TEXT("assert_count"), S.Assert.Num());
		TArray<TSharedPtr<FJsonValue>> Errs;
		for (const FString& E : Errors) Errs.Add(MakeShared<FJsonValueString>(E));
		V->SetArrayField(TEXT("errors"), Errs);
		return V;
	}
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScenarioScaffold(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const FString Folder = ResolveFolder(Params);
	const FString Csv = SeriesCsv(Folder);
	if (!FPaths::FileExists(Csv))
	{
		return MCPError(FString::Printf(TEXT("No series (observation.csv/recording.csv) in %s; scaffold from a real run"), *Folder));
	}

	const FString Name = OptionalString(Params, TEXT("name"), TEXT("scenario"));
	const int32 MaxErrors = static_cast<int32>(OptionalNumber(Params, TEXT("max_errors"), 0.0));

	FScenario S;
	S.Name = Name;
	S.SourceRecordingDir = Folder;
	// Act: replay the source recording by default (one source of act steps).
	TSharedRef<FJsonObject> ReplayStep = MakeShared<FJsonObject>();
	ReplayStep->SetStringField(TEXT("kind"), TEXT("replay"));
	ReplayStep->SetStringField(TEXT("recording_dir"), Folder);
	S.Act.Add(MakeShared<FJsonValueObject>(ReplayStep));
	// Assert: derived starter predicates (shared with test_scaffold).
	S.Assert = FPIEPredicateEvaluator::DeriveStarterPredicates(Csv, MaxErrors);

	TSharedRef<FJsonObject> ScenarioJson = FPIEScenario::ToJson(S);

	const FString Dir = Folder / TEXT("scenarios");
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString Path = Dir / (Name + TEXT(".json"));

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(ScenarioJson, W);
	if (!FFileHelper::SaveStringToFile(Out, *Path))
	{
		return MCPError(FString::Printf(TEXT("Failed to write %s"), *Path));
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("scenario_path"), Path);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetObjectField(TEXT("scenario"), ScenarioJson);
	Result->SetObjectField(TEXT("validation"), ValidationBlock(S));
	Result->SetStringField(TEXT("next"), TEXT("Edit arrange/act into intent and the assert predicates into real expectations, then scenario_validate."));
	MCPSetNoRollback(Result, TEXT("scenario.json is written beside the recording; record_delete removes the recording and its scenario"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScenarioValidate(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	TSharedPtr<FJsonObject> Root;
	const FString Path = OptionalString(Params, TEXT("scenario_path"));
	if (!Path.IsEmpty())
	{
		Root = LoadJsonFile(Path);
		if (!Root.IsValid()) return MCPError(FString::Printf(TEXT("scenario not found/parseable: %s"), *Path));
	}
	else
	{
		const TSharedPtr<FJsonObject>* Inline = nullptr;
		if (TryGetObjectParam(Params, TEXT("scenario"), Inline) && Inline) Root = *Inline;
	}
	if (!Root.IsValid())
	{
		return MCPError(TEXT("scenario_validate needs 'scenario_path' or an inline 'scenario' object"));
	}

	FScenario S;
	FString Err;
	if (!FPIEScenario::Parse(Root, S, Err))
	{
		return MCPError(Err);
	}

	auto Result = MCPSuccess();
	if (!Path.IsEmpty()) Result->SetStringField(TEXT("scenario_path"), Path);
	Result->SetStringField(TEXT("name"), S.Name);
	Result->SetObjectField(TEXT("validation"), ValidationBlock(S));
	return MCPResult(Result);
}
