#pragma once

// Reading handler parameters: the required and optional readers, the vector,
// rotator and transform readers, and the #1057 tracking of which keys were read.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"
#include "Math/Color.h"
#include "Math/Transform.h"
#include <initializer_list>
#include "HandlerResult.h"

// ── Parameter read tracking (#1057) ──────────────────────────────────────────
//
// The registry opens a scope around each handler of a reporting category. The
// parameter helpers below note every top-level key they read, and a key that
// arrived and was never read comes back as `paramsNotRead`. Reads of nested
// objects are not noted, and with no scope open nothing is.

/** Names the dispatcher consumes. Mirrors ROUTING_PARAM_NAMES in src/routing-params.ts. */
inline const TArray<FString>& MCPRoutingParamNames()
{
	static const TArray<FString> Names = {
		TEXT("action"), TEXT("timeoutMs"), TEXT("select"), TEXT("omit"), TEXT("editor"), TEXT("toEditor"),
	};
	return Names;
}

class FMCPParamReadScope
{
public:
	explicit FMCPParamReadScope(const TSharedPtr<FJsonObject>& InParams)
		: Root(InParams.Get())
		, Previous(ActiveSlot())
	{
		if (Root)
		{
			// The key type differs across engine versions; the pair conversion is the portable read.
			for (const auto& JsonEntry : Root->Values)
			{
				const TPair<FString, TSharedPtr<FJsonValue>> Pair(JsonEntry.Key, JsonEntry.Value);
				Arrived.Add(Pair.Key);
			}
		}
		ActiveSlot() = this;
	}

	~FMCPParamReadScope()
	{
		ActiveSlot() = Previous;
	}

	FMCPParamReadScope(const FMCPParamReadScope&) = delete;
	FMCPParamReadScope& operator=(const FMCPParamReadScope&) = delete;

	/** The innermost open scope on this thread, or nullptr. */
	static FMCPParamReadScope* Active()
	{
		return ActiveSlot();
	}

	void Note(const FJsonObject* Object, const TCHAR* Key)
	{
		if (Object != nullptr && Object == Root && Key != nullptr)
		{
			Read.Add(FString(Key));
		}
	}

	/** Every top-level key read so far, whether or not it arrived. */
	const TSet<FString>& ReadKeys() const
	{
		return Read;
	}

	/** Keys that arrived, were not read and are not routing names, sorted. */
	TArray<FString> Unread() const
	{
		TArray<FString> Out;
		for (const FString& Key : Arrived)
		{
			if (!Read.Contains(Key) && !MCPRoutingParamNames().Contains(Key))
			{
				Out.Add(Key);
			}
		}
		Out.Sort();
		return Out;
	}

private:
	static FMCPParamReadScope*& ActiveSlot()
	{
		static thread_local FMCPParamReadScope* Slot = nullptr;
		return Slot;
	}

	const FJsonObject* Root;
	FMCPParamReadScope* Previous;
	TArray<FString> Arrived;
	TSet<FString> Read;
};

/** Note that a handler read `Key` off `Params`. A no-op unless `Params` is the dispatch's own object. */
inline void MCPNoteParamRead(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key)
{
	if (FMCPParamReadScope* Scope = FMCPParamReadScope::Active())
	{
		Scope->Note(Params.Get(), Key);
	}
}

/** Add `paramsNotRead` to a successful object result. Failures are left as they are. */
inline void MCPAttachParamsNotRead(const TSharedPtr<FJsonValue>& Result, const TArray<FString>& Unread)
{
	if (Unread.Num() == 0 || !Result.IsValid() || Result->Type != EJson::Object)
	{
		return;
	}
	const TSharedPtr<FJsonObject> Object = Result->AsObject();
	bool bSuccess = true;
	if (!Object.IsValid() || (Object->TryGetBoolField(TEXT("success"), bSuccess) && !bSuccess))
	{
		return;
	}
	TArray<TSharedPtr<FJsonValue>> Names;
	for (const FString& Key : Unread)
	{
		Names.Add(MakeShared<FJsonValueString>(Key));
	}
	Object->SetArrayField(TEXT("paramsNotRead"), Names);
}

// ── Parameter extraction ─────────────────────────────────────────────────────
//
// Every helper below treats an unset Params the same way it treats an empty
// one: a required key is missing, an optional key falls back to its default.
//
// From the socket an unset pointer cannot arrive, because ProcessMessage
// substitutes a fresh FJsonObject on every path. This header is public and
// shipped, though, and its neighbours here - ResolveWorldFromParams,
// MCPResolveActor, ReadPageRequest - all test validity first. A caller that
// reads those and concludes the file guards, then hands an unset pointer to a
// nested dispatch or a test, was one line from a null dereference on the game
// thread. One rule for the whole header is the thing that stops that.

/** Extract a required string parameter.  Returns error JSON on failure, nullptr on success. */
inline TSharedPtr<FJsonValue> RequireString(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	FString& OutValue)
{
	MCPNoteParamRead(Params, Key);
	if (Params.IsValid() && Params->TryGetStringField(Key, OutValue) && !OutValue.IsEmpty())
		return nullptr;
	return MCPError(FString::Printf(TEXT("Missing required parameter '%s'"), Key));
}

/** Extract a required string from either of two keys (e.g. "path" or "assetPath"). */
inline TSharedPtr<FJsonValue> RequireStringAlt(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key1,
	const TCHAR* Key2,
	FString& OutValue)
{
	MCPNoteParamRead(Params, Key1);
	MCPNoteParamRead(Params, Key2);
	if (Params.IsValid())
	{
		if (Params->TryGetStringField(Key1, OutValue) && !OutValue.IsEmpty())
			return nullptr;
		if (Params->TryGetStringField(Key2, OutValue) && !OutValue.IsEmpty())
			return nullptr;
	}
	return MCPError(FString::Printf(TEXT("Missing required parameter '%s' (or '%s')"), Key1, Key2));
}

/** Extract a required number. Returns error JSON when absent or not a number, nullptr on success. */
template <typename TNumber>
inline TSharedPtr<FJsonValue> RequireNumber(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	TNumber& OutValue)
{
	MCPNoteParamRead(Params, Key);
	if (Params.IsValid() && Params->TryGetNumberField(Key, OutValue)) return nullptr;
	return MCPError(FString::Printf(TEXT("Missing required parameter '%s'"), Key));
}

/** Extract a required array. Returns error JSON when absent or not an array, nullptr on success. */
inline TSharedPtr<FJsonValue> RequireArray(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	const TArray<TSharedPtr<FJsonValue>>*& OutValue)
{
	MCPNoteParamRead(Params, Key);
	OutValue = nullptr;
	if (Params.IsValid() && Params->TryGetArrayField(Key, OutValue) && OutValue) return nullptr;
	return MCPError(FString::Printf(TEXT("Missing required parameter '%s'"), Key));
}

/** Extract a required object. Returns error JSON when absent or not an object, nullptr on success. */
inline TSharedPtr<FJsonValue> RequireObject(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	const TSharedPtr<FJsonObject>*& OutValue)
{
	MCPNoteParamRead(Params, Key);
	OutValue = nullptr;
	if (Params.IsValid() && Params->TryGetObjectField(Key, OutValue) && OutValue && OutValue->IsValid()) return nullptr;
	return MCPError(FString::Printf(TEXT("Missing required parameter '%s'"), Key));
}

/** Extract an optional string, returning DefaultValue if absent. */
inline FString OptionalString(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	const FString& DefaultValue = TEXT(""))
{
	MCPNoteParamRead(Params, Key);
	FString Value;
	return (Params.IsValid() && Params->TryGetStringField(Key, Value)) ? Value : DefaultValue;
}

/** Extract an optional int32, returning DefaultValue if absent. */
inline int32 OptionalInt(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	int32 DefaultValue = 0)
{
	MCPNoteParamRead(Params, Key);
	int32 Value;
	return (Params.IsValid() && Params->TryGetNumberField(Key, Value)) ? Value : DefaultValue;
}

/** Extract an optional double, returning DefaultValue if absent. */
inline double OptionalNumber(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	double DefaultValue = 0.0)
{
	MCPNoteParamRead(Params, Key);
	double Value;
	return (Params.IsValid() && Params->TryGetNumberField(Key, Value)) ? Value : DefaultValue;
}

/** Extract an optional bool, returning DefaultValue if absent. */
inline bool OptionalBool(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	bool DefaultValue = false)
{
	MCPNoteParamRead(Params, Key);
	bool Value;
	return (Params.IsValid() && Params->TryGetBoolField(Key, Value)) ? Value : DefaultValue;
}

// Read-tracked forms of FJsonObject::HasField and TryGet*Field. A handler that
// reads a parameter with these is seen by the #1057 read tracking; a direct
// Params->TryGet*Field call is not, and scripts/audit-direct-param-reads.mjs
// lists those.

inline bool HasParam(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key)
{
	MCPNoteParamRead(Params, Key);
	return Params.IsValid() && Params->HasField(Key);
}

inline TSharedPtr<FJsonValue> TryGetParam(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key)
{
	MCPNoteParamRead(Params, Key);
	if (!Params.IsValid()) return nullptr;
	return Params->TryGetField(Key);
}

inline bool TryGetStringParam(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key, FString& Out)
{
	MCPNoteParamRead(Params, Key);
	return Params.IsValid() && Params->TryGetStringField(Key, Out);
}

template <typename TNumber>
inline bool TryGetNumberParam(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key, TNumber& Out)
{
	MCPNoteParamRead(Params, Key);
	return Params.IsValid() && Params->TryGetNumberField(Key, Out);
}

inline bool TryGetBoolParam(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key, bool& Out)
{
	MCPNoteParamRead(Params, Key);
	return Params.IsValid() && Params->TryGetBoolField(Key, Out);
}

inline bool TryGetArrayParam(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key, const TArray<TSharedPtr<FJsonValue>>*& Out)
{
	MCPNoteParamRead(Params, Key);
	return Params.IsValid() && Params->TryGetArrayField(Key, Out);
}

inline bool TryGetObjectParam(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key, const TSharedPtr<FJsonObject>*& Out)
{
	MCPNoteParamRead(Params, Key);
	return Params.IsValid() && Params->TryGetObjectField(Key, Out);
}

/** #1057: a spec'd handler whose shared helpers read some of its parameters only
 *  on the path that uses them names those keys here, before its first early
 *  return, so its read set is its declared set whatever path a call takes. The
 *  helpers still read each key where they use it. */
inline void MCPReadParamsAhead(const TSharedPtr<FJsonObject>& Params, std::initializer_list<const TCHAR*> Keys)
{
	for (const TCHAR* Key : Keys)
	{
		MCPNoteParamRead(Params, Key);
	}
}

// ── Vector/Rotator/Color/Transform extraction ────────────────────────────────
//
// Wire shape contract (matches src/schemas.ts):
//   Vec3:    { x: number, y: number, z: number }
//   Rotator: { pitch: number, yaw: number, roll: number }
//   Color:   { r, g, b, a? }                          (a defaults to 1)
//   Transform: { location: Vec3, rotation: Rotator, scale: Vec3 }
//
// Per-axis numeric fields are individually optional. Missing axes inherit
// from the default value passed in. Use the *Strict variants when every
// axis must be present.

/** Read x/y/z fields out of a JSON object into Out. Returns true if any field
 *  was present. */
inline bool ReadVec3Fields(const TSharedPtr<FJsonObject>& Obj, FVector& Out)
{
	if (!Obj.IsValid()) return false;
	double Tmp;
	bool Any = false;
	if (Obj->TryGetNumberField(TEXT("x"), Tmp)) { Out.X = Tmp; Any = true; }
	if (Obj->TryGetNumberField(TEXT("y"), Tmp)) { Out.Y = Tmp; Any = true; }
	if (Obj->TryGetNumberField(TEXT("z"), Tmp)) { Out.Z = Tmp; Any = true; }
	return Any;
}

inline bool ReadRotatorFields(const TSharedPtr<FJsonObject>& Obj, FRotator& Out)
{
	if (!Obj.IsValid()) return false;
	double Tmp;
	bool Any = false;
	if (Obj->TryGetNumberField(TEXT("pitch"), Tmp)) { Out.Pitch = Tmp; Any = true; }
	if (Obj->TryGetNumberField(TEXT("yaw"),   Tmp)) { Out.Yaw   = Tmp; Any = true; }
	if (Obj->TryGetNumberField(TEXT("roll"),  Tmp)) { Out.Roll  = Tmp; Any = true; }
	return Any;
}

/** Read x/y/z into Out only when all three are present numbers. Returns false,
 *  leaving Out untouched, when any axis is missing. */
inline bool ReadVec3FieldsStrict(const TSharedPtr<FJsonObject>& Obj, FVector& Out)
{
	if (!Obj.IsValid()) return false;
	double X, Y, Z;
	if (!Obj->TryGetNumberField(TEXT("x"), X)
		|| !Obj->TryGetNumberField(TEXT("y"), Y)
		|| !Obj->TryGetNumberField(TEXT("z"), Z))
	{
		return false;
	}
	Out = FVector(X, Y, Z);
	return true;
}

/** Read pitch/yaw/roll into Out only when all three are present numbers. */
inline bool ReadRotatorFieldsStrict(const TSharedPtr<FJsonObject>& Obj, FRotator& Out)
{
	if (!Obj.IsValid()) return false;
	double Pitch, Yaw, Roll;
	if (!Obj->TryGetNumberField(TEXT("pitch"), Pitch)
		|| !Obj->TryGetNumberField(TEXT("yaw"), Yaw)
		|| !Obj->TryGetNumberField(TEXT("roll"), Roll))
	{
		return false;
	}
	Out = FRotator(Pitch, Yaw, Roll);
	return true;
}

UE_DEPRECATED(5.4, "Unused by the bridge; read r/g/b/a fields directly or use a typed color parser.")
inline bool ReadLinearColorFields(const TSharedPtr<FJsonObject>& Obj, FLinearColor& Out)
{
	if (!Obj.IsValid()) return false;
	double Tmp;
	bool Any = false;
	if (Obj->TryGetNumberField(TEXT("r"), Tmp)) { Out.R = Tmp; Any = true; }
	if (Obj->TryGetNumberField(TEXT("g"), Tmp)) { Out.G = Tmp; Any = true; }
	if (Obj->TryGetNumberField(TEXT("b"), Tmp)) { Out.B = Tmp; Any = true; }
	if (Obj->TryGetNumberField(TEXT("a"), Tmp)) { Out.A = Tmp; Any = true; }
	return Any;
}

/** Extract an optional FVector from Params[Key]. Missing or non-object: returns DefaultValue.
 *  Individual missing axes inherit from DefaultValue. */
inline FVector OptionalVec3(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	const FVector& DefaultValue = FVector::ZeroVector)
{
	MCPNoteParamRead(Params, Key);
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	if (!Params.IsValid() || !Params->TryGetObjectField(Key, Obj) || !Obj || !(*Obj).IsValid()) return DefaultValue;
	FVector Out = DefaultValue;
	ReadVec3Fields(*Obj, Out);
	return Out;
}

/** Extract a required FVector. Returns error JSON on miss/malformed, nullptr on success. */
inline TSharedPtr<FJsonValue> RequireVec3(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	FVector& Out)
{
	MCPNoteParamRead(Params, Key);
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	if (!Params.IsValid() || !Params->TryGetObjectField(Key, Obj) || !Obj || !(*Obj).IsValid())
		return MCPError(FString::Printf(TEXT("Missing required vector parameter '%s' ({x,y,z})"), Key));
	Out = FVector::ZeroVector;
	if (!ReadVec3Fields(*Obj, Out))
		return MCPError(FString::Printf(TEXT("Vector '%s' has no x/y/z fields"), Key));
	return nullptr;
}

inline FRotator OptionalRotator(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	const FRotator& DefaultValue = FRotator::ZeroRotator)
{
	MCPNoteParamRead(Params, Key);
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	if (!Params.IsValid() || !Params->TryGetObjectField(Key, Obj) || !Obj || !(*Obj).IsValid()) return DefaultValue;
	FRotator Out = DefaultValue;
	ReadRotatorFields(*Obj, Out);
	return Out;
}

UE_DEPRECATED(5.4, "Unused by the bridge; use OptionalRotator or ReadRotatorFieldsStrict.")
inline TSharedPtr<FJsonValue> RequireRotator(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	FRotator& Out)
{
	MCPNoteParamRead(Params, Key);
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	if (!Params.IsValid() || !Params->TryGetObjectField(Key, Obj) || !Obj || !(*Obj).IsValid())
		return MCPError(FString::Printf(TEXT("Missing required rotator parameter '%s' ({pitch,yaw,roll})"), Key));
	Out = FRotator::ZeroRotator;
	if (!ReadRotatorFields(*Obj, Out))
		return MCPError(FString::Printf(TEXT("Rotator '%s' has no pitch/yaw/roll fields"), Key));
	return nullptr;
}

UE_DEPRECATED(5.4, "Unused by the bridge; read r/g/b/a fields directly or use a typed color parser.")
inline FLinearColor OptionalLinearColor(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key,
	const FLinearColor& DefaultValue = FLinearColor::White)
{
	MCPNoteParamRead(Params, Key);
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	if (!Params.IsValid() || !Params->TryGetObjectField(Key, Obj) || !Obj || !(*Obj).IsValid()) return DefaultValue;
	FLinearColor Out = DefaultValue;
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	ReadLinearColorFields(*Obj, Out);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	return Out;
}

/** Extract an optional FTransform from Params[Key]. Reads location/rotation/scale sub-objects.
 *  Missing entirely or non-object: returns FTransform::Identity. */
inline FTransform OptionalTransform(
	const TSharedPtr<FJsonObject>& Params,
	const TCHAR* Key)
{
	MCPNoteParamRead(Params, Key);
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	if (!Params.IsValid() || !Params->TryGetObjectField(Key, Obj) || !Obj || !(*Obj).IsValid()) return FTransform::Identity;
	FVector  Loc   = FVector::ZeroVector;
	FRotator Rot   = FRotator::ZeroRotator;
	FVector  Scale = FVector::OneVector;
	const TSharedPtr<FJsonObject>* Sub = nullptr;
	if ((*Obj)->TryGetObjectField(TEXT("location"), Sub) && Sub) ReadVec3Fields(*Sub, Loc);
	if ((*Obj)->TryGetObjectField(TEXT("rotation"), Sub) && Sub) ReadRotatorFields(*Sub, Rot);
	if ((*Obj)->TryGetObjectField(TEXT("scale"),    Sub) && Sub) ReadVec3Fields(*Sub, Scale);
	return FTransform(Rot, Loc, Scale);
}
