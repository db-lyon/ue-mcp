#pragma once

// F5 Property (plans/domain-design.md 3.3): the one property write engine.
// A write goes through the owning object's PreEditChange and a real
// PostEditChangeProperty, reports the value it replaced, and either lands
// whole or leaves the previous value exactly as it was.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UnrealType.h"
#include "HandlerPropertyText.h"
#include "HandlerJsonProperty.h"

namespace MCPPropertyWrite
{
	/** One property to write. Owner is the object the edit belongs to (it is
	 *  modified and notified); Member is Owner's property that holds Leaf, or
	 *  null when Leaf is itself on Owner. */
	struct FTarget
	{
		UObject* Owner = nullptr;
		FProperty* Member = nullptr;
		FProperty* Leaf = nullptr;
		void* Value = nullptr;
	};

	/** What one write did. Previous is the replaced value as export text. */
	struct FOutcome
	{
		bool bWritten = false;
		bool bChanged = false;
		FString Previous;
		FString Error;
	};

	inline FString Export(const FProperty* Prop, const void* Value)
	{
		FString Out;
		if (Prop && Value) Prop->ExportTextItem_Direct(Out, Value, nullptr, nullptr, PPF_None);
		return Out;
	}

	/** Owner's own property called Name, for FTarget::Member. */
	inline FProperty* MemberOf(const UObject* Owner, FName Name)
	{
		return Owner ? Owner->GetClass()->FindPropertyByName(Name) : nullptr;
	}

	inline void NotifyPre(UObject* Owner, FProperty* Member)
	{
		if (Owner) Owner->PreEditChange(Member);
	}

	/** The event the details panel sends: the leaf that changed and the member holding it. */
	inline void NotifyPost(UObject* Owner, FProperty* Member, FProperty* Leaf, EPropertyChangeType::Type Change)
	{
		if (!Owner) return;
		FPropertyChangedEvent Event(Leaf ? Leaf : Member, Change);
		Event.MemberProperty = Member ? Member : Leaf;
		Owner->PostEditChangeProperty(Event);
	}

	/** Write through Apply, which may refuse. A refusal restores the old value and notifies nothing. */
	inline FOutcome Write(const FTarget& Target, TFunctionRef<bool(FString& OutError)> Apply)
	{
		FOutcome Out;
		if (!Target.Leaf || !Target.Value)
		{
			Out.Error = TEXT("There is no property to write.");
			return Out;
		}
		Out.Previous = Export(Target.Leaf, Target.Value);
		FDefaultConstructedPropertyElement Backup(Target.Leaf);
		Target.Leaf->CopyCompleteValue(Backup.GetObjAddress(), Target.Value);

		NotifyPre(Target.Owner, Target.Member ? Target.Member : Target.Leaf);
		if (!Apply(Out.Error))
		{
			Target.Leaf->CopyCompleteValue(Target.Value, Backup.GetObjAddress());
			return Out;
		}
		Out.bWritten = true;
		Out.bChanged = !Target.Leaf->Identical(Backup.GetObjAddress(), Target.Value);
		NotifyPost(Target.Owner, Target.Member, Target.Leaf, EPropertyChangeType::ValueSet);
		return Out;
	}

	/** Import Text into the target, refusing text that does not import whole. */
	inline FOutcome SetText(const FTarget& Target, const FString& Text)
	{
		return Write(Target, [&Target, &Text](FString& OutError)
		{
			FString Error;
			if (MCPPropertyText::ImportTextRaw(Target.Leaf, Target.Value, Text, nullptr, PPF_None, Error)) return true;
			OutError = FString::Printf(TEXT("Could not set '%s' (type %s): %s"),
				*Target.Leaf->GetName(), *Target.Leaf->GetCPPType(), *Error);
			return false;
		});
	}

	/**
	 * Write each key of Values onto the property it names in Struct. Every key
	 * must name a property and every value must land, or every key written so
	 * far goes back. Owner and Member are notified once around the whole map;
	 * pass null when the caller notifies (a node that was just added).
	 */
	inline bool SetJsonMap(UObject* Owner, FProperty* Member, const UStruct* Struct, void* Container,
		const TSharedPtr<FJsonObject>& Values, FString& OutError, const TCHAR* Label = TEXT("instanceProperties"))
	{
		if (!Values.IsValid() || Values->Values.Num() == 0) return true;
		if (!Struct || !Container)
		{
			OutError = FString::Printf(TEXT("%s: this node has no instance data to write"), Label);
			return false;
		}

		struct FWritten
		{
			FProperty* Prop;
			FDefaultConstructedPropertyElement Backup;
		};
		TArray<FWritten> Written;
		auto RestoreAll = [&Written, Container]()
		{
			for (int32 i = Written.Num() - 1; i >= 0; --i)
			{
				Written[i].Prop->CopyCompleteValue(Written[i].Prop->ContainerPtrToValuePtr<void>(Container), Written[i].Backup.GetObjAddress());
			}
		};

		NotifyPre(Owner, Member);
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Values->Values)
		{
			FProperty* Prop = Struct->FindPropertyByName(*Pair.Key);
			if (!Prop)
			{
				RestoreAll();
				OutError = FString::Printf(TEXT("%s: '%s' is not a property of %s"), Label, *Pair.Key, *Struct->GetName());
				return false;
			}
			void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Container);
			const FWritten& Entry = Written.Add_GetRef({ Prop, FDefaultConstructedPropertyElement(Prop) });
			Prop->CopyCompleteValue(Entry.Backup.GetObjAddress(), ValuePtr);
			FString Error;
			if (!MCPJsonProperty::SetJsonOnProperty(Prop, ValuePtr, Pair.Value, Error))
			{
				RestoreAll();
				OutError = FString::Printf(TEXT("%s: '%s' was not written: %s"), Label, *Pair.Key, *Error);
				return false;
			}
		}
		NotifyPost(Owner, Member, nullptr, EPropertyChangeType::ValueSet);
		return true;
	}

	/** The additive key every property writer reports: the value it replaced. */
	inline void NotePrevious(const TSharedPtr<FJsonObject>& Result, const FOutcome& Outcome)
	{
		if (Result.IsValid()) Result->SetStringField(TEXT("previousValue"), Outcome.Previous);
	}
}
