// Python additional search paths (#1828).
//
// UE 5.9 lets a caller put a folder on the editor Python's sys.path under an
// owner name and take it off again: UPythonScriptLibrary::AddAdditionalPath,
// RemoveAdditionalPath, RemoveAdditionalPaths, GetAdditionalPaths and
// GetAllAdditionalPaths. That library is private to PythonScriptPlugin and is a
// thin wrapper over IPythonAdditionalPaths, which the plugin publishes through
// IPythonScriptPlugin::GetAdditionalPathsInterface, so these handlers call the
// interface the library calls.
//
// Older engines have no registry of additional paths: their only additional
// paths are the Python plugin's project settings, read at startup. The actions
// refuse there with unsupported_engine_version.
//
// Translation-unit partition of FEditorHandlers; registration is
// RegisterPythonPathHandlers below, called from EditorHandlers.cpp.

#include "EditorHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "IPythonScriptPlugin.h"
#if UE_MCP_HAS_5_9_API
#include "IPythonAdditionalPaths.h"
#endif
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
#if UE_MCP_HAS_5_9_API
	/** The additional-paths interface, or an error to return when Python is not running. */
	IPythonAdditionalPaths* MCPPyPathsInterface(TSharedPtr<FJsonValue>& OutError)
	{
		IPythonScriptPlugin* Plugin = IPythonScriptPlugin::Get();
		if (!Plugin || !Plugin->IsPythonAvailable())
		{
			OutError = MCPErrorWithCode(TEXT("PLUGIN_NOT_AVAILABLE"),
				TEXT("Python is not available in this editor. Enable the Python Editor Script Plugin with project(enable_plugin, pluginName=\"PythonScriptPlugin\") and restart the editor."));
			return nullptr;
		}
		return &Plugin->GetAdditionalPathsInterface();
	}

	/** The paths registered to Owner, or to every owner when Owner is empty. */
	TArray<FString> MCPPyPathsList(const IPythonAdditionalPaths& Paths, const FString& Owner, bool& bOutOwnerKnown)
	{
		TArray<FString> Out;
		if (Owner.IsEmpty())
		{
			Paths.GetAdditionalPaths(Out);
			bOutOwnerKnown = true;
		}
		else
		{
			bOutOwnerKnown = Paths.GetAdditionalPaths(FName(*Owner), Out);
		}
		return Out;
	}

	bool MCPPyPathsContains(const TArray<FString>& Paths, const FString& Path)
	{
		const FString Wanted = FPaths::ConvertRelativePathToFull(Path);
		return Paths.ContainsByPredicate([&Wanted](const FString& Candidate)
		{
			return FPaths::IsSamePath(FPaths::ConvertRelativePathToFull(Candidate), Wanted);
		});
	}
#else
	TSharedPtr<FJsonValue> MCPPyPathsUnsupported(const TCHAR* Function)
	{
		return MCPUnsupportedEngineError(FString::Printf(TEXT("UPythonScriptLibrary::%s"), Function), TEXT("5.9"),
			TEXT("Older engines read additional Python paths only from the Python plugin's project settings at startup"));
	}
#endif
}


// editor(add_python_path): UPythonScriptLibrary::AddAdditionalPath.
TSharedPtr<FJsonValue> FEditorHandlers::AddPythonPath(const TSharedPtr<FJsonObject>& Params)
{
	FString Path;
	if (auto Err = RequireString(Params, TEXT("path"), Path)) return Err;
	const FString Owner = OptionalString(Params, TEXT("owner"));
	const FString MountPointRoot = OptionalString(Params, TEXT("mountPointRoot"));
#if UE_MCP_HAS_5_9_API
	if (FPaths::IsRelative(Path) || !FPaths::DirectoryExists(Path))
	{
		return MCPError(FString::Printf(TEXT("'%s' is not an absolute path to an existing folder, which an additional Python path has to be."), *Path));
	}
	TSharedPtr<FJsonValue> Error;
	IPythonAdditionalPaths* Paths = MCPPyPathsInterface(Error);
	if (!Paths) return Error;

	TArray<FString> Registered;
	Paths->GetAdditionalPaths(Registered);
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), Path);
	if (!Owner.IsEmpty()) Result->SetStringField(TEXT("owner"), Owner);
	if (MCPPyPathsContains(Registered, Path))
	{
		MCPSetExisted(Result);
		MCPSetNoRollback(Result, TEXT("The path was already registered, so this call added nothing to remove."));
		return MCPResult(Result);
	}
	if (!Paths->AddAdditionalPath(Path, Owner.IsEmpty() ? NAME_None : FName(*Owner), MountPointRoot))
	{
		return MCPError(FString::Printf(
			TEXT("AddAdditionalPath refused '%s': it is invalid, or already on sys.path from another source (a plugin's Python folder or the project settings), in which case it cannot be registered to an owner."),
			*Path));
	}
	MCPSetCreated(Result);
	if (!MountPointRoot.IsEmpty()) Result->SetStringField(TEXT("mountPointRoot"), MountPointRoot);
	TArray<FString> After;
	Paths->GetAdditionalPaths(After);
	Result->SetArrayField(TEXT("paths"), MCPStringListToJson(After));
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), Path);
	if (!Owner.IsEmpty()) Payload->SetStringField(TEXT("owner"), Owner);
	MCPSetRollback(Result, TEXT("remove_python_path"), Payload);
	return MCPResult(Result);
#else
	return MCPPyPathsUnsupported(TEXT("AddAdditionalPath"));
#endif
}


// editor(remove_python_path): UPythonScriptLibrary::RemoveAdditionalPath, or
// RemoveAdditionalPaths when only an owner is given.
TSharedPtr<FJsonValue> FEditorHandlers::RemovePythonPath(const TSharedPtr<FJsonObject>& Params)
{
	const FString Path = OptionalString(Params, TEXT("path"));
	const FString Owner = OptionalString(Params, TEXT("owner"));
	if (Path.IsEmpty() && Owner.IsEmpty())
	{
		return MCPError(TEXT("Pass 'path' to remove one registration, or only 'owner' to remove every path that owner registered."));
	}
#if UE_MCP_HAS_5_9_API
	TSharedPtr<FJsonValue> Error;
	IPythonAdditionalPaths* Paths = MCPPyPathsInterface(Error);
	if (!Paths) return Error;

	const FName OwnerName = Owner.IsEmpty() ? NAME_None : FName(*Owner);
	TArray<FString> Removed;
	bool bRemoved = false;
	if (!Path.IsEmpty())
	{
		bRemoved = Paths->RemoveAdditionalPath(Path, OwnerName);
		if (bRemoved) Removed.Add(Path);
	}
	else
	{
		Paths->GetAdditionalPaths(OwnerName, Removed);
		bRemoved = Paths->RemoveAdditionalPaths(OwnerName);
		if (!bRemoved) Removed.Reset();
	}

	auto Result = MCPSuccess();
	if (!Path.IsEmpty()) Result->SetStringField(TEXT("path"), Path);
	if (!Owner.IsEmpty()) Result->SetStringField(TEXT("owner"), Owner);
	Result->SetArrayField(TEXT("removedPaths"), MCPStringListToJson(Removed));
	TArray<FString> After;
	Paths->GetAdditionalPaths(After);
	Result->SetArrayField(TEXT("paths"), MCPStringListToJson(After));
	if (!bRemoved)
	{
		Result->SetBoolField(TEXT("alreadyRemoved"), true);
		MCPSetNoRollback(Result, Path.IsEmpty()
			? TEXT("No path was registered to this owner, so nothing was removed.")
			: TEXT("The path was not registered (to this owner), so nothing was removed."));
		return MCPResult(Result);
	}
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("note"), TEXT("Removing a registration does not unload modules imported from the path."));
	if (Removed.Num() == 1)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("path"), Removed[0]);
		if (!Owner.IsEmpty()) Payload->SetStringField(TEXT("owner"), Owner);
		MCPSetRollback(Result, TEXT("add_python_path"), Payload);
		Result->SetStringField(TEXT("rollbackNote"), TEXT("The rollback registers the path again without a mount point root, since the registration does not report the one it had."));
	}
	else
	{
		MCPSetNoRollback(Result, FString::Printf(
			TEXT("%d paths were removed; add_python_path registers one path per call, so each comes back with its own call (removedPaths lists them)."), Removed.Num()));
	}
	return MCPResult(Result);
#else
	return MCPPyPathsUnsupported(Path.IsEmpty() ? TEXT("RemoveAdditionalPaths") : TEXT("RemoveAdditionalPath"));
#endif
}


// editor(list_python_paths): UPythonScriptLibrary::GetAllAdditionalPaths, or
// GetAdditionalPaths for one owner.
TSharedPtr<FJsonValue> FEditorHandlers::ListPythonPaths(const TSharedPtr<FJsonObject>& Params)
{
	const FString Owner = OptionalString(Params, TEXT("owner"));
#if UE_MCP_HAS_5_9_API
	TSharedPtr<FJsonValue> Error;
	IPythonAdditionalPaths* Paths = MCPPyPathsInterface(Error);
	if (!Paths) return Error;
	bool bOwnerKnown = true;
	const TArray<FString> Listed = MCPPyPathsList(*Paths, Owner, bOwnerKnown);
	auto Result = MCPSuccess();
	if (!Owner.IsEmpty())
	{
		Result->SetStringField(TEXT("owner"), Owner);
		Result->SetBoolField(TEXT("ownerRegistered"), bOwnerKnown);
	}
	Result->SetArrayField(TEXT("paths"), MCPStringListToJson(Listed));
	Result->SetNumberField(TEXT("count"), Listed.Num());
	return MCPResult(Result);
#else
	return MCPPyPathsUnsupported(Owner.IsEmpty() ? TEXT("GetAllAdditionalPaths") : TEXT("GetAdditionalPaths"));
#endif
}


void FEditorHandlers::RegisterPythonPathHandlers(FMCPHandlerRegistry& Registry)
{
	using EType = EMCPParamType;
	Registry.RegisterHandler(TEXT("add_python_path"), &AddPythonPath, {
		MCPParam::Required(TEXT("path"), EType::String, TEXT("Absolute path of an existing folder to put on sys.path")),
		MCPParam::Optional(TEXT("owner"), EType::String, TEXT("Name of what owns the path; remove_python_path with only owner removes all of its paths")),
		MCPParam::Optional(TEXT("mountPointRoot"), EType::String, TEXT("Package root, such as /MyPlugin/Python/Extra, that gives Python types generated from this path stable names")),
	});
	Registry.RegisterHandler(TEXT("remove_python_path"), &RemovePythonPath, {
		MCPParam::Optional(TEXT("path"), EType::String, TEXT("Path to remove, as it was added")),
		MCPParam::Optional(TEXT("owner"), EType::String, TEXT("Owner it was added under; alone, removes every path of that owner")),
	}, MCPSpec::AtLeastOne({ { TEXT("path") }, { TEXT("owner") } }));
	Registry.RegisterHandler(TEXT("list_python_paths"), &ListPythonPaths, {
		MCPParam::Optional(TEXT("owner"), EType::String, TEXT("Only the paths of this owner (default: every owner)")),
	});
}
