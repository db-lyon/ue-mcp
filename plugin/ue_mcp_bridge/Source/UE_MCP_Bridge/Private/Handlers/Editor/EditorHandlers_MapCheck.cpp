// Map Check: run it and read its messages, and add messages to it (#1828).
//
// editor(run_map_check) runs the editor's Map Check on the open level, the same
// pass as Build > Map Check (the MAP CHECK console command, without its
// results window), and returns the messages it wrote to the MapCheck listing
// with the actor each one names. editor(get_message_log) reads a listing; this
// is the call that fills it.
//
// editor(log_map_check_message) adds a message to the MapCheck listing, which
// is what UMapCheckSubsystem::LogMapCheckError, LogMapCheckWarning and
// LogMapCheckInfo do on UE 5.9. Before 5.9 there is no subsystem, and the same
// message is written straight to the listing with FMessageLog, which is all
// those three functions do.
//
// Translation-unit partition of FEditorHandlers; registration is
// RegisterMapCheckHandlers below, called from EditorHandlers.cpp.

#include "EditorHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/UObjectToken.h"
#include "MessageLogModule.h"
#include "IMessageLogListing.h"
#include "Modules/ModuleManager.h"
#if UE_MCP_HAS_5_9_API
#include "Subsystems/MapCheckSubsystem.h"
#endif
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	const FName MCPMapCheckListing(TEXT("MapCheck"));

	const TCHAR* MCPMapCheckSeverityName(EMessageSeverity::Type Severity)
	{
		switch (Severity)
		{
			case EMessageSeverity::Error:              return TEXT("Error");
			case EMessageSeverity::PerformanceWarning: return TEXT("PerformanceWarning");
			case EMessageSeverity::Warning:            return TEXT("Warning");
			case EMessageSeverity::Info:               return TEXT("Info");
			default:                                   return TEXT("Unknown");
		}
	}

	/** One MapCheck message as {severity, text, objects[]}, objects being the paths its object tokens name. */
	TSharedPtr<FJsonObject> MCPMapCheckDescribe(const TSharedRef<FTokenizedMessage>& Message)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("severity"), MCPMapCheckSeverityName(Message->GetSeverity()));
		Row->SetStringField(TEXT("text"), Message->ToText().ToString());
		TArray<TSharedPtr<FJsonValue>> Objects;
		for (const TSharedRef<IMessageToken>& Token : Message->GetMessageTokens())
		{
			if (Token->GetType() != EMessageToken::Object) continue;
			const TSharedRef<FUObjectToken> ObjectToken = StaticCastSharedRef<FUObjectToken>(Token);
			if (const UObject* Object = ObjectToken->GetObject().Get())
			{
				Objects.Add(MakeShared<FJsonValueString>(Object->GetPathName()));
				if (const AActor* Actor = Cast<AActor>(Object))
				{
					if (!Row->HasField(TEXT("actorLabel"))) Row->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
				}
			}
		}
		Row->SetArrayField(TEXT("objects"), Objects);
		return Row;
	}
}


// editor(run_map_check): the MAP CHECK console command, then the messages it wrote.
TSharedPtr<FJsonValue> FEditorHandlers::RunMapCheck(const TSharedPtr<FJsonObject>& Params)
{
	const int32 MaxMessages = FMath::Clamp(OptionalInt(Params, TEXT("maxMessages"), 500), 1, 5000);
	const FString SeverityFilter = OptionalString(Params, TEXT("severity")).ToLower();
	REQUIRE_EDITOR_WORLD(World);
	if (!GEditor) return MCPError(TEXT("The editor is not available."));

	// Map_Check starts a new MapCheck page, so the listing holds this run's messages only.
	GEditor->Exec(World, TEXT("MAP CHECK DONTDISPLAYDIALOG"));

	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>(TEXT("MessageLog"));
	TSharedRef<IMessageLogListing> Listing = MessageLogModule.GetLogListing(MCPMapCheckListing);

	TArray<TSharedPtr<FJsonValue>> Rows;
	int32 Readable = 0;
	bool bTruncated = false;
	for (const TSharedRef<FTokenizedMessage>& Message : Listing->GetFilteredMessages())
	{
		++Readable;
		if (!SeverityFilter.IsEmpty() && !FString(MCPMapCheckSeverityName(Message->GetSeverity())).ToLower().Contains(SeverityFilter)) continue;
		if (Rows.Num() >= MaxMessages) { bTruncated = true; continue; }
		Rows.Add(MakeShared<FJsonValueObject>(MCPMapCheckDescribe(Message)));
	}

	// NumMessages counts every message at least as severe as the one it is given.
	const int32 Errors = Listing->NumMessages(EMessageSeverity::Error);
	const int32 WarningsOrWorse = Listing->NumMessages(EMessageSeverity::Warning);
	const int32 Total = Listing->NumMessages(EMessageSeverity::Info);
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("level"), World->GetOutermost()->GetName());
	Result->SetArrayField(TEXT("messages"), Rows);
	Result->SetNumberField(TEXT("errors"), Errors);
	Result->SetNumberField(TEXT("warnings"), WarningsOrWorse - Errors);
	Result->SetNumberField(TEXT("total"), Total);
	Result->SetBoolField(TEXT("truncated"), bTruncated);
	if (Readable < Total)
	{
		Result->SetStringField(TEXT("note"), FString::Printf(
			TEXT("The listing holds %d messages but only %d are readable: message bodies honour the Message Log tab's severity checkboxes. The counts above are the real totals."),
			Total, Readable));
	}
	return MCPResult(Result);
}


// editor(log_map_check_message): UMapCheckSubsystem::LogMapCheckError / Warning / Info.
TSharedPtr<FJsonValue> FEditorHandlers::LogMapCheckMessage(const TSharedPtr<FJsonObject>& Params)
{
	FString Severity;
	FString Message;
	if (auto Err = RequireString(Params, TEXT("severity"), Severity)) return Err;
	if (auto Err = RequireString(Params, TEXT("message"), Message)) return Err;
	FMCPActorSelector Selector;
	Selector.bRequired = false;
	UWorld* World = GetEditorWorld();
	TSharedPtr<FJsonValue> LookupError;
	AActor* Actor = MCPResolveActor(World, Params, LookupError, Selector);
	if (LookupError.IsValid()) return LookupError;

	Severity = Severity.ToLower();
	if (Severity != TEXT("error") && Severity != TEXT("warning") && Severity != TEXT("info"))
	{
		return MCPError(FString::Printf(TEXT("severity '%s' is not one of error, warning, info."), *Severity));
	}

	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>(TEXT("MessageLog"));
	TSharedRef<IMessageLogListing> Listing = MessageLogModule.GetLogListing(MCPMapCheckListing);
	const int32 Before = Listing->NumMessages(EMessageSeverity::Info);

#if UE_MCP_HAS_5_9_API
	if (Severity == TEXT("error")) UMapCheckSubsystem::LogMapCheckError(Actor, Message);
	else if (Severity == TEXT("warning")) UMapCheckSubsystem::LogMapCheckWarning(Actor, Message);
	else UMapCheckSubsystem::LogMapCheckInfo(Actor, Message);
	const TCHAR* EngineFunction = Severity == TEXT("error") ? TEXT("UMapCheckSubsystem::LogMapCheckError")
		: Severity == TEXT("warning") ? TEXT("UMapCheckSubsystem::LogMapCheckWarning")
		: TEXT("UMapCheckSubsystem::LogMapCheckInfo");
#else
	{
		FMessageLog MapCheckLog(MCPMapCheckListing);
		TSharedRef<FTokenizedMessage> Tokenized = Severity == TEXT("error") ? MapCheckLog.Error()
			: Severity == TEXT("warning") ? MapCheckLog.Warning()
			: MapCheckLog.Info();
		if (Actor) Tokenized->AddToken(FUObjectToken::Create(static_cast<const UObject*>(Actor)));
		Tokenized->AddToken(FTextToken::Create(FText::FromString(Message)));
	}
	const TCHAR* EngineFunction = TEXT("FMessageLog");
#endif

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("severity"), Severity);
	Result->SetStringField(TEXT("message"), Message);
	if (Actor)
	{
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	}
	Result->SetStringField(TEXT("engineFunction"), EngineFunction);
	Result->SetNumberField(TEXT("messagesBefore"), Before);
	Result->SetNumberField(TEXT("messagesAfter"), Listing->NumMessages(EMessageSeverity::Info));
	Result->SetBoolField(TEXT("changed"), true);
	MCPSetNoRollback(Result, TEXT("The Message Log has no call that removes one message; the next editor(run_map_check) starts a fresh MapCheck page without it."));
	return MCPResult(Result);
}


void FEditorHandlers::RegisterMapCheckHandlers(FMCPHandlerRegistry& Registry)
{
	using EType = EMCPParamType;
	Registry.RegisterHandler(TEXT("run_map_check"), &RunMapCheck, {
		MCPParam::Optional(TEXT("maxMessages"), EType::Integer, TEXT("Messages to return (default 500, max 5000)")),
		MCPParam::Optional(TEXT("severity"), EType::String, TEXT("Severity-name substring: Error | Warning | PerformanceWarning | Info")),
	});
	Registry.RegisterHandler(TEXT("log_map_check_message"), &LogMapCheckMessage, {
		MCPParam::Required(TEXT("severity"), EType::String, TEXT("error | warning | info")),
		MCPParam::Required(TEXT("message"), EType::String, TEXT("Message text")),
		MCPParam::Optional(TEXT("actorLabel"), EType::String, TEXT("Actor the message is about; the Message Log links to it")),
		MCPParam::Optional(TEXT("actorPath"), EType::String, TEXT("Object path of that actor; preferred over actorLabel when known")),
	});
}
