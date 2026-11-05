// Session log / error handlers: session_errors, session_log.
// Members of FPIEHandlers. Surfaces the Output Log captured during a PIE
// session (see FPIESessionLog) so an agent can ask "what errored" first.

#include "Handlers/PIE/PIEHandlers.h"
#include "HandlerUtils.h"
#include "Handlers/PIE/PIESessionLog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	using namespace MCPPIE;

	ELogVerbosity::Type ParseVerbosityFloor(const TSharedPtr<FJsonObject>& Params, ELogVerbosity::Type Default)
	{
		const FString S = OptionalString(Params, TEXT("min_verbosity")).ToLower();
		if (S == TEXT("error"))   return ELogVerbosity::Error;
		if (S == TEXT("warning")) return ELogVerbosity::Warning;
		if (S == TEXT("display")) return ELogVerbosity::Display;
		if (S == TEXT("log"))     return ELogVerbosity::Log;
		if (S == TEXT("verbose")) return ELogVerbosity::Verbose;
		return Default;
	}

	// Resolve the on-disk session directory for an explicit session param, or ""
	// when the caller wants the live/last in-memory session.
	FString ResolveSessionDir(const TSharedPtr<FJsonObject>& Params)
	{
		const FString Abs = OptionalString(Params, TEXT("session_dir"));
		if (!Abs.IsEmpty()) return Abs;
		const FString Id = OptionalString(Params, TEXT("session"));
		if (!Id.IsEmpty())
		{
			return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("MCPSessions") / Id;
		}
		return FString();
	}
}

TSharedPtr<FJsonValue> FPIEHandlers::PieSessionErrors(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	// session_dir overrides session, so session is read only without it.
	MCPReadParamsAhead(Params, { TEXT("session") });
	const ELogVerbosity::Type Floor = ParseVerbosityFloor(Params, ELogVerbosity::Warning);
	const FString Dir = ResolveSessionDir(Params);

	auto Result = MCPSuccess();

	if (Dir.IsEmpty())
	{
		// Live / most-recent in-memory session.
		FPIESessionLog& Log = FPIESessionLog::Get();
		const FString ActiveOrLast = Log.GetLastDir();
		TSharedPtr<FJsonObject> Summary = Log.BuildErrorSummary(Floor);
		Result->SetBoolField(TEXT("active"), Log.IsActive());
		Result->SetStringField(TEXT("session_dir"), ActiveOrLast);
		Result->SetObjectField(TEXT("errors"), Summary);
		return MCPResult(Result);
	}

	// On-disk finished session.
	FString JsonStr;
	const FString Path = Dir / TEXT("session_errors.json");
	if (!FFileHelper::LoadFileToString(JsonStr, *Path))
	{
		return MCPError(FString::Printf(TEXT("session_errors.json not found in %s"), *Dir));
	}
	TSharedPtr<FJsonObject> Obj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
	if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
	{
		return MCPError(TEXT("Failed to parse session_errors.json"));
	}
	Result->SetStringField(TEXT("session_dir"), Dir);
	Result->SetObjectField(TEXT("errors"), Obj);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieSessionLog(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	// session_dir overrides session, so session is read only without it.
	MCPReadParamsAhead(Params, { TEXT("session") });
	const ELogVerbosity::Type Floor = ParseVerbosityFloor(Params, ELogVerbosity::Log);
	const FName Category = FName(*OptionalString(Params, TEXT("category")));
	const FString Contains = OptionalString(Params, TEXT("contains"));
	const int32 Limit = OptionalInt(Params, TEXT("limit"), 200);
	const int32 Offset = OptionalInt(Params, TEXT("offset"), 0);
	const FString Dir = ResolveSessionDir(Params);

	auto Result = MCPSuccess();
	TArray<TSharedPtr<FJsonValue>> Rows;

	if (Dir.IsEmpty())
	{
		FPIESessionLog& Log = FPIESessionLog::Get();
		const TArray<FPIESessionLog::FLogLine> Lines =
			Log.QueryLines(Floor, Category, Contains, FMath::Max(0, Offset), FMath::Max(1, Limit));
		for (const FPIESessionLog::FLogLine& L : Lines)
		{
			TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
			J->SetNumberField(TEXT("time"), L.Time);
			J->SetNumberField(TEXT("frame"), static_cast<double>(L.Frame));
			J->SetStringField(TEXT("category"), L.Category.ToString());
			J->SetStringField(TEXT("message"), L.Message);
			Rows.Add(MakeShared<FJsonValueObject>(J));
		}
		Result->SetStringField(TEXT("session_dir"), Log.GetLastDir());
		Result->SetArrayField(TEXT("rows"), Rows);
		Result->SetNumberField(TEXT("count"), Rows.Num());
		return MCPResult(Result);
	}

	// On-disk: page session_log.jsonl with the same filters.
	FString JsonL;
	if (!FFileHelper::LoadFileToString(JsonL, *(Dir / TEXT("session_log.jsonl"))))
	{
		return MCPError(FString::Printf(TEXT("session_log.jsonl not found in %s"), *Dir));
	}
	TArray<FString> Lines;
	JsonL.ParseIntoArrayLines(Lines);
	int32 Skipped = 0;
	for (const FString& Ln : Lines)
	{
		if (Rows.Num() >= Limit) break;
		TSharedPtr<FJsonObject> Row;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Ln);
		if (!FJsonSerializer::Deserialize(R, Row) || !Row.IsValid()) continue;

		if (!Contains.IsEmpty())
		{
			FString Msg;
			Row->TryGetStringField(TEXT("message"), Msg);
			if (!Msg.Contains(Contains)) continue;
		}
		if (!Category.IsNone())
		{
			FString Cat;
			Row->TryGetStringField(TEXT("category"), Cat);
			if (Cat != Category.ToString()) continue;
		}
		if (Skipped < Offset) { Skipped++; continue; }
		Rows.Add(MakeShared<FJsonValueObject>(Row.ToSharedRef()));
	}
	Result->SetStringField(TEXT("session_dir"), Dir);
	Result->SetArrayField(TEXT("rows"), Rows);
	Result->SetNumberField(TEXT("count"), Rows.Num());
	return MCPResult(Result);
}
