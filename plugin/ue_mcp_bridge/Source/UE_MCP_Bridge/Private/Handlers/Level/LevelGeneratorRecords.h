#pragma once

// Small JSON records kept on a placed actor as one tag each, shared by the
// spline generators (the parameters a rebuild reuses) and the blockout swap
// (what a replaced blockout looked like, so it can be restored).
//
// A tag is the one place a level already persists free text per actor with no
// new class: a component or actor class of the bridge's own would make every
// saved level depend on the plugin being installed. A record is
// "<Prefix><compact json>" and stays under the FName length limit; a write
// that would not fit is refused rather than truncated.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

namespace MCPLevelRecords
{
	/** Longest tag written. FName holds NAME_SIZE - 1 characters. */
	constexpr int32 MaxTagLength = 1000;

	inline FString ToCompactJson(const TSharedPtr<FJsonObject>& Object)
	{
		FString Out;
		if (!Object.IsValid()) return Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Out;
	}

	inline TSharedPtr<FJsonObject> FromJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Object)) return nullptr;
		return Object;
	}

	/** The record stored under Prefix, or null when the actor carries none. */
	inline TSharedPtr<FJsonObject> Read(const AActor* Actor, const FString& Prefix)
	{
		if (!Actor) return nullptr;
		for (const FName& Tag : Actor->Tags)
		{
			const FString Text = Tag.ToString();
			if (Text.StartsWith(Prefix, ESearchCase::CaseSensitive))
			{
				return FromJson(Text.RightChop(Prefix.Len()));
			}
		}
		return nullptr;
	}

	/** Drop the record stored under Prefix. True when there was one. The caller has called Modify. */
	inline bool Remove(AActor* Actor, const FString& Prefix)
	{
		if (!Actor) return false;
		const int32 Removed = Actor->Tags.RemoveAll([&Prefix](const FName& Tag)
		{
			return Tag.ToString().StartsWith(Prefix, ESearchCase::CaseSensitive);
		});
		return Removed > 0;
	}

	/** Replace the record stored under Prefix. Refuses a record too long for a tag. The caller has called Modify. */
	inline bool Write(AActor* Actor, const FString& Prefix, const TSharedPtr<FJsonObject>& Record, FString& OutError)
	{
		if (!Actor) { OutError = TEXT("No actor to store the record on"); return false; }
		const FString Tag = Prefix + ToCompactJson(Record);
		if (Tag.Len() > MaxTagLength)
		{
			OutError = FString::Printf(TEXT("The record is %d characters and an actor tag holds at most %d"), Tag.Len(), MaxTagLength);
			return false;
		}
		Remove(Actor, Prefix);
		Actor->Tags.Add(FName(*Tag));
		return true;
	}
}
