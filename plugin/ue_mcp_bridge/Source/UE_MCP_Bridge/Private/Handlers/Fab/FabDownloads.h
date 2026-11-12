#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IBuildManifest.h"

namespace MCPFab
{
struct FAssetVersion
{
	FString ListingId, Title, AssetId, Namespace, ArtifactId, Platform, EngineVersion;
};

bool ResolveVersion(const TArray<TSharedPtr<FJsonValue>>& Items, const FString& ListingId,
	const FString& EngineVersion, const FString& Platform, FAssetVersion& Out, FString& Error);
bool ValidatePackManifest(const IBuildManifestRef& Manifest, FString& Error);
bool SafePackPath(const FString& Relative);
FString ChunkUrl(const FString& ManifestUrl, const FString& Subdirectory, const FString& ChunkPath);

bool HasActiveDownloads();
void ShutdownDownloads();

#if WITH_DEV_AUTOMATION_TESTS
FString StartFixtureDownload(const IBuildManifestRef& Manifest, const FString& Cloud, const FString& Output, bool bVerifyOnly = false);
#endif
}
