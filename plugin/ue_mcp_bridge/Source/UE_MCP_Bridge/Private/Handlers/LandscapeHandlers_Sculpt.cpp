// Landscape sculpting and weight painting (#742).
//
// The landscape category has always advertised "sculpting, painting, heightmap
// import", but no such action existed - the description promised capability the
// surface did not have, which is worse than a gap because a caller plans around
// it. These are the missing writes, done through FLandscapeEditDataInterface,
// the same path the landscape editor tools use.
//
// Translation-unit partition of FLandscapeHandlers; registrations live in
// LandscapeHandlers.cpp.

#include "LandscapeHandlers.h"

#include "HandlerUtils.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeEdit.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeProxy.h"
#include "LandscapeEditLayer.h"
#include "LandscapeDataAccess.h"
#include "ScopedTransaction.h"

// Region sculpting, erosion, heightmap import/export and terrain analysis need
// file IO and a 16-bit PNG codec on top of the edit-data interface above.
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	// Landscape heights are uint16 with 32768 as "zero". One unit of height is
	// LANDSCAPE_ZSCALE (1/128) cm before the actor's own Z scale.

	/** The landscape to edit: by actorPath or actor label, else the only one in
	 *  the world.
	 *
	 *  #983: a supplied label used to answer with Found[0], so two landscapes
	 *  sharing a label sculpted whichever the actor iterator reached first. A
	 *  named selector now goes through the shared resolver, which refuses and
	 *  lists the candidate paths. The unnamed case keeps its own scan, because
	 *  "the only landscape in the level" is a default this action is entitled
	 *  to and the resolver has no opinion about. */
	ALandscape* ResolveLandscape(UWorld* World, const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonValue>& OutError)
	{
		FMCPActorSelector Selector;
		Selector.bRequired = false;
		if (AActor* Named = MCPResolveActor(World, Params, OutError, Selector))
		{
			ALandscape* AsLandscape = Cast<ALandscape>(Named);
			if (!AsLandscape)
			{
				OutError = MCPError(FString::Printf(
					TEXT("Actor '%s' is a %s, not a Landscape"),
					*Named->GetActorLabel(), *Named->GetClass()->GetName()));
				return nullptr;
			}
			return AsLandscape;
		}
		if (OutError.IsValid()) return nullptr;

		TArray<ALandscape*> Found;
		for (TActorIterator<ALandscape> It(World); It; ++It)
		{
			ALandscape* Candidate = *It;
			if (!Candidate) continue;
			Found.Add(Candidate);
		}
		if (Found.Num() == 0)
		{
			OutError = MCPError(TEXT("No Landscape actor in the current level. Create one with landscape(create)."));
			return nullptr;
		}
		if (Found.Num() > 1)
		{
			TArray<FString> Labels;
			for (ALandscape* L : Found) Labels.Add(L->GetActorLabel());
			OutError = MCPError(FString::Printf(
				TEXT("%d Landscape actors in the level; pass actorLabel or actorPath to choose. Available: [%s]"),
				Found.Num(), *FString::Join(Labels, TEXT(", "))));
			return nullptr;
		}
		return Found[0];
	}

	/**
	 * Convert a world-space XY position and radius into the landscape's own
	 * quad grid, clamped to the loaded extent. Working in world units is what a
	 * caller has; working in quads is what the edit interface needs.
	 */
	bool WorldToLandscapeRect(
		ALandscape* Landscape,
		ULandscapeInfo* Info,
		const FVector2D& Center,
		double Radius,
		int32& OutX1, int32& OutY1, int32& OutX2, int32& OutY2,
		FString& OutError)
	{
		const FTransform ActorToWorld = Landscape->ActorToWorld();
		const FVector LocalCenter = ActorToWorld.InverseTransformPosition(FVector(Center.X, Center.Y, 0.0));
		const FVector Scale = ActorToWorld.GetScale3D();
		if (FMath::IsNearlyZero(Scale.X) || FMath::IsNearlyZero(Scale.Y))
		{
			OutError = TEXT("Landscape has a zero XY scale");
			return false;
		}
		const double RadiusQuadsX = FMath::Abs(Radius / Scale.X);
		const double RadiusQuadsY = FMath::Abs(Radius / Scale.Y);

		int32 MinX, MinY, MaxX, MaxY;
		if (!Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
		{
			OutError = TEXT("Could not read the landscape extent (are its components loaded?)");
			return false;
		}

		OutX1 = FMath::Clamp(FMath::FloorToInt(LocalCenter.X - RadiusQuadsX), MinX, MaxX);
		OutY1 = FMath::Clamp(FMath::FloorToInt(LocalCenter.Y - RadiusQuadsY), MinY, MaxY);
		OutX2 = FMath::Clamp(FMath::CeilToInt(LocalCenter.X + RadiusQuadsX), MinX, MaxX);
		OutY2 = FMath::Clamp(FMath::CeilToInt(LocalCenter.Y + RadiusQuadsY), MinY, MaxY);

		if (OutX2 < OutX1 || OutY2 < OutY1)
		{
			OutError = TEXT("The requested area does not overlap the landscape");
			return false;
		}
		return true;
	}

	/**
	 * Resolve which edit layer to write into. UE 5.8 landscapes ALWAYS have
	 * layer content (HasLayersContent/CanHaveLayersContent return true
	 * unconditionally), so writing without a layer GUID targets the merged
	 * heightmap - which the layer system then regenerates from the untouched
	 * layer stack on the next tick, silently erasing the edit. Outside
	 * Landscape Mode GetEditingLayer() is an invalid GUID, which is exactly
	 * the case a headless bridge is always in.
	 */
	bool ResolveEditLayerGuid(ALandscape* Landscape, const TSharedPtr<FJsonObject>& Params, FGuid& OutGuid, FString& OutName, FString& OutError)
	{
		const FString WantedName = OptionalString(Params, TEXT("editLayer"));
		const int32 WantedIndex = OptionalInt(Params, TEXT("editLayerIndex"), 0);

		const TArray<const ULandscapeEditLayerBase*> Layers = Landscape->GetEditLayersConst();
		if (Layers.Num() == 0)
		{
			OutError = TEXT("Landscape has no edit layers; cannot write a sculpt/paint edit that would survive the next layer update");
			return false;
		}

		if (!WantedName.IsEmpty())
		{
			for (const ULandscapeEditLayerBase* Layer : Layers)
			{
				if (Layer && Layer->GetName().ToString().Equals(WantedName, ESearchCase::IgnoreCase))
				{
					OutGuid = Layer->GetGuid();
					OutName = Layer->GetName().ToString();
					return true;
				}
			}
			TArray<FString> Names;
			for (const ULandscapeEditLayerBase* Layer : Layers) { if (Layer) Names.Add(Layer->GetName().ToString()); }
			OutError = FString::Printf(TEXT("Edit layer '%s' not found. Available: [%s]"), *WantedName, *FString::Join(Names, TEXT(", ")));
			return false;
		}

		if (!Layers.IsValidIndex(WantedIndex) || !Layers[WantedIndex])
		{
			OutError = FString::Printf(TEXT("editLayerIndex %d is out of range (%d edit layers)"), WantedIndex, Layers.Num());
			return false;
		}
		OutGuid = Layers[WantedIndex]->GetGuid();
		OutName = Layers[WantedIndex]->GetName().ToString();
		return true;
	}

	/** Smooth 0..1 brush weight for a point, given a falloff fraction. */
	double BrushWeight(double DistanceFraction, double Falloff)
	{
		if (DistanceFraction >= 1.0) return 0.0;
		const double Inner = FMath::Clamp(1.0 - Falloff, 0.0, 1.0);
		if (DistanceFraction <= Inner) return 1.0;
		const double T = (DistanceFraction - Inner) / FMath::Max(1.0 - Inner, KINDA_SMALL_NUMBER);
		// Smoothstep so a brush edge does not leave a visible ring.
		return 1.0 - (T * T * (3.0 - 2.0 * T));
	}

	/**
	 * The rollback record for the two BRUSH writes, sculpt and paint_layer.
	 *
	 * Every REGION write goes through MCPLscWriteHeights / MCPLscWriteWeights,
	 * which emit their own record; the two brush handlers cannot, because they
	 * are defined above those helpers and compute their footprint from a centre
	 * and a radius rather than from a resolved FMCPLscRegion. So the emission
	 * lives here, once, rather than open-coded twice at the call sites - a
	 * second copy is how one of them quietly stops emitting.
	 *
	 * The payload is deliberately identical in shape to what those helpers
	 * build: quad-space region, raw height space, the previous data base64'd,
	 * and the edit layer by name. The same cap applies, because a record
	 * carrying part of a rectangle is worse than none.
	 */
	void MCPLscBrushRollback(
		TSharedPtr<FJsonObject> Result,
		const TSharedPtr<FJsonObject>& Params,
		const FString& InverseMethod,
		const FString& ActorPath,
		int32 X1, int32 Y1, int32 X2, int32 Y2,
		const FString& EditLayerName,
		const FString& LayerName,
		const TCHAR* DataField,
		const FString& EncodedPrevious,
		int64 DefaultCap)
	{
		const int64 Count = (int64)(X2 - X1 + 1) * (int64)(Y2 - Y1 + 1);
		const int64 RollbackCap = (int64)FMath::Clamp(
			OptionalInt(Params, TEXT("rollbackMaxVertices"), (int32)DefaultCap), 0, 16000000);
		if (Count > RollbackCap)
		{
			Result->SetBoolField(TEXT("rollbackOmitted"), true);
			Result->SetStringField(TEXT("rollbackOmittedReason"), FString::Printf(
				TEXT("The brush covered %lld vertices, above the %lld-vertex rollback cap, so the previous data is NOT ")
				TEXT("carried and this edit cannot be undone through the bridge. Use a smaller 'radius', or raise ")
				TEXT("'rollbackMaxVertices'."),
				Count, RollbackCap));
			return;
		}

		TSharedPtr<FJsonObject> Region = MakeShared<FJsonObject>();
		Region->SetNumberField(TEXT("minX"), X1);
		Region->SetNumberField(TEXT("minY"), Y1);
		Region->SetNumberField(TEXT("maxX"), X2);
		Region->SetNumberField(TEXT("maxY"), Y2);

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), ActorPath);
		Payload->SetObjectField(TEXT("region"), Region);
		Payload->SetStringField(TEXT("space"), TEXT("quad"));
		Payload->SetStringField(DataField, EncodedPrevious);
		Payload->SetStringField(TEXT("editLayer"), EditLayerName);
		if (!LayerName.IsEmpty()) Payload->SetStringField(TEXT("layerName"), LayerName);
		// Heights are written back as the raw uint16 they were read as; the
		// weight writer has no equivalent parameter and needs none.
		if (FCString::Strcmp(DataField, TEXT("heightsBase64")) == 0)
		{
			Payload->SetStringField(TEXT("heightSpace"), TEXT("raw"));
		}
		MCPSetRollback(Result, InverseMethod, Payload);
		Result->SetBoolField(TEXT("rollbackOmitted"), false);
	}
}

// landscape(sculpt): raise, lower or flatten a circular brush footprint.
TSharedPtr<FJsonValue> FLandscapeHandlers::Sculpt(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	TSharedPtr<FJsonValue> ResolveError;
	ALandscape* Landscape = ResolveLandscape(World, Params, ResolveError);
	if (!Landscape) return ResolveError;

	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (!Info) return MCPError(TEXT("Landscape has no LandscapeInfo (not registered yet)"));

	const TSharedPtr<FJsonObject>* CenterObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("center"), CenterObj) || !CenterObj)
	{
		return MCPError(TEXT("Missing 'center' ({x, y} in world space)"));
	}
	FVector2D Center(0.0, 0.0);
	(*CenterObj)->TryGetNumberField(TEXT("x"), Center.X);
	(*CenterObj)->TryGetNumberField(TEXT("y"), Center.Y);

	const double Radius = OptionalNumber(Params, TEXT("radius"), 500.0);
	if (Radius <= 0.0) return MCPError(TEXT("'radius' must be positive"));
	const double Falloff = FMath::Clamp(OptionalNumber(Params, TEXT("falloff"), 0.5), 0.0, 1.0);
	const FString Mode = OptionalString(Params, TEXT("mode"), TEXT("raise")).ToLower();
	if (Mode != TEXT("raise") && Mode != TEXT("lower") && Mode != TEXT("flatten"))
	{
		return MCPError(TEXT("'mode' must be raise, lower or flatten"));
	}
	// World-space centimetres to move the surface at full brush strength.
	const double Amount = OptionalNumber(Params, TEXT("amount"), 100.0);

	int32 X1, Y1, X2, Y2;
	FString RectError;
	if (!WorldToLandscapeRect(Landscape, Info, Center, Radius, X1, Y1, X2, Y2, RectError))
	{
		return MCPError(RectError);
	}

	const int32 Width = X2 - X1 + 1;
	const int32 Height = Y2 - Y1 + 1;
	// Cap the working rect: the extent clamp alone lets an 8k landscape with a
	// huge radius allocate hundreds of MB and iterate tens of millions of verts.
	const int64 VertexCount = (int64)Width * (int64)Height;
	const int64 MaxVertices = (int64)FMath::Clamp(OptionalInt(Params, TEXT("maxVertices"), 4'000'000), 1024, 64'000'000);
	if (VertexCount > MaxVertices)
	{
		return MCPError(FString::Printf(
			TEXT("Brush covers %lld vertices, above the %lld limit. Reduce 'radius' or raise 'maxVertices' deliberately."),
			VertexCount, MaxVertices));
	}
	TArray<uint16> Heights;
	Heights.SetNumZeroed(Width * Height);

	FGuid EditLayerGuid;
	FString EditLayerName;
	FString LayerError;
	if (!ResolveEditLayerGuid(Landscape, Params, EditLayerGuid, EditLayerName, LayerError))
	{
		return MCPError(LayerError);
	}

	FLandscapeEditDataInterface EditData(Info);
	EditData.SetEditLayer(EditLayerGuid);
	// GetHeightData takes the rect by non-const reference and REWRITES it with
	// the valid sub-range, so pass copies: the buffer is sized and indexed
	// against the original rect, and an empty region returns an inverted
	// INT_MAX/INT_MIN rect that would overflow the vertex count downstream.
	{
		int32 GX1 = X1, GY1 = Y1, GX2 = X2, GY2 = Y2;
		EditData.GetHeightData(GX1, GY1, GX2, GY2, Heights.GetData(), 0);
		if (GX2 < GX1 || GY2 < GY1)
		{
			return MCPError(TEXT("No landscape height data in the requested area (unloaded or absent components). Move the brush or load the region first."));
		}
	}

	const FVector Scale = Landscape->ActorToWorld().GetScale3D();
	// uint16 height units per world centimetre.
	const double ZScale = FMath::IsNearlyZero(Scale.Z) ? 0.0 : (1.0 / (LANDSCAPE_ZSCALE * Scale.Z));
	if (ZScale == 0.0) return MCPError(TEXT("Landscape has a zero Z scale; heights cannot be written"));

	const FVector LocalCenter = Landscape->ActorToWorld().InverseTransformPosition(FVector(Center.X, Center.Y, 0.0));
	const double RadiusQuadsX = FMath::Abs(Radius / Scale.X);
	const double RadiusQuadsY = FMath::Abs(Radius / Scale.Y);

	// Flatten needs a target: the height under the brush centre before editing.
	double FlattenTarget = 0.0;
	if (Mode == TEXT("flatten"))
	{
		const int32 CX = FMath::Clamp(FMath::RoundToInt(LocalCenter.X), X1, X2);
		const int32 CY = FMath::Clamp(FMath::RoundToInt(LocalCenter.Y), Y1, Y2);
		FlattenTarget = (double)Heights[(CY - Y1) * Width + (CX - X1)];
	}

	// Resolve the mode once instead of comparing strings per vertex.
	const int32 SculptMode = Mode == TEXT("raise") ? 0 : (Mode == TEXT("lower") ? 1 : 2);

	// The heights as they stand, kept whole so the rollback record can carry
	// them. The loop below edits Heights in place, so a copy taken afterwards
	// would be the post-write state.
	const TArray<uint16> Previous = Heights;

	int32 Changed = 0;
	int32 Touched = 0;
	for (int32 Y = Y1; Y <= Y2; ++Y)
	{
		for (int32 X = X1; X <= X2; ++X)
		{
			const double DX = (X - LocalCenter.X) / FMath::Max(RadiusQuadsX, KINDA_SMALL_NUMBER);
			const double DY = (Y - LocalCenter.Y) / FMath::Max(RadiusQuadsY, KINDA_SMALL_NUMBER);
			const double Fraction = FMath::Sqrt(DX * DX + DY * DY);
			const double Weight = BrushWeight(Fraction, Falloff);
			if (Weight <= 0.0) continue;

			const int32 Index = (Y - Y1) * Width + (X - X1);
			const double Current = (double)Heights[Index];
			double Next = Current;
			if (SculptMode == 0)      Next = Current + Amount * ZScale * Weight;
			else if (SculptMode == 1) Next = Current - Amount * ZScale * Weight;
			else                      Next = FMath::Lerp(Current, FlattenTarget, Weight);

			Heights[Index] = (uint16)FMath::Clamp(FMath::RoundToInt(Next), 0, 65535);
			if (Heights[Index] != Previous[Index]) ++Changed;
			++Touched;
		}
	}

	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("MCP landscape sculpt")));
		Landscape->Modify();
		FScopedSetLandscapeEditingLayer EditingLayer(Landscape, EditLayerGuid);
		int32 SX1 = X1, SY1 = Y1, SX2 = X2, SY2 = Y2;
		EditData.SetHeightData(SX1, SY1, SX2, SY2, Heights.GetData(), 0, /*InCalcNormals=*/true);
		EditData.Flush();
	}
	Landscape->PostEditChange();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("landscape"), Landscape->GetActorLabel());
	Result->SetStringField(TEXT("actorLabel"), Landscape->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Landscape->GetPathName());
	Result->SetStringField(TEXT("mode"), Mode);
	Result->SetNumberField(TEXT("radius"), Radius);
	Result->SetNumberField(TEXT("amount"), Amount);
	Result->SetNumberField(TEXT("verticesTouched"), Touched);
	Result->SetNumberField(TEXT("verticesChanged"), Changed);
	Result->SetStringField(TEXT("editLayer"), EditLayerName);
	Result->SetNumberField(TEXT("rectX1"), X1);
	Result->SetNumberField(TEXT("rectY1"), Y1);
	Result->SetNumberField(TEXT("rectX2"), X2);
	Result->SetNumberField(TEXT("rectY2"), Y2);
	// A brush that rounded to the same height everywhere it touched moved no
	// ground, and a replayed flatten over already-flat terrain is exactly that.
	// Reporting it as an update would make a rerun look like it sculpted.
	if (Changed > 0)
	{
		MCPSetUpdated(Result);
		Result->SetBoolField(TEXT("unchanged"), false);
		// Same record the region writers emit: the exact previous heights over
		// the exact rectangle, replayed through set_landscape_height_region.
		MCPLscBrushRollback(
			Result, Params, TEXT("set_landscape_height_region"), Landscape->GetPathName(),
			X1, Y1, X2, Y2, EditLayerName, FString(), TEXT("heightsBase64"),
			FBase64::Encode(reinterpret_cast<const uint8*>(Previous.GetData()),
				(uint32)(Previous.Num() * sizeof(uint16))),
			/*DefaultCap*/ 262144);
	}
	else
	{
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("rollbackOmitted"), false);
		Result->SetStringField(TEXT("unchangedNote"),
			TEXT("Every vertex the brush covered already held the height this call would have written, so no rollback "
				 "record is needed."));
	}
	Result->SetStringField(TEXT("note"), TEXT("The level is left dirty and unsaved; save it when ready."));
	return MCPResult(Result);
}

// landscape(paint_layer): paint a weight layer over a circular footprint.
TSharedPtr<FJsonValue> FLandscapeHandlers::PaintLayer(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	TSharedPtr<FJsonValue> ResolveError;
	ALandscape* Landscape = ResolveLandscape(World, Params, ResolveError);
	if (!Landscape) return ResolveError;

	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (!Info) return MCPError(TEXT("Landscape has no LandscapeInfo (not registered yet)"));

	FString LayerName;
	if (auto Err = RequireString(Params, TEXT("layerName"), LayerName)) return Err;

	// Resolve the layer against the landscape's registered target layers, so a
	// typo names the layers that DO exist instead of silently painting nothing.
	ULandscapeLayerInfoObject* LayerInfo = nullptr;
	TArray<FString> KnownLayers;
	for (const FLandscapeInfoLayerSettings& Layer : Info->Layers)
	{
		KnownLayers.Add(Layer.GetLayerName().ToString());
		if (Layer.GetLayerName().ToString().Equals(LayerName, ESearchCase::IgnoreCase))
		{
			LayerInfo = Layer.LayerInfoObj;
		}
	}
	if (!LayerInfo)
	{
		return MCPError(FString::Printf(
			TEXT("Paint layer '%s' has no LayerInfo on this landscape. Registered layers: [%s]. Use landscape(add_layer_info) first."),
			*LayerName, *FString::Join(KnownLayers, TEXT(", "))));
	}

	const TSharedPtr<FJsonObject>* CenterObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("center"), CenterObj) || !CenterObj)
	{
		return MCPError(TEXT("Missing 'center' ({x, y} in world space)"));
	}
	FVector2D Center(0.0, 0.0);
	(*CenterObj)->TryGetNumberField(TEXT("x"), Center.X);
	(*CenterObj)->TryGetNumberField(TEXT("y"), Center.Y);

	const double Radius = OptionalNumber(Params, TEXT("radius"), 500.0);
	if (Radius <= 0.0) return MCPError(TEXT("'radius' must be positive"));
	const double Falloff = FMath::Clamp(OptionalNumber(Params, TEXT("falloff"), 0.5), 0.0, 1.0);
	// 0..1 target weight at full brush strength.
	const double Strength = FMath::Clamp(OptionalNumber(Params, TEXT("strength"), 1.0), 0.0, 1.0);

	int32 X1, Y1, X2, Y2;
	FString RectError;
	if (!WorldToLandscapeRect(Landscape, Info, Center, Radius, X1, Y1, X2, Y2, RectError))
	{
		return MCPError(RectError);
	}

	const int32 Width = X2 - X1 + 1;
	const int32 Height = Y2 - Y1 + 1;
	const int64 VertexCount = (int64)Width * (int64)Height;
	const int64 MaxVertices = (int64)FMath::Clamp(OptionalInt(Params, TEXT("maxVertices"), 4'000'000), 1024, 64'000'000);
	if (VertexCount > MaxVertices)
	{
		return MCPError(FString::Printf(
			TEXT("Brush covers %lld vertices, above the %lld limit. Reduce 'radius' or raise 'maxVertices' deliberately."),
			VertexCount, MaxVertices));
	}
	TArray<uint8> Weights;
	Weights.SetNumZeroed(Width * Height);

	FGuid EditLayerGuid;
	FString EditLayerName;
	FString LayerError;
	if (!ResolveEditLayerGuid(Landscape, Params, EditLayerGuid, EditLayerName, LayerError))
	{
		return MCPError(LayerError);
	}

	FLandscapeEditDataInterface EditData(Info);
	EditData.SetEditLayer(EditLayerGuid);
	{
		int32 GX1 = X1, GY1 = Y1, GX2 = X2, GY2 = Y2;
		EditData.GetWeightData(LayerInfo, GX1, GY1, GX2, GY2, Weights.GetData(), 0);
		if (GX2 < GX1 || GY2 < GY1)
		{
			return MCPError(TEXT("No landscape weight data in the requested area (unloaded or absent components). Move the brush or load the region first."));
		}
	}

	const FVector Scale = Landscape->ActorToWorld().GetScale3D();
	const FVector LocalCenter = Landscape->ActorToWorld().InverseTransformPosition(FVector(Center.X, Center.Y, 0.0));
	const double RadiusQuadsX = FMath::Abs(Radius / Scale.X);
	const double RadiusQuadsY = FMath::Abs(Radius / Scale.Y);
	const double TargetWeight = Strength * 255.0;

	// The weights as they stand, kept whole for the rollback record: the loop
	// below edits Weights in place.
	const TArray<uint8> Previous = Weights;

	int32 Changed = 0;
	int32 Touched = 0;
	for (int32 Y = Y1; Y <= Y2; ++Y)
	{
		for (int32 X = X1; X <= X2; ++X)
		{
			const double DX = (X - LocalCenter.X) / FMath::Max(RadiusQuadsX, KINDA_SMALL_NUMBER);
			const double DY = (Y - LocalCenter.Y) / FMath::Max(RadiusQuadsY, KINDA_SMALL_NUMBER);
			const double Weight = BrushWeight(FMath::Sqrt(DX * DX + DY * DY), Falloff);
			if (Weight <= 0.0) continue;

			const int32 Index = (Y - Y1) * Width + (X - X1);
			const double Blended = FMath::Lerp((double)Weights[Index], TargetWeight, Weight);
			Weights[Index] = (uint8)FMath::Clamp(FMath::RoundToInt(Blended), 0, 255);
			if (Weights[Index] != Previous[Index]) ++Changed;
			++Touched;
		}
	}

	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("MCP landscape paint")));
		Landscape->Modify();
		// The 9-arg overload taking bWeightAdjust/bTotalWeightAdjust is
		// deprecated in 5.7 and its body DISCARDS both flags, so calling it
		// would let us claim a renormalisation that never happened.
		FScopedSetLandscapeEditingLayer EditingLayer(Landscape, EditLayerGuid);
		int32 SX1 = X1, SY1 = Y1, SX2 = X2, SY2 = Y2;
		EditData.SetAlphaData(LayerInfo, SX1, SY1, SX2, SY2, Weights.GetData(), 0,
			ELandscapeLayerPaintingRestriction::None);
		EditData.Flush();
	}
	Landscape->PostEditChange();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("landscape"), Landscape->GetActorLabel());
	Result->SetStringField(TEXT("actorLabel"), Landscape->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Landscape->GetPathName());
	Result->SetStringField(TEXT("layerName"), LayerName);
	Result->SetNumberField(TEXT("strength"), Strength);
	Result->SetNumberField(TEXT("radius"), Radius);
	Result->SetNumberField(TEXT("verticesTouched"), Touched);
	Result->SetNumberField(TEXT("verticesChanged"), Changed);
	Result->SetStringField(TEXT("editLayer"), EditLayerName);
	if (Changed > 0)
	{
		MCPSetUpdated(Result);
		Result->SetBoolField(TEXT("unchanged"), false);
		// Same record set_landscape_layer_weight_region emits, carrying the
		// exact previous weights over the exact rectangle.
		MCPLscBrushRollback(
			Result, Params, TEXT("set_landscape_layer_weight_region"), Landscape->GetPathName(),
			X1, Y1, X2, Y2, EditLayerName, LayerName, TEXT("weightsBase64"),
			FBase64::Encode(Previous.GetData(), (uint32)Previous.Num()),
			/*DefaultCap*/ 524288);
	}
	else
	{
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("rollbackOmitted"), false);
		Result->SetStringField(TEXT("unchangedNote"),
			TEXT("Every vertex the brush covered already held the weight this call would have written, so no rollback "
				 "record is needed."));
	}
	Result->SetStringField(TEXT("note"), TEXT("Weights are written as given; the engine no longer renormalises other layers for you, so set them explicitly if they must sum to 1. The level is left dirty and unsaved."));
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// Region read/write, sculpt operators, erosion, heightmap IO, terrain analysis.
//
// Everything below addresses the landscape as a RECTANGLE of vertices rather
// than as a circular brush, because that is the shape a caller can describe,
// verify and undo. The two region primitives (get/set height over a rect) are
// the floor: every operator, every erosion pass and every import reads through
// MCPLscReadHeights and writes through MCPLscWriteHeights, so the idempotency
// marker, the rollback record and the resulting height range are produced in
// exactly one place and cannot drift per action.
//
// Names here are prefixed MCPLsc on purpose. The module is a unity build, so
// two translation units in one blob share their anonymous namespaces and a
// second `BrushWeight` would be a redefinition (C2084). The helpers ABOVE in
// this file (ResolveLandscape, WorldToLandscapeRect, ResolveEditLayerGuid,
// BrushWeight) are in this same translation unit, so they are called directly
// rather than copied.
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	/** A resolved, clamped rectangle of landscape vertices, plus what the
	 *  caller actually asked for so a clamp can be reported rather than hidden. */
	struct FMCPLscRegion
	{
		int32 X1 = 0, Y1 = 0, X2 = 0, Y2 = 0;
		int32 RequestedX1 = 0, RequestedY1 = 0, RequestedX2 = 0, RequestedY2 = 0;
		bool bClamped = false;
		/** Where the numbers came from: "quad", "world", "brush" or "extent". */
		FString Source;

		int32 Width() const { return X2 - X1 + 1; }
		int32 Height() const { return Y2 - Y1 + 1; }
		int64 Count() const { return (int64)Width() * (int64)Height(); }
	};

	/** The height <-> world conversion for one landscape, resolved once.
	 *  Landscape heights are uint16 with 32768 meaning "the actor's own Z", and
	 *  one unit is LANDSCAPE_ZSCALE (1/128) local centimetres before the actor
	 *  Z scale. Doing this arithmetic inline per action is how a sculpt and a
	 *  read end up disagreeing about what a number means. */
	struct FMCPLscHeightSpace
	{
		FTransform ToWorld;
		double ZScale = 1.0;
		/** Raw height units per world centimetre. */
		double RawPerWorldCm = 0.0;
		bool bValid = false;

		double RawToWorldZ(double Raw) const
		{
			return ToWorld.TransformPosition(
				FVector(0.0, 0.0, (Raw - LandscapeDataAccess::MidValue) * LANDSCAPE_ZSCALE)).Z;
		}
		double WorldZToRaw(double WorldZ) const
		{
			const double LocalZ = ToWorld.InverseTransformPosition(FVector(0.0, 0.0, WorldZ)).Z;
			return LocalZ / LANDSCAPE_ZSCALE + LandscapeDataAccess::MidValue;
		}
		/** A world-space DELTA in centimetres expressed in raw height units. */
		double WorldDeltaToRaw(double DeltaCm) const { return DeltaCm * RawPerWorldCm; }
		static uint16 Clamp16(double Value)
		{
			return (uint16)FMath::Clamp(FMath::RoundToInt32(FMath::Clamp(Value, 0.0, 65535.0)), 0, 65535);
		}
	};

	FMCPLscHeightSpace MCPLscHeightSpaceFor(ALandscapeProxy* Proxy)
	{
		FMCPLscHeightSpace Space;
		if (!Proxy) return Space;
		Space.ToWorld = Proxy->ActorToWorld();
		Space.ZScale = Space.ToWorld.GetScale3D().Z;
		if (FMath::IsNearlyZero(Space.ZScale)) return Space;
		Space.RawPerWorldCm = 1.0 / (LANDSCAPE_ZSCALE * Space.ZScale);
		Space.bValid = true;
		return Space;
	}

	/** The landscape's own quad extent and its world footprint, as JSON. This is
	 *  what an out-of-bounds region has to report: "outside the landscape" with
	 *  no numbers is the same sentence for a typo and for a World Partition map
	 *  whose covering proxy is not loaded. */
	TSharedPtr<FJsonObject> MCPLscBoundsJson(ULandscapeInfo* Info, ALandscapeProxy* Proxy)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		if (!Info) return Obj;

		FIntRect Extent;
		if (Info->GetLandscapeExtent(Extent))
		{
			TSharedPtr<FJsonObject> Quads = MakeShared<FJsonObject>();
			Quads->SetNumberField(TEXT("minX"), Extent.Min.X);
			Quads->SetNumberField(TEXT("minY"), Extent.Min.Y);
			Quads->SetNumberField(TEXT("maxX"), Extent.Max.X);
			Quads->SetNumberField(TEXT("maxY"), Extent.Max.Y);
			Quads->SetNumberField(TEXT("width"), Extent.Max.X - Extent.Min.X + 1);
			Quads->SetNumberField(TEXT("height"), Extent.Max.Y - Extent.Min.Y + 1);
			Obj->SetObjectField(TEXT("quadExtent"), Quads);

			if (Proxy)
			{
				const FTransform ToWorld = Proxy->ActorToWorld();
				const FVector A = ToWorld.TransformPosition(FVector(Extent.Min.X, Extent.Min.Y, 0.0));
				const FVector B = ToWorld.TransformPosition(FVector(Extent.Max.X, Extent.Max.Y, 0.0));
				TSharedPtr<FJsonObject> World = MakeShared<FJsonObject>();
				World->SetNumberField(TEXT("minX"), FMath::Min(A.X, B.X));
				World->SetNumberField(TEXT("minY"), FMath::Min(A.Y, B.Y));
				World->SetNumberField(TEXT("maxX"), FMath::Max(A.X, B.X));
				World->SetNumberField(TEXT("maxY"), FMath::Max(A.Y, B.Y));
				Obj->SetObjectField(TEXT("worldExtent"), World);
			}
		}
		const FBox Loaded = Info->GetLoadedBounds();
		if (Loaded.IsValid)
		{
			TSharedPtr<FJsonObject> LoadedObj = MakeShared<FJsonObject>();
			LoadedObj->SetNumberField(TEXT("minX"), Loaded.Min.X);
			LoadedObj->SetNumberField(TEXT("minY"), Loaded.Min.Y);
			LoadedObj->SetNumberField(TEXT("minZ"), Loaded.Min.Z);
			LoadedObj->SetNumberField(TEXT("maxX"), Loaded.Max.X);
			LoadedObj->SetNumberField(TEXT("maxY"), Loaded.Max.Y);
			LoadedObj->SetNumberField(TEXT("maxZ"), Loaded.Max.Z);
			Obj->SetObjectField(TEXT("loadedWorldBounds"), LoadedObj);
		}
		return Obj;
	}

	/** The refusal a bad region produces: the message, plus the bounds that
	 *  WOULD have worked, machine-readable. */
	TSharedPtr<FJsonValue> MCPLscRegionError(
		const FString& Message,
		ULandscapeInfo* Info,
		ALandscapeProxy* Proxy,
		const FString& Reason)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), Message);
		Obj->SetStringField(TEXT("reason"), Reason);
		Obj->SetObjectField(TEXT("landscapeBounds"), MCPLscBoundsJson(Info, Proxy));
		return MakeShared<FJsonValueObject>(Obj);
	}

	TSharedPtr<FJsonObject> MCPLscRegionJson(const FMCPLscRegion& Region)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("minX"), Region.X1);
		Obj->SetNumberField(TEXT("minY"), Region.Y1);
		Obj->SetNumberField(TEXT("maxX"), Region.X2);
		Obj->SetNumberField(TEXT("maxY"), Region.Y2);
		Obj->SetNumberField(TEXT("width"), Region.Width());
		Obj->SetNumberField(TEXT("height"), Region.Height());
		Obj->SetNumberField(TEXT("vertexCount"), (double)Region.Count());
		Obj->SetStringField(TEXT("source"), Region.Source);
		if (Region.bClamped)
		{
			Obj->SetBoolField(TEXT("clampedToLandscape"), true);
			TSharedPtr<FJsonObject> Req = MakeShared<FJsonObject>();
			Req->SetNumberField(TEXT("minX"), Region.RequestedX1);
			Req->SetNumberField(TEXT("minY"), Region.RequestedY1);
			Req->SetNumberField(TEXT("maxX"), Region.RequestedX2);
			Req->SetNumberField(TEXT("maxY"), Region.RequestedY2);
			Obj->SetObjectField(TEXT("requested"), Req);
		}
		return Obj;
	}

	/**
	 * Resolve the rectangle an action works over, from whichever of the four
	 * spellings the caller used:
	 *
	 *   region {minX,minY,maxX,maxY}                   - landscape quad indices
	 *   region {...} with space:"world"                - world centimetres
	 *   center {x,y} + radius                          - the brush form the
	 *                                                    existing sculpt takes
	 *   nothing                                        - the whole landscape
	 *
	 * Returns false with OutError set. A rectangle that misses the landscape
	 * entirely is an error naming the real bounds; one that merely overhangs is
	 * clamped and says so, because silently shrinking a region and then
	 * reporting success over it is how a caller concludes a write covered
	 * ground it never touched.
	 */
	bool MCPLscResolveRegion(
		ALandscape* Landscape,
		ULandscapeInfo* Info,
		const TSharedPtr<FJsonObject>& Params,
		int32 DefaultMaxVertices,
		FMCPLscRegion& Out,
		TSharedPtr<FJsonValue>& OutError)
	{
		OutError.Reset();
		FIntRect Extent;
		if (!Info->GetLandscapeExtent(Extent))
		{
			OutError = MCPLscRegionError(
				TEXT("The landscape has no registered quad extent yet, so no region can be resolved. Its components may not be loaded; on a World Partition map pin them with level(load_actor_descs) first."),
				Info, Landscape, TEXT("noExtent"));
			return false;
		}

		const TSharedPtr<FJsonObject>* RegionObj = nullptr;
		const TSharedPtr<FJsonObject>* CenterObj = nullptr;
		double R1 = 0.0, R2 = 0.0, R3 = 0.0, R4 = 0.0;

		if (Params->TryGetObjectField(TEXT("region"), RegionObj) && RegionObj && RegionObj->IsValid())
		{
			const FString Space = OptionalString(Params, TEXT("space"), TEXT("quad")).ToLower();
			if (!(*RegionObj)->TryGetNumberField(TEXT("minX"), R1)
				|| !(*RegionObj)->TryGetNumberField(TEXT("minY"), R2)
				|| !(*RegionObj)->TryGetNumberField(TEXT("maxX"), R3)
				|| !(*RegionObj)->TryGetNumberField(TEXT("maxY"), R4))
			{
				OutError = MCPError(TEXT("'region' needs all four of minX, minY, maxX and maxY."));
				return false;
			}
			if (Space == TEXT("world"))
			{
				const FTransform ToWorld = Landscape->ActorToWorld();
				const FVector A = ToWorld.InverseTransformPosition(FVector(R1, R2, 0.0));
				const FVector B = ToWorld.InverseTransformPosition(FVector(R3, R4, 0.0));
				Out.RequestedX1 = FMath::FloorToInt32(FMath::Min(A.X, B.X));
				Out.RequestedY1 = FMath::FloorToInt32(FMath::Min(A.Y, B.Y));
				Out.RequestedX2 = FMath::CeilToInt32(FMath::Max(A.X, B.X));
				Out.RequestedY2 = FMath::CeilToInt32(FMath::Max(A.Y, B.Y));
				Out.Source = TEXT("world");
			}
			else if (Space == TEXT("quad"))
			{
				Out.RequestedX1 = FMath::FloorToInt32(FMath::Min(R1, R3));
				Out.RequestedY1 = FMath::FloorToInt32(FMath::Min(R2, R4));
				Out.RequestedX2 = FMath::CeilToInt32(FMath::Max(R1, R3));
				Out.RequestedY2 = FMath::CeilToInt32(FMath::Max(R2, R4));
				Out.Source = TEXT("quad");
			}
			else
			{
				OutError = MCPError(FString::Printf(
					TEXT("'space' must be \"quad\" (landscape vertex indices) or \"world\" (centimetres); got '%s'."), *Space));
				return false;
			}
		}
		else if (Params->TryGetObjectField(TEXT("center"), CenterObj) && CenterObj && CenterObj->IsValid())
		{
			FVector2D Center(0.0, 0.0);
			(*CenterObj)->TryGetNumberField(TEXT("x"), Center.X);
			(*CenterObj)->TryGetNumberField(TEXT("y"), Center.Y);
			const double Radius = OptionalNumber(Params, TEXT("radius"), 500.0);
			if (Radius <= 0.0)
			{
				OutError = MCPError(TEXT("'radius' must be positive."));
				return false;
			}
			int32 BX1, BY1, BX2, BY2;
			FString BrushError;
			// The brush path already exists in this file and already clamps to
			// the extent; reusing it is what keeps center+radius meaning the
			// same thing here as it does in landscape(sculpt).
			if (!WorldToLandscapeRect(Landscape, Info, Center, Radius, BX1, BY1, BX2, BY2, BrushError))
			{
				OutError = MCPLscRegionError(BrushError, Info, Landscape, TEXT("brushOffLandscape"));
				return false;
			}
			Out.RequestedX1 = BX1; Out.RequestedY1 = BY1;
			Out.RequestedX2 = BX2; Out.RequestedY2 = BY2;
			Out.Source = TEXT("brush");
		}
		else
		{
			Out.RequestedX1 = Extent.Min.X; Out.RequestedY1 = Extent.Min.Y;
			Out.RequestedX2 = Extent.Max.X; Out.RequestedY2 = Extent.Max.Y;
			Out.Source = TEXT("extent");
		}

		Out.X1 = FMath::Clamp(Out.RequestedX1, Extent.Min.X, Extent.Max.X);
		Out.Y1 = FMath::Clamp(Out.RequestedY1, Extent.Min.Y, Extent.Max.Y);
		Out.X2 = FMath::Clamp(Out.RequestedX2, Extent.Min.X, Extent.Max.X);
		Out.Y2 = FMath::Clamp(Out.RequestedY2, Extent.Min.Y, Extent.Max.Y);
		Out.bClamped =
			Out.X1 != Out.RequestedX1 || Out.Y1 != Out.RequestedY1 ||
			Out.X2 != Out.RequestedX2 || Out.Y2 != Out.RequestedY2;

		const bool bOverlaps =
			Out.RequestedX2 >= Extent.Min.X && Out.RequestedX1 <= Extent.Max.X &&
			Out.RequestedY2 >= Extent.Min.Y && Out.RequestedY1 <= Extent.Max.Y;
		if (!bOverlaps || Out.X2 < Out.X1 || Out.Y2 < Out.Y1)
		{
			OutError = MCPLscRegionError(
				FString::Printf(
					TEXT("The requested region (%d,%d)-(%d,%d) does not overlap this landscape, whose quad extent is (%d,%d)-(%d,%d). ")
					TEXT("Quad indices are landscape vertex coordinates, not centimetres; pass space:\"world\" to give the rectangle in world units."),
					Out.RequestedX1, Out.RequestedY1, Out.RequestedX2, Out.RequestedY2,
					Extent.Min.X, Extent.Min.Y, Extent.Max.X, Extent.Max.Y),
				Info, Landscape, TEXT("regionOutsideLandscape"));
			return false;
		}

		const int64 MaxVertices = (int64)FMath::Clamp(
			OptionalInt(Params, TEXT("maxVertices"), DefaultMaxVertices), 1, 64000000);
		if (Out.Count() > MaxVertices)
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetBoolField(TEXT("success"), false);
			Obj->SetStringField(TEXT("error"), FString::Printf(
				TEXT("The region covers %lld vertices (%dx%d), above the %lld limit this call runs with. ")
				TEXT("Shrink 'region', or raise 'maxVertices' deliberately - the cost is linear in vertices and the whole rectangle is held in memory."),
				Out.Count(), Out.Width(), Out.Height(), MaxVertices));
			Obj->SetStringField(TEXT("reason"), TEXT("regionTooLarge"));
			Obj->SetNumberField(TEXT("vertexCount"), (double)Out.Count());
			Obj->SetNumberField(TEXT("maxVertices"), (double)MaxVertices);
			Obj->SetObjectField(TEXT("region"), MCPLscRegionJson(Out));
			Obj->SetObjectField(TEXT("landscapeBounds"), MCPLscBoundsJson(Info, Landscape));
			OutError = MakeShared<FJsonValueObject>(Obj);
			return false;
		}
		return true;
	}

	/** Read the heights over a region. The edit interface REWRITES the rect it
	 *  was given with the sub-range it could actually serve, so an unloaded
	 *  component comes back as an inverted rect rather than as a field of zeroes
	 *  that reads exactly like a valley. */
	bool MCPLscReadHeights(
		ULandscapeInfo* Info,
		const FGuid& EditLayerGuid,
		bool bUseEditLayer,
		const FMCPLscRegion& Region,
		TArray<uint16>& OutHeights,
		TSharedPtr<FJsonValue>& OutError)
	{
		OutError.Reset();
		OutHeights.SetNumZeroed(Region.Count());
		FLandscapeEditDataInterface EditData(Info);
		if (bUseEditLayer) EditData.SetEditLayer(EditLayerGuid);
		int32 GX1 = Region.X1, GY1 = Region.Y1, GX2 = Region.X2, GY2 = Region.Y2;
		EditData.GetHeightData(GX1, GY1, GX2, GY2, OutHeights.GetData(), 0);
		if (GX2 < GX1 || GY2 < GY1)
		{
			OutError = MCPLscRegionError(
				TEXT("No landscape height data in that region: the covering components are not loaded. On a World Partition map list them with landscape(list_proxies) and pin them with level(load_actor_descs) before reading or writing."),
				Info, Info->GetLandscapeProxy(), TEXT("componentsNotLoaded"));
			return false;
		}
		return true;
	}

	/** Height statistics over a region, in raw units and in world Z. Every read
	 *  and every write reports these, which is what makes a write verifiable
	 *  without a follow-up call. */
	TSharedPtr<FJsonObject> MCPLscHeightStats(
		const TArray<uint16>& Heights, const FMCPLscHeightSpace& Space)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		if (Heights.Num() == 0) return Obj;
		uint16 MinRaw = MAX_uint16, MaxRaw = 0;
		double Sum = 0.0;
		for (uint16 H : Heights)
		{
			MinRaw = FMath::Min(MinRaw, H);
			MaxRaw = FMath::Max(MaxRaw, H);
			Sum += (double)H;
		}
		const double MeanRaw = Sum / (double)Heights.Num();
		Obj->SetNumberField(TEXT("minRaw"), MinRaw);
		Obj->SetNumberField(TEXT("maxRaw"), MaxRaw);
		Obj->SetNumberField(TEXT("meanRaw"), MeanRaw);
		if (Space.bValid)
		{
			Obj->SetNumberField(TEXT("minZ"), Space.RawToWorldZ(MinRaw));
			Obj->SetNumberField(TEXT("maxZ"), Space.RawToWorldZ(MaxRaw));
			Obj->SetNumberField(TEXT("meanZ"), Space.RawToWorldZ(MeanRaw));
			Obj->SetNumberField(TEXT("rangeZ"), Space.RawToWorldZ(MaxRaw) - Space.RawToWorldZ(MinRaw));
		}
		return Obj;
	}

	/** Little-endian uint16 blob, base64. The plain array form is unreadable
	 *  past a few thousand vertices and the rollback record has to carry the
	 *  exact previous heights, so both forms exist and every response says
	 *  which one it used. */
	FString MCPLscEncodeHeights(const TArray<uint16>& Heights)
	{
		return FBase64::Encode(
			reinterpret_cast<const uint8*>(Heights.GetData()),
			(uint32)(Heights.Num() * sizeof(uint16)));
	}

	bool MCPLscDecodeHeights(const FString& Base64, int32 ExpectedCount, TArray<uint16>& Out, FString& OutError)
	{
		TArray<uint8> Bytes;
		if (!FBase64::Decode(Base64, Bytes))
		{
			OutError = TEXT("'heightsBase64' is not valid base64.");
			return false;
		}
		if (Bytes.Num() != ExpectedCount * (int32)sizeof(uint16))
		{
			OutError = FString::Printf(
				TEXT("'heightsBase64' decodes to %d bytes; the region needs %d (%d vertices x 2 bytes, little-endian uint16)."),
				Bytes.Num(), ExpectedCount * 2, ExpectedCount);
			return false;
		}
		Out.SetNumUninitialized(ExpectedCount);
		FMemory::Memcpy(Out.GetData(), Bytes.GetData(), Bytes.Num());
		return true;
	}

	/** Attach the heights to a result, as an array below the threshold and as a
	 *  base64 blob above it, saying which. */
	void MCPLscAttachHeights(
		TSharedPtr<FJsonObject> Result,
		const TArray<uint16>& Heights,
		const TSharedPtr<FJsonObject>& Params)
	{
		const int32 ArrayLimit = FMath::Clamp(OptionalInt(Params, TEXT("arrayEncodingLimit"), 16384), 0, 4000000);
		FString Encoding = OptionalString(Params, TEXT("encoding"), TEXT("auto")).ToLower();
		if (Encoding == TEXT("auto"))
		{
			Encoding = Heights.Num() <= ArrayLimit ? TEXT("array") : TEXT("base64");
		}
		Result->SetStringField(TEXT("encoding"), Encoding);
		if (Encoding == TEXT("base64"))
		{
			Result->SetStringField(TEXT("heightsBase64"), MCPLscEncodeHeights(Heights));
			Result->SetStringField(TEXT("heightsBase64Note"),
				TEXT("Little-endian uint16, row-major from (minX,minY), width = maxX-minX+1. Feed it straight back to landscape(set_height_region) as heightsBase64."));
		}
		else
		{
			TArray<TSharedPtr<FJsonValue>> Rows;
			Rows.Reserve(Heights.Num());
			for (uint16 H : Heights) Rows.Add(MakeShared<FJsonValueNumber>(H));
			Result->SetArrayField(TEXT("heights"), Rows);
		}
	}

	/** The edit layer, as a typed refusal rather than a bare string when the
	 *  landscape cannot take a durable edit.
	 *
	 *  On 5.5+ every landscape has edit-layer content, and a write made with no
	 *  layer selected lands on the merged heightmap that the layer system then
	 *  regenerates from the untouched stack, erasing the edit on the next tick.
	 *  Reporting that as a success is worse than refusing, so this refuses. */
	bool MCPLscRequireEditLayer(
		ALandscape* Landscape,
		const TSharedPtr<FJsonObject>& Params,
		FGuid& OutGuid,
		FString& OutName,
		TSharedPtr<FJsonValue>& OutError)
	{
		OutError.Reset();
#if UE_MCP_HAS_5_5_API
		FString LayerError;
		if (ResolveEditLayerGuid(Landscape, Params, OutGuid, OutName, LayerError))
		{
			return true;
		}
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), LayerError);
		Obj->SetStringField(TEXT("reason"), TEXT("noWritableEditLayer"));
		Obj->SetBoolField(TEXT("unsupported"), true);
		Obj->SetStringField(TEXT("note"),
			TEXT("A height or weight write with no edit layer selected lands on the merged output, which the layer system regenerates from the untouched layer stack and silently discards. This call refuses rather than write data that will not survive."));
		OutError = MakeShared<FJsonValueObject>(Obj);
		return false;
#else
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"),
			TEXT("Landscape region writes need the landscape edit-layer system, which this plugin drives only on UE 5.5 and newer."));
		Obj->SetStringField(TEXT("reason"), TEXT("engineTooOld"));
		Obj->SetBoolField(TEXT("unsupported"), true);
		OutError = MakeShared<FJsonValueObject>(Obj);
		return false;
#endif
	}

	/**
	 * THE height write. Everything that changes terrain goes through here.
	 *
	 * It captures nothing itself: the caller passes the heights it read before
	 * editing, because it already had to read them to compute the new ones and
	 * a second read after the fact would capture the post-write state. The
	 * rollback record carries those previous heights base64-encoded, and when
	 * the region is too large to carry it says so with rollbackOmitted rather
	 * than emitting a record that would restore part of the rectangle.
	 */
	TSharedPtr<FJsonValue> MCPLscWriteHeights(
		ALandscape* Landscape,
		ULandscapeInfo* Info,
		const TSharedPtr<FJsonObject>& Params,
		const FMCPLscRegion& Region,
		const TArray<uint16>& Previous,
		const TArray<uint16>& NewHeights,
		TSharedPtr<FJsonObject> Result)
	{
		FGuid EditLayerGuid;
		FString EditLayerName;
		TSharedPtr<FJsonValue> LayerError;
		if (!MCPLscRequireEditLayer(Landscape, Params, EditLayerGuid, EditLayerName, LayerError))
		{
			return LayerError;
		}

		int64 Changed = 0;
		for (int32 Index = 0; Index < NewHeights.Num(); ++Index)
		{
			if (NewHeights[Index] != Previous[Index]) ++Changed;
		}

		const FMCPLscHeightSpace Space = MCPLscHeightSpaceFor(Landscape);
		Result->SetStringField(TEXT("actorLabel"), Landscape->GetActorLabel());
		Result->SetStringField(TEXT("actorPath"), Landscape->GetPathName());
		Result->SetStringField(TEXT("editLayer"), EditLayerName);
		Result->SetObjectField(TEXT("region"), MCPLscRegionJson(Region));
		Result->SetNumberField(TEXT("verticesChanged"), (double)Changed);
		Result->SetObjectField(TEXT("heightBefore"), MCPLscHeightStats(Previous, Space));
		Result->SetObjectField(TEXT("heightAfter"), MCPLscHeightStats(NewHeights, Space));

		if (Changed == 0)
		{
			// Idempotent replay: the terrain already looks like this. Not an
			// error, and explicitly NOT reported as an update, so a rerun of a
			// flow does not look like it moved ground it did not move.
			Result->SetBoolField(TEXT("updated"), false);
			Result->SetBoolField(TEXT("unchanged"), true);
			Result->SetBoolField(TEXT("rollbackOmitted"), false);
			Result->SetStringField(TEXT("note"),
				TEXT("Every vertex in the region already held the height this call would have written, so nothing was modified and no rollback record is needed."));
			return MCPResult(Result);
		}

		{
			const FScopedTransaction Transaction(FText::FromString(TEXT("MCP landscape height region")));
			Landscape->Modify();
			FScopedSetLandscapeEditingLayer EditingLayer(Landscape, EditLayerGuid);
			FLandscapeEditDataInterface EditData(Info);
			EditData.SetEditLayer(EditLayerGuid);
			int32 SX1 = Region.X1, SY1 = Region.Y1, SX2 = Region.X2, SY2 = Region.Y2;
			EditData.SetHeightData(SX1, SY1, SX2, SY2, NewHeights.GetData(), 0, /*InCalcNormals=*/true);
			EditData.Flush();
		}
		Landscape->PostEditChange();

		MCPSetUpdated(Result);
		Result->SetBoolField(TEXT("unchanged"), false);

		// Rollback: the exact previous heights over the exact region. Capped,
		// because a 4-million-vertex rectangle is an 8MB payload, and a rollback
		// record that carries part of the rectangle is worse than none at all.
		const int64 RollbackCap = (int64)FMath::Clamp(
			OptionalInt(Params, TEXT("rollbackMaxVertices"), 262144), 0, 8000000);
		if (Region.Count() <= RollbackCap)
		{
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("actorPath"), Landscape->GetPathName());
			Payload->SetObjectField(TEXT("region"), MCPLscRegionJson(Region));
			Payload->SetStringField(TEXT("space"), TEXT("quad"));
			Payload->SetStringField(TEXT("heightSpace"), TEXT("raw"));
			Payload->SetStringField(TEXT("heightsBase64"), MCPLscEncodeHeights(Previous));
			Payload->SetStringField(TEXT("editLayer"), EditLayerName);
			MCPSetRollback(Result, TEXT("set_landscape_height_region"), Payload);
			Result->SetBoolField(TEXT("rollbackOmitted"), false);
		}
		else
		{
			Result->SetBoolField(TEXT("rollbackOmitted"), true);
			Result->SetStringField(TEXT("rollbackOmittedReason"), FString::Printf(
				TEXT("The region holds %lld vertices, above the %lld-vertex rollback cap, so the previous heights are NOT carried and this edit cannot be undone through the bridge. ")
				TEXT("Read the region with landscape(get_height_region) before writing if you need an undo, work in smaller rectangles, or raise 'rollbackMaxVertices'."),
				Region.Count(), RollbackCap));
		}

		Result->SetStringField(TEXT("note"),
			TEXT("The level is left dirty and unsaved; save it when ready."));
		return MCPResult(Result);
	}

	/** The landscape and its info together, since every action below needs both
	 *  and the two failures read differently. */
	bool MCPLscResolve(
		UWorld* World,
		const TSharedPtr<FJsonObject>& Params,
		ALandscape*& OutLandscape,
		ULandscapeInfo*& OutInfo,
		TSharedPtr<FJsonValue>& OutError)
	{
		OutError.Reset();
		OutLandscape = ResolveLandscape(World, Params, OutError);
		if (!OutLandscape) return false;
		OutInfo = OutLandscape->GetLandscapeInfo();
		if (!OutInfo)
		{
			OutError = MCPError(TEXT("The landscape has no LandscapeInfo, so it is not registered with the world yet and cannot be read or written. Reopen the level, or check that its components finished registering."));
			return false;
		}
		return true;
	}

	/** World-space surface normal at one vertex, from the height field. Central
	 *  differences where a neighbour exists, one-sided at the region edge. The
	 *  gradient is taken in world centimetres, so a non-uniform landscape scale
	 *  is accounted for before the actor rotation is applied. */
	FVector MCPLscNormalAt(
		const TArray<uint16>& Heights,
		int32 Width, int32 Height,
		int32 LocalX, int32 LocalY,
		const FVector& Scale,
		const FQuat& Rotation)
	{
		const int32 XMinus = FMath::Max(LocalX - 1, 0);
		const int32 XPlus = FMath::Min(LocalX + 1, Width - 1);
		const int32 YMinus = FMath::Max(LocalY - 1, 0);
		const int32 YPlus = FMath::Min(LocalY + 1, Height - 1);

		const double HXm = (double)Heights[LocalY * Width + XMinus];
		const double HXp = (double)Heights[LocalY * Width + XPlus];
		const double HYm = (double)Heights[YMinus * Width + LocalX];
		const double HYp = (double)Heights[YPlus * Width + LocalX];

		const double SpanX = FMath::Max(XPlus - XMinus, 1) * Scale.X;
		const double SpanY = FMath::Max(YPlus - YMinus, 1) * Scale.Y;
		// Raw units to local cm is LANDSCAPE_ZSCALE, local cm to world is Scale.Z.
		const double DZdX = ((HXp - HXm) * LANDSCAPE_ZSCALE * Scale.Z) / (FMath::Abs(SpanX) > UE_KINDA_SMALL_NUMBER ? SpanX : 1.0);
		const double DZdY = ((HYp - HYm) * LANDSCAPE_ZSCALE * Scale.Z) / (FMath::Abs(SpanY) > UE_KINDA_SMALL_NUMBER ? SpanY : 1.0);

		FVector LocalNormal(-DZdX, -DZdY, 1.0);
		LocalNormal.Normalize();
		return Rotation.RotateVector(LocalNormal).GetSafeNormal();
	}

	/** Degrees from horizontal for a world normal. */
	double MCPLscSlopeDegrees(const FVector& WorldNormal)
	{
		const double CosAngle = FMath::Clamp(FVector::DotProduct(WorldNormal, FVector::UpVector), -1.0, 1.0);
		return FMath::RadiansToDegrees(FMath::Acos(CosAngle));
	}
}

namespace
{
	/** A world XY, in every spelling the landscape category already accepts.
	 *  landscape(sample) takes x/y, point{x,y} and worldX/worldY; a caller
	 *  moving between actions should not have to rename the same two numbers. */
	bool MCPLscReadWorldPoint(const TSharedPtr<FJsonObject>& Params, double& OutX, double& OutY)
	{
		const TSharedPtr<FJsonObject>* PointObj = nullptr;
		if (Params->TryGetObjectField(TEXT("point"), PointObj) && PointObj && PointObj->IsValid())
		{
			(*PointObj)->TryGetNumberField(TEXT("x"), OutX);
			(*PointObj)->TryGetNumberField(TEXT("y"), OutY);
			return true;
		}
		if (Params->HasField(TEXT("x")) && Params->HasField(TEXT("y")))
		{
			OutX = OptionalNumber(Params, TEXT("x"), 0.0);
			OutY = OptionalNumber(Params, TEXT("y"), 0.0);
			return true;
		}
		if (Params->HasField(TEXT("worldX")) && Params->HasField(TEXT("worldY")))
		{
			OutX = OptionalNumber(Params, TEXT("worldX"), 0.0);
			OutY = OptionalNumber(Params, TEXT("worldY"), 0.0);
			return true;
		}
		return false;
	}

	/** The 3x3 patch a point query needs: the vertex itself plus the four
	 *  neighbours a central-difference normal is built from. Clamped to the
	 *  extent, so a point on the very edge answers with a one-sided gradient
	 *  rather than refusing. */
	bool MCPLscPointPatch(
		ALandscape* Landscape,
		ULandscapeInfo* Info,
		const TSharedPtr<FJsonObject>& Params,
		FMCPLscRegion& OutRegion,
		int32& OutQuadX, int32& OutQuadY,
		TSharedPtr<FJsonValue>& OutError)
	{
		OutError.Reset();
		double WorldX = 0.0, WorldY = 0.0;
		if (!MCPLscReadWorldPoint(Params, WorldX, WorldY))
		{
			OutError = MCPError(TEXT("Missing position. Pass x and y, or point {x, y}, or worldX and worldY - all world space centimetres."));
			return false;
		}

		FIntRect Extent;
		if (!Info->GetLandscapeExtent(Extent))
		{
			OutError = MCPLscRegionError(
				TEXT("The landscape has no registered quad extent yet, so a point cannot be located on it."),
				Info, Landscape, TEXT("noExtent"));
			return false;
		}

		const FVector Local = Landscape->ActorToWorld().InverseTransformPosition(FVector(WorldX, WorldY, 0.0));
		OutQuadX = FMath::RoundToInt32(Local.X);
		OutQuadY = FMath::RoundToInt32(Local.Y);
		if (OutQuadX < Extent.Min.X || OutQuadX > Extent.Max.X
			|| OutQuadY < Extent.Min.Y || OutQuadY > Extent.Max.Y)
		{
			OutError = MCPLscRegionError(
				FString::Printf(
					TEXT("World position (%.1f, %.1f) maps to landscape vertex (%d, %d), which is outside this landscape's quad extent (%d,%d)-(%d,%d). ")
					TEXT("On a World Partition map the covering proxy may simply not be loaded - landscape(find_proxy_at) says which one owns that position."),
					WorldX, WorldY, OutQuadX, OutQuadY,
					Extent.Min.X, Extent.Min.Y, Extent.Max.X, Extent.Max.Y),
				Info, Landscape, TEXT("pointOutsideLandscape"));
			return false;
		}

		OutRegion.X1 = FMath::Clamp(OutQuadX - 1, Extent.Min.X, Extent.Max.X);
		OutRegion.Y1 = FMath::Clamp(OutQuadY - 1, Extent.Min.Y, Extent.Max.Y);
		OutRegion.X2 = FMath::Clamp(OutQuadX + 1, Extent.Min.X, Extent.Max.X);
		OutRegion.Y2 = FMath::Clamp(OutQuadY + 1, Extent.Min.Y, Extent.Max.Y);
		OutRegion.RequestedX1 = OutRegion.X1; OutRegion.RequestedY1 = OutRegion.Y1;
		OutRegion.RequestedX2 = OutRegion.X2; OutRegion.RequestedY2 = OutRegion.Y2;
		OutRegion.Source = TEXT("point");
		return true;
	}

	/** True when the caller named an edit layer, meaning it wants THAT layer's
	 *  own contribution rather than the merged surface the renderer shows. */
	bool MCPLscWantsEditLayerRead(const TSharedPtr<FJsonObject>& Params)
	{
		return Params->HasField(TEXT("editLayer")) || Params->HasField(TEXT("editLayerIndex"));
	}

	/** Resolve the layer to read from, when one was named. */
	bool MCPLscResolveReadLayer(
		ALandscape* Landscape,
		const TSharedPtr<FJsonObject>& Params,
		FGuid& OutGuid, FString& OutName, bool& bOutUse,
		TSharedPtr<FJsonValue>& OutError)
	{
		OutError.Reset();
		bOutUse = MCPLscWantsEditLayerRead(Params);
		if (!bOutUse) { OutName = TEXT("(merged)"); return true; }
		FString LayerError;
		if (ResolveEditLayerGuid(Landscape, Params, OutGuid, OutName, LayerError)) return true;
		OutError = MCPError(LayerError);
		return false;
	}
}

// landscape(get_height_region): read the heights over a rectangle.
TSharedPtr<FJsonValue> FLandscapeHandlers::GetHeightRegion(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	ALandscape* Landscape = nullptr;
	ULandscapeInfo* Info = nullptr;
	TSharedPtr<FJsonValue> Error;
	if (!MCPLscResolve(World, Params, Landscape, Info, Error)) return Error;

	FMCPLscRegion Region;
	if (!MCPLscResolveRegion(Landscape, Info, Params, 262144, Region, Error)) return Error;

	FGuid LayerGuid;
	FString LayerName;
	bool bUseLayer = false;
	if (!MCPLscResolveReadLayer(Landscape, Params, LayerGuid, LayerName, bUseLayer, Error)) return Error;

	TArray<uint16> Heights;
	if (!MCPLscReadHeights(Info, LayerGuid, bUseLayer, Region, Heights, Error)) return Error;

	const FMCPLscHeightSpace Space = MCPLscHeightSpaceFor(Landscape);
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Landscape->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Landscape->GetPathName());
	Result->SetStringField(TEXT("editLayer"), LayerName);
	Result->SetBoolField(TEXT("merged"), !bUseLayer);
	Result->SetObjectField(TEXT("region"), MCPLscRegionJson(Region));
	Result->SetObjectField(TEXT("height"), MCPLscHeightStats(Heights, Space));
	Result->SetNumberField(TEXT("rawUnitsPerWorldCm"), Space.RawPerWorldCm);
	Result->SetBoolField(TEXT("hasUnloadedComponentsInRegion"),
		Info->HasUnloadedComponentsInRegion(Region.X1, Region.Y1, Region.X2, Region.Y2));
	if (OptionalBool(Params, TEXT("includeHeights"), true))
	{
		MCPLscAttachHeights(Result, Heights, Params);
	}
	Result->SetStringField(TEXT("heightSpaceNote"),
		TEXT("Raw heights are uint16 with 32768 at the landscape actor's own Z; one unit is 1/128 local cm before the actor Z scale. minZ/maxZ/meanZ are the same numbers in world centimetres."));
	return MCPResult(Result);
}

// landscape(set_height_region): write heights over a rectangle.
TSharedPtr<FJsonValue> FLandscapeHandlers::SetHeightRegion(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	ALandscape* Landscape = nullptr;
	ULandscapeInfo* Info = nullptr;
	TSharedPtr<FJsonValue> Error;
	if (!MCPLscResolve(World, Params, Landscape, Info, Error)) return Error;

	FMCPLscRegion Region;
	if (!MCPLscResolveRegion(Landscape, Info, Params, 4000000, Region, Error)) return Error;

	const FMCPLscHeightSpace Space = MCPLscHeightSpaceFor(Landscape);
	if (!Space.bValid)
	{
		return MCPError(TEXT("The landscape has a zero Z scale, so no height can be expressed in world units. Fix the actor scale before writing heights."));
	}

	const FString HeightSpace = OptionalString(Params, TEXT("heightSpace"), TEXT("raw")).ToLower();
	if (HeightSpace != TEXT("raw") && HeightSpace != TEXT("world"))
	{
		return MCPError(TEXT("'heightSpace' must be \"raw\" (uint16, 32768 = the landscape actor's Z) or \"world\" (centimetres)."));
	}

	const int32 Count = (int32)Region.Count();
	TArray<uint16> NewHeights;
	FString Source;

	FString Base64;
	const TArray<TSharedPtr<FJsonValue>>* HeightArray = nullptr;
	if (Params->TryGetStringField(TEXT("heightsBase64"), Base64) && !Base64.IsEmpty())
	{
		FString DecodeError;
		if (!MCPLscDecodeHeights(Base64, Count, NewHeights, DecodeError))
		{
			return MCPError(DecodeError + FString::Printf(
				TEXT(" The region resolved to %dx%d starting at (%d,%d)."),
				Region.Width(), Region.Height(), Region.X1, Region.Y1));
		}
		if (HeightSpace == TEXT("world"))
		{
			return MCPError(TEXT("'heightsBase64' is always a raw uint16 blob; pass heightSpace \"raw\", or send world-space values through the 'heights' array instead."));
		}
		Source = TEXT("heightsBase64");
	}
	else if (Params->TryGetArrayField(TEXT("heights"), HeightArray) && HeightArray)
	{
		if (HeightArray->Num() != Count)
		{
			return MCPError(FString::Printf(
				TEXT("'heights' has %d entries but the region holds %d vertices (%dx%d starting at (%d,%d)). ")
				TEXT("The array is row-major from (minX,minY) with width = maxX-minX+1."),
				HeightArray->Num(), Count, Region.Width(), Region.Height(), Region.X1, Region.Y1));
		}
		NewHeights.SetNumUninitialized(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double Value = (*HeightArray)[Index].IsValid() ? (*HeightArray)[Index]->AsNumber() : 0.0;
			NewHeights[Index] = FMCPLscHeightSpace::Clamp16(
				HeightSpace == TEXT("world") ? Space.WorldZToRaw(Value) : Value);
		}
		Source = TEXT("heights");
	}
	else if (Params->HasField(TEXT("height")) || Params->HasField(TEXT("rawHeight")))
	{
		const bool bRaw = Params->HasField(TEXT("rawHeight"));
		const double Value = bRaw
			? OptionalNumber(Params, TEXT("rawHeight"), LandscapeDataAccess::MidValue)
			: OptionalNumber(Params, TEXT("height"), 0.0);
		const uint16 Fill = FMCPLscHeightSpace::Clamp16(bRaw ? Value : Space.WorldZToRaw(Value));
		NewHeights.Init(Fill, Count);
		Source = bRaw ? TEXT("rawHeight") : TEXT("height");
	}
	else
	{
		return MCPError(
			TEXT("Nothing to write. Pass 'heightsBase64' (little-endian uint16, row-major, the form landscape(get_height_region) hands back), ")
			TEXT("or 'heights' as one number per vertex, or 'height' / 'rawHeight' to fill the whole region with one value."));
	}

	TArray<uint16> Previous;
	if (!MCPLscReadHeights(Info, FGuid(), false, Region, Previous, Error)) return Error;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("heightSource"), Source);
	Result->SetStringField(TEXT("heightSpace"), HeightSpace);
	return MCPLscWriteHeights(Landscape, Info, Params, Region, Previous, NewHeights, Result);
}

// landscape(get_height_at_point): surface height under one world XY.
TSharedPtr<FJsonValue> FLandscapeHandlers::GetHeightAtPoint(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	ALandscape* Landscape = nullptr;
	ULandscapeInfo* Info = nullptr;
	TSharedPtr<FJsonValue> Error;
	if (!MCPLscResolve(World, Params, Landscape, Info, Error)) return Error;

	FMCPLscRegion Patch;
	int32 QuadX = 0, QuadY = 0;
	if (!MCPLscPointPatch(Landscape, Info, Params, Patch, QuadX, QuadY, Error)) return Error;

	FGuid LayerGuid;
	FString LayerName;
	bool bUseLayer = false;
	if (!MCPLscResolveReadLayer(Landscape, Params, LayerGuid, LayerName, bUseLayer, Error)) return Error;

	TArray<uint16> Heights;
	if (!MCPLscReadHeights(Info, LayerGuid, bUseLayer, Patch, Heights, Error)) return Error;

	const int32 Index = (QuadY - Patch.Y1) * Patch.Width() + (QuadX - Patch.X1);
	const uint16 Raw = Heights[Index];
	const FMCPLscHeightSpace Space = MCPLscHeightSpaceFor(Landscape);
	const FVector Surface = Space.ToWorld.TransformPosition(FVector(
		(double)QuadX, (double)QuadY, (double)LandscapeDataAccess::GetLocalHeight(Raw)));

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Landscape->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Landscape->GetPathName());
	Result->SetStringField(TEXT("editLayer"), LayerName);
	Result->SetBoolField(TEXT("merged"), !bUseLayer);
	Result->SetNumberField(TEXT("rawHeight"), Raw);
	Result->SetNumberField(TEXT("height"), Surface.Z);
	TSharedPtr<FJsonObject> Quad = MakeShared<FJsonObject>();
	Quad->SetNumberField(TEXT("x"), QuadX);
	Quad->SetNumberField(TEXT("y"), QuadY);
	Result->SetObjectField(TEXT("quad"), Quad);
	TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
	Loc->SetNumberField(TEXT("x"), Surface.X);
	Loc->SetNumberField(TEXT("y"), Surface.Y);
	Loc->SetNumberField(TEXT("z"), Surface.Z);
	Result->SetObjectField(TEXT("location"), Loc);
	Result->SetStringField(TEXT("note"),
		TEXT("The height is read at the NEAREST landscape vertex, not interpolated between four of them, so it can differ from a physics trace by up to half a quad of slope."));
	return MCPResult(Result);
}

// landscape(get_normal_at_point): world-space surface normal under one XY.
TSharedPtr<FJsonValue> FLandscapeHandlers::GetNormalAtPoint(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	ALandscape* Landscape = nullptr;
	ULandscapeInfo* Info = nullptr;
	TSharedPtr<FJsonValue> Error;
	if (!MCPLscResolve(World, Params, Landscape, Info, Error)) return Error;

	FMCPLscRegion Patch;
	int32 QuadX = 0, QuadY = 0;
	if (!MCPLscPointPatch(Landscape, Info, Params, Patch, QuadX, QuadY, Error)) return Error;

	TArray<uint16> Heights;
	if (!MCPLscReadHeights(Info, FGuid(), false, Patch, Heights, Error)) return Error;

	const FTransform ToWorld = Landscape->ActorToWorld();
	const FVector Normal = MCPLscNormalAt(
		Heights, Patch.Width(), Patch.Height(),
		QuadX - Patch.X1, QuadY - Patch.Y1,
		ToWorld.GetScale3D(), ToWorld.GetRotation());

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Landscape->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Landscape->GetPathName());
	TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
	N->SetNumberField(TEXT("x"), Normal.X);
	N->SetNumberField(TEXT("y"), Normal.Y);
	N->SetNumberField(TEXT("z"), Normal.Z);
	Result->SetObjectField(TEXT("normal"), N);
	Result->SetNumberField(TEXT("slopeDegrees"), MCPLscSlopeDegrees(Normal));
	TSharedPtr<FJsonObject> Quad = MakeShared<FJsonObject>();
	Quad->SetNumberField(TEXT("x"), QuadX);
	Quad->SetNumberField(TEXT("y"), QuadY);
	Result->SetObjectField(TEXT("quad"), Quad);
	Result->SetNumberField(TEXT("rawHeight"), Heights[(QuadY - Patch.Y1) * Patch.Width() + (QuadX - Patch.X1)]);
	Result->SetStringField(TEXT("note"),
		TEXT("Computed from the merged height field by central difference over the neighbouring vertices, in world centimetres, then rotated into world space. At the very edge of the landscape the difference is one-sided."));
	return MCPResult(Result);
}

// landscape(get_slope_at_point): slope in degrees under one XY.
TSharedPtr<FJsonValue> FLandscapeHandlers::GetSlopeAtPoint(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	ALandscape* Landscape = nullptr;
	ULandscapeInfo* Info = nullptr;
	TSharedPtr<FJsonValue> Error;
	if (!MCPLscResolve(World, Params, Landscape, Info, Error)) return Error;

	FMCPLscRegion Patch;
	int32 QuadX = 0, QuadY = 0;
	if (!MCPLscPointPatch(Landscape, Info, Params, Patch, QuadX, QuadY, Error)) return Error;

	TArray<uint16> Heights;
	if (!MCPLscReadHeights(Info, FGuid(), false, Patch, Heights, Error)) return Error;

	const FTransform ToWorld = Landscape->ActorToWorld();
	const FVector Normal = MCPLscNormalAt(
		Heights, Patch.Width(), Patch.Height(),
		QuadX - Patch.X1, QuadY - Patch.Y1,
		ToWorld.GetScale3D(), ToWorld.GetRotation());
	const double Degrees = MCPLscSlopeDegrees(Normal);

	// The downhill direction: the horizontal part of the normal, reversed.
	FVector Downhill(-Normal.X, -Normal.Y, 0.0);
	Downhill = Downhill.GetSafeNormal();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Landscape->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Landscape->GetPathName());
	Result->SetNumberField(TEXT("slopeDegrees"), Degrees);
	Result->SetNumberField(TEXT("slopeRadians"), FMath::DegreesToRadians(Degrees));
	Result->SetNumberField(TEXT("gradePercent"), FMath::Tan(FMath::DegreesToRadians(FMath::Min(Degrees, 89.9))) * 100.0);
	TSharedPtr<FJsonObject> Down = MakeShared<FJsonObject>();
	Down->SetNumberField(TEXT("x"), Downhill.X);
	Down->SetNumberField(TEXT("y"), Downhill.Y);
	Down->SetNumberField(TEXT("z"), Downhill.Z);
	Result->SetObjectField(TEXT("downhillDirection"), Down);
	TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
	N->SetNumberField(TEXT("x"), Normal.X);
	N->SetNumberField(TEXT("y"), Normal.Y);
	N->SetNumberField(TEXT("z"), Normal.Z);
	Result->SetObjectField(TEXT("normal"), N);
	TSharedPtr<FJsonObject> Quad = MakeShared<FJsonObject>();
	Quad->SetNumberField(TEXT("x"), QuadX);
	Quad->SetNumberField(TEXT("y"), QuadY);
	Result->SetObjectField(TEXT("quad"), Quad);
	return MCPResult(Result);
}

// landscape(get_slope_map): per-vertex slope over a rectangle, plus its shape.
TSharedPtr<FJsonValue> FLandscapeHandlers::GetSlopeMap(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	ALandscape* Landscape = nullptr;
	ULandscapeInfo* Info = nullptr;
	TSharedPtr<FJsonValue> Error;
	if (!MCPLscResolve(World, Params, Landscape, Info, Error)) return Error;

	FMCPLscRegion Region;
	if (!MCPLscResolveRegion(Landscape, Info, Params, 262144, Region, Error)) return Error;

	TArray<uint16> Heights;
	if (!MCPLscReadHeights(Info, FGuid(), false, Region, Heights, Error)) return Error;

	const FTransform ToWorld = Landscape->ActorToWorld();
	const FVector Scale = ToWorld.GetScale3D();
	const FQuat Rotation = ToWorld.GetRotation();
	const int32 Width = Region.Width();
	const int32 Height = Region.Height();

	TArray<double> Slopes;
	Slopes.SetNumUninitialized(Heights.Num());
	double MinSlope = 1e9, MaxSlope = -1e9, SumSlope = 0.0;
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const FVector Normal = MCPLscNormalAt(Heights, Width, Height, X, Y, Scale, Rotation);
			const double Degrees = MCPLscSlopeDegrees(Normal);
			Slopes[Y * Width + X] = Degrees;
			MinSlope = FMath::Min(MinSlope, Degrees);
			MaxSlope = FMath::Max(MaxSlope, Degrees);
			SumSlope += Degrees;
		}
	}

	// A fixed 9-bucket histogram in ten-degree bands. Fixed on purpose: the
	// bands are what a caller reasons about ("anything under 15 degrees is
	// buildable"), and a configurable bin count would make two runs of the same
	// query incomparable.
	int32 Buckets[9] = { 0 };
	for (double Degrees : Slopes)
	{
		Buckets[FMath::Clamp((int32)(Degrees / 10.0), 0, 8)]++;
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Landscape->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Landscape->GetPathName());
	Result->SetObjectField(TEXT("region"), MCPLscRegionJson(Region));
	Result->SetNumberField(TEXT("minSlopeDegrees"), MinSlope);
	Result->SetNumberField(TEXT("maxSlopeDegrees"), MaxSlope);
	Result->SetNumberField(TEXT("meanSlopeDegrees"), Slopes.Num() > 0 ? SumSlope / Slopes.Num() : 0.0);
	TArray<TSharedPtr<FJsonValue>> Histogram;
	for (int32 Bucket = 0; Bucket < 9; ++Bucket)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("minDegrees"), Bucket * 10);
		Row->SetNumberField(TEXT("maxDegrees"), Bucket == 8 ? 90 : (Bucket + 1) * 10);
		Row->SetNumberField(TEXT("count"), Buckets[Bucket]);
		Row->SetNumberField(TEXT("fraction"), Slopes.Num() > 0 ? (double)Buckets[Bucket] / Slopes.Num() : 0.0);
		Histogram.Add(MakeShared<FJsonValueObject>(Row));
	}
	Result->SetArrayField(TEXT("slopeHistogram"), Histogram);

	const int32 ArrayLimit = FMath::Clamp(OptionalInt(Params, TEXT("arrayEncodingLimit"), 16384), 0, 4000000);
	if (OptionalBool(Params, TEXT("includeSlopes"), true) && Slopes.Num() <= ArrayLimit)
	{
		TArray<TSharedPtr<FJsonValue>> Rows;
		Rows.Reserve(Slopes.Num());
		for (double Degrees : Slopes) Rows.Add(MakeShared<FJsonValueNumber>(Degrees));
		Result->SetArrayField(TEXT("slopeDegrees"), Rows);
		Result->SetBoolField(TEXT("slopesIncluded"), true);
	}
	else
	{
		Result->SetBoolField(TEXT("slopesIncluded"), false);
		Result->SetStringField(TEXT("slopesOmittedReason"), FString::Printf(
			TEXT("The per-vertex array holds %d values, above the %d-entry cap, so only the summary is returned. Shrink 'region' or raise 'arrayEncodingLimit' to get the full map."),
			Slopes.Num(), ArrayLimit));
	}
	Result->SetStringField(TEXT("note"),
		TEXT("Row-major from (minX,minY), width = maxX-minX+1. Degrees from horizontal: 0 is flat, 90 is a cliff."));
	return MCPResult(Result);
}

namespace
{
	/** A raised-cosine dome over a normalised distance: 1 at the centre, 0 at
	 *  the rim, flat-tangent at both ends. This is the profile the shape
	 *  operators are built from - a plain linear cone leaves a visible crease
	 *  at the peak and a hard crease where it meets the untouched ground. */
	double MCPLscDome(double Distance)
	{
		const double D = FMath::Clamp(Distance, 0.0, 1.0);
		return 0.5 * (1.0 + FMath::Cos(UE_PI * D));
	}

	/** One 3x3 box blur pass over a height field, edges clamped. */
	void MCPLscBlur(const TArray<double>& In, TArray<double>& Out, int32 Width, int32 Height)
	{
		Out.SetNumUninitialized(In.Num());
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				double Sum = 0.0;
				int32 Count = 0;
				for (int32 DY = -1; DY <= 1; ++DY)
				{
					const int32 SY = FMath::Clamp(Y + DY, 0, Height - 1);
					for (int32 DX = -1; DX <= 1; ++DX)
					{
						const int32 SX = FMath::Clamp(X + DX, 0, Width - 1);
						Sum += In[SY * Width + SX];
						++Count;
					}
				}
				Out[Y * Width + X] = Sum / (double)Count;
			}
		}
	}

	/** The operator names, in one place, so the error that lists them cannot
	 *  fall out of step with the switch that implements them. */
	const TCHAR* MCPLscOperatorList()
	{
		return TEXT("raise, lower, flatten, smooth, mountain, valley, ridge, plateau, crater, terrace");
	}
}

// landscape(sculpt_region): one shaping operator over a rectangle.
//
// One action rather than ten, because the region resolution, the falloff, the
// strength blend, the previous-height capture and the rollback record are the
// same for every shape and only the per-vertex kernel differs. Ten actions
// would be ten copies of that machinery, and the ninth copy is where the
// rollback quietly stops being emitted.
TSharedPtr<FJsonValue> FLandscapeHandlers::SculptRegion(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	ALandscape* Landscape = nullptr;
	ULandscapeInfo* Info = nullptr;
	TSharedPtr<FJsonValue> Error;
	if (!MCPLscResolve(World, Params, Landscape, Info, Error)) return Error;

	FString Operator;
	if (auto Err = RequireString(Params, TEXT("operator"), Operator)) return Err;
	Operator = Operator.ToLower();

	// Resolved to an index ONCE, not compared per vertex: a four-million-vertex
	// region would otherwise run tens of millions of string compares inside the
	// inner loop, and the operator cannot change while it runs.
	static const TCHAR* Known[] = {
		TEXT("raise"), TEXT("lower"), TEXT("flatten"), TEXT("smooth"), TEXT("mountain"),
		TEXT("valley"), TEXT("ridge"), TEXT("plateau"), TEXT("crater"), TEXT("terrace") };
	enum EOp { OpRaise, OpLower, OpFlatten, OpSmooth, OpMountain,
		OpValley, OpRidge, OpPlateau, OpCrater, OpTerrace };
	int32 Op = -1;
	for (int32 Candidate = 0; Candidate < (int32)UE_ARRAY_COUNT(Known); ++Candidate)
	{
		if (Operator == Known[Candidate]) { Op = Candidate; break; }
	}
	if (Op < 0)
	{
		return MCPError(FString::Printf(
			TEXT("Unknown sculpt operator '%s'. Supported: %s."), *Operator, MCPLscOperatorList()));
	}

	FMCPLscRegion Region;
	if (!MCPLscResolveRegion(Landscape, Info, Params, 4000000, Region, Error)) return Error;

	const FMCPLscHeightSpace Space = MCPLscHeightSpaceFor(Landscape);
	if (!Space.bValid)
	{
		return MCPError(TEXT("The landscape has a zero Z scale, so no height can be expressed in world units. Fix the actor scale before sculpting."));
	}

	TArray<uint16> Previous;
	if (!MCPLscReadHeights(Info, FGuid(), false, Region, Previous, Error)) return Error;

	const int32 Width = Region.Width();
	const int32 Height = Region.Height();
	const double Amount = OptionalNumber(Params, TEXT("amount"), 500.0);
	const double AmountRaw = Space.WorldDeltaToRaw(Amount);
	const double Strength = FMath::Clamp(OptionalNumber(Params, TEXT("strength"), 1.0), 0.0, 1.0);
	const double Falloff = FMath::Clamp(OptionalNumber(Params, TEXT("falloff"), 0.5), 0.0, 1.0);
	const double Sharpness = FMath::Clamp(OptionalNumber(Params, TEXT("sharpness"), 1.5), 0.1, 8.0);
	const FString Shape = OptionalString(Params, TEXT("shape"), TEXT("ellipse")).ToLower();
	if (Shape != TEXT("ellipse") && Shape != TEXT("rect"))
	{
		return MCPError(TEXT("'shape' must be \"ellipse\" (radial falloff from the region centre) or \"rect\" (falloff toward the region edges)."));
	}
	const bool bRectShape = Shape == TEXT("rect");

	// Working copy in doubles: the operators compose (smooth is iterative,
	// terrace quantises against the region's own range) and rounding to uint16
	// between steps would stair-step the result.
	TArray<double> Work;
	Work.SetNumUninitialized(Previous.Num());
	double MinRaw = 65535.0, MaxRaw = 0.0, SumRaw = 0.0;
	for (int32 Index = 0; Index < Previous.Num(); ++Index)
	{
		const double H = (double)Previous[Index];
		Work[Index] = H;
		MinRaw = FMath::Min(MinRaw, H);
		MaxRaw = FMath::Max(MaxRaw, H);
		SumRaw += H;
	}
	const double MeanRaw = Previous.Num() > 0 ? SumRaw / Previous.Num() : 0.0;

	const double CentreX = (Region.X1 + Region.X2) * 0.5;
	const double CentreY = (Region.Y1 + Region.Y2) * 0.5;
	const double HalfX = FMath::Max((Region.X2 - Region.X1) * 0.5, 0.5);
	const double HalfY = FMath::Max((Region.Y2 - Region.Y1) * 0.5, 0.5);

	// Per-operator setup that must NOT be recomputed per vertex.
	double FlattenTargetRaw = MeanRaw;
	if (Operator == TEXT("flatten") || Operator == TEXT("plateau"))
	{
		if (Params->HasField(TEXT("targetHeight")))
		{
			FlattenTargetRaw = Space.WorldZToRaw(OptionalNumber(Params, TEXT("targetHeight"), 0.0));
		}
		else if (Operator == TEXT("plateau"))
		{
			FlattenTargetRaw = MeanRaw + AmountRaw;
		}
		else
		{
			const FString FlattenTo = OptionalString(Params, TEXT("flattenTo"), TEXT("mean")).ToLower();
			if (FlattenTo == TEXT("min")) FlattenTargetRaw = MinRaw;
			else if (FlattenTo == TEXT("max")) FlattenTargetRaw = MaxRaw;
			else if (FlattenTo == TEXT("center") || FlattenTo == TEXT("centre"))
			{
				const int32 CX = FMath::Clamp(FMath::RoundToInt32(CentreX) - Region.X1, 0, Width - 1);
				const int32 CY = FMath::Clamp(FMath::RoundToInt32(CentreY) - Region.Y1, 0, Height - 1);
				FlattenTargetRaw = Work[CY * Width + CX];
			}
			else if (FlattenTo != TEXT("mean"))
			{
				return MCPError(TEXT("'flattenTo' must be mean, center, min or max, or pass 'targetHeight' in world Z instead."));
			}
		}
	}

	TArray<double> Blurred;
	const int32 SmoothIterations = FMath::Clamp(OptionalInt(Params, TEXT("iterations"), 1), 1, 64);
	if (Operator == TEXT("smooth"))
	{
		TArray<double> Ping = Work;
		for (int32 Pass = 0; Pass < SmoothIterations; ++Pass)
		{
			MCPLscBlur(Ping, Blurred, Width, Height);
			Ping = Blurred;
		}
		Blurred = MoveTemp(Ping);
	}

	const int32 TerraceSteps = FMath::Clamp(OptionalInt(Params, TEXT("steps"), 8), 2, 256);
	const double TerraceStepRaw = (MaxRaw - MinRaw) / (double)TerraceSteps;

	const double RidgeAngleRad = FMath::DegreesToRadians(OptionalNumber(Params, TEXT("ridgeAngle"), 0.0));
	const double RidgeCos = FMath::Cos(RidgeAngleRad);
	const double RidgeSin = FMath::Sin(RidgeAngleRad);

	const double RimPosition = FMath::Clamp(OptionalNumber(Params, TEXT("rimPosition"), 0.75), 0.1, 0.95);
	const double RimRatio = FMath::Clamp(OptionalNumber(Params, TEXT("rimRatio"), 0.35), 0.0, 4.0);

	for (int32 LocalY = 0; LocalY < Height; ++LocalY)
	{
		for (int32 LocalX = 0; LocalX < Width; ++LocalX)
		{
			const int32 Index = LocalY * Width + LocalX;
			const double NX = ((Region.X1 + LocalX) - CentreX) / HalfX;
			const double NY = ((Region.Y1 + LocalY) - CentreY) / HalfY;
			const double Distance = bRectShape
				? FMath::Max(FMath::Abs(NX), FMath::Abs(NY))
				: FMath::Sqrt(NX * NX + NY * NY);
			// BrushWeight is the smoothstep falloff the circular sculpt already
			// uses, called rather than copied: one falloff curve for the whole
			// category is what makes two calls with the same falloff blend.
			const double Weight = BrushWeight(Distance, Falloff) * Strength;
			const double Current = Work[Index];
			double Next = Current;

			if (Op == OpRaise)
			{
				Next = Current + AmountRaw * Weight;
			}
			else if (Op == OpLower)
			{
				Next = Current - AmountRaw * Weight;
			}
			else if (Op == OpFlatten || Op == OpPlateau)
			{
				Next = FMath::Lerp(Current, FlattenTargetRaw, Weight);
			}
			else if (Op == OpSmooth)
			{
				Next = FMath::Lerp(Current, Blurred[Index], Weight);
			}
			else if (Op == OpMountain)
			{
				Next = Current + AmountRaw * FMath::Pow(MCPLscDome(Distance), Sharpness) * Strength;
			}
			else if (Op == OpValley)
			{
				Next = Current - AmountRaw * FMath::Pow(MCPLscDome(Distance), Sharpness) * Strength;
			}
			else if (Op == OpRidge)
			{
				// Rotate into the ridge's own frame: U runs along the crest,
				// V across it. The crest profile is the dome across V; the
				// falloff tapers the ends along U.
				const double U = NX * RidgeCos + NY * RidgeSin;
				const double V = -NX * RidgeSin + NY * RidgeCos;
				const double Crest = FMath::Pow(MCPLscDome(FMath::Abs(V)), Sharpness);
				const double Along = BrushWeight(FMath::Min(FMath::Abs(U), 1.0), Falloff);
				Next = Current + AmountRaw * Crest * Along * Strength;
			}
			else if (Op == OpCrater)
			{
				double Profile;
				if (Distance <= RimPosition)
				{
					// Bowl: deepest at the centre, meeting zero at the rim.
					Profile = -MCPLscDome(Distance / RimPosition);
				}
				else if (Distance <= 1.0)
				{
					// Rim: a single positive lobe outside the bowl, back to
					// zero at the region edge so nothing steps.
					Profile = RimRatio * FMath::Sin(UE_PI * (Distance - RimPosition) / (1.0 - RimPosition));
				}
				else
				{
					Profile = 0.0;
				}
				Next = Current + AmountRaw * Profile * Strength;
			}
			else if (Op == OpTerrace)
			{
				if (TerraceStepRaw > UE_KINDA_SMALL_NUMBER)
				{
					const double Quantised = MinRaw
						+ FMath::RoundToDouble((Current - MinRaw) / TerraceStepRaw) * TerraceStepRaw;
					Next = FMath::Lerp(Current, Quantised, Weight);
				}
			}

			Work[Index] = Next;
		}
	}

	TArray<uint16> NewHeights;
	NewHeights.SetNumUninitialized(Work.Num());
	int32 ClampedVertices = 0;
	for (int32 Index = 0; Index < Work.Num(); ++Index)
	{
		if (Work[Index] < 0.0 || Work[Index] > 65535.0) ++ClampedVertices;
		NewHeights[Index] = FMCPLscHeightSpace::Clamp16(Work[Index]);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("operator"), Operator);
	Result->SetStringField(TEXT("shape"), Shape);
	Result->SetNumberField(TEXT("amount"), Amount);
	Result->SetNumberField(TEXT("strength"), Strength);
	Result->SetNumberField(TEXT("falloff"), Falloff);
	if (Operator == TEXT("flatten") || Operator == TEXT("plateau"))
	{
		Result->SetNumberField(TEXT("targetHeight"), Space.RawToWorldZ(FlattenTargetRaw));
		Result->SetNumberField(TEXT("targetRawHeight"), FlattenTargetRaw);
	}
	if (Operator == TEXT("smooth")) Result->SetNumberField(TEXT("iterations"), SmoothIterations);
	if (Operator == TEXT("terrace")) Result->SetNumberField(TEXT("steps"), TerraceSteps);
	if (ClampedVertices > 0)
	{
		Result->SetNumberField(TEXT("verticesClampedToHeightRange"), ClampedVertices);
		Result->SetStringField(TEXT("clampWarning"), FString::Printf(
			TEXT("%d vertices ran past the uint16 height range and were clamped, so the shape is flat-topped or flat-bottomed there. ")
			TEXT("Reduce 'amount', or give the landscape actor a larger Z scale so the same world height needs fewer raw units."),
			ClampedVertices));
	}
	return MCPLscWriteHeights(Landscape, Info, Params, Region, Previous, NewHeights, Result);
}

// landscape(apply_erosion): hydraulic or thermal erosion over a rectangle.
TSharedPtr<FJsonValue> FLandscapeHandlers::ApplyErosion(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	ALandscape* Landscape = nullptr;
	ULandscapeInfo* Info = nullptr;
	TSharedPtr<FJsonValue> Error;
	if (!MCPLscResolve(World, Params, Landscape, Info, Error)) return Error;

	const FString Type = OptionalString(Params, TEXT("erosionType"), TEXT("hydraulic")).ToLower();
	if (Type != TEXT("hydraulic") && Type != TEXT("thermal"))
	{
		return MCPError(TEXT("'erosionType' must be \"hydraulic\" (water carves channels and deposits silt) or \"thermal\" (steep slopes slump toward a talus angle)."));
	}

	FMCPLscRegion Region;
	if (!MCPLscResolveRegion(Landscape, Info, Params, 1048576, Region, Error)) return Error;

	const FMCPLscHeightSpace Space = MCPLscHeightSpaceFor(Landscape);
	if (!Space.bValid)
	{
		return MCPError(TEXT("The landscape has a zero Z scale, so erosion has no world height to work in. Fix the actor scale first."));
	}

	const int32 Iterations = FMath::Clamp(OptionalInt(Params, TEXT("iterations"), 20), 1, 2000);
	const int64 Work = Region.Count() * (int64)Iterations;
	const int64 MaxWork = (int64)FMath::Clamp(OptionalInt(Params, TEXT("maxWork"), 40000000), 1, 400000000);
	if (Work > MaxWork)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%d iterations over %lld vertices is %lld cell-steps, above the %lld budget this call runs with. ")
			TEXT("Erosion is O(vertices x iterations) on the game thread, so shrink 'region', drop 'iterations', or raise 'maxWork' knowing the editor is blocked for the duration."),
			Iterations, Region.Count(), Work, MaxWork));
		Obj->SetStringField(TEXT("reason"), TEXT("workBudgetExceeded"));
		Obj->SetNumberField(TEXT("cellSteps"), (double)Work);
		Obj->SetNumberField(TEXT("maxWork"), (double)MaxWork);
		Obj->SetObjectField(TEXT("region"), MCPLscRegionJson(Region));
		return MakeShared<FJsonValueObject>(Obj);
	}

	TArray<uint16> Previous;
	if (!MCPLscReadHeights(Info, FGuid(), false, Region, Previous, Error)) return Error;

	const int32 Width = Region.Width();
	const int32 Height = Region.Height();
	const int32 Count = Previous.Num();
	TArray<double> H;
	H.SetNumUninitialized(Count);
	for (int32 Index = 0; Index < Count; ++Index) H[Index] = (double)Previous[Index];

	const FVector Scale = Space.ToWorld.GetScale3D();
	const double CellSizeCm = FMath::Max(FMath::Abs(Scale.X), UE_KINDA_SMALL_NUMBER);
	const int32 DX4[4] = { 1, -1, 0, 0 };
	const int32 DY4[4] = { 0, 0, 1, -1 };

	if (Type == TEXT("thermal"))
	{
		// Material above the talus angle slumps to its lower neighbours. The
		// angle is a REAL slope, so it is converted through the landscape's own
		// XY and Z scale rather than being applied to raw units directly.
		const double TalusAngle = FMath::Clamp(OptionalNumber(Params, TEXT("talusAngle"), 35.0), 1.0, 89.0);
		const double Strength = FMath::Clamp(OptionalNumber(Params, TEXT("strength"), 0.5), 0.0, 1.0);
		const double TalusRaw = FMath::Tan(FMath::DegreesToRadians(TalusAngle)) * CellSizeCm * Space.RawPerWorldCm;

		TArray<double> Delta;
		Delta.SetNumZeroed(Count);
		for (int32 Pass = 0; Pass < Iterations; ++Pass)
		{
			FMemory::Memzero(Delta.GetData(), Delta.Num() * sizeof(double));
			for (int32 Y = 0; Y < Height; ++Y)
			{
				for (int32 X = 0; X < Width; ++X)
				{
					const int32 Index = Y * Width + X;
					double Excess[4] = { 0.0, 0.0, 0.0, 0.0 };
					double Total = 0.0, Largest = 0.0;
					for (int32 N = 0; N < 4; ++N)
					{
						const int32 NX = X + DX4[N];
						const int32 NY = Y + DY4[N];
						if (NX < 0 || NX >= Width || NY < 0 || NY >= Height) continue;
						const double Drop = H[Index] - H[NY * Width + NX];
						if (Drop > TalusRaw)
						{
							Excess[N] = Drop - TalusRaw;
							Total += Excess[N];
							Largest = FMath::Max(Largest, Excess[N]);
						}
					}
					if (Total <= 0.0) continue;
					// Half the largest excess is the classic move: it cannot
					// invert the slope it is levelling, which is what keeps the
					// pass stable however many times it is run.
					const double Move = Strength * Largest * 0.5;
					for (int32 N = 0; N < 4; ++N)
					{
						if (Excess[N] <= 0.0) continue;
						const double Share = Move * (Excess[N] / Total);
						Delta[Index] -= Share;
						Delta[(Y + DY4[N]) * Width + (X + DX4[N])] += Share;
					}
				}
			}
			for (int32 Index = 0; Index < Count; ++Index) H[Index] += Delta[Index];
		}
	}
	else
	{
		// Grid hydraulic erosion: rain, flow downhill, carry sediment to
		// capacity, deposit what the water can no longer hold, evaporate.
		const double RainCm = FMath::Max(OptionalNumber(Params, TEXT("rainAmount"), 0.5), 0.0);
		const double Rain = Space.WorldDeltaToRaw(RainCm);
		const double Evaporation = FMath::Clamp(OptionalNumber(Params, TEXT("evaporation"), 0.5), 0.01, 1.0);
		const double Capacity = FMath::Clamp(OptionalNumber(Params, TEXT("sedimentCapacity"), 0.6), 0.0, 10.0);
		const double ErosionRate = FMath::Clamp(OptionalNumber(Params, TEXT("erosionRate"), 0.3), 0.0, 1.0);
		const double DepositionRate = FMath::Clamp(OptionalNumber(Params, TEXT("depositionRate"), 0.3), 0.0, 1.0);

		TArray<double> Water, Sediment, WaterDelta, SedimentDelta;
		Water.SetNumZeroed(Count);
		Sediment.SetNumZeroed(Count);
		WaterDelta.SetNumZeroed(Count);
		SedimentDelta.SetNumZeroed(Count);

		for (int32 Pass = 0; Pass < Iterations; ++Pass)
		{
			for (int32 Index = 0; Index < Count; ++Index) Water[Index] += Rain;
			FMemory::Memzero(WaterDelta.GetData(), WaterDelta.Num() * sizeof(double));
			FMemory::Memzero(SedimentDelta.GetData(), SedimentDelta.Num() * sizeof(double));

			for (int32 Y = 0; Y < Height; ++Y)
			{
				for (int32 X = 0; X < Width; ++X)
				{
					const int32 Index = Y * Width + X;
					if (Water[Index] <= 0.0) continue;
					const double Altitude = H[Index] + Water[Index];
					double Drops[4] = { 0.0, 0.0, 0.0, 0.0 };
					int32 Neighbours[4] = { -1, -1, -1, -1 };
					double TotalDrop = 0.0;
					double AltitudeSum = Altitude;
					int32 Lower = 0;
					for (int32 N = 0; N < 4; ++N)
					{
						const int32 NX = X + DX4[N];
						const int32 NY = Y + DY4[N];
						if (NX < 0 || NX >= Width || NY < 0 || NY >= Height) continue;
						const int32 NIndex = NY * Width + NX;
						const double Drop = Altitude - (H[NIndex] + Water[NIndex]);
						if (Drop <= 0.0) continue;
						Drops[N] = Drop;
						Neighbours[N] = NIndex;
						TotalDrop += Drop;
						AltitudeSum += H[NIndex] + Water[NIndex];
						++Lower;
					}
					if (Lower == 0 || TotalDrop <= 0.0) continue;

					// Move no more water than would level this cell against the
					// average of itself and its lower neighbours: that is what
					// stops the field oscillating instead of draining.
					const double Movable = FMath::Min(Water[Index], Altitude - AltitudeSum / (double)(Lower + 1));
					if (Movable <= 0.0) continue;
					const double SedimentFraction = Sediment[Index] / FMath::Max(Water[Index], UE_KINDA_SMALL_NUMBER);
					for (int32 N = 0; N < 4; ++N)
					{
						if (Neighbours[N] < 0) continue;
						const double MovedWater = Movable * (Drops[N] / TotalDrop);
						WaterDelta[Index] -= MovedWater;
						WaterDelta[Neighbours[N]] += MovedWater;
						const double MovedSediment = MovedWater * SedimentFraction;
						SedimentDelta[Index] -= MovedSediment;
						SedimentDelta[Neighbours[N]] += MovedSediment;
					}
				}
			}

			for (int32 Index = 0; Index < Count; ++Index)
			{
				Water[Index] = FMath::Max(Water[Index] + WaterDelta[Index], 0.0);
				Sediment[Index] = FMath::Max(Sediment[Index] + SedimentDelta[Index], 0.0);

				const double Holdable = Capacity * Water[Index];
				if (Sediment[Index] > Holdable)
				{
					const double Deposit = DepositionRate * (Sediment[Index] - Holdable);
					Sediment[Index] -= Deposit;
					H[Index] += Deposit;
				}
				else
				{
					const double Eroded = ErosionRate * (Holdable - Sediment[Index]);
					Sediment[Index] += Eroded;
					H[Index] -= Eroded;
				}
				Water[Index] *= (1.0 - Evaporation);
			}
		}
		// Whatever is still suspended has to land somewhere, or the pass quietly
		// removes material from the terrain and the total volume drifts down
		// every time erosion is run.
		for (int32 Index = 0; Index < Count; ++Index) H[Index] += Sediment[Index];
	}

	TArray<uint16> NewHeights;
	NewHeights.SetNumUninitialized(Count);
	for (int32 Index = 0; Index < Count; ++Index) NewHeights[Index] = FMCPLscHeightSpace::Clamp16(H[Index]);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("erosionType"), Type);
	Result->SetNumberField(TEXT("iterations"), Iterations);
	Result->SetNumberField(TEXT("cellSteps"), (double)Work);
	Result->SetStringField(TEXT("modelNote"),
		TEXT("A grid model run on the CPU over the region, not the Landscape Mode erosion tool: hydraulic rains on every vertex, routes water to lower 4-neighbours, carries sediment to capacity and deposits the rest; thermal slumps anything steeper than the talus angle. Results are shaped by iteration count, so raise it gradually and re-read the region."));
	return MCPLscWriteHeights(Landscape, Info, Params, Region, Previous, NewHeights, Result);
}

namespace
{
	/** Absolute path for a caller-supplied file path. A relative path resolves
	 *  under the project's Saved directory, which is the one place a bridge
	 *  call can always write and the one the caller can always find again. */
	FString MCPLscResolveFilePath(const FString& Path)
	{
		FString Resolved = Path;
		FPaths::NormalizeFilename(Resolved);
		if (FPaths::IsRelative(Resolved))
		{
			Resolved = FPaths::Combine(FPaths::ProjectSavedDir(), Resolved);
		}
		return FPaths::ConvertRelativePathToFull(Resolved);
	}

	/** png16 or raw16, from the caller or from the file extension. */
	bool MCPLscResolveHeightmapFormat(
		const TSharedPtr<FJsonObject>& Params, const FString& Path, FString& OutFormat, FString& OutError)
	{
		OutFormat = OptionalString(Params, TEXT("format")).ToLower();
		if (OutFormat.IsEmpty())
		{
			const FString Extension = FPaths::GetExtension(Path).ToLower();
			if (Extension == TEXT("png")) OutFormat = TEXT("png16");
			else if (Extension == TEXT("raw") || Extension == TEXT("r16")) OutFormat = TEXT("raw16");
			else
			{
				OutError = FString::Printf(
					TEXT("Could not tell the heightmap format from the extension '.%s'. Pass format as \"png16\" (16-bit greyscale PNG) or \"raw16\" (headerless little-endian uint16)."),
					*Extension);
				return false;
			}
		}
		if (OutFormat != TEXT("png16") && OutFormat != TEXT("raw16"))
		{
			OutError = FString::Printf(
				TEXT("'format' must be \"png16\" or \"raw16\"; got '%s'. 8-bit images are refused rather than stretched, because a 256-step heightmap quantises terrain into visible steps."),
				*OutFormat);
			return false;
		}
		return true;
	}

	/** Bilinear resample of a uint16 field. Used only when the caller opts in:
	 *  a silent resample is how an import lands at the wrong scale. */
	void MCPLscResample(
		const TArray<uint16>& Src, int32 SrcW, int32 SrcH,
		TArray<uint16>& Dst, int32 DstW, int32 DstH)
	{
		Dst.SetNumUninitialized(DstW * DstH);
		for (int32 Y = 0; Y < DstH; ++Y)
		{
			const double SY = DstH > 1 ? (double)Y * (SrcH - 1) / (double)(DstH - 1) : 0.0;
			const int32 Y0 = FMath::Clamp((int32)SY, 0, SrcH - 1);
			const int32 Y1 = FMath::Min(Y0 + 1, SrcH - 1);
			const double FY = SY - Y0;
			for (int32 X = 0; X < DstW; ++X)
			{
				const double SX = DstW > 1 ? (double)X * (SrcW - 1) / (double)(DstW - 1) : 0.0;
				const int32 X0 = FMath::Clamp((int32)SX, 0, SrcW - 1);
				const int32 X1 = FMath::Min(X0 + 1, SrcW - 1);
				const double FX = SX - X0;
				const double Top = FMath::Lerp((double)Src[Y0 * SrcW + X0], (double)Src[Y0 * SrcW + X1], FX);
				const double Bottom = FMath::Lerp((double)Src[Y1 * SrcW + X0], (double)Src[Y1 * SrcW + X1], FX);
				Dst[Y * DstW + X] = FMCPLscHeightSpace::Clamp16(FMath::Lerp(Top, Bottom, FY));
			}
		}
	}
}

// landscape(export_heightmap): write the region's heights to a 16-bit file.
TSharedPtr<FJsonValue> FLandscapeHandlers::ExportHeightmap(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	FString FilePath;
	if (auto Err = RequireStringAlt(Params, TEXT("filePath"), TEXT("outputPath"), FilePath)) return Err;

	ALandscape* Landscape = nullptr;
	ULandscapeInfo* Info = nullptr;
	TSharedPtr<FJsonValue> Error;
	if (!MCPLscResolve(World, Params, Landscape, Info, Error)) return Error;

	const FString Resolved = MCPLscResolveFilePath(FilePath);
	FString Format, FormatError;
	if (!MCPLscResolveHeightmapFormat(Params, Resolved, Format, FormatError)) return MCPError(FormatError);

	FMCPLscRegion Region;
	if (!MCPLscResolveRegion(Landscape, Info, Params, 16000000, Region, Error)) return Error;

	FGuid LayerGuid;
	FString LayerName;
	bool bUseLayer = false;
	if (!MCPLscResolveReadLayer(Landscape, Params, LayerGuid, LayerName, bUseLayer, Error)) return Error;

	TArray<uint16> Heights;
	if (!MCPLscReadHeights(Info, LayerGuid, bUseLayer, Region, Heights, Error)) return Error;

	const bool bFileExisted = IFileManager::Get().FileExists(*Resolved);
	if (bFileExisted && !OptionalBool(Params, TEXT("overwrite"), true))
	{
		return MCPError(FString::Printf(
			TEXT("'%s' already exists and overwrite is false. Pass overwrite true, or choose another path."), *Resolved));
	}

	const int32 Width = Region.Width();
	const int32 Height = Region.Height();
	TArray64<uint8> Bytes;
	if (Format == TEXT("png16"))
	{
		IImageWrapperModule& ImageWrapperModule =
			FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		TSharedPtr<IImageWrapper> PngWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (!PngWrapper.IsValid()
			|| !PngWrapper->SetRaw(Heights.GetData(), (int64)Heights.Num() * sizeof(uint16),
				Width, Height, ERGBFormat::Gray, 16))
		{
			return MCPError(FString::Printf(
				TEXT("The %dx%d region could not be encoded as a 16-bit greyscale PNG."), Width, Height));
		}
		Bytes = PngWrapper->GetCompressed(100);
	}
	else
	{
		Bytes.SetNumUninitialized((int64)Heights.Num() * sizeof(uint16));
		FMemory::Memcpy(Bytes.GetData(), Heights.GetData(), Bytes.Num());
	}

	if (!FFileHelper::SaveArrayToFile(Bytes, *Resolved))
	{
		return MCPError(FString::Printf(
			TEXT("The heightmap was encoded but could not be written to '%s'. The directory may not exist or may be read-only."),
			*Resolved));
	}

	const FMCPLscHeightSpace Space = MCPLscHeightSpaceFor(Landscape);
	auto Result = MCPSuccess();
	if (bFileExisted) MCPSetExisted(Result); else MCPSetCreated(Result);
	Result->SetBoolField(TEXT("overwroteExistingFile"), bFileExisted);
	Result->SetStringField(TEXT("actorLabel"), Landscape->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Landscape->GetPathName());
	Result->SetStringField(TEXT("filePath"), Resolved);
	Result->SetStringField(TEXT("format"), Format);
	Result->SetStringField(TEXT("editLayer"), LayerName);
	Result->SetBoolField(TEXT("merged"), !bUseLayer);
	Result->SetNumberField(TEXT("width"), Width);
	Result->SetNumberField(TEXT("height"), Height);
	Result->SetNumberField(TEXT("bytesWritten"), (double)Bytes.Num());
	Result->SetObjectField(TEXT("region"), MCPLscRegionJson(Region));
	Result->SetObjectField(TEXT("heightRange"), MCPLscHeightStats(Heights, Space));
	Result->SetBoolField(TEXT("rollbackOmitted"), true);
	Result->SetStringField(TEXT("rollbackOmittedReason"),
		bFileExisted
			? TEXT("This overwrote an existing file and the previous contents were not read back, so the write cannot be undone through the bridge. Move or copy the old file first if it mattered.")
			: TEXT("No inverse is emitted for a file this call created: the bridge has no delete-file method, and deleting a path the caller chose is not something a rollback should do on its own."));
	// Both fields, for the same reason remove_layer_info below states: they
	// answer different questions. rollbackOmitted says the previous file
	// contents were not carried; rollbackPossible says no call restores them
	// even if they had been. The landscape itself is untouched, so this is the
	// argument asset(export_uv_layout) carries, applied to a heightmap file.
	MCPSetNoRollback(Result, FString::Printf(
		TEXT("Wrote the %s heightmap '%s' and changed no landscape, actor or asset state. An export produces an ")
		TEXT("output artifact, and deleting a file that this same call regenerates on demand is not a meaningful ")
		TEXT("undo, so no inverse is named. Undoing it would need a delete-file call the bridge does not have.%s"),
		*Format, *Resolved,
		bFileExisted
			? TEXT(" The file that was already at that path is gone and no copy was kept.")
			: TEXT("")));
	Result->SetStringField(TEXT("note"), FString::Printf(
		TEXT("%s Row-major from (minX,minY). Values are the landscape's raw uint16 heights, 32768 at the actor's own Z, one unit per 1/128 local cm."),
		Format == TEXT("png16")
			? TEXT("16-bit greyscale PNG.")
			: TEXT("Headerless little-endian uint16; import it back with width and height, since a raw file carries neither.")));
	return MCPResult(Result);
}

// landscape(import_heightmap): write a 16-bit file into the region.
TSharedPtr<FJsonValue> FLandscapeHandlers::ImportHeightmap(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	FString FilePath;
	if (auto Err = RequireStringAlt(Params, TEXT("filePath"), TEXT("sourcePath"), FilePath)) return Err;

	ALandscape* Landscape = nullptr;
	ULandscapeInfo* Info = nullptr;
	TSharedPtr<FJsonValue> Error;
	if (!MCPLscResolve(World, Params, Landscape, Info, Error)) return Error;

	const FString Resolved = MCPLscResolveFilePath(FilePath);
	if (!IFileManager::Get().FileExists(*Resolved))
	{
		return MCPError(FString::Printf(
			TEXT("No heightmap file at '%s'. A relative path resolves under the project's Saved directory; pass an absolute path to read from anywhere else."),
			*Resolved));
	}
	FString Format, FormatError;
	if (!MCPLscResolveHeightmapFormat(Params, Resolved, Format, FormatError)) return MCPError(FormatError);

	TArray<uint8> FileBytes;
	if (!FFileHelper::LoadFileToArray(FileBytes, *Resolved))
	{
		return MCPError(FString::Printf(TEXT("'%s' exists but could not be read."), *Resolved));
	}

	FMCPLscRegion Region;
	if (!MCPLscResolveRegion(Landscape, Info, Params, 16000000, Region, Error)) return Error;

	int32 SourceWidth = 0, SourceHeight = 0;
	TArray<uint16> Source;
	if (Format == TEXT("png16"))
	{
		IImageWrapperModule& ImageWrapperModule =
			FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		TSharedPtr<IImageWrapper> PngWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (!PngWrapper.IsValid() || !PngWrapper->SetCompressed(FileBytes.GetData(), FileBytes.Num()))
		{
			return MCPError(FString::Printf(TEXT("'%s' is not a PNG this engine can decode."), *Resolved));
		}
		// The engine's cross-format GetRaw conversions are documented as
		// unreliable, so a heightmap that is not already 16-bit greyscale is
		// refused with its real format rather than silently reinterpreted.
		if (PngWrapper->GetFormat() != ERGBFormat::Gray || PngWrapper->GetBitDepth() != 16)
		{
			return MCPError(FString::Printf(
				TEXT("'%s' is a %d-bit PNG in format %d, not the 16-bit greyscale a landscape heightmap needs. ")
				TEXT("Re-export it as 16-bit grey (Photoshop: Image > Mode > Grayscale then 16 Bits/Channel; ")
				TEXT("Gaea/World Machine: the 16-bit PNG or RAW preset), or pass a raw16 file instead."),
				*Resolved, PngWrapper->GetBitDepth(), (int32)PngWrapper->GetFormat()));
		}
		TArray64<uint8> Raw;
		if (!PngWrapper->GetRaw(ERGBFormat::Gray, 16, Raw))
		{
			return MCPError(FString::Printf(TEXT("'%s' decoded but its pixels could not be read."), *Resolved));
		}
		SourceWidth = (int32)PngWrapper->GetWidth();
		SourceHeight = (int32)PngWrapper->GetHeight();
		Source.SetNumUninitialized(SourceWidth * SourceHeight);
		FMemory::Memcpy(Source.GetData(), Raw.GetData(),
			FMath::Min((int64)Source.Num() * (int64)sizeof(uint16), Raw.Num()));
	}
	else
	{
		SourceWidth = OptionalInt(Params, TEXT("width"), Region.Width());
		SourceHeight = OptionalInt(Params, TEXT("height"), Region.Height());
		if (SourceWidth <= 0 || SourceHeight <= 0)
		{
			return MCPError(TEXT("'width' and 'height' must be positive for a raw16 import."));
		}
		const int64 Expected = (int64)SourceWidth * (int64)SourceHeight * 2;
		if (FileBytes.Num() != Expected)
		{
			const int32 Square = FMath::RoundToInt32(FMath::Sqrt((double)FileBytes.Num() / 2.0));
			return MCPError(FString::Printf(
				TEXT("'%s' is %d bytes; %dx%d of little-endian uint16 needs %lld. ")
				TEXT("A raw file carries no dimensions, so 'width' and 'height' have to be right - %d bytes would be %dx%d if the image is square."),
				*Resolved, FileBytes.Num(), SourceWidth, SourceHeight, Expected, FileBytes.Num(), Square, Square));
		}
		Source.SetNumUninitialized(SourceWidth * SourceHeight);
		FMemory::Memcpy(Source.GetData(), FileBytes.GetData(), FileBytes.Num());
	}

	const int32 Width = Region.Width();
	const int32 Height = Region.Height();
	TArray<uint16> NewHeights;
	bool bResampled = false;
	if (SourceWidth == Width && SourceHeight == Height)
	{
		NewHeights = MoveTemp(Source);
	}
	else if (OptionalBool(Params, TEXT("resample"), false))
	{
		MCPLscResample(Source, SourceWidth, SourceHeight, NewHeights, Width, Height);
		bResampled = true;
	}
	else
	{
		return MCPError(FString::Printf(
			TEXT("The image is %dx%d but the target region is %dx%d ((%d,%d)-(%d,%d)). ")
			TEXT("Pass resample true to bilinearly fit it, or size the region to the image: ")
			TEXT("region {minX: %d, minY: %d, maxX: %d, maxY: %d}."),
			SourceWidth, SourceHeight, Width, Height, Region.X1, Region.Y1, Region.X2, Region.Y2,
			Region.X1, Region.Y1, Region.X1 + SourceWidth - 1, Region.Y1 + SourceHeight - 1));
	}

	const FMCPLscHeightSpace Space = MCPLscHeightSpaceFor(Landscape);
	if (!Space.bValid)
	{
		return MCPError(TEXT("The landscape has a zero Z scale, so imported heights have no world meaning. Fix the actor scale first."));
	}

	// Optional remap: a heightmap authored elsewhere uses the full 0..65535
	// range for its own elevation band, and the useful control is what that
	// band means in world Z here.
	bool bRemapped = false;
	if (Params->HasField(TEXT("minHeight")) && Params->HasField(TEXT("maxHeight")))
	{
		const double MinZ = OptionalNumber(Params, TEXT("minHeight"), 0.0);
		const double MaxZ = OptionalNumber(Params, TEXT("maxHeight"), 0.0);
		if (MaxZ <= MinZ)
		{
			return MCPError(TEXT("'maxHeight' must be greater than 'minHeight'; they are the world Z the image's 0 and 65535 map to."));
		}
		const double MinRawTarget = Space.WorldZToRaw(MinZ);
		const double MaxRawTarget = Space.WorldZToRaw(MaxZ);
		for (uint16& Value : NewHeights)
		{
			Value = FMCPLscHeightSpace::Clamp16(
				FMath::Lerp(MinRawTarget, MaxRawTarget, (double)Value / 65535.0));
		}
		bRemapped = true;
	}

	TArray<uint16> Previous;
	if (!MCPLscReadHeights(Info, FGuid(), false, Region, Previous, Error)) return Error;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("filePath"), Resolved);
	Result->SetStringField(TEXT("format"), Format);
	Result->SetNumberField(TEXT("sourceWidth"), SourceWidth);
	Result->SetNumberField(TEXT("sourceHeight"), SourceHeight);
	Result->SetBoolField(TEXT("resampled"), bResampled);
	Result->SetBoolField(TEXT("remappedToWorldRange"), bRemapped);
	if (bResampled)
	{
		Result->SetStringField(TEXT("resampleNote"), FString::Printf(
			TEXT("Bilinearly resampled from %dx%d to %dx%d, so fine detail in the source is averaged away."),
			SourceWidth, SourceHeight, Width, Height));
	}
	return MCPLscWriteHeights(Landscape, Info, Params, Region, Previous, NewHeights, Result);
}

// landscape(analyze_terrain): height and slope distribution, and the biggest
// flat rectangle in the region.
TSharedPtr<FJsonValue> FLandscapeHandlers::AnalyzeTerrain(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	ALandscape* Landscape = nullptr;
	ULandscapeInfo* Info = nullptr;
	TSharedPtr<FJsonValue> Error;
	if (!MCPLscResolve(World, Params, Landscape, Info, Error)) return Error;

	FMCPLscRegion Region;
	if (!MCPLscResolveRegion(Landscape, Info, Params, 4000000, Region, Error)) return Error;

	TArray<uint16> Heights;
	if (!MCPLscReadHeights(Info, FGuid(), false, Region, Heights, Error)) return Error;

	const FMCPLscHeightSpace Space = MCPLscHeightSpaceFor(Landscape);
	const FTransform ToWorld = Space.ToWorld;
	const FVector Scale = ToWorld.GetScale3D();
	const FQuat Rotation = ToWorld.GetRotation();
	const int32 Width = Region.Width();
	const int32 Height = Region.Height();
	const int32 Count = Heights.Num();

	uint16 MinRaw = MAX_uint16, MaxRaw = 0;
	for (uint16 H : Heights) { MinRaw = FMath::Min(MinRaw, H); MaxRaw = FMath::Max(MaxRaw, H); }

	const int32 Bins = FMath::Clamp(OptionalInt(Params, TEXT("histogramBins"), 16), 2, 256);
	TArray<int32> HeightBins;
	HeightBins.SetNumZeroed(Bins);
	const double RawSpan = FMath::Max((double)MaxRaw - (double)MinRaw, UE_KINDA_SMALL_NUMBER);
	for (uint16 H : Heights)
	{
		HeightBins[FMath::Clamp((int32)(((double)H - MinRaw) / RawSpan * Bins), 0, Bins - 1)]++;
	}

	const double FlatThreshold = FMath::Clamp(OptionalNumber(Params, TEXT("slopeThresholdDegrees"), 10.0), 0.0, 90.0);
	int32 SlopeBins[9] = { 0 };
	TArray<uint8> Flat;
	Flat.SetNumZeroed(Count);
	double MinSlope = 1e9, MaxSlope = -1e9, SumSlope = 0.0;
	int32 FlatCount = 0;
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const FVector Normal = MCPLscNormalAt(Heights, Width, Height, X, Y, Scale, Rotation);
			const double Degrees = MCPLscSlopeDegrees(Normal);
			SlopeBins[FMath::Clamp((int32)(Degrees / 10.0), 0, 8)]++;
			MinSlope = FMath::Min(MinSlope, Degrees);
			MaxSlope = FMath::Max(MaxSlope, Degrees);
			SumSlope += Degrees;
			if (Degrees <= FlatThreshold) { Flat[Y * Width + X] = 1; ++FlatCount; }
		}
	}

	// Largest all-flat axis-aligned rectangle, by the histogram method: for each
	// row, the run of flat cells above each column, then the largest rectangle
	// in that histogram. This is the answer to "where can I put a building",
	// which a flat-cell COUNT on its own never is - ten thousand scattered flat
	// vertices and one flat plateau report the same number.
	TArray<int32> Run;
	Run.SetNumZeroed(Width);
	int32 BestArea = 0, BestX1 = 0, BestY1 = 0, BestX2 = -1, BestY2 = -1;
	TArray<int32> Stack;
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			Run[X] = Flat[Y * Width + X] ? Run[X] + 1 : 0;
		}
		Stack.Reset();
		for (int32 X = 0; X <= Width; ++X)
		{
			const int32 CurrentRun = X < Width ? Run[X] : 0;
			while (Stack.Num() > 0 && Run[Stack.Last()] >= CurrentRun)
			{
				const int32 TopColumn = Stack.Pop();
				const int32 BarHeight = Run[TopColumn];
				const int32 Left = Stack.Num() > 0 ? Stack.Last() + 1 : 0;
				const int32 BarWidth = X - Left;
				if (BarHeight > 0 && BarHeight * BarWidth > BestArea)
				{
					BestArea = BarHeight * BarWidth;
					BestX1 = Region.X1 + Left;
					BestX2 = Region.X1 + X - 1;
					BestY2 = Region.Y1 + Y;
					BestY1 = BestY2 - BarHeight + 1;
				}
			}
			Stack.Push(X);
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Landscape->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Landscape->GetPathName());
	Result->SetObjectField(TEXT("region"), MCPLscRegionJson(Region));
	Result->SetObjectField(TEXT("height"), MCPLscHeightStats(Heights, Space));

	TArray<TSharedPtr<FJsonValue>> HeightRows;
	for (int32 Bin = 0; Bin < Bins; ++Bin)
	{
		const double LowRaw = MinRaw + RawSpan * Bin / Bins;
		const double HighRaw = MinRaw + RawSpan * (Bin + 1) / Bins;
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("minZ"), Space.RawToWorldZ(LowRaw));
		Row->SetNumberField(TEXT("maxZ"), Space.RawToWorldZ(HighRaw));
		Row->SetNumberField(TEXT("count"), HeightBins[Bin]);
		Row->SetNumberField(TEXT("fraction"), Count > 0 ? (double)HeightBins[Bin] / Count : 0.0);
		HeightRows.Add(MakeShared<FJsonValueObject>(Row));
	}
	Result->SetArrayField(TEXT("heightHistogram"), HeightRows);

	TArray<TSharedPtr<FJsonValue>> SlopeRows;
	for (int32 Bin = 0; Bin < 9; ++Bin)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("minDegrees"), Bin * 10);
		Row->SetNumberField(TEXT("maxDegrees"), Bin == 8 ? 90 : (Bin + 1) * 10);
		Row->SetNumberField(TEXT("count"), SlopeBins[Bin]);
		Row->SetNumberField(TEXT("fraction"), Count > 0 ? (double)SlopeBins[Bin] / Count : 0.0);
		SlopeRows.Add(MakeShared<FJsonValueObject>(Row));
	}
	Result->SetArrayField(TEXT("slopeHistogram"), SlopeRows);
	Result->SetNumberField(TEXT("minSlopeDegrees"), MinSlope);
	Result->SetNumberField(TEXT("maxSlopeDegrees"), MaxSlope);
	Result->SetNumberField(TEXT("meanSlopeDegrees"), Count > 0 ? SumSlope / Count : 0.0);
	Result->SetNumberField(TEXT("slopeThresholdDegrees"), FlatThreshold);
	Result->SetNumberField(TEXT("flatVertexCount"), FlatCount);
	Result->SetNumberField(TEXT("flatFraction"), Count > 0 ? (double)FlatCount / Count : 0.0);

	if (BestArea > 0)
	{
		const FVector CornerA = ToWorld.TransformPosition(FVector(BestX1, BestY1, 0.0));
		const FVector CornerB = ToWorld.TransformPosition(FVector(BestX2, BestY2, 0.0));
		double SumRaw = 0.0;
		for (int32 Y = BestY1; Y <= BestY2; ++Y)
		{
			for (int32 X = BestX1; X <= BestX2; ++X)
			{
				SumRaw += (double)Heights[(Y - Region.Y1) * Width + (X - Region.X1)];
			}
		}
		TSharedPtr<FJsonObject> Area = MakeShared<FJsonObject>();
		Area->SetNumberField(TEXT("minX"), BestX1);
		Area->SetNumberField(TEXT("minY"), BestY1);
		Area->SetNumberField(TEXT("maxX"), BestX2);
		Area->SetNumberField(TEXT("maxY"), BestY2);
		Area->SetNumberField(TEXT("vertexCount"), BestArea);
		TSharedPtr<FJsonObject> AreaWorld = MakeShared<FJsonObject>();
		AreaWorld->SetNumberField(TEXT("minX"), FMath::Min(CornerA.X, CornerB.X));
		AreaWorld->SetNumberField(TEXT("minY"), FMath::Min(CornerA.Y, CornerB.Y));
		AreaWorld->SetNumberField(TEXT("maxX"), FMath::Max(CornerA.X, CornerB.X));
		AreaWorld->SetNumberField(TEXT("maxY"), FMath::Max(CornerA.Y, CornerB.Y));
		Area->SetObjectField(TEXT("world"), AreaWorld);
		Area->SetNumberField(TEXT("meanZ"), Space.RawToWorldZ(SumRaw / BestArea));
		Result->SetObjectField(TEXT("largestFlatArea"), Area);
	}
	else
	{
		Result->SetStringField(TEXT("largestFlatAreaNote"), FString::Printf(
			TEXT("No vertex in the region is flatter than %.1f degrees, so there is no flat rectangle to report. Raise 'slopeThresholdDegrees'."),
			FlatThreshold));
	}

	Result->SetStringField(TEXT("note"),
		TEXT("largestFlatArea is the biggest axis-aligned rectangle whose every vertex is within the slope threshold - the answer to where a building fits, which a flat-vertex count alone never is."));
	return MCPResult(Result);
}

namespace
{
	/** Resolve a paint layer by name against the landscape's registered target
	 *  layers. A miss lists the layers that DO exist: "layer not found" with no
	 *  list is the same sentence for a typo and for a landscape whose material
	 *  never declared the layer, and the fix differs. */
	ULandscapeLayerInfoObject* MCPLscResolveLayer(
		ULandscapeInfo* Info, const FString& LayerName, TSharedPtr<FJsonValue>& OutError)
	{
		OutError.Reset();
		TArray<FString> Known;
		ULandscapeLayerInfoObject* Found = nullptr;
		for (const FLandscapeInfoLayerSettings& Layer : Info->Layers)
		{
			const FString Name = Layer.GetLayerName().ToString();
			Known.Add(Layer.LayerInfoObj ? Name : (Name + TEXT(" (no LayerInfo asset)")));
			if (!Found && Layer.LayerInfoObj && Name.Equals(LayerName, ESearchCase::IgnoreCase))
			{
				Found = Layer.LayerInfoObj;
			}
		}
		if (Found) return Found;

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Paint layer '%s' has no LayerInfo on this landscape. Registered layers: [%s]. ")
			TEXT("Create and register one with landscape(add_layer_info), which makes the ULandscapeLayerInfoObject asset the weight data needs."),
			*LayerName, Known.Num() > 0 ? *FString::Join(Known, TEXT(", ")) : TEXT("none")));
		Obj->SetStringField(TEXT("reason"), TEXT("layerNotFound"));
		Obj->SetStringField(TEXT("layerName"), LayerName);
		Obj->SetArrayField(TEXT("availableLayers"), MCPStringListToJson(Known));
		OutError = MakeShared<FJsonValueObject>(Obj);
		return nullptr;
	}

	bool MCPLscReadWeights(
		ULandscapeInfo* Info,
		ULandscapeLayerInfoObject* LayerInfo,
		const FGuid& EditLayerGuid,
		bool bUseEditLayer,
		const FMCPLscRegion& Region,
		TArray<uint8>& OutWeights,
		TSharedPtr<FJsonValue>& OutError)
	{
		OutError.Reset();
		OutWeights.SetNumZeroed(Region.Count());
		FLandscapeEditDataInterface EditData(Info);
		if (bUseEditLayer) EditData.SetEditLayer(EditLayerGuid);
		int32 GX1 = Region.X1, GY1 = Region.Y1, GX2 = Region.X2, GY2 = Region.Y2;
		EditData.GetWeightData(LayerInfo, GX1, GY1, GX2, GY2, OutWeights.GetData(), 0);
		if (GX2 < GX1 || GY2 < GY1)
		{
			OutError = MCPLscRegionError(
				TEXT("No landscape weight data in that region: the covering components are not loaded. Pin them with level(load_actor_descs) first."),
				Info, Info->GetLandscapeProxy(), TEXT("componentsNotLoaded"));
			return false;
		}
		return true;
	}

	FString MCPLscEncodeWeights(const TArray<uint8>& Weights)
	{
		return FBase64::Encode(Weights.GetData(), (uint32)Weights.Num());
	}

	/** THE weight write, mirroring MCPLscWriteHeights: one idempotency marker,
	 *  one rollback record, one resulting-range report. */
	TSharedPtr<FJsonValue> MCPLscWriteWeights(
		ALandscape* Landscape,
		ULandscapeInfo* Info,
		ULandscapeLayerInfoObject* LayerInfo,
		const FString& LayerName,
		const TCHAR* InverseMethod,
		bool bPayloadCarriesLayerName,
		const TSharedPtr<FJsonObject>& Params,
		const FMCPLscRegion& Region,
		const TArray<uint8>& Previous,
		const TArray<uint8>& NewWeights,
		TSharedPtr<FJsonObject> Result)
	{
		FGuid EditLayerGuid;
		FString EditLayerName;
		TSharedPtr<FJsonValue> LayerError;
		if (!MCPLscRequireEditLayer(Landscape, Params, EditLayerGuid, EditLayerName, LayerError))
		{
			return LayerError;
		}

		int64 Changed = 0;
		uint8 MinBefore = 255, MaxBefore = 0, MinAfter = 255, MaxAfter = 0;
		for (int32 Index = 0; Index < NewWeights.Num(); ++Index)
		{
			if (NewWeights[Index] != Previous[Index]) ++Changed;
			MinBefore = FMath::Min(MinBefore, Previous[Index]);
			MaxBefore = FMath::Max(MaxBefore, Previous[Index]);
			MinAfter = FMath::Min(MinAfter, NewWeights[Index]);
			MaxAfter = FMath::Max(MaxAfter, NewWeights[Index]);
		}

		Result->SetStringField(TEXT("actorLabel"), Landscape->GetActorLabel());
		Result->SetStringField(TEXT("actorPath"), Landscape->GetPathName());
		Result->SetStringField(TEXT("layerName"), LayerName);
		Result->SetStringField(TEXT("editLayer"), EditLayerName);
		Result->SetObjectField(TEXT("region"), MCPLscRegionJson(Region));
		Result->SetNumberField(TEXT("verticesChanged"), (double)Changed);
		Result->SetNumberField(TEXT("minWeightBefore"), MinBefore);
		Result->SetNumberField(TEXT("maxWeightBefore"), MaxBefore);
		Result->SetNumberField(TEXT("minWeightAfter"), MinAfter);
		Result->SetNumberField(TEXT("maxWeightAfter"), MaxAfter);

		if (Changed == 0)
		{
			Result->SetBoolField(TEXT("updated"), false);
			Result->SetBoolField(TEXT("unchanged"), true);
			Result->SetBoolField(TEXT("rollbackOmitted"), false);
			Result->SetStringField(TEXT("note"),
				TEXT("Every vertex already held the weight this call would have written, so nothing was modified and no rollback record is needed."));
			return MCPResult(Result);
		}

		{
			const FScopedTransaction Transaction(FText::FromString(TEXT("MCP landscape weight region")));
			Landscape->Modify();
			FScopedSetLandscapeEditingLayer EditingLayer(Landscape, EditLayerGuid);
			FLandscapeEditDataInterface EditData(Info);
			EditData.SetEditLayer(EditLayerGuid);
			EditData.SetAlphaData(LayerInfo, Region.X1, Region.Y1, Region.X2, Region.Y2,
				NewWeights.GetData(), 0, ELandscapeLayerPaintingRestriction::None);
			EditData.Flush();
		}
		Landscape->PostEditChange();

		MCPSetUpdated(Result);
		Result->SetBoolField(TEXT("unchanged"), false);

		const int64 RollbackCap = (int64)FMath::Clamp(
			OptionalInt(Params, TEXT("rollbackMaxVertices"), 524288), 0, 16000000);
		if (Region.Count() <= RollbackCap)
		{
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("actorPath"), Landscape->GetPathName());
			// The visibility mask is not addressed by layer name, so the record
			// for a hole write must not carry one: a payload naming a layer that
			// no action accepts is a rollback that fails when it is needed.
			if (bPayloadCarriesLayerName) Payload->SetStringField(TEXT("layerName"), LayerName);
			Payload->SetObjectField(TEXT("region"), MCPLscRegionJson(Region));
			Payload->SetStringField(TEXT("space"), TEXT("quad"));
			Payload->SetStringField(TEXT("weightsBase64"), MCPLscEncodeWeights(Previous));
			Payload->SetStringField(TEXT("editLayer"), EditLayerName);
			MCPSetRollback(Result, InverseMethod, Payload);
			Result->SetBoolField(TEXT("rollbackOmitted"), false);
		}
		else
		{
			Result->SetBoolField(TEXT("rollbackOmitted"), true);
			Result->SetStringField(TEXT("rollbackOmittedReason"), FString::Printf(
				TEXT("The region holds %lld vertices, above the %lld-vertex rollback cap, so the previous weights are NOT carried and this edit cannot be undone through the bridge. ")
				TEXT("Read the region first, work in smaller rectangles, or raise 'rollbackMaxVertices'."),
				Region.Count(), RollbackCap));
		}

		Result->SetStringField(TEXT("note"),
			TEXT("Weights are written as given; the engine no longer renormalises the other layers for you, so set them explicitly if they must sum to 1. The level is left dirty and unsaved."));
		return MCPResult(Result);
	}

	/** Build the new weight buffer from whichever form the caller used. */
	bool MCPLscBuildWeights(
		const TSharedPtr<FJsonObject>& Params,
		int32 Count,
		TArray<uint8>& Out,
		FString& OutSource,
		FString& OutError)
	{
		FString Base64;
		const TArray<TSharedPtr<FJsonValue>>* WeightArray = nullptr;
		if (Params->TryGetStringField(TEXT("weightsBase64"), Base64) && !Base64.IsEmpty())
		{
			TArray<uint8> Bytes;
			if (!FBase64::Decode(Base64, Bytes))
			{
				OutError = TEXT("'weightsBase64' is not valid base64.");
				return false;
			}
			if (Bytes.Num() != Count)
			{
				OutError = FString::Printf(
					TEXT("'weightsBase64' decodes to %d bytes; the region needs %d (one uint8 per vertex, row-major)."),
					Bytes.Num(), Count);
				return false;
			}
			Out = MoveTemp(Bytes);
			OutSource = TEXT("weightsBase64");
			return true;
		}
		if (Params->TryGetArrayField(TEXT("weights"), WeightArray) && WeightArray)
		{
			if (WeightArray->Num() != Count)
			{
				OutError = FString::Printf(
					TEXT("'weights' has %d entries but the region holds %d vertices, row-major from (minX,minY)."),
					WeightArray->Num(), Count);
				return false;
			}
			Out.SetNumUninitialized(Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const double Value = (*WeightArray)[Index].IsValid() ? (*WeightArray)[Index]->AsNumber() : 0.0;
				Out[Index] = (uint8)FMath::Clamp(FMath::RoundToInt32(Value), 0, 255);
			}
			OutSource = TEXT("weights");
			return true;
		}
		if (Params->HasField(TEXT("weight")) || Params->HasField(TEXT("strength")))
		{
			const double Value = Params->HasField(TEXT("weight"))
				? OptionalNumber(Params, TEXT("weight"), 1.0)
				: OptionalNumber(Params, TEXT("strength"), 1.0);
			// 0..1 is the schema's own convention for a paint strength, so a
			// value in that band means a fraction and anything above it is
			// already in the 0..255 the engine stores.
			const double Scaled = Value <= 1.0 ? Value * 255.0 : Value;
			Out.Init((uint8)FMath::Clamp(FMath::RoundToInt32(Scaled), 0, 255), Count);
			OutSource = TEXT("weight");
			return true;
		}
		OutError = TEXT("Nothing to write. Pass 'weightsBase64' (one uint8 per vertex, the form the getter hands back), or 'weights' as one number per vertex, or 'weight' as a single 0..1 fraction to fill the region.");
		return false;
	}
}

// landscape(get_layer_weight_region): read one layer's weights over a rectangle.
TSharedPtr<FJsonValue> FLandscapeHandlers::GetLayerWeightRegion(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	FString LayerName;
	if (auto Err = RequireString(Params, TEXT("layerName"), LayerName)) return Err;

	ALandscape* Landscape = nullptr;
	ULandscapeInfo* Info = nullptr;
	TSharedPtr<FJsonValue> Error;
	if (!MCPLscResolve(World, Params, Landscape, Info, Error)) return Error;

	ULandscapeLayerInfoObject* LayerInfo = MCPLscResolveLayer(Info, LayerName, Error);
	if (!LayerInfo) return Error;

	FMCPLscRegion Region;
	if (!MCPLscResolveRegion(Landscape, Info, Params, 262144, Region, Error)) return Error;

	FGuid LayerGuid;
	FString EditLayerName;
	bool bUseLayer = false;
	if (!MCPLscResolveReadLayer(Landscape, Params, LayerGuid, EditLayerName, bUseLayer, Error)) return Error;

	TArray<uint8> Weights;
	if (!MCPLscReadWeights(Info, LayerInfo, LayerGuid, bUseLayer, Region, Weights, Error)) return Error;

	uint8 MinWeight = 255, MaxWeight = 0;
	double Sum = 0.0;
	int32 Painted = 0;
	for (uint8 W : Weights)
	{
		MinWeight = FMath::Min(MinWeight, W);
		MaxWeight = FMath::Max(MaxWeight, W);
		Sum += W;
		if (W > 0) ++Painted;
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Landscape->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Landscape->GetPathName());
	Result->SetStringField(TEXT("layerName"), LayerName);
	Result->SetStringField(TEXT("layerInfoPath"), LayerInfo->GetPathName());
	Result->SetStringField(TEXT("editLayer"), EditLayerName);
	Result->SetBoolField(TEXT("merged"), !bUseLayer);
	Result->SetObjectField(TEXT("region"), MCPLscRegionJson(Region));
	Result->SetNumberField(TEXT("minWeight"), MinWeight);
	Result->SetNumberField(TEXT("maxWeight"), MaxWeight);
	Result->SetNumberField(TEXT("meanWeight"), Weights.Num() > 0 ? Sum / Weights.Num() : 0.0);
	Result->SetNumberField(TEXT("paintedVertexCount"), Painted);
	Result->SetNumberField(TEXT("paintedFraction"), Weights.Num() > 0 ? (double)Painted / Weights.Num() : 0.0);

	const int32 ArrayLimit = FMath::Clamp(OptionalInt(Params, TEXT("arrayEncodingLimit"), 16384), 0, 4000000);
	FString Encoding = OptionalString(Params, TEXT("encoding"), TEXT("auto")).ToLower();
	if (Encoding == TEXT("auto")) Encoding = Weights.Num() <= ArrayLimit ? TEXT("array") : TEXT("base64");
	Result->SetStringField(TEXT("encoding"), Encoding);
	if (OptionalBool(Params, TEXT("includeWeights"), true))
	{
		if (Encoding == TEXT("base64"))
		{
			Result->SetStringField(TEXT("weightsBase64"), MCPLscEncodeWeights(Weights));
		}
		else
		{
			TArray<TSharedPtr<FJsonValue>> Rows;
			Rows.Reserve(Weights.Num());
			for (uint8 W : Weights) Rows.Add(MakeShared<FJsonValueNumber>(W));
			Result->SetArrayField(TEXT("weights"), Rows);
		}
	}
	Result->SetStringField(TEXT("note"),
		TEXT("Weights are 0..255 per vertex, row-major from (minX,minY), width = maxX-minX+1. 255 means this layer alone paints that vertex."));
	return MCPResult(Result);
}

// landscape(set_layer_weight_region): write one layer's weights over a rectangle.
TSharedPtr<FJsonValue> FLandscapeHandlers::SetLayerWeightRegion(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	FString LayerName;
	if (auto Err = RequireString(Params, TEXT("layerName"), LayerName)) return Err;

	ALandscape* Landscape = nullptr;
	ULandscapeInfo* Info = nullptr;
	TSharedPtr<FJsonValue> Error;
	if (!MCPLscResolve(World, Params, Landscape, Info, Error)) return Error;

	ULandscapeLayerInfoObject* LayerInfo = MCPLscResolveLayer(Info, LayerName, Error);
	if (!LayerInfo) return Error;

	FMCPLscRegion Region;
	if (!MCPLscResolveRegion(Landscape, Info, Params, 4000000, Region, Error)) return Error;

	TArray<uint8> NewWeights;
	FString Source, BuildError;
	if (!MCPLscBuildWeights(Params, (int32)Region.Count(), NewWeights, Source, BuildError))
	{
		return MCPError(BuildError);
	}

	TArray<uint8> Previous;
	if (!MCPLscReadWeights(Info, LayerInfo, FGuid(), false, Region, Previous, Error)) return Error;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("weightSource"), Source);
	Result->SetStringField(TEXT("layerInfoPath"), LayerInfo->GetPathName());
	return MCPLscWriteWeights(Landscape, Info, LayerInfo, LayerName,
		TEXT("set_landscape_layer_weight_region"), /*bPayloadCarriesLayerName=*/true,
		Params, Region, Previous, NewWeights, Result);
}

// landscape(layer_exists): is this paint layer registered on the landscape?
TSharedPtr<FJsonValue> FLandscapeHandlers::LayerExists(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	FString LayerName;
	if (auto Err = RequireString(Params, TEXT("layerName"), LayerName)) return Err;

	ALandscape* Landscape = nullptr;
	ULandscapeInfo* Info = nullptr;
	TSharedPtr<FJsonValue> Error;
	if (!MCPLscResolve(World, Params, Landscape, Info, Error)) return Error;

	TArray<TSharedPtr<FJsonValue>> Rows;
	bool bExists = false;
	bool bHasLayerInfo = false;
	FString LayerInfoPath;
	for (const FLandscapeInfoLayerSettings& Layer : Info->Layers)
	{
		const FString Name = Layer.GetLayerName().ToString();
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("layerName"), Name);
		Row->SetBoolField(TEXT("hasLayerInfo"), Layer.LayerInfoObj != nullptr);
		if (Layer.LayerInfoObj) Row->SetStringField(TEXT("layerInfoPath"), Layer.LayerInfoObj->GetPathName());
		Rows.Add(MakeShared<FJsonValueObject>(Row));
		if (Name.Equals(LayerName, ESearchCase::IgnoreCase))
		{
			bExists = true;
			bHasLayerInfo = Layer.LayerInfoObj != nullptr;
			if (Layer.LayerInfoObj) LayerInfoPath = Layer.LayerInfoObj->GetPathName();
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Landscape->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Landscape->GetPathName());
	Result->SetStringField(TEXT("layerName"), LayerName);
	Result->SetBoolField(TEXT("exists"), bExists);
	Result->SetBoolField(TEXT("hasLayerInfo"), bHasLayerInfo);
	if (!LayerInfoPath.IsEmpty()) Result->SetStringField(TEXT("layerInfoPath"), LayerInfoPath);
	Result->SetArrayField(TEXT("layers"), Rows);
	if (bExists && !bHasLayerInfo)
	{
		Result->SetStringField(TEXT("note"),
			TEXT("The landscape material declares this layer but no ULandscapeLayerInfoObject is assigned to it, so it has nowhere to store weights and painting it will fail. landscape(add_layer_info) creates and assigns one."));
	}
	return MCPResult(Result);
}

// landscape(remove_layer): unregister a paint layer and drop its weight data.
TSharedPtr<FJsonValue> FLandscapeHandlers::RemoveLayer(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	FString LayerName;
	if (auto Err = RequireString(Params, TEXT("layerName"), LayerName)) return Err;

	ALandscape* Landscape = nullptr;
	ULandscapeInfo* Info = nullptr;
	TSharedPtr<FJsonValue> Error;
	if (!MCPLscResolve(World, Params, Landscape, Info, Error)) return Error;

	ULandscapeLayerInfoObject* LayerInfo = nullptr;
	FName ResolvedName = NAME_None;
	TArray<FString> Known;
	for (const FLandscapeInfoLayerSettings& Layer : Info->Layers)
	{
		const FString Name = Layer.GetLayerName().ToString();
		Known.Add(Name);
		if (Name.Equals(LayerName, ESearchCase::IgnoreCase))
		{
			LayerInfo = Layer.LayerInfoObj;
			ResolvedName = Layer.GetLayerName();
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Landscape->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Landscape->GetPathName());
	Result->SetStringField(TEXT("layerName"), LayerName);
	Result->SetArrayField(TEXT("layersBefore"), MCPStringListToJson(Known));

	if (ResolvedName.IsNone())
	{
		// Idempotent replay: removing a layer that is not there is the state the
		// caller asked for, not a failure.
		Result->SetBoolField(TEXT("removed"), false);
		Result->SetBoolField(TEXT("alreadyAbsent"), true);
		Result->SetBoolField(TEXT("updated"), false);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("rollbackOmitted"), false);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Nothing was removed, so there is nothing to restore."));
		Result->SetStringField(TEXT("note"), FString::Printf(
			TEXT("No layer named '%s' is registered on this landscape, so nothing was removed. Registered layers: [%s]."),
			*LayerName, Known.Num() > 0 ? *FString::Join(Known, TEXT(", ")) : TEXT("none")));
		return MCPResult(Result);
	}

	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("MCP landscape remove layer")));
		Landscape->Modify();
		Info->DeleteLayer(LayerInfo, ResolvedName);
	}
	Landscape->PostEditChange();

	TArray<FString> After;
	for (const FLandscapeInfoLayerSettings& Layer : Info->Layers) After.Add(Layer.GetLayerName().ToString());

	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("removed"), true);
	Result->SetBoolField(TEXT("alreadyAbsent"), false);
	Result->SetBoolField(TEXT("unchanged"), false);
	Result->SetArrayField(TEXT("layersAfter"), MCPStringListToJson(After));
	if (LayerInfo) Result->SetStringField(TEXT("layerInfoPath"), LayerInfo->GetPathName());
	// Both fields, because they answer different questions and the honest
	// answers differ. rollbackOmitted says the previous DATA was not carried;
	// rollbackPossible says no call can restore it even if it had been, since
	// landscape(add_layer_info) re-registers the layer EMPTY. A record naming
	// that action would look like an undo and silently discard every painted
	// weight the layer held.
	Result->SetBoolField(TEXT("rollbackOmitted"), true);
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("landscape(add_layer_info) re-registers the layer with no weights on it, so it is not an inverse of this "
			 "call and is deliberately not emitted as one. See rollbackOmittedReason."));
	Result->SetStringField(TEXT("rollbackOmittedReason"),
		TEXT("Removing a layer destroys its weightmap across every component of the landscape, and those weights were not read back first - a whole-landscape capture would be tens of megabytes. This is NOT undoable through the bridge. Export the weights with landscape(get_layer_weight_region) first if they matter. The ULandscapeLayerInfoObject asset itself is left on disk, so re-registering it with landscape(add_layer_info) brings the layer back empty."));
	Result->SetStringField(TEXT("note"),
		TEXT("The level is left dirty and unsaved; save it when ready."));
	return MCPResult(Result);
}

// landscape(get_holes): read the visibility mask over a point or a rectangle.
TSharedPtr<FJsonValue> FLandscapeHandlers::GetHoles(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	ALandscape* Landscape = nullptr;
	ULandscapeInfo* Info = nullptr;
	TSharedPtr<FJsonValue> Error;
	if (!MCPLscResolve(World, Params, Landscape, Info, Error)) return Error;

	ULandscapeLayerInfoObject* Visibility = ALandscapeProxy::VisibilityLayer;
	if (!Visibility)
	{
		return MCPError(TEXT("The engine's landscape visibility layer is not available in this session, so holes cannot be read."));
	}

	// A point query is a 1x1 region, so both spellings answer through the same
	// read and cannot disagree about what a hole is.
	FMCPLscRegion Region;
	bool bPointQuery = false;
	int32 QuadX = 0, QuadY = 0;
	if (!Params->HasField(TEXT("region")) && !Params->HasField(TEXT("center")))
	{
		double Ignored = 0.0;
		if (MCPLscReadWorldPoint(Params, Ignored, Ignored))
		{
			if (!MCPLscPointPatch(Landscape, Info, Params, Region, QuadX, QuadY, Error)) return Error;
			Region.X1 = Region.X2 = QuadX;
			Region.Y1 = Region.Y2 = QuadY;
			Region.RequestedX1 = Region.RequestedX2 = QuadX;
			Region.RequestedY1 = Region.RequestedY2 = QuadY;
			bPointQuery = true;
		}
	}
	if (!bPointQuery && !MCPLscResolveRegion(Landscape, Info, Params, 262144, Region, Error)) return Error;

	TArray<uint8> Weights;
	if (!MCPLscReadWeights(Info, Visibility, FGuid(), false, Region, Weights, Error)) return Error;

	// The renderer's own threshold, so "this action says hole" and "the terrain
	// has a hole here" are the same statement.
	const uint8 Threshold = (uint8)FMath::RoundToInt32(LANDSCAPE_VISIBILITY_THRESHOLD * 255.0f);
	int32 HoleCount = 0;
	TArray<TSharedPtr<FJsonValue>> Rows;
	const int32 ArrayLimit = FMath::Clamp(OptionalInt(Params, TEXT("arrayEncodingLimit"), 16384), 0, 4000000);
	const bool bIncludeMask = OptionalBool(Params, TEXT("includeMask"), true) && Weights.Num() <= ArrayLimit;
	for (uint8 W : Weights)
	{
		const bool bHole = W >= Threshold;
		if (bHole) ++HoleCount;
		if (bIncludeMask) Rows.Add(MakeShared<FJsonValueBoolean>(bHole));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Landscape->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Landscape->GetPathName());
	Result->SetObjectField(TEXT("region"), MCPLscRegionJson(Region));
	Result->SetNumberField(TEXT("holeVertexCount"), HoleCount);
	Result->SetNumberField(TEXT("holeFraction"), Weights.Num() > 0 ? (double)HoleCount / Weights.Num() : 0.0);
	Result->SetNumberField(TEXT("visibilityThreshold"), Threshold);
	if (bPointQuery)
	{
		Result->SetBoolField(TEXT("isHole"), HoleCount > 0);
		Result->SetNumberField(TEXT("visibilityWeight"), Weights.Num() > 0 ? Weights[0] : 0);
		TSharedPtr<FJsonObject> Quad = MakeShared<FJsonObject>();
		Quad->SetNumberField(TEXT("x"), QuadX);
		Quad->SetNumberField(TEXT("y"), QuadY);
		Result->SetObjectField(TEXT("quad"), Quad);
	}
	if (bIncludeMask) Result->SetArrayField(TEXT("holes"), Rows);
	Result->SetStringField(TEXT("note"),
		TEXT("Holes are the landscape visibility weightmap: a vertex is a hole once its weight passes the engine's 2/3 threshold. Row-major from (minX,minY). A hole only renders if the landscape material uses the Landscape Visibility Mask node."));
	return MCPResult(Result);
}

// landscape(set_holes): punch or fill the visibility mask over a point or rect.
TSharedPtr<FJsonValue> FLandscapeHandlers::SetHoles(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	ALandscape* Landscape = nullptr;
	ULandscapeInfo* Info = nullptr;
	TSharedPtr<FJsonValue> Error;
	if (!MCPLscResolve(World, Params, Landscape, Info, Error)) return Error;

	ULandscapeLayerInfoObject* Visibility = ALandscapeProxy::VisibilityLayer;
	if (!Visibility)
	{
		return MCPError(TEXT("The engine's landscape visibility layer is not available in this session, so holes cannot be written."));
	}

	FMCPLscRegion Region;
	bool bPointWrite = false;
	int32 QuadX = 0, QuadY = 0;
	if (!Params->HasField(TEXT("region")) && !Params->HasField(TEXT("center")))
	{
		double Ignored = 0.0;
		if (MCPLscReadWorldPoint(Params, Ignored, Ignored))
		{
			if (!MCPLscPointPatch(Landscape, Info, Params, Region, QuadX, QuadY, Error)) return Error;
			Region.X1 = Region.X2 = QuadX;
			Region.Y1 = Region.Y2 = QuadY;
			Region.RequestedX1 = Region.RequestedX2 = QuadX;
			Region.RequestedY1 = Region.RequestedY2 = QuadY;
			bPointWrite = true;
		}
	}
	if (!bPointWrite)
	{
		// Deliberately NOT defaulting to the whole landscape. Every other region
		// action treats an absent region as "all of it", which is a reasonable
		// default for a read and for a sculpt; for this one it would punch every
		// vertex of the terrain into a hole on a call that forgot a parameter.
		if (!Params->HasField(TEXT("region")) && !Params->HasField(TEXT("center")))
		{
			return MCPError(
				TEXT("landscape(set_holes) needs an explicit target: 'region' {minX,minY,maxX,maxY}, or 'center' {x,y} with 'radius', or x and y for a single vertex. ")
				TEXT("Unlike the read actions this one does not default to the whole landscape, because that would punch the entire terrain into a hole."));
		}
		if (!MCPLscResolveRegion(Landscape, Info, Params, 4000000, Region, Error)) return Error;
	}

	const int32 Count = (int32)Region.Count();
	TArray<uint8> NewWeights;
	FString Source;
	const TArray<TSharedPtr<FJsonValue>>* MaskArray = nullptr;
	FString WeightsBase64;
	if (Params->TryGetStringField(TEXT("weightsBase64"), WeightsBase64) && !WeightsBase64.IsEmpty())
	{
		// The form the rollback record carries: the exact previous visibility
		// weights, restored byte for byte rather than re-thresholded.
		TArray<uint8> Bytes;
		if (!FBase64::Decode(WeightsBase64, Bytes))
		{
			return MCPError(TEXT("'weightsBase64' is not valid base64."));
		}
		if (Bytes.Num() != Count)
		{
			return MCPError(FString::Printf(
				TEXT("'weightsBase64' decodes to %d bytes; the region needs %d (one visibility weight per vertex, row-major)."),
				Bytes.Num(), Count));
		}
		NewWeights = MoveTemp(Bytes);
		Source = TEXT("weightsBase64");
	}
	else if (Params->TryGetArrayField(TEXT("holes"), MaskArray) && MaskArray)
	{
		if (MaskArray->Num() != Count)
		{
			return MCPError(FString::Printf(
				TEXT("'holes' has %d entries but the region holds %d vertices, row-major from (minX,minY)."),
				MaskArray->Num(), Count));
		}
		NewWeights.SetNumUninitialized(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			bool bHole = false;
			if ((*MaskArray)[Index].IsValid()) (*MaskArray)[Index]->TryGetBool(bHole);
			NewWeights[Index] = bHole ? 255 : 0;
		}
		Source = TEXT("holes");
	}
	else
	{
		const bool bHole = OptionalBool(Params, TEXT("hole"), true);
		NewWeights.Init(bHole ? 255 : 0, Count);
		Source = TEXT("hole");
	}

	TArray<uint8> Previous;
	if (!MCPLscReadWeights(Info, Visibility, FGuid(), false, Region, Previous, Error)) return Error;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("holeSource"), Source);
	Result->SetBoolField(TEXT("pointWrite"), bPointWrite);
	Result->SetStringField(TEXT("visibilityNote"),
		TEXT("Holes are stored as the landscape visibility weightmap. The data is written regardless, but the hole only renders and only stops collision if the landscape material routes a Landscape Visibility Mask node into opacity mask."));
	return MCPLscWriteWeights(Landscape, Info, Visibility, TEXT("visibility"),
		TEXT("set_landscape_holes"), /*bPayloadCarriesLayerName=*/false,
		Params, Region, Previous, NewWeights, Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// V17, the core half of a real-world terrain pipeline.
//
// The scope decision, written down where the code is rather than only in a
// backlog file: core owns heightmap INGESTION and COORDINATE conversion, and
// does not own fetching DEM tiles over the network or the opinionated
// landcover-to-content passes that turn a landcover grid into roads, fields and
// points of interest.
//
// Why the split falls there. Ingestion is arithmetic against engine invariants
// nobody outside the engine can restate correctly: a landscape's vertex count
// is componentCount x subsectionSizeQuads x numSubsections + 1 with
// subsectionSizeQuads restricted to 7, 15, 31, 63, 127 or 255, and its height
// field is a uint16 whose world meaning is set by the actor's Z scale through
// LANDSCAPE_ZSCALE. Getting either wrong produces a landscape that looks
// plausible and is silently at the wrong scale, and the correction is a rebuild
// rather than an edit. That belongs next to import_landscape_heightmap, which
// is the primitive it feeds.
//
// Network fetching does not. It is a per-provider concern with API keys, rate
// limits, licence terms and tile schemes that change without the engine
// changing, and a bridge that shipped one provider's client would be wrong for
// every other. The content passes do not either: which landcover class becomes
// a forest and which becomes a field is a product opinion, and the plugin
// framework already exists to hold one.
//
// So the pipeline is: fetch a DEM however you like, then
// plan_real_world_landscape to turn that file plus a real-world extent into an
// exact landscape(create) and landscape(import_heightmap) call pair, then
// project_geo_coordinates to place anything carrying a latitude and longitude
// onto the result. Every step after the fetch is core.
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	/** Metres per degree of latitude and of longitude on the WGS84 ellipsoid at
	 *  one latitude, from the standard cosine series.
	 *
	 *  This is a LOCAL TANGENT PLANE approximation, not a datum reprojection:
	 *  it is accurate to a few centimetres per kilometre over a tile a few
	 *  degrees across and is wrong for a continent. That is the right trade for
	 *  a landscape, which is a flat grid and cannot represent curvature at all,
	 *  and every response that uses it says so rather than implying a survey. */
	void MCPLscMetersPerDegree(double LatitudeDegrees, double& OutPerLat, double& OutPerLon)
	{
		const double Phi = FMath::DegreesToRadians(LatitudeDegrees);
		OutPerLat = 111132.92
			- 559.82 * FMath::Cos(2.0 * Phi)
			+ 1.175 * FMath::Cos(4.0 * Phi)
			- 0.0023 * FMath::Cos(6.0 * Phi);
		OutPerLon = 111412.84 * FMath::Cos(Phi)
			- 93.5 * FMath::Cos(3.0 * Phi)
			+ 0.118 * FMath::Cos(5.0 * Phi);
	}

	struct FMCPLscGeoBounds
	{
		double MinLat = 0.0, MinLon = 0.0, MaxLat = 0.0, MaxLon = 0.0;
		double CentreLat() const { return (MinLat + MaxLat) * 0.5; }
		double CentreLon() const { return (MinLon + MaxLon) * 0.5; }
		double LatSpan() const { return MaxLat - MinLat; }
		double LonSpan() const { return MaxLon - MinLon; }
	};

	/** Read boundsLatLon {minLat, minLon, maxLat, maxLon}, refusing a swapped or
	 *  degenerate box by name rather than producing a mirrored landscape. */
	bool MCPLscReadGeoBounds(
		const TSharedPtr<FJsonObject>& Params, FMCPLscGeoBounds& Out, FString& OutError)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Params->TryGetObjectField(TEXT("boundsLatLon"), Obj) || !Obj || !Obj->IsValid())
		{
			OutError = TEXT("Missing 'boundsLatLon' {minLat, minLon, maxLat, maxLon} in decimal degrees.");
			return false;
		}
		if (!(*Obj)->TryGetNumberField(TEXT("minLat"), Out.MinLat)
			|| !(*Obj)->TryGetNumberField(TEXT("minLon"), Out.MinLon)
			|| !(*Obj)->TryGetNumberField(TEXT("maxLat"), Out.MaxLat)
			|| !(*Obj)->TryGetNumberField(TEXT("maxLon"), Out.MaxLon))
		{
			OutError = TEXT("'boundsLatLon' needs all four of minLat, minLon, maxLat and maxLon, in decimal degrees.");
			return false;
		}
		if (Out.MinLat < -90.0 || Out.MaxLat > 90.0 || Out.MinLon < -180.0 || Out.MaxLon > 180.0)
		{
			OutError = FString::Printf(
				TEXT("'boundsLatLon' is out of range: latitude must be within -90..90 and longitude within -180..180, got lat %.6f..%.6f and lon %.6f..%.6f."),
				Out.MinLat, Out.MaxLat, Out.MinLon, Out.MaxLon);
			return false;
		}
		if (Out.MaxLat <= Out.MinLat || Out.MaxLon <= Out.MinLon)
		{
			OutError = FString::Printf(
				TEXT("'boundsLatLon' is empty or inverted: maxLat must exceed minLat and maxLon must exceed minLon, got lat %.6f..%.6f and lon %.6f..%.6f. ")
				TEXT("A box crossing the antimeridian is not supported; split it into two."),
				Out.MinLat, Out.MaxLat, Out.MinLon, Out.MaxLon);
			return false;
		}
		return true;
	}

	/** What an inspected heightmap file turned out to hold. */
	struct FMCPLscHeightmapStats
	{
		bool bInspected = false;
		int32 Width = 0;
		int32 Height = 0;
		uint16 MinRaw = 0;
		uint16 MaxRaw = 0;
		double MeanRaw = 0.0;
		FString Format;
		FString ResolvedPath;
	};

	/**
	 * Read a heightmap file for its dimensions and value range, without writing
	 * anything. This is the read half planning needs: the caller knows what the
	 * elevation band means in metres, and the file knows how many samples it has
	 * and which part of the uint16 range it actually uses.
	 */
	bool MCPLscInspectHeightmapFile(
		const TSharedPtr<FJsonObject>& Params,
		const FString& RawPath,
		FMCPLscHeightmapStats& Out,
		FString& OutError)
	{
		Out.ResolvedPath = MCPLscResolveFilePath(RawPath);
		if (!IFileManager::Get().FileExists(*Out.ResolvedPath))
		{
			OutError = FString::Printf(
				TEXT("No heightmap file at '%s'. A relative path resolves under the project's Saved directory; pass an absolute path to read from anywhere else."),
				*Out.ResolvedPath);
			return false;
		}
		if (!MCPLscResolveHeightmapFormat(Params, Out.ResolvedPath, Out.Format, OutError))
		{
			return false;
		}

		TArray<uint8> FileBytes;
		if (!FFileHelper::LoadFileToArray(FileBytes, *Out.ResolvedPath))
		{
			OutError = FString::Printf(TEXT("'%s' exists but could not be read."), *Out.ResolvedPath);
			return false;
		}

		TArray<uint16> Samples;
		if (Out.Format == TEXT("png16"))
		{
			IImageWrapperModule& ImageWrapperModule =
				FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
			TSharedPtr<IImageWrapper> PngWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
			if (!PngWrapper.IsValid() || !PngWrapper->SetCompressed(FileBytes.GetData(), FileBytes.Num()))
			{
				OutError = FString::Printf(TEXT("'%s' is not a PNG this engine can decode."), *Out.ResolvedPath);
				return false;
			}
			if (PngWrapper->GetFormat() != ERGBFormat::Gray || PngWrapper->GetBitDepth() != 16)
			{
				OutError = FString::Printf(
					TEXT("'%s' is a %d-bit PNG in format %d, not the 16-bit greyscale a landscape heightmap needs. ")
					TEXT("Re-export it as 16-bit grey, or pass a raw16 file instead."),
					*Out.ResolvedPath, PngWrapper->GetBitDepth(), (int32)PngWrapper->GetFormat());
				return false;
			}
			TArray64<uint8> Raw;
			if (!PngWrapper->GetRaw(ERGBFormat::Gray, 16, Raw))
			{
				OutError = FString::Printf(TEXT("'%s' decoded but its pixels could not be read."), *Out.ResolvedPath);
				return false;
			}
			Out.Width = (int32)PngWrapper->GetWidth();
			Out.Height = (int32)PngWrapper->GetHeight();
			Samples.SetNumUninitialized(Out.Width * Out.Height);
			FMemory::Memcpy(Samples.GetData(), Raw.GetData(),
				FMath::Min((int64)Samples.Num() * (int64)sizeof(uint16), Raw.Num()));
		}
		else
		{
			// A headerless file carries no dimensions, so either the caller
			// states them or the only honest guess is that the image is square.
			Out.Width = OptionalInt(Params, TEXT("width"), 0);
			Out.Height = OptionalInt(Params, TEXT("height"), 0);
			if (Out.Width <= 0 || Out.Height <= 0)
			{
				const int32 Square = FMath::RoundToInt32(FMath::Sqrt((double)FileBytes.Num() / 2.0));
				if ((int64)Square * (int64)Square * 2 != (int64)FileBytes.Num())
				{
					OutError = FString::Printf(
						TEXT("'%s' is a raw16 file of %d bytes, which is not a square image, so its dimensions cannot be inferred. Pass 'width' and 'height'."),
						*Out.ResolvedPath, FileBytes.Num());
					return false;
				}
				Out.Width = Square;
				Out.Height = Square;
			}
			const int64 Expected = (int64)Out.Width * (int64)Out.Height * 2;
			if ((int64)FileBytes.Num() != Expected)
			{
				OutError = FString::Printf(
					TEXT("'%s' is %d bytes; %dx%d of little-endian uint16 needs %lld."),
					*Out.ResolvedPath, FileBytes.Num(), Out.Width, Out.Height, Expected);
				return false;
			}
			Samples.SetNumUninitialized(Out.Width * Out.Height);
			FMemory::Memcpy(Samples.GetData(), FileBytes.GetData(), FileBytes.Num());
		}

		if (Samples.Num() == 0)
		{
			OutError = FString::Printf(TEXT("'%s' decoded to no samples."), *Out.ResolvedPath);
			return false;
		}
		Out.MinRaw = MAX_uint16;
		Out.MaxRaw = 0;
		double Sum = 0.0;
		for (uint16 Value : Samples)
		{
			Out.MinRaw = FMath::Min(Out.MinRaw, Value);
			Out.MaxRaw = FMath::Max(Out.MaxRaw, Value);
			Sum += (double)Value;
		}
		Out.MeanRaw = Sum / (double)Samples.Num();
		Out.bInspected = true;
		return true;
	}

	/** One landscape resolution the engine will accept, scored against the
	 *  vertex count a real-world tile actually wants. */
	struct FMCPLscResolution
	{
		int32 SubsectionSizeQuads = 63;
		int32 NumSubsections = 2;
		int32 ComponentCountX = 1;
		int32 ComponentCountY = 1;
		double Error = 0.0;

		int32 ComponentSizeQuads() const { return SubsectionSizeQuads * NumSubsections; }
		int32 QuadsX() const { return ComponentCountX * ComponentSizeQuads(); }
		int32 QuadsY() const { return ComponentCountY * ComponentSizeQuads(); }
		int32 VertsX() const { return QuadsX() + 1; }
		int32 VertsY() const { return QuadsY() + 1; }
		int32 ComponentCount() const { return ComponentCountX * ComponentCountY; }
	};

	TSharedPtr<FJsonObject> MCPLscResolutionJson(const FMCPLscResolution& Res)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("subsectionSizeQuads"), Res.SubsectionSizeQuads);
		Obj->SetNumberField(TEXT("numSubsections"), Res.NumSubsections);
		Obj->SetNumberField(TEXT("componentCountX"), Res.ComponentCountX);
		Obj->SetNumberField(TEXT("componentCountY"), Res.ComponentCountY);
		Obj->SetNumberField(TEXT("componentSizeQuads"), Res.ComponentSizeQuads());
		Obj->SetNumberField(TEXT("componentCount"), Res.ComponentCount());
		Obj->SetNumberField(TEXT("quadsX"), Res.QuadsX());
		Obj->SetNumberField(TEXT("quadsY"), Res.QuadsY());
		Obj->SetNumberField(TEXT("verticesX"), Res.VertsX());
		Obj->SetNumberField(TEXT("verticesY"), Res.VertsY());
		Obj->SetNumberField(TEXT("vertexCount"), (double)Res.VertsX() * (double)Res.VertsY());
		Obj->SetNumberField(TEXT("resolutionErrorFraction"), Res.Error);
		return Obj;
	}
}

// landscape(plan_real_world_landscape): turn a real-world extent plus a
// heightmap into the exact create + import calls that reproduce it in engine.
TSharedPtr<FJsonValue> FLandscapeHandlers::PlanRealWorldLandscape(const TSharedPtr<FJsonObject>& Params)
{
	// Deliberately no REQUIRE_EDITOR_WORLD: planning is arithmetic over a file
	// and a bounding box, and the point of it is to run BEFORE a landscape
	// exists. Refusing it because no level is open would make the one call that
	// says what to create depend on having already created something.

	// ── the ground footprint ────────────────────────────────────────────────
	double GroundMetersX = 0.0, GroundMetersY = 0.0;
	FString ExtentSource;
	FMCPLscGeoBounds Geo;
	bool bHasGeo = false;
	double MetersPerLat = 0.0, MetersPerLon = 0.0;

	const TSharedPtr<FJsonObject>* SizeObj = nullptr;
	if (Params->TryGetObjectField(TEXT("realWorldSizeMeters"), SizeObj) && SizeObj && SizeObj->IsValid())
	{
		(*SizeObj)->TryGetNumberField(TEXT("x"), GroundMetersX);
		(*SizeObj)->TryGetNumberField(TEXT("y"), GroundMetersY);
		if (GroundMetersX <= 0.0 || GroundMetersY <= 0.0)
		{
			return MCPError(TEXT("'realWorldSizeMeters' needs positive x and y, the ground footprint in metres that the heightmap covers."));
		}
		ExtentSource = TEXT("realWorldSizeMeters");
	}
	else if (Params->HasField(TEXT("boundsLatLon")))
	{
		FString GeoError;
		if (!MCPLscReadGeoBounds(Params, Geo, GeoError)) return MCPError(GeoError);
		bHasGeo = true;
		MCPLscMetersPerDegree(Geo.CentreLat(), MetersPerLat, MetersPerLon);
		GroundMetersX = Geo.LonSpan() * MetersPerLon;
		GroundMetersY = Geo.LatSpan() * MetersPerLat;
		ExtentSource = TEXT("boundsLatLon");
		if (GroundMetersX <= 0.0 || GroundMetersY <= 0.0)
		{
			return MCPError(FString::Printf(
				TEXT("'boundsLatLon' projects to a %.1f by %.1f metre footprint at latitude %.4f, which is not usable. Check the box."),
				GroundMetersX, GroundMetersY, Geo.CentreLat()));
		}
	}
	else
	{
		return MCPError(
			TEXT("Nothing to size the landscape against. Pass 'realWorldSizeMeters' {x, y} for the ground footprint in metres, ")
			TEXT("or 'boundsLatLon' {minLat, minLon, maxLat, maxLon} in decimal degrees and it will be projected for you."));
	}

	// ── the elevation band ──────────────────────────────────────────────────
	if (!Params->HasField(TEXT("minElevationMeters")) || !Params->HasField(TEXT("maxElevationMeters")))
	{
		return MCPError(
			TEXT("Missing 'minElevationMeters' and 'maxElevationMeters'. They are what the heightmap's value range means in the real world, ")
			TEXT("and without them there is no way to choose a Z scale, because a landscape's uint16 heights carry no units of their own. ")
			TEXT("A DEM download page states them, and landscape(export_heightmap) reports them for a heightmap this bridge produced."));
	}
	const double MinElevationMeters = OptionalNumber(Params, TEXT("minElevationMeters"), 0.0);
	const double MaxElevationMeters = OptionalNumber(Params, TEXT("maxElevationMeters"), 0.0);
	if (MaxElevationMeters <= MinElevationMeters)
	{
		return MCPError(FString::Printf(
			TEXT("'maxElevationMeters' (%.3f) must be greater than 'minElevationMeters' (%.3f); they are the real-world elevations the heightmap's low and high values stand for."),
			MaxElevationMeters, MinElevationMeters));
	}
	const double Exaggeration = FMath::Clamp(
		OptionalNumber(Params, TEXT("verticalExaggeration"), 1.0), 0.01, 100.0);

	const FString Encoding = OptionalString(Params, TEXT("elevationEncoding"), TEXT("full")).ToLower();
	if (Encoding != TEXT("full") && Encoding != TEXT("data"))
	{
		return MCPError(
			TEXT("'elevationEncoding' must be \"full\" (the image uses the whole 0..65535 range for the elevation band, which is what a landscape export and most DEM exporters produce) ")
			TEXT("or \"data\" (only the values actually present in the image span the band, which needs 'sourcePath' so the range can be measured)."));
	}

	// ── the source raster ───────────────────────────────────────────────────
	FMCPLscHeightmapStats Source;
	FString SourcePath = OptionalString(Params, TEXT("sourcePath"));
	if (SourcePath.IsEmpty()) SourcePath = OptionalString(Params, TEXT("filePath"));
	if (!SourcePath.IsEmpty())
	{
		FString InspectError;
		if (!MCPLscInspectHeightmapFile(Params, SourcePath, Source, InspectError))
		{
			return MCPError(InspectError);
		}
	}
	else
	{
		Source.Width = OptionalInt(Params, TEXT("width"), 0);
		Source.Height = OptionalInt(Params, TEXT("height"), 0);
		Source.Format = OptionalString(Params, TEXT("format")).ToLower();
	}
	if (Encoding == TEXT("data") && !Source.bInspected)
	{
		return MCPError(
			TEXT("elevationEncoding \"data\" measures the image's own minimum and maximum, so it needs 'sourcePath' pointing at the heightmap file. ")
			TEXT("Use \"full\" if the image already spans 0..65535 across the elevation band."));
	}

	// ── how many quads the tile wants ───────────────────────────────────────
	const double MetersPerQuad = OptionalNumber(Params, TEXT("metersPerQuad"), 0.0);
	if (MetersPerQuad < 0.0)
	{
		return MCPError(TEXT("'metersPerQuad' must be positive: it is the ground distance one landscape quad covers."));
	}
	double TargetQuadsX = 0.0, TargetQuadsY = 0.0;
	FString ResolutionSource;
	if (MetersPerQuad > 0.0)
	{
		TargetQuadsX = GroundMetersX / MetersPerQuad;
		TargetQuadsY = GroundMetersY / MetersPerQuad;
		ResolutionSource = TEXT("metersPerQuad");
	}
	else if (Source.Width > 1 && Source.Height > 1)
	{
		// One image sample per landscape VERTEX, so the quad count is one less.
		TargetQuadsX = Source.Width - 1;
		TargetQuadsY = Source.Height - 1;
		ResolutionSource = TEXT("sourceImage");
	}
	else
	{
		return MCPError(
			TEXT("Nothing to set the resolution from. Pass 'metersPerQuad' for the ground distance one quad should cover, ")
			TEXT("or 'sourcePath' (or 'width' and 'height') so the landscape can be sized to one vertex per image sample."));
	}
	if (TargetQuadsX < 1.0 || TargetQuadsY < 1.0)
	{
		return MCPError(FString::Printf(
			TEXT("The requested resolution works out at %.2f by %.2f quads, which is smaller than a single quad. Reduce 'metersPerQuad' or enlarge the footprint."),
			TargetQuadsX, TargetQuadsY));
	}

	const int32 MaxComponents = FMath::Clamp(OptionalInt(Params, TEXT("maxComponents"), 1024), 1, 16384);

	// ── search the legal configurations ─────────────────────────────────────
	// A landscape's vertex count is not free: subsectionSizeQuads is one of six
	// values, numSubsections is 1 or 2, and the product with the component count
	// is the whole grid. Rather than round the caller's number into the nearest
	// legal one silently, every candidate is scored and returned, and the chosen
	// one carries the error it still has.
	static const int32 SubsectionSizes[] = { 7, 15, 31, 63, 127, 255 };
	TArray<FMCPLscResolution> Candidates;
	TArray<FString> Rejected;
	for (int32 SubsectionSizeQuads : SubsectionSizes)
	{
		for (int32 NumSubsections = 1; NumSubsections <= 2; ++NumSubsections)
		{
			FMCPLscResolution Candidate;
			Candidate.SubsectionSizeQuads = SubsectionSizeQuads;
			Candidate.NumSubsections = NumSubsections;
			const double ComponentQuads = (double)Candidate.ComponentSizeQuads();
			Candidate.ComponentCountX = FMath::Max(1, FMath::RoundToInt32(TargetQuadsX / ComponentQuads));
			Candidate.ComponentCountY = FMath::Max(1, FMath::RoundToInt32(TargetQuadsY / ComponentQuads));
			if (Candidate.ComponentCount() > MaxComponents)
			{
				Rejected.Add(FString::Printf(
					TEXT("%d subsections of %d quads would need %d components, above maxComponents %d"),
					NumSubsections * NumSubsections, SubsectionSizeQuads,
					Candidate.ComponentCount(), MaxComponents));
				continue;
			}
			Candidate.Error =
				FMath::Abs(Candidate.QuadsX() - TargetQuadsX) / TargetQuadsX
				+ FMath::Abs(Candidate.QuadsY() - TargetQuadsY) / TargetQuadsY;
			Candidates.Add(Candidate);
		}
	}
	if (Candidates.Num() == 0)
	{
		return MCPError(FString::Printf(
			TEXT("No legal landscape configuration fits %.0f by %.0f quads within maxComponents %d. ")
			TEXT("Raise 'maxComponents', or raise 'metersPerQuad' so the same ground needs fewer quads."),
			TargetQuadsX, TargetQuadsY, MaxComponents));
	}
	// Closest fit wins; a tie goes to the one with fewer, larger components,
	// which is what the engine renders and streams more cheaply.
	Candidates.Sort([](const FMCPLscResolution& A, const FMCPLscResolution& B)
	{
		if (!FMath::IsNearlyEqual(A.Error, B.Error, 1e-9)) return A.Error < B.Error;
		return A.ComponentCount() < B.ComponentCount();
	});
	const FMCPLscResolution Chosen = Candidates[0];

	// ── the Z plan ──────────────────────────────────────────────────────────
	// What the image's 0 and 65535 mean in real elevation, before exaggeration.
	double ElevationAtRaw0 = MinElevationMeters;
	double ElevationAtRawMax = MaxElevationMeters;
	if (Encoding == TEXT("data"))
	{
		if (Source.MaxRaw <= Source.MinRaw)
		{
			return MCPError(FString::Printf(
				TEXT("'%s' is completely flat: every sample is %d, so there is no data range to stretch across the elevation band. Use elevationEncoding \"full\", or supply a heightmap with relief."),
				*Source.ResolvedPath, Source.MinRaw));
		}
		const double PerRaw = (MaxElevationMeters - MinElevationMeters)
			/ ((double)Source.MaxRaw - (double)Source.MinRaw);
		ElevationAtRaw0 = MinElevationMeters + (0.0 - (double)Source.MinRaw) * PerRaw;
		ElevationAtRawMax = MinElevationMeters + (65535.0 - (double)Source.MinRaw) * PerRaw;
	}

	const FVector Origin = OptionalVec3(Params, TEXT("location"));
	// World Z that the image's 0 and 65535 land at. Sea level (elevation zero)
	// sits at the caller's location Z, so two tiles planned separately with the
	// same origin still line up vertically.
	const double MinHeightCm = Origin.Z + ElevationAtRaw0 * 100.0 * Exaggeration;
	const double MaxHeightCm = Origin.Z + ElevationAtRawMax * 100.0 * Exaggeration;
	const double FullSpanCm = MaxHeightCm - MinHeightCm;

	// A landscape at Z scale S represents 65535 * LANDSCAPE_ZSCALE * S
	// centimetres of relief. Solve for the S that just covers the band, then
	// round UP so rounding never clips the peaks.
	const double SpanPerUnitScale = 65535.0 * LANDSCAPE_ZSCALE;
	double ZScale = FullSpanCm / SpanPerUnitScale;
	ZScale = FMath::CeilToDouble(ZScale * 1000.0) / 1000.0;
	if (ZScale <= 0.0) ZScale = 0.001;
	const double RepresentableCm = SpanPerUnitScale * ZScale;
	const double HeadroomPercent = FullSpanCm > 0.0
		? (RepresentableCm - FullSpanCm) / FullSpanCm * 100.0 : 0.0;
	// Raw 0 sits 32768 units below the actor origin, so placing raw 0 at
	// MinHeightCm fixes the actor's own Z.
	const double ActorZ = MinHeightCm + LandscapeDataAccess::MidValue * LANDSCAPE_ZSCALE * ZScale;
	const double VerticalPrecisionCm = LANDSCAPE_ZSCALE * ZScale;

	// ── the XY plan ─────────────────────────────────────────────────────────
	const double ScaleX = (GroundMetersX * 100.0) / (double)Chosen.QuadsX();
	const double ScaleY = (GroundMetersY * 100.0) / (double)Chosen.QuadsY();
	const double ActualMetersPerQuadX = GroundMetersX / (double)Chosen.QuadsX();
	const double ActualMetersPerQuadY = GroundMetersY / (double)Chosen.QuadsY();

	const bool bResampleNeeded = Source.Width > 0 && Source.Height > 0
		&& (Source.Width != Chosen.VertsX() || Source.Height != Chosen.VertsY());

	// ── the answer ──────────────────────────────────────────────────────────
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("extentSource"), ExtentSource);
	Result->SetStringField(TEXT("resolutionSource"), ResolutionSource);
	Result->SetStringField(TEXT("elevationEncoding"), Encoding);
	Result->SetNumberField(TEXT("groundSizeMetersX"), GroundMetersX);
	Result->SetNumberField(TEXT("groundSizeMetersY"), GroundMetersY);
	Result->SetNumberField(TEXT("verticalExaggeration"), Exaggeration);

	if (bHasGeo)
	{
		TSharedPtr<FJsonObject> GeoObj = MakeShared<FJsonObject>();
		GeoObj->SetNumberField(TEXT("minLat"), Geo.MinLat);
		GeoObj->SetNumberField(TEXT("minLon"), Geo.MinLon);
		GeoObj->SetNumberField(TEXT("maxLat"), Geo.MaxLat);
		GeoObj->SetNumberField(TEXT("maxLon"), Geo.MaxLon);
		GeoObj->SetNumberField(TEXT("centreLat"), Geo.CentreLat());
		GeoObj->SetNumberField(TEXT("centreLon"), Geo.CentreLon());
		GeoObj->SetNumberField(TEXT("metersPerDegreeLat"), MetersPerLat);
		GeoObj->SetNumberField(TEXT("metersPerDegreeLon"), MetersPerLon);
		GeoObj->SetStringField(TEXT("projection"),
			TEXT("Local tangent plane on WGS84, evaluated at the box centre latitude. Accurate to a few centimetres per kilometre over a tile a few degrees across, and NOT a datum reprojection. Pass the same boundsLatLon to landscape(project_geo_coordinates) so placement uses this identical mapping."));
		Result->SetObjectField(TEXT("georeference"), GeoObj);
	}

	if (Source.bInspected)
	{
		TSharedPtr<FJsonObject> Src = MakeShared<FJsonObject>();
		Src->SetStringField(TEXT("filePath"), Source.ResolvedPath);
		Src->SetStringField(TEXT("format"), Source.Format);
		Src->SetNumberField(TEXT("width"), Source.Width);
		Src->SetNumberField(TEXT("height"), Source.Height);
		Src->SetNumberField(TEXT("minRaw"), Source.MinRaw);
		Src->SetNumberField(TEXT("maxRaw"), Source.MaxRaw);
		Src->SetNumberField(TEXT("meanRaw"), Source.MeanRaw);
		Src->SetNumberField(TEXT("usedRangeFraction"),
			((double)Source.MaxRaw - (double)Source.MinRaw) / 65535.0);
		Result->SetObjectField(TEXT("source"), Src);
	}

	Result->SetObjectField(TEXT("resolution"), MCPLscResolutionJson(Chosen));
	Result->SetNumberField(TEXT("metersPerQuadX"), ActualMetersPerQuadX);
	Result->SetNumberField(TEXT("metersPerQuadY"), ActualMetersPerQuadY);
	Result->SetNumberField(TEXT("verticalPrecisionCm"), VerticalPrecisionCm);
	Result->SetNumberField(TEXT("zHeadroomPercent"), HeadroomPercent);
	Result->SetNumberField(TEXT("minWorldZ"), MinHeightCm);
	Result->SetNumberField(TEXT("maxWorldZ"), MaxHeightCm);

	// The two calls that build it, spelled exactly as their actions take them.
	TSharedPtr<FJsonObject> Create = MakeShared<FJsonObject>();
	Create->SetNumberField(TEXT("componentCountX"), Chosen.ComponentCountX);
	Create->SetNumberField(TEXT("componentCountY"), Chosen.ComponentCountY);
	Create->SetNumberField(TEXT("subsectionSizeQuads"), Chosen.SubsectionSizeQuads);
	Create->SetNumberField(TEXT("numSubsections"), Chosen.NumSubsections);
	{
		TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
		Loc->SetNumberField(TEXT("x"), Origin.X);
		Loc->SetNumberField(TEXT("y"), Origin.Y);
		Loc->SetNumberField(TEXT("z"), ActorZ);
		Create->SetObjectField(TEXT("location"), Loc);
		TSharedPtr<FJsonObject> Sc = MakeShared<FJsonObject>();
		Sc->SetNumberField(TEXT("x"), ScaleX);
		Sc->SetNumberField(TEXT("y"), ScaleY);
		Sc->SetNumberField(TEXT("z"), ZScale);
		Create->SetObjectField(TEXT("scale"), Sc);
	}
	Result->SetObjectField(TEXT("create"), Create);

	TSharedPtr<FJsonObject> Import = MakeShared<FJsonObject>();
	if (!Source.ResolvedPath.IsEmpty()) Import->SetStringField(TEXT("filePath"), Source.ResolvedPath);
	if (!Source.Format.IsEmpty()) Import->SetStringField(TEXT("format"), Source.Format);
	{
		TSharedPtr<FJsonObject> Rgn = MakeShared<FJsonObject>();
		Rgn->SetNumberField(TEXT("minX"), 0);
		Rgn->SetNumberField(TEXT("minY"), 0);
		Rgn->SetNumberField(TEXT("maxX"), Chosen.QuadsX());
		Rgn->SetNumberField(TEXT("maxY"), Chosen.QuadsY());
		Import->SetObjectField(TEXT("region"), Rgn);
	}
	Import->SetStringField(TEXT("space"), TEXT("quad"));
	Import->SetBoolField(TEXT("resample"), bResampleNeeded);
	Import->SetNumberField(TEXT("minHeight"), MinHeightCm);
	Import->SetNumberField(TEXT("maxHeight"), MaxHeightCm);
	Import->SetNumberField(TEXT("maxVertices"), (double)Chosen.VertsX() * (double)Chosen.VertsY());
	if (Source.bInspected && Source.Format == TEXT("raw16"))
	{
		Import->SetNumberField(TEXT("width"), Source.Width);
		Import->SetNumberField(TEXT("height"), Source.Height);
	}
	Result->SetObjectField(TEXT("import"), Import);

	TArray<TSharedPtr<FJsonValue>> CandidateRows;
	for (const FMCPLscResolution& Candidate : Candidates)
	{
		CandidateRows.Add(MakeShared<FJsonValueObject>(MCPLscResolutionJson(Candidate)));
	}
	Result->SetArrayField(TEXT("candidateResolutions"), CandidateRows);
	if (Rejected.Num() > 0)
	{
		Result->SetArrayField(TEXT("rejectedResolutions"), MCPStringListToJson(Rejected));
	}

	TArray<FString> Warnings;
	if (!Source.bInspected)
	{
		Warnings.Add(TEXT("No 'sourcePath' was given, so the plan assumes the heightmap matches the planned vertex grid. Pass the file and it is measured instead of assumed."));
	}
	if (bResampleNeeded)
	{
		Warnings.Add(FString::Printf(
			TEXT("The image is %dx%d and the planned grid is %dx%d vertices, so the import will bilinearly resample and fine detail is averaged away. Set 'metersPerQuad' to land on a matching grid if that matters."),
			Source.Width, Source.Height, Chosen.VertsX(), Chosen.VertsY()));
	}
	if (Chosen.Error > 0.02)
	{
		Warnings.Add(FString::Printf(
			TEXT("The closest legal landscape is %.1f percent off the requested quad count, because component sizes are quantised. The footprint is preserved exactly by the actor scale, so the ground covered is right and only the sample spacing shifts."),
			Chosen.Error * 100.0));
	}
	if (!FMath::IsNearlyEqual(ActualMetersPerQuadX, ActualMetersPerQuadY, 0.01))
	{
		Warnings.Add(FString::Printf(
			TEXT("The plan is anisotropic: %.3f metres per quad in X against %.3f in Y, so the actor scale differs per axis. That is exact for the footprint, but a square brush is no longer square on the ground."),
			ActualMetersPerQuadX, ActualMetersPerQuadY));
	}
	if (VerticalPrecisionCm > 10.0)
	{
		Warnings.Add(FString::Printf(
			TEXT("One height unit is %.2f cm, so the terrain is quantised to that step and gentle slopes will terrace. Reduce the elevation band, or split the tile."),
			VerticalPrecisionCm));
	}
	if (!FMath::IsNearlyEqual(Exaggeration, 1.0))
	{
		Warnings.Add(FString::Printf(
			TEXT("Vertical exaggeration is %.2fx, so heights are NOT real-world metres and slope readings from landscape(analyze_terrain) will not match the real terrain."),
			Exaggeration));
	}
	if (Source.bInspected && Encoding == TEXT("full")
		&& ((double)Source.MaxRaw - (double)Source.MinRaw) < 0.5 * 65535.0)
	{
		Warnings.Add(FString::Printf(
			TEXT("The image only uses %d..%d of the 0..65535 range but elevationEncoding is \"full\", so the terrain will occupy that same fraction of the planned elevation band. Use \"data\" if the band describes the relief actually present."),
			Source.MinRaw, Source.MaxRaw));
	}
	if (Warnings.Num() > 0) Result->SetArrayField(TEXT("warnings"), MCPStringListToJson(Warnings));

	Result->SetStringField(TEXT("note"),
		TEXT("Nothing was created or written: this call is arithmetic. Run landscape(create) with the 'create' block, then landscape(import_heightmap) with the 'import' block plus that landscape's actorPath, and the terrain lands at true horizontal scale with the stated elevation band. Fetching the DEM tile itself is deliberately outside this bridge, because provider APIs, keys and licences change independently of the engine."));
	return MCPResult(Result);
}

// landscape(project_geo_coordinates): convert between latitude/longitude and
// this landscape's world space, in both directions.
TSharedPtr<FJsonValue> FLandscapeHandlers::ProjectGeoCoordinates(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	FMCPLscGeoBounds Geo;
	FString GeoError;
	if (!MCPLscReadGeoBounds(Params, Geo, GeoError)) return MCPError(GeoError);

	const TArray<TSharedPtr<FJsonValue>>* PointArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("points"), PointArray) || !PointArray)
	{
		return MCPError(
			TEXT("Missing 'points'. Each entry is either {lat, lon} to place a geographic coordinate on the landscape, or {x, y} to ask what geographic coordinate a world position corresponds to. An optional 'name' is echoed back."));
	}
	if (PointArray->Num() == 0)
	{
		return MCPError(TEXT("'points' is empty; pass at least one {lat, lon} or {x, y} entry."));
	}
	const int32 MaxPoints = 512;
	if (PointArray->Num() > MaxPoints)
	{
		return MCPError(FString::Printf(
			TEXT("'points' holds %d entries, above the %d limit: each one samples the landscape height individually. Split the batch."),
			PointArray->Num(), MaxPoints));
	}

	ALandscape* Landscape = nullptr;
	ULandscapeInfo* Info = nullptr;
	TSharedPtr<FJsonValue> Error;
	if (!MCPLscResolve(World, Params, Landscape, Info, Error)) return Error;

	FIntRect Extent;
	if (!Info->GetLandscapeExtent(Extent))
	{
		return MCPLscRegionError(
			TEXT("The landscape has no registered quad extent yet, so nothing can be projected onto it. Its components may not be loaded; on a World Partition map pin them with level(load_actor_descs) first."),
			Info, Landscape, TEXT("noExtent"));
	}

	// Which end of the landscape's Y axis is north. A DEM raster's first row is
	// conventionally the NORTHERN edge, and import writes row 0 at minY, so
	// minY is north by default. Getting this wrong mirrors every placement
	// about the middle of the map and nothing about the result looks wrong,
	// which is exactly why it is a parameter rather than an assumption.
	const FString NorthAt = OptionalString(Params, TEXT("northAt"), TEXT("minY")).ToLower();
	if (NorthAt != TEXT("miny") && NorthAt != TEXT("maxy"))
	{
		return MCPError(TEXT("'northAt' must be \"minY\" (the default: raster row 0 is the northern edge, which is how landscape(import_heightmap) lays a DEM down) or \"maxY\"."));
	}
	const bool bNorthAtMinY = NorthAt == TEXT("miny");

	double MetersPerLat = 0.0, MetersPerLon = 0.0;
	MCPLscMetersPerDegree(Geo.CentreLat(), MetersPerLat, MetersPerLon);

	const FTransform ToWorld = Landscape->ActorToWorld();
	const FMCPLscHeightSpace Space = MCPLscHeightSpaceFor(Landscape);
	const double QuadSpanX = FMath::Max((double)(Extent.Max.X - Extent.Min.X), UE_KINDA_SMALL_NUMBER);
	const double QuadSpanY = FMath::Max((double)(Extent.Max.Y - Extent.Min.Y), UE_KINDA_SMALL_NUMBER);
	const bool bSampleHeight = OptionalBool(Params, TEXT("sampleHeight"), true);

	TArray<TSharedPtr<FJsonValue>> Rows;
	int32 OutOfBounds = 0;
	int32 Sampled = 0;
	for (int32 Index = 0; Index < PointArray->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* PointObj = nullptr;
		if (!(*PointArray)[Index].IsValid() || !(*PointArray)[Index]->TryGetObject(PointObj) || !PointObj)
		{
			return MCPError(FString::Printf(
				TEXT("points[%d] is not an object. Each entry is {lat, lon} or {x, y}, with an optional 'name'."), Index));
		}

		FString Name;
		(*PointObj)->TryGetStringField(TEXT("name"), Name);

		double Lat = 0.0, Lon = 0.0;
		double LocalX = 0.0, LocalY = 0.0;
		FString Direction;

		const bool bHasLatLon = (*PointObj)->HasField(TEXT("lat")) && (*PointObj)->HasField(TEXT("lon"));
		const bool bHasWorld = (*PointObj)->HasField(TEXT("x")) && (*PointObj)->HasField(TEXT("y"));
		if (bHasLatLon)
		{
			(*PointObj)->TryGetNumberField(TEXT("lat"), Lat);
			(*PointObj)->TryGetNumberField(TEXT("lon"), Lon);
			const double U = (Lon - Geo.MinLon) / Geo.LonSpan();
			const double VFromNorth = (Geo.MaxLat - Lat) / Geo.LatSpan();
			const double V = bNorthAtMinY ? VFromNorth : (1.0 - VFromNorth);
			LocalX = Extent.Min.X + U * QuadSpanX;
			LocalY = Extent.Min.Y + V * QuadSpanY;
			Direction = TEXT("geoToWorld");
		}
		else if (bHasWorld)
		{
			double WorldX = 0.0, WorldY = 0.0;
			(*PointObj)->TryGetNumberField(TEXT("x"), WorldX);
			(*PointObj)->TryGetNumberField(TEXT("y"), WorldY);
			const FVector Local = ToWorld.InverseTransformPosition(FVector(WorldX, WorldY, 0.0));
			LocalX = Local.X;
			LocalY = Local.Y;
			const double U = (LocalX - Extent.Min.X) / QuadSpanX;
			const double V = (LocalY - Extent.Min.Y) / QuadSpanY;
			const double VFromNorth = bNorthAtMinY ? V : (1.0 - V);
			Lon = Geo.MinLon + U * Geo.LonSpan();
			Lat = Geo.MaxLat - VFromNorth * Geo.LatSpan();
			Direction = TEXT("worldToGeo");
		}
		else
		{
			return MCPError(FString::Printf(
				TEXT("points[%d] carries neither {lat, lon} nor {x, y}, so there is nothing to convert."), Index));
		}

		const FVector Surface = ToWorld.TransformPosition(FVector(LocalX, LocalY, 0.0));
		const bool bInBounds =
			LocalX >= Extent.Min.X && LocalX <= Extent.Max.X &&
			LocalY >= Extent.Min.Y && LocalY <= Extent.Max.Y;
		if (!bInBounds) ++OutOfBounds;

		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		if (!Name.IsEmpty()) Row->SetStringField(TEXT("name"), Name);
		Row->SetNumberField(TEXT("index"), Index);
		Row->SetStringField(TEXT("direction"), Direction);
		Row->SetNumberField(TEXT("lat"), Lat);
		Row->SetNumberField(TEXT("lon"), Lon);
		Row->SetNumberField(TEXT("x"), Surface.X);
		Row->SetNumberField(TEXT("y"), Surface.Y);
		TSharedPtr<FJsonObject> Quad = MakeShared<FJsonObject>();
		Quad->SetNumberField(TEXT("x"), LocalX);
		Quad->SetNumberField(TEXT("y"), LocalY);
		Row->SetObjectField(TEXT("quad"), Quad);
		Row->SetBoolField(TEXT("inBounds"), bInBounds);

		if (bSampleHeight && bInBounds)
		{
			// A 1x1 region at the nearest vertex, read through the same path
			// every other height read in this file uses, so a projected point
			// and landscape(get_height_at_point) cannot disagree.
			FMCPLscRegion Vertex;
			Vertex.X1 = Vertex.X2 = FMath::Clamp(FMath::RoundToInt32(LocalX), Extent.Min.X, Extent.Max.X);
			Vertex.Y1 = Vertex.Y2 = FMath::Clamp(FMath::RoundToInt32(LocalY), Extent.Min.Y, Extent.Max.Y);
			Vertex.RequestedX1 = Vertex.X1; Vertex.RequestedY1 = Vertex.Y1;
			Vertex.RequestedX2 = Vertex.X2; Vertex.RequestedY2 = Vertex.Y2;
			Vertex.Source = TEXT("point");
			TArray<uint16> Heights;
			TSharedPtr<FJsonValue> ReadError;
			if (MCPLscReadHeights(Info, FGuid(), false, Vertex, Heights, ReadError) && Heights.Num() > 0)
			{
				Row->SetNumberField(TEXT("rawHeight"), Heights[0]);
				Row->SetNumberField(TEXT("z"), Space.bValid
					? Space.RawToWorldZ((double)Heights[0])
					: ToWorld.TransformPosition(FVector(0.0, 0.0, LandscapeDataAccess::GetLocalHeight(Heights[0]))).Z);
				Row->SetBoolField(TEXT("heightSampled"), true);
				++Sampled;
			}
			else
			{
				Row->SetBoolField(TEXT("heightSampled"), false);
				Row->SetStringField(TEXT("heightNote"),
					TEXT("The covering landscape components are not loaded, so no height could be read here. Pin them with level(load_actor_descs)."));
			}
		}
		else if (bSampleHeight)
		{
			Row->SetBoolField(TEXT("heightSampled"), false);
			Row->SetStringField(TEXT("heightNote"),
				TEXT("The point falls outside the landscape, so there is no surface to sample. The world X and Y are still exact."));
		}
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Landscape->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Landscape->GetPathName());
	Result->SetArrayField(TEXT("points"), Rows);
	Result->SetNumberField(TEXT("pointCount"), PointArray->Num());
	Result->SetNumberField(TEXT("outOfBoundsCount"), OutOfBounds);
	Result->SetNumberField(TEXT("heightSampledCount"), Sampled);
	Result->SetNumberField(TEXT("metersPerDegreeLat"), MetersPerLat);
	Result->SetNumberField(TEXT("metersPerDegreeLon"), MetersPerLon);
	Result->SetStringField(TEXT("northAt"), bNorthAtMinY ? TEXT("minY") : TEXT("maxY"));
	TSharedPtr<FJsonObject> Bounds = MakeShared<FJsonObject>();
	Bounds->SetNumberField(TEXT("minX"), Extent.Min.X);
	Bounds->SetNumberField(TEXT("minY"), Extent.Min.Y);
	Bounds->SetNumberField(TEXT("maxX"), Extent.Max.X);
	Bounds->SetNumberField(TEXT("maxY"), Extent.Max.Y);
	Result->SetObjectField(TEXT("quadExtent"), Bounds);
	Result->SetStringField(TEXT("note"),
		TEXT("The mapping stretches the given latitude/longitude box across the landscape's whole quad extent, which is only true if the landscape was built for that box - plan it with landscape(plan_real_world_landscape) and pass the same boundsLatLon here. Distances come from a local tangent plane on WGS84 at the box centre latitude, not a datum reprojection."));
	return MCPResult(Result);
}
