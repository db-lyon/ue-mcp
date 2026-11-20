#pragma once

// A Timeline node's UTimelineTemplate as JSON, so delete_node can remove the
// template as the editor does and still hand import_nodes_t3d everything the
// paste needs to bring the timeline back (#1911).
//
// The template is outered to the generated class, not to the node, so the
// node's exported text carries only its TimelineGuid. UK2Node_Timeline::
// PostPasteNode looks for a live UTimelineTemplate with that guid through
// TObjectIterator and duplicates it into the Blueprint; with none it makes an
// empty timeline. Restore builds that live template from the snapshot, outside
// any Blueprint, for the paste to find.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/TimelineTemplate.h"
#include "Curves/CurveBase.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

namespace MCPTimelineSnapshot
{
	/** FTTTrackBase's reflection, found by path: the timeline structs are not
	 *  exported from the engine module, so StaticStruct() would not link. */
	inline const UScriptStruct* TrackBaseStruct()
	{
		static TWeakObjectPtr<const UScriptStruct> Cached;
		if (!Cached.IsValid()) Cached = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Engine.TTTrackBase"));
		return Cached.Get();
	}

	/** A track array: TArray of a struct deriving from FTTTrackBase. */
	inline const FStructProperty* TrackInner(const FProperty* Prop)
	{
		const FArrayProperty* Array = CastField<FArrayProperty>(Prop);
		const FStructProperty* Inner = Array ? CastField<FStructProperty>(Array->Inner) : nullptr;
		const UScriptStruct* Base = TrackBaseStruct();
		return (Inner && Inner->Struct && Base && Inner->Struct->IsChildOf(Base)) ? Inner : nullptr;
	}

	inline bool IsExternalTrack(const UStruct* TrackStruct, const void* Element)
	{
		const FBoolProperty* Flag = CastField<FBoolProperty>(TrackStruct->FindPropertyByName(TEXT("bIsExternalCurve")));
		return Flag && Flag->GetPropertyValue_InContainer(Element);
	}

	/** The object pointer field a track struct keeps its curve in. */
	inline const FObjectPropertyBase* CurveField(const UStruct* TrackStruct)
	{
		for (TFieldIterator<FProperty> It(TrackStruct); It; ++It)
		{
			if (const FObjectPropertyBase* Obj = CastField<FObjectPropertyBase>(*It))
			{
				if (Obj->PropertyClass && Obj->PropertyClass->IsChildOf(UCurveBase::StaticClass())) return Obj;
			}
		}
		return nullptr;
	}

	/** Export text of every saved field of Struct, leaving out object
	 *  references (curves are carried separately) and track arrays. */
	inline TSharedPtr<FJsonObject> ExportFields(const UStruct* Struct, const void* Container, UObject* Owner)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* Prop = *It;
			if (Prop->HasAnyPropertyFlags(CPF_Transient)) continue;
			if (Prop->GetOwnerClass() == UObject::StaticClass()) continue;
			if (CastField<FObjectPropertyBase>(Prop) || TrackInner(Prop)) continue;
			FString Text;
			const void* Value = Prop->ContainerPtrToValuePtr<void>(Container);
			Prop->ExportText_Direct(Text, Value, Value, Owner, PPF_None);
			Out->SetStringField(Prop->GetName(), Text);
		}
		return Out;
	}

	inline void ImportFields(const UStruct* Struct, void* Container, UObject* Owner, const TSharedPtr<FJsonObject>& Fields)
	{
		if (!Fields.IsValid()) return;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Fields->Values)
		{
			const FProperty* Prop = Struct->FindPropertyByName(FName(*Pair.Key));
			if (!Prop || !Pair.Value.IsValid() || Prop->HasAnyPropertyFlags(CPF_Transient)) continue;
			if (CastField<FObjectPropertyBase>(Prop) || TrackInner(Prop)) continue;
			Prop->ImportText_Direct(*Pair.Value->AsString(), Prop->ContainerPtrToValuePtr<void>(Container), Owner, PPF_None);
		}
	}

	/** The template's settings, every track and every internal curve's keys. */
	inline TSharedPtr<FJsonObject> Capture(UTimelineTemplate* Template)
	{
		if (!Template) return nullptr;
		TSharedPtr<FJsonObject> Snap = MakeShared<FJsonObject>();
		Snap->SetStringField(TEXT("timelineName"), Template->GetVariableName().ToString());
		Snap->SetStringField(TEXT("timelineGuid"), Template->TimelineGuid.ToString());
		Snap->SetObjectField(TEXT("properties"), ExportFields(Template->GetClass(), Template, Template));

		TArray<TSharedPtr<FJsonValue>> Tracks;
		for (TFieldIterator<FProperty> It(Template->GetClass()); It; ++It)
		{
			const FStructProperty* Inner = TrackInner(*It);
			if (!Inner) continue;
			const FArrayProperty* ArrayProp = CastFieldChecked<FArrayProperty>(*It);
			const FObjectPropertyBase* CurveProp = CurveField(Inner->Struct);
			FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Template));
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				uint8* Element = Helper.GetRawPtr(Index);
				TSharedPtr<FJsonObject> Track = MakeShared<FJsonObject>();
				Track->SetStringField(TEXT("array"), ArrayProp->GetName());
				Track->SetObjectField(TEXT("fields"), ExportFields(Inner->Struct, Element, Template));
				const bool bExternal = IsExternalTrack(Inner->Struct, Element);
				Track->SetBoolField(TEXT("external"), bExternal);
				UObject* Curve = CurveProp ? CurveProp->GetObjectPropertyValue_InContainer(Element) : nullptr;
				if (Curve)
				{
					Track->SetStringField(TEXT("curveClass"), Curve->GetClass()->GetPathName());
					if (bExternal) Track->SetStringField(TEXT("curvePath"), Curve->GetPathName());
					else Track->SetObjectField(TEXT("curve"), ExportFields(Curve->GetClass(), Curve, Curve));
				}
				Tracks.Add(MakeShared<FJsonValueObject>(Track));
			}
		}
		Snap->SetArrayField(TEXT("tracks"), Tracks);
		return Snap;
	}

	/** Build the snapshot as a live template outside every Blueprint, carrying
	 *  the snapshot's TimelineGuid, for UK2Node_Timeline::PostPasteNode to find
	 *  and duplicate. Internal curves are outered to it, so the duplicate copies
	 *  them into the Blueprint. Orphaned templates still holding that guid (one a
	 *  delete renamed into the transient package) stop answering to it, so the
	 *  paste cannot pick up a half-collected copy instead. Returns null when
	 *  the snapshot has no valid guid. */
	inline UTimelineTemplate* Restore(const TSharedPtr<FJsonObject>& Snap)
	{
		FString GuidText;
		FGuid Guid;
		if (!Snap.IsValid() || !Snap->TryGetStringField(TEXT("timelineGuid"), GuidText) || !FGuid::Parse(GuidText, Guid)) return nullptr;

		for (TObjectIterator<UTimelineTemplate> It; It; ++It)
		{
			UTimelineTemplate* Other = *It;
			if (Other->TimelineGuid != Guid) continue;
			if (!IsValid(Other) || Other->GetOutermost() == GetTransientPackage()) Other->TimelineGuid = FGuid::NewGuid();
		}

		// No RF_Transient anywhere: DuplicateObject copies object flags, and the
		// duplicate the paste makes has to save with the Blueprint. Living in
		// the transient package already keeps these out of every save. The
		// template and its curves share an outer that is neither a package nor
		// the template: UTimelineTemplate::PostDuplicate copies only curves
		// outered to something other than a package into the Blueprint, and a
		// curve that was an inner of the template would also be duplicated
		// along with it and saved as an unreferenced copy.
		UPackage* Transient = GetTransientPackage();
		UTimelineTemplate* Holder = NewObject<UTimelineTemplate>(Transient,
			MakeUniqueObjectName(Transient, UTimelineTemplate::StaticClass(), TEXT("MCPRestoreHolder")));
		// The template naming rule's "_Template" suffix, written out because
		// UTimelineTemplate::TemplatePostfix is not exported from the engine.
		const FName Name = MakeUniqueObjectName(Holder, UTimelineTemplate::StaticClass(),
			*FString::Printf(TEXT("MCPRestore_%s_Template"), *Guid.ToString()));
		UTimelineTemplate* Template = NewObject<UTimelineTemplate>(Holder, Name);

		const TSharedPtr<FJsonObject>* Props = nullptr;
		if (Snap->TryGetObjectField(TEXT("properties"), Props)) ImportFields(Template->GetClass(), Template, Template, *Props);
		Template->TimelineGuid = Guid;

		const TArray<TSharedPtr<FJsonValue>>* Tracks = nullptr;
		Snap->TryGetArrayField(TEXT("tracks"), Tracks);
		if (Tracks)
		{
			for (const TSharedPtr<FJsonValue>& TrackValue : *Tracks)
			{
				const TSharedPtr<FJsonObject> Track = TrackValue.IsValid() ? TrackValue->AsObject() : nullptr;
				if (!Track.IsValid()) continue;
				const FArrayProperty* ArrayProp = CastField<FArrayProperty>(
					Template->GetClass()->FindPropertyByName(FName(*Track->GetStringField(TEXT("array")))));
				const FStructProperty* Inner = TrackInner(ArrayProp);
				if (!Inner) continue;
				FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Template));
				uint8* Element = Helper.GetRawPtr(Helper.AddValue());
				const TSharedPtr<FJsonObject>* Fields = nullptr;
				if (Track->TryGetObjectField(TEXT("fields"), Fields)) ImportFields(Inner->Struct, Element, Template, *Fields);

				const FObjectPropertyBase* CurveProp = CurveField(Inner->Struct);
				if (!CurveProp) continue;
				UObject* Curve = nullptr;
				FString CurvePath;
				if (Track->TryGetStringField(TEXT("curvePath"), CurvePath) && !CurvePath.IsEmpty())
				{
					Curve = LoadObject<UCurveBase>(nullptr, *CurvePath);
				}
				else
				{
					UClass* CurveClass = LoadObject<UClass>(nullptr, *Track->GetStringField(TEXT("curveClass")));
					if (CurveClass && CurveClass->IsChildOf(CurveProp->PropertyClass))
					{
						Curve = NewObject<UCurveBase>(Holder, CurveClass, NAME_None, RF_Public);
						const TSharedPtr<FJsonObject>* CurveFields = nullptr;
						if (Track->TryGetObjectField(TEXT("curve"), CurveFields)) ImportFields(CurveClass, Curve, Curve, *CurveFields);
					}
				}
				if (Curve) CurveProp->SetObjectPropertyValue_InContainer(Element, Curve);
			}
		}
		return Template;
	}

	/** Retire a template Restore made, once the paste has copied it. */
	inline void Discard(UTimelineTemplate* Template)
	{
		if (!Template) return;
		Template->TimelineGuid = FGuid::NewGuid();
		UObject* Holder = Template->GetOuter();
		Template->MarkAsGarbage();
		if (Holder && Holder != GetTransientPackage()) Holder->MarkAsGarbage();
	}
}
