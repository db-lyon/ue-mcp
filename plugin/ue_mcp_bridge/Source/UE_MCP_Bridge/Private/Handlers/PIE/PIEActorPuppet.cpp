#include "Handlers/PIE/PIEActorPuppet.h"
#include "HandlerFunctionCall.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"
#include "UObject/Class.h"

namespace MCPPIE
{
	namespace
	{
		// Render a JSON scalar into the text form FProperty::ImportText expects.
		FString JsonToImportString(const TSharedPtr<FJsonValue>& V)
		{
			if (!V.IsValid()) return FString();
			switch (V->Type)
			{
			case EJson::Boolean:
				return V->AsBool() ? TEXT("true") : TEXT("false");
			case EJson::Number:
			{
				const double D = V->AsNumber();
				if (FMath::IsNearlyEqual(D, FMath::RoundToDouble(D)))
					return FString::Printf(TEXT("%lld"), static_cast<int64>(D));
				return FString::SanitizeFloat(D);
			}
			case EJson::String:
				return V->AsString();
			default:
			{
				FString S;
				V->TryGetString(S);
				return S;
			}
			}
		}
	}

	bool FPIEActorPuppet::SetPropertyByPath(UObject* Root, const FString& Path,
	                                        const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		if (!Root) { OutError = TEXT("null root object"); return false; }
		if (Path.IsEmpty()) { OutError = TEXT("empty property path"); return false; }

		TArray<FString> Parts;
		Path.ParseIntoArray(Parts, TEXT("."));
		if (Parts.Num() == 0) { OutError = TEXT("empty property path"); return false; }

		UStruct* CurrentStruct = Root->GetClass();
		void* CurrentContainer = Root;
		UObject* OwnerObject = Root;   // nearest owning UObject, for ImportText
		FProperty* Property = nullptr;

		for (int32 i = 0; i < Parts.Num(); ++i)
		{
			FProperty* Seg = CurrentStruct->FindPropertyByName(FName(*Parts[i]));

			// Head-position fallback: a bare component name on an actor.
			if (!Seg && i == 0)
			{
				if (AActor* AsActor = Cast<AActor>(Root))
				{
					UActorComponent* Match = nullptr;
					for (UActorComponent* C : AsActor->GetComponents())
					{
						if (C && C->GetName() == Parts[i]) { Match = C; break; }
					}
					if (Match)
					{
						CurrentContainer = Match;
						CurrentStruct = Match->GetClass();
						OwnerObject = Match;
						continue;
					}
				}
			}
			if (!Seg)
			{
				OutError = FString::Printf(TEXT("property '%s' not found on %s"), *Parts[i], *CurrentStruct->GetName());
				return false;
			}

			if (i < Parts.Num() - 1)
			{
				if (FStructProperty* SP = CastField<FStructProperty>(Seg))
				{
					CurrentContainer = SP->ContainerPtrToValuePtr<void>(CurrentContainer);
					CurrentStruct = SP->Struct;
				}
				else if (FObjectProperty* OP = CastField<FObjectProperty>(Seg))
				{
					UObject* Sub = OP->GetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(CurrentContainer));
					if (!Sub) { OutError = FString::Printf(TEXT("'%s' is null; cannot descend"), *Parts[i]); return false; }
					CurrentContainer = Sub;
					CurrentStruct = Sub->GetClass();
					OwnerObject = Sub;
				}
				else
				{
					OutError = FString::Printf(TEXT("'%s' is not a struct/object; cannot descend"), *Parts[i]);
					return false;
				}
			}
			else
			{
				Property = Seg;
			}
		}

		if (!Property) { OutError = TEXT("could not resolve leaf property"); return false; }

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(CurrentContainer);
		const FString ImportStr = JsonToImportString(Value);
		const TCHAR* Result = Property->ImportText_Direct(*ImportStr, ValuePtr, OwnerObject, PPF_None);
		if (Result == nullptr)
		{
			OutError = FString::Printf(TEXT("could not coerce '%s' into %s (%s)"),
				*ImportStr, *Property->GetName(), *Property->GetClass()->GetName());
			return false;
		}
		return true;
	}

	UClass* FPIEActorPuppet::ResolveActorClass(const FString& ClassPath, FString& OutError)
	{
		if (ClassPath.IsEmpty()) { OutError = TEXT("empty class path"); return nullptr; }

		UClass* Class = LoadObject<UClass>(nullptr, *ClassPath);
		if (!Class)
		{
			// Allow a bare Blueprint path without the _C generated-class suffix.
			Class = LoadObject<UClass>(nullptr, *(ClassPath + TEXT("_C")));
		}
		if (!Class) { OutError = FString::Printf(TEXT("class not found: %s"), *ClassPath); return nullptr; }
		if (!Class->IsChildOf(AActor::StaticClass()))
		{
			OutError = FString::Printf(TEXT("%s is not an AActor"), *ClassPath);
			return nullptr;
		}
		return Class;
	}

	AActor* FPIEActorPuppet::SpawnActor(UWorld* World, const FString& ClassPath,
	                                    const FTransform& Xform, FString& OutError)
	{
		if (!World) { OutError = TEXT("no PIE world"); return nullptr; }
		UClass* Class = ResolveActorClass(ClassPath, OutError);
		if (!Class) return nullptr;

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(Class, Xform, SpawnParams);
		if (!Actor) { OutError = FString::Printf(TEXT("spawn failed for %s"), *ClassPath); return nullptr; }
		return Actor;
	}

	bool FPIEActorPuppet::CallFunction(UObject* Target, const FString& FuncName,
	                                   const TArray<TSharedPtr<FJsonValue>>& Args, FString& OutError,
	                                   bool* bOutUsedEditorGuard)
	{
		if (!Target) { OutError = TEXT("null target"); return false; }
		UFunction* Function = Target->FindFunction(FName(*FuncName));
		if (!Function) { OutError = FString::Printf(TEXT("function '%s' not found on %s"), *FuncName, *Target->GetClass()->GetName()); return false; }

		uint8* Params = static_cast<uint8*>(FMemory::Malloc(FMath::Max<int32>(Function->ParmsSize, 1)));
		FMemory::Memzero(Params, Function->ParmsSize);
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Params);
		}

		bool bOk = true;
		int32 ArgIdx = 0;
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			FProperty* Prm = *It;
			if (Prm->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm)) continue;   // skip outputs
			if (ArgIdx >= Args.Num()) break;   // fewer args than params: leave defaults

			const FString ImportStr = JsonToImportString(Args[ArgIdx++]);
			void* ValuePtr = Prm->ContainerPtrToValuePtr<void>(Params);
			if (Prm->ImportText_Direct(*ImportStr, ValuePtr, Target, PPF_None) == nullptr)
			{
				OutError = FString::Printf(TEXT("arg %d ('%s') could not coerce '%s'"), ArgIdx - 1, *Prm->GetName(), *ImportStr);
				bOk = false;
				break;
			}
		}

		if (bOk)
		{
			bool bUsedEditorGuard = false;
			MCPFunctionCall::ProcessEventInOwnContext(Target, Function, Params, bUsedEditorGuard);
			if (bOutUsedEditorGuard) *bOutUsedEditorGuard = bUsedEditorGuard;
		}

		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(Params);
		}
		FMemory::Free(Params);
		return bOk;
	}
}
