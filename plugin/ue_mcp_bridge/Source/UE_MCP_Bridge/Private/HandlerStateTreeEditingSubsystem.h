#pragma once

// Include StateTreeEditingSubsystem.h through this header, never directly.
//
// In UE 5.9 it pulls in StateTreeViewModel.h, whose inline accessor reads its
// own UE_EXPERIMENTAL delegate member, so the engine header itself raises three
// C4996 warnings in whichever translation unit parses it first. Nothing in the
// bridge touches the experimental members; this only keeps the engine header
// from warning on every user build. The module is a unity build and the header
// is #pragma once, so every includer has to come through here, or the first
// direct include in a blob brings the warnings back.

PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "StateTreeEditingSubsystem.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
