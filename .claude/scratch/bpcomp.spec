	UClass* CompClass = nullptr;
	if (ComponentClass.Contains(TEXT("/")) || ComponentClass.Contains(TEXT(".")))
	{
		CompClass = LoadObject<UClass>(nullptr, *ComponentClass);
	}
	if (!CompClass)
	{
		CompClass = MCPResolveClass(ComponentClass);
	}
	if (!CompClass)
	{
		CompClass = LoadObject<UClass>(nullptr, *(FString(TEXT("/Script/Engine.")) + ComponentClass));
	}

	if (!CompClass)
-----
	UClass* CompClass = MCPWorldSpawn::ResolveComponentClass(ComponentClass);
	if (!CompClass)
=====
#include "Families/MCPCreate.h"
-----
#include "Families/MCPCreate.h"
#include "Families/MCPWorldSpawn.h"
