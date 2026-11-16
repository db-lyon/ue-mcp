#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeLock.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/**
 * Collects the log lines a long engine call writes while it is in scope, so a
 * handler can quote the engine's own warnings instead of pointing at the
 * output log. Lines whose category starts with one of the prefixes are kept;
 * an empty prefix list keeps warnings and errors from every category.
 */
class FMCPScopedLogCapture : public FOutputDevice
{
public:
	explicit FMCPScopedLogCapture(TArray<FString> InCategoryPrefixes, int32 InMaxLines = 200)
		: CategoryPrefixes(MoveTemp(InCategoryPrefixes)), MaxLines(InMaxLines)
	{
		if (GLog) GLog->AddOutputDevice(this);
	}
	virtual ~FMCPScopedLogCapture() override { if (GLog) GLog->RemoveOutputDevice(this); }
	FMCPScopedLogCapture(const FMCPScopedLogCapture&) = delete;
	FMCPScopedLogCapture& operator=(const FMCPScopedLogCapture&) = delete;

	struct FLine
	{
		FString Category;
		FString Verbosity;
		FString Message;
	};

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
	{
		if (!V) return;
		const ELogVerbosity::Type Level = (ELogVerbosity::Type)(Verbosity & ELogVerbosity::VerbosityMask);
		if (Level == ELogVerbosity::NoLogging) return;
		const FString Cat = Category.ToString();
		bool bKeep = false;
		if (CategoryPrefixes.Num() == 0)
		{
			bKeep = Level <= ELogVerbosity::Warning;
		}
		else
		{
			for (const FString& Prefix : CategoryPrefixes)
			{
				if (Cat.StartsWith(Prefix, ESearchCase::IgnoreCase)) { bKeep = true; break; }
			}
		}
		if (!bKeep) return;
		FScopeLock Lock(&Guard);
		// Informational lines share a separate budget, so chatter cannot crowd out a warning.
		const bool bProblem = Level <= ELogVerbosity::Warning;
		int32& Count = bProblem ? ProblemCount : InfoCount;
		if (Count >= MaxLines) { ++Dropped; return; }
		++Count;
		FLine Line;
		Line.Category = Cat;
		Line.Verbosity = Level == ELogVerbosity::Fatal || Level == ELogVerbosity::Error ? TEXT("Error")
			: Level == ELogVerbosity::Warning ? TEXT("Warning") : TEXT("Log");
		Line.Message = FString(V).Left(2000);
		Lines.Add(MoveTemp(Line));
	}
	virtual bool CanBeUsedOnAnyThread() const override { return true; }

	TArray<FLine> GetLines() const
	{
		FScopeLock Lock(&Guard);
		return Lines;
	}

	/** True when any captured line contains Needle, case-insensitively. */
	bool Contains(const TCHAR* Needle) const
	{
		FScopeLock Lock(&Guard);
		return Lines.ContainsByPredicate([Needle](const FLine& L) { return L.Message.Contains(Needle, ESearchCase::IgnoreCase); });
	}

	/** Warnings and errors as {category, verbosity, message}, plus how many were dropped. */
	TArray<TSharedPtr<FJsonValue>> ProblemsJson() const
	{
		FScopeLock Lock(&Guard);
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FLine& L : Lines)
		{
			if (L.Verbosity == TEXT("Log")) continue;
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("category"), L.Category);
			Obj->SetStringField(TEXT("verbosity"), L.Verbosity);
			Obj->SetStringField(TEXT("message"), L.Message);
			Out.Add(MakeShared<FJsonValueObject>(Obj));
		}
		return Out;
	}

	int32 GetDropped() const
	{
		FScopeLock Lock(&Guard);
		return Dropped;
	}

private:
	TArray<FString> CategoryPrefixes;
	int32 MaxLines;
	mutable FCriticalSection Guard;
	TArray<FLine> Lines;
	int32 ProblemCount = 0;
	int32 InfoCount = 0;
	int32 Dropped = 0;
};
