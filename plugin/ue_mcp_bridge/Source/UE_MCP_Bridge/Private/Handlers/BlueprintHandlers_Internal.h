#pragma once

// Helpers shared between BlueprintHandlers.cpp and BlueprintHandlers_Graph.cpp
// after the file was split. Kept in Private/ because it is internal to the
// plugin - no downstream code is expected to include this.

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Class.h"
#include "UObject/Interface.h"

class UBlueprint;
class UActorComponent;

// ── Inherited interfaces ────────────────────────────────────────────────────
//
// UClass::Interfaces is per-class: it lists what THAT class declares and
// nothing it inherited. Reading Blueprint->ParentClass->Interfaces therefore
// answers a one-level question, and an interface declared on a GRANDPARENT
// reads as absent. UClass::ImplementsInterface exists because the real answer
// needs the class's super chain and, for each interface found there, that
// interface's own super chain.
//
// ImplementsInterface returns a bool, and add_blueprint_interface and
// remove_blueprint_interface also have to NAME the class the contract came
// from, while list_blueprint_interfaces has to enumerate the whole set. The
// walk below is the single definition of that reach and answers all three
// questions, so the three handlers read it rather than each rolling their own,
// and none of them asks ImplementsInterface separately - the derivation on the
// walk shows why a second ask could only ever agree.

/** Every interface StartClass implements, each paired with the nearest ancestor
 *  class that brings it in. Nearest wins, so the pair names the class a caller
 *  should be pointed at.
 *
 *  The reach is the one UClass::ImplementsInterface has (Class.cpp:6265-6284 in
 *  the 5.7 source tree): outer loop over the super chain of the class, inner
 *  test `InterfaceClass->IsChildOf(SomeInterface)` against each entry of that
 *  class's own Interfaces array, and UInterface itself rejected up front.
 *
 *  That equality is what lets a caller use this walk INSTEAD of asking
 *  ImplementsInterface and then asking here for a name. ImplementsInterface
 *  answers true for an interface I exactly when some class C on the super chain
 *  has an entry E with E.Class->IsChildOf(I), which is to say I sits on
 *  E.Class's own super chain; and this walk records every class on that same
 *  chain, from E.Class up to but excluding UInterface, which is the same point
 *  ImplementsInterface stops at because it refuses UInterface as a query
 *  outright. Neither reaches UObject: it is above UInterface here, and it fails
 *  the CLASS_Interface test there. The `Seen` break does not narrow the set it
 *  produces, only the work: it fires when a nearer provider already walked this
 *  same chain past this point, and that walk ran to UInterface.
 *
 *  Every recorded pair carries a non-null provider. `Current` is the loop
 *  variable of a walk that starts at StartClass and ends when it becomes null,
 *  so it is never null inside the body. A caller therefore has two states, not
 *  three: not implemented, or implemented with the declaring class named.
 *  "Implemented but the declaring class is unknown" cannot arise, and code that
 *  branched on it was branching on nothing. */
inline void GatherImplementedInterfaces(UClass* StartClass, TArray<TPair<UClass*, UClass*>>& OutInterfaceAndProvider)
{
	TSet<UClass*> Seen;
	for (UClass* Current = StartClass; Current; Current = Current->GetSuperClass())
	{
		for (const FImplementedInterface& Declared : Current->Interfaces)
		{
			// An interface may extend another interface, and a class that
			// declares the derived one implements the base too. That relation
			// is the interface's SUPER CHAIN, not its Interfaces array: a
			// derived interface is written `UUserObjectListEntry : public
			// UUserListEntry` (IUserObjectListEntry.h:12-16), so the base
			// arrives as SuperClass. ImplementsInterface reads it the same way,
			// through IsChildOf, and stops before UInterface because it refuses
			// UInterface as a query outright. Walking the chain here is what
			// keeps the two in step; walking Interfaces would not, because on
			// an interface class that array is empty.
			for (UClass* Iface = Declared.Class;
				Iface && Iface != UInterface::StaticClass();
				Iface = Iface->GetSuperClass())
			{
				// Already recorded means recorded by a nearer provider, and
				// with it everything above it on this same chain, so stop.
				if (Seen.Contains(Iface)) break;
				Seen.Add(Iface);
				OutInterfaceAndProvider.Emplace(Iface, Current);
			}
		}
	}
}

/** The nearest ancestor of StartClass (StartClass itself included) that brings
 *  InterfaceClass in, or nullptr. Built on the gather above so the name a
 *  handler reports and the set another handler lists are read off one walk.
 *
 *  A non-null answer is also the answer StartClass->ImplementsInterface(
 *  InterfaceClass) gives, per the derivation above, so callers that need both
 *  the yes/no and the name ask this once rather than asking twice. */
inline UClass* FindInterfaceProvider(UClass* StartClass, const UClass* InterfaceClass)
{
	if (!StartClass || !InterfaceClass) return nullptr;
	TArray<TPair<UClass*, UClass*>> Implemented;
	GatherImplementedInterfaces(StartClass, Implemented);
	for (const TPair<UClass*, UClass*>& Pair : Implemented)
	{
		if (Pair.Key == InterfaceClass) return Pair.Value;
	}
	return nullptr;
}

// ── Graph selectors ─────────────────────────────────────────────────────────
//
// list_blueprint_graphs hands callers a `selector` they can pass back as
// graphName, and duplicate names (AnimBP "Transition", "Locomotion") get an
// index suffix. search_call_sites has to report the same selector for the same
// graph or the two disagree about how to address one graph, so the rule lives
// here rather than being restated per caller.

/** "Locomotion" when the name is unique, "Locomotion[3]" when it is not. */
inline FString MakeGraphSelector(const FString& Name, int32 DuplicateIndex, int32 DuplicateCount)
{
	return DuplicateCount > 1
		? FString::Printf(TEXT("%s[%d]"), *Name, DuplicateIndex)
		: Name;
}

/** How many graphs share each name, which is what decides whether a selector
 *  needs its index suffix. */
inline void CountGraphNames(const TArray<UEdGraph*>& Graphs, TMap<FString, int32>& OutNameCounts)
{
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph)
		{
			++OutNameCounts.FindOrAdd(Graph->GetName());
		}
	}
}

// ── Graph dump files ────────────────────────────────────────────────────────
//
// read_graph writes an oversized result to a file under Saved/UE_MCP rather
// than returning it inline. search_call_sites follows the same convention, so
// both use the same path builder and the same writer.

inline FString MakeDefaultGraphDumpPath(const FString& AssetPath, const FString& GraphName)
{
	const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
	const FString PathHash = FString::Printf(TEXT("%08x"), GetTypeHash(AssetPath + TEXT(":") + GraphName));
	const FString BaseName = FPaths::MakeValidFileName(AssetName + TEXT("_") + GraphName + TEXT("_") + PathHash);
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UE_MCP"), TEXT("GraphDumps"), BaseName + TEXT(".json"));
}

inline bool WriteJsonObjectToFile(
	const TSharedPtr<FJsonObject>& JsonObject,
	const FString& RequestedPath,
	const FString& AssetPath,
	const FString& GraphName,
	FString& OutResolvedPath,
	FString& OutError)
{
	OutResolvedPath = RequestedPath.IsEmpty() ? MakeDefaultGraphDumpPath(AssetPath, GraphName) : RequestedPath;
	if (FPaths::IsRelative(OutResolvedPath))
	{
		OutResolvedPath = FPaths::Combine(FPaths::ProjectSavedDir(), OutResolvedPath);
	}

	const FString Directory = FPaths::GetPath(OutResolvedPath);
	if (!Directory.IsEmpty() && !IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = FString::Printf(TEXT("Failed to create dump directory: %s"), *Directory);
		return false;
	}

	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	if (!FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer))
	{
		OutError = TEXT("Failed to serialize graph JSON");
		return false;
	}

	if (!FFileHelper::SaveStringToFile(JsonText, *OutResolvedPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Failed to write graph dump: %s"), *OutResolvedPath);
		return false;
	}

	return true;
}

// Resolve the named component template on a blueprint, honouring inheritance.
// See definition in BlueprintHandlers_Graph.cpp for the full contract (bForWrite
// semantics, ICH-override creation on write, CDO fallback on read, etc.).
UActorComponent* ResolveComponentTemplate(
	UBlueprint* Blueprint,
	const FString& ComponentName,
	bool bForWrite,
	bool& bOutIsInherited,
	TArray<FString>& OutAvailable);

// #942: a level script Blueprint is not an asset of its own. It lives inside
// the map package at "<Map>.<Map>:PersistentLevel.<Map>", so every Blueprint
// action handed the umap path a caller actually has answered "Blueprint not
// found". FBlueprintHandlers::LoadBlueprint now resolves a World path to that
// object; the two helpers below carry the shared reporting around it, so read,
// list_graphs, read_graph and get_execution_flow behave identically.
//
// Builds the "not found" response for a failed Blueprint lookup. When the path
// names a World whose level script has never been created, the message says so
// and prints the object path, rather than claiming the Blueprint is missing.
TSharedPtr<FJsonValue> BlueprintNotFoundError(const FString& AssetPath);

// Record which object actually answered the request. A caller that passed a
// umap path gets back the level script Blueprint's object path, so the alias it
// used is visible rather than implied.
void AnnotateResolvedBlueprint(const TSharedPtr<FJsonObject>& Result, UBlueprint* Blueprint);
