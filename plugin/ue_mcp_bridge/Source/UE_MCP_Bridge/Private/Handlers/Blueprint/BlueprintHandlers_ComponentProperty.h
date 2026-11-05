#pragma once

// Batch path resolution, value parsing, mesh assignment and rollback
// serialization follow the single-property writer. Keep scratch validation on
// this same path so it exercises the assignment that will reach the template.
#include "HandlerJsonProperty.h"
#include "HandlerPropertyText.h"
#include "JsonSerializer.h"
#include "HandlerUtils.h"
#include "Components/ActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/UnrealType.h"

namespace MCPBlueprintComponentProperty
{
struct FAddress
{
	FProperty* Property = nullptr;
	void* Value = nullptr;
	USkeletalMeshComponent* Mesh = nullptr;
};

inline bool Resolve(UActorComponent* Component, const FString& Name, FAddress& Out, FString& Error)
{
	Out = FAddress();
	if (!Component)
	{
		Error = TEXT("Component template is unavailable");
		return false;
	}
	FString Path = Name;
	if (!Name.Contains(TEXT(".")) &&
		(Name.Equals(TEXT("SkeletalMesh"), ESearchCase::IgnoreCase) ||
		 Name.Equals(TEXT("SkeletalMeshAsset"), ESearchCase::IgnoreCase) ||
		 Name.Equals(TEXT("SkinnedAsset"), ESearchCase::IgnoreCase)))
	{
		Out.Mesh = Cast<USkeletalMeshComponent>(Component);
		if (Out.Mesh) Path = TEXT("SkeletalMeshAsset");
	}
	TArray<FString> Parts;
	Path.ParseIntoArray(Parts, TEXT("."));
	UStruct* Struct = Component->GetClass();
	void* Container = Component;
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		FProperty* Property = Struct->FindPropertyByName(FName(*Parts[Index]));
		if (!Property) break;
		if (Index == Parts.Num() - 1)
		{
			Out.Property = Property;
			Out.Value = Property->ContainerPtrToValuePtr<void>(Container);
			return true;
		}
		FStructProperty* Nested = CastField<FStructProperty>(Property);
		if (!Nested) break;
		Container = Nested->ContainerPtrToValuePtr<void>(Container);
		Struct = Nested->Struct;
	}
	Error = FString::Printf(TEXT("Property '%s' not found on %s (dotted paths descend through structs)"),
		*Name, *Component->GetClass()->GetName());
	return false;
}

inline bool Read(UActorComponent* Component, const FString& Name, FString& Text,
	TSharedPtr<FJsonValue>& Replay, FString& Error)
{
	Text.Reset();
	Replay.Reset();
	FAddress Address;
	if (!Resolve(Component, Name, Address, Error)) return false;
	Address.Property->ExportText_Direct(Text, Address.Value, Address.Value, nullptr, PPF_None);
	// Export text loses struct-keyed maps. Everything else retains the single
	// setter's export-text rollback semantics, including empty strings and None.
	Replay = MCPPropertyText::ContainsMap(Address.Property)
		? FMCPJsonSerializer::SerializeValue(Address.Value, Address.Property)
		: MakeShared<FJsonValueString>(Text);
	if (Address.Mesh)
	{
		Replay = MakeShared<FJsonValueString>(Address.Mesh->GetSkeletalMeshAsset()
			? Address.Mesh->GetSkeletalMeshAsset()->GetPathName() : TEXT("None"));
	}
	return true;
}

inline bool Apply(UActorComponent* Component, const FString& Name,
	const TSharedPtr<FJsonValue>& Value, FString& Error)
{
	FAddress Address;
	if (!Resolve(Component, Name, Address, Error)) return false;
	if (Address.Mesh)
	{
		FString Path;
		if (!Value.IsValid()) { Error = TEXT("Missing value"); return false; }
		if (Value->Type != EJson::Null && !Value->TryGetString(Path))
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			bool bHasPath = false;
			if (Value->TryGetObject(Object) && Object)
			{
				bHasPath = (*Object)->TryGetStringField(TEXT("path"), Path);
				if (Path.IsEmpty()) bHasPath = (*Object)->TryGetStringField(TEXT("assetPath"), Path) || bHasPath;
			}
			if (!bHasPath)
			{
				Error = TEXT("Skeletal mesh value must be null, a path or {path}/{assetPath}");
				return false;
			}
		}
		USkeletalMesh* Mesh = nullptr;
		if (!Path.IsEmpty() && Path != TEXT("None"))
		{
			Mesh = LoadAssetByPath<USkeletalMesh>(Path);
			if (!Mesh) { Error = TEXT("SkeletalMesh not found: ") + Path; return false; }
		}
		Address.Mesh->SetSkeletalMeshAsset(Mesh);
		return true;
	}
	return MCPJsonProperty::SetJsonOnPropertyWithOwner(Address.Property, Address.Value, Value, Component, Error);
}
}
