#pragma once

#include "MCPHandlerSpec.h"

// Parameters many handlers take, declared once so their name, type and meaning
// cannot drift between categories. A handler whose use adds something of its
// own (what the actor is, which scope is its default) keeps the shared name and
// type and says it with .Describe().
namespace MCPSharedParam
{
	inline FMCPParamSpec ActorLabel()
	{
		return MCPParam::Optional(TEXT("actorLabel"), EMCPParamType::String,
			TEXT("Actor editor label. Labels are not unique, so a label naming several actors is refused; pass actorPath then"));
	}

	inline FMCPParamSpec ActorPath()
	{
		return MCPParam::Optional(TEXT("actorPath"), EMCPParamType::String,
			TEXT("Full actor object path, the unambiguous selector; wins over actorLabel"));
	}

	inline FMCPParamSpec PieInstance()
	{
		return MCPParam::Optional(TEXT("pieInstance"), EMCPParamType::Integer,
			TEXT("Which PIE world: 0 = server or primary, 1..N = clients. See editor(list_pie_instances)")).Min(0);
	}

	/** Each action names its own default scope, so a bare World() says so; most use .Describe(). */
	inline FMCPParamSpec World()
	{
		return MCPParam::Optional(TEXT("world"), EMCPParamType::String,
			TEXT("World scope: editor | pie | auto. Each action names its own default"));
	}

	inline FMCPParamSpec OnConflict()
	{
		return MCPParam::Optional(TEXT("onConflict"), EMCPParamType::String,
			TEXT("When the asset already exists: skip (default) returns it untouched | error refuses; it never overwrites"));
	}

	inline FMCPParamSpec DryRun()
	{
		return MCPParam::Optional(TEXT("dryRun"), EMCPParamType::Boolean,
			TEXT("Validate and report without writing (default false)")).Default(false);
	}
}
