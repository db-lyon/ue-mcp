// Plugin enablement: read, enable and disable entries in the project's
// `.uproject` Plugins array. A translation-unit partition of FProjectHandlers;
// registration stays in ProjectHandlers.cpp::RegisterHandlers.
//
// Why this earns handlers, when the house rule is that asset(set_property) and
// editor(set_property) already reach any UPROPERTY:
//
//   Plugin enablement is not a UPROPERTY and is not in an INI. It lives in the
//   `.uproject` file, in a JSON array of FPluginReferenceDescriptor that is
//   read once at startup. project(set_config) writes INI only, so nothing in
//   the toolset could turn a plugin on. That made every plugin-gated capability
//   permanently unreachable in a project where the plugin ships off - CommonUI
//   being the case that surfaced it.
//
// What a caller must know, and what every result here says out loud:
//
//   The change is a FILE change. Modules load at editor startup and mount
//   points are registered there too, so the plugin's classes, content and
//   settings do not appear until the editor restarts. Nothing in this file
//   pretends otherwise, and nothing here tries to load the modules live: a
//   half-mounted plugin is worse than a plainly-pending one.
//
// The three of them are complete CRUD over one plugin reference: list what
// exists, add or flip an entry, and either flip it back or delete it outright.

#include "ProjectHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerPagination.h"

#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "PluginDescriptor.h"
#include "PluginReferenceDescriptor.h"
#include "ProjectDescriptor.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Paths.h"
#include "Internationalization/Text.h"

// ─────────────────────────────────────────────────────────────────────────────
// File-local helpers.
//
// The module is a unity build, so two .cpp files in one blob share their
// anonymous namespace and a duplicated helper is a redefinition (C2084). Every
// name below is prefixed PPlug_ inside a named namespace and exists nowhere
// else in the tree; nothing was copied out of ProjectHandlers.cpp.
// ─────────────────────────────────────────────────────────────────────────────
namespace MCPProjectPlugins
{

/** The name this plugin ships under. Disabling it severs the bridge. */
static const TCHAR* PPlug_BridgePluginName = TEXT("UE_MCP_Bridge");

static const TCHAR* PPlug_TypeName(EPluginType Type)
{
	switch (Type)
	{
	case EPluginType::Engine:     return TEXT("Engine");
	case EPluginType::Enterprise: return TEXT("Enterprise");
	case EPluginType::Project:    return TEXT("Project");
	case EPluginType::External:   return TEXT("External");
	case EPluginType::Mod:        return TEXT("Mod");
	default:                      return TEXT("Unknown");
	}
}

static const TCHAR* PPlug_LoadedFromName(EPluginLoadedFrom From)
{
	return From == EPluginLoadedFrom::Engine ? TEXT("Engine") : TEXT("Project");
}

/**
 * Resolve a plugin by name, case-insensitively, over everything discovered on
 * disk rather than only over what is enabled. An answer of null means the name
 * is not installed at all, which is a different problem from being off.
 */
static TSharedPtr<IPlugin> PPlug_Find(const FString& Name)
{
	for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetDiscoveredPlugins())
	{
		if (Plugin->GetName().Equals(Name, ESearchCase::IgnoreCase)) return Plugin;
	}
	return nullptr;
}

/** Installed names that look like what was asked for, so a typo is one call. */
static FString PPlug_NearestNames(const FString& Name, int32 Max = 12)
{
	TArray<FString> Hits;
	for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetDiscoveredPlugins())
	{
		const FString& Candidate = Plugin->GetName();
		if (Candidate.Contains(Name, ESearchCase::IgnoreCase)
			|| Name.Contains(Candidate, ESearchCase::IgnoreCase)
			|| Plugin->GetFriendlyName().Contains(Name, ESearchCase::IgnoreCase))
		{
			Hits.Add(Candidate);
			if (Hits.Num() >= Max) break;
		}
	}
	Hits.Sort();
	return Hits.Num() ? FString::Join(Hits, TEXT(", ")) : FString(TEXT("(no similar names installed)"));
}

static TSharedPtr<FJsonValue> PPlug_UnknownPluginError(const FString& Name)
{
	return MCPError(FString::Printf(
		TEXT("No plugin named '%s' is installed in this engine or project, so it cannot be enabled or ")
		TEXT("disabled. Installed names that look similar: %s. List every installed plugin with ")
		TEXT("project(list_available_plugins), optionally filtered with `filter`."),
		*Name, *PPlug_NearestNames(Name)));
}

/** The loaded project descriptor, or null when the editor has no project. */
static const FProjectDescriptor* PPlug_Project()
{
	return IProjectManager::Get().GetCurrentProject();
}

/**
 * The `.uproject` entry for a plugin, as it stands right now.
 *
 * bOutPresent separates "there is an explicit entry" from "the entry says
 * enabled", because those are two different states and the inverse of a write
 * depends on which one it started in. A plugin that is on by DEFAULT has no
 * entry at all, and adding an explicit enabled entry for it is a file change
 * with no behavioural change, which is exactly the case idempotency has to
 * report rather than perform.
 */
static void PPlug_ReadReference(const FString& Name, bool& bOutPresent, bool& bOutEnabled)
{
	bOutPresent = false;
	bOutEnabled = false;
	const FProjectDescriptor* Descriptor = PPlug_Project();
	if (!Descriptor) return;
	const int32 Index = Descriptor->FindPluginReferenceIndex(Name);
	if (Index == INDEX_NONE) return;
	bOutPresent = true;
	bOutEnabled = Descriptor->Plugins[Index].bEnabled;
}

/** Everything a caller needs about one plugin, in one object. */
static TSharedPtr<FJsonObject> PPlug_Describe(const TSharedRef<IPlugin>& Plugin)
{
	const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();

	bool bPresent = false;
	bool bRefEnabled = false;
	PPlug_ReadReference(Plugin->GetName(), bPresent, bRefEnabled);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("name"), Plugin->GetName());
	Obj->SetStringField(TEXT("friendlyName"), Plugin->GetFriendlyName());
	Obj->SetStringField(TEXT("category"), Descriptor.Category);
	Obj->SetStringField(TEXT("description"), Descriptor.Description);
	Obj->SetStringField(TEXT("versionName"), Descriptor.VersionName);
	Obj->SetStringField(TEXT("type"), PPlug_TypeName(Plugin->GetType()));
	Obj->SetStringField(TEXT("loadedFrom"), PPlug_LoadedFromName(Plugin->GetLoadedFrom()));
	Obj->SetStringField(TEXT("descriptorFile"), Plugin->GetDescriptorFileName());
	// `enabled` is the state of THIS editor session, decided at startup. It does
	// not follow a write made in this session; `projectReference` does.
	Obj->SetBoolField(TEXT("enabled"), Plugin->IsEnabled());
	Obj->SetBoolField(TEXT("mounted"), Plugin->IsMounted());
	Obj->SetBoolField(TEXT("enabledByDefault"), Plugin->IsEnabledByDefault(/*bAllowEnginePluginsEnabledByDefault*/ true));
	Obj->SetBoolField(TEXT("isBeta"), Descriptor.bIsBetaVersion);
	Obj->SetBoolField(TEXT("isExperimental"), Descriptor.bIsExperimentalVersion);
	Obj->SetBoolField(TEXT("canContainContent"), Plugin->CanContainContent());
	Obj->SetBoolField(TEXT("hasCode"), !Descriptor.bNoCode);

	TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
	Ref->SetBoolField(TEXT("present"), bPresent);
	Ref->SetBoolField(TEXT("enabled"), bRefEnabled);
	Obj->SetObjectField(TEXT("projectReference"), Ref);

	return Obj;
}

/**
 * Write the descriptor back to the `.uproject`.
 *
 * Returns an error value when the write fails, and that error says the
 * in-memory descriptor is now ahead of the file, because a caller who reads
 * "failed" and assumes nothing happened would be wrong.
 */
static TSharedPtr<FJsonValue> PPlug_Save(const FString& What)
{
	FText FailReason;
	if (IProjectManager::Get().SaveCurrentProjectToDisk(FailReason))
	{
		return nullptr;
	}
	return MCPError(FString::Printf(
		TEXT("%s was applied to the loaded project descriptor but the .uproject could not be written: %s. ")
		TEXT("The file is unchanged and the in-memory change is lost when the editor exits. Check the file ")
		TEXT("is not read-only or checked out elsewhere, then call the action again."),
		*What, *FailReason.ToString()));
}

} // namespace MCPProjectPlugins

using namespace MCPProjectPlugins;

// ═════════════════════════════════════════════════════════════════════════════
// list_available_plugins
// ═════════════════════════════════════════════════════════════════════════════

TSharedPtr<FJsonValue> FProjectHandlers::ListAvailablePlugins(const TSharedPtr<FJsonObject>& Params)
{
	const FString Filter = OptionalString(Params, TEXT("filter"));
	const FString Category = OptionalString(Params, TEXT("pluginCategory"));
	const bool bEnabledOnly = OptionalBool(Params, TEXT("enabledOnly"), false);

	// T3: paged. This used to stop adding rows at `limit` while still counting
	// the matches, so a caller was told there were 900 and handed 200 with no
	// way to ask for the rest.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_available_plugins|filter=%s|pluginCategory=%s|enabledOnly=%d"),
				*Filter, *Category, bEnabledOnly ? 1 : 0),
			/*DefaultLimit*/ 200, /*MaxLimit*/ 2000, Page))
	{
		return Err;
	}

	TArray<TSharedRef<IPlugin>> All = IPluginManager::Get().GetDiscoveredPlugins();
	// GetDiscoveredPlugins returns them in discovery order, which is not a
	// contract, so they are sorted by name before paging, as they always were.
	All.Sort([](const TSharedRef<IPlugin>& A, const TSharedRef<IPlugin>& B)
	{
		return A->GetName() < B->GetName();
	});

	TArray<MCPPagination::FPageRow> Rows;
	for (const TSharedRef<IPlugin>& Plugin : All)
	{
		if (bEnabledOnly && !Plugin->IsEnabled()) continue;
		if (!Filter.IsEmpty()
			&& !Plugin->GetName().Contains(Filter, ESearchCase::IgnoreCase)
			&& !Plugin->GetFriendlyName().Contains(Filter, ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (!Category.IsEmpty()
			&& !Plugin->GetDescriptor().Category.Contains(Category, ESearchCase::IgnoreCase))
		{
			continue;
		}
		// The plugin NAME is the page anchor: it is what enable_plugin and
		// disable_plugin address, and it is unique across discovered plugins.
		Rows.Add({ Plugin->GetName(), MakeShared<FJsonValueObject>(PPlug_Describe(Plugin)) });
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("matched"), Rows.Num());
	Result->SetNumberField(TEXT("totalDiscovered"), All.Num());
	MCPPagination::EmitPage(Page, Rows, TEXT("plugins"), Result);
	Result->SetStringField(TEXT("note"), TEXT(
		"`enabled` is what this editor session started with. `projectReference` is what the .uproject "
		"says right now, so after project(enable_plugin) the two disagree until the editor restarts."));
	return MCPResult(Result);
}

// ═════════════════════════════════════════════════════════════════════════════
// enable_plugin
// ═════════════════════════════════════════════════════════════════════════════

TSharedPtr<FJsonValue> FProjectHandlers::EnablePlugin(const TSharedPtr<FJsonObject>& Params)
{
	FString PluginName;
	if (auto Err = RequireString(Params, TEXT("pluginName"), PluginName)) return Err;

	// ── Validate everything before touching the descriptor.
	TSharedPtr<IPlugin> Plugin = PPlug_Find(PluginName);
	if (!Plugin) return PPlug_UnknownPluginError(PluginName);

	// Use the plugin's own spelling from here on, so the .uproject entry matches
	// the .uplugin regardless of how the caller cased it.
	PluginName = Plugin->GetName();

	if (!PPlug_Project())
	{
		return MCPError(TEXT(
			"No project descriptor is loaded, so there is no .uproject to write a plugin reference into. "
			"This is only possible in a commandlet or a standalone process with no project."));
	}

	bool bPresent = false;
	bool bRefEnabled = false;
	PPlug_ReadReference(PluginName, bPresent, bRefEnabled);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("pluginName"), PluginName);
	Result->SetStringField(TEXT("friendlyName"), Plugin->GetFriendlyName());
	Result->SetStringField(TEXT("descriptorFile"), Plugin->GetDescriptorFileName());
	Result->SetBoolField(TEXT("enabledByDefault"), Plugin->IsEnabledByDefault(true));
	Result->SetBoolField(TEXT("loadedInThisSession"), Plugin->IsEnabled());
	Result->SetBoolField(TEXT("canEnableInCurrentTarget"),
		IPluginManager::Get().CanEnablePluginInCurrentTarget(PluginName));

	// ── Idempotency. Two shapes of "already enabled", and neither writes.
	if (bPresent && bRefEnabled)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("restartRequired"), !Plugin->IsEnabled());
		Result->SetStringField(TEXT("reason"), Plugin->IsEnabled()
			? TEXT("The .uproject already enables this plugin and the editor has it loaded.")
			: TEXT("The .uproject already enables this plugin, but this editor session started before that "
			       "and has not loaded it. Restart the editor with editor(restart_editor)."));
		return MCPResult(Result);
	}
	if (!bPresent && Plugin->IsEnabled())
	{
		// On by default with no explicit entry. Writing one would change the file
		// and change nothing else, so the honest answer is that it is already on.
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("restartRequired"), false);
		Result->SetStringField(TEXT("reason"), TEXT(
			"This plugin is enabled by default and is already loaded, so the .uproject needs no entry for "
			"it and none was added."));
		return MCPResult(Result);
	}

	// ── Mutate.
	FText FailReason;
	if (!IProjectManager::Get().SetPluginEnabled(PluginName, true, FailReason))
	{
		return MCPError(FString::Printf(
			TEXT("Could not enable plugin '%s' in the project descriptor: %s"),
			*PluginName, *FailReason.ToString()));
	}
	if (auto SaveErr = PPlug_Save(FString::Printf(TEXT("Enabling '%s'"), *PluginName)))
	{
		return SaveErr;
	}

	if (bPresent) MCPSetUpdated(Result); else MCPSetCreated(Result);
	Result->SetBoolField(TEXT("unchanged"), false);
	Result->SetBoolField(TEXT("restartRequired"), true);
	Result->SetStringField(TEXT("restartWith"), TEXT("editor(restart_editor)"));
	Result->SetStringField(TEXT("note"), FString::Printf(TEXT(
		"'%s' is now enabled in the .uproject. Its modules, classes, content and settings appear only "
		"after the editor restarts; nothing about this session changed. If the plugin has code and the "
		"project has never built with it, the restart may ask to rebuild."), *PluginName));

	if (!IPluginManager::Get().CanEnablePluginInCurrentTarget(PluginName))
	{
		Result->SetStringField(TEXT("warning"), FString::Printf(TEXT(
			"'%s' declares no support for the current build target, so the entry is written but the plugin "
			"may not load. Check its .uplugin SupportedTargetPlatforms and module HostType."), *PluginName));
	}

	// The inverse depends on where it started, and the two cases differ in the
	// FILE rather than in behaviour: with no entry before, the exact undo is
	// deleting the entry, not writing an explicit disable that was never there.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("pluginName"), PluginName);
	Payload->SetBoolField(TEXT("removeReference"), !bPresent);
	MCPSetRollback(Result, TEXT("disable_plugin"), Payload);

	return MCPResult(Result);
}

// ═════════════════════════════════════════════════════════════════════════════
// disable_plugin
// ═════════════════════════════════════════════════════════════════════════════

TSharedPtr<FJsonValue> FProjectHandlers::DisablePlugin(const TSharedPtr<FJsonObject>& Params)
{
	FString PluginName;
	if (auto Err = RequireString(Params, TEXT("pluginName"), PluginName)) return Err;

	// removeReference deletes the .uproject entry outright instead of writing
	// an explicit `"Enabled": false`. The two differ for a plugin that is on by
	// default: deleting the entry hands it back to the default, writing false
	// overrides the default. Both are legitimate, so the caller picks.
	const bool bRemoveReference = OptionalBool(Params, TEXT("removeReference"), false);

	TSharedPtr<IPlugin> Plugin = PPlug_Find(PluginName);
	if (!Plugin) return PPlug_UnknownPluginError(PluginName);
	PluginName = Plugin->GetName();

	if (PluginName.Equals(PPlug_BridgePluginName, ESearchCase::IgnoreCase))
	{
		return MCPError(FString::Printf(TEXT(
			"Refusing to disable '%s'. It is the bridge serving this call, and disabling it means the next "
			"editor start has no bridge and no way to undo the change from here. Edit the .uproject by hand "
			"if that is really what you want."), *PluginName));
	}

	if (!PPlug_Project())
	{
		return MCPError(TEXT(
			"No project descriptor is loaded, so there is no .uproject to change."));
	}

	bool bPresent = false;
	bool bRefEnabled = false;
	PPlug_ReadReference(PluginName, bPresent, bRefEnabled);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("pluginName"), PluginName);
	Result->SetStringField(TEXT("friendlyName"), Plugin->GetFriendlyName());
	Result->SetBoolField(TEXT("enabledByDefault"), Plugin->IsEnabledByDefault(true));
	Result->SetBoolField(TEXT("loadedInThisSession"), Plugin->IsEnabled());
	Result->SetBoolField(TEXT("removeReference"), bRemoveReference);

	// ── Idempotency, against the state the caller actually asked for.
	if (bRemoveReference && !bPresent)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("restartRequired"), false);
		Result->SetStringField(TEXT("reason"), FString::Printf(TEXT(
			"The .uproject has no entry for '%s', so there is nothing to remove. It is currently %s by "
			"default."), *PluginName, Plugin->IsEnabledByDefault(true) ? TEXT("enabled") : TEXT("disabled")));
		return MCPResult(Result);
	}
	if (!bRemoveReference && bPresent && !bRefEnabled)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("restartRequired"), Plugin->IsEnabled());
		Result->SetStringField(TEXT("reason"), Plugin->IsEnabled()
			? TEXT("The .uproject already disables this plugin, but this editor session started before that "
			       "and still has it loaded. Restart the editor with editor(restart_editor).")
			: TEXT("The .uproject already disables this plugin and the editor has not loaded it."));
		return MCPResult(Result);
	}

	// ── Mutate.
	FText FailReason;
	const bool bApplied = bRemoveReference
		? IProjectManager::Get().RemovePluginReference(PluginName, FailReason)
		: IProjectManager::Get().SetPluginEnabled(PluginName, false, FailReason);
	if (!bApplied)
	{
		return MCPError(FString::Printf(
			TEXT("Could not %s plugin '%s' in the project descriptor: %s"),
			bRemoveReference ? TEXT("remove the reference to") : TEXT("disable"),
			*PluginName, *FailReason.ToString()));
	}
	if (auto SaveErr = PPlug_Save(FString::Printf(
		TEXT("%s '%s'"), bRemoveReference ? TEXT("Removing the reference to") : TEXT("Disabling"), *PluginName)))
	{
		return SaveErr;
	}

	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("unchanged"), false);
	Result->SetBoolField(TEXT("restartRequired"), Plugin->IsEnabled());
	Result->SetStringField(TEXT("restartWith"), TEXT("editor(restart_editor)"));
	Result->SetStringField(TEXT("note"), FString::Printf(TEXT(
		"'%s' is no longer enabled by the .uproject. This editor session keeps whatever it loaded at "
		"startup until it restarts. Assets referencing the plugin's classes will fail to load once it is "
		"off, which is a content change this call does not and cannot undo."), *PluginName));

	// The exact inverse of each starting state.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("pluginName"), PluginName);
	if (bPresent && bRefEnabled)
	{
		// There was an explicit enabled entry: put it back.
		MCPSetRollback(Result, TEXT("enable_plugin"), Payload);
	}
	else
	{
		// Either there was no entry (on by default), or there was a disabled one
		// that removeReference has just deleted. Both are restored by taking the
		// file back to the entry it had, which is what disable_plugin writes:
		// removeReference=true deletes the entry we just added, and
		// removeReference=false re-adds the disabled entry we just deleted.
		Payload->SetBoolField(TEXT("removeReference"), !bPresent);
		MCPSetRollback(Result, TEXT("disable_plugin"), Payload);
	}

	return MCPResult(Result);
}
