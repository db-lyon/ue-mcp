#include "HandlerRegistry.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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

TSharedPtr<FJsonValue> FMCPHandlerRegistry::ExecutePythonHandler(const FString& MethodName, const TSharedPtr<FJsonObject>& Params)
{
	// TODO: Implement Python handler execution
	// For now, return empty result
	// Will be implemented when Python plugin system is added
	return MakeShared<FJsonValueObject>(MakeShared<FJsonObject>());
}
