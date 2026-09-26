#pragma once

// Shared machinery for the UFUNCTION-invoking handlers (invoke_function,
// invoke_object_function, invoke_static_function), and the normalizers for the
// `args` those actions and run_python_file take.
//
// It lives in a header rather than a file-local helper in one of the handler
// .cpp files because the module is compiled as a unity build: two translation
// units that each defined a helper of this name would merge their anonymous
// namespaces and the second definition would be a redefinition.

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/Class.h"
#include "UObject/EnumProperty.h"
#include "UObject/Script.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HandlerResult.h"
#include "HandlerParams.h"

namespace MCPFunctionCall
{
	/**
	 * Typed JSON for one value inside a container. Recursive, so a
	 * TArray<TArray<int32>> or a TMap<FName, TArray<FString>> comes back whole.
	 */
	inline TSharedPtr<FJsonValue> ValueToJson(FProperty* Prop, const void* ValuePtr, UObject* Parent);

	/** Leaf marshalling for container elements: everything that is not a container. */
	inline TSharedPtr<FJsonValue> ElementToJson(FProperty* Prop, const void* ValuePtr, UObject* Parent)
	{
		if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			return MakeShared<FJsonValueBoolean>(BoolProp->GetPropertyValue(ValuePtr));
		}
		if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
		{
			return MakeShared<FJsonValueString>(StrProp->GetPropertyValue(ValuePtr));
		}
		if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
		{
			return MakeShared<FJsonValueString>(NameProp->GetPropertyValue(ValuePtr).ToString());
		}
		if (FTextProperty* TextProp = CastField<FTextProperty>(Prop))
		{
			return MakeShared<FJsonValueString>(TextProp->GetPropertyValue(ValuePtr).ToString());
		}
		// Enums report their name, not their ordinal: a bare number in a result
		// is unreadable and a caller cannot map it back without the UEnum.
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			const int64 Raw = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
			if (UEnum* Enum = EnumProp->GetEnum())
			{
				return MakeShared<FJsonValueString>(Enum->GetNameStringByValue(Raw));
			}
			return MakeShared<FJsonValueNumber>(static_cast<double>(Raw));
		}
		if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			const uint8 Raw = ByteProp->GetPropertyValue(ValuePtr);
			if (UEnum* Enum = ByteProp->Enum)
			{
				return MakeShared<FJsonValueString>(Enum->GetNameStringByValue(static_cast<int64>(Raw)));
			}
			return MakeShared<FJsonValueNumber>(static_cast<double>(Raw));
		}
		if (FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
		{
			if (NumProp->IsFloatingPoint())
			{
				return MakeShared<FJsonValueNumber>(NumProp->GetFloatingPointPropertyValue(ValuePtr));
			}
			return MakeShared<FJsonValueNumber>(static_cast<double>(NumProp->GetSignedIntPropertyValue(ValuePtr)));
		}
		// Soft references before hard ones: a soft pointer derives from
		// FObjectPropertyBase but answers null while its target is unloaded,
		// which would report a live reference as None (#598).
		if (FSoftObjectProperty* SoftProp = CastField<FSoftObjectProperty>(Prop))
		{
			const FString Path = SoftProp->GetPropertyValue(ValuePtr).ToString();
			return MakeShared<FJsonValueString>(Path.IsEmpty() ? FString(TEXT("None")) : Path);
		}
		if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
		{
			UObject* Referenced = ObjProp->GetObjectPropertyValue(ValuePtr);
			if (!Referenced) return MakeShared<FJsonValueString>(TEXT("None"));
			// The frame is raw bytes and invisible to GC, so an object the call
			// destroyed would be dereferenced by the path read below.
			if (!IsValid(Referenced)) return MakeShared<FJsonValueString>(TEXT("(collected during the call)"));
			return MakeShared<FJsonValueString>(Referenced->GetPathName());
		}

		// Structs and anything else: export text, the spelling every other
		// return value in this response already uses.
		FString Exported;
		Prop->ExportTextItem_Direct(Exported, ValuePtr, nullptr, Parent, PPF_None);
		return MakeShared<FJsonValueString>(Exported);
	}

	inline TSharedPtr<FJsonValue> ValueToJson(FProperty* Prop, const void* ValuePtr, UObject* Parent)
	{
		if (!Prop || !ValuePtr) return MakeShared<FJsonValueNull>();

		if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
		{
			FScriptArrayHelper Helper(ArrayProp, ValuePtr);
			const int32 Count = Helper.Num();
			TArray<TSharedPtr<FJsonValue>> Items;
			Items.Reserve(Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Items.Add(ValueToJson(ArrayProp->Inner, Helper.GetRawPtr(Index), Parent));
			}
			return MakeShared<FJsonValueArray>(Items);
		}
		if (FSetProperty* SetProp = CastField<FSetProperty>(Prop))
		{
			FScriptSetHelper Helper(SetProp, ValuePtr);
			TArray<TSharedPtr<FJsonValue>> Items;
			Items.Reserve(Helper.Num());
			for (int32 Index = 0, MaxIndex = Helper.GetMaxIndex(); Index < MaxIndex; ++Index)
			{
				if (!Helper.IsValidIndex(Index)) continue;
				Items.Add(ValueToJson(SetProp->ElementProp, Helper.GetElementPtr(Index), Parent));
			}
			return MakeShared<FJsonValueArray>(Items);
		}
		if (FMapProperty* MapProp = CastField<FMapProperty>(Prop))
		{
			// An array of {key,value} rather than a JSON object: a TMap key can
			// be a struct or an enum, and collapsing it into an object key would
			// lose that.
			FScriptMapHelper Helper(MapProp, ValuePtr);
			TArray<TSharedPtr<FJsonValue>> Pairs;
			Pairs.Reserve(Helper.Num());
			for (int32 Index = 0, MaxIndex = Helper.GetMaxIndex(); Index < MaxIndex; ++Index)
			{
				if (!Helper.IsValidIndex(Index)) continue;
				TSharedPtr<FJsonObject> Pair = MakeShared<FJsonObject>();
				Pair->SetField(TEXT("key"), ValueToJson(MapProp->KeyProp, Helper.GetKeyPtr(Index), Parent));
				Pair->SetField(TEXT("value"), ValueToJson(MapProp->ValueProp, Helper.GetValuePtr(Index), Parent));
				Pairs.Add(MakeShared<FJsonValueObject>(Pair));
			}
			return MakeShared<FJsonValueArray>(Pairs);
		}

		return ElementToJson(Prop, ValuePtr, Parent);
	}

	/** True for the three property kinds export text cannot carry back usefully. */
	inline bool IsContainerProperty(FProperty* Prop)
	{
		return Prop && (Prop->IsA<FArrayProperty>() || Prop->IsA<FSetProperty>() || Prop->IsA<FMapProperty>());
	}

	/**
	 * #885: marshal one return or out parameter.
	 *
	 * A container becomes real JSON. Everything else keeps the export-text
	 * spelling every existing reader of returnValues parses, so this fixes the
	 * reported defect without moving the wire format underneath anyone: a
	 * TArray<FString> return arrived as an empty string, which made every
	 * array-returning accessor unreadable through the bridge.
	 */
	inline TSharedPtr<FJsonValue> OutputToJson(FProperty* Prop, const void* ValuePtr, UObject* Parent)
	{
		if (!Prop || !ValuePtr) return MakeShared<FJsonValueNull>();
		if (IsContainerProperty(Prop)) return ValueToJson(Prop, ValuePtr, Parent);

		// Object out-params are checked before export: the frame is raw bytes
		// and invisible to GC, so exporting one the call destroyed would
		// dereference freed memory. "(collected during the call)" is distinct
		// from "None" on purpose - reporting both as empty reads as a null
		// return.
		if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
		{
			UObject* Referenced = ObjProp->GetObjectPropertyValue(ValuePtr);
			if (!Referenced) return MakeShared<FJsonValueString>(TEXT("None"));
			if (!IsValid(Referenced)) return MakeShared<FJsonValueString>(TEXT("(collected during the call)"));
		}

		FString Exported;
		Prop->ExportTextItem_Direct(Exported, ValuePtr, nullptr, Parent, PPF_None);
		return MakeShared<FJsonValueString>(Exported);
	}

	/**
	 * Read every return and out parameter of a called function out of its frame
	 * and onto OutVals. One implementation for all the invoke actions, so the
	 * wire format cannot diverge between them.
	 */
	inline void WriteOutputs(const TSharedPtr<FJsonObject>& OutVals, UFunction* Func, void* ParamBuf, UObject* Parent)
	{
		if (!OutVals.IsValid() || !Func || !ParamBuf) return;
		for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Prop = *It;
			if (!(Prop->PropertyFlags & (CPF_ReturnParm | CPF_OutParm))) continue;
			OutVals->SetField(Prop->GetName(), OutputToJson(Prop, Prop->ContainerPtrToValuePtr<void>(ParamBuf), Parent));
		}
	}

	/** Destroy every parameter value in a frame that InitializeValue_InContainer built. */
	inline void DestroyFrame(UFunction* Func, void* ParamBuf)
	{
		if (!Func || !ParamBuf) return;
		for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(ParamBuf);
		}
	}

	// ── Dotted-path call syntax (#969) ───────────────────────────────────────
	//
	// get_runtime_values evaluates dotted paths per matched actor or component.
	// A read-only accessor keyed by an id (a tally by option id, a balance by
	// currency id, an attribute by tag) is a very common shape and was
	// unreachable, because only a zero-arg UFUNCTION was accepted. These parse
	// the "Name(literal, literal)" form the old error message already implied
	// was being read.

	/**
	 * Split a dotted path into segments without breaking an argument list.
	 * "Comp.GetWeight(1.5)" is two segments, not three: the dot inside the
	 * parentheses belongs to the float literal. Empty segments are dropped,
	 * which is what ParseIntoArray did before this existed.
	 */
	inline void SplitPathSegments(const FString& Path, TArray<FString>& OutSegments)
	{
		OutSegments.Reset();
		int32 Depth = 0;
		TCHAR Quote = 0;
		FString Current;
		for (int32 Index = 0; Index < Path.Len(); ++Index)
		{
			const TCHAR Ch = Path[Index];
			if (Quote != 0)
			{
				Current.AppendChar(Ch);
				if (Ch == Quote) Quote = 0;
				continue;
			}
			if (Ch == TEXT('"') || Ch == TEXT('\''))
			{
				Quote = Ch;
				Current.AppendChar(Ch);
				continue;
			}
			if (Ch == TEXT('(') || Ch == TEXT('[') || Ch == TEXT('{'))
			{
				++Depth;
				Current.AppendChar(Ch);
				continue;
			}
			if (Ch == TEXT(')') || Ch == TEXT(']') || Ch == TEXT('}'))
			{
				if (Depth > 0) --Depth;
				Current.AppendChar(Ch);
				continue;
			}
			if (Ch == TEXT('.') && Depth == 0)
			{
				OutSegments.Add(Current.TrimStartAndEnd());
				Current.Reset();
				continue;
			}
			Current.AppendChar(Ch);
		}
		OutSegments.Add(Current.TrimStartAndEnd());
		OutSegments.RemoveAll([](const FString& Segment) { return Segment.IsEmpty(); });
	}

	/**
	 * Strip one level of matching quotes from a literal argument. Everything
	 * else is handed on verbatim: the value is coerced by the same property
	 * setter invoke_object_function's args go through, which already reads a
	 * string into an FName, an FString, a number, a bool or an enum.
	 */
	inline FString UnquoteArgLiteral(const FString& Literal)
	{
		const FString Trimmed = Literal.TrimStartAndEnd();
		if (Trimmed.Len() >= 2)
		{
			const TCHAR First = Trimmed[0];
			if ((First == TEXT('"') || First == TEXT('\'')) && Trimmed[Trimmed.Len() - 1] == First)
			{
				return Trimmed.Mid(1, Trimmed.Len() - 2);
			}
		}
		return Trimmed;
	}

	/**
	 * Read "Name(a, b)" into its name and its raw argument literals.
	 *
	 * bOutHasArgList separates "Name" (no list, the historical zero-arg form)
	 * from "Name()" (an explicitly empty list), so a caller can keep answering
	 * the old shape exactly as before. Returns false with OutError set when the
	 * parentheses do not balance or an argument is empty, because a silently
	 * mis-parsed key would read back somebody else's value.
	 */
	inline bool ParseCallSegment(
		const FString& Segment,
		FString& OutName,
		TArray<FString>& OutArgs,
		bool& bOutHasArgList,
		FString& OutError)
	{
		OutName = Segment.TrimStartAndEnd();
		OutArgs.Reset();
		bOutHasArgList = false;
		OutError.Reset();

		int32 Open = INDEX_NONE;
		if (!Segment.FindChar(TEXT('('), Open))
		{
			return true;
		}

		int32 Depth = 0;
		TCHAR Quote = 0;
		int32 Close = INDEX_NONE;
		for (int32 Index = Open; Index < Segment.Len(); ++Index)
		{
			const TCHAR Ch = Segment[Index];
			if (Quote != 0)
			{
				if (Ch == Quote) Quote = 0;
				continue;
			}
			if (Ch == TEXT('"') || Ch == TEXT('\'')) { Quote = Ch; continue; }
			if (Ch == TEXT('(')) { ++Depth; continue; }
			if (Ch == TEXT(')') && --Depth == 0) { Close = Index; break; }
		}
		if (Close != Segment.Len() - 1)
		{
			OutError = FString::Printf(
				TEXT("'%s' opens an argument list that does not close at the end of the segment"), *Segment);
			return false;
		}

		OutName = Segment.Left(Open).TrimStartAndEnd();
		if (OutName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("'%s' has an argument list with no function name in front of it"), *Segment);
			return false;
		}
		bOutHasArgList = true;

		const FString Inner = Segment.Mid(Open + 1, Close - Open - 1);
		if (Inner.TrimStartAndEnd().IsEmpty())
		{
			return true;
		}

		Depth = 0;
		Quote = 0;
		FString Current;
		for (int32 Index = 0; Index < Inner.Len(); ++Index)
		{
			const TCHAR Ch = Inner[Index];
			if (Quote != 0)
			{
				Current.AppendChar(Ch);
				if (Ch == Quote) Quote = 0;
				continue;
			}
			if (Ch == TEXT('"') || Ch == TEXT('\'')) { Quote = Ch; Current.AppendChar(Ch); continue; }
			if (Ch == TEXT('(') || Ch == TEXT('[') || Ch == TEXT('{')) { ++Depth; Current.AppendChar(Ch); continue; }
			if (Ch == TEXT(')') || Ch == TEXT(']') || Ch == TEXT('}')) { if (Depth > 0) --Depth; Current.AppendChar(Ch); continue; }
			if (Ch == TEXT(',') && Depth == 0)
			{
				OutArgs.Add(Current);
				Current.Reset();
				continue;
			}
			Current.AppendChar(Ch);
		}
		OutArgs.Add(Current);

		for (int32 Index = 0; Index < OutArgs.Num(); ++Index)
		{
			OutArgs[Index] = UnquoteArgLiteral(OutArgs[Index]);
			if (OutArgs[Index].IsEmpty())
			{
				OutError = FString::Printf(TEXT("'%s' has an empty argument at position %d"), *Segment, Index + 1);
				OutArgs.Reset();
				return false;
			}
		}
		return true;
	}

	/** Render a callspace, which is a bitmask and can name more than one space. */
	inline FString DescribeCallspace(int32 Callspace)
	{
		if (Callspace == FunctionCallspace::Absorbed) return TEXT("Absorbed");
		TArray<FString> Parts;
		if (Callspace & FunctionCallspace::Local)  Parts.Add(TEXT("Local"));
		if (Callspace & FunctionCallspace::Remote) Parts.Add(TEXT("Remote"));
		if (Parts.Num() == 0) return FString::Printf(TEXT("Unknown(%d)"), Callspace);
		return FString::Join(Parts, TEXT("+"));
	}

	/**
	 * #973: report a network call whose routing the editor script guard is about
	 * to override.
	 *
	 * FEditorScriptExecutionGuard sets GAllowActorScriptExecutionInEditor, and
	 * AActor::GetFunctionCallspace checks that global in its FIRST branch and
	 * answers Local unconditionally. So a correct UFUNCTION(Server) reached from
	 * a scripted call runs the server implementation locally on a client copy
	 * instead of being sent, which looks exactly like a broken RPC.
	 *
	 * Call this BEFORE opening the guard: that is the only moment the honest
	 * callspace is observable. Returns true when the function is a net function
	 * whose callspace is about to be forced to Local from something else, and
	 * writes the callspace that would otherwise have applied.
	 */
	inline bool WouldForceNetCallspaceLocal(UObject* Target, UFunction* Func, FString& OutNaturalCallspace)
	{
		OutNaturalCallspace.Reset();
		if (!Target || !Func) return false;
		if (!Func->HasAnyFunctionFlags(FUNC_Net)) return false;

		const int32 Callspace = Target->GetFunctionCallspace(Func, nullptr);
		if (Callspace == FunctionCallspace::Local) return false;
		OutNaturalCallspace = DescribeCallspace(Callspace);
		return true;
	}

	/** Wording for the case above, so a false negative is visible in the result. */
	inline FString DescribeForcedLocalCallspace(const FString& FunctionName, const FString& NaturalCallspace)
	{
		return FString::Printf(
			TEXT("'%s' is a replicated UFUNCTION whose callspace here is %s, but a scripted call runs under the editor ")
			TEXT("script-execution guard, which forces every actor callspace to Local. The implementation ran locally ")
			TEXT("rather than being sent, so a refusal or a no-op here is not evidence that the RPC is broken. Pass ")
			TEXT("deferToNextTick=true to run the call on the next engine tick, outside the guard, where it routes normally."),
			*FunctionName, *NaturalCallspace);
	}

	/**
	 * #973: run the call on the next engine tick instead of inline.
	 *
	 * The guard's scope is the handler, so a call queued here happens after that
	 * scope has ended and GetFunctionCallspace answers honestly. Takes ownership
	 * of the already-marshalled parameter frame; the frame is destroyed after the
	 * call, or without calling if the target has gone by then.
	 *
	 * Nothing can be read back: the response is written before the call runs.
	 * That is the trade a caller opts into by asking for the deferral.
	 */
	inline void DeferProcessEventToNextTick(UObject* Target, UFunction* Func, TArray<uint8>&& ParamBuf)
	{
		if (!Target || !Func) return;

		TWeakObjectPtr<UObject> WeakTarget(Target);

		// The frame is a raw byte buffer, so the garbage collector cannot see
		// any UObject argument marshalled into it. Between this handler
		// returning and the ticker firing there is a real window for an
		// incremental GC, and a collected argument would be handed to
		// ProcessEvent as a dangling pointer. Hold every object argument
		// strongly for exactly that window.
		TArray<TStrongObjectPtr<UObject>> HeldArgs;
		for (TFieldIterator<FProperty> It(Func); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_ReturnParm)) continue;
			if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(*It))
			{
				if (UObject* Arg = ObjProp->GetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(ParamBuf.GetData())))
				{
					HeldArgs.Emplace(Arg);
				}
			}
		}

		// The UFunction is held strongly rather than weakly because it is what
		// DestroyFrame walks to destruct the frame. If it were allowed to go
		// (a Blueprint recompiled between queue and tick), every FString and
		// TArray parameter in the buffer would leak instead.
		TStrongObjectPtr<UFunction> HeldFunc(Func);
		TSharedRef<TArray<uint8>> Frame = MakeShared<TArray<uint8>>(MoveTemp(ParamBuf));

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[WeakTarget, HeldFunc, HeldArgs, Frame](float) -> bool
			{
				UFunction* LiveFunc = HeldFunc.Get();
				UObject* LiveTarget = WeakTarget.Get();
				if (LiveTarget && LiveFunc && IsValid(LiveTarget))
				{
					// Deliberately NOT under FEditorScriptExecutionGuard: the
					// whole point of the deferral is that the guard is closed.
					LiveTarget->ProcessEvent(LiveFunc, Frame->GetData());
				}
				if (LiveFunc)
				{
					DestroyFrame(LiveFunc, Frame->GetData());
				}
				return false;
			}), 0.0f);
	}
}

/**
 * One reflected call into a UFUNCTION this module does not link against.
 *
 * The frame is heap allocated and every parameter is initialised through its
 * own FProperty, so option structs get their engine defaults and containers are
 * constructed and destroyed correctly without this module knowing the layout.
 * Parameters are addressed BY NAME: a signature that gains an argument fails to
 * find the name instead of shifting a value into the wrong slot.
 */
struct FMCPReflectedCall
{
	UObject* Target = nullptr;
	UFunction* Function = nullptr;
	TArray<uint8> Frame;

	FMCPReflectedCall() = default;
	FMCPReflectedCall(const FMCPReflectedCall&) = delete;
	FMCPReflectedCall& operator=(const FMCPReflectedCall&) = delete;
	~FMCPReflectedCall() { Release(); }

	/** Bind a function on the default object of an already loaded class. */
	bool Bind(const TCHAR* ClassPath, const TCHAR* FunctionName, FString& OutError)
	{
		Release();
		UClass* Class = FindObject<UClass>(nullptr, ClassPath);
		if (!Class)
		{
			OutError = FString::Printf(TEXT("class '%s' is not loaded."), ClassPath);
			return false;
		}
		UFunction* Found = Class->FindFunctionByName(FName(FunctionName));
		if (!Found)
		{
			OutError = FString::Printf(
				TEXT("'%s' has no reflected function named '%s' in this engine build."),
				ClassPath, FunctionName);
			return false;
		}
		UObject* CDO = Class->GetDefaultObject();
		if (!CDO)
		{
			OutError = FString::Printf(TEXT("'%s' has no default object to call through."), ClassPath);
			return false;
		}
		InitFrame(CDO, Found);
		return true;
	}

	/** Bind a function on a live instance. InstanceName and ClassLabel only
	 *  word the errors; ClassLabel defaults to the instance's class name. */
	bool BindInstance(
		UObject* Instance,
		const TCHAR* FunctionName,
		FString& OutError,
		const TCHAR* InstanceName = TEXT("call target"),
		const TCHAR* ClassLabel = nullptr)
	{
		Release();
		if (!Instance)
		{
			OutError = FString::Printf(TEXT("the %s instance is gone."), InstanceName);
			return false;
		}
		UFunction* Found = Instance->GetClass()->FindFunctionByName(FName(FunctionName));
		if (!Found)
		{
			OutError = FString::Printf(
				TEXT("%s has no reflected function named '%s' in this engine build."),
				ClassLabel ? ClassLabel : *Instance->GetClass()->GetName(), FunctionName);
			return false;
		}
		InitFrame(Instance, Found);
		return true;
	}

	void Release()
	{
		if (Function && Frame.Num() > 0)
		{
			MCPFunctionCall::DestroyFrame(Function, Frame.GetData());
		}
		Frame.Reset();
		Function = nullptr;
		Target = nullptr;
	}

	void Invoke() { if (Target && Function) Target->ProcessEvent(Function, Frame.GetData()); }

	FProperty* Param(const TCHAR* Name) const
	{
		return Function ? Function->FindPropertyByName(FName(Name)) : nullptr;
	}

	FProperty* ReturnParam() const
	{
		if (!Function) return nullptr;
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			if (It->PropertyFlags & CPF_ReturnParm) return *It;
		}
		return nullptr;
	}

	// ── Inputs ───────────────────────────────────────────────────────────────

	bool SetObject(const TCHAR* Name, UObject* Value)
	{
		FObjectPropertyBase* Prop = CastField<FObjectPropertyBase>(Param(Name));
		if (!Prop) return false;
		Prop->SetObjectPropertyValue_InContainer(Frame.GetData(), Value);
		return true;
	}

	bool SetBool(const TCHAR* Name, bool Value)
	{
		FBoolProperty* Prop = CastField<FBoolProperty>(Param(Name));
		if (!Prop) return false;
		Prop->SetPropertyValue_InContainer(Frame.GetData(), Value);
		return true;
	}

	bool SetString(const TCHAR* Name, const FString& Value)
	{
		FStrProperty* Prop = CastField<FStrProperty>(Param(Name));
		if (!Prop) return false;
		Prop->SetPropertyValue_InContainer(Frame.GetData(), Value);
		return true;
	}

	bool SetName(const TCHAR* Name, const FName& Value)
	{
		FNameProperty* Prop = CastField<FNameProperty>(Param(Name));
		if (!Prop) return false;
		Prop->SetPropertyValue_InContainer(Frame.GetData(), Value);
		return true;
	}

	bool SetNumber(const TCHAR* Name, double Value)
	{
		FNumericProperty* Prop = CastField<FNumericProperty>(Param(Name));
		if (!Prop) return false;
		void* Ptr = Prop->ContainerPtrToValuePtr<void>(Frame.GetData());
		if (Prop->IsFloatingPoint()) Prop->SetFloatingPointPropertyValue(Ptr, Value);
		else Prop->SetIntPropertyValue(Ptr, static_cast<int64>(Value));
		return true;
	}

	bool SetTransform(const TCHAR* Name, const FTransform& Value)
	{
		FStructProperty* Prop = CastField<FStructProperty>(Param(Name));
		if (!Prop || Prop->Struct != TBaseStructure<FTransform>::Get()) return false;
		*Prop->ContainerPtrToValuePtr<FTransform>(Frame.GetData()) = Value;
		return true;
	}

	/** Enum parameter by value; INDEX_NONE (an enumerator that was not found) is refused. */
	bool SetEnum(const TCHAR* Name, int64 Value)
	{
		if (Value == INDEX_NONE) return false;
		FProperty* Prop = Param(Name);
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(
				EnumProp->ContainerPtrToValuePtr<void>(Frame.GetData()), Value);
			return true;
		}
		if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			ByteProp->SetPropertyValue_InContainer(Frame.GetData(), static_cast<uint8>(Value));
			return true;
		}
		return false;
	}

	/** Address of one field inside a struct parameter, so an option struct can
	 *  be filled without this module knowing its layout. */
	void* StructField(const TCHAR* ParamName, const TCHAR* FieldName, FProperty*& OutField) const
	{
		OutField = nullptr;
		FStructProperty* Prop = CastField<FStructProperty>(Param(ParamName));
		if (!Prop || !Prop->Struct) return nullptr;
		OutField = Prop->Struct->FindPropertyByName(FName(FieldName));
		if (!OutField) return nullptr;
		void* StructPtr = Prop->ContainerPtrToValuePtr<void>(const_cast<uint8*>(Frame.GetData()));
		return OutField->ContainerPtrToValuePtr<void>(StructPtr);
	}

	bool SetStructBool(const TCHAR* ParamName, const TCHAR* FieldName, bool Value)
	{
		FProperty* Field = nullptr;
		void* Ptr = StructField(ParamName, FieldName, Field);
		FBoolProperty* BoolProp = CastField<FBoolProperty>(Field);
		if (!Ptr || !BoolProp) return false;
		BoolProp->SetPropertyValue(Ptr, Value);
		return true;
	}

	bool SetStructNumber(const TCHAR* ParamName, const TCHAR* FieldName, double Value)
	{
		FProperty* Field = nullptr;
		void* Ptr = StructField(ParamName, FieldName, Field);
		FNumericProperty* NumProp = CastField<FNumericProperty>(Field);
		if (!Ptr || !NumProp) return false;
		if (NumProp->IsFloatingPoint()) NumProp->SetFloatingPointPropertyValue(Ptr, Value);
		else NumProp->SetIntPropertyValue(Ptr, static_cast<int64>(Value));
		return true;
	}

	/** One level deeper (Options.PackingOptions.TargetImageWidth): a dotted
	 *  name passed to FindPropertyByName resolves to nothing at all. */
	bool SetNestedStructNumber(
		const TCHAR* ParamName, const TCHAR* OuterField, const TCHAR* InnerField, double Value)
	{
		FStructProperty* Prop = CastField<FStructProperty>(Param(ParamName));
		if (!Prop || !Prop->Struct) return false;
		FStructProperty* Outer = CastField<FStructProperty>(Prop->Struct->FindPropertyByName(FName(OuterField)));
		if (!Outer || !Outer->Struct) return false;
		FNumericProperty* Inner = CastField<FNumericProperty>(Outer->Struct->FindPropertyByName(FName(InnerField)));
		if (!Inner) return false;
		void* ParamPtr = Prop->ContainerPtrToValuePtr<void>(Frame.GetData());
		void* OuterPtr = Outer->ContainerPtrToValuePtr<void>(ParamPtr);
		void* InnerPtr = Inner->ContainerPtrToValuePtr<void>(OuterPtr);
		if (Inner->IsFloatingPoint()) Inner->SetFloatingPointPropertyValue(InnerPtr, Value);
		else Inner->SetIntPropertyValue(InnerPtr, static_cast<int64>(Value));
		return true;
	}

	bool SetStructEnum(const TCHAR* ParamName, const TCHAR* FieldName, int64 Value)
	{
		if (Value == INDEX_NONE) return false;
		FProperty* Field = nullptr;
		void* Ptr = StructField(ParamName, FieldName, Field);
		if (!Ptr) return false;
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Field))
		{
			EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(Ptr, Value);
			return true;
		}
		if (FByteProperty* ByteProp = CastField<FByteProperty>(Field))
		{
			ByteProp->SetPropertyValue(Ptr, static_cast<uint8>(Value));
			return true;
		}
		return false;
	}

	// ── Outputs by parameter name ────────────────────────────────────────────

	UObject* GetObject(const TCHAR* Name) const
	{
		FObjectPropertyBase* Prop = CastField<FObjectPropertyBase>(Param(Name));
		if (!Prop) return nullptr;
		return Prop->GetObjectPropertyValue_InContainer(const_cast<uint8*>(Frame.GetData()));
	}

	int32 GetInt(const TCHAR* Name) const
	{
		FNumericProperty* Prop = CastField<FNumericProperty>(Param(Name));
		if (!Prop) return 0;
		return static_cast<int32>(Prop->GetSignedIntPropertyValue(
			Prop->ContainerPtrToValuePtr<void>(const_cast<uint8*>(Frame.GetData()))));
	}

	bool GetBool(const TCHAR* Name) const
	{
		FBoolProperty* Prop = CastField<FBoolProperty>(Param(Name));
		if (!Prop) return false;
		return Prop->GetPropertyValue_InContainer(const_cast<uint8*>(Frame.GetData()));
	}

	/** An FBox output. FBox is a noexport core struct with no TBaseStructure
	 *  specialisation, so its reflected name (/Script/CoreUObject.Box) is the
	 *  identity check available. */
	bool GetBox(const TCHAR* Name, FBox& Out) const
	{
		FStructProperty* Prop = CastField<FStructProperty>(Param(Name));
		if (!Prop || !Prop->Struct) return false;
		if (Prop->Struct->GetFName() != FName(NAME_Box)) return false;
		Out = *Prop->ContainerPtrToValuePtr<FBox>(const_cast<uint8*>(Frame.GetData()));
		return true;
	}

	/** An array-of-objects output; null elements are skipped. */
	bool GetObjectArray(const TCHAR* Name, TArray<UObject*>& Out) const
	{
		Out.Reset();
		FArrayProperty* ArrayProp = CastField<FArrayProperty>(Param(Name));
		if (!ArrayProp) return false;
		FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(ArrayProp->Inner);
		if (!Inner) return false;

		FScriptArrayHelper Helper(ArrayProp,
			ArrayProp->ContainerPtrToValuePtr<void>(const_cast<uint8*>(Frame.GetData())));
		Out.Reserve(Helper.Num());
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			if (UObject* Element = Inner->GetObjectPropertyValue(Helper.GetRawPtr(Index)))
			{
				Out.Add(Element);
			}
		}
		return true;
	}

	/** An enum output as its enumerator NAME, so an Outcome pin does not depend
	 *  on Failure staying ordinal 0. */
	FString GetEnumName(const TCHAR* Name) const
	{
		FProperty* Prop = Param(Name);
		void* Ptr = Prop ? Prop->ContainerPtrToValuePtr<void>(const_cast<uint8*>(Frame.GetData())) : nullptr;
		if (!Ptr) return FString();
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			const int64 Raw = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(Ptr);
			return EnumProp->GetEnum() ? EnumProp->GetEnum()->GetNameStringByValue(Raw) : FString();
		}
		if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			const int64 Raw = ByteProp->GetPropertyValue(Ptr);
			return ByteProp->Enum ? ByteProp->Enum->GetNameStringByValue(Raw) : FString();
		}
		return FString();
	}

	// ── The return value, whatever it is named ───────────────────────────────

	UObject* ReturnObject() const
	{
		FObjectPropertyBase* Prop = CastField<FObjectPropertyBase>(ReturnParam());
		return Prop ? Prop->GetObjectPropertyValue_InContainer(const_cast<uint8*>(Frame.GetData())) : nullptr;
	}

	bool BoolReturn() const
	{
		FBoolProperty* Prop = CastField<FBoolProperty>(ReturnParam());
		return Prop && Prop->GetPropertyValue_InContainer(Frame.GetData());
	}

	FName NameReturn() const
	{
		FNameProperty* Prop = CastField<FNameProperty>(ReturnParam());
		return Prop ? Prop->GetPropertyValue_InContainer(Frame.GetData()) : NAME_None;
	}

	FTransform TransformReturn() const
	{
		FStructProperty* Prop = CastField<FStructProperty>(ReturnParam());
		if (!Prop || Prop->Struct != TBaseStructure<FTransform>::Get()) return FTransform::Identity;
		return *Prop->ContainerPtrToValuePtr<FTransform>(Frame.GetData());
	}

	TArray<FName> NameArrayReturn() const
	{
		TArray<FName> Out;
		FArrayProperty* Prop = CastField<FArrayProperty>(ReturnParam());
		if (!Prop || !CastField<FNameProperty>(Prop->Inner)) return Out;
		FScriptArrayHelper Helper(Prop, Prop->ContainerPtrToValuePtr<void>(Frame.GetData()));
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			Out.Add(*reinterpret_cast<FName*>(Helper.GetRawPtr(Index)));
		}
		return Out;
	}

private:
	void InitFrame(UObject* InTarget, UFunction* InFunction)
	{
		Target = InTarget;
		Function = InFunction;
		Frame.SetNumZeroed(FMath::Max<int32>(Function->ParmsSize, 1));
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Frame.GetData());
		}
	}
};

// ── Function and script arguments (#811, #1057) ──────────────────────────────
//
// `args` on the invoke_* actions and run_python_file is declared with value
// forms: a name -> value map, an entry list, a list of strings, or a string.
// These normalizers are the one place each form is accepted or refused, so
// every handler taking `args` answers the same shape the same way.

namespace MCPArgsDetail
{
	/** Decode JSON text holding exactly one value of any kind. Wrapped in an
	 *  array so a bare scalar decodes the same on every supported engine. */
	inline bool ParseOneJsonValue(const FString& Text, TSharedPtr<FJsonValue>& Out)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FString(TEXT("[")) + Text + TEXT("]"));
		if (!FJsonSerializer::Deserialize(Reader, Values) || Values.Num() != 1 || !Values[0].IsValid())
		{
			return false;
		}
		Out = Values[0];
		return true;
	}

	/** A value as compact JSON text, a whole number without a fraction. */
	inline FString CompactJson(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid() || Value->Type == EJson::None || Value->Type == EJson::Null)
		{
			return TEXT("null");
		}
		if (Value->Type == EJson::Number)
		{
			const double Number = Value->AsNumber();
			if (FMath::IsFinite(Number) && Number == FMath::RoundToDouble(Number) && FMath::Abs(Number) < 1e15)
			{
				return FString::Printf(TEXT("%lld"), static_cast<long long>(Number));
			}
			return FString::SanitizeFloat(Number);
		}
		FString Text;
		TArray<TSharedPtr<FJsonValue>> Wrapper;
		Wrapper.Add(Value);
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(Wrapper, Writer);
		return Text.Len() >= 2 ? Text.Mid(1, Text.Len() - 2) : Text;
	}

	inline TSharedPtr<FJsonValue> FunctionArgsError(const FString& Label, const TCHAR* Detail)
	{
		return MCPError(FString::Printf(
			TEXT("%s %s. Pass an object mapping parameter name to value, e.g. {\"bEnabled\": true}."), *Label, Detail));
	}
}

/**
 * Reduce a function-call `args` value to the name -> value map a UFUNCTION's
 * parameters are marshalled from. Accepts the map itself, an entry list
 * ([{name, value}]), and a JSON string of either. Returns an error value when
 * the value is none of those, else nullptr; OutArgs is left invalid when there
 * is nothing to pass (absent, null, or a blank string).
 */
inline TSharedPtr<FJsonValue> MCPNormalizeFunctionArgs(const TSharedPtr<FJsonValue>& Raw, const FString& Label, TSharedPtr<FJsonObject>& OutArgs)
{
	OutArgs.Reset();
	if (!Raw.IsValid() || Raw->Type == EJson::None || Raw->Type == EJson::Null)
	{
		return nullptr;
	}
	switch (Raw->Type)
	{
	case EJson::String:
	{
		const FString Text = Raw->AsString().TrimStartAndEnd();
		if (Text.IsEmpty()) return nullptr;
		TSharedPtr<FJsonValue> Parsed;
		if (!MCPArgsDetail::ParseOneJsonValue(Text, Parsed))
		{
			return MCPArgsDetail::FunctionArgsError(Label, TEXT("was a string, but it is not valid JSON"));
		}
		if (Parsed->Type == EJson::String)
		{
			return MCPArgsDetail::FunctionArgsError(Label, TEXT("decoded to a string, not a parameter map"));
		}
		return MCPNormalizeFunctionArgs(Parsed, Label, OutArgs);
	}
	case EJson::Array:
	{
		TSharedPtr<FJsonObject> Map = MakeShared<FJsonObject>();
		for (const TSharedPtr<FJsonValue>& Entry : Raw->AsArray())
		{
			if (!Entry.IsValid() || Entry->Type != EJson::Object || !Entry->AsObject().IsValid())
			{
				return MCPArgsDetail::FunctionArgsError(Label, TEXT("was an array of values, so no parameter name can be resolved"));
			}
			const TSharedPtr<FJsonObject> EntryObject = Entry->AsObject();
			// Typed, not TryGetStringField: that would accept a number as a name.
			const TSharedPtr<FJsonValue> NameValue = EntryObject->TryGetField(TEXT("name"));
			if (!NameValue.IsValid() || NameValue->Type != EJson::String || NameValue->AsString().IsEmpty())
			{
				return MCPArgsDetail::FunctionArgsError(Label, TEXT("was an array whose entries are missing a \"name\" string"));
			}
			const FString Name = NameValue->AsString();
			// An entry with no value passes nothing for that name, and overrides
			// an earlier entry for it, as it would once serialized.
			const TSharedPtr<FJsonValue> Value = EntryObject->TryGetField(TEXT("value"));
			if (Value.IsValid())
			{
				Map->SetField(Name, Value);
			}
			else
			{
				Map->RemoveField(Name);
			}
		}
		OutArgs = Map;
		return nullptr;
	}
	case EJson::Object:
		OutArgs = Raw->AsObject();
		return nullptr;
	case EJson::Number:
		return MCPArgsDetail::FunctionArgsError(Label, TEXT("was a number"));
	case EJson::Boolean:
		return MCPArgsDetail::FunctionArgsError(Label, TEXT("was a boolean"));
	default:
		return nullptr;
	}
}

/** MCPNormalizeFunctionArgs over the parameter `Key`, noted as read. */
inline TSharedPtr<FJsonValue> MCPReadFunctionArgs(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key, TSharedPtr<FJsonObject>& OutArgs)
{
	return MCPNormalizeFunctionArgs(TryGetParam(Params, Key), Key, OutArgs);
}

/**
 * Reduce a script `args` value to positional strings. Accepts a list (a
 * non-string entry becomes its compact JSON), a JSON array string, and a lone
 * string as one argument. A map is a function-call shape and is refused, as is
 * any other value. Returns an error value or nullptr; OutArgs is empty when
 * there is nothing to pass.
 */
inline TSharedPtr<FJsonValue> MCPNormalizePythonArgs(const TSharedPtr<FJsonValue>& Raw, const FString& Label, TArray<FString>& OutArgs)
{
	OutArgs.Reset();
	if (!Raw.IsValid() || Raw->Type == EJson::None || Raw->Type == EJson::Null)
	{
		return nullptr;
	}
	if (Raw->Type == EJson::String)
	{
		const FString Text = Raw->AsString().TrimStartAndEnd();
		if (Text.IsEmpty()) return nullptr;
		if (Text.StartsWith(TEXT("[")))
		{
			TSharedPtr<FJsonValue> Parsed;
			if (!MCPArgsDetail::ParseOneJsonValue(Text, Parsed) || Parsed->Type != EJson::Array)
			{
				return MCPError(FString::Printf(
					TEXT("%s looked like a JSON array but does not parse. Pass an array of positional strings."), *Label));
			}
			return MCPNormalizePythonArgs(Parsed, Label, OutArgs);
		}
		OutArgs.Add(Text);
		return nullptr;
	}
	if (Raw->Type == EJson::Array)
	{
		for (const TSharedPtr<FJsonValue>& Entry : Raw->AsArray())
		{
			OutArgs.Add(Entry.IsValid() && Entry->Type == EJson::String ? Entry->AsString() : MCPArgsDetail::CompactJson(Entry));
		}
		return nullptr;
	}
	return MCPError(FString::Printf(
		TEXT("%s must be an array of positional strings for run_python_file, not an object."), *Label));
}

/** MCPNormalizePythonArgs over the parameter `Key`, noted as read. */
inline TSharedPtr<FJsonValue> MCPReadPythonArgs(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key, TArray<FString>& OutArgs)
{
	return MCPNormalizePythonArgs(TryGetParam(Params, Key), Key, OutArgs);
}
