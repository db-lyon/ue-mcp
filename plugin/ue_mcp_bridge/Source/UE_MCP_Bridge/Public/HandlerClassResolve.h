#pragma once

// Resolving a class or script struct from the spellings callers send, and the
// errors a name that resolves to nothing or to an unusable class returns.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Blueprint.h"
#include "HandlerResult.h"
#include "HandlerAssetResolve.h"

// ── Class name resolution (#823) ─────────────────────────────────────────────
//
// UE reflection registers a class under its C++ name minus the type prefix:
// AActor is the UClass named "Actor", UMyConfig is "MyConfig", and the path is
// /Script/MyGame.MyConfig with no "U" in it. Callers reading engine headers
// naturally pass the prefixed source name (or the prefixed path), every
// exact-match lookup missed, and the bridge answered "Class not found" for a
// class that had been loaded the whole time. Every string-to-UClass path in the
// plugin goes through MCPResolveClass so that one normalization covers all of
// them instead of each handler growing its own half of the rules.

namespace MCPClassResolve
{
	/** Strip one leading UE type prefix (A/U/F/E/I/S/T) when what follows still
	 *  looks like a class name: "UMyConfig" becomes "MyConfig". Names whose
	 *  second character is not upper case are left alone, so "Actor" and
	 *  "Texture2D" survive untouched. */
	inline FString StripPrefix(const FString& Name)
	{
		if (Name.Len() < 3) return Name;
		const TCHAR First = Name[0];
		const bool bIsPrefix =
			First == TEXT('A') || First == TEXT('U') || First == TEXT('F') ||
			First == TEXT('E') || First == TEXT('I') || First == TEXT('S') ||
			First == TEXT('T');
		if (!bIsPrefix || !FChar::IsUpper(Name[1])) return Name;
		return Name.RightChop(1);
	}

	/** Engine bookkeeping classes that must never win a fuzzy match. */
	inline bool IsTransientClassName(const FString& Name)
	{
		return Name.StartsWith(TEXT("SKEL_")) || Name.StartsWith(TEXT("REINST_")) ||
		       Name.StartsWith(TEXT("TRASHCLASS_")) || Name.StartsWith(TEXT("HOTRELOADED_")) ||
		       Name.StartsWith(TEXT("PLACEHOLDER-"));
	}

	/** Every spelling the resolver will try, in the order it tries them. */
	inline TArray<FString> BuildCandidates(const FString& Spec)
	{
		TArray<FString> Out;
		const FString Trimmed = Spec.TrimStartAndEnd();
		if (Trimmed.IsEmpty()) return Out;

		auto Add = [&Out](const FString& Candidate)
		{
			if (!Candidate.IsEmpty()) Out.AddUnique(Candidate);
		};

		// Object path form: /Script/Module.Class or /Game/Path/Asset[.Asset].
		if (Trimmed.StartsWith(TEXT("/")))
		{
			Add(Trimmed);
			FString PackagePart, ObjectPart;
			if (Trimmed.Split(TEXT("."), &PackagePart, &ObjectPart, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
			{
				const FString StrippedObject = StripPrefix(ObjectPart);
				if (StrippedObject != ObjectPart)
				{
					Add(PackagePart + TEXT(".") + StrippedObject);
				}
				if (!ObjectPart.EndsWith(TEXT("_C")))
				{
					Add(Trimmed + TEXT("_C"));
				}

				// A /Script path names a module, and modules move between them:
				// UPhysicalMaterial is /Script/PhysicsCore.PhysicalMaterial, not
				// /Script/Engine.PhysicalMaterial, which is the spelling older
				// documentation uses and the one this resolver's own "not found"
				// text recommends. The leaf name is added LAST, so an exact path
				// still wins and this is only reached when no spelling of the
				// path matched anything at all.
				if (PackagePart.StartsWith(TEXT("/Script/")))
				{
					Add(ObjectPart);
					const FString StrippedLeaf = StripPrefix(ObjectPart);
					if (StrippedLeaf != ObjectPart) Add(StrippedLeaf);
				}
			}
			else
			{
				// Package-only path: /Game/Cfg/MyAsset resolves via /Game/Cfg/MyAsset.MyAsset.
				FString Leaf = Trimmed;
				int32 SlashIndex = INDEX_NONE;
				if (Trimmed.FindLastChar(TEXT('/'), SlashIndex)) Leaf = Trimmed.RightChop(SlashIndex + 1);
				if (!Leaf.IsEmpty())
				{
					Add(Trimmed + TEXT(".") + Leaf);
					Add(Trimmed + TEXT(".") + Leaf + TEXT("_C"));
				}
			}
			return Out;
		}

		// "Module.Class" shorthand: promote it to the /Script path, both spellings.
		FString ModulePart, NamePart;
		if (Trimmed.Split(TEXT("."), &ModulePart, &NamePart, ESearchCase::CaseSensitive, ESearchDir::FromEnd) &&
		    !ModulePart.IsEmpty() && !NamePart.IsEmpty())
		{
			Add(FString::Printf(TEXT("/Script/%s.%s"), *ModulePart, *NamePart));
			const FString StrippedName = StripPrefix(NamePart);
			if (StrippedName != NamePart)
			{
				Add(FString::Printf(TEXT("/Script/%s.%s"), *ModulePart, *StrippedName));
			}
			return Out;
		}

		// Bare name: literal, then prefix-stripped, then the prefixes agents drop.
		Add(Trimmed);
		const FString Stripped = StripPrefix(Trimmed);
		Add(Stripped);
		Add(TEXT("A") + Trimmed);
		Add(TEXT("U") + Trimmed);
		if (Trimmed.EndsWith(TEXT("_C")))
		{
			const FString WithoutGenerated = Trimmed.LeftChop(2);
			Add(WithoutGenerated);
			Add(StripPrefix(WithoutGenerated));
		}
		else
		{
			// Blueprint generated class for a bare Blueprint name.
			Add(Trimmed + TEXT("_C"));
			if (Stripped != Trimmed) Add(Stripped + TEXT("_C"));
		}
		return Out;
	}

	/** One exact lookup. Paths go through FindObject and (optionally) a quiet
	 *  load; bare names go through FindFirstObject, which is the UE 5.6+
	 *  replacement for the "any package" FindObject pattern. */
	inline UClass* LookupExact(const FString& Candidate, bool bAllowLoad)
	{
		if (Candidate.IsEmpty()) return nullptr;
		if (Candidate.Contains(TEXT("/")))
		{
			if (UClass* Found = FindObject<UClass>(nullptr, *Candidate)) return Found;
			if (!bAllowLoad) return nullptr;
			if (UClass* Loaded = LoadObject<UClass>(nullptr, *Candidate, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				return Loaded;
			}
			return LoadClass<UObject>(nullptr, *Candidate, nullptr, LOAD_NoWarn | LOAD_Quiet, nullptr);
		}
		return FindFirstObject<UClass>(*Candidate, EFindFirstObjectOptions::NativeFirst);
	}

	/** Last resort: case-insensitive sweep of loaded classes against the same
	 *  candidate spellings. Native classes win ties so the answer stays stable
	 *  between sessions. */
	inline UClass* ScanCaseInsensitive(const TArray<FString>& Candidates)
	{
		UClass* NativeHit = nullptr;
		UClass* ContentHit = nullptr;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			const FString Name = It->GetName();
			if (IsTransientClassName(Name)) continue;

			bool bMatch = false;
			for (const FString& Candidate : Candidates)
			{
				if (Candidate.Contains(TEXT("/"))) continue;
				if (Name.Equals(Candidate, ESearchCase::IgnoreCase)) { bMatch = true; break; }
			}
			if (!bMatch) continue;

			const UPackage* Package = It->GetOutermost();
			const bool bNative = Package && Package->GetName().StartsWith(TEXT("/Script/"));
			if (bNative) { if (!NativeHit) NativeHit = *It; }
			else if (!ContentHit) { ContentHit = *It; }
		}
		return NativeHit ? NativeHit : ContentHit;
	}

	/** Loaded class names closest to what the caller asked for, for error text. */
	inline TArray<FString> Suggest(const FString& Spec, int32 MaxResults = 5)
	{
		TArray<FString> Result;
		FString Needle = StripPrefix(Spec.TrimStartAndEnd());
		int32 SeparatorIndex = INDEX_NONE;
		if (Needle.FindLastChar(TEXT('.'), SeparatorIndex)) Needle = Needle.RightChop(SeparatorIndex + 1);
		if (Needle.FindLastChar(TEXT('/'), SeparatorIndex)) Needle = Needle.RightChop(SeparatorIndex + 1);
		Needle = StripPrefix(Needle).ToLower();
		if (Needle.Len() < 3) return Result;

		struct FScored { int32 Score; FString Name; };
		TArray<FScored> Scored;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			const FString Name = It->GetName();
			if (IsTransientClassName(Name)) continue;
			const FString Lower = Name.ToLower();
			int32 Score;
			if (Lower == Needle)                 Score = 0;
			else if (Lower.StartsWith(Needle))   Score = 1;
			else if (Lower.EndsWith(Needle))     Score = 2;
			else if (Lower.Contains(Needle))     Score = 3;
			else continue;
			Scored.Add({ Score, Name });
		}
		Scored.Sort([](const FScored& A, const FScored& B)
		{
			if (A.Score != B.Score) return A.Score < B.Score;
			if (A.Name.Len() != B.Name.Len()) return A.Name.Len() < B.Name.Len();
			return A.Name < B.Name;
		});
		for (const FScored& Entry : Scored)
		{
			if (Result.Num() >= MaxResults) break;
			Result.AddUnique(Entry.Name);
		}
		return Result;
	}

	/** Full resolution. OutTried receives the candidate spellings in order. */
	inline UClass* Resolve(const FString& Spec, bool bAllowLoad, TArray<FString>* OutTried)
	{
		const TArray<FString> Candidates = BuildCandidates(Spec);
		if (OutTried) *OutTried = Candidates;
		for (const FString& Candidate : Candidates)
		{
			if (UClass* Found = LookupExact(Candidate, bAllowLoad)) return Found;
		}
		return ScanCaseInsensitive(Candidates);
	}

	/** Resolution constrained to subclasses of Base. Only reached when the
	 *  unconstrained answer is the wrong kind of class: "Timeline" must land on
	 *  the graph node, not on the component that shares the leaf name. */
	inline UClass* ResolveOfType(const FString& Spec, UClass* Base, bool bAllowLoad)
	{
		UClass* Direct = Resolve(Spec, bAllowLoad, nullptr);
		if (Direct && (!Base || Direct->IsChildOf(Base))) return Direct;
		if (!Base) return nullptr;

		const TArray<FString> Candidates = BuildCandidates(Spec);
		UClass* LooseHit = nullptr;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (!It->IsChildOf(Base)) continue;
			const FString Name = It->GetName();
			if (IsTransientClassName(Name)) continue;
			for (const FString& Candidate : Candidates)
			{
				if (Candidate.Contains(TEXT("/"))) continue;
				if (Name.Equals(Candidate, ESearchCase::CaseSensitive)) return *It;
				if (!LooseHit && Name.Equals(Candidate, ESearchCase::IgnoreCase)) LooseHit = *It;
			}
		}
		return LooseHit;
	}
}

/** Resolve a class name or path to a UClass, tolerating the C++ type prefix.
 *  Order: the literal spelling, the prefix-stripped spelling, prefixed
 *  spellings, the /Script/Module.Class path form, the Blueprint generated
 *  class, then a case-insensitive sweep. Pass bAllowLoad=false to keep the
 *  lookup non-loading (a hit then means "already in memory"). */
inline UClass* MCPResolveClass(const FString& Spec, bool bAllowLoad = true)
{
	return MCPClassResolve::Resolve(Spec, bAllowLoad, nullptr);
}

/** Same resolution, restricted to subclasses of Base. Use it wherever only one
 *  family of class is meaningful (graph nodes, schemas, factories) so a leaf
 *  name shared with an unrelated class cannot win. */
inline UClass* MCPResolveClassOfType(const FString& Spec, UClass* Base, bool bAllowLoad = true)
{
	return MCPClassResolve::ResolveOfType(Spec, Base, bAllowLoad);
}

/** "Class not found", with the exact spellings tried and the closest loaded
 *  class names, so a caller can correct the argument without guessing. */
inline TSharedPtr<FJsonValue> MCPClassNotFoundError(
	const FString& Spec,
	const FString& ParamName = TEXT("className"))
{
	const TArray<FString> Tried = MCPClassResolve::BuildCandidates(Spec);
	const TArray<FString> Suggestions = MCPClassResolve::Suggest(Spec);

	FString Message = FString::Printf(
		TEXT("Class not found for %s '%s'. Tried: %s. UE reflection stores class names without the C++ type prefix, so UMyConfig is registered as 'MyConfig' and its path is /Script/<Module>.MyConfig."),
		*ParamName, *Spec, *FString::Join(Tried, TEXT(", ")));
	if (Suggestions.Num() > 0)
	{
		Message += FString::Printf(TEXT(" Closest loaded classes: %s."), *FString::Join(Suggestions, TEXT(", ")));
	}
	else
	{
		Message += TEXT(" No loaded class name resembles it: the owning module may not be loaded yet (check reflection(is_module_loaded)).");
	}

	TSharedPtr<FJsonObject> Obj = MCPErrorObject(Message);
	Obj->SetStringField(TEXT("reason"), TEXT("class_not_found"));
	Obj->SetStringField(TEXT("requested"), Spec);
	TArray<TSharedPtr<FJsonValue>> TriedJson;
	for (const FString& Candidate : Tried) TriedJson.Add(MakeShared<FJsonValueString>(Candidate));
	Obj->SetArrayField(TEXT("tried"), TriedJson);
	TArray<TSharedPtr<FJsonValue>> SuggestJson;
	for (const FString& Name : Suggestions) SuggestJson.Add(MakeShared<FJsonValueString>(Name));
	Obj->SetArrayField(TEXT("suggestions"), SuggestJson);
	return MakeShared<FJsonValueObject>(Obj);
}

/** The name resolved but the class cannot be used here. Reported separately
 *  from "not found" so a caller stops re-spelling a name that was correct. */
inline TSharedPtr<FJsonValue> MCPClassUnusableError(
	const FString& Spec,
	UClass* Resolved,
	const FString& Reason,
	const FString& Detail)
{
	TSharedPtr<FJsonObject> Obj = MCPErrorObject(FString::Printf(
		TEXT("Class '%s' resolved to %s but cannot be used here: %s"),
		*Spec, Resolved ? *Resolved->GetPathName() : TEXT("<null>"), *Detail));
	Obj->SetStringField(TEXT("reason"), Reason);
	Obj->SetStringField(TEXT("requested"), Spec);
	if (Resolved)
	{
		Obj->SetStringField(TEXT("resolvedClass"), Resolved->GetName());
		Obj->SetStringField(TEXT("resolvedPath"), Resolved->GetPathName());
	}
	return MakeShared<FJsonValueObject>(Obj);
}

/** Guard a resolved class: concrete (optional) and derived from RequiredBase
 *  (optional). Returns an error value to return directly, or an unset pointer
 *  when the class is fine. */
inline TSharedPtr<FJsonValue> MCPCheckClassUsable(
	const FString& Spec,
	UClass* Resolved,
	UClass* RequiredBase = nullptr,
	bool bRequireConcrete = true)
{
	if (!Resolved) return MCPClassNotFoundError(Spec);
	if (RequiredBase && !Resolved->IsChildOf(RequiredBase))
	{
		return MCPClassUnusableError(Spec, Resolved, TEXT("wrong_base"), FString::Printf(
			TEXT("it does not derive from %s (its parent chain starts at %s)"),
			*RequiredBase->GetName(),
			Resolved->GetSuperClass() ? *Resolved->GetSuperClass()->GetName() : TEXT("none")));
	}
	if (bRequireConcrete && Resolved->HasAnyClassFlags(CLASS_Abstract))
	{
		return MCPClassUnusableError(Spec, Resolved, TEXT("abstract"),
			TEXT("it is abstract, so it cannot be instantiated. Pass a concrete subclass."));
	}
	if (bRequireConcrete && Resolved->HasAnyClassFlags(CLASS_Deprecated))
	{
		return MCPClassUnusableError(Spec, Resolved, TEXT("deprecated"),
			TEXT("it is deprecated and the engine refuses to instantiate it."));
	}
	return TSharedPtr<FJsonValue>();
}

/** Alias of MCPResolveClass, kept for plugins built against this header. */
UE_DEPRECATED(5.4, "FindClassByShortName is an alias of MCPResolveClass. Call MCPResolveClass.")
inline UClass* FindClassByShortName(const FString& ClassName)
{
	return MCPResolveClass(ClassName);
}

/** Resolve a script struct. Qualified names resolve as written, then with one
 *  leading F stripped from the /Script leaf. Short names try the literal, then
 *  one F stripped, then one F added (bTryAddedF only). A short spelling shared
 *  by several loaded structs fails, and OutError lists their qualified paths. */
inline UScriptStruct* MCPResolveScriptStruct(const FString& Spec, FString* OutError = nullptr, bool bTryAddedF = false)
{
	if (OutError) OutError->Reset();
	if (Spec.IsEmpty()) return nullptr;
	const bool bShortName = !Spec.Contains(TEXT("/")) && !Spec.Contains(TEXT("."));
	bool bAmbiguous = false;
	const auto Lookup = [&](const FString& Name) -> UScriptStruct*
	{
		if (!bShortName)
		{
			if (Name.StartsWith(TEXT("/Script/"))) return FindObject<UScriptStruct>(nullptr, *Name);
			return Cast<UScriptStruct>(MCPLoadAssetObject(Name));
		}
		const FName ShortName(*Name, FNAME_Find);
		if (ShortName.IsNone()) return nullptr;
		TArray<UObject*> Found;
		StaticFindAllObjectsFast(Found, UScriptStruct::StaticClass(), ShortName);
		UScriptStruct* Match = nullptr;
		TArray<FString> Paths;
		for (UObject* Obj : Found)
		{
			if (!IsValid(Obj)) continue;
			Match = CastChecked<UScriptStruct>(Obj);
			Paths.AddUnique(Obj->GetPathName());
		}
		if (Paths.Num() <= 1) return Match;
		bAmbiguous = true;
		if (OutError)
		{
			Paths.Sort();
			*OutError = FString::Printf(
				TEXT("Struct name '%s' is ambiguous: %d loaded structs are named '%s'. Pass one of these qualified paths instead: %s"),
				*Spec, Paths.Num(), *Name, *FString::Join(Paths, TEXT(", ")));
		}
		return nullptr;
	};
	if (UScriptStruct* Found = Lookup(Spec)) return Found;
	if (bAmbiguous) return nullptr;

	if (bShortName || Spec.StartsWith(TEXT("/Script/")))
	{
		const int32 LeafStart = Spec.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd) + 1;
		if (Spec.Len() > LeafStart + 1 && Spec[LeafStart] == TEXT('F'))
		{
			if (UScriptStruct* Found = Lookup(Spec.Left(LeafStart) + Spec.Mid(LeafStart + 1))) return Found;
			if (bAmbiguous) return nullptr;
		}
	}
	return (bShortName && bTryAddedF) ? Lookup(TEXT("F") + Spec) : nullptr;
}
