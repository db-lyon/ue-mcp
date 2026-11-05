#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

/**
 * Checkpoints (#1482): the editor side of the shadow history the server keeps.
 * None of these is a category action of its own; the server's editor(checkpoint)
 * and editor(restore) call them.
 *
 *   checkpoint_arm             { enabled, pendingDir? } start or stop recording saves into pendingDir.
 *                              Answers dirtyPackages, the unsaved work the history does not cover.
 *   checkpoint_prepare_restore { files, checkOnly? } refuse during PIE; close asset editors on the
 *                              files' packages and detach their loaders so the files can be rewritten.
 *   checkpoint_finish_restore  { files: [{file, exists, kind}], reopen } reload or unload the packages,
 *                              rescan them, reload restored config branches, reopen the closed editors.
 */
class FCheckpointHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

	/** Unbind the save hooks, for module shutdown. */
	static void Shutdown();

private:
	static TSharedPtr<FJsonValue> Arm(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PrepareRestore(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> FinishRestore(const TSharedPtr<FJsonObject>& Params);
};
