#include "LevelHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerPagination.h"

// Actor tags: add, remove, replace and list. Registration stays in
// LevelHandlers.cpp.

TSharedPtr<FJsonValue> FLevelHandlers::AddActorTag(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("tag"),
	});

	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel; if (auto E = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return E;
	FString Tag; if (auto E = RequireString(Params, TEXT("tag"), Tag)) return E;

	TSharedPtr<FJsonValue> ActorErr;
	AActor* A = MCPResolveActor(World, Params, ActorErr);
	if (!A) return ActorErr;
	ActorLabel = A->GetActorLabel();

	const FName TagName(*Tag);
	const bool bAlreadyHad = A->Tags.Contains(TagName);
	if (!bAlreadyHad)
	{
		A->Modify();
		A->Tags.Add(TagName);
		A->MarkPackageDirty();
	}
	auto Result = MCPSuccess();
	if (bAlreadyHad) MCPSetExisted(Result); else MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), A->GetPathName());
	Result->SetStringField(TEXT("tag"), Tag);
	TArray<TSharedPtr<FJsonValue>> TagsOut;
	for (const FName& T : A->Tags) TagsOut.Add(MakeShared<FJsonValueString>(T.ToString()));
	Result->SetArrayField(TEXT("tags"), TagsOut);

	// Only the call that actually added the tag has an inverse. Removing one
	// the actor already carried would undo something this call did not do.
	if (!bAlreadyHad)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), A->GetPathName());
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		Payload->SetStringField(TEXT("tag"), Tag);
		MCPSetRollback(Result, TEXT("remove_actor_tag"), Payload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::RemoveActorTag(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("tag"),
	});

	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel; if (auto E = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return E;
	FString Tag; if (auto E = RequireString(Params, TEXT("tag"), Tag)) return E;

	TSharedPtr<FJsonValue> ActorErr;
	AActor* A = MCPResolveActor(World, Params, ActorErr);
	if (!A) return ActorErr;
	ActorLabel = A->GetActorLabel();

	const FName TagName(*Tag);
	const int32 Removed = A->Tags.Remove(TagName);
	if (Removed > 0)
	{
		A->Modify();
		A->MarkPackageDirty();
	}

	auto Result = MCPSuccess();
	if (Removed == 0) MCPSetExisted(Result); else MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), A->GetPathName());
	Result->SetStringField(TEXT("tag"), Tag);
	Result->SetNumberField(TEXT("removed"), Removed);

	if (Removed > 0)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), A->GetPathName());
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		Payload->SetStringField(TEXT("tag"), Tag);
		MCPSetRollback(Result, TEXT("add_actor_tag"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("AActor::Tags is an authored array and add_actor_tag appends, so the tag comes back at the end rather than at the position it held. It also comes back once, which is short if the actor carried the same tag more than once."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SetActorTags(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("tags"),
	});

	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel;

	TSharedPtr<FJsonValue> ActorErr;
	AActor* A = MCPResolveActor(World, Params, ActorErr);
	if (!A) return ActorErr;
	ActorLabel = A->GetActorLabel();

	const TArray<TSharedPtr<FJsonValue>>* TagsArr = nullptr;
	if (!TryGetArrayParam(Params, TEXT("tags"), TagsArr) || !TagsArr)
	{
		return MCPError(TEXT("Missing 'tags' array"));
	}

	// The whole array, as it stands, is what set_actor_tags puts back.
	TArray<TSharedPtr<FJsonValue>> PreviousTags;
	for (const FName& T : A->Tags) PreviousTags.Add(MakeShared<FJsonValueString>(T.ToString()));

	A->Modify();
	A->Tags.Reset();
	for (const TSharedPtr<FJsonValue>& V : *TagsArr)
	{
		FString S;
		if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty())
		{
			A->Tags.AddUnique(FName(*S));
		}
	}
	A->MarkPackageDirty();

	auto Result = MCPSuccess();
	TArray<TSharedPtr<FJsonValue>> Out;
	for (const FName& T : A->Tags) Out.Add(MakeShared<FJsonValueString>(T.ToString()));

	// Order matters here: Tags is an authored array, so a reordering is a real
	// change even when the set is identical.
	bool bTagsChanged = Out.Num() != PreviousTags.Num();
	if (!bTagsChanged)
	{
		for (int32 i = 0; i < Out.Num(); ++i)
		{
			if (Out[i]->AsString() != PreviousTags[i]->AsString()) { bTagsChanged = true; break; }
		}
	}
	if (bTagsChanged) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);
	Result->SetBoolField(TEXT("unchanged"), !bTagsChanged);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), A->GetPathName());
	Result->SetArrayField(TEXT("tags"), Out);
	Result->SetArrayField(TEXT("previousTags"), PreviousTags);

	// This action replaces the array wholesale, so replaying it with the array
	// that was there restores it, order included.
	if (bTagsChanged)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), A->GetPathName());
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		Payload->SetArrayField(TEXT("tags"), PreviousTags);
		MCPSetRollback(Result, TEXT("set_actor_tags"), Payload);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("On a World Partition map the inverse resolves the actor by path against loaded actors only, so it fails if the actor's cell unloaded between this call and the replay."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::ListActorTags(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("cursor"), TEXT("limit"),
	});

	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel;

	TSharedPtr<FJsonValue> ActorErr;
	AActor* A = MCPResolveActor(World, Params, ActorErr);
	if (!A) return ActorErr;
	ActorLabel = A->GetActorLabel();

	// T3: paged. Tag lists are usually short, but a data-driven actor can carry
	// hundreds, and the category pages uniformly rather than making the caller
	// remember which list actions accept a cursor.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_actor_tags|actor=%s"), *A->GetPathName()),
			/*DefaultLimit*/ 200, /*MaxLimit*/ 5000, Page))
	{
		return Err;
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), A->GetPathName());
	// Deliberately NOT sorted: AActor::Tags is an authored array and its order
	// is what the caller sees in the details panel. The tag itself is the
	// anchor, which is why the emitted order can stay as authored.
	TArray<MCPPagination::FPageRow> Rows;
	Rows.Reserve(A->Tags.Num());
	for (const FName& T : A->Tags)
	{
		const FString Tag = T.ToString();
		Rows.Add({ Tag, MakeShared<FJsonValueString>(Tag) });
	}
	MCPPagination::EmitPage(Page, Rows, TEXT("tags"), Result);
	return MCPResult(Result);
}
