#pragma once

#include "CoreMinimal.h"
#include "AssetDefinition.h"
#include "Dom/JsonObject.h"

/**
 * The revision one side of a native diff window shows: the checkpoint's name,
 * and its time as the date (#1606). An unset FRevisionInfo::Date is 1 January
 * of year 1, which the window prints as "Dec 31, 1" west of UTC.
 */
inline FRevisionInfo MCPCheckpointRevision(const FString& Label, const FString& TimeIso8601, const FDateTime& Fallback)
{
	FRevisionInfo Revision;
	Revision.Revision = Label;
	FDateTime Parsed;
	Revision.Date = (!TimeIso8601.IsEmpty() && FDateTime::ParseIso8601(*TimeIso8601, Parsed)) ? Parsed : Fallback;
	return Revision;
}

/** The revision as the handler reports it back: label and ISO 8601 UTC date. */
inline TSharedPtr<FJsonObject> MCPCheckpointRevisionJson(const FRevisionInfo& Revision)
{
	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("label"), Revision.Revision);
	Out->SetStringField(TEXT("date"), Revision.Date.ToIso8601());
	return Out;
}
