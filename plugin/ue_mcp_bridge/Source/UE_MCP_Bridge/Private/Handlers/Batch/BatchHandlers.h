#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FMCPHandlerRegistry;

/**
 * execute_batch: several registered handlers, run back to back as ONE
 * game-thread job.
 *
 * The bridge already runs handlers one at a time on the game thread, so a
 * single call is never torn. A multi-step edit is several calls, though, and
 * with two sessions on one editor another session's call lands between any
 * two of them: a rename there leaves the next step aimed at an asset that no
 * longer exists under that name. A batch is dispatched like any other method,
 * so it occupies the game thread from its first step to its last and nothing
 * from any connection runs in between.
 *
 * The steps share one editor transaction, so whatever they changed that is
 * transactional is one Undo entry. There is NO rollback: a failing step stops
 * the batch (or not, under onError: continue) and the steps before it stay
 * applied. The result says exactly which ran, with each step's own answer.
 *
 * The Node side (src/tools/editor/batch.ts) prepares every step as a direct
 * call to its action and refuses the batch whole before sending it; this side
 * checks the list again, since a raw bridge client can send it too.
 */
class FBatchHandlers
{
public:
	static void RegisterHandlers(FMCPHandlerRegistry& Registry);

private:
	/**
	 * Runs Params->ops, in order, on the calling thread, through the registry
	 * that dispatched this handler (FMCPHandlerRegistry::Executing), so a batch
	 * sent to a test's registry runs that registry's handlers.
	 */
	static TSharedPtr<FJsonValue> ExecuteBatch(const TSharedPtr<FJsonObject>& Params);
};
