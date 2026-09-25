#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "UObject/UnrealType.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/SkeletalMesh.h"
#include "HandlerUtils.h"

// A skinned mesh component's mesh is written through SetSkinnedAssetAndUpdate,
// never by a raw property write. The raw write leaves the component-space
// transform buffers sized for the old skeleton, and the next pose evaluation
// asserts and takes the editor down (#1099).
namespace MCPSkinnedAsset
{
	// True when Prop is the mesh pointer of USkinnedMeshComponent or a subclass.
	inline bool IsMeshProperty(const FProperty* Prop)
	{
		if (!Prop) return false;
		const UClass* OwnerClass = Prop->GetOwnerClass();
		if (!OwnerClass || !OwnerClass->IsChildOf(USkinnedMeshComponent::StaticClass())) return false;
		const FName Name = Prop->GetFName();
		return Name == FName(TEXT("SkinnedAsset"))
			|| Name == FName(TEXT("SkeletalMeshAsset"))
			|| Name == FName(TEXT("SkeletalMesh"));
	}

	inline FString CurrentPath(const USkinnedMeshComponent* Comp)
	{
		const USkinnedAsset* Mesh = Comp ? Comp->GetSkinnedAsset() : nullptr;
		return Mesh ? Mesh->GetPathName() : FString();
	}

	// Accepts a path string, {path} or {assetPath}. null, "" and "None" clear.
	inline bool ReadMeshPath(const TSharedPtr<FJsonValue>& Value, FString& OutPath, FString& OutError)
	{
		OutPath.Reset();
		if (!Value.IsValid() || Value->Type == EJson::Null) return true;
		if (Value->Type == EJson::String)
		{
			OutPath = Value->AsString().TrimStartAndEnd();
		}
		else
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Value->TryGetObject(Obj) || !Obj || !Obj->IsValid())
			{
				OutError = TEXT("The mesh value must be an asset path string, {path}, or null to clear");
				return false;
			}
			if (!(*Obj)->TryGetStringField(TEXT("path"), OutPath))
			{
				(*Obj)->TryGetStringField(TEXT("assetPath"), OutPath);
			}
			OutPath.TrimStartAndEndInline();
		}
		if (OutPath.Equals(TEXT("None"), ESearchCase::IgnoreCase)) OutPath.Reset();
		return true;
	}

	// Loads MeshPath (empty clears) and assigns it through the engine setter.
	inline bool Assign(USkinnedMeshComponent* Comp, const FString& MeshPath, FString& OutError)
	{
		if (!Comp) { OutError = TEXT("No skinned mesh component"); return false; }
		USkinnedAsset* Mesh = nullptr;
		if (!MeshPath.IsEmpty())
		{
			UObject* Loaded = MCPLoadAssetObject(MeshPath);
			Mesh = Cast<USkinnedAsset>(Loaded);
			if (!Mesh)
			{
				OutError = Loaded
					? FString::Printf(TEXT("'%s' is a %s, not a skinned mesh"), *MeshPath, *Loaded->GetClass()->GetName())
					: FString::Printf(TEXT("Skeletal mesh not found: %s"), *MeshPath);
				return false;
			}
			if (Comp->IsA<USkeletalMeshComponent>() && !Mesh->IsA<USkeletalMesh>())
			{
				OutError = FString::Printf(TEXT("'%s' is a %s; a SkeletalMeshComponent takes a SkeletalMesh"),
					*MeshPath, *Mesh->GetClass()->GetName());
				return false;
			}
		}
		Comp->Modify();
		Comp->SetSkinnedAssetAndUpdate(Mesh, /*bReinitPose=*/true);
		return true;
	}

	// ReadMeshPath then Assign. OutPrevious is the mesh path before the write.
	inline bool AssignFromJson(USkinnedMeshComponent* Comp, const TSharedPtr<FJsonValue>& Value,
		FString& OutPrevious, FString& OutError)
	{
		FString MeshPath;
		if (!ReadMeshPath(Value, MeshPath, OutError)) return false;
		OutPrevious = CurrentPath(Comp);
		return Assign(Comp, MeshPath, OutError);
	}

	// Fields every mesh write reports, whichever action made it.
	inline void Report(const TSharedPtr<FJsonObject>& Result, const USkinnedMeshComponent* Comp, const FString& Previous)
	{
		const FString Now = CurrentPath(Comp);
		Result->SetStringField(TEXT("previousValue"), Previous.IsEmpty() ? FString(TEXT("None")) : Previous);
		Result->SetStringField(TEXT("value"), Now.IsEmpty() ? FString(TEXT("None")) : Now);
		Result->SetBoolField(TEXT("changed"), Previous != Now);
		Result->SetNumberField(TEXT("materialSlotCount"), Comp ? Comp->GetNumMaterials() : 0);
		Result->SetStringField(TEXT("note"),
			TEXT("Routed through SetSkinnedAssetAndUpdate, which resizes the pose buffers for the new skeleton. A raw write of this property crashes the next pose evaluation (#1099)."));
	}
}
