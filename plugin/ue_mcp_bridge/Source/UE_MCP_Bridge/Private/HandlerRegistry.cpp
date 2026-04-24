#include "HandlerRegistry.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HandlerUtils.h"

FMCPHandlerRegistry::FMCPHandlerRegistry()
{
}

FMCPHandlerRegistry::~FMCPHandlerRegistry()
{
	Clear();
}

void FMCPHandlerRegistry::RegisterHandler(const FString& MethodName, FHandlerFunction Handler)
{
	CppHandlers.Add(MethodName, Handler);
}

void FMCPHandlerRegistry::RegisterHandlerWithTimeout(const FString& MethodName, FHandlerFunction Handler, float TimeoutSeconds)
{
	CppHandlers.Add(MethodName, Handler);
	if (TimeoutSeconds > 0.0f)
	{
		HandlerTimeouts.Add(MethodName, TimeoutSeconds);
	}
}

float FMCPHandlerRegistry::GetHandlerTimeout(const FString& MethodName) const
{
	if (const float* V = HandlerTimeouts.Find(MethodName))
	{
		return *V;
	}
	return 0.0f;
}

void FMCPHandlerRegistry::RegisterPythonHandler(const FString& MethodName, const FString& PythonScriptPath)
{
	FPythonHandlerInfo Info;
	Info.ScriptPath = PythonScriptPath;
	Info.HandlerName = MethodName;
	PythonHandlers.Add(MethodName, Info);
}

TSharedPtr<FJsonValue> FMCPHandlerRegistry::ExecuteHandler(const FString& MethodName, const TSharedPtr<FJsonObject>& Params)
{
	// Single-point defence-in-depth: every handler must run on the game thread.
	// GameThreadExecutor already dispatches here from the game thread, so this
	// should always hold; if it ever doesn't (async/timer regression), we'd
	// rather assert here than silently corrupt UObject state per-handler.
	MCP_CHECK_GAME_THREAD();

	// Try C++ handler first
	if (CppHandlers.Contains(MethodName))
	{
		return CppHandlers[MethodName](Params);
	}

	// Try Python handler
	if (PythonHandlers.Contains(MethodName))
	{
		return ExecutePythonHandler(MethodName, Params);
	}

	// Handler not found - return nullptr so BridgeServer sends "Unknown method" error
	return nullptr;
}

bool FMCPHandlerRegistry::HasHandler(const FString& MethodName) const
{
	return CppHandlers.Contains(MethodName) || PythonHandlers.Contains(MethodName);
}

TArray<FString> FMCPHandlerRegistry::GetHandlerNames() const
{
	TArray<FString> Names;
	CppHandlers.GetKeys(Names);
	
	TArray<FString> PythonNames;
	PythonHandlers.GetKeys(PythonNames);
	Names.Append(PythonNames);
	
	return Names;
}

void FMCPHandlerRegistry::Clear()
{
	CppHandlers.Empty();
	PythonHandlers.Empty();
	HandlerTimeouts.Empty();
}

TSharedPtr<FJsonValue> FMCPHandlerRegistry::ExecutePythonHandler(const FString& MethodName, const TSharedPtr<FJsonObject>& /*Params*/)
{
	// Python handler dispatch is not implemented. Prior behaviour returned an
	// empty JSON object, which callers could not distinguish from a real
	// empty-success result. Return a typed error instead so callers see the
	// gap clearly; use `execute_python` for ad-hoc Python until the dispatch
	// pipeline lands.
	TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
	Err->SetBoolField(TEXT("success"), false);
	Err->SetStringField(TEXT("error"), FString::Printf(
		TEXT("Python handler '%s' is registered but Python dispatch is not implemented. Use the 'execute_python' action instead."),
		*MethodName));
	return MakeShared<FJsonValueObject>(Err);
}
