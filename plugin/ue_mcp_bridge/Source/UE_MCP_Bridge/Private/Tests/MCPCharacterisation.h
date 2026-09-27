#pragma once

// Characterisation fixtures (plans/domain-design.md 0 and 8): a scripted run of
// handler calls, each response reduced to canonical JSON, compared with a file
// recorded before a refactor. Canonical means sorted keys, GUIDs replaced by
// first-appearance tokens and machine paths replaced by placeholders. The
// comparison follows the response contract: every recorded key must still be
// there with the same value, and new keys are allowed.

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UEMCPCharacterisation
{
	/** Rewrites the volatile parts of a response: GUIDs and machine paths. */
	struct FNormaliser
	{
		TMap<FString, int32> GuidTokens;
		TArray<TPair<FString, FString>> Replacements;

		void Replace(const FString& From, const FString& To)
		{
			if (!From.IsEmpty()) Replacements.Emplace(From, To);
		}

		FString Text(const FString& In)
		{
			FString Out = In;
			for (const TPair<FString, FString>& Pair : Replacements)
			{
				Out = Out.Replace(*Pair.Key, *Pair.Value, ESearchCase::IgnoreCase);
			}
			static const FRegexPattern GuidPattern(TEXT("[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}|\\b[0-9A-F]{32}\\b"));
			FString Result;
			int32 Last = 0;
			FRegexMatcher Matcher(GuidPattern, Out);
			while (Matcher.FindNext())
			{
				const int32 Begin = Matcher.GetMatchBeginning();
				const int32 End = Matcher.GetMatchEnding();
				const FString Key = Out.Mid(Begin, End - Begin).Replace(TEXT("-"), TEXT("")).ToLower();
				int32* Token = GuidTokens.Find(Key);
				const int32 Id = Token ? *Token : GuidTokens.Add(Key, GuidTokens.Num());
				Result += Out.Mid(Last, Begin - Last) + FString::Printf(TEXT("<guid:%d>"), Id);
				Last = End;
			}
			return Result + Out.Mid(Last);
		}

		/** A normalised deep copy with object keys in sorted order. */
		TSharedPtr<FJsonValue> Value(const TSharedPtr<FJsonValue>& In)
		{
			if (!In.IsValid()) return MakeShared<FJsonValueNull>();
			switch (In->Type)
			{
			case EJson::String:
				return MakeShared<FJsonValueString>(Text(In->AsString()));
			case EJson::Array:
			{
				TArray<TSharedPtr<FJsonValue>> Items;
				for (const TSharedPtr<FJsonValue>& Item : In->AsArray()) Items.Add(Value(Item));
				return MakeShared<FJsonValueArray>(Items);
			}
			case EJson::Object:
			{
				const TSharedPtr<FJsonObject> Source = In->AsObject();
				TArray<FString> Keys;
				for (const auto& Pair : Source->Values) Keys.Add(FString(Pair.Key));
				Keys.Sort();
				TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
				for (const FString& Key : Keys) Out->SetField(Key, Value(Source->TryGetField(Key)));
				return MakeShared<FJsonValueObject>(Out);
			}
			default:
				return In;
			}
		}
	};

	inline FString ToText(const TSharedPtr<FJsonValue>& Value)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Value, FString(), Writer);
		return Out;
	}

	/** Where Actual breaks the contract Expected records, or empty when it keeps it. */
	inline FString FindBreak(const TSharedPtr<FJsonValue>& Expected, const TSharedPtr<FJsonValue>& Actual, const FString& Path)
	{
		if (!Actual.IsValid()) return Path + TEXT(": missing");
		if (Expected->Type != Actual->Type)
		{
			return FString::Printf(TEXT("%s: expected %s, got %s"), *Path, *ToText(Expected), *ToText(Actual));
		}
		if (Expected->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Actuals = Actual->AsObject();
			for (const auto& Pair : Expected->AsObject()->Values)
			{
				const FString Key(Pair.Key);
				const FString Break = FindBreak(Pair.Value, Actuals->TryGetField(Key), Path + TEXT(".") + Key);
				if (!Break.IsEmpty()) return Break;
			}
			return FString();
		}
		if (Expected->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& A = Expected->AsArray();
			const TArray<TSharedPtr<FJsonValue>>& B = Actual->AsArray();
			if (A.Num() != B.Num()) return FString::Printf(TEXT("%s: expected %d items, got %d"), *Path, A.Num(), B.Num());
			for (int32 i = 0; i < A.Num(); ++i)
			{
				const FString Break = FindBreak(A[i], B[i], FString::Printf(TEXT("%s[%d]"), *Path, i));
				if (!Break.IsEmpty()) return Break;
			}
			return FString();
		}
		return ToText(Expected) == ToText(Actual)
			? FString()
			: FString::Printf(TEXT("%s: expected %s, got %s"), *Path, *ToText(Expected), *ToText(Actual));
	}

	/** tests/golden/cpp/<Name>.json in the repo, reached from the test project. */
	inline FString FixturePath(const FString& Name)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../golden/cpp"), Name + TEXT(".json")));
	}

	/** An ordered record of one scripted run: step name to normalised response. */
	struct FRecording
	{
		FNormaliser Normaliser;
		TArray<TPair<FString, TSharedPtr<FJsonValue>>> Steps;

		void Add(const FString& Step, const TSharedPtr<FJsonValue>& Response)
		{
			Steps.Emplace(Step, Normaliser.Value(Response));
		}

		TSharedPtr<FJsonObject> ToObject() const
		{
			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Step : Steps) Out->SetField(Step.Key, Step.Value);
			return Out;
		}

		FString ToPrettyText() const
		{
			FString Out;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
			FJsonSerializer::Serialize(ToObject().ToSharedRef(), Writer);
			return Out;
		}
	};

	/**
	 * Compare a run with its fixture. A missing fixture is recorded from this run
	 * and reported through OutNote, which is how the pre-refactor baseline is
	 * made. A broken contract writes <fixture>.actual.json beside it for diffing.
	 */
	inline TArray<FString> CompareWithFixture(const FRecording& Run, const FString& Name, FString& OutNote)
	{
		TArray<FString> Breaks;
		const FString Path = FixturePath(Name);
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
			FFileHelper::SaveStringToFile(Run.ToPrettyText(), *Path);
			OutNote = FString::Printf(TEXT("No fixture at %s, so this run recorded it. Commit it before changing the handlers."), *Path);
			return Breaks;
		}

		TSharedPtr<FJsonObject> Expected;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Expected) || !Expected.IsValid())
		{
			Breaks.Add(FString::Printf(TEXT("%s is not valid JSON"), *Path));
			return Breaks;
		}

		const TSharedPtr<FJsonObject> Actual = Run.ToObject();
		for (const auto& Step : Expected->Values)
		{
			const FString Key(Step.Key);
			const FString Break = FindBreak(Step.Value, Actual->TryGetField(Key), Key);
			if (!Break.IsEmpty()) Breaks.Add(Break);
		}
		if (Breaks.Num() > 0)
		{
			FFileHelper::SaveStringToFile(Run.ToPrettyText(), *(FPaths::GetBaseFilename(Path, false) + TEXT(".actual.json")));
		}
		return Breaks;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
