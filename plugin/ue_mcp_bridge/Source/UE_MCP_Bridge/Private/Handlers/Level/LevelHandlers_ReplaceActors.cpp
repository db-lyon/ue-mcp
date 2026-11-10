// Actor replacement and brush rebuilds through UEditorActorSubsystem (#1828).
//
// level(replace_actors_with_class) and level(replace_actors_with_asset) are the
// editor's "Replace Selected Actors With..." command over named actors. On UE
// 5.9 they call UEditorActorSubsystem::ReplaceActorsWithClass and
// ReplaceActorsWithAsset. Earlier engines have the same machinery one level
// down: the actor factory for the class or asset, handed to
// UEditorActorSubsystem::ReplaceActors (UEditorEngine::ReplaceActors on 5.4),
// which is exactly what the 5.9 functions do after validating their input.
//
// level(rebuild_brush) calls UEditorActorSubsystem::RebuildBrush, which only
// 5.9 has. Older engines rebuild a brush through UBrushBuilder::Build, which
// resets the brush's transform and pivot, so it is not the same operation and
// the action refuses there instead of doing something different.
//
// Translation-unit partition of FLevelHandlers; registration is
// RegisterActorReplaceHandlers below, called from LevelHandlers.cpp.

#include "LevelHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "HandlerLogCapture.h"
#include "HandlerScriptErrors.h"

#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "ActorFactories/ActorFactory.h"
#include "AssetSelection.h"
#include "Engine/Brush.h"
#include "Engine/BrushBuilder.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	/** Resolves every label to one actor, refusing a miss, an ambiguity or a repeat. */
	bool MCPReplaceResolveActors(UWorld* World, const TArray<FString>& Labels, TArray<AActor*>& OutActors, TSharedPtr<FJsonValue>& OutError)
	{
		for (const FString& Label : Labels)
		{
			FMCPActorSelector Selector;
			Selector.Match = EMCPActorMatch::LabelNameOrPath;
			AActor* Actor = MCPResolveActorToken(World, Label, OutError, Selector);
			if (!Actor) return false;
			if (OutActors.Contains(Actor))
			{
				OutError = MCPError(FString::Printf(TEXT("'%s' names an actor already in actorLabels; each actor can be replaced once."), *Label));
				return false;
			}
			OutActors.Add(Actor);
		}
		return true;
	}

	TSharedPtr<FJsonObject> MCPReplaceDescribeActor(const AActor* Actor)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Row->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		Row->SetStringField(TEXT("class"), Actor->GetClass()->GetPathName());
		return Row;
	}

	/** The result both replace actions return: what was replaced, by what, and why anything was not. */
	TSharedPtr<FJsonValue> MCPReplaceResult(
		const TArray<FString>& RequestedLabels,
		const TArray<AActor*>& NewActors,
		const FMCPScopedScriptErrors& Errors,
		const FMCPScopedLogCapture& Log,
		const FString& EngineFunction)
	{
		if (NewActors.Num() == 0)
		{
			FString Reason = Errors.Joined();
			for (const TSharedPtr<FJsonValue>& Problem : Log.ProblemsJson())
			{
				const TSharedPtr<FJsonObject> Obj = Problem->AsObject();
				if (Obj.IsValid()) Reason += (Reason.IsEmpty() ? TEXT("") : TEXT(" ")) + Obj->GetStringField(TEXT("message"));
			}
			return MCPError(Reason.IsEmpty()
				? FString::Printf(TEXT("%s replaced none of the %d actor(s)."), *EngineFunction, RequestedLabels.Num())
				: Reason);
		}

		auto Result = MCPSuccess();
		TArray<TSharedPtr<FJsonValue>> Rows;
		for (const AActor* Actor : NewActors)
		{
			if (Actor) Rows.Add(MakeShared<FJsonValueObject>(MCPReplaceDescribeActor(Actor)));
		}
		Result->SetArrayField(TEXT("newActors"), Rows);
		Result->SetNumberField(TEXT("requested"), RequestedLabels.Num());
		Result->SetNumberField(TEXT("replaced"), Rows.Num());
		Result->SetArrayField(TEXT("replacedLabels"), MCPStringListToJson(RequestedLabels));
		Result->SetBoolField(TEXT("changed"), true);
		Result->SetStringField(TEXT("engineFunction"), EngineFunction);
		if (Rows.Num() < RequestedLabels.Num())
		{
			Result->SetStringField(TEXT("warning"), FString::Printf(
				TEXT("Only %d of %d actor(s) were replaced; the rest are left as they were."), Rows.Num(), RequestedLabels.Num()));
			Result->SetArrayField(TEXT("problems"), Log.ProblemsJson());
		}
		MCPSetNoRollback(Result, TEXT("The replaced actors were deleted, and replacing the new ones back does not restore their properties or identity. The replacement is one transaction on the editor's undo stack, which editor(undo) reverts."));
		return MCPResult(Result);
	}

#if !UE_MCP_HAS_5_9_API
	/** ReplaceActors moved from UEditorEngine to UEditorActorSubsystem in 5.5. */
	void MCPReplaceRun(UActorFactory* Factory, const FAssetData& AssetData, const TArray<AActor*>& Actors, TArray<AActor*>& OutNew, bool bCopySourceProperties)
	{
#if UE_MCP_HAS_5_5_API
		UEditorActorSubsystem::ReplaceActors(Factory, AssetData, Actors, &OutNew, bCopySourceProperties);
#else
		GEditor->ReplaceActors(Factory, AssetData, Actors, &OutNew, bCopySourceProperties);
#endif
	}
#endif
}


// level(replace_actors_with_class): UEditorActorSubsystem::ReplaceActorsWithClass.
TSharedPtr<FJsonValue> FLevelHandlers::ReplaceActorsWithClass(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* LabelsField = nullptr;
	const bool bHasLabels = TryGetArrayParam(Params, TEXT("actorLabels"), LabelsField);
	FString ClassName;
	if (auto Err = RequireString(Params, TEXT("className"), ClassName)) return Err;
	const TArray<FString> Labels = bHasLabels ? JsonArrayToStringList(LabelsField) : TArray<FString>();
	if (Labels.Num() == 0) return MCPError(TEXT("Missing 'actorLabels': the actors to replace."));
	if (MCPIsPlayInEditorActive())
	{
		return MCPError(TEXT("Actors in the editor world cannot be replaced while a play-in-editor session is running. Stop play and retry."));
	}
	REQUIRE_EDITOR_WORLD(World);

	UClass* NewClass = MCPResolveClassOfType(ClassName, AActor::StaticClass());
	if (!NewClass) return MCPClassNotFoundError(ClassName, TEXT("className"));
	TArray<AActor*> Actors;
	TSharedPtr<FJsonValue> LookupError;
	if (!MCPReplaceResolveActors(World, Labels, Actors, LookupError)) return LookupError;

	FMCPScopedScriptErrors Errors;
	FMCPScopedLogCapture Log({ TEXT("LogUtils"), TEXT("LogScript"), TEXT("LogEditorScripting") });
	TArray<AActor*> NewActors;
#if UE_MCP_HAS_5_9_API
	UEditorActorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
	if (!Subsystem) return MCPError(TEXT("The editor actor subsystem is not available."));
	NewActors = Subsystem->ReplaceActorsWithClass(Actors, NewClass);
	const FString EngineFunction = TEXT("UEditorActorSubsystem::ReplaceActorsWithClass");
#else
	const FString EngineFunction = TEXT("UEditorActorSubsystem::ReplaceActors");
	UActorFactory* Factory = GEditor->FindActorFactoryForActorClass(NewClass);
	if (!Factory)
	{
		return MCPError(FString::Printf(TEXT("No actor factory is registered for class '%s'. A class that needs a source asset is replaced with level(replace_actors_with_asset)."), *NewClass->GetName()));
	}
	// The class itself, as the editor's replace menu passes it, or no asset for a factory that wants none.
	const FAssetData ClassAssetData(NewClass);
	FAssetData Resolved;
	FText CannotCreate;
	if (Factory->CanCreateActorFrom(ClassAssetData, CannotCreate))
	{
		Resolved = ClassAssetData;
	}
	else
	{
		FText Unused;
		if (!Factory->CanCreateActorFrom(FAssetData(), Unused))
		{
			return MCPError(FString::Printf(TEXT("'%s' needs a source asset, so replace with level(replace_actors_with_asset) instead (%s)."), *NewClass->GetName(), *CannotCreate.ToString()));
		}
	}
	{
		const FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "ReplaceActorsWithClass", "Replace Actor(s)"));
		MCPReplaceRun(Factory, Resolved, Actors, NewActors, /*bCopySourceProperties*/ false);
	}
#endif
	return MCPReplaceResult(Labels, NewActors, Errors, Log, EngineFunction);
}


// level(replace_actors_with_asset): UEditorActorSubsystem::ReplaceActorsWithAsset.
TSharedPtr<FJsonValue> FLevelHandlers::ReplaceActorsWithAsset(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* LabelsField = nullptr;
	const bool bHasLabels = TryGetArrayParam(Params, TEXT("actorLabels"), LabelsField);
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	const bool bCopySourceProperties = OptionalBool(Params, TEXT("copySourceProperties"), true);
	const TArray<FString> Labels = bHasLabels ? JsonArrayToStringList(LabelsField) : TArray<FString>();
	if (Labels.Num() == 0) return MCPError(TEXT("Missing 'actorLabels': the actors to replace."));
	if (MCPIsPlayInEditorActive())
	{
		return MCPError(TEXT("Actors in the editor world cannot be replaced while a play-in-editor session is running. Stop play and retry."));
	}
	REQUIRE_EDITOR_WORLD(World);

	UObject* Asset = MCPLoadAssetObject(AssetPath);
	if (!Asset) return MCPAssetNotFoundError(AssetPath, TEXT("the asset to create the new actors from"));
	TArray<AActor*> Actors;
	TSharedPtr<FJsonValue> LookupError;
	if (!MCPReplaceResolveActors(World, Labels, Actors, LookupError)) return LookupError;

	FMCPScopedScriptErrors Errors;
	FMCPScopedLogCapture Log({ TEXT("LogUtils"), TEXT("LogScript"), TEXT("LogEditorScripting") });
	TArray<AActor*> NewActors;
#if UE_MCP_HAS_5_9_API
	UEditorActorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
	if (!Subsystem) return MCPError(TEXT("The editor actor subsystem is not available."));
	NewActors = Subsystem->ReplaceActorsWithAsset(Actors, Asset, bCopySourceProperties);
	const FString EngineFunction = TEXT("UEditorActorSubsystem::ReplaceActorsWithAsset");
#else
	const FString EngineFunction = TEXT("UEditorActorSubsystem::ReplaceActors");
	UActorFactory* Factory = FActorFactoryAssetProxy::GetFactoryForAssetObject(Asset);
	if (!Factory)
	{
		return MCPError(FString::Printf(TEXT("No actor factory creates an actor from '%s'."), *Asset->GetPathName()));
	}
	{
		const FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "ReplaceActorsWithAsset", "Replace Actor(s)"));
		MCPReplaceRun(Factory, FAssetData(Asset), Actors, NewActors, bCopySourceProperties);
	}
#endif
	return MCPReplaceResult(Labels, NewActors, Errors, Log, EngineFunction);
}


// level(rebuild_brush): UEditorActorSubsystem::RebuildBrush.
TSharedPtr<FJsonValue> FLevelHandlers::RebuildBrush(const TSharedPtr<FJsonObject>& Params)
{
	const FString BuilderClassName = OptionalString(Params, TEXT("builderClass"));
	const TSharedPtr<FJsonObject>* BuilderPropsField = nullptr;
	const bool bHasBuilderProps = TryGetObjectParam(Params, TEXT("builderProperties"), BuilderPropsField) && BuilderPropsField && (*BuilderPropsField).IsValid();
	const bool bDeferBsp = OptionalBool(Params, TEXT("deferBsp"), false);
#if UE_MCP_HAS_5_9_API
	REQUIRE_EDITOR_WORLD(World);
	TSharedPtr<FJsonValue> LookupError;
	AActor* Actor = MCPResolveActor(World, Params, LookupError);
	if (!Actor) return LookupError;
	ABrush* Brush = Cast<ABrush>(Actor);
	if (!Brush)
	{
		return MCPError(FString::Printf(TEXT("'%s' is a %s, not a brush or volume."), *Actor->GetActorLabel(), *Actor->GetClass()->GetName()));
	}

	UBrushBuilder* Current = Brush->BrushBuilder;
	UClass* BuilderClass = nullptr;
	if (!BuilderClassName.IsEmpty())
	{
		BuilderClass = MCPResolveClassOfType(BuilderClassName, UBrushBuilder::StaticClass());
		if (!BuilderClass || BuilderClass->HasAnyClassFlags(CLASS_Abstract)) return MCPClassNotFoundError(BuilderClassName, TEXT("builderClass"));
	}
	else if (!Current)
	{
		return MCPError(FString::Printf(TEXT("'%s' has no brush builder to rebuild with. Pass builderClass (CubeBuilder, CylinderBuilder, ConeBuilder, CurvedStairBuilder, LinearStairBuilder, SpiralStairBuilder, TetrahedronBuilder, SheetBuilder, VolumetricBuilder)."), *Actor->GetActorLabel()));
	}

	// Resolve every property name before anything changes.
	UClass* TargetClass = BuilderClass ? BuilderClass : Current->GetClass();
	TArray<TPair<FProperty*, TSharedPtr<FJsonValue>>> Writes;
	if (bHasBuilderProps)
	{
		for (const auto& Pair : (*BuilderPropsField)->Values)
		{
			const FString Key(Pair.Key);
			FProperty* Prop = MCPJsonProperty::FindMemberProperty(TargetClass, Key);
			if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit))
			{
				return MCPError(FString::Printf(TEXT("'%s' is not an editable property of %s; nothing was rebuilt."), *Key, *TargetClass->GetName()));
			}
			Writes.Add(TPair<FProperty*, TSharedPtr<FJsonValue>>(Prop, Pair.Value));
		}
	}

	// The previous builder, as a rebuild_brush call can restore it.
	TSharedPtr<FJsonObject> PreviousProps = MakeShared<FJsonObject>();
	if (Current)
	{
		for (TFieldIterator<FProperty> It(Current->GetClass()); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Edit) && !It->HasAnyPropertyFlags(CPF_Deprecated))
			{
				PreviousProps->SetField(It->GetName(), MCPExportPropertyValue(*It, Current));
			}
		}
	}

	FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "RebuildBrush", "Rebuild Brush"));
	UBrushBuilder* Builder = nullptr;
	if (BuilderClass && (!Current || Current->GetClass() != BuilderClass))
	{
		// RebuildBrush copies an externally owned builder into the actor.
		Builder = NewObject<UBrushBuilder>(GetTransientPackage(), BuilderClass, NAME_None, RF_Transactional);
	}
	else
	{
		Builder = Current;
		// The engine asks for the actor's own builder to be recorded before its parameters change.
		Builder->Modify();
	}
	for (const TPair<FProperty*, TSharedPtr<FJsonValue>>& Write : Writes)
	{
		FString WriteError;
		if (!MCPJsonProperty::SetJsonOnProperty(Write.Key, Write.Key->ContainerPtrToValuePtr<void>(Builder), Write.Value, WriteError))
		{
			Transaction.Cancel();
			return MCPError(FString::Printf(TEXT("Could not write '%s': %s"), *Write.Key->GetName(), *WriteError));
		}
	}

	UEditorActorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
	if (!Subsystem)
	{
		Transaction.Cancel();
		return MCPError(TEXT("The editor actor subsystem is not available."));
	}
	FMCPScopedScriptErrors Errors;
	if (!Subsystem->RebuildBrush(Brush, Builder, bDeferBsp ? EBrushBSPRebuild::Deferred : EBrushBSPRebuild::Immediate))
	{
		Transaction.Cancel();
		return MCPError(Errors.Joined(TEXT("UEditorActorSubsystem::RebuildBrush refused the rebuild.")));
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), Brush->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Brush->GetPathName());
	if (Brush->BrushBuilder)
	{
		Result->SetStringField(TEXT("builderClass"), Brush->BrushBuilder->GetClass()->GetPathName());
		TSharedPtr<FJsonObject> Now = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Brush->BrushBuilder->GetClass()); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Edit) && !It->HasAnyPropertyFlags(CPF_Deprecated))
			{
				Now->SetField(It->GetName(), MCPExportPropertyValue(*It, Brush->BrushBuilder));
			}
		}
		Result->SetObjectField(TEXT("builderProperties"), Now);
	}
	Result->SetBoolField(TEXT("bspDeferred"), bDeferBsp);
	if (Current)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), Brush->GetPathName());
		Payload->SetStringField(TEXT("builderClass"), Current->GetClass()->GetPathName());
		Payload->SetObjectField(TEXT("builderProperties"), PreviousProps);
		MCPSetRollback(Result, TEXT("rebuild_brush"), Payload);
	}
	else
	{
		MCPSetNoRollback(Result, TEXT("The brush had no builder before this call, so no rebuild_brush call can describe its previous shape. The rebuild is on the editor's undo stack."));
	}
	return MCPResult(Result);
#else
	return MCPUnsupportedEngineError(TEXT("UEditorActorSubsystem::RebuildBrush"), TEXT("5.9"),
		TEXT("Older engines rebuild a brush only through its builder, which resets the brush's transform and pivot"));
#endif
}


void FLevelHandlers::RegisterActorReplaceHandlers(FMCPHandlerRegistry& Registry)
{
	using EType = EMCPParamType;
	const FMCPParamSpec ActorLabelsSpec = MCPParam::Required(TEXT("actorLabels"), EType::Array, TEXT("Labels (or names or paths) of the actors to replace")).Items(EType::String);

	Registry.RegisterHandler(TEXT("replace_actors_with_class"), &ReplaceActorsWithClass, {
		ActorLabelsSpec,
		MCPParam::Required(TEXT("className"), EType::String, TEXT("Actor class to create in their place; it needs an actor factory that works without an asset (lights, empty actors, volumes)")),
	});
	Registry.RegisterHandler(TEXT("replace_actors_with_asset"), &ReplaceActorsWithAsset, {
		ActorLabelsSpec,
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("Asset to create the new actors from: a static mesh, Blueprint, particle system...")),
		MCPParam::Optional(TEXT("copySourceProperties"), EType::Boolean, TEXT("When the asset is a Blueprint sharing a native parent with a replaced actor, copy that actor's property values (default true)")),
	});
	Registry.RegisterHandler(TEXT("rebuild_brush"), &RebuildBrush, {
		MCPParam::Optional(TEXT("actorLabel"), EType::String, TEXT("Brush or volume to rebuild")),
		MCPParam::Optional(TEXT("actorPath"), EType::String, TEXT("Object path of the brush; preferred over actorLabel when known")),
		MCPParam::Optional(TEXT("builderClass"), EType::String, TEXT("Brush builder class to rebuild with (default: the brush's current builder)")),
		MCPParam::Optional(TEXT("builderProperties"), EType::Object, TEXT("Builder property name -> value, such as {X: 400, Y: 200, Z: 100} for a CubeBuilder")),
		MCPParam::Optional(TEXT("deferBsp"), EType::Boolean, TEXT("Defer the BSP rebuild for batching; a later call without it processes the pending BSP (default false)")),
	}, MCPSpec::AtLeastOne({ { TEXT("actorLabel") }, { TEXT("actorPath") } }));
}
