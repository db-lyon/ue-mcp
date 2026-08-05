// Split from AssetHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FAssetHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in AssetHandlers.cpp::RegisterHandlers.

#include "AssetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
// FSkeletalMaterial moved out of Engine/SkeletalMesh.h in later UE versions.
// Pull it explicitly via SkinnedAssetCommon when available.
#if __has_include("Engine/SkinnedAssetCommon.h")
#include "Engine/SkinnedAssetCommon.h"
#endif
#include "HandlerJsonProperty.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "ClothingAsset.h"
#include "ClothLODData.h"
#include "PointWeightMap.h"
#include "ClothConfigBase.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Animation/Skeleton.h"
#include "StaticMeshResources.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"
#include "AI/Navigation/NavCollisionBase.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

TSharedPtr<FJsonValue> FAssetHandlers::SetMeshMaterial(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString MaterialPath;
	if (auto Err = RequireString(Params, TEXT("materialPath"), MaterialPath)) return Err;

	int32 SlotIndex = OptionalInt(Params, TEXT("slotIndex"), 0);

	UStaticMesh* Mesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *AssetPath));
	if (!Mesh)
	{
		return MCPError(FString::Printf(TEXT("Failed to load static mesh at '%s'"), *AssetPath));
	}

	UMaterialInterface* Material = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MaterialPath));
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Failed to load material at '%s'"), *MaterialPath));
	}

	if (SlotIndex < 0 || SlotIndex >= Mesh->GetStaticMaterials().Num())
	{
		return MCPError(FString::Printf(TEXT("Slot index %d out of range (mesh has %d slots)"), SlotIndex, Mesh->GetStaticMaterials().Num()));
	}

	// Capture previous material for self-inverse rollback.
	FString PreviousMaterialPath;
	if (UMaterialInterface* Prev = Mesh->GetMaterial(SlotIndex))
	{
		PreviousMaterialPath = Prev->GetPathName();
	}

	Mesh->SetMaterial(SlotIndex, Material);
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("materialPath"), MaterialPath);
	Result->SetNumberField(TEXT("slotIndex"), SlotIndex);
	Result->SetStringField(TEXT("previousMaterialPath"), PreviousMaterialPath);

	if (!PreviousMaterialPath.IsEmpty())
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("materialPath"), PreviousMaterialPath);
		Payload->SetNumberField(TEXT("slotIndex"), SlotIndex);
		MCPSetRollback(Result, TEXT("set_mesh_material"), Payload);
	}

	return MCPResult(Result);
}

// ─── #822 set_mesh_materials_batch ──────────────────────────────────
// Batch counterpart to set_mesh_material. Assigning materials to an imported
// kit is N meshes x M slots, and one call per pair made material assignment
// the only step in the import path that scales with kit size.
//
// Slots are addressable by NAME as well as by index. A reimport is free to
// reorder slot indices while the imported slot names stay put, so a batch
// keyed on index alone silently writes the wrong material after a reimport.
// Static and skeletal meshes are both accepted; the handler reads the
// material array that matches the asset it loaded.
namespace
{
	constexpr int32 MaxMeshMaterialAssignments = 500;

	// Per-item outcome vocabulary. Every submitted assignment gets exactly one
	// of these back, so a caller can always account for all N entries it sent.
	const TCHAR* const MeshMatStatusOk           = TEXT("ok");
	const TCHAR* const MeshMatStatusInvalid      = TEXT("invalid");
	const TCHAR* const MeshMatStatusProtected    = TEXT("protected");
	const TCHAR* const MeshMatStatusDuplicate    = TEXT("duplicate");
	const TCHAR* const MeshMatStatusNotFound     = TEXT("not_found");
	const TCHAR* const MeshMatStatusSlotNotFound = TEXT("slot_not_found");
	const TCHAR* const MeshMatStatusUpdated      = TEXT("updated");
	const TCHAR* const MeshMatStatusUnchanged    = TEXT("unchanged");
	const TCHAR* const MeshMatStatusFailed       = TEXT("failed");
	const TCHAR* const MeshMatStatusSkipped      = TEXT("skipped");

	// Mirrors IsProtectedAssetPath in AssetHandlers.cpp, which is file-local to
	// that translation unit. Keep the two rule sets identical: a write path that
	// enforces a weaker rule than its neighbours is how engine content gets
	// mutated through the bridge.
	bool IsProtectedMeshMaterialPath(const FString& Path)
	{
		FString Normalized = Path;
		Normalized.TrimStartAndEndInline();
		if (Normalized.IsEmpty()) return false;
		if (!Normalized.StartsWith(TEXT("/"))) Normalized = TEXT("/") + Normalized;
		const FString Lower = Normalized.ToLower();
		if (Lower.StartsWith(TEXT("/engine/"))) return true;
		if (Lower.StartsWith(TEXT("/memory/"))) return true;
		if (Lower.StartsWith(TEXT("/temp/"))) return true;
		if (Lower.Contains(TEXT("/script/"))) return true;
		return false;
	}

	struct FPreparedMeshMaterialAssignment
	{
		int32 ItemIndex = 0;
		FString AssetPath;
		FString MaterialPath;
		/** Slot addressing exactly as submitted, echoed back on rejection. */
		FString RequestedSlotName;
		int32 RequestedSlotIndex = INDEX_NONE;
		bool bHasSlotName = false;
		bool bHasSlotIndex = false;
		/** Slot the preflight resolved the request to. */
		int32 SlotIndex = INDEX_NONE;
		FString SlotName;
		FString PreviousMaterialPath;
		UMaterialInterface* Material = nullptr;
		int32 TargetIndex = INDEX_NONE;
		/** Preflight verdict. An empty error means the item is eligible to apply. */
		FString Status;
		FString Error;
		bool bWouldChange = false;
		bool bApplied = false;
		bool bChanged = false;
		bool PassedPreflight() const { return Error.IsEmpty(); }
	};

	/** One mesh asset plus every assignment aimed at it, so a mesh is loaded,
	 *  written and saved once no matter how many of its slots the batch names. */
	struct FPreparedMeshTarget
	{
		UStaticMesh* StaticMesh = nullptr;
		USkeletalMesh* SkeletalMesh = nullptr;
		/** Working copy for skeletal meshes; USkeletalMesh has no per-slot setter. */
		TArray<FSkeletalMaterial> SkeletalMaterials;
		TArray<int32> AssignmentIndices;
		bool bChanged = false;
		bool bSaved = false;
	};

	/** Human-readable slot inventory, so a slotName miss says what WAS there. */
	FString DescribeMeshSlots(const FPreparedMeshTarget& Target)
	{
		TArray<FString> Names;
		if (Target.StaticMesh)
		{
			for (const FStaticMaterial& Slot : Target.StaticMesh->GetStaticMaterials())
			{
				Names.Add(Slot.MaterialSlotName.ToString());
			}
		}
		else
		{
			for (const FSkeletalMaterial& Slot : Target.SkeletalMaterials)
			{
				Names.Add(Slot.MaterialSlotName.ToString());
			}
		}
		return FString::Join(Names, TEXT(", "));
	}

	/** Per-item record. Shape matches the other batch handlers: index, ok,
	 *  status, error, plus the resolved addressing so the caller can diff. */
	TSharedPtr<FJsonObject> MakeMeshMaterialItemResult(
		const FPreparedMeshMaterialAssignment& Item,
		const TCHAR* Status,
		bool bOk,
		bool bDryRun,
		bool bSaved)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("index"), Item.ItemIndex);
		Entry->SetBoolField(TEXT("ok"), bOk);
		Entry->SetStringField(TEXT("status"), Status);
		Entry->SetStringField(TEXT("assetPath"), Item.AssetPath);
		Entry->SetStringField(TEXT("materialPath"), Item.MaterialPath);
		if (Item.SlotIndex != INDEX_NONE)
		{
			Entry->SetNumberField(TEXT("slotIndex"), Item.SlotIndex);
			Entry->SetStringField(TEXT("slotName"), Item.SlotName);
			Entry->SetStringField(TEXT("previousMaterialPath"), Item.PreviousMaterialPath);
		}
		Entry->SetBoolField(TEXT("changed"), Item.bChanged);
		Entry->SetBoolField(TEXT("wouldChange"), bDryRun && Item.bWouldChange);
		Entry->SetBoolField(TEXT("saved"), bSaved);
		if (!Item.Error.IsEmpty()) Entry->SetStringField(TEXT("error"), Item.Error);
		return Entry;
	}
}

// Every submitted assignment comes back with its own ok/status/index/error.
// The default is all-or-nothing: a preflight rejection anywhere aborts before
// a single mesh is touched. continueOnError applies the assignments that did
// pass and reports the rejects alongside them. The apply pass never
// early-returns, so writes that already landed are always described.
TSharedPtr<FJsonValue> FAssetHandlers::SetMeshMaterialsBatch(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* Assignments = nullptr;
	if (!Params->TryGetArrayField(TEXT("assignments"), Assignments) || !Assignments)
	{
		return MCPError(TEXT("Missing 'assignments' array"));
	}
	if (Assignments->Num() == 0)
	{
		return MCPError(TEXT("'assignments' must contain at least one entry"));
	}
	if (Assignments->Num() > MaxMeshMaterialAssignments)
	{
		return MCPError(FString::Printf(
			TEXT("'assignments' exceeds the maximum batch size of %d (received %d)"),
			MaxMeshMaterialAssignments, Assignments->Num()));
	}

	const bool bSave = OptionalBool(Params, TEXT("save"), true);
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const bool bContinueOnError = OptionalBool(Params, TEXT("continueOnError"), false);

	TArray<FPreparedMeshMaterialAssignment> Prepared;
	Prepared.Reserve(Assignments->Num());
	TArray<FPreparedMeshTarget> Targets;
	TMap<FString, int32> TargetByPath;
	TSet<FString> ClaimedSlots;
	int32 PreflightFailedCount = 0;
	FString FirstPreflightError;

	// Preflight: resolve every mesh, material and slot without mutating
	// anything. Rejections are recorded on the item rather than returned, so
	// the caller sees all of them at once instead of only the first.
	for (int32 ItemIndex = 0; ItemIndex < Assignments->Num(); ++ItemIndex)
	{
		FPreparedMeshMaterialAssignment Item;
		Item.ItemIndex = ItemIndex;

		auto Reject = [&Item, &PreflightFailedCount, &FirstPreflightError]
			(const TCHAR* Status, const FString& Message)
		{
			Item.Status = Status;
			Item.Error = Message;
			++PreflightFailedCount;
			if (FirstPreflightError.IsEmpty()) FirstPreflightError = Message;
		};

		const TSharedPtr<FJsonObject>* ItemObject = nullptr;
		if (!(*Assignments)[ItemIndex].IsValid()
			|| !(*Assignments)[ItemIndex]->TryGetObject(ItemObject)
			|| !ItemObject || !(*ItemObject).IsValid())
		{
			Reject(MeshMatStatusInvalid, FString::Printf(TEXT("assignments[%d] must be an object"), ItemIndex));
			Prepared.Add(MoveTemp(Item));
			continue;
		}

		FString AssetPath;
		if (!(*ItemObject)->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			Reject(MeshMatStatusInvalid, FString::Printf(TEXT("assignments[%d].assetPath must be a non-empty string"), ItemIndex));
			Prepared.Add(MoveTemp(Item));
			continue;
		}
		Item.AssetPath = AssetPath;

		if (IsProtectedMeshMaterialPath(AssetPath))
		{
			Reject(MeshMatStatusProtected, FString::Printf(
				TEXT("Refusing to mutate protected mount: %s. Engine, /Script/, /Memory/, /Temp/ are read-only via the bridge."),
				*AssetPath));
			Prepared.Add(MoveTemp(Item));
			continue;
		}

		FString MaterialPath;
		if (!(*ItemObject)->TryGetStringField(TEXT("materialPath"), MaterialPath) || MaterialPath.IsEmpty())
		{
			Reject(MeshMatStatusInvalid, FString::Printf(TEXT("assignments[%d].materialPath must be a non-empty string"), ItemIndex));
			Prepared.Add(MoveTemp(Item));
			continue;
		}
		Item.MaterialPath = MaterialPath;

		double RawSlotIndex = 0;
		Item.bHasSlotIndex = (*ItemObject)->TryGetNumberField(TEXT("slotIndex"), RawSlotIndex);
		if (Item.bHasSlotIndex) Item.RequestedSlotIndex = (int32)RawSlotIndex;
		FString RequestedSlotName;
		Item.bHasSlotName = (*ItemObject)->TryGetStringField(TEXT("slotName"), RequestedSlotName) && !RequestedSlotName.IsEmpty();
		Item.RequestedSlotName = RequestedSlotName;

		UStaticMesh* AsStaticMesh = LoadAssetByPath<UStaticMesh>(AssetPath);
		USkeletalMesh* AsSkeletalMesh = AsStaticMesh ? nullptr : LoadAssetByPath<USkeletalMesh>(AssetPath);
		if (!AsStaticMesh && !AsSkeletalMesh)
		{
			Reject(MeshMatStatusNotFound, FString::Printf(
				TEXT("Preflight failed: no StaticMesh or SkeletalMesh at '%s'"), *AssetPath));
			Prepared.Add(MoveTemp(Item));
			continue;
		}

		UMaterialInterface* Material = LoadAssetByPath<UMaterialInterface>(MaterialPath);
		if (!Material)
		{
			Reject(MeshMatStatusNotFound, FString::Printf(
				TEXT("Preflight failed: could not load material '%s'"), *MaterialPath));
			Prepared.Add(MoveTemp(Item));
			continue;
		}
		Item.Material = Material;

		// Group on the loaded object's path so two spellings of the same asset
		// ("/Game/A/SM_X" and "/Game/A/SM_X.SM_X") land in one target.
		const FString CanonicalPath = AsStaticMesh ? AsStaticMesh->GetPathName() : AsSkeletalMesh->GetPathName();
		if (const int32* Existing = TargetByPath.Find(CanonicalPath))
		{
			Item.TargetIndex = *Existing;
		}
		else
		{
			FPreparedMeshTarget Target;
			Target.StaticMesh = AsStaticMesh;
			Target.SkeletalMesh = AsSkeletalMesh;
			if (AsSkeletalMesh) Target.SkeletalMaterials = AsSkeletalMesh->GetMaterials();
			Item.TargetIndex = Targets.Add(MoveTemp(Target));
			TargetByPath.Add(CanonicalPath, Item.TargetIndex);
		}
		const FPreparedMeshTarget& Target = Targets[Item.TargetIndex];
		const int32 SlotCount = AsStaticMesh ? AsStaticMesh->GetStaticMaterials().Num() : Target.SkeletalMaterials.Num();

		auto SlotNameAt = [&Target, AsStaticMesh](int32 Index) -> FName
		{
			return AsStaticMesh
				? AsStaticMesh->GetStaticMaterials()[Index].MaterialSlotName
				: Target.SkeletalMaterials[Index].MaterialSlotName;
		};

		int32 ResolvedSlot = INDEX_NONE;
		if (Item.bHasSlotName)
		{
			const FName Wanted(*Item.RequestedSlotName);
			for (int32 Slot = 0; Slot < SlotCount; ++Slot)
			{
				if (SlotNameAt(Slot) == Wanted) { ResolvedSlot = Slot; break; }
			}
			if (ResolvedSlot == INDEX_NONE)
			{
				Reject(MeshMatStatusSlotNotFound, FString::Printf(
					TEXT("slotName '%s' not found on '%s' (slots: %s)"),
					*Item.RequestedSlotName, *AssetPath, *DescribeMeshSlots(Target)));
				Prepared.Add(MoveTemp(Item));
				continue;
			}
			// Both forms given and disagreeing is ambiguous. Silently honouring
			// one of them is how the wrong slot gets overwritten.
			if (Item.bHasSlotIndex && Item.RequestedSlotIndex != ResolvedSlot)
			{
				Reject(MeshMatStatusInvalid, FString::Printf(
					TEXT("slotName '%s' resolves to index %d on '%s' but slotIndex %d was also passed. Pass one, or make them agree."),
					*Item.RequestedSlotName, ResolvedSlot, *AssetPath, Item.RequestedSlotIndex));
				Prepared.Add(MoveTemp(Item));
				continue;
			}
		}
		else
		{
			// Matches set_mesh_material: slotIndex defaults to 0.
			ResolvedSlot = Item.bHasSlotIndex ? Item.RequestedSlotIndex : 0;
			if (ResolvedSlot < 0 || ResolvedSlot >= SlotCount)
			{
				Reject(MeshMatStatusSlotNotFound, FString::Printf(
					TEXT("slotIndex %d out of range on '%s' (mesh has %d slots: %s)"),
					ResolvedSlot, *AssetPath, SlotCount, *DescribeMeshSlots(Target)));
				Prepared.Add(MoveTemp(Item));
				continue;
			}
		}

		const FString SlotKey = FString::Printf(TEXT("%s#%d"), *CanonicalPath, ResolvedSlot);
		if (ClaimedSlots.Contains(SlotKey))
		{
			Reject(MeshMatStatusDuplicate, FString::Printf(
				TEXT("Duplicate target in batch: '%s' slot %d is assigned more than once. Keep one assignment per slot so the write order is unambiguous."),
				*AssetPath, ResolvedSlot));
			Prepared.Add(MoveTemp(Item));
			continue;
		}
		ClaimedSlots.Add(SlotKey);

		Item.SlotIndex = ResolvedSlot;
		Item.SlotName = SlotNameAt(ResolvedSlot).ToString();
		UMaterialInterface* PreviousMaterial = AsStaticMesh
			? AsStaticMesh->GetStaticMaterials()[ResolvedSlot].MaterialInterface
			: Target.SkeletalMaterials[ResolvedSlot].MaterialInterface;
		if (PreviousMaterial) Item.PreviousMaterialPath = PreviousMaterial->GetPathName();
		Item.bWouldChange = PreviousMaterial != Material;

		Targets[Item.TargetIndex].AssignmentIndices.Add(Prepared.Num());
		Prepared.Add(MoveTemp(Item));
	}

	// All-or-nothing default: report every verdict, mutate nothing.
	if (PreflightFailedCount > 0 && !bContinueOnError)
	{
		TArray<TSharedPtr<FJsonValue>> RejectResults;
		RejectResults.Reserve(Prepared.Num());
		for (const FPreparedMeshMaterialAssignment& Item : Prepared)
		{
			const bool bPassed = Item.PassedPreflight();
			RejectResults.Add(MakeShared<FJsonValueObject>(MakeMeshMaterialItemResult(
				Item, bPassed ? MeshMatStatusSkipped : *Item.Status, bPassed, bDryRun, false)));
		}

		auto Rejected = MCPSuccess();
		Rejected->SetBoolField(TEXT("success"), false);
		Rejected->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Preflight failed for %d of %d assignments; no meshes were modified. First failure: %s. Pass continueOnError=true to apply the assignments that did pass."),
			PreflightFailedCount, Prepared.Num(), *FirstPreflightError));
		Rejected->SetBoolField(TEXT("dryRun"), bDryRun);
		Rejected->SetBoolField(TEXT("continueOnError"), false);
		Rejected->SetBoolField(TEXT("preflightPassed"), false);
		Rejected->SetNumberField(TEXT("requestedCount"), Prepared.Num());
		Rejected->SetNumberField(TEXT("meshCount"), Targets.Num());
		Rejected->SetNumberField(TEXT("preflightFailedCount"), PreflightFailedCount);
		Rejected->SetNumberField(TEXT("updatedCount"), 0);
		Rejected->SetNumberField(TEXT("unchangedCount"), 0);
		Rejected->SetNumberField(TEXT("failedCount"), PreflightFailedCount);
		Rejected->SetNumberField(TEXT("skippedCount"), Prepared.Num() - PreflightFailedCount);
		Rejected->SetNumberField(TEXT("savedMeshCount"), 0);
		Rejected->SetNumberField(TEXT("saveFailedCount"), 0);
		Rejected->SetArrayField(TEXT("items"), RejectResults);
		return MCPResult(Rejected);
	}

	// Apply, one mesh at a time so each package is written and saved once.
	// A failure here is recorded on its item; the remaining meshes still run.
	int32 SavedMeshCount = 0;
	int32 SaveFailedCount = 0;
	if (!bDryRun)
	{
		for (FPreparedMeshTarget& Target : Targets)
		{
			for (const int32 PreparedIndex : Target.AssignmentIndices)
			{
				FPreparedMeshMaterialAssignment& Item = Prepared[PreparedIndex];
				if (!Item.PassedPreflight()) continue;

				if (Target.StaticMesh)
				{
					if (!Target.StaticMesh->GetStaticMaterials().IsValidIndex(Item.SlotIndex))
					{
						Item.Status = MeshMatStatusFailed;
						Item.Error = FString::Printf(
							TEXT("Apply failed after preflight for '%s' slot %d: the slot no longer exists"),
							*Item.AssetPath, Item.SlotIndex);
						continue;
					}
					// UStaticMesh::SetMaterial owns its own transaction and
					// property-change notification, so no manual PostEditChange.
					Target.StaticMesh->SetMaterial(Item.SlotIndex, Item.Material);
					if (Target.StaticMesh->GetStaticMaterials()[Item.SlotIndex].MaterialInterface != Item.Material)
					{
						Item.Status = MeshMatStatusFailed;
						Item.Error = FString::Printf(
							TEXT("Apply failed after preflight for '%s' slot %d: the mesh did not accept '%s'"),
							*Item.AssetPath, Item.SlotIndex, *Item.MaterialPath);
						continue;
					}
				}
				else
				{
					if (!Target.SkeletalMaterials.IsValidIndex(Item.SlotIndex))
					{
						Item.Status = MeshMatStatusFailed;
						Item.Error = FString::Printf(
							TEXT("Apply failed after preflight for '%s' slot %d: the slot no longer exists"),
							*Item.AssetPath, Item.SlotIndex);
						continue;
					}
					Target.SkeletalMaterials[Item.SlotIndex].MaterialInterface = Item.Material;
				}

				Item.bApplied = true;
				Item.bChanged = Item.bWouldChange;
				Target.bChanged |= Item.bChanged;
			}

			// USkeletalMesh has no per-slot setter; the whole array goes back at
			// once, after every assignment for this mesh has been folded in.
			if (Target.SkeletalMesh && Target.bChanged)
			{
				Target.SkeletalMesh->Modify();
				Target.SkeletalMesh->SetMaterials(Target.SkeletalMaterials);
				Target.SkeletalMesh->PostEditChange();
				Target.SkeletalMesh->MarkPackageDirty();
			}

			if (bSave && Target.bChanged)
			{
				UObject* Mesh = Target.StaticMesh
					? static_cast<UObject*>(Target.StaticMesh)
					: static_cast<UObject*>(Target.SkeletalMesh);
				Target.bSaved = UEditorAssetLibrary::SaveLoadedAsset(Mesh, /*bOnlyIfIsDirty=*/false);
				if (Target.bSaved) ++SavedMeshCount;
				else ++SaveFailedCount;
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> ItemResults;
	TArray<TSharedPtr<FJsonValue>> RollbackAssignments;
	ItemResults.Reserve(Prepared.Num());
	int32 UpdatedCount = 0;
	int32 UnchangedCount = 0;
	int32 FailedCount = 0;
	int32 RollbackSkippedCount = 0;

	for (const FPreparedMeshMaterialAssignment& Item : Prepared)
	{
		const bool bSaved = Item.bApplied && Item.TargetIndex != INDEX_NONE && Targets[Item.TargetIndex].bSaved;
		const TCHAR* Status = MeshMatStatusUnchanged;
		bool bOk = true;
		if (!Item.Error.IsEmpty())
		{
			Status = *Item.Status;
			bOk = false;
			++FailedCount;
		}
		else if (bDryRun)
		{
			Status = MeshMatStatusOk;
		}
		else if (Item.bChanged)
		{
			Status = MeshMatStatusUpdated;
			++UpdatedCount;
		}
		else
		{
			++UnchangedCount;
		}

		ItemResults.Add(MakeShared<FJsonValueObject>(MakeMeshMaterialItemResult(Item, Status, bOk, bDryRun, bSaved)));

		if (!bDryRun && Item.bApplied && Item.bChanged)
		{
			// A slot that was empty before cannot be restored through a handler
			// that requires a materialPath, so it is counted rather than faked.
			if (Item.PreviousMaterialPath.IsEmpty())
			{
				++RollbackSkippedCount;
			}
			else
			{
				TSharedPtr<FJsonObject> Undo = MakeShared<FJsonObject>();
				Undo->SetStringField(TEXT("assetPath"), Item.AssetPath);
				Undo->SetNumberField(TEXT("slotIndex"), Item.SlotIndex);
				Undo->SetStringField(TEXT("materialPath"), Item.PreviousMaterialPath);
				RollbackAssignments.Add(MakeShared<FJsonValueObject>(Undo));
			}
		}
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("success"), FailedCount == 0);
	if (FailedCount > 0)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%d of %d assignments failed; see items[] for the per-item status."),
			FailedCount, Prepared.Num()));
	}
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetBoolField(TEXT("continueOnError"), bContinueOnError);
	Result->SetBoolField(TEXT("preflightPassed"), PreflightFailedCount == 0);
	Result->SetNumberField(TEXT("requestedCount"), Prepared.Num());
	Result->SetNumberField(TEXT("meshCount"), Targets.Num());
	Result->SetNumberField(TEXT("preflightFailedCount"), PreflightFailedCount);
	Result->SetNumberField(TEXT("updatedCount"), UpdatedCount);
	Result->SetNumberField(TEXT("unchangedCount"), UnchangedCount);
	Result->SetNumberField(TEXT("failedCount"), FailedCount);
	Result->SetNumberField(TEXT("savedMeshCount"), SavedMeshCount);
	Result->SetNumberField(TEXT("saveFailedCount"), SaveFailedCount);
	Result->SetArrayField(TEXT("items"), ItemResults);
	if (!bDryRun) Result->SetNumberField(TEXT("rollbackSkippedCount"), RollbackSkippedCount);
	if (!bDryRun && UpdatedCount > 0) MCPSetUpdated(Result);
	if (RollbackAssignments.Num() > 0)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("assignments"), RollbackAssignments);
		Payload->SetBoolField(TEXT("save"), bSave);
		Payload->SetBoolField(TEXT("dryRun"), false);
		Payload->SetBoolField(TEXT("continueOnError"), true);
		MCPSetRollback(Result, TEXT("set_mesh_materials_batch"), Payload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::RecenterPivot(const TSharedPtr<FJsonObject>& Params)
{
	// Support single assetPath or array of assetPaths
	TArray<FString> AssetPaths;
	const TArray<TSharedPtr<FJsonValue>>* PathsArray = nullptr;
	FString SinglePath;

	if (Params->TryGetArrayField(TEXT("assetPaths"), PathsArray))
	{
		for (const auto& Val : *PathsArray)
		{
			FString P;
			if (Val->TryGetString(P) && !P.IsEmpty())
			{
				AssetPaths.Add(P);
			}
		}
	}
	else if (Params->TryGetStringField(TEXT("assetPath"), SinglePath) || Params->TryGetStringField(TEXT("path"), SinglePath))
	{
		if (!SinglePath.IsEmpty())
		{
			AssetPaths.Add(SinglePath);
		}
	}

	if (AssetPaths.Num() == 0)
	{
		return MCPError(TEXT("Missing 'assetPath' (string) or 'assetPaths' (array of strings)"));
	}

	// Load all meshes
	TArray<UStaticMesh*> Meshes;
	for (const FString& Path : AssetPaths)
	{
		UStaticMesh* Mesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *Path));
		if (!Mesh)
		{
			return MCPError(FString::Printf(TEXT("Failed to load static mesh at '%s'"), *Path));
		}
		Meshes.Add(Mesh);
	}

	// Compute the center from the FIRST mesh (reference mesh)
	FMeshDescription* RefDesc = Meshes[0]->GetMeshDescription(0);
	if (!RefDesc)
	{
		return MCPError(TEXT("Failed to get mesh description for reference mesh LOD 0"));
	}

	FVertexArray& RefVerts = RefDesc->Vertices();
	TVertexAttributesRef<FVector3f> RefPositions = RefDesc->GetVertexPositions();

	FVector3f Center = FVector3f::ZeroVector;
	int32 RefVertCount = RefVerts.Num();
	if (RefVertCount == 0)
	{
		return MCPError(TEXT("Reference mesh has no vertices"));
	}

	for (FVertexID VertID : RefVerts.GetElementIDs())
	{
		Center += RefPositions[VertID];
	}
	Center /= (float)RefVertCount;

	// Apply the SAME offset to ALL meshes
	TArray<TSharedPtr<FJsonValue>> ResultArray;
	for (int32 i = 0; i < Meshes.Num(); i++)
	{
		FMeshDescription* MeshDesc = Meshes[i]->GetMeshDescription(0);
		if (!MeshDesc) continue;

		FVertexArray& Verts = MeshDesc->Vertices();
		TVertexAttributesRef<FVector3f> Positions = MeshDesc->GetVertexPositions();

		for (FVertexID VertID : Verts.GetElementIDs())
		{
			Positions[VertID] -= Center;
		}

		Meshes[i]->CommitMeshDescription(0);
		Meshes[i]->Build(false);
		Meshes[i]->PostEditChange();
		Meshes[i]->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(AssetPaths[i], false);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("assetPath"), AssetPaths[i]);
		Entry->SetNumberField(TEXT("vertexCount"), Verts.Num());
		ResultArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetArrayField(TEXT("meshes"), ResultArray);
	Result->SetStringField(TEXT("offsetApplied"), FString::Printf(TEXT("(%.2f, %.2f, %.2f)"), Center.X, Center.Y, Center.Z));
	Result->SetNumberField(TEXT("meshCount"), Meshes.Num());
	// No rollback: destructive/external - vertex offsets applied non-idempotently;
	// re-running shifts the pivot again. Not natural-key idempotent.

	return MCPResult(Result);
}


// ─── #155 asset(set_sk_material_slots) ──────────────────────────────
// Blueprint component property writes to SkeletalMeshComponent.OverrideMaterials
// are silently reverted by UE's ICH pipeline; the reliable path is to mutate
// USkeletalMesh.Materials directly. Accepts either slotName or slotIndex per
// entry. Missing slot names are reported, not skipped silently.
TSharedPtr<FJsonValue> FAssetHandlers::SetSkeletalMeshMaterialSlots(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	const TArray<TSharedPtr<FJsonValue>>* SlotsArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("slots"), SlotsArr))
	{
		return MCPError(TEXT("Missing 'slots' array parameter"));
	}

	USkeletalMesh* Mesh = Cast<USkeletalMesh>(StaticLoadObject(USkeletalMesh::StaticClass(), nullptr, *AssetPath));
	if (!Mesh) return MCPError(FString::Printf(TEXT("SkeletalMesh not found: %s"), *AssetPath));

	Mesh->Modify();
	TArray<FSkeletalMaterial> Materials = Mesh->GetMaterials();

	TArray<TSharedPtr<FJsonValue>> Applied;
	TArray<FString> Errors;

	for (const TSharedPtr<FJsonValue>& SlotVal : *SlotsArr)
	{
		const TSharedPtr<FJsonObject>* SlotObjPtr = nullptr;
		if (!SlotVal.IsValid() || !SlotVal->TryGetObject(SlotObjPtr)) continue;
		const TSharedPtr<FJsonObject>& Slot = *SlotObjPtr;

		FString MaterialPath;
		if (!Slot->TryGetStringField(TEXT("materialPath"), MaterialPath))
		{
			Errors.Add(TEXT("slot entry missing 'materialPath'"));
			continue;
		}

		UMaterialInterface* Material = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MaterialPath));
		if (!Material)
		{
			Errors.Add(FString::Printf(TEXT("material not found: %s"), *MaterialPath));
			continue;
		}

		int32 Index = INDEX_NONE;
		double SlotIdxNum = 0;
		if (Slot->TryGetNumberField(TEXT("slotIndex"), SlotIdxNum))
		{
			Index = (int32)SlotIdxNum;
		}
		else
		{
			FString SlotName;
			if (Slot->TryGetStringField(TEXT("slotName"), SlotName))
			{
				const FName Target(*SlotName);
				for (int32 I = 0; I < Materials.Num(); ++I)
				{
					if (Materials[I].MaterialSlotName == Target)
					{
						Index = I; break;
					}
				}
				if (Index == INDEX_NONE)
				{
					Errors.Add(FString::Printf(TEXT("slotName '%s' not found on %s"), *SlotName, *AssetPath));
					continue;
				}
			}
		}

		if (Index < 0 || Index >= Materials.Num())
		{
			Errors.Add(FString::Printf(TEXT("slotIndex %d out of range (mesh has %d slots)"), Index, Materials.Num()));
			continue;
		}

		Materials[Index].MaterialInterface = Material;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("slotIndex"), Index);
		Entry->SetStringField(TEXT("slotName"), Materials[Index].MaterialSlotName.ToString());
		Entry->SetStringField(TEXT("materialPath"), MaterialPath);
		Applied.Add(MakeShared<FJsonValueObject>(Entry));
	}

	Mesh->SetMaterials(Materials);
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(Mesh, /*bOnlyIfIsDirty=*/false);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("slotCount"), Materials.Num());
	Result->SetArrayField(TEXT("applied"), Applied);
	if (Errors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrArr;
		for (const FString& E : Errors) ErrArr.Add(MakeShared<FJsonValueString>(E));
		Result->SetArrayField(TEXT("errors"), ErrArr);
	}
	return MCPResult(Result);
}

// ─── #155 asset(diagnose_registry) ──────────────────────────────────
// Explains the gap between disk state and the in-memory AssetRegistry.
// Returns on-disk vs registry-including-memory counts so callers can
// recognise pending-kill ghost entries after delete(). reconcile=true
// forces a synchronous rescan (matches the Python workaround).


// ---------------------------------------------------------------------------
// v1.0.0-rc.3 - #193 get_mesh_bounds
// ---------------------------------------------------------------------------
// #431: one-call asset QA - bounds + material slots + skeleton + LOD/vertex
// counts in one shot. Works for both UStaticMesh and USkeletalMesh.
TSharedPtr<FJsonValue> FAssetHandlers::GetMeshInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	UStaticMesh* AsStaticMesh = LoadAssetByPath<UStaticMesh>(AssetPath);
	USkeletalMesh* AsSkeletalMesh = AsStaticMesh ? nullptr : LoadAssetByPath<USkeletalMesh>(AssetPath);
	if (!AsStaticMesh && !AsSkeletalMesh)
	{
		return MCPError(FString::Printf(TEXT("Mesh not found at '%s' (tried StaticMesh and SkeletalMesh)"), *AssetPath));
	}

	FBox BoundingBox(ForceInit);
	FString MeshKind;
	int32 LodCount = 0;
	int32 VertexCount = 0;
	FString SkeletonPath;
	TArray<TSharedPtr<FJsonValue>> SlotsJson;

	if (AsStaticMesh)
	{
		MeshKind = TEXT("StaticMesh");
		BoundingBox = AsStaticMesh->GetBoundingBox();
		LodCount = AsStaticMesh->GetNumLODs();
		if (LodCount > 0 && AsStaticMesh->GetRenderData() && AsStaticMesh->GetRenderData()->LODResources.Num() > 0)
		{
			VertexCount = AsStaticMesh->GetRenderData()->LODResources[0].GetNumVertices();
		}
		const TArray<FStaticMaterial>& Mats = AsStaticMesh->GetStaticMaterials();
		for (int32 i = 0; i < Mats.Num(); ++i)
		{
			const FStaticMaterial& M = Mats[i];
			TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
			SlotObj->SetNumberField(TEXT("index"), i);
			SlotObj->SetStringField(TEXT("slotName"), M.MaterialSlotName.ToString());
			SlotObj->SetStringField(TEXT("materialPath"), M.MaterialInterface ? M.MaterialInterface->GetPathName() : FString());
			SlotObj->SetBoolField(TEXT("isDefaultFallback"), M.MaterialInterface == nullptr);
			SlotsJson.Add(MakeShared<FJsonValueObject>(SlotObj));
		}
	}
	else
	{
		MeshKind = TEXT("SkeletalMesh");
		const FBoxSphereBounds Bounds = AsSkeletalMesh->GetBounds();
		BoundingBox = FBox(Bounds.Origin - Bounds.BoxExtent, Bounds.Origin + Bounds.BoxExtent);
		if (USkeleton* Skel = AsSkeletalMesh->GetSkeleton()) SkeletonPath = Skel->GetPathName();
		if (const FSkeletalMeshRenderData* RD = AsSkeletalMesh->GetResourceForRendering())
		{
			LodCount = RD->LODRenderData.Num();
			if (LodCount > 0) VertexCount = RD->LODRenderData[0].GetNumVertices();
		}
		const TArray<FSkeletalMaterial>& Mats = AsSkeletalMesh->GetMaterials();
		for (int32 i = 0; i < Mats.Num(); ++i)
		{
			const FSkeletalMaterial& M = Mats[i];
			TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
			SlotObj->SetNumberField(TEXT("index"), i);
			SlotObj->SetStringField(TEXT("slotName"), M.MaterialSlotName.ToString());
			SlotObj->SetStringField(TEXT("materialPath"), M.MaterialInterface ? M.MaterialInterface->GetPathName() : FString());
			SlotObj->SetBoolField(TEXT("isDefaultFallback"), M.MaterialInterface == nullptr);
			SlotsJson.Add(MakeShared<FJsonValueObject>(SlotObj));
		}
	}

	const FVector Extent = BoundingBox.GetExtent();
	const FVector Origin = BoundingBox.GetCenter();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("meshKind"), MeshKind);
	Result->SetObjectField(TEXT("boundsOrigin"), MCPVec3ToJsonObject(Origin));
	Result->SetObjectField(TEXT("boundsExtent"), MCPVec3ToJsonObject(Extent));
	Result->SetNumberField(TEXT("heightM"), (Extent.Z * 2.0) / 100.0);
	Result->SetNumberField(TEXT("lodCount"), LodCount);
	Result->SetNumberField(TEXT("vertexCount"), VertexCount);
	if (!SkeletonPath.IsEmpty()) Result->SetStringField(TEXT("skeletonPath"), SkeletonPath);
	Result->SetArrayField(TEXT("materialSlots"), SlotsJson);
	Result->SetNumberField(TEXT("materialCount"), SlotsJson.Num());
	return MCPResult(Result);
}

// #593 list_skeleton_bones - bone names + rest-pose transforms straight from a
// SkeletalMesh or Skeleton asset (no live actor required). Complements the
// actor-based animation(list_bones). Returns local and component-space rest
// transforms so callers can place attachments without spawning anything.
TSharedPtr<FJsonValue> FAssetHandlers::ListSkeletonBones(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	const bool bIncludeTransforms = OptionalBool(Params, TEXT("includeTransforms"), true);

	FString SourceKind;
	const FReferenceSkeleton* RefPtr = nullptr;
	if (USkeletalMesh* SkelMesh = LoadAssetByPath<USkeletalMesh>(AssetPath))
	{
		RefPtr = &SkelMesh->GetRefSkeleton();
		SourceKind = TEXT("SkeletalMesh");
	}
	else if (USkeleton* Skeleton = LoadAssetByPath<USkeleton>(AssetPath))
	{
		RefPtr = &Skeleton->GetReferenceSkeleton();
		SourceKind = TEXT("Skeleton");
	}
	else
	{
		return MCPError(FString::Printf(TEXT("No SkeletalMesh or Skeleton found at '%s'"), *AssetPath));
	}

	const FReferenceSkeleton& Ref = *RefPtr;
	const int32 NumBones = Ref.GetNum();
	const TArray<FTransform>& RefPose = Ref.GetRefBonePose();

	// Component-space accumulation. Bones are stored parent-before-child, so a
	// single forward pass yields valid parent transforms for every child.
	TArray<FTransform> CompSpace;
	if (bIncludeTransforms)
	{
		CompSpace.SetNum(NumBones);
		for (int32 i = 0; i < NumBones; ++i)
		{
			const int32 ParentIdx = Ref.GetParentIndex(i);
			CompSpace[i] = (ParentIdx != INDEX_NONE && RefPose.IsValidIndex(i))
				? RefPose[i] * CompSpace[ParentIdx]
				: (RefPose.IsValidIndex(i) ? RefPose[i] : FTransform::Identity);
		}
	}

	TArray<TSharedPtr<FJsonValue>> Bones;
	for (int32 i = 0; i < NumBones; ++i)
	{
		TSharedPtr<FJsonObject> B = MakeShared<FJsonObject>();
		B->SetStringField(TEXT("name"), Ref.GetBoneName(i).ToString());
		B->SetNumberField(TEXT("index"), i);
		const int32 ParentIdx = Ref.GetParentIndex(i);
		B->SetNumberField(TEXT("parentIndex"), ParentIdx);
		if (ParentIdx != INDEX_NONE) B->SetStringField(TEXT("parentName"), Ref.GetBoneName(ParentIdx).ToString());

		if (bIncludeTransforms && RefPose.IsValidIndex(i))
		{
			const FTransform& Local = RefPose[i];
			TSharedPtr<FJsonObject> LocalObj = MakeShared<FJsonObject>();
			LocalObj->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Local.GetLocation()));
			const FRotator LocalRot = Local.Rotator();
			TSharedPtr<FJsonObject> LocalRotObj = MakeShared<FJsonObject>();
			LocalRotObj->SetNumberField(TEXT("pitch"), LocalRot.Pitch);
			LocalRotObj->SetNumberField(TEXT("yaw"), LocalRot.Yaw);
			LocalRotObj->SetNumberField(TEXT("roll"), LocalRot.Roll);
			LocalObj->SetObjectField(TEXT("rotation"), LocalRotObj);
			LocalObj->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Local.GetScale3D()));
			B->SetObjectField(TEXT("localTransform"), LocalObj);

			B->SetObjectField(TEXT("componentSpaceLocation"), MCPVec3ToJsonObject(CompSpace[i].GetLocation()));
		}
		Bones.Add(MakeShared<FJsonValueObject>(B));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("sourceKind"), SourceKind);
	Result->SetNumberField(TEXT("boneCount"), NumBones);
	Result->SetArrayField(TEXT("bones"), Bones);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::GetMeshBounds(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	// #351: accept SkeletalMesh too - get_mesh_bounds previously errored
	// on SkeletalMesh assets and callers had to fall back to Python
	// (load_asset + get_bounds). Probe StaticMesh first, then SkeletalMesh.
	FBox BoundingBox(ForceInit);
	FString MeshKind;
	if (UStaticMesh* AsStaticMesh = LoadAssetByPath<UStaticMesh>(AssetPath))
	{
		BoundingBox = AsStaticMesh->GetBoundingBox();
		MeshKind = TEXT("StaticMesh");
	}
	else if (USkeletalMesh* AsSkeletalMesh = LoadAssetByPath<USkeletalMesh>(AssetPath))
	{
		const FBoxSphereBounds Bounds = AsSkeletalMesh->GetBounds();
		BoundingBox = FBox(Bounds.Origin - Bounds.BoxExtent, Bounds.Origin + Bounds.BoxExtent);
		MeshKind = TEXT("SkeletalMesh");
	}
	else
	{
		return MCPError(FString::Printf(
			TEXT("Mesh not found at '%s' (tried StaticMesh and SkeletalMesh)"), *AssetPath));
	}

	FVector Min = BoundingBox.Min;
	FVector Max = BoundingBox.Max;
	FVector Extent = BoundingBox.GetExtent();
	FVector Center = BoundingBox.GetCenter();

	TSharedPtr<FJsonObject> MinObj = MakeShared<FJsonObject>();
	MinObj->SetNumberField(TEXT("x"), Min.X);
	MinObj->SetNumberField(TEXT("y"), Min.Y);
	MinObj->SetNumberField(TEXT("z"), Min.Z);

	TSharedPtr<FJsonObject> MaxObj = MakeShared<FJsonObject>();
	MaxObj->SetNumberField(TEXT("x"), Max.X);
	MaxObj->SetNumberField(TEXT("y"), Max.Y);
	MaxObj->SetNumberField(TEXT("z"), Max.Z);

	TSharedPtr<FJsonObject> ExtentObj = MakeShared<FJsonObject>();
	ExtentObj->SetNumberField(TEXT("x"), Extent.X);
	ExtentObj->SetNumberField(TEXT("y"), Extent.Y);
	ExtentObj->SetNumberField(TEXT("z"), Extent.Z);

	TSharedPtr<FJsonObject> CenterObj = MakeShared<FJsonObject>();
	CenterObj->SetNumberField(TEXT("x"), Center.X);
	CenterObj->SetNumberField(TEXT("y"), Center.Y);
	CenterObj->SetNumberField(TEXT("z"), Center.Z);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("meshKind"), MeshKind);
	Result->SetObjectField(TEXT("min"), MinObj);
	Result->SetObjectField(TEXT("max"), MaxObj);
	Result->SetObjectField(TEXT("boxExtent"), ExtentObj);
	Result->SetObjectField(TEXT("boxCenter"), CenterObj);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// #270: surface AssetImportData->SourceData filenames on imported assets so
// callers can validate legacy imports without dropping to Python. Works for
// any UObject that owns an AssetImportData (StaticMesh, SkeletalMesh, Texture,
// Animation*, etc.) - resolved via reflection on the asset class.
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// #270: surface AssetImportData->SourceData filenames on imported assets so
// callers can validate legacy imports without dropping to Python. Works for
// any UObject that owns an AssetImportData (StaticMesh, SkeletalMesh, Texture,
// Animation*, etc.) - resolved via reflection on the asset class.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::ReadImportSources(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* Asset = LoadAssetByPath<UObject>(AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	UAssetImportData* ImportData = nullptr;
	if (UStaticMesh* SM = Cast<UStaticMesh>(Asset))
	{
		ImportData = SM->GetAssetImportData();
	}
	else if (USkeletalMesh* SKM = Cast<USkeletalMesh>(Asset))
	{
		ImportData = SKM->GetAssetImportData();
	}
	else
	{
		// Most other importable assets expose an `AssetImportData` UPROPERTY.
		if (FObjectProperty* Prop = CastField<FObjectProperty>(Asset->GetClass()->FindPropertyByName(TEXT("AssetImportData"))))
		{
			ImportData = Cast<UAssetImportData>(Prop->GetObjectPropertyValue_InContainer(Asset));
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), Asset->GetPathName());
	Result->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetName());

	if (!ImportData)
	{
		Result->SetBoolField(TEXT("hasImportData"), false);
		TArray<TSharedPtr<FJsonValue>> Empty;
		Result->SetArrayField(TEXT("sources"), Empty);
		return MCPResult(Result);
	}

	Result->SetBoolField(TEXT("hasImportData"), true);
	TArray<TSharedPtr<FJsonValue>> Sources;
	for (const FAssetImportInfo::FSourceFile& SF : ImportData->SourceData.SourceFiles)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("relativeFilename"), SF.RelativeFilename);
		Entry->SetStringField(TEXT("timestamp"), SF.Timestamp.ToString());
		Entry->SetStringField(TEXT("fileHash"), LexToString(SF.FileHash));
		Entry->SetStringField(TEXT("displayLabelName"), SF.DisplayLabelName);
		// Resolve absolute path: SourceFilenames returns the resolved paths in
		// the same order as SourceData.SourceFiles. The internal Resolve method
		// is protected, so we lift the public ExtractFilenames helper instead.
		Sources.Add(MakeShared<FJsonValueObject>(Entry));
	}
	TArray<FString> AbsoluteFilenames;
	ImportData->ExtractFilenames(AbsoluteFilenames);
	for (int32 i = 0; i < Sources.Num() && i < AbsoluteFilenames.Num(); ++i)
	{
		Sources[i]->AsObject()->SetStringField(TEXT("absolutePath"), AbsoluteFilenames[i]);
	}
	Result->SetArrayField(TEXT("sources"), Sources);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// v1.0.0-rc.3 - #177 get_mesh_collision
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// v1.0.0-rc.3 - #177 get_mesh_collision
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::GetMeshCollision(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	REQUIRE_ASSET(UStaticMesh, Mesh, AssetPath);

	UBodySetup* BodySetup = Mesh->GetBodySetup();
	if (!BodySetup)
	{
		return MCPError(FString::Printf(TEXT("No BodySetup found on mesh: %s"), *AssetPath));
	}

	// Collision trace flag as string
	FString TraceFlag;
	switch (BodySetup->CollisionTraceFlag)
	{
	case CTF_UseDefault:             TraceFlag = TEXT("CTF_UseDefault"); break;
	case CTF_UseSimpleAndComplex:    TraceFlag = TEXT("CTF_UseSimpleAndComplex"); break;
	case CTF_UseSimpleAsComplex:     TraceFlag = TEXT("CTF_UseSimpleAsComplex"); break;
	case CTF_UseComplexAsSimple:     TraceFlag = TEXT("CTF_UseComplexAsSimple"); break;
	default:                         TraceFlag = TEXT("Unknown"); break;
	}

	const FKAggregateGeom& AggGeom = BodySetup->AggGeom;

	int32 NumConvex  = AggGeom.ConvexElems.Num();
	int32 NumBox     = AggGeom.BoxElems.Num();
	int32 NumSphere  = AggGeom.SphereElems.Num();
	int32 NumSphyl   = AggGeom.SphylElems.Num();

	bool bHasSimple = (NumConvex + NumBox + NumSphere + NumSphyl) > 0;

	// Complex collision is available when the trace flag allows it
	bool bHasComplex = (BodySetup->CollisionTraceFlag == CTF_UseDefault
		|| BodySetup->CollisionTraceFlag == CTF_UseSimpleAndComplex
		|| BodySetup->CollisionTraceFlag == CTF_UseComplexAsSimple);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("collisionTraceFlag"), TraceFlag);
	Result->SetBoolField(TEXT("hasSimpleCollision"), bHasSimple);
	Result->SetBoolField(TEXT("hasComplexCollision"), bHasComplex);
	Result->SetNumberField(TEXT("numConvexElems"), NumConvex);
	Result->SetNumberField(TEXT("numBoxElems"), NumBox);
	Result->SetNumberField(TEXT("numSphereElems"), NumSphere);
	Result->SetNumberField(TEXT("numSphylElems"), NumSphyl);

	// NavCollision info (#167)
	Result->SetBoolField(TEXT("bCanEverAffectNavigation"), Mesh->bHasNavigationData);
	if (Mesh->GetNavCollision())
	{
		Result->SetBoolField(TEXT("hasNavCollision"), true);
		Result->SetBoolField(TEXT("bIsDynamicObstacle"), Mesh->GetNavCollision()->IsDynamicObstacle());
	}
	else
	{
		Result->SetBoolField(TEXT("hasNavCollision"), false);
	}

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// v1.0.0-rc.5 - #167 set_mesh_nav
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// v1.0.0-rc.5 - #167 set_mesh_nav
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::SetMeshNav(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	REQUIRE_ASSET(UStaticMesh, Mesh, AssetPath);

	bool bChanged = false;

	bool bHasNavData = false;
	if (Params->TryGetBoolField(TEXT("bHasNavigationData"), bHasNavData))
	{
		Mesh->bHasNavigationData = bHasNavData;
		bChanged = true;
	}

	bool bClearNavCollision = false;
	if (Params->TryGetBoolField(TEXT("clearNavCollision"), bClearNavCollision) && bClearNavCollision)
	{
		Mesh->SetNavCollision(nullptr);
		bChanged = true;
	}

	if (!bChanged)
	{
		return MCPError(TEXT("No changes requested. Provide bHasNavigationData and/or clearNavCollision."));
	}

	Mesh->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetBoolField(TEXT("bHasNavigationData"), Mesh->bHasNavigationData);
	Result->SetBoolField(TEXT("hasNavCollision"), Mesh->GetNavCollision() != nullptr);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// v1.0.0-rc.3 - #192 move_folder
// ---------------------------------------------------------------------------

// ─── #595 read_cloth_data ───────────────────────────────────────────
// Read Chaos cloth data on a skeletal mesh: per clothing asset, its configs
// (reflected UPROPERTYs), LOD count, and per-LOD point-weight-map summary
// (name, target, vertex count, min/max) including the MaxDistances mask.
TSharedPtr<FJsonValue> FAssetHandlers::ReadClothData(const TSharedPtr<FJsonObject>& Params)
{
	FString MeshPath;
	if (auto Err = RequireStringAlt(Params, TEXT("skeletalMeshPath"), TEXT("assetPath"), MeshPath)) return Err;
	USkeletalMesh* Mesh = LoadAssetByPath<USkeletalMesh>(MeshPath);
	if (!Mesh) return MCPError(FString::Printf(TEXT("SkeletalMesh not found: %s"), *MeshPath));

	TArray<TSharedPtr<FJsonValue>> Assets;
	for (UClothingAssetBase* Base : Mesh->GetMeshClothingAssets())
	{
		UClothingAssetCommon* Cloth = Cast<UClothingAssetCommon>(Base);
		if (!Cloth) continue;

		TSharedPtr<FJsonObject> AObj = MakeShared<FJsonObject>();
		AObj->SetStringField(TEXT("name"), Cloth->GetName());

		// Configs (each a UClothConfigBase subclass) - dump editable UPROPERTYs.
		TSharedPtr<FJsonObject> Configs = MakeShared<FJsonObject>();
		for (const TPair<FName, TObjectPtr<UClothConfigBase>>& CfgPair : Cloth->ClothConfigs)
		{
			UClothConfigBase* Cfg = CfgPair.Value;
			if (!Cfg) continue;
			TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
			for (TFieldIterator<FProperty> It(Cfg->GetClass()); It; ++It)
			{
				FProperty* Prop = *It;
				if (!Prop->HasAnyPropertyFlags(CPF_Edit)) continue;
				FString ValueStr;
				Prop->ExportTextItem_Direct(ValueStr, Prop->ContainerPtrToValuePtr<void>(Cfg), nullptr, Cfg, PPF_None);
				Props->SetStringField(Prop->GetName(), ValueStr);
			}
			Configs->SetObjectField(CfgPair.Key.ToString(), Props);
		}
		AObj->SetObjectField(TEXT("configs"), Configs);

		// LOD data + point weight maps (MaxDistances etc.).
		TArray<TSharedPtr<FJsonValue>> Lods;
		for (int32 LodIdx = 0; LodIdx < Cloth->LodData.Num(); ++LodIdx)
		{
			const FClothLODDataCommon& Lod = Cloth->LodData[LodIdx];
			TSharedPtr<FJsonObject> LObj = MakeShared<FJsonObject>();
			LObj->SetNumberField(TEXT("lod"), LodIdx);
			LObj->SetNumberField(TEXT("numVertices"), Lod.PhysicalMeshData.Vertices.Num());

			TArray<TSharedPtr<FJsonValue>> Masks;
			for (const FPointWeightMap& Map : Lod.PointWeightMaps)
			{
				TSharedPtr<FJsonObject> MObj = MakeShared<FJsonObject>();
				MObj->SetStringField(TEXT("name"), Map.Name.ToString());
				MObj->SetNumberField(TEXT("target"), Map.CurrentTarget);
				MObj->SetNumberField(TEXT("valueCount"), Map.Values.Num());
				float MinV = TNumericLimits<float>::Max(), MaxV = -TNumericLimits<float>::Max();
				for (float V : Map.Values) { MinV = FMath::Min(MinV, V); MaxV = FMath::Max(MaxV, V); }
				if (Map.Values.Num() > 0) { MObj->SetNumberField(TEXT("min"), MinV); MObj->SetNumberField(TEXT("max"), MaxV); }
				Masks.Add(MakeShared<FJsonValueObject>(MObj));
			}
			LObj->SetArrayField(TEXT("pointWeightMaps"), Masks);
			Lods.Add(MakeShared<FJsonValueObject>(LObj));
		}
		AObj->SetArrayField(TEXT("lods"), Lods);
		Assets.Add(MakeShared<FJsonValueObject>(AObj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("skeletalMesh"), Mesh->GetPathName());
	Result->SetNumberField(TEXT("clothingAssetCount"), Assets.Num());
	Result->SetArrayField(TEXT("clothingAssets"), Assets);
	return MCPResult(Result);
}

// ─── #595 set_cloth_config ──────────────────────────────────────────
// Set UPROPERTYs on a clothing asset's config object via reflection.
TSharedPtr<FJsonValue> FAssetHandlers::SetClothConfig(const TSharedPtr<FJsonObject>& Params)
{
	FString MeshPath;
	if (auto Err = RequireStringAlt(Params, TEXT("skeletalMeshPath"), TEXT("assetPath"), MeshPath)) return Err;
	USkeletalMesh* Mesh = LoadAssetByPath<USkeletalMesh>(MeshPath);
	if (!Mesh) return MCPError(FString::Printf(TEXT("SkeletalMesh not found: %s"), *MeshPath));

	const FString ClothName = OptionalString(Params, TEXT("clothingAsset"));
	const FString ConfigType = OptionalString(Params, TEXT("configType"));
	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("properties"), PropsObj) || !PropsObj)
	{
		return MCPError(TEXT("Missing 'properties' object"));
	}

	int32 Applied = 0;
	TArray<TSharedPtr<FJsonValue>> Modified;
	for (UClothingAssetBase* Base : Mesh->GetMeshClothingAssets())
	{
		UClothingAssetCommon* Cloth = Cast<UClothingAssetCommon>(Base);
		if (!Cloth) continue;
		if (!ClothName.IsEmpty() && Cloth->GetName() != ClothName) continue;
		for (const TPair<FName, TObjectPtr<UClothConfigBase>>& CfgPair : Cloth->ClothConfigs)
		{
			UClothConfigBase* Cfg = CfgPair.Value;
			if (!Cfg) continue;
			if (!ConfigType.IsEmpty() && !CfgPair.Key.ToString().Contains(ConfigType) && !Cfg->GetClass()->GetName().Contains(ConfigType)) continue;
			Cfg->Modify();
			for (const auto& Pair : (*PropsObj)->Values)
			{
				FProperty* Prop = Cfg->GetClass()->FindPropertyByName(FName(*Pair.Key));
				if (!Prop) continue;
				FString ValueStr;
				if (Pair.Value->Type == EJson::String) ValueStr = Pair.Value->AsString();
				else if (Pair.Value->Type == EJson::Number) ValueStr = FString::SanitizeFloat(Pair.Value->AsNumber());
				else if (Pair.Value->Type == EJson::Boolean) ValueStr = Pair.Value->AsBool() ? TEXT("true") : TEXT("false");
				else continue;
				Prop->ImportText_Direct(*ValueStr, Prop->ContainerPtrToValuePtr<void>(Cfg), Cfg, PPF_None);
				Modified.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s.%s"), *CfgPair.Key.ToString(), *Pair.Key)));
				++Applied;
			}
			Cfg->PostEditChange();
		}
	}
	if (Applied == 0) return MCPError(TEXT("No matching cloth config / properties applied (check clothingAsset/configType/property names)"));

	SaveAssetPackage(Mesh);
	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("skeletalMesh"), Mesh->GetPathName());
	Result->SetNumberField(TEXT("applied"), Applied);
	Result->SetArrayField(TEXT("modified"), Modified);
	return MCPResult(Result);
}
