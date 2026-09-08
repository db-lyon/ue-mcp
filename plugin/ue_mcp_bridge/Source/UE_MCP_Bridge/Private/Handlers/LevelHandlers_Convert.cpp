// level(convert_brushes_to_static_mesh) (#911).
//
// BSP-to-StaticMesh conversion is a first-class editor operation
// (UEditorActorSubsystem::ConvertActors) with no bridge action, so a blockout
// pass had to be finished by hand or through Python. The wrapper is thin; the
// work here is the safety checks, because ConvertActors DESTROYS the actors it
// converts and there is no undo path back to a builder brush once the map is
// saved.
//
// What it refuses, and why each one is a refusal rather than a warning:
//
//   The default builder brush. It is the editor's own scratch brush, not level
//   geometry, and destroying it leaves the brush tools without a subject.
//   Subtractive brushes. They carve space out of additive geometry and have no
//   surface of their own, so converting one produces an empty mesh that looks
//   like a successful conversion.
//   Volumes and brush shapes. Both derive from ABrush and neither is geometry
//   a player sees; a trigger volume converted to a StaticMeshActor is a
//   collision change disguised as a mesh.
//
// Each of those can be opted into by name, which is the difference between a
// guardrail and an obstacle.

#include "LevelHandlers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Brush.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Misc/PackageName.h"
#include "Subsystems/EditorActorSubsystem.h"

namespace
{
	constexpr int32 MCPConvertMaxBrushes = 500;

	/** Per-brush verdict, so every candidate the selector picked is accounted
	 *  for rather than silently dropped from the count. */
	const TCHAR* const MCPConvertStatusEligible      = TEXT("eligible");
	const TCHAR* const MCPConvertStatusDefaultBrush  = TEXT("skipped_default_builder_brush");
	const TCHAR* const MCPConvertStatusSubtractive   = TEXT("skipped_subtractive");
	const TCHAR* const MCPConvertStatusVolume        = TEXT("skipped_volume");
	const TCHAR* const MCPConvertStatusBrushShape    = TEXT("skipped_brush_shape");
	const TCHAR* const MCPConvertStatusNoGeometry    = TEXT("skipped_no_brush_geometry");
	const TCHAR* const MCPConvertStatusWrongClass    = TEXT("skipped_class_filter");

	struct FMCPConvertCandidate
	{
		ABrush* Brush = nullptr;
		FString Label;
		FString ClassName;
		FString Status;
		FVector Location = FVector::ZeroVector;
	};
}

TSharedPtr<FJsonValue> FLevelHandlers::ConvertBrushesToStaticMesh(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	REQUIRE_EDITOR_WORLD(World);

	if (!GEditor)
	{
		return MCPError(TEXT("GEditor is not available"));
	}
	if (GEditor->PlayWorld != nullptr || GEditor->bIsSimulatingInEditor)
	{
		return MCPError(TEXT("Stop PIE or SIE before converting brushes"));
	}

	// ── Selection ───────────────────────────────────────────────────────────
	TArray<FString> ActorLabels;
	{
		const TArray<TSharedPtr<FJsonValue>>* LabelValues = nullptr;
		if (Params->TryGetArrayField(TEXT("actorLabels"), LabelValues) && LabelValues)
		{
			ActorLabels = JsonArrayToStringList(LabelValues);
		}
	}
	const FString FolderPath = OptionalString(Params, TEXT("folderPath"));
	const bool bRecursiveFolder = OptionalBool(Params, TEXT("recursiveFolder"), true);
	if (ActorLabels.IsEmpty() && FolderPath.IsEmpty())
	{
		return MCPError(TEXT("Pass 'actorLabels' or 'folderPath'. This destroys the actors it converts, so it will not run against a whole level implicitly."));
	}

	// An EXACT class filter, which is the thing the folder queries could not
	// do: level(epic_get_actors_in_folder) has no class filter at all, so
	// narrowing a folder to one brush class meant a get_actor_details round
	// trip per entry (#911).
	const FString ClassFilterSpec = OptionalString(Params, TEXT("classFilter"));
	UClass* ClassFilter = nullptr;
	if (!ClassFilterSpec.IsEmpty())
	{
		ClassFilter = MCPResolveClassOfType(ClassFilterSpec, ABrush::StaticClass(), true);
		if (!ClassFilter)
		{
			return MCPClassNotFoundError(ClassFilterSpec, TEXT("classFilter"));
		}
	}
	const bool bExactClass = OptionalBool(Params, TEXT("exactClass"), true);

	// ── Destination ─────────────────────────────────────────────────────────
	FString DestinationPath = OptionalString(Params, TEXT("destinationPath"), TEXT("/Game/Meshes/Converted"));
	DestinationPath.RemoveFromEnd(TEXT("/"));
	if (!FPackageName::IsValidLongPackageName(DestinationPath / TEXT("Probe")))
	{
		return MCPError(FString::Printf(
			TEXT("'destinationPath' is not a valid content path: %s"), *DestinationPath));
	}
	if (DestinationPath.StartsWith(TEXT("/Engine/")) ||
		DestinationPath.StartsWith(TEXT("/Script/")) ||
		DestinationPath.StartsWith(TEXT("/Temp/")))
	{
		return MCPError(FString::Printf(
			TEXT("'destinationPath' is outside writable project content: %s"), *DestinationPath));
	}

	// ── Safety switches ─────────────────────────────────────────────────────
	// dryRun defaults to TRUE. Conversion destroys the source brushes, and a
	// preview is the only chance to notice that the selector picked up a
	// trigger volume.
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), true);
	const bool bAllowSubtractive = OptionalBool(Params, TEXT("allowSubtractive"), false);
	const bool bIncludeVolumes = OptionalBool(Params, TEXT("includeVolumes"), false);

	// ── Gather ──────────────────────────────────────────────────────────────
	ABrush* DefaultBrush = World->GetDefaultBrush();
	TSet<FString> WantedLabels(ActorLabels);
	TSet<FString> FoundLabels;
	TArray<FMCPConvertCandidate> Candidates;

	for (TActorIterator<ABrush> It(World); It; ++It)
	{
		ABrush* Brush = *It;
		if (!Brush) continue;

		const FString Label = Brush->GetActorLabel();
		bool bSelected = false;
		if (!WantedLabels.IsEmpty() && WantedLabels.Contains(Label))
		{
			bSelected = true;
			FoundLabels.Add(Label);
		}
		if (!bSelected && !FolderPath.IsEmpty())
		{
			const FString Folder = Brush->GetFolderPath().ToString();
			bSelected = bRecursiveFolder
				? (Folder.Equals(FolderPath, ESearchCase::IgnoreCase) ||
				   Folder.StartsWith(FolderPath + TEXT("/"), ESearchCase::IgnoreCase))
				: Folder.Equals(FolderPath, ESearchCase::IgnoreCase);
		}
		if (!bSelected) continue;

		FMCPConvertCandidate Candidate;
		Candidate.Brush = Brush;
		Candidate.Label = Label;
		Candidate.ClassName = Brush->GetClass()->GetName();
		Candidate.Location = Brush->GetActorLocation();

		if (ClassFilter)
		{
			const bool bClassMatches = bExactClass
				? (Brush->GetClass() == ClassFilter)
				: Brush->IsA(ClassFilter);
			if (!bClassMatches)
			{
				Candidate.Status = MCPConvertStatusWrongClass;
				Candidates.Add(MoveTemp(Candidate));
				continue;
			}
		}

		if (Brush == DefaultBrush)                       Candidate.Status = MCPConvertStatusDefaultBrush;
		else if (Brush->IsBrushShape())                  Candidate.Status = MCPConvertStatusBrushShape;
		else if (Brush->IsVolumeBrush() && !bIncludeVolumes) Candidate.Status = MCPConvertStatusVolume;
		else if (Brush->BrushType == Brush_Subtract && !bAllowSubtractive) Candidate.Status = MCPConvertStatusSubtractive;
		else if (Brush->Brush == nullptr)                Candidate.Status = MCPConvertStatusNoGeometry;
		else                                             Candidate.Status = MCPConvertStatusEligible;

		Candidates.Add(MoveTemp(Candidate));
	}

	TArray<AActor*> Eligible;
	TArray<TSharedPtr<FJsonValue>> CandidateRows;
	TMap<FString, int32> StatusCounts;
	for (const FMCPConvertCandidate& Candidate : Candidates)
	{
		++StatusCounts.FindOrAdd(Candidate.Status);
		if (Candidate.Status == MCPConvertStatusEligible)
		{
			Eligible.Add(Candidate.Brush);
		}
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("actorLabel"), Candidate.Label);
		Row->SetStringField(TEXT("actorClass"), Candidate.ClassName);
		Row->SetStringField(TEXT("status"), Candidate.Status);
		Row->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Candidate.Location));
		CandidateRows.Add(MakeShared<FJsonValueObject>(Row));
	}

	TArray<FString> MissingLabels;
	for (const FString& Label : ActorLabels)
	{
		if (!FoundLabels.Contains(Label)) MissingLabels.Add(Label);
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetStringField(TEXT("destinationPath"), DestinationPath);
	Result->SetNumberField(TEXT("candidates"), Candidates.Num());
	Result->SetNumberField(TEXT("eligible"), Eligible.Num());
	Result->SetArrayField(TEXT("missingLabels"), MCPStringListToJson(MissingLabels));
	Result->SetArrayField(TEXT("brushes"), CandidateRows);
	{
		TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
		for (const TPair<FString, int32>& Pair : StatusCounts)
		{
			Summary->SetNumberField(Pair.Key, Pair.Value);
		}
		Result->SetObjectField(TEXT("statusSummary"), Summary);
	}

	if (!MissingLabels.IsEmpty())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%d requested label(s) matched no brush actor in this level. Note the selector only iterates ABrush actors, so a label belonging to a non-brush actor will always miss; see missingLabels."),
			MissingLabels.Num()));
		return MCPResult(Result);
	}
	if (Eligible.Num() > MCPConvertMaxBrushes)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%d eligible brushes, over the maximum of %d for one call. Convert in folders."),
			Eligible.Num(), MCPConvertMaxBrushes));
		return MCPResult(Result);
	}
	if (Eligible.IsEmpty())
	{
		Result->SetStringField(TEXT("zeroMatchNote"),
			TEXT("No brush was eligible. Read statusSummary: subtractive brushes, volumes, brush shapes and the default builder brush are excluded unless you opt in with allowSubtractive or includeVolumes."));
		return MCPResult(Result);
	}

	if (bDryRun)
	{
		Result->SetNumberField(TEXT("wouldConvert"), Eligible.Num());
		Result->SetStringField(TEXT("dryRunNote"),
			TEXT("dryRun defaults to TRUE for this action because conversion DESTROYS the source brushes. Pass dryRun=false to commit."));
		return MCPResult(Result);
	}

	UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	if (!ActorSubsystem)
	{
		return MCPError(TEXT("UEditorActorSubsystem is not available"));
	}

	// Record the source identity before the call, because the actors are gone
	// afterwards and the report would otherwise be able to say only "some
	// number of brushes used to be here".
	TArray<FString> ConvertedFrom;
	for (AActor* Actor : Eligible)
	{
		ConvertedFrom.Add(FString::Printf(TEXT("%s (%s)"), *Actor->GetActorLabel(), *Actor->GetClass()->GetName()));
	}

	const TArray<AActor*> NewActors = ActorSubsystem->ConvertActors(
		Eligible, AStaticMeshActor::StaticClass(), DestinationPath / FString());

	TArray<FString> NewLabels;
	TArray<FString> NewMeshPaths;
	for (AActor* Actor : NewActors)
	{
		if (!Actor) continue;
		NewLabels.Add(Actor->GetActorLabel());
		if (const AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(Actor))
		{
			if (const UStaticMeshComponent* Component = MeshActor->GetStaticMeshComponent())
			{
				if (const UStaticMesh* Mesh = Component->GetStaticMesh())
				{
					NewMeshPaths.AddUnique(Mesh->GetPathName());
				}
			}
		}
	}

	Result->SetNumberField(TEXT("converted"), NewActors.Num());
	Result->SetArrayField(TEXT("convertedFrom"), MCPStringListToJson(ConvertedFrom));
	Result->SetArrayField(TEXT("newActorLabels"), MCPStringListToJson(NewLabels));
	Result->SetArrayField(TEXT("newStaticMeshes"), MCPStringListToJson(NewMeshPaths));
	if (NewActors.IsEmpty())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"),
			TEXT("ConvertActors returned no new actors. The editor defers a brush conversion when it needs a destination package decision, so check whether a modal prompt is waiting in the editor window."));
	}
	else
	{
		MCPSetCreated(Result);
		// The header says there is no undo path back to a builder brush, and
		// the result has to say it too: a caller cannot tell a considered
		// decision from a forgotten one by reading a missing field.
		MCPSetNoRollback(Result,
			TEXT("ConvertActors destroyed the source brushes and replaced them with StaticMeshActors and newly generated StaticMesh assets. ")
			TEXT("Undoing that would need a call that rebuilds an ABrush from its original brush model, and the bridge has no action that creates a brush at all."));
		Result->SetStringField(TEXT("saveNote"),
			TEXT("The level is left dirty and is NOT saved, and the generated static meshes are new unsaved packages. Save both when you have checked the result."));
	}
	return MCPResult(Result);
}
