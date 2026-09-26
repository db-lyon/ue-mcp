#include "LevelHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

// Attaching and detaching actors and components. Registration stays in
// LevelHandlers.cpp.

// #205: attach an actor's root component to a parent actor.
TSharedPtr<FJsonValue> FLevelHandlers::AttachActor(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("childLabel"), TEXT("childPath"), TEXT("parentLabel"), TEXT("parentPath"), TEXT("attachRule"),
		TEXT("socketName"),
	});

	REQUIRE_EDITOR_WORLD(World);
	FString ChildLabel; if (auto E = RequireStringAlt(Params, TEXT("childLabel"), TEXT("childPath"), ChildLabel)) return E;
	FString ParentLabel; if (auto E = RequireStringAlt(Params, TEXT("parentLabel"), TEXT("parentPath"), ParentLabel)) return E;

	// #983: both ends of an attachment take a path. Attaching to whichever
	// namesake the iterator reached first is how a prop ends up parented to a
	// building at the other end of the map.
	FMCPActorSelector ChildSel; ChildSel.LabelKey = TEXT("childLabel"); ChildSel.PathKey = TEXT("childPath");
	FMCPActorSelector ParentSel; ParentSel.LabelKey = TEXT("parentLabel"); ParentSel.PathKey = TEXT("parentPath");
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Child = MCPResolveActor(World, Params, ActorErr, ChildSel);
	if (!Child) return ActorErr;
	AActor* Parent = MCPResolveActor(World, Params, ActorErr, ParentSel);
	if (!Parent) return ActorErr;
	ChildLabel = Child->GetActorLabel();
	ParentLabel = Parent->GetActorLabel();

	const FString RuleStr = OptionalString(Params, TEXT("attachRule"), TEXT("KeepWorld")).ToLower();
	EAttachmentRule Loc = EAttachmentRule::KeepWorld;
	if (RuleStr.Contains(TEXT("relative"))) Loc = EAttachmentRule::KeepRelative;
	else if (RuleStr.Contains(TEXT("snap"))) Loc = EAttachmentRule::SnapToTarget;

	const FString SocketName = OptionalString(Params, TEXT("socketName"));

	Child->Modify();
	const bool bOk = Child->AttachToActor(Parent, FAttachmentTransformRules(Loc, Loc, Loc, true), FName(*SocketName));
	Child->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("childLabel"), ChildLabel);
	Result->SetStringField(TEXT("parentLabel"), ParentLabel);
	Result->SetStringField(TEXT("childPath"), Child->GetPathName());
	Result->SetStringField(TEXT("parentPath"), Parent->GetPathName());
	Result->SetBoolField(TEXT("attached"), bOk);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("childLabel"), ChildLabel);
	Payload->SetStringField(TEXT("childPath"), Child->GetPathName());
	MCPSetRollback(Result, TEXT("detach_actor"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::DetachActor(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("childLabel"), TEXT("childPath"),
	});

	REQUIRE_EDITOR_WORLD(World);
	FString ChildLabel; if (auto E = RequireStringAlt(Params, TEXT("childLabel"), TEXT("childPath"), ChildLabel)) return E;

	FMCPActorSelector ChildSel; ChildSel.LabelKey = TEXT("childLabel"); ChildSel.PathKey = TEXT("childPath");
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Child = MCPResolveActor(World, Params, ActorErr, ChildSel);
	if (!Child) return ActorErr;
	ChildLabel = Child->GetActorLabel();

	// The parent this call is about to drop, by path, plus the socket it was
	// attached to. Both are needed to name the attach that undoes it.
	AActor* PreviousParent = Child->GetAttachParentActor();
	const FString PreviousParentPath = PreviousParent ? PreviousParent->GetPathName() : FString();
	const FString PreviousParentLabel = PreviousParent ? PreviousParent->GetActorLabel() : FString();
	FString PreviousSocketName;
	if (PreviousParent && Child->GetRootComponent())
	{
		const FName Socket = Child->GetRootComponent()->GetAttachSocketName();
		if (Socket != NAME_None) PreviousSocketName = Socket.ToString();
	}
	const bool bWasAttached = PreviousParent != nullptr;

	Child->Modify();
	Child->DetachFromActor(FDetachmentTransformRules(EDetachmentRule::KeepWorld, true));
	Child->MarkPackageDirty();

	auto Result = MCPSuccess();
	if (bWasAttached) MCPSetUpdated(Result); else MCPSetExisted(Result);
	// Present in both branches: MCPSetExisted does not write `updated`, and
	// this handler emitted it unconditionally before, so a consumer branching
	// on it must not start reading undefined.
	Result->SetBoolField(TEXT("updated"), bWasAttached);
	Result->SetBoolField(TEXT("unchanged"), !bWasAttached);
	Result->SetStringField(TEXT("childLabel"), ChildLabel);
	Result->SetStringField(TEXT("childPath"), Child->GetPathName());
	Result->SetBoolField(TEXT("detached"), true);
	Result->SetBoolField(TEXT("alreadyDetached"), !bWasAttached);
	Result->SetStringField(TEXT("previousParentLabel"), PreviousParentLabel);
	Result->SetStringField(TEXT("previousParentPath"), PreviousParentPath);

	if (bWasAttached)
	{
		// Detach kept the world transform, and attaching back with KeepWorld
		// keeps it too, so the relative transform is recomputed to the same
		// place it held before.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("childPath"), Child->GetPathName());
		Payload->SetStringField(TEXT("childLabel"), ChildLabel);
		Payload->SetStringField(TEXT("parentPath"), PreviousParentPath);
		Payload->SetStringField(TEXT("parentLabel"), PreviousParentLabel);
		Payload->SetStringField(TEXT("attachRule"), TEXT("KeepWorld"));
		if (!PreviousSocketName.IsEmpty()) Payload->SetStringField(TEXT("socketName"), PreviousSocketName);
		MCPSetRollback(Result, TEXT("attach_actor"), Payload);
	}
	return MCPResult(Result);
}

// Attach an exact named/root SceneComponent to an exact named/root parent
// SceneComponent. Unlike attach_actor, selecting a non-root child only changes
// that component's hierarchy; it does not parent or replicate the owning actor.
TSharedPtr<FJsonValue> FLevelHandlers::AttachComponent(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("childLabel"), TEXT("childPath"), TEXT("parentLabel"), TEXT("parentPath"), TEXT("childComponentName"),
		TEXT("parentComponentName"), TEXT("attachRule"), TEXT("weldSimulatedBodies"), TEXT("socketName"),
	});

	REQUIRE_EDITOR_WORLD(World);
	FString ChildLabel; if (auto E = RequireStringAlt(Params, TEXT("childLabel"), TEXT("childPath"), ChildLabel)) return E;
	FString ParentLabel; if (auto E = RequireStringAlt(Params, TEXT("parentLabel"), TEXT("parentPath"), ParentLabel)) return E;

	// #983: both ends of an attachment take a path. Attaching to whichever
	// namesake the iterator reached first is how a prop ends up parented to a
	// building at the other end of the map.
	FMCPActorSelector ChildSel; ChildSel.LabelKey = TEXT("childLabel"); ChildSel.PathKey = TEXT("childPath");
	FMCPActorSelector ParentSel; ParentSel.LabelKey = TEXT("parentLabel"); ParentSel.PathKey = TEXT("parentPath");
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Child = MCPResolveActor(World, Params, ActorErr, ChildSel);
	if (!Child) return ActorErr;
	AActor* Parent = MCPResolveActor(World, Params, ActorErr, ParentSel);
	if (!Parent) return ActorErr;
	ChildLabel = Child->GetActorLabel();
	ParentLabel = Parent->GetActorLabel();

	const FString ChildComponentSelector = OptionalString(Params, TEXT("childComponentName"));
	const FString ParentComponentSelector = OptionalString(Params, TEXT("parentComponentName"));

	UActorComponent* ResolvedChildComponent = ChildComponentSelector.IsEmpty()
		? static_cast<UActorComponent*>(Child->GetRootComponent())
		: MCPFindComponentByName(Child, ChildComponentSelector);
	if (!ResolvedChildComponent)
	{
		return ChildComponentSelector.IsEmpty()
			? MCPError(FString::Printf(TEXT("Child actor '%s' has no root component"), *ChildLabel))
			: MCPError(FString::Printf(TEXT("Child component '%s' not found on actor '%s'"), *ChildComponentSelector, *ChildLabel));
	}
	USceneComponent* ChildComponent = Cast<USceneComponent>(ResolvedChildComponent);
	if (!ChildComponent)
	{
		return MCPError(FString::Printf(TEXT("Child component '%s' on actor '%s' is not a SceneComponent"), *ResolvedChildComponent->GetName(), *ChildLabel));
	}

	UActorComponent* ResolvedParentComponent = ParentComponentSelector.IsEmpty()
		? static_cast<UActorComponent*>(Parent->GetRootComponent())
		: MCPFindComponentByName(Parent, ParentComponentSelector);
	if (!ResolvedParentComponent)
	{
		return ParentComponentSelector.IsEmpty()
			? MCPError(FString::Printf(TEXT("Parent actor '%s' has no root component"), *ParentLabel))
			: MCPError(FString::Printf(TEXT("Parent component '%s' not found on actor '%s'"), *ParentComponentSelector, *ParentLabel));
	}
	USceneComponent* ParentComponent = Cast<USceneComponent>(ResolvedParentComponent);
	if (!ParentComponent)
	{
		return MCPError(FString::Printf(TEXT("Parent component '%s' on actor '%s' is not a SceneComponent"), *ResolvedParentComponent->GetName(), *ParentLabel));
	}

	const FString RequestedRule = OptionalString(Params, TEXT("attachRule"), TEXT("KeepWorld"));
	FString RuleKey = RequestedRule;
	RuleKey.TrimStartAndEndInline();
	RuleKey = RuleKey.ToLower();
	EAttachmentRule Rule = EAttachmentRule::KeepWorld;
	FString CanonicalRule = TEXT("KeepWorld");
	if (RuleKey == TEXT("keeprelative"))
	{
		Rule = EAttachmentRule::KeepRelative;
		CanonicalRule = TEXT("KeepRelative");
	}
	else if (RuleKey == TEXT("snaptotarget"))
	{
		Rule = EAttachmentRule::SnapToTarget;
		CanonicalRule = TEXT("SnapToTarget");
	}
	else if (RuleKey != TEXT("keepworld"))
	{
		return MCPError(FString::Printf(TEXT("Invalid attachRule '%s'. Expected KeepWorld, KeepRelative, or SnapToTarget"), *RequestedRule));
	}
	const bool bWeldSimulatedBodies = OptionalBool(Params, TEXT("weldSimulatedBodies"), false);

	const FString SocketName = OptionalString(Params, TEXT("socketName"));
	const FName Socket = SocketName.IsEmpty() ? NAME_None : FName(*SocketName);
	if (Socket != NAME_None && !ParentComponent->DoesSocketExist(Socket))
	{
		return MCPError(FString::Printf(
			TEXT("Socket '%s' does not exist on parent component '%s' (%s) of actor '%s'"),
			*SocketName,
			*ParentComponent->GetName(),
			*ParentComponent->GetClass()->GetName(),
			*ParentLabel));
	}

	USceneComponent* PreviousParent = ChildComponent->GetAttachParent();
	const FName PreviousSocket = ChildComponent->GetAttachSocketName();
	AActor* PreviousParentActor = PreviousParent ? PreviousParent->GetOwner() : nullptr;
	const bool bAlreadyAttached = PreviousParent == ParentComponent && PreviousSocket == Socket;

	auto PopulateResult = [Child, Parent, ChildComponent, ParentComponent, Socket, &CanonicalRule, bWeldSimulatedBodies](TSharedPtr<FJsonObject> Result)
	{
		Result->SetStringField(TEXT("childLabel"), Child->GetActorLabel());
		Result->SetStringField(TEXT("parentLabel"), Parent->GetActorLabel());
		Result->SetStringField(TEXT("childComponentName"), ChildComponent->GetName());
		Result->SetStringField(TEXT("childComponentClass"), ChildComponent->GetClass()->GetName());
		Result->SetBoolField(TEXT("childIsRoot"), ChildComponent == Child->GetRootComponent());
		Result->SetStringField(TEXT("parentComponentName"), ParentComponent->GetName());
		Result->SetStringField(TEXT("parentComponentClass"), ParentComponent->GetClass()->GetName());
		Result->SetBoolField(TEXT("parentIsRoot"), ParentComponent == Parent->GetRootComponent());
		Result->SetStringField(TEXT("socketName"), Socket == NAME_None ? FString() : Socket.ToString());
		Result->SetStringField(TEXT("attachRule"), CanonicalRule);
		Result->SetBoolField(TEXT("weldSimulatedBodies"), bWeldSimulatedBodies);
		Result->SetBoolField(TEXT("attached"), true);
	};

	if (bAlreadyAttached)
	{
		auto Result = MCPSuccess();
		MCPSetExisted(Result);
		PopulateResult(Result);
		Result->SetBoolField(TEXT("alreadyAttached"), true);
		Result->SetBoolField(TEXT("attachmentChanged"), false);
		Result->SetBoolField(TEXT("attachmentRulesApplied"), false);
		return MCPResult(Result);
	}

	// Reject topology that native AttachToComponent would refuse before calling
	// Modify(), so failed self/cycle requests cannot create undo or dirty state.
	if (ChildComponent == ParentComponent)
	{
		return MCPError(FString::Printf(
			TEXT("Cannot attach component '%s' on actor '%s' to itself"),
			*ChildComponent->GetName(),
			*ChildLabel));
	}
	if (ParentComponent->IsAttachedTo(ChildComponent))
	{
		return MCPError(FString::Printf(
			TEXT("Cannot attach component '%s' on actor '%s' beneath its descendant component '%s' on actor '%s'"),
			*ChildComponent->GetName(),
			*ChildLabel,
			*ParentComponent->GetName(),
			*ParentLabel));
	}
	if (!ParentComponent->CanAttachAsChild(ChildComponent, Socket))
	{
		return MCPError(FString::Printf(
			TEXT("Parent component '%s' on actor '%s' cannot accept child component '%s' on actor '%s' at socket '%s'"),
			*ParentComponent->GetName(),
			*ParentLabel,
			*ChildComponent->GetName(),
			*ChildLabel,
			Socket == NAME_None ? TEXT("") : *Socket.ToString()));
	}

	// Even when only a named non-root component is reparented, a cross-actor
	// reference must obey the editor's actor-domain rules (level, content bundle,
	// external data layer, World Partition ownership, and actor-level cycles).
	if (Child != Parent)
	{
		if (!GEditor)
		{
			return MCPError(TEXT("Editor actor-parenting validation is unavailable"));
		}
		FText ParentingReason;
		if (!GEditor->CanParentActors(Parent, Child, &ParentingReason))
		{
			return MCPError(ParentingReason.IsEmpty()
				? FString::Printf(TEXT("Actor '%s' cannot be attached to actor '%s'"), *ChildLabel, *ParentLabel)
				: ParentingReason.ToString());
		}
	}
	if (ChildComponent->Mobility == EComponentMobility::Static && ParentComponent->Mobility != EComponentMobility::Static)
	{
		const TCHAR* ParentMobility = ParentComponent->Mobility == EComponentMobility::Stationary
			? TEXT("Stationary")
			: TEXT("Movable");
		return MCPError(FString::Printf(
			TEXT("Cannot attach Static child component '%s' on actor '%s' to %s parent component '%s' on actor '%s'"),
			*ChildComponent->GetName(),
			*ChildLabel,
			ParentMobility,
			*ParentComponent->GetName(),
			*ParentLabel));
	}

	// Record transaction state without dirtying until native attachment succeeds.
	Child->Modify(false);
	ChildComponent->Modify(false);
	Parent->Modify(false);
	ParentComponent->Modify(false);
	if (PreviousParent)
	{
		PreviousParent->Modify(false);
		if (AActor* PreviousOwner = PreviousParent->GetOwner())
		{
			PreviousOwner->Modify(false);
		}
	}
	const bool bAttached = ChildComponent->AttachToComponent(
		ParentComponent,
		FAttachmentTransformRules(Rule, Rule, Rule, bWeldSimulatedBodies),
		Socket);
	const bool bTopologyMatches =
		ChildComponent->GetAttachParent() == ParentComponent &&
		ChildComponent->GetAttachSocketName() == Socket;
	if (!bAttached || !bTopologyMatches)
	{
		const FString SocketSuffix = Socket == NAME_None
			? FString()
			: FString::Printf(TEXT(" at socket '%s'"), *SocketName);
		return MCPError(FString::Printf(
			TEXT("Failed to attach child component '%s' on actor '%s' to parent component '%s' on actor '%s'%s"),
			*ChildComponent->GetName(),
			*ChildLabel,
			*ParentComponent->GetName(),
			*ParentLabel,
			*SocketSuffix));
	}
	Child->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	PopulateResult(Result);
	Result->SetBoolField(TEXT("alreadyAttached"), false);
	Result->SetBoolField(TEXT("attachmentChanged"), true);
	Result->SetBoolField(TEXT("attachmentRulesApplied"), true);
	Result->SetStringField(TEXT("previousParentLabel"), PreviousParentActor ? PreviousParentActor->GetActorLabel() : FString());
	Result->SetStringField(TEXT("previousParentComponentName"), PreviousParent ? PreviousParent->GetName() : FString());
	Result->SetStringField(TEXT("previousParentComponentClass"), PreviousParent ? PreviousParent->GetClass()->GetName() : FString());
	Result->SetStringField(TEXT("previousSocketName"), PreviousSocket == NAME_None ? FString() : PreviousSocket.ToString());

	// Detach is an exact inverse only for a previously-unattached component
	// whose world transform was preserved and whose physics bodies were not
	// welded. Do not advertise a lossy rollback for reparent/snap operations.
	if (!PreviousParent && Rule == EAttachmentRule::KeepWorld && !bWeldSimulatedBodies)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("childLabel"), Child->GetActorLabel());
		if (!ChildComponentSelector.IsEmpty())
		{
			Payload->SetStringField(TEXT("childComponentName"), ChildComponent->GetName());
		}
		MCPSetRollback(Result, TEXT("detach_component"), Payload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::DetachComponent(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("childLabel"), TEXT("childPath"), TEXT("childComponentName"),
	});

	REQUIRE_EDITOR_WORLD(World);
	FString ChildLabel; if (auto E = RequireStringAlt(Params, TEXT("childLabel"), TEXT("childPath"), ChildLabel)) return E;

	FMCPActorSelector ChildSel; ChildSel.LabelKey = TEXT("childLabel"); ChildSel.PathKey = TEXT("childPath");
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Child = MCPResolveActor(World, Params, ActorErr, ChildSel);
	if (!Child) return ActorErr;
	ChildLabel = Child->GetActorLabel();

	const FString ChildComponentSelector = OptionalString(Params, TEXT("childComponentName"));
	UActorComponent* ResolvedChildComponent = ChildComponentSelector.IsEmpty()
		? static_cast<UActorComponent*>(Child->GetRootComponent())
		: MCPFindComponentByName(Child, ChildComponentSelector);
	if (!ResolvedChildComponent)
	{
		return ChildComponentSelector.IsEmpty()
			? MCPError(FString::Printf(TEXT("Actor '%s' has no root component"), *ChildLabel))
			: MCPError(FString::Printf(TEXT("Component '%s' not found on actor '%s'"), *ChildComponentSelector, *ChildLabel));
	}
	USceneComponent* ChildComponent = Cast<USceneComponent>(ResolvedChildComponent);
	if (!ChildComponent)
	{
		return MCPError(FString::Printf(TEXT("Component '%s' on actor '%s' is not a SceneComponent"), *ResolvedChildComponent->GetName(), *ChildLabel));
	}

	USceneComponent* PreviousParent = ChildComponent->GetAttachParent();
	AActor* PreviousParentActor = PreviousParent ? PreviousParent->GetOwner() : nullptr;
	const FString PreviousParentLabel = PreviousParentActor ? PreviousParentActor->GetActorLabel() : FString();
	const FString PreviousParentName = PreviousParent ? PreviousParent->GetName() : FString();
	const FString PreviousParentClass = PreviousParent ? PreviousParent->GetClass()->GetName() : FString();
	const FString PreviousSocketName = PreviousParent && ChildComponent->GetAttachSocketName() != NAME_None
		? ChildComponent->GetAttachSocketName().ToString()
		: FString();
	const bool bWasAttached = PreviousParent != nullptr;

	if (bWasAttached)
	{
		Child->Modify();
		ChildComponent->Modify();
		ChildComponent->DetachFromComponent(FDetachmentTransformRules(EDetachmentRule::KeepWorld, true));
		Child->MarkPackageDirty();
	}
	if (ChildComponent->GetAttachParent() != nullptr)
	{
		return MCPError(FString::Printf(TEXT("Failed to detach component '%s' on actor '%s'"), *ChildComponent->GetName(), *ChildLabel));
	}

	auto Result = MCPSuccess();
	if (bWasAttached) MCPSetUpdated(Result); else MCPSetExisted(Result);
	Result->SetStringField(TEXT("childLabel"), Child->GetActorLabel());
	Result->SetStringField(TEXT("childComponentName"), ChildComponent->GetName());
	Result->SetStringField(TEXT("childComponentClass"), ChildComponent->GetClass()->GetName());
	Result->SetBoolField(TEXT("childIsRoot"), ChildComponent == Child->GetRootComponent());
	Result->SetStringField(TEXT("previousParentLabel"), PreviousParentLabel);
	Result->SetStringField(TEXT("previousParentComponentName"), PreviousParentName);
	Result->SetStringField(TEXT("previousParentComponentClass"), PreviousParentClass);
	Result->SetStringField(TEXT("previousSocketName"), PreviousSocketName);
	Result->SetBoolField(TEXT("detached"), true);
	Result->SetBoolField(TEXT("alreadyDetached"), !bWasAttached);
	Result->SetBoolField(TEXT("detachmentChanged"), bWasAttached);

	if (bWasAttached && PreviousParentActor)
	{
		// attach_component names both ends by path and re-resolves the two
		// components by name, which is exactly what was recorded above. The
		// detach kept the world transform and KeepWorld puts it back.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("childPath"), Child->GetPathName());
		Payload->SetStringField(TEXT("childLabel"), Child->GetActorLabel());
		Payload->SetStringField(TEXT("childComponentName"), ChildComponent->GetName());
		Payload->SetStringField(TEXT("parentPath"), PreviousParentActor->GetPathName());
		Payload->SetStringField(TEXT("parentLabel"), PreviousParentLabel);
		Payload->SetStringField(TEXT("parentComponentName"), PreviousParentName);
		Payload->SetStringField(TEXT("attachRule"), TEXT("KeepWorld"));
		if (!PreviousSocketName.IsEmpty()) Payload->SetStringField(TEXT("socketName"), PreviousSocketName);
		MCPSetRollback(Result, TEXT("attach_component"), Payload);
	}
	return MCPResult(Result);
}
