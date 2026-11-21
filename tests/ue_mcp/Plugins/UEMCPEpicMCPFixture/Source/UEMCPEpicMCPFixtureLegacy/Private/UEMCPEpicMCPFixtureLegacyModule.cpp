// Loaded by UEMCPEpicMCPFixture on engines that still ship the legacy MCP Tool
// Library. Loading the module creates the library's CDO, which registers its tools.

#include "UEMCPFixtureToolLibrary.h"

#include "Modules/ModuleManager.h"

FString UUEMCPFixtureToolLibrary::UeMcpFixtureLibraryGreet(const FString& Name)
{
	return FString::Printf(TEXT("hello, %s"), *Name);
}

IMPLEMENT_MODULE(FDefaultModuleImpl, UEMCPEpicMCPFixtureLegacy)
