#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "UObject/UnrealType.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "HandlerSkinnedAsset.h"
#include "HandlerUtils.h"

// A static mesh component's mesh is written through SetStaticMesh, never by a
// raw property write. The raw write leaves the bounds of the old mesh in place
// and never creates the render state a component with no mesh skipped, so the
// new mesh does not draw (#1548).
namespace MCPStaticMeshAsset
{
	// True when Prop is UStaticMeshComponent's mesh pointer.
	inline bool IsMeshProperty(const FProperty* Prop)
	{
		if (!Prop) return false;
		const UClass* OwnerClass = Prop->GetOwnerClass();
		return OwnerClass && OwnerClass->IsChildOf(UStaticMeshComponent::StaticClass())
			&& Prop->GetFName() == FName(TEXT("StaticMesh"));
	}

	inline FString CurrentPath(const UStaticMeshComponent* Comp)
	{
		const UStaticMesh* Mesh = Comp ? Comp->GetStaticMesh() : nullptr;
		return Mesh ? Mesh->GetPathName() : FString();
	}

	// Loads the mesh named by Value (a path, {path}, or null/""/"None" to clear)
	// and assigns it through the engine setter. OutPrevious is the path before.
	inline bool AssignFromJson(UStaticMeshComponent* Comp, const TSharedPtr<FJsonValue>& Value,
		FString& OutPrevious, FString& OutError)
	{
		if (!Comp) { OutError = TEXT("No static mesh component"); return false; }
		FString MeshPath;
		if (!MCPSkinnedAsset::ReadMeshPath(Value, MeshPath, OutError)) return false;
		UStaticMesh* Mesh = nullptr;
		if (!MeshPath.IsEmpty())
		{
			UObject* Loaded = MCPLoadAssetObject(MeshPath);
			Mesh = Cast<UStaticMesh>(Loaded);
			if (!Mesh)
			{
				OutError = Loaded
					? FString::Printf(TEXT("'%s' is a %s, not a static mesh"), *MeshPath, *Loaded->GetClass()->GetName())
					: FString::Printf(TEXT("Static mesh not found: %s"), *MeshPath);
				return false;
			}
		}
		OutPrevious = CurrentPath(Comp);
		if (Mesh == Comp->GetStaticMesh()) return true;
		Comp->Modify();
		if (!Comp->SetStaticMesh(Mesh))
		{
			OutError = FString::Printf(
				TEXT("SetStaticMesh refused the change on %s. Its mobility is %s and its world has begun play; a Static component's mesh cannot change at runtime."),
				*Comp->GetPathName(), *StaticEnum<EComponentMobility::Type>()->GetNameStringByValue(static_cast<int64>(Comp->GetMobility())));
			return false;
		}
		return true;
	}

	// Fields every mesh write reports, whichever action made it.
	inline void Report(const TSharedPtr<FJsonObject>& Result, const UStaticMeshComponent* Comp, const FString& Previous)
	{
		const FString Now = CurrentPath(Comp);
		Result->SetStringField(TEXT("previousValue"), Previous.IsEmpty() ? FString(TEXT("None")) : Previous);
		Result->SetStringField(TEXT("value"), Now.IsEmpty() ? FString(TEXT("None")) : Now);
		Result->SetBoolField(TEXT("changed"), Previous != Now);
		Result->SetNumberField(TEXT("materialSlotCount"), Comp ? Comp->GetNumMaterials() : 0);
		Result->SetStringField(TEXT("note"),
			TEXT("Routed through SetStaticMesh, which updates the bounds and render state for the new mesh."));
	}
}
