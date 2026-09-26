#pragma once

// Engine values to JSON: vectors, rotators, colours, quaternions, transforms and
// string lists, in the wire shapes src/schemas.ts declares.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"
#include "Math/Color.h"
#include "Math/Quat.h"
#include "Math/Transform.h"

/** Render a TArray<FString> as a JSON string array. The inverse of
 *  JsonArrayToStringList, shared so batch handlers that report label lists do
 *  not each define their own file-local copy (unity build: two anonymous
 *  namespaces sharing a blob merge, and the second definition is C2084). */
inline TArray<TSharedPtr<FJsonValue>> MCPStringListToJson(const TArray<FString>& Values)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	Out.Reserve(Values.Num());
	for (const FString& Value : Values)
	{
		Out.Add(MakeShared<FJsonValueString>(Value));
	}
	return Out;
}

/** Extract a JSON array of strings into a TArray<FString>. */
inline TArray<FString> JsonArrayToStringList(const TArray<TSharedPtr<FJsonValue>>* Arr)
{
	TArray<FString> Out;
	if (!Arr) return Out;
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		FString S;
		if (V.IsValid() && V->TryGetString(S)) Out.Add(S);
	}
	return Out;
}

/** Inline FVector→JSON. Mirrors FMCPJsonSerializer::SerializeVector. Use this
 *  in handlers building result objects so the wire shape stays consistent. */
inline TSharedPtr<FJsonObject> MCPVec3ToJsonObject(const FVector& V)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("x"), V.X);
	Obj->SetNumberField(TEXT("y"), V.Y);
	Obj->SetNumberField(TEXT("z"), V.Z);
	return Obj;
}

inline TSharedPtr<FJsonObject> MCPRotatorToJsonObject(const FRotator& R)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("pitch"), R.Pitch);
	Obj->SetNumberField(TEXT("yaw"),   R.Yaw);
	Obj->SetNumberField(TEXT("roll"),  R.Roll);
	return Obj;
}

inline TSharedPtr<FJsonObject> MCPLinearColorToJsonObject(const FLinearColor& C)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("r"), C.R);
	Obj->SetNumberField(TEXT("g"), C.G);
	Obj->SetNumberField(TEXT("b"), C.B);
	Obj->SetNumberField(TEXT("a"), C.A);
	return Obj;
}

/** FQuat to { x, y, z, w }. */
inline TSharedPtr<FJsonObject> MCPQuatToJsonObject(const FQuat& Q)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("x"), Q.X);
	Obj->SetNumberField(TEXT("y"), Q.Y);
	Obj->SetNumberField(TEXT("z"), Q.Z);
	Obj->SetNumberField(TEXT("w"), Q.W);
	return Obj;
}

/** FTransform to { location, rotation, scale }, the shape OptionalTransform reads. */
inline TSharedPtr<FJsonObject> MCPTransformToJsonObject(const FTransform& T)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(T.GetLocation()));
	Obj->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(T.Rotator()));
	Obj->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(T.GetScale3D()));
	return Obj;
}
