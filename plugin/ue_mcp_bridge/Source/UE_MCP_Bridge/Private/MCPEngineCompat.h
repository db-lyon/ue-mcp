#pragma once

// Engine headers that moved between UE 5.4 and 5.5, in one place.
//
// The plugin supports UE <!-- count:engineRange -->5.4 to 5.9<!-- /count -->.
// Several headers this module includes were relocated in 5.5, when StructUtils
// graduated from an experimental plugin into CoreUObject and
// PerPlatformProperties moved out of Engine. A translation unit that hard-codes
// either spelling compiles on one half of the supported range and fails with
// C1083 on the other, which is what a 5.4 user sees: 30-odd files failing on
// 'StructUtils/InstancedStruct.h' before anything else is even attempted.
//
// Include this header instead of the moved ones. It is include paths only, no
// API shims: the types themselves are the same on both sides of the move. The
// version and feature gates live in HandlerEngineVersion.h.
//
// On 5.4 the StructUtils include path comes in transitively through Chooser and
// StateTreeModule, and Build.cs adds StructUtils as a private dependency below
// 5.5 so its symbols link.

#include "HandlerEngineVersion.h"

#if UE_MCP_HAS_CORE_STRUCT_UTILS
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/PropertyBag.h"
#include "StructUtils/StructView.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/PerPlatformProperties.h"
#else
// UE 5.4: StructUtils is Engine/Plugins/Experimental/StructUtils, whose public
// headers sit at the include root; UserDefinedStruct and PerPlatformProperties
// are still in Engine.
#include "InstancedStruct.h"
#include "PropertyBag.h"
#include "StructView.h"
#include "Engine/UserDefinedStruct.h"
#include "PerPlatformProperties.h"
#endif
