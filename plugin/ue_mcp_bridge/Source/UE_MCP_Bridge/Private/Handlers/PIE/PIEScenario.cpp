#include "Handlers/PIE/PIEScenario.h"
#include "Handlers/PIE/PIEActorPuppet.h"
#include "Handlers/PIE/PIEPredicateEvaluator.h"
#include "Handlers/PIE/PIESequenceFormat.h"   // FindActorById
#include "GameFramework/Actor.h"
#include "Engine/World.h"

namespace MCPPIE
{
	namespace
	{
		void ReadArray(const TSharedPtr<FJsonObject>& Root, const TCHAR* Field, TArray<TSharedPtr<FJsonValue>>& Out)
		{
			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (Root->TryGetArrayField(Field, Arr) && Arr) Out = *Arr;
		}

		bool StepObject(const TSharedPtr<FJsonValue>& V, TSharedPtr<FJsonObject>& Out)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (V.IsValid() && V->TryGetObject(O) && O) { Out = *O; return true; }
			return false;
		}

		void ReadVec3(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, double& X, double& Y, double& Z)
		{
			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (!O->TryGetArrayField(Field, Arr) || !Arr) return;
			if (Arr->Num() > 0 && (*Arr)[0].IsValid()) X = (*Arr)[0]->AsNumber();
			if (Arr->Num() > 1 && (*Arr)[1].IsValid()) Y = (*Arr)[1]->AsNumber();
			if (Arr->Num() > 2 && (*Arr)[2].IsValid()) Z = (*Arr)[2]->AsNumber();
		}
	}

	bool FPIEScenario::Parse(const TSharedPtr<FJsonObject>& Root, FScenario& Out, FString& OutError)
	{
		if (!Root.IsValid()) { OutError = TEXT("scenario is not a JSON object"); return false; }
		Root->TryGetStringField(TEXT("name"), Out.Name);
		Root->TryGetStringField(TEXT("source_recording_dir"), Out.SourceRecordingDir);
		ReadArray(Root, TEXT("arrange"), Out.Arrange);
		ReadArray(Root, TEXT("act"), Out.Act);
		// Accept either 'assert' or 'assertions' (the test.json predicate key).
		ReadArray(Root, TEXT("assert"), Out.Assert);
		if (Out.Assert.Num() == 0) ReadArray(Root, TEXT("assertions"), Out.Assert);
		if (Out.Assert.Num() == 0) ReadArray(Root, TEXT("predicates"), Out.Assert);
		return true;
	}

	bool FPIEScenario::Validate(const FScenario& S, TArray<FString>& OutErrors)
	{
		if (S.Name.IsEmpty()) OutErrors.Add(TEXT("scenario has no 'name'"));

		ValidateArrange(S.Arrange, TEXT("arrange"), OutErrors);

		// Act steps. 'call' is validated per-kind like arrange's, because a
		// mistyped target or func in a timed step fails by silently never
		// firing - the worst failure mode a driver can have (#3).
		static const TSet<FString> KnownAct = { TEXT("replay"), TEXT("inject_tape"), TEXT("drive"), TEXT("goto"), TEXT("face"), TEXT("follow"), TEXT("call") };
		for (int32 i = 0; i < S.Act.Num(); ++i)
		{
			TSharedPtr<FJsonObject> O;
			if (!StepObject(S.Act[i], O)) { OutErrors.Add(FString::Printf(TEXT("act[%d] is not an object"), i)); continue; }
			FString Kind;
			O->TryGetStringField(TEXT("kind"), Kind);
			Kind = Kind.ToLower();
			if (!KnownAct.Contains(Kind))
			{
				OutErrors.Add(FString::Printf(TEXT("act[%d] unknown kind '%s'"), i, *Kind));
				continue;
			}
			if (Kind == TEXT("call"))
			{
				FString Tgt, Func;
				if (!O->TryGetStringField(TEXT("target"), Tgt) || Tgt.IsEmpty())
					OutErrors.Add(FString::Printf(TEXT("act[%d] call needs 'target'"), i));
				if (!O->TryGetStringField(TEXT("func"), Func) || Func.IsEmpty())
					OutErrors.Add(FString::Printf(TEXT("act[%d] call needs 'func'"), i));
				double At = 0.0;
				if (O->TryGetNumberField(TEXT("at_seconds"), At) && At < 0.0)
					OutErrors.Add(FString::Printf(TEXT("act[%d] call 'at_seconds' cannot be negative"), i));
			}
		}

		// Assert: must parse as predicates.
		if (S.Assert.Num() == 0)
		{
			OutErrors.Add(TEXT("scenario has no assertions"));
		}
		else
		{
			TArray<FPredicate> Preds;
			FString PErr;
			if (!FPIEPredicateEvaluator::ParsePredicates(S.Assert, Preds, PErr))
				OutErrors.Add(FString::Printf(TEXT("assert: %s"), *PErr));
		}

		return OutErrors.Num() == 0;
	}

	void FPIEScenario::ValidateArrange(const TArray<TSharedPtr<FJsonValue>>& Arrange, const FString& Where, TArray<FString>& OutErrors)
	{
		for (int32 i = 0; i < Arrange.Num(); ++i)
		{
			TSharedPtr<FJsonObject> O;
			if (!StepObject(Arrange[i], O)) { OutErrors.Add(FString::Printf(TEXT("%s[%d] is not an object"), *Where, i)); continue; }
			FString Kind;
			O->TryGetStringField(TEXT("kind"), Kind);
			Kind = Kind.ToLower();
			if (Kind == TEXT("spawn"))
			{
				FString C;
				if (!O->TryGetStringField(TEXT("class"), C) || C.IsEmpty())
					OutErrors.Add(FString::Printf(TEXT("%s[%d] spawn needs 'class'"), *Where, i));
			}
			else if (Kind == TEXT("set"))
			{
				FString Tgt, Path;
				if (!O->TryGetStringField(TEXT("target"), Tgt) || Tgt.IsEmpty())
					OutErrors.Add(FString::Printf(TEXT("%s[%d] set needs 'target'"), *Where, i));
				if (!O->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty())
					OutErrors.Add(FString::Printf(TEXT("%s[%d] set needs 'path'"), *Where, i));
				if (!O->HasField(TEXT("value")))
					OutErrors.Add(FString::Printf(TEXT("%s[%d] set needs 'value'"), *Where, i));
			}
			else if (Kind == TEXT("call"))
			{
				FString Tgt, Func;
				if (!O->TryGetStringField(TEXT("target"), Tgt) || Tgt.IsEmpty())
					OutErrors.Add(FString::Printf(TEXT("%s[%d] call needs 'target'"), *Where, i));
				if (!O->TryGetStringField(TEXT("func"), Func) || Func.IsEmpty())
					OutErrors.Add(FString::Printf(TEXT("%s[%d] call needs 'func'"), *Where, i));
			}
			else
			{
				OutErrors.Add(FString::Printf(TEXT("%s[%d] unknown kind '%s' (spawn|set|call)"), *Where, i, *Kind));
			}
		}
	}

	TSharedRef<FJsonObject> FPIEScenario::ToJson(const FScenario& S)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), S.Name);
		if (!S.SourceRecordingDir.IsEmpty()) O->SetStringField(TEXT("source_recording_dir"), S.SourceRecordingDir);
		O->SetArrayField(TEXT("arrange"), S.Arrange);
		O->SetArrayField(TEXT("act"), S.Act);
		O->SetArrayField(TEXT("assert"), S.Assert);
		return O;
	}

	bool FPIEScenario::ApplyArrange(UWorld* World, const FScenario& S, int32& OutApplied, FString& OutError)
	{
		OutApplied = 0;
		if (!World) { OutError = TEXT("no PIE world"); return false; }

		for (int32 i = 0; i < S.Arrange.Num(); ++i)
		{
			TSharedPtr<FJsonObject> O;
			if (!StepObject(S.Arrange[i], O)) { OutError = FString::Printf(TEXT("arrange[%d] not an object"), i); return false; }
			FString Kind;
			O->TryGetStringField(TEXT("kind"), Kind);
			Kind = Kind.ToLower();

			if (Kind == TEXT("spawn"))
			{
				FString Class;
				O->TryGetStringField(TEXT("class"), Class);
				double X = 0, Y = 0, Z = 0, P = 0, Yaw = 0, R = 0, SX = 1, SY = 1, SZ = 1;
				ReadVec3(O, TEXT("at"), X, Y, Z);
				ReadVec3(O, TEXT("rotation"), P, Yaw, R);
				ReadVec3(O, TEXT("scale"), SX, SY, SZ);
				const FTransform Xform(FRotator(P, Yaw, R), FVector(X, Y, Z), FVector(SX, SY, SZ));
				FString Err;
				if (!FPIEActorPuppet::SpawnActor(World, Class, Xform, Err))
				{
					OutError = FString::Printf(TEXT("arrange[%d] spawn: %s"), i, *Err);
					return false;
				}
			}
			else if (Kind == TEXT("set"))
			{
				FString Tgt, Path;
				O->TryGetStringField(TEXT("target"), Tgt);
				O->TryGetStringField(TEXT("path"), Path);
				AActor* A = FindActorById(World, Tgt);
				if (!A) { OutError = FString::Printf(TEXT("arrange[%d] set: actor '%s' not found"), i, *Tgt); return false; }
				FString Err;
				if (!FPIEActorPuppet::SetPropertyByPath(A, Path, O->Values.FindRef(TEXT("value")), Err))
				{
					OutError = FString::Printf(TEXT("arrange[%d] set: %s"), i, *Err);
					return false;
				}
			}
			else if (Kind == TEXT("call"))
			{
				FString Tgt, Func;
				O->TryGetStringField(TEXT("target"), Tgt);
				O->TryGetStringField(TEXT("func"), Func);
				AActor* A = FindActorById(World, Tgt);
				if (!A) { OutError = FString::Printf(TEXT("arrange[%d] call: actor '%s' not found"), i, *Tgt); return false; }
				TArray<TSharedPtr<FJsonValue>> Args;
				const TArray<TSharedPtr<FJsonValue>>* ArgArr = nullptr;
				if (O->TryGetArrayField(TEXT("args"), ArgArr) && ArgArr) Args = *ArgArr;
				FString Err;
				if (!FPIEActorPuppet::CallFunction(A, Func, Args, Err))
				{
					OutError = FString::Printf(TEXT("arrange[%d] call: %s"), i, *Err);
					return false;
				}
			}
			else
			{
				OutError = FString::Printf(TEXT("arrange[%d] unknown kind '%s'"), i, *Kind);
				return false;
			}
			++OutApplied;
		}
		return true;
	}
}
