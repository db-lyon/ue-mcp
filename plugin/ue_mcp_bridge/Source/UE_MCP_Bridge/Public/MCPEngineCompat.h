#pragma once

// Engine headers that moved between UE 5.4 and 5.5, in one place.
//
// The plugin supports UE 5.4 through 5.8. Several headers this module includes
// were relocated in 5.5, when StructUtils graduated from an experimental plugin
// into CoreUObject and PerPlatformProperties moved out of Engine. A translation
// unit that hard-codes either spelling compiles on one half of the supported
// range and fails with C1083 on the other, which is what a 5.4 user sees: 30-odd
// files failing on 'StructUtils/InstancedStruct.h' before anything else is even
// attempted.
//
// Include this header instead of the moved ones. It is include paths only, no
// API shims: the types themselves are the same on both sides of the move.
//
// On 5.4 the StructUtils include path comes in transitively through Chooser and
// StateTreeModule, and Build.cs adds StructUtils as a private dependency below
// 5.5 so its symbols link.

#include "Runtime/Launch/Resources/Version.h"

#if (ENGINE_MAJOR_VERSION > 5) || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
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

// StateTree API gates. They live here rather than in a handler .cpp because the
// module is a unity build: a #define in one .cpp leaks into every file after it
// in the same blob, and a second definition there is a redefinition warning.
#define UE_MCP_HAS_STATETREE_STATE_DESCRIPTION (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6))
#define UE_MCP_HAS_STATETREE_STATE_CUSTOM_TICK_RATE (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6))
#define UE_MCP_HAS_STATETREE_COMPILER_TOKENIZED_MESSAGES (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6))
#define UE_MCP_HAS_STATETREE_EXECUTION_RUNTIME_DATA (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7))
#define UE_MCP_HAS_STATETREE_GENERAL_PROPERTY_BINDING (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7))
// FStateTreeEditorNode::InitializeAs(Outer, Struct) and ReallocInstanceData are
// 5.8; on 5.7 the same two cases are handled by hand.
#define UE_MCP_HAS_STATETREE_NODE_OUTER_INIT (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8))
// UStateTree::GetStateHandleFromGameplayTag does not exist before 5.8, so
// request_transition takes targetStateId there and says why.
#define UE_MCP_HAS_STATETREE_TAG_STATE_LOOKUP (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8))
