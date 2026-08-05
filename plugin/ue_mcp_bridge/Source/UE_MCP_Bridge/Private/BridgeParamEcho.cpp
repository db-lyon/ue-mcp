#include "BridgeParamEcho.h"
#include "UE_MCP_BridgeModule.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeLock.h"

FMCPParamEcho& FMCPParamEcho::Get()
{
	static FMCPParamEcho Instance;
	return Instance;
}

bool FMCPParamEcho::ResolveEnabledFromEnvironment()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("MCPParamEcho")))
	{
		return true;
	}

	const FString EnvValue = FPlatformMisc::GetEnvironmentVariable(TEXT("UE_MCP_PARAM_ECHO"));
	return EnvValue == TEXT("1") || EnvValue.Equals(TEXT("true"), ESearchCase::IgnoreCase)
		|| EnvValue.Equals(TEXT("yes"), ESearchCase::IgnoreCase);
}

void FMCPParamEcho::SetEnabled(bool bInEnabled)
{
	const bool bWasEnabled = bEnabled;
	bEnabled = bInEnabled;

	if (bInEnabled && !bWasEnabled)
	{
		UE_LOG(LogMCPBridge, Warning,
			TEXT("[UE-MCP] Parameter echo is on. The bridge will remember the parameter NAMES of the last %d dispatches and hand them to any client that asks. This is a test facility; do not leave it on."),
			MaxEntries);
	}

	if (!bInEnabled)
	{
		Clear();
	}
}

void FMCPParamEcho::Record(const FString& Method, const TSharedPtr<FJsonObject>& Params)
{
	// Checked before the lock so a bridge with the echo off pays one atomic
	// read per dispatch and nothing else.
	if (!bEnabled)
	{
		return;
	}

	FMCPParamEchoEntry Entry;
	Entry.Method = Method;
	Entry.ReceivedAtUtc = FDateTime::UtcNow();

	if (Params.IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Params->Values)
		{
			// The key, never the value. See the header.
			Entry.ParamNames.Add(Pair.Key);
		}
		Entry.ParamNames.Sort();
	}

	FScopeLock Lock(&Mutex);
	Entries.Add(MoveTemp(Entry));
	if (Entries.Num() > MaxEntries)
	{
		Entries.RemoveAt(0, Entries.Num() - MaxEntries);
	}
}

TArray<FMCPParamEchoEntry> FMCPParamEcho::Snapshot() const
{
	FScopeLock Lock(&Mutex);
	return Entries;
}

void FMCPParamEcho::Clear()
{
	FScopeLock Lock(&Mutex);
	Entries.Reset();
}

TSharedPtr<FJsonObject> FMCPParamEcho::BuildPayload() const
{
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetBoolField(TEXT("success"), true);
	Payload->SetBoolField(TEXT("servedWithoutGameThread"), true);

	// Answered rather than refused when the echo is off, so a caller that has
	// checked the capability and a caller that has not both get a shape they
	// can read instead of an error they have to special-case.
	Payload->SetBoolField(TEXT("enabled"), bEnabled);

	const TArray<FMCPParamEchoEntry> Recorded = Snapshot();

	TArray<TSharedPtr<FJsonValue>> EntryValues;
	EntryValues.Reserve(Recorded.Num());
	for (const FMCPParamEchoEntry& Entry : Recorded)
	{
		TSharedPtr<FJsonObject> EntryObject = MakeShared<FJsonObject>();
		EntryObject->SetStringField(TEXT("method"), Entry.Method);
		EntryObject->SetStringField(TEXT("receivedAt"), Entry.ReceivedAtUtc.ToIso8601());

		TArray<TSharedPtr<FJsonValue>> NameValues;
		NameValues.Reserve(Entry.ParamNames.Num());
		for (const FString& Name : Entry.ParamNames)
		{
			NameValues.Add(MakeShared<FJsonValueString>(Name));
		}
		EntryObject->SetArrayField(TEXT("params"), NameValues);

		EntryValues.Add(MakeShared<FJsonValueObject>(EntryObject));
	}

	Payload->SetNumberField(TEXT("entryCount"), Recorded.Num());
	Payload->SetArrayField(TEXT("entries"), EntryValues);
	Payload->SetNumberField(TEXT("maxEntries"), MaxEntries);
	return Payload;
}
