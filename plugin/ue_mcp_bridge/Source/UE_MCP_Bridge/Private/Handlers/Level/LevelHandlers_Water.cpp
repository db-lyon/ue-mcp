// Water zone rebuild and read-back (#1156).
//
// Water is an optional plugin, so nothing here links against it: classes are
// found by path and every field is reached through reflection.
//
// Translation-unit partition of FLevelHandlers; registration lives in
// LevelHandlers.cpp::RegisterHandlers.

#include "LevelHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "UObject/UnrealType.h"
#include "UObject/SoftObjectPtr.h"
#include "JsonObjectConverter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	UClass* MCPWaterZoneClass()
	{
		return LoadClass<AActor>(nullptr, TEXT("/Script/Water.WaterZone"));
	}

	/** The struct property Name on Obj when its struct is StructName, else null. */
	void* MCPWaterStructPtr(UObject* Obj, const TCHAR* Name, const TCHAR* StructName)
	{
		if (!Obj) return nullptr;
		FStructProperty* Prop = CastField<FStructProperty>(Obj->GetClass()->FindPropertyByName(FName(Name)));
		if (!Prop || !Prop->Struct || Prop->Struct->GetName() != StructName) return nullptr;
		return Prop->ContainerPtrToValuePtr<void>(Obj);
	}

	FVector2D* MCPWaterVector2D(UObject* Obj, const TCHAR* Name)
	{
		return static_cast<FVector2D*>(MCPWaterStructPtr(Obj, Name, TEXT("Vector2D")));
	}

	FIntPoint* MCPWaterIntPoint(UObject* Obj, const TCHAR* Name)
	{
		return static_cast<FIntPoint*>(MCPWaterStructPtr(Obj, Name, TEXT("IntPoint")));
	}

	UObject* MCPWaterObjectProp(UObject* Obj, const TCHAR* Name)
	{
		if (!Obj) return nullptr;
		FObjectPropertyBase* Prop = CastField<FObjectPropertyBase>(Obj->GetClass()->FindPropertyByName(FName(Name)));
		return Prop ? Prop->GetObjectPropertyValue_InContainer(Obj) : nullptr;
	}

	bool MCPWaterReadNumber(UObject* Obj, const TCHAR* Name, double& Out)
	{
		if (!Obj) return false;
		FNumericProperty* Prop = CastField<FNumericProperty>(Obj->GetClass()->FindPropertyByName(FName(Name)));
		if (!Prop) return false;
		const void* Addr = Prop->ContainerPtrToValuePtr<void>(Obj);
		Out = Prop->IsFloatingPoint()
			? Prop->GetFloatingPointPropertyValue(Addr)
			: static_cast<double>(Prop->GetSignedIntPropertyValue(Addr));
		return true;
	}

	/** Fire PostEditChangeProperty for Name, which is how the zone and mesh
	 *  react to an edit (ZoneExtent -> OnExtentChanged -> MarkForRebuild). */
	void MCPWaterNotifyChanged(UObject* Obj, const TCHAR* Name)
	{
		if (!Obj) return;
		FProperty* Prop = Obj->GetClass()->FindPropertyByName(FName(Name));
		if (!Prop) return;
		FPropertyChangedEvent Event(Prop, EPropertyChangeType::ValueSet);
		Obj->PostEditChangeProperty(Event);
	}

	/** Tick the Water subsystem once, which is where zones run their pending
	 *  rebuild. Reached through the engine's tickable base, so no Water link. */
	bool MCPWaterTickSubsystem(UWorld* World)
	{
		if (!World) return false;
		UClass* SubsystemClass = LoadClass<UWorldSubsystem>(nullptr, TEXT("/Script/Water.WaterSubsystem"));
		if (!SubsystemClass) return false;
		UTickableWorldSubsystem* Tickable = Cast<UTickableWorldSubsystem>(World->GetSubsystemBase(SubsystemClass));
		if (!Tickable) return false;
		static_cast<FTickableGameObject*>(Tickable)->Tick(0.f);
		return true;
	}

	TSharedPtr<FJsonObject> MCPWaterXY(double X, double Y)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("x"), X);
		Obj->SetNumberField(TEXT("y"), Y);
		return Obj;
	}

	/** {x, y} object or [x, y] array. */
	bool MCPWaterReadXYParam(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key, FVector2D& Out)
	{
		const TSharedPtr<FJsonValue> Value = TryGetParam(Params, Key);
		if (!Value.IsValid()) return false;
		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Obj = Value->AsObject();
			double X = 0.0, Y = 0.0;
			if (!Obj.IsValid() || !Obj->TryGetNumberField(TEXT("x"), X) || !Obj->TryGetNumberField(TEXT("y"), Y)) return false;
			Out = FVector2D(X, Y);
			return true;
		}
		if (Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
			if (Arr.Num() != 2) return false;
			Out = FVector2D(Arr[0]->AsNumber(), Arr[1]->AsNumber());
			return true;
		}
		return false;
	}

	/** One actor by actorLabel/actorPath, or every zone in the world. */
	bool MCPWaterResolveZones(
		UWorld* World,
		const TSharedPtr<FJsonObject>& Params,
		UClass* ZoneClass,
		TArray<AActor*>& OutZones,
		TSharedPtr<FJsonValue>& OutError)
	{
		FMCPActorSelector Selector;
		Selector.bRequired = false;
		if (AActor* Named = MCPResolveActor(World, Params, OutError, Selector))
		{
			if (!Named->IsA(ZoneClass))
			{
				OutError = MCPError(FString::Printf(
					TEXT("Actor '%s' is a %s, not a WaterZone"),
					*Named->GetActorLabel(), *Named->GetClass()->GetName()));
				return false;
			}
			OutZones.Add(Named);
			return true;
		}
		if (OutError.IsValid()) return false;
		for (TActorIterator<AActor> It(World, ZoneClass); It; ++It)
		{
			if (IsValid(*It)) OutZones.Add(*It);
		}
		return true;
	}

	struct FMCPWaterZoneSnapshot
	{
		FVector2D ZoneExtent = FVector2D::ZeroVector;
		double TileSize = 0.0;
		FIntPoint QuadTreeResolution = FIntPoint::ZeroValue;
		bool bHasTileSize = false;
		bool bHasResolution = false;
	};

	FMCPWaterZoneSnapshot MCPWaterSnapshotZone(AActor* Zone)
	{
		FMCPWaterZoneSnapshot Snap;
		if (const FVector2D* Extent = MCPWaterVector2D(Zone, TEXT("ZoneExtent"))) Snap.ZoneExtent = *Extent;
		UObject* Mesh = MCPWaterObjectProp(Zone, TEXT("WaterMesh"));
		Snap.bHasTileSize = MCPWaterReadNumber(Mesh, TEXT("TileSize"), Snap.TileSize);
		if (const FIntPoint* Res = MCPWaterIntPoint(Mesh, TEXT("QuadTreeResolution")))
		{
			Snap.QuadTreeResolution = *Res;
			Snap.bHasResolution = true;
		}
		return Snap;
	}
}

// level(rebuild_water_zone): rebuild a zone's water mesh and water info, and
// repeat until the quad tree resolution stops moving. The mesh grid is sized
// from the extent seen on the previous rebuild, so one pass after an extent or
// TileSize change reports a stale grid.
TSharedPtr<FJsonValue> FLevelHandlers::RebuildWaterZone(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("zoneExtent"), TEXT("tileSize"), TEXT("maxPasses"),
	});

	REQUIRE_EDITOR_WORLD(World);

	UClass* ZoneClass = MCPWaterZoneClass();
	if (!ZoneClass) return MCPError(TEXT("WaterZone class not available - enable the Water plugin"));

	TArray<AActor*> Zones;
	TSharedPtr<FJsonValue> ResolveErr;
	if (!MCPWaterResolveZones(World, Params, ZoneClass, Zones, ResolveErr)) return ResolveErr;
	if (Zones.Num() == 0) return MCPError(TEXT("No WaterZone actor in the editor world"));

	FVector2D NewExtent = FVector2D::ZeroVector;
	const bool bSetExtent = MCPWaterReadXYParam(Params, TEXT("zoneExtent"), NewExtent);
	if (HasParam(Params, TEXT("zoneExtent")) && !bSetExtent)
	{
		return MCPError(TEXT("zoneExtent must be {x, y} or [x, y]"));
	}
	const bool bSetTileSize = HasParam(Params, TEXT("tileSize"));
	const double NewTileSize = OptionalNumber(Params, TEXT("tileSize"), 0.0);
	if (bSetTileSize && NewTileSize <= 0.0) return MCPError(TEXT("tileSize must be greater than 0"));
	if ((bSetExtent || bSetTileSize) && Zones.Num() > 1)
	{
		return MCPError(FString::Printf(
			TEXT("%d WaterZones in the world; pass actorLabel or actorPath to choose which one zoneExtent/tileSize applies to"),
			Zones.Num()));
	}
	const int32 MaxPasses = FMath::Clamp(OptionalInt(Params, TEXT("maxPasses"), 4), 2, 8);

	bool bAnyChanged = false;
	TArray<TSharedPtr<FJsonValue>> ZoneRows;
	for (AActor* Zone : Zones)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("actorLabel"), Zone->GetActorLabel());
		Row->SetStringField(TEXT("actorPath"), Zone->GetPathName());

		FVector2D* ExtentPtr = MCPWaterVector2D(Zone, TEXT("ZoneExtent"));
		UObject* Mesh = MCPWaterObjectProp(Zone, TEXT("WaterMesh"));
		if (!ExtentPtr || !Mesh)
		{
			Row->SetStringField(TEXT("error"), TEXT("WaterZone has no ZoneExtent or WaterMesh property on this engine version"));
			ZoneRows.Add(MakeShared<FJsonValueObject>(Row));
			continue;
		}

		const FMCPWaterZoneSnapshot Before = MCPWaterSnapshotZone(Zone);
		Row->SetObjectField(TEXT("previousZoneExtent"), MCPWaterXY(Before.ZoneExtent.X, Before.ZoneExtent.Y));
		if (Before.bHasTileSize) Row->SetNumberField(TEXT("previousTileSize"), Before.TileSize);

		if (bSetTileSize && !Before.bHasTileSize)
		{
			Row->SetStringField(TEXT("error"), TEXT("WaterMesh has no numeric TileSize property on this engine version"));
			ZoneRows.Add(MakeShared<FJsonValueObject>(Row));
			continue;
		}

		bool bChanged = false;
		if (bSetTileSize && !FMath::IsNearlyEqual(Before.TileSize, NewTileSize))
		{
			FNumericProperty* TileProp = CastField<FNumericProperty>(Mesh->GetClass()->FindPropertyByName(TEXT("TileSize")));
			Mesh->Modify();
			TileProp->SetFloatingPointPropertyValue(TileProp->ContainerPtrToValuePtr<void>(Mesh), NewTileSize);
			MCPWaterNotifyChanged(Mesh, TEXT("TileSize"));
			bChanged = true;
		}
		if (bSetExtent && !Before.ZoneExtent.Equals(NewExtent))
		{
			Zone->Modify();
			*ExtentPtr = NewExtent;
			bChanged = true;
		}

		// Each pass re-applies the extent, which marks the zone for a full
		// rebuild, then ticks the Water subsystem so the rebuild runs now.
		int32 Passes = 0;
		bool bStable = false;
		bool bTicked = false;
		FIntPoint Previous(-1, -1);
		TArray<TSharedPtr<FJsonValue>> PassRows;
		for (int32 Pass = 1; Pass <= MaxPasses; ++Pass)
		{
			MCPWaterNotifyChanged(Zone, TEXT("ZoneExtent"));
			if (UFunction* ForceInfo = Zone->FindFunction(TEXT("ForceUpdateWaterInfoTexture")))
			{
				if (ForceInfo->NumParms == 0) Zone->ProcessEvent(ForceInfo, nullptr);
			}
			bTicked = MCPWaterTickSubsystem(World) || bTicked;
			Passes = Pass;

			const FMCPWaterZoneSnapshot Now = MCPWaterSnapshotZone(Zone);
			TSharedPtr<FJsonObject> PassRow = MakeShared<FJsonObject>();
			PassRow->SetNumberField(TEXT("pass"), Pass);
			PassRow->SetObjectField(TEXT("quadTreeResolution"), MCPWaterXY(Now.QuadTreeResolution.X, Now.QuadTreeResolution.Y));
			PassRows.Add(MakeShared<FJsonValueObject>(PassRow));

			if (Pass >= 2 && Now.QuadTreeResolution == Previous)
			{
				bStable = true;
				break;
			}
			Previous = Now.QuadTreeResolution;
		}

		const FMCPWaterZoneSnapshot After = MCPWaterSnapshotZone(Zone);
		Row->SetObjectField(TEXT("zoneExtent"), MCPWaterXY(After.ZoneExtent.X, After.ZoneExtent.Y));
		if (After.bHasTileSize) Row->SetNumberField(TEXT("tileSize"), After.TileSize);
		if (After.bHasResolution)
		{
			Row->SetObjectField(TEXT("quadTreeResolution"), MCPWaterXY(After.QuadTreeResolution.X, After.QuadTreeResolution.Y));
		}
		if (const FIntPoint* Tiles = MCPWaterIntPoint(Mesh, TEXT("ExtentInTiles")))
		{
			Row->SetObjectField(TEXT("extentInTiles"), MCPWaterXY(Tiles->X, Tiles->Y));
		}
		Row->SetNumberField(TEXT("passes"), Passes);
		Row->SetBoolField(TEXT("stable"), bStable);
		Row->SetBoolField(TEXT("subsystemTicked"), bTicked);
		Row->SetArrayField(TEXT("passResults"), PassRows);
		Row->SetBoolField(TEXT("changed"), bChanged);
		if (!bStable)
		{
			Row->SetStringField(TEXT("note"),
				TEXT("QuadTreeResolution was still moving after the last pass. The rebuild finishes on a later editor frame; call again or read level(get_water_state)."));
		}
		if (bChanged)
		{
			Zone->MarkPackageDirty();
			bAnyChanged = true;
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("actorPath"), Zone->GetPathName());
			Payload->SetObjectField(TEXT("zoneExtent"), MCPWaterXY(Before.ZoneExtent.X, Before.ZoneExtent.Y));
			if (Before.bHasTileSize) Payload->SetNumberField(TEXT("tileSize"), Before.TileSize);
			Row->SetObjectField(TEXT("inverse"), Payload);
		}
		ZoneRows.Add(MakeShared<FJsonValueObject>(Row));
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("zoneCount"), ZoneRows.Num());
	Result->SetArrayField(TEXT("zones"), ZoneRows);
	if (ZoneRows.Num() == 1)
	{
		// One zone is the common case; lift its numbers to the top level.
		const TSharedPtr<FJsonObject> Only = ZoneRows[0]->AsObject();
		for (const TCHAR* Key : { TEXT("zoneExtent"), TEXT("quadTreeResolution") })
		{
			const TSharedPtr<FJsonObject>* Field = nullptr;
			if (Only->TryGetObjectField(Key, Field)) Result->SetObjectField(Key, *Field);
		}
		double Number = 0.0;
		if (Only->TryGetNumberField(TEXT("tileSize"), Number)) Result->SetNumberField(TEXT("tileSize"), Number);
		if (Only->TryGetNumberField(TEXT("passes"), Number)) Result->SetNumberField(TEXT("passes"), Number);
		bool bFlag = false;
		if (Only->TryGetBoolField(TEXT("stable"), bFlag)) Result->SetBoolField(TEXT("stable"), bFlag);
		FString Error;
		if (Only->TryGetStringField(TEXT("error"), Error))
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), Error);
		}
	}

	if (bAnyChanged)
	{
		MCPSetUpdated(Result);
		const TSharedPtr<FJsonObject> Only = ZoneRows[0]->AsObject();
		const TSharedPtr<FJsonObject>* Inverse = nullptr;
		if (Only.IsValid() && Only->TryGetObjectField(TEXT("inverse"), Inverse))
		{
			MCPSetRollback(Result, TEXT("rebuild_water_zone"), *Inverse);
		}
	}
	else
	{
		// A plain rebuild regenerates derived data and writes no property.
		Result->SetBoolField(TEXT("updated"), false);
		MCPSetNoRollback(Result,
			TEXT("A rebuild regenerates the zone's derived mesh and water info from its current properties and changes none of them, so there is nothing to put back."));
	}
	return MCPResult(Result);
}

namespace
{
	/** A reflected value as JSON: object refs as paths, arrays element-wise,
	 *  everything else through the JSON converter or export text. */
	TSharedPtr<FJsonValue> MCPWaterValueJson(FProperty* Prop, const void* Addr)
	{
		if (FSoftObjectProperty* Soft = CastField<FSoftObjectProperty>(Prop))
		{
			const FString Path = Soft->GetPropertyValue(Addr).ToSoftObjectPath().ToString();
			if (Path.IsEmpty()) return MakeShared<FJsonValueNull>();
			return MakeShared<FJsonValueString>(Path);
		}
		if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
		{
			UObject* Value = ObjProp->GetObjectPropertyValue(Addr);
			if (!Value) return MakeShared<FJsonValueNull>();
			return MakeShared<FJsonValueString>(Value->GetPathName());
		}
		if (FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
		{
			FScriptArrayHelper Helper(ArrProp, Addr);
			TArray<TSharedPtr<FJsonValue>> Items;
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				Items.Add(MCPWaterValueJson(ArrProp->Inner, Helper.GetRawPtr(Index)));
			}
			return MakeShared<FJsonValueArray>(Items);
		}
		if (TSharedPtr<FJsonValue> Converted = FJsonObjectConverter::UPropertyToJsonValue(Prop, Addr))
		{
			return Converted;
		}
		FString Text;
		Prop->ExportTextItem_Direct(Text, Addr, nullptr, nullptr, PPF_None);
		return MakeShared<FJsonValueString>(Text);
	}

	/** Copy each named property that exists on Obj into Out, keyed by its
	 *  engine name. Names absent on this engine version are skipped. */
	template <int32 N>
	void MCPWaterCopyProps(UObject* Obj, const TCHAR* const (&Names)[N], const TSharedPtr<FJsonObject>& Out)
	{
		if (!Obj) return;
		for (const TCHAR* Name : Names)
		{
			FProperty* Prop = Obj->GetClass()->FindPropertyByName(FName(Name));
			if (!Prop) continue;
			Out->SetField(Name, MCPWaterValueJson(Prop, Prop->ContainerPtrToValuePtr<void>(Obj)));
		}
	}

	/** Deprecated fields present on Obj, so a caller knows a write there is lost. */
	void MCPWaterNoteDeprecated(UObject* Obj, const TSharedPtr<FJsonObject>& Out)
	{
		if (!Obj) return;
		TArray<TSharedPtr<FJsonValue>> Names;
		for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It)
		{
			if (It->GetName().StartsWith(TEXT("TessellatedWaterMeshExtent")) && It->HasAnyPropertyFlags(CPF_Deprecated))
			{
				Names.Add(MakeShared<FJsonValueString>(It->GetName()));
			}
		}
		if (Names.Num() == 0) return;
		Out->SetArrayField(TEXT("deprecatedProperties"), Names);
		Out->SetStringField(TEXT("deprecationNote"),
			TEXT("TessellatedWaterMeshExtent is deprecated and not serialized. Size the water mesh with ZoneExtent and the WaterMesh TileSize, via level(rebuild_water_zone)."));
	}

	UActorComponent* MCPWaterBodyComponent(AActor* Actor, UClass* BodyClass)
	{
		if (!Actor || !BodyClass) return nullptr;
		TArray<UActorComponent*> Comps;
		Actor->GetComponents(Comps);
		for (UActorComponent* Comp : Comps)
		{
			if (Comp && Comp->IsA(BodyClass)) return Comp;
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> MCPWaterDescribeZone(AActor* Zone)
	{
		static const TCHAR* const ZoneProps[] = {
			TEXT("ZoneExtent"), TEXT("RenderTargetResolution"), TEXT("OverlapPriority"),
			TEXT("CaptureZOffset"), TEXT("bHalfPrecisionTexture"), TEXT("VelocityBlurRadius"),
			TEXT("bEnableLocalOnlyTessellation"), TEXT("LocalTessellationExtent"),
			TEXT("bAutoIncludeLandscapesAsTerrain"), TEXT("WaterZoneIndex"),
			TEXT("TessellatedWaterMeshExtent"), TEXT("OwnedWaterBodies") };
		static const TCHAR* const MeshProps[] = {
			TEXT("TileSize"), TEXT("QuadTreeResolution"), TEXT("ExtentInTiles"),
			TEXT("TessellationFactor"), TEXT("LODScale"), TEXT("ForceCollapseDensityLevel"),
			TEXT("FarDistanceMaterial"), TEXT("FarDistanceMeshExtent"),
			TEXT("bUseFarMeshWithoutOcean"), TEXT("FarDistanceMeshHeightWithoutOcean") };

		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("actorLabel"), Zone->GetActorLabel());
		Row->SetStringField(TEXT("actorPath"), Zone->GetPathName());
		Row->SetStringField(TEXT("class"), Zone->GetClass()->GetName());
		TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
		MCPWaterCopyProps(Zone, ZoneProps, Props);
		Row->SetObjectField(TEXT("properties"), Props);
		if (UObject* Mesh = MCPWaterObjectProp(Zone, TEXT("WaterMesh")))
		{
			TSharedPtr<FJsonObject> MeshRow = MakeShared<FJsonObject>();
			MeshRow->SetStringField(TEXT("path"), Mesh->GetPathName());
			MCPWaterCopyProps(Mesh, MeshProps, MeshRow);
			Row->SetObjectField(TEXT("waterMesh"), MeshRow);
		}
		MCPWaterNoteDeprecated(Zone, Row);
		return Row;
	}

	TSharedPtr<FJsonObject> MCPWaterDescribeBody(AActor* Actor, UActorComponent* Body)
	{
		static const TCHAR* const BodyProps[] = {
			TEXT("WaterZoneOverride"), TEXT("OwningWaterZone"), TEXT("bAffectsLandscape"),
			TEXT("OceanExtents"), TEXT("CollisionExtents"), TEXT("bCenterOnWaterZone"),
			TEXT("ShapeDilation"), TEXT("CollisionHeightOffset"), TEXT("FixedWaterDepth"),
			TEXT("WaterBodyIndex"), TEXT("OverlapMaterialPriority"), TEXT("TessellatedWaterMeshExtent") };
		static const TCHAR* const MaterialProps[] = {
			TEXT("WaterMaterial"), TEXT("WaterHLODMaterial"), TEXT("WaterStaticMeshMaterial"),
			TEXT("WaterLODMaterial"), TEXT("UnderwaterPostProcessMaterial"), TEXT("WaterInfoMaterial") };

		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Row->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		Row->SetStringField(TEXT("componentName"), Body->GetName());
		Row->SetStringField(TEXT("componentClass"), Body->GetClass()->GetName());
		TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
		MCPWaterCopyProps(Body, BodyProps, Props);
		Row->SetObjectField(TEXT("properties"), Props);
		TSharedPtr<FJsonObject> Materials = MakeShared<FJsonObject>();
		MCPWaterCopyProps(Body, MaterialProps, Materials);
		Row->SetObjectField(TEXT("materials"), Materials);
		MCPWaterNoteDeprecated(Body, Row);
		return Row;
	}
}

// level(get_water_state): every WaterZone and WaterBody in one structured read.
TSharedPtr<FJsonValue> FLevelHandlers::GetWaterState(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"),
	});

	REQUIRE_EDITOR_WORLD(World);

	UClass* ZoneClass = MCPWaterZoneClass();
	UClass* BodyClass = LoadClass<UActorComponent>(nullptr, TEXT("/Script/Water.WaterBodyComponent"));
	if (!ZoneClass || !BodyClass) return MCPError(TEXT("Water classes not available - enable the Water plugin"));

	TArray<TSharedPtr<FJsonValue>> Zones;
	TArray<TSharedPtr<FJsonValue>> Bodies;

	FMCPActorSelector Selector;
	Selector.bRequired = false;
	TSharedPtr<FJsonValue> ResolveErr;
	AActor* Named = MCPResolveActor(World, Params, ResolveErr, Selector);
	if (ResolveErr.IsValid()) return ResolveErr;
	if (Named)
	{
		if (Named->IsA(ZoneClass))
		{
			Zones.Add(MakeShared<FJsonValueObject>(MCPWaterDescribeZone(Named)));
		}
		else if (UActorComponent* Body = MCPWaterBodyComponent(Named, BodyClass))
		{
			Bodies.Add(MakeShared<FJsonValueObject>(MCPWaterDescribeBody(Named, Body)));
		}
		else
		{
			return MCPError(FString::Printf(
				TEXT("Actor '%s' is neither a WaterZone nor carries a WaterBodyComponent"), *Named->GetActorLabel()));
		}
	}
	else
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!IsValid(Actor)) continue;
			if (Actor->IsA(ZoneClass))
			{
				Zones.Add(MakeShared<FJsonValueObject>(MCPWaterDescribeZone(Actor)));
			}
			else if (UActorComponent* Body = MCPWaterBodyComponent(Actor, BodyClass))
			{
				Bodies.Add(MakeShared<FJsonValueObject>(MCPWaterDescribeBody(Actor, Body)));
			}
		}
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("zoneCount"), Zones.Num());
	Result->SetNumberField(TEXT("bodyCount"), Bodies.Num());
	Result->SetArrayField(TEXT("zones"), Zones);
	Result->SetArrayField(TEXT("bodies"), Bodies);
	return MCPResult(Result);
}
