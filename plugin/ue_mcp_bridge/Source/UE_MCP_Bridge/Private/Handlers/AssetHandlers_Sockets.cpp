// Split from AssetHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FAssetHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in AssetHandlers.cpp::RegisterHandlers.

#include "AssetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "JsonSerializer.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Animation/Skeleton.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

TSharedPtr<FJsonValue> FAssetHandlers::AddSocket(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString SocketName;
	if (auto Err = RequireString(Params, TEXT("socketName"), SocketName)) return Err;

	FVector RelLoc = FVector::ZeroVector;
	FRotator RelRot = FRotator::ZeroRotator;
	FVector RelScale = FVector::OneVector;

	if (const TSharedPtr<FJsonObject>* LocObj; Params->TryGetObjectField(TEXT("relativeLocation"), LocObj))
	{
		RelLoc.X = (*LocObj)->GetNumberField(TEXT("x"));
		RelLoc.Y = (*LocObj)->GetNumberField(TEXT("y"));
		RelLoc.Z = (*LocObj)->GetNumberField(TEXT("z"));
	}
	if (const TSharedPtr<FJsonObject>* RotObj; Params->TryGetObjectField(TEXT("relativeRotation"), RotObj))
	{
		RelRot.Pitch = (*RotObj)->GetNumberField(TEXT("pitch"));
		RelRot.Yaw   = (*RotObj)->GetNumberField(TEXT("yaw"));
		RelRot.Roll  = (*RotObj)->GetNumberField(TEXT("roll"));
	}
	if (const TSharedPtr<FJsonObject>* ScaleObj; Params->TryGetObjectField(TEXT("relativeScale"), ScaleObj))
	{
		RelScale.X = (*ScaleObj)->GetNumberField(TEXT("x"));
		RelScale.Y = (*ScaleObj)->GetNumberField(TEXT("y"));
		RelScale.Z = (*ScaleObj)->GetNumberField(TEXT("z"));
	}

	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Could not load asset '%s'"), *AssetPath));
	}

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	// Track which transform fields the caller actually supplied so onConflict=update
	// only overwrites what was passed in (matches set_socket_transform semantics).
	const bool bHasLoc   = Params->HasField(TEXT("relativeLocation"));
	const bool bHasRot   = Params->HasField(TEXT("relativeRotation"));
	const bool bHasScale = Params->HasField(TEXT("relativeScale"));

	// Try StaticMesh first
	if (UStaticMesh* SM = Cast<UStaticMesh>(Asset))
	{
		for (UStaticMeshSocket* Existing : SM->Sockets)
		{
			if (Existing && Existing->SocketName == FName(*SocketName))
			{
				if (OnConflict == TEXT("error"))
				{
					return MCPError(FString::Printf(TEXT("Socket '%s' already exists"), *SocketName));
				}
				if (OnConflict == TEXT("update"))
				{
					const FVector PrevLoc = Existing->RelativeLocation;
					const FRotator PrevRot = Existing->RelativeRotation;
					const FVector PrevScale = Existing->RelativeScale;
					SM->Modify();
					Existing->Modify();
					if (bHasLoc)   Existing->RelativeLocation = RelLoc;
					if (bHasRot)   Existing->RelativeRotation = RelRot;
					if (bHasScale) Existing->RelativeScale = RelScale;
					SM->MarkPackageDirty();

					auto UpdatedResult = MCPSuccess();
					MCPSetUpdated(UpdatedResult);
					UpdatedResult->SetStringField(TEXT("socketName"), SocketName);
					UpdatedResult->SetStringField(TEXT("meshType"), TEXT("StaticMesh"));

					// Rollback: restore the previous transform via set_socket_transform.
					TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
					Payload->SetStringField(TEXT("assetPath"), AssetPath);
					Payload->SetStringField(TEXT("socketName"), SocketName);
					TSharedPtr<FJsonObject> PrevLocObj = MakeShared<FJsonObject>();
					PrevLocObj->SetNumberField(TEXT("x"), PrevLoc.X);
					PrevLocObj->SetNumberField(TEXT("y"), PrevLoc.Y);
					PrevLocObj->SetNumberField(TEXT("z"), PrevLoc.Z);
					Payload->SetObjectField(TEXT("relativeLocation"), PrevLocObj);
					TSharedPtr<FJsonObject> PrevRotObj = MakeShared<FJsonObject>();
					PrevRotObj->SetNumberField(TEXT("pitch"), PrevRot.Pitch);
					PrevRotObj->SetNumberField(TEXT("yaw"), PrevRot.Yaw);
					PrevRotObj->SetNumberField(TEXT("roll"), PrevRot.Roll);
					Payload->SetObjectField(TEXT("relativeRotation"), PrevRotObj);
					TSharedPtr<FJsonObject> PrevScaleObj = MakeShared<FJsonObject>();
					PrevScaleObj->SetNumberField(TEXT("x"), PrevScale.X);
					PrevScaleObj->SetNumberField(TEXT("y"), PrevScale.Y);
					PrevScaleObj->SetNumberField(TEXT("z"), PrevScale.Z);
					Payload->SetObjectField(TEXT("relativeScale"), PrevScaleObj);
					MCPSetRollback(UpdatedResult, TEXT("set_socket_transform"), Payload);
					return MCPResult(UpdatedResult);
				}
				auto ExistingResult = MCPSuccess();
				MCPSetExisted(ExistingResult);
				ExistingResult->SetStringField(TEXT("socketName"), SocketName);
				ExistingResult->SetStringField(TEXT("meshType"), TEXT("StaticMesh"));
				return MCPResult(ExistingResult);
			}
		}

		UStaticMeshSocket* NewSocket = NewObject<UStaticMeshSocket>(SM);
		NewSocket->SocketName = FName(*SocketName);
		NewSocket->RelativeLocation = RelLoc;
		NewSocket->RelativeRotation = RelRot;
		NewSocket->RelativeScale = RelScale;
		SM->Modify();
		SM->Sockets.Add(NewSocket);
		SM->MarkPackageDirty();

		auto Result = MCPSuccess();
		MCPSetCreated(Result);
		Result->SetStringField(TEXT("socketName"), SocketName);
		Result->SetStringField(TEXT("meshType"), TEXT("StaticMesh"));
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("socketName"), SocketName);
		MCPSetRollback(Result, TEXT("remove_socket"), Payload);
		return MCPResult(Result);
	}

	// Try SkeletalMesh
	if (USkeletalMesh* SKM = Cast<USkeletalMesh>(Asset))
	{
		FString BoneName = OptionalString(Params, TEXT("boneName"), TEXT("root"));

		for (USkeletalMeshSocket* Existing : SKM->GetMeshOnlySocketList())
		{
			if (Existing && Existing->SocketName == FName(*SocketName))
			{
				if (OnConflict == TEXT("error"))
				{
					return MCPError(FString::Printf(TEXT("Socket '%s' already exists"), *SocketName));
				}
				if (OnConflict == TEXT("update"))
				{
					const FVector PrevLoc = Existing->RelativeLocation;
					const FRotator PrevRot = Existing->RelativeRotation;
					const FVector PrevScale = Existing->RelativeScale;
					SKM->Modify();
					Existing->Modify();
					if (bHasLoc)   Existing->RelativeLocation = RelLoc;
					if (bHasRot)   Existing->RelativeRotation = RelRot;
					if (bHasScale) Existing->RelativeScale = RelScale;
					SKM->MarkPackageDirty();
					SKM->PostEditChange();

					auto UpdatedResult = MCPSuccess();
					MCPSetUpdated(UpdatedResult);
					UpdatedResult->SetStringField(TEXT("socketName"), SocketName);
					UpdatedResult->SetStringField(TEXT("meshType"), TEXT("SkeletalMesh"));

					TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
					Payload->SetStringField(TEXT("assetPath"), AssetPath);
					Payload->SetStringField(TEXT("socketName"), SocketName);
					TSharedPtr<FJsonObject> PrevLocObj = MakeShared<FJsonObject>();
					PrevLocObj->SetNumberField(TEXT("x"), PrevLoc.X);
					PrevLocObj->SetNumberField(TEXT("y"), PrevLoc.Y);
					PrevLocObj->SetNumberField(TEXT("z"), PrevLoc.Z);
					Payload->SetObjectField(TEXT("relativeLocation"), PrevLocObj);
					TSharedPtr<FJsonObject> PrevRotObj = MakeShared<FJsonObject>();
					PrevRotObj->SetNumberField(TEXT("pitch"), PrevRot.Pitch);
					PrevRotObj->SetNumberField(TEXT("yaw"), PrevRot.Yaw);
					PrevRotObj->SetNumberField(TEXT("roll"), PrevRot.Roll);
					Payload->SetObjectField(TEXT("relativeRotation"), PrevRotObj);
					TSharedPtr<FJsonObject> PrevScaleObj = MakeShared<FJsonObject>();
					PrevScaleObj->SetNumberField(TEXT("x"), PrevScale.X);
					PrevScaleObj->SetNumberField(TEXT("y"), PrevScale.Y);
					PrevScaleObj->SetNumberField(TEXT("z"), PrevScale.Z);
					Payload->SetObjectField(TEXT("relativeScale"), PrevScaleObj);
					MCPSetRollback(UpdatedResult, TEXT("set_socket_transform"), Payload);
					return MCPResult(UpdatedResult);
				}
				auto ExistingResult = MCPSuccess();
				MCPSetExisted(ExistingResult);
				ExistingResult->SetStringField(TEXT("socketName"), SocketName);
				ExistingResult->SetStringField(TEXT("meshType"), TEXT("SkeletalMesh"));
				return MCPResult(ExistingResult);
			}
		}

		USkeletalMeshSocket* NewSocket = NewObject<USkeletalMeshSocket>(SKM);
		NewSocket->SocketName = FName(*SocketName);
		NewSocket->BoneName = FName(*BoneName);
		NewSocket->RelativeLocation = RelLoc;
		NewSocket->RelativeRotation = RelRot;
		NewSocket->RelativeScale = RelScale;
		SKM->GetMeshOnlySocketList().Add(NewSocket);
		SKM->MarkPackageDirty();
		SKM->PostEditChange();

		auto Result = MCPSuccess();
		MCPSetCreated(Result);
		Result->SetStringField(TEXT("socketName"), SocketName);
		Result->SetStringField(TEXT("boneName"), BoneName);
		Result->SetStringField(TEXT("meshType"), TEXT("SkeletalMesh"));
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("socketName"), SocketName);
		MCPSetRollback(Result, TEXT("remove_socket"), Payload);
		return MCPResult(Result);
	}

	// #465: Skeleton assets own their own socket array. Mesh-level sockets
	// live on USkeletalMesh; rig-level sockets live on USkeleton. Both flow
	// through asset.add_socket so callers don't have to know which one a
	// given socket belongs to.
	if (USkeleton* Skel = Cast<USkeleton>(Asset))
	{
		const FString BoneName = OptionalString(Params, TEXT("boneName"), TEXT("root"));

		for (USkeletalMeshSocket* Existing : Skel->Sockets)
		{
			if (Existing && Existing->SocketName == FName(*SocketName))
			{
				if (OnConflict == TEXT("error"))
				{
					return MCPError(FString::Printf(TEXT("Socket '%s' already exists on Skeleton"), *SocketName));
				}
				if (OnConflict == TEXT("update"))
				{
					Skel->Modify();
					Existing->Modify();
					if (bHasLoc)   Existing->RelativeLocation = RelLoc;
					if (bHasRot)   Existing->RelativeRotation = RelRot;
					if (bHasScale) Existing->RelativeScale = RelScale;
					Skel->MarkPackageDirty();
					auto UpdatedResult = MCPSuccess();
					MCPSetUpdated(UpdatedResult);
					UpdatedResult->SetStringField(TEXT("socketName"), SocketName);
					UpdatedResult->SetStringField(TEXT("meshType"), TEXT("Skeleton"));
					return MCPResult(UpdatedResult);
				}
				auto ExistingResult = MCPSuccess();
				MCPSetExisted(ExistingResult);
				ExistingResult->SetStringField(TEXT("socketName"), SocketName);
				ExistingResult->SetStringField(TEXT("meshType"), TEXT("Skeleton"));
				return MCPResult(ExistingResult);
			}
		}

		USkeletalMeshSocket* NewSocket = NewObject<USkeletalMeshSocket>(Skel);
		NewSocket->SocketName = FName(*SocketName);
		NewSocket->BoneName = FName(*BoneName);
		NewSocket->RelativeLocation = RelLoc;
		NewSocket->RelativeRotation = RelRot;
		NewSocket->RelativeScale = RelScale;
		Skel->Modify();
		Skel->Sockets.Add(NewSocket);
		Skel->MarkPackageDirty();

		auto Result = MCPSuccess();
		MCPSetCreated(Result);
		Result->SetStringField(TEXT("socketName"), SocketName);
		Result->SetStringField(TEXT("boneName"), BoneName);
		Result->SetStringField(TEXT("meshType"), TEXT("Skeleton"));
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("socketName"), SocketName);
		MCPSetRollback(Result, TEXT("remove_socket"), Payload);
		return MCPResult(Result);
	}

	return MCPError(FString::Printf(TEXT("'%s' is not a StaticMesh, SkeletalMesh, or Skeleton"), *AssetPath));
}


TSharedPtr<FJsonValue> FAssetHandlers::RemoveSocket(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString SocketName;
	if (auto Err = RequireString(Params, TEXT("socketName"), SocketName)) return Err;

	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Could not load asset '%s'"), *AssetPath));
	}

	if (UStaticMesh* SM = Cast<UStaticMesh>(Asset))
	{
		for (int32 i = 0; i < SM->Sockets.Num(); ++i)
		{
			if (SM->Sockets[i] && SM->Sockets[i]->SocketName == FName(*SocketName))
			{
				SM->Modify();
				SM->Sockets.RemoveAt(i);
				SM->MarkPackageDirty();

				auto Result = MCPSuccess();
				Result->SetStringField(TEXT("removed"), SocketName);
				Result->SetBoolField(TEXT("deleted"), true);
				return MCPResult(Result);
			}
		}
		// Idempotent: socket already absent.
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("socketName"), SocketName);
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Noop);
	}

	if (USkeletalMesh* SKM = Cast<USkeletalMesh>(Asset))
	{
		auto& Sockets = SKM->GetMeshOnlySocketList();
		for (int32 i = 0; i < Sockets.Num(); ++i)
		{
			if (Sockets[i] && Sockets[i]->SocketName == FName(*SocketName))
			{
				Sockets.RemoveAt(i);
				SKM->MarkPackageDirty();
				SKM->PostEditChange();

				auto Result = MCPSuccess();
				Result->SetStringField(TEXT("removed"), SocketName);
				Result->SetBoolField(TEXT("deleted"), true);
				return MCPResult(Result);
			}
		}
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("socketName"), SocketName);
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Noop);
	}

	// #465: Skeleton-level socket removal.
	if (USkeleton* Skel = Cast<USkeleton>(Asset))
	{
		for (int32 i = 0; i < Skel->Sockets.Num(); ++i)
		{
			if (Skel->Sockets[i] && Skel->Sockets[i]->SocketName == FName(*SocketName))
			{
				Skel->Modify();
				Skel->Sockets.RemoveAt(i);
				Skel->MarkPackageDirty();
				auto Result = MCPSuccess();
				Result->SetStringField(TEXT("removed"), SocketName);
				Result->SetStringField(TEXT("meshType"), TEXT("Skeleton"));
				Result->SetBoolField(TEXT("deleted"), true);
				return MCPResult(Result);
			}
		}
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("socketName"), SocketName);
		Noop->SetStringField(TEXT("meshType"), TEXT("Skeleton"));
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Noop);
	}

	return MCPError(FString::Printf(TEXT("'%s' is not a StaticMesh, SkeletalMesh, or Skeleton"), *AssetPath));
}


TSharedPtr<FJsonValue> FAssetHandlers::ListSockets(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Could not load asset '%s'"), *AssetPath));
	}

	auto Result = MCPSuccess();
	TArray<TSharedPtr<FJsonValue>> SocketArray;

	if (UStaticMesh* SM = Cast<UStaticMesh>(Asset))
	{
		for (UStaticMeshSocket* Socket : SM->Sockets)
		{
			if (!Socket) continue;
			TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetStringField(TEXT("name"), Socket->SocketName.ToString());
			S->SetStringField(TEXT("tag"), Socket->Tag);

			TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
			Loc->SetNumberField(TEXT("x"), Socket->RelativeLocation.X);
			Loc->SetNumberField(TEXT("y"), Socket->RelativeLocation.Y);
			Loc->SetNumberField(TEXT("z"), Socket->RelativeLocation.Z);
			S->SetObjectField(TEXT("relativeLocation"), Loc);

			TSharedPtr<FJsonObject> Rot = MakeShared<FJsonObject>();
			Rot->SetNumberField(TEXT("pitch"), Socket->RelativeRotation.Pitch);
			Rot->SetNumberField(TEXT("yaw"), Socket->RelativeRotation.Yaw);
			Rot->SetNumberField(TEXT("roll"), Socket->RelativeRotation.Roll);
			S->SetObjectField(TEXT("relativeRotation"), Rot);

			TSharedPtr<FJsonObject> Scale = MakeShared<FJsonObject>();
			Scale->SetNumberField(TEXT("x"), Socket->RelativeScale.X);
			Scale->SetNumberField(TEXT("y"), Socket->RelativeScale.Y);
			Scale->SetNumberField(TEXT("z"), Socket->RelativeScale.Z);
			S->SetObjectField(TEXT("relativeScale"), Scale);

			SocketArray.Add(MakeShared<FJsonValueObject>(S));
		}
		Result->SetStringField(TEXT("meshType"), TEXT("StaticMesh"));
	}
	else if (USkeletalMesh* SKM = Cast<USkeletalMesh>(Asset))
	{
		for (USkeletalMeshSocket* Socket : SKM->GetMeshOnlySocketList())
		{
			if (!Socket) continue;
			TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetStringField(TEXT("name"), Socket->SocketName.ToString());
			S->SetStringField(TEXT("boneName"), Socket->BoneName.ToString());

			TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
			Loc->SetNumberField(TEXT("x"), Socket->RelativeLocation.X);
			Loc->SetNumberField(TEXT("y"), Socket->RelativeLocation.Y);
			Loc->SetNumberField(TEXT("z"), Socket->RelativeLocation.Z);
			S->SetObjectField(TEXT("relativeLocation"), Loc);

			TSharedPtr<FJsonObject> Rot = MakeShared<FJsonObject>();
			Rot->SetNumberField(TEXT("pitch"), Socket->RelativeRotation.Pitch);
			Rot->SetNumberField(TEXT("yaw"), Socket->RelativeRotation.Yaw);
			Rot->SetNumberField(TEXT("roll"), Socket->RelativeRotation.Roll);
			S->SetObjectField(TEXT("relativeRotation"), Rot);

			TSharedPtr<FJsonObject> Scale = MakeShared<FJsonObject>();
			Scale->SetNumberField(TEXT("x"), Socket->RelativeScale.X);
			Scale->SetNumberField(TEXT("y"), Socket->RelativeScale.Y);
			Scale->SetNumberField(TEXT("z"), Socket->RelativeScale.Z);
			S->SetObjectField(TEXT("relativeScale"), Scale);

			SocketArray.Add(MakeShared<FJsonValueObject>(S));
		}
		Result->SetStringField(TEXT("meshType"), TEXT("SkeletalMesh"));
	}
	else if (USkeleton* Skel = Cast<USkeleton>(Asset))
	{
		// #465: rig-level sockets live here. List them with the same shape as mesh sockets.
		for (USkeletalMeshSocket* Socket : Skel->Sockets)
		{
			if (!Socket) continue;
			TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetStringField(TEXT("name"), Socket->SocketName.ToString());
			S->SetStringField(TEXT("boneName"), Socket->BoneName.ToString());
			TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
			Loc->SetNumberField(TEXT("x"), Socket->RelativeLocation.X);
			Loc->SetNumberField(TEXT("y"), Socket->RelativeLocation.Y);
			Loc->SetNumberField(TEXT("z"), Socket->RelativeLocation.Z);
			S->SetObjectField(TEXT("relativeLocation"), Loc);
			TSharedPtr<FJsonObject> Rot = MakeShared<FJsonObject>();
			Rot->SetNumberField(TEXT("pitch"), Socket->RelativeRotation.Pitch);
			Rot->SetNumberField(TEXT("yaw"), Socket->RelativeRotation.Yaw);
			Rot->SetNumberField(TEXT("roll"), Socket->RelativeRotation.Roll);
			S->SetObjectField(TEXT("relativeRotation"), Rot);
			TSharedPtr<FJsonObject> Scale = MakeShared<FJsonObject>();
			Scale->SetNumberField(TEXT("x"), Socket->RelativeScale.X);
			Scale->SetNumberField(TEXT("y"), Socket->RelativeScale.Y);
			Scale->SetNumberField(TEXT("z"), Socket->RelativeScale.Z);
			S->SetObjectField(TEXT("relativeScale"), Scale);
			SocketArray.Add(MakeShared<FJsonValueObject>(S));
		}
		Result->SetStringField(TEXT("meshType"), TEXT("Skeleton"));
	}
	else
	{
		return MCPError(FString::Printf(TEXT("'%s' is not a StaticMesh, SkeletalMesh, or Skeleton"), *AssetPath));
	}

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("socketCount"), SocketArray.Num());
	Result->SetArrayField(TEXT("sockets"), SocketArray);

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_socket_transform -- Update an existing socket's relative transform on a
// StaticMesh or SkeletalMesh. FBX-imported SOCKET_* empties commonly land with
// relative_scale=(100,100,100) and need to be corrected without recreating
// the socket (#412).
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// set_socket_transform -- Update an existing socket's relative transform on a
// StaticMesh or SkeletalMesh. FBX-imported SOCKET_* empties commonly land with
// relative_scale=(100,100,100) and need to be corrected without recreating
// the socket (#412).
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FAssetHandlers::SetSocketTransform(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString SocketName;
	if (auto Err = RequireString(Params, TEXT("socketName"), SocketName)) return Err;

	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Could not load asset '%s'"), *AssetPath));
	}

	// Optional transform components — only fields that are passed are written.
	const bool bHasLoc = Params->HasField(TEXT("relativeLocation"));
	const bool bHasRot = Params->HasField(TEXT("relativeRotation"));
	const bool bHasScale = Params->HasField(TEXT("relativeScale"));
	if (!bHasLoc && !bHasRot && !bHasScale)
	{
		return MCPError(TEXT("Pass at least one of relativeLocation, relativeRotation, relativeScale"));
	}

	FVector NewLoc = FVector::ZeroVector;
	FRotator NewRot = FRotator::ZeroRotator;
	FVector NewScale = FVector::OneVector;
	if (const TSharedPtr<FJsonObject>* LocObj; Params->TryGetObjectField(TEXT("relativeLocation"), LocObj))
	{
		NewLoc.X = (*LocObj)->GetNumberField(TEXT("x"));
		NewLoc.Y = (*LocObj)->GetNumberField(TEXT("y"));
		NewLoc.Z = (*LocObj)->GetNumberField(TEXT("z"));
	}
	if (const TSharedPtr<FJsonObject>* RotObj; Params->TryGetObjectField(TEXT("relativeRotation"), RotObj))
	{
		NewRot.Pitch = (*RotObj)->GetNumberField(TEXT("pitch"));
		NewRot.Yaw   = (*RotObj)->GetNumberField(TEXT("yaw"));
		NewRot.Roll  = (*RotObj)->GetNumberField(TEXT("roll"));
	}
	if (const TSharedPtr<FJsonObject>* ScaleObj; Params->TryGetObjectField(TEXT("relativeScale"), ScaleObj))
	{
		NewScale.X = (*ScaleObj)->GetNumberField(TEXT("x"));
		NewScale.Y = (*ScaleObj)->GetNumberField(TEXT("y"));
		NewScale.Z = (*ScaleObj)->GetNumberField(TEXT("z"));
	}

	// Build the rollback payload from the pre-change values so we can restore.
	auto BuildRollback = [&](const FVector& PrevLoc, const FRotator& PrevRot, const FVector& PrevScale)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("socketName"), SocketName);
		TSharedPtr<FJsonObject> PrevLocObj = MakeShared<FJsonObject>();
		PrevLocObj->SetNumberField(TEXT("x"), PrevLoc.X);
		PrevLocObj->SetNumberField(TEXT("y"), PrevLoc.Y);
		PrevLocObj->SetNumberField(TEXT("z"), PrevLoc.Z);
		Payload->SetObjectField(TEXT("relativeLocation"), PrevLocObj);
		TSharedPtr<FJsonObject> PrevRotObj = MakeShared<FJsonObject>();
		PrevRotObj->SetNumberField(TEXT("pitch"), PrevRot.Pitch);
		PrevRotObj->SetNumberField(TEXT("yaw"), PrevRot.Yaw);
		PrevRotObj->SetNumberField(TEXT("roll"), PrevRot.Roll);
		Payload->SetObjectField(TEXT("relativeRotation"), PrevRotObj);
		TSharedPtr<FJsonObject> PrevScaleObj = MakeShared<FJsonObject>();
		PrevScaleObj->SetNumberField(TEXT("x"), PrevScale.X);
		PrevScaleObj->SetNumberField(TEXT("y"), PrevScale.Y);
		PrevScaleObj->SetNumberField(TEXT("z"), PrevScale.Z);
		Payload->SetObjectField(TEXT("relativeScale"), PrevScaleObj);
		return Payload;
	};

	if (UStaticMesh* SM = Cast<UStaticMesh>(Asset))
	{
		for (UStaticMeshSocket* Existing : SM->Sockets)
		{
			if (Existing && Existing->SocketName == FName(*SocketName))
			{
				const FVector PrevLoc = Existing->RelativeLocation;
				const FRotator PrevRot = Existing->RelativeRotation;
				const FVector PrevScale = Existing->RelativeScale;
				SM->Modify();
				Existing->Modify();
				if (bHasLoc)   Existing->RelativeLocation = NewLoc;
				if (bHasRot)   Existing->RelativeRotation = NewRot;
				if (bHasScale) Existing->RelativeScale = NewScale;
				SM->MarkPackageDirty();

				auto Result = MCPSuccess();
				MCPSetUpdated(Result);
				Result->SetStringField(TEXT("socketName"), SocketName);
				Result->SetStringField(TEXT("meshType"), TEXT("StaticMesh"));
				MCPSetRollback(Result, TEXT("set_socket_transform"), BuildRollback(PrevLoc, PrevRot, PrevScale));
				return MCPResult(Result);
			}
		}
		return MCPError(FString::Printf(TEXT("Socket '%s' not found on StaticMesh '%s'"), *SocketName, *AssetPath));
	}

	if (USkeletalMesh* SKM = Cast<USkeletalMesh>(Asset))
	{
		for (USkeletalMeshSocket* Existing : SKM->GetMeshOnlySocketList())
		{
			if (Existing && Existing->SocketName == FName(*SocketName))
			{
				const FVector PrevLoc = Existing->RelativeLocation;
				const FRotator PrevRot = Existing->RelativeRotation;
				const FVector PrevScale = Existing->RelativeScale;
				SKM->Modify();
				Existing->Modify();
				if (bHasLoc)   Existing->RelativeLocation = NewLoc;
				if (bHasRot)   Existing->RelativeRotation = NewRot;
				if (bHasScale) Existing->RelativeScale = NewScale;
				SKM->MarkPackageDirty();
				SKM->PostEditChange();

				auto Result = MCPSuccess();
				MCPSetUpdated(Result);
				Result->SetStringField(TEXT("socketName"), SocketName);
				Result->SetStringField(TEXT("meshType"), TEXT("SkeletalMesh"));
				MCPSetRollback(Result, TEXT("set_socket_transform"), BuildRollback(PrevLoc, PrevRot, PrevScale));
				return MCPResult(Result);
			}
		}
		return MCPError(FString::Printf(TEXT("Socket '%s' not found on SkeletalMesh '%s'"), *SocketName, *AssetPath));
	}

	return MCPError(FString::Printf(TEXT("'%s' is not a StaticMesh or SkeletalMesh"), *AssetPath));
}

// ---------------------------------------------------------------------------
// reload_package -- Force reload an asset package from disk (#53)
// ---------------------------------------------------------------------------
