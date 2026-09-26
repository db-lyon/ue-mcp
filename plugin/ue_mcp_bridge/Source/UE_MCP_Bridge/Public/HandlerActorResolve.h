#pragma once

// Resolving an actor selector to exactly one actor, refusing ambiguity, and the
// hints a lookup that matched nothing returns.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HandlerResult.h"
#include "HandlerParams.h"

// ── Actor selection ──────────────────────────────────────────────────────────
//
// Editor labels are NOT unique. A copy-pasted Blueprint gives every copy the
// same label, and a label lookup that answers with "the first actor the
// iterator reached" is a coin flip decided by streaming order. #983 is what
// that costs: several actors labelled BP_SnappyRoad2, a write aimed at the one
// the user had selected, and the edit landing on a road at the other end of
// the map with a success response and nothing to suggest a choice was made.
//
// So there is one resolver, and it refuses rather than guesses:
//
//   * 'actorPath' is the unambiguous selector and wins whenever it is given.
//   * A label naming more than one actor is an error listing every candidate
//     and its actorPath, so the caller can retry precisely.
//   * There is no "just pick one" override. The precise selector already
//     exists, so a caller who wants a specific one of the duplicates has a
//     correct answer, and a caller who wants all of them is asking for a
//     different, plural action. A flag that picks an arbitrary actor out of a
//     set the caller could not tell apart is the same silent wrong write with
//     a name on it.
//
// The plural need is served by MCPCollectActorsByToken, which returns every
// match and is what the ignore-list and reference-list parameters use.

/** How a selector token is allowed to match an actor. */
enum class EMCPActorMatch : uint8
{
	/** Editor label only. */
	Label,
	/** Editor label, then the internal UObject name. */
	LabelOrName,
	/** Editor label, then internal name, then the full object path. */
	LabelNameOrPath,
};

/** The actor in World whose full object path is Path, or nullptr. Paths are
 *  unique, so there is never a choice to make. Accepts the export-text form
 *  (Actor'/Game/...') and falls back to a case-insensitive compare, because a
 *  path that came back from one action and was pasted into another is the
 *  whole point of having it. */
inline AActor* MCPFindActorByPath(UWorld* World, const FString& Path)
{
	if (!World || Path.IsEmpty()) return nullptr;
	FString Wanted = FPackageName::ExportTextPathToObjectPath(Path);
	Wanted.TrimStartAndEndInline();
	if (Wanted.IsEmpty()) return nullptr;
	AActor* CaseInsensitive = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!IsValid(A)) continue;
		const FString Actual = A->GetPathName();
		if (Actual.Equals(Wanted, ESearchCase::CaseSensitive)) return A;
		if (!CaseInsensitive && Actual.Equals(Wanted, ESearchCase::IgnoreCase)) CaseInsensitive = A;
	}
	return CaseInsensitive;
}

/** Every actor the token names under Match, sorted by object path so the order
 *  is the same on every run rather than whatever the actor iterator happened
 *  to produce that time.
 *
 *  The passes do not blend: a token that is one actor's label and another
 *  actor's internal name resolves to the label match alone, because the label
 *  is what the outliner shows and what a caller types. Name and path answer
 *  only when the label pass found nothing (#806). */
inline void MCPCollectActorsByToken(
	UWorld* World,
	const FString& Token,
	EMCPActorMatch Match,
	TArray<AActor*>& OutMatches)
{
	OutMatches.Reset();
	if (!World || Token.IsEmpty()) return;

	TArray<AActor*> ByName;
	TArray<AActor*> ByPath;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!IsValid(A)) continue;
		if (A->GetActorLabel() == Token) { OutMatches.Add(A); continue; }
		if (Match != EMCPActorMatch::Label && A->GetName() == Token) { ByName.Add(A); continue; }
		if (Match == EMCPActorMatch::LabelNameOrPath && A->GetPathName() == Token) { ByPath.Add(A); }
	}
	if (OutMatches.Num() == 0) OutMatches = MoveTemp(ByName);
	if (OutMatches.Num() == 0) OutMatches = MoveTemp(ByPath);

	OutMatches.Sort([](const AActor& A, const AActor& B)
	{
		return A.GetPathName().Compare(B.GetPathName()) < 0;
	});
}

/** Which pass of MCPCollectActorsByToken produced a match, for the message. */
inline const TCHAR* MCPDescribeActorMatchTier(const FString& Token, AActor* Match)
{
	if (Match && Match->GetActorLabel() == Token) return TEXT("editor label");
	if (Match && Match->GetName() == Token) return TEXT("internal object name");
	return TEXT("object path");
}

/** One candidate row in an ambiguity refusal: enough to tell two same-labelled
 *  actors apart without a follow-up call, plus the actorPath to retry with. */
inline TSharedPtr<FJsonObject> MCPDescribeActorCandidate(AActor* Actor)
{
	TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
	if (!Actor) return Row;
	Row->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Row->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Row->SetStringField(TEXT("actorName"), Actor->GetName());
	Row->SetStringField(TEXT("actorClass"), Actor->GetClass()->GetName());
	Row->SetStringField(TEXT("folderPath"), Actor->GetFolderPath().ToString());
	const FVector Loc = Actor->GetActorLocation();
	TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
	LocObj->SetNumberField(TEXT("x"), Loc.X);
	LocObj->SetNumberField(TEXT("y"), Loc.Y);
	LocObj->SetNumberField(TEXT("z"), Loc.Z);
	Row->SetObjectField(TEXT("location"), LocObj);
	return Row;
}

/** The refusal an ambiguous selector produces. Lists every candidate and its
 *  actorPath, so the retry is a copy of one field rather than a hunt back
 *  through get_outliner. */
inline TSharedPtr<FJsonValue> MCPAmbiguousActorError(
	const FString& Token,
	const TCHAR* LabelKey,
	const TCHAR* PathKey,
	const TCHAR* MatchedBy,
	const TArray<AActor*>& Candidates)
{
	const int32 Cap = 25;
	TSharedPtr<FJsonObject> Obj = MCPErrorObject(FString::Printf(
		TEXT("Ambiguous actor selector: '%s' is the %s of %d actors. Editor labels are not unique, so this call refuses rather than picking one of them. Retry with '%s' set to one of the candidate paths below."),
		*Token, MatchedBy, Candidates.Num(), PathKey));
	Obj->SetBoolField(TEXT("ambiguous"), true);
	Obj->SetStringField(TEXT("selector"), LabelKey);
	Obj->SetStringField(TEXT("selectorValue"), Token);
	Obj->SetStringField(TEXT("matchedBy"), MatchedBy);
	Obj->SetNumberField(TEXT("matchCount"), Candidates.Num());
	TArray<TSharedPtr<FJsonValue>> Rows;
	for (int32 Index = 0; Index < Candidates.Num() && Index < Cap; ++Index)
	{
		Rows.Add(MakeShared<FJsonValueObject>(MCPDescribeActorCandidate(Candidates[Index])));
	}
	Obj->SetArrayField(TEXT("candidates"), Rows);
	if (Candidates.Num() > Cap) Obj->SetBoolField(TEXT("candidatesTruncated"), true);
	return MakeShared<FJsonValueObject>(Obj);
}

/** True when a resolver failure was a refusal to choose rather than a miss.
 *  An action that treats an absent actor as a no-op still has to fail on an
 *  ambiguous one: "already deleted" is the wrong answer when three actors
 *  carry the label and none of them was touched. */
inline bool MCPIsAmbiguousActorError(const TSharedPtr<FJsonValue>& Error)
{
	if (!Error.IsValid() || Error->Type != EJson::Object) return false;
	const TSharedPtr<FJsonObject> Obj = Error->AsObject();
	bool bAmbiguous = false;
	return Obj.IsValid() && Obj->TryGetBoolField(TEXT("ambiguous"), bAmbiguous) && bAmbiguous;
}

// Legacy first-match actor lookups, kept only for plugins built against this
// public header (#983). They pick one of several same-labelled actors silently;
// use MCPResolveActor, which refuses an ambiguous label.
/** Legacy: first actor whose editor label matches. Prefer MCPResolveActor. */
UE_DEPRECATED(5.4, "FindActorByLabel picks the first of several same-labelled actors. Use MCPResolveActor.")
inline AActor* FindActorByLabel(UWorld* World, const FString& Label)
{
	TArray<AActor*> Matches;
	MCPCollectActorsByToken(World, Label, EMCPActorMatch::Label, Matches);
	return Matches.Num() > 0 ? Matches[0] : nullptr;
}

/** Legacy: first actor matching an editor label or internal name.
 *  Prefer MCPResolveActor. */
UE_DEPRECATED(5.4, "FindActorByLabelOrName picks the first of several matching actors. Use MCPResolveActor.")
inline AActor* FindActorByLabelOrName(UWorld* World, const FString& LabelOrName)
{
	TArray<AActor*> Matches;
	MCPCollectActorsByToken(World, LabelOrName, EMCPActorMatch::LabelOrName, Matches);
	return Matches.Num() > 0 ? Matches[0] : nullptr;
}

/** Legacy: an actor by label, or by full object path when the label is empty.
 *  Prefer MCPResolveActor. */
UE_DEPRECATED(5.4, "FindActorByLabelOrPath picks the first of several same-labelled actors. Use MCPResolveActor.")
inline AActor* FindActorByLabelOrPath(UWorld* World, const FString& Label, const FString& Path)
{
	if (!Path.IsEmpty())
	{
		if (AActor* ByPath = MCPFindActorByPath(World, Path)) return ByPath;
	}
	if (Label.IsEmpty()) return nullptr;
	TArray<AActor*> Matches;
	MCPCollectActorsByToken(World, Label, EMCPActorMatch::Label, Matches);
	return Matches.Num() > 0 ? Matches[0] : nullptr;
}

/** Legacy: first actor matching a label, internal name or object path.
 *  Prefer MCPResolveActor. */
UE_DEPRECATED(5.4, "FindActorByLabelNameOrPath picks the first of several matching actors. Use MCPResolveActor.")
inline AActor* FindActorByLabelNameOrPath(UWorld* World, const FString& Token)
{
	TArray<AActor*> Matches;
	MCPCollectActorsByToken(World, Token, EMCPActorMatch::LabelNameOrPath, Matches);
	return Matches.Num() > 0 ? Matches[0] : nullptr;
}

/** Build the "no such actor" message for a failed label/name/path lookup.
 *  Names what was searched and offers the labels that contain the token, so a
 *  caller that guessed a label sees the real one instead of a bare miss. */
inline FString MCPDescribeActorLookupMiss(
	UWorld* World,
	const FString& Token,
	const FString& WorldLabel,
	EMCPActorMatch Match = EMCPActorMatch::LabelNameOrPath)
{
	int32 ActorCount = 0;
	TArray<FString> Near;
	if (World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* A = *It;
			if (!IsValid(A)) continue;
			++ActorCount;
			if (Near.Num() < 8 && A->GetActorLabel().Contains(Token))
			{
				Near.Add(A->GetActorLabel());
			}
		}
	}
	// Name what was actually searched. Claiming a name and path sweep that a
	// label-only action never ran sends a caller looking for a typo in the
	// wrong field.
	const TCHAR* Searched =
		Match == EMCPActorMatch::Label ? TEXT("by editor label")
		: Match == EMCPActorMatch::LabelOrName ? TEXT("by editor label, then by internal object name")
		: TEXT("by editor label, then by internal object name, then by full object path");
	FString Msg = FString::Printf(
		TEXT("Actor '%s' not found in the %s world. Searched every placed actor %s (%d actors). Pass actorPath for an exact object path when a label is ambiguous or absent."),
		*Token, *WorldLabel, Searched, ActorCount);
	if (Near.Num() > 0)
	{
		Msg += FString::Printf(TEXT(" Labels containing that text: [%s]."), *FString::Join(Near, TEXT(", ")));
	}
	Msg += TEXT(" List the real labels with level(get_outliner).");
	return Msg;
}

/** Which parameters carry the actor selector for one action, and how far the
 *  label token is allowed to reach.
 *
 *  Handlers that name their actor something other than 'actorLabel' pass the
 *  pair explicitly. The convention is that the path key is the label key with
 *  its "Label" suffix swapped for "Path" (childLabel / childPath), so a caller
 *  can guess it correctly. */
struct FMCPActorSelector
{
	/** Parameter carrying the editor label (or the label/name/path token). */
	const TCHAR* LabelKey = TEXT("actorLabel");
	/** Parameter carrying the unambiguous full object path. */
	const TCHAR* PathKey = TEXT("actorPath");
	/** A second spelling of the label parameter, for actions that shipped
	 *  with two (get_relative_transform takes 'target' or 'targetLabel').
	 *  Read only when LabelKey is absent. */
	const TCHAR* AltLabelKey = nullptr;
	/** How far LabelKey's value is allowed to reach. */
	EMCPActorMatch Match = EMCPActorMatch::Label;
	/** When false, an absent selector is not an error: the resolver returns
	 *  nullptr with OutError left unset and the caller decides what that
	 *  means (an optional target, or a second selection route). */
	bool bRequired = true;
	/** Names the world in the miss message: "editor", "PIE". */
	const TCHAR* WorldLabel = TEXT("editor");
};

/** Resolve one actor from an already-extracted token. Returns nullptr and
 *  writes OutError on a miss or on ambiguity; the caller returns OutError
 *  unchanged. Used where the token did not come from a parameter of its own
 *  (a list entry, a fixed label). */
inline AActor* MCPResolveActorToken(
	UWorld* World,
	const FString& Token,
	TSharedPtr<FJsonValue>& OutError,
	const FMCPActorSelector& Selector = FMCPActorSelector())
{
	OutError.Reset();
	if (!World)
	{
		OutError = MCPError(TEXT("Editor world not available"));
		return nullptr;
	}
	TArray<AActor*> Matches;
	MCPCollectActorsByToken(World, Token, Selector.Match, Matches);
	if (Matches.Num() == 1) return Matches[0];
	if (Matches.Num() > 1)
	{
		OutError = MCPAmbiguousActorError(
			Token, Selector.LabelKey, Selector.PathKey,
			MCPDescribeActorMatchTier(Token, Matches[0]), Matches);
		return nullptr;
	}
	OutError = MCPError(MCPDescribeActorLookupMiss(World, Token, Selector.WorldLabel, Selector.Match));
	return nullptr;
}

/** THE actor resolver. Reads the unambiguous path selector first, then the
 *  label, and refuses when the label names more than one actor.
 *
 *  A path that names nothing is an error rather than a quiet fall-through to
 *  the label: the path is the precise selector, and demoting a precise miss to
 *  a fuzzy hit is how the wrong actor gets edited in the first place.
 *
 *  Returns nullptr on every failure with OutError carrying the response to
 *  return. When Selector.bRequired is false and neither key was supplied,
 *  returns nullptr with OutError unset. */
inline AActor* MCPResolveActor(
	UWorld* World,
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonValue>& OutError,
	const FMCPActorSelector& Selector = FMCPActorSelector())
{
	OutError.Reset();

	FString Path;
	FString Token;
	if (Params.IsValid())
	{
		MCPNoteParamRead(Params, Selector.PathKey);
		MCPNoteParamRead(Params, Selector.LabelKey);
		Params->TryGetStringField(Selector.PathKey, Path);
		Params->TryGetStringField(Selector.LabelKey, Token);
		if (Token.IsEmpty() && Selector.AltLabelKey)
		{
			MCPNoteParamRead(Params, Selector.AltLabelKey);
			Params->TryGetStringField(Selector.AltLabelKey, Token);
		}
	}
	Path.TrimStartAndEndInline();
	Token.TrimStartAndEndInline();

	if (Path.IsEmpty() && Token.IsEmpty())
	{
		if (Selector.bRequired)
		{
			OutError = MCPError(FString::Printf(
				TEXT("Missing required parameter '%s' (or '%s'). Editor labels are not unique, so '%s' is the selector to prefer when you have one."),
				Selector.LabelKey, Selector.PathKey, Selector.PathKey));
		}
		return nullptr;
	}

	if (!World)
	{
		OutError = MCPError(TEXT("Editor world not available"));
		return nullptr;
	}

	if (!Path.IsEmpty())
	{
		if (AActor* ByPath = MCPFindActorByPath(World, Path)) return ByPath;
		OutError = MCPError(FString::Printf(
			TEXT("No actor at '%s' in the %s world. Object paths look like /Game/Maps/Map.Map:PersistentLevel.Actor_0; level(get_outliner) reports the real one for every actor."),
			*Path, Selector.WorldLabel));
		return nullptr;
	}

	return MCPResolveActorToken(World, Token, OutError, Selector);
}

/** Spawn-by-label idempotency check. If World already has an actor with the
 *  given Label, returns a fully-formed "already existed" result the caller
 *  can return directly (or an MCPError when OnConflict == "error"). When
 *  Label is empty or no match exists, returns an unset shared pointer so the
 *  caller proceeds to spawn. Mirrors MCPCheckAssetExists's contract for
 *  in-world actors.
 *
 *  #983: this asks "does this label already name something", so several
 *  matches is an answer rather than a refusal, and refusing here would break
 *  a rerun of a spawn that is meant to be idempotent. But it hands back an
 *  actorPath the caller may then write to, so it must not be an arbitrary
 *  one: the search is the shared, path-sorted one, and when the label names
 *  several actors the result says so and lists them all instead of presenting
 *  one as though it were the only. */
inline TSharedPtr<FJsonValue> MCPCheckActorLabelExists(
	UWorld* World,
	const FString& Label,
	const FString& OnConflict,
	const FString& FriendlyType = TEXT("Actor"))
{
	if (!World || Label.IsEmpty()) return TSharedPtr<FJsonValue>();
	TArray<AActor*> Matches;
	MCPCollectActorsByToken(World, Label, EMCPActorMatch::Label, Matches);
	if (Matches.Num() == 0) return TSharedPtr<FJsonValue>();

	if (OnConflict == TEXT("error"))
	{
		return MCPError(FString::Printf(TEXT("%s '%s' already exists"), *FriendlyType, *Label));
	}

	auto Existing = MCPSuccess();
	MCPSetExisted(Existing);
	Existing->SetStringField(TEXT("actorLabel"), Label);
	Existing->SetStringField(TEXT("actorPath"), Matches[0]->GetPathName());
	Existing->SetNumberField(TEXT("existingCount"), Matches.Num());
	if (Matches.Num() > 1)
	{
		// Deliberately NOT the 'ambiguous' key: that one marks a refusal, and
		// this is a success. A consumer branching on 'ambiguous' must not read
		// an idempotent no-op as a call that did nothing because it refused.
		Existing->SetBoolField(TEXT("labelIsAmbiguous"), true);
		TArray<TSharedPtr<FJsonValue>> Rows;
		for (AActor* Match : Matches)
		{
			Rows.Add(MakeShared<FJsonValueObject>(MCPDescribeActorCandidate(Match)));
		}
		Existing->SetArrayField(TEXT("candidates"), Rows);
		Existing->SetStringField(TEXT("note"), FString::Printf(
			TEXT("%d actors already carry the label '%s'. actorPath names the first by object path; address any of them with the actorPath from candidates."),
			Matches.Num(), *Label));
	}
	return MCPResult(Existing);
}

/**
 * Why an actor filter matched nothing, in terms of what it WOULD have matched.
 *
 * Level actions do not agree on filter semantics and their parameter names do
 * not warn you: get_outliner's nameFilter is a case-insensitive substring over
 * the label OR the internal name, while delete_actors' labelPrefix is a
 * case-sensitive prefix over the label only. The same string selects different
 * sets, and the losing call returns success with matched:0, which a caller
 * reasonably reads as "nothing to do".
 *
 * So a zero match reports the counts under the OTHER semantics rather than
 * leaving the caller to discover them. Returns an unset pointer when the
 * string would have matched nothing under any of them, because then a zero
 * really does mean zero.
 */
inline TSharedPtr<FJsonObject> MCPDescribeZeroActorMatch(UWorld* World, const FString& Needle)
{
	if (!World || Needle.IsEmpty())
	{
		return nullptr;
	}

	int32 LabelContains = 0;
	int32 NameContains = 0;
	int32 PrefixIgnoringCase = 0;
	TArray<TSharedPtr<FJsonValue>> Samples;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;
		const FString Label = Actor->GetActorLabel();
		const FString Name = Actor->GetName();
		const bool bLabelContains = Label.Contains(Needle, ESearchCase::IgnoreCase);
		const bool bNameContains = Name.Contains(Needle, ESearchCase::IgnoreCase);
		const bool bPrefix = Label.StartsWith(Needle, ESearchCase::IgnoreCase);
		if (bLabelContains) ++LabelContains;
		if (bNameContains) ++NameContains;
		if (bPrefix) ++PrefixIgnoringCase;
		if ((bLabelContains || bNameContains) && Samples.Num() < 5)
		{
			Samples.Add(MakeShared<FJsonValueString>(
				FString::Printf(TEXT("%s (internal name %s)"), *Label, *Name)));
		}
	}

	if (LabelContains == 0 && NameContains == 0 && PrefixIgnoringCase == 0)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Hint = MakeShared<FJsonObject>();
	Hint->SetStringField(TEXT("filter"), Needle);
	Hint->SetNumberField(TEXT("actorsWhoseLabelContainsIt"), LabelContains);
	Hint->SetNumberField(TEXT("actorsWhoseInternalNameContainsIt"), NameContains);
	Hint->SetNumberField(TEXT("actorsWhoseLabelStartsWithItIgnoringCase"), PrefixIgnoringCase);
	Hint->SetArrayField(TEXT("samples"), Samples);
	Hint->SetStringField(TEXT("note"),
		TEXT("labelPrefix is a case-sensitive PREFIX over the EDITOR LABEL. level(get_outliner)'s nameFilter is a case-insensitive SUBSTRING over the label OR the internal name, so the same string selects a different set there. Use labelContains for a substring over the label, or nameContains for one over the internal name."));
	return Hint;
}

/**
 * Note that an actor enumeration only saw the actors that are loaded.
 *
 * Every actor query in this plugin iterates the world, and on a World
 * Partition map that is a real answer but not the whole answer. Saying so
 * turns a silently wrong zero into an actionable one.
 */
inline void MCPNoteLoadedOnlyEnumeration(UWorld* World, TSharedPtr<FJsonObject> Result)
{
	if (!World || !Result.IsValid() || !World->IsPartitionedWorld())
	{
		return;
	}
	Result->SetBoolField(TEXT("partitionedWorld"), true);
	Result->SetStringField(TEXT("enumerationNote"),
		TEXT("This is a World Partition map and only LOADED actors were enumerated. An actor whose cell is not streamed in is invisible to every world query, including this one. Use level(list_actor_descs) to see the unloaded ones and level(load_actor_descs) to pin them first."));
}
