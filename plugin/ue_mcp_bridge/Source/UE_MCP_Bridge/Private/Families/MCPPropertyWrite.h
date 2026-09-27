#pragma once

// F5 Property (plans/domain-design.md 3.3): the one property write engine.
// A write goes through the owning object's PreEditChange and a real
// PostEditChangeChainProperty (which reaches PostEditChangeProperty too),
// reports the value it replaced, and either lands whole or leaves the
// previous value exactly as it was.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UnrealType.h"
#include "HandlerPropertyText.h"
#include "HandlerJsonProperty.h"

namespace MCPPropertyWrite
{
	/** One step of a property path: the property, and the element when it is an array. */
	struct FLink
	{
		FProperty* Property = nullptr;
		int32 Index = INDEX_NONE;
	};

	/** One property to write. Owner is the object the edit belongs to (it is
	 *  modified and notified); Member is Owner's property that holds Leaf, or
	 *  null when Leaf is itself on Owner. Chain, when given, is the whole path
	 *  from Member down to Leaf with array indices, which is what an owner that
	 *  reacts through PostEditChangeChainProperty reads. */
	struct FTarget
	{
		UObject* Owner = nullptr;
		FProperty* Member = nullptr;
		FProperty* Leaf = nullptr;
		void* Value = nullptr;
		TArray<FLink> Chain;
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

	/**
	 * The chain for a dotted path under Struct, such as "Tasks[2].Node":
	 * property names (any case) with an optional [index] on an array step.
	 * Empty when a step does not resolve.
	 */
	inline TArray<FLink> ChainOf(const UStruct* Struct, const FString& Path)
	{
		TArray<FLink> Out;
		TArray<FString> Parts;
		Path.ParseIntoArray(Parts, TEXT("."));
		const UStruct* Current = Struct;
		for (FString Part : Parts)
		{
			int32 Index = INDEX_NONE;
			int32 Open = INDEX_NONE;
			if (Part.FindChar(TCHAR('['), Open))
			{
				Index = FCString::Atoi(*Part.Mid(Open + 1));
				Part.LeftInline(Open);
			}
			FProperty* Prop = Current ? FindFProperty<FProperty>(Current, *Part) : nullptr;
			if (!Prop) return TArray<FLink>();
			Out.Add({ Prop, Index });
			FProperty* Element = Prop;
			if (const FArrayProperty* Array = CastField<FArrayProperty>(Prop)) Element = Array->Inner;
			const FStructProperty* AsStruct = CastField<FStructProperty>(Element);
			Current = AsStruct ? AsStruct->Struct : nullptr;
		}
		return Out;
	}

	/** The path a notification carries: Chain, else Member then Leaf. */
	inline TArray<FLink> PathOf(FProperty* Member, FProperty* Leaf, const TArray<FLink>& Chain)
	{
		if (Chain.Num() > 0) return Chain;
		TArray<FLink> Out;
		if (Member) Out.Add({ Member, INDEX_NONE });
		if (Leaf && Leaf != Member) Out.Add({ Leaf, INDEX_NONE });
		return Out;
	}

	inline void BuildChain(FEditPropertyChain& Out, const TArray<FLink>& Path)
	{
		for (const FLink& Link : Path) Out.AddTail(Link.Property);
		Out.SetActiveMemberPropertyNode(Path[0].Property);
		Out.SetActivePropertyNode(Path.Last().Property);
	}

	inline void NotifyPre(UObject* Owner, const TArray<FLink>& Path)
	{
		if (!Owner) return;
		if (Path.Num() == 0)
		{
			Owner->PreEditChange(nullptr);
			return;
		}
		FEditPropertyChain Chain;
		BuildChain(Chain, Path);
		Owner->PreEditChange(Chain);
	}

	/** The chain event the details panel sends: the member, the path to the leaf and each array index on it. */
	inline void NotifyPost(UObject* Owner, const TArray<FLink>& Path, EPropertyChangeType::Type Change)
	{
		if (!Owner) return;
		if (Path.Num() == 0)
		{
			FPropertyChangedEvent Event(nullptr, Change);
			Owner->PostEditChangeProperty(Event);
			return;
		}
		FEditPropertyChain Chain;
		BuildChain(Chain, Path);
		FPropertyChangedEvent Event(Path.Last().Property, Change);
		Event.MemberProperty = Path[0].Property;
		TArray<TMap<FString, int32>> Indices;
		TMap<FString, int32>& Map = Indices.AddDefaulted_GetRef();
		for (const FLink& Link : Path)
		{
			if (Link.Index != INDEX_NONE) Map.Add(Link.Property->GetName(), Link.Index);
		}
		Event.ObjectIteratorIndex = 0;
		Event.SetArrayIndexPerObject(Indices);
		FPropertyChangedChainEvent ChainEvent(Chain, Event);
		Owner->PostEditChangeChainProperty(ChainEvent);
	}

	inline void NotifyPre(UObject* Owner, FProperty* Member)
	{
		NotifyPre(Owner, PathOf(Member, nullptr, {}));
	}

	inline void NotifyPost(UObject* Owner, FProperty* Member, FProperty* Leaf, EPropertyChangeType::Type Change)
	{
		NotifyPost(Owner, PathOf(Member, Leaf, {}), Change);
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

		const TArray<FLink> Path = PathOf(Target.Member, Target.Leaf, Target.Chain);
		NotifyPre(Target.Owner, Path);
		if (!Apply(Out.Error))
		{
			Target.Leaf->CopyCompleteValue(Target.Value, Backup.GetObjAddress());
			return Out;
		}
		Out.bWritten = true;
		Out.bChanged = !Target.Leaf->Identical(Backup.GetObjAddress(), Target.Value);
		NotifyPost(Target.Owner, Path, EPropertyChangeType::ValueSet);
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
		for (const auto& Pair : Values->Values)
		{
			const FString Key(Pair.Key);
			FProperty* Prop = Struct->FindPropertyByName(*Key);
			if (!Prop)
			{
				RestoreAll();
				OutError = FString::Printf(TEXT("%s: '%s' is not a property of %s"), Label, *Key, *Struct->GetName());
				return false;
			}
			void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Container);
			const FWritten& Entry = Written.Add_GetRef({ Prop, FDefaultConstructedPropertyElement(Prop) });
			Prop->CopyCompleteValue(Entry.Backup.GetObjAddress(), ValuePtr);
			FString Error;
			if (!MCPJsonProperty::SetJsonOnProperty(Prop, ValuePtr, Pair.Value, Error))
			{
				RestoreAll();
				OutError = FString::Printf(TEXT("%s: '%s' was not written: %s"), Label, *Key, *Error);
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
