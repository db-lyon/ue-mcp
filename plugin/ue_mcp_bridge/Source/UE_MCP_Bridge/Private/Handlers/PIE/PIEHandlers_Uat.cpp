// Multiplayer acceptance runs: uat_run starts one, uat_status reports it, uat_abort ends it. See PIEUatRunner.h for
// the scenario format.

#include "Handlers/PIE/PIEHandlers.h"
#include "HandlerUtils.h"
#include "Handlers/PIE/PIEUatRunner.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

TSharedPtr<FJsonValue> FPIEHandlers::PieUatRun(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	TSharedPtr<FJsonObject> Scenario;
	const TSharedPtr<FJsonObject>* Inline = nullptr;
	FString Path;
	if (TryGetObjectParam(Params, TEXT("scenario"), Inline))
	{
		Scenario = *Inline;
	}
	else if (TryGetStringParam(Params, TEXT("path"), Path))
	{
		FString Full = FPaths::IsRelative(Path) ? FPaths::Combine(FPaths::ProjectDir(), Path) : Path;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Full))
		{
			return MCPError(FString::Printf(TEXT("uat_run: cannot read %s"), *Full));
		}
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Scenario) || !Scenario.IsValid())
		{
			return MCPError(FString::Printf(TEXT("uat_run: %s is not valid JSON"), *Full));
		}
	}
	else
	{
		return MCPError(TEXT("uat_run: pass scenario (object) or path (JSON file, relative to the project)"));
	}

	FString Err;
	if (!MCPPIE::FPIEUatRunner::Get().Start(Scenario, Err))
	{
		return MCPError(Err);
	}
	auto Result = MCPSuccess();
	Result->SetObjectField(TEXT("status"), MCPPIE::FPIEUatRunner::Get().Status());
	Result->SetStringField(TEXT("poll"), TEXT("uat_status until state is finished; the verdict and report_dir are there"));
	MCPSetCreated(Result);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	MCPSetRollback(Result, TEXT("pie_uat_abort"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieUatStatus(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	auto Result = MCPSuccess();
	Result->SetObjectField(TEXT("status"), MCPPIE::FPIEUatRunner::Get().Status());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieUatAbort(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const bool bWasRunning = MCPPIE::FPIEUatRunner::Get().IsRunning();
	MCPPIE::FPIEUatRunner::Get().Abort(OptionalString(Params, TEXT("reason"), TEXT("aborted by request")));
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("was_running"), bWasRunning);
	Result->SetBoolField(TEXT("alreadyStopped"), !bWasRunning);
	MCPSetNoRollback(Result, TEXT("An aborted run cannot be resumed; uat_run starts it again"));
	return MCPResult(Result);
}
