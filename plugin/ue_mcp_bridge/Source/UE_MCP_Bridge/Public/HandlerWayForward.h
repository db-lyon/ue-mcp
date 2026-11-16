#pragma once

// The way forward a refusal names, written once. A refusal that stops a caller
// says what to call next, or says plainly that nothing overrides it and what to
// do instead. Each sentence here is shared by every handler that refuses for
// that reason, so the advice cannot drift between call sites, and an action or
// parameter that is renamed is renamed here only.

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "HandlerResult.h"

/** Why a protected mount refusal cannot be worked around in place, and the
 *  route that does work: a copy, or an import, under /Game. */
inline FString MCPProtectedMountWayForward()
{
	return TEXT("This cannot be overridden. To change it, duplicate the asset into /Game with ")
		TEXT("asset(duplicate, sourcePath=..., destinationPath=\"/Game/...\") and edit the copy, ")
		TEXT("or import the source file into /Game.");
}

/** The step that ends a play-in-editor session, for a guard that needs the
 *  editor world back. */
inline FString MCPStopPlayInEditorStep()
{
	return TEXT("Stop play with editor(play_in_editor, pieAction=\"stop\") and retry.");
}

/** The step that starts a play-in-editor session, and where to find the
 *  instance ids when one is already running. */
inline FString MCPStartPlayInEditorStep()
{
	return TEXT("Start one with editor(play_in_editor, pieAction=\"start\"); ")
		TEXT("editor(list_pie_instances) lists the running PIE worlds and their pieInstance ids.");
}

/** "No PIE world available", with the way to get one. */
inline FString MCPNoPlayWorldMessage()
{
	return TEXT("No PIE world available. ") + MCPStartPlayInEditorStep();
}

/** A requested world scope resolved to nothing. Names both ways forward: a
 *  PIE session for world=pie, or the editor world. */
inline FString MCPWorldUnavailableMessage(const FString& Scope)
{
	return FString::Printf(
		TEXT("World not available for scope '%s'. world='pie' needs a running PIE session: start one with ")
		TEXT("editor(play_in_editor, pieAction=\"start\"), or pass world='editor' to act on the editor world. ")
		TEXT("editor(list_pie_instances) lists the running PIE worlds and their pieInstance ids."),
		*Scope);
}

inline TSharedPtr<FJsonValue> MCPWorldUnavailableError(const FString& Scope)
{
	return MCPError(MCPWorldUnavailableMessage(Scope));
}

/** The editor has no world at all, which happens while it starts or swaps maps. */
inline FString MCPEditorWorldUnavailableMessage()
{
	return TEXT("Editor world not available: the editor has no world open, which happens while it is still ")
		TEXT("starting or loading a map. Check editor(get_engine_state) and retry once it is ready, or open a map with level(load).");
}

/** The step that clears unsaved packages blocking an action: save them, or
 *  drop them explicitly. */
inline FString MCPSaveOrDiscardStep()
{
	return TEXT("Save them with level(save) or editor(save_dirty), or drop them with ")
		TEXT("editor(discard_dirty, packages=[...], allowContent=true), then retry.");
}

/** The two calls that make a plugin's classes available: enable it in the
 *  .uproject, then restart, since plugins load only at startup. */
inline FString MCPEnablePluginStep(const FString& PluginName)
{
	return FString::Printf(
		TEXT("Enable it with project(enable_plugin, pluginName=\"%s\"), then restart the editor with editor(restart_editor)."),
		*PluginName);
}

/** "<What> is not available because the <Plugin> plugin is not loaded", then
 *  the enable-and-restart step. */
inline FString MCPPluginUnavailableMessage(const FString& What, const FString& PluginName)
{
	return FString::Printf(TEXT("%s is not available: the %s plugin is not loaded. "), *What, *PluginName)
		+ MCPEnablePluginStep(PluginName);
}

inline TSharedPtr<FJsonValue> MCPPluginUnavailableError(const FString& What, const FString& PluginName)
{
	return MCPError(MCPPluginUnavailableMessage(What, PluginName));
}

/** For a bound that is clamped to a fixed maximum: says raising it further
 *  changes nothing, and how to split the work instead. HowToSplit names the
 *  selector or range to divide the work by. */
inline FString MCPHardCeilingNote(const TCHAR* ParamName, int64 Ceiling, const FString& HowToSplit)
{
	return FString::Printf(
		TEXT("%s is capped at %lld, a hard ceiling rather than a default, so raising it past that changes nothing. %s"),
		ParamName, Ceiling, *HowToSplit);
}

/** For a refusal at a caller-set bound: raising it is offered only while it
 *  is below its ceiling, so the advice never loops back to the same refusal.
 *  At the ceiling it says so, and how to split the work instead. */
inline FString MCPRaiseOrSplitNote(const TCHAR* ParamName, int64 Current, int64 Ceiling, const FString& HowToSplit)
{
	if (Current < Ceiling)
	{
		const FString Split = HowToSplit.Left(1).ToLower() + HowToSplit.Mid(1);
		return FString::Printf(TEXT("Raise %s (up to %lld, its hard ceiling), or %s"), ParamName, Ceiling, *Split);
	}
	return MCPHardCeilingNote(ParamName, Ceiling, HowToSplit);
}

/** The sentence a batch-size refusal ends with. */
inline FString MCPSplitIntoCallsNote(int64 MaxPerCall)
{
	return FString::Printf(TEXT("Split the work into several calls of %lld or fewer."), MaxPerCall);
}

/** A batch-size refusal message with the split sentence appended. */
inline FString MCPWithSplitNote(const FString& Message, int64 MaxPerCall)
{
	FString Out = Message;
	Out.TrimEndInline();
	if (!Out.EndsWith(TEXT(".")) && !Out.EndsWith(TEXT("?")) && !Out.EndsWith(TEXT("!"))) Out += TEXT(".");
	return Out + TEXT(" ") + MCPSplitIntoCallsNote(MaxPerCall);
}

inline TSharedPtr<FJsonValue> MCPBatchTooLargeError(const FString& Message, int64 MaxPerCall)
{
	return MCPError(MCPWithSplitNote(Message, MaxPerCall));
}
