// Arrange substrate (Roadmap v2, F1). Write access to the live world so an agent
// can construct a scenario, not just observe one: set any UProperty by dotted
// path (mirror of the read walk in PIEFrameSampler.cpp), spawn/destroy actors,
// and invoke callable UFUNCTIONs. SetPropertyByPath is pure reflection and needs
// no PIE, so it is unit testable on any UObject.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

class AActor;
class UClass;
class UObject;
class UWorld;

namespace MCPPIE
{
	class FPIEActorPuppet
	{
	public:
		// Write a UProperty reached by "Component.Struct.Property" from Root, coercing
		// the JSON value via FProperty::ImportText_Direct. Returns false + OutError on
		// an unresolved path or a coercion failure. No PIE required.
		static bool SetPropertyByPath(UObject* Root, const FString& Path,
		                              const TSharedPtr<FJsonValue>& Value, FString& OutError);

		// The actor class SpawnActor would spawn for ClassPath (native class, or a Blueprint
		// path with or without _C). Null + OutError when it is missing or not an actor.
		static UClass* ResolveActorClass(const FString& ClassPath, FString& OutError);

		// Spawn ClassPath (native class or "/Game/.../BP_X.BP_X_C") at Xform. Returns
		// the actor (its GetName() is the id FindActorById resolves), null + OutError otherwise.
		static AActor* SpawnActor(UWorld* World, const FString& ClassPath,
		                          const FTransform& Xform, FString& OutError);

		// Invoke a callable UFUNCTION by name on Target, importing Args positionally
		// into its parameters. It runs in the PIE instance that owns Target, the way
		// core's invoke_object_function does. bOutUsedEditorGuard reports whether the
		// call needed the editor script guard (Target outside an initialised game world).
		// Returns false + OutError if the function is missing or an argument fails to coerce.
		static bool CallFunction(UObject* Target, const FString& FuncName,
		                         const TArray<TSharedPtr<FJsonValue>>& Args, FString& OutError,
		                         bool* bOutUsedEditorGuard = nullptr);
	};
}
