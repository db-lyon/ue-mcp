#pragma once

// Object, property and component helpers: property export, file dumps, GC
// rooting, subobject walks, component and collision lookup, component bounds.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Engine/CollisionProfile.h"
#include "HandlerEngineVersion.h"
#include "HandlerResult.h"

/** Export a property's value as text, honouring C-style fixed arrays.
 *
 *  A UPROPERTY declared as `int32 Foo[3]` is ONE FProperty with ArrayDim == 3,
 *  not three properties. ExportTextItem_Direct exports a single element, so a
 *  caller that passes ContainerPtrToValuePtr<void>(Container) with no index
 *  gets element 0 and nothing else, and the value reads as a plain scalar.
 *
 *  That is how #927 hid two thirds of RecastNavMesh's NavMeshResolutionParams:
 *  lint-prose-allow: tier  RecastNavMesh's own name for its three generation tiers
 *  the Low tier was reported as if it were the whole property while the engine
 *  was generating from Default and High, so a navmesh diagnosis was performed
 *  against numbers the engine was not using.
 *
 *  Returns a JSON string for a normal property, and a JSON array of one string
 *  per element for a fixed array, so a caller can tell the two apart. */
inline TSharedPtr<FJsonValue> MCPExportPropertyValue(const FProperty* Prop, const void* Container)
{
	if (!Prop || !Container) return MakeShared<FJsonValueString>(FString());

	auto ExportOne = [Prop, Container](int32 Index) -> FString
	{
		FString Text;
		Prop->ExportTextItem_Direct(
			Text, Prop->ContainerPtrToValuePtr<void>(Container, Index), nullptr, nullptr, PPF_None);
		return Text;
	};

	if (Prop->ArrayDim <= 1)
	{
		return MakeShared<FJsonValueString>(ExportOne(0));
	}

	TArray<TSharedPtr<FJsonValue>> Elements;
	Elements.Reserve(Prop->ArrayDim);
	for (int32 Index = 0; Index < Prop->ArrayDim; ++Index)
	{
		Elements.Add(MakeShared<FJsonValueString>(ExportOne(Index)));
	}
	return MakeShared<FJsonValueArray>(Elements);
}

/** True when a property is a C-style fixed array, so callers that must emit a
 *  scalar can say the value was truncated rather than silently truncating. */
inline bool MCPPropertyIsFixedArray(const FProperty* Prop)
{
	return Prop != nullptr && Prop->ArrayDim > 1;
}

// ── File dumps ───────────────────────────────────────────────────────────────
//
// A read whose result is too large to return inline writes it to a file
// instead. Every action that does (read_graph, search_call_sites,
// get_mesh_geometry, read_datatable) follows one convention, so it lives here
// rather than as a copy per handler: a relative path resolves under Saved/
// directory, a missing directory is created, an existing file is overwritten,
// and the response echoes the resolved path.

/** Resolve a caller's dump path. A relative path lands under Saved/, so a bare
 *  file name cannot scatter output across the engine's working directory. */
inline FString MCPResolveDumpPath(const FString& RequestedPath)
{
	return FPaths::IsRelative(RequestedPath)
		? FPaths::Combine(FPaths::ProjectSavedDir(), RequestedPath)
		: RequestedPath;
}

/** Write already-serialized dump text to a path from MCPResolveDumpPath,
 *  creating its directory first. `What` names the content in the error. */
inline bool MCPWriteDumpFile(const FString& ResolvedPath, const FString& Text, const TCHAR* What, FString& OutError)
{
	const FString Directory = FPaths::GetPath(ResolvedPath);
	if (!Directory.IsEmpty() && !IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = FString::Printf(TEXT("Failed to create dump directory: %s"), *Directory);
		return false;
	}

	if (!FFileHelper::SaveStringToFile(Text, *ResolvedPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Failed to write %s: %s"), What, *ResolvedPath);
		return false;
	}

	return true;
}

// ── GC root RAII ─────────────────────────────────────────────────────────────

/** RAII: root a UObject on construction, unroot on scope exit. Prevents the
 *  AddToRoot/RemoveFromRoot pairs from leaking when an early return (validation
 *  error, import failure) sneaks into the middle of the pair.
 *
 *  Clears only what it set. The root set is a single flag, not a reference
 *  count: UObjectBaseUtility::AddToRoot is SetRootSet() and RemoveFromRoot is
 *  ClearRootSet(), each one atomic set or clear of
 *  EInternalObjectFlags::RootSet. So a scope that unconditionally cleared on
 *  exit would unroot an object some other party had rooted for its own
 *  reasons, and two nested scopes over the same object would leave it
 *  collectable the moment the INNER one exited while the outer one still
 *  believed it was protected. Recording whether this scope was the one that
 *  set the flag makes both cases correct: the inner scope is a no-op and the
 *  outer keeps its guarantee, and an object rooted elsewhere is left alone.
 *
 *  What it does not promise: if another party clears the flag during the
 *  scope, the object is unrooted from that moment. A single flag cannot
 *  express two owners, and no version of this class ever could. */
class FGCRootScope
{
public:
	explicit FGCRootScope(UObject* InObject) : Object(InObject)
	{
		if (Object && !Object->IsRooted())
		{
			Object->AddToRoot();
			bRootedHere = true;
		}
	}
	~FGCRootScope()
	{
		if (bRootedHere && Object && Object->IsRooted()) Object->RemoveFromRoot();
	}
	FGCRootScope(const FGCRootScope&) = delete;
	FGCRootScope& operator=(const FGCRootScope&) = delete;
private:
	UObject* Object = nullptr;
	bool bRootedHere = false;
};

// ── Reflection helpers ───────────────────────────────────────────────────────

/** Find a property by name and error out cleanly if missing. Returns nullptr
 *  and writes an error JSON to OutError when the property does not exist on
 *  the class, so callers get a typed response instead of a null deref. */
UE_DEPRECATED(5.4, "Unused by the bridge; call FindPropertyByName and build the error at the call site.")
inline FProperty* FindPropertyChecked(
	UClass* Cls,
	const TCHAR* PropertyName,
	TSharedPtr<FJsonValue>& OutError)
{
	if (!Cls)
	{
		OutError = MCPError(FString::Printf(TEXT("FindPropertyChecked('%s'): null class"), PropertyName));
		return nullptr;
	}
	FProperty* Prop = Cls->FindPropertyByName(FName(PropertyName));
	if (!Prop)
	{
		OutError = MCPError(FString::Printf(
			TEXT("Property '%s' not found on class '%s' - engine version drift?"),
			PropertyName, *Cls->GetName()));
	}
	return Prop;
}

// ── Thread context ───────────────────────────────────────────────────────────

/** Defence-in-depth: assert we are on the game thread. UObject API calls from
 *  a non-game thread can corrupt engine state. Handlers are dispatched from
 *  GameThreadExecutor, so this should always hold; when it doesn't, the
 *  assertion surfaces the bug loudly rather than producing a silent race. */
#define MCP_CHECK_GAME_THREAD() \
	checkf(IsInGameThread(), TEXT("MCP handler ran off the game thread - UObject access would be racy"))

// ── Object graph ─────────────────────────────────────────────────────────────

/** Objects an outer owns directly, skipping nested subobjects and anything
 *  already garbage. Spelled the way each engine wants it: 5.8 deprecated the
 *  bool form of GetObjectsWithOuter in favour of EGetObjectsFlags, and the enum
 *  does not exist before it. */
inline void MCPGetDirectSubobjects(const UObjectBase* Outer, TArray<UObject*>& OutObjects)
{
	if (!Outer)
	{
		return;
	}
#if UE_MCP_HAS_5_8_API
	GetObjectsWithOuter(Outer, OutObjects, EGetObjectsFlags::None,
		RF_NoFlags, EInternalObjectFlags::Garbage);
#else
	GetObjectsWithOuter(Outer, OutObjects, /*bIncludeNestedObjects*/ false,
		RF_NoFlags, EInternalObjectFlags::Garbage);
#endif
}

/** Every object under an outer, descending through nested subobjects, skipping
 *  anything already garbage. The twin of MCPGetDirectSubobjects for the callers
 *  that need the whole tree - a Control Rig's RigVM models, for instance, hang
 *  off collapsed nodes and the function library rather than off the blueprint
 *  directly, so a direct-only walk finds none of them.
 *
 *  Both spellings live here and nowhere else. This module is a unity build, so
 *  a file-local copy in a second .cpp is a C2084 redefinition the moment the
 *  adaptive-unity working set puts the two files in one blob. */
inline void MCPGetNestedSubobjects(const UObjectBase* Outer, TArray<UObject*>& OutObjects)
{
	if (!Outer)
	{
		return;
	}
#if UE_MCP_HAS_5_8_API
	GetObjectsWithOuter(Outer, OutObjects, EGetObjectsFlags::IncludeNestedObjects,
		RF_NoFlags, EInternalObjectFlags::Garbage);
#else
	GetObjectsWithOuter(Outer, OutObjects, /*bIncludeNestedObjects*/ true,
		RF_NoFlags, EInternalObjectFlags::Garbage);
#endif
}

// ── Component and collision lookup ──────────────────────────────────────────

/** The component of type T on Actor whose instance name equals Name, compared
 *  case-insensitively (#539). Exact names only: a mutation must never land on a
 *  sibling picked by class, prefix or substring. */
template <typename T = UActorComponent>
inline T* MCPFindComponentByName(AActor* Actor, const FString& Name)
{
	if (!Actor || Name.IsEmpty()) return nullptr;
	for (UActorComponent* Component : Actor->GetComponents())
	{
		T* Typed = Cast<T>(Component);
		if (Typed && Typed->GetName().Equals(Name, ESearchCase::IgnoreCase)) return Typed;
	}
	return nullptr;
}

/** Resolve a collision channel by the project's own channel name (so a
 *  GameTraceChannel renamed "Weapon" answers to "Weapon"), then the built-in
 *  names, then the ECollisionChannel enum spelling. A leading ECC_ is ignored.
 *  OutResolvedName is the name the channel matched under. */
inline bool MCPResolveCollisionChannel(const FString& InName, ECollisionChannel& OutChannel, FString& OutResolvedName)
{
	FString Name = InName.TrimStartAndEnd();
	if (Name.StartsWith(TEXT("ECC_"))) Name = Name.RightChop(4);
	if (Name.IsEmpty()) return false;

	if (const UCollisionProfile* Profile = UCollisionProfile::Get())
	{
		for (int32 Index = 0; Index < ECC_MAX; ++Index)
		{
			const FName ChannelName = Profile->ReturnChannelNameFromContainerIndex(Index);
			if (!ChannelName.IsNone() && ChannelName.ToString().Equals(Name, ESearchCase::IgnoreCase))
			{
				OutChannel = static_cast<ECollisionChannel>(Index);
				OutResolvedName = ChannelName.ToString();
				return true;
			}
		}
	}

	struct FBuiltInChannel { const TCHAR* Name; ECollisionChannel Channel; };
	static const FBuiltInChannel BuiltIns[] = {
		{ TEXT("WorldStatic"), ECC_WorldStatic }, { TEXT("WorldDynamic"), ECC_WorldDynamic },
		{ TEXT("Pawn"), ECC_Pawn }, { TEXT("Visibility"), ECC_Visibility },
		{ TEXT("Camera"), ECC_Camera }, { TEXT("PhysicsBody"), ECC_PhysicsBody },
		{ TEXT("Vehicle"), ECC_Vehicle }, { TEXT("Destructible"), ECC_Destructible },
	};
	for (const FBuiltInChannel& Entry : BuiltIns)
	{
		if (Name.Equals(Entry.Name, ESearchCase::IgnoreCase))
		{
			OutChannel = Entry.Channel;
			OutResolvedName = Entry.Name;
			return true;
		}
	}

	if (const UEnum* Enum = StaticEnum<ECollisionChannel>())
	{
		const int64 Value = Enum->GetValueByNameString(FString(TEXT("ECC_")) + Name);
		if (Value != INDEX_NONE && Value < ECC_MAX)
		{
			OutChannel = static_cast<ECollisionChannel>(Value);
			OutResolvedName = Name;
			return true;
		}
	}
	return false;
}

// ── Component bounds ─────────────────────────────────────────────────────────

/** A scene component's cached world-space bounds, spelled the way each engine
 *  wants it. 5.8 added USceneComponent::GetBounds(), an inline getter that
 *  returns a const reference to the public Bounds member; 5.7 and earlier offer
 *  the member alone. Both branches read the same field, so every supported
 *  engine returns the same value at the same freshness - the accessor is a
 *  rename, and UpdateBounds is still what refreshes what either one reads.
 *
 *  Bounds is public on 5.8 too, so the gate decides which spelling is used
 *  rather than which one compiles. Preferring the accessor there is what keeps
 *  these call sites correct on the engine that eventually makes the member
 *  private, which is the reason Epic added the getter.
 *
 *  Both spellings live here and nowhere else. This module is a unity build, so
 *  a file-local copy in a second .cpp is a C2084 redefinition the moment the
 *  adaptive-unity working set puts the two files in one blob. */
inline const FBoxSphereBounds& MCPComponentWorldBounds(const USceneComponent& Component)
{
#if UE_MCP_HAS_5_8_API
	return Component.GetBounds();
#else
	return Component.Bounds;
#endif
}
