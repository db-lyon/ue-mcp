#pragma once

// Calling engine UFUNCTIONs this module does not link: a static UFUNCTION invoked
// by reflection, and the UToolCallAsyncResult it may hand back read by property
// name. Used by the epic handlers for the ToolsetRegistry and the AI Assistant.

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/UnrealType.h"

namespace MCPStaticInvoke
{
	/** Call static UFUNCTION FuncName on Cls with FString inputs keyed by parameter
	 *  name. ReadOut sees the parm buffer before it is destroyed. False when the
	 *  class or the function is missing. */
	inline bool CallWithStrings(
		UClass* Cls,
		const TCHAR* FuncName,
		const TMap<FString, FString>& StringArgs,
		TFunctionRef<void(UFunction*, uint8*)> ReadOut)
	{
		if (!Cls) return false;
		UFunction* Func = Cls->FindFunctionByName(FName(FuncName));
		if (!Func) return false;
		UObject* CDO = Cls->GetDefaultObject();
		if (!CDO) return false;

		// Zeroed memory is a valid empty state for FString, bool and object parms.
		uint8* Buffer = (uint8*)FMemory_Alloca(FMath::Max<int32>(Func->ParmsSize, 1));
		FMemory::Memzero(Buffer, Func->ParmsSize);

		for (const TPair<FString, FString>& Arg : StringArgs)
		{
			if (FStrProperty* SP = CastField<FStrProperty>(Func->FindPropertyByName(FName(*Arg.Key))))
			{
				SP->SetPropertyValue_InContainer(Buffer, Arg.Value);
			}
		}

		CDO->ProcessEvent(Func, Buffer);
		ReadOut(Func, Buffer);

		for (TFieldIterator<FProperty> It(Func); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Parm))
			{
				It->DestroyValue_InContainer(Buffer);
			}
		}
		return true;
	}

	/** The object a function returned, read from its parm buffer. */
	inline UObject* ReturnedObject(UFunction* Func, uint8* Buffer)
	{
		for (TFieldIterator<FProperty> It(Func); It; ++It)
		{
			if (!It->HasAnyPropertyFlags(CPF_ReturnParm)) continue;
			if (FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(*It))
			{
				return OP->GetObjectPropertyValue_InContainer(Buffer);
			}
		}
		return nullptr;
	}

	/** What a UToolCallAsyncResult holds: bIsComplete, Error, and a string Value. */
	struct FAsyncResultState
	{
		bool bComplete = false;
		FString Error;
		FString Value;
	};

	inline FAsyncResultState ReadAsyncResult(UObject* Result)
	{
		FAsyncResultState State;
		if (!Result) return State;
		UClass* RC = Result->GetClass();
		if (FBoolProperty* P = CastField<FBoolProperty>(RC->FindPropertyByName(TEXT("bIsComplete"))))
		{
			State.bComplete = P->GetPropertyValue_InContainer(Result);
		}
		if (FStrProperty* P = CastField<FStrProperty>(RC->FindPropertyByName(TEXT("Error"))))
		{
			State.Error = P->GetPropertyValue_InContainer(Result);
		}
		if (FStrProperty* P = CastField<FStrProperty>(RC->FindPropertyByName(TEXT("Value"))))
		{
			State.Value = P->GetPropertyValue_InContainer(Result);
		}
		return State;
	}
}
