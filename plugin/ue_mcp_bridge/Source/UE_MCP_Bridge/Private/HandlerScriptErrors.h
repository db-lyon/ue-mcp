#pragma once

// Collects the reasons an engine scripting function reports through the script
// exception handler while it is in scope.
//
// Several editor-scripting functions (the World Partition subsystem's setup
// calls, UEditorActorSubsystem::RebuildBrush and the actor replacement calls)
// return false or an empty result and report why through
// FScriptExceptionHandler, which Python turns into a RuntimeError and which a
// native caller otherwise sees only as a log line. Pushing a handler for the
// length of the call hands that reason to the bridge caller instead.

#include "CoreMinimal.h"
#include "Misc/CoreMisc.h"

class FMCPScopedScriptErrors
{
public:
	FMCPScopedScriptErrors()
		: Handler([this](ELogVerbosity::Type Verbosity, const TCHAR* Message, const TCHAR* /*Stack*/)
		{
			if (Message && *Message) Messages.Add(FString(Message));
		})
	{
	}

	FMCPScopedScriptErrors(const FMCPScopedScriptErrors&) = delete;
	FMCPScopedScriptErrors& operator=(const FMCPScopedScriptErrors&) = delete;

	/** Every reason reported so far, oldest first. */
	const TArray<FString>& GetMessages() const { return Messages; }

	bool HasMessages() const { return Messages.Num() > 0; }

	/** The reasons joined into one sentence-separated string, or Fallback when there were none. */
	FString Joined(const FString& Fallback = FString()) const
	{
		return Messages.Num() > 0 ? FString::Join(Messages, TEXT(" ")) : Fallback;
	}

private:
	TArray<FString> Messages;
	FScopedScriptExceptionHandler Handler;
};
