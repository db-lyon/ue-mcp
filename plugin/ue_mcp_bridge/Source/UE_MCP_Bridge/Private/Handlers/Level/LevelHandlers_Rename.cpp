#include "LevelHandlers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Brush.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "ScopedTransaction.h"
#include "UObject/NameTypes.h"
#include "UObject/UObjectGlobals.h"

// Rename a placed actor's object name (FName), keeping its editor label (#1574).
// UObject::Rename onto a name its outer already holds is a Fatal error, so every
// target is tested before anything is renamed. Registration stays in
// LevelHandlers.cpp.

namespace
{
	struct FMCPActorRename
	{
		AActor* Actor = nullptr;
		FString OldName;
		FString OldPath;
		FString Label;
		FString NewName;
		bool bGenerated = false;
	};

	/** Why this actor's object name must not be changed, or empty when it may. */
	FString MCPWhyActorNotRenamable(AActor* Actor)
	{
		ULevel* Level = Actor->GetLevel();
		if (!Level || Actor->GetOuter() != Level)
		{
			return TEXT("its outer is not a level");
		}
		if (Actor->IsA<AWorldSettings>()) return TEXT("it is the level's WorldSettings actor");
		if (Actor->IsA<ALevelScriptActor>()) return TEXT("it is the level script actor");
		if (Level->GetDefaultBrush() == Actor) return TEXT("it is the level's default brush");
		if (Actor->IsPackageExternal())
		{
			return TEXT("it is saved in its own external actor package (World Partition or one file per actor), whose file name is derived from the actor's path; renaming it would leave that package misnamed");
		}
		if (Actor->HasAnyFlags(RF_Transient)) return TEXT("it is transient and is never saved");
		return FString();
	}
}

TSharedPtr<FJsonValue> FLevelHandlers::RenameActorObject(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("newName"), TEXT("renames"), TEXT("dryRun"),
	});

	REQUIRE_EDITOR_WORLD(World);
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);

	// One request shape for both forms: a list of {selector, newName?}.
	TArray<TSharedPtr<FJsonObject>> Entries;
	const TArray<TSharedPtr<FJsonValue>>* RenamesArr = nullptr;
	const bool bBatch = TryGetArrayParam(Params, TEXT("renames"), RenamesArr) && RenamesArr;
	if (bBatch)
	{
		if (HasParam(Params, TEXT("actorLabel")) || HasParam(Params, TEXT("actorPath")) || HasParam(Params, TEXT("newName")))
		{
			return MCPError(TEXT("Pass either renames[] or a single actorLabel/actorPath with newName, not both."));
		}
		if (RenamesArr->Num() == 0) return MCPError(TEXT("renames is empty"));
		if (RenamesArr->Num() > 512) return MCPError(TEXT("renames holds more than 512 entries"));
		for (int32 i = 0; i < RenamesArr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!(*RenamesArr)[i].IsValid() || !(*RenamesArr)[i]->TryGetObject(Obj) || !Obj || !Obj->IsValid())
			{
				return MCPError(FString::Printf(TEXT("renames[%d] is not an object {actorPath|actorLabel, newName?}"), i));
			}
			Entries.Add(*Obj);
		}
	}
	else
	{
		TSharedPtr<FJsonObject> Single = MakeShared<FJsonObject>();
		FString S;
		if (TryGetStringParam(Params, TEXT("actorPath"), S)) Single->SetStringField(TEXT("actorPath"), S);
		if (TryGetStringParam(Params, TEXT("actorLabel"), S)) Single->SetStringField(TEXT("actorLabel"), S);
		if (TryGetStringParam(Params, TEXT("newName"), S)) Single->SetStringField(TEXT("newName"), S);
		Entries.Add(Single);
	}

	// Preflight every entry. Nothing is renamed unless all of them pass.
	TArray<FMCPActorRename> Plan;
	TArray<FString> Problems;
	TSet<AActor*> Seen;
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		const FString Where = bBatch ? FString::Printf(TEXT("renames[%d]: "), i) : FString();
		TSharedPtr<FJsonValue> ActorErr;
		AActor* Actor = MCPResolveActor(World, Entries[i], ActorErr);
		if (!Actor)
		{
			FString Msg = TEXT("actor not resolved");
			if (ActorErr.IsValid() && ActorErr->AsObject().IsValid())
			{
				ActorErr->AsObject()->TryGetStringField(TEXT("error"), Msg);
			}
			Problems.Add(Where + Msg);
			continue;
		}
		if (Seen.Contains(Actor))
		{
			Problems.Add(Where + FString::Printf(TEXT("'%s' is named by more than one entry"), *Actor->GetPathName()));
			continue;
		}
		Seen.Add(Actor);
		const FString Why = MCPWhyActorNotRenamable(Actor);
		if (!Why.IsEmpty())
		{
			Problems.Add(Where + FString::Printf(TEXT("'%s' cannot be renamed: %s"), *Actor->GetPathName(), *Why));
			continue;
		}

		FMCPActorRename R;
		R.Actor = Actor;
		R.OldName = Actor->GetName();
		R.OldPath = Actor->GetPathName();
		R.Label = Actor->GetActorLabel();
		Entries[i]->TryGetStringField(TEXT("newName"), R.NewName);
		R.NewName.TrimStartAndEndInline();
		Plan.Add(R);
	}

	// Explicit names first, so a generated one can avoid them.
	TSet<FName> Claimed;
	for (FMCPActorRename& R : Plan)
	{
		if (R.NewName.IsEmpty()) continue;
		FText Reason;
		if (!FName::IsValidXName(R.NewName, INVALID_OBJECTNAME_CHARACTERS, &Reason))
		{
			Problems.Add(FString::Printf(TEXT("'%s' is not a valid object name: %s"), *R.NewName, *Reason.ToString()));
			continue;
		}
		const FName Target(*R.NewName);
		if (R.NewName.Equals(R.OldName, ESearchCase::CaseSensitive)) continue;
		if (Claimed.Contains(Target))
		{
			Problems.Add(FString::Printf(TEXT("'%s' is the newName of more than one entry"), *R.NewName));
			continue;
		}
		Claimed.Add(Target);
		ULevel* Level = R.Actor->GetLevel();
		UObject* Existing = StaticFindObjectFast(nullptr, Level, Target);
		if (Existing && Existing != R.Actor)
		{
			Problems.Add(FString::Printf(TEXT("'%s' is already taken in %s by %s '%s'; renaming onto it would be a fatal engine error"),
				*R.NewName, *Level->GetPathName(), *Existing->GetClass()->GetName(), *Existing->GetPathName()));
			continue;
		}
		if (!Existing && !R.Actor->Rename(*R.NewName, nullptr, REN_Test))
		{
			Problems.Add(FString::Printf(TEXT("the engine refused '%s' as a new name for '%s'"), *R.NewName, *R.OldPath));
		}
	}
	for (FMCPActorRename& R : Plan)
	{
		if (!R.NewName.IsEmpty()) continue;
		ULevel* Level = R.Actor->GetLevel();
		FName Generated;
		for (int32 Attempt = 0; Attempt < 64; ++Attempt)
		{
			const FName Candidate = MakeUniqueObjectName(Level, R.Actor->GetClass());
			if (!Claimed.Contains(Candidate) && !StaticFindObjectFast(nullptr, Level, Candidate))
			{
				Generated = Candidate;
				break;
			}
		}
		if (Generated.IsNone())
		{
			Problems.Add(FString::Printf(TEXT("no unique name could be generated for '%s'"), *R.OldPath));
			continue;
		}
		Claimed.Add(Generated);
		R.NewName = Generated.ToString();
		R.bGenerated = true;
	}

	if (Problems.Num() > 0)
	{
		TSharedPtr<FJsonObject> Err = MCPErrorObject(FString::Printf(
			TEXT("Nothing was renamed: %d problem(s). %s"), Problems.Num(), *FString::Join(Problems, TEXT(" | "))));
		TArray<TSharedPtr<FJsonValue>> ProblemsOut;
		for (const FString& P : Problems) ProblemsOut.Add(MakeShared<FJsonValueString>(P));
		Err->SetArrayField(TEXT("problems"), ProblemsOut);
		return MakeShared<FJsonValueObject>(Err);
	}

	TArray<TSharedPtr<FJsonValue>> Out;
	TArray<TSharedPtr<FJsonValue>> Inverse;
	TSet<UPackage*> Dirtied;
	int32 RenamedCount = 0;
	{
		TUniquePtr<FScopedTransaction> Transaction;
		if (!bDryRun)
		{
			Transaction = MakeUnique<FScopedTransaction>(FText::FromString(
				Plan.Num() == 1 ? TEXT("Rename Actor Object") : TEXT("Rename Actor Objects")));
		}
		for (FMCPActorRename& R : Plan)
		{
			const bool bChange = !R.NewName.Equals(R.OldName, ESearchCase::CaseSensitive);
			bool bRenamed = false;
			bool bLabelKept = true;
			if (!bDryRun && bChange)
			{
				R.Actor->Modify();
				bRenamed = R.Actor->Rename(*R.NewName, nullptr, REN_DontCreateRedirectors);
				if (bRenamed)
				{
					// A label the actor never had set explicitly is derived from
					// its name; pin it to the one it showed before the rename.
					if (!R.Actor->GetActorLabel().Equals(R.Label, ESearchCase::CaseSensitive))
					{
						R.Actor->SetActorLabel(R.Label, false);
					}
					bLabelKept = R.Actor->GetActorLabel().Equals(R.Label, ESearchCase::CaseSensitive);
					R.Actor->MarkPackageDirty();
					if (UPackage* Pkg = R.Actor->GetPackage()) Dirtied.Add(Pkg);
					++RenamedCount;
				}
			}

			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("oldName"), R.OldName);
			Row->SetStringField(TEXT("newName"), bDryRun || !bChange ? R.NewName : R.Actor->GetName());
			Row->SetStringField(TEXT("oldPath"), R.OldPath);
			if (bDryRun || !bChange)
			{
				Row->SetStringField(TEXT("newPath"), bChange
					? R.Actor->GetLevel()->GetPathName() + TEXT(".") + R.NewName
					: R.OldPath);
			}
			else
			{
				Row->SetStringField(TEXT("newPath"), R.Actor->GetPathName());
			}
			Row->SetStringField(TEXT("actorLabel"), R.Actor->GetActorLabel());
			Row->SetBoolField(TEXT("generatedName"), R.bGenerated);
			Row->SetBoolField(TEXT("unchanged"), !bChange);
			if (!bDryRun)
			{
				Row->SetBoolField(TEXT("renamed"), bRenamed);
				Row->SetBoolField(TEXT("labelKept"), bLabelKept);
			}
			Out.Add(MakeShared<FJsonValueObject>(Row));

			if (bRenamed)
			{
				TSharedPtr<FJsonObject> Back = MakeShared<FJsonObject>();
				Back->SetStringField(TEXT("actorPath"), R.Actor->GetPathName());
				Back->SetStringField(TEXT("newName"), R.OldName);
				Inverse.Add(MakeShared<FJsonValueObject>(Back));
			}
		}
	}

	// A rename the engine refused after every test passed is still a failure.
	if (!bDryRun)
	{
		int32 Expected = 0;
		for (const FMCPActorRename& R : Plan)
		{
			if (!R.NewName.Equals(R.OldName, ESearchCase::CaseSensitive)) ++Expected;
		}
		if (RenamedCount != Expected)
		{
			TSharedPtr<FJsonObject> Err = MCPErrorObject(FString::Printf(
				TEXT("UObject::Rename refused %d of %d renames after preflight passed; the rest were applied and can be undone with editor(undo)."),
				Expected - RenamedCount, Expected));
			Err->SetArrayField(TEXT("renames"), Out);
			return MakeShared<FJsonValueObject>(Err);
		}
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetArrayField(TEXT("renames"), Out);
	Result->SetNumberField(TEXT("renamedCount"), RenamedCount);
	TArray<TSharedPtr<FJsonValue>> DirtiedOut;
	for (UPackage* Pkg : Dirtied) DirtiedOut.Add(MakeShared<FJsonValueString>(Pkg->GetName()));
	Result->SetArrayField(TEXT("dirtiedPackages"), DirtiedOut);
	if (RenamedCount > 0)
	{
		MCPSetUpdated(Result);
		Result->SetStringField(TEXT("note"),
			TEXT("Object paths changed. Soft references that stored the old path (other maps, sequences, data assets) do not follow a rename and no redirector is created. Run level(save) to write the map."));
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("renames"), Inverse);
		MCPSetRollback(Result, TEXT("rename_actor_object"), Payload);
	}
	else
	{
		Result->SetBoolField(TEXT("updated"), false);
	}
	return MCPResult(Result);
}
