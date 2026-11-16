#pragma once

#include "CoreMinimal.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

// Shared eligibility rules for explicit dirty-flag disposal and editor shutdown.
enum class EMCPDiscardPackageKind : uint8
{
	Transient,
	EngineMap,
	Content,
	Code,
	PlayInEditor,
};

inline EMCPDiscardPackageKind MCPClassifyDiscardPackage(const UPackage* Package)
{
	const FString PackageName = Package->GetName();
	if (Package->HasAnyPackageFlags(PKG_CompiledIn) || PackageName.StartsWith(TEXT("/Script/")))
	{
		return EMCPDiscardPackageKind::Code;
	}
	if (Package->HasAnyPackageFlags(PKG_PlayInEditor))
	{
		return EMCPDiscardPackageKind::PlayInEditor;
	}
	if (Package == GetTransientPackage() || Package->HasAnyFlags(RF_Transient))
	{
		return EMCPDiscardPackageKind::Transient;
	}
	// The map itself, its built data and its external actors. Engine content
	// that is not part of a map is still content: an engine material edited
	// by hand is somebody's work.
	if (PackageName.StartsWith(TEXT("/Engine/"))
		&& (Package->ContainsMap() || Package->HasAnyPackageFlags(PKG_ContainsMapData)))
	{
		return EMCPDiscardPackageKind::EngineMap;
	}
	return EMCPDiscardPackageKind::Content;
}

inline const TCHAR* MCPDiscardPackageKindName(EMCPDiscardPackageKind Kind)
{
	switch (Kind)
	{
	case EMCPDiscardPackageKind::Transient: return TEXT("transient");
	case EMCPDiscardPackageKind::EngineMap: return TEXT("engineMap");
	case EMCPDiscardPackageKind::Code: return TEXT("code");
	case EMCPDiscardPackageKind::PlayInEditor: return TEXT("playInEditor");
	default: return TEXT("content");
	}
}
