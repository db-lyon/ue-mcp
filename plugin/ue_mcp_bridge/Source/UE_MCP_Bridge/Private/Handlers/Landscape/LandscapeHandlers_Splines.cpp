// Landscape spline authoring.
//
// landscape(list_splines) could read a spline network but nothing could make
// one, so a road, river or path cut into terrain was editor-only work. These
// handlers create and edit control points and segments, set the meshes a
// segment deforms along itself, and apply the network to the landscape so it
// raises, lowers and paints the terrain under it.
//
// Control points and segments are UObjects inside a ULandscapeSplinesComponent.
// On a single landscape that component lives on the landscape actor; on a World
// Partition map each network lives on its own ALandscapeSplineActor, created
// through ULandscapeInfo::CreateSplineActor exactly as the Landscape Mode
// spline tool does. Points and segments are addressed by object name or full
// object path, and every write returns both.
//
// Several ULandscapeSplineControlPoint and ULandscapeSplineSegment members are
// neither exported nor virtual (GetLocalMeshComponents, ModifySplines), so the
// handlers call only the virtual and LANDSCAPE_API members and read the mesh
// component list by reflection.

#include "LandscapeHandlers_Splines.h"
#include "LandscapeHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "ScopedTransaction.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeSplineActor.h"
#include "LandscapeSplinesComponent.h"
#include "LandscapeSplineControlPoint.h"
#include "LandscapeSplineSegment.h"
#include "ILandscapeSplineInterface.h"
#if UE_MCP_HAS_5_5_API
#include "LandscapeEditLayer.h"
#endif
#include "Components/SplineMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UnrealType.h"

namespace
{
	const TCHAR* MCPLspAxisName(ESplineMeshAxis::Type Axis)
	{
		switch (Axis)
		{
		case ESplineMeshAxis::Y: return TEXT("Y");
		case ESplineMeshAxis::Z: return TEXT("Z");
		default: return TEXT("X");
		}
	}

	bool MCPLspParseAxis(const FString& Name, ESplineMeshAxis::Type& Out)
	{
		if (Name.Equals(TEXT("X"), ESearchCase::IgnoreCase)) { Out = ESplineMeshAxis::X; return true; }
		if (Name.Equals(TEXT("Y"), ESearchCase::IgnoreCase)) { Out = ESplineMeshAxis::Y; return true; }
		if (Name.Equals(TEXT("Z"), ESearchCase::IgnoreCase)) { Out = ESplineMeshAxis::Z; return true; }
		return false;
	}

	FString MCPLspLayerString(FName Name)
	{
		return Name.IsNone() ? FString() : Name.ToString();
	}

	ILandscapeSplineInterface* MCPLspOwnerInterface(const ULandscapeSplinesComponent* Comp)
	{
		AActor* Owner = Comp ? Comp->GetOwner() : nullptr;
		return Owner ? Cast<ILandscapeSplineInterface>(Owner) : nullptr;
	}

	ALandscape* MCPLspLandscapeOf(const ULandscapeSplinesComponent* Comp)
	{
		ILandscapeSplineInterface* Owner = MCPLspOwnerInterface(Comp);
		ULandscapeInfo* Info = Owner ? Owner->GetLandscapeInfo() : nullptr;
		return Info ? Info->LandscapeActor.Get() : nullptr;
	}

	FVector MCPLspToWorld(const ULandscapeSplinesComponent* Comp, const FVector& Local)
	{
		return Comp->GetComponentTransform().TransformPosition(Local);
	}

	FVector MCPLspToLocal(const ULandscapeSplinesComponent* Comp, const FVector& World)
	{
		return Comp->GetComponentTransform().InverseTransformPosition(World);
	}

	FRotator MCPLspRotToWorld(const ULandscapeSplinesComponent* Comp, const FRotator& Local)
	{
		return (Comp->GetComponentTransform().GetRotation() * Local.Quaternion()).Rotator();
	}

	FRotator MCPLspRotToLocal(const ULandscapeSplinesComponent* Comp, const FRotator& World)
	{
		return (Comp->GetComponentTransform().GetRotation().Inverse() * World.Quaternion()).Rotator();
	}

	/** The spline mesh components a segment built locally. The array is
	 *  protected and its accessor is not exported, so it is read by name. -1
	 *  when the property is missing from this engine. */
	int32 MCPLspMeshComponentCount(ULandscapeSplineSegment* Segment)
	{
		static const FArrayProperty* Prop = FindFProperty<FArrayProperty>(ULandscapeSplineSegment::StaticClass(), TEXT("LocalMeshComponents"));
		if (!Prop || !Segment) return -1;
		const FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(Prop->Inner);
		if (!Inner) return -1;
		FScriptArrayHelper Helper(Prop, Prop->ContainerPtrToValuePtr<void>(Segment));
		int32 Count = 0;
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			if (Inner->GetObjectPropertyValue(Helper.GetRawPtr(Index))) ++Count;
		}
		return Count;
	}

	void MCPLspDescribeOwner(const ULandscapeSplinesComponent* Comp, const TSharedPtr<FJsonObject>& Out)
	{
		AActor* Owner = Comp ? Comp->GetOwner() : nullptr;
		if (Owner)
		{
			Out->SetStringField(TEXT("owner"), Owner->GetPathName());
			Out->SetStringField(TEXT("ownerLabel"), Owner->GetActorLabel());
			Out->SetStringField(TEXT("ownerClass"), Owner->GetClass()->GetName());
			// The name the read reported before the spline writes existed.
			Out->SetStringField(TEXT("landscapeName"), Owner->GetName());
		}
		if (ALandscape* Landscape = MCPLspLandscapeOf(Comp))
		{
			Out->SetStringField(TEXT("landscape"), Landscape->GetActorLabel());
			Out->SetStringField(TEXT("landscapePath"), Landscape->GetPathName());
		}
	}

	TSharedPtr<FJsonObject> MCPLspDescribePoint(ULandscapeSplineControlPoint* Point)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		ULandscapeSplinesComponent* Comp = Point ? Point->GetOuterSafe() : nullptr;
		if (!Comp) return Obj;
		Obj->SetStringField(TEXT("name"), Point->GetName());
		Obj->SetStringField(TEXT("path"), Point->GetPathName());
		// x/y/z stay the spline-space location the read has always returned.
		Obj->SetNumberField(TEXT("x"), Point->Location.X);
		Obj->SetNumberField(TEXT("y"), Point->Location.Y);
		Obj->SetNumberField(TEXT("z"), Point->Location.Z);
		Obj->SetObjectField(TEXT("localLocation"), MCPVec3ToJsonObject(Point->Location));
		Obj->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(MCPLspToWorld(Comp, Point->Location)));
		Obj->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(MCPLspRotToWorld(Comp, Point->Rotation)));
		Obj->SetNumberField(TEXT("halfWidth"), Point->Width);
		Obj->SetNumberField(TEXT("sideFalloff"), Point->SideFalloff);
		Obj->SetNumberField(TEXT("endFalloff"), Point->EndFalloff);
		Obj->SetStringField(TEXT("layerName"), MCPLspLayerString(Point->LayerName));
		Obj->SetBoolField(TEXT("raiseTerrain"), Point->bRaiseTerrain != 0);
		Obj->SetBoolField(TEXT("lowerTerrain"), Point->bLowerTerrain != 0);
		TArray<TSharedPtr<FJsonValue>> Segments;
		for (const FLandscapeSplineConnection& Connection : Point->ConnectedSegments)
		{
			if (Connection.Segment) Segments.Add(MakeShared<FJsonValueString>(Connection.Segment->GetName()));
		}
		Obj->SetArrayField(TEXT("connectedSegments"), Segments);
		MCPLspDescribeOwner(Comp, Obj);
		return Obj;
	}

	TSharedPtr<FJsonObject> MCPLspDescribeMeshEntry(const FLandscapeSplineMeshEntry& Entry)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("mesh"), Entry.Mesh ? Entry.Mesh->GetPathName() : FString());
		Obj->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Entry.Scale));
		Obj->SetStringField(TEXT("forwardAxis"), MCPLspAxisName(Entry.ForwardAxis));
		Obj->SetStringField(TEXT("upAxis"), MCPLspAxisName(Entry.UpAxis));
		Obj->SetBoolField(TEXT("centerH"), Entry.bCenterH != 0);
		Obj->SetBoolField(TEXT("scaleToWidth"), Entry.bScaleToWidth != 0);
		TArray<TSharedPtr<FJsonValue>> Materials;
		for (const TObjectPtr<UMaterialInterface>& Material : Entry.MaterialOverrides)
		{
			Materials.Add(MakeShared<FJsonValueString>(Material ? Material->GetPathName() : FString()));
		}
		Obj->SetArrayField(TEXT("materialOverrides"), Materials);
		return Obj;
	}

	TArray<TSharedPtr<FJsonValue>> MCPLspDescribeMeshes(const ULandscapeSplineSegment* Segment)
	{
		TArray<TSharedPtr<FJsonValue>> Rows;
		for (const FLandscapeSplineMeshEntry& Entry : Segment->SplineMeshes)
		{
			Rows.Add(MakeShared<FJsonValueObject>(MCPLspDescribeMeshEntry(Entry)));
		}
		return Rows;
	}

	TSharedPtr<FJsonObject> MCPLspDescribeSegment(ULandscapeSplineSegment* Segment)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		ULandscapeSplinesComponent* Comp = Segment ? Segment->GetOuterSafe() : nullptr;
		if (!Comp) return Obj;
		Obj->SetStringField(TEXT("name"), Segment->GetName());
		Obj->SetStringField(TEXT("path"), Segment->GetPathName());
		const ULandscapeSplineControlPoint* Start = Segment->Connections[0].ControlPoint;
		const ULandscapeSplineControlPoint* End = Segment->Connections[1].ControlPoint;
		Obj->SetStringField(TEXT("startPoint"), Start ? Start->GetName() : FString());
		Obj->SetStringField(TEXT("endPoint"), End ? End->GetName() : FString());
		Obj->SetStringField(TEXT("startPointPath"), Start ? Start->GetPathName() : FString());
		Obj->SetStringField(TEXT("endPointPath"), End ? End->GetPathName() : FString());
		Obj->SetNumberField(TEXT("startTangentLength"), Segment->Connections[0].TangentLen);
		Obj->SetNumberField(TEXT("endTangentLength"), Segment->Connections[1].TangentLen);
		Obj->SetStringField(TEXT("layerName"), MCPLspLayerString(Segment->LayerName));
		Obj->SetBoolField(TEXT("raiseTerrain"), Segment->bRaiseTerrain != 0);
		Obj->SetBoolField(TEXT("lowerTerrain"), Segment->bLowerTerrain != 0);
		Obj->SetBoolField(TEXT("castShadow"), Segment->bCastShadow != 0);
		Obj->SetStringField(TEXT("collisionProfile"), Segment->BodyInstance.GetCollisionProfileName().ToString());
		Obj->SetArrayField(TEXT("meshes"), MCPLspDescribeMeshes(Segment));
		Obj->SetNumberField(TEXT("meshComponentCount"), MCPLspMeshComponentCount(Segment));
		// Length along the interpolated centre line, in world centimetres.
		double Length = 0.0;
		const TArray<FLandscapeSplineInterpPoint>& Points = Segment->GetPoints();
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			Length += FVector::Distance(MCPLspToWorld(Comp, Points[Index - 1].Center), MCPLspToWorld(Comp, Points[Index].Center));
		}
		Obj->SetNumberField(TEXT("length"), Length);
		MCPLspDescribeOwner(Comp, Obj);
		return Obj;
	}

	template <typename TElement>
	TElement* MCPLspFindElement(UWorld* World, const FString& Token, const TCHAR* What, const TCHAR* ParamName, TSharedPtr<FJsonValue>& OutError)
	{
		OutError.Reset();
		if (Token.IsEmpty())
		{
			OutError = MCPError(FString::Printf(TEXT("Missing required parameter '%s' (a %s name or path)"), ParamName, What));
			return nullptr;
		}
		TArray<ULandscapeSplinesComponent*> Components;
		MCPLandscapeSplines::CollectSplineComponents(World, Components);
		TArray<TElement*> Matches;
		TArray<FString> Known;
		for (ULandscapeSplinesComponent* Comp : Components)
		{
			auto Visit = [&](TElement* Element)
			{
				if (!Element) return;
				if (Element->GetPathName() == Token || Element->GetName() == Token) Matches.Add(Element);
				if (Known.Num() < 20) Known.Add(Element->GetName());
			};
			if constexpr (std::is_same_v<TElement, ULandscapeSplineControlPoint>)
			{
				for (const TObjectPtr<ULandscapeSplineControlPoint>& Point : Comp->GetControlPoints()) Visit(Point.Get());
			}
			else
			{
				for (const TObjectPtr<ULandscapeSplineSegment>& Segment : Comp->GetSegments()) Visit(Segment.Get());
			}
		}
		if (Matches.Num() == 1) return Matches[0];
		if (Matches.Num() > 1)
		{
			TArray<FString> Paths;
			for (TElement* Match : Matches) Paths.Add(Match->GetPathName());
			TSharedPtr<FJsonObject> ErrorObj = MCPErrorObject(FString::Printf(TEXT("%s '%s' names %d %ss in different spline owners; pass the full path. Candidates: [%s]"),
				ParamName, *Token, Matches.Num(), What, *FString::Join(Paths, TEXT(", "))));
			ErrorObj->SetBoolField(TEXT("ambiguous"), true);
			ErrorObj->SetArrayField(TEXT("candidates"), MCPStringListToJson(Paths));
			OutError = MakeShared<FJsonValueObject>(ErrorObj);
			return nullptr;
		}
		OutError = MCPError(FString::Printf(TEXT("No landscape spline %s named '%s' in the editor world. Known: [%s]"),
			What, *Token, *FString::Join(Known, TEXT(", "))));
		return nullptr;
	}

	ULandscapeSplineControlPoint* MCPLspFindPoint(UWorld* World, const FString& Token, const TCHAR* ParamName, TSharedPtr<FJsonValue>& OutError)
	{
		return MCPLspFindElement<ULandscapeSplineControlPoint>(World, Token, TEXT("control point"), ParamName, OutError);
	}

	ULandscapeSplineSegment* MCPLspFindSegment(UWorld* World, const FString& Token, TSharedPtr<FJsonValue>& OutError)
	{
		return MCPLspFindElement<ULandscapeSplineSegment>(World, Token, TEXT("segment"), TEXT("segment"), OutError);
	}

	/** Ask the landscape to re-apply its splines layer on its own, as the
	 *  Landscape Mode spline tool does after an edit. A landscape without a
	 *  reserved splines layer ignores the request; apply_splines is the write. */
	void MCPLspRequestLayerUpdate(ULandscapeSplinesComponent* Comp)
	{
		if (ALandscape* Landscape = MCPLspLandscapeOf(Comp))
		{
			Landscape->RequestSplineLayerUpdate();
		}
		Comp->MarkRenderStateDirty();
	}

	/** The point fields a create or update may set. Each is applied only when the call carries it. */
	TSharedPtr<FJsonValue> MCPLspApplyPointFields(const TSharedPtr<FJsonObject>& Params, ULandscapeSplinesComponent* Comp, ULandscapeSplineControlPoint* Point, bool& bOutRotationGiven)
	{
		bOutRotationGiven = false;
		const TSharedPtr<FJsonObject>* LocationObj = nullptr;
		if (TryGetObjectParam(Params, TEXT("location"), LocationObj) && LocationObj)
		{
			FVector World;
			if (!ReadVec3FieldsStrict(*LocationObj, World) || World.ContainsNaN())
			{
				return MCPError(TEXT("location must be {x, y, z} with every axis a finite number"));
			}
			Point->Location = MCPLspToLocal(Comp, World);
		}
		const TSharedPtr<FJsonObject>* RotationObj = nullptr;
		if (TryGetObjectParam(Params, TEXT("rotation"), RotationObj) && RotationObj)
		{
			FRotator World = FRotator::ZeroRotator;
			if (!ReadRotatorFields(*RotationObj, World) || World.ContainsNaN())
			{
				return MCPError(TEXT("rotation must be {pitch, yaw, roll} in degrees"));
			}
			Point->Rotation = MCPLspRotToLocal(Comp, World);
			bOutRotationGiven = true;
		}
		double Number = 0.0;
		if (TryGetNumberParam(Params, TEXT("halfWidth"), Number))
		{
			if (Number < 0.0) return MCPError(TEXT("halfWidth is in centimetres and cannot be negative"));
			Point->Width = static_cast<float>(Number);
		}
		if (TryGetNumberParam(Params, TEXT("sideFalloff"), Number))
		{
			if (Number < 0.0) return MCPError(TEXT("sideFalloff cannot be negative"));
			Point->SideFalloff = static_cast<float>(Number);
		}
		if (TryGetNumberParam(Params, TEXT("endFalloff"), Number))
		{
			if (Number < 0.0) return MCPError(TEXT("endFalloff cannot be negative"));
			Point->EndFalloff = static_cast<float>(Number);
		}
		FString Layer;
		if (TryGetStringParam(Params, TEXT("layerName"), Layer))
		{
			Point->LayerName = Layer.IsEmpty() ? FName(NAME_None) : FName(*Layer);
		}
		bool bFlag = false;
		if (TryGetBoolParam(Params, TEXT("raiseTerrain"), bFlag)) Point->bRaiseTerrain = bFlag;
		if (TryGetBoolParam(Params, TEXT("lowerTerrain"), bFlag)) Point->bLowerTerrain = bFlag;
		return nullptr;
	}

	/** A point's settable fields, in the form create and update take them. */
	TSharedPtr<FJsonObject> MCPLspPointFieldsPayload(ULandscapeSplineControlPoint* Point)
	{
		ULandscapeSplinesComponent* Comp = Point->GetOuterSafe();
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(MCPLspToWorld(Comp, Point->Location)));
		Payload->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(MCPLspRotToWorld(Comp, Point->Rotation)));
		Payload->SetNumberField(TEXT("halfWidth"), Point->Width);
		Payload->SetNumberField(TEXT("sideFalloff"), Point->SideFalloff);
		Payload->SetNumberField(TEXT("endFalloff"), Point->EndFalloff);
		Payload->SetStringField(TEXT("layerName"), MCPLspLayerString(Point->LayerName));
		Payload->SetBoolField(TEXT("raiseTerrain"), Point->bRaiseTerrain != 0);
		Payload->SetBoolField(TEXT("lowerTerrain"), Point->bLowerTerrain != 0);
		return Payload;
	}

	/** Segment fields shared by connect and update. */
	TSharedPtr<FJsonValue> MCPLspApplySegmentFields(const TSharedPtr<FJsonObject>& Params, ULandscapeSplineSegment* Segment)
	{
		double Number = 0.0;
		if (TryGetNumberParam(Params, TEXT("startTangentLength"), Number))
		{
			if (!FMath::IsFinite(Number)) return MCPError(TEXT("startTangentLength must be a finite number"));
			Segment->Connections[0].TangentLen = static_cast<float>(Number);
		}
		if (TryGetNumberParam(Params, TEXT("endTangentLength"), Number))
		{
			if (!FMath::IsFinite(Number)) return MCPError(TEXT("endTangentLength must be a finite number"));
			Segment->Connections[1].TangentLen = static_cast<float>(Number);
		}
		FString Layer;
		if (TryGetStringParam(Params, TEXT("layerName"), Layer))
		{
			Segment->LayerName = Layer.IsEmpty() ? FName(NAME_None) : FName(*Layer);
		}
		bool bFlag = false;
		if (TryGetBoolParam(Params, TEXT("raiseTerrain"), bFlag)) Segment->bRaiseTerrain = bFlag;
		if (TryGetBoolParam(Params, TEXT("lowerTerrain"), bFlag)) Segment->bLowerTerrain = bFlag;
		return nullptr;
	}

	TSharedPtr<FJsonObject> MCPLspSegmentFieldsPayload(const ULandscapeSplineSegment* Segment)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetNumberField(TEXT("startTangentLength"), Segment->Connections[0].TangentLen);
		Payload->SetNumberField(TEXT("endTangentLength"), Segment->Connections[1].TangentLen);
		Payload->SetStringField(TEXT("layerName"), MCPLspLayerString(Segment->LayerName));
		Payload->SetBoolField(TEXT("raiseTerrain"), Segment->bRaiseTerrain != 0);
		Payload->SetBoolField(TEXT("lowerTerrain"), Segment->bLowerTerrain != 0);
		return Payload;
	}

	/** Parse meshes[] into entries, loading every mesh and material first so a
	 *  bad path refuses the call before the segment is touched. */
	TSharedPtr<FJsonValue> MCPLspParseMeshEntries(const TArray<TSharedPtr<FJsonValue>>& Rows, TArray<FLandscapeSplineMeshEntry>& Out)
	{
		for (int32 Index = 0; Index < Rows.Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* Row = nullptr;
			if (!Rows[Index].IsValid() || !Rows[Index]->TryGetObject(Row) || !Row || !Row->IsValid())
			{
				return MCPError(FString::Printf(TEXT("meshes[%d] must be an object with a mesh path"), Index));
			}
			FString MeshPath;
			if (!(*Row)->TryGetStringField(TEXT("mesh"), MeshPath) || MeshPath.IsEmpty())
			{
				return MCPError(FString::Printf(TEXT("meshes[%d].mesh is required"), Index));
			}
			UStaticMesh* Mesh = Cast<UStaticMesh>(MCPLoadAssetObject(MeshPath));
			if (!Mesh) return MCPError(FString::Printf(TEXT("meshes[%d].mesh: no StaticMesh at '%s'"), Index, *MeshPath));

			FLandscapeSplineMeshEntry Entry;
			Entry.Mesh = Mesh;
			const TSharedPtr<FJsonObject>* ScaleObj = nullptr;
			if ((*Row)->TryGetObjectField(TEXT("scale"), ScaleObj) && ScaleObj)
			{
				FVector Scale = FVector::OneVector;
				if (!ReadVec3FieldsStrict(*ScaleObj, Scale) || Scale.ContainsNaN())
				{
					return MCPError(FString::Printf(TEXT("meshes[%d].scale must be {x, y, z}"), Index));
				}
				Entry.Scale = Scale;
			}
			FString AxisName;
			ESplineMeshAxis::Type Axis = ESplineMeshAxis::X;
			if ((*Row)->TryGetStringField(TEXT("forwardAxis"), AxisName))
			{
				if (!MCPLspParseAxis(AxisName, Axis)) return MCPError(FString::Printf(TEXT("meshes[%d].forwardAxis must be X, Y or Z"), Index));
				Entry.ForwardAxis = Axis;
			}
			if ((*Row)->TryGetStringField(TEXT("upAxis"), AxisName))
			{
				if (!MCPLspParseAxis(AxisName, Axis)) return MCPError(FString::Printf(TEXT("meshes[%d].upAxis must be X, Y or Z"), Index));
				Entry.UpAxis = Axis;
			}
			if (Entry.ForwardAxis == Entry.UpAxis)
			{
				return MCPError(FString::Printf(TEXT("meshes[%d]: forwardAxis and upAxis must differ"), Index));
			}
			bool bFlag = false;
			if ((*Row)->TryGetBoolField(TEXT("centerH"), bFlag)) Entry.bCenterH = bFlag;
			if ((*Row)->TryGetBoolField(TEXT("scaleToWidth"), bFlag)) Entry.bScaleToWidth = bFlag;
			const TArray<TSharedPtr<FJsonValue>>* Materials = nullptr;
			if ((*Row)->TryGetArrayField(TEXT("materialOverrides"), Materials) && Materials)
			{
				for (int32 Slot = 0; Slot < Materials->Num(); ++Slot)
				{
					FString MaterialPath;
					(*Materials)[Slot]->TryGetString(MaterialPath);
					if (MaterialPath.IsEmpty())
					{
						Entry.MaterialOverrides.Add(nullptr);
						continue;
					}
					UMaterialInterface* Material = Cast<UMaterialInterface>(MCPLoadAssetObject(MaterialPath));
					if (!Material)
					{
						return MCPError(FString::Printf(TEXT("meshes[%d].materialOverrides[%d]: no material at '%s'"), Index, Slot, *MaterialPath));
					}
					Entry.MaterialOverrides.Add(Material);
				}
			}
			Out.Add(MoveTemp(Entry));
		}
		return nullptr;
	}

	/** Mesh fields shared by connect and set_segment_meshes: meshes[], collisionProfile, castShadow. */
	TSharedPtr<FJsonValue> MCPLspApplyMeshFields(const TSharedPtr<FJsonObject>& Params, ULandscapeSplineSegment* Segment, bool& bOutMeshesGiven)
	{
		bOutMeshesGiven = false;
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		TArray<FLandscapeSplineMeshEntry> Entries;
		if (TryGetArrayParam(Params, TEXT("meshes"), Rows) && Rows)
		{
			if (auto Err = MCPLspParseMeshEntries(*Rows, Entries)) return Err;
			bOutMeshesGiven = true;
		}
		FString Profile;
		const bool bProfileGiven = TryGetStringParam(Params, TEXT("collisionProfile"), Profile) && !Profile.IsEmpty();
		if (bOutMeshesGiven) Segment->SplineMeshes = MoveTemp(Entries);
		if (bProfileGiven) Segment->BodyInstance.SetCollisionProfileName(FName(*Profile));
		bool bShadow = false;
		if (TryGetBoolParam(Params, TEXT("castShadow"), bShadow)) Segment->bCastShadow = bShadow;
		return nullptr;
	}

	TSharedPtr<FJsonObject> MCPLspMeshFieldsPayload(const ULandscapeSplineSegment* Segment)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("meshes"), MCPLspDescribeMeshes(Segment));
		Payload->SetStringField(TEXT("collisionProfile"), Segment->BodyInstance.GetCollisionProfileName().ToString());
		Payload->SetBoolField(TEXT("castShadow"), Segment->bCastShadow != 0);
		return Payload;
	}

	void MCPLspCopyFields(const TSharedPtr<FJsonObject>& From, const TSharedPtr<FJsonObject>& To)
	{
		for (const auto& Pair : From->Values) To->SetField(Pair.Key, Pair.Value);
	}

	/** Remove a segment from the network, the way the spline tool's delete does. */
	void MCPLspRemoveSegment(ULandscapeSplinesComponent* Comp, ULandscapeSplineSegment* Segment)
	{
		Segment->Modify();
		Segment->DeleteSplinePoints();
		ULandscapeSplineControlPoint* Start = Segment->Connections[0].ControlPoint;
		ULandscapeSplineControlPoint* End = Segment->Connections[1].ControlPoint;
		if (Start)
		{
			Start->Modify();
			Start->ConnectedSegments.Remove(FLandscapeSplineConnection(Segment, 0));
		}
		if (End)
		{
			End->Modify();
			End->ConnectedSegments.Remove(FLandscapeSplineConnection(Segment, 1));
		}
		Comp->GetSegments().Remove(Segment);
		if (Start && Start->GetOuterSafe()) Start->UpdateSplinePoints();
		if (End && End != Start && End->GetOuterSafe()) End->UpdateSplinePoints();
	}

	/** Resolve the splines component a new point goes into.
	 *  ownerPath names an existing owner (a landscape, proxy or spline actor).
	 *  Otherwise a World Partition map gets a new LandscapeSplineActor, as the
	 *  spline tool makes one, and a single landscape uses its own component. */
	ULandscapeSplinesComponent* MCPLspResolveTargetComponent(
		UWorld* World, ALandscape* Landscape, const FString& OwnerToken, bool bNewSplineActor, const FVector& WorldLocation,
		bool& bOutCreatedOwner, TSharedPtr<FJsonValue>& OutError)
	{
		bOutCreatedOwner = false;
		if (!OwnerToken.IsEmpty())
		{
			FMCPActorSelector Selector;
			Selector.LabelKey = TEXT("ownerPath");
			Selector.PathKey = TEXT("ownerPath");
			Selector.Match = EMCPActorMatch::LabelNameOrPath;
			AActor* Owner = MCPResolveActorToken(World, OwnerToken, OutError, Selector);
			if (!Owner) return nullptr;
			ILandscapeSplineInterface* AsOwner = Cast<ILandscapeSplineInterface>(Owner);
			if (!AsOwner)
			{
				OutError = MCPError(FString::Printf(TEXT("ownerPath '%s' is a %s, which cannot own landscape splines; pass a Landscape, LandscapeStreamingProxy or LandscapeSplineActor"),
					*OwnerToken, *Owner->GetClass()->GetName()));
				return nullptr;
			}
			ULandscapeSplinesComponent* Comp = AsOwner->GetSplinesComponent();
			if (!Comp)
			{
				AsOwner->CreateSplineComponent();
				Comp = AsOwner->GetSplinesComponent();
				bOutCreatedOwner = Comp != nullptr;
			}
			if (!Comp) OutError = MCPError(FString::Printf(TEXT("'%s' did not create a splines component"), *Owner->GetActorLabel()));
			return Comp;
		}

		ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
		if (!Info)
		{
			OutError = MCPError(FString::Printf(TEXT("Landscape '%s' has no LandscapeInfo; is it registered?"), *Landscape->GetActorLabel()));
			return nullptr;
		}
		const bool bGridBased = World->IsPartitionedWorld();
		if (bGridBased || bNewSplineActor)
		{
			if (!bGridBased)
			{
				OutError = MCPError(TEXT("newSplineActor is for World Partition maps; on this map the landscape owns its splines component"));
				return nullptr;
			}
			ALandscapeSplineActor* SplineActor = Info->CreateSplineActor(WorldLocation);
			if (!SplineActor || !SplineActor->GetSplinesComponent())
			{
				OutError = MCPError(TEXT("ULandscapeInfo::CreateSplineActor did not produce a spline actor"));
				return nullptr;
			}
			bOutCreatedOwner = true;
			return SplineActor->GetSplinesComponent();
		}
		ULandscapeSplinesComponent* Comp = Landscape->GetSplinesComponent();
		if (!Comp)
		{
			Landscape->CreateSplineComponent();
			Comp = Landscape->GetSplinesComponent();
			bOutCreatedOwner = Comp != nullptr;
		}
		if (!Comp) OutError = MCPError(FString::Printf(TEXT("Landscape '%s' did not create a splines component"), *Landscape->GetActorLabel()));
		return Comp;
	}
}

void MCPLandscapeSplines::CollectSplineComponents(UWorld* World, TArray<ULandscapeSplinesComponent*>& Out)
{
	if (!World) return;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || !Cast<ILandscapeSplineInterface>(Actor)) continue;
		TArray<ULandscapeSplinesComponent*> Found;
		Actor->GetComponents<ULandscapeSplinesComponent>(Found);
		for (ULandscapeSplinesComponent* Comp : Found)
		{
			if (Comp) Out.AddUnique(Comp);
		}
	}
	Out.Sort([](const ULandscapeSplinesComponent& A, const ULandscapeSplinesComponent& B) { return A.GetPathName() < B.GetPathName(); });
}

void MCPLandscapeSplines::DescribeAll(UWorld* World, TArray<TSharedPtr<FJsonValue>>& OutPoints, TArray<TSharedPtr<FJsonValue>>& OutSegments)
{
	TArray<ULandscapeSplinesComponent*> Components;
	CollectSplineComponents(World, Components);
	for (ULandscapeSplinesComponent* Comp : Components)
	{
		for (const TObjectPtr<ULandscapeSplineControlPoint>& Point : Comp->GetControlPoints())
		{
			if (Point) OutPoints.Add(MakeShared<FJsonValueObject>(MCPLspDescribePoint(Point.Get())));
		}
		for (const TObjectPtr<ULandscapeSplineSegment>& Segment : Comp->GetSegments())
		{
			if (Segment) OutSegments.Add(MakeShared<FJsonValueObject>(MCPLspDescribeSegment(Segment.Get())));
		}
	}
}

void FLandscapeSplineHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("landscape"));
	using EType = EMCPParamType;

	const FMCPParamSpec SpecActorLabel = MCPParam::Optional(TEXT("actorLabel"), EType::String, TEXT("Landscape actor label, when the level has more than one"));
	const FMCPParamSpec SpecActorPath = MCPParam::Optional(TEXT("actorPath"), EType::String, TEXT("Landscape actor path; wins over actorLabel"));
	const FMCPParamSpec SpecLocation = MCPParam::Optional(TEXT("location"), EType::Vec3, TEXT("World location of the control point"));
	const FMCPParamSpec SpecRotation = MCPParam::Optional(TEXT("rotation"), EType::Rotator, TEXT("World rotation of the tangent at the point"));
	const FMCPParamSpec SpecWidth = MCPParam::Optional(TEXT("halfWidth"), EType::Number, TEXT("Half-width of the spline at the point, in centimetres (engine default 1000)"));
	const FMCPParamSpec SpecSideFalloff = MCPParam::Optional(TEXT("sideFalloff"), EType::Number, TEXT("Falloff beyond each side, in centimetres (engine default 1000)"));
	const FMCPParamSpec SpecEndFalloff = MCPParam::Optional(TEXT("endFalloff"), EType::Number, TEXT("Falloff past an end point, in centimetres (engine default 2000)"));
	const FMCPParamSpec SpecPointLayer = MCPParam::Optional(TEXT("layerName"), EType::String, TEXT("Paint layer applying the spline paints; empty clears it"));
	const FMCPParamSpec SpecSegmentLayer = MCPParam::Optional(TEXT("layerName"), EType::String, TEXT("Paint layer the segment paints when applied; empty clears it"));
	const FMCPParamSpec SpecRaise = MCPParam::Optional(TEXT("raiseTerrain"), EType::Boolean, TEXT("Raise terrain below the spline up to it when applied (engine default true)"));
	const FMCPParamSpec SpecLower = MCPParam::Optional(TEXT("lowerTerrain"), EType::Boolean, TEXT("Lower terrain above the spline down to it when applied (engine default true)"));
	const FMCPParamSpec SpecStartTangent = MCPParam::Optional(TEXT("startTangentLength"), EType::Number, TEXT("Tangent length at the start point (default the distance between the points)"));
	const FMCPParamSpec SpecEndTangent = MCPParam::Optional(TEXT("endTangentLength"), EType::Number, TEXT("Tangent length at the end point (default the distance between the points)"));
	const FMCPParamSpec SpecSegment = MCPParam::Required(TEXT("segment"), EType::String, TEXT("Segment object name or full path, as list_splines reports it"));
	const FMCPParamSpec SpecControlPoint = MCPParam::Required(TEXT("controlPoint"), EType::String, TEXT("Control point object name or full path, as list_splines reports it"));
	const FMCPParamSpec SpecMeshes = MCPParam::Optional(TEXT("meshes"), EType::Array, TEXT("Meshes deformed along the segment, replacing the current list; an empty list clears it")).Items(EType::Object).WithFields({
		MCPParam::RequiredField(TEXT("mesh"), EType::String, TEXT("StaticMesh asset path")),
		MCPParam::OptionalField(TEXT("scale"), EType::Vec3, TEXT("Mesh scale (default 1, 1, 1)")),
		MCPParam::OptionalField(TEXT("forwardAxis"), EType::String, TEXT("X (default) | Y | Z: the mesh axis laid along the spline")),
		MCPParam::OptionalField(TEXT("upAxis"), EType::String, TEXT("X | Y | Z (default): the mesh axis kept up")),
		MCPParam::OptionalField(TEXT("materialOverrides"), EType::Array, TEXT("Material per slot; an empty string keeps the mesh's own")).Items(EType::String),
		MCPParam::OptionalField(TEXT("centerH"), EType::Boolean, TEXT("Centre the mesh horizontally on the spline (default true)")),
		MCPParam::OptionalField(TEXT("scaleToWidth"), EType::Boolean, TEXT("Scale the mesh to the spline width (default true)")),
	});
	const FMCPParamSpec SpecCollision = MCPParam::Optional(TEXT("collisionProfile"), EType::String, TEXT("Collision profile of the segment's mesh components, e.g. BlockAll or NoCollision"));
	const FMCPParamSpec SpecCastShadow = MCPParam::Optional(TEXT("castShadow"), EType::Boolean, TEXT("Whether the segment's meshes cast shadows"));

	Registry.RegisterHandler(TEXT("create_landscape_spline_point"), &CreatePoint, {
		MCPParam::Required(TEXT("location"), EType::Vec3, TEXT("World location of the control point")),
		SpecRotation, SpecWidth, SpecSideFalloff, SpecEndFalloff, SpecPointLayer, SpecRaise, SpecLower,
		MCPParam::Optional(TEXT("name"), EType::String, TEXT("Object name for the point; an existing point of this name is reported rather than duplicated")),
		MCPParam::Optional(TEXT("ownerPath"), EType::String, TEXT("Existing spline owner to add the point to: a Landscape, LandscapeStreamingProxy or LandscapeSplineActor, by label or path")),
		MCPParam::Optional(TEXT("newSplineActor"), EType::Boolean, TEXT("World Partition: put the point in a new LandscapeSplineActor (the default when ownerPath is not given)")),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("When name is taken: skip (default) | error")),
		SpecActorLabel, SpecActorPath,
	});
	Registry.RegisterHandler(TEXT("update_landscape_spline_point"), &UpdatePoint, {
		SpecControlPoint, SpecLocation, SpecRotation, SpecWidth, SpecSideFalloff, SpecEndFalloff, SpecPointLayer, SpecRaise, SpecLower,
		MCPParam::Optional(TEXT("autoRotate"), EType::Boolean, TEXT("Recompute the rotation from the connected segments when rotation is not given (default true when location moves)")),
	});
	Registry.RegisterHandler(TEXT("delete_landscape_spline_point"), &DeletePoint, {
		SpecControlPoint,
	});
	Registry.RegisterHandler(TEXT("connect_landscape_spline_points"), &ConnectPoints, {
		MCPParam::Required(TEXT("startPoint"), EType::String, TEXT("Control point the segment starts at, by name or path")),
		MCPParam::Required(TEXT("endPoint"), EType::String, TEXT("Control point the segment ends at; must share the start point's spline owner")),
		SpecStartTangent, SpecEndTangent, SpecSegmentLayer, SpecRaise, SpecLower, SpecMeshes, SpecCollision, SpecCastShadow,
		MCPParam::Optional(TEXT("autoRotate"), EType::Boolean, TEXT("Rotate both points to follow the new segment (default true)")),
		MCPParam::Optional(TEXT("name"), EType::String, TEXT("Object name for the segment")),
	});
	Registry.RegisterHandler(TEXT("update_landscape_spline_segment"), &UpdateSegment, {
		SpecSegment, SpecStartTangent, SpecEndTangent, SpecSegmentLayer, SpecRaise, SpecLower,
	});
	Registry.RegisterHandler(TEXT("set_landscape_spline_segment_meshes"), &SetSegmentMeshes, {
		SpecSegment, SpecMeshes, SpecCollision, SpecCastShadow,
	});
	Registry.RegisterHandler(TEXT("delete_landscape_spline_segment"), &DeleteSegment, {
		SpecSegment,
	});
	Registry.RegisterHandlerWithTimeout(TEXT("apply_landscape_splines"), &ApplySplines, 300.0f, {
		SpecActorLabel, SpecActorPath,
		MCPParam::Optional(TEXT("editLayer"), EType::String, TEXT("Edit layer to write into when the landscape has no reserved splines layer")),
		MCPParam::Optional(TEXT("editLayerIndex"), EType::Integer, TEXT("Edit layer index when editLayer is not given (default 0)")),
		MCPParam::Optional(TEXT("updateNow"), EType::Boolean, TEXT("Merge the edit layers now rather than on the next editor tick (default true)")),
	});
}

// landscape(create_spline_point)
TSharedPtr<FJsonValue> FLandscapeSplineHandlers::CreatePoint(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("location"), TEXT("rotation"), TEXT("halfWidth"), TEXT("sideFalloff"), TEXT("endFalloff"), TEXT("layerName"),
		TEXT("raiseTerrain"), TEXT("lowerTerrain"), TEXT("name"), TEXT("ownerPath"), TEXT("newSplineActor"), TEXT("onConflict"),
		TEXT("actorLabel"), TEXT("actorPath"),
	});
	REQUIRE_EDITOR_WORLD(World);

	FVector WorldLocation;
	if (auto Err = RequireVec3(Params, TEXT("location"), WorldLocation)) return Err;
	const FString Name = OptionalString(Params, TEXT("name"));
	const FString OwnerToken = OptionalString(Params, TEXT("ownerPath"));
	const bool bNewSplineActor = OptionalBool(Params, TEXT("newSplineActor"), false);
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	if (OnConflict != TEXT("skip") && OnConflict != TEXT("error"))
	{
		return MCPError(FString::Printf(TEXT("onConflict must be skip or error, not '%s'"), *OnConflict));
	}

	TSharedPtr<FJsonValue> Err;
	ALandscape* Landscape = MCPLandscape::ResolveLandscape(World, Params, Err);
	if (!Landscape) return Err;

	if (!Name.IsEmpty())
	{
		TSharedPtr<FJsonValue> MissErr;
		if (ULandscapeSplineControlPoint* Existing = MCPLspFindPoint(World, Name, TEXT("name"), MissErr))
		{
			if (OnConflict == TEXT("error"))
			{
				return MCPError(FString::Printf(TEXT("A landscape spline control point named '%s' already exists: %s"), *Name, *Existing->GetPathName()));
			}
			auto Result = MCPSuccess();
			MCPSetExisted(Result);
			Result->SetObjectField(TEXT("controlPoint"), MCPLspDescribePoint(Existing));
			Result->SetStringField(TEXT("controlPointPath"), Existing->GetPathName());
			return MCPResult(Result);
		}
		if (MCPIsAmbiguousActorError(MissErr)) return MissErr;
	}

	FScopedTransaction Transaction(NSLOCTEXT("UEMCP", "LandscapeSplineAddPoint", "Add Landscape Spline Control Point"));
	bool bCreatedOwner = false;
	ULandscapeSplinesComponent* Comp = MCPLspResolveTargetComponent(World, Landscape, OwnerToken, bNewSplineActor, WorldLocation, bCreatedOwner, Err);
	if (!Comp) return Err;
	if (!Name.IsEmpty() && StaticFindObjectFast(nullptr, Comp, FName(*Name)))
	{
		return MCPError(FString::Printf(TEXT("'%s' is already used by another object in %s"), *Name, *Comp->GetPathName()));
	}

	Comp->Modify();
	ULandscapeSplineControlPoint* Point = NewObject<ULandscapeSplineControlPoint>(Comp, Name.IsEmpty() ? FName(NAME_None) : FName(*Name), RF_Transactional);
	Point->Location = MCPLspToLocal(Comp, WorldLocation);
	bool bRotationGiven = false;
	if (auto FieldErr = MCPLspApplyPointFields(Params, Comp, Point, bRotationGiven))
	{
		Point->MarkAsGarbage();
		Transaction.Cancel();
		return FieldErr;
	}
	Comp->GetControlPoints().Add(Point);
	Point->UpdateSplinePoints();
	MCPLspRequestLayerUpdate(Comp);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetObjectField(TEXT("controlPoint"), MCPLspDescribePoint(Point));
	Result->SetStringField(TEXT("controlPointPath"), Point->GetPathName());
	Result->SetStringField(TEXT("ownerPath"), Comp->GetOwner() ? Comp->GetOwner()->GetPathName() : FString());
	Result->SetBoolField(TEXT("createdOwner"), bCreatedOwner);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("controlPoint"), Point->GetPathName());
	MCPSetRollback(Result, TEXT("delete_landscape_spline_point"), Payload);
	if (bCreatedOwner)
	{
		Result->SetStringField(TEXT("rollbackNote"), TEXT("The rollback deletes the point; the spline owner created for it stays, empty."));
	}
	return MCPResult(Result);
}

// landscape(update_spline_point): move a point or change its shape and deformation settings.
TSharedPtr<FJsonValue> FLandscapeSplineHandlers::UpdatePoint(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("controlPoint"), TEXT("location"), TEXT("rotation"), TEXT("halfWidth"), TEXT("sideFalloff"), TEXT("endFalloff"),
		TEXT("layerName"), TEXT("raiseTerrain"), TEXT("lowerTerrain"), TEXT("autoRotate"),
	});
	REQUIRE_EDITOR_WORLD(World);
	FString Token;
	if (auto Err = RequireString(Params, TEXT("controlPoint"), Token)) return Err;
	TSharedPtr<FJsonValue> Err;
	ULandscapeSplineControlPoint* Point = MCPLspFindPoint(World, Token, TEXT("controlPoint"), Err);
	if (!Point) return Err;
	ULandscapeSplinesComponent* Comp = Point->GetOuterSafe();

	const TSharedPtr<FJsonObject> Before = MCPLspPointFieldsPayload(Point);
	const FVector PreviousLocation = Point->Location;

	FScopedTransaction Transaction(NSLOCTEXT("UEMCP", "LandscapeSplineUpdatePoint", "Edit Landscape Spline Control Point"));
	Comp->Modify();
	Point->Modify();
	bool bRotationGiven = false;
	if (auto FieldErr = MCPLspApplyPointFields(Params, Comp, Point, bRotationGiven))
	{
		Transaction.Cancel();
		return FieldErr;
	}
	const bool bMoved = !Point->Location.Equals(PreviousLocation, 0.01);
	if (!bRotationGiven && bMoved && OptionalBool(Params, TEXT("autoRotate"), true) && Point->ConnectedSegments.Num() > 0)
	{
		Point->AutoCalcRotation(false);
	}
	Point->UpdateSplinePoints();
	MCPLspRequestLayerUpdate(Comp);

	const TSharedPtr<FJsonObject> After = MCPLspPointFieldsPayload(Point);
	FString BeforeText, AfterText;
	{
		const TSharedRef<TJsonWriter<>> W1 = TJsonWriterFactory<>::Create(&BeforeText);
		FJsonSerializer::Serialize(Before.ToSharedRef(), W1);
		const TSharedRef<TJsonWriter<>> W2 = TJsonWriterFactory<>::Create(&AfterText);
		FJsonSerializer::Serialize(After.ToSharedRef(), W2);
	}
	const bool bChanged = BeforeText != AfterText;

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("updated"), bChanged);
	Result->SetBoolField(TEXT("unchanged"), !bChanged);
	Result->SetObjectField(TEXT("previous"), Before);
	Result->SetObjectField(TEXT("controlPoint"), MCPLspDescribePoint(Point));
	if (bChanged)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("controlPoint"), Point->GetPathName());
		MCPLspCopyFields(Before, Payload);
		Payload->SetBoolField(TEXT("autoRotate"), false);
		MCPSetRollback(Result, TEXT("update_landscape_spline_point"), Payload);
	}
	return MCPResult(Result);
}

// landscape(delete_spline_point): remove a point and every segment connected to it.
TSharedPtr<FJsonValue> FLandscapeSplineHandlers::DeletePoint(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, { TEXT("controlPoint") });
	REQUIRE_EDITOR_WORLD(World);
	FString Token;
	if (auto Err = RequireString(Params, TEXT("controlPoint"), Token)) return Err;
	TSharedPtr<FJsonValue> Err;
	ULandscapeSplineControlPoint* Point = MCPLspFindPoint(World, Token, TEXT("controlPoint"), Err);
	if (!Point)
	{
		if (MCPIsAmbiguousActorError(Err)) return Err;
		auto Result = MCPSuccess();
		Result->SetBoolField(TEXT("alreadyDeleted"), true);
		Result->SetStringField(TEXT("controlPoint"), Token);
		return MCPResult(Result);
	}
	ULandscapeSplinesComponent* Comp = Point->GetOuterSafe();
	const FString PointName = Point->GetName();
	const FString PointPath = Point->GetPathName();
	AActor* Owner = Comp->GetOwner();
	const TSharedPtr<FJsonObject> Fields = MCPLspPointFieldsPayload(Point);

	TArray<FString> RemovedSegments;
	FScopedTransaction Transaction(NSLOCTEXT("UEMCP", "LandscapeSplineDeletePoint", "Delete Landscape Spline Control Point"));
	Comp->Modify();
	Point->Modify();
	Point->DeleteSplinePoints();
	const TArray<FLandscapeSplineConnection> Connections = Point->ConnectedSegments;
	for (const FLandscapeSplineConnection& Connection : Connections)
	{
		ULandscapeSplineSegment* Segment = Connection.Segment;
		if (!Segment) continue;
		RemovedSegments.Add(Segment->GetName());
		Segment->Modify();
		Segment->DeleteSplinePoints();
		ULandscapeSplineControlPoint* OtherEnd = Connection.GetFarConnection().ControlPoint;
		if (OtherEnd && OtherEnd != Point)
		{
			OtherEnd->Modify();
			OtherEnd->ConnectedSegments.Remove(FLandscapeSplineConnection(Segment, 1 - Connection.End));
			if (OtherEnd->GetOuterSafe()) OtherEnd->UpdateSplinePoints();
		}
		Comp->GetSegments().Remove(Segment);
	}
	Point->ConnectedSegments.Empty();
	Comp->GetControlPoints().Remove(Point);
	MCPLspRequestLayerUpdate(Comp);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("deleted"), true);
	Result->SetStringField(TEXT("controlPoint"), PointName);
	Result->SetStringField(TEXT("controlPointPath"), PointPath);
	Result->SetArrayField(TEXT("removedSegments"), MCPStringListToJson(RemovedSegments));
	if (RemovedSegments.Num() == 0)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		MCPLspCopyFields(Fields, Payload);
		Payload->SetStringField(TEXT("name"), PointName);
		if (Owner) Payload->SetStringField(TEXT("ownerPath"), Owner->GetPathName());
		MCPSetRollback(Result, TEXT("create_landscape_spline_point"), Payload);
	}
	else
	{
		MCPSetNoRollback(Result, FString::Printf(
			TEXT("Deleting the point also removed %d connected segment(s) with their meshes and settings. Recreate the point with create_spline_point and each segment with connect_spline_points, or use editor undo."),
			RemovedSegments.Num()));
	}
	return MCPResult(Result);
}

// landscape(connect_spline_points): join two control points with a segment.
TSharedPtr<FJsonValue> FLandscapeSplineHandlers::ConnectPoints(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("startPoint"), TEXT("endPoint"), TEXT("startTangentLength"), TEXT("endTangentLength"), TEXT("layerName"),
		TEXT("raiseTerrain"), TEXT("lowerTerrain"), TEXT("meshes"), TEXT("collisionProfile"), TEXT("castShadow"),
		TEXT("autoRotate"), TEXT("name"),
	});
	REQUIRE_EDITOR_WORLD(World);
	FString StartToken, EndToken;
	if (auto Err = RequireString(Params, TEXT("startPoint"), StartToken)) return Err;
	if (auto Err = RequireString(Params, TEXT("endPoint"), EndToken)) return Err;
	TSharedPtr<FJsonValue> Err;
	ULandscapeSplineControlPoint* Start = MCPLspFindPoint(World, StartToken, TEXT("startPoint"), Err);
	if (!Start) return Err;
	ULandscapeSplineControlPoint* End = MCPLspFindPoint(World, EndToken, TEXT("endPoint"), Err);
	if (!End) return Err;
	if (Start == End) return MCPError(TEXT("startPoint and endPoint are the same control point"));
	ULandscapeSplinesComponent* Comp = Start->GetOuterSafe();
	if (Comp != End->GetOuterSafe())
	{
		return MCPError(FString::Printf(TEXT("The two points belong to different spline owners (%s and %s); a segment can only join points in one owner"),
			*GetNameSafe(Comp ? Comp->GetOwner() : nullptr), *GetNameSafe(End->GetOuterSafe() ? End->GetOuterSafe()->GetOwner() : nullptr)));
	}

	for (const FLandscapeSplineConnection& Connection : Start->ConnectedSegments)
	{
		if (Connection.Segment && Connection.GetFarConnection().ControlPoint == End)
		{
			auto Result = MCPSuccess();
			MCPSetExisted(Result);
			Result->SetObjectField(TEXT("segment"), MCPLspDescribeSegment(Connection.Segment));
			Result->SetStringField(TEXT("segmentPath"), Connection.Segment->GetPathName());
			return MCPResult(Result);
		}
	}

	// Validate the mesh list before anything is created.
	{
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		TArray<FLandscapeSplineMeshEntry> Probe;
		if (TryGetArrayParam(Params, TEXT("meshes"), Rows) && Rows)
		{
			if (auto MeshErr = MCPLspParseMeshEntries(*Rows, Probe)) return MeshErr;
		}
	}
	const FString Name = OptionalString(Params, TEXT("name"));
	if (!Name.IsEmpty() && StaticFindObjectFast(nullptr, Comp, FName(*Name)))
	{
		return MCPError(FString::Printf(TEXT("'%s' is already used by another object in %s"), *Name, *Comp->GetPathName()));
	}

	FScopedTransaction Transaction(NSLOCTEXT("UEMCP", "LandscapeSplineAddSegment", "Add Landscape Spline Segment"));
	Comp->Modify();
	Start->Modify();
	End->Modify();
	ULandscapeSplineSegment* Segment = NewObject<ULandscapeSplineSegment>(Comp, Name.IsEmpty() ? FName(NAME_None) : FName(*Name), RF_Transactional);
	Comp->GetSegments().Add(Segment);
	Segment->Connections[0].ControlPoint = Start;
	Segment->Connections[1].ControlPoint = End;
	Segment->Connections[0].SocketName = Start->GetBestConnectionTo(End->Location);
	Segment->Connections[1].SocketName = End->GetBestConnectionTo(Start->Location);
	FVector StartLocation; FRotator StartRotation;
	Start->GetConnectionLocationAndRotation(Segment->Connections[0].SocketName, StartLocation, StartRotation);
	FVector EndLocation; FRotator EndRotation;
	End->GetConnectionLocationAndRotation(Segment->Connections[1].SocketName, EndLocation, EndRotation);
	const float Distance = static_cast<float>((EndLocation - StartLocation).Size());
	Segment->Connections[0].TangentLen = Distance;
	Segment->Connections[1].TangentLen = Distance;
	Start->ConnectedSegments.Add(FLandscapeSplineConnection(Segment, 0));
	End->ConnectedSegments.Add(FLandscapeSplineConnection(Segment, 1));

	if (OptionalBool(Params, TEXT("autoRotate"), true))
	{
		Start->AutoCalcRotation(false);
		End->AutoCalcRotation(false);
	}
	Segment->AutoFlipTangents();
	if (auto FieldErr = MCPLspApplySegmentFields(Params, Segment)) { Transaction.Cancel(); return FieldErr; }
	bool bMeshesGiven = false;
	if (auto MeshErr = MCPLspApplyMeshFields(Params, Segment, bMeshesGiven)) { Transaction.Cancel(); return MeshErr; }
	Start->UpdateSplinePoints();
	End->UpdateSplinePoints();
	Segment->UpdateSplinePoints();
	MCPLspRequestLayerUpdate(Comp);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetObjectField(TEXT("segment"), MCPLspDescribeSegment(Segment));
	Result->SetStringField(TEXT("segmentPath"), Segment->GetPathName());
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("segment"), Segment->GetPathName());
	MCPSetRollback(Result, TEXT("delete_landscape_spline_segment"), Payload);
	return MCPResult(Result);
}

// landscape(update_spline_segment): tangents, paint layer and raise/lower flags.
TSharedPtr<FJsonValue> FLandscapeSplineHandlers::UpdateSegment(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("segment"), TEXT("startTangentLength"), TEXT("endTangentLength"), TEXT("layerName"), TEXT("raiseTerrain"), TEXT("lowerTerrain"),
	});
	REQUIRE_EDITOR_WORLD(World);
	FString Token;
	if (auto Err = RequireString(Params, TEXT("segment"), Token)) return Err;
	TSharedPtr<FJsonValue> Err;
	ULandscapeSplineSegment* Segment = MCPLspFindSegment(World, Token, Err);
	if (!Segment) return Err;
	ULandscapeSplinesComponent* Comp = Segment->GetOuterSafe();
	const TSharedPtr<FJsonObject> Before = MCPLspSegmentFieldsPayload(Segment);

	FScopedTransaction Transaction(NSLOCTEXT("UEMCP", "LandscapeSplineUpdateSegment", "Edit Landscape Spline Segment"));
	Comp->Modify();
	Segment->Modify();
	if (auto FieldErr = MCPLspApplySegmentFields(Params, Segment)) { Transaction.Cancel(); return FieldErr; }
	Segment->UpdateSplinePoints();
	MCPLspRequestLayerUpdate(Comp);

	const TSharedPtr<FJsonObject> After = MCPLspSegmentFieldsPayload(Segment);
	bool bChanged = false;
	for (const auto& Pair : After->Values)
	{
		const TSharedPtr<FJsonValue> Old = Before->TryGetField(FString(Pair.Key));
		if (!Old.IsValid() || !FJsonValue::CompareEqual(*Old, *Pair.Value)) { bChanged = true; break; }
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("updated"), bChanged);
	Result->SetBoolField(TEXT("unchanged"), !bChanged);
	Result->SetObjectField(TEXT("previous"), Before);
	Result->SetObjectField(TEXT("segment"), MCPLspDescribeSegment(Segment));
	if (bChanged)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("segment"), Segment->GetPathName());
		MCPLspCopyFields(Before, Payload);
		MCPSetRollback(Result, TEXT("update_landscape_spline_segment"), Payload);
	}
	return MCPResult(Result);
}

// landscape(set_spline_segment_meshes): the meshes a segment deforms along itself.
TSharedPtr<FJsonValue> FLandscapeSplineHandlers::SetSegmentMeshes(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, { TEXT("segment"), TEXT("meshes"), TEXT("collisionProfile"), TEXT("castShadow") });
	REQUIRE_EDITOR_WORLD(World);
	FString Token;
	if (auto Err = RequireString(Params, TEXT("segment"), Token)) return Err;
	TSharedPtr<FJsonValue> Err;
	ULandscapeSplineSegment* Segment = MCPLspFindSegment(World, Token, Err);
	if (!Segment) return Err;
	if (!HasParam(Params, TEXT("meshes")) && !HasParam(Params, TEXT("collisionProfile")) && !HasParam(Params, TEXT("castShadow")))
	{
		return MCPError(TEXT("Pass meshes, collisionProfile or castShadow; nothing to change"));
	}
	ULandscapeSplinesComponent* Comp = Segment->GetOuterSafe();
	const TSharedPtr<FJsonObject> Before = MCPLspMeshFieldsPayload(Segment);

	FScopedTransaction Transaction(NSLOCTEXT("UEMCP", "LandscapeSplineSegmentMeshes", "Set Landscape Spline Segment Meshes"));
	Comp->Modify();
	Segment->Modify();
	bool bMeshesGiven = false;
	if (auto MeshErr = MCPLspApplyMeshFields(Params, Segment, bMeshesGiven)) { Transaction.Cancel(); return MeshErr; }
	// Rebuilds the segment's spline mesh components from the new entries.
	Segment->UpdateSplinePoints();
	Comp->MarkRenderStateDirty();

	const TSharedPtr<FJsonObject> After = MCPLspMeshFieldsPayload(Segment);
	bool bChanged = false;
	for (const auto& Pair : After->Values)
	{
		const TSharedPtr<FJsonValue> Old = Before->TryGetField(FString(Pair.Key));
		if (!Old.IsValid() || !FJsonValue::CompareEqual(*Old, *Pair.Value)) { bChanged = true; break; }
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("updated"), bChanged);
	Result->SetBoolField(TEXT("unchanged"), !bChanged);
	Result->SetObjectField(TEXT("previous"), Before);
	Result->SetObjectField(TEXT("segment"), MCPLspDescribeSegment(Segment));
	if (bChanged)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("segment"), Segment->GetPathName());
		MCPLspCopyFields(Before, Payload);
		MCPSetRollback(Result, TEXT("set_landscape_spline_segment_meshes"), Payload);
	}
	return MCPResult(Result);
}

// landscape(delete_spline_segment): remove one segment; its points stay.
TSharedPtr<FJsonValue> FLandscapeSplineHandlers::DeleteSegment(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, { TEXT("segment") });
	REQUIRE_EDITOR_WORLD(World);
	FString Token;
	if (auto Err = RequireString(Params, TEXT("segment"), Token)) return Err;
	TSharedPtr<FJsonValue> Err;
	ULandscapeSplineSegment* Segment = MCPLspFindSegment(World, Token, Err);
	if (!Segment)
	{
		if (MCPIsAmbiguousActorError(Err)) return Err;
		auto Result = MCPSuccess();
		Result->SetBoolField(TEXT("alreadyDeleted"), true);
		Result->SetStringField(TEXT("segment"), Token);
		return MCPResult(Result);
	}
	ULandscapeSplinesComponent* Comp = Segment->GetOuterSafe();
	const ULandscapeSplineControlPoint* Start = Segment->Connections[0].ControlPoint;
	const ULandscapeSplineControlPoint* End = Segment->Connections[1].ControlPoint;

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("startPoint"), Start ? Start->GetPathName() : FString());
	Payload->SetStringField(TEXT("endPoint"), End ? End->GetPathName() : FString());
	Payload->SetStringField(TEXT("name"), Segment->GetName());
	Payload->SetBoolField(TEXT("autoRotate"), false);
	MCPLspCopyFields(MCPLspSegmentFieldsPayload(Segment), Payload);
	MCPLspCopyFields(MCPLspMeshFieldsPayload(Segment), Payload);
	const FString SegmentName = Segment->GetName();
	const FString SegmentPath = Segment->GetPathName();

	FScopedTransaction Transaction(NSLOCTEXT("UEMCP", "LandscapeSplineDeleteSegment", "Delete Landscape Spline Segment"));
	Comp->Modify();
	MCPLspRemoveSegment(Comp, Segment);
	MCPLspRequestLayerUpdate(Comp);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("deleted"), true);
	Result->SetStringField(TEXT("segment"), SegmentName);
	Result->SetStringField(TEXT("segmentPath"), SegmentPath);
	if (Start && End)
	{
		MCPSetRollback(Result, TEXT("connect_landscape_spline_points"), Payload);
	}
	else
	{
		MCPSetNoRollback(Result, TEXT("The segment had a missing end point, so there is nothing to reconnect."));
	}
	return MCPResult(Result);
}

// landscape(apply_splines): raise, lower and paint the landscape along its splines.
TSharedPtr<FJsonValue> FLandscapeSplineHandlers::ApplySplines(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, { TEXT("actorLabel"), TEXT("actorPath"), TEXT("editLayer"), TEXT("editLayerIndex"), TEXT("updateNow") });
	REQUIRE_EDITOR_WORLD(World);
	const bool bUpdateNow = OptionalBool(Params, TEXT("updateNow"), true);
	TSharedPtr<FJsonValue> Err;
	ALandscape* Landscape = MCPLandscape::ResolveLandscape(World, Params, Err);
	if (!Landscape) return Err;
	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (!Info) return MCPError(FString::Printf(TEXT("Landscape '%s' has no LandscapeInfo; is it registered?"), *Landscape->GetActorLabel()));

	// The spline owners that belong to this landscape, and what they hold.
	TArray<ULandscapeSplinesComponent*> Components;
	MCPLandscapeSplines::CollectSplineComponents(World, Components);
	int32 OwnerCount = 0, PointCount = 0, SegmentCount = 0;
	TArray<FString> Owners;
	for (ULandscapeSplinesComponent* Comp : Components)
	{
		if (MCPLspLandscapeOf(Comp) != Landscape) continue;
		++OwnerCount;
		PointCount += Comp->GetControlPoints().Num();
		SegmentCount += Comp->GetSegments().Num();
		if (Comp->GetOwner()) Owners.Add(Comp->GetOwner()->GetPathName());
	}

	FGuid LayerGuid;
	FString LayerName, LayerError;
	if (!MCPLandscape::ResolveEditLayer(Landscape, Params, LayerGuid, LayerName, LayerError)) return MCPError(LayerError);

	// A landscape with a reserved splines layer takes the deformation there
	// whatever layer is passed, and regenerates it from the splines each time.
	FString SplinesLayerName;
#if UE_MCP_HAS_5_5_API
	for (const ULandscapeEditLayerBase* Layer : Landscape->GetEditLayersConst())
	{
		if (Layer && Layer->GetClass()->GetName() == TEXT("LandscapeEditLayerSplines"))
		{
			SplinesLayerName = Layer->GetName().ToString();
			break;
		}
	}
#endif

	{
		FScopedTransaction Transaction(NSLOCTEXT("UEMCP", "LandscapeApplySplines", "Apply Landscape Splines"));
#if UE_MCP_HAS_5_7_API
		Landscape->UpdateAllLandscapeSplines(LayerGuid, true);
#else
		Landscape->UpdateLandscapeSplines(LayerGuid, false, true);
#endif
	}
	if (bUpdateNow)
	{
		Landscape->ForceUpdateLayersContent();
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Landscape->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Landscape->GetPathName());
	Result->SetNumberField(TEXT("splineOwners"), OwnerCount);
	Result->SetArrayField(TEXT("owners"), MCPStringListToJson(Owners));
	Result->SetNumberField(TEXT("controlPoints"), PointCount);
	Result->SetNumberField(TEXT("segments"), SegmentCount);
	Result->SetStringField(TEXT("editLayer"), SplinesLayerName.IsEmpty() ? LayerName : SplinesLayerName);
	Result->SetBoolField(TEXT("reservedSplinesLayer"), !SplinesLayerName.IsEmpty());
	Result->SetBoolField(TEXT("updatedNow"), bUpdateNow);
	Result->SetBoolField(TEXT("updated"), SegmentCount > 0 || PointCount > 0);
	MCPSetIdempotencyUnobservable(Result,
		TEXT("The deformation lands when the edit layers merge, on the GPU over the following frames; read it back with get_height_at_point or get_layer_weight_region after merge_edit_layers."));
	if (SplinesLayerName.IsEmpty())
	{
		MCPSetNoRollback(Result, FString::Printf(
			TEXT("Without a reserved splines layer the heights and weights are written into edit layer '%s' and stay there when the splines change. Restore them with set_height_region from a get_height_region read taken before applying, or use editor undo."),
			*LayerName));
	}
	else
	{
		MCPSetNoRollback(Result, FString::Printf(
			TEXT("Edit layer '%s' is regenerated from the splines on every apply, so the inverse is editing or deleting the splines and applying again."),
			*SplinesLayerName));
	}
	if (SegmentCount == 0 && PointCount == 0)
	{
		Result->SetStringField(TEXT("note"), TEXT("This landscape has no spline control points or segments, so applying changed nothing."));
	}
	return MCPResult(Result);
}
