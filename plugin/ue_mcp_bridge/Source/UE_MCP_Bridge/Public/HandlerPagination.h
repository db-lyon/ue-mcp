#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Misc/Base64.h"
#include "HandlerUtils.h"

// ── Cursor pagination, once, for every list-returning handler ────────────────
//
// The editor holds the data, so paging has to happen where the enumeration
// happens. This header is the single place the rules live, so a second handler
// adopting it cannot invent a second spelling of `cursor`, a second meaning for
// `hasMore`, or a second answer to "what happens when the list changed between
// pages". It is a HEADER rather than a file-local helper on purpose: the module
// is a unity build, and two .cpp files carrying private copies of the same
// helper merge into one translation unit and fail to compile (C2084).
//
// USAGE, in three lines at each end of a handler:
//
//     MCPPagination::FPageRequest Page;
//     if (auto Err = MCPPagination::ReadPageRequest(
//             Params,
//             FString::Printf(TEXT("list_classes|parentFilter=%s"), *ParentFilter),
//             /*DefaultLimit*/ 100, /*MaxLimit*/ 1000, Page)) return Err;
//     ... enumerate the WHOLE collection into TArray<FPageRow> Rows ...
//     MCPPagination::EmitPage(Page, Rows, TEXT("classes"), Result);
//
// The second argument is the COLLECTION KEY: the method name plus every filter
// parameter that changes which rows are enumerated. It is what makes a cursor
// refuse to be replayed against a different filter.
//
// Each FPageRow carries the row's JSON plus a STABLE IDENTITY (`Id`): the one
// string that names that row across two separate enumerations. A class path, a
// tag string, a module name, a property name. It is never the row's index.
//
// ── The cursor ───────────────────────────────────────────────────────────────
//
// The cursor is OPAQUE: callers pass back exactly the string they were handed
// and never construct, parse, edit or reorder one. It is base64url text over a
// small JSON record, so it survives every transport this bridge speaks, and its
// internals are free to change between builds. A cursor from an older build is
// refused by version, with instructions, rather than misread.
//
// It is also STABLE: it records the identity of the LAST row of the page it was
// issued for, not just a row count. Resuming re-enumerates the collection, finds
// that row by identity, and continues after it. So a page boundary survives the
// collection being enumerated again in a different order, or growing and
// shrinking ahead of the boundary, without skipping or repeating rows around it.
//
// ── When the collection changed between pages ────────────────────────────────
//
// It will change: classes load, tags are added, modules load on demand. The
// handler REPORTS what it found rather than papering over it. Three outcomes,
// and every one of them is named in the response:
//
//   1. The anchor row is still exactly where the cursor left it. Nothing is
//      emitted; the page is an exact continuation.
//   2. The anchor row is still present but has MOVED (rows before it were added
//      or removed). Resuming after the anchor is still correct at the boundary,
//      so the page is returned with `collectionChanged: true`, `anchorMoved:
//      true` and `shiftedBy` giving the signed row delta. Rows that moved from
//      one side of the boundary to the other may have been seen twice or not at
//      all; `cursorNote` says so.
//   3. The anchor row is GONE (deleted, unloaded, filtered out). There is no
//      boundary left to be exact about, so the page falls back to the recorded
//      offset and comes back with `collectionChanged: true`,
//      `anchorMissing: true` and a `cursorNote` stating plainly that rows near
//      the boundary may have been skipped or repeated, and that a caller who
//      needs an exact enumeration should re-page from the start with no cursor.
//
// A caller that needs a consistent snapshot should read every page in one
// uninterrupted run and treat `collectionChanged` as a signal to start over.
// Nothing here freezes the editor's state between calls, and nothing pretends
// to.
//
// ── Errors ───────────────────────────────────────────────────────────────────
//
// An invalid, tampered, or stale cursor is an ERROR naming what was wrong and
// how to restart. It is never an empty page, because an empty page reads as
// "the collection is empty" and ends the caller's loop on a lie.
//
// A cursor is bound to the exact query that issued it (the `CollectionKey` the
// handler builds from its own filter parameters). Passing a page-2 cursor with
// a different filter is refused, naming both queries, rather than paging one
// collection at another collection's offsets.

namespace MCPPagination
{
	/** Cursor payload format. Bumped when the encoded fields change meaning;
	 *  an older or newer cursor is refused with instructions, never guessed at. */
	static constexpr int32 CursorVersion = 1;

	/** One row of a paged collection: its JSON, and the identity that names it
	 *  across two separate enumerations. `Id` must be unique within the
	 *  collection and must not be the row's index. */
	struct FPageRow
	{
		FString Id;
		TSharedPtr<FJsonValue> Value;
	};

	/** The paging half of a handler's parameters, after validation. */
	struct FPageRequest
	{
		/** Rows to return at most. Always within [1, MaxLimit]. */
		int32 Limit = 100;

		/** Identity of the query this request belongs to. A cursor is only
		 *  valid against the key that issued it. */
		FString CollectionKey;

		/** True when the caller resumed rather than asking for the first page. */
		bool bResumed = false;

		/** Rows the caller has already been handed, per the cursor. */
		int32 ResumeOffset = 0;

		/** Identity of the last row of the previous page. */
		FString ResumeAnchor;

		/** Row count of the collection when the cursor was issued, or
		 *  INDEX_NONE when the issuing page could not count it. */
		int32 TotalAtIssue = INDEX_NONE;
	};

	namespace Detail
	{
		inline FString EncodeCursor(
			const FString& CollectionKey, int32 Offset, const FString& Anchor, int32 Total)
		{
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetNumberField(TEXT("v"), CursorVersion);
			Payload->SetStringField(TEXT("k"), CollectionKey);
			Payload->SetNumberField(TEXT("o"), Offset);
			Payload->SetStringField(TEXT("a"), Anchor);
			Payload->SetNumberField(TEXT("n"), Total);

			FString Json;
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
			FJsonSerializer::Serialize(Payload.ToSharedRef(), Writer);
			return FBase64::Encode(Json, EBase64Mode::UrlSafe);
		}

		/** Decode a cursor. Returns an empty string on success, or a sentence
		 *  naming what was wrong (the caller prefixes the parameter name). */
		inline FString DecodeCursor(
			const FString& Cursor,
			FString& OutKey, int32& OutOffset, FString& OutAnchor, int32& OutTotal)
		{
			FString Json;
			if (!FBase64::Decode(Cursor, Json, EBase64Mode::UrlSafe) || Json.IsEmpty())
			{
				return TEXT("it is not a cursor this server issued (base64 decode failed)");
			}

			TSharedPtr<FJsonObject> Payload;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
			if (!FJsonSerializer::Deserialize(Reader, Payload) || !Payload.IsValid())
			{
				return TEXT("it is not a cursor this server issued (payload is not readable)");
			}

			double Version = 0.0;
			if (!Payload->TryGetNumberField(TEXT("v"), Version))
			{
				return TEXT("it carries no format version");
			}
			if (static_cast<int32>(Version) != CursorVersion)
			{
				return FString::Printf(
					TEXT("it is cursor format version %d and this build issues version %d"),
					static_cast<int32>(Version), CursorVersion);
			}

			double Offset = 0.0;
			if (!Payload->TryGetStringField(TEXT("k"), OutKey) ||
				!Payload->TryGetNumberField(TEXT("o"), Offset) ||
				!Payload->TryGetStringField(TEXT("a"), OutAnchor))
			{
				return TEXT("it is missing fields this build requires");
			}
			if (Offset < 0.0 || Offset > static_cast<double>(MAX_int32))
			{
				return TEXT("it carries an out-of-range row offset");
			}
			OutOffset = static_cast<int32>(Offset);

			double Total = static_cast<double>(INDEX_NONE);
			Payload->TryGetNumberField(TEXT("n"), Total);
			OutTotal = (Total < 0.0 || Total > static_cast<double>(MAX_int32))
				? INDEX_NONE : static_cast<int32>(Total);
			return FString();
		}
	}

	/**
	 * Read and validate `cursor` and `limit`.
	 *
	 * `CollectionKey` identifies the exact query being paged: the method name
	 * plus every filter parameter that changes which rows are enumerated. It is
	 * what makes a cursor refuse to be replayed against a different filter.
	 *
	 * Returns an MCPError JSON value on a bad limit or a bad cursor, and
	 * nullptr on success. Handlers return the value directly:
	 *   `if (auto Err = ReadPageRequest(...)) return Err;`
	 */
	inline TSharedPtr<FJsonValue> ReadPageRequest(
		const TSharedPtr<FJsonObject>& Params,
		const FString& CollectionKey,
		int32 DefaultLimit,
		int32 MaxLimit,
		FPageRequest& Out)
	{
		Out = FPageRequest();
		Out.CollectionKey = CollectionKey;
		Out.Limit = DefaultLimit;

		if (Params.IsValid() && Params->HasField(TEXT("limit")))
		{
			double Raw = 0.0;
			if (!Params->TryGetNumberField(TEXT("limit"), Raw) ||
				!FMath::IsFinite(Raw) || FMath::TruncToDouble(Raw) != Raw ||
				Raw < 1.0 || Raw > static_cast<double>(MaxLimit))
			{
				return MCPError(FString::Printf(
					TEXT("'limit' must be a whole number between 1 and %d. Omit it for the default of %d."),
					MaxLimit, DefaultLimit));
			}
			Out.Limit = static_cast<int32>(Raw);
		}

		const FString Cursor = Params.IsValid() ? OptionalString(Params, TEXT("cursor")) : FString();
		if (Cursor.IsEmpty())
		{
			return nullptr;
		}

		FString CursorKey;
		int32 Offset = 0;
		FString Anchor;
		int32 Total = INDEX_NONE;
		const FString Problem = Detail::DecodeCursor(Cursor, CursorKey, Offset, Anchor, Total);
		if (!Problem.IsEmpty())
		{
			return MCPError(FString::Printf(
				TEXT("Invalid 'cursor': %s. Pass back only a 'nextCursor' this action returned, unmodified. ")
				TEXT("To restart, call again with no 'cursor'."),
				*Problem));
		}
		if (CursorKey != CollectionKey)
		{
			return MCPError(FString::Printf(
				TEXT("This 'cursor' belongs to a different query: it was issued for [%s] and this call is [%s]. ")
				TEXT("A cursor is only valid while every filter parameter stays the same. ")
				TEXT("Call again with no 'cursor' to page the new query from its first row."),
				*CursorKey, *CollectionKey));
		}

		Out.bResumed = true;
		Out.ResumeOffset = Offset;
		Out.ResumeAnchor = Anchor;
		Out.TotalAtIssue = Total;
		return nullptr;
	}

	/**
	 * Slice one page out of a fully enumerated collection and write it, with
	 * every paging field, into `Result`.
	 *
	 * `Rows` is the WHOLE collection in the order the handler enumerates it.
	 * That is what makes the anchor resolvable and `total` exact. A handler
	 * that genuinely cannot enumerate the whole collection passes what it has
	 * and `bRowsAreComplete = false`; `total` is then omitted and
	 * `totalKnown: false` is emitted in its place, so a caller never reads a
	 * partial count as the real one.
	 *
	 * Writes: <ArrayField>, count, pageOffset, hasMore, nextCursor (only while
	 * there is more), total / totalKnown, and on a resumed page whose
	 * collection moved underneath it: collectionChanged, anchorMoved or
	 * anchorMissing, shiftedBy, and a plain-language cursorNote.
	 */
	inline void EmitPage(
		const FPageRequest& Request,
		const TArray<FPageRow>& Rows,
		const TCHAR* ArrayField,
		TSharedPtr<FJsonObject> Result,
		bool bRowsAreComplete = true)
	{
		if (!Result.IsValid()) return;

		int32 Start = 0;
		bool bChanged = false;
		bool bAnchorMissing = false;
		bool bAnchorMoved = false;
		int32 ShiftedBy = 0;

		if (Request.bResumed)
		{
			int32 AnchorIndex = INDEX_NONE;
			for (int32 i = 0; i < Rows.Num(); ++i)
			{
				if (Rows[i].Id == Request.ResumeAnchor) { AnchorIndex = i; break; }
			}

			if (AnchorIndex == INDEX_NONE)
			{
				// No boundary left to be exact about. Fall back to the recorded
				// offset and say so; silently returning rows from an offset into
				// a collection that has moved is the failure this reports.
				bChanged = true;
				bAnchorMissing = true;
				Start = FMath::Clamp(Request.ResumeOffset, 0, Rows.Num());
			}
			else
			{
				Start = AnchorIndex + 1;
				// The cursor was issued after ResumeOffset rows, so the anchor
				// was row ResumeOffset - 1. Anywhere else means rows ahead of it
				// were added or removed.
				ShiftedBy = AnchorIndex - (Request.ResumeOffset - 1);
				if (ShiftedBy != 0)
				{
					bChanged = true;
					bAnchorMoved = true;
				}
			}
		}

		const int32 End = FMath::Min(Start + Request.Limit, Rows.Num());
		TArray<TSharedPtr<FJsonValue>> Page;
		Page.Reserve(FMath::Max(0, End - Start));
		for (int32 i = Start; i < End; ++i)
		{
			// Written as two statements rather than a ternary: the two arms are
			// a TSharedPtr<FJsonValue> and a TSharedRef<FJsonValueNull>, and
			// leaving the conditional operator to find a common type between
			// them is a coin toss across compilers.
			if (Rows[i].Value.IsValid())
			{
				Page.Add(Rows[i].Value);
			}
			else
			{
				Page.Add(MakeShared<FJsonValueNull>());
			}
		}

		Result->SetArrayField(ArrayField, Page);
		Result->SetNumberField(TEXT("count"), Page.Num());
		Result->SetNumberField(TEXT("pageOffset"), Start);

		const bool bHasMore = bRowsAreComplete ? (End < Rows.Num()) : (End < Rows.Num() || Page.Num() == Request.Limit);
		Result->SetBoolField(TEXT("hasMore"), bHasMore);
		if (bHasMore && Page.Num() > 0)
		{
			Result->SetStringField(TEXT("nextCursor"), Detail::EncodeCursor(
				Request.CollectionKey, End, Rows[End - 1].Id, bRowsAreComplete ? Rows.Num() : INDEX_NONE));
		}

		if (bRowsAreComplete)
		{
			Result->SetNumberField(TEXT("total"), Rows.Num());
			Result->SetBoolField(TEXT("totalKnown"), true);
		}
		else
		{
			Result->SetBoolField(TEXT("totalKnown"), false);
		}

		if (!bChanged) return;

		Result->SetBoolField(TEXT("collectionChanged"), true);
		Result->SetNumberField(TEXT("shiftedBy"), ShiftedBy);
		if (bAnchorMissing)
		{
			Result->SetBoolField(TEXT("anchorMissing"), true);
			Result->SetStringField(TEXT("cursorNote"), FString::Printf(
				TEXT("The row this page was to resume after ('%s') is gone from the collection, so this page ")
				TEXT("resumed at row %d by count instead. Rows near that boundary may have been skipped or ")
				TEXT("returned twice. Call again with no 'cursor' if you need an exact enumeration."),
				*Request.ResumeAnchor, Start));
		}
		else if (bAnchorMoved)
		{
			Result->SetBoolField(TEXT("anchorMoved"), true);
			Result->SetStringField(TEXT("cursorNote"), FString::Printf(
				TEXT("The collection changed between pages: the row this page resumes after ('%s') moved by %d ")
				TEXT("row(s), so %d row(s) were added or removed ahead of it. This page continues immediately ")
				TEXT("after that row, so the boundary itself is exact, but a row that crossed it may have been ")
				TEXT("skipped or returned twice. Call again with no 'cursor' if you need an exact enumeration."),
				*Request.ResumeAnchor, ShiftedBy, FMath::Abs(ShiftedBy)));
		}
		if (Request.TotalAtIssue != INDEX_NONE && bRowsAreComplete && Request.TotalAtIssue != Rows.Num())
		{
			Result->SetNumberField(TEXT("totalAtFirstPage"), Request.TotalAtIssue);
		}
	}
}
