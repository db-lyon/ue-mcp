#pragma once

// A scripted characterisation run: build params, call handlers through a
// registry, record each response and compare the run with its fixture.

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HandlerRegistry.h"
#include "Misc/AutomationTest.h"
#include "Tests/MCPCharacterisation.h"

namespace UEMCPCharacterisation
{
	/** Params built from chained key/value pairs. */
	struct FArgs
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		FArgs& S(const TCHAR* Key, const FString& Value) { Obj->SetStringField(Key, Value); return *this; }
		FArgs& N(const TCHAR* Key, double Value) { Obj->SetNumberField(Key, Value); return *this; }
		FArgs& B(const TCHAR* Key, bool Value) { Obj->SetBoolField(Key, Value); return *this; }
		FArgs& O(const TCHAR* Key, const TSharedPtr<FJsonObject>& Value) { Obj->SetObjectField(Key, Value); return *this; }
		FArgs& A(const TCHAR* Key, const TArray<TSharedPtr<FJsonValue>>& Value) { Obj->SetArrayField(Key, Value); return *this; }
		FArgs& V(const TCHAR* Key, double X, double Y, double Z)
		{
			TSharedPtr<FJsonObject> Vec = MakeShared<FJsonObject>();
			Vec->SetNumberField(TEXT("x"), X);
			Vec->SetNumberField(TEXT("y"), Y);
			Vec->SetNumberField(TEXT("z"), Z);
			return O(Key, Vec);
		}
	};

	inline TArray<TSharedPtr<FJsonValue>> Strings(std::initializer_list<const TCHAR*> Items)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const TCHAR* Item : Items) Out.Add(MakeShared<FJsonValueString>(Item));
		return Out;
	}

	inline TArray<TSharedPtr<FJsonValue>> Objects(std::initializer_list<TSharedPtr<FJsonObject>> Items)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const TSharedPtr<FJsonObject>& Item : Items) Out.Add(MakeShared<FJsonValueObject>(Item));
		return Out;
	}

	/** One scripted run over a registry. */
	struct FScriptedRun
	{
		FMCPHandlerRegistry Registry;
		FRecording Recording;

		/** Call Method, record the response under Name and return it. */
		TSharedPtr<FJsonObject> Step(const TCHAR* Name, const TCHAR* Method, const FArgs& Args)
		{
			const TSharedPtr<FJsonObject> Result = Call(Method, Args);
			Recording.Add(Name, MakeShared<FJsonValueObject>(Result));
			return Result;
		}

		/** Call Method without recording, for setup and cleanup. */
		TSharedPtr<FJsonObject> Call(const TCHAR* Method, const FArgs& Args)
		{
			const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(Method, Args.Obj);
			return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : MakeShared<FJsonObject>();
		}

		/** Compare with tests/golden/cpp/<Fixture>.json, reporting through Test. */
		bool Finish(FAutomationTestBase& Test, const TCHAR* Fixture)
		{
			FString Note;
			const TArray<FString> Breaks = CompareWithFixture(Recording, Fixture, Note);
			if (!Note.IsEmpty()) Test.AddInfo(Note);
			for (const FString& Break : Breaks) Test.AddError(Break);
			return Breaks.Num() == 0;
		}
	};
}

#endif // WITH_DEV_AUTOMATION_TESTS
