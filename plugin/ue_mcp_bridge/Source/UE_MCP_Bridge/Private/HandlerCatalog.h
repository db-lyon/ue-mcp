#pragma once

// Every built-in handler category, registered in one place. Adding a category
// means adding its header and its RegisterHandlers call to HandlerCatalog.cpp.

class FMCPHandlerRegistry;

namespace MCPHandlerCatalog
{
	void RegisterAllHandlers(FMCPHandlerRegistry& Registry);
}
