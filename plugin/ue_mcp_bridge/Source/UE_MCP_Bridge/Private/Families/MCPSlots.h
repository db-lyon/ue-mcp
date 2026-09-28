#pragma once

// F3 Slots (plans/domain-design.md 3.3): add, remove, move and address the
// ordered children an owning object keeps in one array property. The list
// owns the index checks and the owner's PreEditChange / PostEditChangeProperty;
// what an element is, and what its handler reports, stays with the type's
// descriptor. Run it inside an FMCPEditTransaction.

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "HandlerResult.h"
#include "Families/MCPPropertyWrite.h"

template <typename TElem>
class TMCPSlotList
{
public:
	/** Items is Owner's array property called Member. Noun and IndexParam word the errors. */
	TMCPSlotList(UObject* InOwner, FName InMember, TArray<TElem>& InItems, const TCHAR* InNoun, const TCHAR* InIndexParam)
		: Owner(InOwner)
		, Member(MCPPropertyWrite::MemberOf(InOwner, InMember))
		, Items(InItems)
		, Noun(InNoun)
		, IndexParam(InIndexParam)
	{
	}

	int32 Num() const { return Items.Num(); }
	TElem& operator[](int32 Index) { return Items[Index]; }
	const TElem& operator[](int32 Index) const { return Items[Index]; }
	FProperty* MemberProperty() const { return Member; }

	/** Word an empty list's refusal after what holds it: "the state has no tasks". */
	TMCPSlotList& OwnedBy(const TCHAR* InOwnerNoun)
	{
		OwnerNoun = InOwnerNoun;
		return *this;
	}

	/** Say which list an index refusal is about when the owner holds several: " on transition 0". */
	TMCPSlotList& At(const FString& InWhere)
	{
		Where = InWhere;
		return *this;
	}

	/** The refusal for an index this list does not have, or nullptr. */
	TSharedPtr<FJsonValue> CheckIndex(int32 Index) const
	{
		if (Items.IsValidIndex(Index)) return nullptr;
		const FString Valid = Items.Num() > 0
			? FString::Printf(TEXT("0 to %d"), Items.Num() - 1)
			: OwnerNoun
				? FString::Printf(TEXT("none, the %s has no %ss"), OwnerNoun, Noun)
				: FString::Printf(TEXT("none, there is no %s"), Noun);
		return MCPError(FString::Printf(TEXT("Invalid %s %d%s. Valid values: %s."), IndexParam, Index, *Where, *Valid));
	}

	int32 IndexOf(TFunctionRef<bool(const TElem&)> Match) const
	{
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			if (Match(Items[i])) return i;
		}
		return INDEX_NONE;
	}

	/**
	 * Append an element through Make (which may append it itself), then let
	 * Init fill it. A refusal from Init removes the element again, so a failed
	 * add leaves the list as it was. Returns the element, or null with OutError.
	 */
	TElem* Add(TFunctionRef<TElem&(TArray<TElem>&)> Make, TFunctionRef<bool(TElem&, FString&)> Init, FString& OutError)
	{
		MCPPropertyWrite::NotifyPre(Owner, Member);
		const int32 Before = Items.Num();
		TElem& Added = Make(Items);
		if (!Init(Added, OutError))
		{
			Items.RemoveAt(Before, Items.Num() - Before);
			return nullptr;
		}
		MCPPropertyWrite::NotifyPost(Owner, Member, nullptr, EPropertyChangeType::ArrayAdd);
		return &Items.Last();
	}

	TElem* Add(TFunctionRef<bool(TElem&, FString&)> Init, FString& OutError)
	{
		return Add([](TArray<TElem>& List) -> TElem& { return List.AddDefaulted_GetRef(); }, Init, OutError);
	}

	/** Remove the element at Index (checked by the caller) and return it. */
	TElem Remove(int32 Index)
	{
		MCPPropertyWrite::NotifyPre(Owner, Member);
		TElem Removed = MoveTemp(Items[Index]);
		Items.RemoveAt(Index);
		MCPPropertyWrite::NotifyPost(Owner, Member, nullptr, EPropertyChangeType::ArrayRemove);
		return Removed;
	}

	/** Move the element at From to To, both positions in the final list. */
	void Move(int32 From, int32 To)
	{
		if (From == To) return;
		MCPPropertyWrite::NotifyPre(Owner, Member);
		TElem Moved = MoveTemp(Items[From]);
		Items.RemoveAt(From);
		Items.Insert(MoveTemp(Moved), FMath::Clamp(To, 0, Items.Num()));
		MCPPropertyWrite::NotifyPost(Owner, Member, nullptr, EPropertyChangeType::ArrayMove);
	}

	/** Empty the list. Returns how many elements it held. */
	int32 Clear()
	{
		const int32 Held = Items.Num();
		if (Held == 0) return 0;
		MCPPropertyWrite::NotifyPre(Owner, Member);
		Items.Empty();
		MCPPropertyWrite::NotifyPost(Owner, Member, nullptr, EPropertyChangeType::ArrayClear);
		return Held;
	}

private:
	UObject* Owner;
	FProperty* Member;
	TArray<TElem>& Items;
	const TCHAR* Noun;
	const TCHAR* IndexParam;
	const TCHAR* OwnerNoun = nullptr;
	FString Where;
};
