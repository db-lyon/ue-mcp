// Editor viewport control and transaction / undo-stack control.
//
// Two gaps this closes.
//
// Viewport: get_viewport reported location, rotation and FOV, set_viewport
// wrote location and rotation, and nothing anywhere touched view mode,
// exposure, clip planes, camera speed, game view or a forced redraw. That
// makes screenshot verification unreliable rather than merely incomplete: a
// capture inherits the world's auto-exposure, so the same scene photographs
// differently between runs and a visual diff cannot distinguish a lighting
// change from an eye-adaptation drift. A fixed EV100 override and a pinned
// view mode are what make two captures comparable.
//
// Transactions: undo/redo were bare GEditor->UndoTransaction() calls returning
// a lone bool, and there was no cancel at all, so a flow that failed partway
// through an open transaction could only commit the half-finished state.
//
// None of this is reachable through the reflection layer. FEditorViewportClient
// is a plain C++ class, not a UObject, so its fields have no FProperty and
// asset/editor set_property cannot see them. UTransactor is a UObject but its
// undo queue is engine-internal state driven by methods, not properties.

#include "EditorHandlers.h"

#include "HandlerUtils.h"

#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Editor/Transactor.h"
#include "Editor/UnrealEdTypes.h"
#include "EditorViewportClient.h"
#include "Engine/EngineBaseTypes.h"
#include "LevelEditorViewport.h"
#include "UnrealClient.h"
#if UE_MCP_HAS_5_7_API
// FEditorViewportCameraSpeedSettings and the Get/SetCameraSpeedSettings pair
// arrived in 5.7; the integer speed-setting + scalar API they replaced is
// deprecated there and does not exist as a struct before it.
#include "Settings/EditorViewportSettings.h"
#endif

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Internationalization/Text.h"
#include "Misc/Guid.h"

// Every helper below is prefixed MCPViewportCtl. The module is a unity build,
// so two .cpp files in one blob merge their anonymous namespaces and a helper
// that shares a name with one in EditorHandlers.cpp is a redefinition (C2084).
// Nothing here is copied from another handler file for the same reason.
namespace
{
	/** One friendly view-mode name and the EViewModeIndex value behind it.
	 *
	 *  The value is stored as a plain int and cast at use, deliberately. The
	 *  enum's own comment promises the numbers are stable ("Don't change the
	 *  order, the ID is serialized with the editor"), while the *names* come
	 *  and go across engine versions - VMI_VisualizeMegaLights and the
	 *  streaming-deficit modes do not exist before 5.8, and VMI_Lit_Wireframe
	 *  is deprecated in 5.8 and warns on every user build if named. Casting a
	 *  number compiles identically on 5.4 through 5.8, and VMI_Max (which every
	 *  version defines) tells us at runtime which of them this engine actually
	 *  understands. */
	struct FMCPViewportCtlViewModeEntry
	{
		const TCHAR* Name;
		int32 Value;
	};

	const FMCPViewportCtlViewModeEntry MCPViewportCtlViewModes[] =
	{
		{ TEXT("Wireframe"),                     0  }, // VMI_BrushWireframe
		{ TEXT("CSGWireframe"),                  1  }, // VMI_Wireframe
		{ TEXT("Unlit"),                         2  },
		{ TEXT("Lit"),                           3  },
		{ TEXT("DetailLighting"),                4  }, // VMI_Lit_DetailLighting
		{ TEXT("LightingOnly"),                  5  },
		{ TEXT("LightComplexity"),               6  },
		{ TEXT("ShaderComplexity"),              8  },
		{ TEXT("LightmapDensity"),               9  },
		{ TEXT("LitLightmapDensity"),            10 },
		{ TEXT("ReflectionOverride"),            11 },
		{ TEXT("VisualizeBuffer"),               12 },
		{ TEXT("StationaryLightOverlap"),        14 },
		{ TEXT("CollisionPawn"),                 15 },
		{ TEXT("CollisionVisibility"),           16 },
		{ TEXT("LODColoration"),                 18 },
		{ TEXT("QuadOverdraw"),                  19 },
		{ TEXT("PrimitiveDistanceAccuracy"),     20 },
		{ TEXT("MeshUVDensityAccuracy"),         21 },
		{ TEXT("ShaderComplexityWithQuadOverdraw"), 22 },
		{ TEXT("HLODColoration"),                23 },
		{ TEXT("GroupLODColoration"),            24 },
		{ TEXT("MaterialTextureScaleAccuracy"),  25 },
		{ TEXT("RequiredTextureResolution"),     26 },
		{ TEXT("PathTracing"),                   27 },
		{ TEXT("RayTracingDebug"),               28 },
		{ TEXT("VisualizeNanite"),               29 },
		{ TEXT("VisualizeVirtualTexture"),       30 },
		{ TEXT("VisualizeLumen"),                31 },
		{ TEXT("VisualizeVirtualShadowMap"),     32 },
		{ TEXT("VisualizeGPUSkinCache"),         33 },
		{ TEXT("VisualizeSubstrate"),            34 },
		{ TEXT("VisualizeGroom"),                35 },
		{ TEXT("LWCComplexity"),                 36 },
		{ TEXT("VisualizeActorColoration"),      38 },
		{ TEXT("ShadowCasters"),                 39 },
		{ TEXT("Clay"),                          40 },
		{ TEXT("Zebra"),                         41 },
		{ TEXT("FrontBackFace"),                 42 },
		{ TEXT("RandomColor"),                   43 },
		{ TEXT("VisualizeMegaLights"),           44 },
		{ TEXT("StreamingTextureDeficit"),       45 },
		{ TEXT("StreamingTextureResidency"),     46 },
		{ TEXT("StreamingMeshLODDeficit"),       47 },
		{ TEXT("StreamingMeshLODResidency"),     48 },
	};

	/** True when this engine build defines the mode. VMI_Max is the first value
	 *  past the last real mode on whatever version we compiled against. */
	bool MCPViewportCtlViewModeIsSupported(int32 Value)
	{
		return Value >= 0 && Value < static_cast<int32>(VMI_Max);
	}

	/** The friendly names this engine build actually supports, in table order. */
	TArray<FString> MCPViewportCtlSupportedViewModeNames()
	{
		TArray<FString> Names;
		for (const FMCPViewportCtlViewModeEntry& Entry : MCPViewportCtlViewModes)
		{
			if (MCPViewportCtlViewModeIsSupported(Entry.Value))
			{
				Names.Add(Entry.Name);
			}
		}
		return Names;
	}

	/** Friendly name (or a bare "VMI_<n>") for a live view mode value. */
	FString MCPViewportCtlViewModeName(int32 Value)
	{
		for (const FMCPViewportCtlViewModeEntry& Entry : MCPViewportCtlViewModes)
		{
			if (Entry.Value == Value) return Entry.Name;
		}
		return FString::Printf(TEXT("VMI_%d"), Value);
	}

	/** Resolve a caller-supplied view mode name. Accepts the friendly name, the
	 *  raw VMI_ spelling, and either with any casing. Returns INDEX_NONE on a
	 *  miss so the caller can build the listing error. */
	int32 MCPViewportCtlParseViewMode(const FString& Requested)
	{
		FString Wanted = Requested;
		Wanted.TrimStartAndEndInline();
		Wanted.RemoveFromStart(TEXT("VMI_"), ESearchCase::IgnoreCase);
		Wanted.ReplaceInline(TEXT("_"), TEXT(""), ESearchCase::CaseSensitive);
		Wanted.ReplaceInline(TEXT(" "), TEXT(""), ESearchCase::CaseSensitive);
		for (const FMCPViewportCtlViewModeEntry& Entry : MCPViewportCtlViewModes)
		{
			if (!MCPViewportCtlViewModeIsSupported(Entry.Value)) continue;
			if (Wanted.Equals(Entry.Name, ESearchCase::IgnoreCase)) return Entry.Value;
		}
		// The engine's own aliases for two of the entries, so a caller who read
		// the mode out of the editor UI is not told it does not exist.
		if (Wanted.Equals(TEXT("BrushWireframe"), ESearchCase::IgnoreCase)) return 0;
		if (Wanted.Equals(TEXT("LitDetailLighting"), ESearchCase::IgnoreCase)) return 4;
		if (Wanted.Equals(TEXT("Reflections"), ESearchCase::IgnoreCase)) return 11;
		if (Wanted.Equals(TEXT("BufferVisualization"), ESearchCase::IgnoreCase)) return 12;
		return INDEX_NONE;
	}

	struct FMCPViewportCtlViewportTypeEntry
	{
		const TCHAR* Name;
		ELevelViewportType Value;
	};

	const FMCPViewportCtlViewportTypeEntry MCPViewportCtlViewportTypes[] =
	{
		{ TEXT("Perspective"),   LVT_Perspective     },
		{ TEXT("Top"),           LVT_OrthoXY         },
		{ TEXT("Bottom"),        LVT_OrthoNegativeXY },
		{ TEXT("Left"),          LVT_OrthoXZ         },
		{ TEXT("Right"),         LVT_OrthoNegativeXZ },
		{ TEXT("Front"),         LVT_OrthoNegativeYZ },
		{ TEXT("Back"),          LVT_OrthoYZ         },
		{ TEXT("OrthoFreelook"), LVT_OrthoFreelook   },
	};

	FString MCPViewportCtlViewportTypeName(ELevelViewportType Value)
	{
		for (const FMCPViewportCtlViewportTypeEntry& Entry : MCPViewportCtlViewportTypes)
		{
			if (Entry.Value == Value) return Entry.Name;
		}
		return FString::Printf(TEXT("LVT_%d"), static_cast<int32>(Value));
	}

	bool MCPViewportCtlParseViewportType(const FString& Requested, ELevelViewportType& Out)
	{
		FString Wanted = Requested;
		Wanted.TrimStartAndEndInline();
		Wanted.RemoveFromStart(TEXT("LVT_"), ESearchCase::IgnoreCase);
		Wanted.RemoveFromStart(TEXT("Ortho"), ESearchCase::IgnoreCase);
		for (const FMCPViewportCtlViewportTypeEntry& Entry : MCPViewportCtlViewportTypes)
		{
			FString Candidate = Entry.Name;
			Candidate.RemoveFromStart(TEXT("Ortho"), ESearchCase::IgnoreCase);
			if (Wanted.Equals(Candidate, ESearchCase::IgnoreCase))
			{
				Out = Entry.Value;
				return true;
			}
		}
		return false;
	}

	FString MCPViewportCtlViewportTypeList()
	{
		TArray<FString> Names;
		for (const FMCPViewportCtlViewportTypeEntry& Entry : MCPViewportCtlViewportTypes)
		{
			Names.Add(Entry.Name);
		}
		return FString::Join(Names, TEXT(", "));
	}

	/** Which level viewport a call acts on, and how it was chosen. */
	struct FMCPViewportCtlTarget
	{
		FLevelEditorViewportClient* Client = nullptr;
		int32 Index = INDEX_NONE;
		int32 Count = 0;
		bool bIsCurrent = false;
		/** "viewportIndex", "active" or "firstAvailable". */
		const TCHAR* SelectedBy = TEXT("active");
	};

	/** Resolve the level viewport client this call targets.
	 *
	 *  Default is the viewport the editor considers active, falling back to the
	 *  first one, which is what every existing viewport handler does. An
	 *  explicit 'viewportIndex' overrides both, so a caller with a four-up
	 *  layout can pin exactly which pane it is configuring for a capture.
	 *
	 *  Returns nullptr with OutError set; the caller returns OutError unchanged. */
	FMCPViewportCtlTarget MCPViewportCtlResolveTarget(
		const TSharedPtr<FJsonObject>& Params,
		TSharedPtr<FJsonValue>& OutError)
	{
		OutError.Reset();
		FMCPViewportCtlTarget Target;

		if (!GEditor)
		{
			OutError = MCPError(TEXT("Editor not available: GEditor is null, so there is no level viewport to read or configure. This action needs a running editor; start one with editor(start_editor)."));
			return Target;
		}

		const TArray<FLevelEditorViewportClient*>& Clients = GEditor->GetLevelViewportClients();
		Target.Count = Clients.Num();

		int32 RequestedIndex = INDEX_NONE;
		if (Params.IsValid() && Params->HasField(TEXT("viewportIndex")))
		{
			RequestedIndex = OptionalInt(Params, TEXT("viewportIndex"), 0);
			if (!Clients.IsValidIndex(RequestedIndex))
			{
				OutError = MCPError(FString::Printf(
					TEXT("No level viewport at viewportIndex %d. The editor currently has %d level viewport client(s), so valid indices are 0..%d. Omit viewportIndex to target the active viewport."),
					RequestedIndex, Clients.Num(), Clients.Num() - 1));
				return Target;
			}
			Target.Client = Clients[RequestedIndex];
			Target.Index = RequestedIndex;
			Target.SelectedBy = TEXT("viewportIndex");
		}
		else if (GCurrentLevelEditingViewportClient)
		{
			Target.Client = GCurrentLevelEditingViewportClient;
			Target.SelectedBy = TEXT("active");
		}
		else if (Clients.Num() > 0)
		{
			Target.Client = Clients[0];
			Target.Index = 0;
			Target.SelectedBy = TEXT("firstAvailable");
		}

		if (!Target.Client)
		{
			OutError = MCPError(FString::Printf(
				TEXT("No level editor viewport client available. GEditor is running but GetLevelViewportClients() returned %d clients and GCurrentLevelEditingViewportClient is null. This happens when the editor is still starting up, when no level editor tab is open, or in a -nullrhi / commandlet session that never created a viewport."),
				Clients.Num()));
			return Target;
		}

		if (Target.Index == INDEX_NONE)
		{
			Target.Index = Clients.IndexOfByKey(Target.Client);
		}
		Target.bIsCurrent = (Target.Client == GCurrentLevelEditingViewportClient);
		return Target;
	}

	/** Stamp which viewport answered onto any result, so a caller can tell that
	 *  its four-up layout sent the write somewhere other than the pane it is
	 *  about to capture. */
	void MCPViewportCtlWriteTargetFields(const FMCPViewportCtlTarget& Target, TSharedPtr<FJsonObject> Result)
	{
		Result->SetNumberField(TEXT("viewportIndex"), Target.Index);
		Result->SetNumberField(TEXT("viewportCount"), Target.Count);
		Result->SetBoolField(TEXT("isActiveViewport"), Target.bIsCurrent);
		Result->SetStringField(TEXT("viewportSelectedBy"), Target.SelectedBy);
	}

	void MCPViewportCtlWriteVector(TSharedPtr<FJsonObject> Result, const TCHAR* Key, const FVector& Value)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("x"), Value.X);
		Obj->SetNumberField(TEXT("y"), Value.Y);
		Obj->SetNumberField(TEXT("z"), Value.Z);
		Result->SetObjectField(Key, Obj);
	}

	void MCPViewportCtlWriteRotator(TSharedPtr<FJsonObject> Result, const TCHAR* Key, const FRotator& Value)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("pitch"), Value.Pitch);
		Obj->SetNumberField(TEXT("yaw"), Value.Yaw);
		Obj->SetNumberField(TEXT("roll"), Value.Roll);
		Result->SetObjectField(Key, Obj);
	}

	void MCPViewportCtlWriteExposure(TSharedPtr<FJsonObject> Result, const FExposureSettings& Settings)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("fixed"), Settings.bFixed);
		Obj->SetNumberField(TEXT("ev100"), Settings.FixedEV100);
		Obj->SetStringField(TEXT("mode"), Settings.bFixed ? TEXT("fixed") : TEXT("auto"));
		Result->SetObjectField(TEXT("exposure"), Obj);
	}

	/** The complete viewport readout, shared by get_viewport_state and by every
	 *  setter so a write answers with the state it produced rather than making
	 *  the caller ask again. */
	void MCPViewportCtlWriteState(FLevelEditorViewportClient* Client, TSharedPtr<FJsonObject> Result)
	{
		if (!Client) return;

		const int32 ViewModeValue = static_cast<int32>(Client->GetViewMode());
		Result->SetStringField(TEXT("viewMode"), MCPViewportCtlViewModeName(ViewModeValue));
		Result->SetNumberField(TEXT("viewModeIndex"), ViewModeValue);

		const ELevelViewportType Type = Client->GetViewportType();
		Result->SetStringField(TEXT("viewportType"), MCPViewportCtlViewportTypeName(Type));
		Result->SetNumberField(TEXT("viewportTypeIndex"), static_cast<int32>(Type));
		Result->SetBoolField(TEXT("perspective"), Type == LVT_Perspective);

		Result->SetNumberField(TEXT("fov"), Client->ViewFOV);
		Result->SetNumberField(TEXT("nearClip"), Client->GetNearClipPlane());
		const float FarOverride = Client->GetFarClipPlaneOverride();
		Result->SetNumberField(TEXT("farClipOverride"), FarOverride);
		Result->SetBoolField(TEXT("farClipOverridden"), FarOverride >= 0.0f);

		MCPViewportCtlWriteExposure(Result, Client->ExposureSettings);

		Result->SetNumberField(TEXT("cameraSpeed"), Client->GetCameraSpeed());
		Result->SetBoolField(TEXT("gameView"), Client->IsInGameView());
		Result->SetBoolField(TEXT("realtime"), Client->IsRealtime());
		Result->SetBoolField(TEXT("realtimeOverridden"), Client->IsRealtimeOverrideSet());

		MCPViewportCtlWriteVector(Result, TEXT("location"), Client->GetViewLocation());
		MCPViewportCtlWriteRotator(Result, TEXT("rotation"), Client->GetViewRotation());
	}

	// ── Transactions ─────────────────────────────────────────────────────────

	/** The undo buffer, or nullptr with OutError set.
	 *
	 *  Everything below goes through the UTransactor interface rather than
	 *  casting to UTransBuffer. UTransBuffer is MinimalAPI and its queue is
	 *  reachable entirely through virtuals the base class already declares, so
	 *  the base pointer is both sufficient and the one that keeps working if a
	 *  build swaps the transactor implementation. */
	UTransactor* MCPViewportCtlRequireTransactor(TSharedPtr<FJsonValue>& OutError)
	{
		OutError.Reset();
		if (!GEditor)
		{
			OutError = MCPError(TEXT("Editor not available: GEditor is null, so there is no undo buffer. This action needs a running editor; start one with editor(start_editor)."));
			return nullptr;
		}
		if (!GEditor->Trans)
		{
			OutError = MCPError(TEXT("No transaction buffer: GEditor->Trans is null. The editor was started without an undo buffer (a commandlet or -notransactions session), so transactions and undo/redo are unavailable in this process."));
			return nullptr;
		}
		return GEditor->Trans;
	}

	/** Description of the transaction that the next undo would reverse, or an
	 *  empty string when there is nothing to reverse. */
	FString MCPViewportCtlUndoDescription(UTransactor* Trans)
	{
		if (!Trans) return FString();
		const FTransactionContext Context = Trans->GetUndoContext();
		return Context.Title.ToString();
	}

	/** Description of the transaction that the next redo would reapply. */
	FString MCPViewportCtlRedoDescription(UTransactor* Trans)
	{
		if (!Trans) return FString();
		const FTransactionContext Context = Trans->GetRedoContext();
		return Context.Title.ToString();
	}

	/** canUndo / canRedo plus the descriptions and, when a direction is
	 *  blocked, the engine's own reason for blocking it. */
	void MCPViewportCtlWriteUndoState(UTransactor* Trans, TSharedPtr<FJsonObject> Result)
	{
		if (!Trans) return;

		FText UndoBlockedReason;
		FText RedoBlockedReason;
		const bool bCanUndo = Trans->CanUndo(&UndoBlockedReason);
		const bool bCanRedo = Trans->CanRedo(&RedoBlockedReason);

		Result->SetBoolField(TEXT("canUndo"), bCanUndo);
		Result->SetBoolField(TEXT("canRedo"), bCanRedo);
		Result->SetStringField(TEXT("undoDescription"), bCanUndo ? MCPViewportCtlUndoDescription(Trans) : FString());
		Result->SetStringField(TEXT("redoDescription"), bCanRedo ? MCPViewportCtlRedoDescription(Trans) : FString());
		if (!bCanUndo && !UndoBlockedReason.IsEmpty())
		{
			Result->SetStringField(TEXT("undoBlockedReason"), UndoBlockedReason.ToString());
		}
		if (!bCanRedo && !RedoBlockedReason.IsEmpty())
		{
			Result->SetStringField(TEXT("redoBlockedReason"), RedoBlockedReason.ToString());
		}

		const int32 QueueLength = Trans->GetQueueLength();
		const int32 UndoCount = Trans->GetUndoCount();
		Result->SetNumberField(TEXT("queueLength"), QueueLength);
		Result->SetNumberField(TEXT("undoCount"), UndoCount);
		// Everything before this index has been applied; everything from it on
		// has been undone and is waiting to be redone.
		Result->SetNumberField(TEXT("currentIndex"), QueueLength - UndoCount);
		Result->SetNumberField(TEXT("undoableSteps"), QueueLength - UndoCount);
		Result->SetNumberField(TEXT("redoableSteps"), UndoCount);
		Result->SetBoolField(TEXT("transactionActive"), Trans->IsActive());
	}
}

// ---------------------------------------------------------------------------
// get_viewport_state -- the full readout. get_viewport_info answers location,
// rotation and FOV only, which is not enough to tell whether two captures were
// taken under the same conditions.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::GetViewportState(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonValue> Error;
	const FMCPViewportCtlTarget Target = MCPViewportCtlResolveTarget(Params, Error);
	if (!Target.Client) return Error;

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPViewportCtlWriteTargetFields(Target, Result);
	MCPViewportCtlWriteState(Target.Client, Result);
	Result->SetArrayField(TEXT("supportedViewModes"),
		MCPStringListToJson(MCPViewportCtlSupportedViewModeNames()));
	Result->SetStringField(TEXT("determinismNote"),
		TEXT("For screenshot diffing, pin viewMode with editor(set_view_mode) and exposure with editor(set_viewport_exposure) (fixed EV100). With exposure.fixed false the world's auto-exposure adapts between runs and two captures of the same scene will not match."));
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_view_mode -- Lit / Unlit / Wireframe / LightingOnly / ... The single
// biggest determinism lever for a visual diff: Unlit removes lighting from the
// comparison entirely, and Wireframe removes shading.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::SetViewMode(const TSharedPtr<FJsonObject>& Params)
{
	FString Requested;
	if (TSharedPtr<FJsonValue> Missing = RequireString(Params, TEXT("viewMode"), Requested))
	{
		return Missing;
	}

	TSharedPtr<FJsonValue> Error;
	const FMCPViewportCtlTarget Target = MCPViewportCtlResolveTarget(Params, Error);
	if (!Target.Client) return Error;

	const int32 Wanted = MCPViewportCtlParseViewMode(Requested);
	if (Wanted == INDEX_NONE)
	{
		const TArray<FString> Valid = MCPViewportCtlSupportedViewModeNames();
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Unknown view mode '%s'. This engine build supports: %s. Names are case-insensitive and the raw VMI_ spelling is accepted too."),
			*Requested, *FString::Join(Valid, TEXT(", "))));
		Obj->SetStringField(TEXT("requested"), Requested);
		Obj->SetArrayField(TEXT("supportedViewModes"), MCPStringListToJson(Valid));
		return MakeShared<FJsonValueObject>(Obj);
	}

	const int32 Previous = static_cast<int32>(Target.Client->GetViewMode());
	const bool bUnchanged = (Previous == Wanted);
	if (!bUnchanged)
	{
		Target.Client->SetViewMode(static_cast<EViewModeIndex>(Wanted));
		Target.Client->Invalidate();
	}

	// The engine may refuse a mode it cannot honour on this viewport type, so
	// report what actually took rather than what was asked for.
	const int32 Applied = static_cast<int32>(Target.Client->GetViewMode());

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPViewportCtlWriteTargetFields(Target, Result);
	Result->SetStringField(TEXT("requestedViewMode"), MCPViewportCtlViewModeName(Wanted));
	Result->SetStringField(TEXT("previousViewMode"), MCPViewportCtlViewModeName(Previous));
	Result->SetBoolField(TEXT("unchanged"), bUnchanged);
	Result->SetBoolField(TEXT("applied"), Applied == Wanted);
	if (Applied != Wanted)
	{
		Result->SetStringField(TEXT("note"), FString::Printf(
			TEXT("The viewport reports '%s' after the write, not the requested '%s'. Some view modes are rejected on an orthographic viewport or when the required renderer feature is off."),
			*MCPViewportCtlViewModeName(Applied), *MCPViewportCtlViewModeName(Wanted)));
	}
	if (!bUnchanged) MCPSetUpdated(Result);
	MCPViewportCtlWriteState(Target.Client, Result);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("viewMode"), MCPViewportCtlViewModeName(Previous));
	if (FCString::Strcmp(Target.SelectedBy, TEXT("viewportIndex")) == 0)
	{
		Payload->SetNumberField(TEXT("viewportIndex"), Target.Index);
	}
	MCPSetRollback(Result, TEXT("set_view_mode"), Payload);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_viewport_exposure -- fixed EV100 override vs auto eye adaptation, on the
// VIEWPORT CLIENT. This is deliberately not a PostProcessVolume edit: volume
// exposure settings are real UPROPERTYs that asset/level set_property already
// reaches, they are saved into the level, and they change what the game sees.
// FEditorViewportClient::ExposureSettings is editor-viewport-only, transient,
// and is what makes two editor captures comparable without touching content.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::SetViewportExposure(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonValue> Error;
	const FMCPViewportCtlTarget Target = MCPViewportCtlResolveTarget(Params, Error);
	if (!Target.Client) return Error;

	const bool bHasFixed = Params.IsValid() && Params->HasField(TEXT("fixed"));
	const bool bHasEv100 = Params.IsValid() && Params->HasField(TEXT("ev100"));
	FString Mode = OptionalString(Params, TEXT("mode"));
	Mode.TrimStartAndEndInline();

	if (!bHasFixed && !bHasEv100 && Mode.IsEmpty())
	{
		return MCPError(TEXT("Nothing to set. Pass ev100 (a fixed EV100 value, which implies fixed exposure), or fixed (true/false), or mode ('fixed'|'auto'). Read the current setting with editor(get_viewport_state)."));
	}

	const FExposureSettings PreviousSettings = Target.Client->ExposureSettings;

	bool bWantFixed = PreviousSettings.bFixed;
	if (!Mode.IsEmpty())
	{
		if (Mode.Equals(TEXT("fixed"), ESearchCase::IgnoreCase)) bWantFixed = true;
		else if (Mode.Equals(TEXT("auto"), ESearchCase::IgnoreCase)) bWantFixed = false;
		else
		{
			return MCPError(FString::Printf(
				TEXT("Unknown exposure mode '%s'. Valid values are 'fixed' (pin EV100, the setting that makes captures comparable between runs) and 'auto' (the world's own eye adaptation)."),
				*Mode));
		}
	}
	if (bHasFixed) bWantFixed = OptionalBool(Params, TEXT("fixed"), bWantFixed);
	// An EV100 with no explicit mode is only meaningful as a fixed exposure.
	if (bHasEv100 && !bHasFixed && Mode.IsEmpty()) bWantFixed = true;

	const float WantEv100 = bHasEv100
		? static_cast<float>(OptionalNumber(Params, TEXT("ev100"), PreviousSettings.FixedEV100))
		: PreviousSettings.FixedEV100;

	const bool bUnchanged =
		(PreviousSettings.bFixed == bWantFixed) &&
		(FMath::IsNearlyEqual(PreviousSettings.FixedEV100, WantEv100));

	if (!bUnchanged)
	{
		Target.Client->ExposureSettings.bFixed = bWantFixed;
		Target.Client->ExposureSettings.FixedEV100 = WantEv100;
		Target.Client->Invalidate();
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPViewportCtlWriteTargetFields(Target, Result);
	Result->SetBoolField(TEXT("unchanged"), bUnchanged);
	if (!bUnchanged) MCPSetUpdated(Result);

	TSharedPtr<FJsonObject> PreviousObj = MakeShared<FJsonObject>();
	PreviousObj->SetBoolField(TEXT("fixed"), PreviousSettings.bFixed);
	PreviousObj->SetNumberField(TEXT("ev100"), PreviousSettings.FixedEV100);
	PreviousObj->SetStringField(TEXT("mode"), PreviousSettings.bFixed ? TEXT("fixed") : TEXT("auto"));
	Result->SetObjectField(TEXT("previousExposure"), PreviousObj);

	MCPViewportCtlWriteState(Target.Client, Result);
	if (!bWantFixed)
	{
		Result->SetStringField(TEXT("note"),
			TEXT("Exposure is now automatic, so the viewport adapts to scene brightness over time and two captures of the same scene may differ. Set mode 'fixed' with an ev100 before a capture you intend to diff."));
	}

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetBoolField(TEXT("fixed"), PreviousSettings.bFixed);
	Payload->SetNumberField(TEXT("ev100"), PreviousSettings.FixedEV100);
	if (FCString::Strcmp(Target.SelectedBy, TEXT("viewportIndex")) == 0)
	{
		Payload->SetNumberField(TEXT("viewportIndex"), Target.Index);
	}
	MCPSetRollback(Result, TEXT("set_viewport_exposure"), Payload);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_viewport_view -- fov, near/far clip, viewport type and camera speed in
// one call. set_viewport_camera writes location and rotation and does not even
// write back the FOV it reads.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::SetViewportView(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonValue> Error;
	const FMCPViewportCtlTarget Target = MCPViewportCtlResolveTarget(Params, Error);
	if (!Target.Client) return Error;

	const bool bHasFov = Params.IsValid() && Params->HasField(TEXT("fov"));
	const bool bHasNear = Params.IsValid() && Params->HasField(TEXT("nearClip"));
	const bool bHasFar = Params.IsValid() && Params->HasField(TEXT("farClip"));
	const bool bHasType = Params.IsValid() && Params->HasField(TEXT("viewportType"));
	const bool bHasSpeed = Params.IsValid() && Params->HasField(TEXT("cameraSpeed"));

	if (!bHasFov && !bHasNear && !bHasFar && !bHasType && !bHasSpeed)
	{
		return MCPError(TEXT("Nothing to set. Pass at least one of fov, nearClip, farClip, viewportType, cameraSpeed. Read the current values with editor(get_viewport_state)."));
	}

	const float PreviousFov = Target.Client->ViewFOV;
	const float PreviousNear = Target.Client->GetNearClipPlane();
	const float PreviousFar = Target.Client->GetFarClipPlaneOverride();
	const ELevelViewportType PreviousType = Target.Client->GetViewportType();
	const float PreviousSpeed = Target.Client->GetCameraSpeed();

	TSharedPtr<FJsonObject> Changed = MakeShared<FJsonObject>();
	bool bAnyChanged = false;
	FString CameraSpeedClampNote;

	if (bHasFov)
	{
		const float Wanted = static_cast<float>(OptionalNumber(Params, TEXT("fov"), PreviousFov));
		if (Wanted <= 0.0f || Wanted >= 180.0f)
		{
			return MCPError(FString::Printf(
				TEXT("fov %.3f is out of range. A perspective field of view must be greater than 0 and less than 180 degrees; the editor default is 90."),
				Wanted));
		}
		const bool bFovChanged = !FMath::IsNearlyEqual(PreviousFov, Wanted);
		if (bFovChanged)
		{
			Target.Client->ViewFOV = Wanted;
			bAnyChanged = true;
		}
		Changed->SetBoolField(TEXT("fov"), bFovChanged);
	}

	if (bHasNear)
	{
		const float Wanted = static_cast<float>(OptionalNumber(Params, TEXT("nearClip"), PreviousNear));
		const bool bNearChanged = !FMath::IsNearlyEqual(PreviousNear, Wanted);
		if (bNearChanged)
		{
			// A negative value clears the override and returns the viewport to
			// the engine-global near plane; the engine documents that contract.
			Target.Client->OverrideNearClipPlane(Wanted);
			bAnyChanged = true;
		}
		Changed->SetBoolField(TEXT("nearClip"), bNearChanged);
	}

	if (bHasFar)
	{
		const float Wanted = static_cast<float>(OptionalNumber(Params, TEXT("farClip"), PreviousFar));
		const bool bFarChanged = !FMath::IsNearlyEqual(PreviousFar, Wanted);
		if (bFarChanged)
		{
			Target.Client->OverrideFarClipPlane(Wanted);
			bAnyChanged = true;
		}
		Changed->SetBoolField(TEXT("farClip"), bFarChanged);
	}

	if (bHasType)
	{
		const FString Requested = OptionalString(Params, TEXT("viewportType"));
		ELevelViewportType Wanted = PreviousType;
		if (!MCPViewportCtlParseViewportType(Requested, Wanted))
		{
			return MCPError(FString::Printf(
				TEXT("Unknown viewportType '%s'. Valid values are: %s. Names are case-insensitive and the raw LVT_ spellings (LVT_Perspective, LVT_OrthoXY, ...) are accepted too."),
				*Requested, *MCPViewportCtlViewportTypeList()));
		}
		const bool bTypeChanged = (Wanted != PreviousType);
		if (bTypeChanged)
		{
			Target.Client->SetViewportType(Wanted);
			bAnyChanged = true;
		}
		Changed->SetBoolField(TEXT("viewportType"), bTypeChanged);
	}

	if (bHasSpeed)
	{
		const float Wanted = static_cast<float>(OptionalNumber(Params, TEXT("cameraSpeed"), PreviousSpeed));
		if (Wanted <= 0.0f)
		{
			return MCPError(FString::Printf(
				TEXT("cameraSpeed %.4f is out of range. Viewport camera speed must be greater than 0; the value is clamped to the viewport's own min/max, which editor(get_viewport_state) reports as cameraSpeed."),
				Wanted));
		}
		const bool bSpeedChanged = !FMath::IsNearlyEqual(PreviousSpeed, Wanted);
		if (bSpeedChanged)
		{
#if UE_MCP_HAS_5_7_API
			FEditorViewportCameraSpeedSettings Settings = Target.Client->GetCameraSpeedSettings();
			Settings.SetCurrentSpeed(Wanted);
			Target.Client->SetCameraSpeedSettings(Settings);
#else
			// Before 5.7 the effective speed is the discrete speed setting times
			// a scalar, and only the scalar is freely settable. Scaling it by the
			// requested/current ratio lands on the requested speed without
			// disturbing which speed setting the user picked in the UI.
			const float PreviousScalar = Target.Client->GetCameraSpeedScalar();
			if (PreviousSpeed > 0.0f && PreviousScalar > 0.0f)
			{
				Target.Client->SetCameraSpeedScalar(PreviousScalar * (Wanted / PreviousSpeed));
			}
#endif
			bAnyChanged = true;
			// The viewport clamps to its own min/max, so say when the number
			// that took is not the number that was asked for.
			const float AppliedSpeed = Target.Client->GetCameraSpeed();
			if (!FMath::IsNearlyEqual(AppliedSpeed, Wanted))
			{
				CameraSpeedClampNote = FString::Printf(
					TEXT("cameraSpeed %.4f was clamped to %.4f by the viewport's own speed range."),
					Wanted, AppliedSpeed);
			}
		}
		Changed->SetBoolField(TEXT("cameraSpeed"), bSpeedChanged);
	}

	if (bAnyChanged) Target.Client->Invalidate();

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPViewportCtlWriteTargetFields(Target, Result);
	Result->SetObjectField(TEXT("changedFields"), Changed);
	Result->SetBoolField(TEXT("unchanged"), !bAnyChanged);
	if (bAnyChanged) MCPSetUpdated(Result);

	TSharedPtr<FJsonObject> PreviousObj = MakeShared<FJsonObject>();
	PreviousObj->SetNumberField(TEXT("fov"), PreviousFov);
	PreviousObj->SetNumberField(TEXT("nearClip"), PreviousNear);
	PreviousObj->SetNumberField(TEXT("farClip"), PreviousFar);
	PreviousObj->SetStringField(TEXT("viewportType"), MCPViewportCtlViewportTypeName(PreviousType));
	PreviousObj->SetNumberField(TEXT("cameraSpeed"), PreviousSpeed);
	Result->SetObjectField(TEXT("previous"), PreviousObj);

	MCPViewportCtlWriteState(Target.Client, Result);
	if (!CameraSpeedClampNote.IsEmpty())
	{
		Result->SetStringField(TEXT("cameraSpeedNote"), CameraSpeedClampNote);
	}
	if (bHasNear)
	{
		Result->SetStringField(TEXT("nearClipNote"),
			TEXT("nearClip reports the EFFECTIVE near plane. With no override installed the viewport reports the engine-global value, so the rollback record restores that number as an explicit override rather than clearing the override. Pass a negative nearClip to clear it outright."));
	}

	// Restore every field this call could have touched, not only the ones it
	// changed: a single inverse call has to put the viewport back exactly.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	if (bHasFov) Payload->SetNumberField(TEXT("fov"), PreviousFov);
	if (bHasNear) Payload->SetNumberField(TEXT("nearClip"), PreviousNear);
	if (bHasFar) Payload->SetNumberField(TEXT("farClip"), PreviousFar);
	if (bHasType) Payload->SetStringField(TEXT("viewportType"), MCPViewportCtlViewportTypeName(PreviousType));
	if (bHasSpeed) Payload->SetNumberField(TEXT("cameraSpeed"), PreviousSpeed);
	if (FCString::Strcmp(Target.SelectedBy, TEXT("viewportIndex")) == 0)
	{
		Payload->SetNumberField(TEXT("viewportIndex"), Target.Index);
	}
	MCPSetRollback(Result, TEXT("set_viewport_view"), Payload);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_game_view -- hide the editor-only overlays (grid, gizmos, actor icons,
// volume wireframes) so a capture shows what the game shows.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::SetGameView(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonValue> Error;
	const FMCPViewportCtlTarget Target = MCPViewportCtlResolveTarget(Params, Error);
	if (!Target.Client) return Error;

	const bool bWanted = OptionalBool(Params, TEXT("enabled"), true);
	const bool bPrevious = Target.Client->IsInGameView();
	const bool bUnchanged = (bPrevious == bWanted);

	if (!bUnchanged)
	{
		Target.Client->SetGameView(bWanted);
		Target.Client->Invalidate();
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPViewportCtlWriteTargetFields(Target, Result);
	Result->SetBoolField(TEXT("enabled"), Target.Client->IsInGameView());
	Result->SetBoolField(TEXT("previousEnabled"), bPrevious);
	Result->SetBoolField(TEXT("unchanged"), bUnchanged);
	if (!bUnchanged) MCPSetUpdated(Result);
	MCPViewportCtlWriteState(Target.Client, Result);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetBoolField(TEXT("enabled"), bPrevious);
	if (FCString::Strcmp(Target.SelectedBy, TEXT("viewportIndex")) == 0)
	{
		Payload->SetNumberField(TEXT("viewportIndex"), Target.Index);
	}
	MCPSetRollback(Result, TEXT("set_game_view"), Payload);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// redraw_viewport -- force the redraw the capture path already did privately.
// A property write made through the bridge marks the viewport dirty but does
// not repaint it, so a capture taken immediately afterwards can show the state
// before the write.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::RedrawViewport(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonValue> Error;
	const FMCPViewportCtlTarget Target = MCPViewportCtlResolveTarget(Params, Error);
	if (!Target.Client) return Error;

	const bool bAll = OptionalBool(Params, TEXT("allViewports"), false);
	const bool bInvalidateHitProxies = OptionalBool(Params, TEXT("invalidateHitProxies"), true);

	int32 Redrawn = 1;
	if (bAll)
	{
		GEditor->RedrawAllViewports(bInvalidateHitProxies);
		GEditor->RedrawLevelEditingViewports(bInvalidateHitProxies);
		Redrawn = Target.Count;
	}
	else
	{
		Target.Client->Invalidate(true, bInvalidateHitProxies);
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPViewportCtlWriteTargetFields(Target, Result);
	Result->SetBoolField(TEXT("allViewports"), bAll);
	Result->SetBoolField(TEXT("invalidateHitProxies"), bInvalidateHitProxies);
	Result->SetNumberField(TEXT("viewportsInvalidated"), Redrawn);
	Result->SetStringField(TEXT("note"),
		TEXT("The viewport is marked for redraw; the renderer paints it on the next editor tick. A non-realtime viewport draws exactly one frame from this. Use editor(set_realtime) when a ticking simulation has to advance as well."));
	// Classified a mutation, and defensibly so: it drives the renderer and
	// invalidates hit proxies, which is engine work rather than a query. What
	// it does not do is write state, so the did-anything-change answer is "no":
	// the viewport shows the same scene before and after, only sooner. Unlike
	// hit_test_viewport_pixel this is not reported as misclassified.
	Result->SetBoolField(TEXT("changed"), false);
	Result->SetBoolField(TEXT("unchanged"), true);
	// No rollback: a redraw changes no state, and repeating it is a no-op.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Nothing to undo. This asks the renderer to paint sooner than it otherwise would and writes no viewport, world or asset state."));
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// begin_editor_transaction -- general purpose, not material-specific. Groups
// every write until the matching end into one undo step.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::BeginEditorTransaction(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonValue> Error;
	UTransactor* Trans = MCPViewportCtlRequireTransactor(Error);
	if (!Trans) return Error;

	FString Description = OptionalString(Params, TEXT("description"));
	if (Description.IsEmpty()) Description = OptionalString(Params, TEXT("label"));
	if (Description.IsEmpty()) Description = TEXT("MCP Edit");

	const bool bWasActive = Trans->IsActive();
	const int32 Index = GEditor->BeginTransaction(TEXT("UE_MCP_Bridge"), FText::FromString(Description), nullptr);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("description"), Description);
	Result->SetNumberField(TEXT("transactionIndex"), Index);
	// BeginTransaction returns the pre-existing nesting depth, so a non-zero
	// index means this call nested inside a transaction someone else opened.
	Result->SetBoolField(TEXT("nested"), bWasActive);
	// Opening always changes state, nested or not: there is one more open
	// transaction than there was, and nothing here can find that already done
	// and skip it. `nested` answers a different question and is not a stand-in.
	Result->SetBoolField(TEXT("changed"), true);
	if (bWasActive)
	{
		Result->SetStringField(TEXT("note"),
			TEXT("A transaction was already open, so this one nested inside it. Only the outermost end_editor_transaction closes the undo step; cancel_editor_transaction with index 0 aborts the whole nest."));
	}
	MCPViewportCtlWriteUndoState(Trans, Result);

	// The inverse of opening a transaction is aborting it, which is the whole
	// point of having a cancel: a flow that fails after this call rolls back to
	// the state before it rather than committing half an edit.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetNumberField(TEXT("index"), Index);
	MCPSetRollback(Result, TEXT("cancel_editor_transaction"), Payload);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// end_editor_transaction -- commit the open transaction as one undo step.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::EndEditorTransaction(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonValue> Error;
	UTransactor* Trans = MCPViewportCtlRequireTransactor(Error);
	if (!Trans) return Error;

	if (!Trans->IsActive())
	{
		// Not an error: ending when nothing is open is the idempotent replay of
		// a flow that already ended, and failing it would turn a retry into a
		// hard stop.
		TSharedPtr<FJsonObject> NoOp = MCPSuccess();
		NoOp->SetBoolField(TEXT("wasActive"), false);
		NoOp->SetBoolField(TEXT("committed"), false);
		NoOp->SetBoolField(TEXT("unchanged"), true);
		NoOp->SetStringField(TEXT("note"),
			TEXT("No transaction was open, so nothing was committed. Open one with editor(begin_editor_transaction) before the writes that should undo together."));
		MCPViewportCtlWriteUndoState(Trans, NoOp);
		return MCPResult(NoOp);
	}

	const FString Description = MCPViewportCtlUndoDescription(Trans);
	const int32 Index = GEditor->EndTransaction();

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetBoolField(TEXT("wasActive"), true);
	Result->SetBoolField(TEXT("committed"), true);
	Result->SetBoolField(TEXT("unchanged"), false);
	Result->SetNumberField(TEXT("transactionIndex"), Index);
	Result->SetBoolField(TEXT("stillNested"), Trans->IsActive());
	MCPSetUpdated(Result);
	MCPViewportCtlWriteUndoState(Trans, Result);
	if (Trans->IsActive())
	{
		Result->SetStringField(TEXT("note"),
			TEXT("This closed one nesting level and an outer transaction is still open. The undo step is not recorded until the outermost end."));
	}

	// The inverse of committing is undoing the step that was just recorded.
	// Only meaningful once the nest is fully closed, which is when the step
	// actually exists in the buffer.
	if (!Trans->IsActive())
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetNumberField(TEXT("steps"), 1);
		Payload->SetStringField(TEXT("direction"), TEXT("undo"));
		MCPSetRollback(Result, TEXT("undo_redo_steps"), Payload);
		Result->SetStringField(TEXT("committedDescription"), Description);
	}
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// cancel_editor_transaction -- the piece that was missing. Discards the open
// transaction and restores every object it touched, so "fail partway, cancel,
// state unchanged" is possible at all. Without it an aborted flow could only
// commit its half-finished edit.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::CancelEditorTransaction(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonValue> Error;
	UTransactor* Trans = MCPViewportCtlRequireTransactor(Error);
	if (!Trans) return Error;

	if (!Trans->IsActive())
	{
		// Reported, not errored. A rollback runs after a failure whose cause may
		// itself have been "the transaction never opened", and an error here
		// would bury the real one.
		TSharedPtr<FJsonObject> NoOp = MCPSuccess();
		NoOp->SetBoolField(TEXT("wasActive"), false);
		NoOp->SetBoolField(TEXT("cancelled"), false);
		NoOp->SetBoolField(TEXT("unchanged"), true);
		NoOp->SetStringField(TEXT("note"),
			TEXT("No transaction was open, so nothing was cancelled and no state changed. This is the expected answer when a flow rolls back past a transaction it had already ended."));
		NoOp->SetBoolField(TEXT("changed"), false);
		// Same answer as the active branch below, stated on this path too: a
		// cancel has no inverse, and this one did not even have anything to
		// cancel.
		NoOp->SetBoolField(TEXT("rollbackPossible"), false);
		NoOp->SetStringField(TEXT("rollbackNote"),
			TEXT("Nothing was cancelled, so there is nothing to restore."));
		MCPViewportCtlWriteUndoState(Trans, NoOp);
		return MCPResult(NoOp);
	}

	const FString Description = MCPViewportCtlUndoDescription(Trans);
	const int32 CancelIndex = OptionalInt(Params, TEXT("index"), 0);
	if (CancelIndex < 0)
	{
		return MCPError(FString::Printf(
			TEXT("index %d is invalid. Pass 0 (the default) to abort the whole transaction including any nesting, or the transactionIndex that begin_editor_transaction returned to unwind back to that nesting level."),
			CancelIndex));
	}

	GEditor->CancelTransaction(CancelIndex);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetBoolField(TEXT("wasActive"), true);
	Result->SetBoolField(TEXT("cancelled"), true);
	Result->SetBoolField(TEXT("unchanged"), false);
	Result->SetNumberField(TEXT("index"), CancelIndex);
	Result->SetStringField(TEXT("cancelledDescription"), Description);
	Result->SetBoolField(TEXT("stillActive"), Trans->IsActive());
	MCPSetUpdated(Result);
	MCPViewportCtlWriteUndoState(Trans, Result);
	Result->SetStringField(TEXT("note"),
		TEXT("Every object the transaction recorded was restored and no undo step was added. A cancel is itself the rollback, so there is nothing to reverse afterwards."));
	// No rollback: this IS the inverse operation. A discarded transaction leaves
	// no record to reapply.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("A cancel is itself an inverse, and it consumes what it undid: CancelTransaction restores the recorded objects and discards the buffer, so the edits it reverted no longer exist anywhere to be reapplied. Reopening a transaction with begin_editor_transaction starts an empty one."));
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// get_undo_state -- canUndo/canRedo plus the DESCRIPTIONS, so a caller can see
// what an undo would reverse before doing it. editor(undo) previously answered
// a lone success bool and there was no way to look first.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::GetUndoState(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonValue> Error;
	UTransactor* Trans = MCPViewportCtlRequireTransactor(Error);
	if (!Trans) return Error;

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPViewportCtlWriteUndoState(Trans, Result);
	Result->SetNumberField(TEXT("undoSizeBytes"), static_cast<double>(Trans->GetUndoSize()));
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// undo_redo_steps -- multi-step undo/redo that reports the descriptions it
// actually applied, so a caller learns what it reversed rather than counting
// bare booleans.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::UndoRedoSteps(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonValue> Error;
	UTransactor* Trans = MCPViewportCtlRequireTransactor(Error);
	if (!Trans) return Error;

	FString Direction = OptionalString(Params, TEXT("direction"), TEXT("undo"));
	Direction.TrimStartAndEndInline();
	const bool bUndo = Direction.Equals(TEXT("undo"), ESearchCase::IgnoreCase);
	const bool bRedo = Direction.Equals(TEXT("redo"), ESearchCase::IgnoreCase);
	if (!bUndo && !bRedo)
	{
		return MCPError(FString::Printf(
			TEXT("Unknown direction '%s'. Valid values are 'undo' (reverse applied transactions) and 'redo' (reapply transactions that were undone). editor(get_undo_state) reports how many steps each direction has."),
			*Direction));
	}

	const int32 RequestedSteps = OptionalInt(Params, TEXT("steps"), 1);
	if (RequestedSteps < 1)
	{
		return MCPError(FString::Printf(
			TEXT("steps must be 1 or greater, got %d. editor(get_undo_state) reports undoableSteps and redoableSteps for the maximum that will do anything."),
			RequestedSteps));
	}

	if (Trans->IsActive())
	{
		return MCPError(TEXT("A transaction is currently open, so the undo buffer cannot be moved. Close it with editor(end_editor_transaction) or discard it with editor(cancel_editor_transaction) first."));
	}

	const int32 AvailableBefore = bUndo
		? (Trans->GetQueueLength() - Trans->GetUndoCount())
		: Trans->GetUndoCount();

	TArray<FString> Applied;
	FString StoppedReason;
	for (int32 Step = 0; Step < RequestedSteps; ++Step)
	{
		FText BlockedReason;
		const bool bCan = bUndo ? Trans->CanUndo(&BlockedReason) : Trans->CanRedo(&BlockedReason);
		if (!bCan)
		{
			StoppedReason = BlockedReason.IsEmpty()
				? FString(TEXT("nothing left to apply in that direction"))
				: BlockedReason.ToString();
			break;
		}

		// Read the description BEFORE applying: afterwards it names a different
		// transaction, which is why a bare bool return could never say what it
		// had just reversed.
		const FString Description = bUndo
			? MCPViewportCtlUndoDescription(Trans)
			: MCPViewportCtlRedoDescription(Trans);

		const bool bOk = bUndo ? GEditor->UndoTransaction(true) : GEditor->RedoTransaction();
		if (!bOk)
		{
			StoppedReason = FString::Printf(TEXT("the engine refused step %d ('%s')"), Step + 1, *Description);
			break;
		}
		Applied.Add(Description);
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("direction"), bUndo ? TEXT("undo") : TEXT("redo"));
	Result->SetNumberField(TEXT("requestedSteps"), RequestedSteps);
	Result->SetNumberField(TEXT("appliedSteps"), Applied.Num());
	Result->SetNumberField(TEXT("availableStepsBefore"), AvailableBefore);
	Result->SetArrayField(TEXT("appliedDescriptions"), MCPStringListToJson(Applied));
	Result->SetBoolField(TEXT("unchanged"), Applied.Num() == 0);
	if (Applied.Num() > 0) MCPSetUpdated(Result);
	if (!StoppedReason.IsEmpty())
	{
		Result->SetBoolField(TEXT("stoppedEarly"), true);
		Result->SetStringField(TEXT("stoppedReason"), FString::Printf(
			TEXT("Applied %d of %d requested steps, then stopped: %s."),
			Applied.Num(), RequestedSteps, *StoppedReason));
	}
	MCPViewportCtlWriteUndoState(Trans, Result);

	// The inverse is the same count in the opposite direction. Only for the
	// steps that actually landed.
	if (Applied.Num() > 0)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetNumberField(TEXT("steps"), Applied.Num());
		Payload->SetStringField(TEXT("direction"), bUndo ? TEXT("redo") : TEXT("undo"));
		MCPSetRollback(Result, TEXT("undo_redo_steps"), Payload);
	}
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// get_transaction_history -- the undo queue itself, with the index that splits
// applied from undone.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::GetTransactionHistory(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonValue> Error;
	UTransactor* Trans = MCPViewportCtlRequireTransactor(Error);
	if (!Trans) return Error;

	const int32 QueueLength = Trans->GetQueueLength();
	const int32 UndoCount = Trans->GetUndoCount();
	const int32 CurrentIndex = QueueLength - UndoCount;

	int32 MaxEntries = OptionalInt(Params, TEXT("maxEntries"), 50);
	if (MaxEntries < 1) MaxEntries = 1;
	if (MaxEntries > QueueLength) MaxEntries = QueueLength;

	// Newest first: the entries next to the current index are the ones a caller
	// is about to undo or redo, and are what a long buffer must not truncate.
	const int32 FirstIndex = FMath::Max(0, QueueLength - MaxEntries);

	TArray<TSharedPtr<FJsonValue>> Entries;
	for (int32 Index = QueueLength - 1; Index >= FirstIndex; --Index)
	{
		const FTransaction* Transaction = Trans->GetTransaction(Index);
		if (!Transaction) continue;

		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("index"), Index);
		Row->SetStringField(TEXT("title"), Transaction->GetTitle().ToString());
		Row->SetStringField(TEXT("id"), Transaction->GetId().ToString());
		Row->SetNumberField(TEXT("recordCount"), Transaction->GetRecordCount());
		Row->SetNumberField(TEXT("dataSizeBytes"), static_cast<double>(Transaction->DataSize()));
		Row->SetStringField(TEXT("state"), Index < CurrentIndex ? TEXT("applied") : TEXT("undone"));
		Row->SetBoolField(TEXT("isNextUndo"), Index == CurrentIndex - 1);
		Row->SetBoolField(TEXT("isNextRedo"), Index == CurrentIndex);
		if (const UObject* Primary = Transaction->GetPrimaryObject())
		{
			Row->SetStringField(TEXT("primaryObject"), Primary->GetPathName());
			Row->SetStringField(TEXT("primaryObjectClass"), Primary->GetClass()->GetName());
		}
		Entries.Add(MakeShared<FJsonValueObject>(Row));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetArrayField(TEXT("transactions"), Entries);
	Result->SetNumberField(TEXT("returned"), Entries.Num());
	Result->SetBoolField(TEXT("truncated"), FirstIndex > 0);
	Result->SetStringField(TEXT("order"), TEXT("newestFirst"));
	Result->SetStringField(TEXT("indexNote"),
		TEXT("currentIndex splits the queue: entries below it are applied, entries at or above it were undone and can be redone. index is the position in the engine's own undo buffer, so it stays stable while nothing new is recorded."));
	MCPViewportCtlWriteUndoState(Trans, Result);
	return MCPResult(Result);
}
