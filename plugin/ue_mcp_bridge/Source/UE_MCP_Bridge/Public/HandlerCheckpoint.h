#pragma once

#include "CoreMinimal.h"
#include "IAssetTools.h"

/**
 * The bridge half of checkpoints (#1482). While the server has armed it,
 * the first write of a project file after each checkpoint copies the file's
 * current bytes into the pending generation the server named, or records that
 * the file did not exist. Package saves reach it through
 * UPackage::PreSavePackageWithContextEvent and deletes through
 * FEditorDelegates::OnPackageDeleted; a handler that writes or renames a file
 * any other way calls one of these first. Unarmed, each is a no-op.
 */

/** Record a file (absolute or relative to the process) before it is written, renamed or deleted. */
UE_MCP_BRIDGE_API void MCPCheckpointRecordBeforeWrite(const FString& Filename);

/** Record a package's file, found by long package name, before it is written, renamed or deleted. */
UE_MCP_BRIDGE_API void MCPCheckpointRecordPackage(const FString& PackageName);

/** Record the source package of every asset a rename batch is about to move. */
inline void MCPCheckpointRecordRenameSources(const TArray<FAssetRenameData>& Batch)
{
	for (const FAssetRenameData& Data : Batch)
	{
		if (const UObject* Asset = Data.Asset.Get())
		{
			MCPCheckpointRecordPackage(Asset->GetOutermost()->GetName());
		}
	}
}
